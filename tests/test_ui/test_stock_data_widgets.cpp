#include "data/idata_provider.h"
#include "core/thread_pool.h"
#include "ui/panels/quant_window.h"
#include "ui/panels/stock_search_bar.h"
#include "ui/widgets/stock_pool_picker.h"

#include <gtest/gtest.h>
#include <QApplication>
#include <QFocusEvent>
#include <QEventLoop>
#include <QLineEdit>
#include <QTimer>
#include <atomic>

namespace st {
namespace {

class CountingProvider final : public IDataProvider {
public:
    bool connect() override { connected = true; return true; }
    void disconnect() override { connected = false; }
    bool isConnected() const override { return connected; }
    std::optional<StockInfo> getStockInfo(const StockCode&) override { return std::nullopt; }
    std::vector<StockInfo> getStockList(Market) override {
        stockListCalls.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    std::vector<Bar> getBars(const StockCode&, BarPeriod, DateTime, DateTime) override { return {}; }
    void subscribeQuote(const StockCode&) override {}
    void unsubscribeQuote(const StockCode&) override {}
    std::vector<Quote> batchQuote(const std::vector<StockCode>&) override { return {}; }
    std::optional<IntradayData> getIntraday(const StockCode&) override { return std::nullopt; }
    void refreshQuotes() override {}

    std::atomic<int> stockListCalls{0};
    bool connected = true;
};

void drainIoAndEvents() {
    ASSERT_TRUE(ThreadPool::ioPool()->waitForDone(1000));
    QApplication::processEvents();
}

TEST(StockDataWidgetsTest, HiddenStockPoolPickerDoesNotLoadUntilShown) {
    CountingProvider provider;
    StockPoolPicker picker(&provider);

    drainIoAndEvents();
    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 0);
    EXPECT_FALSE(picker.isReady());

    picker.show();
    QApplication::processEvents();
    drainIoAndEvents();

    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 2);
    EXPECT_TRUE(picker.isReady());
}

TEST(StockDataWidgetsTest, StockSearchBarLoadsOnlyWhenUsed) {
    CountingProvider provider;
    StockSearchBar searchBar(&provider);
    searchBar.show();
    QApplication::processEvents();
    drainIoAndEvents();

    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 0);

    auto* edit = searchBar.findChild<QLineEdit*>();
    ASSERT_NE(edit, nullptr);
    QFocusEvent focusIn(QEvent::FocusIn, Qt::OtherFocusReason);
    QApplication::sendEvent(edit, &focusIn);
    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 0);

    edit->setText(QStringLiteral("600000"));
    QEventLoop debounceLoop;
    QTimer::singleShot(150, &debounceLoop, &QEventLoop::quit);
    debounceLoop.exec();
    drainIoAndEvents();

    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 2);
}

TEST(QuantWindowLifecycleTest, OpeningDoesNotLoadHiddenTabStockLists) {
    CountingProvider provider;
    {
        QuantWindow window(&provider);
        window.show();
        QApplication::processEvents();
        drainIoAndEvents();

        EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 0);
        window.hide();
    }
    EXPECT_EQ(provider.stockListCalls.load(std::memory_order_relaxed), 0);
}

}  // namespace
}  // namespace st
