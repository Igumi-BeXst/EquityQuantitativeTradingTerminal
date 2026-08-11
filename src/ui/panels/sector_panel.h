#pragma once

#include "data/eastmoney_sector_provider.h"  // SectorType
#include "data/idata_provider.h"
#include "ui/models/sector_list_model.h"
#include <QString>
#include <QWidget>
#include <map>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;
class QButtonGroup;
class QTableView;
class QStackedWidget;

namespace st {

/// 板块榜单 — 行业/概念板块涨跌幅列表（通达信板块指数 880xxx 全量源）
///
/// 数据：IDataProvider::getSectorIndices（全量 8803xx-8804xx 行业 / 8805xx+ 概念，与叠加
/// 对话框同源同过滤）+ batchQuote（涨跌幅/成交额）；领涨股列已移除（TDX 简单报价不提供）。
/// 展示：QTableView + SectorListModel（虚拟化渲染，大表滚动不卡）+ 涨跌幅降序 + 红涨绿跌；30s 刷新。
class SectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit SectorPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 公开刷新（供定时任务/外部调用）
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onRefresh();

private:
    void setType(SectorType type);
    void applyRows(std::vector<SectorRow> rows);
    /// 拉取指定类型板块数据（IO 池，返回未排序行）
    static std::vector<SectorRow> fetchRows(IDataProvider* provider, SectorType type);
    /// 发起指定类型异步拉取（同类型在途则跳过）
    void fetchType(SectorType type);
    /// 异步回调：按类型 seq 去陈旧、更新缓存、当前类型才显示
    void onRowsReady(SectorType type, int seq, std::vector<SectorRow> rows);

    IDataProvider* provider_ = nullptr;
    SectorType type_ = SectorType::Industry;
    int fetchSeq_ = 0;                              // 全局递增 fetch 序号
    std::map<SectorType, int> lastSeq_;             // 每类型最近接受的 seq
    std::map<SectorType, bool> fetching_;           // 每类型是否在途（防重叠）
    std::map<SectorType, std::vector<SectorRow>> cache_;  // 每类型缓存（切回即现）
    QTimer* timer_ = nullptr;

    QTableView* table_ = nullptr;
    SectorListModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLabel* updateLabel_ = nullptr;
    QButtonGroup* typeGroup_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
};

} // namespace st
