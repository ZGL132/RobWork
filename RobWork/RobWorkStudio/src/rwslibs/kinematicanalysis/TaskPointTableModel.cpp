#include "TaskPointTableModel.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisValidation.hpp>

#include <QColor>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

using namespace rws;

namespace {

// 16 个原始字段的列头,result 列(17..26)用 status / reason 等。
const char* kHeaders[TaskPointColumnCount] = {
    "Enabled", "id", "name", "type",
    "refFrame", "tcpFrame",
    "x", "y", "z",
    "roll", "pitch", "yaw",
    "posTol", "oriTol", "freeRoll",
    "weight", "note",
    "status", "reason",
    "raw", "usable", "bestQ",
    "posErr (m)", "oriErr (deg)",
    "margin", "condition", "collision"
};

QString taskPointTypeText (TaskPointType t)
{
    switch (t) {
        case TaskPointType::Generic:  return QStringLiteral ("Generic");
        case TaskPointType::Pick:     return QStringLiteral ("Pick");
        case TaskPointType::Place:    return QStringLiteral ("Place");
        case TaskPointType::Weld:     return QStringLiteral ("Weld");
        case TaskPointType::Glue:     return QStringLiteral ("Glue");
        case TaskPointType::Inspect:  return QStringLiteral ("Inspect");
        case TaskPointType::Screw:    return QStringLiteral ("Screw");
        case TaskPointType::Custom:   return QStringLiteral ("Custom");
    }
    return QStringLiteral ("Generic");
}

TaskPointType parseTaskPointType (const QString& s)
{
    const QString t = s.trimmed ();
    if (t.compare ("Pick", Qt::CaseInsensitive) == 0)    return TaskPointType::Pick;
    if (t.compare ("Place", Qt::CaseInsensitive) == 0)   return TaskPointType::Place;
    if (t.compare ("Weld", Qt::CaseInsensitive) == 0)    return TaskPointType::Weld;
    if (t.compare ("Glue", Qt::CaseInsensitive) == 0)    return TaskPointType::Glue;
    if (t.compare ("Inspect", Qt::CaseInsensitive) == 0) return TaskPointType::Inspect;
    if (t.compare ("Screw", Qt::CaseInsensitive) == 0)   return TaskPointType::Screw;
    if (t.compare ("Custom", Qt::CaseInsensitive) == 0)  return TaskPointType::Custom;
    return TaskPointType::Generic;
}

QString statusTextLocal (AnalysisStatus s)
{
    switch (s) {
        case AnalysisStatus::Pass:    return QStringLiteral ("Pass");
        case AnalysisStatus::Warning: return QStringLiteral ("Warning");
        case AnalysisStatus::Fail:    return QStringLiteral ("Fail");
        case AnalysisStatus::Unknown:
        default:                      return QStringLiteral ("Unknown");
    }
}

QString bestSolutionSummary (const KinematicIkSolution& s)
{
    if (std::isinf (s.conditionNumber))
        return QStringLiteral ("inf");
    return QString::number (s.conditionNumber, 'g', 6);
}

QString reasonText (const std::vector< KinematicFailureReason >& reasons)
{
    if (reasons.empty ())
        return QStringLiteral ("-");
    QStringList out;
    for (auto r : reasons)
        out << QString::fromLatin1 (rws::toString (r));
    return out.join (QStringLiteral (", "));
}

// 检查整数字符串转 double 合法且 finite,数字列写回时复用。
bool safeParseDouble (const QString& s, double& out)
{
    bool ok = false;
    const double v = s.toDouble (&ok);
    if (ok && std::isfinite (v)) {
        out = v;
        return true;
    }
    return false;
}

}    // namespace

// =====================================================================
//  TaskPointTableModel 实现
// =====================================================================

TaskPointTableModel::TaskPointTableModel (QObject* parent) :
    QAbstractTableModel (parent)
{}

int TaskPointTableModel::rowCount (const QModelIndex& parent) const
{
    if (parent.isValid ())
        return 0;
    return static_cast<int> (_rows.size ());
}

int TaskPointTableModel::columnCount (const QModelIndex& parent) const
{
    if (parent.isValid ())
        return 0;
    return TaskPointColumnCount;
}

QString TaskPointTableModel::headerText (int column)
{
    if (column < 0 || column >= TaskPointColumnCount)
        return QString ();
    return QString::fromLatin1 (kHeaders[column]);
}

QStringList TaskPointTableModel::allHeaderTexts ()
{
    QStringList out;
    for (int i = 0; i < TaskPointColumnCount; ++i)
        out << headerText (i);
    return out;
}

QVariant TaskPointTableModel::headerData (
    int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant ();
    if (orientation == Qt::Horizontal)
        return headerText (section);
    return section + 1;
}

Qt::ItemFlags TaskPointTableModel::flags (const QModelIndex& index) const
{
    if (!index.isValid ())
        return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::NoItemFlags;
    if (index.column () <= ColNote)
        f |= Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
    else
        f |= Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    if (index.column () == ColEnabled)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

QString TaskPointTableModel::taskPointToString (const TaskPoint& p, int column) const
{
    switch (column) {
        case ColEnabled:  return p.enabled ? QStringLiteral ("true") : QStringLiteral ("false");
        case ColId:       return QString::fromStdString (p.id);
        case ColName:     return QString::fromStdString (p.name);
        case ColType:     return taskPointTypeText (p.type);
        case ColRefFrame: return QString::fromStdString (p.refFrame);
        case ColTcpFrame: return QString::fromStdString (p.tcpFrame);
        case ColX:        return QString::number (p.position[0]);
        case ColY:        return QString::number (p.position[1]);
        case ColZ:        return QString::number (p.position[2]);
        case ColRoll:     return QString::number (p.rpyDeg[0]);
        case ColPitch:    return QString::number (p.rpyDeg[1]);
        case ColYaw:      return QString::number (p.rpyDeg[2]);
        case ColPosTol:   return QString::number (p.tolerance.positionMeters);
        case ColOriTol:   return QString::number (p.tolerance.orientationDeg);
        case ColFreeRoll: return p.tolerance.allowToolRollFree ?
                              QStringLiteral ("true") : QStringLiteral ("false");
        case ColWeight:   return QString::number (p.weight);
        case ColNote:     return QString::fromStdString (p.note);
        default:          return QString ();
    }
}

bool TaskPointTableModel::stringToTaskPointField (
    const QString& s, int column, TaskPoint& p) const
{
    const QString trimmed = s.trimmed ();
    switch (column) {
        case ColEnabled: {
            const QString t = trimmed.toLower ();
            if (t == "true" || t == "1" || t == "yes" ||
                t == "y" || t == "on") {
                p.enabled = true;
                return true;
            }
            if (t == "false" || t == "0" || t == "no" ||
                t == "n" || t == "off" || t.isEmpty ()) {
                p.enabled = false;
                return true;
            }
            return false;  // 未知字符串,严格失败
        }
        case ColId:    p.id = trimmed.toStdString ();      return true;
        case ColName:  p.name = trimmed.toStdString ();    return true;
        case ColType:  p.type = parseTaskPointType (trimmed); return true;
        case ColRefFrame: p.refFrame = trimmed.toStdString (); return true;
        case ColTcpFrame: p.tcpFrame = trimmed.toStdString (); return true;
        case ColX: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[0] = v; return true;
        }
        case ColY: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[1] = v; return true;
        }
        case ColZ: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[2] = v; return true;
        }
        case ColRoll: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[0] = v; return true;
        }
        case ColPitch: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[1] = v; return true;
        }
        case ColYaw: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[2] = v; return true;
        }
        case ColPosTol: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.tolerance.positionMeters = v; return true;
        }
        case ColOriTol: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.tolerance.orientationDeg = v; return true;
        }
        case ColFreeRoll: {
            const QString t = trimmed.toLower ();
            if (t == "true" || t == "1" || t == "yes" ||
                t == "y" || t == "on") {
                p.tolerance.allowToolRollFree = true;
                return true;
            }
            if (t == "false" || t == "0" || t == "no" ||
                t == "n" || t == "off" || t.isEmpty ()) {
                p.tolerance.allowToolRollFree = false;
                return true;
            }
            return false;
        }
        case ColWeight: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.weight = v; return true;
        }
        case ColNote:  p.note = trimmed.toStdString (); return true;
        default:       return false;
    }
}

QVariant TaskPointTableModel::data (const QModelIndex& index, int role) const
{
    if (!index.isValid ())
        return QVariant ();
    const int row = index.row ();
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return QVariant ();
    const TaskPointTableRow& r = _rows[static_cast<std::size_t> (row)];
    const int col = index.column ();

    // Enabled 列:CheckStateRole 优先;其他 Role 用普通字符串。
    if (col == ColEnabled) {
        if (role == Qt::CheckStateRole)
            return r.point.enabled ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::EditRole || role == Qt::DisplayRole)
            return r.point.enabled ? QStringLiteral ("true") : QStringLiteral ("false");
    }

    // result 列(17..26):只读展示;支持 BackgroundRole / ToolTipRole。
    if (col >= ColStatus) {
        switch (role) {
            case Qt::DisplayRole:
            case Qt::EditRole:
                break;
            case Qt::BackgroundRole:
                if (col == ColStatus) {
                    switch (r.result.status) {
                        case AnalysisStatus::Fail:    return QColor (255, 224, 224);
                        case AnalysisStatus::Warning: return QColor (255, 247, 205);
                        case AnalysisStatus::Pass:    return QColor (224, 247, 224);
                        default: break;
                    }
                }
                return QVariant ();
            case Qt::ToolTipRole: {
                QStringList tip;
                tip << QStringLiteral ("status=%1").arg (statusTextLocal (r.result.status));
                if (!r.result.failureReasons.empty ())
                    tip << QStringLiteral ("reasons=%1")
                        .arg (reasonText (r.result.failureReasons));
                if (r.hasResult) {
                    tip << QStringLiteral ("raw=%1")
                        .arg (static_cast<int> (r.result.ik.rawCandidateCount));
                    tip << QStringLiteral ("usable=%1")
                        .arg (static_cast<int> (r.result.ik.usableSolutionCount));
                }
                return tip.join (QStringLiteral ("\n"));
            }
            default: return QVariant ();
        }
    }

    // 任务点定义列(0..16):也支持 BackgroundRole / ToolTipRole 表达 validation。
    if (role == Qt::BackgroundRole && !r.validationWarnings.empty ())
        return QColor (255, 224, 224);
    if (role == Qt::ToolTipRole && !r.validationWarnings.empty ()) {
        QStringList tip;
        for (const AnalysisWarning& w : r.validationWarnings)
            tip << QStringLiteral ("[%1] %2: %3")
                .arg (QString::fromLatin1 (rws::statusText (w.severity)))
                .arg (QString::fromStdString (w.code))
                .arg (QString::fromStdString (w.message));
        return tip.join (QStringLiteral ("\n"));
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (col <= ColNote)
            return taskPointToString (r.point, col);
        // result 列展示
        if (!r.hasResult)
            return QStringLiteral ("-");
        switch (col) {
            case ColStatus:    return statusTextLocal (r.result.status);
            case ColReason:    return reasonText (r.result.failureReasons);
            case ColRawCandidates:
                return QString::number (
                    static_cast<int> (r.result.ik.rawCandidateCount));
            case ColUsableSolutions:
                return QString::number (
                    static_cast<int> (r.result.ik.usableSolutionCount));
            case ColBestQ: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") : QStringLiteral ("[...]");
            }
            case ColPositionError: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (best->positionErrorMeters, 'g', 6);
            }
            case ColOrientationError: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (best->orientationErrorDeg, 'g', 6);
            }
            case ColMinMargin: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (best->minJointLimitMargin, 'g', 6);
            }
            case ColCondition: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    bestSolutionSummary (*best);
            }
            case ColCollision: {
                const KinematicIkSolution* best = bestUsableSolution (r.result.ik);
                if (best == nullptr) return QStringLiteral ("-");
                return best->inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No");
            }
            default: return QString ();
        }
    }
    return QVariant ();
}

bool TaskPointTableModel::setData (
    const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid ())
        return false;
    const int row = index.row ();
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return false;
    const int col = index.column ();
    TaskPointTableRow& r = _rows[static_cast<std::size_t> (row)];

    if (col == ColEnabled && role == Qt::CheckStateRole) {
        const Qt::CheckState state = value.value< Qt::CheckState > ();
        r.point.enabled = (state == Qt::Checked);
        emit dataChanged (index, index, {Qt::CheckStateRole, Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    if (col <= ColNote &&
        (role == Qt::EditRole || role == Qt::DisplayRole)) {
        TaskPoint before = r.point;
        if (!stringToTaskPointField (value.toString (), col, r.point))
            return false;
        // 写回成功:重新跑这一行的 validation,失败用浅红标出 + tooltip。
        recomputeValidation (r);
        emit dataChanged (this->index (row, 0),
                          this->index (row, TaskPointColumnCount - 1),
                          {Qt::DisplayRole, Qt::EditRole,
                           Qt::BackgroundRole, Qt::ToolTipRole});
        return true;
    }
    return false;
}

bool TaskPointTableModel::insertRows (
    int row, int count, const QModelIndex& parent)
{
    if (parent.isValid () || row < 0 || count <= 0)
        return false;
    if (row > static_cast<int> (_rows.size ()))
        row = static_cast<int> (_rows.size ());
    beginInsertRows (QModelIndex (), row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        TaskPointTableRow r;
        r.point.id   = QString ("P%1").arg (row + i + 1).toStdString ();
        r.point.name = QString ("Task %1").arg (row + i + 1).toStdString ();
        r.point.type = TaskPointType::Generic;
        r.point.refFrame = "WORLD";
        r.point.tcpFrame = "TCP";
        r.point.tolerance.positionMeters = 1e-3;
        r.point.tolerance.orientationDeg = 1.0;
        r.point.weight = 1.0;
        r.point.enabled = true;
        _rows.insert (_rows.begin () + row + i, r);
    }
    endInsertRows ();
    return true;
}

bool TaskPointTableModel::removeRows (
    int row, int count, const QModelIndex& parent)
{
    if (parent.isValid () || row < 0 || count <= 0)
        return false;
    if (row + count > static_cast<int> (_rows.size ()))
        return false;
    beginRemoveRows (QModelIndex (), row, row + count - 1);
    _rows.erase (_rows.begin () + row, _rows.begin () + row + count);
    endRemoveRows ();
    return true;
}

void TaskPointTableModel::setRowsFromTaskPoints (const std::vector< TaskPoint >& points)
{
    beginResetModel ();
    _rows.clear ();
    _rows.reserve (points.size ());
    for (const TaskPoint& p : points) {
        TaskPointTableRow r;
        r.point = p;
        recomputeValidation (r);
        _rows.push_back (std::move (r));
    }
    _reachableRate = 0.0;
    endResetModel ();
}

int TaskPointTableModel::appendTaskPoint (const TaskPoint& point)
{
    const int row = static_cast<int> (_rows.size ());
    beginInsertRows (QModelIndex (), row, row);
    TaskPointTableRow r;
    r.point = point;
    recomputeValidation (r);
    _rows.push_back (std::move (r));
    endInsertRows ();
    return row;
}

void TaskPointTableModel::setResultForRow (
    int row, const TaskPointReachabilityResult& result)
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return;
    _rows[static_cast<std::size_t> (row)].result   = result;
    _rows[static_cast<std::size_t> (row)].hasResult = true;
    emit dataChanged (index (row, ColStatus),
                      index (row, TaskPointColumnCount - 1),
                      {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

void TaskPointTableModel::setResults (
    const std::vector< TaskPointReachabilityResult >& results, double reachableRate)
{
    // 表格行数 = results 数 + 不再存在的(导入后删除等) → 用 results.size() 对齐,
    // 任何越界位置都清空结果。
    const std::size_t n = results.size ();
    _rows.resize (n);
    for (std::size_t i = 0; i < n; ++i) {
        _rows[i].result    = results[i];
        _rows[i].hasResult = true;
        if (_rows[i].point.id.empty ())
            _rows[i].point.id = results[i].taskPoint.id;
    }
    _reachableRate = reachableRate;
    emit dataChanged (index (0, ColStatus),
                      index (static_cast<int> (n) - 1, TaskPointColumnCount - 1),
                      {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

void TaskPointTableModel::clearAllResults ()
{
    for (TaskPointTableRow& r : _rows) {
        r.hasResult = false;
        r.result = TaskPointReachabilityResult {};
    }
    _reachableRate = 0.0;
    if (_rows.empty ())
        return;
    emit dataChanged (index (0, ColStatus),
                      index (static_cast<int> (_rows.size ()) - 1,
                             TaskPointColumnCount - 1),
                      {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

bool TaskPointTableModel::validateAll (QString* summary)
{
    if (summary != nullptr)
        summary->clear ();
    bool allValid = true;
    for (std::size_t i = 0; i < _rows.size (); ++i) {
        recomputeValidation (_rows[i]);
        if (_rows[i].validationWarnings.empty ())
            continue;
        allValid = false;
        if (summary == nullptr)
            continue;
        const AnalysisWarning& w = _rows[i].validationWarnings.front ();
        if (!summary->isEmpty ())
            *summary += QLatin1String ("\n");
        *summary += QStringLiteral ("Row %1 (%2): %3: %4")
            .arg (static_cast<int> (i + 1))
            .arg (QString::fromStdString (_rows[i].point.id))
            .arg (QString::fromStdString (w.code))
            .arg (QString::fromStdString (w.message));
    }
    if (!_rows.empty ()) {
        emit dataChanged (index (0, 0),
                          index (static_cast<int> (_rows.size ()) - 1,
                                 TaskPointColumnCount - 1),
                          {Qt::BackgroundRole, Qt::ToolTipRole});
    }
    return allValid;
}

void TaskPointTableModel::clearValidationMarks ()
{
    for (TaskPointTableRow& r : _rows)
        r.validationWarnings.clear ();
    if (!_rows.empty ()) {
        emit dataChanged (index (0, 0),
                          index (static_cast<int> (_rows.size ()) - 1,
                                 TaskPointColumnCount - 1),
                          {Qt::BackgroundRole, Qt::ToolTipRole});
    }
}

std::vector< std::vector< AnalysisWarning > >
TaskPointTableModel::allValidationWarnings () const
{
    std::vector< std::vector< AnalysisWarning > > out;
    out.reserve (_rows.size ());
    for (const TaskPointTableRow& r : _rows)
        out.push_back (r.validationWarnings);
    return out;
}

std::vector< TaskPoint > TaskPointTableModel::taskPoints (QString* error) const
{
    std::vector< TaskPoint > out;
    if (error != nullptr)
        error->clear ();
    for (std::size_t i = 0; i < _rows.size (); ++i) {
        // 简单复制;refFrame / tcpFrame 空值不补默认值,由
        // RobotAnalysisValidation 在导出 / Analyze 时拦截,
        // 与 P0 / P1 行为一致。
        out.push_back (_rows[i].point);
    }
    return out;
}

std::vector< TaskPointReachabilityResult >
TaskPointTableModel::results () const
{
    std::vector< TaskPointReachabilityResult > out;
    out.reserve (_rows.size ());
    for (const TaskPointTableRow& r : _rows) {
        if (r.hasResult)
            out.push_back (r.result);
        else
            out.push_back (TaskPointReachabilityResult {});
    }
    return out;
}

TaskPoint TaskPointTableModel::taskPointAt (int row) const
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return TaskPoint {};
    return _rows[static_cast<std::size_t> (row)].point;
}

TaskPointReachabilityResult TaskPointTableModel::resultAt (int row) const
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return TaskPointReachabilityResult {};
    return _rows[static_cast<std::size_t> (row)].result;
}

bool TaskPointTableModel::hasResultAt (int row) const
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return false;
    return _rows[static_cast<std::size_t> (row)].hasResult;
}

const KinematicIkSolution* TaskPointTableModel::bestUsableSolutionForRow (int row) const
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return nullptr;
    if (!_rows[static_cast<std::size_t> (row)].hasResult)
        return nullptr;
    return bestUsableSolution (_rows[static_cast<std::size_t> (row)].result.ik);
}

bool TaskPointTableModel::hasUsableResult (int row) const
{
    const KinematicIkSolution* best = bestUsableSolutionForRow (row);
    return best != nullptr && !best->inCollision &&
           best->status != AnalysisStatus::Fail;
}

void TaskPointTableModel::recomputeValidation (TaskPointTableRow& row)
{
    row.validationWarnings =
        RobotAnalysisValidation::validateTaskPoint (row.point);
}