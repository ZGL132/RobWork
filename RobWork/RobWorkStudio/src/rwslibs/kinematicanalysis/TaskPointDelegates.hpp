#ifndef RWS_KINEMATICANALYSIS_TASKPOINTDELEGATES_HPP
#define RWS_KINEMATICANALYSIS_TASKPOINTDELEGATES_HPP

#include "KinematicAnalysisTypes.hpp"
#include "TaskPointTableModel.hpp"

#include <rw/models/WorkCell.hpp>

#include <QStyledItemDelegate>
#include <QStringList>
#include <QAbstractItemView>

namespace rws {

//! @brief combo box delegate:下拉列表给 type / refFrame / tcpFrame 列。
//! 强制值必须在给定列表内,避免拼写错误。
class ComboBoxDelegate : public QStyledItemDelegate
{
    Q_OBJECT
  public:
    ComboBoxDelegate (const QStringList& values, QObject* parent = nullptr);
    QWidget* createEditor (QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override;
    void setEditorData (QWidget* editor, const QModelIndex& index) const override;
    void setModelData (QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override;
  private:
    QStringList _values;
};

//! @brief double spin box delegate:数值列用,限制输入范围和精度。
//! 负 tolerance / 非有限数 / 明显越界值都被编辑器直接拒绝。
class DoubleSpinDelegate : public QStyledItemDelegate
{
    Q_OBJECT
  public:
    DoubleSpinDelegate (double minimum, double maximum, int decimals,
                        double step, QObject* parent = nullptr);
    QWidget* createEditor (QWidget* parent, const QStyleOptionViewItem&,
                           const QModelIndex&) const override;
    void setEditorData (QWidget* editor, const QModelIndex& index) const override;
    void setModelData (QWidget* editor, QAbstractItemModel* model,
                       const QModelIndex& index) const override;
  private:
    double _minimum;
    double _maximum;
    int _decimals;
    double _step;
};

//! @brief 一次性把 TaskPoint 表所有列的 delegate 装上。
//! frameNames / tcpNames 通常来自 WorkCell 的全部 frame 名字,加上常用特殊名。
//! 替换前会 delete 旧的同列 delegate(避免累积)。
void installTaskPointDelegates (
    QAbstractItemView* view,
    const QStringList& frameNames,
    const QStringList& tcpNames);

//! @brief 收集 WorkCell 全部 frame 名字(去重 + 排序),供 refFrame / tcpFrame 用。
QStringList collectWorkCellFrameNames (rw::models::WorkCell* workcell,
                                       const QString& extra = QString ());

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_TASKPOINTDELEGATES_HPP
