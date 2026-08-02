#pragma once

#include <QWidget>
#include <vector>

namespace st {

/// 净值曲线小控件 — 折线 + 渐变填充 + 1.0 基准虚线
class EquityCurveWidget : public QWidget {
    Q_OBJECT

public:
    explicit EquityCurveWidget(QWidget* parent = nullptr);

    /// 净值序列（NAV，初始 1.0）
    void setData(const std::vector<double>& equity);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<double> data_;
};

} // namespace st
