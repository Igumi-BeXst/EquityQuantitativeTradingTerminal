#include "ui/panels/task_window.h"
#include "core/task_scheduler.h"
#include "core/thread_pool.h"
#include "core/log_manager.h"
#include "data/idata_provider.h"
#include "foundation/stock_info.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimeEdit>
#include <QToolBar>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>
#include <algorithm>

namespace st {

// ============================================================
// 辅助函数（匿名命名空间）
// ============================================================

namespace {

/// 类型 → 中文
QString typeDisplayName(ScheduledTaskType t) {
    switch (t) {
    case ScheduledTaskType::RefreshQuotes: return QStringLiteral("刷新行情");
    case ScheduledTaskType::RunScreener:   return QStringLiteral("跑选股");
    case ScheduledTaskType::FetchData:     return QStringLiteral("抓数据");
    case ScheduledTaskType::Remind:        return QStringLiteral("提醒");
    }
    return QStringLiteral("未知");
}

/// 目标摘要 — 表格列显示用
QString targetSummary(const ScheduledTask& t) {
    if (t.type == ScheduledTaskType::RefreshQuotes)
        return QStringLiteral("—");
    if (t.type == ScheduledTaskType::Remind) {
        try {
            auto j = nlohmann::json::parse(t.target);
            std::string msg = j.value("message", "");
            if (!msg.empty()) return QString::fromStdString(msg);
        } catch (...) {}
        return QStringLiteral("(空)");
    }
    // RunScreener / FetchData
    if (t.target.empty()) return QStringLiteral("—");
    try {
        auto j = nlohmann::json::parse(t.target);
        std::string scope = j.value("scope", "");
        if (scope == "all")  return QStringLiteral("全部A股");
        if (scope == "last") return QStringLiteral("上次手动选股");
        if (scope == "sector")
            return QStringLiteral("板块:%1").arg(
                QString::fromStdString(j.value("sector", "")));
    } catch (...) {}
    return QString::fromStdString(t.target);
}

/// 时间/周期列显示
QString timeOrPeriod(const ScheduledTask& t) {
    if (t.kind == ScheduleKind::Daily)
        return QString::fromStdString(t.timeOfDay);
    int minutes = t.intervalSeconds / 60;
    if (minutes < 1) minutes = 1;
    return QStringLiteral("每 %1 分钟").arg(minutes);
}

/// 状态列
QString runningStatus(const ScheduledTask& t) {
    return t.running ? QStringLiteral("运行中") : QStringLiteral("空闲");
}

} // anonymous namespace

// ============================================================
// TaskEditDialog — 新建/编辑任务对话框
// ============================================================

TaskEditDialog::TaskEditDialog(IDataProvider* provider,
                               const ScheduledTask* existing,
                               QWidget* parent)
    : QDialog(parent), provider_(provider) {
    setWindowTitle(existing ? tr("编辑任务") : tr("新建任务"));
    setMinimumWidth(440);

    auto* mainLayout = new QVBoxLayout(this);

    // ---- 表单 ----
    auto* form = new QFormLayout;

    // 类型
    typeCombo_ = new QComboBox(this);
    typeCombo_->addItem(tr("刷新行情"));
    typeCombo_->addItem(tr("跑选股"));
    typeCombo_->addItem(tr("抓数据"));
    typeCombo_->addItem(tr("提醒"));
    form->addRow(tr("类型："), typeCombo_);

    // 触发方式
    kindCombo_ = new QComboBox(this);
    kindCombo_->addItem(tr("每天固定时间"));
    kindCombo_->addItem(tr("每 N 分钟"));
    form->addRow(tr("触发方式："), kindCombo_);

    // 时间（Daily）
    dailyRow_ = new QWidget(this);
    auto* dailyLayout = new QHBoxLayout(dailyRow_);
    dailyLayout->setContentsMargins(0, 0, 0, 0);
    timeEdit_ = new QTimeEdit(QTime(15, 5), dailyRow_);
    timeEdit_->setDisplayFormat("HH:mm");
    dailyLayout->addWidget(timeEdit_);
    dailyLayout->addStretch();
    form->addRow(tr("时间："), dailyRow_);

    // 间隔（Interval）
    intervalRow_ = new QWidget(this);
    auto* intervalLayout = new QHBoxLayout(intervalRow_);
    intervalLayout->setContentsMargins(0, 0, 0, 0);
    intervalSpin_ = new QSpinBox(intervalRow_);
    intervalSpin_->setRange(1, 10080);  // 1 分钟 ~ 1 周
    intervalSpin_->setValue(5);
    intervalSpin_->setSuffix(tr(" 分钟"));
    intervalLayout->addWidget(intervalSpin_);
    intervalLayout->addStretch();
    form->addRow(tr("间隔："), intervalRow_);

    // 选股范围（RunScreener / FetchData 可见）
    scopeRow_ = new QWidget(this);
    auto* scopeLayout = new QHBoxLayout(scopeRow_);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    scopeCombo_ = new QComboBox(scopeRow_);
    scopeCombo_->addItem(tr("全部A股"));
    scopeCombo_->addItem(tr("板块"));
    scopeCombo_->addItem(tr("上次手动选股"));
    sectorCombo_ = new QComboBox(scopeRow_);
    sectorCombo_->setMinimumWidth(140);
    sectorCombo_->addItem(tr("加载中…"), QString());
    sectorCombo_->setVisible(false);
    scopeLayout->addWidget(scopeCombo_);
    scopeLayout->addWidget(sectorCombo_);
    scopeLayout->addStretch();
    form->addRow(tr("选股范围："), scopeRow_);

    // 提醒内容（Remind 可见）
    remindRow_ = new QWidget(this);
    auto* remindLayout = new QHBoxLayout(remindRow_);
    remindLayout->setContentsMargins(0, 0, 0, 0);
    remindEdit_ = new QLineEdit(remindRow_);
    remindEdit_->setPlaceholderText(tr("提醒内容"));
    remindLayout->addWidget(remindEdit_);
    form->addRow(tr("提醒内容："), remindRow_);

    // 启用
    enabledCheck_ = new QCheckBox(tr("启用"), this);
    enabledCheck_->setChecked(true);
    form->addRow(QString(), enabledCheck_);

    mainLayout->addLayout(form);
    mainLayout->addStretch();

    // ---- 按钮 ----
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    mainLayout->addWidget(buttons);

    // ---- 信号连接 ----
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TaskEditDialog::onTypeChanged);
    connect(kindCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TaskEditDialog::onKindChanged);
    connect(scopeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        // 选「板块」时显示板块下拉
        sectorCombo_->setVisible(idx == 1);
    });

    // 确定 → 校验 + accept
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        const int typeIdx = typeCombo_->currentIndex();
        // 选股/抓数据 + 板块范围 → 必须实际选择板块（data 非空）
        if ((typeIdx == 1 || typeIdx == 2) && scopeCombo_->currentIndex() == 1) {
            if (sectorCombo_->currentData().toString().isEmpty()) {
                QMessageBox::warning(this, tr("校验失败"), tr("请选择板块"));
                return;
            }
        }
        // 提醒 → 内容非空
        if (typeIdx == 3) {
            if (remindEdit_->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, tr("校验失败"), tr("请输入提醒内容"));
                return;
            }
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ---- 回填（编辑模式） ----
    if (existing) {
        editId_ = existing->id;
        typeCombo_->setCurrentIndex(static_cast<int>(existing->type));
        kindCombo_->setCurrentIndex(static_cast<int>(existing->kind));

        if (existing->kind == ScheduleKind::Daily) {
            QTime preset = QTime::fromString(
                QString::fromStdString(existing->timeOfDay), "HH:mm");
            if (preset.isValid()) timeEdit_->setTime(preset);
        } else {
            int mins = existing->intervalSeconds / 60;
            if (mins < 1) mins = 1;
            intervalSpin_->setValue(mins);
        }

        enabledCheck_->setChecked(existing->enabled);

        // 解析 target JSON 回填参数区
        try {
            auto j = nlohmann::json::parse(existing->target);
            if (existing->type == ScheduledTaskType::Remind) {
                remindEdit_->setText(
                    QString::fromStdString(j.value("message", "")));
            } else if (existing->type == ScheduledTaskType::RunScreener ||
                       existing->type == ScheduledTaskType::FetchData) {
                std::string scope = j.value("scope", "");
                if (scope == "all") {
                    scopeCombo_->setCurrentIndex(0);
                } else if (scope == "sector") {
                    scopeCombo_->setCurrentIndex(1);
                    pendingSectorCode_ =
                        QString::fromStdString(j.value("sector", ""));
                } else if (scope == "last") {
                    scopeCombo_->setCurrentIndex(2);
                }
            }
        } catch (...) {}
    }

    // 初始可见性（必须在回填之后设置，否则 onTypeChanged 的 Reshow 会覆盖回填）
    onTypeChanged();
    onKindChanged();

    // 异步加载板块列表（延迟：让对话框先显示）
    loadBoards();
}

void TaskEditDialog::loadBoards() {
    if (!provider_) return;

    QPointer<TaskEditDialog> guard(this);
    ThreadPool::submitIO([guard]() {
        if (!guard) return;
        std::vector<StockInfo> boards;
        try {
            boards = guard->provider_->getSectorIndices();
        } catch (...) {
            // 静默失败，下拉保持空
        }
        QMetaObject::invokeMethod(guard.data(), [guard, b = std::move(boards)]() {
            if (!guard) return;
            guard->sectorCombo_->clear();
            guard->sectorCombo_->addItem(
                QObject::tr("请选择板块"), QString());
            for (const auto& info : b) {
                guard->sectorCombo_->addItem(
                    QString::fromStdString(info.name),
                    QString::fromStdString(info.code.code()));
            }
            // 编辑回填：尝试选中 pendingSectorCode_
            if (!guard->pendingSectorCode_.isEmpty()) {
                int matchIdx = guard->sectorCombo_->findData(
                    guard->pendingSectorCode_);
                if (matchIdx >= 0) {
                    guard->sectorCombo_->setCurrentIndex(matchIdx);
                } else {
                    // 未找到：临时追加显示
                    guard->sectorCombo_->addItem(guard->pendingSectorCode_,
                                                  guard->pendingSectorCode_);
                    guard->sectorCombo_->setCurrentIndex(
                        guard->sectorCombo_->count() - 1);
                }
                guard->pendingSectorCode_.clear();
            }
        }, Qt::QueuedConnection);
    });
}

void TaskEditDialog::onTypeChanged() {
    int idx = typeCombo_->currentIndex();
    // idx: 0=RefreshQuotes, 1=RunScreener, 2=FetchData, 3=Remind
    bool showScope = (idx == 1 || idx == 2);
    bool showRemind = (idx == 3);

    scopeRow_->setVisible(showScope);
    remindRow_->setVisible(showRemind);
}

void TaskEditDialog::onKindChanged() {
    auto k = static_cast<ScheduleKind>(kindCombo_->currentIndex());
    dailyRow_->setVisible(k == ScheduleKind::Daily);
    intervalRow_->setVisible(k == ScheduleKind::Interval);
}

ScheduledTask TaskEditDialog::task() const {
    ScheduledTask t;
    if (!editId_.empty()) t.id = editId_;
    t.type = static_cast<ScheduledTaskType>(typeCombo_->currentIndex());
    t.kind = static_cast<ScheduleKind>(kindCombo_->currentIndex());
    t.enabled = enabledCheck_->isChecked();

    if (t.kind == ScheduleKind::Daily) {
        t.timeOfDay = timeEdit_->time().toString("HH:mm").toStdString();
    } else {
        t.intervalSeconds = intervalSpin_->value() * 60;   // 分钟 → 秒
    }

    nlohmann::json target;
    switch (t.type) {
    case ScheduledTaskType::RefreshQuotes:
        target["scope"] = "all";
        break;
    case ScheduledTaskType::RunScreener:
    case ScheduledTaskType::FetchData: {
        int scopeIdx = scopeCombo_->currentIndex();
        if (scopeIdx == 0) {
            target["scope"] = "all";
        } else if (scopeIdx == 1) {
            target["scope"] = "sector";
            target["sector"] = sectorCombo_->currentData().toString().toStdString();
        } else {
            target["scope"] = "last";
        }
        break;
    }
    case ScheduledTaskType::Remind:
        target["message"] = remindEdit_->text().trimmed().toStdString();
        break;
    }
    t.target = target.dump();

    return t;
}

// ============================================================
// TaskWindow — 定时任务主窗口
// ============================================================

TaskWindow::TaskWindow(std::shared_ptr<TaskScheduler> scheduler,
                       IDataProvider* provider,
                       QWidget* parent)
    : QMainWindow(parent), scheduler_(std::move(scheduler)), provider_(provider) {
    setWindowTitle(tr("定时任务"));
    resize(780, 420);

    // ---- 中央控件 ----
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(4);

    // ---- 工具栏 ----
    auto* toolRow = new QHBoxLayout;
    auto* newBtn    = new QPushButton(tr("新建"), central);
    auto* editBtn   = new QPushButton(tr("编辑"), central);
    auto* deleteBtn = new QPushButton(tr("删除"), central);
    auto* runNowBtn = new QPushButton(tr("立即执行"), central);
    toolRow->addWidget(newBtn);
    toolRow->addWidget(editBtn);
    toolRow->addWidget(deleteBtn);
    toolRow->addWidget(runNowBtn);
    toolRow->addStretch();
    mainLayout->addLayout(toolRow);

    // ---- 表格：6 列 ----
    table_ = new QTableWidget(0, 6, central);
    table_->setHorizontalHeaderLabels({
        tr("类型"), tr("时间/周期"), tr("目标摘要"),
        tr("启用"), tr("上次结果"), tr("状态")});
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(table_, 1);

    // ---- 信号 ----

    // 新建
    connect(newBtn, &QPushButton::clicked, this, [this]() {
        TaskEditDialog dlg(provider_, nullptr, this);
        if (dlg.exec() == QDialog::Accepted) {
            auto t = dlg.task();
            // 生成 ID："T" + 序号
            if (t.id.empty()) {
                int maxN = 0;
                for (const auto& existing : scheduler_->tasks()) {
                    if (existing.id.size() > 1 && existing.id[0] == 'T') {
                        try {
                            int n = std::stoi(existing.id.substr(1));
                            if (n > maxN) maxN = n;
                        } catch (...) {}
                    }
                }
                t.id = "T" + std::to_string(maxN + 1);
            }
            scheduler_->addTask(t);
            LogManager::instance()->log(LogLevel::Info,
                "定时任务 新建: {} ({})", t.id,
                typeDisplayName(t.type).toStdString());
            rebuildAll();
        }
    });

    // 编辑
    connect(editBtn, &QPushButton::clicked, this, [this]() {
        const int row = table_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("提示"), tr("请先选中一行"));
            return;
        }
        // 从列 0 UserRole 取任务 id
        const QString selId = table_->item(row, 0)->data(Qt::UserRole).toString();
        const auto& all = scheduler_->tasks();
        auto it = std::find_if(all.begin(), all.end(),
            [&](const ScheduledTask& t2) { return t2.id == selId.toStdString(); });
        if (it == all.end()) return;

        TaskEditDialog dlg(provider_, &(*it), this);
        if (dlg.exec() == QDialog::Accepted) {
            auto updated = dlg.task();
            // 保 id（task() 已从 editId_ 带过来）
            scheduler_->updateTask(selId.toStdString(), updated);
            LogManager::instance()->log(LogLevel::Info,
                "定时任务 编辑: {}", selId.toStdString());
            rebuildAll();
        }
    });

    // 删除
    connect(deleteBtn, &QPushButton::clicked, this, [this]() {
        const int row = table_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("提示"), tr("请先选中一行"));
            return;
        }
        const QString selId = table_->item(row, 0)->data(Qt::UserRole).toString();
        const auto& all = scheduler_->tasks();
        // 先取展示字段（removeTask 后引用失效）
        QString dispType, dispTarget;
        {
            auto it = std::find_if(all.begin(), all.end(),
                [&](const ScheduledTask& t2) { return t2.id == selId.toStdString(); });
            if (it == all.end()) return;
            dispType   = typeDisplayName(it->type);
            dispTarget = targetSummary(*it);
        }
        auto reply = QMessageBox::question(this, tr("确认删除"),
            tr("确定要删除任务「%1 — %2」吗？").arg(dispType, dispTarget));
        if (reply == QMessageBox::Yes) {
            scheduler_->removeTask(selId.toStdString());
            LogManager::instance()->log(LogLevel::Info,
                "定时任务 删除: {}", selId.toStdString());
            rebuildAll();
        }
    });

    // 立即执行
    connect(runNowBtn, &QPushButton::clicked, this, [this]() {
        const int row = table_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, tr("提示"), tr("请先选中一行"));
            return;
        }
        const QString selId = table_->item(row, 0)->data(Qt::UserRole).toString();
        scheduler_->runNow(selId.toStdString());
        LogManager::instance()->log(LogLevel::Info,
            "定时任务 立即执行: {}", selId.toStdString());
        // 立即刷新表格（lastResult / running 可能已更新）
        rebuildAll();
    });

    // 初始加载
    rebuildAll();
}

TaskWindow::~TaskWindow() = default;

void TaskWindow::rebuildAll() {
    if (!scheduler_ || !table_) return;

    const auto& tasks = scheduler_->tasks();
    table_->setRowCount(0);

    for (int row = 0; row < static_cast<int>(tasks.size()); ++row) {
        const auto& t = tasks[row];
        table_->insertRow(row);

        // 列 0：类型（存 id 到 UserRole）
        auto* typeItem = new QTableWidgetItem(typeDisplayName(t.type));
        typeItem->setData(Qt::UserRole, QString::fromStdString(t.id));
        table_->setItem(row, 0, typeItem);

        // 列 1：时间/周期
        table_->setItem(row, 1,
            new QTableWidgetItem(timeOrPeriod(t)));

        // 列 2：目标摘要
        table_->setItem(row, 2,
            new QTableWidgetItem(targetSummary(t)));

        // 列 3：启用
        auto* enabledItem = new QTableWidgetItem(
            t.enabled ? QStringLiteral("✓") : QStringLiteral("✗"));
        enabledItem->setTextAlignment(Qt::AlignCenter);
        table_->setItem(row, 3, enabledItem);

        // 列 4：上次结果
        table_->setItem(row, 4,
            new QTableWidgetItem(QString::fromStdString(t.lastResult)));

        // 列 5：状态
        table_->setItem(row, 5,
            new QTableWidgetItem(runningStatus(t)));
    }
}

} // namespace st

#include "moc_task_window.cpp"
