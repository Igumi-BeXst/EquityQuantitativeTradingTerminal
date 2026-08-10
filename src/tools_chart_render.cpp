// 图表渲染验证工具 — K线图/分时图离屏渲染为 PNG，供人工检查视觉
#include "ui/widgets/kline_chart.h"
#include "ui/widgets/time_line_chart.h"
#include "data/provider_factory.h"
#include "engine/journal/trade_journal_store.h"
#include "core/app_paths.h"
#include "core/log_manager.h"
#include <QApplication>
#include <QTimer>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QWidget>
#include <QMouseEvent>
#include <iostream>
#include <cmath>

using namespace st;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("StockTerminal");   // 与主程序一致，configDir 指向 StockTerminal/config
    LogManager::instance()->init("logs/chart_render.log");

    auto provider = makeDataProvider();
    provider->connect();
    StockCode code(Market::SH, "600519");

    KLineChart kline(provider.get());
    kline.resize(900, 520);
    kline.loadStock(code, "贵州茅台");
    kline.show();

    TimelineChart timeline(provider.get());
    timeline.resize(900, 520);
    timeline.loadStock(code, "贵州茅台");
    timeline.hide();

    // 交易标记诊断：从真实日志加载 SH600519 的标记 + 持仓成本线
    std::cout << "[诊断] configDir=" << AppPaths::configDir() << std::endl;
    TradeJournalEngine journal;
    TradeJournalStore store;
    store.load(AppPaths::configDir() + "/trade_journal.json", journal);
    const auto allEntries = journal.entries();
    std::cout << "[诊断] 日志条目数=" << allEntries.size() << std::endl;
    const auto marks = collectTradeMarks(allEntries, StockCode(Market::SH, "600519"));
    const auto holdings = deriveHoldings(allEntries, StockCode(Market::SH, "600519"));
    std::cout << "[诊断] SH600519 标记数=" << marks.size()
              << " 持仓线数=" << holdings.size() << std::endl;
    for (const auto& m : marks) {
        std::cout << "  标记: " << m.code.fullCode() << " dir="
                  << static_cast<int>(m.direction) << " price=" << m.price
                  << " vol=" << m.volume << " type=" << static_cast<int>(m.type)
                  << std::endl;
    }
    for (const auto& h : holdings) {
        std::cout << "  持仓线: type=" << static_cast<int>(h.type)
                  << " qty=" << h.quantity << " avgCost=" << h.avgCost << std::endl;
    }
    kline.setTradeMarks(marks, holdings);
    timeline.setTradeMarks(marks);

    QTimer::singleShot(3500, &app, [&] {
        auto countColor = [](const QImage& img, const QColor& target, int tol) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); x += 2) {
                    QColor c = img.pixelColor(x, y);
                    if (std::abs(c.red() - target.red()) <= tol &&
                        std::abs(c.green() - target.green()) <= tol &&
                        std::abs(c.blue() - target.blue()) <= tol) ++n;
                }
            }
            return n;
        };
        auto count = [&](QWidget* w) { return w->grab().toImage(); };

        // 1. 全开
        QImage img = count(&kline);
        std::cout << "[K线全开] 红=" << countColor(img, QColor(0xe5,0x46,0x48), 30)
                  << " 绿=" << countColor(img, QColor(0x2e,0x9e,0x5b), 30)
                  << " 黄(MA/MACD-DEA)=" << countColor(img, QColor(0xff,0xd5,0x4f), 30)
                  << " 橙(BOLL)=" << countColor(img, QColor(0xff,0x8a,0x65), 40)
                  << " 青(成本线#00e5ff)=" << countColor(img, QColor(0x00,0xe5,0xff), 40)
                  << " 买红(#ff5252)=" << countColor(img, QColor(0xff,0x52,0x52), 40)
                  << " T金(#ffd700)=" << countColor(img, QColor(0xff,0xd7,0x00), 25)
                  << std::endl;

        // 2. 关 BOLL → 橙像素应大幅下降
        kline.setIndicatorVisible(KLineChart::Indicator::Boll, false);
        QImage imgNoBoll = count(&kline);
        std::cout << "[关BOLL] 橙=" << countColor(imgNoBoll, QColor(0xff,0x8a,0x65), 40)
                  << " (应明显小于全开)" << std::endl;
        kline.setIndicatorVisible(KLineChart::Indicator::Boll, true);

        // 3. 关 MA → 黄像素下降（MACD DEA 仍黄）
        kline.setIndicatorVisible(KLineChart::Indicator::Ma, false);
        QImage imgNoMa = count(&kline);
        std::cout << "[关MA] 黄=" << countColor(imgNoMa, QColor(0xff,0xd5,0x4f), 30)
                  << " (应明显小于全开)" << std::endl;
        kline.setIndicatorVisible(KLineChart::Indicator::Ma, true);

        kline.grab().save("chart_kline.png");

        // 4. 分时: 均价橙线 + MACD DIF 白/DEA 黄 + 交易箭头
        // 模拟悬停（x=中间 → 触发信息框），验证信息框文字颜色不被交易标记污染
        timeline.show();
        QApplication::processEvents();
        QMouseEvent move(QEvent::MouseMove, QPointF(400, 100), QPointF(400, 100),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&timeline, &move);
        QApplication::processEvents();
        QImage timg = count(&timeline);
        std::cout << "[分时] 红=" << countColor(timg, QColor(0xe5,0x46,0x48), 30)
                  << " 绿=" << countColor(timg, QColor(0x2e,0x9e,0x5b), 30)
                  << " 橙(均价)=" << countColor(timg, QColor(0xff,0xa7,0x26), 30)
                  << " 蓝(价格线)=" << countColor(timg, QColor(0x4f,0xc3,0xf7), 30)
                  << " 黄(MACD-DEA)=" << countColor(timg, QColor(0xff,0xd5,0x4f), 30)
                  << " 箭头(#ff5252)=" << countColor(timg, QColor(0xff,0x52,0x52), 40)
                  << std::endl;
        timeline.grab().save("chart_timeline.png");
        app.quit();
    });
    return app.exec();
}
