#pragma once

#include "foundation/stock_code.h"
#include <QAbstractTableModel>
#include <QString>
#include <vector>

namespace st {

/// 自选股行
struct WatchItem {
    StockCode code;
    QString name;
    double price = 0.0;
    double changePct = 0.0;
};

/// 自选股 Model — QTableView 虚拟化渲染（名称/现价/涨跌幅）
class WatchlistModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit WatchlistModel(QObject* parent = nullptr);
    void setItems(std::vector<WatchItem> items);
    const WatchItem& itemAt(int row) const;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
private:
    std::vector<WatchItem> items_;
};

} // namespace st
