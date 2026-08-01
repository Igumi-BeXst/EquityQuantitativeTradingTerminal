#pragma once

#include <QObject>
#include <QThread>
#include <QRunnable>
#include <QThreadPool>
#include <functional>
#include <memory>

namespace st {

/// Wraps QThreadPool for domain-friendly API.
/// Worker pool (CPU) and IO pool (network/disk) share the same Qt thread pool
/// but with different priority and concurrency settings.
class ThreadPool : public QObject {
    Q_OBJECT

public:
    /// Returns the global CPU-bound worker pool
    static QThreadPool* workerPool();

    /// Returns the IO-bound pool (limited concurrency)
    static QThreadPool* ioPool();

    /// Submit a task to the worker pool
    static void submitWorker(std::function<void()> task);

    /// Submit a task to the IO pool
    static void submitIO(std::function<void()> task);

    explicit ThreadPool(QObject* parent = nullptr);
};

/// Runnable wrapper for std::function
class FunctionRunnable : public QRunnable {
public:
    explicit FunctionRunnable(std::function<void()> fn) : fn_(std::move(fn)) {
        setAutoDelete(true);
    }
    void run() override {
        if (fn_) fn_();
    }
private:
    std::function<void()> fn_;
};

} // namespace st
