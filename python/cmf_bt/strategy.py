from cmf_bt._cmf_bt import (
    BookUpdate,
    EngineContext,
    EngineStrategy,
    OrderFill,
    OrderReject,
    TradeTick,
)


class Strategy(EngineStrategy):
    """
    Base class for a trading strategy. Subclass it and override the callbacks you
    care about. Each callback receives the EngineContext to act through:

        ctx.send_limit(instrument_id, side, price, size) -> order_id
        ctx.send_market(instrument_id, side, size)       -> order_id
        ctx.cancel(order_id)                             -> bool
        ctx.book(instrument_id)                          -> SimulatedLOB | None
        ctx.position(instrument_id)                      -> int

    Prices are scaled integers (price * cmf_bt.PRICE_SCALE); use cmf_bt.scale /
    cmf_bt.unscale to convert.
    """

    def __init__(self) -> None:
        super().__init__()

    def on_start(self, ctx: EngineContext) -> None: ...
    def on_book_update(self, ctx: EngineContext, update: BookUpdate) -> None: ...
    def on_trade(self, ctx: EngineContext, trade: TradeTick) -> None: ...
    def on_fill(self, ctx: EngineContext, fill: OrderFill) -> None: ...
    def on_reject(self, ctx: EngineContext, reject: OrderReject) -> None: ...
    def on_stop(self, ctx: EngineContext) -> None: ...
