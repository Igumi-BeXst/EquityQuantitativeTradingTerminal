// 表单布局离屏渲染验证（修复轮⑦：标签紧贴行式布局效果实测）
// 渲染 4 面板配置表单为 PNG，人工/像素检查标签与控件间距
#include "ui/panels/optimization_panel.h"
#include "ui/panels/advisor_panel.h"
#include "ui/panels/backtest_panel.h"
#include "ui/panels/strategy_compare_panel.h"
#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QSpinBox>
#include <QImage>
#include <QDebug>
#include <crtdbg.h>
#include <iostream>
#include <vector>

using namespace st;

/// 打印面板内所有标签与控件的几何，人工核对标签-控件间距
static void dumpGeometry(QWidget* panel, const QString& name) {
    struct Rect { QString tag; int x, y, w, h; };
    std::vector<Rect> rects;
    const auto labels = panel->findChildren<QLabel*>();
    for (auto* l : labels) {
        if (l->isVisible() && !l->text().isEmpty()) {
            rects.push_back({l->text(), l->x(), l->y(), l->width(), l->height()});
        }
    }
    const auto combos = panel->findChildren<QComboBox*>();
    for (auto* c : combos) {
        if (c->isVisible()) rects.push_back({QStringLiteral("<combo>"), c->x(), c->y(), c->width(), c->height()});
    }
    const auto spins = panel->findChildren<QAbstractSpinBox*>();
    for (auto* s : spins) {
        if (s->isVisible()) rects.push_back({QStringLiteral("<spin>"), s->x(), s->y(), s->width(), s->height()});
    }
    const auto lists = panel->findChildren<QListWidget*>();
    for (auto* lst : lists) {
        if (lst->isVisible()) rects.push_back({QStringLiteral("<list>"), lst->x(), lst->y(), lst->width(), lst->height()});
    }
    std::cout << "--- " << name.toStdString() << " 几何 ---" << std::endl;
    for (const auto& r : rects) {
        std::cout << "  " << r.tag.toStdString() << "  x=" << r.x << " y=" << r.y
                  << " w=" << r.w << " h=" << r.h << std::endl;
    }
}

static void render(QWidget* panel, const QString& name) {
    panel->resize(760, 640);
    panel->show();
    QApplication::processEvents();
    const QImage img = panel->grab().toImage();
    const QString path = QStringLiteral("form_%1.png").arg(name);
    img.save(path);
    std::cout << "saved: " << path.toStdString()
              << "  size=" << img.width() << "x" << img.height() << std::endl;
    dumpGeometry(panel, name);
    panel->hide();
}

int main(int argc, char* argv[]) {
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    QApplication app(argc, argv);

    std::cout << "[1] optimization..." << std::endl;
    render(new OptimizationPanel(nullptr), "optimization");
    std::cout << "[2] advisor..." << std::endl;
    render(new AdvisorPanel(nullptr), "advisor");
    std::cout << "[3] backtest..." << std::endl;
    render(new BacktestPanel(nullptr), "backtest");
    std::cout << "[4] strategy_compare..." << std::endl;
    render(new StrategyComparePanel(nullptr), "strategy_compare");

    std::cout << "=== 渲染完成 ===" << std::endl;
    return 0;
}
