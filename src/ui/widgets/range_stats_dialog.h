#pragma once

#include "foundation/bar.h"
#include <QDialog>
#include <vector>

class QTableWidget;

namespace st {

/// K线区间统计弹窗 — 模态表格（指标/数值两列），红涨绿跌上色
class RangeStatsDialog : public QDialog {
    Q_OBJECT
public:
    explicit RangeStatsDialog(const std::vector<Bar>& bars, int from, int to,
                              const QString& title, QWidget* parent = nullptr);
private:
    QTableWidget* table_ = nullptr;
};

} // namespace st
