#include "ui/widgets/custom_index_editor.h"
#include "ui/panels/stock_search_bar.h"
#include "data/idata_provider.h"
#include "foundation/stock_info.h"
#include "foundation/utils/datetime.h"
#include "engine/analyzer/custom_index.h"
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <chrono>
#include <string>

namespace st {

namespace {
QString weightText(double w) {
    return QString::number(w * 100.0, 'f', 1) + QStringLiteral("%");
}
}  // namespace

CustomIndexEditorDialog::CustomIndexEditorDialog(IDataProvider* provider,
                                                 const CustomIndex& editing,
                                                 QWidget* parent)
    : QDialog(parent), provider_(provider), editing_(editing) {
    setWindowTitle(editing.id.empty() ? tr("新建自定义指数") : tr("编辑自定义指数"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);

    // 名称 + 基点
    auto* nameRow = new QHBoxLayout();
    nameRow->addWidget(new QLabel(tr("名称："), this));
    nameEdit_ = new QLineEdit(QString::fromStdString(editing.name), this);
    nameEdit_->setPlaceholderText(tr("如 白酒组合"));
    nameRow->addWidget(nameEdit_, 1);
    nameRow->addWidget(new QLabel(tr("基点："), this));
    baseSpin_ = new QDoubleSpinBox(this);
    baseSpin_->setRange(1, 1000000);
    baseSpin_->setDecimals(0);
    baseSpin_->setValue(editing.baseValue > 0 ? editing.baseValue : 1000.0);
    nameRow->addWidget(baseSpin_);
    layout->addLayout(nameRow);

    // 搜索加股
    layout->addWidget(new QLabel(tr("添加成分股（搜索代码/名称/拼音）："), this));
    auto* search = new StockSearchBar(provider_, this);
    search->setFixedWidth(400);
    layout->addWidget(search);
    connect(search, &StockSearchBar::stockSelected,
            this, [this](const StockInfo& info) { addConstituent(info); });

    // 成分股表: 代码 | 名称 | 权重% | 删除
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({tr("代码"), tr("名称"), tr("权重"), QString()});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    layout->addWidget(table_, 1);

    // 均分权重 + 权重合计
    auto* weightRow = new QHBoxLayout();
    auto* evenBtn = new QPushButton(tr("均分权重"), this);
    connect(evenBtn, &QPushButton::clicked, this, [this] { evenWeights(); });
    weightRow->addWidget(evenBtn);
    weightRow->addStretch();
    sumLabel_ = new QLabel(this);
    weightRow->addWidget(sumLabel_);
    layout->addLayout(weightRow);

    // 按钮行
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("取消"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto* okBtn = new QPushButton(tr("确定"), this);
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, [this] {
        if (nameEdit_->text().trimmed().isEmpty()) {
            nameEdit_->setFocus();
            return;
        }
        if (pending_.empty()) return;
        accept();
    });
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    // 预填编辑中的成分股（新建则空表 → 全 0 → 等权）
    pending_ = editing.constituents;
    refreshTable();
}

CustomIndex CustomIndexEditorDialog::result() const {
    CustomIndex idx = editing_;
    if (idx.id.empty()) {
        const auto ts = std::chrono::system_clock::now().time_since_epoch().count();
        idx.id = "ci_" + std::to_string(ts);
    }
    idx.name = nameEdit_->text().trimmed().toStdString();
    idx.baseValue = baseSpin_->value();
    // 基准日 = 创建当天：指数当天 = 基点（默认 1000），历史往前回溯到此值，
    // 数值直观（不像"最早共同数据日"那样可能累计出几千上万的点位）。
    idx.baseDate = utils::today();
    idx.constituents = pending_;
    normalizeWeights(idx.constituents);
    return idx;
}

void CustomIndexEditorDialog::addConstituent(const StockInfo& info) {
    if (!info.code.isValid()) return;
    for (const auto& c : pending_) {
        if (c.code == info.code) return;  // 已存在，忽略
    }
    IndexConstituent c;
    c.code = info.code;
    c.name = info.name;
    c.weight = 0.0;   // 均分占位
    pending_.push_back(std::move(c));
    evenWeights();    // 默认等权
    refreshTable();
}

void CustomIndexEditorDialog::evenWeights() {
    if (pending_.empty()) return;
    const double w = 1.0 / static_cast<double>(pending_.size());
    for (auto& c : pending_) c.weight = w;
    refreshTable();
}

void CustomIndexEditorDialog::removeRow(int row) {
    if (row < 0 || row >= static_cast<int>(pending_.size())) return;
    pending_.erase(pending_.begin() + row);
    refreshTable();
}

void CustomIndexEditorDialog::refreshTable() {
    table_->setRowCount(static_cast<int>(pending_.size()));
    for (int r = 0; r < static_cast<int>(pending_.size()); ++r) {
        const auto& c = pending_[r];
        auto* codeItem = new QTableWidgetItem(QString::fromStdString(c.code.displayCode()));
        codeItem->setFlags(codeItem->flags() & ~Qt::ItemIsEditable);
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(c.name));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        table_->setItem(r, 0, codeItem);
        table_->setItem(r, 1, nameItem);

        auto* spin = new QDoubleSpinBox(table_);
        spin->setRange(0.0, 100.0);
        spin->setDecimals(1);
        spin->setSuffix(QStringLiteral(" %"));
        spin->setValue(c.weight * 100.0);
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this, r](double v) {
                    if (r < static_cast<int>(pending_.size())) {
                        pending_[r].weight = v / 100.0;
                        updateSumLabel();
                    }
                });
        table_->setCellWidget(r, 2, spin);

        auto* delBtn = new QPushButton(tr("删除"), table_);
        connect(delBtn, &QPushButton::clicked, this, [this, r] { removeRow(r); });
        table_->setCellWidget(r, 3, delBtn);
    }
    updateSumLabel();
}

void CustomIndexEditorDialog::updateSumLabel() {
    double sum = 0.0;
    for (const auto& c : pending_) sum += c.weight;
    sumLabel_->setText(tr("权重合计: %1").arg(weightText(sum)));
}

} // namespace st
