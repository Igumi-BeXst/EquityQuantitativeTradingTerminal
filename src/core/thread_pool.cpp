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
        s_ioPool->setMaxThreadCount(2); // IO limited to 2 threads
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
