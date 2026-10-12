/****************************************************************************
** Meta object code from reading C++ file 'KinematicsPanelWidget.hpp'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.hpp"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'KinematicsPanelWidget.hpp' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t {};
} // unnamed namespace

template <> constexpr inline auto sdurws::ird::kinematics::KinematicsPanelWidget::qt_create_metaobjectdata<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "sdurws::ird::kinematics::KinematicsPanelWidget",
        "showStatusText",
        "",
        "text",
        "onSolveClicked",
        "onBatchClicked",
        "onCoverageRunClicked",
        "onResetHomeClicked",
        "onSetDefaultTcpClicked",
        "onSetDefaultDeviceClicked",
        "onExportJsonClicked",
        "onExportCsvClicked",
        "onSolutionSelectionChanged",
        "onSolutionDoubleClicked",
        "row",
        "column"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'showStatusText'
        QtMocHelpers::SlotData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'onSolveClicked'
        QtMocHelpers::SlotData<void()>(4, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onBatchClicked'
        QtMocHelpers::SlotData<void()>(5, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onCoverageRunClicked'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onResetHomeClicked'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSetDefaultTcpClicked'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSetDefaultDeviceClicked'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onExportJsonClicked'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onExportCsvClicked'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSolutionSelectionChanged'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSolutionDoubleClicked'
        QtMocHelpers::SlotData<void(int, int)>(13, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 14 }, { QMetaType::Int, 15 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<KinematicsPanelWidget, qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject sdurws::ird::kinematics::KinematicsPanelWidget::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>.metaTypes,
    nullptr
} };

void sdurws::ird::kinematics::KinematicsPanelWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<KinematicsPanelWidget *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->showStatusText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->onSolveClicked(); break;
        case 2: _t->onBatchClicked(); break;
        case 3: _t->onCoverageRunClicked(); break;
        case 4: _t->onResetHomeClicked(); break;
        case 5: _t->onSetDefaultTcpClicked(); break;
        case 6: _t->onSetDefaultDeviceClicked(); break;
        case 7: _t->onExportJsonClicked(); break;
        case 8: _t->onExportCsvClicked(); break;
        case 9: _t->onSolutionSelectionChanged(); break;
        case 10: _t->onSolutionDoubleClicked((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObject *sdurws::ird::kinematics::KinematicsPanelWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *sdurws::ird::kinematics::KinematicsPanelWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsPanelWidgetE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int sdurws::ird::kinematics::KinematicsPanelWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 11)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 11;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 11)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 11;
    }
    return _id;
}
namespace {
struct qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t {};
} // unnamed namespace

template <> constexpr inline auto sdurws::ird::kinematics::KinematicsConfigPanel::qt_create_metaobjectdata<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "sdurws::ird::kinematics::KinematicsConfigPanel",
        "showStatusText",
        "",
        "text",
        "onStrategyChanged",
        "index",
        "onApplyResult",
        "statusText"
    };

    QtMocHelpers::UintData qt_methods {
        // Slot 'showStatusText'
        QtMocHelpers::SlotData<void(const QString &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Slot 'onStrategyChanged'
        QtMocHelpers::SlotData<void(int)>(4, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
        // Slot 'onApplyResult'
        QtMocHelpers::SlotData<void(const QString &)>(6, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::QString, 7 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<KinematicsConfigPanel, qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject sdurws::ird::kinematics::KinematicsConfigPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>.metaTypes,
    nullptr
} };

void sdurws::ird::kinematics::KinematicsConfigPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<KinematicsConfigPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->showStatusText((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->onStrategyChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->onApplyResult((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *sdurws::ird::kinematics::KinematicsConfigPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *sdurws::ird::kinematics::KinematicsConfigPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN6sdurws3ird10kinematics21KinematicsConfigPanelE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int sdurws::ird::kinematics::KinematicsConfigPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 3)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 3;
    }
    return _id;
}
QT_WARNING_POP
