// The book a single engine sees: the shared HistoricalLOB basement, minus the
// liquidity this engine already consumed, plus the engine's own resting orders.
// It is a thin read-only view that composes a basement reference and an
// EngineView reference and merges them lazily; it owns no level storage of the
// full book, which answers the HW3 design question - store diffs, not N copies.

#pragma once

#include "sim/EngineView.hpp"
#include "sim/HistoricalLOB.hpp"
#include "sim/SimTypes.hpp"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace cmf::sim {

class SimulatedLOB {
public:
  using Level = std::pair<ScaledPrice, Qty>;

  SimulatedLOB(const HistoricalLOB &basement, const EngineView &view) noexcept
      : basement_(&basement), view_(&view) {}

  InstrumentId instrument_id() const noexcept {
    return basement_->instrument_id();
  }
  const HistoricalLOB &basement() const noexcept { return *basement_; }
  const EngineView &view() const noexcept { return *view_; }

  Qty qty_at(Side side, ScaledPrice price) const noexcept {
    const Qty hist = static_cast<Qty>(basement_->qty_at(side, price));
    return view_->net_basement_qty(side, price, hist) +
           view_->own_qty(side, price);
  }

  std::optional<Level> best(Side side) const {
    const std::optional<ScaledPrice> hp = visible_basement_best(side);
    const std::optional<PriceLadder::Level> op = view_->own_best(side);
    if (!hp && !op)
      return std::nullopt;
    ScaledPrice price =
        !op   ? *hp
        : !hp ? op->first
              : (price_is_better(side, *hp, op->first) ? *hp : op->first);
    return Level{price, qty_at(side, price)};
  }

  std::optional<ScaledPrice> best_price(Side side) const {
    const auto b = best(side);
    return b ? std::optional<ScaledPrice>(b->first) : std::nullopt;
  }

  std::optional<ScaledPrice> mid() const {
    const auto b = best_price(Side::Buy);
    const auto a = best_price(Side::Sell);
    if (!b || !a)
      return std::nullopt;
    return (*b + *a) / 2;
  }

  // Merged depth, best-first, into `out`, capped at maxN levels.
  std::size_t snapshot(Side side, std::vector<Level> &out,
                       std::size_t maxN) const;

private:
  // Best basement price on `side` whose depth survives this engine's
  // consumption.
  std::optional<ScaledPrice> visible_basement_best(Side side) const {
    std::optional<ScaledPrice> found;
    basement_->for_each_level_until(
        side, [&](ScaledPrice p, LimitOrderBook::AggQty q) {
          if (view_->net_basement_qty(side, p, static_cast<Qty>(q)) > 0) {
            found = p;
            return false;
          }
          return true;
        });
    return found;
  }

  const HistoricalLOB *basement_;
  const EngineView *view_;
};

} // namespace cmf::sim
