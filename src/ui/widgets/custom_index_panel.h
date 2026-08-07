#pragma once

#include "engine/analyzer/custom_index.h"
#include "engine/analyzer/custom_index_store.h"
#include <QHash>
#include <QString>
#include <QWidget>
#include <map>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QPushButton;

namespace st {

class IDataProvider;

/// 自定义指数面板 — 指数列表 + 实时点位（订阅成分股行情驱动）+ 新建/编辑/删除/打开图表
///
/// 实时值 = 指数昨收 × (1 + Σ wᵢ·涨跌幅ᵢ/100)；昨收由日线指数序列（缓存）计算。
class CustomIndexPanel : public QWidget {
    Q_OBJECT

public:
    explicit CustomIndexPanel(IDataProvider* provider, QWidget* parent = nullptr);

signals:
    /// 请求把指数加载到中央图表
    void openChart(const CustomIndex& idx);

private slots:
    void onQuoteEvent(const QString& event, const QVariantMap& data);

private:
    void reloadList();
    void save();
    void subscribeQuotes();
    void computePrevCloses();          // 异步算每个指数昨收（日线指数序列最后已完成收盘）
    void updateLiveValues();           // 用最新涨跌幅刷新列表点位

    IDataProvider* provider_ = nullptr;
    std::vector<CustomIndex> indexes_;
    std::map<std::string, double> prevClose_;       // index id → 昨收
    std::map<std::string, double> pendingPrevClose_;// 异步计算中暂存
    QHash<QString, double> changePct_;              // 成分股 fullCode → 涨跌幅%
    std::string storePath_;

    QListWidget* list_ = nullptr;
    QLabel* status_ = nullptr;
    CustomIndexStore store_;
};

} // namespace st
