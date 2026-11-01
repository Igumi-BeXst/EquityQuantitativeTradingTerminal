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
    std::vector<WeightedFactor> out;
    out.emplace_back(std::make_shared<Alpha002>(), 1.0);
    out.emplace_back(std::make_shared<Alpha003>(), 1.0);
    out.emplace_back(std::make_shared<Alpha004>(), 1.0);
    out.emplace_back(std::make_shared<Alpha005>(), 1.0);
    out.emplace_back(std::make_shared<Alpha009>(), 1.0);
    out.emplace_back(std::make_shared<Alpha011>(), 1.0);
    out.emplace_back(std::make_shared<Alpha013>(), 1.0);
    out.emplace_back(std::make_shared<Alpha014>(), 1.0);
    out.emplace_back(std::make_shared<Alpha015>(), 1.0);
    out.emplace_back(std::make_shared<Alpha018>(), 1.0);
    out.emplace_back(std::make_shared<Alpha020>(), 1.0);
    out.emplace_back(std::make_shared<Alpha021>(), 1.0);
    out.emplace_back(std::make_shared<Alpha022>(), 1.0);
    out.emplace_back(std::make_shared<Alpha024>(), 1.0);
    out.emplace_back(std::make_shared<Alpha026>(), 1.0);
    out.emplace_back(std::make_shared<Alpha027>(), 1.0);
    out.emplace_back(std::make_shared<Alpha028>(), 1.0);
    out.emplace_back(std::make_shared<Alpha031>(), 1.0);
    out.emplace_back(std::make_shared<Alpha034>(), 1.0);
    out.emplace_back(std::make_shared<Alpha040>(), 1.0);
    out.emplace_back(std::make_shared<Alpha043>(), 1.0);
    out.emplace_back(std::make_shared<Alpha046>(), 1.0);
    out.emplace_back(std::make_shared<Alpha053>(), 1.0);
    out.emplace_back(std::make_shared<Alpha057>(), 1.0);
    out.emplace_back(std::make_shared<Alpha063>(), 1.0);
    out.emplace_back(std::make_shared<Alpha070>(), 1.0);
    out.emplace_back(std::make_shared<Alpha080>(), 1.0);
    out.emplace_back(std::make_shared<Alpha085>(), 1.0);
    out.emplace_back(std::make_shared<Alpha088>(), 1.0);
    out.emplace_back(std::make_shared<Alpha103>(), 1.0);
    out.emplace_back(std::make_shared<Alpha110>(), 1.0);
    out.emplace_back(std::make_shared<Alpha118>(), 1.0);
    out.emplace_back(std::make_shared<Alpha127>(), 1.0);
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
