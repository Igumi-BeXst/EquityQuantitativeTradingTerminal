#include "ui/widgets/time_line_chart.h"
#include "data/idata_provider.h"
#include "data/tdx/tdx_models.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMetaObject>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

namespace st {

namespace {
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");

constexpr double kRightAxisW = 64;
constexpr double kLeftAxisW = 56;   // 左价格轴
constexpr double kBottomAxisH = 26;
constexpr double kTitleH = 22;
constexpr double kMainRatio = 0.58;   // 主价格区占可绘图区
constexpr double kVolRatio = 0.16;    // 量区
constexpr double kMargin = 8;         // 图表左右留空，避免紧贴边缘
constexpr double kPaneHeaderH = 16;   // 指标面板顶部图例条带（标签与指标图分离）

// 股 → 手显示：≥1亿用亿、≥1万用万、否则手数（不显示"手"后缀，对齐参照软件）
QString formatVolume(double shares) {
    const double hands = shares / 100.0;
    if (hands >= 1e8) return QStringLiteral("%1亿").arg(hands / 1e8, 0, 'f', 2);
    if (hands >= 1e4) return QStringLiteral("%1万").arg(hands / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(hands, 0, 'f', 0);
}

// 是否交易时段（工作日 09:25 集合竞价前 ~ 15:05 收盘后）——非交易时段分时数据静态，跳过自动刷新
bool isTradingTime() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    if (tm.tm_wday == 0 || tm.tm_wday == 6) return false;  // 周末
    const int mins = tm.tm_hour * 60 + tm.tm_min;
    return mins >= 9 * 60 + 25 && mins <= 15 * 60 + 5;
}
}  // namespace

TimelineChart::TimelineChart(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    setMouseTracking(true);
    setMinimumSize(400, 300);
    // 实时自动刷新：交易时段每 10s 静默重拉分时数据（新分钟/最新价自动更新）
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(10000);
    connect(refreshTimer_, &QTimer::timeout, this, &TimelineChart::refreshData);
}

void TimelineChart::loadStock(const StockCode& code, const QString& name) {
    const int gen = ++loadGen_;
    code_ = code;
    name_ = name;
    loading_ = true;
    update();
    if (!provider_) { loading_ = false; return; }
    if (refreshTimer_) refreshTimer_->start();

    ThreadPool::submitIO([this, gen, code] {
        auto data = provider_->getIntraday(code);
        QMetaObject::invokeMethod(this, [this, gen, data = std::move(data)]() mutable {
            if (gen != loadGen_) return;
            setData(data ? *data : IntradayData{});
        }, Qt::QueuedConnection);
    });
}

void TimelineChart::refreshData() {
    if (!provider_ || !code_.isValid()) return;
    if (!isTradingTime()) return;  // 非交易时段数据静态，跳过
    const int gen = ++loadGen_;
    const int savedMouse = mouseIndex_;
    ThreadPool::submitIO([this, gen, code = code_, savedMouse] {
        auto data = provider_->getIntraday(code);
        QMetaObject::invokeMethod(this, [this, gen, savedMouse, data = std::move(data)]() mutable {
            if (gen != loadGen_) return;
            if (data) {
                setData(*data);      // 内部 mouseIndex_=-1
                mouseIndex_ = savedMouse;  // 保留十字线位置
                update();
            }
        }, Qt::QueuedConnection);
    });
}

void TimelineChart::setData(IntradayData newData) {
    data_ = std::move(newData);
    loading_ = false;
    mouseIndex_ = -1;
    computeAvgLine();
    computeMacd();
    computeRanges();
    update();
}

int TimelineChart::minutesFromOpen(const DateTime& t) const {
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

double TimelineChart::xFor(int minutes) const {
    const double plotW = mainRect_.width() - 2 * kMargin;
    return mainRect_.left() + kMargin + minutes / 240.0 * plotW;
}

double TimelineChart::priceToY(double price) const {
    const double h = mainRect_.height();
    if (priceHi_ - priceLo_ < 1e-12) return mainRect_.top();
    return mainRect_.top() + (priceHi_ - price) / (priceHi_ - priceLo_) * h;
}

double TimelineChart::volToY(double v) const {
    const double h = volRect_.height() - kPaneHeaderH;
    return volRect_.bottom() - v / volHi_ * h;
}

double TimelineChart::macdToY(double v) const {
    const double top = macdRect_.top() + kPaneHeaderH;
    const double mid = (top + macdRect_.bottom()) / 2.0;
    return mid - v / macdMaxAbs_ * ((macdRect_.bottom() - top) / 2.0);
}

void TimelineChart::computeAvgLine() {
    avgLine_.clear();
    // 指数无均价线（均价=每股均价，与指数点位量纲不同）
    if (tdx::isIndexCode(code_)) return;
    // 腾讯分时数据是累计值 → 均价 = 累计额 / 累计量（正确 VWAP）
    for (const auto& pt : data_.points) {
        if (pt.volume > 0) avgLine_.push_back(pt.amount / pt.volume);
        else avgLine_.push_back(avgLine_.empty() ? 0.0 : avgLine_.back());
    }
    // 开盘首点：均价 = 开盘价（集合竞价是当日首笔，价格线与均价线同起点）
    if (!avgLine_.empty() && !data_.points.empty())
        avgLine_[0] = data_.points.front().price;
}

void TimelineChart::computeMacd() {
    macdDif_.clear();
    macdDea_.clear();
    macdHist_.clear();
    if (data_.points.size() < 30) return;

    // 用 preClose 扩展 ~40 个点作 EMA 预热，使 MACD 从最左侧即有有效值
    // （macd(12,26,9) 需 ~35 个预热点，否则左侧 ~33 分钟为 NaN）
    const double seed = data_.preClose > 0 ? data_.preClose : data_.points.front().price;
    constexpr int kWarmUp = 40;
    std::vector<double> prices;
    prices.reserve(data_.points.size() + kWarmUp);
    for (int i = 0; i < kWarmUp; ++i) prices.push_back(seed);
    for (const auto& pt : data_.points) prices.push_back(pt.price);

    auto m = st::indicators::macd(prices);
    macdDif_.assign(m.dif.begin() + kWarmUp, m.dif.end());
    macdDea_.assign(m.dea.begin() + kWarmUp, m.dea.end());
    macdHist_.assign(m.hist.begin() + kWarmUp, m.hist.end());
}

void TimelineChart::computeRanges() {
    if (data_.points.empty()) return;

    // 主图价格范围: 涨跌对称，取最大单侧波动，保证涨/跌分段区间一致
    double hi = data_.preClose, lo = data_.preClose;
    for (const auto& pt : data_.points) {
        hi = std::max(hi, pt.price);
        lo = std::min(lo, pt.price);
    }
    symRange_ = std::max(hi - data_.preClose, data_.preClose - lo);
    if (symRange_ < 1e-9) symRange_ = data_.preClose * 0.01;  // 无波动默认 ±1%
    const double pad = symRange_ * 0.05 + 0.01;
    priceHi_ = data_.preClose + symRange_ + pad;
    priceLo_ = data_.preClose - symRange_ - pad;

    // 量程: 每分钟增量（腾讯分时量为累计值）
    volHi_ = 0;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const double cum = static_cast<double>(data_.points[i].volume);
        const double prev = i > 0 ? static_cast<double>(data_.points[i - 1].volume) : 0.0;
        volHi_ = std::max(volHi_, cum - prev);
    }
    if (volHi_ <= 0) volHi_ = 1;

    // MACD 量程
    macdMaxAbs_ = 0;
    for (size_t i = 0; i < macdDif_.size(); ++i) {
        for (double v : {macdDif_[i], macdDea_[i], macdHist_[i]}) {
            if (std::isfinite(v)) macdMaxAbs_ = std::max(macdMaxAbs_, std::abs(v));
        }
    }
    if (macdMaxAbs_ <= 0) macdMaxAbs_ = 1;
}

// ============================================================
// 绘制
// ============================================================
void TimelineChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));

    if (data_.points.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, loading_ ? tr("加载中…") : tr("无数据"));
        return;
    }

    // 布局: 主图 / 量 / 分时MACD
    const double plotW = width() - kRightAxisW - kLeftAxisW;
    const double plotH = height() - kTitleH - kBottomAxisH;
    const QRectF plot(kLeftAxisW, kTitleH, plotW, plotH);
    mainRect_ = QRectF(plot.left(), plot.top(), plot.width(), plotH * kMainRatio);
    volRect_ = QRectF(plot.left(), plot.top() + mainRect_.height() + 8,
                      plot.width(), plotH * kVolRatio);
    macdRect_ = QRectF(plot.left(), volRect_.bottom() + 8,
                       plot.width(), plot.bottom() - volRect_.bottom() - 8);

    // 面板背景交替着色 + 分隔线（同日线图样式：主图保持底色，指标面板异色 + 顶部分隔线）
    const QRectF panels[] = {mainRect_, volRect_, macdRect_};
    for (int i = 1; i < 3; ++i) {
        p.fillRect(panels[i], (i % 2 == 0) ? QColor(26, 26, 29) : QColor(22, 22, 24));
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        p.drawLine(QPointF(panels[i].left(), panels[i].top()),
                   QPointF(panels[i].right(), panels[i].top()));
    }

    drawTitle(p);
    drawGridAndAxis(p);
    drawPriceLines(p);
    drawVolume(p);
    drawMacd(p);
    drawCrosshair(p);
}

void TimelineChart::drawTitle(QPainter& p) {
    p.setPen(QColor("#d4d4d4"));
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);

    const auto& last = data_.points.back();
    const double change = data_.preClose > 0
        ? (last.price - data_.preClose) / data_.preClose * 100.0 : 0.0;
    QColor c = change >= 0 ? kUpColor : kDownColor;

    QString text = name_ + "  " + QString::fromStdString(code_.displayCode())
        + tr("  [分时]  ") + QString::number(last.price, 'f', 2);
    p.setPen(c);
    text += QStringLiteral("  %1%").arg(change, 0, 'f', 2);
    p.drawText(QRectF(4, 2, width() - 8, kTitleH - 2),
               Qt::AlignLeft | Qt::AlignVCenter, text);
}

void TimelineChart::drawGridAndAxis(QPainter& p) {
    p.setPen(QColor("#888888"));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    const double rightX = mainRect_.right();

    // 竖分隔: 10:30 / 11:30(午休虚线) / 14:00
    for (int m : {60, 120, 180}) {
        double x = xFor(m);
        p.setPen(QPen(QColor(255, 255, 255, m == 120 ? 50 : 25), 1,
                      m == 120 ? Qt::DashLine : Qt::SolidLine));
        p.drawLine(QPointF(x, mainRect_.top()), QPointF(x, macdRect_.bottom()));
    }

    // X 轴标签
    p.setPen(QColor("#888888"));
    const double axisY = height() - 20;
    const QPointF labels[] = {{xFor(0), axisY}, {xFor(60), axisY},
                              {xFor(120) - 20, axisY}, {xFor(180), axisY},
                              {rightX - kMargin - 40, axisY}};
    const char* texts[] = {"09:30", "10:30", "11:30/13:00", "14:00", "15:00"};
    for (int i = 0; i < 5; ++i) {
        p.drawText(QRectF(labels[i].x(), labels[i].y(), 60, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::fromUtf8(texts[i]));
    }

    // 主图: 涨/跌各 6 段，两侧对称等分（symRange_），左=价格标签，右=涨跌幅标签
    if (!data_.points.empty() && data_.preClose > 0 && symRange_ > 1e-9) {
        constexpr int kSeg = 6;

        auto labelLevel = [&](double price, bool isCenter) {
            const double y = priceToY(price);
            // 昨收线用虚线强调，其余为细分隔线
            p.setPen(isCenter ? QPen(QColor("#aaaaaa"), 1, Qt::DashLine)
                              : QPen(QColor(255, 255, 255, 28), 1));
            p.drawLine(QPointF(mainRect_.left(), y), QPointF(rightX, y));
            // 左: 价格标签
            p.setPen(isCenter ? QColor("#aaaaaa") : QColor("#888888"));
            p.drawText(QRectF(2, y - 7, kLeftAxisW - 4, 14),
                       Qt::AlignRight | Qt::AlignVCenter,
                       QString::number(price, 'f', 2));
            // 右: 涨跌幅标签
            const double pct = (price - data_.preClose) / data_.preClose * 100.0;
            p.setPen(isCenter ? QColor("#aaaaaa") : (pct >= 0 ? kUpColor : kDownColor));
            p.drawText(QRectF(rightX + 2, y - 7, kRightAxisW - 4, 14),
                       Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1%").arg(pct, 0, 'f', 2));
        };

        for (int i = kSeg; i >= 1; --i) labelLevel(data_.preClose - symRange_ * i / kSeg, false);
        labelLevel(data_.preClose, true);
        for (int i = 1; i <= kSeg; ++i) labelLevel(data_.preClose + symRange_ * i / kSeg, false);
    }

    // 量轴（手，自动缩放）
    p.drawText(QRectF(rightX + 2, volRect_.top() + 2, kRightAxisW - 4, 14),
               Qt::AlignLeft | Qt::AlignVCenter, formatVolume(volHi_));
    // MACD 轴: 高低标注
    if (!macdDif_.empty()) {
        p.drawText(QRectF(rightX + 2, macdRect_.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("+%1").arg(macdMaxAbs_, 0, 'f', 2));
        p.drawText(QRectF(rightX + 2, macdRect_.bottom() - 16, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("-%1").arg(macdMaxAbs_, 0, 'f', 2));
    }
}

void TimelineChart::drawPriceLines(QPainter& p) {
    const double w = mainRect_.right();
    // 价格线
    QPainterPath pricePath, avgPath;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const auto& pt = data_.points[i];
        double x = xFor(minutesFromOpen(pt.time));
        double y = priceToY(pt.price);
        if (i == 0) pricePath.moveTo(x, y);
        else pricePath.lineTo(x, y);

        if (i < avgLine_.size() && avgLine_[i] > 0) {
            double ay = priceToY(avgLine_[i]);
            if (i == 0) avgPath.moveTo(x, ay);
            else avgPath.lineTo(x, ay);
        }
    }
    QPen pricePen(QColor("#4fc3f7"), 1.2);
    p.setPen(pricePen);
    p.drawPath(pricePath);
    QPen avgPen(QColor("#ffa726"), 1.2);
    p.setPen(avgPen);
    p.drawPath(avgPath);

    // 均价线图例（指数无均价线）
    if (!avgLine_.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(QRectF(w - 110, kTitleH + 2, 106, 14), Qt::AlignRight,
                   tr("均价 %1").arg(avgLine_.back(), 0, 'f', 2));
    }
}

void TimelineChart::drawVolume(QPainter& p) {
    const double bw = (mainRect_.width() - 2 * kMargin) / 240.0;

    QPainterPath upPath, downPath;
    for (size_t i = 0; i < data_.points.size(); ++i) {
        const auto& pt = data_.points[i];
        int m = minutesFromOpen(pt.time);
        if (m < 0 || m > 239) continue;
        // 分时量 = 每分钟增量（腾讯原始为累计值）
        const double cum = static_cast<double>(pt.volume);
        const double prev = i > 0 ? static_cast<double>(data_.points[i - 1].volume) : 0.0;
        const double barVol = cum - prev;
        if (barVol <= 0) continue;
        double x = xFor(m);
        double h = barVol / volHi_ * (volRect_.height() - kPaneHeaderH);
        // 量柱颜色按分钟自身涨跌（对比前一分钟），对齐成熟软件；首分钟对比昨收
        const double refPrice = (i > 0) ? data_.points[i - 1].price
                                : (data_.preClose > 0 ? data_.preClose : pt.price);
        auto& path = pt.price >= refPrice ? upPath : downPath;
        path.addRect(QRectF(x - 0.35 * bw, volRect_.bottom() - h, 0.7 * bw, h));
    }
    p.fillPath(upPath, kUpColor);
    p.fillPath(downPath, kDownColor);

    // 图例（顶部独立条带，与量柱分开）
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor("#d4d4d4"));
    p.drawText(QRectF(volRect_.left() + 6, volRect_.top() + 2, 200, kPaneHeaderH - 4),
               Qt::AlignLeft | Qt::AlignVCenter, tr("分时量"));
}

void TimelineChart::drawMacd(QPainter& p) {
    if (macdDif_.empty()) return;

    // 0 轴（在图例条带下方）
    const double midY = (macdRect_.top() + kPaneHeaderH + macdRect_.bottom()) / 2.0;
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(macdRect_.left(), midY),
               QPointF(macdRect_.right(), midY));

    // 柱
    QPainterPath upHist, downHist;
    const double bw = (mainRect_.width() - 2 * kMargin) / 240.0;
    for (size_t i = 0; i < macdHist_.size() && i < data_.points.size(); ++i) {
        if (!std::isfinite(macdHist_[i])) continue;
        const int m = minutesFromOpen(data_.points[i].time);
        if (m < 0 || m > 239) continue;
        const double y = macdToY(macdHist_[i]);
        auto& path = macdHist_[i] >= 0 ? upHist : downHist;
        path.addRect(QRectF(xFor(m) - 0.35 * bw, std::min(y, midY), 0.7 * bw,
                            std::max(std::abs(y - midY), 1.0)));
    }
    p.fillPath(upHist, kUpColor);
    p.fillPath(downHist, kDownColor);

    // DIF/DEA 线
    auto drawLine = [&](const std::vector<double>& series, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (size_t i = 0; i < series.size() && i < data_.points.size(); ++i) {
            if (!std::isfinite(series[i])) { started = false; continue; }
            const int m = minutesFromOpen(data_.points[i].time);
            if (m < 0 || m > 239) { started = false; continue; }
            const double x = xFor(m);
            if (!started) { path.moveTo(x, macdToY(series[i])); started = true; }
            else { path.lineTo(x, macdToY(series[i])); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(macdDif_, QColor("#ffffff"));
    drawLine(macdDea_, QColor("#ffd54f"));

    // 图例（顶部独立条带，与柱图分开）
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const int lastIdx = static_cast<int>(macdDif_.size()) - 1;
    p.setPen(QColor("#d4d4d4"));
    p.drawText(QRectF(macdRect_.left() + 6, macdRect_.top() + 2,
                      macdRect_.width() - 12, kPaneHeaderH - 4),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("分时MACD(12,26,9)  DIF %1  DEA %2")
                   .arg(macdDif_[static_cast<size_t>(lastIdx)], 0, 'f', 2)
                   .arg(macdDea_[static_cast<size_t>(lastIdx)], 0, 'f', 2));
}

void TimelineChart::drawCrosshair(QPainter& p) {
    if (mouseIndex_ < 0 || mouseIndex_ >= static_cast<int>(data_.points.size())) return;
    const auto& pt = data_.points[static_cast<size_t>(mouseIndex_)];
    double x = xFor(minutesFromOpen(pt.time));

    const double volBottom = macdRect_.bottom();
    p.setPen(QPen(QColor(200, 200, 200, 160), 1, Qt::DashLine));
    p.drawLine(QPointF(x, mainRect_.top()), QPointF(x, volBottom));
    p.drawLine(QPointF(mainRect_.left(), mouseY_), QPointF(mainRect_.right(), mouseY_));

    const double change = data_.preClose > 0
        ? (pt.price - data_.preClose) / data_.preClose * 100.0 : 0.0;
    const double changeAbs = data_.preClose > 0 ? (pt.price - data_.preClose) : 0.0;
    const double avg = mouseIndex_ < static_cast<int>(avgLine_.size())
        ? avgLine_[static_cast<size_t>(mouseIndex_)] : 0.0;
    // 分时量 = 该分钟累计量的增量（与量柱一致）
    const double cumVol = static_cast<double>(pt.volume);
    const double prevVol = mouseIndex_ > 0
        ? static_cast<double>(data_.points[static_cast<size_t>(mouseIndex_ - 1)].volume) : 0.0;
    const double minuteShares = std::max(cumVol - prevVol, 0.0);  // 股
    QString txt = QStringLiteral("%1\n价格:%2\n均价:%3\n涨跌:%4\n涨跌幅:%5%\n分时量:%6")
        .arg(QString::fromStdString(utils::toDateTimeString(pt.time)))
        .arg(pt.price, 0, 'f', 2)
        .arg(avg, 0, 'f', 2)
        .arg(changeAbs, 0, 'f', 2)
        .arg(change, 0, 'f', 2)
        .arg(formatVolume(minuteShares));

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect box = fm.boundingRect(QRect(0, 0, 200, 200), Qt::AlignLeft | Qt::AlignTop, txt);
    box.adjust(-6, -4, 10, 4);
    box.moveTo(std::min(x + 8, static_cast<double>(width() - box.width() - 4)),
               std::max(kTitleH, static_cast<double>(kTitleH + 4)));
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(QColor("#e8e8e8"));
    p.drawRect(box);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, txt);
}

void TimelineChart::mouseMoveEvent(QMouseEvent* event) {
    mouseY_ = event->pos().y();
    if (!data_.points.empty()) {
        int idx = 0;
        double bestDist = 1e18;
        for (size_t i = 0; i < data_.points.size(); ++i) {
            double d = std::abs(xFor(minutesFromOpen(data_.points[i].time)) - event->pos().x());
            if (d < bestDist) { bestDist = d; idx = static_cast<int>(i); }
        }
        mouseIndex_ = idx;
    }
    update();
}

void TimelineChart::leaveEvent(QEvent*) {
    mouseIndex_ = -1;
    update();
}

void TimelineChart::hideEvent(QHideEvent*) {
    if (refreshTimer_) refreshTimer_->stop();  // 隐藏时停止自动刷新
}

void TimelineChart::showEvent(QShowEvent*) {
    if (refreshTimer_ && !refreshTimer_->isActive() && code_.isValid())
        refreshTimer_->start();
}

} // namespace st

#include "moc_time_line_chart.cpp"
