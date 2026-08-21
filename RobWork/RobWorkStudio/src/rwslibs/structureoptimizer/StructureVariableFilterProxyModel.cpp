#include "StructureVariableFilterProxyModel.hpp"

#include "StructureVariableTableModel.hpp"

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

} // namespace

StructureVariableFilterProxyModel::StructureVariableFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void StructureVariableFilterProxyModel::setKeyword(const QString& keyword)
{
    const QString normalized = keyword.trimmed();
    if (_keyword == normalized)
        return;
    _keyword = normalized;
    refreshFilter();
}

QString StructureVariableFilterProxyModel::keyword() const
{
    return _keyword;
}

void StructureVariableFilterProxyModel::setKindFilter(
    std::optional<StructureVariableKind> kind)
{
    if (_kindFilter == kind)
        return;
    _kindFilter = kind;
    refreshFilter();
}

std::optional<StructureVariableKind> StructureVariableFilterProxyModel::kindFilter() const
{
    return _kindFilter;
}

void StructureVariableFilterProxyModel::refreshFilter()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
    endFilterChange();
#else
    invalidateFilter();
#endif
}

bool StructureVariableFilterProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex& sourceParent) const
{
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr)
        return false;

    const QModelIndex kindIndex = source->index(
        sourceRow, StructureVariableTableModel::KindColumn, sourceParent);
    const QString type = source->data(kindIndex, Qt::DisplayRole).toString();
    if (_kindFilter.has_value() && type != kindName(*_kindFilter))
        return false;
    if (_keyword.isEmpty())
        return true;

    for (const int column : {StructureVariableTableModel::IdColumn,
                             StructureVariableTableModel::LabelColumn,
                             StructureVariableTableModel::TargetColumn,
                             StructureVariableTableModel::KindColumn}) {
        const QModelIndex index = source->index(sourceRow, column, sourceParent);
        if (source->data(index, Qt::DisplayRole).toString().contains(
                _keyword, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool StructureVariableFilterProxyModel::lessThan(const QModelIndex& left,
                                                 const QModelIndex& right) const
{
    // 排序只发生在代理模型，源变量向量顺序保持不变，避免候选绑定因展示排序漂移。
    const QAbstractItemModel* source = sourceModel();
    if (source == nullptr)
        return false;
    const QVariant leftValue = source->data(left, Qt::EditRole);
    const QVariant rightValue = source->data(right, Qt::EditRole);
    if (leftValue.canConvert<double>() && rightValue.canConvert<double>())
        return leftValue.toDouble() < rightValue.toDouble();
    const QString leftText = source->data(left, Qt::DisplayRole).toString();
    const QString rightText = source->data(right, Qt::DisplayRole).toString();
    const int comparison = QString::compare(leftText, rightText, Qt::CaseInsensitive);
    return comparison == 0 ? left.row() < right.row() : comparison < 0;
}
