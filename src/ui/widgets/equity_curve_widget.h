#pragma once

#include <QWidget>
#include <QString>
#include <QColor>
#include <QPointF>
#include <vector>

class QMouseEvent;

namespace st {

/// 净值曲线小控件 — 单/多序列折线 + 渐变填充 + 1.0 基准虚线 + 图例
/// 支持：左侧 Y 轴刻度、底部 X 轴标签、0 轴参考线（可选）、鼠标悬停/点击查看数据。
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

    /// 设置 X 轴时间标签（与序列等长；为空时按索引显示）
    void setXLabels(const std::vector<QString>& labels);

    /// 是否强制把 0 纳入 Y 轴范围并绘制 0 轴参考线（回测净值图建议开启）
    void setIncludeZero(bool includeZero);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    std::vector<EquitySeries> series_;
    std::vector<QString> xLabels_;
    bool includeZero_ = false;
    int hoverIndex_ = -1;
    double hoverX_ = -1.0;
    bool hoverValid_ = false;
};

} // namespace st
