#pragma once

// 策略模板共享目录（单一数据源）
//
// 策略 tab（StrategyPanel）与所有策略相关面板（优化/对比/压力/模拟/回测）
// 的下拉与参数向导都从这里读取，新增策略只需在此登记 + makeStrategy 注册
// + 引擎实现，各面板自动同步。
#include <QString>
#include <QVariantMap>
#include <vector>

namespace st::strategy_catalog {

/// 策略模板规格（含参数键名，供面板统一映射）
struct StrategySpec {
    QString id;         // 引擎注册 id（"MACross"/"Turtle"/"Momentum"/...）
    QString category;   // 类别（趋势跟踪/动量/突破/均值回归/反转）
    QString display;    // 显示名
    QString desc;       // 策略说明
    QString p1Name, p1Key;   // 参数 1 显示名 / 引擎参数键
    QString p2Name, p2Key;   // 参数 2 显示名 / 引擎参数键
    QString p1Desc, p2Desc;  // 参数说明（常显小字）
    int p1 = 0, p2 = 0;              // 默认值
    int p1Min = 0, p1Max = 0;        // 范围
    int p2Min = 0, p2Max = 0;
};

/// 全部模板（6 个，按类别分组排序）
inline const std::vector<StrategySpec>& all() {
    static const std::vector<StrategySpec> kSpecs = {
        {
            QStringLiteral("MACross"), QStringLiteral("趋势跟踪"),
            QStringLiteral("双均线策略"),
            QStringLiteral("快线上穿慢线金叉买入，下穿死叉清仓。经典趋势跟踪。"),
            QStringLiteral("快线周期"), QStringLiteral("fastPeriod"),
            QStringLiteral("慢线周期"), QStringLiteral("slowPeriod"),
            QStringLiteral("均线金叉的快线周期，越小越敏感"),
            QStringLiteral("均线金叉的慢线周期，越大越稳健"),
            5, 20, 1, 100, 2, 200,
        },
        {
            QStringLiteral("Turtle"), QStringLiteral("趋势跟踪"),
            QStringLiteral("海龟策略"),
            QStringLiteral("唐奇安通道突破：突破 N 日最高买入，跌破 M 日最低卖出。"),
            QStringLiteral("入场周期"), QStringLiteral("entryPeriod"),
            QStringLiteral("出场周期"), QStringLiteral("exitPeriod"),
            QStringLiteral("突破回看：突破 N 日最高价买入"),
            QStringLiteral("止损回看：跌破 M 日最低价清仓"),
            20, 10, 1, 120, 1, 120,
        },
        {
            QStringLiteral("Momentum"), QStringLiteral("动量"),
            QStringLiteral("动量策略"),
            QStringLiteral("N 日收益率突破阈值买入（趋势确认），收盘跌破 M 日均线离场。"),
            QStringLiteral("动量回看"), QStringLiteral("lookbackPeriod"),
            QStringLiteral("离场均线"), QStringLiteral("exitPeriod"),
            QStringLiteral("计算 N 日收益率（如 20 = 近 20 日涨幅）"),
            QStringLiteral("收盘跌破 M 日均线清仓（趋势破坏）"),
            20, 10, 2, 120, 2, 120,
        },
        {
            QStringLiteral("Breakout"), QStringLiteral("突破"),
            QStringLiteral("收盘突破策略"),
            QStringLiteral("收盘价突破 N 日最高收盘买入，跌破 M 日最低收盘离场。"
                           "与海龟不同：用收盘价确认突破，减少假突破。"),
            QStringLiteral("突破回看"), QStringLiteral("entryPeriod"),
            QStringLiteral("离场回看"), QStringLiteral("exitPeriod"),
            QStringLiteral("收盘突破 N 日最高收盘买入"),
            QStringLiteral("收盘跌破 M 日最低收盘清仓"),
            20, 10, 2, 120, 2, 120,
        },
        {
            QStringLiteral("MeanReversion"), QStringLiteral("均值回归"),
            QStringLiteral("均值回归策略"),
            QStringLiteral("收盘低于均线 X% 买入（超跌反弹），回到均线上方离场。"),
            QStringLiteral("均线周期"), QStringLiteral("maPeriod"),
            QStringLiteral("超跌阈值"), QStringLiteral("deviationPct"),
            QStringLiteral("偏离基准均线周期"),
            QStringLiteral("低于均线千分数阈值（如 30 = 3%）触发买入"),
            20, 30, 2, 120, 5, 200,
        },
        {
            QStringLiteral("Rsi"), QStringLiteral("反转"),
            QStringLiteral("RSI 策略"),
            QStringLiteral("RSI 超卖买入，超买离场（周期固定 14）。"),
            QStringLiteral("买入线"), QStringLiteral("buyLevel"),
            QStringLiteral("卖出线"), QStringLiteral("sellLevel"),
            QStringLiteral("RSI 低于该值买入（超卖，默认 30）"),
            QStringLiteral("RSI 高于该值清仓（超买，默认 70）"),
            30, 70, 5, 50, 50, 95,
        },
    };
    return kSpecs;
}

/// 按 id 查找（未找到返回 nullptr）
inline const StrategySpec* byId(const QString& id) {
    for (const auto& s : all()) {
        if (s.id == id) return &s;
    }
    return nullptr;
}

/// 由规格 + 两个参数值构造 QVariantMap（参数键名映射）
inline QVariantMap makeParams(const StrategySpec& s, int p1Value, int p2Value) {
    QVariantMap params;
    params[s.p1Key] = p1Value;
    params[s.p2Key] = p2Value;
    return params;
}

} // namespace st::strategy_catalog
