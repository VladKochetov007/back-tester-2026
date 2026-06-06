// Per-instrument position and cash accounting for one engine. Equity is marked
// against a supplied reference price (typically the simulated mid).

#pragma once

#include "sim/SimTypes.hpp"

#include <optional>

namespace cmf::sim {

struct Position {
  Qty qty = 0; // signed: +long / -short
  double cash =
      0.0; // realised cash flow in price units (sells add, buys subtract)
  Qty bought = 0;
  Qty sold = 0;

  void on_fill(Side side, ScaledPrice price, Qty size) noexcept {
    const int s = side_sign(side);
    const double px = unscale_price(price);
    qty += s * size;
    cash -= s * static_cast<double>(size) * px;
    if (s > 0)
      bought += size;
    else
      sold += size;
  }

  double equity(std::optional<ScaledPrice> mark) const noexcept {
    const double m = mark ? unscale_price(*mark) : 0.0;
    return cash + static_cast<double>(qty) * m;
  }
};

} // namespace cmf::sim
