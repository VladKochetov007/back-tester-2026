// Canonical L3 (market-by-order) transition applied to an aggregated book plus
// its per-order index. Extracted so both the live Dispatcher and the
// simulation's HistoricalLOB reconstruct the book identically. Trade ('T')
// events are observations, not book mutations, and are handled by the caller.

#pragma once

#include "common/MarketDataEvent.hpp"
#include "lob/LimitOrderBook.hpp"
#include "lob/OrderIndex.hpp"

namespace cmf {

// Resolves an event's order against the index, falling back to the event's own
// fields when the order is unknown (mirrors Dispatcher::resolve). Returns false
// for orphans that carry no instrument.
inline bool resolve_order(const OrderIndex &index, const MarketDataEvent &e,
                          OrderRecord &out) noexcept {
  if (e.order_id != 0 && index.find(e.order_id, out))
    return true;
  if (e.instrument_id != 0) {
    out = OrderRecord{e.instrument_id, e.side, LimitOrderBook::scale(e.price),
                      e.size};
    return true;
  }
  return false;
}

// Applies a single add/cancel/modify/fill/clear event to `book` and `index`.
// Returns true when the event resolved and mutated the book.
inline bool apply_order_event(LimitOrderBook &book, OrderIndex &index,
                              const MarketDataEvent &e) noexcept {
  switch (e.action) {
  case 'A': {
    if (e.instrument_id == 0)
      return false;
    const auto px = LimitOrderBook::scale(e.price);
    if (e.order_id != 0)
      index.insert(e.order_id,
                   OrderRecord{e.instrument_id, e.side, px, e.size});
    book.apply_add(e.side, px, e.size);
    return true;
  }
  case 'C': {
    OrderRecord rec;
    if (!resolve_order(index, e, rec))
      return false;
    const uint64_t cancel_qty = e.size != 0 ? e.size : rec.remaining_qty;
    book.apply_cancel(rec.side, rec.scaled_price, cancel_qty);
    if (cancel_qty >= rec.remaining_qty)
      index.erase(e.order_id);
    else
      index.update_qty(e.order_id, rec.remaining_qty - cancel_qty);
    return true;
  }
  case 'M': {
    OrderRecord rec;
    if (!resolve_order(index, e, rec))
      return false;
    book.apply_cancel(rec.side, rec.scaled_price, rec.remaining_qty);
    const auto new_px = LimitOrderBook::scale(e.price);
    const char new_side = e.side != 'N' ? e.side : rec.side;
    book.apply_add(new_side, new_px, e.size);
    if (e.order_id != 0)
      index.insert(e.order_id,
                   OrderRecord{rec.instrument_id, new_side, new_px, e.size});
    return true;
  }
  case 'F': {
    OrderRecord rec;
    if (!resolve_order(index, e, rec))
      return false;
    const uint64_t fill_qty = e.size;
    book.apply_fill(rec.side, rec.scaled_price, fill_qty);
    if (fill_qty >= rec.remaining_qty)
      index.erase(e.order_id);
    else
      index.update_qty(e.order_id, rec.remaining_qty - fill_qty);
    return true;
  }
  case 'R': {
    if (e.instrument_id == 0)
      return false;
    book.clear();
    return true;
  }
  default:
    return false;
  }
}

} // namespace cmf
