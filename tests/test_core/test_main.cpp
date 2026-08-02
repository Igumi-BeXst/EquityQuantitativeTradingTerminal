#include <gtest/gtest.h>
#include <QCoreApplication>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Qt 组件（QByteArray 等）需要 QCoreApplication 初始化
    QCoreApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
