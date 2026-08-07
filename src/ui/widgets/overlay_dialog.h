#pragma once

#include "engine/analyzer/overlay_analysis.h"
#include <QDialog>

class QTabWidget;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QCheckBox;

namespace st {

class IDataProvider;
class StockSearchBar;

/// 叠加目标选择对话框 — 三来源：指数/个股（搜索 + 快捷）、行业板块、概念板块
///
/// 模态（exec()）。选中即发 overlaySelected 并 accept；「清除叠加」发 clearRequested。
/// 板块列表按 tab 懒加载（IO 池异步，QPointer 守卫；已加载不重复拉取）。
/// 行业/概念来自通达信板块指数（880xxx/885xxx，走 TDX 主源，不受东财封锁影响）。
class OverlayDialog : public QDialog {
    Q_OBJECT

public:
    explicit OverlayDialog(IDataProvider* provider,
                           bool showRelativeStrength,
                           QWidget* parent = nullptr);

signals:
    /// 用户选定叠加目标（指数/个股/板块/概念；showRelativeStrength 供 K 线图副图）
    void overlaySelected(const OverlayTarget& target, bool showRelativeStrength);
    /// 用户请求清除当前叠加
    void clearRequested();

private:
    void loadSectorList(QListWidget* list, QLineEdit* search, SectorType type);
    void selectTarget(const OverlayTarget& target);

    IDataProvider* provider_ = nullptr;
    StockSearchBar* search_ = nullptr;
    QCheckBox* rsCheck_ = nullptr;
    bool busy_ = false;
    int gen_ = 0;
};

} // namespace st
