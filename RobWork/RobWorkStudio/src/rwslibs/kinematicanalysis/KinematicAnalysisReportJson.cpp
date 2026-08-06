// =============================================================================
//  KinematicAnalysisReportJson.cpp —— 分析报告 JSON / CSV 序列化实现
// =============================================================================
//
// 实现全部序列化与反序列化函数。本文件分为几大块:
//   - 数值 / 变换 / 数组的原子序列化辅助(finiteValue / transformToObject 等),
//     统一处理"非有限数值 -> JSON null"约定,并在写出入口置 nonFinite 标记;
//   - 各类结果结构的 toXxx / fromXxx 对称对(配置 / 任务点 / 区域 / 溯源等);
//   - KinematicAnalysisReportJson 公开入口(toObject / fromObject / JSON 往返);
//   - CSV 摘要导出(taskCsv / regionCsv)与视图过滤(filterReportView)。
//
// 反序列化策略:字段缺失或类型不符时,绝大多数返回 false(错误传播),仅当
// 报告结构演进需要向后兼容时才放行缺省(例如早期报告没有 level 字段按 Must)。
#include "KinematicAnalysisReportJson.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>
#include <cmath>
#include <limits>

namespace rws {
namespace {

// -----------------------------------------------------------------------------
// 数值 / 数组 / 变换的原子序列化辅助(匿名命名空间)
// -----------------------------------------------------------------------------
//
// finiteValue:把 double 序列化为 JSON 数值;非有限值(NaN / ±inf)转为 null。
// 若传入 nonFinite 指针,遇到非有限值会置 true,调用方据此在报告顶层
// 追加 KIN_REPORT_NONFINITE 告警 —— 保证 JSON 永远合法且不丢"有数据不合法"的信息。
QJsonValue finiteValue (double value, bool* nonFinite = nullptr)
{
    if (std::isfinite (value))
        return QJsonValue (value);
    if (nonFinite != nullptr)
        *nonFinite = true;
    return QJsonValue (QJsonValue::Null);
}

// doubleValue:把 JSON 值读回 double;null(对应写出时的非有限值)恢复为 NaN,
// 与 finiteValue 形成往返对称,使"写出 null -> 读回 NaN"可被调用方识别。
double doubleValue (const QJsonValue& value)
{
    return value.isNull () ? std::numeric_limits< double >::quiet_NaN () : value.toDouble ();
}

// requirementLevelToString:需求契约等级 -> 稳定字符串("Must"/"Should"/"Info")。
// 这些字符串是报告 JSON 的持久化格式,改名会破坏历史报告,故保持稳定。
const char* requirementLevelToString (RequirementExecutionLevel level)
{
    switch (level) {
    case RequirementExecutionLevel::Must: return "Must";
    case RequirementExecutionLevel::Should: return "Should";
    case RequirementExecutionLevel::Info: return "Info";
    }
    return "Unknown";
}

// requirementLevelFromString:字符串 -> 需求契约等级。
// 未知字符串返回 false 并(可选)写入错误描述;成功时清空 error。
bool requirementLevelFromString (const std::string& text,
                                 RequirementExecutionLevel& level,
                                 std::string* error)
{
    if (text == "Must") level = RequirementExecutionLevel::Must;
    else if (text == "Should") level = RequirementExecutionLevel::Should;
    else if (text == "Info") level = RequirementExecutionLevel::Info;
    else {
        if (error != nullptr)
            *error = "Unknown RequirementExecutionLevel value: " + text;
        return false;
    }
    if (error != nullptr)
        error->clear ();
    return true;
}

// doubleArray(std::vector):把 double 向量整体序列化为 JSON 数组。
// 逐元素走 finiteValue,统一应用"非有限 -> null"约定。
QJsonArray doubleArray (const std::vector< double >& values, bool* nonFinite = nullptr)
{
    QJsonArray result;
    for (double value : values)
        result.append (finiteValue (value, nonFinite));
    return result;
}

// doubleArray(std::array<3>):三元固定数组(double 位置 / RPY)的序列化重载。
QJsonArray doubleArray (const std::array< double, 3 >& values, bool* nonFinite = nullptr)
{
    QJsonArray result;
    for (double value : values)
        result.append (finiteValue (value, nonFinite));
    return result;
}

// transformToObject:把齐次变换序列化为 { position: [x,y,z],
// rotationRowMajor: 9 元旋转矩阵 }。旋转按行优先展开成平铺数组,
// 与 transformFromObject 的解析严格对称。
QJsonObject transformToObject (const rw::math::Transform3D<>& value, bool* nonFinite)
{
    QJsonObject result;
    result["position"] = doubleArray (
        std::array< double, 3 > {{value.P ()[0], value.P ()[1], value.P ()[2]}}, nonFinite);
    QJsonArray rotation;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column)
            rotation.append (finiteValue (value.R () (row, column), nonFinite));
    }
    result["rotationRowMajor"] = rotation;
    return result;
}

// transformFromObject:解析变换;position 须为 3 元、rotationRowMajor 须为 9 元,
// 否则返回 false。这是对"结构完整性"的严格校验,避免坏数据流入后续计算。
bool transformFromObject (const QJsonObject& object, rw::math::Transform3D<>& value)
{
    const QJsonArray position = object.value ("position").toArray ();
    const QJsonArray rotation = object.value ("rotationRowMajor").toArray ();
    if (position.size () != 3 || rotation.size () != 9)
        return false;
    const rw::math::Vector3D<> translation (
        doubleValue (position.at (0)), doubleValue (position.at (1)), doubleValue (position.at (2)));
    const rw::math::Rotation3D<> matrix (
        doubleValue (rotation.at (0)), doubleValue (rotation.at (1)), doubleValue (rotation.at (2)),
        doubleValue (rotation.at (3)), doubleValue (rotation.at (4)), doubleValue (rotation.at (5)),
        doubleValue (rotation.at (6)), doubleValue (rotation.at (7)), doubleValue (rotation.at (8)));
    value = rw::math::Transform3D<> (translation, matrix);
    return true;
}

// readDoubleArray:把 JSON 数组读入任意大小固定的容器(vector / array)。
// 要求 JSON 是数组且长度与容器当前大小一致,否则返回 false —— 防止
// 长度不匹配导致越界或静默截断。
template < class Container >
bool readDoubleArray (const QJsonValue& json, Container& output)
{
    if (!json.isArray ())
        return false;
    const QJsonArray array = json.toArray ();
    if (array.size () != static_cast< int > (output.size ()))
        return false;
    for (int index = 0; index < array.size (); ++index)
        output[static_cast< std::size_t > (index)] = doubleValue (array.at (index));
    return true;
}

// provenanceToObject:把"溯源信息"(需求 / 模型 / 环境指纹、编译器版本、
// 冻结时间、源路径)序列化。溯源是报告可信度与可复现性的核心,逐字段完整写出。
QJsonObject provenanceToObject (const RequirementExecutionProvenance& value)
{
    QJsonObject result;
    result["requirementFingerprint"] = QString::fromStdString (value.requirementFingerprint);
    result["robotModelFingerprint"] = QString::fromStdString (value.robotModelFingerprint);
    result["workcellFingerprint"] = QString::fromStdString (value.workcellFingerprint);
    result["environmentFingerprint"] = QString::fromStdString (value.environmentFingerprint);
    result["compilerVersion"] = QString::fromStdString (value.compilerVersion);
    result["frozenAt"] = QString::fromStdString (value.frozenAt);
    result["sourcePath"] = QString::fromStdString (value.sourcePath);
    return result;
}

// provenanceFromObject:解析溯源信息(与 provenanceToObject 对称)。
void provenanceFromObject (const QJsonObject& object, RequirementExecutionProvenance& value)
{
    value.requirementFingerprint = object.value ("requirementFingerprint").toString ().toStdString ();
    value.robotModelFingerprint = object.value ("robotModelFingerprint").toString ().toStdString ();
    value.workcellFingerprint = object.value ("workcellFingerprint").toString ().toStdString ();
    value.environmentFingerprint = object.value ("environmentFingerprint").toString ().toStdString ();
    value.compilerVersion = object.value ("compilerVersion").toString ().toStdString ();
    value.frozenAt = object.value ("frozenAt").toString ().toStdString ();
    value.sourcePath = object.value ("sourcePath").toString ().toStdString ();
}

// itemProvenanceToObject:序列化"条目级溯源"(来源 ID / 种类 + 编译诊断列表)。
// 诊断逐条包含 code / severity / requirementId / field / message / source,
// 供 UI 在结果表格中直接展示每个任务 / 区域的来源与编译期告警。
QJsonObject itemProvenanceToObject (const RequirementItemProvenance& value)
{
    QJsonObject result;
    result["sourceId"] = QString::fromStdString (value.sourceId);
    result["sourceKind"] = QString::fromStdString (value.sourceKind);
    QJsonArray diagnostics;
    for (const RequirementExecutionDiagnostic& diagnostic : value.diagnostics) {
        QJsonObject item;
        item["code"] = QString::fromStdString (diagnostic.code);
        item["severity"] = static_cast< int > (diagnostic.severity);
        item["requirementId"] = QString::fromStdString (diagnostic.requirementId);
        item["field"] = QString::fromStdString (diagnostic.field);
        item["message"] = QString::fromStdString (diagnostic.message);
        item["source"] = QString::fromStdString (diagnostic.source);
        diagnostics.append (item);
    }
    result["diagnostics"] = diagnostics;
    return result;
}

// itemProvenanceFromObject:解析条目级溯源与诊断列表(对称解析)。
void itemProvenanceFromObject (const QJsonObject& object, RequirementItemProvenance& value)
{
    value.sourceId = object.value ("sourceId").toString ().toStdString ();
    value.sourceKind = object.value ("sourceKind").toString ().toStdString ();
    value.diagnostics.clear ();
    for (const QJsonValue& json : object.value ("diagnostics").toArray ()) {
        const QJsonObject item = json.toObject ();
        RequirementExecutionDiagnostic diagnostic;
        diagnostic.code = item.value ("code").toString ().toStdString ();
        diagnostic.severity = static_cast< RequirementExecutionDiagnosticSeverity > (
            item.value ("severity").toInt ());
        diagnostic.requirementId = item.value ("requirementId").toString ().toStdString ();
        diagnostic.field = item.value ("field").toString ().toStdString ();
        diagnostic.message = item.value ("message").toString ().toStdString ();
        diagnostic.source = item.value ("source").toString ().toStdString ();
        value.diagnostics.push_back (diagnostic);
    }
}

// warningToObject:序列化单条分析告警(code / message / source / severity)。
QJsonObject warningToObject (const AnalysisWarning& warning)
{
    QJsonObject result;
    result["code"] = QString::fromStdString (warning.code);
    result["message"] = QString::fromStdString (warning.message);
    result["source"] = QString::fromStdString (warning.source);
    result["severity"] = static_cast< int > (warning.severity);
    return result;
}

// warningFromObject:解析单条分析告警(对称解析)。
AnalysisWarning warningFromObject (const QJsonObject& object)
{
    AnalysisWarning warning;
    warning.code = object.value ("code").toString ().toStdString ();
    warning.message = object.value ("message").toString ().toStdString ();
    warning.source = object.value ("source").toString ().toStdString ();
    warning.severity = static_cast< AnalysisStatus > (object.value ("severity").toInt ());
    return warning;
}

// currentPoseToObject:序列化"当前位姿结果" —— 状态、设备 / TCP 名、关节值、
// TCP 位置 / RPY、裕度、雅可比行列数与平铺数据、奇异值、告警等。
QJsonObject currentPoseToObject (const KinematicCurrentPoseResult& value, bool* nonFinite)
{
    QJsonObject result;
    result["status"] = static_cast< int > (value.status);
    result["deviceName"] = QString::fromStdString (value.deviceName);
    result["tcpFrameName"] = QString::fromStdString (value.tcpFrameName);
    result["q"] = doubleArray (value.q, nonFinite);
    result["tcpPosition"] = doubleArray (value.tcpPosition, nonFinite);
    result["tcpRpyDeg"] = doubleArray (value.tcpRpyDeg, nonFinite);
    result["jointLimitMargins"] = doubleArray (value.jointLimitMargins, nonFinite);
    result["minJointLimitMargin"] = finiteValue (value.minJointLimitMargin, nonFinite);
    result["jacobianRowMajor"] = doubleArray (value.jacobianRowMajor, nonFinite);
    result["jacobianRows"] = value.jacobianRows;
    result["jacobianCols"] = value.jacobianCols;
    result["singularValues"] = doubleArray (value.singularValues, nonFinite);
    result["conditionNumber"] = finiteValue (value.conditionNumber, nonFinite);
    result["manipulability"] = finiteValue (value.manipulability, nonFinite);
    QJsonArray warnings;
    for (const AnalysisWarning& warning : value.warnings)
        warnings.append (warningToObject (warning));
    result["warnings"] = warnings;
    return result;
}

// currentPoseFromObject:解析当前位姿结果(对称解析)。
// tcpPosition / tcpRpyDeg 用 readDoubleArray 严格校验长度。
bool currentPoseFromObject (const QJsonObject& object, KinematicCurrentPoseResult& value)
{
    value.status = static_cast< AnalysisStatus > (object.value ("status").toInt ());
    value.deviceName = object.value ("deviceName").toString ().toStdString ();
    value.tcpFrameName = object.value ("tcpFrameName").toString ().toStdString ();
    const QJsonArray q = object.value ("q").toArray ();
    value.q.clear ();
    for (const QJsonValue& item : q) value.q.push_back (doubleValue (item));
    if (!readDoubleArray (object.value ("tcpPosition"), value.tcpPosition) ||
        !readDoubleArray (object.value ("tcpRpyDeg"), value.tcpRpyDeg))
        return false;
    const QJsonArray margins = object.value ("jointLimitMargins").toArray ();
    value.jointLimitMargins.clear ();
    for (const QJsonValue& item : margins) value.jointLimitMargins.push_back (doubleValue (item));
    value.minJointLimitMargin = doubleValue (object.value ("minJointLimitMargin"));
    const QJsonArray jacobian = object.value ("jacobianRowMajor").toArray ();
    value.jacobianRowMajor.clear ();
    for (const QJsonValue& item : jacobian) value.jacobianRowMajor.push_back (doubleValue (item));
    value.jacobianRows = object.value ("jacobianRows").toInt ();
    value.jacobianCols = object.value ("jacobianCols").toInt ();
    const QJsonArray singular = object.value ("singularValues").toArray ();
    value.singularValues.clear ();
    for (const QJsonValue& item : singular) value.singularValues.push_back (doubleValue (item));
    value.conditionNumber = doubleValue (object.value ("conditionNumber"));
    value.manipulability = doubleValue (object.value ("manipulability"));
    value.warnings.clear ();
    for (const QJsonValue& item : object.value ("warnings").toArray ())
        value.warnings.push_back (warningFromObject (item.toObject ())); 
    return true;
}

// failureReasonsToArray:把失败原因列表序列化为字符串数组
// (用 toString 的稳定英文标识,如 "JointLimit"),便于人读与跨版本稳定。
QJsonArray failureReasonsToArray (const std::vector< KinematicFailureReason >& reasons)
{
    QJsonArray result;
    for (KinematicFailureReason reason : reasons)
        result.append (QString::fromLatin1 (toString (reason)));
    return result;
}

// failureReasonsFromArray:解析失败原因数组;未知字符串被忽略(不构成失败),
// 避免新增原因枚举导致旧报告解析失败。
std::vector< KinematicFailureReason > failureReasonsFromArray (const QJsonValue& json)
{
    std::vector< KinematicFailureReason > result;
    for (const QJsonValue& item : json.toArray ()) {
        KinematicFailureReason reason = KinematicFailureReason::None;
        if (kinematicFailureReasonFromString (item.toString ().toStdString (), reason))
            result.push_back (reason);
    }
    return result;
}

// configurationToObject:序列化一次"配置评估"(阶段 / 可行性 / 质量 / 溯源 /
// 关节值 / TCP 位姿 / 裕度 / 雅可比 / 奇异值 / 碰撞状态 / 失败原因)。
// 该结构是候选解的核心,完整写出以支持报告离线重分析。
QJsonObject configurationToObject (const ConfigurationEvaluation& value, bool* nonFinite)
{
    QJsonObject result;
    result["stage"] = QString::fromLatin1 (toString (value.stage));
    result["feasibility"] = QString::fromLatin1 (toString (value.feasibility));
    result["quality"] = QString::fromLatin1 (toString (value.quality));
    result["provenance"] = provenanceToObject (value.provenance);
    QJsonArray q;
    for (std::size_t index = 0; index < value.q.size (); ++index)
        q.append (finiteValue (value.q[index], nonFinite));
    result["q"] = q;
    result["tcpPose"] = transformToObject (value.tcpPose, nonFinite);
    result["jointLimitMargins"] = doubleArray (value.jointLimitMargins, nonFinite);
    result["jacobianRowMajor"] = doubleArray (value.jacobianRowMajor, nonFinite);
    result["jacobianRows"] = value.jacobianRows;
    result["jacobianCols"] = value.jacobianCols;
    result["singularValues"] = doubleArray (value.singularValues, nonFinite);
    result["minimumJointMargin"] = finiteValue (value.minimumJointMargin, nonFinite);
    result["conditionNumber"] = finiteValue (value.conditionNumber, nonFinite);
    result["manipulability"] = finiteValue (value.manipulability, nonFinite);
    result["collisionChecked"] = value.collisionChecked;
    result["inCollision"] = value.inCollision;
    result["failureReasons"] = failureReasonsToArray (value.failureReasons);
    return result;
}

// configurationFromObject:解析配置评估(对称解析)。
// 顶层枚举用 fromString 严格校验,失败即整体拒绝,避免损坏数据被误当作有效结果。
bool configurationFromObject (const QJsonObject& object, ConfigurationEvaluation& value)
{
    if (!analysisEvidenceStageFromString (object.value ("stage").toString ().toStdString (), value.stage) ||
        !feasibilityFromString (object.value ("feasibility").toString ().toStdString (), value.feasibility) ||
        !qualityFromString (object.value ("quality").toString ().toStdString (), value.quality))
        return false;
    provenanceFromObject (object.value ("provenance").toObject (), value.provenance);
    const QJsonArray q = object.value ("q").toArray ();
    value.q = rw::math::Q (static_cast< int > (q.size ()), 0.0);
    for (int index = 0; index < q.size (); ++index)
        value.q[static_cast< std::size_t > (index)] = doubleValue (q.at (index));
    if (object.contains ("tcpPose") &&
        !transformFromObject (object.value ("tcpPose").toObject (), value.tcpPose))
        return false;
    value.jointLimitMargins.clear ();
    for (const QJsonValue& item : object.value ("jointLimitMargins").toArray ())
        value.jointLimitMargins.push_back (doubleValue (item));
    const QJsonArray jacobian = object.value ("jacobianRowMajor").toArray ();
    value.jacobianRowMajor.clear ();
    for (const QJsonValue& item : jacobian)
        value.jacobianRowMajor.push_back (doubleValue (item));
    value.jacobianRows = object.value ("jacobianRows").toInt ();
    value.jacobianCols = object.value ("jacobianCols").toInt ();
    value.singularValues.clear ();
    for (const QJsonValue& item : object.value ("singularValues").toArray ())
        value.singularValues.push_back (doubleValue (item));
    value.minimumJointMargin = doubleValue (object.value ("minimumJointMargin"));
    value.conditionNumber = doubleValue (object.value ("conditionNumber"));
    value.manipulability = doubleValue (object.value ("manipulability"));
    value.collisionChecked = object.value ("collisionChecked").toBool ();
    value.inCollision = object.value ("inCollision").toBool ();
    value.failureReasons = failureReasonsFromArray (object.value ("failureReasons"));
    return true;
}

// targetToObject:序列化一个"任务点评估"(等级 / 目标 / 溯源 / 全部候选解 /
// 失败原因 / 告警)。候选解逐个完整写出,保证下游可从 JSON 恢复排序与评分。
QJsonObject targetToObject (const TargetEvaluation& value, bool* nonFinite)
{
    QJsonObject result;
    result["stage"] = QString::fromLatin1 (toString (value.stage));
    result["feasibility"] = QString::fromLatin1 (toString (value.feasibility));
    result["quality"] = QString::fromLatin1 (toString (value.quality));
    result["level"] = QString::fromLatin1 (requirementLevelToString (value.level));
    result["id"] = QString::fromStdString (value.target.id);
    result["name"] = QString::fromStdString (value.target.name);
    result["provenance"] = provenanceToObject (value.provenance);
    result["itemProvenance"] = itemProvenanceToObject (value.itemProvenance);
    QJsonArray candidates;
    for (const TargetCandidate& candidate : value.candidates) {
        QJsonObject item;
        item["configuration"] = configurationToObject (candidate.configuration, nonFinite);
        item["positionErrorMeters"] = finiteValue (candidate.positionErrorMeters, nonFinite);
        item["orientationErrorDeg"] = finiteValue (candidate.orientationErrorDeg, nonFinite);
        item["distanceToReferenceQ"] = finiteValue (candidate.distanceToReferenceQ, nonFinite);
        item["score"] = finiteValue (candidate.score, nonFinite);
        candidates.append (item);
    }
    result["candidates"] = candidates;
    result["failureReasons"] = failureReasonsToArray (value.failureReasons);
    QJsonArray warnings;
    for (const AnalysisWarning& warning : value.warnings)
        warnings.append (warningToObject (warning));
    result["warnings"] = warnings;
    return result;
}

// targetFromObject:解析任务点评估(对称解析)。候选解中任一配置解析失败即整体失败,
// 保证"报告里能读出来的候选"都是结构完整的。
bool targetFromObject (const QJsonObject& object,
                       TargetEvaluation& value,
                       std::string* error)
{
    if (!analysisEvidenceStageFromString (object.value ("stage").toString ().toStdString (), value.stage) ||
        !feasibilityFromString (object.value ("feasibility").toString ().toStdString (), value.feasibility) ||
        !qualityFromString (object.value ("quality").toString ().toStdString (), value.quality))
        return false;
    // 向后兼容:早期报告没有 level 字段,按其语义解释为 Must,保持旧报告有效。
    // Reports written before level was introduced remain valid and mean Must.
    if (object.contains ("level") &&
        !requirementLevelFromString (object.value ("level").toString ().toStdString (),
                                     value.level, error))
        return false;
    value.target.id = object.value ("id").toString ().toStdString ();
    value.target.name = object.value ("name").toString ().toStdString ();
    provenanceFromObject (object.value ("provenance").toObject (), value.provenance);
    itemProvenanceFromObject (object.value ("itemProvenance").toObject (), value.itemProvenance);
    value.candidates.clear ();
    for (const QJsonValue& json : object.value ("candidates").toArray ()) {
        const QJsonObject item = json.toObject ();
        TargetCandidate candidate;
        if (!configurationFromObject (item.value ("configuration").toObject (), candidate.configuration))
            return false;
        candidate.positionErrorMeters = doubleValue (item.value ("positionErrorMeters"));
        candidate.orientationErrorDeg = doubleValue (item.value ("orientationErrorDeg"));
        candidate.distanceToReferenceQ = doubleValue (item.value ("distanceToReferenceQ"));
        candidate.score = doubleValue (item.value ("score"));
        value.candidates.push_back (candidate);
    }
    value.failureReasons = failureReasonsFromArray (object.value ("failureReasons"));
    value.warnings.clear ();
    for (const QJsonValue& json : object.value ("warnings").toArray ())
        value.warnings.push_back (warningFromObject (json.toObject ()));
    return true;
}

// regionToObject:序列化一个"区域覆盖率评估" —— 区域元信息、覆盖率统计、
// 网格单元明细(每个单元的可达 / 采样次数与最佳指标)、失败原因与告警。
// 单元明细体积可能很大,但完整写出便于离线逐格回放分析。
QJsonObject regionToObject (const RegionCoverageResult& value, bool* nonFinite)
{
    QJsonObject result;
    result["stage"] = QString::fromLatin1 (toString (value.stage));
    result["feasibility"] = QString::fromLatin1 (toString (value.feasibility));
    result["quality"] = QString::fromLatin1 (toString (value.quality));
    result["regionId"] = QString::fromStdString (value.regionId);
    result["provenance"] = provenanceToObject (value.provenance);
    result["itemProvenance"] = itemProvenanceToObject (value.itemProvenance);
    result["totalCells"] = value.totalCells;
    result["reachableCells"] = value.reachableCells;
    result["sampledOrientations"] = value.sampledOrientations;
    result["reachableOrientations"] = value.reachableOrientations;
    result["positionCoverage"] = finiteValue (value.positionCoverage, nonFinite);
    result["orientationCoverage"] = finiteValue (value.orientationCoverage, nonFinite);
    QJsonArray cells;
    for (const RegionCellResult& cell : value.cells) {
        QJsonObject item;
        QJsonArray index;
        for (int axis : cell.index) index.append (axis);
        item["index"] = index;
        item["position"] = doubleArray (cell.position, nonFinite);
        item["feasibility"] = QString::fromLatin1 (toString (cell.feasibility));
        item["quality"] = QString::fromLatin1 (toString (cell.quality));
        item["reachableOrientationCount"] = cell.reachableOrientationCount;
        item["sampledOrientationCount"] = cell.sampledOrientationCount;
        item["bestManipulability"] = finiteValue (cell.bestManipulability, nonFinite);
        item["bestJointMargin"] = finiteValue (cell.bestJointMargin, nonFinite);
        item["failureReasons"] = failureReasonsToArray (cell.failureReasons);
        cells.append (item);
    }
    result["cells"] = cells;
    QJsonArray warnings;
    for (const AnalysisWarning& warning : value.warnings)
        warnings.append (warningToObject (warning));
    result["warnings"] = warnings;
    return result;
}

// regionFromObject:解析区域覆盖率评估(对称解析)。
// 单元索引与位置数组用长度校验,枚举用 fromString 校验,坏数据整体拒绝。
bool regionFromObject (const QJsonObject& object, RegionCoverageResult& value)
{
    if (!analysisEvidenceStageFromString (object.value ("stage").toString ().toStdString (), value.stage) ||
        !feasibilityFromString (object.value ("feasibility").toString ().toStdString (), value.feasibility) ||
        !qualityFromString (object.value ("quality").toString ().toStdString (), value.quality))
        return false;
    value.regionId = object.value ("regionId").toString ().toStdString ();
    provenanceFromObject (object.value ("provenance").toObject (), value.provenance);
    itemProvenanceFromObject (object.value ("itemProvenance").toObject (), value.itemProvenance);
    value.totalCells = object.value ("totalCells").toInt ();
    value.reachableCells = object.value ("reachableCells").toInt ();
    value.sampledOrientations = object.value ("sampledOrientations").toInt ();
    value.reachableOrientations = object.value ("reachableOrientations").toInt ();
    value.positionCoverage = doubleValue (object.value ("positionCoverage"));
    value.orientationCoverage = doubleValue (object.value ("orientationCoverage"));
    value.cells.clear ();
    for (const QJsonValue& json : object.value ("cells").toArray ()) {
        const QJsonObject item = json.toObject ();
        RegionCellResult cell;
        const QJsonArray index = item.value ("index").toArray ();
        if (index.size () != 3 || !readDoubleArray (item.value ("position"), cell.position))
            return false;
        for (int axis = 0; axis < 3; ++axis) cell.index[axis] = index.at (axis).toInt ();
        if (!feasibilityFromString (item.value ("feasibility").toString ().toStdString (), cell.feasibility) ||
            !qualityFromString (item.value ("quality").toString ().toStdString (), cell.quality))
            return false;
        cell.reachableOrientationCount = item.value ("reachableOrientationCount").toInt ();
        cell.sampledOrientationCount = item.value ("sampledOrientationCount").toInt ();
        cell.bestManipulability = doubleValue (item.value ("bestManipulability"));
        cell.bestJointMargin = doubleValue (item.value ("bestJointMargin"));
        cell.failureReasons = failureReasonsFromArray (item.value ("failureReasons"));
        value.cells.push_back (cell);
    }
    value.warnings.clear ();
    for (const QJsonValue& json : object.value ("warnings").toArray ())
        value.warnings.push_back (warningFromObject (json.toObject ()));
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// KinematicAnalysisReportJson 公开入口
// -----------------------------------------------------------------------------
//
// toObject:组装报告根对象。单个 nonFinite 标记贯穿整个序列化过程:
// 任一字段写出时出现非有限数值,即在报告顶层追加 KIN_REPORT_NONFINITE 告警,
// 让用户明确知道报告中存在被截断为 null 的数值。
QJsonObject KinematicAnalysisReportJson::toObject (const KinematicAnalysisReport& report)
{
    QJsonObject object;
    object["schemaVersion"] = report.schemaVersion;
    object["pluginName"] = QString::fromStdString (report.pluginName);
    object["analysisId"] = QString::fromStdString (report.analysisId);
    object["provenance"] = provenanceToObject (report.provenance);
    object["feasibility"] = QString::fromLatin1 (toString (report.feasibility));
    object["quality"] = QString::fromLatin1 (toString (report.quality));
    object["evidenceStage"] = QString::fromLatin1 (toString (report.evidenceStage));
    bool nonFinite = false;
    object["currentPose"] = currentPoseToObject (report.currentPose, &nonFinite);
    QJsonArray tasks;
    for (const TargetEvaluation& task : report.taskResults)
        tasks.append (targetToObject (task, &nonFinite));
    object["taskResults"] = tasks;
    QJsonArray regions;
    for (const RegionCoverageResult& region : report.regionResults)
        regions.append (regionToObject (region, &nonFinite));
    object["regionResults"] = regions;
    QJsonArray warnings;
    if (nonFinite) {
        QJsonObject warning;
        warning["code"] = QStringLiteral ("KIN_REPORT_NONFINITE");
        warning["message"] = QStringLiteral ("A non-finite numeric value was serialized as null.");
        warning["source"] = QStringLiteral ("KinematicAnalysisReportJson");
        warning["severity"] = static_cast< int > (AnalysisStatus::Warning);
        warnings.append (warning);
    }
    for (const AnalysisWarning& warning : report.warnings)
        warnings.append (warningToObject (warning));
    object["warnings"] = warnings;
    return object;
}

// fromObject:解析报告根对象。先做最小完整性检查(schemaVersion 与 currentPose
// 必须存在),再逐块解析;任何子块失败都返回 false 并给出 error,绝不半填报告。
bool KinematicAnalysisReportJson::fromObject (const QJsonObject& object,
                                              KinematicAnalysisReport& report,
                                              std::string* error)
{
    if (!object.contains ("schemaVersion") || !object.value ("schemaVersion").isDouble () ||
        !object.value ("currentPose").isObject ()) {
        if (error != nullptr) *error = "Kinematic report root is incomplete.";
        return false;
    }
    report.schemaVersion = object.value ("schemaVersion").toInt ();
    report.pluginName = object.value ("pluginName").toString ().toStdString ();
    report.analysisId = object.value ("analysisId").toString ().toStdString ();
    provenanceFromObject (object.value ("provenance").toObject (), report.provenance);
    if (!feasibilityFromString (object.value ("feasibility").toString ().toStdString (),
                                report.feasibility, error) ||
        !qualityFromString (object.value ("quality").toString ().toStdString (),
                            report.quality, error) ||
        !analysisEvidenceStageFromString (
            object.value ("evidenceStage").toString ().toStdString (), report.evidenceStage,
            error) ||
        !currentPoseFromObject (object.value ("currentPose").toObject (), report.currentPose))
        return false;
    report.warnings.clear ();
    for (const QJsonValue& item : object.value ("warnings").toArray ())
        report.warnings.push_back (warningFromObject (item.toObject ()));
    report.taskResults.clear ();
    for (const QJsonValue& item : object.value ("taskResults").toArray ()) {
        TargetEvaluation task;
        if (!targetFromObject (item.toObject (), task, error)) {
            if (error != nullptr && error->empty ())
                *error = "Kinematic report task result is invalid.";
            return false;
        }
        report.taskResults.push_back (task);
    }
    report.regionResults.clear ();
    for (const QJsonValue& item : object.value ("regionResults").toArray ()) {
        RegionCoverageResult region;
        if (!regionFromObject (item.toObject (), region)) {
            if (error != nullptr) *error = "Kinematic report region result is invalid.";
            return false;
        }
        report.regionResults.push_back (region);
    }
    if (error != nullptr) error->clear ();
    return true;
}

// toJson:报告 -> 紧凑 JSON 文本(无多余空白,便于存储与网络传输)。
std::string KinematicAnalysisReportJson::toJson (const KinematicAnalysisReport& report)
{
    return QJsonDocument (toObject (report)).toJson (QJsonDocument::Compact).toStdString ();
}

// fromJson:JSON 文本 -> 报告。先做语法解析(含错误定位),语法通过再交给
// fromObject 做结构校验;语法错误时经 error 返回解析器给出的错误字符串。
bool KinematicAnalysisReportJson::fromJson (const std::string& json,
                                            KinematicAnalysisReport& report,
                                            std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson (
        QByteArray::fromStdString (json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject ()) {
        if (error != nullptr) *error = parseError.errorString ().toStdString ();
        return false;
    }
    return fromObject (document.object (), report, error);
}

namespace {

// -----------------------------------------------------------------------------
// CSV 导出辅助(匿名命名空间)
// -----------------------------------------------------------------------------
//
// csvEscape:按 RFC 4180 对 CSV 字段做转义 —— 含逗号 / 引号 / 换行时用双引号
// 包裹,内部引号翻倍。保证任意字符串(如任务 ID)不会破坏 CSV 的列结构。
std::string csvEscape (const std::string& value)
{
    if (value.find_first_of (",\"\r\n") == std::string::npos)
        return value;
    std::string escaped = "\"";
    for (char character : value) {
        if (character == '"') escaped += "\"\"";
        else escaped += character;
    }
    escaped += '"';
    return escaped;
}

// csvDouble:把 double 格式化为文本;非有限值输出字面量 "null",
// 与 JSON 约定保持一致,避免 CSV 中出现非法数值。
std::string csvDouble (double value)
{
    return std::isfinite (value) ? std::to_string (value) : "null";
}

// csvReasons:把失败原因列表压缩成单字段文本,多个原因以 '|' 分隔,
// 便于在 CSV 一列里展示完整原因集合。
std::string csvReasons (const std::vector< KinematicFailureReason >& reasons)
{
    std::string result;
    for (std::size_t index = 0; index < reasons.size (); ++index) {
        if (index != 0) result += '|';
        result += toString (reasons[index]);
    }
    return result;
}

} // namespace

// taskCsv:导出任务级 CSV。每行一个任务点,列含 ID / 等级 / 可行性 / 质量 /
// 位置与姿态残差 / 最小关节裕度 / 可操作度 / 碰撞检查与碰撞状态 / 失败原因。
// 指标取自每个任务的最佳候选(排序后的第一个),无候选时以 null 占位。
std::string KinematicAnalysisReportJson::taskCsv (const KinematicAnalysisReport& report)
{
    std::string csv =
        "id,level,feasibility,quality,position_error_m,orientation_error_deg,"
        "min_joint_margin,manipulability,collision_checked,collision,failure_reasons\n";
    for (const TargetEvaluation& task : report.taskResults) {
        const TargetCandidate* candidate = task.candidates.empty () ? nullptr : &task.candidates.front ();
        const ConfigurationEvaluation* configuration =
            candidate == nullptr ? nullptr : &candidate->configuration;
        csv += csvEscape (task.target.id) + "," + requirementLevelToString (task.level) + "," +
            toString (task.feasibility) + "," +
            toString (task.quality) + "," +
            csvDouble (candidate == nullptr ? std::numeric_limits< double >::quiet_NaN () :
                       candidate->positionErrorMeters) + "," +
            csvDouble (candidate == nullptr ? std::numeric_limits< double >::quiet_NaN () :
                       candidate->orientationErrorDeg) + "," +
            csvDouble (configuration == nullptr ? std::numeric_limits< double >::quiet_NaN () :
                       configuration->minimumJointMargin) + "," +
            csvDouble (configuration == nullptr ? std::numeric_limits< double >::quiet_NaN () :
                       configuration->manipulability) + "," +
            (configuration != nullptr && configuration->collisionChecked ? "true" : "false") + "," +
            (configuration != nullptr && configuration->inCollision ? "true" : "false") + "," +
            csvEscape (csvReasons (task.failureReasons)) + "\n";
    }
    return csv;
}

// regionCsv:导出区域级 CSV。每行一个区域,列含区域 ID / 证据等级 /
// 总单元数 / 可达单元数 / 位置与姿态覆盖率 / 可行性 / 质量。
std::string KinematicAnalysisReportJson::regionCsv (const KinematicAnalysisReport& report)
{
    std::string csv =
        "region_id,stage,total_cells,reachable_cells,position_coverage,"
        "orientation_coverage,feasibility,quality\n";
    for (const RegionCoverageResult& region : report.regionResults) {
        csv += csvEscape (region.regionId) + "," + toString (region.stage) + "," +
            std::to_string (region.totalCells) + "," +
            std::to_string (region.reachableCells) + "," +
            csvDouble (region.positionCoverage) + "," +
            csvDouble (region.orientationCoverage) + "," +
            toString (region.feasibility) + "," + toString (region.quality) + "\n";
    }
    return csv;
}

namespace {

// -----------------------------------------------------------------------------
// 视图过滤辅助(匿名命名空间)
// -----------------------------------------------------------------------------
//
// containsFailure(TaskEvaluation):判断指定失败原因是否出现在任务级或任一候选解中。
bool containsFailure (const TargetEvaluation& task, KinematicFailureReason reason)
{
    if (std::find (task.failureReasons.begin (), task.failureReasons.end (), reason) !=
        task.failureReasons.end ())
        return true;
    for (const TargetCandidate& candidate : task.candidates) {
        if (std::find (candidate.configuration.failureReasons.begin (),
                       candidate.configuration.failureReasons.end (), reason) !=
            candidate.configuration.failureReasons.end ())
            return true;
    }
    return false;
}

// containsFailure(RegionCoverageResult):判断指定失败原因是否出现在区域任一个
// 网格单元中。
bool containsFailure (const RegionCoverageResult& region, KinematicFailureReason reason)
{
    for (const RegionCellResult& cell : region.cells) {
        if (std::find (cell.failureReasons.begin (), cell.failureReasons.end (), reason) !=
            cell.failureReasons.end ())
            return true;
    }
    return false;
}

// matches(TaskEvaluation):任务是否满足过滤条件 —— 每个开启的维度必须精确匹配,
// 未开启的维度不参与判断(恒真)。
bool matches (const TargetEvaluation& task, const KinematicAnalysisReportFilters& filters)
{
    return (!filters.filterStage || task.stage == filters.stage) &&
           (!filters.filterFeasibility || task.feasibility == filters.feasibility) &&
           (!filters.filterQuality || task.quality == filters.quality) &&
           (!filters.filterFailureReason || containsFailure (task, filters.failureReason));
}

// matches(RegionCoverageResult):区域是否满足过滤条件 —— 额外支持 regionId
// 过滤(非空时要求区域 ID 完全相等)。
bool matches (const RegionCoverageResult& region,
              const KinematicAnalysisReportFilters& filters)
{
    return (!filters.filterStage || region.stage == filters.stage) &&
           (!filters.filterFeasibility || region.feasibility == filters.feasibility) &&
           (!filters.filterQuality || region.quality == filters.quality) &&
           (filters.regionId.empty () || region.regionId == filters.regionId) &&
           (!filters.filterFailureReason || containsFailure (region, filters.failureReason));
}

} // namespace

// -----------------------------------------------------------------------------
// filterReportView —— 生成过滤后的报告视图
// -----------------------------------------------------------------------------
//
// 以 report 为模板做浅拷贝,再只保留匹配过滤条件的任务与区域结果。
// 顶层元信息(溯源 / 可行性 / 当前位姿等)保持不变;源报告不被修改,
// 调用方可安全持有多个不同过滤条件的视图。
KinematicAnalysisReport filterReportView (
    const KinematicAnalysisReport& report,
    const KinematicAnalysisReportFilters& filters)
{
    KinematicAnalysisReport view = report;
    view.taskResults.clear ();
    view.regionResults.clear ();
    for (const TargetEvaluation& task : report.taskResults) {
        if (matches (task, filters))
            view.taskResults.push_back (task);
    }
    for (const RegionCoverageResult& region : report.regionResults) {
        if (matches (region, filters))
            view.regionResults.push_back (region);
    }
    return view;
}

} // namespace rws
