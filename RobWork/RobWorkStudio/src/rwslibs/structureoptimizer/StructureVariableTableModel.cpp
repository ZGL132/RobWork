#include "StructureVariableTableModel.hpp"

#include <QString>

using namespace rws;

namespace {

QString kindName(StructureVariableKind kind)
{
    switch (kind) {
        case StructureVariableKind::JointPositionX: return "JointPositionX";
        case StructureVariableKind::JointPositionY: return "JointPositionY";
        case StructureVariableKind::JointPositionZ: return "JointPositionZ";
        case StructureVariableKind::JointRotationRoll: return "JointRotationRoll";
        case StructureVariableKind::JointRotationPitch: return "JointRotationPitch";
        case StructureVariableKind::JointRotationYaw: return "JointRotationYaw";
        case StructureVariableKind::DhA: return "DhA";
        case StructureVariableKind::DhD: return "DhD";
        case StructureVariableKind::BaseHeight: return "BaseHeight";
        case StructureVariableKind::TcpOffsetX: return "TcpOffsetX";
        case StructureVariableKind::TcpOffsetY: return "TcpOffsetY";
        case StructureVariableKind::TcpOffsetZ: return "TcpOffsetZ";
        case StructureVariableKind::LinkRadius: return "LinkRadius";
        case StructureVariableKind::LinkWidth: return "LinkWidth";
        case StructureVariableKind::LinkHeight: return "LinkHeight";
    }
    return "Unknown";
}

} // namespace

StructureVariableTableModel::StructureVariableTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int StructureVariableTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_variables.size());
}

int StructureVariableTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant StructureVariableTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_variables.size()))
        return QVariant();

    const StructureDesignVariable& variable =
        _variables[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole && index.column() == EnabledColumn)
        return variable.enabled ? Qt::Checked : Qt::Unchecked;
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    switch (index.column()) {
        case IdColumn: return QString::fromStdString(variable.id);
        case LabelColumn: return QString::fromStdString(variable.label);
        case TargetColumn: return QString::fromStdString(variable.targetName);
        case KindColumn: return kindName(variable.kind);
        case CurrentColumn: return variable.currentValue;
        case MinimumColumn: return variable.minimum;
        case MaximumColumn: return variable.maximum;
        case StepColumn: return variable.step;
        case EnabledColumn: return variable.enabled;
        default: return QVariant();
    }
}

QVariant StructureVariableTableModel::headerData(int section,
                                                Qt::Orientation orientation,
                                                int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Vertical)
        return section + 1;

    switch (section) {
        case IdColumn: return "ID";
        case LabelColumn: return "名称";
        case TargetColumn: return "目标";
        case KindColumn: return "类型";
        case CurrentColumn: return "当前值";
        case MinimumColumn: return "最小值";
        case MaximumColumn: return "最大值";
        case StepColumn: return "步长";
        case EnabledColumn: return "启用";
        default: return QVariant();
    }
}

Qt::ItemFlags StructureVariableTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return itemFlags;
    itemFlags |= Qt::ItemIsEditable;
    if (index.column() == EnabledColumn)
        itemFlags |= Qt::ItemIsUserCheckable;
    return itemFlags;
}

bool StructureVariableTableModel::setData(const QModelIndex& index,
                                          const QVariant& value,
                                          int role)
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_variables.size()))
        return false;

    StructureDesignVariable& variable =
        _variables[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole && index.column() == EnabledColumn) {
        variable.enabled = value.toInt() == Qt::Checked;
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
            case IdColumn: variable.id = value.toString().toStdString(); break;
            case LabelColumn: variable.label = value.toString().toStdString(); break;
            case TargetColumn: variable.targetName = value.toString().toStdString(); break;
            case CurrentColumn: variable.currentValue = value.toDouble(); break;
            case MinimumColumn: variable.minimum = value.toDouble(); break;
            case MaximumColumn: variable.maximum = value.toDouble(); break;
            case StepColumn: variable.step = value.toDouble(); break;
            case EnabledColumn: variable.enabled = value.toBool(); break;
            default: return false;
        }
    } else {
        return false;
    }

    Q_EMIT dataChanged(index, index, {role});
    return true;
}

void StructureVariableTableModel::setVariables(
    const std::vector<StructureDesignVariable>& variables)
{
    beginResetModel();
    _variables = variables;
    endResetModel();
}

const std::vector<StructureDesignVariable>&
StructureVariableTableModel::variables() const
{
    return _variables;
}
