#pragma once

#include "engine/optimizer/grid_search.h"
#include <QAbstractTableModel>
#include <QString>
#include <string>
#include <utility>
#include <vector>

namespace st {

/// 参数优化结果表 Model — 参数组合 + 目标值 + 绩效
class GridSearchTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit GridSearchTableModel(QObject* parent = nullptr);

    void setResults(const std::vector<GridSearchResult>& results,
                    const QString& p1Name, const QString& p2Name);

    /// 第 row 行的参数键值对（应用到回测面板用）
    std::vector<std::pair<std::string, int>> paramsAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<GridSearchResult> results_;
    QString p1Name_, p2Name_;
};

} // namespace st
