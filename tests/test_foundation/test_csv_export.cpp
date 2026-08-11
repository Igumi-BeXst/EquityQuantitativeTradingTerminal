#include "foundation/utils/csv.h"
#include "gtest/gtest.h"

using namespace st;

TEST(CsvExportTest, BasicTable) {
    const std::vector<std::vector<std::string>> rows = {
        {"代码", "名称", "价格"},
        {"600519", "贵州茅台", "1500.00"},
        {"000858", "五粮液", "130.50"},
    };
    const auto csv = csv::tableToCsv(rows);
    EXPECT_EQ(csv,
              std::string("\xEF\xBB\xBF") +          // UTF-8 BOM
              "代码,名称,价格\n"
              "600519,贵州茅台,1500.00\n"
              "000858,五粮液,130.50\n");
}

TEST(CsvExportTest, EscapesCommaAndQuote) {
    const std::vector<std::vector<std::string>> rows = {
        {"字段", "值"},
        {"注释", "他说\"好的\",然后离开"},   // 逗号+引号 → 转义
    };
    const auto csv = csv::tableToCsv(rows);
    EXPECT_NE(csv.find("\"他说\"\"好的\"\",然后离开\""), std::string::npos);
}

TEST(CsvExportTest, EmptyReturnsBomOnly) {
    const auto csv = csv::tableToCsv({});
    EXPECT_EQ(csv, std::string("\xEF\xBB\xBF"));
}

TEST(CsvExportTest, NumericStringsNotScientific) {
    const std::vector<std::vector<std::string>> rows = {
        {"值"},
        {"1500.000000"},
    };
    // 纯字符串导出不引入科学计数（调用方已格式化）
    EXPECT_EQ(csv::tableToCsv(rows), std::string("\xEF\xBB\xBF") + "值\n1500.000000\n");
}

TEST(CsvExportTest, MultiLineCellWrapped) {
    const std::vector<std::vector<std::string>> rows = {
        {"备注"},
        {"第一行\n第二行"},
    };
    const auto csv = csv::tableToCsv(rows);
    // 含换行的字段 → 包引号（joinRow 已处理）；BOM + 表头 + 行
    EXPECT_NE(csv.find("\"第一行\n第二行\""), std::string::npos);
}
