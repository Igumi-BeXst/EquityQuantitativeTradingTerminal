#include "ui/models/grid_search_table_model.h"
#include <QColor>

namespace st {

namespace {
constexpr int kColumnCount = 9;  // 参数1/参数2/目标值/总收益%/最大回撤%/夏普/交易数/Alpha/Beta
}  // namespace

GridSearchTableModel::GridSearchTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void GridSearchTableModel::setResults(const std::vector<GridSearchResult>& results,
                                      const QString& p1Name, const QString& p2Name) {
    beginResetModel();
    results_ = results;
    p1Name_ = p1Name;
    p2Name_ = p2Name;
    endResetModel();
}

std::vector<std::pair<std::string, int>> GridSearchTableModel::paramsAt(int row) const {
    if (row < 0 || row >= static_cast<int>(results_.size())) return {};
    return results_[static_cast<size_t>(row)].params;
}

int GridSearchTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(results_.size());
}

int GridSearchTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kColumnCount;
}

QVariant GridSearchTableModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return p1Name_.isEmpty() ? tr("参数1") : p1Name_;
        case 1: return p2Name_.isEmpty() ? tr("参数2") : p2Name_;
        case 2: return tr("目标值");
        case 3: return tr("总收益%");
        case 4: return tr("最大回撤%");
        case 5: return tr("夏普");
        case 6: return tr("交易数");
        case 7: return tr("Alpha");
        case 8: return tr("Beta");
        default: return {};
    }
}

QVariant GridSearchTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= static_cast<int>(results_.size())) return {};
    const auto& r = results_[static_cast<size_t>(row)];
    const auto paramValue = [&](size_t idx) -> QVariant {
        if (idx < r.params.size()) return r.params[idx].second;
        return QStringLiteral("--");
    };

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return paramValue(0);
            case 1: return paramValue(1);
            case 2: return QString::number(r.objectiveValue, 'f', 2);
            case 3: return QString::number(r.performance.totalReturn, 'f', 2);
            case 4: return QString::number(r.performance.maxDrawdown, 'f', 2);
            case 5: return QString::number(r.performance.sharpeRatio, 'f', 2);
            case 6: return r.performance.totalTrades;
            case 7: return QString::number(r.performance.alpha / 100.0, 'f', 4);
            case 8: return QString::number(r.performance.beta, 'f', 2);
            default: return {};
        }
    }
    if (role == Qt::ForegroundRole && index.column() == 2) {
        return r.objectiveValue >= 0 ? QColor("#e54648") : QColor("#2e9e5b");
    }
    if (role == Qt::TextAlignmentRole) {
        return QVariant(Qt::AlignCenter);
    }
    return {};
}

} // namespace st

#include "moc_grid_search_table_model.cpp"
