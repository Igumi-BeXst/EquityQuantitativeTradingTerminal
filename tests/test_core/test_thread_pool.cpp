#include <gtest/gtest.h>
#include "core/thread_pool.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace st;

TEST(ThreadPoolTest, ConcurrentInitializationReturnsSinglePools) {
    constexpr int kCallers = 32;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<QThreadPool*> workers(kCallers);
    std::vector<QThreadPool*> io(kCallers);
    std::vector<std::thread> callers;
    callers.reserve(kCallers);

    for (int i = 0; i < kCallers; ++i) {
        callers.emplace_back([&, i] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            workers[i] = ThreadPool::workerPool();
            io[i] = ThreadPool::ioPool();
        });
    }
    while (ready.load(std::memory_order_acquire) != kCallers) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& caller : callers) caller.join();

    for (int i = 1; i < kCallers; ++i) {
        EXPECT_EQ(workers[i], workers[0]);
        EXPECT_EQ(io[i], io[0]);
    }
}

TEST(ThreadPoolTest, SubmitWorkerTask) {
    std::atomic<bool> executed{false};
    ThreadPool::submitWorker([&executed]() {
        executed.store(true);
    });
    // Wait for task to complete
    for (int i = 0; i < 50 && !executed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, SubmitIOTask) {
    std::atomic<bool> executed{false};
    ThreadPool::submitIO([&executed]() {
        executed.store(true);
    });
    for (int i = 0; i < 50 && !executed.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, MultipleTasks) {
    std::atomic<int> counter{0};
    const int kTasks = 5;
    for (int i = 0; i < kTasks; ++i) {
        ThreadPool::submitWorker([&counter]() {
            counter.fetch_add(1);
        });
    }
    for (int i = 0; i < 100 && counter.load() < kTasks; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(counter.load(), kTasks);
}
