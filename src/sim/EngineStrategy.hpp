// The trading-engine interface. A strategy reacts to market data and fills and
// acts through an EngineContext (send/cancel orders, read its private book).
// This is the single seam the Python bindings extend: a Python Strategy is just
// an EngineStrategy whose virtual callbacks are overridden in Python.

#pragma once

#include "sim/Messages.hpp"
#include "sim/SimTypes.hpp"
#include "sim/SimulatedLOB.hpp"

namespace cmf::sim {

// Handle a strategy uses to act during a callback. Orders are queued and
// matched by the simulator after the callback returns, so fills never reenter
// mid-call.
class EngineContext {
public:
  virtual ~EngineContext() = default;

  virtual EngineId engine_id() const = 0;
  virtual NanoTime now() const = 0;

  virtual OrderId send_limit(InstrumentId, Side, ScaledPrice price,
                             Qty size) = 0;
  virtual OrderId send_market(InstrumentId, Side, Qty size) = 0;
  virtual bool cancel(OrderId) = 0;

  virtual const SimulatedLOB *book(InstrumentId) const = 0;
  virtual Qty position(InstrumentId) const = 0;
};

class EngineStrategy {
public:
  virtual ~EngineStrategy() = default;

  virtual void on_start(EngineContext &) {}
  virtual void on_book_update(EngineContext &, const BookUpdate &) {}
  virtual void on_trade(EngineContext &, const TradeTick &) {}
  virtual void on_fill(EngineContext &, const OrderFill &) {}
  virtual void on_reject(EngineContext &, const OrderReject &) {}
  virtual void on_stop(EngineContext &) {}
};

} // namespace cmf::sim
