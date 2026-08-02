#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <vector>

namespace st {

/// 净值曲线小控件 — 单/多序列折线 + 渐变填充 + 1.0 基准虚线 + 右上角图例
class EquityCurveWidget : public QWidget {
    Q_OBJECT

public:
    /// 一条净值序列
    struct EquitySeries {
        QString name;
        QColor color;
        std::vector<double> data;
    };

    explicit EquityCurveWidget(QWidget* parent = nullptr);

    /// 多序列净值（策略对比/压力测试叠加用）
    void setSeries(const std::vector<EquitySeries>& seriesIn);

    /// 单序列净值（兼容旧调用，委托为单序列）
    void setData(const std::vector<double>& equity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<EquitySeries> series_;
};

} // namespace st
