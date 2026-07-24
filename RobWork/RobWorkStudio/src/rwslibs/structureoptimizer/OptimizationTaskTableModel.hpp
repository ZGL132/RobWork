#ifndef RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QAbstractTableModel>

namespace rws {

class OptimizationTaskTableModel : public QAbstractTableModel
{
public:
    enum Column
    {
        IdColumn = 0,
        NameColumn,
        RequiredColumn,
        EnabledColumn,
        XColumn,
        YColumn,
        ZColumn,
        WeightColumn,
        ColumnCount
    };

    explicit OptimizationTaskTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role = Qt::EditRole) override;

    void setTasks(const std::vector<OptimizationTaskPoint>& tasks);
    const std::vector<OptimizationTaskPoint>& tasks() const;

private:
    std::vector<OptimizationTaskPoint> _tasks;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_OPTIMIZATIONTASKTABLEMODEL_HPP
