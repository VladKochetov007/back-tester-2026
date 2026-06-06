"""
Generates a synthetic L3 (MBO) stream in the Databento line format used by the
ingestion layer. The mid follows an Ornstein-Uhlenbeck process so a
mean-reversion strategy has something to lean against. Handy for demos and the
example notebook when the real data is not on hand.
"""

from __future__ import annotations

from pathlib import Path


def _line(
    inst: int,
    action: str,
    side: str,
    px: float | None,
    size: int,
    oid: int,
    seq: int,
    ns: int,
) -> str:
    price = "null" if px is None else f'"{px:.9f}"'
    sec, sub = divmod(ns, 1_000_000_000)
    ts = f"2026-03-09T07:{52 + sec // 60:02d}:{(41 + sec) % 60:02d}.{sub:09d}Z"
    return (
        f'{{"ts_recv":"{ts}","hd":{{"ts_event":"{ts}","rtype":160,'
        f'"publisher_id":101,"instrument_id":{inst}}},"action":"{action}",'
        f'"side":"{side}","price":{price},"size":{size},"channel_id":1,'
        f'"order_id":"{oid}","flags":0,"ts_in_delta":0,"sequence":{seq},"symbol":"SYNTH"}}'
    )


def generate(
    path: str | Path,
    *,
    instrument: int = 1000,
    steps: int = 3000,
    mid0: float = 100.0,
    kappa: float = 0.02,
    sigma: float = 0.05,
    tick: float = 0.01,
    seed: int = 7,
) -> str:
    """Writes an OU mid quoted one tick wide; returns the path as a string."""
    rng = _Lcg(seed)
    mid = mid0
    lines: list[str] = []
    seq = 0
    ns = 100_000_000
    bid_oid = ask_oid = 0
    last_bid = last_ask = None

    def emit(action, side, px, size, oid):
        nonlocal seq, ns
        seq += 1
        ns += 1_000_000
        lines.append(_line(instrument, action, side, px, size, oid, seq, ns))

    for _ in range(steps):
        mid += kappa * (mid0 - mid) + sigma * (rng.next() - 0.5)
        bid = round(mid - tick, 9)
        ask = round(mid + tick, 9)
        if last_bid is not None:
            emit("C", "B", last_bid, 20, bid_oid)
            emit("C", "A", last_ask, 20, ask_oid)
        bid_oid += 2
        ask_oid = bid_oid + 1
        emit("A", "B", bid, 20, bid_oid)
        emit("A", "A", ask, 20, ask_oid)
        last_bid, last_ask = bid, ask

    Path(path).write_text("\n".join(lines) + "\n")
    return str(path)


class _Lcg:
    """Tiny deterministic RNG so the demo needs no numpy and is reproducible."""

    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFF

    def next(self) -> float:
        self.state = (1664525 * self.state + 1013904223) & 0xFFFFFFFF
        return self.state / 0xFFFFFFFF


if __name__ == "__main__":
    out = generate("/tmp/synthetic.mbo.json")
    print("wrote", out, "lines:", sum(1 for _ in open(out)))
    print("sample:", open(out).readline().strip()[:120], "...")
