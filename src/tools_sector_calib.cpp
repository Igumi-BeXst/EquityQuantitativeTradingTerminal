// 东财板块行情实连校准工具（P10 第四轮开发用）
// 用法: sector_calib [industry|concept]  默认两者都拉
// 验证：total 量级（行业 ~86、概念 ~400）与 top10 名称/涨跌幅合理性
#include "data/eastmoney_sector_provider.h"
#include <QCoreApplication>
#include <algorithm>
#include <cstdio>

using namespace st;

namespace {

void dump(const char* title, SectorType type) {
    EastMoneySectorProvider provider;
    auto boards = provider.fetchBoards(type);
    std::printf("\n== %s ==  count=%zu\n", title, boards.size());

    auto sorted = boards;
    std::sort(sorted.begin(), sorted.end(),
              [](const SectorBoard& a, const SectorBoard& b) {
                  return a.changePct > b.changePct;
              });
    const size_t n = std::min<size_t>(10, sorted.size());
    std::printf("  top %zu（按涨跌幅）:\n", n);
    for (size_t i = 0; i < n; ++i) {
        const auto& b = sorted[i];
        std::printf("    %-8s %-10s %+6.2f%%  领涨 %-8s %+5.2f%%  涨%3d 跌%3d  额%.0f亿\n",
                    b.code.c_str(), b.name.c_str(), b.changePct,
                    b.leadingStock.c_str(), b.leadingChangePct,
                    b.upCount, b.downCount, b.amount / 1e8);
    }
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::string arg = argc > 1 ? argv[1] : "all";
    if (arg == "industry") {
        dump("行业板块", SectorType::Industry);
    } else if (arg == "concept") {
        dump("概念板块", SectorType::Concept);
    } else {
        dump("行业板块", SectorType::Industry);
        dump("概念板块", SectorType::Concept);
    }
    return 0;
}
