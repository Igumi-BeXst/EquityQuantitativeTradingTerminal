#pragma once

#include "data/eastmoney_sector_provider.h"
#include "engine/analyzer/treemap.h"
#include <QWidget>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;
class QButtonGroup;

namespace st {

/// 板块全景面板 — 行业/概念板块 Squarified Treemap 热力图
///
/// 数据：EastMoneySectorProvider（东财 clist），IO 池异步拉取 + gen_ 世代守卫。
/// 面积∝成交额、颜色∝涨跌幅（涨红跌绿平盘灰，|涨跌|/5% 饱和度）；30s 自动刷新。
class SectorPanel : public QWidget {
    Q_OBJECT

public:
    explicit SectorPanel(QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onRefresh();

private:
    void setType(SectorType type);
    void applyBoards(std::vector<SectorBoard> boards);
    void onTileHover(int index);  // 嵌套类 SectorHeatmap 可访问

    class SectorHeatmap;

    std::shared_ptr<EastMoneySectorProvider> provider_;
    SectorType type_ = SectorType::Industry;
    int gen_ = 0;
    bool busy_ = false;
    QTimer* timer_ = nullptr;

    std::vector<SectorBoard> boards_;  // 当前板块列表

    SectorHeatmap* heatmap_ = nullptr;
    QLabel* updateLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    QButtonGroup* typeGroup_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
};

} // namespace st
