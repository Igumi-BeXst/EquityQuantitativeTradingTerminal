#include "ui/widgets/kline_chart.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaObject>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace st {

namespace {
// A股红涨绿跌，独立于 QSS 主题
const QColor kUpColor("#e54648");
const QColor kDownColor("#2e9e5b");

// 布局常量
constexpr double kRightAxisW = 64;
constexpr double kBottomAxisH = 26;
constexpr double kTitleH = 22;
constexpr double kPaneGap = 8;  // 面板间隙（强化分隔）
constexpr double kMargin = 8;   // 图表左右留空，避免紧贴边缘

QString periodLabel(BarPeriod p) {
    switch (p) {
        case BarPeriod::Daily:   return QStringLiteral("日线");
        case BarPeriod::Weekly:  return QStringLiteral("周线");
        case BarPeriod::Monthly: return QStringLiteral("月线");
        case BarPeriod::Minute5: return QStringLiteral("5分");
        case BarPeriod::Minute15:return QStringLiteral("15分");
        case BarPeriod::Minute30:return QStringLiteral("30分");
        case BarPeriod::Minute60:return QStringLiteral("60分");
        default:                 return QStringLiteral("日线");
    }
}
}  // namespace

KLineChart::KLineChart(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    setMouseTracking(true);
    setMinimumSize(400, 300);

    // 指标控制条（顶部），下方为绘制区
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    controlBar_ = new QWidget(this);
    auto* bar = new QHBoxLayout(controlBar_);
    bar->setContentsMargins(6, 2, 6, 2);
    bar->setSpacing(4);
    auto addToggle = [&](const QString& text, Indicator ind) {
        auto* btn = new QToolButton(controlBar_);
        btn->setText(text);
        btn->setCheckable(true);
        btn->setChecked(true);
        btn->setAutoRaise(true);
        bar->addWidget(btn);
        connect(btn, &QToolButton::clicked, this, [this, btn, ind] {
            setIndicatorVisible(ind, btn->isChecked());
        });
    };
    addToggle(tr("MA"), Indicator::Ma);
    addToggle(tr("VOL"), Indicator::Vol);
    addToggle(tr("BOLL"), Indicator::Boll);
    addToggle(tr("MACD"), Indicator::Macd);
    addToggle(tr("RSI"), Indicator::Rsi);
    bar->addStretch();
    layout->addWidget(controlBar_);
    layout->addStretch();
}

void KLineChart::setIndicatorVisible(Indicator ind, bool visible) {
    switch (ind) {
        case Indicator::Ma:   showMa_ = visible; break;
        case Indicator::Vol:  showVol_ = visible; break;
        case Indicator::Boll: showBoll_ = visible; break;
        case Indicator::Macd: showMacd_ = visible; break;
        case Indicator::Rsi:  showRsi_ = visible; break;
    }
    update();
}

bool KLineChart::isIndicatorVisible(Indicator ind) const {
    switch (ind) {
        case Indicator::Ma:   return showMa_;
        case Indicator::Vol:  return showVol_;
        case Indicator::Boll: return showBoll_;
        case Indicator::Macd: return showMacd_;
        case Indicator::Rsi:  return showRsi_;
    }
    return true;
}

// ============================================================
// 数据
// ============================================================
void KLineChart::loadStock(const StockCode& code, const QString& name) {
    const int gen = ++loadGen_;
    code_ = code;
    name_ = name;
    loading_ = true;
    update();
    if (!provider_) { loading_ = false; return; }

    ThreadPool::submitIO([this, gen, code] {
        auto bars = provider_->getBars(code, period_, DateTime{}, utils::now());
        QMetaObject::invokeMethod(this, [this, gen, bars = std::move(bars)]() mutable {
            if (gen != loadGen_) return;  // 旧请求回写丢弃
            setData(bars);
        }, Qt::QueuedConnection);
    });
}

void KLineChart::setPeriod(BarPeriod period) {
    if (period == period_) return;
    period_ = period;
    if (code_.isValid()) loadStock(code_, name_);
    emit periodChanged(period_);
}

void KLineChart::setData(const std::vector<Bar>& bars) {
    bars_ = bars;
    loading_ = false;
    firstVisible_ = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
    mouseIndex_ = -1;
    recomputeIndicators();
    update();
}

void KLineChart::recomputeIndicators() {
    ind_ = IndicatorSet{};
    if (bars_.size() < 2) return;

    std::vector<double> closes(bars_.size());
    for (size_t i = 0; i < bars_.size(); ++i) closes[i] = bars_[i].close;

    using namespace st::indicators;
    ind_.ma5  = sma(closes, 5);
    ind_.ma10 = sma(closes, 10);
    ind_.ma20 = sma(closes, 20);
    ind_.ma60 = sma(closes, 60);

    auto b = boll(closes, 20, 2.0);
    ind_.bollMid = std::move(b.mid);
    ind_.bollUpper = std::move(b.upper);
    ind_.bollLower = std::move(b.lower);

    auto m = macd(closes);
    ind_.macdDif = std::move(m.dif);
    ind_.macdDea = std::move(m.dea);
    ind_.macdHist = std::move(m.hist);

    ind_.rsi6 = rsi(closes, 6);
    ind_.rsi12 = rsi(closes, 12);
    ind_.rsi24 = rsi(closes, 24);

    ind_.valid = true;
}

// ============================================================
// 布局与坐标
// ============================================================
void KLineChart::buildLayout() {
    // 主图从标题条之下开始（plotTop 为控制条高度），左右留 margin
    const double top = plotTop() + kTitleH;
    const double plotW = width() - kRightAxisW - 2 * kMargin;
    const double plotH = height() - top - kBottomAxisH;
    const QRectF plot(kMargin, top, plotW, plotH);

    // 可见面板（顺序: VOL/BOLL/MACD/RSI），用指针引用成员矩形
    struct PaneInfo { Indicator ind; QRectF* member; };
    std::vector<PaneInfo> panes;
    if (showVol_)  panes.push_back({Indicator::Vol, &volRect_});
    if (showBoll_) panes.push_back({Indicator::Boll, &bollRect_});
    if (showMacd_) panes.push_back({Indicator::Macd, &macdRect_});
    if (showRsi_)  panes.push_back({Indicator::Rsi, &rsiRect_});

    const int paneCount = static_cast<int>(panes.size());
    double mainH, paneH;
    if (paneCount == 0) {
        mainH = plotH;
        paneH = 0;
    } else {
        mainH = plotH * 0.52;
        paneH = (plotH - mainH - paneCount * kPaneGap) / paneCount;
    }
    mainRect_ = QRectF(plot.left(), plot.top(), plot.width(), mainH);

    paneRects_.clear();
    double y = plot.top() + mainH + kPaneGap;
    for (auto& p : panes) {
        *p.member = QRectF(plot.left(), y, plot.width(), paneH);
        paneRects_.emplace_back(p.ind, *p.member);
        y += paneH + kPaneGap;
    }
}

double KLineChart::barCenterX(int index) const {
    return mainRect_.left() + (index - firstVisible_ + 0.5) * bodyWidth();
}

double KLineChart::priceToY(double price) const {
    if (priceHi_ - priceLo_ < 1e-12) return mainRect_.top();
    return mainRect_.top() + (priceHi_ - price) / (priceHi_ - priceLo_) * mainRect_.height();
}

double KLineChart::volToY(double v) const {
    return volRect_.bottom() - v / volHi_ * volRect_.height();
}

double KLineChart::bollToY(double v) const {
    if (bollHi_ - bollLo_ < 1e-12) return bollRect_.top();
    return bollRect_.top() + (bollHi_ - v) / (bollHi_ - bollLo_) * bollRect_.height();
}

double KLineChart::macdToY(double v) const {
    double mid = macdRect_.center().y();
    return mid - v / macdMaxAbs_ * (macdRect_.height() / 2.0);
}

double KLineChart::rsiToY(double v) const {
    return rsiRect_.top() + (100.0 - v) / 100.0 * rsiRect_.height();
}

void KLineChart::computeVisibleRange() {
    if (bars_.empty()) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 主图 y 范围: 蜡烛 + MA（BOLL 已独立成面板，不再参与）
    double hi = -1e18, lo = 1e18;
    for (int i = start; i < end; ++i) {
        hi = std::max(hi, bars_[static_cast<size_t>(i)].high);
        lo = std::min(lo, bars_[static_cast<size_t>(i)].low);
    }
    if (ind_.valid && showMa_) {
        for (int i = start; i < end; ++i) {
            for (double v : {ind_.ma5[static_cast<size_t>(i)],
                             ind_.ma10[static_cast<size_t>(i)],
                             ind_.ma20[static_cast<size_t>(i)],
                             ind_.ma60[static_cast<size_t>(i)]}) {
                if (std::isfinite(v)) { hi = std::max(hi, v); lo = std::min(lo, v); }
            }
        }
    }
    double pad = (hi - lo) * 0.05;
    if (pad < 1e-9) pad = 1.0;
    priceHi_ = hi + pad;
    priceLo_ = lo - pad;

    // VOL 量程（可见切片最大量）
    volHi_ = 0;
    for (int i = start; i < end; ++i) {
        volHi_ = std::max(volHi_, static_cast<double>(bars_[static_cast<size_t>(i)].volume));
    }
    if (volHi_ <= 0) volHi_ = 1;

    // BOLL 面板量程（含蜡烛 OHLC + 布林带，使蜡烛在面板内完整显示）
    bollHi_ = -1e18, bollLo_ = 1e18;
    if (ind_.valid && showBoll_) {
        for (int i = start; i < end; ++i) {
            const auto& b = bars_[static_cast<size_t>(i)];
            bollHi_ = std::max(bollHi_, b.high);
            bollLo_ = std::min(bollLo_, b.low);
            for (double v : {ind_.bollUpper[static_cast<size_t>(i)],
                             ind_.bollLower[static_cast<size_t>(i)],
                             ind_.bollMid[static_cast<size_t>(i)]}) {
                if (std::isfinite(v)) { bollHi_ = std::max(bollHi_, v); bollLo_ = std::min(bollLo_, v); }
            }
        }
    }
    if (bollHi_ < bollLo_ || !std::isfinite(bollHi_)) { bollHi_ = 1; bollLo_ = 0; }
    double bpad = (bollHi_ - bollLo_) * 0.05;
    if (bpad < 1e-9) bpad = 1.0;
    bollHi_ += bpad;
    bollLo_ -= bpad;

    // MACD 量程
    macdMaxAbs_ = 0;
    if (ind_.valid) {
        for (int i = start; i < end; ++i) {
            for (double v : {ind_.macdDif[static_cast<size_t>(i)],
                             ind_.macdDea[static_cast<size_t>(i)],
                             ind_.macdHist[static_cast<size_t>(i)]}) {
                if (std::isfinite(v)) macdMaxAbs_ = std::max(macdMaxAbs_, std::abs(v));
            }
        }
    }
    if (macdMaxAbs_ <= 0) macdMaxAbs_ = 1;
}

// ============================================================
// 绘制
// ============================================================
void KLineChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 24, 26));  // 图表底色（两主题通用深色）

    if (bars_.empty()) {
        p.setPen(QColor("#888888"));
        p.drawText(rect(), Qt::AlignCenter, loading_ ? tr("加载中…") : tr("无数据"));
        return;
    }

    buildLayout();
    computeVisibleRange();
    drawTitle(p);
    drawCandles(p);
    drawOverlayLines(p);  // MA 叠加（主图）

    // 面板背景交替着色 + 分隔
    int paneIdx = 0;
    for (const auto& [ind, rect] : paneRects_) {
        QColor bg = (paneIdx % 2 == 0) ? QColor(22, 22, 24) : QColor(26, 26, 29);
        p.fillRect(rect, bg);
        ++paneIdx;
        // 分隔线
        p.setPen(QPen(QColor(255, 255, 255, 30), 1));
        p.drawLine(QPointF(rect.left(), rect.top()), QPointF(rect.right(), rect.top()));
    }

    if (showVol_)  drawVolume(p);
    if (showBoll_) drawBoll(p);
    if (showMacd_) drawMacd(p);
    if (showRsi_)  drawRsi(p);
    drawAxes(p);
    drawCrosshair(p);
}

void KLineChart::drawCandles(QPainter& p) {
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bodyHalf = std::min(0.35 * bodyWidth(), 6.0);

    QPainterPath upWick, downWick, upBody, downBody;
    for (int i = start; i < end; ++i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        const bool rising = b.close >= b.open;
        const double cx = barCenterX(i);
        const double hiY = priceToY(b.high);
        const double loY = priceToY(b.low);
        const double openY = priceToY(b.open);
        const double closeY = priceToY(b.close);

        auto& wick = rising ? upWick : downWick;
        auto& body = rising ? upBody : downBody;
        wick.moveTo(cx, hiY);
        wick.lineTo(cx, loY);
        const double top = std::min(openY, closeY);
        const double bh = std::max(std::abs(closeY - openY), 1.0);
        body.addRect(QRectF(cx - bodyHalf, top, 2 * bodyHalf, bh));
    }
    // 影线只画线（不填充！drawPath 会用当前 brush 填充路径，线段路径填充会变色块）
    p.setPen(QPen(kUpColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(upWick);
    p.setPen(QPen(kDownColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(downWick);
    // 实体只填充
    p.fillPath(upBody, kUpColor);
    p.fillPath(downBody, kDownColor);
}

void KLineChart::drawVolume(QPainter& p) {
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bw = bodyWidth();

    QPainterPath upPath, downPath;
    for (int i = start; i < end; ++i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        const bool rising = b.close >= b.open;
        const double cx = barCenterX(i);
        const double v = static_cast<double>(b.volume);
        if (v <= 0) continue;
        const double h = volRect_.bottom() - volToY(v);
        auto& path = rising ? upPath : downPath;
        path.addRect(QRectF(cx - 0.4 * bw, volRect_.bottom() - h, 0.8 * bw, h));
    }
    p.fillPath(upPath, kUpColor);
    p.fillPath(downPath, kDownColor);

    drawPaneHeader(p, volRect_, tr("VOL"), QColor("#d4d4d4"));
}

void KLineChart::drawBoll(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 上下轨区间填充
    QPainterPath fillPath;
    bool started = false;
    for (int i = start; i < end; ++i) {
        const double up = ind_.bollUpper[static_cast<size_t>(i)];
        if (!std::isfinite(up)) { started = false; continue; }
        const double x = barCenterX(i);
        if (!started) {
            fillPath.moveTo(x, bollToY(up));
            started = true;
        } else {
            fillPath.lineTo(x, bollToY(up));
        }
    }
    for (int i = end - 1; i >= start; --i) {
        const double lo = ind_.bollLower[static_cast<size_t>(i)];
        if (!std::isfinite(lo)) continue;
        fillPath.lineTo(barCenterX(i), bollToY(lo));
    }
    fillPath.closeSubpath();
    p.fillPath(fillPath, QColor(255, 138, 101, 28));  // 浅橙半透明

    // 三条线
    auto drawLine = [&](const std::vector<double>& series, const QColor& color,
                        Qt::PenStyle style = Qt::SolidLine) {
        QPainterPath path;
        bool s = false;
        for (int i = start; i < end; ++i) {
            const double v = series[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { s = false; continue; }
            if (!s) { path.moveTo(barCenterX(i), bollToY(v)); s = true; }
            else { path.lineTo(barCenterX(i), bollToY(v)); }
        }
        QPen pen(color);
        pen.setStyle(style);
        p.setPen(pen);
        p.drawPath(path);
    };
    drawLine(ind_.bollMid, QColor("#f5f5f5"));
    drawLine(ind_.bollUpper, QColor("#ff8a65"), Qt::DashLine);
    drawLine(ind_.bollLower, QColor("#ff8a65"), Qt::DashLine);

    // 面板内叠加 K 线蜡烛（对照布林带）
    QPainterPath upWick, downWick, upBody, downBody;
    for (int i = start; i < end; ++i) {
        const auto& b = bars_[static_cast<size_t>(i)];
        const bool rising = b.close >= b.open;
        const double cx = barCenterX(i);
        const double hiY = bollToY(b.high);
        const double loY = bollToY(b.low);
        const double openY = bollToY(b.open);
        const double closeY = bollToY(b.close);
        auto& wick = rising ? upWick : downWick;
        auto& body = rising ? upBody : downBody;
        wick.moveTo(cx, hiY);
        wick.lineTo(cx, loY);
        const double top = std::min(openY, closeY);
        const double bh = std::max(std::abs(closeY - openY), 1.0);
        body.addRect(QRectF(cx - 0.35 * bodyWidth(), top, 0.7 * bodyWidth(), bh));
    }
    p.setPen(QPen(kUpColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(upWick);
    p.setPen(QPen(kDownColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(downWick);
    p.fillPath(upBody, kUpColor);
    p.fillPath(downBody, kDownColor);

    // 图例
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    const double mid = ind_.bollMid[static_cast<size_t>(idx)];
    drawPaneHeader(p, bollRect_, tr("BOLL(20,2)  %1").arg(mid, 0, 'f', 2),
                   QColor("#ff8a65"));
}

void KLineChart::drawMacd(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double bw = bodyWidth();

    // 0 轴
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(macdRect_.left(), macdRect_.center().y()),
               QPointF(macdRect_.right(), macdRect_.center().y()));

    // 柱
    QPainterPath upHist, downHist;
    for (int i = start; i < end; ++i) {
        const double h = ind_.macdHist[static_cast<size_t>(i)];
        if (!std::isfinite(h)) continue;
        const double midY = macdRect_.center().y();
        const double y = macdToY(h);
        auto& path = h >= 0 ? upHist : downHist;
        path.addRect(QRectF(barCenterX(i) - 0.4 * bw, std::min(y, midY), 0.8 * bw,
                            std::max(std::abs(y - midY), 1.0)));
    }
    p.fillPath(upHist, kUpColor);
    p.fillPath(downHist, kDownColor);

    // DIF / DEA 线
    auto drawLine = [&](const std::vector<double>& series, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = series[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), macdToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), macdToY(v)); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(ind_.macdDif, QColor("#ffffff"));
    drawLine(ind_.macdDea, QColor("#ffd54f"));

    // 图例
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    drawPaneHeader(p, macdRect_,
                   QStringLiteral("MACD(12,26,9)  DIF %1  DEA %2")
                       .arg(ind_.macdDif[static_cast<size_t>(idx)], 0, 'f', 2)
                       .arg(ind_.macdDea[static_cast<size_t>(idx)], 0, 'f', 2),
                   QColor("#d4d4d4"));
}

void KLineChart::drawRsi(QPainter& p) {
    if (!ind_.valid) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 30/70 参考线
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(rsiRect_.left(), rsiToY(30)), QPointF(rsiRect_.right(), rsiToY(30)));
    p.drawLine(QPointF(rsiRect_.left(), rsiToY(70)), QPointF(rsiRect_.right(), rsiToY(70)));

    auto drawLine = [&](const std::vector<double>& series, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = series[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), rsiToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), rsiToY(v)); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };
    drawLine(ind_.rsi6, QColor("#ffd54f"));
    drawLine(ind_.rsi12, QColor("#4dd0e1"));
    drawLine(ind_.rsi24, QColor("#ce93d8"));

    // 图例
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    drawPaneHeader(p, rsiRect_,
                   QStringLiteral("RSI  RSI6 %1  RSI12 %2  RSI24 %3")
                       .arg(ind_.rsi6[static_cast<size_t>(idx)], 0, 'f', 1)
                       .arg(ind_.rsi12[static_cast<size_t>(idx)], 0, 'f', 1)
                       .arg(ind_.rsi24[static_cast<size_t>(idx)], 0, 'f', 1),
                   QColor("#d4d4d4"));
}

void KLineChart::drawPaneHeader(QPainter& p, const QRectF& rect, const QString& title,
                                const QColor& color) {
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(title) + 10;
    QRectF chip(rect.left() + 4, rect.top() + 2, w, 16);
    p.fillRect(chip, QColor(0, 0, 0, 90));
    p.setPen(color);
    p.drawText(chip.adjusted(5, 1, -1, -1), Qt::AlignLeft | Qt::AlignVCenter, title);
}

void KLineChart::drawOverlayLines(QPainter& p) {
    if (!ind_.valid || !showMa_) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    auto drawLine = [&](const std::vector<double>& series, const QColor& color) {
        QPainterPath path;
        bool started = false;
        for (int i = start; i < end; ++i) {
            const double v = series[static_cast<size_t>(i)];
            if (!std::isfinite(v)) { started = false; continue; }
            if (!started) { path.moveTo(barCenterX(i), priceToY(v)); started = true; }
            else { path.lineTo(barCenterX(i), priceToY(v)); }
        }
        p.setPen(QPen(color, 1));
        p.drawPath(path);
    };

    drawLine(ind_.ma5, QColor("#ffd54f"));
    drawLine(ind_.ma10, QColor("#ce93d8"));
    drawLine(ind_.ma20, QColor("#4dd0e1"));
    drawLine(ind_.ma60, QColor("#90a4ae"));

    // 主图图例（取鼠标所在 bar，无鼠标取最后一根）
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    QFont lf = p.font();
    lf.setPixelSize(11);
    p.setFont(lf);
    double x = mainRect_.left() + 6;
    const double y = mainRect_.top() + 12;
    auto legend = [&](const QString& name, double v, const QColor& c) {
        const QString text = QStringLiteral("%1 %2").arg(name, QString::number(v, 'f', 2));
        p.setPen(c);
        p.drawText(QPointF(x, y), text);
        x += p.fontMetrics().horizontalAdvance(text) + 12;
    };
    legend("MA5", ind_.ma5[static_cast<size_t>(idx)], QColor("#ffd54f"));
    legend("MA10", ind_.ma10[static_cast<size_t>(idx)], QColor("#ce93d8"));
    legend("MA20", ind_.ma20[static_cast<size_t>(idx)], QColor("#4dd0e1"));
    legend("MA60", ind_.ma60[static_cast<size_t>(idx)], QColor("#90a4ae"));
}

void KLineChart::drawAxes(QPainter& p) {
    p.setPen(QColor("#888888"));
    QFont f = p.font();
    f.setPixelSize(10);
    p.setFont(f);

    // 主图价格轴（顶部=最高价，底部=最低价）
    const int ticks = 5;
    for (int i = 0; i <= ticks; ++i) {
        const double price = priceHi_ - (priceHi_ - priceLo_) * i / ticks;
        const double y = mainRect_.top() + mainRect_.height() * i / ticks;
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        p.drawLine(QPointF(mainRect_.left(), y), QPointF(mainRect_.right(), y));
        p.setPen(QColor("#888888"));
        p.drawText(QRectF(mainRect_.right() + 2, y - 8, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(price, 'f', 2));
    }

    // 各面板轴标签
    auto drawAxisLabel = [&](const QRectF& r, double v) {
        if (r.isEmpty()) return;
        p.drawText(QRectF(r.right() + 2, r.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(v, 'f', v >= 100 ? 0 : 2));
    };
    if (showVol_)  drawAxisLabel(volRect_, volHi_);
    if (showBoll_) drawAxisLabel(bollRect_, bollHi_);
    if (showMacd_) drawAxisLabel(macdRect_, macdMaxAbs_);
    if (showRsi_) {
        p.drawText(QRectF(rsiRect_.right() + 2, rsiRect_.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, "100");
    }

    // 时间轴
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double axisY = height() - kBottomAxisH;
    p.setPen(QColor("#888888"));
    const auto& firstBar = bars_[static_cast<size_t>(start)];
    const auto& lastBar = bars_[static_cast<size_t>(end - 1)];
    const auto& midBar = bars_[static_cast<size_t>(start + (end - start) / 2)];
    const auto fmt = [&](const Bar& b) {
        return QString::fromStdString(utils::toDateString(b.time));
    };
    p.drawText(QRectF(mainRect_.left(), axisY + 4, 90, 16), Qt::AlignLeft, fmt(firstBar));
    p.drawText(QRectF(mainRect_.center().x() - 45, axisY + 4, 90, 16), Qt::AlignCenter, fmt(midBar));
    p.drawText(QRectF(mainRect_.right() - 90, axisY + 4, 90, 16), Qt::AlignRight, fmt(lastBar));
}

void KLineChart::drawTitle(QPainter& p) {
    p.setPen(QColor("#d4d4d4"));
    QFont f = p.font();
    f.setPixelSize(12);
    f.setBold(true);
    p.setFont(f);

    QString text = name_ + "  " + QString::fromStdString(code_.displayCode());
    if (!bars_.empty()) {
        const auto& b = bars_.back();
        // 涨跌幅 vs 前一根收盘（Bar 无 preClose 字段，图表用前收作昨收）
        const double prevClose = bars_.size() >= 2 ? bars_[bars_.size() - 2].close : 0.0;
        const double change = prevClose > 0 ? (b.close - prevClose) / prevClose * 100.0 : 0.0;
        QColor c = change >= 0 ? kUpColor : kDownColor;
        text += QStringLiteral("  [%1]  %2").arg(periodLabel(period_),
                                                 QString::number(b.close, 'f', 2));
        p.setPen(c);
        text += QStringLiteral("  %1%").arg(change, 0, 'f', 2);
    }
    if (loading_) text += tr("  (加载中…)");
    p.drawText(QRectF(kMargin, plotTop() + 2, width() - kMargin - 8, kTitleH - 2),
               Qt::AlignLeft | Qt::AlignVCenter, text);
}

void KLineChart::drawCrosshair(QPainter& p) {
    if (mouseIndex_ < 0 || mouseIndex_ >= static_cast<int>(bars_.size())) return;
    const double cx = barCenterX(mouseIndex_);
    const double plotBottom = (height() - kBottomAxisH);

    p.setPen(QPen(QColor(200, 200, 200, 160), 1, Qt::DashLine));
    p.drawLine(QPointF(cx, mainRect_.top()), QPointF(cx, plotBottom));
    p.drawLine(QPointF(mainRect_.left(), mouseY_), QPointF(mainRect_.right(), mouseY_));

    // 浮框
    const auto& b = bars_[static_cast<size_t>(mouseIndex_)];
    const double prevClose = mouseIndex_ >= 1
        ? bars_[static_cast<size_t>(mouseIndex_ - 1)].close : 0.0;
    const double change = prevClose > 0 ? (b.close - prevClose) / prevClose * 100.0 : 0.0;
    QString txt = QStringLiteral("%1\n开:%2 高:%3\n低:%4 收:%5\n涨跌幅:%6% 量:%7")
        .arg(QString::fromStdString(utils::toDateString(b.time)))
        .arg(b.open, 0, 'f', 2).arg(b.high, 0, 'f', 2)
        .arg(b.low, 0, 'f', 2).arg(b.close, 0, 'f', 2)
        .arg(change, 0, 'f', 2)
        .arg(static_cast<qlonglong>(b.volume));
    if (ind_.valid) {
        txt += QStringLiteral("\nMA5:%1 MA10:%2 MA20:%3")
                   .arg(ind_.ma5[static_cast<size_t>(mouseIndex_)], 0, 'f', 2)
                   .arg(ind_.ma10[static_cast<size_t>(mouseIndex_)], 0, 'f', 2)
                   .arg(ind_.ma20[static_cast<size_t>(mouseIndex_)], 0, 'f', 2);
    }

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect box = fm.boundingRect(QRect(0, 0, 200, 200), Qt::AlignLeft | Qt::AlignTop, txt);
    box.adjust(-6, -4, 10, 4);
    box.moveTo(std::min(cx + 8, static_cast<double>(width() - box.width() - 4)),
               std::max(plotTop(), std::min(mouseY_ - box.height() / 2,
                                            static_cast<double>(height() - box.height() - 4))));
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(QColor("#e8e8e8"));
    p.drawRect(box);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, txt);
}

// ============================================================
// 交互
// ============================================================
void KLineChart::mouseMoveEvent(QMouseEvent* event) {
    if (event->pos().y() < plotTop()) return;  // 控制条区域忽略
    if (dragging_ && !bars_.empty()) {
        const int dx = dragStartX_ - event->pos().x();
        const int maxFirst = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
        firstVisible_ = std::clamp(dragStartFirst_ + static_cast<int>(dx / bodyWidth()),
                                   0, maxFirst);
    }
    mouseY_ = event->pos().y();
    if (!bars_.empty()) {
        int idx = firstVisible_ + static_cast<int>(
            (event->pos().x() - mainRect_.left()) / bodyWidth());
        idx = std::clamp(idx, firstVisible_,
                         std::min(firstVisible_ + visibleCount_ - 1,
                                  static_cast<int>(bars_.size()) - 1));
        mouseIndex_ = idx;
    }
    update();
}

void KLineChart::mousePressEvent(QMouseEvent* event) {
    if (event->pos().y() < plotTop()) return;
    if (event->button() == Qt::LeftButton && !bars_.empty()) {
        dragging_ = true;
        dragStartX_ = event->pos().x();
        dragStartFirst_ = firstVisible_;
        setCursor(Qt::ClosedHandCursor);
    }
}

void KLineChart::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && dragging_) {
        dragging_ = false;
        unsetCursor();
    }
}

void KLineChart::wheelEvent(QWheelEvent* event) {
    if (event->position().y() < plotTop() || bars_.empty()) return;
    const double factor = event->angleDelta().y() > 0 ? 0.9 : 1.1;
    const int newCount = std::clamp(static_cast<int>(std::round(visibleCount_ * factor)),
                                    20, 800);
    const double ratio = std::clamp(
        (event->position().x() - mainRect_.left()) / std::max(mainRect_.width(), 1.0), 0.0, 1.0);
    const int anchorIdx = firstVisible_ + static_cast<int>(ratio * visibleCount_);
    const int maxFirst = std::max(0, static_cast<int>(bars_.size()) - newCount);
    firstVisible_ = std::clamp(anchorIdx - static_cast<int>(ratio * newCount), 0, maxFirst);
    visibleCount_ = newCount;
    update();
    event->accept();
}

void KLineChart::leaveEvent(QEvent*) {
    mouseIndex_ = -1;
    update();
}

} // namespace st

#include "moc_kline_chart.cpp"
