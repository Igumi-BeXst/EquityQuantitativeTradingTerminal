#pragma once

#include "engine/analyzer/custom_index.h"
#include <QDialog>
#include <QString>
#include <vector>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QTableWidget;

namespace st {

class IDataProvider;
struct StockInfo;

/// 自定义指数编辑器 — 名称 / 基点 + 成分股表
///
/// 权重默认等权（新加成分股自动均分），可手动改（百分比，保存时归一化到 1）。
class CustomIndexEditorDialog : public QDialog {
    Q_OBJECT

public:
    /// editing.id 为空 = 新建（自动生成 id）；否则编辑既有指数
    CustomIndexEditorDialog(IDataProvider* provider, const CustomIndex& editing,
                            QWidget* parent = nullptr);

    /// 确认后返回的指数（权重已归一化，id 已定）
    CustomIndex result() const;

private:
    void addConstituent(const StockInfo& info);
    void evenWeights();
    void removeRow(int row);
    void refreshTable();
    void updateSumLabel();

    IDataProvider* provider_ = nullptr;
    CustomIndex editing_;
    std::vector<IndexConstituent> pending_;  // 编辑中的成分股（表的数据源）

    QLineEdit* nameEdit_ = nullptr;
    QDoubleSpinBox* baseSpin_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* sumLabel_ = nullptr;
};

} // namespace st
