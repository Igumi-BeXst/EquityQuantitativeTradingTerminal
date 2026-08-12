#pragma once

#include "data/idata_provider.h"
#include "foundation/stock_code.h"
#include <QDialog>
#include <QString>
#include <unordered_map>
#include <vector>

class QLabel;
class QTableView;

namespace st {

class MarketRankModel;

/// 板块成分股弹窗 — 输入板块中文名，异步拉成分股 + 行情 + 名称，双击开图
///
/// 数据：EastMoneySectorConstituents::fetchConstituents(boardName) → codes；
/// provider->getStockList(SH/SZ) 构建名称 map（TDX 缓存命中即快）；batchQuoteInteractive 行情。
class SectorConstituentsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SectorConstituentsDialog(IDataProvider* provider,
                                      const QString& boardName, QWidget* parent = nullptr);

signals:
    void openChart(const StockCode& code, const QString& name);

private:
    void fetchConstituents();
    void onCodesReady(std::vector<StockCode> codes);
    void fetchQuotes(const std::vector<StockCode>& codes);
    void onQuotesReady(std::vector<Quote> quotes);

    IDataProvider* provider_ = nullptr;
    QString boardName_;
    QLabel* title_ = nullptr;
    QTableView* table_ = nullptr;
    MarketRankModel* model_ = nullptr;
    class MarketRankSortProxy* proxy_ = nullptr;  // 列头排序代理
    int fetchSeq_ = 0;
    bool fetching_ = false;
    std::vector<StockCode> codes_;            // 当前成分股
    std::unordered_map<std::string, std::string> names_;  // fullCode → 中文名
};

} // namespace st
