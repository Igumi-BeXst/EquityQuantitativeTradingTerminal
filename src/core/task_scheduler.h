#pragma once
#include "foundation/types.h"
#include <functional>
namespace st {
class TaskScheduler { public: TaskScheduler();
    void scheduleAt(std::function<void()> task, DateTime time);
    void scheduleRecurring(std::function<void()> task, int intervalSeconds); };
}
