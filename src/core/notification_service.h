#pragma once

#include "foundation/enums.h"
#include "foundation/types.h"
#include <QObject>
#include <QString>
#include <string>
#include <vector>
#include <functional>

namespace st {

struct Notification {
    NotificationLevel level = NotificationLevel::Info;
    std::string title;
    std::string message;
    DateTime timestamp;
};

/// Unified notification channel: price alerts, strategy signals, system errors.
/// Subscribers (UI panels, log, etc.) listen via EventBus.
class NotificationService : public QObject {
    Q_OBJECT

public:
    static NotificationService* instance();

    explicit NotificationService(QObject* parent = nullptr);

    /// Post a notification
    void notify(NotificationLevel level, const std::string& title, const std::string& msg);

    /// Convenience helpers
    void info(const std::string& title, const std::string& msg);
    void warning(const std::string& title, const std::string& msg);
    void alert(const std::string& title, const std::string& msg);
    void critical(const std::string& title, const std::string& msg);

    /// Get recent notifications
    const std::vector<Notification>& history() const { return history_; }
    void clearHistory() { history_.clear(); }

signals:
    void notificationPosted(const Notification& notification);

private:
    std::vector<Notification> history_;
    static constexpr size_t kMaxHistory = 500;
};

} // namespace st
