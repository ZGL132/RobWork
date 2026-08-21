#include "StructureCandidateTableModel.hpp"

#include <QStringList>

using namespace rws;

namespace {

QString statusText(const StructureCandidateResult& candidate)
{
    switch (candidate.status) {
        case StructureCandidateStatus::Feasible: return "Feasible";
        case StructureCandidateStatus::Infeasible: return "Infeasible";
        case StructureCandidateStatus::Failed: return "Failed";
        case StructureCandidateStatus::Canceled: return "Canceled";
        case StructureCandidateStatus::Pending:
            return candidate.warnings.empty() ? "Pending" : "Data Insufficient";
    }
    return "Unknown";
}

QString stageText(StructureEvaluationStage stage)
{
    return stage == StructureEvaluationStage::Verified ? "Verified" : "Quick";
}

} // namespace

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
        case FeasibleColumn: {
            const bool dataInsufficient = candidate.status == StructureCandidateStatus::Pending &&
                                          !candidate.warnings.empty();
            const bool shownFeasible = candidate.feasible && !dataInsufficient &&
                                        candidate.status != StructureCandidateStatus::Failed &&
                                        candidate.status != StructureCandidateStatus::Canceled &&
                                        candidate.status != StructureCandidateStatus::Infeasible;
            return shownFeasible ? "Yes" : "No";
        }
        case TotalScoreColumn: return candidate.totalScore;
        case ReachabilityColumn: return candidate.scores.reachability;
        case ManipulabilityColumn: return candidate.scores.manipulability;
        case JointMarginColumn: return candidate.scores.jointMargin;
        case CollisionColumn: return candidate.scores.collision;
        case TotalLengthColumn: return candidate.raw.totalKinematicLength;
        case ImprovementColumn: {
            const StructureCandidateResult* baseline =
                candidateByIndex(_baselineCandidateIndex);
            return baseline != nullptr ? candidate.totalScore - baseline->totalScore : 0.0;
        }
        case StatusColumn: return statusText(candidate);
        case StageColumn: return stageText(candidate.stage);
        case ViolationsColumn: {
            QStringList values;
            for (const std::string& violation : candidate.violatedConstraints)
                values << QString::fromStdString(violation);
            return values.join(", ");
        }
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
        case IndexColumn: return "#";
        case FeasibleColumn: return "Feasible";
        case TotalScoreColumn: return "Score";
        case ReachabilityColumn: return "Reachability";
        case ManipulabilityColumn: return "Manipulability";
        case JointMarginColumn: return "Joint Margin";
        case CollisionColumn: return "Collision-Free";
        case TotalLengthColumn: return "Length";
        case ImprovementColumn: return "Improvement";
        case StatusColumn: return "Status";
        case StageColumn: return "Evidence";
        case ViolationsColumn: return "Hard Violations";
        default: return QVariant();
    }
}

void StructureCandidateTableModel::setCandidates(
    const std::vector<StructureCandidateResult>& candidates)
{
    beginResetModel();
    _candidates = candidates;
    _baselineCandidateIndex = -1;
    endResetModel();
}

void StructureCandidateTableModel::setResult(const StructureOptimizationResult& result)
{
    beginResetModel();
    _candidates = result.candidates;
    _baselineCandidateIndex = result.baselineCandidateIndex;
    endResetModel();
}

const std::vector<StructureCandidateResult>&
StructureCandidateTableModel::candidates() const
{
    return _candidates;
}

const StructureCandidateResult*
StructureCandidateTableModel::candidateByIndex(int candidateIndex) const
{
    for (const StructureCandidateResult& candidate : _candidates) {
        if (candidate.index == candidateIndex)
            return &candidate;
    }
    return nullptr;
}
