#include "sim/TouchFillModel.hpp"

#include "SimTestUtil.hpp"
#include "catch2/catch_all.hpp"

using namespace cmf;
using namespace cmf::sim;
using cmf::test::add;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
constexpr InstrumentId INST = 5;

NewOrder limit(Side side, double px, Qty sz) {
  return NewOrder{0, 0, INST, side, S(px), sz, OrderType::Limit, 0};
}
NewOrder market(Side side, Qty sz) {
  return NewOrder{0, 0, INST, side, PRICE_NONE, sz, OrderType::Market, 0};
}
} // namespace

TEST_CASE(
    "TouchFillModel - marketable limit fills at touch and consumes basement",
    "[sim][fill]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 101.0, 10, 1, INST, 1));
  EngineView v(INST);
  SimulatedLOB sim(b, v);
  TouchFillModel model;

  FillOutcome out =
      model.on_submit(sim, v, /*order_id=*/1, limit(Side::Buy, 101.0, 4));
  REQUIRE_FALSE(out.rejected);
  REQUIRE(out.resting == 0);
  REQUIRE(out.fills.size() == 1);
  REQUIRE(out.fills[0].price == S(101.0));
  REQUIRE(out.fills[0].size == 4);
  REQUIRE_FALSE(out.fills[0].maker);
  REQUIRE(sim.qty_at(Side::Sell, S(101.0)) == 6); // 4 consumed of 10
}

TEST_CASE("TouchFillModel - marketable order walks multiple levels",
          "[sim][fill]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 101.0, 10, 1, INST, 1));
  b.apply(add('A', 102.0, 5, 2, INST, 2));
  EngineView v(INST);
  SimulatedLOB sim(b, v);
  TouchFillModel model;

  FillOutcome out = model.on_submit(sim, v, 1, limit(Side::Buy, 102.0, 12));
  REQUIRE(out.fills.size() == 2);
  REQUIRE(out.fills[0].price == S(101.0));
  REQUIRE(out.fills[0].size == 10);
  REQUIRE(out.fills[1].price == S(102.0));
  REQUIRE(out.fills[1].size == 2);
  REQUIRE(out.resting == 0);
}

TEST_CASE("TouchFillModel - non-marketable limit rests", "[sim][fill]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 101.0, 10, 1, INST, 1));
  EngineView v(INST);
  SimulatedLOB sim(b, v);
  TouchFillModel model;

  FillOutcome out = model.on_submit(sim, v, 7, limit(Side::Buy, 100.0, 4));
  REQUIRE(out.fills.empty());
  REQUIRE(out.resting == 4);
  REQUIRE(v.own_qty(Side::Buy, S(100.0)) == 4);
}

TEST_CASE(
    "TouchFillModel - resting order fills passively when market crosses it",
    "[sim][fill]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 101.0, 10, 1, INST, 1));
  EngineView v(INST);
  SimulatedLOB sim(b, v);
  TouchFillModel model;

  model.on_submit(sim, v, 7, limit(Side::Buy, 100.0, 4)); // rests at 100
  b.apply(add('A', 100.0, 5, 2, INST, 2)); // best ask drops to 100

  std::vector<RawFill> passive;
  model.on_market_event(sim, v, add('A', 100.0, 5, 2, INST, 2), passive);
  REQUIRE(passive.size() == 1);
  REQUIRE(passive[0].maker);
  REQUIRE(passive[0].price == S(100.0));
  REQUIRE(passive[0].size == 4);
  REQUIRE(v.own_qty(Side::Buy, S(100.0)) == 0); // resting consumed
}

TEST_CASE("TouchFillModel - market order with no liquidity is rejected",
          "[sim][fill]") {
  HistoricalLOB b(INST);
  EngineView v(INST);
  SimulatedLOB sim(b, v);
  TouchFillModel model;

  FillOutcome out = model.on_submit(sim, v, 1, market(Side::Buy, 5));
  REQUIRE(out.rejected);
  REQUIRE(out.reason == RejectReason::NoLiquidity);
  REQUIRE(out.fills.empty());
}
