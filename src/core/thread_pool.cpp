#include "core/thread_pool.h"
#include <QCoreApplication>

namespace st {

QThreadPool* ThreadPool::workerPool() {
    static const auto instance = [] {
        auto pool = std::make_unique<QThreadPool>();
        int idealThreads = QThread::idealThreadCount();
        pool->setMaxThreadCount(std::max(idealThreads, 2));
        return pool;
    }();
    return instance.get();
}

QThreadPool* ThreadPool::ioPool() {
    static const auto instance = [] {
        auto pool = std::make_unique<QThreadPool>();
        // 6 线程：市场面板全 A 股批量报价（~87 次串行 TDX 往返，实测 ~68s）会长时间占
        // 用一个线程，若仅 2 线程，期间选股/加载 K 线的 getBars 会被长时间排队。
        // TDX 连接为每命令加锁（executeCommand 持有 mutex 仅单命令），增加线程后
        // 交互请求可在批量报价的 chunk 间隙插入，大幅提升响应。
        pool->setMaxThreadCount(6);
        return pool;
    }();
    return instance.get();
}

void ThreadPool::submitWorker(std::function<void()> task) {
    if (!task) return;
    workerPool()->start(new FunctionRunnable(std::move(task)));
}

void ThreadPool::submitIO(std::function<void()> task) {
    if (!task) return;
    ioPool()->start(new FunctionRunnable(std::move(task)));
}

ThreadPool::ThreadPool(QObject* parent) : QObject(parent) {}

} // namespace st
