// 精选股票池校验工具 — 对比静态名称与腾讯实时名称，抓出错误代码
#include "data/tencent_provider.h"
#include "data/curated_stocks.h"
#include "core/log_manager.h"
#include <QCoreApplication>
#include <iostream>
#include <unordered_map>

using namespace st;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    LogManager::instance()->init("logs/curated_check.log");

    TencentProvider provider;
    provider.connect();

    int mismatch = 0;
    int noLive = 0;
    int total = 0;

    auto check = [&](Market market, const std::vector<CuratedStock>& table,
                     const std::string& marketName) {
        auto infos = provider.getStockList(market);
        std::unordered_map<std::string, std::string> liveName;
        for (const auto& i : infos) liveName[i.code.displayCode()] = i.name;

        for (const auto& c : table) {
            ++total;
            auto it = liveName.find(c.code);
            if (it == liveName.end()) {
                ++noLive;
                std::cout << "[" << marketName << "] NO-LIVE  " << c.code << "  " << c.name
                          << "  (腾讯无此代码记录)" << std::endl;
            } else if (it->second != c.name) {
                ++mismatch;
                std::cout << "[" << marketName << "] MISMATCH " << c.code << "  静态=" << c.name
                          << "  实时=" << it->second << std::endl;
            }
        }
    };

    check(Market::SH, kCuratedSH, "SH");
    check(Market::SZ, kCuratedSZ, "SZ");

    std::cout << "\n=== 汇总: 共 " << total << " 只, 名称不一致 " << mismatch
              << ", 无实时记录 " << noLive << " ===" << std::endl;
    return (mismatch + noLive) > 0 ? 1 : 0;
}
