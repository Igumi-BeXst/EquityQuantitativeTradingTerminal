#include "ui/utils/table_csv_export.h"
#include "foundation/utils/csv.h"
#include "core/log_manager.h"
#include "core/app_paths.h"
#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QModelIndex>
#include <QWidget>

namespace st::ui {

std::string tableViewToCsv(const QAbstractItemView* view) {
    if (!view || !view->model()) return csv::tableToCsv({});
    const auto* model = view->model();
    std::vector<std::vector<std::string>> rows;
    const int cols = model->columnCount();
    if (cols <= 0) return csv::tableToCsv({});
    // 表头
    std::vector<std::string> header;
    header.reserve(static_cast<size_t>(cols));
    for (int c = 0; c < cols; ++c) {
        header.push_back(model->headerData(c, Qt::Horizontal).toString().toStdString());
    }
    rows.push_back(std::move(header));
    // 数据行
    const int rCount = model->rowCount();
    for (int r = 0; r < rCount; ++r) {
        std::vector<std::string> row;
        row.reserve(static_cast<size_t>(cols));
        for (int c = 0; c < cols; ++c) {
            row.push_back(model->data(model->index(r, c)).toString().toStdString());
        }
        rows.push_back(std::move(row));
    }
    return csv::tableToCsv(rows);
}

bool exportViewToCsv(QAbstractItemView* view, QWidget* parent,
                     const QString& defaultName) {
    if (!view || !view->model() || view->model()->rowCount() <= 0) {
        // 空表格：提示并返回
        return false;
    }
    const QString defaultPath = QString::fromStdString(AppPaths::dataDir() + "/") + defaultName;
    const QString path = QFileDialog::getSaveFileName(
        parent, QObject::tr("导出 CSV"), defaultPath, QObject::tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LogManager::instance()->log(LogLevel::Warn, "CSV 导出失败: {}", path.toStdString());
        return false;
    }
    const std::string csv = tableViewToCsv(view);
    file.write(csv.data(), static_cast<qint64>(csv.size()));
    LogManager::instance()->log(LogLevel::Info, "已导出 CSV {} 行: {}", view->model()->rowCount(),
                                path.toStdString());
    return true;
}

} // namespace st::ui
