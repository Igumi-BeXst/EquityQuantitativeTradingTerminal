# AI 缁煎悎淇″彿闈㈡澘 Implementation Plan锛圓I 閲忓寲宸ヤ綔娴?路 绗?1 杞級

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 鍗曞彧鑲＄エ涓婅瀺鍚?K绾垮舰鎬?+ 鑸嗘儏鎯呯华 + 鎶€鏈寚鏍囷紙RSI/MACD/鍔ㄩ噺锛夆啋 缁煎悎淇″彿璇勭骇锛堝己鐑堜拱鍏?涔板叆/瑙傛湜/鍗栧嚭/寮虹儓鍗栧嚭锛? 缃俊搴?+ 鍒嗛」鏄庣粏 + 鍘嗗彶淇″彿璁板綍銆備富绐楀彛鍙充晶 Dock锛岀粦瀹氫腑澶浘琛?`currentCodeChanged` 鑷姩璺熼殢銆?
**Architecture:** 寮曟搸灞傜函鍑芥暟 `composeSignal`锛堝彲鍗曟祴锛? 涓荤獥鍙ｅ彸渚?AI 淇″彿 Dock 闈㈡澘锛堝畨鍏ㄥ紓姝ワ細IO 鎷夋棩K/鏂伴椈 鈫?Worker 绠楁寚鏍?褰㈡€?鈫?QPointer 瀹堝崼鍥炰富绾跨▼锛夈€?
**Tech Stack:** C++17, Qt 6.11 (Widgets), 鏃犳柊渚濊禆銆?
璁捐鏂囨。锛歔2026-08-13-ai-quant-workflow-design.md](../specs/2026-08-13-ai-quant-workflow-design.md)

## Global Constraints

- **鍒嗗眰淇锛堢浉瀵硅璁℃枃妗ｏ級**锛氳璁℃枃妗ｅ啓 `engine/analyzer/composite_signal`锛屼絾 `composeSignal` 鐨勫叆鍙傚惈 `pattern::PatternSignal` / `sentiment::SentimentScore`锛坕ntelligence 灞傜被鍨嬶級锛岃€?st_engine **涓嶉摼鎺?* st_intelligence锛坄st_intelligence` PUBLIC 閾炬帴 `st_engine`锛夆€斺€旀斁 engine 浼氬舰鎴愬惊鐜緷璧栵紝杩濆弽銆屼弗鏍艰嚜涓婅€屼笅渚濊禆銆嶃€?*鏀逛负 `intelligence/signal/composite_signal.{h,cpp}`**锛坕ntelligence 鍙嚜鐢变緷璧?pattern/sentiment/foundation锛夛紱鍗曟祴鏀?`tests/test_intelligence/test_composite_signal.cpp`銆?- 缂栬瘧闆惰鍛婏細`cmake --build --preset with-qt` 蹇呴』闆堕敊璇浂璀﹀憡锛圢inja 鑷姩 re-configure锛屾柊澧炴枃浠舵棤闇€鎵嬪姩 cmake锛?- 鍥炲綊锛歝test 鐜版湁 **407 tests** 鍏ㄧ豢锛涙柊澧?12 渚?鈫?棰勮 **419**
- 蹇€?TDD 鍛戒护锛歚ctest --preset default -R CompositeSignalTest --output-on-failure`
- 娑ㄨ穼鑹?`#e54648`锛堢孩锛?`#2e9e5b`锛堢豢锛?涓€?`#d4d4d4`锛涜瘎绾ц壊锛歋trongBuy 绾€丅uy 娴呯孩銆丯eutral 鐏般€丼ell 娴呯豢銆丼trongSell 娣辩豢
- 瀹夊叏寮傛妯″紡锛堥」鐩搧寰嬶級锛欼O/Worker lambda 鎸夊€兼崟鑾?provider/shared_ptr + `QPointer` 瀹堝崼 + `QMetaObject::invokeMethod(guard, ..., Qt::QueuedConnection)`锛涚姝㈣８ `this` 鎹曡幏
- 鎸囨爣锛歚st::indicators::rsi(closes, 14)` / `st::indicators::macd(closes)`锛坒oundation/utils/indicators.h锛夛紝鏈€煎彲鑳戒负 NaN
- 褰㈡€侊細`st::pattern::PatternRecognizer::detectAt(BarSeries, 3)`锛堣繎 3 鏍圭獥鍙ｏ紝minBars=40 鍓嶇疆锛?- 鎯呯华锛歚st::sentiment::SentimentAnalyzer::averageScore(items)`锛坈onst锛孖O 绾跨▼瀹夊叏锛夛紱鏃?provider/鏂伴椈涓虹┖ 鈫?鎯呯华鍒嗛」缂哄け
- 鍘嗗彶淇″彿璁板綍锛氶潰鏉垮唴鍐呭瓨琛紙浠ｇ爜/鏃ユ湡/璇勭骇/寰楀垎锛夛紝涓婇檺 50 鏉★紝鏈細璇濇湁鏁堬紙v2 鍙寔涔呭寲 JSON锛?
---

### Task 1: Engine 鈥?composite_signal 绾嚱鏁?+ 鍗曟祴

**Files:**
- Create: `src/intelligence/signal/composite_signal.h`
- Create: `src/intelligence/signal/composite_signal.cpp`
- Test: `tests/test_intelligence/test_composite_signal.cpp`
- Modify: `src/CMakeLists.txt`锛坰t_intelligence 鍔?`intelligence/signal/composite_signal.cpp`锛?- Modify: `tests/CMakeLists.txt`锛坱est_intelligence 鍔?`test_intelligence/test_composite_signal.cpp`锛?
**Interfaces:**
- Produces:
  - `enum class SignalRating : uint8_t { StrongBuy, Buy, Neutral, Sell, StrongSell }`
  - `struct SignalComponent { std::string name; double score = 0.0; double weight = 0.0; std::string detail; }`
  - `struct CompositeSignal { SignalRating rating = SignalRating::Neutral; double score = 0.0; double confidence = 0.0; std::vector<SignalComponent> components; std::string summary; }`
  - `std::string ratingName(SignalRating)`锛堜腑鏂囧悕锛?  - `CompositeSignal composeSignal(const std::vector<pattern::PatternSignal>& patterns, const sentiment::SentimentScore& sentiment, double rsi, const indicators::MacdResult& macd, double close, double prevClose, const std::vector<double>& weights = {})`
- Consumes: `pattern::PatternSignal`锛坕ntelligence/pattern/pattern_types.h锛夈€乣sentiment::SentimentScore`锛坕ntelligence/sentiment/sentiment_types.h锛夈€乣indicators::MacdResult`锛坒oundation/utils/indicators.h锛夈€?
**鎵撳垎瑙勫垯锛堥粯璁ゆ潈閲?褰㈡€?0.4 / 鎯呯华 0.3 / 鎶€鏈?0.3锛夛細**
- 褰㈡€佸垎椤癸細閬嶅巻 patterns锛宍contribution = direction(卤1) * confidence`锛圖oji=0 鏂瑰悜璺宠繃锛夛紱鍙?|contribution| 鏈€澶ц€咃紙澶氬舰鎬佸彇鏈€鏄捐憲锛岄伩鍏嶅彔鍔犻噸澶嶈鏁帮級锛?*patterns 涓虹┖ = 鏃犺瘉鎹?鈫?鍒嗛」缂哄け锛堟潈閲嶆姌鍑忥紝涓庢儏缁竴鑷达級**锛沝etail = 褰㈡€佸悕鍒楄〃
- 鎯呯华鍒嗛」锛歴core = sentiment.score锛?1~+1锛夛紝detail = sentiment.summary锛涙棤鏂伴椈锛坕tems 绌?鈫?score 0 涓€э級鈫?**鍒嗛」缂哄け**锛堟潈閲嶆姌鍑忥級
- 鎶€鏈垎椤癸紙RSI/MACD/鍔ㄩ噺 涓夊瓙鍒嗗钩鍧囷紝鏃犲彲鐢ㄥ瓙鍒嗗垯鍒嗛」缂哄け锛夛細
  - RSI锛?30 鈫?+1锛堣秴鍗栵級锛?70 鈫?-1锛堣秴涔帮級锛涘惁鍒?(50-rsi)/20*0.5锛?0鈫?0.5銆?0鈫?銆?0鈫?0.5锛夛紱NaN 鈫?鏃?  - MACD锛歞if>dea 涓?hist>0 鈫?+0.5锛堥噾鍙夊澶达級锛沝if<dea 涓?hist<0 鈫?-0.5锛涘叾浠?鈫?0锛汵aN 鈫?鏃?  - 鍔ㄩ噺锛歱revClose>0 鏃?clamp((close-prevClose)/prevClose / 0.03, -1, 1)锛?% 鍗曟棩娑ㄨ穼 = 婊″垎锛夛紱鍚﹀垯鏃?  - detail = "RSI 62.3 路 MACD 閲戝弶 路 +1.2%"
- 缁煎悎 score = 危(present 鍒嗛」 score脳weight) / 危(present 鍒嗛」 weight)锛涘叏缂哄け 鈫?score=0
- 璇勭骇闃堝€硷細score 鈮?+0.5 鈫?StrongBuy锛涒墺 +0.2 鈫?Buy锛涒墹 -0.5 鈫?StrongSell锛涒墹 -0.2 鈫?Sell锛涘叾浣?Neutral
- confidence = coverage 脳 agreement锛沜overage = 危(present weight)/危(all weight)锛沘greement = 1 - (max-min)/2锛堝垎椤归棿鏋佸樊褰掍竴锛屽崟鍒嗛」 = 1锛?- summary锛歚缁煎悎淇″彿锛氬己鐑堜拱鍏ワ紙+0.72锛夆€斺€?K绾垮舰鎬佺湅娑?+ 鑸嗘儏绉瀬 + 鎶€鏈秴鍗朻锛堥┍鍔ㄥ垎椤规寜 |score| 闄嶅簭鍒椾腑鏂囧悕锛屽叏缂哄け 鈫?"鏁版嵁涓嶈冻"锛?
- [x] **Step 1: 鍐欏け璐ユ祴璇?`tests/test_intelligence/test_composite_signal.cpp`**

```cpp
#include "intelligence/signal/composite_signal.h"
#include "foundation/utils/indicators.h"
#include <gtest/gtest.h>

namespace st {
namespace {

using st::pattern::PatternSignal;
using st::pattern::PatternType;
using st::sentiment::SentimentScore;
using st::indicators::MacdResult;

PatternSignal bullSig(double conf) {
    PatternSignal s;
    s.type = PatternType::Hammer;
    s.confidence = conf;
    s.name = "閿ゅご绾?;
    return s;
}
PatternSignal bearSig(double conf) {
    PatternSignal s;
    s.type = PatternType::ShootingStar;
    s.confidence = conf;
    s.name = "娴佹槦";
    return s;
}
SentimentScore senti(double score) {
    SentimentScore s;
    s.score = score;
    s.summary = score > 0 ? "绉瀬" : score < 0 ? "娑堟瀬" : "涓€?;
    return s;
}
MacdResult macdOf(double dif, double dea, double hist) {
    MacdResult m;
    m.dif = { dif }; m.dea = { dea }; m.hist = { hist };
    return m;
}

TEST(CompositeSignalTest, BullishEverythingStrongBuy) {
    auto cs = composeSignal({ bullSig(0.9) }, senti(0.6), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::StrongBuy);
    EXPECT_GT(cs.score, 0.5);
    EXPECT_GT(cs.confidence, 0.8);
    EXPECT_EQ(cs.components.size(), 3u);
}

TEST(CompositeSignalTest, BearishEverythingStrongSell) {
    auto cs = composeSignal({ bearSig(0.9) }, senti(-0.6), 75.0,
                            macdOf(-0.5, 0.5, -0.8), 9.7, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::StrongSell);
    EXPECT_LT(cs.score, -0.5);
}

TEST(CompositeSignalTest, MixedSignalsNeutral) {
    auto cs = composeSignal({ bearSig(0.9) }, senti(0.6), 50.0,
                            macdOf(0.1, 0.0, 0.2), 10.0, 10.0);
    EXPECT_EQ(cs.rating, SignalRating::Neutral);
    EXPECT_NEAR(cs.score, 0.0, 0.3);
}

TEST(CompositeSignalTest, MissingSentimentDeductsConfidence) {
    auto cs = composeSignal({ bullSig(0.9) }, senti(0.0), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    // 鎯呯华 score=0 浠嶇畻瀛樺湪锛堜腑鎬ф柊闂伙級鈫?3 鍒嗛」鍏ㄨ鐩?    auto cs2 = composeSignal({ bullSig(0.9) }, SentimentScore{}, 25.0,
                             macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    // SentimentScore{} 榛樿 score=0 鈫?鎯呯华鍒嗛」瀛樺湪锛坰core 0锛夆啋 涓庝笂闈㈢瓑浠凤紵涓嶏細鏃犳柊闂?= 鍒嗛」缂哄け
    EXPECT_LT(cs2.confidence, cs.confidence);
}

TEST(CompositeSignalTest, OnlyPatternComponent) {
    auto cs = composeSignal({ bullSig(1.0) }, SentimentScore{}, 
                            std::numeric_limits<double>::quiet_NaN(),
                            MacdResult{}, 0.0, 0.0);
    EXPECT_EQ(cs.components.size(), 1u);
    EXPECT_NEAR(cs.score, 1.0, 1e-9);
    EXPECT_NEAR(cs.confidence, 0.4, 1e-9);  // 浠呭舰鎬佹潈閲?0.4
    EXPECT_EQ(cs.rating, SignalRating::StrongBuy);
}

TEST(CompositeSignalTest, RsiBoundaries) {
    // 鏋勯€犲彧鍚妧鏈垎椤癸紙鍏朵粬缂哄け锛?    auto rsiOnly = [](double rsiVal) {
        return composeSignal({}, SentimentScore{}, rsiVal,
                             MacdResult{}, 0.0, 0.0).components;
    };
    auto c1 = rsiOnly(25.0);
    ASSERT_EQ(c1.size(), 1u);
    EXPECT_NEAR(c1[0].score, 1.0, 1e-9);
    auto c2 = rsiOnly(75.0);
    EXPECT_NEAR(c2[0].score, -1.0, 1e-9);
    auto c3 = rsiOnly(50.0);
    EXPECT_NEAR(c3[0].score, 0.0, 1e-9);
    auto c4 = rsiOnly(30.0);
    EXPECT_NEAR(c4[0].score, 0.5, 1e-9);
}

TEST(CompositeSignalTest, MacdStates) {
    auto techScore = [](double dif, double dea, double hist) {
        // rsi 浼?NaN 鈫?鎶€鏈垎椤逛粎鍚?MACD 瀛愬垎锛屽彲绮剧‘鏂█
        auto comps = composeSignal({}, SentimentScore{},
                                   std::numeric_limits<double>::quiet_NaN(),
                                   macdOf(dif, dea, hist), 0.0, 0.0).components;
        for (const auto& c : comps) if (c.name == "鎶€鏈寚鏍?) return c.score;
        return -999.0;
    };
    EXPECT_NEAR(techScore(1.0, 0.5, 0.8), 0.5, 1e-9);  // 閲戝弶 + hist>0
    EXPECT_NEAR(techScore(-0.5, 0.5, -0.8), -0.5, 1e-9);  // 姝诲弶 + hist<0
    EXPECT_NEAR(techScore(0.5, 0.5, 0.0), 0.0, 1e-9);     // 绮樺悎
}

TEST(CompositeSignalTest, ThresholdBoundaries) {
    auto r = [](double score) {
        // 浠呭舰鎬佸垎椤癸紙confidence=score 鍙簿纭帶鍒剁患鍚堝垎锛?        return composeSignal({ bullSig(score) }, SentimentScore{},
                             std::numeric_limits<double>::quiet_NaN(),
                             MacdResult{}, 0.0, 0.0).rating;
    };
    EXPECT_EQ(r(0.5), SignalRating::StrongBuy);
    EXPECT_EQ(r(0.49), SignalRating::Buy);
    EXPECT_EQ(r(0.2), SignalRating::Buy);
    EXPECT_EQ(r(0.19), SignalRating::Neutral);
    EXPECT_EQ(r(-0.2), SignalRating::Sell);
    EXPECT_EQ(r(-0.5), SignalRating::StrongSell);
}

TEST(CompositeSignalTest, NoDataAtAllNeutral) {
    auto cs = composeSignal({}, SentimentScore{},
                            std::numeric_limits<double>::quiet_NaN(),
                            MacdResult{}, 0.0, 0.0);
    EXPECT_EQ(cs.rating, SignalRating::Neutral);
    EXPECT_NEAR(cs.score, 0.0, 1e-9);
    EXPECT_NEAR(cs.confidence, 0.0, 1e-9);
    EXPECT_TRUE(cs.components.empty());
}

TEST(CompositeSignalTest, MomentumContributesToTech) {
    auto flat = composeSignal({}, SentimentScore{}, 50.0,
                              macdOf(0.0, 0.0, 0.0), 10.0, 10.0);
    auto up = composeSignal({}, SentimentScore{}, 50.0,
                            macdOf(0.0, 0.0, 0.0), 10.3, 10.0);
    auto techOf = [](const CompositeSignal& cs) {
        for (const auto& c : cs.components) if (c.name == "鎶€鏈寚鏍?) return c.score;
        return 0.0;
    };
    EXPECT_GT(techOf(up), techOf(flat));
}

TEST(CompositeSignalTest, CustomWeights) {
    // 鎯呯华鏉冮噸鎷夊埌 1.0锛氳礋闈㈡儏缁富瀵?    auto cs = composeSignal({ bullSig(1.0) }, senti(-1.0), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0,
                            {0.0, 1.0, 0.0});
    EXPECT_EQ(cs.rating, SignalRating::StrongSell);
    EXPECT_NEAR(cs.score, -1.0, 1e-9);
}

TEST(CompositeSignalTest, SummaryContainsRatingName) {
    auto cs = composeSignal({ bullSig(0.9) }, senti(0.6), 25.0,
                            macdOf(1.0, 0.5, 0.8), 10.3, 10.0);
    EXPECT_NE(cs.summary.find("寮虹儓涔板叆"), std::string::npos);
    auto empty = composeSignal({}, SentimentScore{},
                               std::numeric_limits<double>::quiet_NaN(),
                               MacdResult{}, 0.0, 0.0);
    EXPECT_NE(empty.summary.find("鏁版嵁涓嶈冻"), std::string::npos);
}

TEST(CompositeSignalTest, RatingNameCoverage) {
    EXPECT_EQ(ratingName(SignalRating::StrongBuy), "寮虹儓涔板叆");
    EXPECT_EQ(ratingName(SignalRating::Buy), "涔板叆");
    EXPECT_EQ(ratingName(SignalRating::Neutral), "瑙傛湜");
    EXPECT_EQ(ratingName(SignalRating::Sell), "鍗栧嚭");
    EXPECT_EQ(ratingName(SignalRating::StrongSell), "寮虹儓鍗栧嚭");
}

}  // namespace
}  // namespace st
```

- [x] **Step 2: 鏀?CMakeLists 骞惰窇娴嬭瘯纭澶辫触**

Modify `src/CMakeLists.txt` st_intelligence 鍧?`intelligence/sentiment/eastmoney_news_provider.cpp` 鍚庡姞涓€琛岋細
```cmake
    intelligence/signal/composite_signal.cpp
```
Modify `tests/CMakeLists.txt` test_intelligence 鍧?`test_intelligence/test_eastmoney_news_provider.cpp` 鍚庡姞涓€琛岋細
```cmake
        test_intelligence/test_composite_signal.cpp
```
Run: `cmake --build --preset with-qt 2>&1 | tail -5` 鍐?`ctest --preset default -R CompositeSignalTest --output-on-failure`
Expected: 缂栬瘧鎶ラ敊锛堝ご鏂囦欢涓嶅瓨鍦級銆?
- [x] **Step 3: 鍐欏疄鐜?`intelligence/signal/composite_signal.h`**

```cpp
#pragma once

#include "foundation/utils/indicators.h"
#include "intelligence/pattern/pattern_types.h"
#include "intelligence/sentiment/sentiment_types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace st::signal {

/// 缁煎悎淇″彿璇勭骇
enum class SignalRating : uint8_t {
    StrongBuy,   // 寮虹儓涔板叆
    Buy,         // 涔板叆
    Neutral,     // 瑙傛湜
    Sell,        // 鍗栧嚭
    StrongSell,  // 寮虹儓鍗栧嚭
};

/// 璇勭骇涓枃鍚?std::string ratingName(SignalRating rating);

/// 缁煎悎淇″彿鍒嗛」锛堝舰鎬?鎯呯华/鎶€鏈€︼級
struct SignalComponent {
    std::string name;       // 涓枃鍚嶏紙"K绾垮舰鎬?/"鑸嗘儏鎯呯华"/"鎶€鏈寚鏍?锛?    double score = 0.0;     // -1 ~ +1
    double weight = 0.0;    // 璇ュ垎椤规潈閲?    std::string detail;     // 鍒嗛」璇存槑
};

/// 缁煎悎淇″彿
struct CompositeSignal {
    SignalRating rating = SignalRating::Neutral;
    double score = 0.0;          // 鍔犳潈缁煎悎 -1 ~ +1
    double confidence = 0.0;     // 0~1锛堝垎椤硅鐩栧害 脳 涓€鑷村害锛?    std::vector<SignalComponent> components;  // 浠呭惈瀛樺湪鐨勫垎椤?    std::string summary;         // 涓枃涓€鍙ヨ瘽缁撹
};

/// 铻嶅悎褰㈡€?+ 鎯呯华 + 鎶€鏈寚鏍?鈫?缁煎悎淇″彿銆傜函 C++17锛屾棤 Qt 渚濊禆锛屽彲鍗曟祴銆?///
/// 榛樿鏉冮噸 褰㈡€?0.4 / 鎯呯华 0.3 / 鎶€鏈?0.3锛坵eights 鍙鐩栵紝闇€涓庡垎椤规暟 3 瀵归綈锛?/// 鑷姩褰掍竴鍖栵級銆傜己澶卞垎椤癸紙鏃犳柊闂?鎸囨爣 NaN/鏃犱环鏍硷級鎸夋潈閲嶆姌鍑忚鐩栧害銆?/// rating 闃堝€硷細|score| 鈮?0.5 Strong銆佲墺 0.2 鏅€氥€佸叾浣?Neutral銆?CompositeSignal composeSignal(
    const std::vector<pattern::PatternSignal>& patterns,
    const sentiment::SentimentScore& sentiment,
    double rsi,
    const indicators::MacdResult& macd,
    double close, double prevClose,
    const std::vector<double>& weights = {});

} // namespace st::signal
```

- [x] **Step 4: 鍐欏疄鐜?`intelligence/signal/composite_signal.cpp`**

```cpp
#include "intelligence/signal/composite_signal.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace st::signal {

namespace {

constexpr double kStrongThreshold = 0.5;
constexpr double kNormalThreshold = 0.2;

std::string ratingSummary(SignalRating r) {
    switch (r) {
        case SignalRating::StrongBuy:  return "寮虹儓涔板叆";
        case SignalRating::Buy:        return "涔板叆";
        case SignalRating::Neutral:    return "瑙傛湜";
        case SignalRating::Sell:       return "鍗栧嚭";
        case SignalRating::StrongSell: return "寮虹儓鍗栧嚭";
    }
    return "瑙傛湜";
}

double clamp1(double v) { return std::clamp(v, -1.0, 1.0); }

/// 鎶€鏈瓙鍒嗭細RSI锛堣秴鍗栨/瓒呬拱璐燂級
std::optional<double> rsiScore(double rsi) {
    if (!std::isfinite(rsi)) return std::nullopt;
    if (rsi < 30.0) return 1.0;
    if (rsi > 70.0) return -1.0;
    return (50.0 - rsi) / 20.0 * 0.5;
}

/// 鎶€鏈瓙鍒嗭細MACD锛堥噾鍙夊澶?姝诲弶绌哄ご锛?std::optional<double> macdScore(const indicators::MacdResult& macd) {
    if (macd.dif.empty() || macd.dea.empty() || macd.hist.empty()) return std::nullopt;
    const double dif = macd.dif.back(), dea = macd.dea.back(), hist = macd.hist.back();
    if (!std::isfinite(dif) || !std::isfinite(dea) || !std::isfinite(hist)) return std::nullopt;
    if (dif > dea && hist > 0.0) return 0.5;
    if (dif < dea && hist < 0.0) return -0.5;
    return 0.0;
}

/// 鎶€鏈瓙鍒嗭細鍗曟棩鍔ㄩ噺锛?% = 婊″垎锛?std::optional<double> momentumScore(double close, double prevClose) {
    if (!(close > 0.0) || !(prevClose > 0.0)) return std::nullopt;
    return clamp1((close - prevClose) / prevClose / 0.03);
}

}  // namespace

std::string ratingName(SignalRating rating) { return ratingSummary(rating); }

CompositeSignal composeSignal(
    const std::vector<pattern::PatternSignal>& patterns,
    const sentiment::SentimentScore& sentiment,
    double rsi,
    const indicators::MacdResult& macd,
    double close, double prevClose,
    const std::vector<double>& weights) {

    // 榛樿鏉冮噸锛氬舰鎬?0.4 / 鎯呯华 0.3 / 鎶€鏈?0.3
    std::vector<double> w = weights;
    if (w.size() != 3) w = {0.4, 0.3, 0.3};
    const double wSum = w[0] + w[1] + w[2];
    if (wSum <= 0.0) w = {0.4, 0.3, 0.3};

    CompositeSignal out;
    std::vector<SignalComponent> present;

    // 1. 褰㈡€佸垎椤癸紙bars 瀛樺湪鍗虫湁鏁堬紱澶氬舰鎬佸彇 |contribution| 鏈€澶ц€咃級
    {
        SignalComponent c;
        c.name = "K绾垮舰鎬?;
        c.weight = w[0];
        double best = 0.0;
        std::string names;
        for (const auto& p : patterns) {
            if (!names.empty()) names += "銆?;
            names += p.name.empty() ? pattern::PatternRecognizer::typeName(p.type) : p.name;
            const double dir = pattern::PatternRecognizer::isBullish(p.type) ? 1.0
                             : pattern::PatternRecognizer::isBearish(p.type) ? -1.0 : 0.0;
            if (dir == 0.0) continue;
            const double contrib = dir * std::clamp(p.confidence, 0.0, 1.0);
            if (std::abs(contrib) > std::abs(best)) best = contrib;
        }
        c.score = best;
        c.detail = names.empty() ? "杩?鏍规棤鏄捐憲褰㈡€? : names;
        present.push_back(std::move(c));
    }

    // 2. 鎯呯华鍒嗛」锛堟棤鏂伴椈 鈫?缂哄け锛?    if (sentiment.summary.find("鏃犳暟鎹?) == std::string::npos &&
        !sentiment.summary.empty()) {
        SignalComponent c;
        c.name = "鑸嗘儏鎯呯华";
        c.weight = w[1];
        c.score = clamp1(sentiment.score);
        c.detail = sentiment.summary;
        present.push_back(std::move(c));
    }

    // 3. 鎶€鏈垎椤癸紙RSI/MACD/鍔ㄩ噺 瀛愬垎骞冲潎锛?    {
        const auto rs = rsiScore(rsi);
        const auto ms = macdScore(macd);
        const auto mom = momentumScore(close, prevClose);
        if (rs || ms || mom) {
            SignalComponent c;
            c.name = "鎶€鏈寚鏍?;
            c.weight = w[2];
            double sum = 0.0; int n = 0;
            if (rs) { sum += *rs; ++n; }
            if (ms) { sum += *ms; ++n; }
            if (mom) { sum += *mom; ++n; }
            c.score = clamp1(sum / n);
            std::ostringstream oss;
            oss << "RSI " << (rs ? std::to_string(static_cast<int>(std::round(rsi))) : "鈥?)
                << " 路 MACD " << (ms ? (*ms > 0 ? "閲戝弶" : *ms < 0 ? "姝诲弶" : "绮樺悎") : "鈥?);
            if (mom) {
                oss << " 路 " << ((close - prevClose) / prevClose * 100.0 >= 0 ? "+" : "")
                    << std::fixed << std::setprecision(1)
                    << (close - prevClose) / prevClose * 100.0 << "%";
            }
            c.detail = oss.str();
            present.push_back(std::move(c));
        }
    }

    // 缁煎悎鍒?+ 缃俊搴?    if (present.empty()) {
        out.summary = "缁煎悎淇″彿锛氳鏈涳紙鏁版嵁涓嶈冻锛?;
        return out;
    }
    double scoreSum = 0.0, weightSum = 0.0, coverageSum = 0.0;
    double minS = 1.0, maxS = -1.0;
    for (const auto& c : present) {
        scoreSum += c.score * c.weight;
        weightSum += c.weight;
        coverageSum += c.weight;
        minS = std::min(minS, c.score);
        maxS = std::max(maxS, c.score);
    }
    out.score = clamp1(scoreSum / weightSum);
    const double coverage = coverageSum / (w[0] + w[1] + w[2]);
    const double agreement = 1.0 - (maxS - minS) / 2.0;
    out.confidence = std::clamp(coverage * agreement, 0.0, 1.0);

    if (out.score >= kStrongThreshold) out.rating = SignalRating::StrongBuy;
    else if (out.score >= kNormalThreshold) out.rating = SignalRating::Buy;
    else if (out.score <= -kStrongThreshold) out.rating = SignalRating::StrongSell;
    else if (out.score <= -kNormalThreshold) out.rating = SignalRating::Sell;
    else out.rating = SignalRating::Neutral;

    // 鎽樿锛氭寜 |score| 闄嶅簭鍒楅┍鍔ㄥ垎椤?    std::vector<const SignalComponent*> sorted;
    for (const auto& c : present) sorted.push_back(&c);
    std::sort(sorted.begin(), sorted.end(),
              [](const SignalComponent* a, const SignalComponent* b) {
                  return std::abs(a->score) > std::abs(b->score);
              });
    std::string drivers;
    for (const auto* c : sorted) {
        if (std::abs(c->score) < 0.05) continue;
        if (!drivers.empty()) drivers += " + ";
        drivers += c->name;
        drivers += (c->score > 0 ? "鐪嬫定" : "鐪嬭穼");
    }
    std::ostringstream ss;
    ss << "缁煎悎淇″彿锛? << ratingSummary(out.rating)
       << "锛? << (out.score >= 0 ? "+" : "") << std::fixed << std::setprecision(2)
       << out.score << "锛夆€斺€?" << (drivers.empty() ? "淇″彿骞虫贰" : drivers);
    out.summary = ss.str();

    out.components = std::move(present);
    return out;
}

} // namespace st::signal
```

锛堟敞锛歚std::optional`/`std::ostringstream`/`std::setprecision` 闇€瀵瑰簲 include锛歚<optional>`銆乣<sstream>`銆乣<iomanip>`锛沗pattern::PatternRecognizer::typeName` 闇€ include `intelligence/pattern/pattern_recognizer.h`銆傦級

- [x] **Step 5: 鏋勫缓 + 璺戞祴璇曠‘璁ら€氳繃**

Run: `cmake --build --preset with-qt`锛堥浂璀﹀憡锛? `ctest --preset default -R CompositeSignalTest --output-on-failure`
Expected: 12 渚嬪叏 PASS銆?
- [x] **Step 6: Commit**

```bash
git add src/intelligence/signal/composite_signal.h src/intelligence/signal/composite_signal.cpp \
        tests/test_intelligence/test_composite_signal.cpp src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat: 缁煎悎淇″彿寮曟搸 composeSignal锛堝舰鎬?鎯呯华+鎶€鏈瀺鍚堬紝绾嚱鏁?+ 12 渚嬪崟娴嬶級"
```

---

### Task 2: UI 鈥?AiSignalPanel + MainWindow 瑁呴厤

**Files:**
- Create: `src/ui/panels/ai_signal_panel.h`
- Create: `src/ui/panels/ai_signal_panel.cpp`
- Modify: `src/ui/main_window.h`锛堟垚鍛?+ 鏂规硶锛?- Modify: `src/ui/main_window.cpp`锛坉ock 鍒涘缓 + 4 澶?setStock 鎺ョ嚎锛?- Modify: `src/CMakeLists.txt`锛坰t_ui 鍔?`ui/panels/ai_signal_panel.cpp`锛?
**Interfaces:**
- Consumes: Task 1 `composeSignal`锛沗PatternRecognizer::detectAt`锛沗SentimentAnalyzer::averageScore`锛沗IDataProvider::getBars`锛沗st::indicators::rsi/macd`銆?- Produces: `class AiSignalPanel : public QWidget`鈥斺€擿setStock(const StockCode&, const QString&)`锛涗俊鍙?`void openChart(const StockCode&, const QString&)`锛堝巻鍙茶鍙屽嚮寮€鍥撅級銆?
- [x] **Step 1: 寤?`ai_signal_panel.h`**

```cpp
#pragma once

#include "foundation/stock_code.h"
#include "intelligence/signal/composite_signal.h"
#include <QWidget>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTableView;

namespace st {

class IDataProvider;
class SignalBarWidget;   // 鍒嗛」鍒嗘暟鏉★紙鑷粯锛?
/// AI 缁煎悎淇″彿闈㈡澘 鈥?鍗曞彧鑲＄エ铻嶅悎褰㈡€?鎯呯华+鎶€鏈?鈫?璇勭骇/缃俊搴?鍒嗛」/鍘嗗彶
///
/// 缁戝畾涓荤獥鍙ｄ腑澶浘琛?currentCodeChanged 鈫?setStock銆傚紓姝ワ細IO 鎷夋棩K+鏂伴椈 鈫?/// Worker 绠楁寚鏍?褰㈡€?缁煎悎淇″彿 鈫?QPointer 瀹堝崼 + gen 涓栦唬瀹堝崼鍥炰富绾跨▼銆?/// 鎯呯华婧?= EastMoneyNewsProvider锛堟棤鏂伴椈闄嶇骇缂哄け鍒嗛」锛屼笉闃诲淇″彿锛夈€?class AiSignalPanel : public QWidget {
    Q_OBJECT

public:
    explicit AiSignalPanel(IDataProvider* provider, QWidget* parent = nullptr);

    /// 鍒囨崲鍒版煇鍙偂绁紙寮傛璁＄畻骞跺埛鏂帮級
    void setStock(const StockCode& code, const QString& name);

signals:
    /// 鍘嗗彶淇″彿琛屽弻鍑?鈫?涓荤獥鍙ｅ紑鍥?    void openChart(const StockCode& code, const QString& name);

private:
    struct HistoryRow { StockCode code; QString name; QString date; SignalRating rating; double score; };
    void startCompute();
    void applyResult(st::signal::CompositeSignal cs, QString date);
    void addHistory(const HistoryRow& row);
    void resetToIdle();

    IDataProvider* provider_ = nullptr;
    std::shared_ptr<st::sentiment::ISentimentProvider> newsProvider_;  // 涓滆储璧勮锛坰hared 渚涘紓姝ユ崟鑾凤級
    StockCode code_;
    QString name_;
    int gen_ = 0;
    bool busy_ = false;

    QLabel* titleLabel_ = nullptr;
    QLabel* ratingLabel_ = nullptr;    // 璇勭骇澶у瓧
    QLabel* metaLabel_ = nullptr;      // 寰楀垎/缃俊搴?鏃ユ湡
    QLabel* summaryLabel_ = nullptr;   // 涓€鍙ヨ瘽缁撹
    std::vector<SignalBarWidget*> bars_;  // 鍒嗛」鏉?    QLabel* emptyLabel_ = nullptr;     // 鏃犲垎椤规彁绀?    QTableView* historyView_ = nullptr;
    QPushButton* refreshBtn_ = nullptr;
    std::vector<HistoryRow> history_;  // 浼氳瘽鍐呭巻鍙诧紙涓婇檺 50锛?};

} // namespace st
```

- [x] **Step 2: 寤?`ai_signal_panel.cpp`**

瑕佺偣锛?- `SignalBarWidget`锛堢鏈夎嚜缁樼被锛夛細paintEvent 鐢?鍚嶇О | 鍒嗘暟鏉★紙-1~+1 灞呬腑 0 杞达紝绾㈡缁胯礋锛墊 鏁板€?+ detail 鏂囨湰鎹㈣
- `startCompute()`锛?  - 浠?A 鑲′釜鑲★紙SH 6xxxxx / SZ 0/3xxxxx锛夋媺鏂伴椈锛涙寚鏁?鏉垮潡璺宠繃鎯呯华鍒嗛」锛坣ewsProvider 浠嶅彲绌猴級
  - `gen = ++gen_`锛汭O 浠诲姟锛歚provider->getBars(code, Daily, parseDate("2015-01-01"), now())` + `newsProvider ? newsProvider->fetchNews(code, 20) : {}`锛堝悓绾跨▼涓茶锛孴DX mutex 瀹夊叏锛?  - 鍥炰富绾跨▼ 鈫?busy_=false + gen 鏍￠獙 鈫?submitWorker锛歚BarSeries bars` 鈫?`PatternRecognizer().detectAt(bars, 3).items` + `rsi(closes,14)` 鏈€?+ `macd(closes)` 鏈€?+ `SentimentAnalyzer().averageScore(news)`锛堟棤鏂伴椈 鈫?SentimentScore{} 涓?summary 绌烘爣璁扮己澶憋級鈫?`composeSignal(...)` 鈫?鍥炰富绾跨▼搴旂敤
  - 鎯呯华缂哄け鏍囪锛歚SentimentScore{}` 榛樿 summary 涓虹┖ 鈫?寮曟搸鎸夈€宻ummary 涓虹┖鍗崇己澶便€嶅鐞嗭紙Task 1 瀹炵幇宸叉寜姝ょ害瀹氾級
- `applyResult`锛氬～ ratingLabel锛堝ぇ瀛?+ 璇勭骇鑹诧級銆乵etaLabel锛?寰楀垎 +0.72 路 缃俊搴?0.85 路 2026-08-14"锛夈€乻ummaryLabel锛堟崲琛岋級銆乥ars_锛堜笉瓒宠ˉ QLabel 鍗犱綅锛夈€乤ddHistory
- `addHistory`锛氳〃澶?鏃ユ湡/浠ｇ爜/鍚嶇О/璇勭骇/寰楀垎锛沬nsertRow(0) 鏈€鏂板湪鍓嶏紱瓒?50 鍒犲熬锛涘弻鍑昏 鈫?emit openChart
- 绌烘€侊細鏃犺偂绁ㄦ椂 ratingLabel "璇烽€夋嫨鑲＄エ"锛涗俊鍙疯绠椾腑 "璁＄畻涓€?
- 璇勭骇鑹诧細StrongBuy `#e54648` / Buy `#ff7b72` / Neutral `#d4d4d4` / Sell `#58a06b` / StrongSell `#2e9e5b`

- [x] **Step 3: MainWindow 瑁呴厤**

`main_window.h`锛歠orward declare `class AiSignalPanel;` + 鎴愬憳 `AiSignalPanel* aiSignalPanel_ = nullptr;` + 绉佹湁鏂规硶 `void setAiSignalStock(const StockCode& code, const QString& name);`锛堥泦涓?4 澶勬帴绾匡級

`main_window.cpp`锛?- `createDocks()`锛氬彸渚т笌绛圭爜 tabify锛坄tabifyDockWidget(chipDock_, aiDock_)`锛宎iDock 榛樿鏄剧ず锛夆€斺€擿QDockWidget* aiDock = new QDockWidget(tr("AI 缁煎悎淇″彿"), this); aiDock->setObjectName("aiSignalDock"); aiSignalPanel_ = new AiSignalPanel(provider_.get(), aiDock); aiDock->setWidget(aiSignalPanel_); addDockWidget(Qt::RightDockWidgetArea, aiDock); tabifyDockWidget(chipDock_, aiDock); aiDock->show();` 瀹?260-400
- 鎺ョ嚎 4 澶勶紙鎼滅储閫変腑 / 甯傚満闈㈡澘 openChart / 閲忓寲 openChart / openStockChart锛夛細`aiSignalPanel_ ? aiSignalPanel_->setStock(code, name) : void(0)`鈥斺€斾笌 chipPanel_ 鐩稿悓浣嶇疆
- `currentCodeChanged` 鈫?涓嶇洿鎺ラ┍鍔紙openStockChart 绛夊凡瑕嗙洊涓昏矾寰勶紱鎸囨暟/鑷畾涔夋寚鏁颁笉鍔犱俊鍙凤級
- 鍘嗗彶琛屽弻鍑?`openChart` 鈫?澶嶇敤 `openStockChart(code, name)` lambda
- 瑙嗗浘鑿滃崟鍙?toggle锛坄viewMenu->addAction(aiDock->toggleViewAction())`鈥斺€斾笌绛圭爜鍚岀粍锛?
- [x] **Step 4: CMake + 鏋勫缓 + 鍐掔儫**

Modify `src/CMakeLists.txt` st_ui 鍧?`ui/panels/pattern_panel.cpp` 鍚庡姞涓€琛岋細
```cmake
    ui/panels/ai_signal_panel.cpp
```
Run: `cmake --build --preset with-qt` 鈫?闆堕敊璇浂璀﹀憡锛沗ctest --preset default` 鈫?419 鍏ㄧ豢銆?鎵嬪姩鍐掔儫锛堢敤鎴锋墽琛岋級锛氭悳绱㈠紑鑲?鈫?闈㈡澘鑷姩鍑鸿瘎绾?鍒嗛」/鍘嗗彶锛涘垏鑲″埛鏂帮紱鏃犳柊闂昏偂鎯呯华缂哄け涓嶅穿锛涘巻鍙插弻鍑诲紑鍥撅紱鍏抽棴闈㈡澘涓嶅穿銆?
- [x] **Step 5: Commit**

```bash
git add src/ui/panels/ai_signal_panel.h src/ui/panels/ai_signal_panel.cpp \
        src/ui/main_window.h src/ui/main_window.cpp src/CMakeLists.txt
git commit -m "feat: AI 缁煎悎淇″彿闈㈡澘 AiSignalPanel + 涓荤獥鍙ｅ彸渚?Dock 瑁呴厤"
```

---

### Task 3: 鏂囨。鏀跺熬 + 缁堥獙

**Files:**
- Modify: `docs/DEVLOG.md`锛堥《閮ㄥ姞鏉＄洰锛?- Modify: `docs/changelog.md`锛堥《閮ㄥ姞鏉＄洰锛?- Modify: `CLAUDE.md`锛堝綋鍓嶉樁娈?+ 娴嬭瘯鏁?407 鈫?419锛?
- [x] **Step 1: DEVLOG 椤堕儴鍔犳潯鐩?*锛堥渶姹?瀹炴柦/楠岃瘉/宸茬煡闄愬埗锛屽惈鍒嗗眰淇璇存槑锛?- [x] **Step 2: changelog 椤堕儴鍔犳潯鐩?*锛堝姛鑳?+ 娴嬭瘯鏁帮級
- [x] **Step 3: CLAUDE.md**銆屽綋鍓嶉樁娈点€嶈杩藉姞 `鈫?P10 绗崄涓冭疆 鉁咃紙AI 缁煎悎淇″彿锛氬舰鎬?鎯呯华+鎶€鏈瀺鍚堣瘎绾?+ 涓荤獥鍙?Dock + 鍘嗗彶淇″彿锛塦锛涖€屾€昏: 鉁?407 tests銆嶆敼 `鉁?419 tests`锛汧oundation/Core/Data/Engine/Intelligence 璁℃暟鏇存柊锛圛ntelligence 52 鈫?64锛?- [x] **Step 4: 缁堥獙**

Run: `cmake --build --preset with-qt`锛堥浂閿欒闆惰鍛婏級+ `ctest --preset default`锛堝叏缁匡紝419锛夈€?
- [x] **Step 5: Commit + push**

```bash
git add docs/DEVLOG.md docs/changelog.md CLAUDE.md
git commit -m "docs: P10 绗崄涓冭疆 AI 缁煎悎淇″彿 DEVLOG/changelog/CLAUDE.md"
```
push 鐢辩敤鎴风‘璁ゅ悗鎵ц锛坄git push`锛夈€?
