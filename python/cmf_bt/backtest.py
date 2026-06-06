from __future__ import annotations

from collections.abc import Callable, Sequence
from typing import Any

import pandas as pd

from cmf_bt._cmf_bt import BacktestConfig, EngineConfig, Progress, run_backtest
from cmf_bt.result import Result, _stats_dict
from cmf_bt.strategy import Strategy


def _to_ns(value: Any) -> int:
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    return int(pd.Timestamp(value).value)


def _progress_dict(p: Progress) -> dict:
    return {
        "percent": p.percent,
        "last_ts": p.last_ts,
        "events": p.events,
        "pnl": p.pnl,
        "stats": _stats_dict(p.stats),
        "by_instrument": {
            int(k): _stats_dict(v) for k, v in p.stats_by_instrument.items()
        },
    }


class Backtest:
    """
    Runs a Strategy over historical L3 data.

        bt = Backtest(pnl_sample_seconds=1.0, progress_seconds=30.0)
        result = bt.run(strategy, data_path, date_range=("2026-03-09", "2026-03-10"))

    `data_path` is a file, a folder, or a list of either. `date_range` is an
    optional (start, end) pair of anything pandas.Timestamp accepts, or epoch-ns
    ints. `progress` is an optional callback invoked every `progress_seconds`
    with a dict (percent / last_ts / events / pnl / stats / by_instrument).
    """

    def __init__(
        self, pnl_sample_seconds: float = 1.0, progress_seconds: float = 30.0
    ) -> None:
        self.pnl_sample_seconds = pnl_sample_seconds
        self.progress_seconds = progress_seconds

    def run(
        self,
        strategy: Strategy,
        data_path: str | Sequence[str],
        date_range: tuple[Any, Any] | None = None,
        instrument: int | None = None,
        risk: dict | None = None,
        progress: Callable[[dict], None] | None = None,
    ) -> Result:
        cfg = BacktestConfig()
        paths = data_path if isinstance(data_path, (list, tuple)) else [data_path]
        cfg.paths = [str(p) for p in paths]
        if date_range is not None:
            cfg.start_ts = _to_ns(date_range[0])
            cfg.end_ts = _to_ns(date_range[1])
        cfg.instrument_filter = int(instrument or 0)
        cfg.progress_seconds = float(self.progress_seconds)

        engine = EngineConfig()
        engine.pnl_sample_interval_ns = int(self.pnl_sample_seconds * 1e9)
        if risk and "max_position" in risk:
            engine.max_position = int(risk["max_position"])
        cfg.engine = engine

        cb = (lambda p: progress(_progress_dict(p))) if progress is not None else None
        data = run_backtest(strategy, cfg, cb)
        return Result(data)
