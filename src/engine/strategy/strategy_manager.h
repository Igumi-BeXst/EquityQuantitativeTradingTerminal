#pragma once
#include "engine/strategy/istrategy.h"
#include <memory>
namespace st { class StrategyManager { public: void registerStrategy(std::unique_ptr<IStrategy>); }; }
