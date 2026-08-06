#include <gtest/gtest.h>
#include "foundation/utils/csv.h"
#include "foundation/utils/datetime.h"

using namespace st;

TEST(CsvTest, EscapeCommaQuoteNewline) {
    EXPECT_EQ(csv::escape("abc"), "abc");
    EXPECT_EQ(csv::escape("a,b"), "\"a,b\"");
    EXPECT_EQ(csv::escape("a\"b"), "\"a\"\"b\"");
    EXPECT_EQ(csv::escape("a\nb"), "\"a\nb\"");
}

TEST(CsvTest, JoinRowAssemblesWithCommas) {
    EXPECT_EQ(csv::joinRow({"日期", "a,b", "c\"d"}), "日期,\"a,b\",\"c\"\"d\"");
}

TEST(CsvTest, KlineToCsvRows) {
    StockCode code(Market::SH, "600519");
    std::vector<Bar> bars;
    Bar b;
    b.code = code;
    b.time = utils::parseDate("2024-01-02");
    b.open = 100.0;
    b.high = 110.0;
    b.low = 90.0;
    b.close = 105.0;
    b.volume = 10000;
    b.amount = 1050000.0;
    b.turnoverRate = 0.12;
    bars.push_back(b);

    const std::string text = csv::klineToCsv(bars);
    EXPECT_EQ(text.find("日期,开盘,最高,最低,收盘,成交量(股),成交额(元),换手率"), 0u);
    EXPECT_NE(text.find("2024-01-02,100.00,110.00,90.00,105.00,10000,1050000.00,0.1200"),
              std::string::npos);
}

TEST(CsvTest, EmptyBarsHeaderOnly) {
    EXPECT_EQ(csv::klineToCsv({}),
              "日期,开盘,最高,最低,收盘,成交量(股),成交额(元),换手率\n");
}
