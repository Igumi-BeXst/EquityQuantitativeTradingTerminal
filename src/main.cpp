#include <iostream>

#ifdef ST_BUILD_WITH_QT
#include <QApplication>
#include <QMessageBox>
#include <cstdio>
#include <exception>
#include <string>
#include "ui/main_window.h"
#include "ui/theme_manager.h"
#endif

int main(int argc, char* argv[]) {
#ifdef ST_BUILD_WITH_QT
    QApplication app(argc, argv);
    app.setApplicationName("StockTerminal");
    app.setApplicationVersion("0.1.0");

    // 顶层兜底：启动路径上的 std::filesystem / 配置解析异常若无人捕获，
    // 进程会 std::terminate -> abort，用户只看到一个 "Debug Error!" 对话框
    // （Release 连对话框都没有，表现为双击无反应）。这里转成可读信息。
    try {
        st::MainWindow window;
        window.setWindowTitle("StockTerminal - 量化交易工作站");
        window.resize(1400, 900);
        window.show();

        // 应用配置中的主题（MainWindow 构造时已加载配置）
        st::ThemeManager::applyCurrent();

        return app.exec();
    } catch (const std::exception& e) {
        const std::string msg = std::string("启动失败（未捕获异常）:\n") + e.what();
        std::fprintf(stderr, "%s\n", msg.c_str());
        try {
            QMessageBox::critical(nullptr, QStringLiteral("StockTerminal"),
                                  QString::fromStdString(msg));
        } catch (...) {
            // 弹窗本身失败也不能再抛
        }
        return 1;
    } catch (...) {
        const char* msg = "启动失败（未捕获未知异常）";
        std::fprintf(stderr, "%s\n", msg);
        try {
            QMessageBox::critical(nullptr, QStringLiteral("StockTerminal"),
                                  QString::fromUtf8(msg));
        } catch (...) {
        }
        return 1;
    }
#else
    std::cout << "StockTerminal v0.1.0" << std::endl;
    std::cout << "Qt support: disabled (BUILD_WITH_QT=OFF)" << std::endl;
    std::cout << "Build with Qt: cmake --preset with-qt" << std::endl;
    return 0;
#endif
}
