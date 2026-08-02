// QuotePoller 实时轮询验证工具 — 实测异步 QNAM → EventBus 发布链路
#include "data/tencent_provider.h"
#include "core/event_bus.h"
#include "core/log_manager.h"
#include "foundation/enums.h"
#include <QCoreApplication>
#include <QTimer>
#include <iostream>

using namespace st;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/quote_test.log");

    TencentProvider provider;
    provider.connect();

    int received = 0;
    EventBus::instance()->subscribeCallback(events::QuoteReceived,
        [&](const QVariantMap& data) {
            ++received;
            std::cout << "QuoteReceived: " << data["code"].toString().toStdString()
                      << " last=" << data["lastPrice"].toDouble()
                      << " change=" << data["change"].toDouble() << "%" << std::endl;
        });

    // 订阅 4 大指数
    for (const char* c : {"sh000001", "sz399001", "sz399006", "sh000688"}) {
        provider.subscribeQuote(StockCode(std::string_view(c)));
    }

    // 等 4 秒（启动立即刷一次 + 一个轮询周期）
    QTimer::singleShot(4000, &app, &QCoreApplication::quit);
    app.exec();

    std::cout << "\n=== 收到 " << received << " 条行情 ===" << std::endl;
    provider.unsubscribeQuote(StockCode(std::string_view("sh000001")));
    return received >= 4 ? 0 : 1;
}
