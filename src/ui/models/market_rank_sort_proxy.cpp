#include "ui/models/market_rank_sort_proxy.h"
#include "ui/models/market_rank_model.h"

namespace st {

MarketRankSortProxy::MarketRankSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent) {}

bool MarketRankSortProxy::lessThan(const QModelIndex& left,
                                   const QModelIndex& right) const {
    const auto* src = qobject_cast<const MarketRankModel*>(sourceModel());
    if (!src) return QSortFilterProxyModel::lessThan(left, right);
    const auto& a = src->itemAt(left.row());
    const auto& b = src->itemAt(right.row());
    switch (left.column()) {
        case 0: return a.code.displayCode() < b.code.displayCode();
        case 1: return a.name < b.name;
        case 2: return a.price < b.price;         // 现价
        case 3: return a.changePct < b.changePct; // 涨跌幅
        case 4: return a.turnover < b.turnover;   // 换手率
        default: return QSortFilterProxyModel::lessThan(left, right);
    }
}

} // namespace st

#include "moc_market_rank_sort_proxy.cpp"
