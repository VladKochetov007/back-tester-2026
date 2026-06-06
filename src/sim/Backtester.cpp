#include "sim/Backtester.hpp"

#include "ingestion/EventQueue.hpp"
#include "ingestion/FlatMerger.hpp"
#include "ingestion/Producer.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>

namespace cmf::sim {

namespace {

constexpr NanoTime NS_PER_DAY = 86'400'000'000'000LL;

// Days since the Unix epoch for a Gregorian y/m/d (Howard Hinnant's algorithm).
std::int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097LL + static_cast<int>(doe) - 719468;
}

// Extracts the first 8-digit YYYYMMDD run from a filename, returns its
// [start,end) nanosecond bounds. Returns false when no date is present.
bool file_day_bounds(const std::string &name, NanoTime &day_start,
                     NanoTime &day_end) noexcept {
  for (std::size_t i = 0; i + 8 <= name.size(); ++i) {
    bool all_digits = true;
    for (std::size_t j = 0; j < 8; ++j)
      if (!std::isdigit(static_cast<unsigned char>(name[i + j]))) {
        all_digits = false;
        break;
      }
    if (!all_digits)
      continue;
    const int y = std::stoi(name.substr(i, 4));
    const int m = std::stoi(name.substr(i + 4, 2));
    const int d = std::stoi(name.substr(i + 6, 2));
    if (y < 2000 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31)
      continue;
    day_start =
        days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d)) *
        NS_PER_DAY;
    day_end = day_start + NS_PER_DAY;
    return true;
  }
  return false;
}

} // namespace

Backtester::Backtester(EngineStrategy &strategy, FillModel &fill,
                       BacktestConfig cfg)
    : cfg_(std::move(cfg)) {
  engine_ = sim_.add_engine(strategy, fill, cfg_.engine);
}

std::vector<std::filesystem::path> Backtester::collect_files() const {
  namespace fs = std::filesystem;
  std::vector<fs::path> files;

  const auto keep = [&](const fs::path &p) {
    if (!p.string().ends_with(".mbo.json"))
      return;
    if (cfg_.start_ts || cfg_.end_ts) {
      NanoTime ds, de;
      if (file_day_bounds(p.filename().string(), ds, de)) {
        if (cfg_.end_ts && ds > cfg_.end_ts)
          return; // file starts after range
        if (cfg_.start_ts && de <= cfg_.start_ts)
          return; // file ends before range
      }
    }
    files.push_back(p);
  };

  for (const auto &path : cfg_.paths) {
    const fs::path p(path);
    if (fs::is_regular_file(p)) {
      keep(p);
    } else if (fs::is_directory(p)) {
      for (const auto &entry : fs::recursive_directory_iterator(p))
        if (entry.is_regular_file())
          keep(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

void Backtester::emit(const ProgressFn &on_progress, double span) const {
  const ProgressInfo pi = sim_.progress(engine_);
  Progress p;
  p.last_ts = pi.last_ts;
  p.events = pi.events;
  p.pnl = pi.pnl;
  p.stats = pi.stats;
  p.stats_by_instrument = pi.stats_by_instrument;
  if (span > 0.0 && cfg_.start_ts) {
    const double frac = static_cast<double>(pi.last_ts - cfg_.start_ts) / span;
    p.percent = frac < 0.0 ? 0.0 : frac > 1.0 ? 1.0 : frac;
  }
  on_progress(p);
}

void Backtester::run(const ProgressFn &on_progress) {
  const auto files = collect_files();

  std::vector<std::unique_ptr<EventQueue>> queues;
  std::vector<EventQueue *> ptrs;
  std::vector<std::unique_ptr<Producer>> producers;
  queues.reserve(files.size());
  ptrs.reserve(files.size());
  producers.reserve(files.size());
  for (const auto &f : files) {
    auto &q = queues.emplace_back(std::make_unique<EventQueue>());
    ptrs.push_back(q.get());
    producers.emplace_back(std::make_unique<Producer>(f, *q));
  }

  FlatMerger merger(ptrs);
  for (auto &p : producers)
    p->start();
  merger.start();

  sim_.start();

  using clock = std::chrono::steady_clock;
  const double span = (cfg_.end_ts > cfg_.start_ts)
                          ? static_cast<double>(cfg_.end_ts - cfg_.start_ts)
                          : 0.0;
  auto next =
      clock::now() + std::chrono::duration<double>(cfg_.progress_seconds);

  MarketDataEvent e;
  bool past_end = false;
  while (merger.next(e)) {
    if (past_end)
      continue; // keep draining so producers can finish and not block on join
    if (cfg_.end_ts && e.ts_recv > cfg_.end_ts) {
      past_end = true;
      continue;
    }
    if (cfg_.start_ts && e.ts_recv < cfg_.start_ts)
      continue;
    if (cfg_.instrument_filter && e.instrument_id != cfg_.instrument_filter)
      continue;

    sim_.step(e);

    if (on_progress && clock::now() >= next) {
      emit(on_progress, span);
      next =
          clock::now() + std::chrono::duration<double>(cfg_.progress_seconds);
    }
  }

  sim_.finish();
  for (auto &p : producers)
    p->join();
  if (on_progress)
    emit(on_progress, span);
}

} // namespace cmf::sim
