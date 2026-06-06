// Wire schema for the in-process protocols: Group 1 (orders/trades, engine <->
// backtest) and Group 2 (market data, backtest -> engines). All structs are
// trivially copyable so they can travel through the lock-free SPSC channels.

#pragma once

#include "sim/SimTypes.hpp"

#include <cstdint>

namespace cmf::sim {

// OrderType (None/Limit/Market) comes from common/BasicTypes.hpp.
enum class OrderStatus : std::uint8_t {
  New,
  Acked,
  PartiallyFilled,
  Filled,
  Cancelled,
  Rejected
};
enum class RejectReason : std::uint8_t {
  None,
  RiskLimit,
  NoLiquidity,
  UnknownOrder,
  Halted
};

// ---- Group 1: order & trade protocol ----

struct NewOrder {
  EngineId engine_id = 0;
  ClOrdId client_order_id = 0;
  InstrumentId instrument_id = 0;
  Side side = Side::None;
  ScaledPrice price = PRICE_NONE; // ignored for Market orders
  Qty size = 0;
  OrderType type = OrderType::Limit;
  NanoTime ts = 0;
};

struct OrderFill {
  EngineId engine_id = 0;
  ClOrdId client_order_id = 0;
  OrderId order_id = 0;
  InstrumentId instrument_id = 0;
  Side side = Side::None;
  ScaledPrice price = PRICE_NONE;
  Qty size = 0;
  bool maker = false; // true = passive (resting) fill
  NanoTime ts = 0;
};

struct OrderReject {
  EngineId engine_id = 0;
  ClOrdId client_order_id = 0;
  InstrumentId instrument_id = 0;
  RejectReason reason = RejectReason::None;
  NanoTime ts = 0;
};

// ---- Group 2: market data protocol ----

struct BookUpdate {
  InstrumentId instrument_id = 0;
  NanoTime ts = 0;
  Seq seq = 0;
  ScaledPrice bid = 0; // 0 = no bid level
  ScaledPrice ask = 0; // 0 = no ask level
  Qty bid_size = 0;
  Qty ask_size = 0;
};

struct TradeTick {
  InstrumentId instrument_id = 0;
  NanoTime ts = 0;
  Seq seq = 0;
  ScaledPrice price = PRICE_NONE;
  Qty size = 0;
  Side aggressor = Side::None;
};

} // namespace cmf::sim
