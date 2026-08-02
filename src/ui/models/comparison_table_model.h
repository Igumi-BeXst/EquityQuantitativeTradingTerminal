#pragma once

#include "engine/analyzer/strategy_comparator.h"
#include <QAbstractTableModel>
#include <vector>

namespace st {

/// 策略对比结果表 Model — 每策略一行，关键绩效对比
class ComparisonTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ComparisonTableModel(QObject* parent = nullptr);

    void setItems(const std::vector<ComparisonItemResult>& items);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<ComparisonItemResult> items_;
};

} // namespace st
