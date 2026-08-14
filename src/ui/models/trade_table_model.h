#pragma once

#include "foundation/order.h"
#include <QAbstractTableModel>
#include <string>
#include <unordered_map>
#include <vector>

namespace st {

/// 成交明细表 Model — BacktestResult.trades 展示（时间/代码/名称/方向/...）
class TradeTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit TradeTableModel(QObject* parent = nullptr);

    void setTrades(const std::vector<Trade>& trades);
    /// 代码全码 → 股票名称（空 map 时名称列显示 "--"）
    void setNameByCode(std::unordered_map<std::string, std::string> nameByCode);
    void clear();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<Trade> trades_;
    std::unordered_map<std::string, std::string> nameByCode_;
};

} // namespace st
