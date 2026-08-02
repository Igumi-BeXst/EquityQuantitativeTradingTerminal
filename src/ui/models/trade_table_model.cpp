#include "ui/models/trade_table_model.h"
#include "foundation/utils/datetime.h"
#include <QColor>

namespace st {

TradeTableModel::TradeTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void TradeTableModel::setTrades(const std::vector<Trade>& trades) {
    beginResetModel();
    trades_ = trades;
    endResetModel();
}

void TradeTableModel::clear() {
    beginResetModel();
    trades_.clear();
    endResetModel();
}

int TradeTableModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(trades_.size());
}

int TradeTableModel::columnCount(const QModelIndex&) const {
    return 10;
}

QVariant TradeTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(trades_.size())) return {};
    const auto& t = trades_[static_cast<size_t>(index.row())];

    if (role == Qt::ForegroundRole) {
        if (index.column() == 2) {
            return QColor(t.direction == Direction::Buy ? "#e54648" : "#2e9e5b");
        }
        return {};
    }
    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
        case 0: return QString::fromStdString(utils::toDateTimeString(t.time));
        case 1: return QString::fromStdString(t.code.displayCode());
        case 2: return t.direction == Direction::Buy ? tr("买入") : tr("卖出");
        case 3: return QString::number(t.price, 'f', 2);
        case 4: return static_cast<qlonglong>(t.volume);
        case 5: return QString::number(t.amount, 'f', 2);
        case 6: return QString::number(t.commission, 'f', 2);
        case 7: return QString::number(t.stampTax, 'f', 2);
        case 8: return QString::number(t.otherFees, 'f', 2);
        case 9: return QString::number(t.totalFee, 'f', 2);
        default: return {};
    }
}

QVariant TradeTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return {};
    switch (section) {
        case 0: return tr("时间");
        case 1: return tr("代码");
        case 2: return tr("方向");
        case 3: return tr("价格");
        case 4: return tr("数量");
        case 5: return tr("成交额");
        case 6: return tr("佣金");
        case 7: return tr("印花税");
        case 8: return tr("其他费");
        case 9: return tr("总费用");
        default: return {};
    }
}

} // namespace st

#include "moc_trade_table_model.cpp"
