#include "engine/analyzer/custom_index.h"
#include "foundation/utils/datetime.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>

namespace st {

void normalizeWeights(std::vector<IndexConstituent>& constituents) {
    if (constituents.empty()) return;
    double sum = 0.0;
    for (const auto& c : constituents) {
        if (c.weight > 0.0) sum += c.weight;
    }
    if (sum <= 0.0) {  // 全 0 → 等权
        const double w = 1.0 / static_cast<double>(constituents.size());
        for (auto& c : constituents) c.weight = w;
        return;
    }
    for (auto& c : constituents) {
        c.weight = (c.weight > 0.0) ? (c.weight / sum) : 0.0;
    }
}

namespace {

constexpr double EPS = 1e-9;

/// "YYYY-MM-DD" → 自 1970-01-01 的天数（Howard Hinnant civil 算法）
int daysFromCivil(const std::string& dateStr) {
    int y = 0, m = 0, d = 0;
    if (dateStr.size() >= 10) {
        y = std::stoi(dateStr.substr(0, 4));
        m = std::stoi(dateStr.substr(5, 2));
        d = std::stoi(dateStr.substr(8, 2));
    }
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;                      // [0, 399]
    const int mp  = m + (m > 2 ? -3 : 9);               // 3..14
    const int doy = (153 * mp + 2) / 5 + d - 1;         // [0, 365]
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;  // [0, 146096]
    return era * 146097 + doe - 719468;                 // 天数（1970-01-01 = 0）
}

/// 周一为一周起点 → 该日期所在周的周一（天数）
int weekStart(int days) {
    // 1970-01-01 是周四：offset = (days + 3) mod 7（周一 = 0）
    return days - ((days + 3) % 7 + 7) % 7;
}

/// "YYYY-MM-DD" → 年*100+月（月线分组键）
int yearMonthKey(const std::string& dateStr) {
    int y = 0, m = 0;
    if (dateStr.size() >= 7) {
        y = std::stoi(dateStr.substr(0, 4));
        m = std::stoi(dateStr.substr(5, 2));
    }
    return y * 100 + m;
}

/// 把日线指数 bar 序列聚合到周/月（周：周一起点分组；月：自然月分组）
/// 周/月 bar：open=组内第一根开，high=最大，low=最小，close=最后一根收，time=组内最后日期
std::vector<Bar> aggregateToPeriod(const std::vector<Bar>& daily, BarPeriod period) {
    if (period == BarPeriod::Daily) return daily;
    if (period != BarPeriod::Weekly && period != BarPeriod::Monthly) return daily;

    std::map<int, size_t> groupIndex;   // 组键 → out 中的索引
    std::vector<Bar> out;
    for (const auto& b : daily) {
        const std::string ds = utils::toDateString(b.time);
        const int days = daysFromCivil(ds);
        const int key = (period == BarPeriod::Weekly)
            ? weekStart(days)
            : yearMonthKey(ds);
        auto it = groupIndex.find(key);
        if (it == groupIndex.end()) {
            groupIndex[key] = out.size();
            out.push_back(b);
        } else {
            Bar& g = out[it->second];
            g.high  = std::max(g.high, b.high);
            g.low   = std::min(g.low, b.low);
            g.close = b.close;      // 组内最后一根为收盘
            g.time  = b.time;       // 组内最后日期
        }
    }
    return out;
}

struct ComponentSeries {
    double weight = 0.0;
    std::string firstDate;               // 第一根 bar 的日期
    bool excluded = false;               // 基准日前无数据（上市晚于基准日）→ 剔除
    std::map<std::string, Bar> byDate;   // 日期 → 当日 bar
};

}  // namespace

std::vector<Bar> computeIndexBars(const CustomIndex& idx, const BarsFetcher& fetch,
                                  BarPeriod period) {
    std::vector<Bar> result;
    if (idx.constituents.empty() || idx.baseValue <= 0.0) return result;

    CustomIndex norm = idx;
    normalizeWeights(norm.constituents);  // 内部归一化：全 0 → 等权；手动权重等比缩放

    // 1. 拉成分股日线，计算基准日（显式 baseDate 或首个共同数据日）
    std::vector<ComponentSeries> comps;
    std::string maxFirst;
    for (const auto& c : norm.constituents) {
        if (c.weight <= 0.0) continue;
        auto bars = fetch(c.code, BarPeriod::Daily);
        if (bars.empty()) continue;
        ComponentSeries cs;
        cs.weight = c.weight;
        cs.firstDate = utils::toDateString(bars.front().time);
        for (const auto& b : bars) cs.byDate[utils::toDateString(b.time)] = b;
        if (maxFirst.empty() || cs.firstDate > maxFirst) maxFirst = cs.firstDate;
        comps.push_back(std::move(cs));
    }
    if (comps.empty()) return result;

    // 2. 基准日：显式 baseDate（创建当天）或首个共同数据日；不早于所有成分股都有数据的起点
    std::string baseDateKey = idx.baseDate.has_value()
        ? utils::toDateString(*idx.baseDate) : "";
    if (baseDateKey.empty()) baseDateKey = maxFirst;
    else if (baseDateKey < maxFirst) baseDateKey = maxFirst;

    // 3. 除数 D = Σ wᵢ·Pᵢ(T₀)，取各成分股 ≤ 基准日的最后一根 bar 收盘
    //    （基准日落在周末/节假日时自动回退到最近交易日）；基准日前无数据的成分股剔除
    double divisor = 0.0;
    for (auto& cs : comps) {
        auto it = cs.byDate.upper_bound(baseDateKey);  // 首个 > 基准日的 bar
        if (it == cs.byDate.begin()) { cs.excluded = true; continue; }
        --it;  // 最后一根 ≤ 基准日的 bar
        divisor += cs.weight * it->second.close;
    }
    if (divisor <= 0.0) return result;

    // 4. 合并交易日（≥ 首个共同数据日），逐日价格加权
    std::vector<std::string> dates;
    for (const auto& cs : comps) {
        if (cs.excluded) continue;
        for (const auto& [d, b] : cs.byDate) {
            if (d >= maxFirst) dates.push_back(d);
        }
    }
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());

    std::vector<const Bar*> lastBar(comps.size(), nullptr);  // 每成分股 carry-forward
    std::vector<Bar> daily;
    daily.reserve(dates.size());

    const double f = idx.baseValue / divisor;
    for (const auto& d : dates) {
        double o = 0, h = 0, l = 0, c = 0;
        for (size_t i = 0; i < comps.size(); ++i) {
            const auto& cs = comps[i];
            if (cs.excluded) continue;  // 基准日前无数据，不参与
            auto it = cs.byDate.find(d);
            if (it != cs.byDate.end()) lastBar[i] = &it->second;
            const Bar* bar = lastBar[i];
            if (!bar) continue;  // 基准日后异常缺数据，跳过该成分股当日
            o += cs.weight * bar->open;
            h += cs.weight * bar->high;
            l += cs.weight * bar->low;
            c += cs.weight * bar->close;
        }
        if (c <= 0.0) continue;
        Bar b;
        b.code = norm.constituents.front().code;  // 占位（指数无真实代码）
        b.time = utils::parseDate(d);
        b.period = BarPeriod::Daily;
        b.open   = o * f;
        b.high   = h * f;
        b.low    = l * f;
        b.close  = c * f;
        b.volume = 0;
        b.amount = 0.0;
        daily.push_back(b);
    }

    return aggregateToPeriod(daily, period);
}

IntradayData computeIndexIntraday(const CustomIndex& idx, double indexPrevClose,
                                  const IntradayFetcher& fetch) {
    IntradayData result;
    result.preClose = indexPrevClose;
    if (indexPrevClose <= 0.0 || idx.constituents.empty()) return result;

    auto constituents = idx.constituents;
    normalizeWeights(constituents);

    struct Comp {
        double w = 0.0;
        double preClose = 0.0;
        std::map<int, double> minutePrice; // 当日分钟(0..1439) → 价格
    };
    std::vector<Comp> comps;
    DateTime baseDay;   // 当日基准（全部成分股同一交易日）
    bool hasBaseDay = false;
    for (const auto& c : constituents) {
        if (c.weight <= 0.0) continue;
        auto data = fetch(c.code);
        if (data.empty() || data.preClose <= 0.0) continue;
        Comp cc;
        cc.w = c.weight;
        cc.preClose = data.preClose;
        if (!hasBaseDay) {
            baseDay = utils::today();
            if (!data.points.empty()) {
                baseDay = utils::parseDate(utils::toDateString(data.points.front().time));
            }
            hasBaseDay = true;
        }
        for (const auto& p : data.points) {
            // 取【本地时区】当天分钟数（分时图 minutesFromOpen 用 localtime，须与之一致；
            // 若用 secs%86400 是 UTC 当天分钟，本地 UTC+8 会偏 8 小时 → 价格线画到屏幕外）
            const std::time_t tt = std::chrono::system_clock::to_time_t(p.time);
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &tt);
#else
            localtime_r(&tt, &tm);
#endif
            const int minute = tm.tm_hour * 60 + tm.tm_min;
            cc.minutePrice[minute] = p.price;
        }
        comps.push_back(std::move(cc));
    }
    if (comps.empty() || !hasBaseDay) return result;

    // 分时数据缺失的成分股不参与当日 → 权重在可用成分股上重归一化（避免指数迟钝）
    double sumW = 0.0;
    for (const auto& cc : comps) sumW += cc.w;
    if (sumW <= 0.0) return result;
    if (std::abs(sumW - 1.0) > EPS) {
        for (auto& cc : comps) cc.w /= sumW;
    }

    // 合并所有分钟（升序，去重）
    std::vector<int> minutes;
    for (const auto& cc : comps) {
        for (const auto& [m, p] : cc.minutePrice) minutes.push_back(m);
    }
    std::sort(minutes.begin(), minutes.end());
    minutes.erase(std::unique(minutes.begin(), minutes.end()), minutes.end());

    std::vector<int> lastMinute(comps.size(), -1);  // 每成分股最后已知分钟
    for (int m : minutes) {
        double ret = 0.0;
        for (size_t i = 0; i < comps.size(); ++i) {
            const auto& cc = comps[i];
            auto it = cc.minutePrice.find(m);
            if (it != cc.minutePrice.end()) lastMinute[i] = m;
            const auto cur = lastMinute[i] >= 0
                ? cc.minutePrice.at(lastMinute[i]) : 0.0;
            if (cur > 0.0) ret += cc.w * (cur / cc.preClose - 1.0);
        }
        IntradayPoint pt;
        pt.time = baseDay + std::chrono::minutes(m);
        pt.price = indexPrevClose * (1.0 + ret);
        pt.volume = 0;
        pt.amount = 0.0;
        result.points.push_back(std::move(pt));
    }
    if (!result.points.empty()) result.date = result.points.front().time;
    return result;
}

double computeIndexLive(double indexPrevClose, const CustomIndex& idx,
                        const std::vector<Quote>& quotes) {
    if (indexPrevClose <= 0.0) return 0.0;
    auto constituents = idx.constituents;
    normalizeWeights(constituents);
    std::unordered_map<std::string, double> changeByCode;
    for (const auto& q : quotes) changeByCode[q.code.fullCode()] = q.change;
    double ret = 0.0;
    for (const auto& c : constituents) {
        auto it = changeByCode.find(c.code.fullCode());
        if (it != changeByCode.end()) ret += c.weight * it->second / 100.0;
    }
    return indexPrevClose * (1.0 + ret);
}

double lastCompletedClose(const std::vector<Bar>& dailyIndexBars, const DateTime& now) {
    if (dailyIndexBars.empty()) return 0.0;
    const std::string today = utils::toDateString(now);
    const Bar& last = dailyIndexBars.back();
    if (utils::toDateString(last.time) < today) return last.close;
    if (dailyIndexBars.size() >= 2) {
        return dailyIndexBars[dailyIndexBars.size() - 2].close;
    }
    return 0.0;  // 只有今日 → 无昨收
}

} // namespace st
