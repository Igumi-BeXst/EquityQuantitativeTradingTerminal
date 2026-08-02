#include "ui/models/comparison_table_model.h"
#include <QColor>
#include <QString>

namespace st {

namespace {
constexpr int kColumnCount = 9;  // 策略/参数/总收益%/年化%/夏普/最大回撤%/胜率%/盈亏比/交易数

QString formatParams(const std::vector<std::pair<std::string, int>>& params) {
    QStringList parts;
    for (const auto& [name, val] : params) {
        parts << QStringLiteral("%1=%2").arg(QString::fromStdString(name)).arg(val);
    }
    return parts.join(QStringLiteral(", "));
}
}  // namespace

ComparisonTableModel::ComparisonTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void ComparisonTableModel::setItems(const std::vector<ComparisonItemResult>& items) {
    beginResetModel();
    items_ = items;
    endResetModel();
}

int ComparisonTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(items_.size());
}

int ComparisonTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kColumnCount;
}

QVariant ComparisonTableModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return tr("策略");
        case 1: return tr("参数");
        case 2: return tr("总收益%");
        case 3: return tr("年化%");
        case 4: return tr("夏普");
        case 5: return tr("最大回撤%");
        case 6: return tr("胜率%");
        case 7: return tr("盈亏比");
        case 8: return tr("交易数");
        default: return {};
    }
}

QVariant ComparisonTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= static_cast<int>(items_.size())) return {};
    const auto& r = items_[static_cast<size_t>(row)];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return QString::fromStdString(r.item.label);
            case 1: return formatParams(r.item.params);
            case 2: return QString::number(r.performance.totalReturn, 'f', 2);
            case 3: return QString::number(r.performance.annualReturn, 'f', 2);
            case 4: return QString::number(r.performance.sharpeRatio, 'f', 2);
            case 5: return QString::number(r.performance.maxDrawdown, 'f', 2);
            case 6: return QString::number(r.performance.winRate, 'f', 1);
            case 7: return QString::number(r.performance.profitFactor, 'f', 2);
            case 8: return r.performance.totalTrades;
            default: return {};
        }
    }
    if (role == Qt::ForegroundRole && index.column() == 2) {
        return r.performance.totalReturn >= 0 ? QColor("#e54648") : QColor("#2e9e5b");
    }
    return {};
}

} // namespace st

#include "moc_comparison_table_model.cpp"
