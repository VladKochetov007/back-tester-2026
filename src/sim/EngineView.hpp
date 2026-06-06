// Per-engine overlay over one instrument's HistoricalLOB. It tracks two private
// things the basement cannot know about:
//   * the engine's own resting orders (its injected liquidity), and
//   * how much historical liquidity this engine has already consumed, so the
//     same shared basement order is never handed to it twice.
// Only the owning engine mutates its EngineView, so N engines over one shared
// basement never touch the same memory.

#pragma once

#include "sim/PriceLadder.hpp"
#include "sim/SimTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <unordered_map>

namespace cmf::sim {

class EngineView {
public:
  struct RestingOrder {
    Side side = Side::None;
    ScaledPrice price = PRICE_NONE;
    Qty remaining = 0;
  };

  explicit EngineView(InstrumentId id = 0) noexcept : instrument_id_(id) {}
  InstrumentId instrument_id() const noexcept { return instrument_id_; }

  void add_resting(OrderId id, Side side, ScaledPrice price, Qty qty) {
    if (qty <= 0)
      return;
    own_[id] = RestingOrder{side, price, qty};
    ladder(side).add(price, qty);
  }

  // Reduces a resting order by `qty`; returns the remaining size (0 if gone).
  Qty reduce_resting(OrderId id, Qty qty) {
    auto it = own_.find(id);
    if (it == own_.end())
      return 0;
    RestingOrder &r = it->second;
    const Qty take = std::min(qty, r.remaining);
    r.remaining -= take;
    ladder(r.side).add(r.price, -take);
    if (r.remaining == 0) {
      own_.erase(it);
      return 0;
    }
    return r.remaining;
  }

  bool cancel_resting(OrderId id) {
    auto it = own_.find(id);
    if (it == own_.end())
      return false;
    ladder(it->second.side).add(it->second.price, -it->second.remaining);
    own_.erase(it);
    return true;
  }

  const RestingOrder *find_resting(OrderId id) const noexcept {
    auto it = own_.find(id);
    return it == own_.end() ? nullptr : &it->second;
  }

  void consume_basement(Side side, ScaledPrice price, Qty qty) {
    if (qty > 0)
      consumed(side).add(price, qty);
  }

  Qty consumed_qty(Side side, ScaledPrice price) const noexcept {
    return consumed_const(side).get(price);
  }

  // Historical depth at (side, price) still visible to THIS engine.
  Qty net_basement_qty(Side side, ScaledPrice price,
                       Qty hist_qty) const noexcept {
    const Qty net = hist_qty - consumed_const(side).get(price);
    return net > 0 ? net : 0;
  }

  Qty own_qty(Side side, ScaledPrice price) const noexcept {
    return ladder_const(side).get(price);
  }
  std::optional<PriceLadder::Level> own_best(Side side) const noexcept {
    return ladder_const(side).best(side);
  }
  std::span<const PriceLadder::Level> own_levels(Side side) const noexcept {
    return ladder_const(side).ascending();
  }
  std::size_t resting_count() const noexcept { return own_.size(); }

  template <class Fn> void for_each_resting(Fn &&fn) const {
    for (const auto &[id, r] : own_)
      fn(id, r);
  }

private:
  PriceLadder &ladder(Side s) noexcept { return own_ladder_[side_index(s)]; }
  const PriceLadder &ladder_const(Side s) const noexcept {
    return own_ladder_[side_index(s)];
  }
  PriceLadder &consumed(Side s) noexcept { return consumed_[side_index(s)]; }
  const PriceLadder &consumed_const(Side s) const noexcept {
    return consumed_[side_index(s)];
  }

  InstrumentId instrument_id_;
  std::unordered_map<OrderId, RestingOrder> own_;
  PriceLadder own_ladder_[2]; // [0] = Buy, [1] = Sell
  PriceLadder consumed_[2];
};

} // namespace cmf::sim
