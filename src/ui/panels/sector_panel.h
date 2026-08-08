#pragma once

#include "data/eastmoney_sector_provider.h"
#include <QWidget>
#include <memory>

class QLabel;
class QPushButton;
class QTimer;
class QButtonGroup;
class QTableWidget;
class QStackedWidget;

namespace st {

/// 板块前十榜单 — 行业/概念板块涨跌幅 Top10 简单列表
///
/// 数据：EastMoneySectorProvider（东财 clist 优先，封锁时新浪兜底），IO 池异步拉取 + gen_ 世代守卫。
/// 展示：按涨跌幅降序取前 10（板块/涨跌幅/领涨股/成交额），红涨绿跌；30s 自动刷新。
class SectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit SectorPanel(QWidget* parent = nullptr);

    /// 公开刷新（供定时任务/外部调用）
    void refresh();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onRefresh();

private:
    void setType(SectorType type);
    void applyBoards(std::vector<SectorBoard> boards);

    std::shared_ptr<EastMoneySectorProvider> provider_;
    SectorType type_ = SectorType::Industry;
    int gen_ = 0;
    bool busy_ = false;
    QTimer* timer_ = nullptr;

    QTableWidget* table_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLabel* updateLabel_ = nullptr;
    QButtonGroup* typeGroup_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
};

} // namespace st
