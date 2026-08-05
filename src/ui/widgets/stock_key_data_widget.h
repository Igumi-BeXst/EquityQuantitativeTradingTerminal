#pragma once

#include "foundation/stock_code.h"
#include "foundation/tick.h"
#include <QString>
#include <QWidget>
#include <memory>
#include <vector>

class QLabel;
class QTimer;

namespace st {

class IDataProvider;

/// 个股关键数据 — 20 项（最高/最低/开盘/昨收/量/额/量比/振幅/涨停/跌停/
/// 换手率/换手率实/外盘/内盘/市盈静/市盈TTM/总市值/流通值/总股本/流通股）
///
/// 来自实时报价（batchQuote）+ 计算（振幅/涨停/跌停/量比）。
/// 换手率/市盈/市值/股本等基础数据本 TDX 服务器无财务命令（0x0A04 不支持），显示 "—"。
class StockKeyDataWidget : public QWidget {
    Q_OBJECT

public:
    explicit StockKeyDataWidget(IDataProvider* provider, QWidget* parent = nullptr);

    /// 切换到某只股票（立即刷新一次 + 异步取 5 日均量算量比）
    void setStock(const StockCode& code, const QString& name);
    void setPollIntervalMs(int ms);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onPoll();

private:
    void applyQuote(const Quote& q);
    void onBarsFetched(double avg5dVol, double lastVol, double lastAmt,
                       double prevVol, double prevAmt);
    void resetLabels();

    enum Field {
        FHigh, FLow, FOpen, FPreClose, FVolume, FAmount, FVolRatio, FAmplitude,
        FLimitUp, FLimitDown, FTurnover, FTurnoverReal, FOuter, FInner,
        FPeStatic, FPeTtm, FMarketCap, FFloatCap, FTotalShares, FFloatShares,
        kFieldCount,
    };

    IDataProvider* provider_ = nullptr;
    StockCode code_;
    QString name_;
    QTimer* timer_ = nullptr;
    bool polling_ = false;

    std::vector<QLabel*> values_;  // 与 Field 枚举一一对应
    double avg5dVol_ = 0.0;        // 5 日均量（股），量比分母
    bool barsLoaded_ = false;
    double lastVol_ = 0.0;         // 最近完成日量（股）
    double lastAmt_ = 0.0;         // 最近完成日额（元）
    double prevVol_ = 0.0;         // 前一日量（股，量额对比用）
    double prevAmt_ = 0.0;         // 前一日额（元）
};

} // namespace st
