#include "ui/models/watchlist_model.h"

#include <QColor>

namespace st {

namespace {
constexpr const char* kUpColor = "#e54648";    // 红涨
constexpr const char* kDownColor = "#2e9e5b";  // 绿跌
}  // namespace

WatchlistModel::WatchlistModel(QObject* parent) : QAbstractTableModel(parent) {}

void WatchlistModel::setItems(std::vector<WatchItem> items) {
    beginResetModel();
    items_ = std::move(items);
    endResetModel();
}

const WatchItem& WatchlistModel::itemAt(int row) const {
    static const WatchItem kEmpty;
    if (row < 0 || static_cast<size_t>(row) >= items_.size()) return kEmpty;
    return items_[static_cast<size_t>(row)];
}

int WatchlistModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(items_.size());
}

int WatchlistModel::columnCount(const QModelIndex&) const { return 4; }

QVariant WatchlistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(items_.size())) return {};
    const auto& it = items_[static_cast<size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case 0: return QString::fromStdString(it.code.displayCode());
                case 1: return it.name;
                case 2: return QString::number(it.price, 'f', 2);
                case 3: return QStringLiteral("%1%").arg(it.changePct, 0, 'f', 2);
                default: return {};
            }
        case Qt::ForegroundRole:
            if (index.column() == 2 || index.column() == 3) {  // 现价/涨跌幅：对比昨收红涨绿跌
                return it.changePct >= 0.0
                    ? QColor(QString::fromUtf8(kUpColor))
                    : QColor(QString::fromUtf8(kDownColor));
            }
            return {};  // 代码/名称跟随应用主题默认色
        case Qt::TextAlignmentRole:
            return Qt::AlignCenter;  // 各列居中
        default: return {};
    }
}

QVariant WatchlistModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
    if (orientation != Qt::Horizontal) return {};
    if (role == Qt::TextAlignmentRole) return Qt::AlignCenter;  // 表头居中
    if (role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return QStringLiteral("代码");
        case 1: return QStringLiteral("名称");
        case 2: return QStringLiteral("现价");
        case 3: return QStringLiteral("涨跌幅");
        default: return {};
    }
}

} // namespace st

#include "moc_watchlist_model.cpp"
