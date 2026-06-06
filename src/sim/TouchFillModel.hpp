// Simplest fill policy required by HW3: an order fills when it is at or through
// the best historical price, at the touch. Marketable size is taken from the
// opposite side of the basement, level by level, best price first; any limit
// remainder rests. Resting orders fill passively once the historical market
// trades down (or up) to their price.

#pragma once

#include "sim/FillModel.hpp"

namespace cmf::sim {

class TouchFillModel : public FillModel {
public:
  FillOutcome on_submit(const SimulatedLOB &book, EngineView &ev,
                        OrderId order_id, const NewOrder &order) override;

  void on_market_event(const SimulatedLOB &book, EngineView &ev,
                       const MarketDataEvent &event,
                       std::vector<RawFill> &out) override;
};

} // namespace cmf::sim
