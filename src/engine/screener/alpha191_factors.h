#pragma once

#include "engine/screener/factor.h"
#include "engine/screener/alpha191_expression.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace st::factors {

/// Alpha191 公式表条目
struct Alpha191FormulaInfo {
    std::string id;      // alpha001 ... alpha191
    std::string formula; // 规范化后的官方公式
};

/// 全部 191 条官方公式（解析失败/自定义实现的条目也保留）
const std::vector<Alpha191FormulaInfo>& alpha191FormulaTable();

/// 基于面板表达式引擎的 Alpha191 因子
///
/// 公式来自 GTJA191 官方公式表；横截面 RANK / 基准指数 / 风格因子等
/// 通过 PanelData 面板计算。少数公式无法用通用算子表达（SELF/FILTER 回归/
/// SUMAC 窗口等），由 calculatePanel 内的自定义实现兜底。
class Alpha191Factor : public IFactor {
public:
    Alpha191Factor(std::string id, std::string formula, std::string description);

    std::string name() const override { return id_; }
    std::string displayName() const override;
    std::string description() const override { return description_; }
    FactorCategory category() const override { return FactorCategory::Alpha191; }
    bool higherIsBetter() const override { return true; }
    std::optional<double> calculate(const FactorContext&) const override {
        return std::nullopt;  // 面板因子，逐股接口不适用
    }
    bool isPanel() const override { return true; }
    std::vector<double> calculatePanel(const PanelData& panel) const override;

private:
    alpha191::Expression expression_;
    std::string id_;
    std::string formula_;
    std::string description_;
};

/// 创建 191 个 Alpha191 面板因子（权重由因子库统一归一）
std::vector<std::pair<std::shared_ptr<IFactor>, double>> makeAlpha191Factors();

} // namespace st::factors
