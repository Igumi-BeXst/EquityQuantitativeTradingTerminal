#pragma once
#include <QAbstractTableModel>
namespace st { class StockListModel : public QAbstractTableModel { Q_OBJECT public: int rowCount(const QModelIndex&) const override { return 0; } int columnCount(const QModelIndex&) const override { return 0; } QVariant data(const QModelIndex&, int) const override { return {}; } }; }
