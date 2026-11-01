#pragma once

#include "engine/screener/screener_types.h"
#include <memory>
#include <optional>
#include <string>

namespace st {

/// 因子抽象接口
///
/// 因子只负责计算「原始因子值」，不做 0~100 映射：
/// 选股引擎在 run() 内对同一个因子做全池截面标准化（去极值 → z-score → 百分位），
/// 因此不同量纲的因子（Alpha191 / 量价 / 基本面）可以统一打分与加权。
class IFactor {
public:
    virtual ~IFactor() = default;

    /// 因子唯一英文 id（用于结果表、CSV、权重映射；须稳定）
    virtual std::string name() const = 0;

    /// 中文显示名（选股面板勾选项、结果表列头）
    virtual std::string displayName() const = 0;

    /// 因子说明（UI tooltip：公式/数据口径/方向）
    virtual std::string description() const { return {}; }

    /// 因子分类
    virtual FactorCategory category() const = 0;

    /// 因子方向：true = 原始值越大越好；false = 越小越好
    /// 截面标准化时按方向取符号（score = Φ(direction × z) × 100）
    virtual bool higherIsBetter() const { return true; }

    /// 计算原始因子值（数据不足返回 nullopt）
    virtual std::optional<double> calculate(const FactorContext& ctx) const = 0;
};

} // namespace st
