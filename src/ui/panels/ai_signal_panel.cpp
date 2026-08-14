#include "ui/panels/ai_signal_panel.h"

#include "core/log_manager.h"
#include "core/thread_pool.h"
#include "data/idata_provider.h"
#include "foundation/utils/datetime.h"
#include "foundation/utils/indicators.h"
#include "intelligence/pattern/pattern_recognizer.h"
#include "intelligence/sentiment/sentiment_analyzer.h"
#include "intelligence/sentiment/eastmoney_news_provider.h"

#include <QColor>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextOption>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

namespace st {

namespace {

constexpr char kUpColor[] = "#e54648";       // 红（涨/强买）
constexpr char kUpSoftColor[] = "#ff7b72";   // 浅红（买入）
constexpr char kNeutralColor[] = "#d4d4d4";  // 灰（观望）
constexpr char kDownSoftColor[] = "#58a06b"; // 浅绿（卖出）
constexpr char kDownColor[] = "#2e9e5b";     // 绿（跌/强卖）

QString ratingColor(st::signal::SignalRating rating) {
    switch (rating) {
        case st::signal::SignalRating::StrongBuy:  return QString::fromUtf8(kUpColor);
        case st::signal::SignalRating::Buy:        return QString::fromUtf8(kUpSoftColor);
        case st::signal::SignalRating::Sell:       return QString::fromUtf8(kDownSoftColor);
        case st::signal::SignalRating::StrongSell: return QString::fromUtf8(kDownColor);
        default:                                   return QString::fromUtf8(kNeutralColor);
    }
}

bool isAShareStock(const StockCode& code) {
    const std::string& c = code.code();
    if (c.size() != 6) return false;
    if (code.market() == Market::SH) return c[0] == '6';      // 600/601/603/605/688
    if (code.market() == Market::SZ) return c[0] == '0' || c[0] == '3';  // 000/001/002/003/300/301
    return false;
}

/// 分项分数条（自绘）：名称 + -1~+1 横条（0 轴居中，红正绿负）+ 数值 + 说明
class SignalBarWidget : public QWidget {
public:
    explicit SignalBarWidget(const st::signal::SignalComponent& c, QWidget* parent = nullptr)
        : QWidget(parent), comp_(c) {
        setMinimumHeight(40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        const int h = height();
        const int nameW = 62;
        const int barW = width() - nameW - 56;
        const int barH = 10;
        const int barY = 4;

        p.setPen(QColor(kNeutralColor));
        p.drawText(QRect(0, 0, nameW - 4, barH + 2),
                   QString::fromStdString(comp_.name),
                   QTextOption(Qt::AlignLeft | Qt::AlignVCenter));

        // 轨道 + 0 轴
        const QRectF track(nameW, barY, barW, barH);
        p.fillRect(track, QColor(255, 255, 255, 26));
        const double zeroX = nameW + barW * 0.5;
        p.setPen(QPen(QColor(255, 255, 255, 90), 1));
        p.drawLine(QPointF(zeroX, barY), QPointF(zeroX, barY + barH));

        // 分数条：正红向右 / 负绿向左
        const double len = barW * 0.5 * std::clamp(std::abs(comp_.score), 0.0, 1.0);
        if (len > 0.5) {
            const QColor color = comp_.score >= 0.0
                ? QColor(kUpColor) : QColor(kDownColor);
            const QRectF bar = comp_.score >= 0.0
                ? QRectF(zeroX, barY, len, barH)
                : QRectF(zeroX - len, barY, len, barH);
            p.fillRect(bar, color);
        }

        // 数值
        p.setPen(QColor(kNeutralColor));
        p.drawText(QRect(nameW + barW + 4, 0, 52, barH + 2),
                   QStringLiteral("%1%2").arg(comp_.score >= 0.0 ? "+" : "")
                       .arg(comp_.score, 0, 'f', 2),
                   QTextOption(Qt::AlignRight | Qt::AlignVCenter));

        // 说明（第二行，小字灰色）
        p.setPen(QColor("#999999"));
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        p.setFont(f);
        p.drawText(QRect(0, barY + barH + 2, width(), h - barY - barH - 2),
                   QString::fromStdString(comp_.detail),
                   QTextOption(Qt::AlignLeft | Qt::AlignVCenter));
    }

private:
    st::signal::SignalComponent comp_;
};

}  // namespace

AiSignalPanel::AiSignalPanel(IDataProvider* provider, QWidget* parent)
    : QWidget(parent), provider_(provider) {
    newsProvider_ = std::make_shared<st::sentiment::EastMoneyNewsProvider>();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    // 标题行：股票名 + 刷新
    auto* titleRow = new QHBoxLayout;
    titleLabel_ = new QLabel(tr("请选择股票"));
    titleLabel_->setStyleSheet(QStringLiteral("font-weight:bold;"));
    refreshBtn_ = new QPushButton(tr("刷新"));
    refreshBtn_->setEnabled(false);
    titleRow->addWidget(titleLabel_, 1);
    titleRow->addWidget(refreshBtn_);
    layout->addLayout(titleRow);

    // 评级大字
    ratingLabel_ = new QLabel(tr("—"));
    QFont ratingFont = ratingLabel_->font();
    ratingFont.setPointSizeF(ratingFont.pointSizeF() + 6.0);
    ratingFont.setBold(true);
    ratingLabel_->setFont(ratingFont);
    ratingLabel_->setAlignment(Qt::AlignCenter);
    layout->addWidget(ratingLabel_);

    metaLabel_ = new QLabel(tr("—"));
    metaLabel_->setAlignment(Qt::AlignCenter);
    metaLabel_->setStyleSheet(QStringLiteral("color:#999999;"));
    layout->addWidget(metaLabel_);

    summaryLabel_ = new QLabel(tr("—"));
    summaryLabel_->setWordWrap(true);
    summaryLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kNeutralColor));
    layout->addWidget(summaryLabel_);

    // 分项容器
    componentsBox_ = new QWidget(this);
    auto* compLayout = new QVBoxLayout(componentsBox_);
    compLayout->setContentsMargins(0, 0, 0, 0);
    compLayout->setSpacing(8);
    layout->addWidget(componentsBox_);

    // 历史信号表
    layout->addWidget(new QLabel(tr("历史信号（本会话）")));
    historyView_ = new QTableView;
    historyView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyView_->setFocusPolicy(Qt::NoFocus);
    historyView_->horizontalHeader()->setStretchLastSection(true);
    historyView_->verticalHeader()->setVisible(false);
    auto* model = new QStandardItemModel(0, 5, this);
    model->setHorizontalHeaderLabels({tr("日期"), tr("代码"), tr("名称"), tr("评级"), tr("得分")});
    historyView_->setModel(model);
    historyView_->setMinimumHeight(120);
    layout->addWidget(historyView_, 1);

    connect(refreshBtn_, &QPushButton::clicked, this, [this] {
        if (code_.isValid()) startCompute();
    });
    connect(historyView_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || idx.row() < 0 ||
            idx.row() >= static_cast<int>(history_.size())) return;
        const auto& row = history_[static_cast<size_t>(idx.row())];
        emit openChart(row.code, row.name);
    });
}

void AiSignalPanel::setStock(const StockCode& code, const QString& name) {
    code_ = code;
    name_ = name;
    if (!code.isValid()) {
        resetToIdle();
        return;
    }
    titleLabel_->setText(name_.isEmpty()
        ? QString::fromStdString(code.displayCode()) : name_ + "  " +
            QString::fromStdString(code.displayCode()));
    refreshBtn_->setEnabled(true);
    ++gen_;        // 丢弃在途任务（快速切股防陈旧回写）
    busy_ = false; // 允许新拉取启动
    startCompute();
}

void AiSignalPanel::startCompute() {
    if (busy_) return;   // 在飞任务由 gen 守卫作废，不重入
    busy_ = true;
    const int gen = ++gen_;
    const StockCode c = code_;
    const bool fetchNews = isAShareStock(c);
    const auto end = utils::now();
    const auto start = utils::parseDate("2015-01-01");

    // 安全异步：provider 裸指针 + newsProvider shared_ptr 按值捕获 + QPointer 守卫
    IDataProvider* provider = provider_;
    auto newsProvider = newsProvider_;
    QPointer<AiSignalPanel> guard(this);
    ratingLabel_->setText(tr("计算中…"));
    ratingLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kNeutralColor));
    metaLabel_->setText(tr("拉取日K与资讯…"));

    ThreadPool::submitIO([provider, newsProvider, guard, gen, c, fetchNews, start, end] {
        auto bars = provider->getBars(c, BarPeriod::Daily, start, end);
        std::vector<st::sentiment::NewsItem> news;
        if (fetchNews && newsProvider) {
            news = newsProvider->fetchNews(c, 20);
        }
        QMetaObject::invokeMethod(guard,
            [guard, gen, bars = std::move(bars), news = std::move(news)]() mutable {
                guard->busy_ = false;
                if (gen != guard->gen_) return;
                guard->metaLabel_->setText(tr("计算指标与形态…"));
                // 先转成局部变量再捕获：MSVC 不支持内层 init-capture 直接引用外层 lambda 的捕获成员
                std::vector<Bar> barsLocal = std::move(bars);
                std::vector<st::sentiment::NewsItem> newsLocal = std::move(news);
                QPointer<AiSignalPanel> g2 = guard;
                const int gen2 = gen;
                ThreadPool::submitWorker(
                    [g2, gen2, bars2 = std::move(barsLocal),
                     news2 = std::move(newsLocal)]() mutable {
                    st::signal::CompositeSignal cs;
                    QString date;
                    if (!bars2.empty()) {
                        const DateTime lastTime = bars2.back().time;  // 移动前取日期
                        const auto closes = [&bars2] {
                            std::vector<double> v;
                            v.reserve(bars2.size());
                            for (const auto& b : bars2) v.push_back(b.close);
                            return v;
                        }();
                        const auto rsi = st::indicators::rsi(closes, 14);
                        const auto macd = st::indicators::macd(closes);
                        st::pattern::PatternRecognizer recognizer;
                        auto patterns = recognizer.detectAt(
                            st::BarSeries(std::move(bars2)), 3).items;
                        st::sentiment::SentimentAnalyzer analyzer;
                        st::sentiment::SentimentScore senti;
                        if (!news2.empty()) senti = analyzer.averageScore(news2);
                        const double close = closes.empty() ? 0.0 : closes.back();
                        const double prevClose = closes.size() >= 2
                            ? closes[closes.size() - 2] : 0.0;
                        cs = st::signal::composeSignal(
                            patterns, senti,
                            rsi.empty() ? std::numeric_limits<double>::quiet_NaN()
                                        : rsi.back(),
                            macd, close, prevClose);
                        date = QString::fromStdString(utils::toDateString(lastTime));
                    }
                    QMetaObject::invokeMethod(g2,
                        [g2, gen2, cs = std::move(cs), date]() mutable {
                            if (!g2 || gen2 != g2->gen_) return;
                            g2->applyResult(std::move(cs), date);
                        }, Qt::QueuedConnection);
                });
            }, Qt::QueuedConnection);
    });
}

void AiSignalPanel::applyResult(st::signal::CompositeSignal cs, const QString& date) {
    const QString ratingText = QString::fromStdString(
        st::signal::ratingName(cs.rating));
    ratingLabel_->setText(ratingText);
    ratingLabel_->setStyleSheet(
        QStringLiteral("color:%1;").arg(ratingColor(cs.rating)));
    metaLabel_->setText(tr("得分 %1 · 置信度 %2 · %3")
        .arg(cs.score, 0, 'f', 2)
        .arg(cs.confidence, 0, 'f', 2)
        .arg(date.isEmpty() ? tr("—") : date));
    summaryLabel_->setText(QString::fromStdString(cs.summary));

    // 重建分项条
    auto* compLayout = componentsBox_->layout();
    while (QLayoutItem* item = compLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    if (cs.components.empty()) {
        auto* empty = new QLabel(tr("无可用数据（形态/情绪/技术均缺失）"));
        empty->setStyleSheet(QStringLiteral("color:#888888;"));
        compLayout->addWidget(empty);
    } else {
        for (const auto& comp : cs.components) {
            compLayout->addWidget(new SignalBarWidget(comp, componentsBox_));
        }
    }

    // 历史记录（最新在前，上限 50）
    HistoryRow row;
    row.code = code_;
    row.name = name_;
    row.date = date;
    row.rating = cs.rating;
    row.score = cs.score;
    addHistory(std::move(row));
}

void AiSignalPanel::addHistory(const HistoryRow& row) {
    history_.insert(history_.begin(), row);
    if (history_.size() > 50) history_.resize(50);

    auto* model = static_cast<QStandardItemModel*>(historyView_->model());
    model->insertRow(0);
    const auto color = ratingColor(row.rating);
    const auto setCell = [&](int col, const QString& text, bool colored = false) {
        auto* item = new QStandardItem(text);
        if (colored) item->setForeground(QColor(color));
        item->setTextAlignment(Qt::AlignCenter);
        model->setItem(0, col, item);
    };
    setCell(0, row.date);
    setCell(1, QString::fromStdString(row.code.displayCode()));
    setCell(2, row.name);
    setCell(3, QString::fromStdString(st::signal::ratingName(row.rating)), true);
    setCell(4, QStringLiteral("%1%2").arg(row.score >= 0.0 ? "+" : "")
                 .arg(row.score, 0, 'f', 2), true);
    while (model->rowCount() > 50) model->removeRow(model->rowCount() - 1);
}

void AiSignalPanel::resetToIdle() {
    ++gen_;   // 作废在飞任务
    busy_ = false;
    titleLabel_->setText(tr("请选择股票"));
    ratingLabel_->setText(tr("—"));
    ratingLabel_->setStyleSheet(QString());
    metaLabel_->setText(tr("—"));
    summaryLabel_->setText(tr("—"));
    refreshBtn_->setEnabled(false);
    auto* compLayout = componentsBox_->layout();
    while (QLayoutItem* item = compLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
}

} // namespace st

#include "moc_ai_signal_panel.cpp"
