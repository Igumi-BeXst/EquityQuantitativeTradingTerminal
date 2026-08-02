#include <gtest/gtest.h>
#include "core/log_manager.h"
#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QElapsedTimer>

using namespace st;

namespace {

/// 等待排队到主线程的 logMessage 信号被投递
bool waitForSignal(QString& out, int timeoutMs = 1000) {
    QElapsedTimer t;
    t.start();
    while (out.isEmpty() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return !out.isEmpty();
}

}  // namespace

TEST(LogManagerTest, LogEmitsLogMessageSignal) {
    auto* lm = LogManager::instance();
    QObject ctx;
    QString received;
    QObject::connect(lm, &LogManager::logMessage, &ctx,
                     [&](LogLevel, const QString& msg) { received = msg; });

    lm->log(LogLevel::Info, "hello {}", 42);

    EXPECT_TRUE(waitForSignal(received));
    EXPECT_EQ(received, QStringLiteral("hello 42"));
}

TEST(LogManagerTest, WarnLevelReachesSignal) {
    auto* lm = LogManager::instance();
    QObject ctx;
    QString received;
    QObject::connect(lm, &LogManager::logMessage, &ctx,
                     [&](LogLevel level, const QString& msg) {
                         if (level == LogLevel::Warn) received = msg;
                     });

    lm->log(LogLevel::Warn, "warning: {}%", 3.5);

    EXPECT_TRUE(waitForSignal(received));
    EXPECT_EQ(received, QStringLiteral("warning: 3.5%"));
}

TEST(LogManagerTest, EmitSafeWithoutApp) {
    // 无 QCoreApplication 场景（本测试存在 app，无法直接测；此处验证 emitLogMessage 逻辑的同步路径
    // 通过 LogManager 实例本身不崩溃即可）
    auto* lm = LogManager::instance();
    EXPECT_NE(lm, nullptr);
    lm->log(LogLevel::Debug, "no-crash path {}", 1);
}
