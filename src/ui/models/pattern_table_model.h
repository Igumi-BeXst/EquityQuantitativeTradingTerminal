#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <vector>

namespace st {

/// 形态信号表 Model — 日期/形态名/置信度/方向/说明
class PatternTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum class Direction { Bullish, Bearish, Neutral };

    struct Row {
        QString date;                 // 触发 bar 日期
        QString name;                 // 形态中文名
        double confidence = 0.0;      // 置信度 (0~1)
        Direction direction = Direction::Neutral;
        QString description;          // 形态描述
    };

    explicit PatternTableModel(QObject* parent = nullptr);

    void setRows(const std::vector<Row>& rows);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    std::vector<Row> rows_;
};

} // namespace st
