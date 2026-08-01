#pragma once

#include "foundation/enums.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <string>
#include <QObject>

namespace st {

/// Unified logging: console + file + (later) UI sink
class LogManager : public QObject {
    Q_OBJECT

public:
    static LogManager* instance();

    explicit LogManager(QObject* parent = nullptr);
    ~LogManager() override;

    /// Initialize with log file path
    bool init(const std::string& logFilePath = "logs/stock_terminal.log");

    /// Log at level with fmt-style formatting
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
    }

    void setLevel(LogLevel level);
    void flush();

signals:
    void logMessage(LogLevel level, const QString& message);

private:
    std::shared_ptr<spdlog::logger> getLogger();
    std::shared_ptr<spdlog::logger> logger_;
    bool initialized_ = false;
};

} // namespace st
