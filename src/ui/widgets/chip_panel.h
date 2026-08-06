#pragma once

#include "foundation/stock_code.h"
#include "foundation/types.h"
#include "engine/analyzer/chip_distribution.h"
#include "data/akshare_provider.h"
#include <QString>
#include <QWidget>
#include <memory>
#include <optional>
#include <vector>

class QLabel;
class QPushButton;

namespace st {

class IDataProvider;

/// 筹码分布面板 — 筹码云 + 成本统计（平均成本/获利盘/集中度/90%/70%成本区间）
///
/// 数据：provider_（主源）拉日K；fundProvider_（东财）取流通股本做换手率衰减，
/// 缺失时降级纯量模式。异步：IO 池拉数 → Worker 池计算 → QPointer 守卫回主线程。
/// 按日期锚定：默认最新一天，K线（日/周/月）十字光标悬停 → 查该日期的筹码分布。
class ChipPanel : public QWidget {
    Q_OBJECT

public:
    explicit ChipPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 切换到某只股票（异步拉日K/流通股本并计算）
    void setStock(const StockCode& code, const QString& name);

    /// 按日期查询筹码分布（nullopt = 最新一天）；由 K线十字光标驱动
    void setAsOfDate(const std::optional<DateTime>& date);

private slots:
    void onRefresh();

private:
    void requestData();
    void onDataLoaded(std::vector<Bar> bars);
    void computeAndApply();
    void applyResult(const DateTime& asOfDate);
    void resetStats();

    IDataProvider* provider_ = nullptr;
    std::shared_ptr<AKShareProvider> fundProvider_;  // 东财基本面源（流通股本）；shared 供异步捕获
    StockCode code_;
    QString name_;

    int gen_ = 0;                    // 世代守卫（快速切股防陈旧回写）
    bool busy_ = false;
    bool pending_ = false;           // 计算在飞时有新日期请求 → 完成后补算
    std::optional<DateTime> asOfDate_;  // 查询日期（nullopt = 最新一天）
    double floatShares_ = 0.0;       // 流通股本（0=未知 → 纯量模式）
    double lastClose_ = 0.0;         // 现价（末根日K收盘）

    std::vector<Bar> bars_;          // 全量日K（2005→now）
    ChipDistResult chip_;

    // 自绘区（筹码云）
    class ChipChartArea;

    ChipChartArea* chart_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* priceLabel_ = nullptr;
    QLabel* dateLabel_ = nullptr;      // 筹码截至日期
    std::vector<QLabel*> statLabels_;  // 与统计项一一对应
    QPushButton* refreshBtn_ = nullptr;
};

} // namespace st
