# 模块接口规范

## Foundation 层
- StockCode: 股票代码类型
- Bar/BarSeries: K线数据
- Order/Trade: 订单/成交
- Portfolio/Position: 持仓

## Data 层
- IDataProvider: 数据源抽象接口
- DataRepository: SQLite 持久化辅助模块（当前未接入应用数据流程）
- DataCache: 回测内存缓存

## Engine 层
- IStrategy: 策略基类
- BacktestEngine: 回测引擎
- PaperTradeEngine: 模拟交易引擎
