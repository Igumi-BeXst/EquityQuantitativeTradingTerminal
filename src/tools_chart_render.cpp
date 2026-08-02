// 图表渲染验证工具 — K线图/分时图离屏渲染为 PNG，供人工检查视觉
#include "ui/widgets/kline_chart.h"
#include "ui/widgets/time_line_chart.h"
#include "data/tencent_provider.h"
#include "core/log_manager.h"
#include <QApplication>
#include <QTimer>
#include <QPixmap>
#include <QImage>
#include <QColor>
#include <QWidget>
#include <iostream>
#include <cmath>

using namespace st;

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    LogManager::instance()->init("logs/chart_render.log");

    TencentProvider provider;
    provider.connect();
    StockCode code(Market::SH, "600519");

    KLineChart kline(&provider);
    kline.resize(900, 520);
    kline.loadStock(code, "贵州茅台");
    kline.show();

    TimelineChart timeline(&provider);
    timeline.resize(900, 520);
    timeline.loadStock(code, "贵州茅台");
    timeline.hide();

    QTimer::singleShot(3500, &app, [&] {
        QPixmap pk = kline.grab();
        pk.save("chart_kline.png");
        pk.scaled(675, 390, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save("chart_kline_small.png");
        // 像素分析: 统计接近红涨/绿跌色的像素（验证蜡烛/量柱/指标实际绘制）
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
        QImage img = pk.toImage();
        std::cout << "kline: 红色像素=" << countColor(img, QColor(0xe5, 0x46, 0x48), 30)
                  << " 绿色像素=" << countColor(img, QColor(0x2e, 0x9e, 0x5b), 30)
                  << " 黄色像素(MA)=" << countColor(img, QColor(0xff, 0xd5, 0x4f), 30)
                  << std::endl;

        QPixmap pt = timeline.grab();
        pt.save("chart_timeline.png");
        pt.scaled(675, 390, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save("chart_timeline_small.png");
        QImage timg = pt.toImage();
        std::cout << "timeline: 红色像素=" << countColor(timg, QColor(0xe5, 0x46, 0x48), 30)
                  << " 绿色像素=" << countColor(timg, QColor(0x2e, 0x9e, 0x5b), 30)
                  << " 橙色像素(均价)=" << countColor(timg, QColor(0xff, 0xa7, 0x26), 30)
                  << " 蓝色像素(价格线)=" << countColor(timg, QColor(0x4f, 0xc3, 0xf7), 30)
                  << std::endl;
        app.quit();
    });
    return app.exec();
}
