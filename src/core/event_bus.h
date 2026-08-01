#pragma once

#include <QObject>
#include <QVariantMap>
#include <QString>
#include <QHash>
#include <QVector>
#include <mutex>
#include <functional>

namespace st {

using EventCallback = std::function<void(const QVariantMap&)>;

/// Cross-module event bus. Thread-safe publish/subscribe.
class EventBus : public QObject {
    Q_OBJECT

public:
    static EventBus* instance();

    explicit EventBus(QObject* parent = nullptr);
    ~EventBus() override;

    /// Publish event (thread-safe)
    void publish(const QString& event, const QVariantMap& data = {});

    /// Qt signal-slot subscription (receiver must live on main thread)
    void subscribe(const QString& event, QObject* receiver, const char* slot);

    /// Lambda subscription (thread-safe, callback runs on publishing thread)
    int subscribeCallback(const QString& event, EventCallback callback);

    /// Remove Qt subscription
    void unsubscribe(const QString& event, QObject* receiver);

    /// Remove lambda subscription
    void unsubscribeCallback(int id);

signals:
    void eventFired(const QString& event, const QVariantMap& data);

private:
    struct CallbackEntry {
        int id;
        EventCallback callback;
    };
    QHash<QString, QVector<CallbackEntry>> callbacks_;
    std::mutex mutex_;
    int nextCallbackId_ = 1;
};

// Predefined events
namespace events {
    inline const QString QuoteReceived    = QStringLiteral("QuoteReceived");
    inline const QString BarCompleted     = QStringLiteral("BarCompleted");
    inline const QString TickReceived     = QStringLiteral("TickReceived");
    inline const QString StrategySignal   = QStringLiteral("StrategySignal");
    inline const QString StrategyStarted  = QStringLiteral("StrategyStarted");
    inline const QString StrategyStopped  = QStringLiteral("StrategyStopped");
    inline const QString BacktestProgress = QStringLiteral("BacktestProgress");
    inline const QString BacktestCompleted= QStringLiteral("BacktestCompleted");
    inline const QString OrderSubmitted   = QStringLiteral("OrderSubmitted");
    inline const QString OrderFilled      = QStringLiteral("OrderFilled");
    inline const QString TradeExecuted    = QStringLiteral("TradeExecuted");
    inline const QString DataSyncCompleted= QStringLiteral("DataSyncCompleted");
    inline const QString ConfigChanged    = QStringLiteral("ConfigChanged");
    inline const QString SystemError      = QStringLiteral("SystemError");
    inline const QString PriceAlert       = QStringLiteral("PriceAlert");
    inline const QString SignalAlert      = QStringLiteral("SignalAlert");
    inline const QString SystemAlert      = QStringLiteral("SystemAlert");
}

} // namespace st
