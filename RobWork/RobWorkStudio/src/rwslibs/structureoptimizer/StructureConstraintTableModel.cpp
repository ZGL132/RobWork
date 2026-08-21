#include "StructureConstraintTableModel.hpp"

#include <cmath>

using namespace rws;

namespace {

QString constraintKindLabel(rws::StructureConstraintKind kind)
{
    switch (kind) {
        case rws::StructureConstraintKind::ModelValid: return "Model Valid";
        case rws::StructureConstraintKind::RequiredTaskReachable: return "Required Task Reachable";
        case rws::StructureConstraintKind::RequiredTaskCollisionFree: return "Required Task Collision-Free";
        case rws::StructureConstraintKind::MinimumJointMargin: return "Minimum Joint Margin";
        case rws::StructureConstraintKind::MaximumTotalLength: return "Maximum Total Length";
        case rws::StructureConstraintKind::MaximumBaseHeight: return "Maximum Base Height";
        case rws::StructureConstraintKind::MaximumCrossSection: return "Maximum Cross Section";
        case rws::StructureConstraintKind::MaximumLinkSlenderness: return "Maximum Link Slenderness";
        case rws::StructureConstraintKind::MinimumWorkspaceCoverage: return "Minimum Workspace Coverage";
    }
    return QString();
}

bool isSafetyConstraint(const StructureConstraint& constraint)
{
    // 模型有效性与碰撞安全约束属于 Must 规则，不能在 UI 中降级为软约束。
    return constraint.kind == StructureConstraintKind::ModelValid ||
           constraint.kind == StructureConstraintKind::RequiredTaskCollisionFree;
}

} // namespace

StructureConstraintTableModel::StructureConstraintTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int StructureConstraintTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_constraints.size());
}

int StructureConstraintTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant StructureConstraintTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_constraints.size()))
        return QVariant();

    const StructureConstraint& constraint =
        _constraints[static_cast<std::size_t>(index.row())];
    if (role == Qt::CheckStateRole) {
        if (index.column() == EnabledColumn)
            return constraint.enabled ? Qt::Checked : Qt::Unchecked;
        if (index.column() == HardColumn)
            return constraint.hard ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::UserRole && index.column() == HardColumn)
        return isSafetyConstraint(constraint);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    switch (index.column()) {
        case IdColumn: return QString::fromStdString(constraint.id);
        case LabelColumn: return QString::fromStdString(constraint.label);
        case TargetColumn: return QString::fromStdString(constraint.targetName);
        case KindColumn: return constraintKindLabel(constraint.kind);
        case ThresholdColumn: return constraint.threshold;
        case SecondaryThresholdColumn: return constraint.secondaryThreshold;
        case EnabledColumn: return constraint.enabled;
        case HardColumn: return constraint.hard;
        default: return QVariant();
    }
}

QVariant StructureConstraintTableModel::headerData(int section,
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
        case ThresholdColumn: return "Limit";
        case SecondaryThresholdColumn: return "Aux. Limit";
        case EnabledColumn: return "Enabled";
        case HardColumn: return "Hard";
        default: return QVariant();
    }
}

Qt::ItemFlags StructureConstraintTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags result = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    if (index.column() == EnabledColumn || index.column() == HardColumn)
        result |= Qt::ItemIsUserCheckable;
    if (index.column() == HardColumn && isSafetyConstraint(
            _constraints[static_cast<std::size_t>(index.row())]))
        result &= ~Qt::ItemIsEditable;
    return result;
}

bool StructureConstraintTableModel::setData(const QModelIndex& index, const QVariant& value,
                                            int role)
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_constraints.size()) ||
        (role != Qt::EditRole && role != Qt::CheckStateRole))
        return false;

    StructureConstraint updated = _constraints[static_cast<std::size_t>(index.row())];
    switch (index.column()) {
        case IdColumn: updated.id = value.toString().toStdString(); break;
        case LabelColumn: updated.label = value.toString().toStdString(); break;
        case TargetColumn: updated.targetName = value.toString().toStdString(); break;
        case ThresholdColumn: updated.threshold = value.toDouble(); break;
        case SecondaryThresholdColumn: updated.secondaryThreshold = value.toDouble(); break;
        case EnabledColumn: updated.enabled = value.toBool() || value.toInt() == Qt::Checked; break;
        case HardColumn: updated.hard = value.toBool() || value.toInt() == Qt::Checked; break;
        default: return false;
    }
    if (!std::isfinite(updated.threshold) || !std::isfinite(updated.secondaryThreshold)) {
        Q_EMIT editRejected(QStringLiteral("Constraint thresholds must be finite."));
        return false;
    }
    if (isSafetyConstraint(updated) && !updated.hard) {
        Q_EMIT editRejected(QStringLiteral("Safety constraints must remain hard constraints."));
        return false;
    }
    if (updated.id.empty()) {
        Q_EMIT editRejected(QStringLiteral("Constraint ID must not be empty."));
        return false;
    }
    _constraints[static_cast<std::size_t>(index.row())] = updated;
    Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::CheckStateRole});
    return true;
}

void StructureConstraintTableModel::setConstraints(
    const std::vector<StructureConstraint>& constraints)
{
    beginResetModel();
    _constraints = constraints;
    endResetModel();
}

int StructureConstraintTableModel::appendConstraint(const StructureConstraint& constraint)
{
    const int row = static_cast<int>(_constraints.size());
    beginInsertRows(QModelIndex(), row, row);
    _constraints.push_back(constraint);
    endInsertRows();
    return row;
}

bool StructureConstraintTableModel::removeConstraint(int row)
{
    if (row < 0 || row >= static_cast<int>(_constraints.size()))
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    _constraints.erase(_constraints.begin() + row);
    endRemoveRows();
    return true;
}

const std::vector<StructureConstraint>& StructureConstraintTableModel::constraints() const
{
    return _constraints;
}
