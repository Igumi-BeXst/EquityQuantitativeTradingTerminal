#pragma once

#include "engine/screener/panel.h"

#include <memory>
#include <string>
#include <vector>

namespace st::factors::alpha191 {

/// GTJA191 公式表达式（编译后 AST，可重复对 PanelData 求值）
///
/// 支持算子：DELAY/DELTA/SUM/MEAN/STD/TSMAX/TSMIN/TSRANK/CORR/COVIANCE/
/// COUNT/SUMIF/DECAYLINEAR/WMA/SMA/REGBETA/REGRESI/HIGHDAY/LOWDAY/MAX/MIN/
/// RANK/FILTER/SEQUENCE 等；变量：OPEN/HIGH/LOW/CLOSE/VOLUME/AMOUNT/VWAP/RET/
/// MKT/SMB/HML/BANCHMARKINDEXCLOSE/OPEN/TR/HD/LD/DTM/DBM。
/// 横截面 RANK 按日计算（升序分位 0~1），输出为每只股票最后一日原始值。
class Expression {
public:
    explicit Expression(const std::string& formula);
    ~Expression();
    Expression(const Expression&) = delete;
    Expression& operator=(const Expression&) = delete;
    Expression(Expression&&) noexcept;
    Expression& operator=(Expression&&) noexcept;

    /// 公式是否解析成功
    [[nodiscard]] bool valid() const;
    /// 解析错误信息（valid()==false 时非空）
    [[nodiscard]] const std::string& error() const;

    /// 计算最后一日各股票的原始值（长度 = panel.stocks.size()；NaN = 缺失）
    [[nodiscard]] std::vector<double> evaluateLastRow(const PanelData& panel) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace st::factors::alpha191
