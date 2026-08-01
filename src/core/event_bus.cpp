#include "core/event_bus.h"
#include <QMetaObject>
#include <QCoreApplication>
#include <QThread>

namespace st {

EventBus* EventBus::instance() {
    static EventBus bus;
    return &bus;
}

EventBus::EventBus(QObject* parent) : QObject(parent) {}

EventBus::~EventBus() = default;

void EventBus::publish(const QString& event, const QVariantMap& data) {
    // Emit to Qt subscribers via signal (must be in main thread)
    // Defer the emit to avoid direct cross-thread issues
    if (QCoreApplication::instance() &&
        QThread::currentThread() != QCoreApplication::instance()->thread()) {
        QMetaObject::invokeMethod(this, [this, event, data]() {
            emit eventFired(event, data);
        }, Qt::QueuedConnection);
    } else {
        emit eventFired(event, data);
    }

    // Call lambda subscribers (thread-safe)
    QVector<CallbackEntry> entries;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = callbacks_.find(event);
        if (it != callbacks_.end()) {
            entries = it.value();
        }
    }
    for (const auto& entry : entries) {
        if (entry.callback) {
            entry.callback(data);
        }
    }
}

void EventBus::subscribe(const QString& event, QObject* receiver, const char* slot) {
    if (!receiver) return;
    // Use a private signal connection pattern
    connect(this, SIGNAL(eventFired(QString,QVariantMap)),
            receiver, slot,
            Qt::AutoConnection);
    // Filter by event name: the receiver's slot should check event name
    // Note: Qt doesn't support "event name filtering" natively.
    // Receivers receive all events and should filter by event string.
    // For convenience, we also expose subscribeCallback for lambda filtering.
    (void)event; // "event" parameter available for future filtering proxy
}

int EventBus::subscribeCallback(const QString& event, EventCallback callback) {
    if (!callback) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    int id = nextCallbackId_++;
    callbacks_[event].append({id, std::move(callback)});
    return id;
}

void EventBus::unsubscribe(const QString&, QObject* receiver) {
    if (!receiver) return;
    disconnect(receiver);
}

void EventBus::unsubscribeCallback(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
        auto& entries = it.value();
        entries.erase(
            std::remove_if(entries.begin(), entries.end(),
                           [id](const CallbackEntry& e) { return e.id == id; }),
            entries.end());
    }
}

} // namespace st
