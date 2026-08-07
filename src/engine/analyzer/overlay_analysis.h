#pragma once

#include "foundation/bar.h"
#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include "data/eastmoney_sector_provider.h"
#include <QString>
#include <vector>

namespace st {

/// 叠加目标类型：证券（指数/个股）或 板块（行业/概念）
enum class OverlayKind { Security, Sector };

/// 叠加目标 — 用户选择的第二只对比标的
struct OverlayTarget {
    OverlayKind kind = OverlayKind::Security;
    StockCode  stockCode;                  // kind==Security：指数或个股
    std::string sectorCode;                // kind==Sector：板块代码 BKxxxx
    SectorType sectorType = SectorType::Industry;
    QString name;

    [[nodiscard]] bool isValid() const {
        return kind == OverlayKind::Sector ? !sectorCode.empty()
                                           : stockCode.isValid();
    }
};

/// K线对齐行：叠加标在某根 base bar 日期上的对应 OHLC 与相对强弱
/// （叠加显示 K 线蜡烛需要完整 OHLC；未匹配全为 0）
struct OverlayRow {
    bool matched = false;
    double overlayOpen = 0.0;
    double overlayHigh = 0.0;
    double overlayLow = 0.0;
    double overlayClose = 0.0;      // 叠加标同日收盘
    double relativeStrength = 0.0;  // = baseClose / overlayClose
};

/// 以 base 为轴，按日期对齐 overlay；长度 == base.size()（无匹配/非法 close 为 unmatched）
std::vector<OverlayRow> alignOverlay(const std::vector<Bar>& base,
                                     const std::vector<Bar>& overlay);

/// 分时对齐行：叠加标在某个分钟点上的对应价格与相对强弱
struct IntradayOverlayRow {
    bool matched = false;
    double overlayPrice = 0.0;      // 叠加标同分钟价格
    double relativeStrength = 0.0;  // = basePrice / overlayPrice
};

/// 通达信板块指数代码判断（880xxx 行业 / 885xxx 概念）——叠加拉取时走 TDX 主源
inline bool isTdxSectorCode(const std::string& code) {
    return code.size() >= 3 && (code.compare(0, 3, "880") == 0 ||
                                code.compare(0, 3, "885") == 0);
}

/// 以 base 为轴，按分钟（09:30=0 … 15:00=240）对齐 overlay；长度 == base.points.size()
std::vector<IntradayOverlayRow> alignIntradayOverlay(const IntradayData& base,
                                                     const IntradayData& overlay);

} // namespace st
