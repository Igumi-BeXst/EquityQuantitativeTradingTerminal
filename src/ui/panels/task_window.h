#pragma once

#include "foundation/scheduler/scheduled_task.h"
#include <QDialog>
#include <QMainWindow>
#include <QString>
#include <memory>

class QCheckBox;
class QTableWidget;
class QLineEdit;
class QTimeEdit;
class QSpinBox;
class QComboBox;

namespace st {

class TaskScheduler;
class IDataProvider;

/// 定时任务窗口 — 设置菜单 → 独立窗口
/// 任务列表 + 新建/编辑/删除/立即执行
class TaskWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit TaskWindow(std::shared_ptr<TaskScheduler> scheduler,
                        IDataProvider* provider,
                        QWidget* parent = nullptr);
    ~TaskWindow() override;

private:
    void rebuildAll();

    std::shared_ptr<TaskScheduler> scheduler_;
    IDataProvider* provider_ = nullptr;
    QTableWidget* table_ = nullptr;
};

/// 新建/编辑任务对话框
class TaskEditDialog : public QDialog {
    Q_OBJECT
public:
    /// @param existing  非空时回填编辑；nullptr 为新建
    explicit TaskEditDialog(IDataProvider* provider,
                            const ScheduledTask* existing = nullptr,
                            QWidget* parent = nullptr);

    /// 获取填写结果（accept 后调用）
    ScheduledTask task() const;

private slots:
    void onTypeChanged();
    void onKindChanged();

private:
    void loadBoards();  // 异步加载板块列表

    IDataProvider* provider_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QComboBox* kindCombo_ = nullptr;
    QTimeEdit* timeEdit_ = nullptr;
    QSpinBox* intervalSpin_ = nullptr;   // UI 显示分钟，存储 intervalSeconds
    QWidget* dailyRow_ = nullptr;
    QWidget* intervalRow_ = nullptr;

    // 选股范围（RunScreener / FetchData）
    QWidget* scopeRow_ = nullptr;
    QComboBox* scopeCombo_ = nullptr;    // 全部A股 / 板块 / 上次手动选股
    QComboBox* sectorCombo_ = nullptr;   // 板块下拉（异步 fetchBoards）

    // 提醒（Remind）
    QWidget* remindRow_ = nullptr;
    QLineEdit* remindEdit_ = nullptr;

    // 启用开关
    QCheckBox* enabledCheck_ = nullptr;

    std::string editId_;              // 非空 = 编辑模式
    QString pendingSectorCode_;       // 编辑回填时待选中的板块代码（loadBoards 完成后再选中）
};

} // namespace st
