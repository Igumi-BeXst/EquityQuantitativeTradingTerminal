#pragma once

#include "engine/screener/screener_types.h"
#include <QAbstractTableModel>
#include <string>
#include <unordered_map>
#include <vector>

namespace st {

/// 选股结果表 Model — 排名/代码/名称/总分 + 各因子明细
class ScreenResultModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ScreenResultModel(QObject* parent = nullptr);

    /// 注入结果（factorNames 顺序须与 factorResults 对齐）
    void setResults(const std::vector<ScreenResult>& results,
                    const std::vector<std::string>& factorNames,
                    const std::unordered_map<std::string, std::string>& nameByCode);

    /// 第 row 行的选股结果（双击开图用）
    const ScreenResult* resultAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    static constexpr int kFixedColumns = 4;  // 排名/代码/名称/总分

    std::vector<ScreenResult> results_;
    std::vector<std::string> factorNames_;
    std::unordered_map<std::string, std::string> nameByCode_;
};

} // namespace st
