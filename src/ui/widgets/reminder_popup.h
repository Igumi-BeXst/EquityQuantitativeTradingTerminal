#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QTimer;

namespace st {

/// 定时提醒气泡 — 右下角非模态小窗口
/// 显示提醒内容 + 关闭按钮，数秒后自动消失；触发时播放系统提示音
/// WA_DeleteOnClose；用完即弃，不阻塞其他操作
class ReminderPopup : public QWidget {
    Q_OBJECT

public:
    explicit ReminderPopup(const QString& title, const QString& message,
                           QWidget* parent = nullptr);

    /// 便捷静态调用：创建并显示于屏幕右下角
    static void showReminder(const QString& title, const QString& message);

protected:
    void showEvent(QShowEvent* event) override;   // 显示时定位右下角 + 启动自动关闭
    void paintEvent(QPaintEvent* event) override; // 简单圆角背景

private:
    void positionBottomRight();
    void closeIfIdle();

    QLabel* titleLabel_ = nullptr;
    QLabel* messageLabel_ = nullptr;
    QPushButton* closeBtn_ = nullptr;
    QTimer* autoClose_ = nullptr;
};

} // namespace st
