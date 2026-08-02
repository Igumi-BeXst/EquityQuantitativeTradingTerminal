#include <gtest/gtest.h>
#include "core/app_paths.h"
#include <filesystem>

using namespace st;

TEST(AppPathsTest, AppRootIsNonEmpty) {
    EXPECT_FALSE(AppPaths::appRoot().empty());
}

TEST(AppPathsTest, DirectoriesAreDistinct) {
    EXPECT_NE(AppPaths::configDir(), AppPaths::dataDir());
    EXPECT_NE(AppPaths::dataDir(), AppPaths::logDir());
    EXPECT_NE(AppPaths::secretFilePath(), AppPaths::configDir());
}

TEST(AppPathsTest, SecretPathHasFilename) {
    auto path = AppPaths::secretFilePath();
    EXPECT_FALSE(path.empty());
    std::filesystem::path p(path);
    EXPECT_EQ(p.filename().string(), "secrets.json");
}

TEST(AppPathsTest, EnsureDirectoriesCreatesAll) {
    EXPECT_TRUE(AppPaths::ensureDirectories());
    EXPECT_TRUE(std::filesystem::exists(AppPaths::configDir()));
    EXPECT_TRUE(std::filesystem::exists(AppPaths::dataDir()));
    EXPECT_TRUE(std::filesystem::exists(AppPaths::logDir()));
}
