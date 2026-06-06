// Drives many trading engines over one shared set of historical basements.
//
// Per event the loop runs in phases so each engine sees a consistent private
// view and fills never reenter a callback:
//   1. advance the instrument's basement (the only write to shared state),
//   2. for each engine: passive-fill resting orders, deliver market data,
//      then drain and match the orders the strategy queued.
// Each engine owns its EngineView/positions/orders, so the basement is shared
// read-only during the engine phase - N engines, no data races.

#pragma once

#include "common/MarketDataEvent.hpp"
#include "sim/EngineStrategy.hpp"
#include "sim/FillModel.hpp"
#include "sim/HistoricalLOB.hpp"
#include "sim/Messages.hpp"
#include "sim/Position.hpp"
#include "sim/SimTypes.hpp"
#include "sim/SimulatedLOB.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cmf::sim {

struct OrderStats {
  std::uint64_t sent = 0;
  std::uint64_t filled = 0; // number of fill events
  std::uint64_t cancelled = 0;
  std::uint64_t rejected = 0;
  Qty filled_qty = 0;
};

struct OrderLogEntry {
  NanoTime ts = 0;
  EngineId engine_id = 0;
  OrderId order_id = 0;
  ClOrdId client_order_id = 0;
  InstrumentId instrument_id = 0;
  Side side = Side::None;
  ScaledPrice price = PRICE_NONE;
  Qty size = 0;
  OrderType type = OrderType::Limit;
  OrderStatus status = OrderStatus::New;
};

struct ProgressInfo {
  NanoTime last_ts = 0;
  std::uint64_t events = 0;
  double pnl = 0.0;
  OrderStats stats;
  std::unordered_map<InstrumentId, OrderStats> stats_by_instrument;
};

struct EngineConfig {
  NanoTime pnl_sample_interval_ns =
      1'000'000'000;    // sample equity on this sim-time grid
  Qty max_position = 0; // absolute per-instrument cap; 0 = unlimited
};

class MultiEngineSimulator {
public:
  MultiEngineSimulator();
  ~MultiEngineSimulator();
  MultiEngineSimulator(const MultiEngineSimulator &) = delete;
  MultiEngineSimulator &operator=(const MultiEngineSimulator &) = delete;

  EngineId add_engine(EngineStrategy &strategy, FillModel &fill,
                      EngineConfig cfg = {});

  void start();
  void step(const MarketDataEvent &e);
  void finish();

  template <class Source> void run(Source &source) {
    start();
    MarketDataEvent e;
    while (source.next(e))
      step(e);
    finish();
  }

  std::size_t engine_count() const noexcept { return engines_.size(); }
  std::uint64_t events_processed() const noexcept { return events_; }
  NanoTime first_ts() const noexcept { return first_ts_; }
  NanoTime last_ts() const noexcept { return last_ts_; }
  std::size_t instrument_count() const noexcept { return basements_.size(); }

  const HistoricalLOB *basement(InstrumentId id) const noexcept;
  const SimulatedLOB *book(EngineId engine, InstrumentId id) const noexcept;

  const std::vector<OrderFill> &fills(EngineId engine) const;
  const std::vector<OrderLogEntry> &order_log(EngineId engine) const;
  const std::vector<std::pair<NanoTime, double>> &
  pnl_curve(EngineId engine) const;
  const OrderStats &stats(EngineId engine) const;
  const std::unordered_map<InstrumentId, OrderStats> &
  stats_by_instrument(EngineId engine) const;
  double final_equity(EngineId engine) const;
  Qty position(EngineId engine, InstrumentId id) const;
  ProgressInfo progress(EngineId engine) const;

private:
  struct PendingNew {
    OrderId order_id = 0;
    NewOrder order;
  };

  struct EngineOrder {
    OrderId order_id = 0;
    EngineId engine_id = 0;
    ClOrdId client_order_id = 0;
    InstrumentId instrument_id = 0;
    Side side = Side::None;
    ScaledPrice price = PRICE_NONE;
    Qty size = 0;
    Qty remaining = 0;
    OrderType type = OrderType::Limit;
  };

  struct Engine {
    EngineId id = 0;
    EngineStrategy *strategy = nullptr;
    FillModel *fill = nullptr;
    EngineConfig cfg{};

    std::unordered_map<InstrumentId, EngineView> views;
    std::unordered_map<InstrumentId, SimulatedLOB> sims;
    std::unordered_map<InstrumentId, Position> positions;
    std::unordered_map<OrderId, EngineOrder> live;

    OrderStats total_stats;
    std::unordered_map<InstrumentId, OrderStats> by_instrument;
    std::vector<OrderFill> fills_log;
    std::vector<OrderLogEntry> order_log;
    std::vector<std::pair<NanoTime, double>> pnl;

    std::vector<PendingNew> pending_new;
    std::vector<OrderId> pending_cancel;
    ClOrdId cl_seq = 0;
    NanoTime next_pnl_sample = 0;
  };

  class Ctx;

  OrderId ctx_send(std::size_t idx, InstrumentId inst, Side side,
                   ScaledPrice price, Qty size, OrderType type, NanoTime ts);
  bool ctx_cancel(std::size_t idx, OrderId order_id);

  HistoricalLOB &get_basement(InstrumentId id);
  void ensure_engine_instrument(Engine &eng, InstrumentId id);
  void notify_engine(Engine &eng, std::size_t idx, const MarketDataEvent &e,
                     bool bbo_moved, const BookUpdate &bu, bool is_trade,
                     const TradeTick &tt);
  void drain(Engine &eng, std::size_t idx, NanoTime ts);
  void do_submit(Engine &eng, std::size_t idx, const PendingNew &pn,
                 NanoTime ts);
  void do_cancel(Engine &eng, OrderId order_id, NanoTime ts);
  void apply_fill(Engine &eng, std::size_t idx, const OrderFill &f);
  OrderFill enrich_fill(Engine &eng, const RawFill &rf, NanoTime ts) const;
  void record_log(Engine &eng, const EngineOrder &o, NanoTime ts,
                  OrderStatus status);
  double mark_equity(const Engine &eng) const;
  void sample_pnl(Engine &eng, NanoTime ts);

  Engine &eng_at(EngineId id);
  const Engine &eng_at(EngineId id) const;

  std::unordered_map<InstrumentId, HistoricalLOB> basements_;
  std::unordered_map<InstrumentId, ScaledPrice> marks_;
  std::vector<std::unique_ptr<Engine>> engines_;
  std::vector<std::unique_ptr<Ctx>> contexts_;
  OrderId next_order_id_ = 1;
  std::uint64_t events_ = 0;
  NanoTime first_ts_ = 0;
  NanoTime last_ts_ = 0;
  bool started_ = false;
};

} // namespace cmf::sim
