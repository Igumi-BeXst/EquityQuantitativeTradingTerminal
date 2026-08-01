# Qt UI 开发规范

## 控件开发
- K线图: QWidget + QPainter 自绘
- 表格: QTableView + QAbstractTableModel
- 布局: QDockWidget 可拖拽面板
- 主题: QSS 暗色/亮色

## 信号槽
- 跨模块: EventBus 统一事件通道
- 模块内: 直接 connect

## 线程
- Qt 主线程只做 UI
- WorkThread 处理计算，通过 EventBus 回传结果
