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

int WatchlistModel::columnCount(const QModelIndex&) const { return 3; }

QVariant WatchlistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(items_.size())) return {};
    const auto& it = items_[static_cast<size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case 0: return it.name;
                case 1: return QString::number(it.price, 'f', 2);
                case 2: return QStringLiteral("%1%").arg(it.changePct, 0, 'f', 2);
                default: return {};
            }
        case Qt::ForegroundRole:
            if (index.column() == 2) {
                return it.changePct >= 0.0
                    ? QColor(QString::fromUtf8(kUpColor))
                    : QColor(QString::fromUtf8(kDownColor));
            }
            return {};  // 名称/现价跟随应用主题默认色
        default: return {};
    }
}

QVariant WatchlistModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return QStringLiteral("名称");
        case 1: return QStringLiteral("现价");
        case 2: return QStringLiteral("涨跌幅");
        default: return {};
    }
}

} // namespace st

#include "moc_watchlist_model.cpp"
