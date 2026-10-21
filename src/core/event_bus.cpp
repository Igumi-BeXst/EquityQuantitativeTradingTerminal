#include "core/event_bus.h"
#include <QCoreApplication>
#include <QMetaObject>
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
    if (!receiver || !slot) return;

    QByteArray signature(slot);
    if (!signature.isEmpty() && (signature.front() == '1' || signature.front() == '2')) {
        signature.remove(0, 1);  // SLOT()/SIGNAL() prefix
    }
    signature = QMetaObject::normalizedSignature(signature.constData());
    const qsizetype argsStart = signature.indexOf('(');
    if (argsStart <= 0) return;
    const QByteArray methodName = signature.left(argsStart);
    const QPointer<QObject> guardedReceiver(receiver);
    const auto connection = QObject::connect(
        this, &EventBus::eventFired, receiver,
        [event, guardedReceiver, methodName](const QString& publishedEvent,
                                             const QVariantMap& data) {
            if (publishedEvent != event || guardedReceiver.isNull()) return;
            QMetaObject::invokeMethod(guardedReceiver.data(), methodName.constData(),
                                      Qt::AutoConnection,
                                      Q_ARG(QString, publishedEvent),
                                      Q_ARG(QVariantMap, data));
        });

    std::lock_guard<std::mutex> lock(qtMutex_);
    qtSubscriptions_.append({event, guardedReceiver, connection});
}

int EventBus::subscribeCallback(const QString& event, EventCallback callback) {
    if (!callback) return 0;
    std::lock_guard<std::mutex> lock(mutex_);
    int id = nextCallbackId_++;
    callbacks_[event].append({id, std::move(callback)});
    return id;
}

void EventBus::unsubscribe(const QString& event, QObject* receiver) {
    if (!receiver) return;
    std::lock_guard<std::mutex> lock(qtMutex_);
    for (auto it = qtSubscriptions_.begin(); it != qtSubscriptions_.end();) {
        if (it->event == event && it->receiver.data() == receiver) {
            QObject::disconnect(it->connection);
            it = qtSubscriptions_.erase(it);
        } else if (it->receiver.isNull()) {
            it = qtSubscriptions_.erase(it);
        } else {
            ++it;
        }
    }
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
