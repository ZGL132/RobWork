#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

class StructureVariableTableModel : public QAbstractTableModel
{
public:
    enum Column
    {
        IdColumn = 0,
        LabelColumn,
        TargetColumn,
        KindColumn,
        CurrentColumn,
        MinimumColumn,
        MaximumColumn,
        StepColumn,
        EnabledColumn,
        ColumnCount
    };

    explicit StructureVariableTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    void setVariables(const std::vector<StructureDesignVariable>& variables);
    const std::vector<StructureDesignVariable>& variables() const;

private:
    std::vector<StructureDesignVariable> _variables;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLETABLEMODEL_HPP
