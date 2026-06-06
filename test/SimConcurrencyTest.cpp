// N engine views read one shared, const HistoricalLOB while each mutates only
// its own EngineView. That is race-free by construction; this test exercises it
// and doubles as a ThreadSanitizer target (build with -fsanitize=thread).

#include "sim/SimulatedLOB.hpp"
#include "sim/TouchFillModel.hpp"

#include "SimTestUtil.hpp"
#include "catch2/catch_all.hpp"

#include <thread>
#include <vector>

using namespace cmf;
using namespace cmf::sim;
using cmf::test::add;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
constexpr InstrumentId INST = 11;
} // namespace

TEST_CASE("Concurrency - many engines share one basement with no races",
          "[sim][concurrency]") {
  HistoricalLOB basement(INST);
  basement.apply(add('A', 101.0, 1'000'000, 1, INST, 1));
  basement.apply(add('B', 100.0, 1'000'000, 2, INST, 2));

  constexpr int N = 8;
  constexpr Qty per_lot = 5;
  constexpr int lots = 1000;

  std::vector<std::thread> threads;
  std::vector<Qty> filled(N, 0);

  for (int t = 0; t < N; ++t) {
    threads.emplace_back([&, t] {
      EngineView view(INST);            // private to this thread
      SimulatedLOB sim(basement, view); // reads the shared const basement
      TouchFillModel model;
      Qty got = 0;
      for (int i = 0; i < lots; ++i) {
        NewOrder o{
            static_cast<EngineId>(t), 0, INST, Side::Buy, S(101.0), per_lot,
            OrderType::Limit,         0};
        FillOutcome out =
            model.on_submit(sim, view, static_cast<OrderId>(i + 1), o);
        for (const RawFill &f : out.fills)
          got += f.size;
      }
      filled[t] = got;
    });
  }
  for (auto &th : threads)
    th.join();

  for (int t = 0; t < N; ++t)
    REQUIRE(filled[t] == per_lot * lots);

  // The shared basement was only read; its depth is intact.
  REQUIRE(basement.qty_at(Side::Sell, S(101.0)) == 1'000'000u);
}
