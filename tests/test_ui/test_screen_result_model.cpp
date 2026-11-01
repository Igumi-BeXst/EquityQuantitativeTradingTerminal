#include <gtest/gtest.h>

#include "ui/models/screen_result_model.h"

using namespace st;

TEST(ScreenResultModelTest, ShowsFactorScoreAndRawTooltip) {
    ScreenResult r;
    r.code = StockCode(Market::SH, "600000");
    r.totalScore = 88.5;

    FactorResult fr;
    fr.name = "alpha017";
    fr.rawValue = 5.748e29;  // Alpha017 类指数因子的极端原始值
    fr.score = 92.25;
    r.factorResults.push_back(fr);

    ScreenResultModel model;
    model.setResults({r}, {"alpha017"}, {});

    ASSERT_EQ(model.columnCount(), 5);
    EXPECT_EQ(model.headerData(4, Qt::Horizontal, Qt::DisplayRole).toString(),
              QString::fromUtf8("alpha017 得分"));
    EXPECT_EQ(model.data(model.index(0, 4), Qt::DisplayRole).toString(),
              QStringLiteral("92.25"));

    const QString tip = model.data(model.index(0, 4), Qt::ToolTipRole).toString();
    EXPECT_TRUE(tip.contains(QString::fromUtf8("原始值")));
    EXPECT_TRUE(tip.contains(QStringLiteral("e+")));
}

TEST(ScreenResultModelTest, MissingFactorValueShowsNeutralScore) {
    ScreenResult r;
    r.code = StockCode(Market::SZ, "000001");
    r.totalScore = 50.0;
    FactorResult fr;
    fr.name = "alpha001";
    fr.score = 50.0;  // 缺失 → 引擎给中性分
    r.factorResults.push_back(fr);

    ScreenResultModel model;
    model.setResults({r}, {"alpha001"}, {});
    EXPECT_EQ(model.data(model.index(0, 4), Qt::DisplayRole).toString(),
              QStringLiteral("50.00"));
    const QString tip = model.data(model.index(0, 4), Qt::ToolTipRole).toString();
    EXPECT_TRUE(tip.contains(QStringLiteral("--")));
}
