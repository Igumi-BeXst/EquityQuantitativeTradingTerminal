#include <gtest/gtest.h>
#include "engine/scheduler/screener_scope.h"

using namespace st;

TEST(ScopeResolverTest, ParseAllScope) {
    auto v = ScopeResolver::resolve(R"({"scope":"all"})", nullptr, {});
    EXPECT_TRUE(v.empty());   // provider=nullptr → 空
}

TEST(ScopeResolverTest, ParseSectorScope) {
    auto v = ScopeResolver::resolve(R"({"scope":"sector","sector":"BK0475"})", nullptr, {});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].code(), "BK0475");
}

TEST(ScopeResolverTest, ParseLastScopeFallsBackAll) {
    // provider=nullptr + lastConfig 空 → allAShares(nullptr)=空
    EXPECT_TRUE(ScopeResolver::resolve(R"({"scope":"last"})", nullptr, {}).empty());
}

TEST(ScopeResolverTest, LastScopeUsesConfig) {
    std::vector<StockCode> last = { StockCode("SH600519") };
    auto v = ScopeResolver::resolve(R"({"scope":"last"})", nullptr, last);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].code(), "600519");
}

TEST(ScopeResolverTest, MalformedFallsBackEmpty) {
    EXPECT_TRUE(ScopeResolver::resolve("not json{", nullptr, {}).empty());
}
