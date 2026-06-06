#include "sim/HistoricalLOB.hpp"

#include "lob/BookApplier.hpp"

namespace cmf::sim {

HistoricalLOB::Bbo HistoricalLOB::current_bbo() const noexcept {
  Bbo b;
  book_.best_bid_scaled(b.bid, b.bid_qty);
  book_.best_ask_scaled(b.ask, b.ask_qty);
  return b;
}

bool HistoricalLOB::apply(const MarketDataEvent &e) {
  last_ts_ = e.ts_recv;

  if (e.action == 'T') {
    last_trade_ =
        TradePrint{LimitOrderBook::scale(e.price), static_cast<Qty>(e.size),
                   side_from_char(e.side), e.ts_event, e.sequence};
    return false;
  }

  apply_order_event(book_, index_, e);

  const Bbo now = current_bbo();
  if (now == last_bbo_)
    return false;
  last_bbo_ = now;
  ++version_;
  return true;
}

} // namespace cmf::sim
