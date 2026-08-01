#include <gtest/gtest.h>
#include "core/thread_pool.h"
#include <atomic>
#include <chrono>

using namespace st;

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
