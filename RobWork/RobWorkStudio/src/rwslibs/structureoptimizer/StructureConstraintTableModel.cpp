#include "StructureConstraintTableModel.hpp"

using namespace rws;

namespace {

QString constraintKindLabel(rws::StructureConstraintKind kind)
{
    switch (kind) {
        case rws::StructureConstraintKind::ModelValid: return "模型有效";
        case rws::StructureConstraintKind::RequiredTaskReachable: return "必达任务可达";
        case rws::StructureConstraintKind::RequiredTaskCollisionFree: return "必达任务无碰撞";
        case rws::StructureConstraintKind::MinimumJointMargin: return "最小关节裕度";
        case rws::StructureConstraintKind::MaximumTotalLength: return "最大总长度";
        case rws::StructureConstraintKind::MaximumBaseHeight: return "最大基座高度";
        case rws::StructureConstraintKind::MaximumCrossSection: return "最大横截面积";
        case rws::StructureConstraintKind::MaximumLinkSlenderness: return "最大长细比";
        case rws::StructureConstraintKind::MinimumWorkspaceCoverage: return "最小工作空间覆盖";
    }
    return QString();
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
        case LabelColumn: return "名称";
        case TargetColumn: return "目标";
        case KindColumn: return "类型";
        case ThresholdColumn: return "阈值";
        case SecondaryThresholdColumn: return "辅助阈值";
        case EnabledColumn: return "启用";
        case HardColumn: return "硬约束";
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
    return result;
}

bool StructureConstraintTableModel::setData(const QModelIndex& index, const QVariant& value,
                                            int role)
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_constraints.size()) ||
        (role != Qt::EditRole && role != Qt::CheckStateRole))
        return false;

    StructureConstraint& constraint = _constraints[static_cast<std::size_t>(index.row())];
    switch (index.column()) {
        case IdColumn: constraint.id = value.toString().toStdString(); break;
        case LabelColumn: constraint.label = value.toString().toStdString(); break;
        case TargetColumn: constraint.targetName = value.toString().toStdString(); break;
        case ThresholdColumn: constraint.threshold = value.toDouble(); break;
        case SecondaryThresholdColumn: constraint.secondaryThreshold = value.toDouble(); break;
        case EnabledColumn: constraint.enabled = value.toBool() || value.toInt() == Qt::Checked; break;
        case HardColumn: constraint.hard = value.toBool() || value.toInt() == Qt::Checked; break;
        default: return false;
    }
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

const std::vector<StructureConstraint>& StructureConstraintTableModel::constraints() const
{
    return _constraints;
}
