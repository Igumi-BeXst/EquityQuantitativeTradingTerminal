# 架构设计

## 分层架构

```
UI → Intelligence → Engine → Core → Data → Foundation
```

依赖方向严格自上而下，层间通过纯虚接口耦合。

## 各层职责

| 层 | 职责 | 不依赖 |
|----|------|--------|
| **Foundation** | 基础类型、数据结构、工具类 | 无 |
| **Data** | 数据获取、存储、缓存 | Foundation |
| **Core** | 事件总线、线程池、配置、日志 | Foundation |
| **Engine** | 业务引擎（策略/回测/选股等） | Core, Data |
| **Intelligence** | AI 服务（后期） | Engine, Core |
| **UI** | Qt 界面 | Engine, Intelligence |

## 模块间通信

- **同步调用**: 层间直接调用下层接口
- **异步事件**: EventBus（Qt 信号槽封装），跨层松耦合
- **数据推送**: DataProvider → EventBus → Subscribers

## 多线程模型

```
ThreadPool
├── WorkerPool  (N线程, CPU密集型: 回测计算、因子计算)
├── IOWorker    (2线程, IO密集型: 数据下载)
└── GUI 主线程  (仅UI渲染和事件处理)

结果回传: ThreadPool → EventBus (信号槽) → UI 更新
```

规则: Qt 主线程永不阻塞。
