#pragma once

#include "engine/market/market_scanner.h"
#include <QAbstractTableModel>
#include <vector>

namespace st {

/// 涨幅/跌幅榜 Model — MarketRankItem 列表展示
class MarketRankModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit MarketRankModel(QObject* parent = nullptr);

    void setItems(const std::vector<MarketRankItem>& items);
    const MarketRankItem& itemAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<MarketRankItem> items_;
};

} // namespace st
