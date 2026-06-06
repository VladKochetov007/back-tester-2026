// The "basement": the real historical book for one instrument, reconstructed
// from L3 (market-by-order) replay. Shared and read-only to every trading
// engine. A single replay thread advances it via apply(); engines only read,
// so any number of EngineViews can observe one HistoricalLOB without a lock as
// long as reads happen between apply() calls (the simulator enforces this).

#pragma once

#include "common/MarketDataEvent.hpp"
#include "lob/LimitOrderBook.hpp"
#include "lob/OrderIndex.hpp"
#include "sim/SimTypes.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace cmf::sim {

struct TradePrint {
  ScaledPrice price = PRICE_NONE;
  Qty qty = 0;
  Side aggressor = Side::None;
  NanoTime ts_event = 0;
  Seq sequence = 0;
};

class HistoricalLOB {
public:
  using AggQty = LimitOrderBook::AggQty;

  explicit HistoricalLOB(InstrumentId id = 0) noexcept : instrument_id_(id) {}

  InstrumentId instrument_id() const noexcept { return instrument_id_; }

  // Applies one event to the basement. Returns true when the top of book moved.
  bool apply(const MarketDataEvent &e);

  bool best(Side side, ScaledPrice &px, AggQty &qty) const noexcept {
    return side == Side::Buy    ? book_.best_bid_scaled(px, qty)
           : side == Side::Sell ? book_.best_ask_scaled(px, qty)
                                : false;
  }

  std::optional<ScaledPrice> best_price(Side side) const noexcept {
    ScaledPrice px;
    AggQty q;
    return best(side, px, q) ? std::optional<ScaledPrice>(px) : std::nullopt;
  }

  AggQty qty_at(Side side, ScaledPrice px) const noexcept {
    return book_.volume_at(side_to_char(side), px);
  }

  template <class Fn> void for_each_level(Side side, Fn &&fn) const {
    if (side == Side::Buy)
      book_.for_each_bid(std::forward<Fn>(fn));
    else if (side == Side::Sell)
      book_.for_each_ask(std::forward<Fn>(fn));
  }

  // Best-first, stops when `fn` returns false.
  template <class Fn> void for_each_level_until(Side side, Fn &&fn) const {
    if (side == Side::Buy)
      book_.for_each_bid_until(std::forward<Fn>(fn));
    else if (side == Side::Sell)
      book_.for_each_ask_until(std::forward<Fn>(fn));
  }

  bool empty() const noexcept { return book_.empty(); }
  std::size_t levels(Side side) const noexcept {
    return side == Side::Buy ? book_.bid_levels() : book_.ask_levels();
  }

  const std::optional<TradePrint> &last_trade() const noexcept {
    return last_trade_;
  }
  std::uint64_t version() const noexcept { return version_; }
  NanoTime last_ts() const noexcept { return last_ts_; }
  const LimitOrderBook &book() const noexcept { return book_; }

private:
  struct Bbo {
    ScaledPrice bid = PRICE_NONE, ask = PRICE_NONE;
    AggQty bid_qty = 0, ask_qty = 0;
    bool operator==(const Bbo &) const noexcept = default;
  };
  Bbo current_bbo() const noexcept;

  InstrumentId instrument_id_;
  LimitOrderBook book_{};
  OrderIndex index_{1u << 12};
  std::optional<TradePrint> last_trade_;
  Bbo last_bbo_{};
  std::uint64_t version_ = 0;
  NanoTime last_ts_ = 0;
};

} // namespace cmf::sim
