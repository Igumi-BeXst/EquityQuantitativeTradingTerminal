#include <gtest/gtest.h>
#include "core/credential_store.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace st;

namespace {
std::string makeTempSecretPath() {
    static int count = 0;
    return "test_secrets_" + std::to_string(count++) + ".json";
}
}

TEST(CredentialStoreTest, InitCreatesFile) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    EXPECT_TRUE(store.init(path));
    EXPECT_TRUE(store.isInitialized());
    EXPECT_TRUE(std::filesystem::exists(path));
    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, SetAndGetSecret) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    ASSERT_TRUE(store.init(path));

    store.setSecret("tushare_token", "abc123secret");
    auto value = store.getSecret("tushare_token");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(*value, "abc123secret");

    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, GetMissingKeyReturnsNullopt) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    ASSERT_TRUE(store.init(path));

    auto value = store.getSecret("nonexistent");
    EXPECT_FALSE(value.has_value());

    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, HasSecret) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    ASSERT_TRUE(store.init(path));

    store.setSecret("ai_key", "sk-xyz");
    EXPECT_TRUE(store.hasSecret("ai_key"));
    EXPECT_FALSE(store.hasSecret("other"));

    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, RemoveSecret) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    ASSERT_TRUE(store.init(path));

    store.setSecret("key1", "value1");
    EXPECT_TRUE(store.removeSecret("key1"));
    EXPECT_FALSE(store.removeSecret("key1")); // 已删除
    EXPECT_FALSE(store.hasSecret("key1"));

    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, PersistsAcrossReopen) {
    auto path = makeTempSecretPath();
    {
        CredentialStore store;
        ASSERT_TRUE(store.init(path));
        store.setSecret("broker_password", "hunter2");
    }
    {
        CredentialStore store;
        ASSERT_TRUE(store.init(path));
        auto value = store.getSecret("broker_password");
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(*value, "hunter2");
    }
    std::filesystem::remove(path);
}

TEST(CredentialStoreTest, FileDoesNotContainPlaintext) {
    auto path = makeTempSecretPath();
    CredentialStore store;
    ASSERT_TRUE(store.init(path));

    const std::string secret = "top_secret_token_987654";
    store.setSecret("token", secret);

    // 读取文件内容，明文不应直接出现在文件中
    {
        std::ifstream file(path);
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find(secret), std::string::npos)
            << "明文凭证不应出现在存储文件中!";
    } // 关闭文件流，确保可删除

    std::filesystem::remove(path);
}
