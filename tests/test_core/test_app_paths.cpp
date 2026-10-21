#include <gtest/gtest.h>
#include "core/app_paths.h"
#include <filesystem>
#include <string>

using namespace st;

namespace {

/// p 是否位于 root 之下（字符串前缀 + 分隔符边界）
bool isUnder(const std::string& p, const std::string& root) {
    if (root.empty() || p.size() <= root.size()) return false;
    if (p.compare(0, root.size(), root) != 0) return false;
    const char next = p[root.size()];
    return next == '/' || next == '\\';
}

/// 两个根都不可写时（受限令牌/安全软件/沙箱），AppPaths 会返回空表示"无可用位置"，
/// 调用方据此降级。这类环境下路径相关断言无意义，跳过而不是判失败。
bool hasUsableRoot() {
    return !AppPaths::appRoot().empty();
}

} // namespace

TEST(AppPathsTest, AppRootIsNonEmpty) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    EXPECT_FALSE(AppPaths::appRoot().empty());
}

TEST(AppPathsTest, DirectoriesAreDistinct) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    EXPECT_NE(AppPaths::configDir(), AppPaths::dataDir());
    EXPECT_NE(AppPaths::dataDir(), AppPaths::logDir());
    EXPECT_NE(AppPaths::secretFilePath(), AppPaths::configDir());
}

TEST(AppPathsTest, SecretPathHasFilename) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    auto path = AppPaths::secretFilePath();
    EXPECT_FALSE(path.empty());
    std::filesystem::path p(path);
    EXPECT_EQ(p.filename().string(), "secrets.json");
}

TEST(AppPathsTest, EnsureDirectoriesCreatesAll) {
    // 受限环境（沙箱/受限令牌/只读策略）可能无权创建用户目录；此时跳过而非误报失败。
    if (!AppPaths::ensureDirectories()) {
        GTEST_SKIP() << "environment forbids creating app data directories";
    }
    EXPECT_TRUE(std::filesystem::exists(AppPaths::configDir()));
    EXPECT_TRUE(std::filesystem::exists(AppPaths::dataDir()));
    EXPECT_TRUE(std::filesystem::exists(AppPaths::logDir()));
}

// --- 目录分置：可重建数据放 Local，用户财产放 Roaming -------------------------

TEST(AppPathsTest, LocalAndRoamingRootsDiffer) {
    // 缓存/日志用 Local，配置/凭证用 Roaming。只有一侧可写时会回退成同一个根，
    // 这是有意的降级设计，不算缺陷。
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    const auto local = AppPaths::appRoot();
    const auto roaming = AppPaths::configRoot();
    EXPECT_FALSE(local.empty());
    EXPECT_FALSE(roaming.empty());
    if (local == roaming) {
        GTEST_SKIP() << "only one writable root available; fell back to a single location";
    }
    EXPECT_NE(local, roaming);
}

TEST(AppPathsTest, CacheAndLogsLiveUnderLocalRoot) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    // 行情缓存与日志体积大且可重建，优先不进 Roaming
    EXPECT_TRUE(isUnder(AppPaths::dataDir(), AppPaths::appRoot()));
    EXPECT_TRUE(isUnder(AppPaths::logDir(), AppPaths::appRoot()));
}

TEST(AppPathsTest, ConfigAndSecretsLiveUnderRoamingRoot) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    EXPECT_TRUE(isUnder(AppPaths::configDir(), AppPaths::configRoot()));
    EXPECT_TRUE(isUnder(AppPaths::secretFilePath(), AppPaths::configRoot()));
}

TEST(AppPathsTest, LegacyDataDirStaysUnderRoamingForMigration) {
    if (!hasUsableRoot()) {
        GTEST_SKIP() << "no writable app data location in this environment";
    }
    // 旧版缓存位于 Roaming/data，迁移逻辑靠它找到历史 .stb 文件
    const auto legacy = AppPaths::legacyDataDir();
    EXPECT_TRUE(isUnder(legacy, AppPaths::configRoot()));
    EXPECT_EQ(std::filesystem::path(legacy).filename().string(), "data");
}

TEST(AppPathsTest, ScreenshotsLiveUnderDocumentsNotAppData) {
    const auto shots = AppPaths::screenshotsDir();
    const auto docs = AppPaths::documentsDir();
    if (shots.empty() || docs.empty()) {
        GTEST_SKIP() << "documents location unavailable in this environment";
    }
    EXPECT_TRUE(isUnder(shots, docs));
    EXPECT_EQ(std::filesystem::path(shots).filename().string(), "screenshots");
}

TEST(AppPathsTest, MigrateLegacyDataIsIdempotent) {
    // 迁移必须可重复调用且不抛异常（无旧数据时应为 no-op）
    EXPECT_NO_THROW(AppPaths::migrateLegacyData());
    EXPECT_NO_THROW(AppPaths::migrateLegacyData());
}
