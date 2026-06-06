#pragma once

#include "common/MarketDataEvent.hpp"

#include <cstdint>

namespace cmf::test {

inline MarketDataEvent ev(char action, char side, double price,
                          std::uint32_t size, std::uint64_t order_id,
                          std::uint32_t inst, std::int64_t ts,
                          std::uint32_t seq = 0) {
  MarketDataEvent e;
  e.ts_recv = ts;
  e.ts_event = ts;
  e.order_id = order_id;
  e.price = price;
  e.instrument_id = inst;
  e.sequence = seq;
  e.size = size;
  e.action = action;
  e.side = side;
  return e;
}

inline MarketDataEvent add(char side, double px, std::uint32_t sz,
                           std::uint64_t oid, std::uint32_t inst,
                           std::int64_t ts) {
  return ev('A', side, px, sz, oid, inst, ts);
}
inline MarketDataEvent cancel(char side, double px, std::uint32_t sz,
                              std::uint64_t oid, std::uint32_t inst,
                              std::int64_t ts) {
  return ev('C', side, px, sz, oid, inst, ts);
}
inline MarketDataEvent trade(char side, double px, std::uint32_t sz,
                             std::uint32_t inst, std::int64_t ts) {
  return ev('T', side, px, sz, 0, inst, ts);
}

} // namespace cmf::test
