// Shared interface types for the multi-engine simulation layer (HW3).
// Realises the HW3 "interface document": instrument_id, timestamp_ns,
// scaled-integer price, order_id and a trading-engine id.

#pragma once

#include "common/BasicTypes.hpp"
#include "lob/LimitOrderBook.hpp"

#include <cstdint>
#include <limits>

namespace cmf::sim {

using InstrumentId =
    std::uint32_t; // matches MarketDataEvent::instrument_id (HW1 field)
using EngineId =
    std::uint32_t; // identifies a trading engine; many run simultaneously
using Seq = std::uint32_t; // market-data sequence number
using ScaledPrice =
    LimitOrderBook::ScaledPrice; // price as scaled integer (price * SCALE)
using Qty = std::int64_t;        // signed quantity for overlay arithmetic

inline constexpr ScaledPrice PRICE_NONE =
    std::numeric_limits<ScaledPrice>::min();

inline ScaledPrice scale_price(double px) noexcept {
  return LimitOrderBook::scale(px);
}
inline double unscale_price(ScaledPrice p) noexcept {
  return LimitOrderBook::unscale(p);
}

inline Side side_from_char(char c) noexcept {
  return c == 'B' ? Side::Buy : c == 'A' ? Side::Sell : Side::None;
}

inline char side_to_char(Side s) noexcept {
  return s == Side::Buy ? 'B' : s == Side::Sell ? 'A' : 'N';
}

inline Side opposite(Side s) noexcept {
  return static_cast<Side>(-static_cast<int>(s));
}

// +1 for Buy, -1 for Sell, 0 for None. Convenient as a position/PnL multiplier.
inline int side_sign(Side s) noexcept { return static_cast<int>(s); }

// Dense index for per-side arrays: 0 = Buy, 1 = Sell.
inline int side_index(Side s) noexcept { return s == Side::Buy ? 0 : 1; }

// True if `a` is a more aggressive price than `b` for a resting order on `side`
// (higher is better for bids, lower is better for asks).
inline bool price_is_better(Side side, ScaledPrice a, ScaledPrice b) noexcept {
  return side == Side::Buy ? a > b : a < b;
}

} // namespace cmf::sim
