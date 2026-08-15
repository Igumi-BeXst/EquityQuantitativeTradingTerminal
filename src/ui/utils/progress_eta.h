#pragma once

#include <QElapsedTimer>
#include <QString>

namespace st::ui {

/// 进度时间估算辅助 — 记录开始时刻，按当前进度推算剩余时间
///
/// 用法：面板运行开始时 reset()；每次进度更新调用 format(pct) 得到
/// 「已用 12s · 预计剩余 3m 20s」文本。多线程场景下仅在主线程调用。
class ProgressEta {
public:
    void reset() { timer_.restart(); }

    /// 已用时间（秒）
    qint64 elapsedSec() const { return timer_.elapsed() / 1000; }

    /// 按进度百分比（0~100）估算剩余秒数；进度 <=0 时返回 -1（未知）
    qint64 remainingSec(double pct) const {
        if (pct <= 0.0) return -1;
        const double done = pct / 100.0;
        const double total = static_cast<double>(timer_.elapsed()) / done;
        return static_cast<qint64>(total - static_cast<double>(timer_.elapsed())) / 1000;
    }

    /// 格式化「已用 X · 预计剩余 Y」；进度未知时只显示已用
    static QString format(qint64 sec) {
        if (sec < 0) return QStringLiteral("--");
        const qint64 m = sec / 60;
        const qint64 s = sec % 60;
        if (m <= 0) return QStringLiteral("%1s").arg(s);
        return QStringLiteral("%1m%2s").arg(m).arg(s, 2, 10, QLatin1Char('0'));
    }

    /// 组装完整文本：已用 + 剩余
    QString text(double pct) const {
        const qint64 remain = remainingSec(pct);
        const QString used = format(elapsedSec());
        return remain < 0
            ? QStringLiteral("已用 %1").arg(used)
            : QStringLiteral("已用 %1 · 预计剩余 %2").arg(used, format(remain));
    }

private:
    QElapsedTimer timer_;
};

} // namespace st::ui
