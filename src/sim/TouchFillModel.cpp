#include "sim/TouchFillModel.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace cmf::sim {

namespace {

bool marketable(Side taker_side, ScaledPrice level_price,
                ScaledPrice limit_price) noexcept {
  return taker_side == Side::Buy ? level_price <= limit_price
                                 : level_price >= limit_price;
}

} // namespace

FillOutcome TouchFillModel::on_submit(const SimulatedLOB &book, EngineView &ev,
                                      OrderId order_id, const NewOrder &order) {
  FillOutcome out;
  if (order.side == Side::None || order.size <= 0) {
    out.rejected = true;
    out.reason = RejectReason::NoLiquidity;
    return out;
  }

  const Side opp = opposite(order.side);
  const bool is_market = order.type == OrderType::Market;
  Qty want = order.size;

  book.basement().for_each_level_until(
      opp, [&](ScaledPrice px, LimitOrderBook::AggQty hist_qty) {
        // Levels arrive best-first; for a limit order the first non-marketable
        // level means every deeper one is worse too, so stop.
        if (!is_market && !marketable(order.side, px, order.price))
          return false;
        const Qty avail =
            ev.net_basement_qty(opp, px, static_cast<Qty>(hist_qty));
        if (avail > 0) {
          const Qty take = std::min(want, avail);
          out.fills.push_back(
              RawFill{order_id, order.side, px, take, /*maker=*/false});
          ev.consume_basement(opp, px, take);
          want -= take;
        }
        return want > 0;
      });

  if (want > 0) {
    if (is_market) {
      if (out.fills.empty()) {
        out.rejected = true;
        out.reason = RejectReason::NoLiquidity;
      }
    } else {
      ev.add_resting(order_id, order.side, order.price, want);
      out.resting = want;
    }
  }
  return out;
}

void TouchFillModel::on_market_event(const SimulatedLOB &book, EngineView &ev,
                                     const MarketDataEvent & /*event*/,
                                     std::vector<RawFill> &out) {
  if (ev.resting_count() == 0)
    return;

  // Collect first, then mutate - reduce_resting()/consume_basement() would
  // otherwise invalidate the iteration over resting orders.
  std::vector<RawFill> matched;
  ev.for_each_resting([&](OrderId id, const EngineView::RestingOrder &r) {
    const Side opp = opposite(r.side);
    ScaledPrice best_px;
    LimitOrderBook::AggQty best_qty;
    if (!book.basement().best(opp, best_px, best_qty))
      return;

    const bool crossed =
        r.side == Side::Buy ? best_px <= r.price : best_px >= r.price;
    if (!crossed)
      return;

    const Qty avail =
        ev.net_basement_qty(opp, best_px, static_cast<Qty>(best_qty));
    const Qty take = std::min(r.remaining, avail);
    if (take <= 0)
      return;
    matched.push_back(RawFill{id, r.side, best_px, take, /*maker=*/true});
  });

  for (const RawFill &f : matched) {
    ev.consume_basement(opposite(f.side), f.price, f.size);
    ev.reduce_resting(f.order_id, f.size);
    out.push_back(f);
  }
}

} // namespace cmf::sim
