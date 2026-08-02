#include "engine/analyzer/monte_carlo.h"
#include <algorithm>
#include <random>

namespace st {

size_t MonteCarlo::defaultRandomIndex(size_t n) {
    static thread_local std::mt19937 gen(0x5EED);
    std::uniform_int_distribution<size_t> dist(0, n - 1);
    return dist(gen);
}

MonteCarlo::Output MonteCarlo::simulate(const Input& in) {
    Output out;
    if (in.dailyReturns.empty() || in.iterations <= 0) return out;

    const size_t n = in.horizonDays > 0
        ? static_cast<size_t>(in.horizonDays) : in.dailyReturns.size();
    const RandomSource rng = in.rng ? in.rng : &defaultRandomIndex;

    out.finals.reserve(static_cast<size_t>(in.iterations));
    for (int it = 0; it < in.iterations; ++it) {
        double eq = in.initialEquity;
        for (size_t i = 0; i < n; ++i) {
            eq *= 1.0 + in.dailyReturns[rng(in.dailyReturns.size())];
        }
        out.finals.push_back(eq);
    }

    std::sort(out.finals.begin(), out.finals.end());
    const size_t N = out.finals.size();
    out.p5 = out.finals[static_cast<size_t>(0.05 * static_cast<double>(N - 1))];
    out.p50 = out.finals[static_cast<size_t>(0.50 * static_cast<double>(N - 1))];
    out.p95 = out.finals[static_cast<size_t>(0.95 * static_cast<double>(N - 1))];

    size_t losses = 0;
    for (double f : out.finals) {
        if (f < in.initialEquity) ++losses;
    }
    out.probOfLoss = static_cast<double>(losses) / static_cast<double>(N);
    return out;
}

} // namespace st
