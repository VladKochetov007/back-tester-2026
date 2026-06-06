#include "sim/HistoricalLOB.hpp"

#include "SimTestUtil.hpp"
#include "catch2/catch_all.hpp"

using namespace cmf;
using namespace cmf::sim;
using cmf::test::add;
using cmf::test::cancel;
using cmf::test::trade;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
constexpr InstrumentId INST = 7;
} // namespace

TEST_CASE("HistoricalLOB - reconstructs BBO from L3 adds",
          "[sim][historical]") {
  HistoricalLOB b(INST);
  REQUIRE(b.instrument_id() == INST);
  REQUIRE(b.empty());

  b.apply(add('B', 100.0, 5, 1, INST, 10));
  b.apply(add('B', 101.0, 2, 2, INST, 11));
  b.apply(add('A', 102.0, 4, 3, INST, 12));

  REQUIRE(b.best_price(Side::Buy) == S(101.0));
  REQUIRE(b.best_price(Side::Sell) == S(102.0));
  REQUIRE(b.qty_at(Side::Buy, S(100.0)) == 5u);
}

TEST_CASE("HistoricalLOB - version bumps only when top of book moves",
          "[sim][historical]") {
  HistoricalLOB b(INST);
  REQUIRE(b.apply(add('B', 100.0, 5, 1, INST, 1))); // new best bid -> moves
  const auto v1 = b.version();
  REQUIRE_FALSE(b.apply(add('B', 99.0, 5, 2, INST, 2))); // worse bid -> no move
  REQUIRE(b.version() == v1);
  REQUIRE(b.apply(add('A', 101.0, 5, 3, INST, 3))); // first ask -> moves
  REQUIRE(b.version() == v1 + 1);
}

TEST_CASE("HistoricalLOB - cancel reduces depth and clears level",
          "[sim][historical]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 50.0, 10, 1, INST, 1));
  b.apply(cancel('A', 50.0, 4, 1, INST, 2));
  REQUIRE(b.qty_at(Side::Sell, S(50.0)) == 6u);
  b.apply(cancel('A', 50.0, 6, 1, INST, 3));
  REQUIRE(b.empty());
}

TEST_CASE("HistoricalLOB - trade is recorded without mutating the book",
          "[sim][historical]") {
  HistoricalLOB b(INST);
  b.apply(add('A', 101.0, 5, 1, INST, 1));
  const auto v = b.version();
  REQUIRE_FALSE(b.apply(trade('A', 101.0, 2, INST, 2)));
  REQUIRE(b.version() == v); // trade does not move the book
  REQUIRE(b.qty_at(Side::Sell, S(101.0)) == 5u);
  REQUIRE(b.last_trade().has_value());
  REQUIRE(b.last_trade()->price == S(101.0));
  REQUIRE(b.last_trade()->qty == 2);
}

TEST_CASE("HistoricalLOB - level iteration is best first",
          "[sim][historical]") {
  HistoricalLOB b(INST);
  b.apply(add('B', 100.0, 1, 1, INST, 1));
  b.apply(add('B', 101.0, 2, 2, INST, 2));
  b.apply(add('B', 99.0, 3, 3, INST, 3));

  std::vector<ScaledPrice> seen;
  b.for_each_level(Side::Buy, [&](ScaledPrice p, LimitOrderBook::AggQty) {
    seen.push_back(p);
  });
  REQUIRE(seen.size() == 3);
  REQUIRE(seen[0] == S(101.0));
  REQUIRE(seen[1] == S(100.0));
  REQUIRE(seen[2] == S(99.0));
}
