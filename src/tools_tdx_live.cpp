// TDX 实连验证工具 — 连接真实通达信主站验证完整协议链
#include "data/tdx/tdx_provider.h"
#include "data/tdx/tdx_protocol.h"
#include "data/tdx/tdx_socket.h"
#include "foundation/utils/datetime.h"
#include <QCoreApplication>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace st;

namespace {

void printBar(const Bar& b) {
    std::printf("%s 开%.2f 高%.2f 低%.2f 收%.2f 量%.0f 额%.0f\n",
                utils::toDateTimeString(b.time).c_str(),
                b.open, b.high, b.low, b.close,
                static_cast<double>(b.volume), b.amount);
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    TdxProvider provider;
    provider.setRequestTimeoutMs(8000);
    provider.connect();
    std::this_thread::sleep_for(std::chrono::seconds(4));
    std::cout << "isConnected=" << provider.isConnected() << std::endl;
    if (!provider.isConnected()) {
        std::cout << "连接失败" << std::endl;
        return 1;
    }

    const StockCode code(Market::SH, "600519");  // 贵州茅台

    // 1. 日K线
    auto bars = provider.getBars(code, BarPeriod::Daily, DateTime{}, utils::now());
    std::cout << "[日K] bars=" << bars.size() << std::endl;
    if (!bars.empty()) {
        std::cout << "  最新: "; printBar(bars.back());
        std::cout << "  最早: "; printBar(bars.front());
    }

    // 2. 周K线
    auto wbars = provider.getBars(code, BarPeriod::Weekly, DateTime{}, utils::now());
    std::cout << "[周K] bars=" << wbars.size() << std::endl;
    if (!wbars.empty()) { std::cout << "  最新: "; printBar(wbars.back()); }

    // 3. 月K线
    auto mbars = provider.getBars(code, BarPeriod::Monthly, DateTime{}, utils::now());
    std::cout << "[月K] bars=" << mbars.size() << std::endl;
    if (!mbars.empty()) { std::cout << "  最新: "; printBar(mbars.back()); }

    // 3.5 分钟K线（5分/60分）
    auto m5 = provider.getBars(code, BarPeriod::Minute5, DateTime{}, utils::now());
    std::cout << "[5分K] bars=" << m5.size() << std::endl;
    if (!m5.empty()) {
        std::cout << "  最新: "; printBar(m5.back());
    }
    auto m60 = provider.getBars(code, BarPeriod::Minute60, DateTime{}, utils::now());
    std::cout << "[60分K] bars=" << m60.size() << std::endl;
    if (!m60.empty()) { std::cout << "  最新: "; printBar(m60.back()); }

    // 4. 分时
    auto intraday = provider.getIntraday(code);
    std::cout << "[分时] " << (intraday ? std::to_string(intraday->points.size()) : "FAIL")
              << " 点 preClose=" << (intraday ? intraday->preClose : 0.0) << std::endl;
    if (intraday && !intraday->points.empty()) {
        std::cout << "  前5: ";
        for (size_t i = 0; i < 5 && i < intraday->points.size(); ++i) {
            std::cout << utils::toDateTimeString(intraday->points[i].time).substr(11,5)
                      << "@" << intraday->points[i].price << " ";
        }
        std::cout << std::endl;
        const size_t n = intraday->points.size();
        std::cout << "  后5: ";
        for (size_t i = (n > 5 ? n - 5 : 0); i < n; ++i) {
            std::cout << utils::toDateTimeString(intraday->points[i].time).substr(11,5)
                      << "@" << intraday->points[i].price << " ";
        }
        std::cout << std::endl;
    }

    // 5. 实时报价
    auto quotes = provider.batchQuote({code});
    std::cout << "[报价] " << quotes.size() << " 条" << std::endl;
    if (!quotes.empty()) {
        const auto& q = quotes.front();
        std::cout << "  " << q.code.fullCode() << " 现价" << q.lastPrice
                  << " 昨收" << q.preClose << " 涨跌" << q.change << "%"
                  << " 量" << q.volume << " 额" << q.amount << std::endl;
    }

    // 6. 股票列表
    auto sh = provider.getStockList(Market::SH);
    std::cout << "[SH列表] " << sh.size() << " 只" << std::endl;
    if (!sh.empty()) {
        std::cout << "  首只: " << sh.front().code.fullCode() << " " << sh.front().name << std::endl;
        const StockCode sc(Market::SH, "600519");
        for (const auto& s : sh) {
            if (s.code == sc) {
                std::cout << "  找到茅台: " << s.name << std::endl;
                break;
            }
        }
    }

    provider.disconnect();
    return 0;
}
