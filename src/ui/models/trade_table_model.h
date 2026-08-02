#pragma once

#include "foundation/order.h"
#include <QAbstractTableModel>
#include <vector>

namespace st {

/// 成交明细表 Model — BacktestResult.trades 展示
class TradeTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit TradeTableModel(QObject* parent = nullptr);

    void setTrades(const std::vector<Trade>& trades);
    void clear();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<Trade> trades_;
};

} // namespace st
