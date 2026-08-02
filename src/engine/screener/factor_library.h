#pragma once

#include "engine/screener/factor.h"
#include "foundation/bar.h"
#include <memory>
#include <vector>
#include <string>

namespace st {

/// 内置因子集（核心 15 个）
///
/// 使用 TA-Lib 计算技术指标，分类：
/// - 动量: ROC / RSI / MACD柱
/// - 波动: 年化波动率 / ATR / 最大回撤
/// - 质量: 均线多头排列 / 趋势强度(ADX)
/// - 量价: 量比 / 换手率 / OBV
namespace factors {

// --- 动量类 ---
ST_DECLARE_FACTOR(RocFactor, "roc_20", FactorCategory::Momentum);       // 20日动量
ST_DECLARE_FACTOR(RsiFactor, "rsi_14", FactorCategory::Momentum);       // RSI(14)
ST_DECLARE_FACTOR(MacdHistFactor, "macd_hist", FactorCategory::Momentum); // MACD柱

// --- 波动类 ---
ST_DECLARE_FACTOR(VolatilityFactor, "volatility", FactorCategory::Volatility); // 年化波动率
ST_DECLARE_FACTOR(AtRFactor, "atr_14", FactorCategory::Volatility);     // ATR(14)
ST_DECLARE_FACTOR(MaxDrawdownFactor, "max_drawdown", FactorCategory::Volatility); // 最大回撤

// --- 质量类 ---
ST_DECLARE_FACTOR(MAAlignmentFactor, "ma_alignment", FactorCategory::Quality); // 均线多头排列
ST_DECLARE_FACTOR(AdxFactor, "adx_14", FactorCategory::Quality);        // ADX(14)

// --- 量价类 ---
ST_DECLARE_FACTOR(VolumeRatioFactor, "volume_ratio", FactorCategory::VolumePrice); // 量比
ST_DECLARE_FACTOR(TurnoverFactor, "turnover", FactorCategory::VolumePrice); // 换手率
ST_DECLARE_FACTOR(ObvFactor, "obv", FactorCategory::VolumePrice);       // OBV

/// 创建默认因子集合（名称 → 权重）
std::vector<std::pair<std::shared_ptr<IFactor>, double>> defaultFactorSet();

} // namespace factors

} // namespace st
