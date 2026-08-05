// Intelligence 层实连验证工具 — 用真实行情跑通 4 个模块并打印结果
//
// 验证内容：
//   1. PatternRecognizer   形态识别（真实日K 最近形态信号）
//   2. PatternFactor       形态因子（pattern_score）
//   3. StrategyAdvisor     策略参数优化建议（MACross 真实网格回测 → 中文建议）
//   4. SentimentAnalyzer   舆情情绪（本地关键词打分桩）
// 数据：优先连 TDX 拉真实日K；连接失败则回退合成序列（保证离线可演示）。
#include "intelligence/pattern/pattern_recognizer.h"
#include "intelligence/advisor/strategy_advisor.h"
#include "intelligence/screener/pattern_factor.h"
#include "intelligence/sentiment/sentiment_analyzer.h"
#include "data/tdx/tdx_provider.h"
#include "data/data_cache.h"
#include "engine/optimizer/grid_search.h"
#include "engine/backtest/fee_calculator.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace st;
using namespace st::pattern;
using namespace st::advisor;
using namespace st::sentiment;

namespace {

/// 连接失败时的合成日K（振荡行情），保证工具离线可演示
std::vector<Bar> makeSyntheticBars(const StockCode& code, int n = 200) {
    std::vector<Bar> bars;
    auto base = utils::parseDate("2025-01-02");
    double price = 100.0;
    for (int i = 0; i < n; ++i) {
        Bar bar;
        bar.code = code;
        bar.period = BarPeriod::Daily;
        bar.time = utils::addTradingDays(base, i);
        const double open = price;
        price = std::max(10.0, price + std::sin(i * 0.12) * 0.9 + 0.25);
        bar.open = open;
        bar.close = price;
        bar.high = std::max(open, price) + 0.6;
        bar.low = std::min(open, price) - 0.6;
        bar.volume = 100000 + (i % 7) * 8000;
        bars.push_back(bar);
    }
    return bars;
}

void printPatterns(const BarSeries& bars, const PatternDetectResult& result) {
    if (result.items.empty()) {
        std::cout << "    (无形态信号)\n";
        return;
    }
    const size_t start = result.items.size() > 10 ? result.items.size() - 10 : 0;
    for (size_t i = start; i < result.items.size(); ++i) {
        const auto& s = result.items[i];
        const std::string date =
            utils::toDateTimeString(bars[s.index].time).substr(0, 10);
        std::printf("    [%s] %-8s 置信度 %.2f  %s\n",
                    date.c_str(), s.name.c_str(), s.confidence, s.description.c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    const StockCode code(Market::SH, "600519");  // 贵州茅台

    // 1. 数据：优先 TDX 真实日K，失败回退合成
    std::vector<Bar> bars;
    bool synthetic = false;
    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (provider.isConnected()) {
        bars = provider.getBars(code, BarPeriod::Daily, DateTime{}, utils::now());
        provider.disconnect();
    }
    if (bars.size() < 60) {
        std::cout << "[数据] TDX 连接失败或无数据，回退合成序列\n";
        bars = makeSyntheticBars(code);
        synthetic = true;
    }
    std::cout << "[数据] " << (synthetic ? "合成" : "TDX 真实") << " 日K "
              << bars.size() << " 根\n";

    // 2. PatternRecognizer — 形态识别
    std::cout << "\n===== 1. PatternRecognizer（" << code.fullCode()
              << " 最近形态信号）=====\n";
    BarSeries series(bars);
    PatternRecognizer recognizer;
    auto result = recognizer.detect(series);
    std::cout << "  共 " << result.items.size() << " 条信号，最近 10 条：\n";
    printPatterns(series, result);

    // 3. PatternFactor — 形态因子
    std::cout << "\n===== 2. PatternFactor（" << code.fullCode() << "）=====\n";
    FactorContext fctx;
    fctx.code = &code;
    fctx.bars = &series;
    st::screener::PatternFactor factor;
    auto fval = factor.calculate(fctx);
    if (fval) {
        std::printf("  pattern_score = %.1f（>50 偏多，<50 偏空）\n", *fval);
    } else {
        std::cout << "  pattern_score = 数据不足\n";
    }

    // 4. StrategyAdvisor — 策略参数优化建议
    std::cout << "\n===== 3. StrategyAdvisor（MACross 参数优化）=====\n";
    for (auto& b : bars) b.code = code;
    DataCache cache;
    cache.cacheBars(code, BarPeriod::Daily, bars);

    GridSearchConfig gcfg;
    gcfg.strategyId = "MACross";
    gcfg.ranges = {{"fastPeriod", 2, 6, 2}, {"slowPeriod", 10, 30, 10}};
    gcfg.symbols = {code};
    gcfg.startDate = DateTime{};
    gcfg.endDate = utils::now();
    gcfg.initialCapital = 100000.0;
    gcfg.period = BarPeriod::Daily;
    gcfg.feeConfig = FeeConfig::defaultAShare();
    gcfg.objective = Objective::TotalReturn;
    gcfg.cache = &cache;
    gcfg.parallelLanes = 1;

    GridSearchOptimizer opt;
    auto results = opt.run(gcfg);
    std::cout << "  网格 " << results.size() << " 组回测完成\n";
    if (!results.empty()) {
        const auto& best = results.front();
        std::cout << "  网格最优: ";
        for (const auto& [k, v] : best.params) std::cout << k << "=" << v << " ";
        std::printf(" 目标=%.2f 收益=%.2f%%\n", best.objectiveValue,
                    best.performance.totalReturn);
    }

    AdvisorContext actx;
    actx.strategyId = "MACross";
    actx.results = results;
    actx.objective = Objective::TotalReturn;
    StrategyAdvisor advisor;
    auto sug = advisor.advise(actx);
    std::cout << "  建议: " << sug.text << "\n";
    std::cout << "  理由: " << sug.rationale << "\n";
    std::printf("  置信度=%.2f  过拟合=%s  风险=%s  整体不佳=%s\n", sug.confidence,
                sug.overfitWarning ? "是" : "否", sug.riskWarning ? "是" : "否",
                sug.poorResultWarning ? "是" : "否");
    auto refined = advisor.suggestRefinedRanges(actx);
    if (!refined.empty()) {
        std::cout << "  精化网格: ";
        for (const auto& r : refined) std::cout << r.name << "[" << r.from << ".." << r.to << "] ";
        std::cout << "\n";
    }

    // 5. SentimentAnalyzer — 舆情情绪桩
    std::cout << "\n===== 4. SentimentAnalyzer（关键词打分，无真实新闻源）=====\n";
    SentimentAnalyzer analyzer;
    std::vector<NewsItem> news;
    NewsItem n1;
    n1.title = "贵州茅台半年报业绩增长超预期，净利润创新高";
    NewsItem n2;
    n2.title = "公司发布减持公告，机构下调评级，股价下跌";
    news.push_back(n1);
    news.push_back(n2);
    for (const auto& n : news) {
        auto s = analyzer.analyze(n);
        std::printf("  [%s] score=%.2f  %s\n", s.summary.c_str(), s.score,
                    n.title.c_str());
    }
    auto avg = analyzer.averageScore(news);
    std::printf("  综合: [%s] score=%.2f\n", avg.summary.c_str(), avg.score);

    std::cout << "\n验证完成。单元测试：ctest --preset default（220/220 应全过）\n";
    return 0;
}
