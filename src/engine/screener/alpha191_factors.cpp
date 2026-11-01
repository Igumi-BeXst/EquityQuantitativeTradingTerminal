#include "engine/screener/alpha191_factors.h"

#include "engine/screener/factor_math.h"

#include <cstddef>
#include <optional>
#include <string>

namespace st::factors {
namespace {

namespace math = ::st::factors::math;
using math::Series;

/// 数据量是否满足因子所需的最小窗口（不满足由 calculate 返回 nullopt）
bool hasEnoughBars(const FactorContext& ctx, std::size_t minBars) {
    return ctx.bars != nullptr && ctx.bars->size() >= minBars;
}

/// ((CLOSE-LOW)-(HIGH-CLOSE))/(HIGH-LOW)：多空失衡系数（>0 收盘偏强）
Series bullBearBalance(const Series& high, const Series& low, const Series& close) {
    const Series closeFromLow = math::sub(close, low);   // C-L
    const Series highToClose = math::sub(high, close);   // H-C
    return math::div(math::sub(closeFromLow, highToClose), math::sub(high, low));
}

/// KDJ 的 RSV：(C-TSMIN(L,n))/(TSMAX(H,n)-TSMIN(L,n))*100
Series kdjRsv(const Series& high, const Series& low, const Series& close, int n) {
    const Series highest = math::tsMax(high, n);
    const Series lowest = math::tsMin(low, n);
    return math::div(math::mulScalar(math::sub(close, lowest), 100.0),
                     math::sub(highest, lowest));
}

}  // namespace

// ============================ 因子元信息 ============================
//
// name()        —— 稳定英文 id（结果表 / 权重映射 / CSV 列名）
// displayName() —— 中文 UI 显示名
// description() —— 中文说明 + 数据窗口（UI tooltip，不展示公式）

#define ST_ALPHA191_META(ClassName, Id, Display, Window, Desc)         \
    std::string ClassName::name() const { return Id; }                 \
    std::string ClassName::displayName() const { return Display; }     \
    std::string ClassName::description() const {                       \
        return std::string(Desc) +                                     \
               "（数据窗口 ≥" + Window + " 根日线）";                    \
    }

// ============================ 量价结构 / 多空失衡 ============================

ST_ALPHA191_META(Alpha002, "alpha002", "Alpha002", "6",
                 "多空失衡度（收盘在当日振幅中的相对位置）的日变化取反，值越大表示多头力量边际增强")

std::optional<double> Alpha002::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series balance = bullBearBalance(high, low, close);
    return math::lastValid(math::mulScalar(math::delta(balance, 1), -1.0));
}

ST_ALPHA191_META(Alpha003, "alpha003", "Alpha003", "7",
                 "近 6 日上攻强度累计——上涨日按「最低价相对昨收」计，下跌日按「最高价相对昨收」计，值越大"
                 "越偏多")

std::optional<double> Alpha003::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 7)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series prevClose = math::delay(close, 1);
    const Series rising = math::egt(close, prevClose);       // C > DELAY(C,1)
    const Series flat = math::eeq(close, prevClose);         // C == DELAY(C,1)
    const Series risePart = math::sub(close, math::emin(low, prevClose));
    const Series fallPart = math::sub(close, math::emax(high, prevClose));
    const Series inner = math::ewhere(rising, risePart, fallPart);
    const Series term = math::ewhere(flat, math::zeros(ctx.bars->size()), inner);
    return math::lastValid(math::tsSum(term, 6));
}

ST_ALPHA191_META(Alpha004, "alpha004", "Alpha004", "20",
                 "短期价格相对 8 日均值上下轨的突破状态——短期急涨取 -1、急跌取 +1，其余按当日量能相"
                 "对 20 日均量强弱取 ±1")

std::optional<double> Alpha004::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const std::size_t n = ctx.bars->size();
    const Series close = math::closeSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series avg8 = math::tsMean(close, 8);
    const Series std8 = math::tsStd(close, 8);
    const Series avg2 = math::tsMean(close, 2);
    const Series meanVolume20 = math::tsMean(volume, 20);
    const Series ones(n, 1.0);
    const Series minusOnes(n, -1.0);
    const Series upperBreak = math::elt(math::add(avg8, std8), avg2);  // avg8+std8 < avg2
    const Series lowerBreak = math::elt(avg2, math::sub(avg8, std8));  // avg2 < avg8-std8
    const Series volumeStrong = math::ege(math::div(volume, meanVolume20), ones);
    const Series rest = math::ewhere(volumeStrong, ones, minusOnes);
    const Series lowerPick = math::ewhere(lowerBreak, ones, rest);
    return math::lastValid(math::ewhere(upperBreak, minusOnes, lowerPick));
}

ST_ALPHA191_META(Alpha005, "alpha005", "Alpha005", "11",
                 "近 3 日「成交量分位—最高价分位」相关系数的最大值取反，衡量量价结构一致性，值越大越偏多")

std::optional<double> Alpha005::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 11)) return std::nullopt;
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series high = math::highSeries(*ctx.bars);
    const Series volumeRank = math::tsRank(volume, 5);
    const Series highRank = math::tsRank(high, 5);
    const Series rollingCorr = math::corr(volumeRank, highRank, 5);
    return math::lastValid(math::mulScalar(math::tsMax(rollingCorr, 3), -1.0));
}

ST_ALPHA191_META(Alpha009, "alpha009", "Alpha009", "10",
                 "以「中间价变化 × 当日振幅 / 成交量」衡量的中间价资金流强度，再做中国式 SMA(7,2"
                 ") 平滑，正值表示买盘主动")

std::optional<double> Alpha009::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 10)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series mid = math::divScalar(math::add(high, low), 2.0);              // (H+L)/2
    const Series prevMid = math::divScalar(
        math::add(math::delay(high, 1), math::delay(low, 1)), 2.0);             // 昨日 (H+L)/2
    const Series priceChange = math::sub(mid, prevMid);
    const Series range = math::sub(high, low);
    const Series flow = math::div(math::mul(priceChange, range), volume);
    return math::lastValid(math::smaCn(flow, 7, 2));
}

ST_ALPHA191_META(Alpha011, "alpha011", "Alpha011", "6",
                 "近 6 日多空失衡系数的成交量加权和，值越大买盘越强")

std::optional<double> Alpha011::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series balance = bullBearBalance(high, low, close);
    return math::lastValid(math::tsSum(math::mul(balance, volume), 6));
}

ST_ALPHA191_META(Alpha013, "alpha013", "Alpha013", "6",
                 "当日价格中枢（高低价几何均值）相对成交均价的偏离，正值表示现价强于当日成交成本")

std::optional<double> Alpha013::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series vwap = math::vwapSeries(*ctx.bars);
    return math::lastValid(math::sub(math::esqrt(math::mul(high, low)), vwap));
}

ST_ALPHA191_META(Alpha014, "alpha014", "Alpha014", "6",
                 "5 日绝对动量，正值表示最近 5 日上涨")

std::optional<double> Alpha014::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    return math::lastValid(math::sub(close, math::delay(close, 5)));
}

ST_ALPHA191_META(Alpha015, "alpha015", "Alpha015", "6",
                 "隔夜跳空幅度（今开相对昨收的收益率），正值表示高开")

std::optional<double> Alpha015::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series open = math::openSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series ratio = math::div(open, math::delay(close, 1));
    return math::lastValid(math::subScalar(ratio, 1.0));
}

ST_ALPHA191_META(Alpha018, "alpha018", "Alpha018", "6",
                 "5 日价格比（未减 1 的动量口径），>1 表示最近 5 日上涨")

std::optional<double> Alpha018::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    return math::lastValid(math::div(close, math::delay(close, 5)));
}

ST_ALPHA191_META(Alpha020, "alpha020", "Alpha020", "7",
                 "6 日动量百分比，正值表示上涨、负值表示下跌")

std::optional<double> Alpha020::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 7)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series prev = math::delay(close, 6);
    const Series change = math::div(math::sub(close, prev), prev);
    return math::lastValid(math::mulScalar(change, 100.0));
}

ST_ALPHA191_META(Alpha021, "alpha021", "Alpha021", "11",
                 "6 日均线对时间序列 {0,1,...,5} 的滚动回归斜率，衡量价格趋势的强度与方向，正值"
                 "表示均线向上")

std::optional<double> Alpha021::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 11)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series mean6 = math::tsMean(close, 6);
    return math::lastValid(math::regBetaSeq(mean6, 6));
}

ST_ALPHA191_META(Alpha022, "alpha022", "Alpha022", "12",
                 "6 日均线乖离率的 3 日变化的平滑值，衡量短期动能的边际变化，正值表示乖离扩大（动能加速）")

std::optional<double> Alpha022::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 12)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series mean6 = math::tsMean(close, 6);
    const Series bias = math::div(math::sub(close, mean6), mean6);
    const Series change = math::sub(bias, math::delay(bias, 3));
    return math::lastValid(math::smaCn(change, 12, 1));
}

ST_ALPHA191_META(Alpha024, "alpha024", "Alpha024", "10",
                 "5 日动量的中国式 SMA(5,1) 平滑，短期量价动量")

std::optional<double> Alpha024::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 10)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series momentum = math::sub(close, math::delay(close, 5));
    return math::lastValid(math::smaCn(momentum, 5, 1));
}

ST_ALPHA191_META(Alpha026, "alpha026", "Alpha026", "235",
                 "7 日均价相对现价的偏离，与「成交均价对 5 日滞后价格」230 日相关系数的合成，衡量长周"
                 "期量价背离")

std::optional<double> Alpha026::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 235)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series vwap = math::vwapSeries(*ctx.bars);
    const Series shortBias = math::sub(math::divScalar(math::tsSum(close, 7), 7.0), close);
    const Series longCorr = math::corr(vwap, math::delay(close, 5), 230);
    return math::lastValid(math::add(shortBias, longCorr));
}

ST_ALPHA191_META(Alpha027, "alpha027", "Alpha027", "18",
                 "3 日与 6 日动量百分比之和的 12 日线性加权均值（WMA），正值表示多周期动量向上")

std::optional<double> Alpha027::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 18)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series prev3 = math::delay(close, 3);
    const Series prev6 = math::delay(close, 6);
    const Series momentum3 = math::mulScalar(math::div(math::sub(close, prev3), prev3), 100.0);
    const Series momentum6 = math::mulScalar(math::div(math::sub(close, prev6), prev6), 100.0);
    return math::lastValid(math::wma(math::add(momentum3, momentum6), 12));
}

ST_ALPHA191_META(Alpha028, "alpha028", "Alpha028", "20",
                 "KDJ 超买超卖指标，>100 超买、<0 超卖")

std::optional<double> Alpha028::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series rsvSeries = kdjRsv(high, low, close, 9);
    const Series k = math::smaCn(rsvSeries, 3, 1);
    const Series d = math::smaCn(k, 3, 1);
    const Series j = math::sub(math::mulScalar(k, 3.0), math::mulScalar(d, 2.0));
    return math::lastValid(j);
}

ST_ALPHA191_META(Alpha031, "alpha031", "Alpha031", "12",
                 "12 日乖离率（BIAS），正值表示现价高于 12 日均价")

std::optional<double> Alpha031::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 12)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series mean12 = math::tsMean(close, 12);
    const Series bias = math::div(math::sub(close, mean12), mean12);
    return math::lastValid(math::mulScalar(bias, 100.0));
}

ST_ALPHA191_META(Alpha034, "alpha034", "Alpha034", "12",
                 "12 日均价与现价之比，<1 表示现价高于均价（短期强势）")

std::optional<double> Alpha034::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 12)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    return math::lastValid(math::div(math::tsMean(close, 12), close));
}

ST_ALPHA191_META(Alpha040, "alpha040", "Alpha040", "26",
                 "近 26 日上涨日成交量与下跌日成交量之比，>100 表示上涨量能占优、<100 表示下跌量"
                 "能占优")

std::optional<double> Alpha040::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 26)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series prevClose = math::delay(close, 1);
    const Series upVolume = math::sumIf(volume, math::egt(close, prevClose), 26);
    const Series downVolume = math::sumIf(volume, math::ele(close, prevClose), 26);
    return math::lastValid(math::mulScalar(math::div(upVolume, downVolume), 100.0));
}

ST_ALPHA191_META(Alpha043, "alpha043", "Alpha043", "6",
                 "近 6 日净资金流（涨日加量、跌日减量），正值表示放量上涨、资金净流入")

std::optional<double> Alpha043::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const std::size_t n = ctx.bars->size();
    const Series close = math::closeSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series prevClose = math::delay(close, 1);
    const Series negativeVolume = math::mulScalar(volume, -1.0);
    const Series fallPart = math::ewhere(math::elt(close, prevClose), negativeVolume,
                                         math::zeros(n));
    const Series term = math::ewhere(math::egt(close, prevClose), volume, fallPart);
    return math::lastValid(math::tsSum(term, 6));
}

ST_ALPHA191_META(Alpha046, "alpha046", "Alpha046", "24",
                 "3/6/12/24 日均值之和相对 4 倍现价的比值，<1 表示现价站上全部均线（均线多头排"
                 "列强度）")

std::optional<double> Alpha046::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 24)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series sum = math::add(
        math::add(math::tsMean(close, 3), math::tsMean(close, 6)),
        math::add(math::tsMean(close, 12), math::tsMean(close, 24)));
    return math::lastValid(math::div(sum, math::mulScalar(close, 4.0)));
}

ST_ALPHA191_META(Alpha053, "alpha053", "Alpha053", "13",
                 "近 12 日上涨天数占比（%），越高表示上涨的持续性越强")

std::optional<double> Alpha053::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 13)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series rising = math::egt(close, math::delay(close, 1));
    const Series upDays = math::count(rising, 12);
    return math::lastValid(math::mulScalar(math::divScalar(upDays, 12.0), 100.0));
}

ST_ALPHA191_META(Alpha057, "alpha057", "Alpha057", "20",
                 "KDJ 的 K 值，衡量收盘价在近 9 日高低区间中的位置，越高越接近区间顶部（超买）")

std::optional<double> Alpha057::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    return math::lastValid(math::smaCn(kdjRsv(high, low, close, 9), 3, 1));
}

ST_ALPHA191_META(Alpha063, "alpha063", "Alpha063", "10",
                 "RSI(6) 类相对强弱指标，>50 表示上涨动能占优、<50 表示下跌动能占优")

std::optional<double> Alpha063::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 10)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series change = math::sub(close, math::delay(close, 1));
    const Series gain = math::smaCn(math::emaxScalar(change, 0.0), 6, 1);
    const Series swing = math::smaCn(math::eabs(change), 6, 1);
    return math::lastValid(math::mulScalar(math::div(gain, swing), 100.0));
}

ST_ALPHA191_META(Alpha070, "alpha070", "Alpha070", "6",
                 "近 6 日成交额标准差，衡量资金活跃度的波动（值越大分歧越大）；AMOUNT 缺失（数据源未"
                 "提供）视为数据不足")

std::optional<double> Alpha070::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series amount = math::amountSeries(*ctx.bars);
    const std::size_t n = amount.size();
    bool hasAmount = false;
    for (std::size_t i = n - 6; i < n; ++i) {
        if (amount[i] > 0.0) {
            hasAmount = true;
            break;
        }
    }
    if (!hasAmount) return std::nullopt;  // 成交额缺失时不把「全 0」误当成零波动
    return math::lastValid(math::tsStd(amount, 6));
}

ST_ALPHA191_META(Alpha080, "alpha080", "Alpha080", "6",
                 "成交量 5 日变化率（%），正值表示放量、负值表示缩量")

std::optional<double> Alpha080::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 6)) return std::nullopt;
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series prev = math::delay(volume, 5);
    const Series change = math::div(math::sub(volume, prev), prev);
    return math::lastValid(math::mulScalar(change, 100.0));
}

ST_ALPHA191_META(Alpha085, "alpha085", "Alpha085", "39",
                 "量能相对水平的时间序列分位 × 价格 7 日变动反向分位，衡量量价配合的极值状态（[0,1]"
                 "，越大表示放量且价格短期回调）")

std::optional<double> Alpha085::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 39)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series volume = math::volumeSeries(*ctx.bars);
    const Series volumeRatio = math::div(volume, math::tsMean(volume, 20));
    const Series volumeRank = math::tsRank(volumeRatio, 20);
    const Series fallRank = math::tsRank(math::mulScalar(math::delta(close, 7), -1.0), 8);
    return math::lastValid(math::mul(volumeRank, fallRank));
}

ST_ALPHA191_META(Alpha088, "alpha088", "Alpha088", "21",
                 "20 日动量百分比，正值表示中期上涨、负值表示中期下跌")

std::optional<double> Alpha088::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 21)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series prev = math::delay(close, 20);
    const Series change = math::div(math::sub(close, prev), prev);
    return math::lastValid(math::mulScalar(change, 100.0));
}

ST_ALPHA191_META(Alpha103, "alpha103", "Alpha103", "20",
                 "20 日最低点距今天数的占比，值越大表示最低点越久远、上行动能越持续（100 表示当日仍创新"
                 "低）")

std::optional<double> Alpha103::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const std::size_t n = ctx.bars->size();
    const Series low = math::lowSeries(*ctx.bars);
    const Series lowDays = math::lowDay(low, 20);
    const Series window(n, 20.0);
    const Series position = math::div(math::sub(window, lowDays), window);
    return math::lastValid(math::mulScalar(position, 100.0));
}

ST_ALPHA191_META(Alpha110, "alpha110", "Alpha110", "20",
                 "近 20 日上攻力度（最高价高于昨收部分之和）与下杀力度（昨收低于最低价部分之和）之比，>1"
                 "00 表示上攻占优")

std::optional<double> Alpha110::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series close = math::closeSeries(*ctx.bars);
    const Series prevClose = math::delay(close, 1);
    const Series upForce = math::emaxScalar(math::sub(high, prevClose), 0.0);
    const Series downForce = math::emaxScalar(math::sub(prevClose, low), 0.0);
    const Series upSum = math::tsSum(upForce, 20);
    const Series downSum = math::tsSum(downForce, 20);
    return math::lastValid(math::mulScalar(math::div(upSum, downSum), 100.0));
}

ST_ALPHA191_META(Alpha118, "alpha118", "Alpha118", "20",
                 "近 20 日上影线总和与下影线总和之比，>100 表示盘中冲高力度强于回落、<100 表示回"
                 "落力量更大")

std::optional<double> Alpha118::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 20)) return std::nullopt;
    const Series open = math::openSeries(*ctx.bars);
    const Series high = math::highSeries(*ctx.bars);
    const Series low = math::lowSeries(*ctx.bars);
    const Series upSum = math::tsSum(math::sub(high, open), 20);
    const Series downSum = math::tsSum(math::sub(open, low), 20);
    return math::lastValid(math::mulScalar(math::div(upSum, downSum), 100.0));
}

ST_ALPHA191_META(Alpha127, "alpha127", "Alpha127", "24",
                 "近 12 日相对 12 日滚动最高价回撤的均方根（%），衡量下行波动/回撤风险，值越大回撤越"
                 "剧烈")

std::optional<double> Alpha127::calculate(const FactorContext& ctx) const {
    if (!hasEnoughBars(ctx, 24)) return std::nullopt;
    const Series close = math::closeSeries(*ctx.bars);
    const Series highest = math::tsMax(close, 12);
    const Series drawdown = math::mulScalar(math::div(math::sub(close, highest), highest), 100.0);
    const Series squared = math::mul(drawdown, drawdown);
    return math::lastValid(math::esqrt(math::tsMean(squared, 12)));
}

#undef ST_ALPHA191_META

}  // namespace st::factors
