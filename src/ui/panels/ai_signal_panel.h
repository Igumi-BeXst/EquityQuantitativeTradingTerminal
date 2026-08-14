#pragma once

#include "foundation/stock_code.h"
#include "intelligence/signal/composite_signal.h"
#include <QWidget>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTableView;

namespace st {

class IDataProvider;

/// AI 综合信号面板 — 单只股票融合 形态+情绪+技术 → 评级/置信度/分项/历史
///
/// 绑定主窗口中央图表开图路径（搜索/市场/量化/openStockChart）→ setStock。
/// 异步：IO 拉日K + 东财新闻 → Worker 算指标/形态/综合信号 →
/// QPointer 守卫 + gen 世代守卫回主线程（项目标准安全异步模式）。
/// 情绪无新闻/失败 → 分项缺失折减置信度，不阻塞综合信号。
class AiSignalPanel : public QWidget {
    Q_OBJECT

public:
    explicit AiSignalPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 切换到某只股票（异步计算并刷新；指数也支持——情绪分项自动缺失）
    void setStock(const StockCode& code, const QString& name);

signals:
    /// 历史信号行双击 → 主窗口打开该股 K 线
    void openChart(const StockCode& code, const QString& name);

private:
    struct HistoryRow {
        StockCode code;
        QString name;
        QString date;
        st::signal::SignalRating rating = st::signal::SignalRating::Neutral;
        double score = 0.0;
    };

    void startCompute();
    void applyResult(st::signal::CompositeSignal cs, const QString& date);
    void addHistory(const HistoryRow& row);
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider_;  // 东财资讯（shared 供异步捕获）
    StockCode code_;
    QString name_;
    int gen_ = 0;         // 世代守卫：快速切股丢弃陈旧结果
    bool busy_ = false;

    QLabel* titleLabel_ = nullptr;
    QLabel* ratingLabel_ = nullptr;    // 评级大字
    QLabel* metaLabel_ = nullptr;      // 得分/置信度/信号日期
    QLabel* summaryLabel_ = nullptr;   // 一句话结论
    QWidget* componentsBox_ = nullptr; // 分项条容器（不足时 QLabel 占位）
    QTableView* historyView_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    std::vector<HistoryRow> history_;  // 会话内历史（上限 50，最新在前）
};

} // namespace st
