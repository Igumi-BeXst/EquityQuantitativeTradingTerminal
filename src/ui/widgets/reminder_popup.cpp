#include "ui/widgets/reminder_popup.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <windows.h>   // MessageBeep

namespace st {

ReminderPopup::ReminderPopup(const QString& title, const QString& message,
                             QWidget* parent)
    : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(320);

    // 圆角深色背景
    setAutoFillBackground(false);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(6);

    titleLabel_ = new QLabel(title, this);
    titleLabel_->setStyleSheet("color:#ffffff;font-weight:bold;font-size:14px;");
    layout->addWidget(titleLabel_);

    messageLabel_ = new QLabel(message, this);
    messageLabel_->setWordWrap(true);
    messageLabel_->setStyleSheet("color:#dddddd;font-size:12px;");
    layout->addWidget(messageLabel_);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    closeBtn_ = new QPushButton(tr("知道了"), this);
    closeBtn_->setCursor(Qt::PointingHandCursor);
    btnRow->addWidget(closeBtn_);
    layout->addLayout(btnRow);

    connect(closeBtn_, &QPushButton::clicked, this, &QWidget::close);

    // 8 秒自动关闭
    autoClose_ = new QTimer(this);
    autoClose_->setSingleShot(true);
    autoClose_->setInterval(8000);
    connect(autoClose_, &QTimer::timeout, this, &QWidget::close);
}

void ReminderPopup::showReminder(const QString& title, const QString& message) {
    // 系统提示音
    MessageBeep(MB_ICONASTERISK);

    auto* popup = new ReminderPopup(title, message);
    popup->show();
    popup->raise();
    popup->activateWindow();
}

void ReminderPopup::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    positionBottomRight();
    autoClose_->start();
}

void ReminderPopup::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(32, 32, 36));
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8, 8);
    QWidget::paintEvent(event);
}

void ReminderPopup::positionBottomRight() {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();
    move(area.right() - width() - 20, area.bottom() - height() - 20);
}

} // namespace st
