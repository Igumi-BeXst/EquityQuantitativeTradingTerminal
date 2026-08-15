#pragma once

#include "engine/screener/screener_types.h"
#include <string>
#include <optional>
#include <memory>

namespace st {

/// 因子抽象接口
///
/// 子类实现 calculate() 计算原始因子值，toScore() 将原始值映射到 0~100 分。
/// 不同的因子类别有不同的映射策略（越大越好 / 越小越好 / 中性）。
class IFactor {
public:
    virtual ~IFactor() = default;

    /// 因子名称（唯一标识）
    virtual std::string name() const = 0;

    /// 因子分类
    virtual FactorCategory category() const = 0;

    /// 计算原始因子值（数据不足返回 nullopt）
    virtual std::optional<double> calculate(const FactorContext& ctx) const = 0;

    /// 将原始值映射到 0~100 分
    /// 默认实现: 线性映射到 [0,100]，缺失值给 50 分（中性）
    virtual double toScore(std::optional<double> value) const {
        if (!value.has_value()) return 50.0;
        double v = *value;
        // 简单 clamp 到合理范围
        if (v < 0.0) return 0.0;
        if (v > 100.0) return 100.0;
        return v;
    }
};

/// 便利宏：简化单指标因子定义
#define ST_DECLARE_FACTOR(ClassName, FactorName, FactorCat)        \
    class ClassName : public IFactor {                             \
    public:                                                        \
        std::string name() const override { return FactorName; }   \
        FactorCategory category() const override { return FactorCat; } \
        std::optional<double> calculate(const FactorContext& ctx) const override; \
    };

/// 便利宏：同上，但允许自定义 toScore（原始值 → 0~100 分映射）
#define ST_DECLARE_SCORED_FACTOR(ClassName, FactorName, FactorCat) \
    class ClassName : public IFactor {                             \
    public:                                                        \
        std::string name() const override { return FactorName; }   \
        FactorCategory category() const override { return FactorCat; } \
        std::optional<double> calculate(const FactorContext& ctx) const override; \
        double toScore(std::optional<double> value) const override; \
    };

} // namespace st
