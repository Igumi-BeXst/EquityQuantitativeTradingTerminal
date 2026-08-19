#pragma once

#include "foundation/stock_info.h"
#include "foundation/stock_code.h"
#include <QWidget>
#include <vector>

class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;

namespace st {

class IDataProvider;

/// 全市场股票池选择器 — 异步加载全 A 股列表 + 搜索过滤 + 多选
///
/// 替换各量化面板内置的精选股池（curated_stocks.h）：
/// 构造后经 IO 线程池拉取 SH/SZ 全量列表（过滤为可交易 A 股），
/// 加载完成自动全选；搜索框按 代码/名称/全拼/拼音首字母 过滤可见项，
/// 「全选/清空」作用于当前过滤结果；已选数量实时显示。
class StockPoolPicker : public QWidget {
    Q_OBJECT

public:
    explicit StockPoolPicker(IDataProvider* provider, QWidget* parent = nullptr);

    /// 当前选中的股票（过滤不影响选中集）
    std::vector<StockCode> selectedSymbols() const;

    /// 全市场股票列表（名称映射等用；加载完成前为空）
    const std::vector<StockInfo>& allStocks() const { return allStocks_; }

    /// 是否已完成全市场加载
    bool isReady() const { return ready_; }

signals:
    /// 勾选变化（面板可据此刷新运行按钮状态）
    void selectionChanged();

private slots:
    void onSearchTextChanged(const QString& text);
    void onLoadingFinished(std::vector<StockInfo> stocks);

private:
    void applyFilter();
    void updateStatus();
    void setAllVisibleChecked(bool on);

    IDataProvider* provider_ = nullptr;

    QLineEdit* searchEdit_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* selectAllBtn_ = nullptr;
    QPushButton* clearBtn_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    std::vector<StockInfo> allStocks_;
    std::vector<std::string> pinyinFullCache_;  // 全拼缓存，避免每次过滤重复转换
    bool ready_ = false;
};

} // namespace st
