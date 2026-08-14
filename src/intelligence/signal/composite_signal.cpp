#include "intelligence/signal/composite_signal.h"

#include "intelligence/pattern/pattern_recognizer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace st::signal {

namespace {

constexpr double kStrongThreshold = 0.5;
constexpr double kNormalThreshold = 0.2;
constexpr double kRsiOversold = 30.0;
constexpr double kRsiOverbought = 70.0;
constexpr double kMomentumFullScale = 0.03;  // 单日 ±3% 涨跌 = 动量满分

double clamp1(double v) { return std::clamp(v, -1.0, 1.0); }

/// 技术子分：RSI（超卖正 / 超买负 / 中部线性）
std::optional<double> rsiScore(double rsi) {
    if (!std::isfinite(rsi)) return std::nullopt;
    if (rsi < kRsiOversold) return 1.0;
    if (rsi > kRsiOverbought) return -1.0;
    return (50.0 - rsi) / 20.0 * 0.5;
}

/// 技术子分：MACD（金叉多头 / 死叉空头 / 粘合零）
std::optional<double> macdScore(const indicators::MacdResult& macd) {
    if (macd.dif.empty() || macd.dea.empty() || macd.hist.empty()) return std::nullopt;
    const double dif = macd.dif.back();
    const double dea = macd.dea.back();
    const double hist = macd.hist.back();
    if (!std::isfinite(dif) || !std::isfinite(dea) || !std::isfinite(hist)) {
        return std::nullopt;
    }
    if (dif > dea && hist > 0.0) return 0.5;
    if (dif < dea && hist < 0.0) return -0.5;
    return 0.0;
}

/// 技术子分：单日动量（±3% = 满分）
std::optional<double> momentumScore(double close, double prevClose) {
    if (!(close > 0.0) || !(prevClose > 0.0)) return std::nullopt;
    return clamp1((close - prevClose) / prevClose / kMomentumFullScale);
}

}  // namespace

std::string ratingName(SignalRating rating) {
    switch (rating) {
        case SignalRating::StrongBuy:  return "强烈买入";
        case SignalRating::Buy:        return "买入";
        case SignalRating::Neutral:    return "观望";
        case SignalRating::Sell:       return "卖出";
        case SignalRating::StrongSell: return "强烈卖出";
    }
    return "观望";
}

CompositeSignal composeSignal(
    const std::vector<pattern::PatternSignal>& patterns,
    const sentiment::SentimentScore& sentiment,
    double rsi,
    const indicators::MacdResult& macd,
    double close, double prevClose,
    const std::vector<double>& weights) {

    std::vector<double> w = weights;
    if (w.size() != 3) w = {0.4, 0.3, 0.3};
    const double wSum = w[0] + w[1] + w[2];
    if (!(wSum > 0.0)) w = {0.4, 0.3, 0.3};

    CompositeSignal out;
    std::vector<SignalComponent> present;

    // 1. 形态分项：多形态取 |direction×confidence| 最大者（避免叠加重复计数）
    if (!patterns.empty()) {
        SignalComponent c;
        c.name = "K线形态";
        c.weight = w[0];
        double best = 0.0;
        std::string names;
        for (const auto& p : patterns) {
            if (!names.empty()) names += "、";
            names += p.name.empty()
                ? pattern::PatternRecognizer::typeName(p.type) : p.name;
            const double dir = pattern::PatternRecognizer::isBullish(p.type) ? 1.0
                             : pattern::PatternRecognizer::isBearish(p.type) ? -1.0
                             : 0.0;
            if (dir == 0.0) continue;   // 十字星中性
            const double contrib = dir * std::clamp(p.confidence, 0.0, 1.0);
            if (std::abs(contrib) > std::abs(best)) best = contrib;
        }
        c.score = best;
        c.detail = names;
        present.push_back(std::move(c));
    }

    // 2. 情绪分项：summary 空 = 无新闻 → 缺失
    if (!sentiment.summary.empty()) {
        SignalComponent c;
        c.name = "舆情情绪";
        c.weight = w[1];
        c.score = clamp1(sentiment.score);
        c.detail = sentiment.summary;
        present.push_back(std::move(c));
    }

    // 3. 技术分项：RSI / MACD / 动量 子分平均
    {
        const auto rs = rsiScore(rsi);
        const auto ms = macdScore(macd);
        const auto mom = momentumScore(close, prevClose);
        if (rs || ms || mom) {
            SignalComponent c;
            c.name = "技术指标";
            c.weight = w[2];
            double sum = 0.0;
            int n = 0;
            if (rs) { sum += *rs; ++n; }
            if (ms) { sum += *ms; ++n; }
            if (mom) { sum += *mom; ++n; }
            c.score = clamp1(sum / static_cast<double>(n));
            std::ostringstream oss;
            oss << "RSI "
                << (rs ? std::to_string(static_cast<int>(std::round(rsi))) : "—")
                << " · MACD "
                << (ms ? (*ms > 0.0 ? "金叉" : *ms < 0.0 ? "死叉" : "粘合") : "—");
            if (mom) {
                const double pct = (close - prevClose) / prevClose * 100.0;
                oss << " · " << (pct >= 0.0 ? "+" : "")
                    << std::fixed << std::setprecision(1) << pct << "%";
            }
            c.detail = oss.str();
            present.push_back(std::move(c));
        }
    }

    if (present.empty()) {
        out.summary = "综合信号：观望（数据不足）";
        return out;
    }

    // 综合分（缺失分项权重折减）+ 置信度 = 覆盖度 × 一致度
    const double wAll = w[0] + w[1] + w[2];
    double scoreSum = 0.0;
    double weightSum = 0.0;
    double coverageSum = 0.0;
    double minS = 1.0, maxS = -1.0;
    for (const auto& c : present) {
        scoreSum += c.score * c.weight;
        weightSum += c.weight;
        coverageSum += c.weight;
        minS = std::min(minS, c.score);
        maxS = std::max(maxS, c.score);
    }
    out.score = clamp1(scoreSum / weightSum);
    const double coverage = coverageSum / wAll;
    const double agreement = 1.0 - (maxS - minS) / 2.0;
    out.confidence = std::clamp(coverage * agreement, 0.0, 1.0);

    if (out.score >= kStrongThreshold) out.rating = SignalRating::StrongBuy;
    else if (out.score >= kNormalThreshold) out.rating = SignalRating::Buy;
    else if (out.score <= -kStrongThreshold) out.rating = SignalRating::StrongSell;
    else if (out.score <= -kNormalThreshold) out.rating = SignalRating::Sell;
    else out.rating = SignalRating::Neutral;

    // 摘要：驱动分项按 |score| 降序（弱分项不列）
    std::vector<const SignalComponent*> sorted;
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
        drivers += (c->score > 0.0 ? "看涨" : "看跌");
    }

    std::ostringstream ss;
    ss << "综合信号：" << ratingName(out.rating)
       << "（" << (out.score >= 0.0 ? "+" : "")
       << std::fixed << std::setprecision(2) << out.score
       << "）—— " << (drivers.empty() ? "信号平淡" : drivers);
    out.summary = ss.str();

    out.components = std::move(present);
    return out;
}

} // namespace st::signal
