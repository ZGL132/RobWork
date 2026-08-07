// =====================================================================
// KinematicAnalysisProjectDocument：运动学分析工程文档的 JSON 序列化/反序列化。
//
// 本文件负责把“可编辑的分析配置”与磁盘上的规范 JSON 文档相互转换：
//   - toJson：将当前分析设置(设备/TCP 名称、采样参数、评价阈值、任务点等)
//     编码为 format + schemaVersion + settings 三层结构的规范 JSON，用于保存；
//   - fromJson：解析并严格校验 JSON 后恢复可编辑配置，格式/版本/必需字段
//     不合法时整体拒绝，其余可选字段带默认值读取以兼容早期项目。
// 序列化只保存“如何重新执行分析”的输入，不保存任何一次运行的派生结果，
// 也避免机器相关的绝对路径，保证文档可移植、可原样重放。
// =====================================================================
#include "KinematicAnalysisProjectDocument.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace rws {
namespace {

// 项目文档格式版本号。仅当字段结构发生不兼容变更时才递增；读取时校验其一致性，
// 拒绝旧版本无法解释的配置。
constexpr int ProjectDocumentSchemaVersion = 1;

// 统一错误回填工具：仅当调用方提供了 error 指针时才写入错误描述。
void setError (QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

// 把一维 double 向量编码为 JSON 数值数组（用于任务点权重等可变长字段）。
QJsonArray doublesToArray (const std::vector< double >& values)
{
    QJsonArray result;
    for (const double value : values)
        result.append (value);
    return result;
}

// 把三维空间点编码为长度固定为 3 的 JSON 数值数组。
QJsonArray pointToArray (const std::array< double, 3 >& point)
{
    QJsonArray result;
    for (const double value : point)
        result.append (value);
    return result;
}

// 从 JSON 值读取三维点：必须是长度恰为 3、元素均为有限 double 的数组。
// 任何不满足条件的输入都返回 false，避免 NaN/Inf 或缺失维度混入配置。
bool readPoint (const QJsonValue& value, std::array< double, 3 >& point)
{
    const QJsonArray array = value.toArray ();
    if (array.size () != 3)
        return false;
    for (int index = 0; index < 3; ++index) {
        if (!array[index].isDouble () || !std::isfinite (array[index].toDouble ()))
            return false;
        point[static_cast< std::size_t > (index)] = array[index].toDouble ();
    }
    return true;
}

// 把一个任务点序列化为 JSON 对象；所有字段均以名称为主键，便于人工审阅与差异对比。
void writeTaskPoint (QJsonObject& object, const TaskPoint& point)
{
    object["id"] = QString::fromStdString (point.id);
    object["name"] = QString::fromStdString (point.name);
    object["type"] = static_cast< int > (point.type);
    object["refFrame"] = QString::fromStdString (point.refFrame);
    object["tcpFrame"] = QString::fromStdString (point.tcpFrame);
    object["position"] = pointToArray (point.position);
    object["rpyDeg"] = pointToArray (point.rpyDeg);
    object["positionToleranceMeters"] = point.tolerance.positionMeters;
    object["orientationToleranceDeg"] = point.tolerance.orientationDeg;
    object["allowToolRollFree"] = point.tolerance.allowToolRollFree;
    object["weight"] = point.weight;
    object["enabled"] = point.enabled;
    object["note"] = QString::fromStdString (point.note);
}

// 从 JSON 对象反序列化任务点。position/rpyDeg 必须有效，其余字段带默认值以兼容
// 早期项目；字段缺失时不会整体失败。
bool readTaskPoint (const QJsonValue& value, TaskPoint& point)
{
    const QJsonObject object = value.toObject ();
    if (object.isEmpty () || !readPoint (object.value ("position"), point.position) ||
        !readPoint (object.value ("rpyDeg"), point.rpyDeg))
        return false;
    point.id = object.value ("id").toString ().toStdString ();
    point.name = object.value ("name").toString ().toStdString ();
    point.type = static_cast< TaskPointType > (object.value ("type").toInt ());
    point.refFrame = object.value ("refFrame").toString ("WORLD").toStdString ();
    point.tcpFrame = object.value ("tcpFrame").toString ("TCP").toStdString ();
    point.tolerance.positionMeters = object.value ("positionToleranceMeters").toDouble (0.001);
    point.tolerance.orientationDeg = object.value ("orientationToleranceDeg").toDouble (1.0);
    point.tolerance.allowToolRollFree = object.value ("allowToolRollFree").toBool (false);
    point.weight = object.value ("weight").toDouble (1.0);
    point.enabled = object.value ("enabled").toBool (true);
    point.note = object.value ("note").toString ().toStdString ();
    return true;
}

}    // namespace

// 序列化：把可编辑分析配置编码为带 format/schemaVersion 的规范 JSON。
// 只写入“如何重新执行分析”的输入(设备/TCP 名称、采样参数、阈值、任务点等)，
// 不包含任何一次运行产生的结果或机器相关的绝对路径。
QByteArray KinematicAnalysisProjectDocument::toJson (
    const KinematicAnalysisProjectSettings& settings)
{
    QJsonObject root;
    root["format"] = QStringLiteral ("RobWorkStudioKinematicAnalysis");
    root["schemaVersion"] = ProjectDocumentSchemaVersion;

    QJsonObject object;
    // 保存 IK 求解的核心输入：设备、TCP 框架、目标位姿与重复解去重阈值。
    // 这些字段是 fromJson 侧的硬性校验项，缺失将导致整个文档无法加载。
    object["deviceName"] = settings.deviceName;
    object["tcpFrameName"] = settings.tcpFrameName;
    object["ikPositionMeters"] = pointToArray (settings.ikPositionMeters);
    object["ikRpyDeg"] = pointToArray (settings.ikRpyDeg);
    const double duplicateQThreshold = settings.ikDuplicateQThreshold;
    object["ikDuplicateQThreshold"] = duplicateQThreshold;
    object["ikCollisionCheck"] = settings.ikCollisionCheck;
    object["lengthUnit"] = static_cast< int > (settings.lengthUnit);
    object["angleUnit"] = static_cast< int > (settings.angleUnit);
    // 工作空间采样参数：采样模式、样本数/栅格步数与碰撞开关。
    // randomSeed 固定后可保证随机采样的结果可复现。
    object["workspaceMode"] = static_cast< int > (settings.workspace.mode);
    object["workspaceSampleCount"] = settings.workspace.sampleCount;
    object["workspaceGridStepsPerJoint"] = settings.workspace.gridStepsPerJoint;
    object["workspaceCheckCollision"] = settings.workspace.checkCollision;
    object["workspaceRandomSeed"] = static_cast< qint64 > (settings.workspace.randomSeed);
    object["workspaceColorMode"] = static_cast< int > (settings.workspaceColorMode);
    // 姿态可达性分析参数：方向/翻滚采样数与碰撞开关；
    // poseTaskPointsSource 决定采样来源是手动位置还是表格任务点。
    object["poseDirectionSamples"] = settings.poseReachability.directionSamples;
    object["poseRollSamples"] = settings.poseReachability.rollSamples;
    object["poseCheckCollision"] = settings.poseReachability.checkCollision;
    object["poseTaskPointsSource"] = settings.poseTaskPointsSource;
    QJsonArray manualPositions;
    for (const auto& position : settings.manualPosePositions)
        manualPositions.append (pointToArray (position));
    object["manualPosePositions"] = manualPositions;
    // 可视化渲染选项：点来源、投影方式、标量场模式、渲染模式与包络方向。
    // 显示开关与点大小属于纯 UI 偏好，不影响分析结果，但仍随文档保存。
    object["visualSource"] = static_cast< int > (settings.visualSource);
    object["visualProjection"] = static_cast< int > (settings.visualProjection);
    object["visualScalarMode"] = static_cast< int > (settings.visualScalarMode);
    object["visualRenderMode"] = static_cast< int > (settings.visualRenderMode);
    object["envelopeDirections"] = settings.envelopeDirections;
    object["showPass"] = settings.showPass;
    object["showWarning"] = settings.showWarning;
    object["showFail"] = settings.showFail;
    object["showUnknown"] = settings.showUnknown;
    object["showLabels"] = settings.showLabels;
    object["showGrid"] = settings.showGrid;
    object["showLegend"] = settings.showLegend;
    object["pointSize"] = settings.pointSize;
    // 评价阈值集合：关节限位比例、奇异值、条件数、可操作度的告警/失败阈值，
    // 以及位置/姿态容差与 IK 重复解去重阈值。它们决定状态 Pass/Warning/Fail 的划分。
    object["thresholdNearJointLimitRatio"] = settings.thresholds.nearJointLimitRatio;
    object["thresholdSingularValueWarning"] = settings.thresholds.singularValueWarning;
    object["thresholdConditionWarning"] = settings.thresholds.conditionWarning;
    object["thresholdConditionFail"] = settings.thresholds.conditionFail;
    object["thresholdManipulabilityWarning"] = settings.thresholds.manipulabilityWarning;
    object["thresholdPositionToleranceMeters"] = settings.thresholds.positionToleranceMeters;
    object["thresholdOrientationToleranceDeg"] = settings.thresholds.orientationToleranceDeg;
    object["thresholdIkDuplicateQ"] = duplicateQThreshold;

    // 任务点集合：按表内顺序保存，每个任务点用 writeTaskPoint 完整编码。
    QJsonArray taskPoints;
    for (const TaskPoint& point : settings.taskPoints) {
        QJsonObject taskPoint;
        writeTaskPoint (taskPoint, point);
        taskPoints.append (taskPoint);
    }
    object["taskPoints"] = taskPoints;
    root["settings"] = object;

    return QJsonDocument (root).toJson (QJsonDocument::Indented);
}

// 反序列化：解析规范 JSON 并恢复可编辑配置。先做语法解析与 format/schemaVersion
// 校验，再校验 IK 核心字段；其余可选字段带默认值读取，缺失时保持默认以兼容旧版。
bool KinematicAnalysisProjectDocument::fromJson (
    const QByteArray& json,
    KinematicAnalysisProjectSettings& settings,
    QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson (json, &parseError);
    if (document.isNull () || !document.isObject ()) {
        setError (error, QStringLiteral ("KinematicAnalysis 项目文档不是有效 JSON：%1。")
                              .arg (parseError.errorString ()));
        return false;
    }
    const QJsonObject root = document.object ();
    // 先识别格式与版本，避免把任意 JSON 或未来版本误当作可编辑配置。
    if (root.value ("format").toString () != QStringLiteral ("RobWorkStudioKinematicAnalysis") ||
        root.value ("schemaVersion").toInt () != ProjectDocumentSchemaVersion) {
        setError (error, QStringLiteral ("KinematicAnalysis 项目文档格式或版本不受支持。"));
        return false;
    }
    const QJsonObject object = root.value ("settings").toObject ();
    // IK 位置与姿态是分析的必需输入，缺失或非法时整体拒绝加载。
    if (object.isEmpty () || !readPoint (object.value ("ikPositionMeters"), settings.ikPositionMeters) ||
        !readPoint (object.value ("ikRpyDeg"), settings.ikRpyDeg)) {
        setError (error, QStringLiteral ("KinematicAnalysis 项目文档缺少有效的 IK 配置。"));
        return false;
    }

    // 逐项恢复可选字段：字段缺失时采用默认值，保持与早期项目文档的兼容性。
    settings.deviceName = object.value ("deviceName").toString ();
    settings.tcpFrameName = object.value ("tcpFrameName").toString ();
    // ikDuplicateQThreshold 的键名在早期版本中曾写作 thresholdIkDuplicateQ，
    // 这里优先读新键，缺失时回退旧键，保证旧文档仍可加载。
    const QJsonValue currentDuplicateQ = object.value ("ikDuplicateQThreshold");
    const QJsonValue legacyDuplicateQ = object.value ("thresholdIkDuplicateQ");
    settings.ikDuplicateQThreshold = currentDuplicateQ.isDouble ()
        ? currentDuplicateQ.toDouble () : legacyDuplicateQ.toDouble (1e-4);
    settings.ikCollisionCheck = object.value ("ikCollisionCheck").toBool (true);
    settings.lengthUnit = static_cast< KinematicLengthUnit > (object.value ("lengthUnit").toInt (0));
    settings.angleUnit = static_cast< KinematicAngleUnit > (object.value ("angleUnit").toInt (0));
    settings.workspace.mode = static_cast< WorkspaceSamplingMode > (object.value ("workspaceMode").toInt (0));
    settings.workspace.sampleCount = object.value ("workspaceSampleCount").toInt (1000);
    settings.workspace.gridStepsPerJoint = object.value ("workspaceGridStepsPerJoint").toInt (5);
    settings.workspace.checkCollision = object.value ("workspaceCheckCollision").toBool (true);
    settings.workspace.randomSeed = static_cast< unsigned int > (
        object.value ("workspaceRandomSeed").toInteger (1));
    settings.workspaceColorMode = static_cast< WorkspaceColorMode > (
        object.value ("workspaceColorMode").toInt (0));
    settings.poseReachability.directionSamples = object.value ("poseDirectionSamples").toInt (24);
    settings.poseReachability.rollSamples = object.value ("poseRollSamples").toInt (1);
    settings.poseReachability.checkCollision = object.value ("poseCheckCollision").toBool (true);
    settings.poseTaskPointsSource = object.value ("poseTaskPointsSource").toBool (true);
    // 手动位置为可变长字段：逐个解析，任一元素非法则整体拒绝加载。
    settings.manualPosePositions.clear ();
    for (const QJsonValue& value : object.value ("manualPosePositions").toArray ()) {
        std::array< double, 3 > position;
        if (!readPoint (value, position)) {
            setError (error, QStringLiteral ("KinematicAnalysis 项目文档包含无效手动位置。"));
            return false;
        }
        settings.manualPosePositions.push_back (position);
    }
    // 可视化显示选项恢复。
    settings.visualSource = static_cast< VisualPointSource > (object.value ("visualSource").toInt (0));
    settings.visualProjection = static_cast< VisualProjection > (object.value ("visualProjection").toInt (0));
    settings.visualScalarMode = static_cast< VisualScalarMode > (object.value ("visualScalarMode").toInt (0));
    settings.visualRenderMode = static_cast< VisualRenderMode > (object.value ("visualRenderMode").toInt (0));
    settings.envelopeDirections = object.value ("envelopeDirections").toInt (180);
    settings.showPass = object.value ("showPass").toBool (true);
    settings.showWarning = object.value ("showWarning").toBool (true);
    settings.showFail = object.value ("showFail").toBool (true);
    settings.showUnknown = object.value ("showUnknown").toBool (true);
    settings.showLabels = object.value ("showLabels").toBool (false);
    settings.showGrid = object.value ("showGrid").toBool (true);
    settings.showLegend = object.value ("showLegend").toBool (true);
    settings.pointSize = object.value ("pointSize").toDouble (4.5);
    // 评价阈值恢复：缺失时回退到与分析库一致的内置默认值。
    settings.thresholds.nearJointLimitRatio = object.value ("thresholdNearJointLimitRatio").toDouble (0.05);
    settings.thresholds.singularValueWarning = object.value ("thresholdSingularValueWarning").toDouble (1e-4);
    settings.thresholds.conditionWarning = object.value ("thresholdConditionWarning").toDouble (100.0);
    settings.thresholds.conditionFail = object.value ("thresholdConditionFail").toDouble (1000.0);
    settings.thresholds.manipulabilityWarning = object.value ("thresholdManipulabilityWarning").toDouble (1e-5);
    settings.thresholds.positionToleranceMeters = object.value ("thresholdPositionToleranceMeters").toDouble (0.001);
    settings.thresholds.orientationToleranceDeg = object.value ("thresholdOrientationToleranceDeg").toDouble (1.0);
    settings.thresholds.ikDuplicateQThreshold = settings.ikDuplicateQThreshold;
    // 任务点集合恢复：逐个解析，任一任务点非法则整体拒绝加载。
    settings.taskPoints.clear ();
    for (const QJsonValue& value : object.value ("taskPoints").toArray ()) {
        TaskPoint point;
        if (!readTaskPoint (value, point)) {
            setError (error, QStringLiteral ("KinematicAnalysis 项目文档包含无效任务点。"));
            return false;
        }
        settings.taskPoints.push_back (point);
    }
    return true;
}

}    // namespace rws
