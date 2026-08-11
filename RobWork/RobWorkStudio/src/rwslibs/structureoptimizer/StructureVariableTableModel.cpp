#include "StructureVariableTableModel.hpp"

#include <QLocale>
#include <QString>

#include <algorithm>
#include <cmath>

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


QString formatValue(double value, const std::string& unit)
{
    const QString displayUnit = QString::fromStdString(unit).trimmed();
    int precision = 6;
    if (displayUnit == "m" || displayUnit == "mm" || displayUnit == "cm")
        precision = 3;
    else if (displayUnit == "deg" || displayUnit == "rad")
        precision = 2;

    const QString number = QLocale().toString(value, 'f', precision);
    return displayUnit.isEmpty() ? number : number + " " + displayUnit;
}

bool isEditableColumn(int column)
{
    return column == StructureVariableTableModel::CurrentColumn ||
           column == StructureVariableTableModel::MinimumColumn ||
           column == StructureVariableTableModel::MaximumColumn ||
           column == StructureVariableTableModel::StepColumn ||
           column == StructureVariableTableModel::PreferredColumn ||
           column == StructureVariableTableModel::PreferenceWeightColumn ||
           column == StructureVariableTableModel::EnabledColumn;
}

bool hasValidNumericValues(const StructureDesignVariable& variable, QString* error)
{
    if (!std::isfinite(variable.currentValue) || !std::isfinite(variable.minimum) ||
        !std::isfinite(variable.maximum) || !std::isfinite(variable.step)) {
        if (error != nullptr)
            *error = "Design variable values must be finite.";
        return false;
    }
    if (variable.minimum > variable.currentValue ||
        variable.currentValue > variable.maximum || variable.step <= 0.0) {
        if (error != nullptr)
            *error = "Design variable values must satisfy min <= current <= max and step > 0.";
        return false;
    }
    return true;
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
    if (role == Qt::TextAlignmentRole) {
        if (index.column() == EnabledColumn)
            return static_cast<int>(Qt::AlignCenter);
        if (index.column() >= CurrentColumn && index.column() <= PreferenceWeightColumn)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    const auto numericValue = [&variable, role](double value) {
        return role == Qt::DisplayRole ? QVariant(formatValue(value, variable.unit))
                                       : QVariant(value);
    };
    switch (index.column()) {
        case IdColumn: return QString::fromStdString(variable.id);
        case LabelColumn: return QString::fromStdString(variable.label);
        case TargetColumn: return QString::fromStdString(variable.targetName);
        case KindColumn: return kindName(variable.kind);
        case CurrentColumn: return numericValue(variable.currentValue);
        case MinimumColumn: return numericValue(variable.minimum);
        case MaximumColumn: return numericValue(variable.maximum);
        case StepColumn: return numericValue(variable.step);
        case PreferredColumn: return numericValue(variable.preferredValue);
        case PreferenceWeightColumn:
            return role == Qt::DisplayRole ? QVariant(QLocale().toString(
                variable.preferenceWeight, 'f', 3)) : QVariant(variable.preferenceWeight);
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
        case LabelColumn: return "Name";
        case TargetColumn: return "Target";
        case KindColumn: return "Type";
        case CurrentColumn: return "Current";
        case MinimumColumn: return "Min";
        case MaximumColumn: return "Max";
        case StepColumn: return "Step";
        case PreferredColumn: return "Preferred";
        case PreferenceWeightColumn: return "Preference Weight";
        case EnabledColumn: return "Enabled";
        default: return QVariant();
    }
}

Qt::ItemFlags StructureVariableTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return itemFlags;
    if (isEditableColumn(index.column()))
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

    if (role == Qt::CheckStateRole && index.column() == EnabledColumn) {
        _variables[static_cast<std::size_t>(index.row())].enabled =
            value.toInt() == Qt::Checked;
        Q_EMIT dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    if (role != Qt::EditRole || !isEditableColumn(index.column()))
        return false;

    StructureDesignVariable updated =
        _variables[static_cast<std::size_t>(index.row())];
    switch (index.column()) {
        case CurrentColumn: updated.currentValue = value.toDouble(); break;
        case MinimumColumn: updated.minimum = value.toDouble(); break;
        case MaximumColumn: updated.maximum = value.toDouble(); break;
        case StepColumn: updated.step = value.toDouble(); break;
        case PreferredColumn:
            return setPreferences(index.row(), value.toDouble(), updated.preferenceWeight);
        case PreferenceWeightColumn:
            return setPreferences(index.row(), updated.preferredValue, value.toDouble());
        case EnabledColumn: updated.enabled = value.toBool(); break;
        default: return false;
    }

    QString validationError;
    if (!hasValidNumericValues(updated, &validationError)) {
        Q_EMIT editRejected(validationError);
        return false;
    }
    _variables[static_cast<std::size_t>(index.row())] = updated;
    Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

void StructureVariableTableModel::setVariables(
    const std::vector<StructureDesignVariable>& variables)
{
    resetVariables(variables);
}

void StructureVariableTableModel::resetVariables(
    const std::vector<StructureDesignVariable>& variables)
{
    beginResetModel();
    _variables = variables;
    endResetModel();
}

bool StructureVariableTableModel::appendVariable(
    const StructureDesignVariable& variable)
{
    if (variable.id.empty() || std::any_of(_variables.begin(), _variables.end(),
                                            [&variable](const StructureDesignVariable& existing) {
                                                return existing.id == variable.id;
                                            }))
        return false;

    const int row = static_cast<int>(_variables.size());
    beginInsertRows(QModelIndex(), row, row);
    _variables.push_back(variable);
    endInsertRows();
    return true;
}

bool StructureVariableTableModel::removeVariable(int row)
{
    if (row < 0 || row >= static_cast<int>(_variables.size()))
        return false;

    beginRemoveRows(QModelIndex(), row, row);
    _variables.erase(_variables.begin() + row);
    endRemoveRows();
    return true;
}

int StructureVariableTableModel::duplicateVariable(int row)
{
    if (row < 0 || row >= static_cast<int>(_variables.size()))
        return -1;

    StructureDesignVariable copy = _variables[static_cast<std::size_t>(row)];
    const std::string prefix = copy.id + "_copy_";
    int suffix = 1;
    while (std::any_of(_variables.begin(), _variables.end(),
                       [&prefix, suffix](const StructureDesignVariable& variable) {
                           return variable.id == prefix + std::to_string(suffix);
                       })) {
        ++suffix;
    }
    copy.id = prefix + std::to_string(suffix);
    copy.label += " (Copy)";
    return appendVariable(copy) ? static_cast<int>(_variables.size()) - 1 : -1;
}

bool StructureVariableTableModel::setPreferences(
    int row, double preferredValue, double preferenceWeight)
{
    if (row < 0 || row >= static_cast<int>(_variables.size()))
        return false;
    if (!std::isfinite(preferredValue) || !std::isfinite(preferenceWeight) ||
        preferenceWeight < 0.0 || preferenceWeight > 1.0) {
        Q_EMIT editRejected("Preference value must be finite and weight must be between 0 and 1.");
        return false;
    }

    StructureDesignVariable& variable = _variables[static_cast<std::size_t>(row)];
    variable.preferredValue = preferredValue;
    variable.preferenceWeight = preferenceWeight;
    Q_EMIT dataChanged(index(row, PreferredColumn), index(row, PreferenceWeightColumn),
                       {Qt::DisplayRole, Qt::EditRole});
    return true;
}

int StructureVariableTableModel::removeRows(const QModelIndexList& indexes)
{
    std::vector<int> rows;
    rows.reserve(static_cast<std::size_t>(indexes.size()));
    for (const QModelIndex& index : indexes) {
        if (index.isValid() && index.model() == this && !index.parent().isValid() &&
            index.row() >= 0 && index.row() < static_cast<int>(_variables.size()))
            rows.push_back(index.row());
    }
    if (rows.empty())
        return 0;

    std::sort(rows.begin(), rows.end(), std::greater<int>());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    const int removed = static_cast<int>(rows.size());

    int high = rows.front();
    int low = high;
    const auto removeRange = [this](int first, int last) {
        beginRemoveRows(QModelIndex(), first, last);
        _variables.erase(_variables.begin() + first, _variables.begin() + last + 1);
        endRemoveRows();
    };
    for (std::size_t index = 1; index < rows.size(); ++index) {
        if (rows[index] == low - 1) {
            low = rows[index];
            continue;
        }
        removeRange(low, high);
        high = rows[index];
        low = high;
    }
    removeRange(low, high);
    return removed;
}

const std::vector<StructureDesignVariable>&
StructureVariableTableModel::variables() const
{
    return _variables;
}
