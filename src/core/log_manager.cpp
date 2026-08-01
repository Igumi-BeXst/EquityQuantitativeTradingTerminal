#include "core/log_manager.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <filesystem>
#include <QDebug>

namespace st {

LogManager* LogManager::instance() {
    static LogManager mgr;
    return &mgr;
}

LogManager::LogManager(QObject* parent) : QObject(parent) {}

LogManager::~LogManager() {
    if (logger_) logger_->flush();
}

bool LogManager::init(const std::string& logFilePath) {
    if (initialized_) return true;

    try {
        // Create log directory if needed
        std::filesystem::path path(logFilePath);
        auto parentPath = path.parent_path();
        if (!parentPath.empty() && !std::filesystem::exists(parentPath)) {
            std::filesystem::create_directories(parentPath);
        }

        // Console sink (colored)
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

        // File sink
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        std::vector<spdlog::sink_ptr> sinks = {consoleSink, fileSink};
        logger_ = std::make_shared<spdlog::logger>("StockTerminal", sinks.begin(), sinks.end());
        logger_->set_level(spdlog::level::trace);
        spdlog::set_default_logger(logger_);

        initialized_ = true;
        logger_->info("LogManager initialized, writing to: {}", logFilePath);
    } catch (const spdlog::spdlog_ex& ex) {
        qWarning() << "LogManager init failed:" << ex.what();
        return false;
    }
    return true;
}

void LogManager::setLevel(LogLevel level) {
    auto l = getLogger();
    switch (level) {
        case LogLevel::Trace:    l->set_level(spdlog::level::trace);    break;
        case LogLevel::Debug:    l->set_level(spdlog::level::debug);    break;
        case LogLevel::Info:     l->set_level(spdlog::level::info);     break;
        case LogLevel::Warn:     l->set_level(spdlog::level::warn);     break;
        case LogLevel::Error:    l->set_level(spdlog::level::err);      break;
        case LogLevel::Critical: l->set_level(spdlog::level::critical); break;
    }
}

void LogManager::flush() {
    if (logger_) logger_->flush();
}

std::shared_ptr<spdlog::logger> LogManager::getLogger() {
    if (!initialized_) {
        init(); // Auto-init with default path
    }
    return logger_ ? logger_ : spdlog::default_logger();
}

} // namespace st
