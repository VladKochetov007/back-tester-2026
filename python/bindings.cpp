// Python bindings for the simulation core. A Python Strategy subclasses
// EngineStrategy (trampoline below) and acts through an EngineContext; the
// backtest runs entirely in C++ and hands back copyable result data that the
// cmf_bt package turns into pandas frames.

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "lob/LimitOrderBook.hpp"
#include "sim/Backtester.hpp"
#include "sim/EngineStrategy.hpp"
#include "sim/Messages.hpp"
#include "sim/MultiEngineSimulator.hpp"
#include "sim/SimulatedLOB.hpp"
#include "sim/TouchFillModel.hpp"

#include <utility>
#include <vector>

namespace py = pybind11;
using namespace cmf;
using namespace cmf::sim;

namespace {

// EngineContext is non-copyable, so it must reach Python as a reference rather
// than via pybind's default copy. We dispatch overrides by hand and cast the
// context pointer with reference policy.
class PyEngineStrategy : public EngineStrategy {
public:
  using EngineStrategy::EngineStrategy;

  void on_start(EngineContext &c) override { dispatch0("on_start", c); }
  void on_book_update(EngineContext &c, const BookUpdate &b) override {
    dispatch1("on_book_update", c, b);
  }
  void on_trade(EngineContext &c, const TradeTick &t) override {
    dispatch1("on_trade", c, t);
  }
  void on_fill(EngineContext &c, const OrderFill &f) override {
    dispatch1("on_fill", c, f);
  }
  void on_reject(EngineContext &c, const OrderReject &r) override {
    dispatch1("on_reject", c, r);
  }
  void on_stop(EngineContext &c) override { dispatch0("on_stop", c); }

private:
  void dispatch0(const char *name, EngineContext &c) {
    py::gil_scoped_acquire gil;
    if (auto f =
            py::get_override(static_cast<const EngineStrategy *>(this), name))
      f(py::cast(&c, py::return_value_policy::reference));
  }
  template <class Msg>
  void dispatch1(const char *name, EngineContext &c, const Msg &m) {
    py::gil_scoped_acquire gil;
    if (auto f =
            py::get_override(static_cast<const EngineStrategy *>(this), name))
      f(py::cast(&c, py::return_value_policy::reference), m);
  }
};

// Self-contained, copyable snapshot of one engine's results for return to
// Python.
struct BacktestResultData {
  EngineId engine_id = 0;
  NanoTime first_ts = 0;
  NanoTime last_ts = 0;
  std::uint64_t events = 0;
  double final_equity = 0.0;
  OrderStats stats;
  std::unordered_map<InstrumentId, OrderStats> stats_by_instrument;
  std::vector<std::pair<NanoTime, double>> pnl;
  std::vector<OrderFill> fills;
  std::vector<OrderLogEntry> order_log;
};

BacktestResultData extract(const MultiEngineSimulator &sim, EngineId eng) {
  BacktestResultData r;
  r.engine_id = eng;
  r.first_ts = sim.first_ts();
  r.last_ts = sim.last_ts();
  r.events = sim.events_processed();
  r.final_equity = sim.final_equity(eng);
  r.stats = sim.stats(eng);
  r.stats_by_instrument = sim.stats_by_instrument(eng);
  r.pnl = sim.pnl_curve(eng);
  r.fills = sim.fills(eng);
  r.order_log = sim.order_log(eng);
  return r;
}

BacktestResultData run_backtest(EngineStrategy &strategy, BacktestConfig cfg,
                                py::object progress) {
  TouchFillModel model;
  Backtester bt(strategy, model, std::move(cfg));

  Backtester::ProgressFn fn;
  if (!progress.is_none())
    fn = [&progress](const Progress &p) { progress(p); };

  bt.run(fn);
  return extract(bt.simulator(), bt.engine_id());
}

} // namespace

PYBIND11_MODULE(_cmf_bt, m) {
  m.doc() = "C++ core for the CMF multi-engine backtester (HW3 groups 3 & 4)";

  m.attr("PRICE_SCALE") = LimitOrderBook::SCALE;
  m.def("scale", &LimitOrderBook::scale, "price (float) -> scaled integer");
  m.def("unscale", &LimitOrderBook::unscale, "scaled integer -> price (float)");

  py::enum_<Side>(m, "Side")
      .value("Buy", Side::Buy)
      .value("Sell", Side::Sell)
      .value("Nil", Side::None);

  py::enum_<OrderType>(m, "OrderType")
      .value("Limit", OrderType::Limit)
      .value("Market", OrderType::Market)
      .value("Nil", OrderType::None);

  py::enum_<OrderStatus>(m, "OrderStatus")
      .value("New", OrderStatus::New)
      .value("Acked", OrderStatus::Acked)
      .value("PartiallyFilled", OrderStatus::PartiallyFilled)
      .value("Filled", OrderStatus::Filled)
      .value("Cancelled", OrderStatus::Cancelled)
      .value("Rejected", OrderStatus::Rejected);

  py::enum_<RejectReason>(m, "RejectReason")
      .value("Nil", RejectReason::None)
      .value("RiskLimit", RejectReason::RiskLimit)
      .value("NoLiquidity", RejectReason::NoLiquidity)
      .value("UnknownOrder", RejectReason::UnknownOrder)
      .value("Halted", RejectReason::Halted);

  py::class_<BookUpdate>(m, "BookUpdate")
      .def_readonly("instrument_id", &BookUpdate::instrument_id)
      .def_readonly("ts", &BookUpdate::ts)
      .def_readonly("seq", &BookUpdate::seq)
      .def_readonly("bid", &BookUpdate::bid)
      .def_readonly("ask", &BookUpdate::ask)
      .def_readonly("bid_size", &BookUpdate::bid_size)
      .def_readonly("ask_size", &BookUpdate::ask_size);

  py::class_<TradeTick>(m, "TradeTick")
      .def_readonly("instrument_id", &TradeTick::instrument_id)
      .def_readonly("ts", &TradeTick::ts)
      .def_readonly("seq", &TradeTick::seq)
      .def_readonly("price", &TradeTick::price)
      .def_readonly("size", &TradeTick::size)
      .def_readonly("aggressor", &TradeTick::aggressor);

  py::class_<OrderFill>(m, "OrderFill")
      .def_readonly("engine_id", &OrderFill::engine_id)
      .def_readonly("client_order_id", &OrderFill::client_order_id)
      .def_readonly("order_id", &OrderFill::order_id)
      .def_readonly("instrument_id", &OrderFill::instrument_id)
      .def_readonly("side", &OrderFill::side)
      .def_readonly("price", &OrderFill::price)
      .def_readonly("size", &OrderFill::size)
      .def_readonly("maker", &OrderFill::maker)
      .def_readonly("ts", &OrderFill::ts);

  py::class_<OrderReject>(m, "OrderReject")
      .def_readonly("engine_id", &OrderReject::engine_id)
      .def_readonly("client_order_id", &OrderReject::client_order_id)
      .def_readonly("instrument_id", &OrderReject::instrument_id)
      .def_readonly("reason", &OrderReject::reason)
      .def_readonly("ts", &OrderReject::ts);

  py::class_<OrderStats>(m, "OrderStats")
      .def_readonly("sent", &OrderStats::sent)
      .def_readonly("filled", &OrderStats::filled)
      .def_readonly("cancelled", &OrderStats::cancelled)
      .def_readonly("rejected", &OrderStats::rejected)
      .def_readonly("filled_qty", &OrderStats::filled_qty);

  py::class_<OrderLogEntry>(m, "OrderLogEntry")
      .def_readonly("ts", &OrderLogEntry::ts)
      .def_readonly("engine_id", &OrderLogEntry::engine_id)
      .def_readonly("order_id", &OrderLogEntry::order_id)
      .def_readonly("client_order_id", &OrderLogEntry::client_order_id)
      .def_readonly("instrument_id", &OrderLogEntry::instrument_id)
      .def_readonly("side", &OrderLogEntry::side)
      .def_readonly("price", &OrderLogEntry::price)
      .def_readonly("size", &OrderLogEntry::size)
      .def_readonly("type", &OrderLogEntry::type)
      .def_readonly("status", &OrderLogEntry::status);

  py::class_<SimulatedLOB>(m, "SimulatedLOB")
      .def_property_readonly("instrument_id", &SimulatedLOB::instrument_id)
      .def("best_price",
           [](const SimulatedLOB &s, Side side) { return s.best_price(side); })
      .def("qty_at", &SimulatedLOB::qty_at)
      .def("mid", [](const SimulatedLOB &s) { return s.mid(); })
      .def("snapshot", [](const SimulatedLOB &s, Side side, std::size_t n) {
        std::vector<SimulatedLOB::Level> out;
        s.snapshot(side, out, n);
        return out;
      });

  py::class_<EngineContext>(m, "EngineContext")
      .def_property_readonly("engine_id", &EngineContext::engine_id)
      .def_property_readonly("now", &EngineContext::now)
      .def("send_limit", &EngineContext::send_limit, py::arg("instrument_id"),
           py::arg("side"), py::arg("price"), py::arg("size"))
      .def("send_market", &EngineContext::send_market, py::arg("instrument_id"),
           py::arg("side"), py::arg("size"))
      .def("cancel", &EngineContext::cancel, py::arg("order_id"))
      .def("book", &EngineContext::book, py::return_value_policy::reference)
      .def("position", &EngineContext::position, py::arg("instrument_id"));

  py::class_<EngineStrategy, PyEngineStrategy>(m, "EngineStrategy")
      .def(py::init<>())
      .def("on_start", &EngineStrategy::on_start)
      .def("on_book_update", &EngineStrategy::on_book_update)
      .def("on_trade", &EngineStrategy::on_trade)
      .def("on_fill", &EngineStrategy::on_fill)
      .def("on_reject", &EngineStrategy::on_reject)
      .def("on_stop", &EngineStrategy::on_stop);

  py::class_<EngineConfig>(m, "EngineConfig")
      .def(py::init<>())
      .def_readwrite("pnl_sample_interval_ns",
                     &EngineConfig::pnl_sample_interval_ns)
      .def_readwrite("max_position", &EngineConfig::max_position);

  py::class_<BacktestConfig>(m, "BacktestConfig")
      .def(py::init<>())
      .def_readwrite("paths", &BacktestConfig::paths)
      .def_readwrite("start_ts", &BacktestConfig::start_ts)
      .def_readwrite("end_ts", &BacktestConfig::end_ts)
      .def_readwrite("instrument_filter", &BacktestConfig::instrument_filter)
      .def_readwrite("engine", &BacktestConfig::engine)
      .def_readwrite("progress_seconds", &BacktestConfig::progress_seconds);

  py::class_<Progress>(m, "Progress")
      .def_readonly("percent", &Progress::percent)
      .def_readonly("last_ts", &Progress::last_ts)
      .def_readonly("events", &Progress::events)
      .def_readonly("pnl", &Progress::pnl)
      .def_readonly("stats", &Progress::stats)
      .def_readonly("stats_by_instrument", &Progress::stats_by_instrument);

  py::class_<BacktestResultData>(m, "BacktestResultData")
      .def_readonly("engine_id", &BacktestResultData::engine_id)
      .def_readonly("first_ts", &BacktestResultData::first_ts)
      .def_readonly("last_ts", &BacktestResultData::last_ts)
      .def_readonly("events", &BacktestResultData::events)
      .def_readonly("final_equity", &BacktestResultData::final_equity)
      .def_readonly("stats", &BacktestResultData::stats)
      .def_readonly("stats_by_instrument",
                    &BacktestResultData::stats_by_instrument)
      .def_readonly("pnl", &BacktestResultData::pnl)
      .def_readonly("fills", &BacktestResultData::fills)
      .def_readonly("order_log", &BacktestResultData::order_log);

  m.def("run_backtest", &run_backtest, py::arg("strategy"), py::arg("config"),
        py::arg("progress") = py::none());
}
