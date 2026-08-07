#include "engine/analyzer/overlay_analysis.h"
#include "foundation/utils/datetime.h"
#include <chrono>
#include <ctime>
#include <unordered_map>

namespace st {

namespace {

/// 距开盘分钟（09:30=0 … 15:00=240，含午休的连续刻度）——与分时图 X 轴一致
int minuteOfDay(const DateTime& t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    const int mins = tm.tm_hour * 60 + tm.tm_min;
    if (mins < 13 * 60) return mins - (9 * 60 + 30);  // 上午 09:30=0
    return 120 + mins - 13 * 60;                       // 下午 13:00=120
}

}  // namespace

std::vector<OverlayRow> alignOverlay(const std::vector<Bar>& base,
                                     const std::vector<Bar>& overlay) {
    // overlay 日期 → OHLC（close<=0 跳过；同日重复后写覆盖）
    struct OverlayBarRef { double o = 0, h = 0, l = 0, c = 0; };
    std::unordered_map<std::string, OverlayBarRef> ovBar;
    ovBar.reserve(overlay.size());
    for (const auto& b : overlay) {
        if (b.close <= 0) continue;
        OverlayBarRef ref;
        ref.o = b.open;
        ref.h = b.high;
        ref.l = b.low;
        ref.c = b.close;
        ovBar[utils::toDateString(b.time)] = ref;
    }

    std::vector<OverlayRow> rows;
    rows.reserve(base.size());
    for (const auto& b : base) {
        OverlayRow row;
        if (b.close > 0) {
            const auto it = ovBar.find(utils::toDateString(b.time));
            if (it != ovBar.end() && it->second.c > 0) {
                row.matched = true;
                row.overlayOpen = it->second.o;
                row.overlayHigh = it->second.h;
                row.overlayLow = it->second.l;
                row.overlayClose = it->second.c;
                row.relativeStrength = b.close / it->second.c;
            }
        }
        rows.push_back(row);
    }
    return rows;
}

std::vector<IntradayOverlayRow> alignIntradayOverlay(const IntradayData& base,
                                                     const IntradayData& overlay) {
    // overlay 分钟 → 价格（price<=0 跳过；同分钟后写覆盖）
    std::unordered_map<int, double> ovPrice;
    ovPrice.reserve(overlay.points.size());
    for (const auto& pt : overlay.points) {
        if (pt.price <= 0) continue;
        const int m = minuteOfDay(pt.time);
        if (m < 0 || m > 239) continue;
        ovPrice[m] = pt.price;
    }

    std::vector<IntradayOverlayRow> rows;
    rows.reserve(base.points.size());
    for (const auto& pt : base.points) {
        IntradayOverlayRow row;
        if (pt.price > 0) {
            const auto it = ovPrice.find(minuteOfDay(pt.time));
            if (it != ovPrice.end() && it->second > 0) {
                row.matched = true;
                row.overlayPrice = it->second;
                row.relativeStrength = pt.price / it->second;
            }
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace st
