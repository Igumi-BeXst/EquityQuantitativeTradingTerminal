#include "ui/panels/journal_window.h"
#include "ui/panels/stock_search_bar.h"
#include "ui/widgets/equity_curve_widget.h"
#include "data/idata_provider.h"
#include "engine/journal/trade_journal.h"
#include "engine/backtest/fee_calculator.h"
#include "core/log_manager.h"
#include "core/app_paths.h"
#include "engine/journal/trade_journal_store.h"
#include "foundation/stock_info.h"
#include "foundation/utils/datetime.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace st {

// ============================================================
// JournalEntryDialog — 新建/编辑交易记录弹出框
// ============================================================

JournalEntryDialog::JournalEntryDialog(const FeeConfig& fees,
                                       IDataProvider* provider,
                                       QWidget* parent)
    : QDialog(parent), feesConfig_(fees) {
    setWindowTitle(tr("交易记录"));
    setMinimumWidth(400);

    auto* mainLayout = new QVBoxLayout(this);

    // 股票搜索栏
    auto* searchRow = new QHBoxLayout;
    searchRow->addWidget(new QLabel(tr("股票："), this));
    searchBar_ = new StockSearchBar(provider, this);
    searchRow->addWidget(searchBar_, 1);
    mainLayout->addLayout(searchRow);

    // 表单布局
    auto* form = new QFormLayout;

    // 方向
    directionCombo_ = new QComboBox(this);
    directionCombo_->addItem(tr("买入"));
    directionCombo_->addItem(tr("卖出"));
    form->addRow(tr("方向："), directionCombo_);

    // 价格
    priceSpin_ = new QDoubleSpinBox(this);
    priceSpin_->setRange(0.0, 999999.99);
    priceSpin_->setDecimals(2);
    priceSpin_->setSingleStep(0.01);
    priceSpin_->setValue(0.0);
    form->addRow(tr("价格："), priceSpin_);

    // 数量
    volumeSpin_ = new QSpinBox(this);
    volumeSpin_->setRange(0, 99999999);
    volumeSpin_->setSingleStep(100);
    volumeSpin_->setValue(0);
    form->addRow(tr("数量："), volumeSpin_);

    // 费用（自动算预填可改）
    feesSpin_ = new QDoubleSpinBox(this);
    feesSpin_->setRange(0.0, 999999.99);
    feesSpin_->setDecimals(2);
    feesSpin_->setSingleStep(0.01);
    feesSpin_->setValue(0.0);
    feesSpin_->setToolTip(tr("根据方向/价格/数量自动计算，可手动修改"));
    form->addRow(tr("费用："), feesSpin_);

    // 策略
    strategyEdit_ = new QLineEdit(this);
    strategyEdit_->setPlaceholderText(tr("关联策略名（可空）"));
    form->addRow(tr("策略："), strategyEdit_);

    // 注解
    noteEdit_ = new QLineEdit(this);
    noteEdit_->setPlaceholderText(tr("交易备注"));
    form->addRow(tr("注解："), noteEdit_);

    mainLayout->addLayout(form);

    // 确认/取消按钮
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    mainLayout->addWidget(buttons);

    // 连接信号
    connect(searchBar_, &StockSearchBar::stockSelected,
            this, &JournalEntryDialog::onStockSelected);

    // 价格/数量/方向变化 → 重算费用
    connect(priceSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &JournalEntryDialog::recalcFees);
    connect(volumeSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &JournalEntryDialog::recalcFees);
    connect(directionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &JournalEntryDialog::recalcFees);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        // 校验：必须有股票代码
        if (!code_.isValid()) {
            QMessageBox::warning(this, tr("校验失败"), tr("请先选择股票"));
            return;
        }
        if (priceSpin_->value() <= 0.0) {
            QMessageBox::warning(this, tr("校验失败"), tr("价格必须大于 0"));
            return;
        }
        if (volumeSpin_->value() <= 0) {
            QMessageBox::warning(this, tr("校验失败"), tr("数量必须大于 0"));
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void JournalEntryDialog::onStockSelected(const StockInfo& info) {
    code_ = info.code;
    name_ = QString::fromStdString(info.name);
    // 搜索栏选中后触发费用重算（如果价格/数量已有值）
    recalcFees();
}

void JournalEntryDialog::recalcFees() {
    // 必须有股票和有效价格才能算
    if (!code_.isValid() || priceSpin_->value() <= 0.0 || volumeSpin_->value() <= 0) {
        return;
    }
    // 构造临时 Trade 给 FeeCalculator
    Trade trade;
    trade.code = code_;
    trade.direction = (directionCombo_->currentIndex() == 0)
                          ? Direction::Buy
                          : Direction::Sell;
    trade.price = priceSpin_->value();
    trade.volume = volumeSpin_->value();

    FeeCalculator calc(feesConfig_);
    const double fee = calc.calculateTotal(trade);

    // 预填费用框（不触发递归 valueChanged）
    feesSpin_->blockSignals(true);
    feesSpin_->setValue(fee);
    feesSpin_->blockSignals(false);
}

void JournalEntryDialog::setEntry(const JournalEntry& entry) {
    editId_ = entry.id;
    editTime_ = entry.time;
    code_ = entry.code;
    name_ = QString::fromStdString(entry.name);

    // 设置搜索栏文本
    if (auto* searchEdit = searchBar_->findChild<QLineEdit*>()) {
        searchEdit->setText(
            QString::fromStdString(entry.code.displayCode()) + " " + name_);
    }

    directionCombo_->setCurrentIndex(entry.direction == Direction::Buy ? 0 : 1);
    priceSpin_->setValue(entry.price);
    volumeSpin_->setValue(static_cast<int>(entry.volume));
    feesSpin_->setValue(entry.fees);
    strategyEdit_->setText(QString::fromStdString(entry.strategy));
    noteEdit_->setText(QString::fromStdString(entry.note));
}

JournalEntry JournalEntryDialog::entry() const {
    JournalEntry e;
    e.code = code_;
    e.name = name_.toStdString();
    e.type = JournalType::ManualNote;
    e.direction = (directionCombo_->currentIndex() == 0)
                      ? Direction::Buy
                      : Direction::Sell;
    e.price = priceSpin_->value();
    e.volume = volumeSpin_->value();
    e.fees = feesSpin_->value();
    e.strategy = strategyEdit_->text().toStdString();
    e.note = noteEdit_->text().toStdString();
    // 编辑模式保留原时间，新建用当前时间
    e.time = editId_.empty() ? utils::now() : editTime_;
    // 编辑模式保留原 id（传给 updateEntry 用），新建留空由引擎生成
    if (!editId_.empty()) {
        e.id = editId_;
    }
    return e;
}

// ============================================================
// JournalFeeDialog — 费率设置对话框
// ============================================================

JournalFeeDialog::JournalFeeDialog(const FeeConfig& fees, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("费率设置"));
    setMinimumWidth(360);

    auto* form = new QFormLayout(this);

    commissionRateSpin_ = new QDoubleSpinBox(this);
    commissionRateSpin_->setRange(0.0, 0.01);
    commissionRateSpin_->setDecimals(5);
    commissionRateSpin_->setSingleStep(0.00001);
    commissionRateSpin_->setValue(fees.commissionRate);
    commissionRateSpin_->setToolTip(tr("成交金额比例，万2.5 = 0.00025"));
    form->addRow(tr("佣金费率："), commissionRateSpin_);

    minCommissionSpin_ = new QDoubleSpinBox(this);
    minCommissionSpin_->setRange(0.0, 100.0);
    minCommissionSpin_->setDecimals(2);
    minCommissionSpin_->setSingleStep(0.5);
    minCommissionSpin_->setValue(fees.minCommission);
    minCommissionSpin_->setToolTip(tr("每笔最低佣金（元）"));
    form->addRow(tr("最低佣金："), minCommissionSpin_);

    stampTaxRateSpin_ = new QDoubleSpinBox(this);
    stampTaxRateSpin_->setRange(0.0, 0.01);
    stampTaxRateSpin_->setDecimals(4);
    stampTaxRateSpin_->setSingleStep(0.0001);
    stampTaxRateSpin_->setValue(fees.stampTaxRate);
    stampTaxRateSpin_->setToolTip(tr("仅卖出收取，千一 = 0.001"));
    form->addRow(tr("印花税率："), stampTaxRateSpin_);

    transferFeeRateSpin_ = new QDoubleSpinBox(this);
    transferFeeRateSpin_->setRange(0.0, 0.001);
    transferFeeRateSpin_->setDecimals(5);
    transferFeeRateSpin_->setSingleStep(0.00001);
    transferFeeRateSpin_->setValue(fees.transferFeeRate);
    transferFeeRateSpin_->setToolTip(tr("双向收取，十万分之二 = 0.00002"));
    form->addRow(tr("过户费率："), transferFeeRateSpin_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

FeeConfig JournalFeeDialog::feeConfig() const {
    FeeConfig cfg;
    cfg.commissionRate = commissionRateSpin_->value();
    cfg.minCommission = minCommissionSpin_->value();
    cfg.stampTaxRate = stampTaxRateSpin_->value();
    cfg.transferFeeRate = transferFeeRateSpin_->value();
    return cfg;
}

// ============================================================
// JournalWindow — 交易日志主窗口
// ============================================================

namespace {
const char* kAutoTradeColor = "#42a5f5";   // 蓝 — 自动成交
const char* kManualNoteColor = "#ef5350";  // 红 — 手动录入
const char* kSignalColor = "#66bb6a";      // 绿 — 策略信号

/// JournalType → 中文
QString typeText(JournalType t) {
    switch (t) {
    case JournalType::AutoTrade:  return QStringLiteral("自动交易");
    case JournalType::ManualNote: return QStringLiteral("手动录入");
    case JournalType::Signal:     return QStringLiteral("策略信号");
    }
    return QStringLiteral("未知");
}

/// Direction → 中文
QString dirText(Direction d) {
    return d == Direction::Buy ? QStringLiteral("买入") : QStringLiteral("卖出");
}
}  // namespace

JournalWindow::JournalWindow(std::shared_ptr<TradeJournalEngine> journal,
                             QWidget* parent)
    : QMainWindow(parent), journal_(std::move(journal)) {
    setWindowTitle(tr("交易日志"));
    resize(1000, 620);

    tabs_ = new QTabWidget(this);
    setCentralWidget(tabs_);

    // ---- 交易记录 tab ---------------------------------------------------
    auto* recordsPage = new QWidget(tabs_);
    auto* recordsLayout = new QVBoxLayout(recordsPage);
    recordsLayout->setContentsMargins(6, 6, 6, 6);
    recordsLayout->setSpacing(4);

    // 工具栏：新建/编辑/删除/清空 + 筛选
    auto* toolRow = new QHBoxLayout;
    auto* newBtn = new QPushButton(tr("新建"), recordsPage);
    auto* editBtn = new QPushButton(tr("编辑"), recordsPage);
    auto* deleteBtn = new QPushButton(tr("删除"), recordsPage);
    auto* clearBtn = new QPushButton(tr("清空"), recordsPage);

    toolRow->addWidget(newBtn);
    toolRow->addWidget(editBtn);
    toolRow->addWidget(deleteBtn);
    toolRow->addWidget(clearBtn);

    // 费率设置按钮
    auto* feeBtn = new QPushButton(tr("费率设置"), recordsPage);
    toolRow->addWidget(feeBtn);

    filter_ = new QLineEdit(recordsPage);
    filter_->setPlaceholderText(tr("筛选 代码/名称/策略/注解"));
    filter_->setClearButtonEnabled(true);
    toolRow->addWidget(filter_);
    toolRow->addStretch();
    recordsLayout->addLayout(toolRow);

    // 表格 10 列
    recordsTable_ = new QTableWidget(0, 10, recordsPage);
    recordsTable_->setHorizontalHeaderLabels({
        tr("时间"), tr("代码"), tr("名称"), tr("类型"),
        tr("方向"), tr("价格"), tr("数量"), tr("费用"),
        tr("策略"), tr("注解")});
    recordsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    recordsTable_->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Stretch);
    recordsTable_->verticalHeader()->setVisible(false);
    recordsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recordsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    recordsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    recordsLayout->addWidget(recordsTable_, 1);

    tabs_->addTab(recordsPage, tr("交易记录"));

    // ---- 对比回顾 tab ---------------------------------------------------
    auto* statsPage = new QWidget(tabs_);
    auto* statsLayout = new QVBoxLayout(statsPage);
    statsLayout->setContentsMargins(6, 6, 6, 6);
    statsLayout->setSpacing(4);

    // 1) 统计卡行（3 个 QLabel 并排）
    auto* statCardRow = new QHBoxLayout;
    auto makeCard = [&](QLabel*& label, const QString& title) {
        auto* card = new QWidget(statsPage);
        card->setStyleSheet(
            "QWidget { background: #1e1e20; border: 1px solid #3a3a3d; "
            "border-radius: 4px; }");
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(8, 4, 8, 4);
        auto* titleLabel = new QLabel(title, card);
        titleLabel->setStyleSheet("color: #888; font-size: 11px;");
        cardLayout->addWidget(titleLabel);
        label = new QLabel(card);
        label->setStyleSheet("color: #d4d4d4; font-size: 13px; font-weight: bold;");
        label->setTextFormat(Qt::RichText);
        cardLayout->addWidget(label);
        statCardRow->addWidget(card, 1);
    };
    makeCard(overallLabel_, tr("总体"));
    makeCard(simLabel_, tr("模拟"));
    makeCard(manualLabel_, tr("实盘"));
    statsLayout->addLayout(statCardRow);

    // 2) 双序列收益曲线
    curve_ = new EquityCurveWidget(statsPage);
    curve_->setMinimumHeight(150);
    curve_->setMaximumHeight(220);
    statsLayout->addWidget(curve_);

    // 3) 逐笔配对表 8 列
    pairTable_ = new QTableWidget(0, 8, statsPage);
    pairTable_->setHorizontalHeaderLabels({
        tr("代码"), tr("方向"), tr("模拟价"), tr("实盘价"),
        tr("价差"), tr("差距%"), tr("数量"), tr("时间")});
    pairTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    pairTable_->verticalHeader()->setVisible(false);
    pairTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pairTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pairTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    statsLayout->addWidget(pairTable_, 1);

    // 4) 月度收益 + 持仓已实现 并排
    auto* bottomRow = new QHBoxLayout;
    monthlyTable_ = new QTableWidget(0, 2, statsPage);
    monthlyTable_->setHorizontalHeaderLabels({tr("月份"), tr("盈亏")});
    monthlyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    monthlyTable_->verticalHeader()->setVisible(false);
    monthlyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    monthlyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    monthlyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    bottomRow->addWidget(monthlyTable_);

    realizedTable_ = new QTableWidget(0, 4, statsPage);
    realizedTable_->setHorizontalHeaderLabels({
        tr("代码"), tr("名称"), tr("已实现盈亏"), tr("回合数")});
    realizedTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    realizedTable_->verticalHeader()->setVisible(false);
    realizedTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    realizedTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    realizedTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    bottomRow->addWidget(realizedTable_);
    statsLayout->addLayout(bottomRow, 1);

    // 5) 按策略表
    strategyTable_ = new QTableWidget(0, 4, statsPage);
    strategyTable_->setHorizontalHeaderLabels({
        tr("策略"), tr("胜率"), tr("盈亏"), tr("交易数")});
    strategyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    strategyTable_->verticalHeader()->setVisible(false);
    strategyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    strategyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    strategyTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    statsLayout->addWidget(strategyTable_, 1);

    tabs_->addTab(statsPage, tr("对比回顾"));

    // ---- 连接信号 -------------------------------------------------------
    // 筛选
    connect(filter_, &QLineEdit::textChanged, this, [this]() { rebuildAll(); });

    // 新建
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        JournalEntryDialog dlg(journal_->fees(), nullptr, this);
        if (dlg.exec() == QDialog::Accepted) {
            const auto e = dlg.entry();
            const auto id = journal_->addEntry(e);
            LogManager::instance()->log(LogLevel::Info,
                "交易日志 新建记录: {} {} {}", id, e.code.displayCode(), e.name);
            rebuildAll();
        }
    });

    // 编辑
    connect(editBtn, &QPushButton::clicked, this, [this]() {
        const int row = recordsTable_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("提示"), tr("请先选中一行"));
            return;
        }
        // 从 UserRole 取 id
        const auto idVar = recordsTable_->item(row, 0)->data(Qt::UserRole);
        const auto selId = idVar.toString().toStdString();
        // 在 entries 里找对应条目
        const auto& allEntries = journal_->entries();
        auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&](const JournalEntry& e) { return e.id == selId; });
        if (it == allEntries.end()) {
            return;
        }
        JournalEntryDialog dlg(journal_->fees(), nullptr, this);
        dlg.setEntry(*it);
        if (dlg.exec() == QDialog::Accepted) {
            const auto updated = dlg.entry();
            journal_->updateEntry(selId, updated);
            LogManager::instance()->log(LogLevel::Info,
                "交易日志 编辑记录: {} {} {}", selId, updated.code.displayCode(), updated.name);
            rebuildAll();
        }
    });

    // 删除
    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        const int row = recordsTable_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("提示"), tr("请先选中一行"));
            return;
        }
        const auto idVar = recordsTable_->item(row, 0)->data(Qt::UserRole);
        const auto selId = idVar.toString().toStdString();
        const auto& allEntries = journal_->entries();
        auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&](const JournalEntry& e) { return e.id == selId; });
        if (it == allEntries.end()) {
            return;
        }
        auto reply = QMessageBox::question(this, tr("确认删除"),
            tr("确定要删除记录「%1 %2」吗？")
                .arg(QString::fromStdString(it->code.displayCode()),
                     QString::fromStdString(it->name)));
        if (reply == QMessageBox::Yes) {
            journal_->removeEntry(selId);
            LogManager::instance()->log(LogLevel::Info,
                "交易日志 删除记录: {} {} {}", selId, it->code.displayCode(), it->name);
            rebuildAll();
        }
    });

    // 清空
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        const auto& allEntries = journal_->entries();
        if (allEntries.empty()) {
            return;
        }
        auto reply = QMessageBox::question(this, tr("确认清空"),
            tr("确定要清空全部 %1 条记录吗？此操作不可撤销。").arg(allEntries.size()));
        if (reply == QMessageBox::Yes) {
            journal_->clear();
            LogManager::instance()->log(LogLevel::Info,
                "交易日志 清空全部记录");
            rebuildAll();
        }
    });

    // 费率设置
    connect(feeBtn, &QPushButton::clicked, this, [this]() {
        JournalFeeDialog dlg(journal_->fees(), this);
        if (dlg.exec() == QDialog::Accepted) {
            const auto cfg = dlg.feeConfig();
            journal_->setFees(cfg);
            const std::string configPath = AppPaths::configDir() + "/journal_config.json";
            if (!TradeJournalStore::saveFeeConfig(configPath, cfg)) {
                LogManager::instance()->log(LogLevel::Warn,
                    "交易日志 费率保存失败: {}", configPath);
            } else {
                LogManager::instance()->log(LogLevel::Info, "交易日志 费率已更新并保存");
            }
        }
    });

    // 双击行 → 开图
    connect(recordsTable_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) {
                if (row < 0 || row >= recordsTable_->rowCount()) return;
                const auto idVar = recordsTable_->item(row, 0)->data(Qt::UserRole);
                const auto selId = idVar.toString().toStdString();
                const auto& allEntries = journal_->entries();
                auto it = std::find_if(allEntries.begin(), allEntries.end(),
                                       [&](const JournalEntry& e) { return e.id == selId; });
                if (it == allEntries.end() || !it->code.isValid()) return;
                emit openChart(it->code, QString::fromStdString(it->name));
            });

    // 初始加载
    rebuildAll();
}

JournalWindow::~JournalWindow() = default;

void JournalWindow::rebuildAll() {
    if (!journal_ || !recordsTable_) return;

    // 获取并排序（时间降序）
    auto entries = journal_->entries();
    std::sort(entries.begin(), entries.end(),
              [](const JournalEntry& a, const JournalEntry& b) {
                  return a.time > b.time;
              });

    // 筛选
    const QString filterText = filter_ ? filter_->text().trimmed() : QString();
    QStringList filters;
    if (!filterText.isEmpty()) {
        filters = filterText.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    }

    recordsTable_->setRowCount(0);
    int rowIdx = 0;

    for (const auto& e : entries) {
        // 应用筛选
        if (!filters.isEmpty()) {
            bool match = false;
            const QString codeStr = QString::fromStdString(e.code.displayCode());
            const QString codeRaw = QString::fromStdString(e.code.code());
            const QString nameStr = QString::fromStdString(e.name);
            const QString stratStr = QString::fromStdString(e.strategy);
            const QString noteStr = QString::fromStdString(e.note);
            for (const auto& kw : filters) {
                if (codeStr.contains(kw, Qt::CaseInsensitive) ||
                    codeRaw.contains(kw, Qt::CaseInsensitive) ||
                    nameStr.contains(kw, Qt::CaseInsensitive) ||
                    stratStr.contains(kw, Qt::CaseInsensitive) ||
                    noteStr.contains(kw, Qt::CaseInsensitive)) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;
        }

        recordsTable_->insertRow(rowIdx);

        // 列 0：时间
        auto* timeItem = new QTableWidgetItem(
            QString::fromStdString(utils::toDateTimeString(e.time)));
        timeItem->setData(Qt::UserRole, QString::fromStdString(e.id));
        recordsTable_->setItem(rowIdx, 0, timeItem);

        // 列 1：代码
        recordsTable_->setItem(rowIdx, 1,
            new QTableWidgetItem(QString::fromStdString(e.code.displayCode())));

        // 列 2：名称
        recordsTable_->setItem(rowIdx, 2,
            new QTableWidgetItem(QString::fromStdString(e.name)));

        // 列 3：类型（徽标颜色）
        auto* typeItem = new QTableWidgetItem(typeText(e.type));
        QColor typeColor;
        switch (e.type) {
        case JournalType::AutoTrade:  typeColor = QColor(kAutoTradeColor); break;
        case JournalType::ManualNote: typeColor = QColor(kManualNoteColor); break;
        case JournalType::Signal:     typeColor = QColor(kSignalColor); break;
        }
        typeItem->setForeground(typeColor);
        recordsTable_->setItem(rowIdx, 3, typeItem);

        // 列 4：方向
        recordsTable_->setItem(rowIdx, 4, new QTableWidgetItem(dirText(e.direction)));

        // 列 5：价格
        recordsTable_->setItem(rowIdx, 5,
            new QTableWidgetItem(QString::number(e.price, 'f', 2)));

        // 列 6：数量
        recordsTable_->setItem(rowIdx, 6,
            new QTableWidgetItem(QString::number(e.volume)));

        // 列 7：费用
        recordsTable_->setItem(rowIdx, 7,
            new QTableWidgetItem(QString::number(e.fees, 'f', 2)));

        // 列 8：策略
        recordsTable_->setItem(rowIdx, 8,
            new QTableWidgetItem(QString::fromStdString(e.strategy)));

        // 列 9：注解
        recordsTable_->setItem(rowIdx, 9,
            new QTableWidgetItem(QString::fromStdString(e.note)));

        ++rowIdx;
    }

    LogManager::instance()->log(LogLevel::Debug,
        "交易日志 刷新表格: {} 条（过滤后 {} 条）", journal_->entries().size(), rowIdx);

    // 刷新对比回顾 tab
    refreshStats();
}

void JournalWindow::refreshStats() {
    if (!journal_) return;

    const auto stats = computeStats(journal_->entries());

    // 辅助：格式化盈亏金额（带正负号 + 分隔符）
    auto fmtPnl = [](double v) -> QString {
        return QString::number(v, 'f', 2);
    };
    // 辅助：格式化百分比
    auto fmtPct = [](double v) -> QString {
        return QString::number(v * 100.0, 'f', 1) + "%";
    };
    // 辅助：填充 GroupStats 到卡片 QLabel（RichText）
    auto fillCard = [&](QLabel* label, const GroupStats& g) {
        const QString text = QStringLiteral(
            "<span style='color:#d4d4d4;'>胜率 %1</span>"
            "&nbsp;&nbsp;<span style='color:#%2;'>盈亏 %3</span>"
            "&nbsp;&nbsp;<span style='color:#888;'>回撤 %4</span>")
            .arg(fmtPct(g.winRate))
            .arg(g.totalPnl >= 0 ? "26a69a" : "ef5350")
            .arg(fmtPnl(g.totalPnl))
            .arg(fmtPnl(g.maxDrawdown));
        label->setText(text);
    };

    fillCard(overallLabel_, stats.overall);
    fillCard(simLabel_, stats.sim);
    fillCard(manualLabel_, stats.manual);

    // ---- 收益曲线（双序列，负值可渲染 — EquityCurveWidget 已支持）----
    {
        auto simCum = stats.sim.cumPnl;
        auto manCum = stats.manual.cumPnl;
        if (simCum.empty()) simCum = {0.0};
        if (manCum.empty()) manCum = {0.0};

        curve_->setSeries({
            {tr("模拟"), QColor("#42a5f5"), simCum},
            {tr("实盘"), QColor("#ef5350"), manCum}});
    }

    // ---- 逐笔配对表 ----
    pairTable_->setRowCount(0);
    for (int i = 0; i < static_cast<int>(stats.pairs.size()); ++i) {
        const auto& pr = stats.pairs[i];
        pairTable_->insertRow(i);

        auto setItem = [&](int col, const QString& text, const QColor& fg = QColor()) {
            auto* item = new QTableWidgetItem(text);
            if (fg.isValid()) item->setForeground(fg);
            pairTable_->setItem(i, col, item);
        };

        setItem(0, QString::fromStdString(pr.code.displayCode()));
        setItem(1, dirText(pr.direction));

        setItem(2, QString::number(pr.simPrice, 'f', 2));
        setItem(3, QString::number(pr.manualPrice, 'f', 2));

        // 价差着色：>0 红 #ef5350（实盘价 > 模拟价），<0 绿 #26a69a
        const QColor diffColor = pr.priceDiff > 0 ? QColor("#ef5350")
            : (pr.priceDiff < 0 ? QColor("#26a69a") : QColor());
        setItem(4, QString::number(pr.priceDiff, 'f', 2), diffColor);
        setItem(5, QString::number(pr.diffPct, 'f', 2) + "%", diffColor);

        setItem(6, QString::number(pr.matchedVol));
        setItem(7, QString::fromStdString(utils::toDateTimeString(pr.manualTime)));
    }

    // ---- 月度收益表 ----
    monthlyTable_->setRowCount(0);
    for (int i = 0; i < static_cast<int>(stats.monthly.size()); ++i) {
        const auto& m = stats.monthly[i];
        monthlyTable_->insertRow(i);
        monthlyTable_->setItem(i, 0,
            new QTableWidgetItem(QString::fromStdString(m.ym)));
        auto* pnlItem = new QTableWidgetItem(fmtPnl(m.pnl));
        pnlItem->setForeground(m.pnl >= 0 ? QColor("#26a69a") : QColor("#ef5350"));
        monthlyTable_->setItem(i, 1, pnlItem);
    }

    // ---- 持仓已实现表 ----
    realizedTable_->setRowCount(0);
    for (int i = 0; i < static_cast<int>(stats.realized.size()); ++i) {
        const auto& r = stats.realized[i];
        realizedTable_->insertRow(i);
        realizedTable_->setItem(i, 0,
            new QTableWidgetItem(QString::fromStdString(r.code.displayCode())));
        realizedTable_->setItem(i, 1,
            new QTableWidgetItem(QString::fromStdString(r.name)));
        auto* pnlItem = new QTableWidgetItem(fmtPnl(r.pnl));
        pnlItem->setForeground(r.pnl >= 0 ? QColor("#26a69a") : QColor("#ef5350"));
        realizedTable_->setItem(i, 2, pnlItem);
        realizedTable_->setItem(i, 3,
            new QTableWidgetItem(QString::number(r.roundTrips)));
    }

    // ---- 按策略表 ----
    strategyTable_->setRowCount(0);
    for (int i = 0; i < static_cast<int>(stats.byStrategy.size()); ++i) {
        const auto& [name, gs] = stats.byStrategy[i];
        strategyTable_->insertRow(i);
        strategyTable_->setItem(i, 0,
            new QTableWidgetItem(QString::fromStdString(name)));
        strategyTable_->setItem(i, 1,
            new QTableWidgetItem(fmtPct(gs.winRate)));
        auto* pnlItem = new QTableWidgetItem(fmtPnl(gs.totalPnl));
        pnlItem->setForeground(gs.totalPnl >= 0 ? QColor("#26a69a") : QColor("#ef5350"));
        strategyTable_->setItem(i, 2, pnlItem);
        strategyTable_->setItem(i, 3,
            new QTableWidgetItem(QString::number(gs.count)));
    }
}

} // namespace st

#include "moc_journal_window.cpp"
