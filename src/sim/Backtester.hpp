// Wires the existing ingestion pipeline (Producer -> FlatMerger) into the
// MultiEngineSimulator for one strategy. Group 4's Backtest.run() is a thin
// Python shim over this: point it at a folder of L3 files, optionally restrict
// the time range / instrument, and get back a fully-populated simulator.

#pragma once

#include "common/MarketDataEvent.hpp"
#include "sim/EngineStrategy.hpp"
#include "sim/FillModel.hpp"
#include "sim/MultiEngineSimulator.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cmf::sim {

struct BacktestConfig {
  std::vector<std::string> paths;     // files and/or folders of *.mbo.json
  NanoTime start_ts = 0;              // 0 = unbounded
  NanoTime end_ts = 0;                // 0 = unbounded
  InstrumentId instrument_filter = 0; // 0 = all instruments
  EngineConfig engine{};
  double progress_seconds = 30.0; // wall-clock cadence of progress callback
};

struct Progress {
  double percent = 0.0; // [0,1], 0 when the range is unknown
  NanoTime last_ts = 0;
  std::uint64_t events = 0;
  double pnl = 0.0;
  OrderStats stats;
  std::unordered_map<InstrumentId, OrderStats> stats_by_instrument;
};

class Backtester {
public:
  using ProgressFn = std::function<void(const Progress &)>;

  Backtester(EngineStrategy &strategy, FillModel &fill, BacktestConfig cfg);

  void run(const ProgressFn &on_progress = {});

  MultiEngineSimulator &simulator() noexcept { return sim_; }
  const MultiEngineSimulator &simulator() const noexcept { return sim_; }
  EngineId engine_id() const noexcept { return engine_; }

private:
  std::vector<std::filesystem::path> collect_files() const;
  void emit(const ProgressFn &on_progress, double span) const;

  BacktestConfig cfg_;
  MultiEngineSimulator sim_;
  EngineId engine_ = 0;
};

} // namespace cmf::sim
