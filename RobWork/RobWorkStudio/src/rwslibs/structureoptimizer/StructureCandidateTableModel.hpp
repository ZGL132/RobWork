#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

class StructureCandidateTableModel : public QAbstractTableModel
{
public:
    enum Column
    {
        IndexColumn = 0,
        FeasibleColumn,
        TotalScoreColumn,
        ReachabilityColumn,
        ManipulabilityColumn,
        JointMarginColumn,
        CollisionColumn,
        TotalLengthColumn,
        ImprovementColumn,
        ColumnCount
    };

    explicit StructureCandidateTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setCandidates(const std::vector<StructureCandidateResult>& candidates);
    const std::vector<StructureCandidateResult>& candidates() const;

private:
    std::vector<StructureCandidateResult> _candidates;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATETABLEMODEL_HPP
