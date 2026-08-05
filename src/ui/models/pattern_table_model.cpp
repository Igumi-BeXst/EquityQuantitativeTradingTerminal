#include "ui/models/pattern_table_model.h"
#include <QColor>

namespace st {

PatternTableModel::PatternTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

void PatternTableModel::setRows(const std::vector<Row>& rows) {
    beginResetModel();
    rows_ = rows;
    endResetModel();
}

int PatternTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

int PatternTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return 5;
}

QVariant PatternTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto& r = rows_[static_cast<size_t>(index.row())];

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return r.date;
            case 1: return r.name;
            case 2:
                return QString::number(r.confidence * 100.0, 'f', 0) +
                       QStringLiteral("%");
            case 3:
                switch (r.direction) {
                    case Direction::Bullish: return QStringLiteral("看涨");
                    case Direction::Bearish: return QStringLiteral("看跌");
                    default: return QStringLiteral("中性");
                }
            case 4: return r.description;
        }
        return {};
    }
    if (role == Qt::ToolTipRole) {
        return r.description;
    }
    if (role == Qt::ForegroundRole && index.column() == 3) {
        if (r.direction == Direction::Bullish) return QColor(Qt::red);    // A股红涨
        if (r.direction == Direction::Bearish) return QColor(Qt::green);  // 绿跌
    }
    return {};
}

QVariant PatternTableModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation == Qt::Vertical) return {};
    switch (section) {
        case 0: return tr("日期");
        case 1: return tr("形态");
        case 2: return tr("置信度");
        case 3: return tr("方向");
        case 4: return tr("说明");
    }
    return {};
}

} // namespace st

#include "moc_pattern_table_model.cpp"
