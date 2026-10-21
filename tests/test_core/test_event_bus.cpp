#include <gtest/gtest.h>
#include "core/event_bus.h"

#include <QCoreApplication>
#include <QObject>

using namespace st;

namespace {

class EventReceiver : public QObject {
    Q_OBJECT
public:
    QStringList received;

public slots:
    void receive(const QString& event, const QVariantMap&) {
        received.push_back(event);
    }
};

} // namespace

TEST(EventBusTest, QtSubscriptionsFilterAndUnsubscribeByEvent) {
    EventBus bus;
    EventReceiver receiver;
    const char* slot = SLOT(receive(QString,QVariantMap));

    bus.subscribe(events::QuoteReceived, &receiver, slot);
    bus.subscribe(events::SystemError, &receiver, slot);

    bus.publish(events::QuoteReceived);
    ASSERT_EQ(receiver.received.size(), 1);
    EXPECT_EQ(receiver.received[0], events::QuoteReceived);

    bus.unsubscribe(events::QuoteReceived, &receiver);
    bus.publish(events::QuoteReceived);
    bus.publish(events::SystemError);
    ASSERT_EQ(receiver.received.size(), 2);
    EXPECT_EQ(receiver.received[1], events::SystemError);
}

#include "test_event_bus.moc"
