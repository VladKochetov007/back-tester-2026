#include "sim/SimulatedLOB.hpp"

#include "SimTestUtil.hpp"
#include "catch2/catch_all.hpp"

using namespace cmf;
using namespace cmf::sim;
using cmf::test::add;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
constexpr InstrumentId INST = 3;

HistoricalLOB make_basement() {
  HistoricalLOB b(INST);
  b.apply(add('B', 100.0, 5, 1, INST, 1));
  b.apply(add('B', 99.0, 3, 2, INST, 2));
  b.apply(add('A', 101.0, 4, 3, INST, 3));
  b.apply(add('A', 102.0, 6, 4, INST, 4));
  return b;
}
} // namespace

TEST_CASE("SimulatedLOB - empty overlay equals the basement",
          "[sim][simulated]") {
  HistoricalLOB b = make_basement();
  EngineView v(INST);
  SimulatedLOB sim(b, v);

  REQUIRE(sim.best_price(Side::Buy) == S(100.0));
  REQUIRE(sim.best_price(Side::Sell) == S(101.0));
  REQUIRE(sim.qty_at(Side::Buy, S(100.0)) == 5);
  REQUIRE(sim.mid() == (S(100.0) + S(101.0)) / 2);
}

TEST_CASE("SimulatedLOB - own orders add depth and can improve the touch",
          "[sim][simulated]") {
  HistoricalLOB b = make_basement();
  EngineView v(INST);
  v.add_resting(50, Side::Buy, S(100.5), 2); // better than basement bid 100
  v.add_resting(51, Side::Buy, S(100.0), 4); // joins basement level 100
  SimulatedLOB sim(b, v);

  REQUIRE(sim.best_price(Side::Buy) == S(100.5));
  REQUIRE(sim.qty_at(Side::Buy, S(100.0)) == 9); // 5 historical + 4 own

  std::vector<SimulatedLOB::Level> snap;
  REQUIRE(sim.snapshot(Side::Buy, snap, 3) == 3);
  REQUIRE(snap[0] == SimulatedLOB::Level{S(100.5), 2});
  REQUIRE(snap[1] == SimulatedLOB::Level{S(100.0), 9});
  REQUIRE(snap[2] == SimulatedLOB::Level{S(99.0), 3});
}

TEST_CASE("SimulatedLOB - consumed basement liquidity disappears from the view",
          "[sim][simulated]") {
  HistoricalLOB b = make_basement();
  EngineView v(INST);
  v.consume_basement(Side::Sell, S(101.0), 4); // engine ate the whole top ask
  SimulatedLOB sim(b, v);

  REQUIRE(sim.best_price(Side::Sell) ==
          S(102.0)); // top ask gone for this engine
  REQUIRE(sim.qty_at(Side::Sell, S(101.0)) == 0);
}
