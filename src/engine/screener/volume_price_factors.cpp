#include "engine/screener/volume_price_factors.h"

#include "engine/screener/factor_math.h"

#include <cstddef>
#include <optional>

namespace st::factors {

namespace {

// 各因子回看窗口（交易日）
constexpr int kVolumeRatioWindow = 5;   // 量比：前 5 日均量
constexpr int kAmountWindow = 20;       // 成交额动量 / OBV / 量能突破 / Amihud
constexpr int kMfiPeriod = 14;          // MFI 周期
constexpr int kCorrWindow = 10;         // 量价相关
constexpr int kEfiWindow = 13;          // EFI 均值窗口
constexpr std::size_t kAmihudMinSamples = 10;  // Amihud 最少有效样本数

/// 前置检查：K 线序列存在且长度足够（不足 → 因子缺失）
bool hasBars(const FactorContext& ctx, std::size_t minBars) {
    return ctx.bars != nullptr && ctx.bars->size() >= minBars;
}

}  // namespace

std::optional<double> VpVolumeRatio::calculate(const FactorContext& ctx) const {
    // 量比 = 当日成交量 / 前 5 日均量（不含当日）
    // delay(tsMean(volume,5),1)[i] 即 volume[i-5..i-1] 的均值
    constexpr int kMinBars = kVolumeRatioWindow + 1;
    if (!hasBars(ctx, static_cast<std::size_t>(kMinBars))) return std::nullopt;

    const math::Series volume = math::volumeSeries(*ctx.bars);
    const math::Series prevAvg = math::delay(math::tsMean(volume, kVolumeRatioWindow), 1);
    return math::lastValid(math::div(volume, prevAvg));
}

std::optional<double> VpTurnover::calculate(const FactorContext& ctx) const {
    // 优先基本面快照的换手率(%)；缺失则回退 K 线换手率（Bar 内为 0~1 小数）
    if (ctx.quote != nullptr && ctx.quote->valid && ctx.quote->turnoverRate > 0.0) {
        return ctx.quote->turnoverRate;
    }
    if (ctx.bars == nullptr || ctx.bars->empty()) return std::nullopt;

    const double ratePercent = ctx.bars->current().turnoverRate * 100.0;
    if (!(ratePercent > 0.0)) return std::nullopt;  // 换手率为 0 → 数据缺失
    return ratePercent;
}

std::optional<double> VpVwapBias::calculate(const FactorContext& ctx) const {
    // VWAP 乖离 = (C - VWAP) / VWAP × 100；VWAP 缺失时 vwapSeries 已回退典型价
    if (!hasBars(ctx, 1)) return std::nullopt;

    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series vwap = math::vwapSeries(*ctx.bars);
    const math::Series bias =
        math::mulScalar(math::div(math::sub(close, vwap), vwap), 100.0);
    return math::lastValid(bias);
}

std::optional<double> VpAmountMomentum::calculate(const FactorContext& ctx) const {
    // 成交额动量 = 当日成交额 / 20 日均成交额（含当日） - 1
    if (!hasBars(ctx, static_cast<std::size_t>(kAmountWindow))) return std::nullopt;

    const math::Series amount = math::amountSeries(*ctx.bars);
    const math::Series mean = math::tsMean(amount, kAmountWindow);
    const math::Series momentum = math::subScalar(math::div(amount, mean), 1.0);
    return math::lastValid(momentum);
}

std::optional<double> VpObvSlope::calculate(const FactorContext& ctx) const {
    // OBV 增量：涨加量、跌减量、平盘不变；斜率 = 近 20 日 OBV 变化 / 20 日均量
    // 近 20 日 OBV 变化 = 近 20 根带符号成交量之和，需 21 根 K 线（首根无昨收）
    constexpr int kMinBars = kAmountWindow + 1;
    if (!hasBars(ctx, static_cast<std::size_t>(kMinBars))) return std::nullopt;

    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series volume = math::volumeSeries(*ctx.bars);
    const math::Series signedVolume =
        math::mul(math::esign(math::delta(close, 1)), volume);
    const math::Series obvChange = math::tsSum(signedVolume, kAmountWindow);
    const math::Series avgVolume = math::tsMean(volume, kAmountWindow);
    return math::lastValid(math::div(obvChange, avgVolume));
}

std::optional<double> VpMfi::calculate(const FactorContext& ctx) const {
    // MFI(14)：典型价上涨日资金流为正、下跌日为负，MFI = 100 - 100/(1 + 正/负)
    constexpr int kMinBars = kMfiPeriod + 1;  // 14 个涨跌差分需要 15 根 K 线
    if (!hasBars(ctx, static_cast<std::size_t>(kMinBars))) return std::nullopt;

    const math::Series high = math::highSeries(*ctx.bars);
    const math::Series low = math::lowSeries(*ctx.bars);
    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series volume = math::volumeSeries(*ctx.bars);

    // 典型价 (H + L + C) / 3
    const math::Series typical =
        math::divScalar(math::add(math::add(high, low), close), 3.0);
    const math::Series moneyFlow = math::mul(typical, volume);
    const math::Series typicalDelta = math::delta(typical, 1);
    const math::Series zero = math::zeros(ctx.bars->size());

    const math::Series posFlow =
        math::sumIf(moneyFlow, math::egt(typicalDelta, zero), kMfiPeriod);
    const math::Series negFlow =
        math::sumIf(moneyFlow, math::elt(typicalDelta, zero), kMfiPeriod);

    const auto pos = math::lastValid(posFlow);
    const auto neg = math::lastValid(negFlow);
    if (!pos.has_value() || !neg.has_value()) return std::nullopt;

    if (*pos + *neg <= 0.0) return std::nullopt;  // 无有效资金流
    if (*neg == 0.0) return 100.0;                // 只有流入 → 超买极值
    return 100.0 - 100.0 / (1.0 + *pos / *neg);
}

std::optional<double> VpPriceVolumeCorr::calculate(const FactorContext& ctx) const {
    // 量价相关 = corr(close, volume, 10) 的最近值
    if (!hasBars(ctx, static_cast<std::size_t>(kCorrWindow))) return std::nullopt;

    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series volume = math::volumeSeries(*ctx.bars);
    return math::lastValid(math::corr(close, volume, kCorrWindow));
}

std::optional<double> VpVolumeBreakout::calculate(const FactorContext& ctx) const {
    // 量能突破 = 当日成交量 / 20 日最大成交量（窗口含当日）
    if (!hasBars(ctx, static_cast<std::size_t>(kAmountWindow))) return std::nullopt;

    const math::Series volume = math::volumeSeries(*ctx.bars);
    const math::Series maxVolume = math::tsMax(volume, kAmountWindow);
    return math::lastValid(math::div(volume, maxVolume));
}

std::optional<double> VpAmihud::calculate(const FactorContext& ctx) const {
    // Amihud 非流动性 = 近 20 日 mean(|日收益| / 成交额(亿元))，成交额<=0 的 bar 跳过
    // |日收益| 需要昨收 → 20 个收益样本需要 21 根 K 线
    constexpr int kMinBars = kAmountWindow + 1;
    if (!hasBars(ctx, static_cast<std::size_t>(kMinBars))) return std::nullopt;

    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series amount = math::amountSeries(*ctx.bars);
    const math::Series returns = math::returnsSeries(close);

    // 单位换算：成交额（元）→ 亿元
    const math::Series amountYi = math::divScalar(amount, 1e8);
    const math::Series illiqRaw = math::div(math::eabs(returns), amountYi);

    // 有效样本 = 成交额 > 0 且日收益有限；无效处置 0，用 count() 单独统计样本数
    const math::Series zero = math::zeros(ctx.bars->size());
    const math::Series finiteReturns =
        math::esign(math::addScalar(math::eabs(returns), 1.0));  // 有限→1，NaN→NaN
    const math::Series valid =
        math::mul(math::egt(amount, zero), finiteReturns);
    const math::Series illiq = math::ewhere(valid, illiqRaw, zero);

    const math::Series sumIlliq = math::tsSum(illiq, kAmountWindow);
    const math::Series sampleCount = math::count(valid, kAmountWindow);

    const auto count = math::lastValid(sampleCount);
    if (!count.has_value() || *count < static_cast<double>(kAmihudMinSamples)) {
        return std::nullopt;
    }
    const auto sum = math::lastValid(sumIlliq);
    if (!sum.has_value()) return std::nullopt;
    return *sum / *count;
}

std::optional<double> VpEfi::calculate(const FactorContext& ctx) const {
    // Elder 力度指数 = 近 13 日 mean((C - 昨收) × 成交量)
    constexpr int kMinBars = kEfiWindow + 1;  // 13 个差分需要 14 根 K 线
    if (!hasBars(ctx, static_cast<std::size_t>(kMinBars))) return std::nullopt;

    const math::Series close = math::closeSeries(*ctx.bars);
    const math::Series volume = math::volumeSeries(*ctx.bars);
    const math::Series force = math::mul(math::delta(close, 1), volume);
    return math::lastValid(math::tsMean(force, kEfiWindow));
}

}  // namespace st::factors
