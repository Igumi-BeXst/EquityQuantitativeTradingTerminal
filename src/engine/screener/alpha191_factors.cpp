#include "engine/screener/alpha191_factors.h"

#include "engine/screener/factor_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace st::factors {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct FormulaEntry {
    const char* id;
    const char* name;
    const char* formula;
};

// 由任务 alpha-scribe 生成：{id, name, R"(formula)"} 共 191 条
const FormulaEntry kFormulaEntries[] = {
#include "engine/screener/alpha191_formulas.inc"
};

const std::unordered_map<std::string, std::string>& legacyDescriptions() {
    static const std::unordered_map<std::string, std::string> map = {
#include "engine/screener/alpha191_legacy_descriptions.inc"
    };
    return map;
}

/// 公式结构 → 中文类别（仅用于 tooltip，不展示公式本身）
std::string formulaCategory(const std::string& formula) {
    const auto has = [&formula](const char* token) {
        return formula.find(token) != std::string::npos;
    };
    if (has("RANK(") && has("CORR(")) return "截面排序量价相关类";
    if (has("RANK(") && has("TSRANK(")) return "截面排序时序分位类";
    if (has("RANK(")) return "截面排序类";
    if (has("REGRESI(") || has("REGBETA(")) return "回归类";
    if (has("CORR(") || has("COVIANCE(")) return "量价相关类";
    if (has("TSRANK(")) return "时序分位类";
    if (has("STD(") || has("SUMAC(")) return "波动/风险类";
    if (has("DELAY(") || has("MEAN(") || has("SMA(")) return "均值回复/趋势类";
    return "价量类";
}

std::string makeDescription(const std::string& id, const std::string& formula) {
    auto it = legacyDescriptions().find(id);
    if (it != legacyDescriptions().end()) return it->second;
    int number = 0;
    if (id.size() > 5) {
        try { number = std::stoi(id.substr(5)); } catch (...) { number = 0; }
    }
    return "Alpha191 第 " + std::to_string(number) + " 式：" +
           formulaCategory(formula) + " GTJA 因子（面板/横截面计算）";
}

/// 已知缺陷/简化/近似标注（用于 UI 星号 + tooltip）
std::string makeDefectTag(const std::string& id) {
    if (id == "alpha166") {
        return "公式存疑：原文排版损坏且数学不成立，当前按 20 日收益偏度（G1）还原，未经第三方数值核对";
    }
    if (id == "alpha157") {
        return "简化实现：原文含未定义算子 PROD 等，当前仅实现主项 TSRANK(DELAY(-RET,6),5)，与官方原式不等价";
    }
    if (id == "alpha030") {
        return "风格因子近似：MKT/SMB/HML 由当前快照股本/PB 构造（非历史 Fama-French 因子），回归残差仅供近似参考";
    }
    return {};
}

/// ---------------- 自定义实现（通用算子无法表达的少数公式） ----------------

/// Alpha143：SELF 递归累计——上涨日按收益率累乘，其余日保持不变
std::vector<double> customSelfRecursion(const PanelData& panel) {
    const size_t rows = panel.rows();
    const size_t cols = panel.cols();
    std::vector<double> out(cols, kNaN);
    for (size_t i = 0; i < cols; ++i) {
        double self = 1.0;
        bool has = false;
        for (size_t t = 1; t < rows; ++t) {
            const double prev = panel.close[(t - 1) * cols + i];
            const double cur = panel.close[t * cols + i];
            if (!std::isfinite(prev) || !std::isfinite(cur) || prev <= 0.0) continue;
            has = true;
            if (cur > prev) self *= (cur / prev);
        }
        if (has) out[i] = self;
    }
    return out;
}

/// Alpha149：仅基准下跌日，个股收益对基准收益做 252 日滚动回归取斜率
std::vector<double> customBenchmarkBeta(const PanelData& panel) {
    const size_t rows = panel.rows();
    const size_t cols = panel.cols();
    const size_t window = 252;
    const size_t minSamples = 30;
    std::vector<double> out(cols, kNaN);
    for (size_t i = 0; i < cols; ++i) {
        std::vector<double> xs;
        std::vector<double> ys;
        xs.reserve(window);
        ys.reserve(window);
        for (size_t back = 0; back < window && back + 1 < rows; ++back) {
            const size_t t = rows - 1 - back;
            const double b0 = panel.benchClose[t - 1];
            const double b1 = panel.benchClose[t];
            const double br = panel.benchRet[t];
            const double sr = panel.ret[t * cols + i];
            if (!std::isfinite(b0) || !std::isfinite(b1) || !std::isfinite(br)) continue;
            if (!(b1 < b0)) continue;
            if (!std::isfinite(sr)) continue;
            xs.push_back(br);
            ys.push_back(sr);
        }
        if (xs.size() < minSamples) continue;
        double mx = 0.0, my = 0.0;
        for (size_t k = 0; k < xs.size(); ++k) { mx += xs[k]; my += ys[k]; }
        mx /= static_cast<double>(xs.size());
        my /= static_cast<double>(ys.size());
        double cov = 0.0, var = 0.0;
        for (size_t k = 0; k < xs.size(); ++k) {
            cov += (xs[k] - mx) * (ys[k] - my);
            var += (xs[k] - mx) * (xs[k] - mx);
        }
        if (!(var > 1e-15)) continue;
        out[i] = cov / var;
    }
    return out;
}

/// Alpha157：公式含 PROD 等非标准算子，按主项 TSRANK(DELAY(-RET,6),5) 实现
std::vector<double> customDelayReturnRank(const PanelData& panel) {
    const size_t rows = panel.rows();
    const size_t cols = panel.cols();
    std::vector<double> out(cols, kNaN);
    for (size_t i = 0; i < cols; ++i) {
        math::Series r(rows, kNaN);
        for (size_t t = 0; t < rows; ++t) {
            const double v = panel.ret[t * cols + i];
            if (std::isfinite(v)) r[t] = -v;
        }
        const auto delayed = math::delay(r, 6);
        const auto ranks = math::tsRank(delayed, 5);
        auto last = math::lastValid(ranks);
        if (last.has_value()) out[i] = *last;
    }
    return out;
}

/// Alpha165 / Alpha183：滚动窗口 (最大值-最小值)/标准差（SUMAC 重标极差）
std::vector<double> customRescaledRange(const PanelData& panel, int window) {
    const size_t rows = panel.rows();
    const size_t cols = panel.cols();
    std::vector<double> out(cols, kNaN);
    if (rows < static_cast<size_t>(window)) return out;
    for (size_t i = 0; i < cols; ++i) {
        bool ok = true;
        double mean = 0.0;
        for (int d = 0; d < window; ++d) {
            const double v = panel.close[(rows - 1 - static_cast<size_t>(d)) * cols + i];
            if (!std::isfinite(v)) { ok = false; break; }
            mean += v;
        }
        if (!ok) continue;
        mean /= static_cast<double>(window);
        double variance = 0.0;
        for (int d = 0; d < window; ++d) {
            const double v = panel.close[(rows - 1 - static_cast<size_t>(d)) * cols + i];
            variance += (v - mean) * (v - mean);
        }
        const double stdDev = std::sqrt(variance / static_cast<double>(window));
        if (!(stdDev > 0.0)) continue;
        double cumulative = 0.0;
        double maxCum = -std::numeric_limits<double>::infinity();
        double minCum = std::numeric_limits<double>::infinity();
        for (int d = window - 1; d >= 0; --d) {
            const double v = panel.close[(rows - 1 - static_cast<size_t>(d)) * cols + i];
            cumulative += v - mean;
            maxCum = std::max(maxCum, cumulative);
            minCum = std::min(minCum, cumulative);
        }
        out[i] = (maxCum - minCum) / stdDev;
    }
    return out;
}

std::vector<double> customEvaluate(const std::string& id, const PanelData& panel) {
    if (id == "alpha143") return customSelfRecursion(panel);
    if (id == "alpha149") return customBenchmarkBeta(panel);
    if (id == "alpha157") return customDelayReturnRank(panel);
    if (id == "alpha165") return customRescaledRange(panel, 48);
    if (id == "alpha183") return customRescaledRange(panel, 24);
    if (id == "alpha127") {
        // 官方公式 MEAN(...) 缺窗口；按标准 12 日窗口实现
        const size_t rows = panel.rows();
        const size_t cols = panel.cols();
        std::vector<double> out(cols, kNaN);
        for (size_t i = 0; i < cols; ++i) {
            double sum = 0.0;
            int count = 0;
            for (size_t t = rows - std::min<size_t>(rows, 12); t < rows; ++t) {
                double windowMax = -std::numeric_limits<double>::infinity();
                for (size_t k = t + 1 - std::min<size_t>(t + 1, 12); k <= t; ++k) {
                    const double c = panel.close[k * cols + i];
                    if (std::isfinite(c)) windowMax = std::max(windowMax, c);
                }
                const double c = panel.close[t * cols + i];
                if (!std::isfinite(c) || !(windowMax > 0.0)) continue;
                const double v = 100.0 * (c - windowMax) / windowMax;
                sum += v * v;
                ++count;
            }
            if (count > 0) out[i] = std::sqrt(sum / static_cast<double>(count));
        }
        return out;
    }
    return {};
}

}  // namespace

const std::vector<Alpha191FormulaInfo>& alpha191FormulaTable() {
    static const std::vector<Alpha191FormulaInfo> table = [] {
        std::vector<Alpha191FormulaInfo> out;
        out.reserve(std::size(kFormulaEntries));
        for (const auto& entry : kFormulaEntries) {
            out.push_back({entry.id, entry.formula});
        }
        return out;
    }();
    return table;
}

Alpha191Factor::Alpha191Factor(std::string id, std::string formula, std::string description,
                               std::string defectTag)
    : expression_(formula), id_(std::move(id)), formula_(std::move(formula)),
      description_(std::move(description)), defectTag_(std::move(defectTag)) {}

std::string Alpha191Factor::displayName() const {
    // alpha001 → Alpha001；有缺陷标注时追加 " *"
    std::string digits = id_.size() > 5 ? id_.substr(5) : id_;
    std::string name = "Alpha" + digits;
    if (!defectTag_.empty()) name += " *";
    return name;
}

std::string Alpha191Factor::description() const {
    if (defectTag_.empty()) return description_;
    return "【缺陷：" + defectTag_ + "】" + description_;
}

std::vector<double> Alpha191Factor::calculatePanel(const PanelData& panel) const {
    if (panel.empty()) return {};
    std::vector<double> values = customEvaluate(id_, panel);
    if (!values.empty()) return values;
    if (!expression_.valid()) return {};
    return expression_.evaluateLastRow(panel);
}

std::vector<std::pair<std::shared_ptr<IFactor>, double>> makeAlpha191Factors() {
    std::vector<std::pair<std::shared_ptr<IFactor>, double>> out;
    const auto& table = alpha191FormulaTable();
    out.reserve(table.size());
    for (const auto& info : table) {
        out.emplace_back(std::make_shared<Alpha191Factor>(
            info.id, info.formula, makeDescription(info.id, info.formula),
            makeDefectTag(info.id)), 1.0);
    }
    return out;
}

} // namespace st::factors
