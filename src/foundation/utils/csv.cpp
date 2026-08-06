#include "foundation/utils/csv.h"
#include "foundation/utils/datetime.h"

#include <iomanip>
#include <sstream>

namespace st::csv {

std::string escape(const std::string& s) {
    const bool needQuote = s.find_first_of(",\"\r\n") != std::string::npos;
    if (!needQuote) return s;
    std::string out("\"");
    for (const char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

std::string joinRow(const std::vector<std::string>& cols) {
    std::ostringstream os;
    for (size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) os << ',';
        os << escape(cols[i]);
    }
    return os.str();
}

std::string klineToCsv(const std::vector<Bar>& bars) {
    std::ostringstream os;
    os << "日期,开盘,最高,最低,收盘,成交量(股),成交额(元),换手率\n";
    for (const auto& b : bars) {
        // 固定小数避免科学计数（默认精度 6 会把大额变成 1.05e+06）
        os << utils::toDateString(b.time) << ','
           << std::fixed << std::setprecision(2) << b.open << ','
           << std::fixed << std::setprecision(2) << b.high << ','
           << std::fixed << std::setprecision(2) << b.low << ','
           << std::fixed << std::setprecision(2) << b.close << ','
           << b.volume << ','
           << std::fixed << std::setprecision(2) << b.amount << ','
           << std::fixed << std::setprecision(4) << b.turnoverRate << '\n';
    }
    return os.str();
}

} // namespace st::csv
