#pragma once

#include <QString>
#include <string>

class QAbstractItemView;
class QWidget;

namespace st::ui {

/// 把 QAbstractItemView（QTableView/QTableWidget 公共基类）当前内容导出为 CSV
/// 表头 = model->headerData(横向)；单元格 = model->data(index, DisplayRole)
/// 返回 CSV 文本（含 UTF-8 BOM）；空表格返回仅 BOM
std::string tableViewToCsv(const QAbstractItemView* view);

/// 弹保存对话框并写文件（UTF-8 BOM）；返回是否成功
/// parent 为空则无父窗口；defaultName 为建议文件名
bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                     const QString& defaultName);

} // namespace st::ui
