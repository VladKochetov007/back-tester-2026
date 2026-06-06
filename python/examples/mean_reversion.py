"""
Mean-reversion example for the CMF backtester (HW3 group 4).

The strategy tracks an EWMA of the mid price and leans against deviations: when
the mid is cheap relative to its average it buys, when rich it sells, holding at
most `max_position` lots. Orders cross the spread (marketable limits) so fills
are immediate against the historical book.

Run:
    python examples/mean_reversion.py <data_path> --instrument 34112
"""

from __future__ import annotations

import argparse

from cmf_bt import Backtest, Side, Strategy, unscale


class MeanReversion(Strategy):
    def __init__(
        self, alpha: float = 0.02, threshold: float = 0.0005, max_position: int = 5
    ):
        super().__init__()
        self.alpha = alpha
        self.threshold = threshold
        self.max_position = max_position
        self.ewma: float | None = None

    def on_book_update(self, ctx, u) -> None:
        if u.bid == 0 or u.ask == 0:
            return
        mid = unscale((u.bid + u.ask) // 2)
        self.ewma = (
            mid
            if self.ewma is None
            else (1 - self.alpha) * self.ewma + self.alpha * mid
        )

        deviation = (mid - self.ewma) / self.ewma
        pos = ctx.position(u.instrument_id)

        if deviation < -self.threshold and pos < self.max_position:
            ctx.send_limit(u.instrument_id, Side.Buy, u.ask, 1)  # cheap -> buy
        elif deviation > self.threshold and pos > -self.max_position:
            ctx.send_limit(u.instrument_id, Side.Sell, u.bid, 1)  # rich -> sell


def main() -> None:
    p = argparse.ArgumentParser(description="Mean-reversion backtest example")
    p.add_argument("data_path", help="file or folder of *.mbo.json")
    p.add_argument(
        "--instrument", type=int, default=0, help="instrument_id filter (0 = all)"
    )
    p.add_argument("--start", default=None, help="range start (date/timestamp)")
    p.add_argument("--end", default=None, help="range end (date/timestamp)")
    p.add_argument("--max-position", type=int, default=5)
    p.add_argument(
        "--plot", default=None, help="save the performance chart to this path"
    )
    args = p.parse_args()

    date_range = (args.start, args.end) if (args.start or args.end) else None

    def progress(info: dict) -> None:
        print(
            f"  {info['percent'] * 100:5.1f}%  events={info['events']:>10}  "
            f"pnl={info['pnl']:.6f}  {info['stats']}"
        )

    strat = MeanReversion(max_position=args.max_position)
    bt = Backtest(pnl_sample_seconds=1.0, progress_seconds=2.0)
    result = bt.run(
        strat,
        args.data_path,
        date_range=date_range,
        instrument=args.instrument or None,
        risk={"max_position": args.max_position},
        progress=progress,
    )

    print("\n=== summary ===")
    print(f"events processed : {result.events}")
    print(f"final equity     : {result.final_equity:.6f}")
    print(f"order stats      : {result.stats['total']}")
    print(f"fills            : {len(result.fills_df)}")
    print(f"pnl samples      : {len(result.pnl_series)}")
    if not result.fills_df.empty:
        print(result.fills_df.head(8).to_string(index=False))

    if args.plot:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        result.plot()
        plt.tight_layout()
        plt.savefig(args.plot, dpi=110)
        print(f"chart saved to {args.plot}")


if __name__ == "__main__":
    main()
