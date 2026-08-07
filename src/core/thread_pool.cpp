#include "core/thread_pool.h"
#include <QCoreApplication>

namespace st {

static QThreadPool* s_workerPool = nullptr;
static QThreadPool* s_ioPool = nullptr;

QThreadPool* ThreadPool::workerPool() {
    if (!s_workerPool) {
        s_workerPool = new QThreadPool();
        int idealThreads = QThread::idealThreadCount();
        s_workerPool->setMaxThreadCount(std::max(idealThreads, 2));
    }
    return s_workerPool;
}

QThreadPool* ThreadPool::ioPool() {
    if (!s_ioPool) {
        s_ioPool = new QThreadPool();
        // 6 线程：市场面板全 A 股批量报价（~87 次串行 TDX 往返，实测 ~68s）会长时间占
        // 用一个线程，若仅 2 线程，期间选股/加载 K 线的 getBars 会被长时间排队。
        // TDX 连接为每命令加锁（executeCommand 持有 mutex 仅单命令），增加线程后
        // 交互请求可在批量报价的 chunk 间隙插入，大幅提升响应。
        s_ioPool->setMaxThreadCount(6);
    }
    return s_ioPool;
}

void ThreadPool::submitWorker(std::function<void()> task) {
    workerPool()->start(new FunctionRunnable(std::move(task)));
}

void ThreadPool::submitIO(std::function<void()> task) {
    ioPool()->start(new FunctionRunnable(std::move(task)));
}

ThreadPool::ThreadPool(QObject* parent) : QObject(parent) {}

} // namespace st
