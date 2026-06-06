import pandas as pd
import pytest

import cmf_bt
from cmf_bt import Backtest, Side, Strategy

INST = 1000
NOISE = 2000


def _line(inst, action, side, px, size, oid, seq, ns):
    price = "null" if px is None else f'"{px:.9f}"'
    ts = f"2026-03-09T07:52:41.{ns:09d}Z"
    return (
        f'{{"ts_recv":"{ts}","hd":{{"ts_event":"{ts}","rtype":160,'
        f'"publisher_id":101,"instrument_id":{inst}}},"action":"{action}",'
        f'"side":"{side}","price":{price},"size":{size},"channel_id":1,'
        f'"order_id":"{oid}","flags":0,"ts_in_delta":0,"sequence":{seq},"symbol":"SYNTH"}}'
    )


@pytest.fixture(scope="session")
def data_path(tmp_path_factory):
    """Deterministic synthetic L3 stream in the real Databento MBO line format."""
    events = [
        (INST, "A", "B", 99.0, 10, 11),  # best bid appears
        (NOISE, "A", "B", 50.0, 5, 90),  # unrelated instrument (filtered out)
        (INST, "A", "A", 101.0, 10, 12),  # best ask -> two-sided book
        (INST, "A", "A", 102.0, 5, 13),  # depth behind ask (no BBO move)
        (INST, "A", "B", 98.0, 5, 14),  # depth behind bid (no BBO move)
        (INST, "A", "B", 99.0, 5, 15),  # adds to best bid -> BBO qty moves
        (INST, "T", "A", 101.0, 2, 0),  # a trade print
        (INST, "A", "A", 101.0, 4, 16),  # refills the ask
        (INST, "C", "A", 102.0, 5, 13),  # cancel behind-ask depth
    ]
    lines = [
        _line(inst, a, s, px, sz, oid, seq, 100_000_000 + seq * 1000)
        for seq, (inst, a, s, px, sz, oid) in enumerate(events, start=1)
    ]
    path = tmp_path_factory.mktemp("l3") / "synthetic.mbo.json"
    path.write_text("\n".join(lines) + "\n")
    return str(path)


class Crosser(Strategy):
    """Buys one lot on the first two-sided book, then sells it back."""

    def __init__(self):
        super().__init__()
        self.bought = False
        self.sold = False
        self.fills = 0

    def on_book_update(self, ctx, u):
        if u.bid == 0 or u.ask == 0:
            return
        pos = ctx.position(u.instrument_id)
        if not self.bought:
            ctx.send_limit(u.instrument_id, Side.Buy, u.ask, 1)
            self.bought = True
        elif pos > 0 and not self.sold:
            ctx.send_limit(u.instrument_id, Side.Sell, u.bid, pos)
            self.sold = True

    def on_fill(self, ctx, f):
        self.fills += 1


def run(strategy, data_path, **kw):
    bt = Backtest(pnl_sample_seconds=0.001, progress_seconds=1e9)
    return bt.run(strategy, data_path, instrument=INST, **kw)


def test_module_surface():
    assert cmf_bt.PRICE_SCALE == 1_000_000_000.0
    assert cmf_bt.scale(1.5) == 1_500_000_000
    assert cmf_bt.unscale(1_500_000_000) == pytest.approx(1.5)


def test_backtest_processes_events_and_fills(data_path):
    strat = Crosser()
    result = run(strat, data_path)
    assert result.events > 0
    assert strat.fills >= 1
    assert result.stats["total"]["sent"] >= 1
    assert result.stats["total"]["filled"] >= 1


def test_instrument_filter_excludes_noise(data_path):
    result = run(Crosser(), data_path)
    fills = result.fills_df
    assert not fills.empty
    assert (fills["instrument_id"] == INST).all()


def test_result_frames_shapes(data_path):
    result = run(Crosser(), data_path)

    assert isinstance(result.pnl_series, pd.Series)
    assert len(result.pnl_series) > 0

    fills = result.fills_df
    assert list(fills.columns) == [
        "ts",
        "instrument_id",
        "order_id",
        "side",
        "price",
        "size",
        "maker",
    ]
    assert not fills.empty
    assert fills["price"].gt(0).all()

    log = result.order_log_df
    assert not log.empty
    assert {"New", "Filled"}.issubset(set(log["status"]))


def test_progress_callback_fires(data_path):
    seen = []
    bt = Backtest(progress_seconds=0.0)
    bt.run(Crosser(), data_path, instrument=INST, progress=seen.append)
    assert seen
    assert "percent" in seen[-1] and "stats" in seen[-1]


def test_risk_limit_blocks_position(data_path):
    class Greedy(Strategy):
        def on_book_update(self, ctx, u):
            if u.ask:
                ctx.send_limit(u.instrument_id, Side.Buy, u.ask, 10)

    result = run(Greedy(), data_path, risk={"max_position": 3})
    assert result.stats["total"]["rejected"] >= 1
    assert result.fills_df.empty  # size-10 orders never fit a cap of 3
