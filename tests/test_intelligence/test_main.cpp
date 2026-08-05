#include <gtest/gtest.h>
#include <QCoreApplication>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Intelligence 层依赖 Qt 组件（日志等），需要 QCoreApplication
    QCoreApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
