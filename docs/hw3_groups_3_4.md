# HW3 — Groups 3 & 4: Multi-Engine LOB Simulation + Python Strategy API

This builds on the HW1/HW2 ingestion + LOB pipeline (`src/ingestion`, `src/lob`,
`src/dispatch`). Group 3 adds a C++ multi-engine simulation layer (`src/sim`);
Group 4 wraps it in a Python API (`python/cmf_bt`).

## Group 3 — multi-engine LOB simulation

### The model

The historical book is the **basement**: the real market reconstructed from L3
replay. Each engine adds its own orders and its own market impact on top, and
sees *the basement plus its own orders, minus the liquidity it already
consumed* — as if it were the only participant.

```
        shared, read-only                 private per engine
   ┌────────────────────────┐        ┌───────────────────────────┐
   │      HistoricalLOB      │  <───  │ SimulatedLOB = basement +  │
   │ (LimitOrderBook +       │        │   EngineView overlay       │
   │  OrderIndex, L3 replay) │  <───  │ (own orders + consumed)    │
   └────────────────────────┘        └───────────────────────────┘
              ▲                              ▲            ▲
              │ apply(MarketDataEvent)       │            │
        replay thread                   engine 0      engine 1 ...
```

| Class | Role |
|-------|------|
| `HistoricalLOB` | Basement for one instrument. Wraps the HW1/HW2 `LimitOrderBook` + `OrderIndex`, applies events through the shared `BookApplier`, tracks BBO version and last trade. |
| `EngineView` | One engine's overlay: its resting orders (a `PriceLadder`) and the basement liquidity it has consumed (another `PriceLadder`). |
| `SimulatedLOB` | A read-only view that merges `HistoricalLOB` and an `EngineView` on demand. Owns no full-book storage. |
| `FillModel` / `TouchFillModel` | Policy for how orders execute. Default fills at touch. Swappable by composition. |
| `MultiEngineSimulator` | Drives N engines over the shared basements; routes market data, matches orders, tracks positions/PnL/stats/logs. |

### Key design question — store N copies or diffs?

**Diffs.** There is exactly one `HistoricalLOB` per instrument, shared by every
engine. Each engine stores only its own deltas in an `EngineView` (resting
orders + consumed levels), which are tiny compared to a full options book. The
book an engine sees is computed lazily:

```
qty_seen(side, price) = max(0, hist_qty - consumed) + own_qty
```

N engines cost `one basement + N small overlays`, not N full books.

### Concurrency — N engines, no data races

The replay is single-threaded and deterministic. Within a step the basement is
written once (replay), then engines are notified while it is **read-only**. Each
`EngineView`/position/order map belongs to a single engine, so engines never
touch shared mutable state. `test/SimConcurrencyTest.cpp` runs N threads reading
one `const HistoricalLOB` while each mutates its own `EngineView`; build with
`-fsanitize=thread` to verify.

### Fill model

`TouchFillModel` (the HW3 "simplest model"):
- a marketable order takes liquidity from the opposite side, best price first,
  recording consumption on the `EngineView`;
- a non-marketable limit rests in the overlay;
- a resting order fills passively once the historical market trades to or
  through its price, at the touch.

Richer models (queue position, partial impact, latency) implement `FillModel`
and are injected without touching the simulator.

## Group 4 — Python strategy API

`python/cmf_bt` wraps the C++ core (pybind11). A Python `Strategy` *is* a C++
`EngineStrategy` whose virtual callbacks are overridden in Python.

```python
import cmf_bt
from cmf_bt import Strategy, Backtest, Side

class MyStrat(Strategy):
    def on_book_update(self, ctx, u):
        if u.ask and ctx.position(u.instrument_id) == 0:
            ctx.send_limit(u.instrument_id, Side.Buy, u.ask, 1)
    def on_fill(self, ctx, fill): ...

bt = Backtest(pnl_sample_seconds=1.0, progress_seconds=30.0)
result = bt.run(MyStrat(), data_path, date_range=("2026-03-09", "2026-03-10"),
                instrument=34112, risk={"max_position": 10}, progress=print)

result.pnl_series      # pd.Series, equity over time
result.fills_df        # pd.DataFrame of fills
result.order_log_df    # pd.DataFrame of order events
result.plot()          # cumulative PnL + fills
```

Callbacks: `on_start`, `on_book_update`, `on_trade`, `on_fill`, `on_reject`,
`on_stop`. The `progress` callback fires on a wall-clock cadence with percent
done, last processed timestamp, current PnL and order stats (total and per
instrument).

## Building

```
# C++ core + tests
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build

# Python bindings (needs pybind11 in the active venv)
uv venv --python 3.12 .venv-hw3 && . .venv-hw3/bin/activate
uv pip install pybind11 numpy pandas matplotlib pytest
cmake -B build -S . -DBUILD_PYTHON=ON \
      -DPython3_EXECUTABLE=$(which python) \
      -Dpybind11_DIR=$(python -c "import pybind11; print(pybind11.get_cmake_dir())")
cmake --build build -j --target _cmf_bt
PYTHONPATH=python pytest python/tests
```

## Interface document (shared types — `src/sim/SimTypes.hpp`)

| Concept | Type |
|---------|------|
| `instrument_id` | `std::uint32_t` (matches `MarketDataEvent::instrument_id`) |
| `timestamp_ns` | `std::int64_t` (`cmf::NanoTime`) |
| `price` | scaled integer `std::int64_t` (`price * 1e9`) |
| `order_id` | `std::uint64_t` |
| `tradingEngine_id` | `std::uint32_t` (`cmf::sim::EngineId`) |
