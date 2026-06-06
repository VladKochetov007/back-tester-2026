#include "sim/MultiEngineSimulator.hpp"

#include "lob/LimitOrderBook.hpp"

#include <utility>
#include <vector>

namespace cmf::sim {

class MultiEngineSimulator::Ctx final : public EngineContext {
public:
  Ctx(MultiEngineSimulator *sim, std::size_t idx, EngineId id) noexcept
      : sim_(sim), idx_(idx), id_(id) {}

  void set_ts(NanoTime ts) noexcept { ts_ = ts; }

  EngineId engine_id() const override { return id_; }
  NanoTime now() const override { return ts_; }

  OrderId send_limit(InstrumentId inst, Side side, ScaledPrice price,
                     Qty size) override {
    return sim_->ctx_send(idx_, inst, side, price, size, OrderType::Limit, ts_);
  }
  OrderId send_market(InstrumentId inst, Side side, Qty size) override {
    return sim_->ctx_send(idx_, inst, side, PRICE_NONE, size, OrderType::Market,
                          ts_);
  }
  bool cancel(OrderId order_id) override {
    return sim_->ctx_cancel(idx_, order_id);
  }

  const SimulatedLOB *book(InstrumentId inst) const override {
    return sim_->book(id_, inst);
  }
  Qty position(InstrumentId inst) const override {
    return sim_->position(id_, inst);
  }

private:
  MultiEngineSimulator *sim_;
  std::size_t idx_;
  EngineId id_;
  NanoTime ts_ = 0;
};

MultiEngineSimulator::MultiEngineSimulator() = default;
MultiEngineSimulator::~MultiEngineSimulator() = default;

EngineId MultiEngineSimulator::add_engine(EngineStrategy &strategy,
                                          FillModel &fill, EngineConfig cfg) {
  const EngineId id = static_cast<EngineId>(engines_.size());
  auto eng = std::make_unique<Engine>();
  eng->id = id;
  eng->strategy = &strategy;
  eng->fill = &fill;
  eng->cfg = cfg;
  engines_.push_back(std::move(eng));
  contexts_.push_back(std::make_unique<Ctx>(this, engines_.size() - 1, id));
  return id;
}

void MultiEngineSimulator::start() {
  if (started_)
    return;
  started_ = true;
  for (std::size_t idx = 0; idx < engines_.size(); ++idx) {
    engines_[idx]->next_pnl_sample = 0;
    contexts_[idx]->set_ts(0);
    engines_[idx]->strategy->on_start(*contexts_[idx]);
  }
}

HistoricalLOB &MultiEngineSimulator::get_basement(InstrumentId id) {
  return basements_.try_emplace(id, id).first->second;
}

void MultiEngineSimulator::ensure_engine_instrument(Engine &eng,
                                                    InstrumentId id) {
  auto [view_it, inserted] = eng.views.try_emplace(id, id);
  if (!inserted)
    return;
  HistoricalLOB &base = get_basement(id);
  eng.sims.try_emplace(id, base, view_it->second);
  eng.positions.try_emplace(id);
}

void MultiEngineSimulator::step(const MarketDataEvent &e) {
  if (!started_)
    start();
  const InstrumentId inst = e.instrument_id;
  if (inst == 0)
    return;

  HistoricalLOB &base = get_basement(inst);
  const bool bbo_moved = base.apply(e);
  ++events_;
  if (first_ts_ == 0)
    first_ts_ = e.ts_recv;
  last_ts_ = e.ts_recv;

  ScaledPrice bp = PRICE_NONE, ap = PRICE_NONE;
  LimitOrderBook::AggQty bq = 0, aq = 0;
  const bool have_bid = base.best(Side::Buy, bp, bq);
  const bool have_ask = base.best(Side::Sell, ap, aq);
  if (have_bid && have_ask)
    marks_[inst] = (bp + ap) / 2;
  else if (have_bid)
    marks_[inst] = bp;
  else if (have_ask)
    marks_[inst] = ap;

  BookUpdate bu;
  if (bbo_moved) {
    bu.instrument_id = inst;
    bu.ts = e.ts_recv;
    bu.seq = e.sequence;
    if (have_bid) {
      bu.bid = bp;
      bu.bid_size = static_cast<Qty>(bq);
    }
    if (have_ask) {
      bu.ask = ap;
      bu.ask_size = static_cast<Qty>(aq);
    }
  }

  const bool is_trade = e.action == 'T';
  TradeTick tt;
  if (is_trade) {
    tt.instrument_id = inst;
    tt.ts = e.ts_recv;
    tt.seq = e.sequence;
    tt.price = LimitOrderBook::scale(e.price);
    tt.size = static_cast<Qty>(e.size);
    tt.aggressor = side_from_char(e.side);
  }

  for (std::size_t idx = 0; idx < engines_.size(); ++idx) {
    Engine &eng = *engines_[idx];
    ensure_engine_instrument(eng, inst);
    contexts_[idx]->set_ts(e.ts_recv);
    notify_engine(eng, idx, e, bbo_moved, bu, is_trade, tt);
    drain(eng, idx, e.ts_recv);

    if (e.ts_recv >= eng.next_pnl_sample) {
      sample_pnl(eng, e.ts_recv);
      const NanoTime iv = eng.cfg.pnl_sample_interval_ns > 0
                              ? eng.cfg.pnl_sample_interval_ns
                              : 1;
      eng.next_pnl_sample = e.ts_recv + iv;
    }
  }
}

void MultiEngineSimulator::notify_engine(Engine &eng, std::size_t idx,
                                         const MarketDataEvent &e,
                                         bool bbo_moved, const BookUpdate &bu,
                                         bool is_trade, const TradeTick &tt) {
  EngineView &view = eng.views.at(e.instrument_id);
  SimulatedLOB &sim = eng.sims.at(e.instrument_id);

  std::vector<RawFill> raws;
  eng.fill->on_market_event(sim, view, e, raws);
  for (const RawFill &rf : raws)
    apply_fill(eng, idx, enrich_fill(eng, rf, e.ts_recv));

  if (is_trade)
    eng.strategy->on_trade(*contexts_[idx], tt);
  if (bbo_moved)
    eng.strategy->on_book_update(*contexts_[idx], bu);
}

OrderFill MultiEngineSimulator::enrich_fill(Engine &eng, const RawFill &rf,
                                            NanoTime ts) const {
  OrderFill f;
  f.engine_id = eng.id;
  f.order_id = rf.order_id;
  f.side = rf.side;
  f.price = rf.price;
  f.size = rf.size;
  f.maker = rf.maker;
  f.ts = ts;
  auto it = eng.live.find(rf.order_id);
  if (it != eng.live.end()) {
    f.client_order_id = it->second.client_order_id;
    f.instrument_id = it->second.instrument_id;
  }
  return f;
}

void MultiEngineSimulator::apply_fill(Engine &eng, std::size_t idx,
                                      const OrderFill &f) {
  eng.positions[f.instrument_id].on_fill(f.side, f.price, f.size);

  ++eng.total_stats.filled;
  eng.total_stats.filled_qty += f.size;
  auto &bi = eng.by_instrument[f.instrument_id];
  ++bi.filled;
  bi.filled_qty += f.size;

  eng.fills_log.push_back(f);

  auto it = eng.live.find(f.order_id);
  if (it != eng.live.end()) {
    it->second.remaining -= f.size;
    if (it->second.remaining <= 0) {
      record_log(eng, it->second, f.ts, OrderStatus::Filled);
      eng.live.erase(it);
    } else {
      record_log(eng, it->second, f.ts, OrderStatus::PartiallyFilled);
    }
  }
  eng.strategy->on_fill(*contexts_[idx], f);
}

void MultiEngineSimulator::drain(Engine &eng, std::size_t idx, NanoTime ts) {
  constexpr std::size_t CAP = 1'000'000;
  std::size_t guard = 0;
  while (!eng.pending_cancel.empty() || !eng.pending_new.empty()) {
    if (++guard > CAP)
      break;
    if (!eng.pending_cancel.empty()) {
      std::vector<OrderId> cancels;
      cancels.swap(eng.pending_cancel);
      for (OrderId oid : cancels)
        do_cancel(eng, oid, ts);
    }
    if (!eng.pending_new.empty()) {
      std::vector<PendingNew> news;
      news.swap(eng.pending_new);
      for (const PendingNew &pn : news)
        do_submit(eng, idx, pn, ts);
    }
  }
}

void MultiEngineSimulator::do_submit(Engine &eng, std::size_t idx,
                                     const PendingNew &pn, NanoTime ts) {
  const NewOrder &o = pn.order;
  const OrderId oid = pn.order_id;

  auto live_it = eng.live.find(oid);
  if (live_it == eng.live.end())
    return; // cancelled before it was matched

  ensure_engine_instrument(eng, o.instrument_id);
  EngineView &view = eng.views.at(o.instrument_id);
  SimulatedLOB &sim = eng.sims.at(o.instrument_id);

  if (eng.cfg.max_position > 0) {
    const Qty cur = eng.positions[o.instrument_id].qty;
    const Qty projected = cur + static_cast<Qty>(side_sign(o.side)) * o.size;
    if (projected > eng.cfg.max_position || projected < -eng.cfg.max_position) {
      ++eng.total_stats.rejected;
      ++eng.by_instrument[o.instrument_id].rejected;
      record_log(eng, live_it->second, ts, OrderStatus::Rejected);
      const OrderReject r{eng.id, o.client_order_id, o.instrument_id,
                          RejectReason::RiskLimit, ts};
      eng.live.erase(live_it);
      eng.strategy->on_reject(*contexts_[idx], r);
      return;
    }
  }

  FillOutcome outcome = eng.fill->on_submit(sim, view, oid, o);
  if (outcome.rejected) {
    ++eng.total_stats.rejected;
    ++eng.by_instrument[o.instrument_id].rejected;
    record_log(eng, live_it->second, ts, OrderStatus::Rejected);
    const OrderReject r{eng.id, o.client_order_id, o.instrument_id,
                        outcome.reason, ts};
    eng.live.erase(live_it);
    eng.strategy->on_reject(*contexts_[idx], r);
    return;
  }

  for (const RawFill &rf : outcome.fills)
    apply_fill(eng, idx, enrich_fill(eng, rf, ts));

  auto it = eng.live.find(oid);
  if (it == eng.live.end())
    return; // fully filled and erased by apply_fill
  if (outcome.resting > 0) {
    record_log(eng, it->second, ts, OrderStatus::Acked);
  } else {
    record_log(eng, it->second, ts,
               OrderStatus::Cancelled); // marketable remainder, IOC
    eng.live.erase(it);
  }
}

void MultiEngineSimulator::do_cancel(Engine &eng, OrderId order_id,
                                     NanoTime ts) {
  auto it = eng.live.find(order_id);
  if (it == eng.live.end())
    return;
  EngineView &view = eng.views.at(it->second.instrument_id);
  view.cancel_resting(order_id);
  ++eng.total_stats.cancelled;
  ++eng.by_instrument[it->second.instrument_id].cancelled;
  record_log(eng, it->second, ts, OrderStatus::Cancelled);
  eng.live.erase(it);
}

OrderId MultiEngineSimulator::ctx_send(std::size_t idx, InstrumentId inst,
                                       Side side, ScaledPrice price, Qty size,
                                       OrderType type, NanoTime ts) {
  if (side == Side::None || size <= 0)
    return 0;
  Engine &eng = *engines_[idx];
  const OrderId oid = next_order_id_++;
  const ClOrdId cl = ++eng.cl_seq;

  eng.live.emplace(
      oid, EngineOrder{oid, eng.id, cl, inst, side, price, size, size, type});
  ++eng.total_stats.sent;
  ++eng.by_instrument[inst].sent;
  record_log(eng, eng.live.at(oid), ts, OrderStatus::New);

  eng.pending_new.push_back(
      PendingNew{oid, NewOrder{eng.id, cl, inst, side, price, size, type, ts}});
  return oid;
}

bool MultiEngineSimulator::ctx_cancel(std::size_t idx, OrderId order_id) {
  Engine &eng = *engines_[idx];
  if (eng.live.find(order_id) == eng.live.end())
    return false;
  eng.pending_cancel.push_back(order_id);
  return true;
}

void MultiEngineSimulator::record_log(Engine &eng, const EngineOrder &o,
                                      NanoTime ts, OrderStatus status) {
  eng.order_log.push_back(
      OrderLogEntry{ts, o.engine_id, o.order_id, o.client_order_id,
                    o.instrument_id, o.side, o.price, o.size, o.type, status});
}

double MultiEngineSimulator::mark_equity(const Engine &eng) const {
  double eq = 0.0;
  for (const auto &[inst, pos] : eng.positions) {
    const auto mit = marks_.find(inst);
    eq += (mit == marks_.end()) ? pos.cash : pos.equity(mit->second);
  }
  return eq;
}

void MultiEngineSimulator::sample_pnl(Engine &eng, NanoTime ts) {
  eng.pnl.emplace_back(ts, mark_equity(eng));
}

void MultiEngineSimulator::finish() {
  for (std::size_t idx = 0; idx < engines_.size(); ++idx) {
    contexts_[idx]->set_ts(last_ts_);
    engines_[idx]->strategy->on_stop(*contexts_[idx]);
    sample_pnl(*engines_[idx], last_ts_);
  }
}

MultiEngineSimulator::Engine &MultiEngineSimulator::eng_at(EngineId id) {
  return *engines_.at(id);
}
const MultiEngineSimulator::Engine &
MultiEngineSimulator::eng_at(EngineId id) const {
  return *engines_.at(id);
}

const HistoricalLOB *
MultiEngineSimulator::basement(InstrumentId id) const noexcept {
  auto it = basements_.find(id);
  return it == basements_.end() ? nullptr : &it->second;
}

const SimulatedLOB *MultiEngineSimulator::book(EngineId engine,
                                               InstrumentId id) const noexcept {
  if (engine >= engines_.size())
    return nullptr;
  const Engine &eng = *engines_[engine];
  auto it = eng.sims.find(id);
  return it == eng.sims.end() ? nullptr : &it->second;
}

const std::vector<OrderFill> &
MultiEngineSimulator::fills(EngineId engine) const {
  return eng_at(engine).fills_log;
}
const std::vector<OrderLogEntry> &
MultiEngineSimulator::order_log(EngineId engine) const {
  return eng_at(engine).order_log;
}
const std::vector<std::pair<NanoTime, double>> &
MultiEngineSimulator::pnl_curve(EngineId engine) const {
  return eng_at(engine).pnl;
}
const OrderStats &MultiEngineSimulator::stats(EngineId engine) const {
  return eng_at(engine).total_stats;
}
const std::unordered_map<InstrumentId, OrderStats> &
MultiEngineSimulator::stats_by_instrument(EngineId engine) const {
  return eng_at(engine).by_instrument;
}
double MultiEngineSimulator::final_equity(EngineId engine) const {
  return mark_equity(eng_at(engine));
}
Qty MultiEngineSimulator::position(EngineId engine, InstrumentId id) const {
  const Engine &eng = eng_at(engine);
  auto it = eng.positions.find(id);
  return it == eng.positions.end() ? 0 : it->second.qty;
}
ProgressInfo MultiEngineSimulator::progress(EngineId engine) const {
  const Engine &eng = eng_at(engine);
  ProgressInfo p;
  p.last_ts = last_ts_;
  p.events = events_;
  p.pnl = mark_equity(eng);
  p.stats = eng.total_stats;
  p.stats_by_instrument = eng.by_instrument;
  return p;
}

} // namespace cmf::sim
