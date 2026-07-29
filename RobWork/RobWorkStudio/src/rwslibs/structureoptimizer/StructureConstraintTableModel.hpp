#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

class StructureConstraintTableModel : public QAbstractTableModel
{
public:
    enum Column
    {
        IdColumn = 0,
        LabelColumn,
        TargetColumn,
        KindColumn,
        ThresholdColumn,
        SecondaryThresholdColumn,
        EnabledColumn,
        HardColumn,
        ColumnCount
    };

    explicit StructureConstraintTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    void setConstraints(const std::vector<StructureConstraint>& constraints);
    int appendConstraint(const StructureConstraint& constraint);
    bool removeConstraint(int row);
    const std::vector<StructureConstraint>& constraints() const;

private:
    std::vector<StructureConstraint> _constraints;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTURECONSTRAINTTABLEMODEL_HPP
