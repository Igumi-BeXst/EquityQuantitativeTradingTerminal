#pragma once

#include "engine/screener/factor.h"

namespace st::factors {

/// Alpha191 因子（国泰君安 191 因子子集）
///
/// 命名 alphaNNN；displayName = "AlphaNNN"；description 为中文含义 + 数据窗口（不展示公式）。
/// 本子集只实现纯日线 OHLCV/AMOUNT 可算的公式（不含横截面 RANK/行业/市值中性类），
/// 其余因子后续按批次补齐。
/// 方向统一按 GTJA 研报口径（公式值越大越偏多），higherIsBetter = true。

#define ST_DECLARE_ALPHA191(ClassName)                                    \
    class ClassName : public IFactor {                                    \
    public:                                                               \
        std::string name() const override;                                \
        std::string displayName() const override;                         \
        std::string description() const override;                         \
        FactorCategory category() const override {                        \
            return FactorCategory::Alpha191;                              \
        }                                                                 \
        std::optional<double> calculate(const FactorContext& ctx) const override; \
    };

// —— 量价结构 / 多空失衡 ——
ST_DECLARE_ALPHA191(Alpha002)  // 多空失衡的日变化
ST_DECLARE_ALPHA191(Alpha003)  // 上攻强度累计
ST_DECLARE_ALPHA191(Alpha004)  // 价格波动突破
ST_DECLARE_ALPHA191(Alpha005)  // 量价结构相关
ST_DECLARE_ALPHA191(Alpha009)  // 中间价资金流
ST_DECLARE_ALPHA191(Alpha011)  // 量价动量
ST_DECLARE_ALPHA191(Alpha013)  // 价格中枢偏离
ST_DECLARE_ALPHA191(Alpha014)  // 5 日动量
ST_DECLARE_ALPHA191(Alpha015)  // 隔夜跳空
ST_DECLARE_ALPHA191(Alpha018)  // 5 日收益
ST_DECLARE_ALPHA191(Alpha020)  // 6 日动量
ST_DECLARE_ALPHA191(Alpha021)  // 价格趋势斜率
ST_DECLARE_ALPHA191(Alpha022)  // 短期量价平滑
ST_DECLARE_ALPHA191(Alpha024)  // 短期量价平滑（5 日）
ST_DECLARE_ALPHA191(Alpha026)  // 长周期量价背离
ST_DECLARE_ALPHA191(Alpha027)  // 双周期动量平滑
ST_DECLARE_ALPHA191(Alpha028)  // KDJ 类超买超卖
ST_DECLARE_ALPHA191(Alpha031)  // 12 日乖离
ST_DECLARE_ALPHA191(Alpha034)  // 均价 / 现价
ST_DECLARE_ALPHA191(Alpha040)  // 上涨量能占比
ST_DECLARE_ALPHA191(Alpha043)  // 6 日净资金流
ST_DECLARE_ALPHA191(Alpha046)  // 多均线比值
ST_DECLARE_ALPHA191(Alpha053)  // 12 日上涨天数占比
ST_DECLARE_ALPHA191(Alpha057)  // KDJ 平滑
ST_DECLARE_ALPHA191(Alpha063)  // RSI(6) 类
ST_DECLARE_ALPHA191(Alpha070)  // 成交额波动（6 日）
ST_DECLARE_ALPHA191(Alpha080)  // 成交量变化率
ST_DECLARE_ALPHA191(Alpha085)  // 量能时间序列位置
ST_DECLARE_ALPHA191(Alpha088)  // 20 日动量
ST_DECLARE_ALPHA191(Alpha103)  // 20 日低点位置
ST_DECLARE_ALPHA191(Alpha110)  // 上攻/下杀力度比
ST_DECLARE_ALPHA191(Alpha118)  // 上下影线比
ST_DECLARE_ALPHA191(Alpha127)  // 12 日回撤平方

} // namespace st::factors
