#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLEFILTERPROXYMODEL_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLEFILTERPROXYMODEL_HPP

#include "StructureOptimizationTypes.hpp"

#include <QSortFilterProxyModel>
#include <QString>

#include <optional>

namespace rws {

class StructureVariableFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit StructureVariableFilterProxyModel(QObject* parent = nullptr);

    void setKeyword(const QString& keyword);
    QString keyword() const;
    void setKindFilter(std::optional<StructureVariableKind> kind);
    std::optional<StructureVariableKind> kindFilter() const;

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;

private:
    void refreshFilter();

    QString _keyword;
    std::optional<StructureVariableKind> _kindFilter;
};

} // namespace rws

#endif // RWS_STRUCTUREOPTIMIZATION_STRUCTUREVARIABLEFILTERPROXYMODEL_HPP
