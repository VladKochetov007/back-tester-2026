#include "sim/EngineView.hpp"
#include "sim/PriceLadder.hpp"

#include "catch2/catch_all.hpp"

using namespace cmf;
using namespace cmf::sim;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
} // namespace

TEST_CASE("PriceLadder - add aggregates and erases at zero", "[sim][ladder]") {
  PriceLadder l;
  l.add(S(100.0), 5);
  l.add(S(100.0), 3);
  REQUIRE(l.get(S(100.0)) == 8);
  l.add(S(100.0), -8);
  REQUIRE(l.get(S(100.0)) == 0);
  REQUIRE(l.empty());
}

TEST_CASE("PriceLadder - best is side aware", "[sim][ladder]") {
  PriceLadder l;
  l.add(S(100.0), 1);
  l.add(S(102.0), 1);
  l.add(S(101.0), 1);
  REQUIRE(l.best(Side::Buy)->first == S(102.0));  // highest bid
  REQUIRE(l.best(Side::Sell)->first == S(100.0)); // lowest ask
}

TEST_CASE("EngineView - resting orders aggregate into own depth",
          "[sim][engineview]") {
  EngineView v(1);
  v.add_resting(10, Side::Buy, S(100.0), 5);
  v.add_resting(11, Side::Buy, S(100.0), 3);
  REQUIRE(v.own_qty(Side::Buy, S(100.0)) == 8);
  REQUIRE(v.resting_count() == 2);

  REQUIRE(v.reduce_resting(10, 2) == 3); // 5 - 2 left on order 10
  REQUIRE(v.own_qty(Side::Buy, S(100.0)) == 6);
  REQUIRE(v.cancel_resting(11));
  REQUIRE(v.own_qty(Side::Buy, S(100.0)) == 3);
}

TEST_CASE("EngineView - consumed basement nets out historical depth",
          "[sim][engineview]") {
  EngineView v(1);
  REQUIRE(v.net_basement_qty(Side::Sell, S(101.0), 10) == 10);
  v.consume_basement(Side::Sell, S(101.0), 4);
  REQUIRE(v.net_basement_qty(Side::Sell, S(101.0), 10) == 6);
  v.consume_basement(Side::Sell, S(101.0), 100); // over-consume clamps at 0
  REQUIRE(v.net_basement_qty(Side::Sell, S(101.0), 10) == 0);
}
