#include <gtest/gtest.h>

#include "engine/screener/alpha191_factors.h"
#include "engine/screener/factor_math.h"
#include "foundation/utils/datetime.h"

#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace st;

namespace {

using FactorPtr = std::unique_ptr<IFactor>;

// ============================ 合成 Bar 序列工具 ============================

Bar makeBar(int index, double open, double high, double low, double close,
            double volume, double amount) {
    static const auto base = utils::parseDate("2024-01-02");
    Bar bar;
    bar.period = BarPeriod::Daily;
    bar.time = utils::addTradingDays(base, index);
    bar.open = open;
    bar.high = high;
    bar.low = low;
    bar.close = close;
    bar.volume = static_cast<Volume>(volume);
    bar.amount = amount;
    return bar;
}

std::optional<double> calculate(const IFactor& factor, const BarSeries& bars) {
    FactorContext ctx;
    ctx.bars = &bars;
    return factor.calculate(ctx);
}

/// 线性上涨：CLOSE = 100 + i，OPEN = CLOSE，HIGH = CLOSE+1，LOW = CLOSE-1，
/// VOLUME = 1000+10i，AMOUNT = VOLUME*CLOSE（即 VWAP == CLOSE）
BarSeries makeLinearUpSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const double close = 100.0 + static_cast<double>(i);
        const double volume = 1000.0 + 10.0 * static_cast<double>(i);
        bars.push_back(makeBar(i, close, close + 1.0, close - 1.0, close, volume,
                               volume * close));
    }
    return BarSeries(std::move(bars));
}

/// 线性下跌（用于方向断言）：CLOSE = 300 - i
BarSeries makeLinearDownSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const double close = 300.0 - static_cast<double>(i);
        const double volume = 1000.0 + 10.0 * static_cast<double>(i);
        bars.push_back(makeBar(i, close, close + 1.0, close - 1.0, close, volume,
                               volume * close));
    }
    return BarSeries(std::move(bars));
}

/// 震荡上行（价格 + 正弦波动，成交量递增并叠加周期）——覆盖全部因子的可算性
BarSeries makeOscillatingSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const double idx = static_cast<double>(i);
        const double close = 100.0 + 0.6 * idx + 2.0 * std::sin(0.45 * idx);
        const double prevClose = (i == 0) ? close - 0.2 : bars.back().close;
        const double open = prevClose + 0.15;
        const double high = std::max(open, close) + 0.8 + 0.1 * std::sin(0.3 * idx);
        const double low = std::min(open, close) - 0.8 - 0.1 * std::cos(0.3 * idx);
        const double volume =
            10000.0 + 400.0 * idx + 4000.0 * (1.0 + std::sin(0.8 * idx + 0.4));
        const double typical = (high + low + close) / 3.0;
        bars.push_back(makeBar(i, open, high, low, close, volume, volume * typical));
    }
    return BarSeries(std::move(bars));
}

/// 恒定实体：OPEN=100, CLOSE=101, HIGH=101.5, LOW=99.5, VOL=1000, AMOUNT=100000（VWAP=100）
BarSeries makeFlatBodySeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        bars.push_back(makeBar(i, 100.0, 101.5, 99.5, 101.0, 1000.0, 100000.0));
    }
    return BarSeries(std::move(bars));
}

/// 恒定价格、递增成交量（用于 Alpha004 的量能分支）
BarSeries makeFlatCloseRisingVolumeSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const double volume = 1000.0 + 10.0 * static_cast<double>(i);
        const double amount = volume * 100.0;
        bars.push_back(makeBar(i, 100.0, 100.5, 99.5, 100.0, volume, amount));
    }
    return BarSeries(std::move(bars));
}

/// 高开 1% 的恒定价格序列（OPEN=101, CLOSE=100）
BarSeries makeFlatGapUpSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        bars.push_back(makeBar(i, 101.0, 101.5, 99.5, 100.0, 1000.0, 100000.0));
    }
    return BarSeries(std::move(bars));
}

/// 多空失衡持续改善：CLOSE=99.5+0.008i（HIGH=101、LOW=99 固定）
BarSeries makeBalanceImprovingSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const double close = 99.5 + 0.008 * static_cast<double>(i);
        bars.push_back(makeBar(i, close, 101.0, 99.0, close, 1000.0, 1000.0 * close));
    }
    return BarSeries(std::move(bars));
}

/// 交替涨跌序列：奇数日上涨（CLOSE=100.5，量 300），偶数日下跌（CLOSE=100，量 100）
BarSeries makeAlternatingSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        const bool up = (i % 2) == 1;
        const double close = up ? 100.5 : 100.0;
        const double volume = up ? 300.0 : 100.0;
        bars.push_back(makeBar(i, close, close + 0.5, close - 0.5, close, volume,
                               volume * close));
    }
    return BarSeries(std::move(bars));
}

/// 指定成交额序列（其余 OHLC 恒定），用于 Alpha070 手算
BarSeries makeAmountSeries(const std::vector<double>& amounts) {
    std::vector<Bar> bars;
    for (size_t i = 0; i < amounts.size(); ++i) {
        bars.push_back(makeBar(static_cast<int>(i), 100.0, 100.5, 99.5, 100.0, 1000.0,
                               amounts[i]));
    }
    return BarSeries(std::move(bars));
}

BarSeries makeSmallSeries(int n) {
    std::vector<Bar> bars;
    for (int i = 0; i < n; ++i) {
        bars.push_back(makeBar(i, 100.0, 101.0, 99.0, 100.5, 1000.0, 100500.0));
    }
    return BarSeries(std::move(bars));
}

// ============================ 因子集合 ============================

template <typename FactorType>
void addFactor(std::vector<FactorPtr>& out) {
    out.push_back(std::make_unique<FactorType>());
}

std::vector<FactorPtr> allAlphaFactors() {
    std::vector<FactorPtr> out;
    out.reserve(33);
    addFactor<factors::Alpha002>(out);
    addFactor<factors::Alpha003>(out);
    addFactor<factors::Alpha004>(out);
    addFactor<factors::Alpha005>(out);
    addFactor<factors::Alpha009>(out);
    addFactor<factors::Alpha011>(out);
    addFactor<factors::Alpha013>(out);
    addFactor<factors::Alpha014>(out);
    addFactor<factors::Alpha015>(out);
    addFactor<factors::Alpha018>(out);
    addFactor<factors::Alpha020>(out);
    addFactor<factors::Alpha021>(out);
    addFactor<factors::Alpha022>(out);
    addFactor<factors::Alpha024>(out);
    addFactor<factors::Alpha026>(out);
    addFactor<factors::Alpha027>(out);
    addFactor<factors::Alpha028>(out);
    addFactor<factors::Alpha031>(out);
    addFactor<factors::Alpha034>(out);
    addFactor<factors::Alpha040>(out);
    addFactor<factors::Alpha043>(out);
    addFactor<factors::Alpha046>(out);
    addFactor<factors::Alpha053>(out);
    addFactor<factors::Alpha057>(out);
    addFactor<factors::Alpha063>(out);
    addFactor<factors::Alpha070>(out);
    addFactor<factors::Alpha080>(out);
    addFactor<factors::Alpha085>(out);
    addFactor<factors::Alpha088>(out);
    addFactor<factors::Alpha103>(out);
    addFactor<factors::Alpha110>(out);
    addFactor<factors::Alpha118>(out);
    addFactor<factors::Alpha127>(out);
    return out;
}

}  // namespace

// ============================ 元信息 / 数据不足 ============================

TEST(Alpha191Test, MetadataIsGtjaStyle) {
    const std::vector<std::string> expectedIds = {
        "alpha002", "alpha003", "alpha004", "alpha005", "alpha009", "alpha011",
        "alpha013", "alpha014", "alpha015", "alpha018", "alpha020", "alpha021",
        "alpha022", "alpha024", "alpha026", "alpha027", "alpha028", "alpha031",
        "alpha034", "alpha040", "alpha043", "alpha046", "alpha053", "alpha057",
        "alpha063", "alpha070", "alpha080", "alpha085", "alpha088", "alpha103",
        "alpha110", "alpha118", "alpha127"};

    auto all = allAlphaFactors();
    ASSERT_EQ(all.size(), expectedIds.size());
    ASSERT_EQ(all.size(), 33u);

    std::set<std::string> uniqueIds;
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& factor = *all[i];
        SCOPED_TRACE(factor.name());
        EXPECT_EQ(factor.name(), expectedIds[i]);
        EXPECT_EQ(factor.category(), FactorCategory::Alpha191);
        EXPECT_TRUE(factor.higherIsBetter());
        // displayName = "Alpha" + 数字后缀
        ASSERT_GT(factor.name().size(), 6u);
        EXPECT_EQ(factor.displayName(), "Alpha" + factor.name().substr(5));
        EXPECT_EQ(factor.displayName().size(), factor.name().size());
        const std::string desc = factor.description();
        // 说明只保留中文解释 + 数据窗口，不再展示公式
        EXPECT_EQ(desc.find("GTJA 原公式"), std::string::npos);
        EXPECT_EQ(desc.find("中文含义"), std::string::npos);
        EXPECT_NE(desc.find("数据窗口"), std::string::npos);
        EXPECT_FALSE(desc.empty());
        uniqueIds.insert(factor.name());
    }
    EXPECT_EQ(uniqueIds.size(), all.size());
}

TEST(Alpha191Test, InsufficientDataReturnsNullopt) {
    const BarSeries tiny = makeSmallSeries(5);  // 所有因子的最小窗口都 > 5
    for (const auto& factor : allAlphaFactors()) {
        SCOPED_TRACE(factor->name());
        EXPECT_FALSE(calculate(*factor, tiny).has_value());
    }
}

TEST(Alpha191Test, NullBarsReturnsNullopt) {
    for (const auto& factor : allAlphaFactors()) {
        SCOPED_TRACE(factor->name());
        FactorContext ctx;  // ctx.bars == nullptr
        EXPECT_FALSE(factor->calculate(ctx).has_value());
    }
}

TEST(Alpha191Test, AllFactorsFiniteOnOscillatingSeries) {
    const BarSeries series = makeOscillatingSeries(300);
    for (const auto& factor : allAlphaFactors()) {
        SCOPED_TRACE(factor->name());
        const auto value = calculate(*factor, series);
        ASSERT_TRUE(value.has_value());
        EXPECT_TRUE(std::isfinite(*value));
    }
}

// ============================ 手算校验 / 方向 ============================

TEST(Alpha191Test, Alpha002ReflectsBalanceImprovement) {
    // ((C-L)-(H-C))/(H-L) = (C-100)，每日 +0.008 → DELTA = 0.008 → -1*DELTA = -0.008
    const auto value = calculate(factors::Alpha002(), makeBalanceImprovingSeries(30));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, -0.008, 1e-9);
    EXPECT_LT(*value, 0.0);  // 失衡度改善当日 → 负值
}

TEST(Alpha191Test, Alpha003SumsAttackStrength) {
    // 线性上涨：C - MIN(L, 昨收) = C - (C-1) = 1，连续 6 日 → 6
    const auto value = calculate(factors::Alpha003(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 6.0, 1e-12);
}

TEST(Alpha191Test, Alpha004BreakoutBranches) {
    // 线性急涨：SUM(C,8)/8+STD < SUM(C,2)/2 → -1
    const auto trend = calculate(factors::Alpha004(), makeLinearUpSeries(260));
    ASSERT_TRUE(trend.has_value());
    EXPECT_DOUBLE_EQ(*trend, -1.0);

    // 横盘放量：不触发上下轨 → V/MEAN(V,20) > 1 → +1
    const auto volumeUp = calculate(factors::Alpha004(), makeFlatCloseRisingVolumeSeries(30));
    ASSERT_TRUE(volumeUp.has_value());
    EXPECT_DOUBLE_EQ(*volumeUp, 1.0);
}

TEST(Alpha191Test, Alpha005BoundedOnOscillatingSeries) {
    // -TSMAX(CORR(...),3)：相关系数 ∈ [-1,1] → 因子 ∈ [-1,1]
    const auto value = calculate(factors::Alpha005(), makeOscillatingSeries(300));
    ASSERT_TRUE(value.has_value());
    EXPECT_GE(*value, -1.0);
    EXPECT_LE(*value, 1.0);
    EXPECT_TRUE(std::isfinite(*value));
}

TEST(Alpha191Test, Alpha005NegativeWhenVolumeRankMatchesHighRank) {
    // 成交量 = 最高价的严格单调递增函数 → 任何 5 日窗口内「量能分位」与「最高价分位」
    // 完全一致 → CORR = 1 → -1*TSMAX(...,3) = -1
    std::vector<Bar> bars;
    for (int i = 0; i < 60; ++i) {
        const double idx = static_cast<double>(i);
        const double high = 100.0 + 2.0 * std::sin(0.9 * idx);
        const double volume = 10000.0 + 1000.0 * (high - 98.0);
        const double close = high - 0.5;
        bars.push_back(makeBar(i, close, high, high - 1.0, close, volume, volume * close));
    }
    const auto value = calculate(factors::Alpha005(), BarSeries(std::move(bars)));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, -1.0, 1e-9);
    EXPECT_LT(*value, 0.0);
}

TEST(Alpha191Test, Alpha009PositiveMoneyFlowInUptrend) {
    // 线性上涨：中间价上行 → 资金流为正 → SMA 后仍 > 0
    const auto value = calculate(factors::Alpha009(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);
}

TEST(Alpha191Test, Alpha011VolumeWeightedBalance) {
    // 恒定实体：失衡系数 = ((101-99.5)-(101.5-101))/2 = 0.5，×1000 量，6 日 → 3000
    const auto value = calculate(factors::Alpha011(), makeFlatBodySeries(25));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 3000.0, 1e-9);
}

TEST(Alpha191Test, Alpha013MidPriceVsVwap) {
    // 恒定实体：SQRT(101.5*99.5) - VWAP(100)
    const auto value = calculate(factors::Alpha013(), makeFlatBodySeries(25));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, std::sqrt(101.5 * 99.5) - 100.0, 1e-12);

    // 下跌趋势中 高低价几何均值 < 收盘价（VWAP=CLOSE），因子为负
    const auto down = calculate(factors::Alpha013(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_LT(*down, 0.0);
}

TEST(Alpha191Test, Alpha014FiveDayMomentum) {
    const auto value = calculate(factors::Alpha014(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 5.0, 1e-12);  // CLOSE_t - CLOSE_{t-5} = 5

    const auto flat = calculate(factors::Alpha014(), makeFlatBodySeries(25));
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(*flat, 0.0, 1e-12);
}

TEST(Alpha191Test, Alpha015OvernightGap) {
    const auto gapUp = calculate(factors::Alpha015(), makeFlatGapUpSeries(25));
    ASSERT_TRUE(gapUp.has_value());
    EXPECT_NEAR(*gapUp, 0.01, 1e-12);  // 101/100 - 1

    const auto upTrend = calculate(factors::Alpha015(), makeLinearUpSeries(260));
    ASSERT_TRUE(upTrend.has_value());
    EXPECT_GT(*upTrend, 0.0);
}

TEST(Alpha191Test, Alpha018FiveDayPriceRatio) {
    const auto value = calculate(factors::Alpha018(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 359.0 / 354.0, 1e-12);  // C / DELAY(C,5)，未减 1
    EXPECT_GT(*value, 1.0);

    const auto flat = calculate(factors::Alpha018(), makeFlatBodySeries(25));
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(*flat, 1.0, 1e-12);
}

TEST(Alpha191Test, Alpha020SixDayMomentumPercent) {
    const auto value = calculate(factors::Alpha020(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0 * 6.0 / 353.0, 1e-9);
    EXPECT_GT(*value, 0.0);
}

TEST(Alpha191Test, Alpha021RegressionSlope) {
    // 6 日均线每日 +1 → 对 SEQUENCE(6) 回归斜率 = 1
    const auto up = calculate(factors::Alpha021(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 1.0, 1e-9);

    // 线性下跌 → 斜率 = -1
    const auto down = calculate(factors::Alpha021(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, -1.0, 1e-9);
}

TEST(Alpha191Test, Alpha022BiasChangeNegativeWhenTrendStable) {
    // 线性上涨：乖离率随价格升高而缓慢收窄 → 3 日变化为负 → 平滑后 < 0
    const auto value = calculate(factors::Alpha022(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_LT(*value, 0.0);
    EXPECT_TRUE(std::isfinite(*value));
}

TEST(Alpha191Test, Alpha024SmoothedFiveDayMomentum) {
    const auto value = calculate(factors::Alpha024(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 5.0, 1e-12);
}

TEST(Alpha191Test, Alpha026LongTermDivergence) {
    // 线性上涨、VWAP==CLOSE：SUM(C,7)/7-C = -3；CORR(VWAP, DELAY(C,5),230) = 1 → ≈ -2
    const auto value = calculate(factors::Alpha026(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, -2.0, 1e-6);
}

TEST(Alpha191Test, Alpha027DualMomentumWeightedMean) {
    const auto value = calculate(factors::Alpha027(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_GT(*value, 0.0);  // 3 日、6 日动量均为正
    EXPECT_TRUE(std::isfinite(*value));
}

TEST(Alpha191Test, Alpha028KdjJValue) {
    // 线性上涨：RSV = (C-TSMIN(L,9))/(TSMAX(H,9)-TSMIN(L,9))*100 = 90，
    // SMA 后 K=D=90 → J = 3K-2D = 90
    const auto up = calculate(factors::Alpha028(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 90.0, 1e-9);

    // 线性下跌：RSV = 10 → J = 10
    const auto down = calculate(factors::Alpha028(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 10.0, 1e-9);
}

TEST(Alpha191Test, Alpha031TwelveDayBias) {
    const auto value = calculate(factors::Alpha031(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0 * 5.5 / 353.5, 1e-9);  // (359-353.5)/353.5*100
    EXPECT_GT(*value, 0.0);

    const auto flat = calculate(factors::Alpha031(), makeFlatBodySeries(25));
    ASSERT_TRUE(flat.has_value());
    EXPECT_NEAR(*flat, 0.0, 1e-12);
}

TEST(Alpha191Test, Alpha034MeanToCloseRatio) {
    const auto value = calculate(factors::Alpha034(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 353.5 / 359.0, 1e-12);
    EXPECT_LT(*value, 1.0);  // 上涨趋势现价高于均价

    // 下跌趋势现价低于均价 → > 1
    const auto down = calculate(factors::Alpha034(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_GT(*down, 1.0);
}

TEST(Alpha191Test, Alpha040UpVolumeRatio) {
    // 26 日窗口内 13 个上涨日（量 300）与 13 个下跌日（量 100）
    const auto value = calculate(factors::Alpha040(), makeAlternatingSeries(27));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 300.0, 1e-9);

    // 单调下跌：上涨量能为 0 → 0
    const auto down = calculate(factors::Alpha040(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 0.0, 1e-12);
}

TEST(Alpha191Test, Alpha043SixDayNetFlow) {
    // 最后 6 日：+300, -100, +300, -100, +300, -100 → 600
    const auto value = calculate(factors::Alpha043(), makeAlternatingSeries(27));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 600.0, 1e-12);

    const auto down = calculate(factors::Alpha043(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_LT(*down, 0.0);
}

TEST(Alpha191Test, Alpha046MultiMeanRatio) {
    // 线性上涨：MEAN 偏移 1+2.5+5.5+11.5 = 20.5 → 1 - 20.5/(4*359)
    const auto value = calculate(factors::Alpha046(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 1.0 - 20.5 / (4.0 * 359.0), 1e-12);
    EXPECT_LT(*value, 1.0);

    const auto down = calculate(factors::Alpha046(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_GT(*down, 1.0);
}

TEST(Alpha191Test, Alpha053UpDayRatio) {
    // 最后 12 日 6 涨 6 跌 → 50
    const auto value = calculate(factors::Alpha053(), makeAlternatingSeries(27));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 50.0, 1e-12);

    const auto down = calculate(factors::Alpha053(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 0.0, 1e-12);
}

TEST(Alpha191Test, Alpha057KdjKValue) {
    const auto up = calculate(factors::Alpha057(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 90.0, 1e-9);

    const auto down = calculate(factors::Alpha057(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 10.0, 1e-9);
}

TEST(Alpha191Test, Alpha063RsiStyle) {
    // 每日 +1：涨幅均值 1 / 波动均值 1 → 100
    const auto up = calculate(factors::Alpha063(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 100.0, 1e-12);

    // 每日 -1：涨幅均值 0 → 0
    const auto down = calculate(factors::Alpha063(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 0.0, 1e-12);
}

TEST(Alpha191Test, Alpha070AmountStandardDeviation) {
    // 总体标准差：{100,...,600} → sqrt(175000/6)
    const auto value = calculate(factors::Alpha070(), makeAmountSeries({100, 200, 300, 400, 500, 600}));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, std::sqrt(175000.0 / 6.0), 1e-9);
}

TEST(Alpha191Test, Alpha070MissingAmountReturnsNullopt) {
    // 数据源未提供成交额（AMOUNT 全 0）→ 视为数据不足，不把「零波动」当成有效值
    std::vector<Bar> bars;
    for (int i = 0; i < 12; ++i) {
        bars.push_back(makeBar(i, 100.0, 100.5, 99.5, 100.0, 1000.0, 0.0));
    }
    EXPECT_FALSE(calculate(factors::Alpha070(), BarSeries(std::move(bars))).has_value());
}

TEST(Alpha191Test, Alpha080VolumeChangeRate) {
    // 线性放量：V=3590，DELAY(V,5)=3540 → (50/3540)*100
    const auto value = calculate(factors::Alpha080(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0 * 50.0 / 3540.0, 1e-9);
    EXPECT_GT(*value, 0.0);
}

TEST(Alpha191Test, Alpha085VolumePriceRankProduct) {
    // 两个 TSRANK 都在 [0,1] → 乘积在 [0,1]
    const auto value = calculate(factors::Alpha085(), makeOscillatingSeries(300));
    ASSERT_TRUE(value.has_value());
    EXPECT_GE(*value, 0.0);
    EXPECT_LE(*value, 1.0);
    EXPECT_TRUE(std::isfinite(*value));
}

TEST(Alpha191Test, Alpha085HandCalculatedRankProduct) {
    // 36 日横盘后连续 8 日缩量下跌、末日量能进一步放大：
    // 当日 V/MEAN(V,20) 为近 20 日最高 → 量能分位 = 19/19 = 1；
    // -DELTA(C,7) 近 8 日 = {0.5,1.0,1.5,2.0,2.5,3.0,3.5,3.5}，当日排名 = 6/7
    // → 因子 = 1 * 6/7
    std::vector<Bar> bars;
    for (int i = 0; i < 45; ++i) {
        double close = 100.0;
        double volume = 1000.0;
        if (i >= 37) {
            close = 100.0 - 0.5 * static_cast<double>(i - 36);   // 99.5 → 96
            volume = 1000.0 + 500.0 * static_cast<double>(i - 36);  // 1500 → 5000
        }
        if (i == 44) volume = 6000.0;  // 末日放量，使当日量能分位为窗口最高
        bars.push_back(makeBar(i, close, close + 0.5, close - 0.5, close, volume,
                               volume * close));
    }
    const auto value = calculate(factors::Alpha085(), BarSeries(std::move(bars)));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 6.0 / 7.0, 1e-12);
    EXPECT_GT(*value, 0.0);
}

TEST(Alpha191Test, Alpha088TwentyDayMomentum) {
    const auto value = calculate(factors::Alpha088(), makeLinearUpSeries(260));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0 * 20.0 / 339.0, 1e-9);

    const auto down = calculate(factors::Alpha088(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_LT(*down, 0.0);
}

TEST(Alpha191Test, Alpha103LowDayPosition) {
    // 单调上涨：20 日最低点在 19 日前 → (20-19)/20*100 = 5
    const auto up = calculate(factors::Alpha103(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 5.0, 1e-9);

    // 单调下跌：最低点即今日 → 100
    const auto down = calculate(factors::Alpha103(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_NEAR(*down, 100.0, 1e-9);
}

TEST(Alpha191Test, Alpha110UpDownForceRatio) {
    // 恒定实体：MAX(0,H-昨收)=0.5，MAX(0,昨收-L)=1.5 → 0.5/1.5*100
    const auto value = calculate(factors::Alpha110(), makeFlatBodySeries(25));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 100.0 / 3.0, 1e-9);
}

TEST(Alpha191Test, Alpha118ShadowRatio) {
    // 恒定实体：上影线 H-O=1.5，下影线 O-L=0.5 → 1.5/0.5*100 = 300
    const auto value = calculate(factors::Alpha118(), makeFlatBodySeries(25));
    ASSERT_TRUE(value.has_value());
    EXPECT_NEAR(*value, 300.0, 1e-12);
}

TEST(Alpha191Test, Alpha127DrawdownRms) {
    // 单调上涨：每日即 12 日最高 → 回撤为 0 → RMS = 0
    const auto up = calculate(factors::Alpha127(), makeLinearUpSeries(260));
    ASSERT_TRUE(up.has_value());
    EXPECT_NEAR(*up, 0.0, 1e-12);

    // 下跌趋势：相对 12 日滚动高点存在回撤 → RMS > 0
    const auto down = calculate(factors::Alpha127(), makeLinearDownSeries(260));
    ASSERT_TRUE(down.has_value());
    EXPECT_GT(*down, 0.0);
}
