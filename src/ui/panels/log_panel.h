#pragma once

#include "foundation/enums.h"
#include <QWidget>
#include <QVector>
#include <QPair>

class QPlainTextEdit;
class QComboBox;
class QCheckBox;
class QPushButton;

namespace st {

/// 日志面板 — 实时日志流 + 级别过滤 + 自动滚动 + 清空
///
/// connect LogManager::logMessage 信号，带级别颜色渲染。
/// 维护有界缓冲，切换级别过滤时重新渲染历史日志。
class LogPanel : public QWidget {
    Q_OBJECT

public:
    explicit LogPanel(QWidget* parent = nullptr);

private slots:
    void appendMessage(LogLevel level, const QString& message);
    void onFilterChanged();
    void onClearClicked();

private:
    /// 按级别生成带颜色的 HTML 行
    static QString formatted(LogLevel level, const QString& message);
    void reRender();

    QPlainTextEdit* view_ = nullptr;
    QComboBox* levelFilter_ = nullptr;
    QCheckBox* autoScroll_ = nullptr;
    int minLevel_ = 0;  // LogLevel::Trace
    QVector<QPair<LogLevel, QString>> buffer_;  // 有界缓冲（≤5000）
};

} // namespace st
