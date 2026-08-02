#include <gtest/gtest.h>
#include <QCoreApplication>

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    // Qt SQL 驱动插件加载需要 QCoreApplication 初始化
    QCoreApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
