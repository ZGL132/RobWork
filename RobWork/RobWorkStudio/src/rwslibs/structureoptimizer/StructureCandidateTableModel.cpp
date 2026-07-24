#include "StructureCandidateTableModel.hpp"

using namespace rws;

StructureCandidateTableModel::StructureCandidateTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int StructureCandidateTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_candidates.size());
}

int StructureCandidateTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant StructureCandidateTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_candidates.size()) ||
        role != Qt::DisplayRole)
        return QVariant();

    const StructureCandidateResult& candidate =
        _candidates[static_cast<std::size_t>(index.row())];

    switch (index.column()) {
        case IndexColumn: return candidate.index;
        case FeasibleColumn: return candidate.feasible ? "是" : "否";
        case TotalScoreColumn: return candidate.totalScore;
        case ReachabilityColumn: return candidate.scores.reachability;
        case ManipulabilityColumn: return candidate.scores.manipulability;
        case JointMarginColumn: return candidate.scores.jointMargin;
        case CollisionColumn: return candidate.scores.collision;
        case TotalLengthColumn: return candidate.raw.totalKinematicLength;
        case ImprovementColumn: return candidate.scores.preference;
        default: return QVariant();
    }
}

QVariant StructureCandidateTableModel::headerData(int section,
                                                 Qt::Orientation orientation,
                                                 int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Vertical)
        return section + 1;

    switch (section) {
        case IndexColumn: return "排名";
        case FeasibleColumn: return "可行性";
        case TotalScoreColumn: return "总分";
        case ReachabilityColumn: return "可达率";
        case ManipulabilityColumn: return "操纵度";
        case JointMarginColumn: return "关节裕度";
        case CollisionColumn: return "碰撞安全";
        case TotalLengthColumn: return "总长度";
        case ImprovementColumn: return "相对基准改善";
        default: return QVariant();
    }
}

void StructureCandidateTableModel::setCandidates(
    const std::vector<StructureCandidateResult>& candidates)
{
    beginResetModel();
    _candidates = candidates;
    endResetModel();
}

const std::vector<StructureCandidateResult>&
StructureCandidateTableModel::candidates() const
{
    return _candidates;
}
