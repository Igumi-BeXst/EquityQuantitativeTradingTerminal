#include "ui/models/sector_list_model.h"

namespace st {

namespace {
constexpr const char* kUpColor = "#e54648";    // 红涨
constexpr const char* kDownColor = "#2e9e5b";  // 绿跌

/// 成交额（元）→ 亿/万
QString amountText(double yuan) {
    if (yuan >= 1e8) return QStringLiteral("%1亿").arg(yuan / 1e8, 0, 'f', 1);
    if (yuan >= 1e4) return QStringLiteral("%1万").arg(yuan / 1e4, 0, 'f', 1);
    return QStringLiteral("%1").arg(yuan, 0, 'f', 0);
}
}  // namespace

SectorListModel::SectorListModel(QObject* parent) : QAbstractTableModel(parent) {}

void SectorListModel::setRows(std::vector<SectorRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

int SectorListModel::rowCount(const QModelIndex&) const {
    return static_cast<int>(rows_.size());
}

int SectorListModel::columnCount(const QModelIndex&) const {
    return 3;
}

QVariant SectorListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(rows_.size())) {
        return {};
    }
    const auto& r = rows_[static_cast<size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case 0: return r.name;
                case 1: return QStringLiteral("%1%").arg(r.changePct, 0, 'f', 2);
                case 2: return amountText(r.amount);
                default: return {};
            }
        case Qt::ForegroundRole:
            if (index.column() == 1) {  // 涨跌幅列：红涨绿跌（与涨跌幅榜同色值）
                return r.changePct >= 0.0
                    ? QColor(QString::fromUtf8(kUpColor))
                    : QColor(QString::fromUtf8(kDownColor));
            }
            return {};  // 名称/成交额跟随应用主题默认色（模板同步涨跌幅榜，去掉硬编码灰）
        case Qt::TextAlignmentRole:
            return Qt::AlignCenter;  // 各列居中
        default:
            return {};
    }
}

QVariant SectorListModel::headerData(int section, Qt::Orientation orientation,
                                     int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    switch (section) {
        case 0: return QStringLiteral("板块");
        case 1: return QStringLiteral("涨跌幅");
        case 2: return QStringLiteral("成交额");
        default: return {};
    }
}

const SectorRow& SectorListModel::rowAt(int row) const {
    static const SectorRow kEmpty;  // 越界返回的空行（code.isValid()==false）
    if (row < 0 || static_cast<size_t>(row) >= rows_.size()) return kEmpty;
    return rows_[static_cast<size_t>(row)];
}

} // namespace st

#include "moc_sector_list_model.cpp"
