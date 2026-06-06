# cmf_bt — Python strategy API (HW3 group 4)

Thin pybind11 layer over the C++ multi-engine simulator (`src/sim`). Write a
strategy in Python, run it over historical L3 data, get pandas results.

## Build

```bash
uv venv --python 3.12 .venv-hw3 && . .venv-hw3/bin/activate
uv pip install pybind11 numpy pandas matplotlib pytest

cmake -B build -S . -DBUILD_PYTHON=ON \
      -DPython3_EXECUTABLE=$(which python) \
      -Dpybind11_DIR=$(python -c "import pybind11; print(pybind11.get_cmake_dir())")
cmake --build build -j --target _cmf_bt        # builds python/cmf_bt/_cmf_bt*.so
```

Then put `python/` on the path (`export PYTHONPATH=python`) or run from `python/`.

## Use

```python
import cmf_bt
from cmf_bt import Strategy, Backtest, Side, unscale

class MeanReversion(Strategy):
    def on_book_update(self, ctx, u):
        if u.bid == 0 or u.ask == 0:
            return
        pos = ctx.position(u.instrument_id)
        # ... decide, then act through ctx:
        # ctx.send_limit(u.instrument_id, Side.Buy, u.ask, 1)
        # ctx.send_market(u.instrument_id, Side.Sell, 1)
        # ctx.cancel(order_id)

    def on_fill(self, ctx, fill): ...
    def on_reject(self, ctx, reject): ...
    def on_trade(self, ctx, trade): ...

bt = Backtest(pnl_sample_seconds=1.0, progress_seconds=30.0)
result = bt.run(
    MeanReversion(),
    data_path="/path/to/folder-of-mbo-json",   # file, folder, or list
    date_range=("2026-03-09", "2026-03-11"),    # optional
    instrument=34112,                            # optional filter
    risk={"max_position": 10},                   # optional
    progress=print,                              # optional, fires every 30s
)

result.pnl_series      # pd.Series, equity over time
result.fills_df        # pd.DataFrame: ts, instrument_id, order_id, side, price, size, maker
result.order_log_df    # pd.DataFrame: order lifecycle events
result.stats           # {'total': {...}, 'by_instrument': {...}}
result.final_equity
result.plot()          # cumulative PnL + fills (matplotlib)
```

Prices are scaled integers (`price * cmf_bt.PRICE_SCALE`); convert with
`cmf_bt.scale` / `cmf_bt.unscale`.

## Examples & tests

```bash
python examples/synthetic.py                                   # write a synthetic stream
python examples/mean_reversion.py /tmp/synthetic.mbo.json --instrument 1000 --plot out.png
jupyter notebook examples/mean_reversion.ipynb                 # end-to-end walkthrough

PYTHONPATH=. pytest tests                                      # uses a synthetic fixture
```
