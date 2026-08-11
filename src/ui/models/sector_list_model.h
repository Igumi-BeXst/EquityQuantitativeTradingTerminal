#pragma once

#include <QAbstractTableModel>
#include <QColor>
#include <QString>
#include <vector>

namespace st {

/// 板块榜单行：板块名称 / 涨跌幅 / 成交额
struct SectorRow {
    QString name;
    double changePct = 0.0;
    double amount = 0.0;
};

/// 板块榜单 Model — QTableView 虚拟化渲染（大表滚动只画可见行，避免 QTableWidget
/// 全量 item 在大表 + 样式下触发布局/绘制死循环）
class SectorListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit SectorListModel(QObject* parent = nullptr);

    void setRows(std::vector<SectorRow> rows);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<SectorRow> rows_;
};

} // namespace st
