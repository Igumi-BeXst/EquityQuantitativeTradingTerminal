#pragma once

#include "engine/screener/screener_types.h"
#include <QAbstractTableModel>
#include <string>
#include <unordered_map>
#include <vector>

namespace st {

/// 选股结果表 Model — 排名/代码/名称/总分 + 可选 AI 分列 + 各因子明细
class ScreenResultModel : public QAbstractTableModel {
    Q_OBJECT

public:
    explicit ScreenResultModel(QObject* parent = nullptr);

    /// 注入结果（factorNames 顺序须与 factorResults 对齐）
    /// aiScores 与 results 对齐；空 = 不显示「AI 分」列
    void setResults(const std::vector<ScreenResult>& results,
                    const std::vector<std::string>& factorNames,
                    const std::unordered_map<std::string, std::string>& nameByCode,
                    const std::vector<double>& aiScores = {});

    /// 第 row 行的选股结果（双击开图用）
    const ScreenResult* resultAt(int row) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    static constexpr int kFixedColumns = 4;  // 排名/代码/名称/总分
    int aiColumn() const { return aiScores_.empty() ? -1 : kFixedColumns; }  // AI 分列（无则 -1）
    int factorBase() const { return aiScores_.empty() ? kFixedColumns : kFixedColumns + 1; }

    std::vector<ScreenResult> results_;
    std::vector<std::string> factorNames_;
    std::unordered_map<std::string, std::string> nameByCode_;
    std::vector<double> aiScores_;  // 与 results_ 对齐；空 = 无 AI 列
};

} // namespace st
