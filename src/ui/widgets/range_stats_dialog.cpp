#include "ui/widgets/range_stats_dialog.h"
#include "engine/analyzer/range_statistics.h"
#include "foundation/utils/datetime.h"
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace st {

namespace {
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");
const QColor kNeutralColor("#d4d4d4");

QString formatVolume(double volume) {   // 股 → 手/万/亿（对齐图表浮框）
    const double hands = volume / 100.0;
    if (hands >= 1e8) return QStringLiteral("%1亿").arg(hands / 1e8, 0, 'f', 2);
    if (hands >= 1e4) return QStringLiteral("%1万").arg(hands / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(hands, 0, 'f', 0);
}
QString formatAmount(double amount) {   // 元 → 万/亿
    if (amount >= 1e12) return QStringLiteral("%1万亿").arg(amount / 1e12, 0, 'f', 2);
    if (amount >= 1e8)  return QStringLiteral("%1亿").arg(amount / 1e8, 0, 'f', 2);
    if (amount >= 1e4)  return QStringLiteral("%1万").arg(amount / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(amount, 0, 'f', 0);
}
}  // namespace

RangeStatsDialog::RangeStatsDialog(const std::vector<Bar>& bars, int from, int to,
                                   const QString& title, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("区间统计 — %1").arg(title));
    setMinimumSize(360, 420);
    auto* layout = new QVBoxLayout(this);

    const auto rs = computeRangeStats(bars, from, to);
    if (!rs) {
        auto* label = new QLabel(tr("无可统计数据"), this);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("color:#888888;"));
        layout->addWidget(label);
        return;
    }

    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({tr("指标"), tr("数值")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setShowGrid(false);

    auto addRow = [this](const QString& name, const QString& value,
                         const QColor& color = kNeutralColor) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* ni = new QTableWidgetItem(name);
        auto* vi = new QTableWidgetItem(value);
        ni->setForeground(kNeutralColor);
        vi->setForeground(color);
        vi->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, ni);
        table_->setItem(row, 1, vi);
    };
    const auto dstr = [](DateTime dt) {
        return QString::fromStdString(utils::toDateString(dt));
    };
    const QColor pctColor = rs->openClosePct >= 0 ? kUpColor : kDownColor;

    addRow(tr("日期范围"), tr("%1 ~ %2").arg(dstr(rs->fromDate), dstr(rs->toDate)));
    addRow(tr("区间天数"), tr("%1 根").arg(rs->barCount));
    addRow(tr("区间涨跌幅"), tr("%1%").arg(rs->openClosePct * 100.0, 0, 'f', 2), pctColor);
    addRow(tr("区间振幅"), tr("%1%").arg(rs->amplitude * 100.0, 0, 'f', 2), pctColor);
    addRow(tr("区间最高"), tr("%1  (%2)").arg(rs->high, 0, 'f', 2).arg(dstr(rs->highDate)));
    addRow(tr("区间最低"), tr("%1  (%2)").arg(rs->low, 0, 'f', 2).arg(dstr(rs->lowDate)));
    addRow(tr("累计成交量"), formatVolume(rs->totalVolume));
    addRow(tr("累计成交额"), formatAmount(rs->totalAmount));
    addRow(tr("区间换手率"), tr("%1%").arg(rs->turnoverSum * 100.0, 0, 'f', 2));
    addRow(tr("均价"), tr("%1").arg(rs->avgPrice, 0, 'f', 2));

    layout->addWidget(table_);
}

} // namespace st

#include "moc_range_stats_dialog.cpp"
