#include "ui/widgets/stock_key_data_widget.h"
#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "foundation/utils/datetime.h"
#include <QLabel>
#include <QTimer>
#include <QGridLayout>
#include <QScrollArea>
#include <QMetaObject>
#include <QPointer>
#include <QShowEvent>
#include <QHideEvent>
#include <cmath>
#include <algorithm>
#include <utility>

namespace st {

namespace {
constexpr char kUpColor[] = "#e54648";    // 红涨
constexpr char kDownColor[] = "#2e9e5b";  // 绿跌

/// 股 → 手，自动缩放
QString volText(double shares) {
    const double hands = shares / 100.0;
    if (hands >= 1e4) return QStringLiteral("%1万手").arg(hands / 1e4, 0, 'f', 2);
    return QStringLiteral("%1手").arg(hands, 0, 'f', 0);
}

/// 成交额（元）→ 亿/万
QString amountText(double yuan) {
    if (yuan >= 1e8) return QStringLiteral("%1亿").arg(yuan / 1e8, 0, 'f', 2);
    if (yuan >= 1e4) return QStringLiteral("%1万").arg(yuan / 1e4, 0, 'f', 2);
    return QStringLiteral("%1").arg(yuan, 0, 'f', 0);
}

/// 涨停/跌停幅度：创业板(300/301)/科创板(688) 20%，其余 10%
double limitPctFor(const StockCode& code) {
    const std::string c = code.code();
    if (c.size() >= 3) {
        const std::string pre = c.substr(0, 3);
        if (pre == "300" || pre == "301" || pre == "688") return 0.20;
    }
    return 0.10;
}

double round2(double v) { return std::round(v * 100.0) / 100.0; }
}  // namespace

StockKeyDataWidget::StockKeyDataWidget(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider),
      fundProvider_(std::make_shared<AKShareProvider>()) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto* body = new QWidget;
    auto* grid = new QGridLayout(body);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(2);

    static const char* kLabels[kFieldCount] = {
        "最高价", "最低价", "开盘价", "昨收价", "成交量", "成交额", "量比", "振幅",
        "涨停价", "跌停价", "换手率", "换手率(实)", "外盘", "内盘",
        "市盈(静)", "市盈(TTM)", "总市值", "流通值", "总股本", "流通股",
    };
    values_.resize(kFieldCount);
    for (int i = 0; i < kFieldCount; ++i) {
        auto* label = new QLabel(kLabels[i]);
        label->setStyleSheet(QStringLiteral("color:#888888;"));
        auto* value = new QLabel(tr("--"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        // 两列排序：第 i 项在 第 (i/2) 行，(i%2) 决定左/右列；
        // 布局列：0标签|1值|2间隔|3标签|4值 —— 第 2 列固定窄宽分隔左右两组数据
        const int col = (i % 2) * 3;
        grid->addWidget(label, i / 2, col);
        grid->addWidget(value, i / 2, col + 1);
        values_[i] = value;
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(4, 1);
    grid->setColumnMinimumWidth(2, 16);  // 左右两列数据间隔

    scroll->setWidget(body);
    auto* outer = new QGridLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    timer_ = new QTimer(this);
    timer_->setInterval(5000);  // OHLC/量比变化慢，5s 轮询与盘口 2.5s 错开
    connect(timer_, &QTimer::timeout, this, &StockKeyDataWidget::onPoll);

    fundTimer_ = new QTimer(this);
    fundTimer_->setInterval(60000);  // 基本面慢刷新（与行情 5s 错开）
    connect(fundTimer_, &QTimer::timeout, this, &StockKeyDataWidget::requestFundamentals);
}

void StockKeyDataWidget::setPollIntervalMs(int ms) {
    timer_->setInterval(ms);
}

void StockKeyDataWidget::setStock(const StockCode& code, const QString& name) {
    code_ = code;
    name_ = name;
    barsLoaded_ = false;
    avg5dVol_ = 0.0;
    fund_ = std::nullopt;
    fundFetching_ = false;
    resetLabels();

    // 异步取 5 日均量（量比分母）
    if (!code_.code().empty()) {
        const auto end = utils::now();
        const auto start = utils::addTradingDays(end, -10);
        const StockCode c = code_;
        // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程
        IDataProvider* provider = provider_;
        QPointer<StockKeyDataWidget> guard(this);
        ThreadPool::submitIO([provider, guard, c, start, end] {
            auto bars = provider->getBars(c, BarPeriod::Daily, start, end);
            double sum = 0.0;
            int n = 0;
            // 取最后 5 根
            const size_t begin = bars.size() > 5 ? bars.size() - 5 : 0;
            for (size_t i = begin; i < bars.size(); ++i) {
                sum += static_cast<double>(bars[i].volume);
                ++n;
            }
            const double avg = n > 0 ? sum / n : 0.0;
            // 最近完成日 + 前一日（量额对比用）
            double lastVol = 0.0, lastAmt = 0.0, prevVol = 0.0, prevAmt = 0.0;
            if (!bars.empty()) {
                lastVol = static_cast<double>(bars.back().volume);
                lastAmt = bars.back().amount;
                if (bars.size() >= 2) {
                    prevVol = static_cast<double>(bars[bars.size() - 2].volume);
                    prevAmt = bars[bars.size() - 2].amount;
                }
            }
            QMetaObject::invokeMethod(guard,
                [guard, avg, lastVol, lastAmt, prevVol, prevAmt] {
                    guard->onBarsFetched(avg, lastVol, lastAmt, prevVol, prevAmt);
                }, Qt::QueuedConnection);
        });
    }
    onPoll();
    requestFundamentals();
}

void StockKeyDataWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (timer_ && !timer_->isActive()) {
        timer_->start();
        if (!code_.code().empty()) onPoll();
    }
    if (fundTimer_ && !fundTimer_->isActive()) {
        fundTimer_->start();
        if (!code_.code().empty()) requestFundamentals();
    }
}

void StockKeyDataWidget::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    if (timer_) timer_->stop();
    if (fundTimer_) fundTimer_->stop();
}

void StockKeyDataWidget::onPoll() {
    if (polling_ || code_.code().empty()) return;
    polling_ = true;
    const StockCode code = code_;
    // 安全异步：按值捕获 provider + QPointer 守卫投递回主线程
    IDataProvider* provider = provider_;
    QPointer<StockKeyDataWidget> guard(this);
    ThreadPool::submitIO([provider, guard, code] {
        auto quotes = provider->batchQuote({code});
        QMetaObject::invokeMethod(guard,
            [guard, quotes = std::move(quotes)]() mutable {
                guard->polling_ = false;
                if (!quotes.empty()) guard->applyQuote(quotes.front());
            }, Qt::QueuedConnection);
    });
}

void StockKeyDataWidget::requestFundamentals() {
    if (fundFetching_ || code_.code().empty() || !fundProvider_) return;
    fundFetching_ = true;
    const StockCode code = code_;
    // 安全异步：shared_ptr 按值捕获（widget 销毁后 provider 仍存活）+ QPointer 守卫回主线程。
    // provider 移入主线程回调，确保 AKShareProvider 在 IO 线程用完、回主线程后才析构
    // （避免析构时跨线程访问成员 QNAM clearAccessCache）。
    auto provider = fundProvider_;
    QPointer<StockKeyDataWidget> guard(this);
    ThreadPool::submitIO([provider, guard, code] {
        auto f = provider->getQuoteFundamentals(code);
        QMetaObject::invokeMethod(guard,
            [guard, f = std::move(f), provider = std::move(provider)]() mutable {
                guard->fundFetching_ = false;
                guard->onFundamentalsFetched(std::move(f));
            }, Qt::QueuedConnection);
    });
}

void StockKeyDataWidget::onFundamentalsFetched(std::optional<QuoteFundamentals> f) {
    fund_ = std::move(f);
    applyFundamentals();
}

void StockKeyDataWidget::applyFundamentals() {
    auto setText = [this](Field f, const QString& text) {
        values_[f]->setText(text);
    };
    const auto numText = [](double v) -> QString {
        return v > 0.0 ? QString::number(v, 'f', 2) : QStringLiteral("—");
    };
    const auto pctText = [](double v) -> QString {
        return v > 0.0 ? QStringLiteral("%1%").arg(v, 0, 'f', 2) : QStringLiteral("—");
    };
    const auto capText = [](double yuan) -> QString {  // 市值 元 → 万亿/亿
        if (yuan >= 1e12) return QStringLiteral("%1万亿").arg(yuan / 1e12, 0, 'f', 2);
        if (yuan >= 1e8) return QStringLiteral("%1亿").arg(yuan / 1e8, 0, 'f', 2);
        return QStringLiteral("—");
    };
    const auto sharesText = [](double shares) -> QString {  // 股本 股 → 亿股
        if (shares >= 1e8) return QStringLiteral("%1亿股").arg(shares / 1e8, 0, 'f', 2);
        return QStringLiteral("—");
    };
    if (!fund_ || !fund_->valid) {
        setText(FTurnover, QStringLiteral("—"));
        setText(FTurnoverReal, QStringLiteral("—"));
        setText(FPeStatic, QStringLiteral("—"));
        setText(FPeTtm, QStringLiteral("—"));
        setText(FMarketCap, QStringLiteral("—"));
        setText(FFloatCap, QStringLiteral("—"));
        setText(FTotalShares, QStringLiteral("—"));
        setText(FFloatShares, QStringLiteral("—"));
        return;
    }
    const auto& f = *fund_;
    setText(FTurnover, pctText(f.turnoverRate));
    setText(FTurnoverReal, pctText(f.turnoverRateReal));
    setText(FPeStatic, numText(f.peStatic));
    setText(FPeTtm, numText(f.peTtm));  // 东财 clist 无可靠 TTM → 常为 "—"
    setText(FMarketCap, capText(f.marketCap));
    setText(FFloatCap, capText(f.floatCap));
    setText(FTotalShares, sharesText(f.totalShares));
    setText(FFloatShares, sharesText(f.floatShares));
}

void StockKeyDataWidget::onBarsFetched(double avg5dVol, double lastVol, double lastAmt,
                                       double prevVol, double prevAmt) {
    avg5dVol_ = avg5dVol;
    lastVol_ = lastVol;
    lastAmt_ = lastAmt;
    prevVol_ = prevVol;
    prevAmt_ = prevAmt;
    barsLoaded_ = true;
}

void StockKeyDataWidget::applyQuote(const Quote& q) {
    auto setText = [this](Field f, const QString& text) {
        values_[f]->setText(text);
    };
    auto setColor = [this](Field f, const QString& color) {
        values_[f]->setStyleSheet(color.isEmpty() ? QString()
            : QStringLiteral("color:%1;").arg(color));
    };

    setText(FHigh, QString::number(q.high, 'f', 2));
    setText(FLow, QString::number(q.low, 'f', 2));
    setText(FOpen, QString::number(q.open, 'f', 2));
    setText(FPreClose, QString::number(q.preClose, 'f', 2));
    setText(FVolume, volText(static_cast<double>(q.volume)));
    setText(FAmount, amountText(q.amount));
    setText(FOuter, volText(static_cast<double>(q.outerVol)));
    setText(FInner, volText(static_cast<double>(q.innerVol)));

    // 振幅（%）
    if (q.preClose > 0.0 && q.high >= q.low) {
        setText(FAmplitude, QStringLiteral("%1%")
            .arg((q.high - q.low) / q.preClose * 100.0, 0, 'f', 2));
    }

    // 涨停/跌停价
    if (q.preClose > 0.0) {
        const double pct = limitPctFor(code_);
        setText(FLimitUp, QString::number(round2(q.preClose * (1.0 + pct)), 'f', 2));
        setText(FLimitDown, QString::number(round2(q.preClose * (1.0 - pct)), 'f', 2));
    }

    // 量比 = 今日量 / 5 日均量
    if (barsLoaded_ && avg5dVol_ > 0.0) {
        setText(FVolRatio, QString::number(
            static_cast<double>(q.volume) / avg5dVol_, 'f', 2));
    }

    // 高/低/开 各自与昨收（零轴）比较：高于昨收→红、低于昨收→绿、持平→默认黑
    const double preClose = q.preClose;
    const auto colorVsPreClose = [this, preClose](double v) -> QString {
        if (preClose <= 0.0) return QString();
        if (v > preClose) return kUpColor;
        if (v < preClose) return kDownColor;
        return QString();
    };
    setColor(FHigh, colorVsPreClose(q.high));
    setColor(FLow, colorVsPreClose(q.low));
    setColor(FOpen, colorVsPreClose(q.open));

    // 涨停红 / 跌停绿 / 外盘红 / 内盘绿
    setColor(FLimitUp, kUpColor);
    setColor(FLimitDown, kDownColor);
    setColor(FOuter, kUpColor);
    setColor(FInner, kDownColor);

    // 成交量/成交额：与前一日对比（放量红 / 缩量绿 / 持平默认）
    // 若末根日K与当前报价会话同日（量近似），"前一日"取再前一根
    double cmpVol = lastVol_;
    double cmpAmt = lastAmt_;
    if (lastVol_ > 0.0 && prevVol_ > 0.0 &&
        std::abs(static_cast<double>(q.volume) - lastVol_) / lastVol_ < 0.05) {
        cmpVol = prevVol_;
        cmpAmt = prevAmt_;
    }
    if (cmpVol > 0.0) {
        const double todayVol = static_cast<double>(q.volume);
        setColor(FVolume, todayVol > cmpVol ? kUpColor
                          : (todayVol < cmpVol ? kDownColor : QString()));
    }
    if (cmpAmt > 0.0) {
        setColor(FAmount, q.amount > cmpAmt ? kUpColor
                          : (q.amount < cmpAmt ? kDownColor : QString()));
    }
}

void StockKeyDataWidget::resetLabels() {
    for (auto* v : values_) {
        if (v) {
            v->setText(tr("--"));
            v->setStyleSheet(QString());
        }
    }
}

} // namespace st

#include "moc_stock_key_data_widget.cpp"
