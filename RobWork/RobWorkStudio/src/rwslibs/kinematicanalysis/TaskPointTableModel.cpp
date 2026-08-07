// =====================================================================
// TaskPointTableModel：任务点表格的 QAbstractTableModel 实现。
//
// 本文件实现任务点表格的数据模型：列定义、单位换算、单元格读写、
// 行增删、分析结果回填与校验状态维护。列分为两类：
//   - 可编辑列(ColEnabled..ColNote)：任务点定义本身，UI 可编辑并即时校验；
//   - 结果列(ColStatus..ColCollision)：由批量 IK 分析产生的只读衍生值。
// 所有位置/姿态值内部统一以米/度存储，仅在显示与编辑时按当前单位换算，
// 切换单位只会刷新表头与显示值，不会改动底层数据。
// =====================================================================
#include "TaskPointTableModel.hpp"

#include <rwslibs/robotanalysiscore/RobotAnalysisValidation.hpp>

#include <QColor>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

using namespace rws;

namespace {

// Headers for editable task point columns and derived result columns.
// 表头顺序定义：前半部分为可编辑任务点字段，后半部分为分析结果衍生列。
// 列索引必须与 hpp 中的 TaskPointColumn 枚举一一对应，改动需两边同步。
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

// 任务点类型的显示文本：枚举转字符串，用于表格展示与 CSV 导出。
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

// 字符串解析回任务点类型：大小写不敏感，未知类型回退为 Generic。
// 与 taskPointTypeText 互为逆操作，保证编辑往返后类型不会漂移。
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

// 分析状态的本地显示文本，与 CSV 导出使用的状态字面量保持一致。
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

// 最优解的摘要文本：直接显示条件数；条件数为 inf 时显式输出 "inf"。
QString bestSolutionSummary (const KinematicIkSolution& s)
{
    if (std::isinf (s.conditionNumber))
        return QStringLiteral ("inf");
    return QString::number (s.conditionNumber, 'g', 6);
}

// 失败原因列表转逗号分隔文本；为空时显示 "-" 占位。
QString reasonText (const std::vector< KinematicFailureReason >& reasons)
{
    if (reasons.empty ())
        return QStringLiteral ("-");
    QStringList out;
    for (auto r : reasons)
        out << QString::fromLatin1 (rws::toString (r));
    return out.join (QStringLiteral (", "));
}

// 在给定 IK 结果中挑选“可用”的最优解：跳过失败或碰撞的解，
// 其余按评分升序取最小者；没有可用解时返回 nullptr。
// 各结果列共用该选择逻辑，保证状态、误差、条件数等取自同一个解。
const KinematicIkSolution* bestUsableSolutionLocal (const KinematicIkAnalysisResult& ik)
{
    const KinematicIkSolution* best = nullptr;
    for (const KinematicIkSolution& solution : ik.solutions) {
        if (solution.status == AnalysisStatus::Fail || solution.inCollision)
            continue;
        if (best == nullptr || solution.score < best->score)
            best = &solution;
    }
    return best;
}

// Parse only finite floating point values.
// 只接受有限浮点数：拒绝 NaN/Inf 与非法文本，避免脏数据混入任务点。
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
//  TaskPointTableModel 瀹炵幇
// =====================================================================

// 构造空模型；数据行通过 insertRows / setRowsFromTaskPoints 填充。
TaskPointTableModel::TaskPointTableModel (QObject* parent) :
    QAbstractTableModel (parent)
{}

// 无父索引时返回行数；本模型为扁平表格，带父索引(嵌套场景)一律返回 0。
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

// 静态表头文本：供 QTableView 与单元测试共用，避免表头字符串重复定义。
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

// 表头显示：横向返回带单位后缀的显示表头，纵向返回 1-based 行号。
QVariant TaskPointTableModel::headerData (
    int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return QVariant ();
    if (orientation == Qt::Horizontal)
        return displayHeaderText (section);
    return section + 1;
}

// 切换显示单位：仅改变表头与单元格的显示换算，不触碰 TaskPoint 内部
// 以米/度存储的原始值；单位未变化时直接短路返回，避免无谓的模型刷新。
void TaskPointTableModel::setDisplayUnits (
    KinematicLengthUnit lengthUnit, KinematicAngleUnit angleUnit)
{
    if (_lengthUnit == lengthUnit && _angleUnit == angleUnit)
        return;
    _lengthUnit = lengthUnit;
    _angleUnit = angleUnit;
    Q_EMIT headerDataChanged (Qt::Horizontal, ColX, ColOrientationError);
    if (!_rows.empty ())
        Q_EMIT dataChanged (index (0, ColX), index (rowCount () - 1, ColOrientationError));
}

// 生成带当前单位后缀的表头文本：单位相关列(位置/容差/姿态/误差)使用
// 动态后缀，其余列直接返回静态表头。
QString TaskPointTableModel::displayHeaderText (int column) const
{
    const QString length = QString::fromLatin1 (unitSuffix (_lengthUnit));
    const QString angle = QString::fromLatin1 (unitSuffix (_angleUnit));
    switch (column) {
        case ColX: return QStringLiteral ("x (%1)").arg (length);
        case ColY: return QStringLiteral ("y (%1)").arg (length);
        case ColZ: return QStringLiteral ("z (%1)").arg (length);
        case ColRoll: return QStringLiteral ("roll (%1)").arg (angle);
        case ColPitch: return QStringLiteral ("pitch (%1)").arg (angle);
        case ColYaw: return QStringLiteral ("yaw (%1)").arg (angle);
        case ColPosTol: return QStringLiteral ("posTol (%1)").arg (length);
        case ColOriTol: return QStringLiteral ("oriTol (%1)").arg (angle);
        case ColPositionError: return QStringLiteral ("posErr (%1)").arg (length);
        case ColOrientationError: return QStringLiteral ("oriErr (%1)").arg (angle);
        default: return headerText (column);
    }
}

// 列权限：ColNote 及之前的列为可编辑列(可选择/可用/可编辑)；
// 结果列只读；ColEnabled 额外支持勾选框(CheckStateRole)。
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

// 任务点转显示字符串：数值列先按当前单位换算，布尔列输出 true/false，
// 供 DisplayRole 与 EditRole 共用，保证表格展示与编辑起点一致。
QString TaskPointTableModel::taskPointToString (const TaskPoint& p, int column) const
{
    switch (column) {
        case ColEnabled:  return p.enabled ? QStringLiteral ("true") : QStringLiteral ("false");
        case ColId:       return QString::fromStdString (p.id);
        case ColName:     return QString::fromStdString (p.name);
        case ColType:     return taskPointTypeText (p.type);
        case ColRefFrame: return QString::fromStdString (p.refFrame);
        case ColTcpFrame: return QString::fromStdString (p.tcpFrame);
        case ColX:        return QString::number (displayLengthFromMeters (p.position[0], _lengthUnit));
        case ColY:        return QString::number (displayLengthFromMeters (p.position[1], _lengthUnit));
        case ColZ:        return QString::number (displayLengthFromMeters (p.position[2], _lengthUnit));
        case ColRoll:     return QString::number (displayAngleFromDegrees (p.rpyDeg[0], _angleUnit));
        case ColPitch:    return QString::number (displayAngleFromDegrees (p.rpyDeg[1], _angleUnit));
        case ColYaw:      return QString::number (displayAngleFromDegrees (p.rpyDeg[2], _angleUnit));
        case ColPosTol:   return QString::number (displayLengthFromMeters (p.tolerance.positionMeters, _lengthUnit));
        case ColOriTol:   return QString::number (displayAngleFromDegrees (p.tolerance.orientationDeg, _angleUnit));
        case ColFreeRoll: return p.tolerance.allowToolRollFree ?
                              QStringLiteral ("true") : QStringLiteral ("false");
        case ColWeight:   return QString::number (p.weight);
        case ColNote:     return QString::fromStdString (p.note);
        default:          return QString ();
    }
}

// 把编辑框字符串写回 TaskPoint 对应字段：数值列做有限性校验并换算回
// 米/度，布尔列接受多种真/假写法；非法输入返回 false 拒绝本次提交。
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
            return false;  // 鏈煡瀛楃涓?涓ユ牸澶辫触
        }
        case ColId:    p.id = trimmed.toStdString ();      return true;
        case ColName:  p.name = trimmed.toStdString ();    return true;
        case ColType:  p.type = parseTaskPointType (trimmed); return true;
        case ColRefFrame: p.refFrame = trimmed.toStdString (); return true;
        case ColTcpFrame: p.tcpFrame = trimmed.toStdString (); return true;
        case ColX: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[0] = metersFromDisplayLength (v, _lengthUnit); return true;
        }
        case ColY: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[1] = metersFromDisplayLength (v, _lengthUnit); return true;
        }
        case ColZ: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.position[2] = metersFromDisplayLength (v, _lengthUnit); return true;
        }
        case ColRoll: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[0] = degreesFromDisplayAngle (v, _angleUnit); return true;
        }
        case ColPitch: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[1] = degreesFromDisplayAngle (v, _angleUnit); return true;
        }
        case ColYaw: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.rpyDeg[2] = degreesFromDisplayAngle (v, _angleUnit); return true;
        }
        case ColPosTol: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.tolerance.positionMeters = metersFromDisplayLength (v, _lengthUnit); return true;
        }
        case ColOriTol: {
            double v;
            if (!safeParseDouble (trimmed, v)) return false;
            p.tolerance.orientationDeg = degreesFromDisplayAngle (v, _angleUnit); return true;
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

// 单元格数据查询：Qt::UserRole 返回稳定的任务点 ID(供排序/查找复用)，
// ColEnabled 走 CheckStateRole；结果列提供状态背景色与悬浮提示，
// 可编辑列提供校验警告的背景色与提示；最后统一输出显示文本。
QVariant TaskPointTableModel::data (const QModelIndex& index, int role) const
{
    if (!index.isValid ())
        return QVariant ();
    const int row = index.row ();
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return QVariant ();
    const TaskPointTableRow& r = _rows[static_cast<std::size_t> (row)];
    const int col = index.column ();

    if (role == Qt::UserRole)
        return QString::fromStdString (r.point.id);

    // Enabled uses CheckStateRole; display/edit roles keep string compatibility.
    if (col == ColEnabled) {
        if (role == Qt::CheckStateRole)
            return r.point.enabled ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::EditRole || role == Qt::DisplayRole)
            return r.point.enabled ? QStringLiteral ("true") : QStringLiteral ("false");
    }

    // Result columns are read-only and support status background/tooltips.
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

    // Editable task point columns also expose validation background/tooltips.
    if (role == Qt::BackgroundRole && !r.validationWarnings.empty ())
        return QColor (255, 224, 224);
    if (role == Qt::ToolTipRole && !r.validationWarnings.empty ()) {
        QStringList tip;
        for (const AnalysisWarning& w : r.validationWarnings)
            tip << QStringLiteral ("[%1] %2: %3")
                .arg (statusTextLocal (w.severity))
                .arg (QString::fromStdString (w.code))
                .arg (QString::fromStdString (w.message));
        return tip.join (QStringLiteral ("\n"));
    }

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (col <= ColNote)
            return taskPointToString (r.point, col);
        // Derived result columns display "-" until a result is available.
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
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") : QStringLiteral ("[...]");
            }
            case ColPositionError: {
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (
                        displayLengthFromMeters (best->positionErrorMeters, _lengthUnit), 'g', 6);
            }
            case ColOrientationError: {
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (
                        displayAngleFromDegrees (best->orientationErrorDeg, _angleUnit), 'g', 6);
            }
            case ColMinMargin: {
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    QString::number (best->minJointLimitMargin, 'g', 6);
            }
            case ColCondition: {
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                return best == nullptr ? QStringLiteral ("-") :
                    bestSolutionSummary (*best);
            }
            case ColCollision: {
                const KinematicIkSolution* best = bestUsableSolutionLocal (r.result.ik);
                if (best == nullptr) return QStringLiteral ("-");
                return best->inCollision ? QStringLiteral ("Yes") : QStringLiteral ("No");
            }
            default: return QString ();
        }
    }
    return QVariant ();
}

// 编辑提交：勾选框直接更新 enabled；文本列先校验再写回，并同步到已存在
// 的结果(保持 taskPoint 快照一致)，随后重算校验并通知视图整行刷新。
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
        Q_EMIT dataChanged (index, index, {Qt::CheckStateRole, Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    if (col <= ColNote &&
        (role == Qt::EditRole || role == Qt::DisplayRole)) {
        TaskPoint updated = r.point;
        if (!stringToTaskPointField (value.toString (), col, updated))
            return false;
        if (col == ColId && containsTaskPointId (updated.id, row))
            return false;
        r.point = updated;
        if (r.hasResult)
            r.result.taskPoint = r.point;
        recomputeValidation (r);
        Q_EMIT dataChanged (this->index (row, 0),
                          this->index (row, TaskPointColumnCount - 1),
                          {Qt::DisplayRole, Qt::EditRole,
                           Qt::BackgroundRole, Qt::ToolTipRole});
        return true;
    }
    return false;
}

// 插入行：在指定位置批量插入，自动生成不重复的默认 ID(P1/P2/...)与
// 默认字段，并通过 beginInsertRows/endInsertRows 通知视图。
bool TaskPointTableModel::insertRows (
    int row, int count, const QModelIndex& parent)
{
    if (parent.isValid () || row < 0 || count <= 0)
        return false;
    if (row > static_cast<int> (_rows.size ()))
        row = static_cast<int> (_rows.size ());
    std::set< std::string > usedIds;
    for (const TaskPointTableRow& existing : _rows)
        usedIds.insert (existing.point.id);
    int nextIdNumber = 1;
    const auto nextDefaultId = [&usedIds, &nextIdNumber] () {
        std::string id;
        do {
            id = QString ("P%1").arg (nextIdNumber++).toStdString ();
        } while (usedIds.find (id) != usedIds.end ());
        usedIds.insert (id);
        return id;
    };
    beginInsertRows (QModelIndex (), row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        TaskPointTableRow r;
        r.point.id   = nextDefaultId ();
        r.point.name = QString ("Task %1").arg (QString::fromStdString (r.point.id)).toStdString ();
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

// 删除行：越界或非法请求返回 false，成功后通知视图更新。
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

// 全量替换行(导入用)：重复的非空 ID 保留首次出现，每行重算校验，
// 通过 beginResetModel 整体重置视图；旧的分析结果一并清空。
void TaskPointTableModel::setRowsFromTaskPoints (const std::vector< TaskPoint >& points)
{
    beginResetModel ();
    _rows.clear ();
    _rows.reserve (points.size ());
    std::set< std::string > importedIds;
    for (const TaskPoint& p : points) {
        if (!p.id.empty () && !importedIds.insert (p.id).second)
            continue;
        TaskPointTableRow r;
        r.point = p;
        recomputeValidation (r);
        _rows.push_back (std::move (r));
    }
    _reachableRate = 0.0;
    endResetModel ();
}

// 追加单行(Import current TCP 等场景)：ID 已存在时返回 -1 拒绝重复，
// 成功返回新行号并通知视图。
int TaskPointTableModel::appendTaskPoint (const TaskPoint& point)
{
    if (containsTaskPointId (point.id))
        return -1;
    const int row = static_cast<int> (_rows.size ());
    beginInsertRows (QModelIndex (), row, row);
    TaskPointTableRow r;
    r.point = point;
    recomputeValidation (r);
    _rows.push_back (std::move (r));
    endInsertRows ();
    return row;
}

// 把单个分析结果写回指定行，仅刷新结果列区域的数据变化。
void TaskPointTableModel::setResultForRow (
    int row, const TaskPointReachabilityResult& result)
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return;
    _rows[static_cast<std::size_t> (row)].result   = result;
    _rows[static_cast<std::size_t> (row)].hasResult = true;
    Q_EMIT dataChanged (index (row, ColStatus),
                      index (row, TaskPointColumnCount - 1),
                      {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

// 批量写入分析结果(整表分析)：先按结果数对齐行数，再逐行回填，
// 空 ID 行用结果中的任务点 ID 补齐，并同步更新总可达率。
void TaskPointTableModel::setResults (
    const std::vector< TaskPointReachabilityResult >& results, double reachableRate)
{
    // Keep the model row count aligned with the result vector.
    const std::size_t n = results.size ();
    _rows.resize (n);
    for (std::size_t i = 0; i < n; ++i) {
        _rows[i].result    = results[i];
        _rows[i].hasResult = true;
        if (_rows[i].point.id.empty ())
            _rows[i].point.id = results[i].taskPoint.id;
    }
    _reachableRate = reachableRate;
    if (n == 0)
        return;
    Q_EMIT dataChanged (index (0, ColStatus),
                        index (static_cast<int> (n) - 1, TaskPointColumnCount - 1),
                        {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

// 按稳定任务点 ID 增量更新结果：仅更新 ID 唯一匹配的行，
// 重复 ID 导致歧义时跳过，避免结果错位覆盖。
void TaskPointTableModel::applyResultsByTaskId (
    const std::vector< TaskPointReachabilityResult >& results)
{
    for (const TaskPointReachabilityResult& result : results) {
        if (result.taskPoint.id.empty ())
            continue;
        int matchingRow = -1;
        for (int row = 0; row < rowCount (); ++row) {
            if (_rows[static_cast< std::size_t > (row)].point.id != result.taskPoint.id)
                continue;
            if (matchingRow >= 0) {
                matchingRow = -1;
                break;
            }
            matchingRow = row;
        }
        if (matchingRow >= 0)
            setResultForRow (matchingRow, result);
    }
}

// 清空所有行结果与可达率(导入/删除前调用)，防止旧结果污染新数据。
void TaskPointTableModel::clearAllResults ()
{
    for (TaskPointTableRow& r : _rows) {
        r.hasResult = false;
        r.result = TaskPointReachabilityResult {};
    }
    _reachableRate = 0.0;
    if (_rows.empty ())
        return;
    Q_EMIT dataChanged (index (0, ColStatus),
                      index (static_cast<int> (_rows.size ()) - 1,
                             TaskPointColumnCount - 1),
                      {Qt::DisplayRole, Qt::BackgroundRole, Qt::ToolTipRole});
}

// 全表触发校验，收集每行首条警告到 summary 摘要；返回是否任意行校验失败，
// 供上层决定是否允许继续分析/导出。
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
        Q_EMIT dataChanged (index (0, 0),
                          index (static_cast<int> (_rows.size ()) - 1,
                                 TaskPointColumnCount - 1),
                          {Qt::BackgroundRole, Qt::ToolTipRole});
    }
    return allValid;
}

// 清空所有行的校验警告标记，供下一次 validateAll 之前清掉过期背景/提示。
void TaskPointTableModel::clearValidationMarks ()
{
    for (TaskPointTableRow& r : _rows)
        r.validationWarnings.clear ();
    if (!_rows.empty ()) {
        Q_EMIT dataChanged (index (0, 0),
                          index (static_cast<int> (_rows.size ()) - 1,
                                 TaskPointColumnCount - 1),
                          {Qt::BackgroundRole, Qt::ToolTipRole});
    }
}

std::vector< std::vector< AnalysisWarning > >
// 导出所有行的校验警告，供 UI 逐行查看完整警告列表。
TaskPointTableModel::allValidationWarnings () const
{
    std::vector< std::vector< AnalysisWarning > > out;
    out.reserve (_rows.size ());
    for (const TaskPointTableRow& r : _rows)
        out.push_back (r.validationWarnings);
    return out;
}

// 导出全部任务点(CSV 导出 / 整表分析用)：保留原始行值，
// 校验由分析与导出路径负责，这里不做过滤。
std::vector< TaskPoint > TaskPointTableModel::taskPoints (QString* error) const
{
    std::vector< TaskPoint > out;
    if (error != nullptr)
        error->clear ();
    for (std::size_t i = 0; i < _rows.size (); ++i) {
        // Keep raw row values; validation is handled by analysis/export paths.
        out.push_back (_rows[i].point);
    }
    return out;
}

std::vector< TaskPointReachabilityResult >
// 导出有结果的行(报告导出 / Apply best Q 用)：跳过尚未分析的行，
// 并用当前表格值刷新结果中的任务点快照，保证导出一致。
TaskPointTableModel::results () const
{
    std::vector< TaskPointReachabilityResult > out;
    out.reserve (_rows.size ());
    for (const TaskPointTableRow& r : _rows) {
        if (!r.hasResult)
            continue;
        TaskPointReachabilityResult result = r.result;
        result.taskPoint = r.point;
        out.push_back (std::move (result));
    }
    return out;
}

// 检查 ID 是否已存在(可排除指定行)：空 ID 不算重复，
// 用于新增/编辑时保证 ID 唯一性。
bool TaskPointTableModel::containsTaskPointId (const std::string& id, int exceptRow) const
{
    if (id.empty ())
        return false;
    for (int row = 0; row < rowCount (); ++row) {
        if (row != exceptRow && _rows[static_cast< std::size_t > (row)].point.id == id)
            return true;
    }
    return false;
}

// 单行只读访问器：越界返回空对象/空结果，供 UI 与测试使用。
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

// 返回指定行结果中的“可用”最优解指针；无结果或不可用时返回 nullptr。
const KinematicIkSolution* TaskPointTableModel::bestUsableSolutionForRow (int row) const
{
    if (row < 0 || static_cast<std::size_t> (row) >= _rows.size ())
        return nullptr;
    if (!_rows[static_cast<std::size_t> (row)].hasResult)
        return nullptr;
    return bestUsableSolutionLocal (_rows[static_cast<std::size_t> (row)].result.ik);
}

// 判定指定行是否存在可用结果：存在非碰撞且非失败状态的最优解。
bool TaskPointTableModel::hasUsableResult (int row) const
{
    const KinematicIkSolution* best = bestUsableSolutionForRow (row);
    return best != nullptr && !best->inCollision &&
           best->status != AnalysisStatus::Fail;
}

// 对单行任务点重新运行 RobotAnalysisValidation，刷新其校验警告列表。
void TaskPointTableModel::recomputeValidation (TaskPointTableRow& row)
{
    row.validationWarnings =
        RobotAnalysisValidation::validateTaskPoint (row.point);
}
