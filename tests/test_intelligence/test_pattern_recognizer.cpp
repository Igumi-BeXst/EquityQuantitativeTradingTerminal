#include <gtest/gtest.h>
#include "intelligence/pattern/pattern_recognizer.h"
#include "foundation/utils/datetime.h"

#include <vector>

using namespace st;
using namespace st::pattern;

namespace {

/// K 线规格（OHLCV）
struct BarSpec {
    double open = 100.0;
    double high = 100.0;
    double low = 100.0;
    double close = 100.0;
    double volume = 100000.0;
};

BarSeries makeBars(const std::vector<BarSpec>& specs) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2024-01-02");
    for (size_t i = 0; i < specs.size(); ++i) {
        Bar bar;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, static_cast<int>(i));
        bar.open = specs[i].open;
        bar.high = specs[i].high;
        bar.low = specs[i].low;
        bar.close = specs[i].close;
        bar.volume = static_cast<Volume>(specs[i].volume);
        bars.push_back(bar);
    }
    return BarSeries(std::move(bars));
}

/// 单调上涨基准（每根为阳线）
std::vector<BarSpec> rising(int n, double start = 100.0, double step = 1.0) {
    std::vector<BarSpec> specs;
    for (int i = 0; i < n; ++i) {
        BarSpec s;
        const double price = start + step * i;
        s.open = price;
        s.close = price + 0.5;
        s.high = price + 1.0;
        s.low = price;
        specs.push_back(s);
    }
    return specs;
}

/// 单调下跌基准（每根为阴线）
std::vector<BarSpec> falling(int n, double start = 150.0, double step = 1.0) {
    std::vector<BarSpec> specs;
    for (int i = 0; i < n; ++i) {
        BarSpec s;
        const double price = start - step * i;
        s.open = price;
        s.close = price - 0.5;
        s.high = price + 0.5;
        s.low = price - 1.0;
        specs.push_back(s);
    }
    return specs;
}

bool hasSignal(const PatternDetectResult& r, PatternType type, int* index = nullptr) {
    for (const auto& s : r.items) {
        if (s.type == type) {
            if (index) *index = s.index;
            return true;
        }
    }
    return false;
}

} // namespace

TEST(PatternRecognizerTest, InsufficientDataReturnsEmpty) {
    auto series = makeBars(rising(30));
    PatternRecognizer rec;
    EXPECT_TRUE(rec.detect(series).items.empty());
    EXPECT_TRUE(rec.detectAt(series).items.empty());
}

TEST(PatternRecognizerTest, DetectDoji) {
    auto specs = rising(42);
    BarSpec d;
    d.open = 140.0;
    d.close = 140.0;
    d.high = 141.0;
    d.low = 139.0;
    specs.push_back(d);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    int idx = -1;
    ASSERT_TRUE(hasSignal(result, PatternType::Doji, &idx));
    EXPECT_EQ(idx, 42);
    for (const auto& s : result.items) {
        if (s.type == PatternType::Doji) {
            EXPECT_NEAR(s.confidence, 0.6, 1e-9);
        }
    }
}

TEST(PatternRecognizerTest, DetectHammer) {
    auto specs = falling(42);
    BarSpec h;
    h.open = 98.5;
    h.close = 99.5;
    h.high = 100.0;
    h.low = 95.0;
    specs.push_back(h);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::Hammer));
}

TEST(PatternRecognizerTest, DetectInvertedHammer) {
    auto specs = falling(42);
    BarSpec h;
    h.open = 96.5;
    h.close = 95.5;
    h.high = 100.0;
    h.low = 95.0;
    specs.push_back(h);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::InvertedHammer));
}

TEST(PatternRecognizerTest, DetectHangingMan) {
    auto specs = rising(42);
    BarSpec h;
    h.open = 98.5;
    h.close = 99.5;
    h.high = 100.0;
    h.low = 95.0;
    specs.push_back(h);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::HangingMan));
}

TEST(PatternRecognizerTest, DetectShootingStar) {
    auto specs = rising(42);
    BarSpec h;
    h.open = 95.5;
    h.close = 96.5;
    h.high = 100.0;
    h.low = 95.0;
    specs.push_back(h);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::ShootingStar));
}

TEST(PatternRecognizerTest, DetectBullishEngulfing) {
    auto specs = rising(41);
    specs.pop_back();
    BarSpec prev;
    prev.open = 100.0;
    prev.close = 98.0;
    prev.high = 101.0;
    prev.low = 97.0;
    specs.push_back(prev);
    BarSpec cur;
    cur.open = 97.5;
    cur.close = 100.5;
    cur.high = 101.0;
    cur.low = 97.0;
    specs.push_back(cur);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::BullishEngulfing));
}

TEST(PatternRecognizerTest, DetectBearishEngulfing) {
    auto specs = falling(41);
    specs.pop_back();
    BarSpec prev;
    prev.open = 100.0;
    prev.close = 102.0;
    prev.high = 102.5;
    prev.low = 99.5;
    specs.push_back(prev);
    BarSpec cur;
    cur.open = 102.5;
    cur.close = 99.5;
    cur.high = 103.0;
    cur.low = 99.0;
    specs.push_back(cur);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::BearishEngulfing));
}

TEST(PatternRecognizerTest, EngulfingConfidenceBoost) {
    auto specs = rising(41);
    specs.pop_back();
    BarSpec prev;
    prev.open = 100.0;
    prev.close = 98.0;
    prev.high = 101.0;
    prev.low = 97.0;
    specs.push_back(prev);
    BarSpec cur;  // 实体 ≥ 2× 前实体 → 置信度 +0.1
    cur.open = 97.0;
    cur.close = 102.0;
    cur.high = 102.5;
    cur.low = 96.5;
    specs.push_back(cur);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    bool found = false;
    for (const auto& s : result.items) {
        if (s.type == PatternType::BullishEngulfing) {
            EXPECT_NEAR(s.confidence, 0.85, 1e-9);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PatternRecognizerTest, DetectMorningStar) {
    auto specs = falling(40);
    BarSpec b0;
    b0.open = 100.0; b0.close = 95.0; b0.high = 100.5; b0.low = 94.5;
    specs.push_back(b0);
    BarSpec b1;
    b1.open = 94.8; b1.close = 94.9; b1.high = 95.2; b1.low = 94.6;
    specs.push_back(b1);
    BarSpec b2;
    b2.open = 95.5; b2.close = 99.0; b2.high = 99.5; b2.low = 95.0;
    specs.push_back(b2);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::MorningStar));
}

TEST(PatternRecognizerTest, DetectEveningStar) {
    auto specs = rising(40);
    BarSpec b0;
    b0.open = 95.0; b0.close = 100.0; b0.high = 100.5; b0.low = 94.5;
    specs.push_back(b0);
    BarSpec b1;
    b1.open = 100.2; b1.close = 100.1; b1.high = 100.5; b1.low = 99.8;
    specs.push_back(b1);
    BarSpec b2;
    b2.open = 99.0; b2.close = 96.0; b2.high = 99.5; b2.low = 95.5;
    specs.push_back(b2);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::EveningStar));
}

TEST(PatternRecognizerTest, DetectThreeWhiteSoldiers) {
    auto specs = rising(40);
    BarSpec b0;
    b0.open = 98.0; b0.close = 99.0; b0.high = 99.5; b0.low = 97.5;
    specs.push_back(b0);
    BarSpec b1;
    b1.open = 99.0; b1.close = 100.0; b1.high = 100.5; b1.low = 98.5;
    specs.push_back(b1);
    BarSpec b2;
    b2.open = 100.0; b2.close = 101.0; b2.high = 101.5; b2.low = 99.5;
    specs.push_back(b2);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::ThreeWhiteSoldiers));
}

TEST(PatternRecognizerTest, DetectThreeBlackCrows) {
    auto specs = falling(40);
    BarSpec b0;
    b0.open = 101.0; b0.close = 100.0; b0.high = 101.5; b0.low = 99.5;
    specs.push_back(b0);
    BarSpec b1;
    b1.open = 100.0; b1.close = 99.0; b1.high = 100.5; b1.low = 98.5;
    specs.push_back(b1);
    BarSpec b2;
    b2.open = 99.0; b2.close = 98.0; b2.high = 99.5; b2.low = 97.5;
    specs.push_back(b2);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::ThreeBlackCrows));
}

TEST(PatternRecognizerTest, DetectGoldenCross) {
    // 39 根横盘后陡升 → MA5 上穿 MA20
    std::vector<BarSpec> specs;
    for (int i = 0; i < 39; ++i) specs.push_back(BarSpec{});
    for (int i = 1; i <= 6; ++i) {
        BarSpec s;
        const double price = 100.0 + 15.0 * i;
        s.open = price;
        s.close = price + 1.0;
        s.high = price + 2.0;
        s.low = price;
        specs.push_back(s);
    }
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    int idx = -1;
    ASSERT_TRUE(hasSignal(result, PatternType::GoldenCross, &idx));
    EXPECT_EQ(idx, 39);
}

TEST(PatternRecognizerTest, DetectDeathCross) {
    // 39 根横盘后陡降 → MA5 下穿 MA20
    std::vector<BarSpec> specs;
    BarSpec hi;
    hi.open = 200.0;
    hi.close = 200.0;
    hi.high = 200.0;
    hi.low = 200.0;
    for (int i = 0; i < 39; ++i) specs.push_back(hi);
    for (int i = 1; i <= 6; ++i) {
        BarSpec s;
        const double price = 200.0 - 15.0 * i;
        s.open = price;
        s.close = price - 1.0;
        s.high = price;
        s.low = price - 2.0;
        specs.push_back(s);
    }
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::DeathCross));
}

TEST(PatternRecognizerTest, DetectBullishAlignment) {
    auto series = makeBars(rising(55, 100.0, 2.0));
    PatternRecognizer rec;
    auto result = rec.detect(series);
    EXPECT_TRUE(hasSignal(result, PatternType::BullishAlignment));
    EXPECT_FALSE(hasSignal(result, PatternType::BearishAlignment));
}

TEST(PatternRecognizerTest, DetectBearishAlignment) {
    auto series = makeBars(falling(55, 200.0, 2.0));
    PatternRecognizer rec;
    auto result = rec.detect(series);
    EXPECT_TRUE(hasSignal(result, PatternType::BearishAlignment));
    EXPECT_FALSE(hasSignal(result, PatternType::BullishAlignment));
}

TEST(PatternRecognizerTest, DetectVolumeBreakout) {
    std::vector<BarSpec> specs;
    BarSpec flat;
    flat.open = 100.0;
    flat.close = 100.0;
    flat.high = 100.5;
    flat.low = 99.5;
    for (int i = 0; i < 40; ++i) specs.push_back(flat);
    BarSpec bk;
    bk.open = 99.0;
    bk.close = 105.0;
    bk.high = 106.0;
    bk.low = 99.0;
    bk.volume = 300000.0;  // 3× 平均量
    specs.push_back(bk);
    PatternRecognizer rec;
    auto result = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(result, PatternType::VolumeBreakout));
}

TEST(PatternRecognizerTest, DetectAtOnlyRecentBars) {
    auto specs = falling(42);
    BarSpec d;
    d.open = 130.0;
    d.close = 130.0;
    d.high = 131.0;
    d.low = 129.0;
    specs[39] = d;  // 早期十字星（index 39，detect 覆盖但 detectAt(3) 不覆盖）
    BarSpec h;
    h.open = 98.5;
    h.close = 99.5;
    h.high = 100.0;
    h.low = 95.0;
    specs.push_back(h);  // 末端锤头（index 42）
    PatternRecognizer rec;
    auto full = rec.detect(makeBars(specs));
    EXPECT_TRUE(hasSignal(full, PatternType::Doji));
    EXPECT_TRUE(hasSignal(full, PatternType::Hammer));

    auto recent = rec.detectAt(makeBars(specs), 3);
    ASSERT_FALSE(recent.items.empty());
    const int n = 43;
    for (const auto& s : recent.items) {
        EXPECT_GE(s.index, n - 3);
    }
    EXPECT_FALSE(hasSignal(recent, PatternType::Doji));
    EXPECT_TRUE(hasSignal(recent, PatternType::Hammer));
}

TEST(PatternRecognizerTest, TypeNameAndDirectionCoverage) {
    const PatternType all[] = {
        PatternType::Doji,           PatternType::Hammer,
        PatternType::InvertedHammer, PatternType::HangingMan,
        PatternType::ShootingStar,   PatternType::BullishEngulfing,
        PatternType::BearishEngulfing, PatternType::MorningStar,
        PatternType::EveningStar,    PatternType::ThreeWhiteSoldiers,
        PatternType::ThreeBlackCrows, PatternType::GoldenCross,
        PatternType::DeathCross,     PatternType::BullishAlignment,
        PatternType::BearishAlignment, PatternType::VolumeBreakout,
    };
    int bull = 0;
    int bear = 0;
    for (auto t : all) {
        EXPECT_FALSE(PatternRecognizer::typeName(t).empty());
        EXPECT_FALSE(PatternRecognizer::isBullish(t) &&
                     PatternRecognizer::isBearish(t));
        if (PatternRecognizer::isBullish(t)) ++bull;
        if (PatternRecognizer::isBearish(t)) ++bear;
    }
    EXPECT_EQ(bull, 8);
    EXPECT_EQ(bear, 7);  // 十字星为中性
}
