#include "intelligence/screener/pattern_factor.h"
#include "intelligence/pattern/pattern_recognizer.h"

#include <algorithm>
#include <optional>

namespace st::screener {

std::optional<double> PatternFactor::calculate(const FactorContext& ctx) const {
    if (!ctx.bars) return std::nullopt;
    const int n = static_cast<int>(ctx.bars->size());
    if (n < minBars_) return std::nullopt;

    st::pattern::PatternRecognizer rec;
    rec.setMinBars(minBars_);
    const auto result = rec.detect(*ctx.bars);

    const int windowStart = n - std::min(lookback_, n);
    int bull = 0;
    int bear = 0;
    double alignTerm = 0.0;
    for (const auto& sig : result.items) {
        if (sig.index < windowStart) continue;
        if (sig.type == st::pattern::PatternType::BullishAlignment) {
            alignTerm = 15.0;
        } else if (sig.type == st::pattern::PatternType::BearishAlignment) {
            alignTerm = -15.0;
        } else if (st::pattern::PatternRecognizer::isBullish(sig.type)) {
            ++bull;
        } else if (st::pattern::PatternRecognizer::isBearish(sig.type)) {
            ++bear;
        }
    }

    const double score = 50.0 + 12.0 * (bull - bear) + alignTerm;
    return std::clamp(score, 0.0, 100.0);
}

} // namespace st::screener
