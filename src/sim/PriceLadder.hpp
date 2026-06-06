// A sparse price -> signed-quantity ladder kept sorted ascending by price.
// Used by EngineView to track an engine's own resting depth and the historical
// liquidity it has already consumed. Linear in the number of distinct levels,
// which for a single engine's footprint is small.

#pragma once

#include "sim/SimTypes.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cmf::sim {

class PriceLadder {
public:
  using Level = std::pair<ScaledPrice, Qty>;

  void add(ScaledPrice price, Qty delta) {
    if (delta == 0)
      return;
    auto it = lower(price);
    if (it != v_.end() && it->first == price) {
      it->second += delta;
      if (it->second == 0)
        v_.erase(it);
    } else {
      v_.insert(it, Level{price, delta});
    }
  }

  void set(ScaledPrice price, Qty value) {
    auto it = lower(price);
    if (it != v_.end() && it->first == price) {
      if (value == 0)
        v_.erase(it);
      else
        it->second = value;
    } else if (value != 0) {
      v_.insert(it, Level{price, value});
    }
  }

  Qty get(ScaledPrice price) const noexcept {
    auto it = lower(price);
    return (it != v_.end() && it->first == price) ? it->second : 0;
  }

  // Most aggressive own price on `side`: highest bid, lowest ask.
  std::optional<Level> best(Side side) const noexcept {
    if (v_.empty())
      return std::nullopt;
    return side == Side::Buy ? v_.back() : v_.front();
  }

  std::span<const Level> ascending() const noexcept {
    return {v_.data(), v_.size()};
  }
  bool empty() const noexcept { return v_.empty(); }
  std::size_t size() const noexcept { return v_.size(); }
  void clear() noexcept { v_.clear(); }

private:
  std::vector<Level>::iterator lower(ScaledPrice price) {
    return std::lower_bound(
        v_.begin(), v_.end(), price,
        [](const Level &a, ScaledPrice k) { return a.first < k; });
  }
  std::vector<Level>::const_iterator lower(ScaledPrice price) const {
    return std::lower_bound(
        v_.begin(), v_.end(), price,
        [](const Level &a, ScaledPrice k) { return a.first < k; });
  }

  std::vector<Level> v_;
};

} // namespace cmf::sim
