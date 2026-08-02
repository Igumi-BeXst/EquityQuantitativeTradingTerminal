#include "ui/models/market_rank_model.h"
#include <QColor>

namespace st {

MarketRankModel::MarketRankModel(QObject* parent) : QAbstractTableModel(parent) {}

void MarketRankModel::setItems(const std::vector<MarketRankItem>& items) {
    beginResetModel();
    items_ = items;
    endResetModel();
}

const MarketRankItem& MarketRankModel::itemAt(int row) const {
    return items_[static_cast<size_t>(row)];
}

int MarketRankModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(items_.size());
}

int MarketRankModel::columnCount(const QModelIndex&) const {
    return 5;
}

QVariant MarketRankModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(items_.size())) return {};
    const auto& item = items_[static_cast<size_t>(index.row())];

    if (role == Qt::ForegroundRole) {
        if (index.column() == 3) {
            return QColor(item.changePct >= 0 ? "#e54648" : "#2e9e5b");
        }
        return {};
    }
    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
        case 0: return QString::fromStdString(item.code.displayCode());
        case 1: return QString::fromStdString(item.name);
        case 2: return QString::number(item.price, 'f', 2);
        case 3: return QString("%1%").arg(item.changePct, 0, 'f', 2);
        case 4: return QString("%1%").arg(item.turnover, 0, 'f', 2);
        default: return {};
    }
}

QVariant MarketRankModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
        case 0: return tr("代码");
        case 1: return tr("名称");
        case 2: return tr("现价");
        case 3: return tr("涨跌幅");
        case 4: return tr("换手率");
        default: return {};
    }
}

} // namespace st

#include "moc_market_rank_model.cpp"
