// TDX 市场异常探测 — 全 A 股池批量报价，检查 price=0/preClose=0/change=-100% 等异常
#include "data/tdx/tdx_provider.h"
#include "foundation/stock_code.h"
#include <QCoreApplication>
#include <cstdio>
#include <thread>

using namespace st;

namespace {

bool isTradableAShare(const StockCode& code) {
    const std::string& c = code.code();
    if (c.size() < 3) return false;
    if (code.market() == Market::SH) {
        return c.compare(0, 3, "600") == 0 || c.compare(0, 3, "601") == 0 ||
               c.compare(0, 3, "603") == 0 || c.compare(0, 3, "605") == 0 ||
               c.compare(0, 3, "688") == 0;
    }
    if (code.market() == Market::SZ) {
        return c.compare(0, 3, "000") == 0 || c.compare(0, 3, "001") == 0 ||
               c.compare(0, 3, "002") == 0 || c.compare(0, 3, "003") == 0 ||
               c.compare(0, 3, "300") == 0 || c.compare(0, 3, "301") == 0;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(3));
    if (!provider.isConnected()) { std::printf("连接失败\n"); return 1; }

    std::vector<StockCode> pool;
    auto sh = provider.getStockList(Market::SH);
    for (const auto& s : sh) if (isTradableAShare(s.code)) pool.push_back(s.code);
    auto sz = provider.getStockList(Market::SZ);
    for (const auto& s : sz) if (isTradableAShare(s.code)) pool.push_back(s.code);
    std::printf("全A股池: %zu 只\n", pool.size());

    auto quotes = provider.batchQuote(pool);
    std::printf("报价返回: %zu 条\n", quotes.size());
    int zeroPrice = 0, zeroPre = 0, neg100 = 0;
    for (const auto& q : quotes) {
        if (q.lastPrice <= 0) ++zeroPrice;
        if (q.preClose <= 0) ++zeroPre;
        if (q.preClose > 0 && q.lastPrice <= 0) ++neg100;
    }
    std::printf("price<=0: %d, preClose<=0: %d, change==-100%%: %d\n",
                zeroPrice, zeroPre, neg100);

    int shown = 0;
    for (const auto& q : quotes) {
        if (q.preClose > 0 && q.lastPrice <= 0 && shown < 8) {
            std::printf("  异常: %s price=%.2f preClose=%.2f change=%.1f%%\n",
                        q.code.fullCode().c_str(), q.lastPrice, q.preClose, q.change);
            ++shown;
        }
    }
    provider.disconnect();
    return 0;
}
