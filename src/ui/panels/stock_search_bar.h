#pragma once

#include "foundation/stock_info.h"
#include "data/stock_search_index.h"
#include <QWidget>
#include <QString>
#include <vector>

class QLineEdit;
class QListWidget;
class QTimer;

namespace st {

class IDataProvider;

/// 全局搜索栏 — 代码/名称/拼音首字母模糊搜索，自定义下拉浮层
///
/// 启动时经 IO 线程池异步加载精选股票池构建搜索索引；
/// 输入防抖 100ms，QListWidget 弹层，键盘上下/回车/Esc 导航。
class StockSearchBar : public QWidget {
    Q_OBJECT

public:
    explicit StockSearchBar(IDataProvider* provider, QWidget* parent = nullptr);

    /// 聚焦输入框（快捷键 Ctrl+Space 用）
    void focusEdit();

signals:
    /// 用户选中某只股票
    void stockSelected(const StockInfo& info);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTextChanged(const QString& text);
    void performSearch();
    void onPopupActivated();
    void onLoadingFinished(const std::vector<StockInfo>& stocks);

private:
    void showResults(const std::vector<StockInfo>& results);
    void hidePopup();
    void moveNext(bool down);

    IDataProvider* provider_ = nullptr;
    QLineEdit* edit_ = nullptr;
    QListWidget* popup_ = nullptr;
    QTimer* debounce_ = nullptr;
    StockSearchIndex index_;
    std::vector<StockInfo> allStocks_;   // 已加载全量（空查询热门前 8）
    std::vector<StockInfo> results_;     // 当前弹层结果
    int currentRow_ = -1;
};

} // namespace st
