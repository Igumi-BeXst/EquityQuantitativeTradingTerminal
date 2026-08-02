#pragma once

#include <QAbstractTableModel>

namespace st {

/// 股票列表 Model（P6 实现，QTableView + QAbstractTableModel）
class StockListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    int rowCount(const QModelIndex&) const override { return 0; }
    int columnCount(const QModelIndex&) const override { return 0; }
    QVariant data(const QModelIndex&, int) const override { return {}; }
};

} // namespace st
