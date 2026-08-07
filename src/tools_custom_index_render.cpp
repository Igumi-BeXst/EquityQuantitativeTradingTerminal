// 自定义指数图表离屏渲染验证工具（P10 第七轮开发用）
// 直接用 CentralChartWidget（chart.show() 可见）→ loadCustomIndex → 切分时 → 截图 + drawPriceLines 坐标日志
#include "ui/widgets/central_chart_widget.h"
#include "engine/analyzer/custom_index.h"
#include "data/provider_factory.h"
#include "core/log_manager.h"
#include "foundation/utils/datetime.h"
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QTimer>
#include <iostream>

using namespace st;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    LogManager::instance()->init("logs/custom_index_render.log");

    auto provider = makeDataProvider();
    provider->connect();

    CustomIndex idx;
    idx.id = "ci_render_test";
    idx.name = "渲染测试";
    idx.baseValue = 1000.0;
    idx.baseDate = utils::today();
    idx.constituents = {
        {StockCode(Market::SH, "600667"), "太极实业", 0.0},
        {StockCode(Market::SZ, "002185"), "华天科技", 0.0},
    };
    normalizeWeights(idx.constituents);

    CentralChartWidget chart(provider.get());
    chart.resize(900, 520);
    chart.show();

    chart.loadCustomIndex(idx);  // 默认日线

    QTimer::singleShot(3500, &app, [&] {
        chart.setPeriod(BarPeriod::Minute5);  // 切分时
        QTimer::singleShot(5000, &app, [&] {
            QImage tl = chart.grab().toImage();
            tl.save("ci_render_timeline.png");
            // 价格线 #4fc3f7 精确色（1.2px 线：看有没有严格匹配像素）
            int strict = 0, blueHue = 0;
            for (int y = 0; y < tl.height(); ++y)
                for (int x = 0; x < tl.width(); ++x) {
                    const QColor c = tl.pixelColor(x, y);
                    if (std::abs(c.red()-0x4f) <= 30 && std::abs(c.green()-0xc3) <= 30 &&
                        std::abs(c.blue()-0xf7) <= 30) ++strict;
                    if (c.blue() > 170 && c.blue() > c.red()+40 && c.green() > c.red()+40) ++blueHue;
                }
            std::cout << "[分时] 价格线严格蓝=" << strict << " 蓝调=" << blueHue << std::endl;
            app.quit();
        });
    });
    return app.exec();
}
