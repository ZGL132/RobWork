#include "TaskPointDelegates.hpp"

#include <rw/kinematics/Frame.hpp>
#include <rw/kinematics/Kinematics.hpp>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSet>
#include <QStringList>

using namespace rws;

ComboBoxDelegate::ComboBoxDelegate (const QStringList& values, QObject* parent) :
    QStyledItemDelegate (parent), _values (values)
{}

QWidget* ComboBoxDelegate::createEditor (
    QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const
{
    QComboBox* editor = new QComboBox (parent);
    editor->addItems (_values);
    editor->setEditable (false);
    return editor;
}

void ComboBoxDelegate::setEditorData (QWidget* editor, const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast< QComboBox* > (editor);
    if (combo == nullptr)
        return;
    const QString value = index.data (Qt::EditRole).toString ();
    const int idx = combo->findText (value);
    combo->setCurrentIndex (idx >= 0 ? idx : 0);
}

void ComboBoxDelegate::setModelData (
    QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    QComboBox* combo = qobject_cast< QComboBox* > (editor);
    if (combo != nullptr)
        model->setData (index, combo->currentText (), Qt::EditRole);
}

DoubleSpinDelegate::DoubleSpinDelegate (
    double minimum, double maximum, int decimals, double step, QObject* parent) :
    QStyledItemDelegate (parent),
    _minimum (minimum), _maximum (maximum),
    _decimals (decimals), _step (step)
{}

QWidget* DoubleSpinDelegate::createEditor (
    QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const
{
    QDoubleSpinBox* editor = new QDoubleSpinBox (parent);
    editor->setRange (_minimum, _maximum);
    editor->setDecimals (_decimals);
    editor->setSingleStep (_step);
    editor->setKeyboardTracking (false);
    return editor;
}

void DoubleSpinDelegate::setEditorData (QWidget* editor, const QModelIndex& index) const
{
    QDoubleSpinBox* spin = qobject_cast< QDoubleSpinBox* > (editor);
    if (spin != nullptr)
        spin->setValue (index.data (Qt::EditRole).toDouble ());
}

void DoubleSpinDelegate::setModelData (
    QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const
{
    QDoubleSpinBox* spin = qobject_cast< QDoubleSpinBox* > (editor);
    if (spin != nullptr)
        model->setData (index, QString::number (spin->value (), 'g', 12), Qt::EditRole);
}

namespace {

// Replace one column delegate and release an old delegate owned by the view.
void replaceColumnDelegate (QAbstractItemView* view, int column,
                            QStyledItemDelegate* delegate)
{
    if (view == nullptr)
        return;
    QAbstractItemDelegate* oldDelegate = view->itemDelegateForColumn (column);
    view->setItemDelegateForColumn (column, delegate);
    if (oldDelegate != nullptr && oldDelegate->parent () == view)
        oldDelegate->deleteLater ();
}

}    // namespace

void rws::installTaskPointDelegates (
    QAbstractItemView* view,
    const QStringList& frameNames,
    const QStringList& tcpNames)
{
    QStringList typeValues;
    typeValues << QStringLiteral ("Generic") << QStringLiteral ("Pick")
               << QStringLiteral ("Place") << QStringLiteral ("Weld")
               << QStringLiteral ("Glue") << QStringLiteral ("Inspect")
               << QStringLiteral ("Screw") << QStringLiteral ("Custom");
    replaceColumnDelegate (view, ColType,
        new ComboBoxDelegate (typeValues, view));

    replaceColumnDelegate (view, ColRefFrame,
        new ComboBoxDelegate (frameNames, view));
    replaceColumnDelegate (view, ColTcpFrame,
        new ComboBoxDelegate (tcpNames, view));

    QStringList freeRollValues;
    freeRollValues << QStringLiteral ("true") << QStringLiteral ("false");
    replaceColumnDelegate (view, ColFreeRoll,
        new ComboBoxDelegate (freeRollValues, view));

    auto spin = [view] (double min, double max, int dec, double step) {
        return new DoubleSpinDelegate (min, max, dec, step, view);
    };
    replaceColumnDelegate (view, ColX,         spin (-1e3,  1e3, 6, 0.01));
    replaceColumnDelegate (view, ColY,         spin (-1e3,  1e3, 6, 0.01));
    replaceColumnDelegate (view, ColZ,         spin (-1e3,  1e3, 6, 0.01));
    replaceColumnDelegate (view, ColRoll,      spin (-360,  360, 6, 1.0));
    replaceColumnDelegate (view, ColPitch,     spin (-360,  360, 6, 1.0));
    replaceColumnDelegate (view, ColYaw,       spin (-360,  360, 6, 1.0));
    replaceColumnDelegate (view, ColPosTol,    spin ( 0,    10,  6, 0.001));
    replaceColumnDelegate (view, ColOriTol,    spin ( 0,    360, 6, 0.1));
    replaceColumnDelegate (view, ColWeight,    spin ( 0,    1e6, 6, 0.1));
}

QStringList rws::collectWorkCellFrameNames (
    rw::models::WorkCell* workcell, const QString& extra)
{
    QStringList out;
    if (!extra.isEmpty ())
        out << extra;
    if (workcell == nullptr)
        return out;
    QSet< QString > seen (out.begin (), out.end ());
    rw::kinematics::State state = workcell->getDefaultState ();
    const std::vector< rw::kinematics::Frame* > frames =
        rw::kinematics::Kinematics::findAllFrames (workcell->getWorldFrame (), state);
    for (const rw::kinematics::Frame* f : frames) {
        if (f == nullptr) continue;
        const QString name = QString::fromStdString (f->getName ());
        if (name.isEmpty () || seen.contains (name))
            continue;
        seen.insert (name);
        out << name;
    }
    return out;
}
