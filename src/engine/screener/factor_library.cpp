#include "engine/screener/factor_library.h"

#include "engine/screener/alpha191_factors.h"
#include "engine/screener/fundamental_factors.h"
#include "engine/screener/volume_price_factors.h"
namespace st::factors {

namespace {

/// 组内等权，并把整组权重归一为 totalWeight
void normalizeGroup(std::vector<WeightedFactor>& items, double totalWeight) {
    if (items.empty()) return;
    const double each = totalWeight / static_cast<double>(items.size());
    for (auto& [factor, weight] : items) weight = each;
}

}  // namespace

std::vector<WeightedFactor> alpha191FactorSet() {
    auto out = makeAlpha191Factors();
    normalizeGroup(out, 1.0);
    return out;
}

std::vector<WeightedFactor> volumePriceFactorSet() {
    std::vector<WeightedFactor> out;
    out.emplace_back(std::make_shared<VpVolumeRatio>(), 1.0);
    out.emplace_back(std::make_shared<VpTurnover>(), 1.0);
    out.emplace_back(std::make_shared<VpVwapBias>(), 1.0);
    out.emplace_back(std::make_shared<VpAmountMomentum>(), 1.0);
    out.emplace_back(std::make_shared<VpObvSlope>(), 1.0);
    out.emplace_back(std::make_shared<VpMfi>(), 1.0);
    out.emplace_back(std::make_shared<VpPriceVolumeCorr>(), 1.0);
    out.emplace_back(std::make_shared<VpVolumeBreakout>(), 1.0);
    out.emplace_back(std::make_shared<VpAmihud>(), 1.0);
    out.emplace_back(std::make_shared<VpEfi>(), 1.0);
    normalizeGroup(out, 1.0);
    return out;
}

std::vector<WeightedFactor> fundamentalFactorSet() {
    std::vector<WeightedFactor> out;
    out.emplace_back(std::make_shared<FundPe>(), 1.0);
    out.emplace_back(std::make_shared<FundPb>(), 1.0);
    out.emplace_back(std::make_shared<FundRoe>(), 1.0);
    out.emplace_back(std::make_shared<FundRevenueYoy>(), 1.0);
    out.emplace_back(std::make_shared<FundProfitYoy>(), 1.0);
    out.emplace_back(std::make_shared<FundGrossMargin>(), 1.0);
    out.emplace_back(std::make_shared<FundDebtRatio>(), 1.0);
    out.emplace_back(std::make_shared<FundMarketCap>(), 1.0);
    normalizeGroup(out, 1.0);
    return out;
}

std::vector<WeightedFactor> defaultFactorSet() {
    std::vector<WeightedFactor> out;
    auto append = [&out](std::vector<WeightedFactor> group) {
        out.insert(out.end(), std::make_move_iterator(group.begin()),
                   std::make_move_iterator(group.end()));
    };
    append(alpha191FactorSet());
    append(volumePriceFactorSet());
    append(fundamentalFactorSet());
    return out;
}

std::string categoryName(FactorCategory category) {
    switch (category) {
        case FactorCategory::Alpha191: return "Alpha191 因子";
        case FactorCategory::VolumePrice: return "量价因子";
        case FactorCategory::Fundamental: return "基本面因子";
    }
    return "其他因子";
}

} // namespace st::factors
