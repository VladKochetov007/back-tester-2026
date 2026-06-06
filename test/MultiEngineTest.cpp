#include "sim/MultiEngineSimulator.hpp"
#include "sim/TouchFillModel.hpp"

#include "SimTestUtil.hpp"
#include "catch2/catch_all.hpp"

using namespace cmf;
using namespace cmf::sim;
using cmf::test::add;

namespace {
ScaledPrice S(double p) { return LimitOrderBook::scale(p); }
constexpr InstrumentId INST = 9;

// Sends one marketable buy the first time it sees the book, then counts fills.
struct OneShotBuy : EngineStrategy {
  ScaledPrice price;
  Qty size;
  bool sent = false;
  int fills = 0;
  OneShotBuy(double px, Qty sz) : price(S(px)), size(sz) {}

  void on_book_update(EngineContext &ctx, const BookUpdate &) override {
    if (sent)
      return;
    sent = true;
    ctx.send_limit(INST, Side::Buy, price, size);
  }
  void on_fill(EngineContext &, const OrderFill &) override { ++fills; }
};
} // namespace

TEST_CASE("MultiEngineSimulator - marketable order fills and books position",
          "[sim][engine]") {
  MultiEngineSimulator simr;
  TouchFillModel model;
  OneShotBuy strat(101.0, 4);
  const EngineId eng = simr.add_engine(strat, model);

  simr.start();
  simr.step(
      add('A', 101.0, 10, 1, INST, 1)); // first ask -> book update -> buy 4
  simr.finish();

  REQUIRE(strat.fills == 1);
  REQUIRE(simr.fills(eng).size() == 1);
  REQUIRE(simr.fills(eng)[0].price == S(101.0));
  REQUIRE(simr.position(eng, INST) == 4);
  REQUIRE(simr.stats(eng).sent == 1);
  REQUIRE(simr.stats(eng).filled == 1);
  REQUIRE_FALSE(simr.order_log(eng).empty());
  REQUIRE_FALSE(simr.pnl_curve(eng).empty());
}

TEST_CASE(
    "MultiEngineSimulator - engines see the shared basement independently",
    "[sim][engine]") {
  MultiEngineSimulator simr;
  TouchFillModel model;
  OneShotBuy a(101.0, 10); // each lifts the whole top ask
  OneShotBuy b(101.0, 10);
  const EngineId ea = simr.add_engine(a, model);
  const EngineId eb = simr.add_engine(b, model);

  simr.start();
  simr.step(add('A', 101.0, 10, 1, INST, 1));
  simr.finish();

  // Both engines fill the full 10: consumption is private to each EngineView,
  // so one engine eating the basement does not deny the other.
  REQUIRE(simr.position(ea, INST) == 10);
  REQUIRE(simr.position(eb, INST) == 10);

  // The shared basement is untouched by either engine's trading.
  REQUIRE(simr.basement(INST) != nullptr);
  REQUIRE(simr.basement(INST)->qty_at(Side::Sell, S(101.0)) == 10u);
}

TEST_CASE("MultiEngineSimulator - risk limit rejects oversized orders",
          "[sim][engine]") {
  MultiEngineSimulator simr;
  TouchFillModel model;
  OneShotBuy strat(101.0, 50);
  EngineConfig cfg;
  cfg.max_position = 10;
  const EngineId eng = simr.add_engine(strat, model, cfg);

  simr.start();
  simr.step(add('A', 101.0, 100, 1, INST, 1));
  simr.finish();

  REQUIRE(simr.stats(eng).rejected == 1);
  REQUIRE(simr.position(eng, INST) == 0);
}
