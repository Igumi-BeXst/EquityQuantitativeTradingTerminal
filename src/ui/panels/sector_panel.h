#pragma once

#include "foundation/stock_code.h"
#include "data/eastmoney_sector_provider.h"  // SectorType
#include "data/idata_provider.h"
#include "ui/models/sector_list_model.h"
#include <QString>
#include <QWidget>
#include <vector>

class QLabel;
class QTableView;
class QStackedWidget;

namespace st {

/// 板块榜单页 — 固定类型（行业/概念）的板块涨跌幅列表（通达信板块指数 880xxx 全量源）
///
/// 数据：IDataProvider::getSectorIndices（全量 8803xx-8804xx 行业 / 8805xx+ 概念，与叠加
/// 对话框同源同过滤）+ batchQuoteInteractive（涨跌幅/成交额）。
/// 展示：QTableView + SectorListModel（虚拟化渲染，大表滚动不卡；模板同步涨跌幅榜，跟随主题）。
/// 刷新：由 MarketPanel 统一错峰时钟调度（本页不自管定时器）。
/// 双击行 → openSectorChart(StockCode, name)：打开板块指数（880xxx）K 线图。
class SectorListPage : public QWidget {
    Q_OBJECT

public:
    explicit SectorListPage(IDataProvider* provider, SectorType type, QWidget* parent = nullptr);

    /// 拉取/刷新本页数据（MarketPanel 统一调度；在途则跳过）
    void refresh();
    SectorType type() const { return type_; }
    QTableView* tableView() const { return table_; }

signals:
    void openSectorChart(const StockCode& code, const QString& name);

private:
    void applyRows(std::vector<SectorRow> rows);
    /// 拉取指定类型板块数据（IO 池，返回未排序行）
    static std::vector<SectorRow> fetchRows(IDataProvider* provider, SectorType type);
    /// 发起异步拉取（在途则跳过）
    void fetch();
    /// 异步回调：seq 去陈旧、更新缓存、显示
    void onRowsReady(int seq, std::vector<SectorRow> rows);

    IDataProvider* provider_ = nullptr;
    SectorType type_ = SectorType::Industry;
    int fetchSeq_ = 0;
    int lastSeq_ = 0;
    bool fetching_ = false;
    std::vector<SectorRow> cache_;

    QTableView* table_ = nullptr;
    SectorListModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLabel* updateLabel_ = nullptr;
};

} // namespace st
