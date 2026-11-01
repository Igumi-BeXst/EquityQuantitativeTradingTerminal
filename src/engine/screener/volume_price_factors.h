#pragma once

#include "engine/screener/factor.h"

namespace st::factors {

/// 量价因子（成交量/成交额与价格关系）
///
/// 命名 vp_*；displayName 为中文短名；description 说明公式与方向。
#define ST_DECLARE_VP(ClassName, FactorId, Display, Desc, Higher)   \
    class ClassName : public IFactor {                              \
    public:                                                         \
        std::string name() const override { return FactorId; }      \
        std::string displayName() const override { return Display; }\
        std::string description() const override { return Desc; }   \
        FactorCategory category() const override {                  \
            return FactorCategory::VolumePrice;                     \
        }                                                           \
        bool higherIsBetter() const override { return Higher; }     \
        std::optional<double> calculate(const FactorContext& ctx) const override; \
    };

ST_DECLARE_VP(VpVolumeRatio, "vp_volume_ratio", "量比",
              "当日成交量 / 前 5 日均量；放量程度（越大越活跃）", true)
ST_DECLARE_VP(VpTurnover, "vp_turnover", "换手率",
              "当日换手率 %（优先基本面快照，缺失回退 K 线换手率）；低换手溢价", false)
ST_DECLARE_VP(VpVwapBias, "vp_vwap_bias", "VWAP乖离",
              "(C - VWAP) / VWAP × 100；现价相对当日成交均价的偏离，正值偏强", true)
ST_DECLARE_VP(VpAmountMomentum, "vp_amount_momentum", "成交额动量",
              "当日成交额 / 20 日均成交额 - 1；资金关注度变化", true)
ST_DECLARE_VP(VpObvSlope, "vp_obv_slope", "OBV斜率",
              "OBV 近 20 日变化 / 20 日均量；能量潮趋势强度", true)
ST_DECLARE_VP(VpMfi, "vp_mfi", "MFI(14)",
              "资金流量指标 0~100；资金流入强度（>80 超买、<20 超卖）", true)
ST_DECLARE_VP(VpPriceVolumeCorr, "vp_price_volume_corr", "量价相关",
              "近 10 日收盘价与成交量的相关系数；量价同向程度", true)
ST_DECLARE_VP(VpVolumeBreakout, "vp_volume_breakout", "量能突破",
              "当日成交量 / 20 日最大成交量；接近 1 表示放量突破", true)
ST_DECLARE_VP(VpAmihud, "vp_amihud", "非流动性",
              "Amihud 非流动性：近 20 日 mean(|日收益| / 成交额(亿))；非流动性溢价", true)
ST_DECLARE_VP(VpEfi, "vp_efi", "力度指数",
              "Elder 力度指数：近 13 日 mean((C - 昨收) × 成交量)，多空力量", true)

} // namespace st::factors
