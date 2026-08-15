#pragma once

#include "engine/screener/factor.h"
#include "foundation/bar.h"
#include <memory>
#include <vector>
#include <string>

namespace st {

/// 内置因子集（核心 24 个）
///
/// 使用 TA-Lib 计算技术指标，分类：
/// - 动量: ROC / RSI / MACD柱 / CCI / 威廉%R / 乖离率 / 连涨天数
/// - 波动: 年化波动率 / ATR / 最大回撤 / 布林带位置 / 振幅
/// - 质量: 均线多头排列 / 趋势强度(ADX) / MA5-MA20 金叉 / 52周价格位置 / MA20 斜率
/// - 量价: 量比 / 换手率 / OBV / MFI / 量价配合
/// - 估值: 市盈率TTM / 总市值（依赖 QuoteFundamentals 快照，需注入 ctx.quote）
namespace factors {

// --- 动量类 ---
ST_DECLARE_FACTOR(RocFactor, "roc_20", FactorCategory::Momentum);       // 20日动量
ST_DECLARE_FACTOR(RsiFactor, "rsi_14", FactorCategory::Momentum);       // RSI(14)
ST_DECLARE_FACTOR(MacdHistFactor, "macd_hist", FactorCategory::Momentum); // MACD柱
ST_DECLARE_SCORED_FACTOR(CciFactor, "cci_14", FactorCategory::Momentum); // CCI(14) 顺势指标
ST_DECLARE_SCORED_FACTOR(WilliamsRFactor, "williams_r", FactorCategory::Momentum); // 威廉%R(14)
ST_DECLARE_SCORED_FACTOR(BiasFactor, "bias_6", FactorCategory::Momentum); // 乖离率 BIAS(6)
ST_DECLARE_SCORED_FACTOR(UpStreakFactor, "up_streak", FactorCategory::Momentum); // 连涨天数

// --- 波动类 ---
ST_DECLARE_FACTOR(VolatilityFactor, "volatility", FactorCategory::Volatility); // 年化波动率
ST_DECLARE_FACTOR(AtRFactor, "atr_14", FactorCategory::Volatility);     // ATR(14)
ST_DECLARE_FACTOR(MaxDrawdownFactor, "max_drawdown", FactorCategory::Volatility); // 最大回撤
ST_DECLARE_FACTOR(BollPosFactor, "boll_pos", FactorCategory::Volatility); // 布林带位置
ST_DECLARE_FACTOR(AmplitudeFactor, "amplitude_20", FactorCategory::Volatility); // 20日均振幅

// --- 质量类 ---
ST_DECLARE_FACTOR(MAAlignmentFactor, "ma_alignment", FactorCategory::Quality); // 均线多头排列
ST_DECLARE_FACTOR(AdxFactor, "adx_14", FactorCategory::Quality);        // ADX(14)
ST_DECLARE_FACTOR(MaCrossFactor, "ma_cross", FactorCategory::Quality);  // MA5/MA20 金叉状态
ST_DECLARE_FACTOR(PricePosFactor, "price_pos_52w", FactorCategory::Quality); // 52周价格位置
ST_DECLARE_SCORED_FACTOR(MaSlopeFactor, "ma20_slope", FactorCategory::Quality); // MA20 斜率

// --- 量价类 ---
ST_DECLARE_FACTOR(VolumeRatioFactor, "volume_ratio", FactorCategory::VolumePrice); // 量比
ST_DECLARE_FACTOR(TurnoverFactor, "turnover", FactorCategory::VolumePrice); // 换手率
ST_DECLARE_FACTOR(ObvFactor, "obv", FactorCategory::VolumePrice);       // OBV
ST_DECLARE_FACTOR(MfiFactor, "mfi_14", FactorCategory::VolumePrice);    // MFI(14) 资金流量
ST_DECLARE_FACTOR(VolPriceFactor, "vol_price", FactorCategory::VolumePrice); // 量价配合

// --- 估值类（需 ctx.quote 非空）---
ST_DECLARE_SCORED_FACTOR(PeTtmFactor, "pe_ttm", FactorCategory::Valuation); // 市盈率 TTM（低估值高分）
ST_DECLARE_SCORED_FACTOR(MarketCapFactor, "market_cap", FactorCategory::Valuation); // 总市值（小市值偏好）

/// 创建默认因子集合（名称 → 权重）
std::vector<std::pair<std::shared_ptr<IFactor>, double>> defaultFactorSet();

} // namespace factors

} // namespace st
