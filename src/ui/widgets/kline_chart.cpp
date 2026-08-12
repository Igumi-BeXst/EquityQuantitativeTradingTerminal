#include "ui/widgets/kline_chart.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "core/app_paths.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include "foundation/utils/csv.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMetaObject>
#include <QPointer>
#include <QToolButton>
#include <QPushButton>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <optional>
#include <utility>

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

// 股 → 手显示：≥1亿用亿、≥1万用万、否则手数（不显示"手"后缀，对齐参照软件）
QString formatVolume(double shares) {
    const double hands = shares / 100.0;
    if (hands >= 1e8) return QStringLiteral("%1亿").arg(hands / 1e8, 0, 'f', 2);
    if (hands >= 1e4) return QStringLiteral("%1万").arg(hands / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(hands, 0, 'f', 0);
}

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

/// 两 DateTime 是否同一天（本地时区年/月/日）
bool isSameDate(DateTime a, DateTime b) {
    const std::time_t ta = std::chrono::system_clock::to_time_t(a);
    const std::time_t tb = std::chrono::system_clock::to_time_t(b);
    std::tm ma{}, mb{};
#ifdef _WIN32
    localtime_s(&ma, &ta); localtime_s(&mb, &tb);
#else
    localtime_r(&ta, &ma); localtime_r(&tb, &mb);
#endif
    return ma.tm_year == mb.tm_year && ma.tm_mon == mb.tm_mon && ma.tm_mday == mb.tm_mday;
}
}  // namespace

KLineChart::KLineChart(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      sectorProvider_(std::make_shared<EastMoneySectorProvider>()) {
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
    bar->addStretch();  // 指标开关靠左，工具组右对齐（图表右上角）

    // 画线/导出工具组：显眼样式（亮字 + 边框 + 激活金色高亮），与指标开关区分
    const QString toolBtnStyle = QStringLiteral(
        "QPushButton { color:#e6e6e6; border:1px solid #6a6a6a; border-radius:3px;"
        "  padding:2px 8px; background:#2a2a2c; }"
        "QPushButton:hover { background:#3d3d40; border-color:#999999; }"
        "QPushButton:checked { color:#ffd700; border-color:#ffd700;"
        "  background:#3a3220; font-weight:bold; }");
    auto* sep = new QFrame(controlBar_);
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet(QStringLiteral("color:#555555;"));
    bar->addWidget(sep);
    bar->addSpacing(4);

    auto* drawGroup = new QButtonGroup(this);
    drawGroup->setExclusive(false);
    const auto addDrawToggle = [&](const QString& text, DrawMode mode) {
        auto* btn = new QPushButton(text, controlBar_);
        btn->setCheckable(true);
        btn->setStyleSheet(toolBtnStyle);
        drawGroup->addButton(btn);
        bar->addWidget(btn);
        connect(btn, &QPushButton::clicked, this, [this, btn, drawGroup, mode] {
            if (btn->isChecked()) {
                for (auto* b : drawGroup->buttons()) {
                    if (b != btn) b->setChecked(false);
                }
                setDrawMode(mode);
            } else {
                setDrawMode(DrawMode::None);
            }
        });
    };
    addDrawToggle(tr("水平线"), DrawMode::Horizontal);
    addDrawToggle(tr("趋势线"), DrawMode::Trend);
    addDrawToggle(tr("区间统计"), DrawMode::Range);

    auto* clearBtn = new QPushButton(tr("清除标注"), controlBar_);
    clearBtn->setStyleSheet(toolBtnStyle);
    bar->addWidget(clearBtn);
    connect(clearBtn, &QPushButton::clicked, this, &KLineChart::clearAnnotations);

    auto* exportBtn = new QPushButton(tr("导出"), controlBar_);
    exportBtn->setStyleSheet(toolBtnStyle);
    bar->addWidget(exportBtn);
    connect(exportBtn, &QPushButton::clicked, this, &KLineChart::exportData);

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
        case Indicator::RelativeStrength: showRelativeStrength_ = visible; break;
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
        case Indicator::RelativeStrength: return showRelativeStrength_;
    }
    return true;
}

void KLineChart::setDrawMode(DrawMode mode) {
    if (drawMode_ == DrawMode::Range && mode != DrawMode::Range) {
        rangeFrom_ = rangeTo_ = -1;   // 退出区间模式（切其他工具/取消选中）清选区
        rangeDragStart_ = -1;
        rangeDragging_ = false;
    }
    drawMode_ = mode;
    drawing_ = false;
    setCursor(mode != DrawMode::None ? Qt::CrossCursor : Qt::ArrowCursor);
    update();
}

void KLineChart::clearAnnotations() {
    lines_.clear();
    rangeFrom_ = rangeTo_ = -1;
    rangeDragStart_ = -1;
    rangeDragging_ = false;
    update();
}

void KLineChart::exportData() {
    if (bars_.empty()) return;
    const QString defaultPath = QString::fromStdString(AppPaths::dataDir() + "/kline.csv");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出 K 线数据"), defaultPath, tr("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LogManager::instance()->log(LogLevel::Warn, "K线导出失败: {}", path.toStdString());
        return;
    }
    const std::string csv = csv::klineToCsv(bars_);
    file.write(csv.c_str(), static_cast<qint64>(csv.size()));
    LogManager::instance()->log(LogLevel::Info, "已导出 K 线数据 {} 根: {}", bars_.size(),
                                path.toStdString());
}

int KLineChart::indexAtX(double x) const {
    if (bars_.empty()) return -1;
    int idx = firstVisible_ + static_cast<int>((x - mainRect_.left()) / bodyWidth());
    return std::clamp(idx, 0, static_cast<int>(bars_.size()) - 1);
}

double KLineChart::priceFromY(double y) const {
    if (priceHi_ - priceLo_ < 1e-12) return priceHi_;
    return priceHi_ - (y - mainRect_.top()) / mainRect_.height() *
                          (priceHi_ - priceLo_);
}

void KLineChart::drawAnnotations(QPainter& p) {
    const bool preview = drawing_ && drawMode_ == DrawMode::Trend;
    if (lines_.empty() && !preview) return;
    p.setPen(QPen(QColor("#ffd700"), 1, Qt::DashLine));
    for (const auto& line : lines_) {
        if (line.horizontal) {
            const double y = priceToY(line.price1);
            if (y < mainRect_.top() - 12 || y > mainRect_.bottom() + 12) continue;
            p.drawLine(QPointF(mainRect_.left(), y), QPointF(mainRect_.right(), y));
            p.drawText(QPointF(mainRect_.right() - 44, y - 4),
                       QString::number(line.price1, 'f', 2));
        } else {
            p.drawLine(QPointF(barCenterX(line.idx1), priceToY(line.price1)),
                       QPointF(barCenterX(line.idx2), priceToY(line.price2)));
        }
    }
    // 趋势预览：起点 → 当前光标（2px 实线，拖拽时清楚跟随）
    if (preview && dragStartIdx_ >= 0) {
        p.setPen(QPen(QColor("#ffd700"), 2));
        p.drawLine(QPointF(barCenterX(dragStartIdx_), priceToY(dragStartPrice_)),
                   QPointF(mouseX_, priceFromY(mouseY_)));
        p.setPen(QPen(QColor("#ffd700"), 1, Qt::DashLine));  // 恢复已存线的虚线笔
    }
}

void KLineChart::drawRangeSelection(QPainter& p) {
    if (rangeFrom_ < 0 || rangeTo_ < 0 || bars_.empty()) return;
    const int from = std::clamp(rangeFrom_, 0, static_cast<int>(bars_.size()) - 1);
    const int to = std::clamp(rangeTo_, 0, static_cast<int>(bars_.size()) - 1);
    const double x1 = barCenterX(from) - bodyWidth() / 2;
    const double x2 = barCenterX(to) + bodyWidth() / 2;
    p.fillRect(QRectF(QPointF(x1, mainRect_.top()), QPointF(x2, mainRect_.bottom())),
               QColor(255, 255, 255, 18));   // 半透明高亮
    p.setPen(QPen(QColor("#ffd700"), 1, Qt::DashLine));
    p.drawLine(QPointF(x1, mainRect_.top()), QPointF(x1, mainRect_.bottom()));
    p.drawLine(QPointF(x2, mainRect_.top()), QPointF(x2, mainRect_.bottom()));
    p.setPen(QColor("#d4d4d4"));   // 首末日期
    p.drawText(QPointF(x1 + 2, mainRect_.top() + 12),
               QString::fromStdString(
                   utils::toDateString(bars_[static_cast<size_t>(from)].time)));
    p.drawText(QPointF(x2 - 90, mainRect_.top() + 12),
               QString::fromStdString(
                   utils::toDateString(bars_[static_cast<size_t>(to)].time)));
}

// ============================================================
// 数据
// ============================================================
void KLineChart::loadStock(const StockCode& code, const QString& name) {
    // 切股清交易标记；切周期/重载同代码保留（setData 后按新周期重对齐）
    if (code != code_) {
        tradeMarks_.clear();
        holdings_.clear();
        markBarIndex_.clear();
    }
    const int gen = ++loadGen_;
    code_ = code;
    name_ = name;
    loading_ = true;
    lines_.clear();   // 切股不带旧标注
    rangeFrom_ = rangeTo_ = -1;
    rangeDragStart_ = -1;
    rangeDragging_ = false;
    drawMode_ = DrawMode::None;
    drawing_ = false;
    ++overlayGen_;            // 作废在途叠加请求
    overlayActive_ = false;   // 切股清叠加（按视图隔离）
    overlayRows_.clear();
    update();
    // 外部数据模式（自定义指数等）：由拥有者重算后经 loadBars 喂入
    if (externalReloader_) { externalReloader_(); return; }
    if (!provider_) { loading_ = false; return; }

    // 安全异步：捕获 provider 按值 + QPointer 守卫（widget 销毁后自动跳过，避免悬垂 this）
    const BarPeriod period = period_;
    IDataProvider* provider = provider_;
    QPointer<KLineChart> guard(this);
    ThreadPool::submitIO([provider, guard, gen, code, period] {
        auto bars = provider->getBars(code, period, DateTime{}, utils::now());
        QMetaObject::invokeMethod(guard, [guard, gen, bars = std::move(bars)]() mutable {
            if (gen != guard->loadGen_) return;  // 旧请求回写丢弃
            guard->setData(bars);
        }, Qt::QueuedConnection);
    });
}

void KLineChart::setPeriod(BarPeriod period) {
    if (period == period_) return;
    period_ = period;
    if (code_.isValid()) {
        // 外部数据模式：由拥有者按新周期重算（不做 provider 拉取，也不保留叠加——重算后重设）
        if (externalReloader_) {
            externalReloader_();
        } else {
            // 周期切换保留叠加（同图日/周/月）：loadStock 会清叠加，先存后重设重取
            const bool saved = overlayActive_;
            const OverlayTarget target = overlayTarget_;
            const bool showRs = showRelativeStrength_;
            loadStock(code_, name_);
            if (saved) setOverlay(target, showRs);
        }
    }
    emit periodChanged(period_);
}

void KLineChart::setExternalReloader(std::function<void()> reloadFn) {
    externalReloader_ = std::move(reloadFn);
}

void KLineChart::loadBars(const std::vector<Bar>& bars, const StockCode& code,
                          const QString& name) {
    // 切股清交易标记；同代码重载（自定义指数刷新）保留
    if (code != code_) {
        tradeMarks_.clear();
        holdings_.clear();
        markBarIndex_.clear();
    }
    code_ = code;
    name_ = name;
    lines_.clear();
    rangeFrom_ = rangeTo_ = -1;
    rangeDragStart_ = -1;
    rangeDragging_ = false;
    drawMode_ = DrawMode::None;
    drawing_ = false;
    ++overlayGen_;
    overlayActive_ = false;
    overlayRows_.clear();
    setData(bars);
}

void KLineChart::setData(const std::vector<Bar>& bars) {
    bars_ = bars;
    loading_ = false;
    firstVisible_ = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
    mouseIndex_ = -1;
    lastEmittedDate_.reset();
    emit crosshairDateChanged(std::nullopt);  // 数据重载 → 外部面板回退最新
    recomputeIndicators();
    buildMarkBarIndex();  // bars 更新后重对齐交易标记（周期/股票切换后）
    update();
}

void KLineChart::setTradeMarks(const std::vector<TradeMark>& marks,
                               const std::vector<HoldingLine>& holdings) {
    tradeMarks_ = marks;
    holdings_ = holdings;
    buildMarkBarIndex();
    update();
}

void KLineChart::buildMarkBarIndex() {
    markBarIndex_.assign(tradeMarks_.size(), -1);
    if (bars_.empty() || tradeMarks_.empty()) return;
    size_t j = 0;
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        const DateTime mt = tradeMarks_[i].time;
        while (j < bars_.size() && bars_[j].time < mt) ++j;   // 第一个 bar.time >= mt
        if (j >= bars_.size()) {
            // 晚于最后一根 bar：归属最后一根（末周期含其后全部；TDX bar.time=周期首日 15:00）
            markBarIndex_[i] = (j > 0 && bars_[j - 1].time < mt)
                ? static_cast<int>(j - 1) : -1;
            continue;
        }
        if (isSameDate(bars_[j].time, mt)) {                   // 日线/当日
            markBarIndex_[i] = static_cast<int>(j);
        } else if (j > 0 && bars_[j - 1].time < mt) {          // 周/月：归属前一根
            markBarIndex_[i] = static_cast<int>(j - 1);
        } else {
            markBarIndex_[i] = -1;                             // 数据范围外（早于最早 bar）
        }
    }
}

// ============================================================
// 叠加对比（指数/个股/板块/概念；按视图隔离：只叠加本图日/周/月）
// ============================================================
void KLineChart::setOverlay(const OverlayTarget& target, bool showRelativeStrength) {
    if (target.kind == OverlayKind::Security && target.stockCode == code_) return;  // 自我叠加拒绝
    overlayTarget_ = target;
    showRelativeStrength_ = showRelativeStrength;
    fetchOverlayData();
}

void KLineChart::clearOverlay() {
    ++overlayGen_;
    overlayActive_ = false;
    overlayRows_.clear();
    showRelativeStrength_ = false;
    rsHi_ = rsLo_ = 100;
    update();
}

void KLineChart::fetchOverlayData() {
    if (!provider_ || !overlayTarget_.isValid()) return;
    const int gen = ++overlayGen_;
    overlayActive_ = true;
    const BarPeriod period = period_;
    const OverlayTarget target = overlayTarget_;
    IDataProvider* provider = provider_;
    auto sectorProvider = sectorProvider_;
    QPointer<KLineChart> guard(this);
    ThreadPool::submitIO([provider, sectorProvider, guard, gen, target, period] {
        std::vector<Bar> ovBars;
        if (target.kind == OverlayKind::Sector) {
            if (isTdxSectorCode(target.sectorCode)) {
                // 通达信板块指数 → 走 TDX 主源（isIndex 解码已支持 880/885），不受东财封锁影响
                ovBars = provider->getBars(StockCode(Market::SH, target.sectorCode), period,
                                           DateTime{}, utils::now());
            } else {
                // 东财板块（BKxxxx / 新浪 new_xxx 经 suggest 解析）
                ovBars = sectorProvider->fetchSectorKline(target.sectorType, target.sectorCode,
                                                          target.name.toStdString(), period);
            }
        } else {
            ovBars = provider->getBars(target.stockCode, period, DateTime{}, utils::now());
        }
        QMetaObject::invokeMethod(guard, [guard, gen, ovBars = std::move(ovBars)]() mutable {
            if (gen != guard->overlayGen_) return;  // 旧请求回写丢弃
            auto rows = alignOverlay(guard->bars_, ovBars);
            int matched = 0;
            for (const auto& r : rows) if (r.matched) ++matched;
            LogManager::instance()->log(LogLevel::Info,
                "叠加 {} ({}): 拉取 {} 根, 对齐 {} 行 / 匹配 {}",
                guard->overlayTarget_.name.toStdString(),
                guard->overlayTarget_.sectorCode,
                ovBars.size(), rows.size(), matched);
            guard->overlayRows_ = std::move(rows);
            guard->update();
        }, Qt::QueuedConnection);
    });
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
    if (overlayActive_ && showRelativeStrength_) {
        panes.push_back({Indicator::RelativeStrength, &rsRect_});
    }

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

    // 归一化叠加线量程：纳入主图范围，保证叠线可见（锚点 = 可见区首个 matched，每帧重算）
    overlayAnchor_ = -1;
    if (overlayActive_ && overlayRows_.size() == bars_.size()) {
        for (int i = start; i < end; ++i) {
            if (overlayRows_[static_cast<size_t>(i)].matched) { overlayAnchor_ = i; break; }
        }
        if (overlayAnchor_ >= 0) {
            const double baseClose = bars_[static_cast<size_t>(overlayAnchor_)].close;
            const double ovAnchor = overlayRows_[static_cast<size_t>(overlayAnchor_)].overlayClose;
            if (baseClose > 0 && ovAnchor > 0) {
                for (int i = start; i < end; ++i) {
                    const auto& row = overlayRows_[static_cast<size_t>(i)];
                    if (!row.matched) continue;
                    // 叠加蜡烛的缩放后高低价纳入主图量程，保证蜡烛可见
                    const double vh = baseClose * row.overlayHigh / ovAnchor;
                    const double vl = baseClose * row.overlayLow / ovAnchor;
                    if (std::isfinite(vh)) hi = std::max(hi, vh);
                    if (std::isfinite(vl)) lo = std::min(lo, vl);
                }
            }
        }
    }

    // 交易标记成本线纳入量程（模拟青 / 实盘橙）
    for (const auto& h : holdings_) {
        if (h.quantity > 0 && h.avgCost > 0) {
            hi = std::max(hi, h.avgCost);
            lo = std::min(lo, h.avgCost);
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

    // 相对强弱面板量程（锚点=100；无有效数据时保持 100 范围）
    rsHi_ = 100, rsLo_ = 100;
    if (overlayActive_ && showRelativeStrength_ && overlayAnchor_ >= 0 &&
        overlayRows_.size() == bars_.size()) {
        const double rsAnchor = overlayRows_[static_cast<size_t>(overlayAnchor_)].relativeStrength;
        if (rsAnchor > 0) {
            for (int i = start; i < end; ++i) {
                const auto& row = overlayRows_[static_cast<size_t>(i)];
                if (!row.matched || row.relativeStrength <= 0) continue;
                const double v = row.relativeStrength / rsAnchor * 100.0;
                if (std::isfinite(v)) { rsHi_ = std::max(rsHi_, v); rsLo_ = std::min(rsLo_, v); }
            }
        }
        double rpad = (rsHi_ - rsLo_) * 0.05;
        if (rpad < 1e-9) rpad = 1.0;
        rsHi_ += rpad;
        rsLo_ -= rpad;
    }
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
    if (overlayActive_) drawRebasedOverlay(p);  // 归一化叠加线（独立函数，不受 MA 开关影响）

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
    if (overlayActive_ && showRelativeStrength_) drawRelativeStrength(p);
    drawAxes(p);
    drawTradeMarks(p);   // 交易标记（成本线 + 买卖箭头）
    drawAnnotations(p);  // 画线标注（十字线之下）
    drawRangeSelection(p);
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

    drawPaneHeader(p, volRect_, tr("成交量"), QColor("#d4d4d4"));
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

void KLineChart::drawRebasedOverlay(QPainter& p) {
    if (!overlayActive_ || overlayAnchor_ < 0) return;
    if (overlayRows_.size() != bars_.size()) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const auto& anchorRow = overlayRows_[static_cast<size_t>(overlayAnchor_)];
    const double baseClose = bars_[static_cast<size_t>(overlayAnchor_)].close;
    if (anchorRow.overlayClose <= 0 || baseClose <= 0) return;

    // 叠加 K 线蜡烛：缩放因子使叠加标锚点收盘对齐 base 锚点收盘，OHLC 等比缩放。
    // 空心蜡烛 + 按涨跌分色（橙涨/青跌，区别于 base 红涨绿跌；空心与 base 实心区分）。
    const QColor kOverlayUp("#ff9800");    // 叠加涨：橙
    const QColor kOverlayDown("#00b8d4");  // 叠加跌：青
    const double scale = baseClose / anchorRow.overlayClose;
    const double bodyHalf = std::min(0.35 * bodyWidth(), 6.0);
    QPainterPath upWick, downWick;
    for (int i = start; i < end; ++i) {
        const auto& row = overlayRows_[static_cast<size_t>(i)];
        if (!row.matched) continue;
        const double o = scale * row.overlayOpen;
        const double h = scale * row.overlayHigh;
        const double l = scale * row.overlayLow;
        const double c = scale * row.overlayClose;
        if (!std::isfinite(h) || !std::isfinite(l) || h < l || h <= 0) continue;
        const bool rising = c >= o;
        const QColor& col = rising ? kOverlayUp : kOverlayDown;
        const double cx = barCenterX(i);
        auto& wick = rising ? upWick : downWick;
        wick.moveTo(cx, priceToY(h));
        wick.lineTo(cx, priceToY(l));
        // 空心蜡烛：仅描边，不填充
        const double top = std::min(priceToY(o), priceToY(c));
        const double bh = std::max(std::abs(priceToY(c) - priceToY(o)), 1.0);
        p.setPen(QPen(col, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(cx - bodyHalf, top, 2 * bodyHalf, bh));
    }
    p.setPen(QPen(kOverlayUp, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(upWick);
    p.setPen(QPen(kOverlayDown, 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(downWick);

    // 图例（MA 图例下方第二行，避开一行重叠；颜色跟随最新叠加蜡烛方向）
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    double v = 0.0;
    QColor legendCol = QColor("#ff9800");
    if (idx >= 0 && idx < static_cast<int>(overlayRows_.size()) &&
        overlayRows_[static_cast<size_t>(idx)].matched) {
        v = baseClose * overlayRows_[static_cast<size_t>(idx)].overlayClose /
            anchorRow.overlayClose;
        const bool rising = overlayRows_[static_cast<size_t>(idx)].overlayClose >=
                            overlayRows_[static_cast<size_t>(idx)].overlayOpen;
        legendCol = rising ? QColor("#ff9800") : QColor("#00b8d4");
    }
    QFont lf = p.font();
    lf.setPixelSize(11);
    p.setFont(lf);
    p.setPen(legendCol);
    p.drawText(QPointF(mainRect_.left() + 6, mainRect_.top() + 26),
               idx >= 0 ? QStringLiteral("%1  %2")
                             .arg(overlayTarget_.name, QString::number(v, 'f', 2))
                        : overlayTarget_.name);
}

double KLineChart::rsToY(double v) const {
    if (rsHi_ - rsLo_ < 1e-12) return rsRect_.top();
    return rsRect_.top() + (rsHi_ - v) / (rsHi_ - rsLo_) * rsRect_.height();
}

void KLineChart::drawRelativeStrength(QPainter& p) {
    if (!overlayActive_ || !showRelativeStrength_) return;
    if (overlayAnchor_ < 0 || overlayRows_.size() != bars_.size()) return;
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));
    const double rsAnchor = overlayRows_[static_cast<size_t>(overlayAnchor_)].relativeStrength;
    if (rsAnchor <= 0) return;

    // 100 参考线（锚点处比值）
    p.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
    p.drawLine(QPointF(rsRect_.left(), rsToY(100.0)),
               QPointF(rsRect_.right(), rsToY(100.0)));

    // 相对强弱曲线（锚点=100）
    QPainterPath path;
    bool started = false;
    for (int i = start; i < end; ++i) {
        const auto& row = overlayRows_[static_cast<size_t>(i)];
        if (!row.matched || row.relativeStrength <= 0) { started = false; continue; }
        const double v = row.relativeStrength / rsAnchor * 100.0;
        if (!std::isfinite(v)) { started = false; continue; }
        const double x = barCenterX(i);
        const double y = rsToY(v);
        if (!started) { path.moveTo(x, y); started = true; }
        else { path.lineTo(x, y); }
    }
    p.setPen(QPen(QColor("#ff9800"), 1));
    p.drawPath(path);

    // 图例（当前值）
    const int idx = (mouseIndex_ >= 0) ? mouseIndex_ : static_cast<int>(bars_.size()) - 1;
    double v = 0.0;
    if (idx >= 0 && idx < static_cast<int>(overlayRows_.size()) &&
        overlayRows_[static_cast<size_t>(idx)].matched &&
        overlayRows_[static_cast<size_t>(idx)].relativeStrength > 0) {
        v = overlayRows_[static_cast<size_t>(idx)].relativeStrength / rsAnchor * 100.0;
    }
    drawPaneHeader(p, rsRect_,
                   QStringLiteral("相对强弱 %1").arg(v, 0, 'f', 1), QColor("#ff9800"));
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
    if (showVol_) {
        p.drawText(QRectF(volRect_.right() + 2, volRect_.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, formatVolume(volHi_));
    }
    if (showBoll_) drawAxisLabel(bollRect_, bollHi_);
    if (showMacd_) drawAxisLabel(macdRect_, macdMaxAbs_);
    if (showRsi_) {
        p.drawText(QRectF(rsiRect_.right() + 2, rsiRect_.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, "100");
    }
    if (overlayActive_ && showRelativeStrength_) {
        p.drawText(QRectF(rsRect_.right() + 2, rsRect_.top() + 2, kRightAxisW - 4, 14),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::number(rsHi_, 'f', 1));
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
    if (overlayActive_ && !overlayTarget_.name.isEmpty()) {
        text += QStringLiteral("  对比 ") + overlayTarget_.name;
    }
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

    // 浮框（每项一行，不含 MA）
    const auto& b = bars_[static_cast<size_t>(mouseIndex_)];
    const double prevClose = mouseIndex_ >= 1
        ? bars_[static_cast<size_t>(mouseIndex_ - 1)].close : 0.0;
    const double change = prevClose > 0 ? (b.close - prevClose) / prevClose * 100.0 : 0.0;
    const double changeAbs = prevClose > 0 ? (b.close - prevClose) : 0.0;
    QString txt = QStringLiteral("%1\n开:%2\n高:%3\n低:%4\n收:%5\n涨跌:%6\n涨跌幅:%7%\n量:%8")
        .arg(QString::fromStdString(utils::toDateString(b.time)))
        .arg(b.open, 0, 'f', 2).arg(b.high, 0, 'f', 2)
        .arg(b.low, 0, 'f', 2).arg(b.close, 0, 'f', 2)
        .arg(changeAbs, 0, 'f', 2)
        .arg(change, 0, 'f', 2)
        .arg(formatVolume(b.volume));

    // 悬停 K 线若有交易标记 → 浮框追加交易行
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        if (markBarIndex_[i] != mouseIndex_) continue;
        const auto& m = tradeMarks_[i];
        const QString dir = (m.direction == Direction::Buy) ? tr("买") : tr("卖");
        const QString typ = (m.type == JournalType::AutoTrade) ? tr("模拟") : tr("实盘");
        txt += QStringLiteral("\n%1%2 %3 %4 @ %5")
            .arg(typ).arg(dir)
            .arg(static_cast<qint64>(m.volume))
            .arg(QString::fromStdString(m.name))
            .arg(m.price, 0, 'f', 2);
        if (!m.strategy.empty())
            txt += QStringLiteral(" [%1]").arg(QString::fromStdString(m.strategy));
    }

    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    QFontMetrics fm(f);
    QRect box = fm.boundingRect(QRect(0, 0, 200, 200), Qt::AlignLeft | Qt::AlignTop, txt);
    box.adjust(-6, -4, 10, 4);
    // 信息框固定在最上方（与分时图一致），水平仍跟随鼠标
    box.moveTo(std::min(cx + 8, static_cast<double>(width() - box.width() - 4)),
               mainRect_.top() + 4);
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(QColor("#e8e8e8"));
    p.drawRect(box);
    p.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignLeft | Qt::AlignTop, txt);
}

void KLineChart::drawTradeMarks(QPainter& p) {
    if (bars_.empty()) return;
    p.save();   // 保护画笔状态：本函数改动 pen/brush/font，返回前 restore 恢复，避免污染后续绘制
    const int start = firstVisible_;
    const int end = std::min(start + visibleCount_, static_cast<int>(bars_.size()));

    // 持仓成本线（模拟青 #00e5ff / 实盘橙 #ff9800）
    QFont f = p.font();
    f.setPixelSize(11);
    p.setFont(f);
    const QColor kSimColor("#00e5ff");
    const QColor kManualColor("#ff9800");
    for (const auto& h : holdings_) {
        if (h.quantity <= 0 || h.avgCost <= 0) continue;
        const double y = priceToY(h.avgCost);
        if (y < mainRect_.top() - 12 || y > mainRect_.bottom() + 12) continue;
        const QColor color = (h.type == JournalType::AutoTrade) ? kSimColor : kManualColor;
        p.setPen(QPen(color, 1, Qt::DashLine));
        p.drawLine(QPointF(mainRect_.left(), y), QPointF(mainRect_.right(), y));
        const QString label = QStringLiteral("[%1] %2 @ %3")
            .arg(h.type == JournalType::AutoTrade ? tr("模拟") : tr("实盘"))
            .arg(static_cast<qint64>(h.quantity))
            .arg(h.avgCost, 0, 'f', 2);
        const int tw = QFontMetrics(f).horizontalAdvance(label);
        p.drawText(QPointF(mainRect_.right() - tw - 4, y - 4), label);
    }

    // 买卖箭头（红 ▲ 买 / 绿 ▼ 卖），按 markBarIndex_
    // 箭头锚定在 K 线高低点之外（买在 bar 下方、卖在 bar 上方），不遮挡蜡烛实体
    // 同一天（同一 bar）既有买又有卖的 → 只标 "T"（不画箭头，悬停浮框仍显示买卖明细）
    const double bw = bodyWidth();
    const double bodyHalf = std::min(0.35 * bw, 6.0);
    const QColor kBuyColor("#ff5252");   // 红买
    const QColor kSellColor("#00e676");  // 绿卖
    const double kArrowOffset = bodyHalf * 2.2;   // 箭头与 K 线高低点间距
    const double kTriSize = 5.0;

    // 预扫描：每个 bar 是否有买/卖（用于判断是否画 T 并跳过箭头）
    std::vector<std::pair<bool, bool>> hasBS(bars_.size(), {false, false});
    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        const int bi = markBarIndex_[i];
        if (bi < start || bi >= end) continue;
        if (tradeMarks_[i].direction == Direction::Buy) hasBS[static_cast<size_t>(bi)].first = true;
        else hasBS[static_cast<size_t>(bi)].second = true;
    }
    auto isTBar = [&](int bi) {
        return hasBS[static_cast<size_t>(bi)].first &&
               hasBS[static_cast<size_t>(bi)].second;
    };

    for (size_t i = 0; i < tradeMarks_.size(); ++i) {
        const int bi = markBarIndex_[i];
        if (bi < start || bi >= end) continue;
        if (isTBar(bi)) continue;   // 同天有买有卖 → 只标 T，不画箭头
        const auto& m = tradeMarks_[i];
        const double cx = barCenterX(bi);
        // 买：锚定 bar 低点之下；卖：锚定 bar 高点之上（不遮蜡烛）
        const double anchorPrice = (m.direction == Direction::Buy)
            ? bars_[static_cast<size_t>(bi)].low
            : bars_[static_cast<size_t>(bi)].high;
        const double y = priceToY(anchorPrice);
        if (y < mainRect_.top() - 12 || y > mainRect_.bottom() + 12) continue;
        const QColor color = (m.direction == Direction::Buy) ? kBuyColor : kSellColor;
        p.setPen(QPen(color, 1));
        p.setBrush(color);
        // 买：尖朝上（画在 bar 低点下方，指示低位买入）；卖：尖朝下（画在 bar 高点上方，指示高位卖出）
        const double up = (m.direction == Direction::Buy) ? 1.0 : -1.0;   // 买在低点下方，卖在高点上方
        const double baseY = y + up * kArrowOffset;   // 三角底边（靠外一侧）
        QPolygonF tri;
        tri << QPointF(cx, baseY - up * kTriSize)      // 尖端（靠 bar 一侧）
            << QPointF(cx - kTriSize, baseY + up * kTriSize)
            << QPointF(cx + kTriSize, baseY + up * kTriSize);
        p.drawPolygon(tri);
    }

    // "T" 标记：同一天（同一 bar）既有买又有卖 → 画 "T"（当日做过回转交易）
    p.setPen(QPen(QColor("#ffd700"), 1));   // 金黄 "T"
    p.setBrush(Qt::NoBrush);
    QFont tf = p.font();
    tf.setPixelSize(11);
    tf.setBold(true);
    p.setFont(tf);
    for (int bi = start; bi < end; ++bi) {
        if (!isTBar(bi)) continue;
        const double cx = barCenterX(bi);
        // "T" 画在 bar 高点之上、贴近 K 线（与买卖箭头同样紧贴）
        const double yT = priceToY(bars_[static_cast<size_t>(bi)].high) - 16.0;
        if (yT < mainRect_.top() + 4) continue;
        p.drawText(QPointF(cx - 4.0, yT), QStringLiteral("T"));
    }
    p.restore();   // 恢复画笔/字体状态（含 save 前的 pen/brush/font）
}

// ============================================================
// 交互
// ============================================================
void KLineChart::mouseMoveEvent(QMouseEvent* event) {
    // 先记录光标位置（趋势线预览用原始坐标，即使移到控制条区域也保持最新）
    mouseX_ = event->pos().x();
    mouseY_ = event->pos().y();
    if (event->pos().y() < plotTop()) return;  // 控制条区域忽略其余处理
    if (dragging_ && !bars_.empty()) {
        const int dx = dragStartX_ - event->pos().x();
        const int maxFirst = std::max(0, static_cast<int>(bars_.size()) - visibleCount_);
        firstVisible_ = std::clamp(dragStartFirst_ + static_cast<int>(dx / bodyWidth()),
                                   0, maxFirst);
    }
    if (rangeDragging_ && drawMode_ == DrawMode::Range && !bars_.empty()) {
        rangeTo_ = indexAtX(event->pos().x());
    }
    if (!bars_.empty()) {
        int idx = firstVisible_ + static_cast<int>(
            (event->pos().x() - mainRect_.left()) / bodyWidth());
        idx = std::clamp(idx, firstVisible_,
                         std::min(firstVisible_ + visibleCount_ - 1,
                                  static_cast<int>(bars_.size()) - 1));
        mouseIndex_ = idx;
    }
    // 悬停 K 线日期变化 → 通知外部面板（筹码分布等按日期查询）
    const std::optional<DateTime> date =
        (mouseIndex_ >= 0 && mouseIndex_ < static_cast<int>(bars_.size()))
            ? std::optional<DateTime>(bars_[static_cast<size_t>(mouseIndex_)].time)
            : std::nullopt;
    if (date != lastEmittedDate_) {
        lastEmittedDate_ = date;
        emit crosshairDateChanged(date);
    }
    update();
}

void KLineChart::mousePressEvent(QMouseEvent* event) {
    if (event->pos().y() < plotTop()) return;
    if (event->button() == Qt::LeftButton && !bars_.empty()) {
        // 画线模式：水平线点击即画，趋势线按下记起点
        if (drawMode_ == DrawMode::Range) {
            rangeDragStart_ = indexAtX(event->pos().x());
            rangeFrom_ = rangeTo_ = rangeDragStart_;
            rangeDragging_ = true;
            update();
            return;
        }
        if (drawMode_ == DrawMode::Horizontal) {
            lines_.push_back({true, 0, priceFromY(event->pos().y()), 0, 0});
            update();
            return;
        }
        if (drawMode_ == DrawMode::Trend) {
            drawing_ = true;
            dragStartIdx_ = indexAtX(event->pos().x());
            dragStartPrice_ = priceFromY(event->pos().y());
            dragStartX_ = event->pos().x();
            setCursor(Qt::CrossCursor);
            update();
            return;
        }
        // 默认：拖拽平移
        dragging_ = true;
        dragStartX_ = event->pos().x();
        dragStartFirst_ = firstVisible_;
        setCursor(Qt::ClosedHandCursor);
    }
}

void KLineChart::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && drawMode_ == DrawMode::Range &&
        rangeDragging_) {
        rangeDragging_ = false;
        setCursor(Qt::CrossCursor);
        const int cur = indexAtX(event->pos().x());
        rangeFrom_ = std::min(rangeDragStart_, cur);
        rangeTo_ = std::max(rangeDragStart_, cur);
        if (!bars_.empty()) emit rangeSelected(bars_, rangeFrom_, rangeTo_);
        update();
        return;
    }
    if (event->button() == Qt::LeftButton && drawing_ &&
        drawMode_ == DrawMode::Trend) {
        // 趋势线提交终点（退化点：同 bar 同价则忽略）
        const int idx2 = indexAtX(event->pos().x());
        const double price2 = priceFromY(event->pos().y());
        drawing_ = false;
        unsetCursor();
        if (idx2 != dragStartIdx_ || std::abs(price2 - dragStartPrice_) > 1e-6) {
            lines_.push_back({false, dragStartIdx_, dragStartPrice_, idx2, price2});
        }
        update();
        return;
    }
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
    if (lastEmittedDate_) {
        lastEmittedDate_.reset();
        emit crosshairDateChanged(std::nullopt);  // 离开图表 → 外部面板回退最新
    }
    update();
}

} // namespace st

#include "moc_kline_chart.cpp"
