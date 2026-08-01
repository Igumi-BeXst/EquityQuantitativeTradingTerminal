#include <gtest/gtest.h>
#include "core/config_manager.h"

using namespace st;

TEST(ConfigManagerTest, GetWithDefault) {
    auto* cfg = ConfigManager::instance();
    double val = cfg->get("backtest.initialCapital", 100000.0);
    EXPECT_DOUBLE_EQ(val, 100000.0);
}

TEST(ConfigManagerTest, SetAndGet) {
    auto* cfg = ConfigManager::instance();
    cfg->set("test.value", 42);
    int val = cfg->get("test.value", 0);
    EXPECT_EQ(val, 42);
}

TEST(ConfigManagerTest, NestedPath) {
    auto* cfg = ConfigManager::instance();
    cfg->set("a.b.c", std::string("hello"));
    auto val = cfg->get("a.b.c", std::string(""));
    EXPECT_EQ(val, "hello");
}

TEST(ConfigManagerTest, BoolValue) {
    auto* cfg = ConfigManager::instance();
    cfg->set("flags.enabled", true);
    EXPECT_TRUE(cfg->get("flags.enabled", false));
}
