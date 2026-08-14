#include "intelligence/screener/ai_screener.h"

#include "foundation/utils/indicators.h"
#include "intelligence/pattern/pattern_recognizer.h"
#include "intelligence/signal/composite_signal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace st::screener {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// composeSignal 分项 score（-1~+1）→ 0~100
double toPercent(double score) { return (std::clamp(score, -1.0, 1.0) + 1.0) / 2.0 * 100.0; }

/// 取指定分项（缺失 → nullopt）
std::optional<double> componentScore(const st::signal::CompositeSignal& cs,
                                     const char* name) {
    for (const auto& c : cs.components) {
        if (c.name == name) return toPercent(c.score);
    }
    return std::nullopt;
}

}  // namespace

std::vector<AiScore> runAiScreener(
    const std::vector<StockCode>& pool,
    const std::vector<std::vector<Bar>>& barsByCode,
    const std::vector<std::optional<sentiment::SentimentScore>>& sentiments,
    const AiScreenerConfig& cfg) {

    const size_t n = std::min({pool.size(), barsByCode.size(), sentiments.size()});

    std::vector<AiScore> out;
    out.reserve(n);

    for (size_t i = 0; i < n; ++i) {
        const auto& bars = barsByCode[i];
        if (bars.empty()) continue;   // 无数据 → 跳过

        // 技术指标（末值可能 NaN）
        std::vector<double> closes;
        closes.reserve(bars.size());
        for (const auto& b : bars) closes.push_back(b.close);
        const auto rsi = st::indicators::rsi(closes, 14);
        const auto macd = st::indicators::macd(closes);

        // 形态（近 3 根窗口；usePattern=false 时不参与 → 分项缺失折减）
        st::pattern::PatternRecognizer recognizer;
        auto patterns = cfg.usePattern
            ? recognizer.detectAt(st::BarSeries(bars), 3).items
            : std::vector<st::pattern::PatternSignal>{};

        // 情绪（无数据 / 禁用 → 空 summary 触发分项缺失）
        st::sentiment::SentimentScore senti;
        if (cfg.useSentiment && sentiments[i]) senti = *sentiments[i];

        const double close = closes.back();
        const double prevClose = closes.size() >= 2 ? closes[closes.size() - 2] : 0.0;
        const double rsiVal = rsi.empty() ? kNaN : rsi.back();

        const auto cs = st::signal::composeSignal(
            patterns, senti, rsiVal, macd, close, prevClose, cfg.weights);

        AiScore s;
        s.code = pool[i];
        s.compositeScore = toPercent(cs.score);
        s.patternScore = componentScore(cs, "K线形态");
        s.sentimentScore = componentScore(cs, "舆情情绪");
        s.technicalScore = componentScore(cs, "技术指标");
        s.summary = cs.summary;
        out.push_back(std::move(s));
    }

    // 综合分降序（同分保持输入顺序）
    std::stable_sort(out.begin(), out.end(),
                     [](const AiScore& a, const AiScore& b) {
                         return a.compositeScore > b.compositeScore;
                     });
    return out;
}

} // namespace st::screener
