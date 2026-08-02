#pragma once

#include <functional>
#include <vector>

namespace st {

/// 蒙特卡洛模拟 — 日收益有放回重采样，估计期末净值分布
class MonteCarlo {
public:
    /// 随机源：返回 [0, n) 索引，可注入以便测试
    using RandomSource = std::function<size_t(size_t)>;

    struct Input {
        std::vector<double> dailyReturns;   // 原始日收益序列
        int iterations = 1000;              // 模拟次数
        int horizonDays = 0;                // 0 → 用整个序列长度
        double initialEquity = 1.0;         // 初始净值
        RandomSource rng;                   // 可选；默认 mt19937(0x5EED)
    };

    struct Output {
        std::vector<double> finals;         // 全部期末净值（升序）
        double p5 = 0.0;                    // 5% 分位
        double p50 = 0.0;                   // 50% 分位
        double p95 = 0.0;                   // 95% 分位
        double probOfLoss = 0.0;            // 期末净值 < 初始净值 的概率
    };

    static Output simulate(const Input& in);

private:
    static size_t defaultRandomIndex(size_t n);
};

} // namespace st
