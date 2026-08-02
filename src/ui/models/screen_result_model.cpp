#include "ui/models/screen_result_model.h"
#include <QString>
#include <QColor>

namespace st {

ScreenResultModel::ScreenResultModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void ScreenResultModel::setResults(
    const std::vector<ScreenResult>& results,
    const std::vector<std::string>& factorNames,
    const std::unordered_map<std::string, std::string>& nameByCode) {
    beginResetModel();
    results_ = results;
    factorNames_ = factorNames;
    nameByCode_ = nameByCode;
    endResetModel();
}

const ScreenResult* ScreenResultModel::resultAt(int row) const {
    if (row < 0 || row >= static_cast<int>(results_.size())) return nullptr;
    return &results_[static_cast<size_t>(row)];
}

int ScreenResultModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(results_.size());
}

int ScreenResultModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : kFixedColumns + static_cast<int>(factorNames_.size());
}

QVariant ScreenResultModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section < kFixedColumns) {
        switch (section) {
            case 0: return tr("排名");
            case 1: return tr("代码");
            case 2: return tr("名称");
            case 3: return tr("总分");
            default: return {};
        }
    }
    const int fi = section - kFixedColumns;
    if (fi < static_cast<int>(factorNames_.size())) {
        return QString::fromStdString(factorNames_[static_cast<size_t>(fi)]);
    }
    return {};
}

QVariant ScreenResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) return {};
    const int row = index.row();
    if (row < 0 || row >= static_cast<int>(results_.size())) return {};
    const auto& r = results_[static_cast<size_t>(row)];

    const int col = index.column();
    if (role == Qt::DisplayRole) {
        switch (col) {
            case 0: return row + 1;
            case 1: return QString::fromStdString(r.code.displayCode());
            case 2: {
                auto it = nameByCode_.find(r.code.fullCode());
                return it != nameByCode_.end()
                    ? QString::fromStdString(it->second) : QStringLiteral("--");
            }
            case 3: return QString::number(r.totalScore, 'f', 2);
            default: {
                const int fi = col - kFixedColumns;
                if (fi >= 0 && fi < static_cast<int>(r.factorResults.size())) {
                    const auto& fr = r.factorResults[static_cast<size_t>(fi)];
                    return fr.rawValue.has_value()
                        ? QString::number(*fr.rawValue, 'f', 2)
                        : QStringLiteral("--");
                }
                return {};
            }
        }
    }
    if (role == Qt::ForegroundRole && col == 3) {
        if (r.totalScore >= 60.0) return QColor("#e54648");
        if (r.totalScore >= 40.0) return QColor("#bbbbbb");
        return QColor("#2e9e5b");
    }
    if (role == Qt::TextAlignmentRole && col >= 0 && col < 2) {
        return QVariant(Qt::AlignCenter);
    }
    return {};
}

} // namespace st

#include "moc_screen_result_model.cpp"
