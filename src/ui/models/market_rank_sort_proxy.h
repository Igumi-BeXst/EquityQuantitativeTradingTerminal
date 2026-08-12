#pragma once

#include <QSortFilterProxyModel>

namespace st {

class MarketRankModel;

/// 市场排行排序代理 — 点击列头按对应列数值排序（代码/名称/现价/涨跌幅/换手率）
///
/// QSortFilterProxyModel 默认按 DisplayRole 字符串比较，数字会错序（"100.5" < "9.5"），
/// 故自定义 lessThan 直读源模型 MarketRankItem 的数值字段。
class MarketRankSortProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit MarketRankSortProxy(QObject* parent = nullptr);

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
};

} // namespace st
