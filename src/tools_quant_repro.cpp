// 量化工作台打开/关闭耗时复现工具（调试用）
#include "ui/panels/quant_window.h"
#include "data/provider_factory.h"
#include "core/thread_pool.h"
#include <QApplication>
#include <QDateTime>
#include <QTimer>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace st;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    auto provider = makeDataProvider();
    provider->connect();
    QuantWindow w(provider.get(), nullptr);
    w.show();

    QTimer::singleShot(2000, &app, [&] {
        // 模拟市场面板批量报价在飞（3s 长任务占共享 IO 池）
        // 无注入任务
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        w.close();
        const qint64 dt = QDateTime::currentMSecsSinceEpoch() - t0;
        std::printf("close 耗时 %lld ms（在飞 3s 长任务下）\n", static_cast<long long>(dt));
        app.quit();
    });
    const int rc = app.exec();
    // 模拟真机 ~MainWindow：进程退出前排空线程池，避免在途任务访问已释放的 provider
    ThreadPool::ioPool()->waitForDone();
    ThreadPool::workerPool()->waitForDone();
    std::printf("退出 rc=%d\n", rc);
    return rc;
}
