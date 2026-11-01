#include <gtest/gtest.h>

#include "engine/screener/alpha191_expression.h"
#include "engine/screener/alpha191_factors.h"
#include "engine/screener/factor_library.h"
#include "engine/screener/panel.h"
#include "engine/screener/stock_screener.h"
#include "data/data_cache.h"
#include "foundation/utils/datetime.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace st;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Fixture {
    std::vector<StockCode> stocks;
    std::vector<std::vector<Bar>> bars;
    std::vector<Bar> benchmark;
    std::unordered_map<std::string, QuoteFundamentals> quotes;
};

std::vector<Bar> makeBars(const StockCode& code, size_t days, double base,
                          double phase, double volumeBase) {
    std::vector<Bar> bars;
    bars.reserve(days);
    const auto start = utils::parseDate("2023-01-03");
    for (size_t i = 0; i < days; ++i) {
        const double x = static_cast<double>(i);
        Bar b;
        b.code = code;
        b.time = utils::addTradingDays(start, static_cast<int>(i));
        b.period = BarPeriod::Daily;
        b.close = base * (1.0 + 0.0006 * x + 0.03 * std::sin(0.08 * x + phase));
        b.open = b.close * (1.0 + 0.004 * std::cos(0.06 * x + phase));
        b.high = std::max(b.open, b.close) * 1.01;
        b.low = std::min(b.open, b.close) * 0.99;
        b.volume = static_cast<Volume>(volumeBase * (1.0 + 0.4 * std::sin(0.1 * x + phase)));
        b.amount = b.close * static_cast<double>(b.volume);
        bars.push_back(b);
    }
    return bars;
}

Fixture makeFixture(size_t stockCount = 6, size_t days = 320) {
    Fixture f;
    for (size_t i = 0; i < stockCount; ++i) {
        StockCode code(Market::SH, std::to_string(600000 + static_cast<int>(i)));
        f.stocks.push_back(code);
        f.bars.push_back(makeBars(code, days, 10.0 + 4.0 * static_cast<double>(i),
                                  static_cast<double>(i) * 0.7, 200000.0));
        QuoteFundamentals q;
        q.code = code;
        q.valid = true;
        q.totalShares = static_cast<double>(i + 1) * 1e9;
        q.pb = 1.0 + 0.7 * static_cast<double>((i * 2 + 1) % stockCount);
        q.peDynamic = 10.0 + 5.0 * static_cast<double>(i);
        q.marketCap = q.totalShares * f.bars.back().back().close;
        f.quotes[code.fullCode()] = q;
    }
    StockCode benchCode(Market::SH, "000300");
    f.benchmark = makeBars(benchCode, days, 3000.0, 0.3, 1e8);
    return f;
}

std::vector<double> evalFormula(const std::string& formula, const PanelData& panel) {
    factors::alpha191::Expression expr(formula);
    EXPECT_TRUE(expr.valid()) << formula << " : " << expr.error();
    return expr.evaluateLastRow(panel);
}

PanelData buildFixturePanel(const Fixture& f) {
    return buildPanel(f.stocks, f.bars, f.quotes, f.benchmark);
}

size_t finiteCount(const std::vector<double>& values) {
    size_t n = 0;
    for (double v : values) {
        if (std::isfinite(v)) ++n;
    }
    return n;
}

}  // namespace

// ============================ PanelData ============================

TEST(PanelDataTest, AlignsStocksAndDates) {
    Fixture f = makeFixture(6, 60);
    // 第二只股票缺少前 10 天 → 对齐后该段 NaN
    f.bars[1].erase(f.bars[1].begin(), f.bars[1].begin() + 10);
    const PanelData panel = buildFixturePanel(f);
    ASSERT_TRUE(panel.valid);
    EXPECT_EQ(panel.rows(), 60u);
    EXPECT_EQ(panel.cols(), 6u);

    EXPECT_TRUE(std::isnan(panel.close[0 * panel.cols() + 1]));
    EXPECT_FALSE(std::isnan(panel.close[0 * panel.cols() + 0]));
    EXPECT_FALSE(std::isnan(panel.close[59 * panel.cols() + 1]));
    // 收益率只在有前收时存在
    EXPECT_FALSE(std::isfinite(panel.ret[0 * panel.cols() + 0]));
    EXPECT_TRUE(std::isfinite(panel.ret[59 * panel.cols() + 0]));
    // 基准指数与风格因子
    EXPECT_TRUE(std::isfinite(panel.benchClose.back()));
    EXPECT_TRUE(std::isfinite(panel.mkt.back()));
    EXPECT_TRUE(std::isfinite(panel.smb.back()));
    EXPECT_TRUE(std::isfinite(panel.hml.back()));
}

// ============================ 表达式引擎 ============================

TEST(Alpha191ExpressionTest, RankIsCrossSectional) {
    // 3 只股票最后一天收盘 10/20/30 → RANK 分位 1/3、2/3、1
    Fixture f = makeFixture(3, 5);
    for (size_t i = 0; i < 3; ++i) {
        for (auto& bar : f.bars[i]) {
            bar.close = 10.0 * static_cast<double>(i + 1);
            bar.open = bar.close;
            bar.high = bar.close;
            bar.low = bar.close;
        }
    }
    const PanelData panel = buildFixturePanel(f);
    const auto values = evalFormula("RANK(CLOSE)", panel);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_NEAR(values[0], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(values[1], 2.0 / 3.0, 1e-12);
    EXPECT_NEAR(values[2], 1.0, 1e-12);
}

TEST(Alpha191ExpressionTest, DelayMeanSum) {
    Fixture f = makeFixture(1, 10);
    for (size_t i = 0; i < f.bars[0].size(); ++i) {
        f.bars[0][i].close = static_cast<double>(i + 1);
        f.bars[0][i].open = f.bars[0][i].close;
        f.bars[0][i].high = f.bars[0][i].close;
        f.bars[0][i].low = f.bars[0][i].close;
    }
    const PanelData panel = buildFixturePanel(f);
    EXPECT_NEAR(evalFormula("CLOSE", panel)[0], 10.0, 1e-12);
    EXPECT_NEAR(evalFormula("DELAY(CLOSE, 1)", panel)[0], 9.0, 1e-12);
    EXPECT_NEAR(evalFormula("MEAN(CLOSE, 3)", panel)[0], 9.0, 1e-12);
    EXPECT_NEAR(evalFormula("SUM(CLOSE, 3)", panel)[0], 27.0, 1e-12);
    EXPECT_NEAR(evalFormula("TSMAX(CLOSE, 5)", panel)[0], 10.0, 1e-12);
}

TEST(Alpha191ExpressionTest, BenchmarkAndStyleBroadcast) {
    Fixture f = makeFixture(6, 80);
    const PanelData panel = buildFixturePanel(f);
    const auto bench = evalFormula("BANCHMARKINDEXCLOSE", panel);
    ASSERT_EQ(bench.size(), 6u);
    EXPECT_NEAR(bench[0], panel.benchClose.back(), 1e-12);
    EXPECT_NEAR(bench[5], panel.benchClose.back(), 1e-12);

    for (const char* name : {"MKT", "SMB", "HML"}) {
        const auto values = evalFormula(name, panel);
        for (double v : values) EXPECT_TRUE(std::isfinite(v)) << name;
    }
}

// ============================ 公式表 / 因子 ============================

TEST(Alpha191FormulaTableTest, Has191SequentialFormulas) {
    const auto& table = factors::alpha191FormulaTable();
    ASSERT_EQ(table.size(), 191u);
    EXPECT_EQ(table.front().id, "alpha001");
    EXPECT_EQ(table.back().id, "alpha191");
    for (size_t i = 0; i < table.size(); ++i) {
        char expected[16];
        std::snprintf(expected, sizeof(expected), "alpha%03zu", i + 1);
        EXPECT_EQ(table[i].id, expected);
    }
}

TEST(Alpha191FormulaTableTest, AllFormulasParse) {
    std::vector<std::string> invalid;
    for (const auto& info : factors::alpha191FormulaTable()) {
        factors::alpha191::Expression expr(info.formula);
        if (!expr.valid()) invalid.push_back(info.id + ": " + expr.error());
    }
    std::string joined;
    for (const auto& e : invalid) joined += e + "\n";
    EXPECT_TRUE(invalid.empty()) << joined;
}

TEST(Alpha191PanelFactorTest, FactorsProduceValues) {
    const Fixture f = makeFixture(6, 320);
    const PanelData panel = buildFixturePanel(f);
    ASSERT_TRUE(panel.valid);

    auto weights = factors::makeAlpha191Factors();
    ASSERT_EQ(weights.size(), 191u);
    size_t usable = 0;
    std::vector<std::string> emptyIds;
    for (const auto& [factor, weight] : weights) {
        (void)weight;
        ASSERT_TRUE(factor->isPanel());
        const auto values = factor->calculatePanel(panel);
        if (finiteCount(values) > 0) {
            ++usable;
        } else {
            emptyIds.push_back(factor->name());
        }
    }
    std::string joined;
    for (const auto& id : emptyIds) joined += id + " ";    EXPECT_GE(usable, 175u) << "无有效值的因子: " << joined;

    // 关键 RANK / 基准 / 风格 / 自定义因子至少各一个有效
    std::unordered_map<std::string, size_t> index;
    for (size_t i = 0; i < weights.size(); ++i) index[weights[i].first->name()] = i;
    for (const char* id : {"alpha001", "alpha016", "alpha030", "alpha032",
                           "alpha075", "alpha143", "alpha149", "alpha165",
                           "alpha181", "alpha182", "alpha183", "alpha191"}) {
        auto it = index.find(id);
        ASSERT_TRUE(it != index.end()) << id;
        const auto values = weights[it->second].first->calculatePanel(panel);
        EXPECT_GT(finiteCount(values), 0u) << id;
    }
}

// ============================ 引擎集成 ============================

TEST(StockScreenerPanelTest, RunsPanelFactorAcrossPool) {
    Fixture f = makeFixture(6, 320);
    DataCache cache;
    for (size_t i = 0; i < f.stocks.size(); ++i) {
        cache.cacheBars(f.stocks[i], BarPeriod::Daily, f.bars[i]);
    }

    ScreenerConfig config;
    config.period = BarPeriod::Daily;
    config.endDate = f.bars[0].back().time;
    config.lookbackDays = 320;
    config.topN = 10;

    StockScreener screener;
    screener.setConfig(config);
    screener.setDataCache(&cache);
    screener.setQuoteFundamentals(f.quotes);
    screener.setBenchmarkBars(f.benchmark);

    // Alpha020：6 日动量（面板因子）
    std::shared_ptr<IFactor> momentum;
    for (const auto& info : factors::alpha191FormulaTable()) {
        if (info.id == "alpha020") {
            momentum = std::make_shared<factors::Alpha191Factor>(
                info.id, info.formula, "测试用");
            break;
        }
    }
    ASSERT_TRUE(momentum != nullptr);
    screener.addFactor(momentum, 1.0);

    const auto results = screener.run(f.stocks);
    ASSERT_EQ(results.size(), 6u);
    for (const auto& r : results) {
        ASSERT_EQ(r.factorResults.size(), 1u);
        EXPECT_TRUE(r.factorResults[0].rawValue.has_value()) << r.code.fullCode();
        EXPECT_GE(r.factorResults[0].score, 0.0);
        EXPECT_LE(r.factorResults[0].score, 100.0);
        EXPECT_GE(r.totalScore, 0.0);
        EXPECT_LE(r.totalScore, 100.0);
    }
}
