#pragma once

#include "foundation/enums.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/fmt.h>
#include <memory>
#include <string>
#include <utility>
#include <QObject>
#include <QCoreApplication>
#include <QMetaObject>
#include <QString>

namespace st {

/// Unified logging: console + file + UI sink (logMessage signal)
class LogManager : public QObject {
    Q_OBJECT

public:
    static LogManager* instance();

    explicit LogManager(QObject* parent = nullptr);
    ~LogManager() override;

    /// Initialize with log file path
    bool init(const std::string& logFilePath = "logs/stock_terminal.log");

    /// Log at level with fmt-style formatting; also emits logMessage to UI
    template<typename... Args>
    void log(LogLevel level, const char* fmt, Args&&... args) {
        auto logger = getLogger();
        switch (level) {
            case LogLevel::Trace:    logger->trace(fmt, std::forward<Args>(args)...); break;
            case LogLevel::Debug:    logger->debug(fmt, std::forward<Args>(args)...); break;
            case LogLevel::Info:     logger->info(fmt, std::forward<Args>(args)...);  break;
            case LogLevel::Warn:     logger->warn(fmt, std::forward<Args>(args)...);  break;
            case LogLevel::Error:    logger->error(fmt, std::forward<Args>(args)...); break;
            case LogLevel::Critical: logger->critical(fmt, std::forward<Args>(args)...); break;
        }
        // 格式化消息并投递到主线程 emit（日志面板实时展示）
        emitLogMessage(level, fmt::format(fmt, std::forward<Args>(args)...));
    }

    void setLevel(LogLevel level);
    void flush();

signals:
    /// 每条日志的主线程投递信号（日志面板 connect 此信号）
    void logMessage(LogLevel level, const QString& message);

private:
    /// 从任意线程安全地 emit logMessage（QueuedConnection 到主线程）。
    /// 目标用 QCoreApplication 实例而非 this —— LogManager 可能在 worker 线程首建。
    void emitLogMessage(LogLevel level, std::string message) {
        auto fn = [this, level, msg = std::move(message)] {
            emit logMessage(level, QString::fromUtf8(msg.data(), static_cast<int>(msg.size())));
        };
        if (auto* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, std::move(fn), Qt::QueuedConnection);
        } else {
            fn();
        }
    }

    std::shared_ptr<spdlog::logger> getLogger();
    std::shared_ptr<spdlog::logger> logger_;
    bool initialized_ = false;
};

} // namespace st
