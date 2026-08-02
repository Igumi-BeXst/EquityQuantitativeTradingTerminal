#include "ui/panels/log_panel.h"
#include "core/log_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QScrollBar>

namespace st {

namespace {
constexpr int kMaxBuffer = 5000;
}

LogPanel::LogPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // 工具栏: 级别过滤 | 自动滚动 | 清空
    auto* toolbar = new QHBoxLayout();
    toolbar->addWidget(new QLabel(tr("级别:")));
    levelFilter_ = new QComboBox();
    levelFilter_->addItem(tr("全部"), static_cast<int>(LogLevel::Trace));
    levelFilter_->addItem(tr("信息及以上"), static_cast<int>(LogLevel::Info));
    levelFilter_->addItem(tr("警告及以上"), static_cast<int>(LogLevel::Warn));
    levelFilter_->addItem(tr("错误及以上"), static_cast<int>(LogLevel::Error));
    autoScroll_ = new QCheckBox(tr("自动滚动"));
    autoScroll_->setChecked(true);
    auto* clearBtn = new QPushButton(tr("清空"));
    toolbar->addWidget(levelFilter_);
    toolbar->addStretch();
    toolbar->addWidget(autoScroll_);
    toolbar->addWidget(clearBtn);
    layout->addLayout(toolbar);

    view_ = new QPlainTextEdit();
    view_->setReadOnly(true);
    view_->document()->setMaximumBlockCount(kMaxBuffer);
    layout->addWidget(view_);

    connect(levelFilter_, &QComboBox::currentIndexChanged,
            this, &LogPanel::onFilterChanged);
    connect(clearBtn, &QPushButton::clicked, this, &LogPanel::onClearClicked);
    // 双保险: LogManager 内部已 Queued 到主线程，这里再 Queued 跨线程安全
    connect(LogManager::instance(), &LogManager::logMessage,
            this, &LogPanel::appendMessage, Qt::QueuedConnection);

    onFilterChanged();  // 初始化 minLevel_
}

QString LogPanel::formatted(LogLevel level, const QString& message) {
    QString color;
    switch (level) {
        case LogLevel::Trace:
        case LogLevel::Debug:
            color = QStringLiteral("#9e9e9e");  // 灰
            break;
        case LogLevel::Info:
            break;  // 默认前景色
        case LogLevel::Warn:
            color = QStringLiteral("#fb8c00");  // 橙
            break;
        case LogLevel::Error:
        case LogLevel::Critical:
            color = QStringLiteral("#e53935");  // 红
            break;
    }
    QString escaped = message.toHtmlEscaped();
    if (color.isEmpty()) {
        return escaped + QStringLiteral("<br>");
    }
    return QStringLiteral("<span style='color:%1'>%2</span><br>")
        .arg(color, escaped);
}

void LogPanel::appendMessage(LogLevel level, const QString& message) {
    buffer_.append({level, message});
    if (buffer_.size() > kMaxBuffer) {
        buffer_.remove(0, buffer_.size() - kMaxBuffer);
    }
    if (static_cast<int>(level) < minLevel_) return;  // 过滤

    view_->appendHtml(formatted(level, message));
    if (autoScroll_->isChecked()) {
        auto* sb = view_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }
}

void LogPanel::onFilterChanged() {
    minLevel_ = levelFilter_->currentData().toInt();
    reRender();
}

void LogPanel::reRender() {
    view_->clear();
    for (const auto& entry : buffer_) {
        if (static_cast<int>(entry.first) >= minLevel_) {
            view_->appendHtml(formatted(entry.first, entry.second));
        }
    }
    auto* sb = view_->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void LogPanel::onClearClicked() {
    buffer_.clear();
    view_->clear();
}

} // namespace st

#include "moc_log_panel.cpp"
