#include "core/notification_service.h"
#include "core/event_bus.h"
#include "core/log_manager.h"
#include <chrono>

namespace st {

NotificationService* NotificationService::instance() {
    static NotificationService svc;
    return &svc;
}

NotificationService::NotificationService(QObject* parent) : QObject(parent) {}

void NotificationService::notify(NotificationLevel level, const std::string& title, const std::string& msg) {
    Notification n;
    n.level = level;
    n.title = title;
    n.message = msg;
    n.timestamp = std::chrono::system_clock::now();

    // Keep history
    history_.push_back(n);
    if (history_.size() > kMaxHistory) {
        history_.erase(history_.begin());
    }

    // Log it
    auto* log = LogManager::instance();
    if (log) {
        LogLevel logLevel = LogLevel::Info;
        switch (level) {
            case NotificationLevel::Info:     logLevel = LogLevel::Info; break;
            case NotificationLevel::Warning:  logLevel = LogLevel::Warn; break;
            case NotificationLevel::Alert:    logLevel = LogLevel::Warn; break;
            case NotificationLevel::Critical: logLevel = LogLevel::Error; break;
        }
        log->log(logLevel, "[{}] {}", title, msg);
    }

    // Emit
    emit notificationPosted(n);

    // Publish to EventBus for other subscribers
    QVariantMap data;
    data["level"] = static_cast<int>(level);
    data["title"] = QString::fromStdString(title);
    data["message"] = QString::fromStdString(msg);
    EventBus::instance()->publish(events::SystemAlert, data);
}

void NotificationService::info(const std::string& title, const std::string& msg) {
    notify(NotificationLevel::Info, title, msg);
}

void NotificationService::warning(const std::string& title, const std::string& msg) {
    notify(NotificationLevel::Warning, title, msg);
}

void NotificationService::alert(const std::string& title, const std::string& msg) {
    notify(NotificationLevel::Alert, title, msg);
}

void NotificationService::critical(const std::string& title, const std::string& msg) {
    notify(NotificationLevel::Critical, title, msg);
}

} // namespace st
