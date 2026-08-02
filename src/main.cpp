#include <iostream>

#ifdef ST_BUILD_WITH_QT
#include <QApplication>
#include "ui/main_window.h"
#include "ui/theme_manager.h"
#endif

int main(int argc, char* argv[]) {
#ifdef ST_BUILD_WITH_QT
    QApplication app(argc, argv);
    app.setApplicationName("StockTerminal");
    app.setApplicationVersion("0.1.0");

    st::MainWindow window;
    window.setWindowTitle("StockTerminal - 量化交易工作站");
    window.resize(1400, 900);
    window.show();

    // 应用配置中的主题（MainWindow 构造时已加载配置）
    st::ThemeManager::applyCurrent();

    return app.exec();
#else
    std::cout << "StockTerminal v0.1.0" << std::endl;
    std::cout << "Qt support: disabled (BUILD_WITH_QT=OFF)" << std::endl;
    std::cout << "Build with Qt: cmake --preset with-qt" << std::endl;
    return 0;
#endif
}
