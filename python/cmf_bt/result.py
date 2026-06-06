from __future__ import annotations

import pandas as pd

from cmf_bt._cmf_bt import BacktestResultData, Side, unscale

_SIDE = {Side.Buy: "B", Side.Sell: "S", Side.Nil: "N"}


def _stats_dict(s) -> dict:
    return {
        "sent": s.sent,
        "filled": s.filled,
        "cancelled": s.cancelled,
        "rejected": s.rejected,
        "filled_qty": s.filled_qty,
    }


class Result:
    """Pandas-friendly view over one engine's backtest output."""

    def __init__(self, data: BacktestResultData) -> None:
        self._d = data

    @property
    def engine_id(self) -> int:
        return self._d.engine_id

    @property
    def events(self) -> int:
        return self._d.events

    @property
    def final_equity(self) -> float:
        return self._d.final_equity

    @property
    def stats(self) -> dict:
        return {
            "total": _stats_dict(self._d.stats),
            "by_instrument": {
                int(k): _stats_dict(v) for k, v in self._d.stats_by_instrument.items()
            },
        }

    @property
    def pnl_series(self) -> pd.Series:
        points = self._d.pnl
        if not points:
            return pd.Series(dtype=float, name="pnl")
        index = pd.to_datetime([t for t, _ in points], unit="ns", utc=True)
        return pd.Series([v for _, v in points], index=index, name="pnl")

    @property
    def fills_df(self) -> pd.DataFrame:
        rows = [
            {
                "ts": f.ts,
                "instrument_id": f.instrument_id,
                "order_id": f.order_id,
                "side": _SIDE.get(f.side, "?"),
                "price": unscale(f.price),
                "size": f.size,
                "maker": f.maker,
            }
            for f in self._d.fills
        ]
        df = pd.DataFrame(
            rows,
            columns=[
                "ts",
                "instrument_id",
                "order_id",
                "side",
                "price",
                "size",
                "maker",
            ],
        )
        if not df.empty:
            df["ts"] = pd.to_datetime(df["ts"], unit="ns", utc=True)
        return df

    @property
    def order_log_df(self) -> pd.DataFrame:
        rows = [
            {
                "ts": e.ts,
                "order_id": e.order_id,
                "client_order_id": e.client_order_id,
                "instrument_id": e.instrument_id,
                "side": _SIDE.get(e.side, "?"),
                "price": unscale(e.price),
                "size": e.size,
                "status": str(e.status).split(".")[-1],
            }
            for e in self._d.order_log
        ]
        cols = [
            "ts",
            "order_id",
            "client_order_id",
            "instrument_id",
            "side",
            "price",
            "size",
            "status",
        ]
        df = pd.DataFrame(rows, columns=cols)
        if not df.empty:
            df["ts"] = pd.to_datetime(df["ts"], unit="ns", utc=True)
        return df

    def plot(self, axes=None):
        """Cumulative PnL with fill prices over time (bonus chart)."""
        import matplotlib.pyplot as plt

        if axes is None:
            _, axes = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
        pnl_ax, px_ax = axes

        pnl = self.pnl_series
        pnl_ax.plot(pnl.index, pnl.values, color="tab:blue", lw=1.2)
        pnl_ax.set_ylabel("equity (PnL)")
        pnl_ax.grid(True, alpha=0.3)
        pnl_ax.set_title(
            f"engine {self.engine_id}: final equity = {self.final_equity:.4f}"
        )

        fills = self.fills_df
        if not fills.empty:
            for side, color in (("B", "tab:green"), ("S", "tab:red")):
                s = fills[fills["side"] == side]
                px_ax.scatter(s["ts"], s["price"], s=10, c=color, label=f"{side} fills")
            px_ax.legend(loc="best")
        px_ax.set_ylabel("fill price")
        px_ax.grid(True, alpha=0.3)
        return axes
