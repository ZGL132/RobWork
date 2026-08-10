#include "OptimizationTaskTableModel.hpp"

#include <QString>

using namespace rws;

OptimizationTaskTableModel::OptimizationTaskTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int OptimizationTaskTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_tasks.size());
}

int OptimizationTaskTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant OptimizationTaskTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_tasks.size()))
        return QVariant();

    const OptimizationTaskPoint& task = _tasks[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole) {
        if (index.column() == RequiredColumn)
            return task.required ? Qt::Checked : Qt::Unchecked;
        if (index.column() == EnabledColumn)
            return task.point.enabled ? Qt::Checked : Qt::Unchecked;
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return QVariant();

    switch (index.column()) {
        case IdColumn: return QString::fromStdString(task.point.id);
        case NameColumn: return QString::fromStdString(task.point.name);
        case RequiredColumn: return task.required;
        case EnabledColumn: return task.point.enabled;
        case XColumn: return task.point.position[0];
        case YColumn: return task.point.position[1];
        case ZColumn: return task.point.position[2];
        case RollColumn: return task.point.rpyDeg[0];
        case PitchColumn: return task.point.rpyDeg[1];
        case YawColumn: return task.point.rpyDeg[2];
        case RefFrameColumn: return QString::fromStdString(task.point.refFrame);
        case TcpFrameColumn: return QString::fromStdString(task.point.tcpFrame);
        case WeightColumn: return task.point.weight;
        default: return QVariant();
    }
}

QVariant OptimizationTaskTableModel::headerData(int section,
                                               Qt::Orientation orientation,
                                               int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant();
    if (orientation == Qt::Vertical)
        return section + 1;

    switch (section) {
        case IdColumn: return "ID";
        case NameColumn: return "Name";
        case RequiredColumn: return "Required";
        case EnabledColumn: return "Enabled";
        case XColumn: return "X";
        case YColumn: return "Y";
        case ZColumn: return "Z";
        case RollColumn: return "Roll";
        case PitchColumn: return "Pitch";
        case YawColumn: return "Yaw";
        case RefFrameColumn: return "Frame";
        case TcpFrameColumn: return "TCP";
        case WeightColumn: return "Weight";
        default: return QVariant();
    }
}

Qt::ItemFlags OptimizationTaskTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags itemFlags = QAbstractTableModel::flags(index);
    if (!index.isValid())
        return itemFlags;
    itemFlags |= Qt::ItemIsEditable;
    if (index.column() == RequiredColumn || index.column() == EnabledColumn)
        itemFlags |= Qt::ItemIsUserCheckable;
    return itemFlags;
}

bool OptimizationTaskTableModel::setData(const QModelIndex& index,
                                         const QVariant& value,
                                         int role)
{
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(_tasks.size()))
        return false;

    OptimizationTaskPoint& task = _tasks[static_cast<std::size_t>(index.row())];

    if (role == Qt::CheckStateRole) {
        if (index.column() == RequiredColumn)
            task.required = value.toInt() == Qt::Checked;
        else if (index.column() == EnabledColumn)
            task.point.enabled = value.toInt() == Qt::Checked;
        else
            return false;
    } else if (role == Qt::EditRole) {
        switch (index.column()) {
            case IdColumn: task.point.id = value.toString().toStdString(); break;
            case NameColumn: task.point.name = value.toString().toStdString(); break;
            case RequiredColumn: task.required = value.toBool(); break;
            case EnabledColumn: task.point.enabled = value.toBool(); break;
            case XColumn: task.point.position[0] = value.toDouble(); break;
            case YColumn: task.point.position[1] = value.toDouble(); break;
            case ZColumn: task.point.position[2] = value.toDouble(); break;
            case RollColumn: task.point.rpyDeg[0] = value.toDouble(); break;
            case PitchColumn: task.point.rpyDeg[1] = value.toDouble(); break;
            case YawColumn: task.point.rpyDeg[2] = value.toDouble(); break;
            case RefFrameColumn: task.point.refFrame = value.toString().toStdString(); break;
            case TcpFrameColumn: task.point.tcpFrame = value.toString().toStdString(); break;
            case WeightColumn: task.point.weight = value.toDouble(); break;
            default: return false;
        }
    } else {
        return false;
    }

    Q_EMIT dataChanged(index, index, {role});
    return true;
}

void OptimizationTaskTableModel::setTasks(
    const std::vector<OptimizationTaskPoint>& tasks)
{
    beginResetModel();
    _tasks = tasks;
    endResetModel();
}

int OptimizationTaskTableModel::appendTask(const OptimizationTaskPoint& task)
{
    const int row = static_cast<int>(_tasks.size());
    beginInsertRows(QModelIndex(), row, row);
    _tasks.push_back(task);
    endInsertRows();
    return row;
}

bool OptimizationTaskTableModel::removeTask(int row)
{
    if (row < 0 || row >= static_cast<int>(_tasks.size()))
        return false;
    beginRemoveRows(QModelIndex(), row, row);
    _tasks.erase(_tasks.begin() + row);
    endRemoveRows();
    return true;
}

const std::vector<OptimizationTaskPoint>& OptimizationTaskTableModel::tasks() const
{
    return _tasks;
}
