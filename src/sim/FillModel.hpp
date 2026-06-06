// Policy that decides how an engine's orders execute against its private
// SimulatedLOB. The simulator owns no matching rules of its own - it delegates
// to a FillModel, so richer models (queue position, partial-impact, latency)
// drop in by composition without changing the engine loop. Open for extension,
// closed for modification.
//
// The model reports price/size/liquidity-role only (a RawFill); the simulator
// adds order identity, PnL and routing. The model owns book-side bookkeeping:
// it records consumed basement liquidity and reduces resting orders on `ev`.

#pragma once

#include "common/MarketDataEvent.hpp"
#include "sim/EngineView.hpp"
#include "sim/Messages.hpp"
#include "sim/SimulatedLOB.hpp"

#include <vector>

namespace cmf::sim {

struct RawFill {
  OrderId order_id = 0;
  Side side = Side::None;
  ScaledPrice price = PRICE_NONE;
  Qty size = 0;
  bool maker = false;
};

struct FillOutcome {
  std::vector<RawFill> fills; // taker executions, in order
  Qty resting = 0;            // limit remainder left on the book
  bool rejected = false;
  RejectReason reason = RejectReason::None;
};

class FillModel {
public:
  virtual ~FillModel() = default;

  // An engine submitted a new order. The model may execute marketable size
  // immediately, rest the remainder via ev.add_resting(), and MUST record any
  // basement liquidity it consumed via ev.consume_basement().
  virtual FillOutcome on_submit(const SimulatedLOB &book, EngineView &ev,
                                OrderId order_id, const NewOrder &order) = 0;

  // The basement advanced. Resting own orders now at or through the best
  // historical price fill here (maker fills) and are appended to `out`. The
  // model reduces the resting orders and consumes the matched basement depth.
  virtual void on_market_event(const SimulatedLOB &book, EngineView &ev,
                               const MarketDataEvent &event,
                               std::vector<RawFill> &out) = 0;
};

} // namespace cmf::sim
