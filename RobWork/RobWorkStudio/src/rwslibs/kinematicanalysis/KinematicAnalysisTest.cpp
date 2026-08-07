#include "KinematicAnalysisTypes.hpp"
#include "KinematicAnalysisContext.hpp"
#include "ConfigurationEvaluator.hpp"
#include "TargetEvaluator.hpp"
#include "KinematicBatchRunner.hpp"
#include "RegionCoverageEvaluator.hpp"
#include "KinematicMetrics.hpp"
#include "KinematicAnalyzer.hpp"
#include "KinematicAnalysisUiLogic.hpp"
#include "KinematicAnalysisEnvelope.hpp"
#include "TaskPointResolver.hpp"
#include "TaskPointUiLogic.hpp"
#include "TaskPointTableModel.hpp"
#include "KinematicAnalysisVisualizationTypes.hpp"
#include "KinematicAnalysisWorkspace.hpp"
#include "KinematicAnalysisPoseReachability.hpp"
#include "OrientationCoverageEvaluator.hpp"
#include "KinematicAnalysisReportJson.hpp"
#include "KinematicAnalysisCollision.hpp"
#include "KinematicAnalysisJson.hpp"
#include "KinematicAnalysisProjectDocument.hpp"
#include "FrozenRequirementKinematicAdapter.hpp"
#include "KinematicAnalysisWidget.hpp"
#include "KinematicAnalysisPlotWidget.hpp"
#include "KinematicPlotDialog.hpp"
#include "KinematicThresholdsDialog.hpp"

#include <rwslibs/engineeringrequirements/RequirementFreezer.hpp>
#include <rwslibs/engineeringrequirements/RequirementSetJson.hpp>
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>

#include <rws/CallbackProjectDocumentProvider.hpp>
#include <rws/ProjectManager.hpp>
#include <rws/RobWorkStudio.hpp>

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QLineEdit>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRect>
#include <QRectF>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QLabel>
#include <QJsonArray>
#include <QPushButton>
#include <QPointer>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableView>
#include <QTabWidget>
#include <QToolButton>
#include <QMenu>

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/kinematics/MovableFrame.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/StateStructure.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Jacobian.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/SerialDevice.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

// 测试程序:不依赖完整 RobWorkStudio 渲染环境,只验证分析器/指标/类型层
// 的纯算法正确性(QCoreApplication 仅为 Q_OBJECT 机制 / 元对象而存在)。
// 用 main 的首个命令行参数选择子套件,默认 "all" 跑全套。
// 每个子套件返回 0 表示通过,非 0 表示有失败用例。

// 浮点近似比较:绝对差 ≤ eps。eps 默认 1e-12,适合整型解析/无浮点误差场景。
static bool nearlyEqual (double lhs, double rhs, double eps = 1e-12)
{
    return std::fabs (lhs - rhs) <= eps;
}

// 失败:打 stderr 并返回 1,便于 CTest 直接收到非 0 退出码。
static int fail (const std::string& message)
{
    std::cerr << message << std::endl;
    return 1;
}

// 通用断言:失败时附带 "what" 描述。
static int require (bool condition, const std::string& what)
{
    if (!condition)
        return fail ("requirement failed: " + what);
    return 0;
}

// 子套件 ABI:验证 FrozenRequirementKinematicAdapter 的两个历史公开入口
// (apply / applyWithValidation)仍能以既有的函数指针类型链接成功。
// 目的是在重构、模板化或换签名时守住二进制接口,防止旧插件/调用方链接失败。
// 补充说明:用 static_cast 到既有函数指针类型来强制符号地址可解析,若未来有人改动
// 函数签名、模板化或删掉某入口,此处会编译失败,从源头守住二进制接口。
static int testHistoricalFrozenRequirementAdapterAbiRemainsLinkable ()
{
    using Apply = bool (*) (const rws::FrozenRequirementArtifact&,
                            const rw::models::WorkCell&, const rw::kinematics::State&,
                            std::vector< rws::TaskPoint >&, std::string*);
    using ApplyWithValidation = bool (*) (
        const rws::FrozenRequirementArtifact&, const rw::models::WorkCell&,
        const rw::kinematics::State&, std::vector< rws::TaskPoint >&, std::string*, bool*,
        std::vector< std::string >*);

    const Apply apply = static_cast< Apply > (&rws::FrozenRequirementKinematicAdapter::apply);
    const ApplyWithValidation applyWithValidation = static_cast< ApplyWithValidation > (
        &rws::FrozenRequirementKinematicAdapter::applyWithValidation);

    if (const int rc = require (apply != nullptr, "historical apply ABI symbol"))
        return rc;
    return require (applyWithValidation != nullptr,
                    "historical applyWithValidation ABI symbol");
}

// 浮点断言:失败时同时打印 expected / actual。
static int assertNear (double actual, double expected, double eps, const std::string& what)
{
    if (!nearlyEqual (actual, expected, eps))
        return fail ("expected " + what + " = " + std::to_string (expected) +
                     " but got " + std::to_string (actual));
    return 0;
}

// 测试设备工厂 1:构造一个 7 自由度 Kuka IIWA 串联机器人(设备名 KukaIIWA)。
// 关节 1..7 全部为旋转关节,限位对称(hi = -lo),角度约 ±(170/120/170/120/170/120/175) 度,
// 每个关节用位置偏移 + RPY 描述,末端 TCP 固定于关节 7 上。
// 该设备用于"当前位姿可达自身"这类需要完整 7-DOF IK 的测试。
static rw::models::SerialDevice::Ptr makeTestKukaIIWA (
    rw::kinematics::StateStructure& stateStructure)
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    const Frame::Ptr base =
        rw::core::ownedPtr (new FixedFrame ("Base", Transform3D<>::identity ()));
    const Joint::Ptr joint1 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint1", Transform3D<> (Vector3D<> (0, 0, 0.158))));
    const Joint::Ptr joint2 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint2", Transform3D<> (Vector3D<> (0, 0, 0.182),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint3 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint3", Transform3D<> (Vector3D<> (0, -0.182, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint4 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint4", Transform3D<> (Vector3D<> (0, 0, 0.218),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint5 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint5", Transform3D<> (Vector3D<> (0, 0.182, 0),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint6 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint6", Transform3D<> (Vector3D<> (0, 0, 0.218),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint7 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint7", Transform3D<> (Vector3D<>::zero (),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Frame::Ptr end =
        rw::core::ownedPtr (new FixedFrame ("TCP", Transform3D<> (Vector3D<> (0, 0, 0.126))));

    stateStructure.addFrame (base);
    stateStructure.addFrame (joint1, base);
    stateStructure.addFrame (joint2, joint1);
    stateStructure.addFrame (joint3, joint2);
    stateStructure.addFrame (joint4, joint3);
    stateStructure.addFrame (joint5, joint4);
    stateStructure.addFrame (joint6, joint5);
    stateStructure.addFrame (joint7, joint6);
    stateStructure.addFrame (end, joint7);

    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::models::SerialDevice::Ptr device =
        rw::core::ownedPtr (new rw::models::SerialDevice (base.get (), end.get (), "KukaIIWA", state));
    std::pair< Q, Q > bounds;
    bounds.first = Q (7, -170 * Deg2Rad, -120 * Deg2Rad, -170 * Deg2Rad, -120 * Deg2Rad,
                      -170 * Deg2Rad, -120 * Deg2Rad, -175 * Deg2Rad);
    bounds.second = -bounds.first;
    device->setBounds (bounds);
    return device;
}

// 测试设备工厂 2:构造一个通用 6 轴串联机器人(设备名 GenericSixAxis)。
// 关节限位非对称(J2/J3/J5 为 ±120°/±150°/±120°,J6 为 ±2π),用于区分
// "可解析/不可达目标"以及大部分分析路径的测试;与 KukaIIWA 形成对照。
static rw::models::SerialDevice::Ptr makeGenericSixAxis (
    rw::kinematics::StateStructure& stateStructure)
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    const Frame::Ptr base =
        rw::core::ownedPtr (new FixedFrame ("Base", Transform3D<>::identity ()));
    const Joint::Ptr joint1 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint1", Transform3D<> (Vector3D<> (0, 0, 0.35))));
    const Joint::Ptr joint2 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint2", Transform3D<> (Vector3D<> (0.12, 0, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint3 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint3", Transform3D<> (Vector3D<> (0.52, 0, 0))));
    const Joint::Ptr joint4 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint4", Transform3D<> (Vector3D<> (0.42, 0, 0),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Joint::Ptr joint5 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint5", Transform3D<> (Vector3D<> (0, 0, 0.38),
                                                                        RPY<> (0, 0, -Pi / 2.0))));
    const Joint::Ptr joint6 =
        rw::core::ownedPtr (new RevoluteJoint ("Joint6", Transform3D<> (Vector3D<> (0, 0, 0.12),
                                                                        RPY<> (0, 0, Pi / 2.0))));
    const Frame::Ptr end =
        rw::core::ownedPtr (new FixedFrame ("TCP", Transform3D<>::identity ()));

    stateStructure.addFrame (base);
    stateStructure.addFrame (joint1, base);
    stateStructure.addFrame (joint2, joint1);
    stateStructure.addFrame (joint3, joint2);
    stateStructure.addFrame (joint4, joint3);
    stateStructure.addFrame (joint5, joint4);
    stateStructure.addFrame (joint6, joint5);
    stateStructure.addFrame (end, joint6);

    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::models::SerialDevice::Ptr device =
        rw::core::ownedPtr (new rw::models::SerialDevice (base.get (), end.get (),
                                                          "GenericSixAxis", state));
    std::pair< Q, Q > bounds;
    bounds.first = Q (6, -Pi, -120.0 * Deg2Rad, -150.0 * Deg2Rad,
                      -Pi, -120.0 * Deg2Rad, -2.0 * Pi);
    bounds.second = Q (6, Pi, 120.0 * Deg2Rad, 150.0 * Deg2Rad,
                       Pi, 120.0 * Deg2Rad, 2.0 * Pi);
    device->setBounds (bounds);
    return device;
}

// 子套件 1:基础类型默认值 + toString。
//   - 校验 KinematicThresholds 的默认值与 KinematicAnalysisTypes 注释一致;
//   - 校验 KinematicFailureReason 的相等语义;
//   - 校验 toString 的几个代表值。
// 补充说明:
//   - 默认阈值逐项断言,与 KinematicAnalysisTypes 头文件注释值一致,防止改默认值
//     而不同步文档;
//   - KinematicFailureReason 的相等语义与 toString 的稳定字符串是诊断码/告警跨进程
//     (JSON / UI / 日志)传递的基础,改动会造成下游解析断裂;
//   - ConfigurationEvaluation 默认证据阶段(Quick)/可行性(NotEvaluated)/质量(Unknown)
//     是"尚未求值"的协议约定;
//   - 四个枚举的字符串往返(含拒绝未知字符串并给出 error)是严格类型解析的回归保障:
//     编辑态 JSON 中的错误类型不得被静默降级为默认值。
static int testTypes ()
{
    rws::KinematicThresholds thresholds;
    if (const int rc = assertNear (thresholds.nearJointLimitRatio, 0.05, 1e-12, "nearJointLimitRatio"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionWarning, 100.0, 1e-12, "conditionWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.conditionFail, 1000.0, 1e-12, "conditionFail"))
        return rc;
    if (const int rc = assertNear (thresholds.singularValueWarning, 1e-4, 1e-12, "singularValueWarning"))
        return rc;
    if (const int rc = assertNear (thresholds.manipulabilityWarning, 1e-5, 1e-12, "manipulabilityWarning"))
        return rc;
    if (const int rc =
            assertNear (thresholds.positionToleranceMeters, 0.001, 1e-12, "positionToleranceMeters"))
        return rc;
    if (const int rc =
            assertNear (thresholds.orientationToleranceDeg, 1.0, 1e-12, "orientationToleranceDeg"))
        return rc;

    rws::KinematicAnalysisResult result;
    result.header.pluginName = "KinematicAnalysis";
    result.reachableRate    = 0.75;
    result.workspaceSamples.push_back (rws::WorkspaceSample ());
    rws::KinematicFailureReason reason = rws::KinematicFailureReason::Collision;
    if (reason != rws::KinematicFailureReason::Collision)
        return fail ("KinematicFailureReason equality comparison broke.");

    if (const int rc = require (result.header.pluginName == "KinematicAnalysis", "result.pluginName"))
        return rc;
    if (const int rc = assertNear (result.reachableRate, 0.75, 1e-12, "result.reachableRate"))
        return rc;
    if (const int rc = require (result.workspaceSamples.size () == 1, "result.workspaceSamples size"))
        return rc;
    if (const int rc = require (std::string (rws::toString (rws::KinematicFailureReason::Singular)) ==
                                "Singular",
                                "toString Singular"))
        return rc;
    if (const int rc = require (
            std::string (rws::toString (rws::KinematicFailureReason::TargetResidual)) ==
                "TargetResidual",
            "toString TargetResidual"))
        return rc;

    rws::ConfigurationEvaluation configuration;
    if (const int rc = require (
            configuration.stage == rws::AnalysisEvidenceStage::Quick,
            "configuration evaluation defaults to Quick evidence"))
        return rc;
    if (const int rc = require (
            configuration.feasibility == rws::Feasibility::NotEvaluated,
            "configuration evaluation defaults to NotEvaluated"))
        return rc;
    if (const int rc = require (
            configuration.quality == rws::Quality::Unknown,
            "configuration evaluation defaults to Unknown quality"))
        return rc;

    rws::Feasibility feasibility = rws::Feasibility::NotEvaluated;
    std::string conversionError;
    if (const int rc = require (
            rws::feasibilityFromString ("DataInsufficient", feasibility, &conversionError) &&
                feasibility == rws::Feasibility::DataInsufficient && conversionError.empty (),
            "Feasibility stable string round trip"))
        return rc;
    rws::Quality quality = rws::Quality::Unknown;
    if (const int rc = require (
            rws::qualityFromString ("Degraded", quality, &conversionError) &&
                quality == rws::Quality::Degraded && conversionError.empty (),
            "Quality stable string round trip"))
        return rc;
    rws::AnalysisEvidenceStage stage = rws::AnalysisEvidenceStage::Estimated;
    if (const int rc = require (
            rws::analysisEvidenceStageFromString ("Verified", stage, &conversionError) &&
                stage == rws::AnalysisEvidenceStage::Verified && conversionError.empty (),
            "evidence stage stable string round trip"))
        return rc;
    rws::KinematicFailureReason convertedReason = rws::KinematicFailureReason::None;
    if (const int rc = require (
            rws::kinematicFailureReasonFromString (
                "CollisionDetectorUnavailable", convertedReason, &conversionError) &&
                convertedReason == rws::KinematicFailureReason::CollisionDetectorUnavailable &&
                conversionError.empty (),
            "failure reason stable string round trip"))
        return rc;
    if (const int rc = require (
            !rws::qualityFromString ("not-a-quality", quality, &conversionError) &&
                !conversionError.empty (),
            "unknown stable string is rejected with an error"))
        return rc;
    return 0;
}

// 子套件 报告 JSON:验证 KinematicAnalysisReportJson 的序列化/反序列化往返。
// 刻意构造包含 inf/NaN 的分析报告,验证非有限浮点在 JSON 中被替换为 null 并
// 追加 KIN_REPORT_NONFINITE 诊断;要求等级(Should/Info)保真,未知等级被拒绝;
// CSV 导出表头稳定;filterReportView 支持按证据阶段/可行性/失败原因/区域 id
// 过滤且不修改源报告。
// 关键非显而易见点:
//   - 刻意把 currentPose / 候选 / 配置 / 区域单元格的各字段塞满 inf/NaN,以验证
//     toObject 统一把非有限数写成 JSON null 并追加 KIN_REPORT_NONFINITE 诊断,
//     而从 fromObject 读回后仍是非有限值(而非被截断成 0),保证往返不丢信息;
//   - filteredTask 作为第二个任务结果,用于验证 level(Should/Info)保真与
//     未知 level 被整体拒绝(不静默降级);
//   - filterReportView 的四种过滤(证据阶段/可行性/失败原因/区域 id)均只影响视图,
//     不得修改源报告;CSV 表头是稳定导出契约,改动会破坏下游解析。
static int testReportJsonRoundTrip ()
{
    rws::KinematicAnalysisReport report;
    report.schemaVersion = 1;
    report.pluginName = "kinematicanalysis";
    report.analysisId = "report-roundtrip";
    report.provenance.robotModelFingerprint = "model-report";
    report.feasibility = rws::Feasibility::DataInsufficient;
    report.quality = rws::Quality::Degraded;
    report.evidenceStage = rws::AnalysisEvidenceStage::Verified;
    report.currentPose.deviceName = "Robot";
    report.currentPose.q = {std::numeric_limits<double>::infinity ()};
    report.currentPose.tcpPosition = {{std::numeric_limits<double>::quiet_NaN (), 0.2, 0.3}};
    report.currentPose.tcpRpyDeg = {{0.1, std::numeric_limits<double>::infinity (), 0.3}};
    report.currentPose.jointLimitMargins = {std::numeric_limits<double>::quiet_NaN ()};
    report.currentPose.jacobianRowMajor = {std::numeric_limits<double>::infinity ()};
    report.currentPose.singularValues = {std::numeric_limits<double>::quiet_NaN ()};
    report.currentPose.conditionNumber = std::numeric_limits<double>::infinity ();
    report.currentPose.manipulability = std::numeric_limits<double>::quiet_NaN ();
    rws::TargetEvaluation task;
    task.stage = rws::AnalysisEvidenceStage::Quick;
    task.feasibility = rws::Feasibility::Feasible;
    task.quality = rws::Quality::Good;
    task.level = rws::RequirementExecutionLevel::Should;
    task.target.id = "report-task";
    task.target.name = "Report Task";
    rws::TargetCandidate candidate;
    candidate.positionErrorMeters = 0.0001;
    candidate.orientationErrorDeg = 0.1;
    candidate.distanceToReferenceQ = std::numeric_limits<double>::infinity ();
    candidate.score = std::numeric_limits<double>::quiet_NaN ();
    candidate.configuration.q = rw::math::Q (1, std::numeric_limits<double>::infinity ());
    candidate.configuration.jointLimitMargins = {std::numeric_limits<double>::quiet_NaN ()};
    candidate.configuration.jacobianRowMajor = {std::numeric_limits<double>::infinity ()};
    candidate.configuration.singularValues = {std::numeric_limits<double>::quiet_NaN ()};
    candidate.configuration.tcpPose = rw::math::Transform3D<> (
        rw::math::Vector3D<> (std::numeric_limits<double>::infinity (), 0.0, 0.0),
        rw::math::Rotation3D<> (
            std::numeric_limits<double>::quiet_NaN (), 0.0, 0.0,
            0.0, 1.0, 0.0,
            0.0, 0.0, 1.0));
    candidate.positionErrorMeters = std::numeric_limits<double>::infinity ();
    candidate.orientationErrorDeg = std::numeric_limits<double>::quiet_NaN ();
    candidate.configuration.minimumJointMargin = 0.25;
    candidate.configuration.manipulability = 0.5;
    task.candidates.push_back (candidate);
    report.taskResults.push_back (task);
    rws::RegionCoverageResult region;
    region.stage = rws::AnalysisEvidenceStage::Verified;
    region.feasibility = rws::Feasibility::Feasible;
    region.quality = rws::Quality::Good;
    region.regionId = "report-region";
    region.totalCells = 1;
    region.reachableCells = 1;
    region.positionCoverage = std::numeric_limits<double>::infinity ();
    region.orientationCoverage = 1.0;
    rws::RegionCellResult cell;
    cell.position = {{std::numeric_limits<double>::quiet_NaN (), 0.2, 0.3}};
    cell.feasibility = rws::Feasibility::Feasible;
    cell.quality = rws::Quality::Good;
    cell.reachableOrientationCount = 1;
    cell.sampledOrientationCount = 1;
    cell.bestManipulability = std::numeric_limits<double>::infinity ();
    cell.bestJointMargin = std::numeric_limits<double>::quiet_NaN ();
    region.cells.push_back (cell);
    report.regionResults.push_back (region);

    rws::TargetEvaluation filteredTask;
    filteredTask.stage = rws::AnalysisEvidenceStage::Estimated;
    filteredTask.feasibility = rws::Feasibility::Infeasible;
    filteredTask.quality = rws::Quality::Critical;
    filteredTask.level = rws::RequirementExecutionLevel::Info;
    filteredTask.target.id = "filtered-task";
    filteredTask.failureReasons.push_back (rws::KinematicFailureReason::InvalidTarget);
    report.taskResults.push_back (filteredTask);

    const QJsonObject object = rws::KinematicAnalysisReportJson::toObject (report);
    for (const QString& key : {QStringLiteral ("schemaVersion"), QStringLiteral ("pluginName"),
                               QStringLiteral ("analysisId"), QStringLiteral ("provenance"),
                               QStringLiteral ("feasibility"), QStringLiteral ("quality"),
                               QStringLiteral ("evidenceStage"), QStringLiteral ("currentPose"),
                               QStringLiteral ("taskResults"), QStringLiteral ("regionResults"),
                               QStringLiteral ("warnings")}) {
        if (const int rc = require (object.contains (key),
                                    "report JSON contains the stable root field"))
            return rc;
    }
    const QJsonObject pose = object.value (QStringLiteral ("currentPose")).toObject ();
    if (const int rc = require (
            pose.value (QStringLiteral ("conditionNumber")).isNull () &&
                pose.value (QStringLiteral ("manipulability")).isNull (),
            "report JSON converts non-finite doubles to null"))
        return rc;
    if (const int rc = require (
            object.value (QStringLiteral ("taskResults")).toArray ().size () == 2 &&
                object.value (QStringLiteral ("regionResults")).toArray ().size () == 1,
            "report JSON contains task and region result details"))
        return rc;
    const QJsonObject taskCandidate = object.value (QStringLiteral ("taskResults")).toArray ().at (0)
        .toObject ().value (QStringLiteral ("candidates")).toArray ().at (0).toObject ();
    const QJsonObject taskConfiguration = taskCandidate.value (QStringLiteral ("configuration")).toObject ();
    const QJsonObject tcpPose = taskConfiguration.value (QStringLiteral ("tcpPose")).toObject ();
    if (const int rc = require (
            taskCandidate.value (QStringLiteral ("positionErrorMeters")).isNull () &&
                taskCandidate.value (QStringLiteral ("orientationErrorDeg")).isNull () &&
                taskCandidate.value (QStringLiteral ("distanceToReferenceQ")).isNull () &&
                taskCandidate.value (QStringLiteral ("score")).isNull () &&
                taskConfiguration.value (QStringLiteral ("q")).toArray ().at (0).isNull () &&
                taskConfiguration.value (QStringLiteral ("jointLimitMargins")).toArray ().at (0).isNull () &&
                taskConfiguration.value (QStringLiteral ("jacobianRowMajor")).toArray ().at (0).isNull () &&
                taskConfiguration.value (QStringLiteral ("singularValues")).toArray ().at (0).isNull () &&
                tcpPose.value (QStringLiteral ("position")).toArray ().at (0).isNull () &&
                tcpPose.value (QStringLiteral ("rotationRowMajor")).toArray ().at (0).isNull (),
            "report JSON converts task candidate and configuration non-finite doubles to null"))
        return rc;
    const QJsonObject regionObject = object.value (QStringLiteral ("regionResults")).toArray ().at (0).toObject ();
    const QJsonObject regionCell = regionObject.value (QStringLiteral ("cells")).toArray ().at (0).toObject ();
    if (const int rc = require (
            regionObject.value (QStringLiteral ("positionCoverage")).isNull () &&
                regionCell.value (QStringLiteral ("position")).toArray ().at (0).isNull () &&
                regionCell.value (QStringLiteral ("bestManipulability")).isNull () &&
                regionCell.value (QStringLiteral ("bestJointMargin")).isNull (),
            "report JSON converts region non-finite doubles to null"))
        return rc;
    const QJsonArray taskObjects = object.value (QStringLiteral ("taskResults")).toArray ();
    if (const int rc = require (
            taskObjects.at (0).toObject ().value (QStringLiteral ("level")).toString () ==
                    QStringLiteral ("Should") &&
                taskObjects.at (1).toObject ().value (QStringLiteral ("level")).toString () ==
                    QStringLiteral ("Info"),
            "report JSON preserves task requirement levels"))
        return rc;
    const QJsonArray reportWarnings = object.value (QStringLiteral ("warnings")).toArray ();
    if (const int rc = require (
            !reportWarnings.isEmpty () &&
                reportWarnings.at (0).toObject ().value (QStringLiteral ("code")).toString () ==
                    QStringLiteral ("KIN_REPORT_NONFINITE"),
            "report JSON adds a diagnostic when non-finite values become null"))
        return rc;

    rws::KinematicAnalysisReport decoded;
    std::string error;
    if (const int rc = require (
            rws::KinematicAnalysisReportJson::fromObject (object, decoded, &error) &&
                error.empty () && decoded.analysisId == report.analysisId &&
                decoded.provenance.robotModelFingerprint ==
                    report.provenance.robotModelFingerprint &&
                decoded.evidenceStage == report.evidenceStage &&
                decoded.taskResults.size () == 2 &&
                decoded.taskResults.front ().target.id == "report-task" &&
                decoded.taskResults.front ().level == rws::RequirementExecutionLevel::Should &&
                decoded.taskResults.back ().level == rws::RequirementExecutionLevel::Info &&
                decoded.regionResults.size () == 1 &&
                decoded.regionResults.front ().regionId == "report-region" &&
                decoded.regionResults.front ().cells.size () == 1 &&
                !std::isfinite (decoded.currentPose.q.front ()) &&
                !std::isfinite (decoded.taskResults.front ().candidates.front ().score) &&
                !std::isfinite (decoded.taskResults.front ().candidates.front ().configuration.q[0]) &&
                !std::isfinite (decoded.taskResults.front ().candidates.front ().configuration.tcpPose.P ()[0]) &&
                !std::isfinite (decoded.regionResults.front ().positionCoverage) &&
                !std::isfinite (decoded.regionResults.front ().cells.front ().position[0]),
            "report JSON round trips identity and provenance"))
        return rc;
    // 篡改一个任务的 level 为未知值,验证反序列化整体拒绝(而非静默降级),
    // 保证未知等级永远不会出现在下游分析结果中。
    QJsonObject unknownLevelObject = object;
    QJsonArray unknownLevelTasks = unknownLevelObject.value (QStringLiteral ("taskResults")).toArray ();
    QJsonObject unknownLevelTask = unknownLevelTasks.at (0).toObject ();
    unknownLevelTask[QStringLiteral ("level")] = QStringLiteral ("Unknown");
    unknownLevelTasks[0] = unknownLevelTask;
    unknownLevelObject[QStringLiteral ("taskResults")] = unknownLevelTasks;
    rws::KinematicAnalysisReport rejectedReport;
    std::string rejectionError;
    if (const int rc = require (
            !rws::KinematicAnalysisReportJson::fromObject (
                unknownLevelObject, rejectedReport, &rejectionError) &&
                !rejectionError.empty (),
            "report JSON rejects unknown task requirement levels"))
        return rc;
    // CSV 导出:表头是稳定契约(供下游脚本/表格解析),并须保留任务的 level 字段。
    const std::string taskCsv = rws::KinematicAnalysisReportJson::taskCsv (report);
    const std::string regionCsv = rws::KinematicAnalysisReportJson::regionCsv (report);
    if (const int rc = require (
            taskCsv.find ("id,level,feasibility,quality,position_error_m,orientation_error_deg,min_joint_margin,manipulability,collision_checked,collision,failure_reasons") == 0,
            "task CSV uses the stable report header"))
        return rc;
    if (const int rc = require (
            taskCsv.find ("report-task,Should,") != std::string::npos &&
                taskCsv.find ("filtered-task,Info,") != std::string::npos,
            "task CSV preserves task requirement levels"))
        return rc;
    if (const int rc = require (
            regionCsv.find ("region_id,stage,total_cells,reachable_cells,position_coverage,orientation_coverage,feasibility,quality") == 0,
            "region CSV uses the stable report header"))
        return rc;

    // filterReportView 是只读视图:先记录源报告规模,再分别按证据阶段/可行性/
    // 失败原因/区域 id 过滤,最后断言源报告完全未被修改。
    const std::size_t originalTaskCount = report.taskResults.size ();
    const std::size_t originalRegionCount = report.regionResults.size ();
    rws::KinematicAnalysisReportFilters filters;
    filters.stage = rws::AnalysisEvidenceStage::Quick;
    filters.filterStage = true;
    rws::KinematicAnalysisReport filtered =
        rws::filterReportView (report, filters);
    if (const int rc = require (
            filtered.taskResults.size () == 1 &&
                filtered.taskResults.front ().target.id == "report-task" &&
                filtered.regionResults.empty (),
            "report view filters by evidence stage"))
        return rc;
    filters = rws::KinematicAnalysisReportFilters ();
    filters.feasibility = rws::Feasibility::Infeasible;
    filters.filterFeasibility = true;
    filtered = rws::filterReportView (report, filters);
    if (const int rc = require (
            filtered.taskResults.size () == 1 &&
                filtered.taskResults.front ().target.id == "filtered-task",
            "report view filters by feasibility"))
        return rc;
    filters = rws::KinematicAnalysisReportFilters ();
    filters.failureReason = rws::KinematicFailureReason::InvalidTarget;
    filters.filterFailureReason = true;
    filtered = rws::filterReportView (report, filters);
    if (const int rc = require (
            filtered.taskResults.size () == 1 &&
                filtered.taskResults.front ().target.id == "filtered-task",
            "report view filters by failure reason"))
        return rc;
    filters = rws::KinematicAnalysisReportFilters ();
    filters.regionId = "report-region";
    filtered = rws::filterReportView (report, filters);
    if (const int rc = require (
            filtered.taskResults.size () == report.taskResults.size () &&
                filtered.regionResults.size () == 1,
            "report view filters by region id"))
        return rc;
    if (const int rc = require (
            report.taskResults.size () == originalTaskCount &&
                report.regionResults.size () == originalRegionCount &&
                report.taskResults.back ().target.id == "filtered-task" &&
                report.regionResults.front ().regionId == "report-region",
            "report view filtering leaves source results unchanged"))
        return rc;
    return 0;
}

// 子套件 批缓存键:验证 makeKinematicBatchCacheKey 把所有来源维度
// (模型/环境/需求指纹、证据阶段、配置名、随机种子)都编码进缓存键字符串,
// 并且仅改变随机种子就能得到不同的键,保证批量结果缓存不会串用。
// 补充说明:批量结果缓存是"分析选项 -> 结果"的映射,任何来源维度(模型/环境/需求
// 指纹、证据阶段、配置名、随机种子)变化都必须产生不同缓存键,否则会串用旧结果。
// 这里验证全部维度都编码进字符串,且仅改随机种子 42->43 即得到不同键。
static int testBatchCacheKey ()
{
    const rws::KinematicBatchCacheKey base = rws::makeKinematicBatchCacheKey (
        "model", "environment", "requirements", rws::AnalysisEvidenceStage::Verified,
        "config", 42);
    const std::string encoded = base.toString ();
    if (const int rc = require (
            encoded.find ("model") != std::string::npos &&
                encoded.find ("environment") != std::string::npos &&
                encoded.find ("requirements") != std::string::npos &&
                encoded.find ("Verified") != std::string::npos &&
                encoded.find ("config") != std::string::npos &&
                encoded.find ("42") != std::string::npos,
            "batch cache key contains every provenance dimension"))
        return rc;
    const rws::KinematicBatchCacheKey changed = rws::makeKinematicBatchCacheKey (
        "model", "environment", "requirements", rws::AnalysisEvidenceStage::Verified,
        "config", 43);
    if (const int rc = require (base != changed,
                                "changing the random seed isolates a batch cache key"))
        return rc;
    return 0;
}

// 子套件 分析上下文:验证 makeAnalysisContext 的输入校验链——缺少 WorkCell /
// Device / TCP / 模型指纹 / 环境指纹时依次返回稳定的错误码(KIN_CONTEXT_*)。
// 在能力缺失(如必需碰撞但无检测器)时仍能构造可求值上下文并记录显式告警,
// 同时确认传入的 State 被深度拷贝到 context.baseState,不受外部改写影响。
// 补充说明:
//   - 错误码链顺序固定:NO_WORKCELL -> NO_DEVICE -> NO_TCP -> NO_MODEL_FINGERPRINT
//     -> NO_ENVIRONMENT_FINGERPRINT,任何顺序变化都会破坏上层 UI 的提示逻辑;
//   - 碰撞检测器缺失(collisionRequired=true)时仍构造成功但记录显式告警
//     KIN_COLLISION_DETECTOR_UNAVAILABLE,而不是让调用方猜测能力缺失;
//   - 最后验证 context.baseState 是输入 State 的深拷贝:外部改写 device 的 Q 后
//     context.baseState 内位姿保持不变,保证分析不依赖调用方后续改动。
static int testAnalysisContext ()
{
    rws::AnalysisContextInput input;
    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (
            !rws::makeAnalysisContext (input, context, &error) &&
                error == "KIN_CONTEXT_NO_WORKCELL",
            "context rejects a missing WorkCell with a stable code"))
        return rc;

    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "ContextWorkCell", ""));
    input.workcell = workcell;
    input.baseState = workcell->getDefaultState ();
    if (const int rc = require (
            !rws::makeAnalysisContext (input, context, &error) &&
                error == "KIN_CONTEXT_NO_DEVICE",
            "context rejects a missing Device with a stable code"))
        return rc;

    input.device = device;
    if (const int rc = require (
            !rws::makeAnalysisContext (input, context, &error) &&
                error == "KIN_CONTEXT_NO_TCP",
            "context rejects a missing TCP with a stable code"))
        return rc;

    input.tcpFrame = device->getEnd ();
    if (const int rc = require (
            !rws::makeAnalysisContext (input, context, &error) &&
                error == "KIN_CONTEXT_NO_MODEL_FINGERPRINT",
            "context rejects a missing model fingerprint with a stable code"))
        return rc;
    input.modelFingerprint = "model-fingerprint";
    if (const int rc = require (
            !rws::makeAnalysisContext (input, context, &error) &&
                error == "KIN_CONTEXT_NO_ENVIRONMENT_FINGERPRINT",
            "context rejects a missing environment fingerprint with a stable code"))
        return rc;

    input.environmentFingerprint = "environment-fingerprint";
    input.collisionRequired = true;
    const rw::math::Q originalQ (6, 0.1, -0.2, 0.3, -0.4, 0.5, -0.6);
    device->setQ (originalQ, input.baseState);
    if (const int rc = require (
            rws::makeAnalysisContext (input, context, &error) && error.empty (),
            "missing optional capability still creates an evaluable context"))
        return rc;
    if (const int rc = require (
            context.workcell == workcell && context.device == device,
            "context owns the WorkCell and Device inputs"))
        return rc;
    device->setQ (rw::math::Q (6, 0.0), input.baseState);
    const rw::math::Q copiedQ = device->getQ (context.baseState);
    for (std::size_t i = 0; i < originalQ.size (); ++i) {
        if (const int rc = assertNear (copiedQ[i], originalQ[i], 1e-12,
                                       "context copies the State input"))
            return rc;
    }
    if (const int rc = require (
            context.capabilityWarnings.size () == 1 &&
                context.capabilityWarnings.front ().code ==
                    "KIN_COLLISION_DETECTOR_UNAVAILABLE",
            "required missing collision capability is explicit"))
        return rc;
    return 0;
}

// 子套件 配置求值:验证 ConfigurationEvaluator 的能力降级语义——
// 必需碰撞检查但缺少碰撞检测器时结果为 DataInsufficient 且带
// CollisionDetectorUnavailable 失败原因;可选碰撞检查则明确标记
// collisionChecked=false,不做任何碰撞判定,避免误报"未检测即为无碰撞"。
// 补充说明:这是"能力降级语义"的回归保障——必需碰撞但缺检测器时,结果必须是
// DataInsufficient 并带 CollisionDetectorUnavailable 失败原因,而不是假装"未检测
// 即无碰撞"而误判 Feasible;可选碰撞(collisionRequired=false)则显式标记
// collisionChecked=false / inCollision=false,让上层 UI 明确显示"未做碰撞检查"。
static int testConfigurationEvaluator ()
{
    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "ConfigurationWorkCell", ""));

    rws::AnalysisContextInput input;
    input.workcell = workcell;
    input.device = device;
    input.tcpFrame = device->getEnd ();
    input.baseState = workcell->getDefaultState ();
    input.modelFingerprint = "model-fingerprint";
    input.environmentFingerprint = "environment-fingerprint";
    input.collisionRequired = true;

    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "configuration context is valid"))
        return rc;

    rws::ConfigurationEvaluator evaluator;
    rws::ConfigurationEvaluationOptions options;
    const rws::ConfigurationEvaluation requiredResult = evaluator.evaluate (
        context, device->getQ (context.baseState), options);
    if (const int rc = require (
            requiredResult.feasibility == rws::Feasibility::DataInsufficient &&
                std::find (requiredResult.failureReasons.begin (),
                           requiredResult.failureReasons.end (),
                           rws::KinematicFailureReason::CollisionDetectorUnavailable) !=
                    requiredResult.failureReasons.end (),
            "required collision without a detector is DataInsufficient"))
        return rc;

    input.collisionRequired = false;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "optional collision context is valid"))
        return rc;
    const rws::ConfigurationEvaluation optionalResult = evaluator.evaluate (
        context, device->getQ (context.baseState), options);
    if (const int rc = require (!optionalResult.collisionChecked &&
                                    !optionalResult.inCollision,
                                "optional missing collision is explicitly unchecked"))
        return rc;
    return 0;
}

// 子套件 目标残差分类:验证 classifyTargetResidual 在残差超出容差时给出
// TargetResidual 失败原因并记录 KIN_TARGET_RESIDUAL 告警;非有限残差一律
// Fail 关闭(拒绝任何 NaN 解析)。同时验证 analyzeIk 对非有限目标返回
// InvalidTarget,对有效目标但缺失设备返回 NoDevice,失败原因不被吞掉。
// 补充说明:classifyTargetResidual 的容差来源是 KinematicThresholds 的
// positionToleranceMeters / orientationToleranceDeg;位置残差 0.5mm < 1mm 通过,
// 2mm > 1mm 失败并同时记录失败原因与 KIN_TARGET_RESIDUAL 告警;
// 非有限残差"失败关闭"(fail-closed),拒绝任何 NaN 被当作可解;
// analyzeIk 对非有限目标返回 InvalidTarget、对有效目标但 NULL 设备返回 NoDevice,
// 确保失败原因不被通用逻辑吞掉。
static int testTargetValidationAndResidual ()
{
    rws::KinematicThresholds thresholds;
    std::vector< rws::KinematicFailureReason > reasons;
    std::vector< rws::AnalysisWarning > warnings;

    rws::AnalysisStatus status = rws::classifyTargetResidual (
        0.0005, 0.5,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Pass,
                                "residual inside tolerance passes"))
        return rc;
    if (const int rc = require (reasons.empty () && warnings.empty (),
                                "passing residual has no diagnostics"))
        return rc;

    status = rws::classifyTargetResidual (
        0.002, 0.5,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Fail,
                                "position residual outside tolerance fails"))
        return rc;
    if (const int rc = require (
            reasons.size () == 1 &&
                reasons.front () == rws::KinematicFailureReason::TargetResidual,
            "target residual failure reason is recorded"))
        return rc;
    if (const int rc = require (!warnings.empty () && warnings.front ().code == "KIN_TARGET_RESIDUAL",
                                "target residual warning is recorded"))
        return rc;

    reasons.clear ();
    warnings.clear ();
    status = rws::classifyTargetResidual (
        std::numeric_limits< double >::quiet_NaN (), 0.0,
        thresholds.positionToleranceMeters,
        thresholds.orientationToleranceDeg,
        &reasons, &warnings);
    if (const int rc = require (status == rws::AnalysisStatus::Fail,
                                "non-finite residual fails closed"))
        return rc;

    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;
    rws::TaskPoint invalidTarget;
    invalidTarget.position[0] = std::numeric_limits< double >::quiet_NaN ();
    const rws::KinematicIkAnalysisResult invalid =
        analyzer.analyzeIk (NULL, NULL, state, invalidTarget, NULL);
    if (const int rc = require (invalid.status == rws::AnalysisStatus::Fail,
                                "non-finite target fails"))
        return rc;
    if (const int rc = require (
            invalid.failureReason == rws::KinematicFailureReason::InvalidTarget,
            "non-finite target preserves InvalidTarget"))
        return rc;

    rws::TaskPoint validTarget;
    const rws::KinematicIkAnalysisResult noDevice =
        analyzer.analyzeIk (NULL, NULL, state, validTarget, NULL);
    if (const int rc = require (
            noDevice.failureReason == rws::KinematicFailureReason::NoDevice,
            "valid target without a device preserves NoDevice"))
        return rc;
    return 0;
}

// 子套件 2:analyzeCurrentPose 在 NULL device 下的降级路径,以及阈值存取。
// 子套件 单位换算:验证长度单位(米→毫米、厘米→米)与角度单位
// (度→弧度、圈→度)的显示换算精度,以及长度/角度单位的
// toString 与 unitSuffix("rad")稳定输出,保证 UI 显示与导出一致。
// 补充说明:显示单位换算用于 UI 输入/显示与导出的一致性,改动会导致用户看到的数值
// 与实际分析单位不符;这里对四组换算(米->毫米、厘米->米、度->弧度、圈->度)做高精度
// 断言,并锁定长度/角度单位的 toString 与 unitSuffix(如 "rad")稳定输出。
static int testPoseUnitConversions ()
{
    if (const int rc = assertNear (
            rws::displayLengthFromMeters (0.125, rws::KinematicLengthUnit::Millimeters),
            125.0, 1e-12, "meters to millimeters"))
        return rc;
    if (const int rc = assertNear (
            rws::metersFromDisplayLength (12.5, rws::KinematicLengthUnit::Centimeters),
            0.125, 1e-12, "centimeters to meters"))
        return rc;
    if (const int rc = assertNear (
            rws::displayAngleFromDegrees (180.0, rws::KinematicAngleUnit::Radians),
            rw::math::Pi, 1e-12, "degrees to radians"))
        return rc;
    if (const int rc = assertNear (
            rws::degreesFromDisplayAngle (0.25, rws::KinematicAngleUnit::Turns),
            90.0, 1e-12, "turns to degrees"))
        return rc;
    if (const int rc = require (
            std::string (rws::toString (rws::KinematicLengthUnit::Millimeters)) ==
                "Millimeters",
            "length unit string"))
        return rc;
    if (const int rc = require (
            std::string (rws::unitSuffix (rws::KinematicAngleUnit::Radians)) == "rad",
            "angle unit suffix"))
        return rc;
    return 0;
}

// 子套件 当前位姿:先验证 analyzeCurrentPose 在 NULL 设备下的降级路径
// (返回 Fail + 告警 + 空设备名),并确认阈值可存取。随后用真实六轴设备
// 验证非 NULL 路径:报告的位姿与 FK 结果一致,且雅可比维度、最小关节裕度、
// 条件数、可操作性等指标与 ConfigurationEvaluator 直接求值的结果完全一致,
// 证明"当前位姿"只是配置求值的一个视图。
// 补充说明:
//   - 第一部分验证 NULL 设备降级(Fail + 告警 + 空设备名),并确认阈值可读写;
//   - 第二部分用真实六轴设备验证"当前位姿"只是配置求值的一个视图:FK 位姿、
//     雅可比维度、最小关节裕度、条件数、可操作性全部与 ConfigurationEvaluator
//     直接求值一致,任何一处偏差都说明 analyzeCurrentPose 没有正确委托。
static int testCurrentPose ()
{
    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State emptyState;
    const rws::KinematicCurrentPoseResult result =
        analyzer.analyzeCurrentPose (NULL, NULL, emptyState);
    if (const int rc =
            require (result.status == rws::AnalysisStatus::Fail, "null device triggers Fail"))
        return rc;
    if (const int rc = require (!result.warnings.empty (), "null device emits a warning"))
        return rc;
    if (const int rc = require (result.deviceName.empty (), "no device name set"))
        return rc;

    analyzer.setThresholds (rws::KinematicThresholds ());
    const rws::KinematicThresholds& t = analyzer.thresholds ();
    if (const int rc = assertNear (t.nearJointLimitRatio, 0.05, 1e-12, "thresholds preserved"))
        return rc;

    // Characterize the non-null path before delegating it to ConfigurationEvaluator.
    // The public result must continue to report the same base-to-TCP FK pose.
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::math::Q q (6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7);
    device->setQ (q, state);
    const rw::math::Transform3D<> expectedTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rws::KinematicCurrentPoseResult evaluated =
        analyzer.analyzeCurrentPose (device, device->getEnd (), state);
    if (const int rc = require (evaluated.q.size () == q.size (),
                                "current pose reports every joint"))
        return rc;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (const int rc = assertNear (evaluated.tcpPosition[axis], expectedTcp.P ()[axis],
                                       1e-12, "current pose matches FK position"))
            return rc;
    }

    rws::AnalysisContext context;
    context.device = device;
    context.tcpFrame = device->getEnd ();
    context.baseState = state;
    context.thresholds = analyzer.thresholds ();
    rws::ConfigurationEvaluationOptions options;
    options.checkCollision = false;
    const rws::ConfigurationEvaluation direct =
        rws::ConfigurationEvaluator ().evaluate (context, q, options);
    if (const int rc = require (
            direct.jacobianRows == evaluated.jacobianRows &&
                direct.jacobianCols == evaluated.jacobianCols &&
                direct.jacobianRowMajor.size () == evaluated.jacobianRowMajor.size (),
            "current pose and configuration evaluator expose the same Jacobian"))
        return rc;
    if (const int rc = assertNear (direct.minimumJointMargin,
                                   evaluated.minJointLimitMargin, 1e-12,
                                   "current pose delegates joint margin"))
        return rc;
    if (const int rc = assertNear (direct.conditionNumber, evaluated.conditionNumber,
                                   1e-12, "current pose delegates condition number"))
        return rc;
    if (const int rc = assertNear (direct.manipulability, evaluated.manipulability,
                                   1e-12, "current pose delegates manipulability"))
        return rc;
    return 0;
}

// 子套件 目标求值:验证 TargetEvaluator 对"当前 FK 位姿"目标给出可行候选,
// 对 100 米外的目标报告 IkNoSolution,对缺失参考系报告 FrameNotFound,
// 并保留目标 id 与失败原因;同时确认旧 analyzeIk 入口能穿透到同一套逻辑,
// 保持向后兼容。
// 补充说明:三个场景分别覆盖"可行"(当前 FK 位姿)、"不可达"(100 米外,期望
// IkNoSolution)、"参考系缺失"(FrameNotFound),并逐一断言目标 id 保真;
// 最后的 legacy analyzeIk 调用验证新旧入口穿透到同一套 TargetEvaluator 逻辑,
// 保持向后兼容——旧插件/调用方不会因重构而链接失败。
static int testTargetEvaluator ()
{
    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "TargetEvaluatorWorkCell", ""));
    rw::kinematics::State state = workcell->getDefaultState ();
    const rw::math::Q currentQ (6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7);
    device->setQ (currentQ, state);

    rws::AnalysisContextInput input;
    input.workcell = workcell;
    input.device = device;
    input.tcpFrame = device->getEnd ();
    input.baseState = state;
    input.modelFingerprint = "target-model";
    input.environmentFingerprint = "target-environment";
    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "target evaluator context is valid"))
        return rc;

    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> currentRpy (currentTcp.R ());
    rws::TaskPoint reachable;
    reachable.id = "reachable-current-fk";
    reachable.refFrame = device->getBase ()->getName ();
    reachable.tcpFrame = device->getEnd ()->getName ();
    reachable.position = {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}};
    reachable.rpyDeg = {{currentRpy (0) * 180.0 / rw::math::Pi,
                         currentRpy (1) * 180.0 / rw::math::Pi,
                         currentRpy (2) * 180.0 / rw::math::Pi}};
    reachable.tolerance.positionMeters = 0.001;
    reachable.tolerance.orientationDeg = 1.0;

    rws::TargetEvaluationOptions options;
    options.checkCollision = false;
    rws::TargetEvaluator evaluator;
    const rws::TargetEvaluation reachableResult =
        evaluator.evaluate (context, reachable, options);
    if (const int rc = require (!reachableResult.candidates.empty (),
                                "current FK target produces a candidate"))
        return rc;
    if (const int rc = require (reachableResult.feasibility == rws::Feasibility::Feasible,
                                "current FK target is feasible"))
        return rc;
    if (const int rc = require (reachableResult.target.id == reachable.id,
                                "target identity is preserved"))
        return rc;

    rws::TaskPoint farAway = reachable;
    farAway.id = "unreachable-far-away";
    farAway.position = {{100.0, 100.0, 100.0}};
    const rws::TargetEvaluation farResult =
        evaluator.evaluate (context, farAway, options);
    if (const int rc = require (
            farResult.feasibility == rws::Feasibility::Infeasible &&
                std::find (farResult.failureReasons.begin (), farResult.failureReasons.end (),
                           rws::KinematicFailureReason::IkNoSolution) !=
                    farResult.failureReasons.end (),
            "far-away target reports IkNoSolution"))
        return rc;

    rws::TaskPoint missingFrame = reachable;
    missingFrame.id = "missing-reference-frame";
    missingFrame.refFrame = "NoSuchReferenceFrame";
    const rws::TargetEvaluation missingResult =
        evaluator.evaluate (context, missingFrame, options);
    if (const int rc = require (
        missingResult.feasibility == rws::Feasibility::Infeasible &&
            std::find (missingResult.failureReasons.begin (), missingResult.failureReasons.end (),
                       rws::KinematicFailureReason::FrameNotFound) !=
                missingResult.failureReasons.end (),
        "missing reference frame reports FrameNotFound"))
        return rc;

    rws::KinematicAnalyzer legacyAnalyzer;
    const rws::KinematicIkAnalysisResult legacyResult = legacyAnalyzer.analyzeIk (
        device, device->getEnd (), state, reachable, NULL);
    if (const int rc = require (!legacyResult.solutions.empty (),
                                "legacy analyzeIk remains usable through TargetEvaluator"))
        return rc;
    const rws::KinematicIkAnalysisResult legacyMissing = legacyAnalyzer.analyzeIk (
        device, device->getEnd (), state, missingFrame, NULL);
    return require (
        legacyMissing.failureReason == rws::KinematicFailureReason::FrameNotFound,
        "legacy analyzeIk honors missing reference frame diagnostics");
}

// 子套件 候选排序:验证 sortTargetCandidatesForDisplay 的显示优先级链——
// 无碰撞候选排在碰撞候选之前(碰撞候选排最后);在残差相同时用最小关节裕度
// 与可操作性打破平局;完全相同时按 Q 字典序保证排序确定性,便于 UI 稳定展示。
// 补充说明:排序优先级链为"无碰撞 > 残差小 > 关节裕度/可操作性 > Q 字典序",
// 碰撞候选总是最后。构造 5 个候选分别命中链上的不同环节,验证
// sortTargetCandidatesForDisplay 稳定、可预期,保证 UI 展示不抖动。
static int testTargetCandidateOrdering ()
{
    rws::TargetCandidate collisionCandidate;
    collisionCandidate.configuration.inCollision = true;
    collisionCandidate.configuration.q = rw::math::Q (2, 0.0, 0.0);
    collisionCandidate.positionErrorMeters = 0.0;
    collisionCandidate.orientationErrorDeg = 0.0;
    collisionCandidate.configuration.minimumJointMargin = 1.0;
    collisionCandidate.configuration.manipulability = 10.0;

    rws::TargetCandidate residualCandidate;
    residualCandidate.configuration.q = rw::math::Q (2, 0.0, 1.0);
    residualCandidate.positionErrorMeters = 0.01;
    residualCandidate.orientationErrorDeg = 0.0;
    residualCandidate.configuration.minimumJointMargin = 1.0;
    residualCandidate.configuration.manipulability = 10.0;

    rws::TargetCandidate marginCandidate;
    marginCandidate.configuration.q = rw::math::Q (2, 0.0, 2.0);
    marginCandidate.positionErrorMeters = 0.001;
    marginCandidate.orientationErrorDeg = 0.0;
    marginCandidate.configuration.minimumJointMargin = 0.8;
    marginCandidate.configuration.manipulability = 1.0;

    rws::TargetCandidate bestCandidate;
    bestCandidate.configuration.q = rw::math::Q (2, 0.0, 3.0);
    bestCandidate.positionErrorMeters = 0.001;
    bestCandidate.orientationErrorDeg = 0.0;
    bestCandidate.configuration.minimumJointMargin = 0.9;
    bestCandidate.configuration.manipulability = 2.0;

    rws::TargetCandidate lexicographicCandidate = bestCandidate;
    lexicographicCandidate.configuration.q = rw::math::Q (2, 0.0, 4.0);
    lexicographicCandidate.configuration.minimumJointMargin = bestCandidate.configuration.minimumJointMargin;
    lexicographicCandidate.configuration.manipulability = bestCandidate.configuration.manipulability;

    std::vector< rws::TargetCandidate > candidates = {
        collisionCandidate, lexicographicCandidate, residualCandidate, marginCandidate, bestCandidate};
    rws::sortTargetCandidatesForDisplay (candidates);
    if (const int rc = require (!candidates[0].configuration.inCollision,
                                "collision-free candidate sorts before collision candidate"))
        return rc;
    if (const int rc = require (candidates[0].configuration.q (1) == 3.0,
                                "joint margin and manipulability break residual ties"))
        return rc;
    if (const int rc = require (candidates[1].configuration.q (1) == 4.0,
                                "lexicographic Q order is deterministic"))
        return rc;
    return require (candidates.back ().configuration.inCollision,
                    "colliding candidate sorts last");
}

// 子套件 批处理 Must 聚合:构造 Must/Should/Info 与 Excluded 混合任务批,
// 验证 validateRequirements 只把 Included 的 Must 任务计入 mustTaskCount 与
// mustTaskFeasibleCount;任一 Must 任务不可达则整批 Infeasible,而 Should/Info
// 失败不会改变 Must 可行性;被 Excluded 的任务保持 NotEvaluated。
// 这正是"Must 是硬约束、Should/Info 仅供参考"语义的回归保障。
// 补充说明:makeTask lambda 按 id/level/reachable 构造任务,其中可达目标取"当前 FK
// 位姿"、不可达目标取 (100,100,100);6 个任务中 2 个 Included Must 可达、1 个
// Included Must 不可达、Should/Info 各 1 个不可达、1 个 Excluded Must;
// 断言 mustTaskCount=3 / mustTaskFeasibleCount=2、整批 Infeasible、Excluded 保持
// NotEvaluated;删除不可达 Must 后 Should/Info 失败不再影响整批(Feasible),
// 这是"Must 是硬约束、Should/Info 仅供参考"的语义回归。
static int testKinematicBatchRunnerMustOnlyAggregation ()
{
    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "BatchRunnerWorkCell", ""));
    rw::kinematics::State state = workcell->getDefaultState ();
    const rw::math::Q currentQ (6, 0.2, -0.4, 0.6, -0.3, 0.5, -0.7);
    device->setQ (currentQ, state);

    rws::AnalysisContextInput input;
    input.workcell = workcell;
    input.device = device;
    input.tcpFrame = device->getEnd ();
    input.baseState = state;
    input.modelFingerprint = "batch-model";
    input.environmentFingerprint = "batch-environment";
    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "batch runner context is valid"))
        return rc;

    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> currentRpy (currentTcp.R ());
    const auto makeTask = [&] (const std::string& id,
                               rws::RequirementExecutionLevel level,
                               bool reachable,
                               rws::RequirementExecutionCompileState compileState =
                                   rws::RequirementExecutionCompileState::Included) {
        rws::RequirementExecutionTask task;
        task.id = id;
        task.name = id;
        task.level = level;
        task.compileState = compileState;
        task.refFrame = device->getBase ()->getName ();
        task.tcpFrame = device->getEnd ()->getName ();
        task.position = reachable ?
            std::array< double, 3 > {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}} :
            std::array< double, 3 > {{100.0, 100.0, 100.0}};
        task.rpyDeg = {{currentRpy (0) * 180.0 / rw::math::Pi,
                        currentRpy (1) * 180.0 / rw::math::Pi,
                        currentRpy (2) * 180.0 / rw::math::Pi}};
        task.collisionFreeRequired = false;
        return task;
    };

    rws::RequirementExecutionSet requirements;
    requirements.provenance.requirementFingerprint = "batch-requirements";
    requirements.tasks.push_back (
        makeTask ("must-reachable-1", rws::RequirementExecutionLevel::Must, true));
    requirements.tasks.push_back (
        makeTask ("must-reachable-2", rws::RequirementExecutionLevel::Must, true));
    requirements.tasks.push_back (
        makeTask ("must-unreachable", rws::RequirementExecutionLevel::Must, false));
    requirements.tasks.push_back (
        makeTask ("should-unreachable", rws::RequirementExecutionLevel::Should, false));
    requirements.tasks.push_back (
        makeTask ("info-unreachable", rws::RequirementExecutionLevel::Info, false));
    requirements.tasks.push_back (
        makeTask ("must-excluded", rws::RequirementExecutionLevel::Must, true,
                  rws::RequirementExecutionCompileState::Excluded));

    rws::BatchRunOptions options;
    options.targetOptions.checkCollision = false;
    const rws::RequirementValidationSummary summary =
        rws::KinematicBatchRunner ().validateRequirements (
            context, requirements, options, rws::CancellationToken ());
    if (const int rc = require (summary.taskResults.size () == requirements.tasks.size (),
                                "batch preserves every requirement task"))
        return rc;
    if (const int rc = require (
            summary.taskResults[0].level == rws::RequirementExecutionLevel::Must &&
                summary.taskResults[3].level == rws::RequirementExecutionLevel::Should &&
                summary.taskResults[4].level == rws::RequirementExecutionLevel::Info,
            "batch copies requirement levels to task results"))
        return rc;
    if (const int rc = require (summary.mustTaskCount == 3,
                                "batch counts only included Must tasks"))
        return rc;
    if (const int rc = require (summary.mustTaskFeasibleCount == 2,
                                "batch counts feasible included Must tasks"))
        return rc;
    if (const int rc = require (summary.feasibility == rws::Feasibility::Infeasible,
                                "an infeasible Must task fails the batch"))
        return rc;
    if (const int rc = require (
            summary.taskResults.back ().feasibility == rws::Feasibility::NotEvaluated,
            "excluded task remains NotEvaluated"))
        return rc;

    requirements.tasks.erase (requirements.tasks.begin () + 2);
    const rws::RequirementValidationSummary shouldAndInfoDoNotFail =
        rws::KinematicBatchRunner ().validateRequirements (
            context, requirements, options, rws::CancellationToken ());
    return require (
        shouldAndInfoDoNotFail.feasibility == rws::Feasibility::Feasible &&
            shouldAndInfoDoNotFail.mustTaskCount == 2 &&
            shouldAndInfoDoNotFail.mustTaskFeasibleCount == 2,
        "Should and Info failures do not change Must feasibility");
}

// 取消回调:第二次检查时返回 true(第一次返回 false),让"第一个任务求值后、第二个
// 任务求值前"触发取消,从而验证取消语义发生在批处理任务之间。
static bool cancelAfterFirstBatchCheck (void* userData)
{
    int* checks = static_cast< int* > (userData);
    return (*checks)++ > 0;
}

// 子套件 批处理取消:验证取消回调在第二个任务求值前触发时,只保留已完成的
// 任务结果,整批状态为 DataInsufficient,并携带 KIN_BATCH_CANCELLED 告警——
// 取消不是"当作失败",而是明确表示"数据不足,结果不完整"。
// 补充说明:取消语义——不是"当作失败",而是 DataInsufficient + KIN_BATCH_CANCELLED;
// 只保留已完成的任务结果,未求值的任务不合成占位结果,防止 UI 把"取消"误显示为
// 失败或成功。这里用空 AnalysisContext 即可,因为取消发生在任何任务求值之前。
static int testKinematicBatchRunnerCancellation ()
{
    rws::RequirementExecutionSet requirements;
    rws::RequirementExecutionTask first;
    first.id = "first";
    first.refFrame = "WORLD";
    first.collisionFreeRequired = false;
    requirements.tasks.push_back (first);
    rws::RequirementExecutionTask second = first;
    second.id = "second";
    requirements.tasks.push_back (second);

    rws::AnalysisContext context;
    int checks = 0;
    rws::CancellationToken cancellation;
    cancellation.isCancellationRequested = &cancelAfterFirstBatchCheck;
    cancellation.userData = &checks;
    const rws::RequirementValidationSummary summary =
        rws::KinematicBatchRunner ().validateRequirements (
            context, requirements, rws::BatchRunOptions (), cancellation);
    if (const int rc = require (summary.taskResults.size () == 1,
                                "cancellation preserves only completed task results"))
        return rc;
    if (const int rc = require (summary.feasibility == rws::Feasibility::DataInsufficient,
                                "cancellation reports DataInsufficient"))
        return rc;
    return require (!summary.warnings.empty () &&
                        summary.warnings.front ().code == "KIN_BATCH_CANCELLED",
                    "cancellation carries a diagnostic warning");
}

// 子套件 批处理器入口:把"Must 聚合"与"取消"两条路径串行跑一遍,
// 首条失败即返回,作为 validateRequirements 的外部观察点。
// 补充说明:批处理器入口的对外观察点,串行跑"Must 聚合"与"取消"两个子用例,
// 首个失败即返回,作为 CTest 单独跑 "batch" 子套件时的执行入口。
static int testKinematicBatchRunner ()
{
    if (const int rc = testKinematicBatchRunnerMustOnlyAggregation ())
        return rc;
    return testKinematicBatchRunnerCancellation ();
}

// 子套件 需求汇总:验证 buildRequirementValidationSummary 的支配语义——
// Must 区域数据不足时整批状态为 DataInsufficient/Critical(不被 Should 任务
// 结果稀释);正确统计 Included 的 Must 任务/区域数量与可行数;保留 provenance;
// 去掉区域后仅 Must 任务可行即整批 Feasible,证明 Should 失败不影响 Must。
// 补充说明:buildRequirementValidationSummary 是"把各任务/区域结果聚合成整批结论"
// 的纯函数;Must 区域 DataInsufficient 时整批必须 DataInsufficient/Critical,不被
// Should 任务结果稀释;统计只算 Included 的 Must 项;去掉区域后仅 Must 任务可行即
// 整批 Feasible,证明 Should 失败不影响 Must。
static int testRequirementValidationSummary ()
{
    rws::RequirementExecutionSet requirements;
    requirements.provenance.requirementFingerprint = "summary-requirements";
    rws::RequirementExecutionTask mustTask;
    mustTask.id = "must-task";
    mustTask.level = rws::RequirementExecutionLevel::Must;
    mustTask.compileState = rws::RequirementExecutionCompileState::Included;
    rws::RequirementExecutionTask shouldTask = mustTask;
    shouldTask.id = "should-task";
    shouldTask.level = rws::RequirementExecutionLevel::Should;
    requirements.tasks = {mustTask, shouldTask};

    rws::TargetEvaluation mustTaskResult;
    mustTaskResult.feasibility = rws::Feasibility::Feasible;
    mustTaskResult.quality = rws::Quality::Good;
    rws::TargetEvaluation shouldTaskResult;
    shouldTaskResult.feasibility = rws::Feasibility::Infeasible;
    shouldTaskResult.quality = rws::Quality::Critical;

    rws::RequirementExecutionRegion mustRegion;
    mustRegion.id = "must-region";
    mustRegion.level = rws::RequirementExecutionLevel::Must;
    mustRegion.compileState = rws::RequirementExecutionCompileState::Included;
    requirements.workspaceRegions.push_back (mustRegion);
    rws::RegionCoverageResult mustRegionResult;
    mustRegionResult.feasibility = rws::Feasibility::DataInsufficient;
    mustRegionResult.quality = rws::Quality::Critical;
    mustRegionResult.regionId = mustRegion.id;

    const rws::RequirementValidationSummary insufficient =
        rws::buildRequirementValidationSummary (
            requirements,
            std::vector< rws::TargetEvaluation > {mustTaskResult, shouldTaskResult},
            std::vector< rws::RegionCoverageResult > {mustRegionResult});
    if (const int rc = require (
            insufficient.feasibility == rws::Feasibility::DataInsufficient &&
                insufficient.quality == rws::Quality::Critical,
            "Must region data insufficiency dominates requirement summary"))
        return rc;
    if (const int rc = require (
            insufficient.mustTaskCount == 1 && insufficient.mustTaskFeasibleCount == 1 &&
                insufficient.mustRegionCount == 1 && insufficient.mustRegionFeasibleCount == 0,
            "requirement summary counts included Must tasks and regions"))
        return rc;
    if (const int rc = require (
            insufficient.provenance.requirementFingerprint == "summary-requirements",
            "requirement summary preserves provenance"))
        return rc;

    requirements.workspaceRegions.clear ();
    const rws::RequirementValidationSummary mustOnlyFeasible =
        rws::buildRequirementValidationSummary (
            requirements,
            std::vector< rws::TargetEvaluation > {mustTaskResult, shouldTaskResult},
            std::vector< rws::RegionCoverageResult > ());
    return require (
        mustOnlyFeasible.feasibility == rws::Feasibility::Feasible &&
            mustOnlyFeasible.mustTaskCount == 1 &&
            mustOnlyFeasible.mustTaskFeasibleCount == 1,
        "Should task failure does not change Must feasibility");
}

// 子套件 转发:验证 KinematicAnalyzer::validateRequirements 只是把批处理器
// 的汇总结果原样转发——Excluded 任务仍出现在 taskResults 中但不计入 Must 计数,
// 空 Must 时状态为 NotEvaluated,保证高层入口不改变低层语义。
// 补充说明:KinematicAnalyzer::validateRequirements 只是转发批处理器的汇总,高层
// 入口不得改变低层语义:Excluded 任务仍出现在 taskResults(供 UI 展示)但不计入
// mustTaskCount;空 Must 时状态为 NotEvaluated(而非 Feasible/Infeasible)。
static int testKinematicAnalyzerRequirementValidationForwarding ()
{
    rws::RequirementExecutionSet requirements;
    rws::RequirementExecutionTask excluded;
    excluded.id = "excluded-forwarding-task";
    excluded.compileState = rws::RequirementExecutionCompileState::Excluded;
    requirements.tasks.push_back (excluded);

    const rws::RequirementValidationSummary summary =
        rws::KinematicAnalyzer ().validateRequirements (
            rws::AnalysisContext (), requirements, rws::BatchRunOptions (),
            rws::CancellationToken ());
    if (const int rc = require (summary.taskResults.size () == 1,
                                "analyzer forwarding preserves batch task result"))
        return rc;
    if (const int rc = require (summary.mustTaskCount == 0,
                                "analyzer forwarding exposes Must-only count"))
        return rc;
    return require (summary.feasibility == rws::Feasibility::NotEvaluated,
                    "analyzer forwarding preserves empty Must status");
}

// 子套件 Verified 区域网格:验证 RegionCoverageEvaluator.generateGrid 对非法
// 尺寸(<=0)/采样数(<2)/未知参考系返回 DataInsufficient;2x2x2 区域生成
// 8 个单元且索引/角落坐标确定可预测。generateTargets 覆盖四种朝向模式——
// Fixed(用固定 RPY)、AlignFrame(对齐目标 frame 姿态)、AlignGeometryNormal
// (对齐 frame 的 Z 轴)、PointAtTarget(指向目标点并按 rollSamples 扩展绕 Z 旋转),
// 并验证各模式产出的目标姿态数值;非法几何引用/缺失 frame/坏指向点均拒绝。
// 补充说明:此用例覆盖 Verified 区域的"网格生成 + 目标生成"两层;
// generateGrid 对非法输入(尺寸<=0 / 采样<2 / 未知参考系)返回 DataInsufficient,
// 2x2x2 区域生成 8 个单元且索引/角点坐标确定可预测(供测试与 UI 复用);
// generateTargets 的四种朝向模式逐一验证数值:Fixed 用固定 RPY、AlignFrame 用目标
// frame 姿态、AlignGeometryNormal 用目标 frame 的 Z 轴、PointAtTarget 指向目标点并
// 按 rollSamples 绕工具 Z 扩展;非法几何引用/缺失 frame/坏指向点均拒绝。
static int testVerifiedRegionGridGeneration ()
{
    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::kinematics::FixedFrame::Ptr orientationFrame = rw::core::ownedPtr (
        new rw::kinematics::FixedFrame (
            "RegionOrientationFrame",
            rw::math::Transform3D<> (rw::math::Vector3D<> (0.5, 0.25, 0.75),
                                     rw::math::RPY<> (0.0, 0.0, rw::math::Pi / 2.0))));
    stateStructure->addFrame (orientationFrame, stateStructure->getRoot ());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "RegionGridWorkCell", ""));

    rws::AnalysisContextInput input;
    input.workcell = workcell;
    input.device = device;
    input.tcpFrame = device->getEnd ();
    input.baseState = workcell->getDefaultState ();
    input.modelFingerprint = "region-grid-model";
    input.environmentFingerprint = "region-grid-environment";
    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "region grid context is valid"))
        return rc;

    rws::RequirementExecutionRegion region;
    region.id = "verified-box";
    region.refFrame = device->getBase ()->getName ();
    region.tcpFrame = device->getEnd ()->getName ();
    region.center = {{1.0, 2.0, 3.0}};
    region.size = {{2.0, 4.0, 6.0}};
    region.samplesPerAxis = 2;
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.collisionFreeRequired = false;

    rws::RegionCoverageEvaluator evaluator;
    rws::RequirementExecutionRegion invalidSize = region;
    invalidSize.size[1] = 0.0;
    if (const int rc = require (
            evaluator.generateGrid (context, invalidSize).feasibility ==
                rws::Feasibility::DataInsufficient,
            "non-positive region size is DataInsufficient"))
        return rc;

    rws::RequirementExecutionRegion invalidSamples = region;
    invalidSamples.samplesPerAxis = 1;
    if (const int rc = require (
            evaluator.generateGrid (context, invalidSamples).feasibility ==
                rws::Feasibility::DataInsufficient,
            "region requires at least two samples per axis"))
        return rc;

    rws::RequirementExecutionRegion missingFrame = region;
    missingFrame.refFrame = "NoSuchRegionFrame";
    if (const int rc = require (
            evaluator.generateGrid (context, missingFrame).feasibility ==
                rws::Feasibility::DataInsufficient,
            "unknown region frame is DataInsufficient"))
        return rc;

    const rws::RegionCoverageResult grid = evaluator.generateGrid (context, region);
    if (const int rc = require (
            grid.stage == rws::AnalysisEvidenceStage::Verified &&
                grid.feasibility == rws::Feasibility::NotEvaluated,
            "generated grid is Verified evidence awaiting IK"))
        return rc;
    if (const int rc = require (grid.totalCells == 8 && grid.cells.size () == 8,
                                "2x2x2 region generates eight cells"))
        return rc;
    if (const int rc = require (
            grid.cells.front ().index == std::array< int, 3 > {{0, 0, 0}} &&
                grid.cells.back ().index == std::array< int, 3 > {{1, 1, 1}},
            "region grid preserves deterministic cell indices"))
        return rc;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (const int rc = assertNear (grid.cells.front ().position[axis], 0.0, 1e-12,
                                       "region grid minimum corner"))
            return rc;
    }
    if (const int rc = require (
            nearlyEqual (grid.cells.back ().position[0], 2.0) &&
                nearlyEqual (grid.cells.back ().position[1], 4.0) &&
                nearlyEqual (grid.cells.back ().position[2], 6.0),
            "region grid maximum corner"))
        return rc;

    region.fixedRpyDeg = {{10.0, 20.0, 30.0}};
    const rws::RegionTargetGenerationResult fixed =
        evaluator.generateTargets (context, region, grid.cells.front ());
    if (const int rc = require (
            fixed.stage == rws::AnalysisEvidenceStage::Verified &&
                fixed.feasibility == rws::Feasibility::NotEvaluated &&
                fixed.targets.size () == 1,
            "Fixed region orientation generates one Verified target"))
        return rc;
    if (const int rc = require (
        fixed.targets.front ().position == grid.cells.front ().position &&
            fixed.targets.front ().rpyDeg == region.fixedRpyDeg &&
            fixed.targets.front ().refFrame == device->getBase ()->getName (),
        "Fixed region target uses the cell position and fixed RPY in device base"))
        return rc;

    region.orientationMode = rws::RequirementExecutionOrientationMode::AlignFrame;
    region.orientationTargetFrame = orientationFrame->getName ();
    const rws::RegionTargetGenerationResult aligned =
        evaluator.generateTargets (context, region, grid.cells.front ());
    if (const int rc = require (
            aligned.feasibility == rws::Feasibility::NotEvaluated &&
                aligned.targets.size () == 1,
            "AlignFrame generates one target"))
        return rc;
    const std::array< double, 3 >& alignedRpyDeg = aligned.targets.front ().rpyDeg;
    const rw::math::Rotation3D<> alignedRotation =
        rw::math::RPY<> (alignedRpyDeg[0] * rw::math::Deg2Rad,
                         alignedRpyDeg[1] * rw::math::Deg2Rad,
                         alignedRpyDeg[2] * rw::math::Deg2Rad)
            .toRotation3D ();
    const rw::math::Rotation3D<> expectedRotation =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), orientationFrame, context.baseState)
            .R ();
    bool rotationMatches = true;
    for (std::size_t row = 0; row < 3; ++row)
        for (std::size_t column = 0; column < 3; ++column)
            rotationMatches = rotationMatches &&
                              nearlyEqual (alignedRotation (row, column),
                                           expectedRotation (row, column), 1e-12);
    if (const int rc = require (
            rotationMatches,
            "AlignFrame uses target Frame orientation in device base"))
        return rc;

    region.orientationMode =
        rws::RequirementExecutionOrientationMode::AlignGeometryNormal;
    region.orientationTargetGeometry = "frame:" + orientationFrame->getName ();
    const rws::RegionTargetGenerationResult normalAligned =
        evaluator.generateTargets (context, region, grid.cells.front ());
    if (const int rc = require (
            normalAligned.feasibility == rws::Feasibility::NotEvaluated &&
                normalAligned.targets.size () == 1,
            "AlignGeometryNormal generates one target from a frame reference"))
        return rc;
    const std::array< double, 3 >& normalRpyDeg = normalAligned.targets.front ().rpyDeg;
    const rw::math::Vector3D<> generatedToolZ =
        rw::math::RPY<> (normalRpyDeg[0] * rw::math::Deg2Rad,
                         normalRpyDeg[1] * rw::math::Deg2Rad,
                         normalRpyDeg[2] * rw::math::Deg2Rad)
            .toRotation3D ()
            .getCol (2);
    const rw::math::Vector3D<> expectedNormal = expectedRotation.getCol (2);
    if (const int rc = require (
        nearlyEqual (generatedToolZ[0], expectedNormal[0], 1e-12) &&
            nearlyEqual (generatedToolZ[1], expectedNormal[1], 1e-12) &&
            nearlyEqual (generatedToolZ[2], expectedNormal[2], 1e-12),
        "AlignGeometryNormal aligns tool Z with referenced Frame Z"))
        return rc;

    region.orientationMode = rws::RequirementExecutionOrientationMode::PointAtTarget;
    region.orientationTargetFrame.clear ();
    region.orientationTargetPoint = "0, 1, 0";
    region.rollSamples = 2;
    const rws::RegionTargetGenerationResult pointing =
        evaluator.generateTargets (context, region, grid.cells.front ());
    if (const int rc = require (
            pointing.feasibility == rws::Feasibility::NotEvaluated &&
                pointing.targets.size () == 2,
            "PointAtTarget expands the requested roll samples"))
        return rc;
    std::array< rw::math::Rotation3D<>, 2 > pointingRotations;
    for (std::size_t sample = 0; sample < pointingRotations.size (); ++sample) {
        const std::array< double, 3 >& rpyDeg = pointing.targets[sample].rpyDeg;
        pointingRotations[sample] =
            rw::math::RPY<> (rpyDeg[0] * rw::math::Deg2Rad,
                             rpyDeg[1] * rw::math::Deg2Rad,
                             rpyDeg[2] * rw::math::Deg2Rad)
                .toRotation3D ();
        const rw::math::Vector3D<> toolZ = pointingRotations[sample].getCol (2);
        if (const int rc = require (
                nearlyEqual (toolZ[0], 0.0, 1e-12) &&
                    nearlyEqual (toolZ[1], 1.0, 1e-12) &&
                    nearlyEqual (toolZ[2], 0.0, 1e-12),
                "PointAtTarget keeps tool Z pointed at the target"))
            return rc;
    }
    if (const int rc = require (
        dot (pointingRotations[0].getCol (0), pointingRotations[1].getCol (0)) < -0.999,
        "PointAtTarget roll samples rotate around tool Z"))
        return rc;

    rws::RequirementExecutionRegion invalidOrientation = region;
    invalidOrientation.orientationMode =
        rws::RequirementExecutionOrientationMode::AlignGeometryNormal;
    invalidOrientation.orientationTargetGeometry = "mesh:unsupported";
    if (const int rc = require (
            evaluator.generateTargets (context, invalidOrientation, grid.cells.front ())
                    .feasibility == rws::Feasibility::DataInsufficient,
            "unknown geometry reference format is DataInsufficient"))
        return rc;
    invalidOrientation.orientationMode =
        rws::RequirementExecutionOrientationMode::AlignFrame;
    invalidOrientation.orientationTargetFrame = "NoSuchOrientationFrame";
    if (const int rc = require (
            evaluator.generateTargets (context, invalidOrientation, grid.cells.front ())
                    .feasibility == rws::Feasibility::DataInsufficient,
            "missing orientation Frame is DataInsufficient"))
        return rc;
    invalidOrientation.orientationMode =
        rws::RequirementExecutionOrientationMode::PointAtTarget;
    invalidOrientation.orientationTargetFrame.clear ();
    invalidOrientation.orientationTargetPoint = "not-a-point";
    return require (
        evaluator.generateTargets (context, invalidOrientation, grid.cells.front ())
                .feasibility == rws::Feasibility::DataInsufficient,
        "invalid pointing target is DataInsufficient");
}

// 子套件 Verified 区域求值:验证对不可达区域(中心在 100 米外)整区域 Infeasible,
// 且每个单元恰好求值一次(采样数=求值数);把覆盖率阈值降为 0 时区域 Feasible;
// 必需碰撞检查缺检测器在求值前即 DataInsufficient;超大方向/滚转采样被
// KIN_REGION_SAMPLING_LIMIT 拒绝;取消时保留已采样部分(标记部分进度)但整体
// 不通过,并带 KIN_REGION_CANCELLED 告警。
// 补充说明:对 100 米外的不可达区域,整区域 Infeasible 且每个单元恰好采样/求值一次
// (sampledOrientations == totalCells == 8);把覆盖率阈值降为 0 后 Feasible;
// 必需碰撞缺检测器在求值前即 DataInsufficient;超大方向/滚转采样被
// KIN_REGION_SAMPLING_LIMIT 拒绝(防无限运行);取消时保留已采样部分(partial 进度)
// 但整体不通过,并带 KIN_REGION_CANCELLED 告警。
static int testVerifiedRegionTargetEvaluation ()
{
    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "RegionIkWorkCell", ""));
    rws::AnalysisContextInput input;
    input.workcell = workcell;
    input.device = device;
    input.tcpFrame = device->getEnd ();
    input.baseState = workcell->getDefaultState ();
    input.modelFingerprint = "region-ik-model";
    input.environmentFingerprint = "region-ik-environment";
    rws::AnalysisContext context;
    std::string error;
    if (const int rc = require (rws::makeAnalysisContext (input, context, &error),
                                "region IK context is valid"))
        return rc;

    rws::RequirementExecutionRegion region;
    region.id = "unreachable-box";
    region.refFrame = device->getBase ()->getName ();
    region.tcpFrame = device->getEnd ()->getName ();
    region.center = {{100.0, 100.0, 100.0}};
    region.size = {{0.1, 0.1, 0.1}};
    region.samplesPerAxis = 2;
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.fixedRpyDeg = {{0.0, 0.0, 0.0}};
    region.collisionFreeRequired = false;

    const rws::RegionCoverageResult evaluated =
        rws::RegionCoverageEvaluator ().evaluate (context, region);
    if (const int rc = require (
            evaluated.stage == rws::AnalysisEvidenceStage::Verified &&
                evaluated.feasibility == rws::Feasibility::Infeasible,
            "unreachable region fails its coverage threshold"))
        return rc;
    if (const int rc = require (
            evaluated.totalCells == 8 && evaluated.sampledOrientations == 8 &&
                evaluated.reachableCells == 0 && evaluated.reachableOrientations == 0,
            "each region cell orientation is evaluated exactly once"))
        return rc;
    if (const int rc = require (
        std::all_of (evaluated.cells.begin (), evaluated.cells.end (),
                     [] (const rws::RegionCellResult& cell) {
                         return cell.sampledOrientationCount == 1 &&
                                cell.reachableOrientationCount == 0 &&
                                cell.feasibility == rws::Feasibility::Infeasible;
                     }),
        "unreachable region targets preserve per-cell TargetEvaluator results"))
        return rc;

    rws::RequirementExecutionRegion zeroThreshold = region;
    zeroThreshold.minimumCoverage = 0.0;
    zeroThreshold.minimumOrientationCoverage = 0.0;
    if (const int rc = require (
            rws::RegionCoverageEvaluator ().evaluate (context, zeroThreshold).feasibility ==
                rws::Feasibility::Feasible,
            "region meeting zero coverage thresholds is Feasible"))
        return rc;

    rws::RequirementExecutionRegion collisionRequired = region;
    collisionRequired.collisionFreeRequired = true;
    if (const int rc = require (
        rws::RegionCoverageEvaluator ().evaluate (context, collisionRequired).feasibility ==
            rws::Feasibility::DataInsufficient,
        "required collision detector absence is DataInsufficient before IK"))
        return rc;

    rws::RequirementExecutionRegion oversizedSampling = region;
    oversizedSampling.orientationMode =
        rws::RequirementExecutionOrientationMode::PointAtTarget;
    oversizedSampling.orientationTargetPoint = "0,0,0";
    oversizedSampling.directionSamples = 1000;
    oversizedSampling.rollSamples = 360;
    const rws::RegionCoverageResult limited =
        rws::RegionCoverageEvaluator ().evaluate (context, oversizedSampling);
    if (const int rc = require (
            limited.feasibility == rws::Feasibility::DataInsufficient &&
                std::any_of (limited.warnings.begin (), limited.warnings.end (),
                             [] (const rws::AnalysisWarning& warning) {
                                 return warning.code == "KIN_REGION_SAMPLING_LIMIT";
                             }),
            "composite Verified sampling limit rejects an unbounded run"))
        return rc;

    int cancellationChecks = 0;
    rws::CancellationToken cancellation;
    cancellation.userData = &cancellationChecks;
    cancellation.isCancellationRequested = [] (void* userData) {
        int& checks = *static_cast< int* > (userData);
        return ++checks > 1;
    };
    const rws::RegionCoverageResult cancelled =
        rws::RegionCoverageEvaluator ().evaluate (context, region, cancellation);
    return require (
        cancelled.feasibility == rws::Feasibility::DataInsufficient &&
            cancelled.sampledOrientations == 1 && cancelled.sampledOrientations < 8 &&
            std::any_of (cancelled.warnings.begin (), cancelled.warnings.end (),
                         [] (const rws::AnalysisWarning& warning) {
                             return warning.code == "KIN_REGION_CANCELLED";
                         }),
        "cancelled region evaluation preserves partial progress without passing");
}

// 子套件 3:关节裕度 + SVD 指标的纯算法正确性。
//   - q=5   → margin=0.5 → Pass,无警告;
//   - q=0.2 → margin=0.02 → Warning,至少一条警告;
//   - q=-1  → 超出 [0,10] → Fail,至少一条警告;
//   - 对角 J=[4,2]  → σ={4,2}, κ=2, manipulability=8, Pass;
//   - 对角 J=[4,0]  → κ=∞, Fail, 至少一条警告。
// 补充说明:关节裕度取"离上下限距离的最小值 / 关节范围",阈值 nearJointLimitRatio
// 默认 0.05;SVD 指标用对角雅可比直接构造,避免数值求解器带来的不确定性;
// 奇异配置([4,0])条件数为 inf 且 Fail,验证"奇异 = 不可用"而非静默通过。
static int testMetrics ()
{
    using namespace rw::math;
    rws::KinematicThresholds thresholds;

    // Joint-limit margins: bounds [0, 10], q [5] => margin 0.5; q [0.2] => 0.02.
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 5.0);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.5, 1e-9, "margin[0.5]"))
            return rc;
        if (const int rc =
                assertNear (rws::minimumJointLimitMargin (margins), 0.5, 1e-9, "min margin[0.5]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Pass, "classify Pass for q=5"))
            return rc;
        if (const int rc = require (warnings.empty (), "no warnings for q=5"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, 0.2);
        std::pair< Q, Q > bounds = {lo, hi};
        const std::vector< double > margins = rws::calculateJointLimitMargins (q, bounds);
        if (const int rc = require (margins.size () == 1, "margin size"))
            return rc;
        if (const int rc = assertNear (margins.front (), 0.02, 1e-9, "margin[0.02]"))
            return rc;
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Warning, "classify Warning for q=0.2"))
            return rc;
        if (const int rc = require (!warnings.empty (), "warning emitted for q=0.2"))
            return rc;
    }
    {
        Q lo(1, 0.0), hi(1, 10.0), q(1, -1.0);
        std::pair< Q, Q > bounds = {lo, hi};
        std::vector< rws::AnalysisWarning > warnings;
        const rws::AnalysisStatus s = rws::classifyJointLimitMargins (q, bounds, thresholds, &warnings);
        if (const int rc = require (s == rws::AnalysisStatus::Fail, "classify Fail outside bounds"))
            return rc;
        if (const int rc = require (!warnings.empty (), "fail warning emitted outside bounds"))
            return rc;
    }

    // SVD: condition 2 / manipulability 8 for diag [4, 2].
    {
        Jacobian j = Jacobian::zero (3, 2);
        // Set up a diagonal-ish 2-row x 2-col proxy by setting two rows to the identity.
        // We use a 3x2 zero Jacobian and only validate condition via direct matrix
        // construction. Cleaner: build a 2x2 Jacobian via Math::zero and assign.
        Jacobian j2(2, 2);
        j2 (0, 0) = 4.0;
        j2 (0, 1) = 0.0;
        j2 (1, 0) = 0.0;
        j2 (1, 1) = 2.0;
        (void) j;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j2, thresholds);
        if (const int rc =
                require (m.singularValues.size () == 2, "singularValues size"))
            return rc;
        if (const int rc = assertNear (m.singularValues[0], 4.0, 1e-6, "sigmaMax"))
            return rc;
        if (const int rc = assertNear (m.singularValues[1], 2.0, 1e-6, "sigmaMin"))
            return rc;
        if (const int rc = assertNear (m.conditionNumber, 2.0, 1e-6, "conditionNumber"))
            return rc;
        if (const int rc = assertNear (m.manipulability, 8.0, 1e-6, "manipulability"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Pass, "metric status Pass"))
            return rc;
    }
    // SVD: singular config [4, 0] => infinite condition + Fail.
    {
        Jacobian j(2, 2);
        j (0, 0) = 4.0;
        j (0, 1) = 0.0;
        j (1, 0) = 0.0;
        j (1, 1) = 0.0;
        const rws::SingularMetrics m = rws::calculateSingularMetrics (j, thresholds);
        if (const int rc =
                require (std::isinf (m.conditionNumber), "infinite conditionNumber"))
            return rc;
        if (const int rc = require (m.status == rws::AnalysisStatus::Fail, "metric status Fail"))
            return rc;
        if (const int rc =
                require (!m.warnings.empty (), "warning emitted when singular"))
            return rc;
    }
    return 0;
}

// 子套件 4:sortIkSolutionsForDisplay 的优先级链。
// 准备 4 条解:colliding / worseResidual / betterMargin / lowerDistance;
// 排序后应当是 lowerDistance → betterMargin → worseResidual → colliding。
// 补充说明:排序链 lowerDistance -> betterMargin -> worseResidual -> colliding;
// addUniqueIkCandidate 用阈值合并邻近候选,并只对回转关节做周期归并(revolutionMask),
// 避免把 ±π 的同一姿态当两个解;countUsableIkSolutions 排除 Fail;
// summarizeIkSolutions 统计 pass/warning/fail/usable 计数,供 UI 汇总展示。
static int testIkRanking ()
{
    std::vector< rws::KinematicIkSolution > solutions;

    rws::KinematicIkSolution colliding;
    colliding.inCollision = true;
    colliding.positionErrorMeters = 0.0;
    colliding.orientationErrorDeg = 0.0;
    colliding.minJointLimitMargin = 0.8;
    colliding.manipulability = 10.0;
    colliding.distanceToCurrentQ = 0.1;
    colliding.q = {9.0};

    rws::KinematicIkSolution worseResidual;
    worseResidual.inCollision = false;
    worseResidual.positionErrorMeters = 0.1;
    worseResidual.orientationErrorDeg = 0.0;
    worseResidual.minJointLimitMargin = 0.9;
    worseResidual.manipulability = 20.0;
    worseResidual.distanceToCurrentQ = 0.1;
    worseResidual.q = {2.0};

    rws::KinematicIkSolution betterMargin;
    betterMargin.inCollision = false;
    betterMargin.positionErrorMeters = 0.0;
    betterMargin.orientationErrorDeg = 0.0;
    betterMargin.minJointLimitMargin = 0.7;
    betterMargin.manipulability = 1.0;
    betterMargin.distanceToCurrentQ = 0.5;
    betterMargin.q = {1.0};

    rws::KinematicIkSolution lowerDistance;
    lowerDistance.inCollision = false;
    lowerDistance.positionErrorMeters = 0.0;
    lowerDistance.orientationErrorDeg = 0.0;
    lowerDistance.minJointLimitMargin = 0.7;
    lowerDistance.manipulability = 1.0;
    lowerDistance.distanceToCurrentQ = 0.2;
    lowerDistance.q = {0.0};

    solutions.push_back (colliding);
    solutions.push_back (worseResidual);
    solutions.push_back (betterMargin);
    solutions.push_back (lowerDistance);

    rws::sortIkSolutionsForDisplay (solutions);

    if (const int rc = require (solutions.size () == 4, "IK ranking preserves size"))
        return rc;
    if (const int rc = assertNear (solutions[0].q[0], 0.0, 1e-12, "lower distance first"))
        return rc;
    if (const int rc = assertNear (solutions[1].q[0], 1.0, 1e-12, "same quality next"))
        return rc;
    if (const int rc = assertNear (solutions[2].q[0], 2.0, 1e-12, "worse residual after"))
        return rc;
    if (const int rc = assertNear (solutions[3].q[0], 9.0, 1e-12, "colliding last"))
        return rc;

    std::vector< rw::math::Q > candidates;
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.0), 1e-4);
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.0 + 5e-5), 1e-4);
    rws::addUniqueIkCandidate (candidates, rw::math::Q (2, 0.0, 1.1), 1e-4);
    if (const int rc = require (candidates.size () == 2,
                                "near-duplicate IK candidates are merged"))
        return rc;

    std::vector< rw::math::Q > wrappedCandidates;
    const std::vector< bool > revoluteMask = {true, false};
    rws::addUniqueIkCandidate (
        wrappedCandidates, rw::math::Q (2, -rw::math::Pi, 0.0), 1e-4, revoluteMask);
    rws::addUniqueIkCandidate (
        wrappedCandidates, rw::math::Q (2, rw::math::Pi, 0.0), 1e-4, revoluteMask);
    rws::addUniqueIkCandidate (
        wrappedCandidates, rw::math::Q (2, -rw::math::Pi, 2.0 * rw::math::Pi), 1e-4,
        revoluteMask);
    if (const int rc = require (wrappedCandidates.size () == 2,
                                "IK duplicate detection wraps revolute joints only"))
        return rc;

    std::vector< rws::KinematicIkSolution > validity;
    rws::KinematicIkSolution pass;
    pass.status = rws::AnalysisStatus::Pass;
    validity.push_back (pass);
    rws::KinematicIkSolution warning;
    warning.status = rws::AnalysisStatus::Warning;
    validity.push_back (warning);
    rws::KinematicIkSolution fail;
    fail.status = rws::AnalysisStatus::Fail;
    validity.push_back (fail);
    if (const int rc = require (rws::countUsableIkSolutions (validity) == 2,
                                "usable IK count excludes Fail candidates"))
        return rc;

    // Task 1 Step 1:summarizeIkSolutions 的状态计数。
    const rws::KinematicIkSummary summary = rws::summarizeIkSolutions (validity);
    if (const int rc = require (summary.passCount == 1, "IK summary pass count"))
        return rc;
    if (const int rc = require (summary.warningCount == 1, "IK summary warning count"))
        return rc;
    if (const int rc = require (summary.failCount == 1, "IK summary fail count"))
        return rc;
    if (const int rc = require (summary.usableCount == 2, "IK summary usable count"))
        return rc;
    return 0;
}

// 子套件 IK 当前解:当目标恰好是设备当前 TCP 位姿时,IK 结果中必须包含一条
// 距当前 Q 距离为 0 的 Pass 解(放宽条件数/可操作性阈值以免奇异配置误判)。
// 这保证"当前位置可达自身"这一平凡事实总被识别,UI 不会误报原地不可达。
// 补充说明:平凡事实回归——"当前位姿可达自身"。目标=当前 TCP 位姿时,IK 结果中必须
// 含距离当前 Q 为 0 的 Pass 解;放宽条件数/可操作性阈值是为了避免奇异配置误判,
// 但位置/姿态残差仍用标准容差,确保不是"随便一条解就算通过"。
// 使用 7-DOF KukaIIWA 而非 6 轴,以保证 IK 有解且解集包含当前位姿。
static int testIkIncludesCurrentQForCurrentTcpTarget ()
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeTestKukaIIWA (stateStructure);
    rw::kinematics::State state = stateStructure.getDefaultState ();

    const rw::math::Q currentQ (7, 0.4, -0.7, 0.6, 0.8, -0.5, 0.9, 0.3);
    device->setQ (currentQ, state);
    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> rpy (currentTcp.R ());

    rws::TaskPoint target;
    target.position = {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}};
    target.rpyDeg = {{rpy (0) * 180.0 / rw::math::Pi,
                      rpy (1) * 180.0 / rw::math::Pi,
                      rpy (2) * 180.0 / rw::math::Pi}};

    rws::KinematicThresholds thresholds;
    thresholds.conditionWarning = 1e12;
    thresholds.conditionFail = 1e13;
    thresholds.singularValueWarning = 0.0;
    thresholds.manipulabilityWarning = 0.0;
    rws::KinematicAnalyzer analyzer;
    analyzer.setThresholds (thresholds);

    const rws::KinematicIkAnalysisResult result =
        analyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    bool foundCurrentQ = false;
    for (const rws::KinematicIkSolution& solution : result.solutions) {
        if (solution.distanceToCurrentQ <= 1e-10) {
            foundCurrentQ = solution.status == rws::AnalysisStatus::Pass &&
                            solution.positionErrorMeters <= thresholds.positionToleranceMeters &&
                            solution.orientationErrorDeg <= thresholds.orientationToleranceDeg;
            break;
        }
    }
    if (const int rc = require (foundCurrentQ,
                                "IK includes current Q as a passing solution for current TCP target"))
        return rc;
    return 0;
}

// 子套件 5:calculateReachableRate 的边界:
//   - 2 Pass + 1 Warning + 1 Fail + 1 disabled = 3/4 = 0.75;
//   - 全部 disabled → 0.0(避免除零);
//   - 全部 Pass    → 1.0。
// 子套件 IK 去重阈值:同一当前 TCP 目标下,把 ikDuplicateQThreshold 从默认
// 1e-4 放大到 0.01 后,analyzeIk 返回的解应显著变少——证明该阈值控制着
// 邻近候选的合并强度,是展示层去重的关键旋钮。
// 补充说明:把 ikDuplicateQThreshold 从默认 1e-4 放大到 0.01 后,analyzeIk 返回的解
// 显著变少,证明该阈值控制邻近候选的合并强度,是展示层去重的关键旋钮;
// 若未来实现把去重与求解解耦,此用例会立即失败提醒维护者。
static int testIkDuplicateThresholdControlsCandidateMerging ()
{
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    rw::kinematics::State state = stateStructure.getDefaultState ();
    const rw::math::Q currentQ (6, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    device->setQ (currentQ, state);

    const rw::math::Transform3D<> currentTcp =
        rw::kinematics::Kinematics::frameTframe (
            device->getBase (), device->getEnd (), state);
    const rw::math::RPY<> rpy (currentTcp.R ());
    rws::TaskPoint target;
    target.position = {{currentTcp.P ()[0], currentTcp.P ()[1], currentTcp.P ()[2]}};
    target.rpyDeg = {{rpy (0) * 180.0 / rw::math::Pi,
                      rpy (1) * 180.0 / rw::math::Pi,
                      rpy (2) * 180.0 / rw::math::Pi}};

    rws::KinematicAnalyzer defaultAnalyzer;
    const rws::KinematicIkAnalysisResult defaultResult =
        defaultAnalyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    rws::KinematicThresholds thresholds;
    if (const int rc = assertNear (
            thresholds.ikDuplicateQThreshold, 1e-4, 1e-12, "default IK duplicate threshold"))
        return rc;
    thresholds.ikDuplicateQThreshold = 0.01;
    rws::KinematicAnalyzer mergedAnalyzer;
    mergedAnalyzer.setThresholds (thresholds);
    const rws::KinematicIkAnalysisResult mergedResult =
        mergedAnalyzer.analyzeIk (device, device->getEnd (), state, target, NULL);

    if (const int rc = require (!defaultResult.solutions.empty (),
                                "default duplicate threshold yields IK candidates"))
        return rc;
    if (const int rc = require (mergedResult.solutions.size () < defaultResult.solutions.size (),
                                "larger duplicate threshold merges nearby IK candidates"))
        return rc;
    return 0;
}

// 子套件 任务点可达率:验证 calculateReachableRate 把 Pass/Warning 计入可达、
// 只除以启用(enabled)任务数——2 Pass + 1 Warning + 1 Fail + 1 disabled 得 3/4;
// 全部 disabled 时得 0(避免除零),全部 Pass 得 1。另验证空的批量请求是成功的
// no-op,不合成任何占位结果。
// 补充说明:可达率 = Pass+Warning 数 / enabled 数(disabled 不计入分母);
// 2 Pass + 1 Warning + 1 Fail + 1 disabled 得 3/4;全部 disabled 得 0 避免除零;
// 全部 Pass 得 1;最后验证空批量请求是成功 no-op,不合成占位结果。
static int testTaskPointReachableRate ()
{
    // 2 pass + 1 warning + 1 fail + 1 disabled:
    // reachable count = 2 + 1 = 3; enabled = 4; rate = 3/4 = 0.75.
    auto makeResult =
        [] (const std::string& id, rws::AnalysisStatus status, bool enabled) {
            rws::TaskPointReachabilityResult r;
            r.taskPoint.id      = id;
            r.taskPoint.enabled = enabled;
            r.status            = status;
            return r;
        };
    std::vector< rws::TaskPointReachabilityResult > results;
    results.push_back (makeResult ("P1", rws::AnalysisStatus::Pass, true));
    results.push_back (makeResult ("P2", rws::AnalysisStatus::Pass, true));
    results.push_back (makeResult ("P3", rws::AnalysisStatus::Warning, true));
    results.push_back (makeResult ("P4", rws::AnalysisStatus::Fail, true));
    results.push_back (makeResult ("P5", rws::AnalysisStatus::Unknown, false));

    rws::KinematicAnalyzer analyzer;
    const double rate = analyzer.calculateReachableRate (results);
    if (const int rc = assertNear (rate, 0.75, 1e-12, "reachable rate = 3/4"))
        return rc;

    // All disabled: rate should be 0.0 with no divide-by-zero.
    std::vector< rws::TaskPointReachabilityResult > allDisabled;
    allDisabled.push_back (makeResult ("D1", rws::AnalysisStatus::Pass, false));
    allDisabled.push_back (makeResult ("D2", rws::AnalysisStatus::Warning, false));
    const double rate2 = analyzer.calculateReachableRate (allDisabled);
    if (const int rc = assertNear (rate2, 0.0, 1e-12, "all disabled rate = 0"))
        return rc;

    // All pass: rate should be 1.0.
    std::vector< rws::TaskPointReachabilityResult > allPass;
    allPass.push_back (makeResult ("A1", rws::AnalysisStatus::Pass, true));
    allPass.push_back (makeResult ("A2", rws::AnalysisStatus::Pass, true));
    const double rate3 = analyzer.calculateReachableRate (allPass);
    if (const int rc = assertNear (rate3, 1.0, 1e-12, "all pass rate = 1"))
        return rc;

    // Characterize the batch API boundary: an empty request is a successful
    // no-op and must not synthesize placeholder results.
    rw::kinematics::StateStructure stateStructure;
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
    const rw::kinematics::State state = stateStructure.getDefaultState ();
    const std::vector< rws::TaskPointReachabilityResult > emptyBatch =
        analyzer.analyzeTaskPoints (
            device, device->getEnd (), state, std::vector< rws::TaskPoint > (), NULL);
    if (const int rc = require (emptyBatch.empty (),
                                "empty task point batch returns no results"))
        return rc;

    return 0;
}

// 子套件 6a:KinematicAnalysisWorkspace helper — sanitize config / planned count / summary。
// 补充说明:三个独立小用例——(1) sanitize 把负 sampleCount 收敛为 0、gridSteps 收敛为
// 1、随机种子 0 调整为 1,并逐项记录诊断;(2) Grid 模式下计划采样数受 sampleCount
// 上限约束,理论网格数 4096(=4^6)被截断并记录;(3) summarizeWorkspaceSamples 的
// 计数、平均可操作性(pass/warning 平均)、p10 分位、有限 max 条件数(忽略 inf)
// 与最小裕度。
static int testWorkspaceHelpers ()
{
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -7;
        config.gridStepsPerJoint = 0;
        config.randomSeed = 0;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const rws::WorkspaceSamplingConfig sanitized =
            rws::sanitizeWorkspaceSamplingConfig (config, &diagnostics);

        if (const int rc = require (sanitized.sampleCount == 0,
                                    "workspace sanitize clamps negative sample count"))
            return rc;
        if (const int rc = require (sanitized.gridStepsPerJoint == 1,
                                    "workspace sanitize clamps grid steps"))
            return rc;
        if (const int rc = require (sanitized.randomSeed == 1,
                                    "workspace sanitize adjusts zero seed"))
            return rc;
        if (const int rc = require (diagnostics.sampleCountClamped,
                                    "workspace diagnostics sample count clamped"))
            return rc;
        if (const int rc = require (diagnostics.gridStepsClamped,
                                    "workspace diagnostics grid steps clamped"))
            return rc;
        if (const int rc = require (diagnostics.randomSeedAdjusted,
                                    "workspace diagnostics random seed adjusted"))
            return rc;
    }

    {
        rws::WorkspaceSamplingConfig config;
        config.mode = rws::WorkspaceSamplingMode::Grid;
        config.sampleCount = 100;
        config.gridStepsPerJoint = 4;

        rws::WorkspaceSamplingDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);

        if (const int rc = require (planned == 100,
                                    "workspace grid planning respects sample cap"))
            return rc;
        if (const int rc = require (diagnostics.theoreticalGridSamples == 4096,
                                    "workspace grid theoretical count"))
            return rc;
        if (const int rc = require (diagnostics.gridCountTruncated,
                                    "workspace grid diagnostics truncated"))
            return rc;
    }

    {
        rws::WorkspaceSample pass;
        pass.status = rws::AnalysisStatus::Pass;
        pass.manipulability = 10.0;
        pass.conditionNumber = 20.0;
        pass.minJointLimitMargin = 0.3;

        rws::WorkspaceSample warning;
        warning.status = rws::AnalysisStatus::Warning;
        warning.manipulability = 2.0;
        warning.conditionNumber = 100.0;
        warning.minJointLimitMargin = 0.1;

        rws::WorkspaceSample fail;
        fail.status = rws::AnalysisStatus::Fail;
        fail.inCollision = true;
        fail.manipulability = std::numeric_limits< double >::infinity ();
        fail.conditionNumber = std::numeric_limits< double >::infinity ();
        fail.minJointLimitMargin = 0.0;

        const rws::WorkspaceSummary summary = rws::summarizeWorkspaceSamples (
            std::vector< rws::WorkspaceSample > {pass, warning, fail});

        if (const int rc = require (summary.totalCount == 3, "workspace summary total"))
            return rc;
        if (const int rc = require (summary.passCount == 1, "workspace summary pass"))
            return rc;
        if (const int rc = require (summary.warningCount == 1, "workspace summary warning"))
            return rc;
        if (const int rc = require (summary.failCount == 1, "workspace summary fail"))
            return rc;
        if (const int rc = require (summary.collisionCount == 1,
                                    "workspace summary collision"))
            return rc;
        if (const int rc = assertNear (summary.avgManipulability, 6.0, 1e-12,
                                       "workspace summary avg manipulability"))
            return rc;
        if (const int rc = assertNear (summary.p10Manipulability, 2.0, 1e-12,
                                       "workspace summary p10 manipulability"))
            return rc;
        if (const int rc = assertNear (summary.maxCondition, 100.0, 1e-12,
                                       "workspace summary finite max condition"))
            return rc;
        if (const int rc = assertNear (summary.minJointLimitMargin, 0.0, 1e-12,
                                       "workspace summary min margin"))
            return rc;
    }

    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -1;
        rws::WorkspaceSamplingDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedWorkspaceSampleCount (config, 6, &diagnostics);
        if (const int rc = require (planned == 0,
                                    "workspace planned count handles negative count"))
            return rc;
        if (const int rc = require (diagnostics.sampleCountClamped,
                                    "workspace planned count reports clamped negative count"))
            return rc;
    }

    return 0;
}

// 子套件 7a:PoseReachability 辅助— sanitize / planned count / summary。
// 补充说明:四组用例——(1) sanitize 把负 directionSamples 收敛为 0、rollSamples 收敛
// 为 1;(2) 24 方向 x 3 滚转的计划数 720(10 个位置 => 每位置 72 方向);
// (3) P7 关键边界:诊断用目标数被 MaxPoseReachabilityTargets 封顶(防 UI 卡死),
// 而执行计数 uncapped(真实求解用),1000 方向 x 360 滚转 x 3 位置 = 1080000 不溢出;
// (4) summarizePoseReachabilitySamples 汇总覆盖率/部分进度/计划与完成 IK 数。
static int testPoseReachabilityHelpers ()
{
    {
        rws::PoseReachabilityConfig config;
        config.directionSamples = -5;
        config.rollSamples = 0;
        rws::PoseReachabilityDiagnostics diagnostics;
        const rws::PoseReachabilityConfig sanitized =
            rws::sanitizePoseReachabilityConfig (config, &diagnostics);
        if (const int rc = require (sanitized.directionSamples == 0,
                                    "pose direction samples clamped low"))
            return rc;
        if (const int rc = require (sanitized.rollSamples == 1,
                                    "pose roll samples clamped low"))
            return rc;
        if (const int rc = require (diagnostics.directionSamplesClamped,
                                    "pose direction clamp diagnostic"))
            return rc;
        if (const int rc = require (diagnostics.rollSamplesClamped,
                                    "pose roll clamp diagnostic"))
            return rc;
    }

    {
        rws::PoseReachabilityConfig config;
        config.directionSamples = 24;
        config.rollSamples = 3;
        rws::PoseReachabilityDiagnostics diagnostics;
        const std::size_t planned =
            rws::plannedPoseReachabilityTargetCount (config, 10, &diagnostics);
        if (const int rc = require (planned == 720,
                                    "pose planned target count"))
            return rc;
        if (const int rc = require (diagnostics.plannedDirectionsPerPosition == 72,
                                    "pose planned directions per position"))
            return rc;
    }

    // P7:大配置下诊断 capped 但执行计数 uncapped。
    {
        rws::PoseReachabilityConfig hugeConfig;
        hugeConfig.directionSamples = 1000;
        hugeConfig.rollSamples = 360;

        rws::PoseReachabilityDiagnostics diagnostics;
        const std::size_t diagnosticPlanned =
            rws::plannedPoseReachabilityTargetCount (
                hugeConfig, 3, &diagnostics);
        if (const int rc = require (
                diagnosticPlanned == rws::MaxPoseReachabilityTargets,
                "pose diagnostic target count remains capped"))
            return rc;
        if (const int rc = require (
                diagnostics.targetCountCapped,
                "pose diagnostic target count reports capped"))
            return rc;

        bool overflowed = true;
        const std::size_t executionPlanned =
            rws::poseReachabilityExecutionTargetCount (
                hugeConfig, 3, &overflowed);
        if (const int rc = require (
                executionPlanned == 1080000,
                "pose execution target count is uncapped"))
            return rc;
        if (const int rc = require (
                !overflowed,
                "pose execution target count does not overflow"))
            return rc;
        if (const int rc = require (
                rws::poseReachabilityTargetsPerPosition (hugeConfig) == 360000,
                "pose execution target count per position"))
            return rc;
    }

    {
        rws::PoseReachabilitySample pass;
        pass.status = rws::AnalysisStatus::Pass;
        pass.sampledDirections = 10;
        pass.reachableDirections = 10;
        pass.sampledOrientationSamples = 10;
        pass.reachableOrientationSamples = 10;
        pass.directionCoverage = 1.0;
        pass.orientationCoverage = 1.0;
        pass.coverage = 1.0;
        pass.plannedIkTargets = 10;
        pass.completedIkTargets = 10;
        pass.partial = false;
        rws::PoseReachabilitySample warning;
        warning.status = rws::AnalysisStatus::Warning;
        warning.sampledDirections = 10;
        warning.reachableDirections = 4;
        warning.sampledOrientationSamples = 10;
        warning.reachableOrientationSamples = 4;
        warning.directionCoverage = 0.4;
        warning.orientationCoverage = 0.4;
        warning.coverage = 0.4;
        warning.plannedIkTargets = 10;
        warning.completedIkTargets = 4;
        warning.partial = true;
        const rws::PoseReachabilitySummary summary =
            rws::summarizePoseReachabilitySamples (
                std::vector< rws::PoseReachabilitySample > {pass, warning});
        if (const int rc = require (summary.totalPositions == 2,
                                    "pose summary total positions"))
            return rc;
        if (const int rc = assertNear (summary.averageCoverage, 0.7, 1e-12,
                                       "pose average coverage"))
            return rc;
        if (const int rc = require (summary.partialCount == 1,
                                    "pose summary partial count"))
            return rc;
        if (const int rc = require (summary.plannedIkTargets == 20,
                                    "pose summary planned IK targets"))
            return rc;
        if (const int rc = require (summary.completedIkTargets == 14,
                                    "pose summary completed IK targets"))
            return rc;
    }

    return 0;
}

// 子套件 7:sampleWorkspace 在 NULL / 0 / 负 sampleCount / Grid 模式下的快速返回路径。
// 补充说明:四个快速返回路径(sampleCount=0 / 负 / NULL 设备 / NULL 设备+Grid)都返回空;
// P9 取消回调验证:取消后返回已完成数量的样本,进度回调至少 2 次且最后报告
// completed=取消数、planned=20;Estimated 采样是"确定性、带元数据、FK 证据"
// ——同种子重复采样结果一致、stage=Estimated、保留 seed/count、collisionChecked=false;
// 必需碰撞缺检测器时首样本 DataInsufficient。
static int testWorkspaceSampling ()
{
    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;

    // Zero sample count: return empty regardless of mode.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = 0;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling returns empty for sampleCount=0"))
            return rc;
    }

    // Negative sample count: return empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount = -1;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling returns empty for negative count"))
            return rc;
    }

    // Null device: return empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount            = 10;
        config.mode                    = rws::WorkspaceSamplingMode::RandomUniform;
        config.randomSeed              = 42;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc = require (samples.empty (), "workspace sampling handles null device"))
            return rc;
    }

    // Null device + Grid: also returns empty.
    {
        rws::WorkspaceSamplingConfig config;
        config.sampleCount       = 10;
        config.mode               = rws::WorkspaceSamplingMode::Grid;
        config.gridStepsPerJoint = 0;
        const std::vector< rws::WorkspaceSample > samples =
            analyzer.sampleWorkspace (NULL, NULL, state, config, NULL);
        if (const int rc =
                require (samples.empty (), "workspace sampling handles null device under Grid"))
            return rc;
    }

    // P9:Workspace callback 测试 — cancel + progress。
    {
        rw::kinematics::StateStructure ss;
        const rw::models::SerialDevice::Ptr dev = makeGenericSixAxis (ss);
        rw::kinematics::State devState = ss.getDefaultState ();
        rws::KinematicAnalyzer an;

        struct CallbackState {
            std::size_t progressCalls = 0;
            std::size_t lastCompleted = 0;
            std::size_t lastPlanned = 0;
            std::size_t cancelAfter = 3;
        };
        CallbackState callbackState;

        rws::WorkspaceSamplingConfig cancelConfig;
        cancelConfig.mode = rws::WorkspaceSamplingMode::RandomUniform;
        cancelConfig.sampleCount = 20;
        cancelConfig.randomSeed = 11u;
        cancelConfig.checkCollision = false;

        rws::WorkspaceSamplingRunCallbacks callbacks;
        callbacks.userData = &callbackState;
        callbacks.isCancellationRequested = [] (void* userData) -> bool {
            const CallbackState* s =
                static_cast< const CallbackState* > (userData);
            return s != NULL && s->lastCompleted >= s->cancelAfter;
        };
        callbacks.onProgress = [] (std::size_t completed,
                                   std::size_t planned,
                                   void* userData) {
            CallbackState* s = static_cast< CallbackState* > (userData);
            if (s == NULL) return;
            ++s->progressCalls;
            s->lastCompleted = completed;
            s->lastPlanned = planned;
        };

        const std::vector< rws::WorkspaceSample > canceledSamples =
            an.sampleWorkspace (
                dev, dev->getEnd (), devState, cancelConfig, NULL, callbacks);
        if (canceledSamples.size () != callbackState.cancelAfter)
            return fail ("workspace sampling should stop after requested cancellation");
        if (callbackState.progressCalls < 2)
            return fail ("workspace sampling should emit initial and sample progress");
        if (callbackState.lastPlanned != 20)
            return fail ("workspace progress should report planned random sample count");
        if (callbackState.lastCompleted != callbackState.cancelAfter)
            return fail ("workspace progress should report the completed canceled count");
    }

    // Estimated workspace samples are deterministic metadata-bearing FK evidence;
    // they do not claim verified IK or orientation coverage.
    {
        rw::kinematics::StateStructure ss;
        const rw::models::SerialDevice::Ptr dev = makeGenericSixAxis (ss);
        const rw::kinematics::State devState = ss.getDefaultState ();
        rws::WorkspaceSamplingConfig estimatedConfig;
        estimatedConfig.sampleCount = 4;
        estimatedConfig.randomSeed = 23u;
        estimatedConfig.checkCollision = false;
        const std::vector< rws::WorkspaceSample > first =
            analyzer.sampleWorkspace (dev, dev->getEnd (), devState, estimatedConfig, NULL);
        const std::vector< rws::WorkspaceSample > second =
            analyzer.sampleWorkspace (dev, dev->getEnd (), devState, estimatedConfig, NULL);
        if (const int rc = require (first.size () == 4 && second.size () == first.size (),
                                    "estimated workspace sample count"))
            return rc;
        for (std::size_t i = 0; i < first.size (); ++i) {
            if (const int rc = require (first[i].q == second[i].q,
                                        "estimated workspace sampling is reproducible"))
                return rc;
            if (const int rc = require (first[i].stage == rws::AnalysisEvidenceStage::Estimated,
                                        "workspace sample evidence stage is Estimated"))
                return rc;
            if (const int rc = require (first[i].sampleSeed == estimatedConfig.randomSeed &&
                                            first[i].sampleCount == estimatedConfig.sampleCount,
                                        "workspace sample preserves seed and count"))
                return rc;
            if (const int rc = require (!first[i].collisionChecked,
                                        "workspace sample records collision check disabled"))
                return rc;
        }

        estimatedConfig.checkCollision = true;
        const std::vector< rws::WorkspaceSample > missingDetector =
            analyzer.sampleWorkspace (dev, dev->getEnd (), devState, estimatedConfig, NULL);
        if (const int rc = require (!missingDetector.empty () &&
                                        missingDetector.front ().feasibility ==
                                            rws::Feasibility::DataInsufficient,
                                    "required collision evidence is DataInsufficient"))
            return rc;
    }

    return 0;
}

// Task 9 / S13:方向采样与滚转采样必须分别表达工具 Z 方向和绕工具 Z 的姿态自由度。
// 补充说明:方向采样与滚转采样分别表达"工具 Z 方向"与"绕工具 Z 的姿态自由度";
// 4 方向 x 2 滚转 = 8 个目标,滚转样本必须保持工具 Z 方向不变而让 X/Y 轴反向旋转
// (dot < -0.999);isDirectionTargetReachable 只看工具 Z(滚转残差不破坏方向可达),
// isOrientationTargetReachable 则要求完整姿态(滚转残差 90 度不可达);
// 位置残差超容差或工具轴残差(5 度)都会拒绝方向可达。
static int testOrientationCoverageSampling ()
{
    rws::PoseReachabilityConfig config;
    config.directionSamples = 4;
    config.rollSamples = 2;
    const std::vector< rws::OrientationTargetSample > samples =
        rws::generateOrientationTargetSamples (config);
    if (const int rc = require (samples.size () == 8,
                                "orientation sampling creates direction x roll targets"))
        return rc;

    for (int directionIndex = 0; directionIndex < config.directionSamples;
         ++directionIndex) {
        const rws::OrientationTargetSample& first =
            samples[static_cast< std::size_t > (directionIndex * config.rollSamples)];
        const rws::OrientationTargetSample& second =
            samples[static_cast< std::size_t > (directionIndex * config.rollSamples + 1)];
        if (const int rc = require (first.directionIndex == directionIndex &&
                                        second.directionIndex == directionIndex &&
                                        first.rollIndex == 0 && second.rollIndex == 1,
                                    "orientation sampling preserves direction and roll indices"))
            return rc;
        if (const int rc = require (
                nearlyEqual (first.rotation.getCol (2)[0], second.rotation.getCol (2)[0]) &&
                    nearlyEqual (first.rotation.getCol (2)[1], second.rotation.getCol (2)[1]) &&
                    nearlyEqual (first.rotation.getCol (2)[2], second.rotation.getCol (2)[2]),
                "roll samples preserve the tool Z direction"))
            return rc;
        if (const int rc = require (
                dot (first.rotation.getCol (0), second.rotation.getCol (0)) < -0.999 &&
                    dot (first.rotation.getCol (1), second.rotation.getCol (1)) < -0.999,
                "roll samples rotate the tool X and Y axes"))
            return rc;
    }

    config.directionSamples = 0;
    if (const int rc = require (rws::generateOrientationTargetSamples (config).empty (),
                                "zero direction samples produce no orientation targets"))
        return rc;

    rws::TargetEvaluation rollOnlyEvaluation;
    rws::TargetCandidate rollOnlyCandidate;
    rollOnlyCandidate.configuration.feasibility = rws::Feasibility::Feasible;
    rollOnlyCandidate.configuration.tcpPose = rw::math::Transform3D<> (
        rw::math::Vector3D<> (0.0, 0.0, 0.0),
        rw::math::EAA<> (rw::math::Vector3D<>::z (), 0.5 * rw::math::Pi));
    rollOnlyCandidate.positionErrorMeters = 0.0;
    rollOnlyCandidate.orientationErrorDeg = 90.0;
    rollOnlyEvaluation.candidates.push_back (rollOnlyCandidate);
    const rw::math::Rotation3D<> identity =
        rw::math::RPY<> (0.0, 0.0, 0.0).toRotation3D ();
    if (const int rc = require (
            rws::isDirectionTargetReachable (
                rollOnlyEvaluation, identity, 0.001, 1.0),
            "roll-only residual preserves direction reachability"))
        return rc;
    if (const int rc = require (
            !rws::isOrientationTargetReachable (
                rollOnlyEvaluation, 0.001, 1.0),
            "roll-only residual fails full orientation reachability"))
        return rc;

    rollOnlyEvaluation.candidates.front ().positionErrorMeters = 0.002;
    if (const int rc = require (
            !rws::isDirectionTargetReachable (
                rollOnlyEvaluation, identity, 0.001, 1.0),
            "position residual rejects direction reachability"))
        return rc;
    rollOnlyEvaluation.candidates.front ().positionErrorMeters = 0.0;
    rollOnlyEvaluation.candidates.front ().configuration.tcpPose =
        rw::math::Transform3D<> (
            rw::math::Vector3D<> (0.0, 0.0, 0.0),
            rw::math::EAA<> (rw::math::Vector3D<>::x (), 5.0 * rw::math::Deg2Rad));
    if (const int rc = require (
            !rws::isDirectionTargetReachable (
                rollOnlyEvaluation, identity, 0.001, 1.0),
            "tool-axis residual rejects direction reachability"))
        return rc;
    return 0;
}

// 子套件 7:analyzePoseReachability 在 NULL device / directionSamples=0 时的兜底。
// 补充说明:先验证默认配置(24 方向 / 1 滚转 / 碰撞检查开);NULL 设备下按方向数采样、
// 可达数全 0、coverage 0、status Fail;directionSamples=0 时样本数为 0;
// splitCoverage 验证 direction 与 orientation 计数分离(4 方向 x 2 滚转 = 8 姿态),
// 并保留 legacy coverage == orientationCoverage 的映射;
// P4 负 roll 被 sanitize 为 1;P5 预取消在 position 边界生效、单 position 内取消在
// IK target 循环内生效并标记 partial;进度回调每个 IK target 一次;
// P10 可达配置保存代表 Q、方向/滚转索引;P7 多位置 2 位置 x 2 方向 x 2 滚转 = 8 个
// IK target 的总进度。
static int testPoseReachability ()
{
    rws::PoseReachabilityConfig config;
    if (const int rc = require (config.directionSamples == 24, "default direction samples"))
        return rc;
    if (const int rc = require (config.rollSamples == 1, "default roll samples"))
        return rc;
    if (const int rc = require (config.checkCollision, "default pose collision check"))
        return rc;

    rws::KinematicAnalyzer analyzer;
    rw::kinematics::State state;

    std::vector< std::array< double, 3 > > positions;
    positions.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});

    const std::vector< rws::PoseReachabilitySample > noDevice =
        analyzer.analyzePoseReachability (NULL, NULL, state, positions, config, NULL);
    if (const int rc = require (noDevice.size () == 1, "no-device pose sample count"))
        return rc;
    if (const int rc = require (noDevice.front ().sampledDirections == 24,
                                "no-device sampled directions"))
        return rc;
    if (const int rc = require (noDevice.front ().sampledOrientationSamples == 24,
                                "no-device sampled orientations"))
        return rc;
    if (const int rc = require (noDevice.front ().reachableDirections == 0,
                                "no-device reachable directions"))
        return rc;
    if (const int rc = require (noDevice.front ().reachableOrientationSamples == 0,
                                "no-device reachable orientations"))
        return rc;
    if (const int rc = assertNear (noDevice.front ().directionCoverage, 0.0, 1e-12,
                                   "no-device direction coverage"))
        return rc;
    if (const int rc = assertNear (noDevice.front ().orientationCoverage, 0.0, 1e-12,
                                   "no-device orientation coverage"))
        return rc;
    if (const int rc = assertNear (noDevice.front ().coverage, 0.0, 1e-12,
                                   "no-device coverage"))
        return rc;
    if (const int rc = require (noDevice.front ().status == rws::AnalysisStatus::Fail,
                                "no-device status"))
        return rc;

    rws::PoseReachabilityConfig zero;
    zero.directionSamples = 0;
    zero.rollSamples      = 2;
    const std::vector< rws::PoseReachabilitySample > zeroResult =
        analyzer.analyzePoseReachability (NULL, NULL, state, positions, zero, NULL);
    if (const int rc = require (zeroResult.size () == 1, "zero pose sample count"))
        return rc;
    if (const int rc = require (zeroResult.front ().sampledDirections == 0,
                                "zero sampled directions"))
        return rc;
    if (const int rc = require (zeroResult.front ().sampledOrientationSamples == 0,
                                "zero sampled orientations"))
        return rc;
    if (const int rc = assertNear (zeroResult.front ().coverage, 0.0, 1e-12,
                                   "zero coverage"))
        return rc;

    rws::PoseReachabilityConfig splitCoverage;
    splitCoverage.directionSamples = 4;
    splitCoverage.rollSamples = 2;
    const std::vector< rws::PoseReachabilitySample > splitResult =
        analyzer.analyzePoseReachability (
            NULL, NULL, state, positions, splitCoverage, NULL);
    if (const int rc = require (
            splitResult.front ().sampledDirections == 4 &&
                splitResult.front ().sampledOrientationSamples == 8,
            "pose result separates direction and orientation sample counts"))
        return rc;
    if (const int rc = assertNear (
            splitResult.front ().coverage,
            splitResult.front ().orientationCoverage,
            1e-12, "legacy coverage maps to orientation coverage"))
        return rc;

    // P4:负 rollSamples 应被 sanitize 为 1,使 sampledDirections = directionSamples × 1。
    {
        rws::PoseReachabilityConfig negativeRoll;
        negativeRoll.directionSamples = 4;
        negativeRoll.rollSamples = -9;
        const std::vector< rws::PoseReachabilitySample > negativeRollResult =
            analyzer.analyzePoseReachability (NULL, NULL, state, positions, negativeRoll, NULL);
        if (const int rc = require (negativeRollResult.front ().sampledDirections == 4,
                                    "negative roll is sanitized to one roll"))
            return rc;
    }

    // P5:取消测试:预取消的 alwaysCancel 回调应产生 1 个 position 的 Fail。
    {
        rws::PoseReachabilityRunCallbacks cancelCb;
        cancelCb.isCancellationRequested = [] (void*) -> bool { return true; };
        const std::vector< rws::PoseReachabilitySample > canceled =
            analyzer.analyzePoseReachability (
                NULL, NULL, state, positions, config, NULL, cancelCb);
        if (const int rc = require (canceled.size () == 1,
                                    "canceled pose result preserves current position"))
            return rc;
    }

    // P5:取消必须在单个 position 内的 IK target 之间生效,而不是只在 position 边界检查。
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
        rw::kinematics::State deviceState = stateStructure.getDefaultState ();

        rws::PoseReachabilityConfig cancelInsidePosition;
        cancelInsidePosition.directionSamples = 2;
        cancelInsidePosition.rollSamples = 2;
        cancelInsidePosition.checkCollision = false;

        struct CancelAfterFirstCheck {
            int checks = 0;
        } cancelState;
        rws::PoseReachabilityRunCallbacks cancelCb;
        cancelCb.userData = &cancelState;
        cancelCb.isCancellationRequested = [] (void* userData) -> bool {
            CancelAfterFirstCheck* state =
                static_cast< CancelAfterFirstCheck* > (userData);
            ++state->checks;
            return state->checks >= 2;
        };

        const std::vector< rws::PoseReachabilitySample > canceled =
            analyzer.analyzePoseReachability (
                device, device->getEnd (), deviceState, positions,
                cancelInsidePosition, NULL, cancelCb);
        if (const int rc = require (cancelState.checks >= 2,
                                    "pose cancellation checked inside IK target loop"))
            return rc;
        if (const int rc = require (canceled.size () == 1,
                                    "inner-loop canceled pose result count"))
            return rc;
        if (const int rc = require (canceled.front ().sampledDirections == 2,
                                    "inner-loop canceled sampled directions"))
            return rc;
        if (const int rc = require (canceled.front ().sampledOrientationSamples == 4,
                                    "inner-loop canceled sampled orientations"))
            return rc;
        if (const int rc = require (canceled.front ().plannedIkTargets == 4,
                                    "inner-loop canceled planned IK targets"))
            return rc;
        if (const int rc = require (canceled.front ().completedIkTargets < 4,
                                    "inner-loop canceled completed IK targets"))
            return rc;
        if (const int rc = require (canceled.front ().partial,
                                    "inner-loop canceled sample marked partial"))
            return rc;
    }

    // P5:进度回调测试:每个 IK target 完成后回调一次,最后完成数 = 计划数。
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
        rw::kinematics::State deviceState = stateStructure.getDefaultState ();

        rws::PoseReachabilityConfig progressConfig;
        progressConfig.directionSamples = 2;
        progressConfig.rollSamples = 2;
        progressConfig.checkCollision = false;

        struct ProgressState {
            std::size_t calls = 0;
            std::size_t lastCompleted = 0;
            std::size_t lastPlanned = 0;
        } progressState;

        rws::PoseReachabilityRunCallbacks progressCb;
        progressCb.userData = &progressState;
        progressCb.onProgress = [] (std::size_t completed,
                                    std::size_t planned,
                                    void* userData) {
            ProgressState* state = static_cast< ProgressState* > (userData);
            ++state->calls;
            state->lastCompleted = completed;
            state->lastPlanned = planned;
        };

        const std::vector< rws::PoseReachabilitySample > progressResult =
            analyzer.analyzePoseReachability (
                device, device->getEnd (), deviceState, positions,
                progressConfig, NULL, progressCb);

        if (const int rc = require (progressState.calls == 4,
                                    "pose progress callback per IK target"))
            return rc;
        if (const int rc = require (progressState.lastCompleted == 4,
                                    "pose progress last completed target"))
            return rc;
        if (const int rc = require (progressState.lastPlanned == 4,
                                    "pose progress planned target count"))
            return rc;
        if (const int rc = require (progressResult.front ().plannedIkTargets == 4,
                                    "pose sample planned IK targets"))
            return rc;
        if (const int rc = require (progressResult.front ().completedIkTargets == 4,
                                    "pose sample completed IK targets"))
            return rc;
        if (const int rc = require (!progressResult.front ().partial,
                                    "pose complete sample is not partial"))
            return rc;
    }

    // P10:代表性 Q 保存测试:一个可达配置应保存代表性 Q。
    {
        rw::kinematics::StateStructure ss;
        const rw::models::SerialDevice::Ptr dev =
            makeGenericSixAxis (ss);
        rw::kinematics::State reachableState = ss.getDefaultState ();
        const rw::math::Q targetQ (6, 0.2, -0.3, 0.25, 0.1, -0.2, 0.15);
        dev->setQ (targetQ, reachableState);
        const rw::math::Transform3D<> tcp =
            rw::kinematics::Kinematics::frameTframe (
                dev->getBase (), dev->getEnd (), reachableState);

        std::vector< std::array< double, 3 > > reachablePositions;
        reachablePositions.push_back (
            std::array< double, 3 > {{tcp.P ()[0], tcp.P ()[1], tcp.P ()[2]}});

        rws::PoseReachabilityConfig repConfig;
        repConfig.directionSamples = 12;
        repConfig.rollSamples = 2;
        repConfig.checkCollision = false;

        rws::KinematicThresholds repThresholds;
        repThresholds.conditionWarning = 1e12;
        repThresholds.conditionFail = 1e13;
        repThresholds.singularValueWarning = 0.0;
        repThresholds.manipulabilityWarning = 0.0;
        rws::KinematicAnalyzer repAnalyzer;
        repAnalyzer.setThresholds (repThresholds);

        const std::vector< rws::PoseReachabilitySample > repSamples =
            repAnalyzer.analyzePoseReachability (
                dev, dev->getEnd (), reachableState, reachablePositions,
                repConfig, NULL);
        if (const int rc = require (repSamples.size () == 1,
                                    "representative pose sample count"))
            return rc;
        if (const int rc = require (
                repSamples[0].reachableDirections > 0,
                "representative pose has at least one reachable direction"))
            return rc;
        if (const int rc = require (
                repSamples[0].hasRepresentativeQ,
                "reachable pose stores representative Q"))
            return rc;
        if (const int rc = require (
                repSamples[0].representativeQ.size () == dev->getDOF (),
                "representative Q dimension matches device"))
            return rc;
        if (const int rc = require (
                repSamples[0].representativeDirectionIndex >= 0,
                "representative direction index stored"))
            return rc;
        if (const int rc = require (
                repSamples[0].representativeRollIndex >= 0,
                "representative roll index stored"))
            return rc;
    }

    // P7:多位置进度回调:2 positions × 2 dirs × 2 rolls = 8 IK targets。
    {
        rw::kinematics::StateStructure stateStructure;
        const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (stateStructure);
        rw::kinematics::State deviceState = stateStructure.getDefaultState ();

        std::vector< std::array< double, 3 > > twoPositions;
        twoPositions.push_back (std::array< double, 3 > {{1.0, 2.0, 3.0}});
        twoPositions.push_back (std::array< double, 3 > {{1.1, 2.0, 3.0}});

        rws::PoseReachabilityConfig progressConfig;
        progressConfig.directionSamples = 2;
        progressConfig.rollSamples = 2;
        progressConfig.checkCollision = false;

        struct MultiProgressState {
            std::size_t lastCompleted = 0;
            std::size_t lastPlanned = 0;
        } progressState;

        rws::PoseReachabilityRunCallbacks progressCb;
        progressCb.userData = &progressState;
        progressCb.onProgress = [] (std::size_t completed,
                                    std::size_t planned,
                                    void* userData) {
            MultiProgressState* state =
                static_cast< MultiProgressState* > (userData);
            state->lastCompleted = completed;
            state->lastPlanned = planned;
        };

        const std::vector< rws::PoseReachabilitySample > result =
            analyzer.analyzePoseReachability (
                device, device->getEnd (), deviceState, twoPositions,
                progressConfig, NULL, progressCb);

        if (const int rc = require (result.size () == 2,
                                    "pose progress multi-position result count"))
            return rc;
        if (const int rc = require (progressState.lastCompleted == 8,
                                    "pose progress multi-position completed count"))
            return rc;
        if (const int rc = require (progressState.lastPlanned == 8,
                                    "pose progress multi-position planned count"))
            return rc;
    }

    return 0;
}

// 子套件 8:buildAggregateResult 综合:
//   - 包含 1 个 Fail 任务点时,总 status 是 Fail;
//   - reachableRate = 0.5(1 Pass + 1 Fail);
//   - manipulabilityMap 至少有 min/max/mean。
// 补充说明:buildAggregateResult 把当前位姿/任务点/工作区/位姿可达四类结果合成总览;
// 任一 Fail 任务点 => 总 status Fail、feasibility Infeasible、quality Critical;
// reachableRate 0.5(1 Pass + 1 Fail);manipulabilityMap 至少含 min/max/mean。
static int testAggregateResult ()
{
    rws::KinematicCurrentPoseResult current;
    current.status = rws::AnalysisStatus::Pass;
    current.manipulability = 3.0;
    current.conditionNumber = 4.0;

    rws::TaskPointReachabilityResult pass;
    pass.taskPoint.enabled = true;
    pass.status = rws::AnalysisStatus::Pass;
    rws::TaskPointReachabilityResult fail;
    fail.taskPoint.enabled = true;
    fail.status = rws::AnalysisStatus::Fail;

    rws::WorkspaceSample ws1;
    ws1.status = rws::AnalysisStatus::Pass;
    ws1.manipulability = 2.0;
    rws::WorkspaceSample ws2;
    ws2.status = rws::AnalysisStatus::Warning;
    ws2.manipulability = 4.0;

    rws::PoseReachabilitySample pose;
    pose.sampledDirections = 8;
    pose.reachableDirections = 6;
    pose.coverage = 0.75;
    pose.status = rws::AnalysisStatus::Warning;

    rws::KinematicAnalyzer analyzer;
    const rws::KinematicAnalysisResult result = analyzer.buildAggregateResult (
        current,
        std::vector< rws::TaskPointReachabilityResult > {pass, fail},
        std::vector< rws::WorkspaceSample > {ws1, ws2},
        std::vector< rws::PoseReachabilitySample > {pose});

    if (const int rc = require (result.header.pluginName == "KinematicAnalysis",
                                "aggregate plugin name"))
        return rc;
    if (const int rc = require (result.status == rws::AnalysisStatus::Fail,
                                "aggregate worst status"))
        return rc;
    if (const int rc = require (result.feasibility == rws::Feasibility::Infeasible &&
                                    result.quality == rws::Quality::Critical,
                                "aggregate maps legacy status to feasibility and quality"))
        return rc;
    if (const int rc = assertNear (result.reachableRate, 0.5, 1e-12,
                                   "aggregate reachable rate"))
        return rc;
    if (const int rc = require (result.workspaceSamples.size () == 2,
                                "aggregate workspace sample count"))
        return rc;
    if (const int rc = require (result.poseReachability.size () == 1,
                                "aggregate pose sample count"))
        return rc;
    if (const int rc = require (result.manipulabilityMap.size () >= 3,
                                "aggregate manipulability metrics"))
        return rc;
    return 0;
}

// ============================================================================
//  TaskPointResolver 单元测试
//  使用 makeGenericSixAxis 构造 device + StateStructure,
//  再 addFrame 到 StateStructure 上,然后用 WorkCell 包装,验证
//  refFrame / tcpFrame 在不同 frame 下的解析行为。
// ============================================================================
// 补充场景说明:
//   - FixtureA 挂在设备 base 下(偏移 0.5,0,0),ToolTip 挂在设备 end 下,
//     ForeignTcp 挂在场景根下(不在所选设备运动链上);
//   - 归属校验是核心:全局可解析但不在所选设备运动链上的 Frame 不能被当作本设备
//     TCP(稳定告警 KIN_TASK_TCP_WRONG_DEVICE),防止跨设备误用 TCP;
//   - 数值验证:base 参考系下数值原样保留;named frame 参考系下数值变换到 base;
//     空 tcpFrame 回退默认 TCP;空 tcpFrame + 空默认 TCP 报 NoTcpFrame;
//   - 未知参考系/TCP 分别返回 InvalidTarget/NoTcpFrame 并带对应告警码。
static int testTaskPointResolver ()
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);

    // 多挂几个 named frame 供 resolver 查找:
    const Frame::Ptr fixtureA =
        rw::core::ownedPtr (new FixedFrame (
            "FixtureA", Transform3D<> (Vector3D<> (0.5, 0.0, 0.0))));
    const Frame::Ptr toolTip =
        rw::core::ownedPtr (new FixedFrame (
            "ToolTip", Transform3D<> (Vector3D<> (0.0, 0.0, 0.05))));
    // 额外加入一个挂在场景根(而非所选设备)下的"外来 TCP"，用于验证全局可解析但
    // 不属于所选设备的 Frame 不能被当作本设备 TCP 接受。
    const Frame::Ptr foreignTcp =
        rw::core::ownedPtr (new FixedFrame (
            "ForeignTcp", Transform3D<> (Vector3D<> (0.0, 0.0, 0.1))));
    stateStructure->addFrame (fixtureA, device->getBase ());
    stateStructure->addFrame (toolTip, device->getEnd ());
    stateStructure->addFrame (foreignTcp, stateStructure->getRoot ());

    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (
            stateStructure, "TestWorkCell", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    // 1) WORLD refFrame:valid + 目标在 base 下
    {
        rws::TaskPoint p;
        p.id = "P1";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.5, 0.0, 0.3}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "WORLD refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.tcpFrame == device->getEnd (),
                                    "WORLD uses row-level TCP"))
            return rc;
        if (const int rc = require (r.targetInDeviceBase.refFrame == device->getBase ()->getName (),
                                    "WORLD output refFrame = device base name"))
            return rc;
        if (const int rc = require (r.warnings.empty (), "WORLD success has no warnings"))
            return rc;
    }

    // 归属校验用例：全局可解析但不在所选设备运动链上的 Frame，不能被当作本设备
    // TCP 接受。断言其解析失败，并给出稳定的 KIN_TASK_TCP_WRONG_DEVICE 告警码。
    {
        rws::TaskPoint p;
        p.id = "ForeignTcp";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "ForeignTcp";
        p.position = {{0.0, 0.0, 0.3}};
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (!r.valid, "TCP outside selected device is rejected"))
            return rc;
        if (const int rc = require (!r.warnings.empty () &&
                                    r.warnings.front ().code == "KIN_TASK_TCP_WRONG_DEVICE",
                                    "wrong-device TCP reports a stable diagnostic"))
            return rc;
    }

    // 2) device base refFrame:valid + refFrame 重写为 base
    {
        rws::TaskPoint p;
        p.id = "P2";
        p.refFrame = device->getBase ()->getName ();
        p.tcpFrame = "TCP";
        p.position = {{1.0, 2.0, 3.0}};
        p.rpyDeg   = {{10.0, 20.0, 30.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "device base refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.targetInDeviceBase.refFrame == device->getBase ()->getName (),
                                    "device base output refFrame stays base name"))
            return rc;
        // 已声明在 base 下,数值应当原样保留(无变换)。
        if (const int rc = assertNear (r.targetInDeviceBase.position[0], 1.0, 1e-9,
                                       "base refFrame preserves x"))
            return rc;
        if (const int rc = assertNear (r.targetInDeviceBase.position[1], 2.0, 1e-9,
                                       "base refFrame preserves y"))
            return rc;
    }

    // 3) named frame refFrame:valid + 数值被变换到 base
    {
        rws::TaskPoint p;
        p.id = "P3";
        p.refFrame = "FixtureA";   // 在 (0.5, 0, 0) 偏移
        p.tcpFrame = "ToolTip";
        p.position = {{0.0, 0.0, 0.2}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "named frame refFrame resolves to valid"))
            return rc;
        if (const int rc = require (r.tcpFrame != nullptr && r.tcpFrame->getName () == "ToolTip",
                                    "named frame uses row-level TCP"))
            return rc;
        // FixtureA 在 base 的 (0.5, 0, 0) 偏移,ref 下 (0, 0, 0.2) 应转到 base 下 (0.5, 0, 0.2)。
        if (const int rc = assertNear (r.targetInDeviceBase.position[0], 0.5, 1e-9,
                                       "named frame transform x"))
            return rc;
        if (const int rc = assertNear (r.targetInDeviceBase.position[2], 0.2, 1e-9,
                                       "named frame transform z"))
            return rc;
    }

    // 4) unknown refFrame:invalid + KIN_TASK_REF_NOT_FOUND warning
    {
        rws::TaskPoint p;
        p.id = "P4";
        p.refFrame = "MissingFrame";
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (!r.valid, "unknown refFrame is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::InvalidTarget,
                                    "unknown refFrame -> InvalidTarget"))
            return rc;
        bool found = false;
        for (const rws::AnalysisWarning& w : r.warnings) {
            if (w.code == "KIN_TASK_REF_NOT_FOUND") {
                found = true;
                break;
            }
        }
        if (const int rc = require (found, "KIN_TASK_REF_NOT_FOUND warning emitted"))
            return rc;
    }

    // 5) unknown tcpFrame:invalid + KIN_TASK_TCP_NOT_FOUND warning
    {
        rws::TaskPoint p;
        p.id = "P5";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "MissingTCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (!r.valid, "unknown tcpFrame is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::NoTcpFrame,
                                    "unknown tcpFrame -> NoTcpFrame"))
            return rc;
        bool found = false;
        for (const rws::AnalysisWarning& w : r.warnings) {
            if (w.code == "KIN_TASK_TCP_NOT_FOUND") {
                found = true;
                break;
            }
        }
        if (const int rc = require (found, "KIN_TASK_TCP_NOT_FOUND warning emitted"))
            return rc;
    }

    // 6) 空 tcpFrame:fallback 到 default TCP
    {
        rws::TaskPoint p;
        p.id = "P6";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame.clear ();        // 空 → 用 default
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, device->getEnd (), state, p);
        if (const int rc = require (r.valid, "empty tcpFrame falls back to default"))
            return rc;
        if (const int rc = require (r.tcpFrame == device->getEnd (),
                                    "empty tcpFrame uses default TCP"))
            return rc;
    }

    // 7) 空 tcpFrame + 空 default TCP:invalid + NoTcpFrame
    {
        rws::TaskPoint p;
        p.id = "P7";
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame.clear ();
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        rws::ResolvedTaskPoint r = rws::resolveTaskPoint (
            workcell.get (), device, nullptr, state, p);
        if (const int rc = require (!r.valid, "empty tcpFrame + null default is invalid"))
            return rc;
        if (const int rc = require (r.failure == rws::KinematicFailureReason::NoTcpFrame,
                                    "no TCP at all -> NoTcpFrame"))
            return rc;
    }

    return 0;
}

// ============================================================================
//  P1:workcell-aware analyzeTaskPoint 行为
//    - disabled 不跑 resolver,不计 reachable;
//    - unknown refFrame / tcpFrame → resolver invalid → Fail;
//    - WORLD / device base 成功路径 → 调用旧 analyzeIk。
// ============================================================================
// 补充说明:workcell-aware analyzeTaskPoint 把 resolver(参考系/TCP 解析)接到
// analyzeIk 之前;disabled 任务返回 Warning + KIN_TASK_DISABLED 且不产生解;
// 未知参考系/TCP 直接 Fail 且 primaryFailure 是 InvalidTarget/NoTcpFrame;
// WORLD 成功路径穿透到 analyzeIk;批量可达率统计中 disabled 不计入分母。
static int testWorkcellAwareAnalyzeTaskPoint ()
{
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (stateStructure, "TestWC", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    rws::KinematicAnalyzer analyzer;
    analyzer.setThresholds (rws::KinematicThresholds ());

    // 1) disabled → status Warning, status 文字通过 KIN_TASK_DISABLED 警告体现,
    //    且 r.ik.solutions 为空(不影响 reachable rate)。
    {
        rws::TaskPoint p;
        p.id = "P_disabled";
        p.enabled = false;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Warning,
                                    "disabled -> Warning"))
            return rc;
        if (const int rc = require (r.ik.solutions.empty (), "disabled -> no solutions"))
            return rc;
        bool saw = false;
        for (const rws::AnalysisWarning& w : r.ik.warnings) {
            if (w.code == "KIN_TASK_DISABLED") { saw = true; break; }
        }
        if (const int rc = require (saw, "disabled -> KIN_TASK_DISABLED warning"))
            return rc;
    }

    // 2) unknown refFrame → resolver InvalidTarget → Fail + warning。
    {
        rws::TaskPoint p;
        p.id = "P_missing_ref";
        p.enabled = true;
        p.refFrame = "NoSuchFrame";
        p.tcpFrame = "TCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Fail,
                                    "unknown refFrame -> Fail"))
            return rc;
        if (const int rc = require (r.primaryFailure == rws::KinematicFailureReason::InvalidTarget,
                                    "unknown refFrame -> InvalidTarget"))
            return rc;
        bool saw = false;
        for (const rws::AnalysisWarning& w : r.ik.warnings) {
            if (w.code == "KIN_TASK_REF_NOT_FOUND") { saw = true; break; }
        }
        if (const int rc = require (saw, "unknown refFrame -> KIN_TASK_REF_NOT_FOUND"))
            return rc;
    }

    // 3) unknown tcpFrame → NoTcpFrame → Fail。
    {
        rws::TaskPoint p;
        p.id = "P_missing_tcp";
        p.enabled = true;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "NoSuchTCP";
        p.position = {{0.0, 0.0, 0.0}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.001;
        p.tolerance.orientationDeg = 1.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        if (const int rc = require (r.status == rws::AnalysisStatus::Fail,
                                    "unknown tcpFrame -> Fail"))
            return rc;
        if (const int rc = require (r.primaryFailure == rws::KinematicFailureReason::NoTcpFrame,
                                    "unknown tcpFrame -> NoTcpFrame"))
            return rc;
    }

    // 4) WORLD + 已知 TCP → 走 analyzeIk,rawCandidateCount > 0 或 NoSolution(可能)。
    {
        rws::TaskPoint p;
        p.id = "P_world";
        p.enabled = true;
        p.refFrame = rws::kTaskWorldFrameName;
        p.tcpFrame = "TCP";
        p.position = {{0.3, 0.0, 0.4}};
        p.rpyDeg   = {{0.0, 0.0, 0.0}};
        p.tolerance.positionMeters = 0.01;
        p.tolerance.orientationDeg = 5.0;
        const auto r = analyzer.analyzeTaskPoint (workcell.get (), device,
                                                  device->getEnd (), state, p, NULL);
        // r.status 至少应不是 Unknown,可能 Pass / Warning / Fail。
        if (const int rc = require (r.status != rws::AnalysisStatus::Unknown,
                                    "WORLD run produced a status"))
            return rc;
        // 批量可达率:disabled 不计,enabled 计分母。
        std::vector< rws::TaskPoint > batch;
        batch.push_back (p);
        rws::TaskPoint p2 = p; p2.id = "P_disabled_2"; p2.enabled = false;
        batch.push_back (p2);
        const auto results = analyzer.analyzeTaskPoints (
            workcell.get (), device, device->getEnd (), state, batch, NULL);
        if (const int rc = require (results.size () == 2,
                                    "workcell-aware batch returns 2 results"))
            return rc;
        if (const int rc = require (results[0].status != rws::AnalysisStatus::Unknown,
                                    "enabled world task point receives a concrete status"))
            return rc;
        if (const int rc = require (results[1].status == rws::AnalysisStatus::Warning,
                                    "disabled batch point remains skipped warning"))
            return rc;
    }

    return 0;
}

// runAll:把所有子套件串行跑一遍,首个失败立即返回。
// 子套件 UI 逻辑辅助函数:全部为纯函数/列定义/导入辅助,不需要 QApplication。
//   - ikCollisionCheckRequested:碰撞复选框(存在/勾选)与 legacy 碰撞分析的关系;
//   - visualEnvelopeModeAvailable / visualEnvelopeDirectionChangeSupersedesRequest /
//     visualEnvelopeStateChangeRequiresRefresh:包络可视化模式/状态/方向的刷新规则;
//   - taskPointCompactTableColumns / taskPointDetailColumns:紧凑/详情列集合的契约,
//     紧凑列不含位姿与 BestQ(留给详情列);
//   - defaultTcpFrameName:设备 TCP 默认取设备末端 frame(NULL 设备为空);
//   - analyzeSelectedTaskPointRows:只重新分析选中行,未选中行保留上次结果;
//   - taskPointFromCurrentTcpPose:从当前 TCP 位姿导入任务点(参考系=设备 base)。
static int testTaskPointUiLogic ()
{
    if (const int rc = require (rws::ikCollisionCheckRequested (true, true),
                                "IK collision checkbox checked requests collision analysis"))
        return rc;
    if (const int rc = require (!rws::ikCollisionCheckRequested (true, false),
                                "IK collision checkbox unchecked skips collision analysis"))
        return rc;
    if (const int rc = require (rws::ikCollisionCheckRequested (false, false),
                                "missing IK collision checkbox preserves legacy collision analysis"))
        return rc;
    if (const int rc = require (
            rws::visualEnvelopeModeAvailable (1, static_cast<int> (rws::VisualRenderMode::Envelope)),
            "envelope mode is available for workspace source"))
        return rc;
    if (const int rc = require (
            !rws::visualEnvelopeModeAvailable (0, static_cast<int> (rws::VisualRenderMode::Envelope)),
            "envelope mode is not available for task point source"))
        return rc;
    if (const int rc = require (
            rws::visualEnvelopeDirectionChangeSupersedesRequest (true, true),
            "direction change supersedes active envelope request"))
        return rc;
    if (const int rc = require (
            !rws::visualEnvelopeDirectionChangeSupersedesRequest (false, false),
            "direction change without active request does not force generation bump"))
        return rc;
    if (const int rc = require (
            rws::visualEnvelopeStateChangeRequiresRefresh (true, true),
            "state change refreshes active envelope visualization"))
        return rc;
    if (const int rc = require (
            !rws::visualEnvelopeStateChangeRequiresRefresh (false, true),
            "state change does not refresh inactive envelope visualization"))
        return rc;

    const std::vector< int > compactColumns = rws::taskPointCompactTableColumns ();
    const std::vector< int > detailColumns  = rws::taskPointDetailColumns ();
    auto containsColumn = [] (const std::vector< int >& columns, int column) {
        for (int c : columns) {
            if (c == column)
                return true;
        }
        return false;
    };

    if (const int rc = require (containsColumn (compactColumns, rws::ColEnabled),
                                "compact task point columns include enabled"))
        return rc;
    if (const int rc = require (containsColumn (compactColumns, rws::ColName),
                                "compact task point columns include name"))
        return rc;
    if (const int rc = require (containsColumn (compactColumns, rws::ColRefFrame) &&
                                containsColumn (compactColumns, rws::ColTcpFrame),
                                "compact task point columns include frames"))
        return rc;
    if (const int rc = require (containsColumn (compactColumns, rws::ColStatus),
                                "compact task point columns include status"))
        return rc;
    if (const int rc = require (!containsColumn (compactColumns, rws::ColX) &&
                                !containsColumn (compactColumns, rws::ColBestQ),
                                "compact task point columns leave pose and best Q for details"))
        return rc;

    if (const int rc = require (containsColumn (detailColumns, rws::ColId) &&
                                containsColumn (detailColumns, rws::ColType),
                                "detail task point columns include definition fields"))
        return rc;
    if (const int rc = require (containsColumn (detailColumns, rws::ColPosTol),
                                "detail task point columns include position tolerance"))
        return rc;
    if (const int rc = require (containsColumn (detailColumns, rws::ColCondition),
                                "detail task point columns include condition"))
        return rc;

    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    if (const int rc = require (
            rws::defaultTcpFrameName (device.get ()) ==
                device->getEnd ()->getName (),
            "device TCP defaults to the device end frame"))
        return rc;
    if (const int rc = require (
            rws::defaultTcpFrameName (NULL).empty (),
            "null device has no default TCP frame"))
        return rc;
    rw::models::WorkCell::Ptr workcell =
        rw::core::ownedPtr (new rw::models::WorkCell (stateStructure, "TestWC", ""));
    const rw::kinematics::State state = workcell->getDefaultState ();

    rws::KinematicAnalyzer analyzer;

    rws::TaskPoint keep;
    keep.id = "keep";
    keep.name = "keep";
    keep.enabled = true;
    keep.refFrame = device->getBase ()->getName ();
    keep.tcpFrame = "TCP";
    keep.position = {{0.0, 0.0, 0.0}};

    rws::TaskPoint selected = keep;
    selected.id = "selected";
    selected.name = "selected";
    selected.refFrame = "MissingFrame";

    rws::TaskPointReachabilityResult previous;
    previous.taskPoint = keep;
    previous.status = rws::AnalysisStatus::Pass;
    previous.primaryFailure = rws::KinematicFailureReason::None;

    const std::vector< rws::TaskPointReachabilityResult > updated =
        rws::analyzeSelectedTaskPointRows (
            analyzer, workcell.get (), device, device->getEnd (), state,
            std::vector< rws::TaskPoint > {keep, selected},
            std::vector< int > {1},
            std::vector< rws::TaskPointReachabilityResult > {previous},
            NULL);

    if (const int rc = require (updated.size () == 2,
                                "selected analysis keeps result vector aligned to rows"))
        return rc;
    if (const int rc = require (updated[0].status == rws::AnalysisStatus::Pass,
                                "selected analysis preserves unselected previous result"))
        return rc;
    if (const int rc = require (updated[1].status == rws::AnalysisStatus::Fail,
                                "selected analysis uses workcell-aware resolver failure"))
        return rc;
    if (const int rc = require (updated[1].primaryFailure == rws::KinematicFailureReason::InvalidTarget,
                                "selected analysis reports missing refFrame as InvalidTarget"))
        return rc;

    const Transform3D<> baseTtcp (Vector3D<> (1.0, 2.0, 3.0),
                                  RPY<> (0.1, 0.2, 0.3));
    const rws::TaskPoint imported = rws::taskPointFromCurrentTcpPose (
        "TP_001", "TCP", device->getBase ()->getName (), baseTtcp,
        rws::KinematicThresholds ());
    if (const int rc = require (imported.refFrame == device->getBase ()->getName (),
                                "current TCP import stores base refFrame for base coordinates"))
        return rc;
    if (const int rc = assertNear (imported.position[0], 1.0, 1e-12,
                                   "current TCP import preserves base x"))
        return rc;
    if (const int rc = require (imported.tcpFrame == "TCP",
                                "current TCP import stores selected TCP name"))
        return rc;
    return 0;
}

// 子套件:P3 TaskPointTableModel 数据层单测。
// 覆盖:列数 27、header 文本、insertRows / removeRows、setData 字段、
// validation 行为、result 列只读、Q_OBJECT 兼容(QModelIndex)。
// 补充说明:数据层契约的核心断言——27 列与列头固定;显示单位(毫米/弧度)只影响表
// 展示,存储与分析仍是米/度;result 列(ColStatus 起)只读;非法数值输入返回 false
// 且不污染已有值;setRowsFromTaskPoints 覆盖式导入后立即 validateAll;
// 稳定任务 ID 是模型身份边界:重复 ID 导入保留首行、append 重复 ID 拒绝、编辑 ID
// 不得引入重复;applyResultsByTaskId 只作用于唯一 ID;
// 多行插入自动生成未占用的稳定默认 ID(P1/P2/P4/P3)。
static int testTaskPointModel ()
{
    using namespace rws;

    TaskPointTableModel model;

    // 1) 初始状态:0 行 27 列。
    if (const int rc = require (model.rowCount () == 0, "model starts empty"))
        return rc;
    if (const int rc = require (model.columnCount () == 27,
                                "model column count is 27"))
        return rc;

    // 2) header 与当前 UI 一致(只检查代表性列)。
    const QStringList headers = model.allHeaderTexts ();
    if (const int rc = require (headers.size () == 27, "header list size matches"))
        return rc;
    if (const int rc = require (headers[ColEnabled]  == QStringLiteral ("Enabled"),
                                "header[ColEnabled]"))
        return rc;
    if (const int rc = require (headers[ColType]    == QStringLiteral ("type"),
                                "header[ColType]"))
        return rc;
    if (const int rc = require (headers[ColTcpFrame]== QStringLiteral ("tcpFrame"),
                                "header[ColTcpFrame]"))
        return rc;
    if (const int rc = require (headers[ColCollision] == QStringLiteral ("collision"),
                                "header[ColCollision]"))
        return rc;

    // Display units affect only the table representation.  The TaskPoint
    // stored by the model remains in meters/degrees for analysis and export.
    model.setDisplayUnits (KinematicLengthUnit::Millimeters,
                           KinematicAngleUnit::Radians);
    if (const int rc = require (
            model.headerData (ColX, Qt::Horizontal, Qt::DisplayRole).toString () ==
                QStringLiteral ("x (mm)"),
            "position header follows selected length unit"))
        return rc;
    if (const int rc = require (
            model.headerData (ColRoll, Qt::Horizontal, Qt::DisplayRole).toString () ==
                QStringLiteral ("roll (rad)"),
            "orientation header follows selected angle unit"))
        return rc;

    // 3) insertRows:默认行的 id / name / type / refFrame / tcpFrame。
    if (const int rc = !model.insertRows (0, 1) ? 1 : 0)
        return rc;
    if (const int rc = require (model.rowCount () == 1, "insert one row"))
        return rc;
    const QModelIndex e0 = model.index (0, ColEnabled);
    const QModelIndex x0 = model.index (0, ColX);
    const QModelIndex id0 = model.index (0, ColId);
    if (const int rc = require (e0.data (Qt::CheckStateRole).toInt () == int (Qt::Checked),
                                "default row enabled is checked"))
        return rc;
    if (const int rc = require (model.data (id0, Qt::DisplayRole).toString () == QStringLiteral ("P1"),
                                "default id P1"))
        return rc;
    if (const int rc = require (x0.data (Qt::DisplayRole).toString () == QStringLiteral ("0"),
                                "default x = 0"))
        return rc;

    // 4) setData:改 x / type / freeRoll,确认 taskPointAt 返回正确。
    if (const int rc = !model.setData (x0, QStringLiteral ("420")) ? 1 : 0)
        return rc;
    if (const int rc = !model.setData (model.index (0, ColType),
                                       QStringLiteral ("Pick")) ? 1 : 0)
        return rc;
    if (const int rc = !model.setData (model.index (0, ColFreeRoll),
                                       QStringLiteral ("true")) ? 1 : 0)
        return rc;
    {
        const TaskPoint p = model.taskPointAt (0);
        if (const int rc = require (nearlyEqual (p.position[0], 0.42, 1e-12),
                                    "x = 420 mm round-trips to 0.42 m"))
            return rc;
        if (const int rc = require (p.type == TaskPointType::Pick,
                                    "type Pick round-trip"))
            return rc;
        if (const int rc = require (p.tolerance.allowToolRollFree == true,
                                    "freeRoll true round-trip"))
            return rc;
    }
    if (const int rc = require (
            model.data (x0, Qt::DisplayRole).toString () == QStringLiteral ("420"),
            "meters are rendered in selected millimeters"))
        return rc;

    // 5) result 列(17..26)flags 不应包含 editable。
    for (int c = ColStatus; c < TaskPointColumnCount; ++c) {
        if (const int rc = require (
                !((model.flags (model.index (0, c))) & Qt::ItemIsEditable),
                QStringLiteral ("result column %1 is read-only").arg (c).toStdString ()))
            return rc;
    }

    // 6) 非法数值输入返回 false,不污染已有值。
    const double before = model.taskPointAt (0).position[1];
    if (const int rc = model.setData (model.index (0, ColY), QStringLiteral ("not a number")) ? 1 : 0)
        return rc;
    if (const int rc = require (nearlyEqual (model.taskPointAt (0).position[1], before, 0),
                                "invalid y input leaves value unchanged"))
        return rc;

    // 7) removeRows。
    if (const int rc = !model.removeRows (0, 1) ? 1 : 0)
        return rc;
    if (const int rc = require (model.rowCount () == 0, "remove one row"))
        return rc;

    // 8) setRowsFromTaskPoints:覆盖式导入,validateAll 立刻跑。
    TaskPoint a; a.id = "A"; a.name = "A";
    a.enabled = true; a.refFrame = "WORLD"; a.tcpFrame = "TCP";
    a.position = {{1.0, 2.0, 3.0}};
    TaskPoint b = a; b.id = "B"; b.refFrame = "";  // invalid: empty refFrame
    std::vector< TaskPoint > rows = {a, b};
    model.setRowsFromTaskPoints (rows);
    if (const int rc = require (model.rowCount () == 2, "import 2 rows"))
        return rc;
    QString taskPointError;
    const std::vector< TaskPoint > roundTrip = model.taskPoints (&taskPointError);
    if (const int rc = require (roundTrip.size () == 2,
                                "taskPoints returns all imported rows"))
        return rc;
    if (const int rc = require (roundTrip[0].id == "A" && roundTrip[1].id == "B",
                                "taskPoints preserves row ids"))
        return rc;
    QString summary;
    if (const int rc = require (!model.validateAll (&summary),
                                "imported invalid row reported"))
        return rc;
    if (const int rc = require (summary.contains (QStringLiteral ("Row 2")),
                                "summary points to row 2"))
        return rc;

    // Stable task IDs are the model identity boundary. Imports retain the
    // first occurrence and later duplicate sources cannot target one visual
    // row while silently leaving another row with the same identity stale.
    TaskPoint duplicateA = a;
    duplicateA.name = "duplicate A";
    TaskPoint distinct = a;
    distinct.id = "C";
    model.setRowsFromTaskPoints ({a, duplicateA, distinct});
    if (const int rc = require (model.rowCount () == 2 &&
                                model.taskPointAt (0).id == "A" &&
                                model.taskPointAt (0).name == "A" &&
                                model.taskPointAt (1).id == "C",
                                "duplicate task IDs are normalized at import preserving the first row"))
        return rc;
    if (const int rc = require (model.appendTaskPoint (duplicateA) == -1 &&
                                model.rowCount () == 2,
                                "duplicate task IDs are rejected when appended"))
        return rc;
    if (const int rc = require (!model.setData (model.index (1, ColId),
                                                QStringLiteral ("A")) &&
                                model.taskPointAt (1).id == "C",
                                "editing a task ID cannot introduce a duplicate"))
        return rc;
    TaskPointReachabilityResult canonicalResult;
    canonicalResult.taskPoint = a;
    canonicalResult.status = AnalysisStatus::Pass;
    model.applyResultsByTaskId ({canonicalResult});
    const std::vector< TaskPointReachabilityResult > canonicalResults = model.results ();
    if (const int rc = require (canonicalResults.size () == 1 &&
                                canonicalResults.front ().taskPoint.id == "A",
                                "result application targets exactly one canonical task ID"))
        return rc;

    TaskPoint p1 = a;
    p1.id = "P1";
    TaskPoint p3 = a;
    p3.id = "P3";
    model.setRowsFromTaskPoints ({p1, p3});
    if (const int rc = require (model.insertRows (1, 2) && model.rowCount () == 4 &&
                                model.taskPointAt (0).id == "P1" &&
                                model.taskPointAt (1).id == "P2" &&
                                model.taskPointAt (2).id == "P4" &&
                                model.taskPointAt (3).id == "P3",
                                "multi-row insertion generates unused stable default IDs"))
        return rc;

    return 0;
}

// 项目文档只保存工程师可编辑的分析配置。该回归用例刻意不构造任何分析结果，
// 以防后续实现为了方便而把大量、可重新计算的 workspace/IK 结果写入 rwproj 资源。
// 同时验证设备和 TCP 仅以名称保存，项目文档不应携带机器相关的绝对文件路径。
// 补充说明:工程文档只持久化"可编辑配置",刻意不写入 workspaceSamples/analysisResults
// 这类可重算结果;设备与 TCP 仅以名称保存、不含机器相关绝对路径;
// ikDuplicateQThreshold 在顶层与阈值结构内双写且保持同步,并兼容 schema v1 的单一
// 字段(thresholdIkDuplicateQ / ikDuplicateQThreshold)读入,保证旧工程可迁移。
static int testProjectDocumentRoundTrip ()
{
    rws::KinematicAnalysisProjectSettings original;
    original.deviceName = "GenericSixAxis";
    original.tcpFrameName = "TCP";
    original.ikPositionMeters = {{0.15, -0.20, 0.45}};
    original.workspace.sampleCount = 2400;
    original.workspace.randomSeed = 17;
    original.poseReachability.directionSamples = 36;
    original.ikDuplicateQThreshold = 0.0042;
    original.thresholds.ikDuplicateQThreshold = 0.0042;
    // 可视化偏好是配置的一部分，但分析点与渲染结果不是；这里使用布尔开关验证该边界。
    original.showLabels = true;

    rws::TaskPoint point;
    point.id = "pick-01";
    point.name = "Pick target";
    point.position = {{0.10, 0.20, 0.30}};
    point.tolerance.positionMeters = 0.002;
    original.taskPoints.push_back (point);
    original.manualPosePositions.push_back ({{0.40, 0.50, 0.60}});

    const QByteArray json = rws::KinematicAnalysisProjectDocument::toJson (original);
    if (const int rc = require (!json.contains ("workspaceSamples") &&
                                    !json.contains ("analysisResults"),
                                "project document excludes recomputable analysis results"))
        return rc;
    if (const int rc = require (!json.contains (":\\\\") && !json.contains ("/home/"),
                                "project document excludes absolute paths"))
        return rc;

    const QJsonObject serializedSettings = QJsonDocument::fromJson (json).object ()
        .value (QStringLiteral ("settings")).toObject ();
    if (const int rc = require (
            serializedSettings.contains (QStringLiteral ("ikDuplicateQThreshold")) &&
                serializedSettings.contains (QStringLiteral ("thresholdIkDuplicateQ")) &&
                serializedSettings.value (QStringLiteral ("ikDuplicateQThreshold")).toDouble () ==
                    serializedSettings.value (QStringLiteral ("thresholdIkDuplicateQ")).toDouble (),
            "project document writes synchronized duplicate-Q fields"))
        return rc;

    rws::KinematicAnalysisProjectSettings decoded;
    QString error;
    if (const int rc = require (rws::KinematicAnalysisProjectDocument::fromJson (
                                    json, decoded, &error),
                                "project document parses after serialization"))
        return rc;
    if (const int rc = require (decoded.deviceName == original.deviceName &&
                                    decoded.tcpFrameName == original.tcpFrameName,
                                "project document preserves device binding by name"))
        return rc;
    if (const int rc = require (decoded.taskPoints.size () == 1 &&
                                    decoded.taskPoints.front ().id == point.id &&
                                    decoded.manualPosePositions.size () == 1,
                                "project document preserves authored task and pose inputs"))
        return rc;
    QJsonObject legacyRoot = QJsonDocument::fromJson (json).object ();
    QJsonObject legacySettings = legacyRoot.value (QStringLiteral ("settings")).toObject ();
    legacySettings.remove (QStringLiteral ("ikDuplicateQThreshold"));
    legacySettings[QStringLiteral ("thresholdIkDuplicateQ")] = 0.0075;
    legacyRoot[QStringLiteral ("settings")] = legacySettings;
    rws::KinematicAnalysisProjectSettings legacyDecoded;
    if (const int rc = require (
            rws::KinematicAnalysisProjectDocument::fromJson (
                QJsonDocument (legacyRoot).toJson (), legacyDecoded, &error) &&
                nearlyEqual (legacyDecoded.ikDuplicateQThreshold, 0.0075) &&
                nearlyEqual (legacyDecoded.thresholds.ikDuplicateQThreshold, 0.0075),
            "schema v1 legacy duplicate-Q field synchronizes both settings"))
        return rc;
    QJsonObject currentRoot = QJsonDocument::fromJson (json).object ();
    QJsonObject currentSettings = currentRoot.value (QStringLiteral ("settings")).toObject ();
    currentSettings.remove (QStringLiteral ("thresholdIkDuplicateQ"));
    currentSettings[QStringLiteral ("ikDuplicateQThreshold")] = 0.0065;
    currentRoot[QStringLiteral ("settings")] = currentSettings;
    rws::KinematicAnalysisProjectSettings currentDecoded;
    if (const int rc = require (
            rws::KinematicAnalysisProjectDocument::fromJson (
                QJsonDocument (currentRoot).toJson (), currentDecoded, &error) &&
                nearlyEqual (currentDecoded.ikDuplicateQThreshold, 0.0065) &&
                nearlyEqual (currentDecoded.thresholds.ikDuplicateQThreshold, 0.0065),
            "schema v1 current duplicate-Q field synchronizes both settings"))
        return rc;
    return assertNear (decoded.workspace.sampleCount, original.workspace.sampleCount, 0.0,
                       "project document workspace sample count");
}

// 子套件 冻结工件导入(本文件最复杂的集成场景):构造真实 WorkCell + 冻结工件,
// 系统验证运动学输入路径 apply / applyWithValidation / applyExecutionSet:
//   - 工位任务点(含非 WORLD 工装参考系、TCP、姿态解析证据)与覆盖盒字段
//     无损进入执行输入,Info 级覆盖盒被排除、Included 项不被降级;
//   - v4 执行契约校验指纹/来源溯源;v3 工件可导入 Quick 但要求重冻结、拒绝 Verified;
//     v4 Quick-only 区域被 Verified 分析拒绝(REQ_VERIFIED_REGION_POLICY_MISSING);
//   - 缺失 schemaVersion/模型指纹、场景指纹不匹配、被排除的 Must、执行契约被篡改
//     均返回稳定错误码,且失败时绝不改写调用方输出(原子性);
//   - 项目整体迁移后 provenance 重定位;机器人 jog/模型 TCP 变更/场景状态变更时
//     的接受或拒绝行为;解析嵌套在 RequirementSet 根对象里的 frozenArtifact。
// 补充关键点:
//   - 工位任务点非 WORLD 工装参考系(Fixture_A)、TCP(ToolTCP)与姿态解析证据
//     (resolutionEvidence)须无损进入 TaskPoint;
//   - Included 覆盖盒逐字段无损进入执行输入,Info 级 audit_workspace 被排除;
//   - v4 执行契约(applyExecutionSet)完整保留条目集合与 provenance(含 sourcePath);
//   - v3 工件可导入 Quick(要求重冻结 REQ_V3_REQUIRES_REFREEZE)、拒绝 Verified;
//     只有 Quick 区域的 v4 工件被 Verified 分析拒绝(REQ_VERIFIED_REGION_POLICY_MISSING);
//   - 失败原子性:校验失败时调用方输出完全不变(sentinel 哨兵);
//   - 项目迁移后 provenance 按新根重定位;机器人 jog 允许但报告 robotStateChanged;
//     源文件内容变更给出告警但不阻断;机器人模型/TCP 变更拒绝并要求重冻结;
//     场景状态(工装移动)变更拒绝;
//   - 冻结工件可嵌套在 RequirementSet 根对象的 frozenArtifact 字段,未冻结项目
//     必须报"not frozen"而非误导性"schema 不支持"。
static int testFrozenRequirementArtifactImportsIntoKinematicTasks ()
{
    // 运动学分析只能读取已经冻结的编译结果。测试同时验证非 WORLD 工装参考系、
    // TCP、代表姿态和姿态解析证据均原样传入现有 TaskPoint 数据模型。
    QTemporaryDir sourceDirectory;
    if (const int rc = require(sourceDirectory.isValid(),
                               "create temporary source WorkCell directory")) return rc;
    const QString sourcePath = sourceDirectory.filePath(QStringLiteral("source.wc.xml"));
    QFile sourceFile(sourcePath);
    if (const int rc = require(sourceFile.open(QIODevice::WriteOnly | QIODevice::Text),
                               "create source WorkCell file")) return rc;
    sourceFile.write("<WorkCell name=\"FrozenImportCell\" />\n");
    sourceFile.close();

    rw::kinematics::StateStructure::Ptr structure =
        rw::core::ownedPtr(new rw::kinematics::StateStructure());
    const rw::kinematics::FixedFrame::Ptr base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("FrozenImportBase", rw::math::Transform3D<>()));
    const rw::models::RevoluteJoint::Ptr joint = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("FrozenImportJoint", rw::math::Transform3D<>()));
    const rw::kinematics::MovableFrame::Ptr fixture = rw::core::ownedPtr(
        new rw::kinematics::MovableFrame("Fixture_A"));
    const rw::kinematics::FixedFrame::Ptr tcp = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("ToolTCP", rw::math::Transform3D<>()));
    structure->addFrame(base, structure->getRoot());
    structure->addFrame(joint, base);
    structure->addFrame(tcp, joint);
    structure->addFrame(fixture, structure->getRoot());
    const rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr(
        new rw::models::WorkCell(structure, "FrozenImportCell", sourcePath.toStdString()));

    rws::RobotModelSpec model;
    model.robotName = "FrozenImportRobot";
    const rw::models::SerialDevice::Ptr device = rw::core::ownedPtr(
        new rw::models::SerialDevice(base.get(), tcp.get(), model.robotName,
                                     structure->getDefaultState()));
    workcell->addDevice(device);
    rws::RequirementSet requirements;
    requirements.modelBinding.robotName = model.robotName;
    requirements.modelBinding.robotModelFingerprint =
        rws::RobotModelFingerprint::canonicalSha256(model);
    rws::PoseTask station;
    station.id = "fixture_pick";
    station.name = "Fixture pick";
    station.refFrame = "Fixture_A";
    station.tcpFrame = "ToolTCP";
    station.position = {{0.10, 0.20, 0.30}};
    station.rpyDeg = {{0.0, 90.0, 0.0}};
    station.orientation.resolutionEvidence =
        "resolver=OrientationRuleResolver.1;mode=AlignFrame;target=Fixture_A";
    requirements.poseTasks.push_back(station);

    // 构造一个"包含型"覆盖盒：各采样/朝向/阈值字段全部显式填充，用于验证冻结后
    // 覆盖盒能完整无损地进入运动学输入，且验证阶段不被降级。
    rws::BoxRegion includedRegion;
    includedRegion.id = "fixture_workspace";
    includedRegion.name = "Fixture workspace";
    includedRegion.level = rws::RequirementLevel::Should;
    includedRegion.refFrame = "Fixture_A";
    includedRegion.tcpFrame = "ToolTCP";
    includedRegion.center = {{0.11, 0.22, 0.33}};
    includedRegion.size = {{0.44, 0.55, 0.66}};
    includedRegion.minimumCoverage = 0.73;
    includedRegion.samplesPerAxis = 4;
    includedRegion.orientationMode = rws::OrientationMode::AlignFrame;
    includedRegion.orientationTargetFrame = "Fixture_A";
    includedRegion.orientationTargetGeometry = "frame:Fixture_A";
    includedRegion.orientationTargetPoint = "0.7,0.8,0.9";
    includedRegion.fixedRpyDeg = {{11.0, 22.0, 33.0}};
    includedRegion.directionSamples = 12;
    includedRegion.rollSamples = 7;
    includedRegion.minimumOrientationCoverage = 0.61;
    includedRegion.minimumVerificationStage = rws::RequirementVerificationStage::Verified;
    includedRegion.collisionFreeRequired = false;
    includedRegion.positionToleranceMeters = 0.0025;
    includedRegion.orientationToleranceDeg = 1.75;
    includedRegion.minimumJointMargin = 0.08;
    includedRegion.minimumManipulability = 0.015;
    requirements.boxRegions.push_back(includedRegion);

    // 再构造一个 Info 级覆盖盒：Info 仅作审计记录，冻结后必须被排除在运动学输入外，
    // 验证"只让 Included 项进入执行输入"的过滤规则。
    rws::BoxRegion excludedRegion = includedRegion;
    excludedRegion.id = "audit_workspace";
    excludedRegion.name = "Audit-only workspace";
    excludedRegion.level = rws::RequirementLevel::Info;
    requirements.boxRegions.push_back(excludedRegion);

    rws::FrozenRequirementArtifact artifact;
    std::string error;
    if (const int rc = require(rws::RequirementFreezer::freeze(
            requirements, *workcell, workcell->getDefaultState(), model, artifact, &error,
            sourceDirectory.path().toStdString()),
                               "freeze artifact for kinematic import")) return rc;
    if (const int rc = require(artifact.compiled.frozen,
                               "freeze artifact records compiled frozen state")) return rc;
    if (const int rc = require(artifact.compiled.requirementFingerprint == artifact.requirementFingerprint,
                               "freeze artifact keeps compiled requirement fingerprint")) return rc;
    rws::RobotModelSpec restoredScene;
    error.clear();
    if (const int rc = require(rws::RobotModelSpecJson::fromObject(
            rws::RobotModelSpecJson::toObject(artifact.scenario.sceneSpec), restoredScene, &error),
                               "round-trip frozen scenario specification: " + error)) return rc;
    if (const int rc = require(artifact.compiled.frozen,
                               "scenario round-trip preserves compiled frozen state")) return rc;
    if (const int rc = require(artifact.compiled.requirementFingerprint == artifact.requirementFingerprint,
                               "scenario round-trip preserves compiled requirement fingerprint")) return rc;
    std::vector<rws::TaskPoint> tasks;
    const bool imported = rws::FrozenRequirementKinematicAdapter::apply(
        artifact, *workcell, workcell->getDefaultState(), tasks, &error);
    if (const int rc = require(imported,
                               "import frozen artifact into kinematic tasks: " + error)) return rc;
    if (const int rc = require(tasks.size() == 1, "one frozen task is imported")) return rc;
    if (const int rc = require(tasks.front().refFrame == "Fixture_A", "fixture reference is retained")) return rc;
    if (const int rc = require(tasks.front().tcpFrame == "ToolTCP", "tcp is retained")) return rc;
    if (const int rc = require(tasks.front().note.find("OrientationRuleResolver.1") != std::string::npos,
                                "orientation evidence is retained")) return rc;

    // 新增的 FrozenKinematicRequirementInput 输入路径：同时导入工位任务点与覆盖盒。
    // 验证只保留 Included 覆盖盒(Info 级 audit_workspace 不得进入执行输入)，
    // 且覆盖盒的身份/几何/采样/朝向/阈值/验证阶段逐字段与冻结前的输入一致、不被降级。
    rws::FrozenKinematicRequirementInput requirementInput;
    error.clear();
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::apply(
            artifact, *workcell, workcell->getDefaultState(), requirementInput, &error),
                               "import frozen tasks and workspace regions: " + error)) return rc;
    if (const int rc = require(requirementInput.tasks.size() == 1,
                               "new kinematic input retains included tasks")) return rc;
    if (const int rc = require(requirementInput.workspaceRegions.size() == 1,
                               "excluded workspace regions do not enter kinematic execution input")) return rc;
    const rws::RequirementExecutionRegion& region = requirementInput.workspaceRegions.front();
    if (const int rc = require(region.id == includedRegion.id &&
                                   region.name == includedRegion.name &&
                                   region.level == rws::RequirementExecutionLevel::Should &&
                                   region.compileState == rws::RequirementExecutionCompileState::Included &&
                                   region.excludedReason.empty(),
                               "workspace region identity and inclusion state are retained")) return rc;
    if (const int rc = require(region.refFrame == includedRegion.refFrame &&
                                   region.tcpFrame == includedRegion.tcpFrame &&
                                   region.center == includedRegion.center &&
                                   region.size == includedRegion.size,
                               "workspace region frames and geometry are retained")) return rc;
    if (const int rc = require(region.samplesPerAxis == includedRegion.samplesPerAxis &&
                                   region.orientationMode == rws::RequirementExecutionOrientationMode::AlignFrame &&
                                   region.orientationTargetFrame == includedRegion.orientationTargetFrame &&
                                   region.orientationTargetGeometry == includedRegion.orientationTargetGeometry &&
                                   region.orientationTargetPoint == includedRegion.orientationTargetPoint &&
                                   region.fixedRpyDeg == includedRegion.fixedRpyDeg &&
                                   region.directionSamples == includedRegion.directionSamples &&
                                   region.rollSamples == includedRegion.rollSamples,
                               "workspace region sampling and orientation policy are retained")) return rc;
    if (const int rc = require(
            nearlyEqual(region.minimumCoverage, includedRegion.minimumCoverage) &&
                nearlyEqual(region.minimumOrientationCoverage,
                            includedRegion.minimumOrientationCoverage) &&
                region.minimumVerificationStage == rws::RequirementExecutionStage::Verified,
            "workspace coverage thresholds and Verified stage are retained without downgrade")) return rc;
    if (const int rc = require(
            region.collisionFreeRequired == includedRegion.collisionFreeRequired &&
                nearlyEqual(region.positionToleranceMeters,
                            includedRegion.positionToleranceMeters) &&
                nearlyEqual(region.orientationToleranceDeg,
                            includedRegion.orientationToleranceDeg) &&
                nearlyEqual(region.minimumJointMargin, includedRegion.minimumJointMargin) &&
                nearlyEqual(region.minimumManipulability,
                            includedRegion.minimumManipulability),
            "workspace validation policy is retained")) return rc;

    // 校验路径(applyWithValidation)同样输出工作区覆盖盒，且经过场景校验后
    // Included 的工位与覆盖盒都应原样保留。
    rws::FrozenKinematicRequirementInput validatedInput;
    bool validatedRobotStateChanged = true;
    std::vector<std::string> validatedWarnings;
    error.clear();
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::applyWithValidation(
            artifact, *workcell, workcell->getDefaultState(), validatedInput, &error,
            &validatedRobotStateChanged, &validatedWarnings),
                               "validated kinematic input import: " + error)) return rc;
    if (const int rc = require(validatedInput.tasks.size() == 1 &&
                                   validatedInput.workspaceRegions.size() == 1,
                               "validated kinematic input preserves included task and region")) return rc;

    // v4 执行契约导入路径:把完整条目集合与 provenance(需求/模型/场景/环境指纹、
    // 编译器版本、冻结时间、源路径)整体搬入执行输入,防止任何字段跨界传输时丢失。
    rws::RequirementExecutionSet importedExecution;
    error.clear();
    if (const int rc = require(
            rws::FrozenRequirementKinematicAdapter::applyExecutionSet(
                artifact, *workcell, workcell->getDefaultState(),
                rws::AnalysisEvidenceStage::Verified, importedExecution, &error),
            "import the complete v4 execution contract: " + error)) return rc;
    if (const int rc = require(
            importedExecution.schemaVersion == artifact.execution.schemaVersion &&
                importedExecution.tasks.size() == artifact.execution.tasks.size() &&
                importedExecution.workspaceRegions.size() ==
                    artifact.execution.workspaceRegions.size(),
            "execution contract import preserves its complete item set")) return rc;
    if (const int rc = require(
            importedExecution.provenance.requirementFingerprint ==
                    artifact.execution.provenance.requirementFingerprint &&
                importedExecution.provenance.robotModelFingerprint ==
                    artifact.execution.provenance.robotModelFingerprint &&
                importedExecution.provenance.workcellFingerprint ==
                    artifact.execution.provenance.workcellFingerprint &&
                importedExecution.provenance.environmentFingerprint ==
                    artifact.execution.provenance.environmentFingerprint &&
                importedExecution.provenance.compilerVersion ==
                    artifact.execution.provenance.compilerVersion &&
                importedExecution.provenance.frozenAt ==
                    artifact.execution.provenance.frozenAt &&
                importedExecution.provenance.sourcePath ==
                    artifact.execution.provenance.sourcePath,
            "execution contract import preserves set provenance")) return rc;
    if (const int rc = require(
            importedExecution.tasks.front().provenance.sourceId ==
                    artifact.execution.tasks.front().provenance.sourceId &&
                importedExecution.tasks.front().provenance.sourceKind ==
                    artifact.execution.tasks.front().provenance.sourceKind &&
                importedExecution.workspaceRegions.front().provenance.sourceId ==
                    artifact.execution.workspaceRegions.front().provenance.sourceId &&
                importedExecution.workspaceRegions.front().provenance.sourceKind ==
                    artifact.execution.workspaceRegions.front().provenance.sourceKind,
            "execution contract import preserves item provenance")) return rc;

    // 稳定错误码判定:允许"精确匹配"或"码:前缀匹配"(错误码可能带冒号后附详情)。
    const auto hasStableErrorCode = [] (const std::string& value, const std::string& code) {
        return value == code || value.rfind(code + ":", 0) == 0;
    };

    // 把 v4 工件的一个区域降为 Quick-only 验证阶段,用于断言 Verified 分析会拒绝它
    // (REQ_VERIFIED_REGION_POLICY_MISSING),且失败时调用方输出不被改写(原子性)。
    rws::FrozenRequirementArtifact quickOnlyV4 = artifact;
    quickOnlyV4.compiled.workspaceRegions.front().minimumVerificationStage =
        rws::RequirementVerificationStage::Quick;
    quickOnlyV4.execution.workspaceRegions.front().minimumVerificationStage =
        rws::RequirementExecutionStage::Quick;
    quickOnlyV4.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(quickOnlyV4.execution);
    rws::RequirementExecutionSet unchangedExecution;
    rws::RequirementExecutionTask executionSentinel;
    executionSentinel.id = "sentinel";
    unchangedExecution.tasks.push_back(executionSentinel);
    error.clear();
    if (const int rc = require(
            !rws::FrozenRequirementKinematicAdapter::applyExecutionSet(
                quickOnlyV4, *workcell, workcell->getDefaultState(),
                rws::AnalysisEvidenceStage::Verified, unchangedExecution, &error),
            "reject a v4 Quick-only region for Verified analysis")) return rc;
    if (const int rc = require(
            hasStableErrorCode(error, "REQ_VERIFIED_REGION_POLICY_MISSING"),
            "missing Verified region policy reports a stable code")) return rc;
    if (const int rc = require(
            unchangedExecution.tasks.size() == 1 &&
                unchangedExecution.tasks.front().id == "sentinel",
            "failed Verified import leaves caller execution output unchanged")) return rc;

    // 构造 v3 老工件(schemaVersion=3、无执行契约):Quick 分析可导入并自动降级区域、
    // 记录 REQ_V3_REQUIRES_REFREEZE 诊断;Verified 分析必须拒绝并要求重新冻结。
    rws::FrozenRequirementArtifact legacyV3 = artifact;
    legacyV3.schemaVersion = 3;
    legacyV3.executionFingerprint.clear();
    legacyV3.execution = rws::RequirementExecutionSet();
    rws::RequirementExecutionSet legacyQuickExecution;
    error.clear();
    const bool legacyQuickImported =
        rws::FrozenRequirementKinematicAdapter::applyExecutionSet(
            legacyV3, *workcell, workcell->getDefaultState(),
            rws::AnalysisEvidenceStage::Quick, legacyQuickExecution, &error);
    if (const int rc = require(
            legacyQuickImported,
            "import a v3 frozen artifact for Quick analysis: " + error)) return rc;
    if (const int rc = require(
            legacyQuickExecution.tasks.size() == 1 &&
                legacyQuickExecution.workspaceRegions.size() == 1 &&
                legacyQuickExecution.workspaceRegions.front().minimumVerificationStage ==
                    rws::RequirementExecutionStage::Quick,
            "v3 Quick import migrates complete tasks and downgrades regions")) return rc;
    if (const int rc = require(
            std::any_of(legacyQuickExecution.diagnostics.begin(),
                        legacyQuickExecution.diagnostics.end(),
                        [] (const rws::RequirementExecutionDiagnostic& diagnostic) {
                            return diagnostic.code == "REQ_V3_REQUIRES_REFREEZE";
                        }),
            "v3 Quick import records the refreeze diagnostic")) return rc;

    rws::FrozenKinematicRequirementInput legacyCompatibilityInput;
    error.clear();
    if (const int rc = require(
            rws::FrozenRequirementKinematicAdapter::apply(
                legacyV3, *workcell, workcell->getDefaultState(),
                legacyCompatibilityInput, &error),
            "legacy adapter entry accepts v3 as Quick compatibility input")) return rc;
    if (const int rc = require(
            legacyCompatibilityInput.workspaceRegions.size() == 1 &&
                legacyCompatibilityInput.workspaceRegions.front().minimumVerificationStage ==
                    rws::RequirementExecutionStage::Quick,
            "legacy v3 adapter entry never exposes a Verified region")) return rc;

    error.clear();
    if (const int rc = require(
            !rws::FrozenRequirementKinematicAdapter::applyExecutionSet(
                legacyV3, *workcell, workcell->getDefaultState(),
                rws::AnalysisEvidenceStage::Verified, unchangedExecution, &error),
            "reject a v3 frozen artifact for Verified analysis")) return rc;
    if (const int rc = require(hasStableErrorCode(error, "REQ_V3_REQUIRES_REFREEZE"),
                               "v3 Verified import requires refreeze")) return rc;

    // 缺少 schemaVersion 的工件必须被拒绝(REQ_SCHEMA_UNSUPPORTED),而不是被当作
    // 某种默认版本导入。
    QJsonObject missingSchemaObject =
        rws::FrozenRequirementArtifactJson::toObject(artifact);
    missingSchemaObject.remove("schemaVersion");
    rws::FrozenRequirementArtifact missingSchemaArtifact;
    error.clear();
    if (const int rc = require(
            !rws::FrozenRequirementKinematicAdapter::parseArtifactJson(
                missingSchemaObject, missingSchemaArtifact, &error),
            "reject a frozen artifact without schemaVersion")) return rc;
    if (const int rc = require(hasStableErrorCode(error, "REQ_SCHEMA_UNSUPPORTED"),
                               "missing schema reports REQ_SCHEMA_UNSUPPORTED")) return rc;

    // 缺少模型指纹的工件被拒绝(REQ_MODEL_FINGERPRINT_MISSING)——冻结结果不能在没有
    // 模型身份校验的情况下进入分析。
    rws::FrozenRequirementArtifact missingModelFingerprint = artifact;
    missingModelFingerprint.modelBinding.robotModelFingerprint.clear();
    error.clear();
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            missingModelFingerprint, *workcell, workcell->getDefaultState(), validatedInput,
            &error),
                               "reject a frozen artifact without model fingerprint")) return rc;
    if (const int rc = require(
            hasStableErrorCode(error, "REQ_MODEL_FINGERPRINT_MISSING"),
            "missing model fingerprint reports REQ_MODEL_FINGERPRINT_MISSING")) return rc;

    // 场景快照指纹不匹配被拒绝(REQ_SCENARIO_FINGERPRINT_MISMATCH),防止把冻结时场景
    // 与当前场景不同的工件误当作有效输入。
    rws::FrozenRequirementArtifact mismatchedScenario = artifact;
    mismatchedScenario.scenario.snapshotFingerprint = "mismatched-scenario-fingerprint";
    error.clear();
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            mismatchedScenario, *workcell, workcell->getDefaultState(), validatedInput,
            &error),
                               "reject a frozen artifact with a mismatched scenario fingerprint"))
        return rc;
    if (const int rc = require(
            hasStableErrorCode(error, "REQ_SCENARIO_FINGERPRINT_MISMATCH"),
            "scenario mismatch reports REQ_SCENARIO_FINGERPRINT_MISMATCH")) return rc;

    // 被 Excluded 的 Must 任务不得进入执行输入(REQ_MUST_ITEM_EXCLUDED),因为 Must 是
    // 硬约束,不能因为"排除"而静默丢失或降级。
    rws::FrozenRequirementArtifact excludedMust = artifact;
    excludedMust.compiled.poseTasks.front().compileState =
        rws::RequirementCompileState::Excluded;
    excludedMust.compiled.poseTasks.front().excludedReason = "test exclusion";
    excludedMust.execution.tasks.clear();
    excludedMust.executionFingerprint =
        rws::RequirementExecutionJson::fingerprint(excludedMust.execution);
    error.clear();
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            excludedMust, *workcell, workcell->getDefaultState(), validatedInput, &error),
                               "reject a frozen artifact with an excluded Must task")) return rc;
    if (const int rc = require(hasStableErrorCode(error, "REQ_MUST_ITEM_EXCLUDED"),
                               "excluded Must reports REQ_MUST_ITEM_EXCLUDED")) return rc;

    rws::FrozenRequirementArtifact tamperedExecution = artifact;
    tamperedExecution.execution.tasks.front().position[0] += 0.01;
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            tamperedExecution, *workcell, workcell->getDefaultState(), tasks, &error),
                               "reject a frozen artifact after execution task tampering")) return rc;
    if (const int rc = require(error.find("execution contract") != std::string::npos ||
                                    error.find("execution is missing") != std::string::npos,
                               "execution tampering reports a contract error")) return rc;
    // 失败原子性：用哨兵值预填充调用方输出，再对篡改过的执行契约调用 apply。
    // 断言导入失败且调用方的整个输出(工位任务点与覆盖盒)保持原样不被部分改写，
    // 保证下游不会在失败时拿到残缺输入。
    rws::FrozenKinematicRequirementInput unchangedInput;
    unchangedInput.tasks.push_back(rws::TaskPoint());
    unchangedInput.tasks.front().id = "sentinel";
    unchangedInput.workspaceRegions.push_back(region);
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            tamperedExecution, *workcell, workcell->getDefaultState(), unchangedInput, &error),
                               "new kinematic input rejects a tampered execution contract")) return rc;
    if (const int rc = require(unchangedInput.tasks.size() == 1 &&
                                   unchangedInput.tasks.front().id == "sentinel" &&
                                   unchangedInput.workspaceRegions.size() == 1 &&
                                   unchangedInput.workspaceRegions.front().id == region.id,
                               "failed import leaves the complete caller output unchanged")) return rc;

    // 项目整体迁移场景:把冻结工件从原目录复制到"克隆项目"新根目录并删除原源文件,
    // 验证 provenance 能按新根重定位解析,且不产生告警。
    QTemporaryDir clonedDirectory;
    if (const int rc = require(clonedDirectory.isValid(),
                               "create cloned project directory")) return rc;
    const QString clonedSourcePath = clonedDirectory.filePath(QStringLiteral("source.wc.xml"));
    if (const int rc = require(QFile::copy(sourcePath, clonedSourcePath),
                               "copy frozen WorkCell provenance into cloned project")) return rc;
    if (const int rc = require(QFile::remove(sourcePath),
                               "make original project source unavailable")) return rc;
    bool robotStateChanged = false;
    std::vector<std::string> validationWarnings;
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::applyWithValidation(
            artifact, *workcell, workcell->getDefaultState(), tasks, &error,
            &robotStateChanged, &validationWarnings, clonedDirectory.path().toStdString()),
                               "import relocated frozen artifact from cloned project root")) return rc;
    if (const int rc = require(validationWarnings.empty(),
                               "relocated source provenance resolves without warnings")) return rc;

    // 机器人 jog(关节移动)不改变运动学结构,应被接受但报告 robotStateChanged=true,
    // 供 UI 提示用户当前状态已偏离冻结快照。
    rw::kinematics::State joggedState = workcell->getDefaultState();
    device->setQ(rw::math::Q(1, 0.35), joggedState);
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::applyWithValidation(
            artifact, *workcell, joggedState, tasks, &error, &robotStateChanged),
                               "import frozen artifact after robot jog")) return rc;
    if (const int rc = require(robotStateChanged,
                               "robot jog is reported without rejecting frozen requirements")) return rc;

    // 迁移后源 WorkCell 文件内容被改写:导入仍成功(不阻断),但必须返回告警给
    // 运动学 UI,提示场景来源已变化。
    QFile clonedSourceFile(clonedSourcePath);
    if (const int rc = require(clonedSourceFile.open(QIODevice::WriteOnly | QIODevice::Text |
                                                     QIODevice::Truncate),
                               "replace cloned source WorkCell file after freeze")) return rc;
    clonedSourceFile.write("<WorkCell name=\"FrozenImportCellChanged\" />\n");
    clonedSourceFile.close();
    validationWarnings.clear();
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::applyWithValidation(
            artifact, *workcell, joggedState, tasks, &error, &robotStateChanged,
            &validationWarnings, clonedDirectory.path().toStdString()),
                               "source provenance warning does not block kinematic import")) return rc;
    if (const int rc = require(!validationWarnings.empty() &&
                                   validationWarnings.front().find("source WorkCell file") !=
                                       std::string::npos,
                               "source provenance warning is returned to the kinematic UI")) return rc;

    // 机器人模型或 TCP 配置变更(运动学指纹变化)必须拒绝并给出重新冻结指引,
    // 因为 IK 结果依赖 TCP 定义。
    rws::FrozenRequirementArtifact tcpChangedArtifact = artifact;
    tcpChangedArtifact.frozenRobotState.kinematicFingerprint = "changed-robot-kinematic-fingerprint";
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::applyWithValidation(
            tcpChangedArtifact, *workcell, joggedState, tasks, &error, &robotStateChanged),
                               "reject frozen artifact after robot model or TCP change")) return rc;
    if (const int rc = require(error.find("Robot model or TCP configuration") != std::string::npos,
                               "robot model or TCP change reports refreeze guidance")) return rc;

    // 场景状态变更(工装 Fixture_A 平移)使冻结工件失效:工位参考系不再是冻结时的
    // 相对位姿,必须拒绝导入。
    rw::kinematics::State changedState = joggedState;
    fixture->setTransform(rw::math::Transform3D<>(rw::math::Vector3D<>(0.1, 0.0, 0.0)), changedState);
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::apply(
            artifact, *workcell, changedState, tasks, &error),
                               "reject artifact for changed WorkCell state")) return rc;

    // 工程需求插件保存时将冻结工件嵌入可编辑 RequirementSet 根对象。这里必须走与
    // Widget 文件导入完全相同的解析入口，防止未来 UI 修改后又把项目根对象误传给
    // FrozenRequirementArtifactJson 而报“schema 不支持”。
    const QJsonObject project = rws::RequirementSetJson::toObject(requirements);
    QJsonObject frozenProject = project;
    frozenProject["frozenArtifact"] = rws::FrozenRequirementArtifactJson::toObject(artifact);
    rws::FrozenRequirementArtifact parsedArtifact;
    error.clear();
    if (const int rc = require(rws::FrozenRequirementKinematicAdapter::parseArtifactJson(
            frozenProject, parsedArtifact, &error),
                               "parse frozen artifact nested in requirement project: " + error)) return rc;
    if (const int rc = require(parsedArtifact.requirementFingerprint == artifact.requirementFingerprint,
                               "nested artifact preserves requirement fingerprint")) return rc;

    // 未冻结的项目必须得到可操作的错误，不能以“schema 不支持”误导用户，也不能
    // 退化为直接导入编辑态需求而绕过冻结状态和场景指纹复核。
    if (const int rc = require(!rws::FrozenRequirementKinematicAdapter::parseArtifactJson(
            project, parsedArtifact, &error), "reject an unfrozen requirement project")) return rc;
    if (const int rc = require(error.find("not frozen") != std::string::npos,
                               "unfrozen project reports actionable error")) return rc;
    return 0;
}

// 子套件 工作区包络辅助:验证 updateEnvelopeDimensions 对多边形边界的合法性
// 判定——少于 3 点、含非有限点、共线零面积都判为无效;相邻重复点被去重后
// 仍有效。computeWorkspaceEnvelope 能在六轴设备上算出非零尺寸包络;取消时
// 返回无效且不发布部分几何;非有限关节限位同样不产出几何。
// 补充说明:updateEnvelopeDimensions 的边界判定——<3 点、含非有限点(移除后 <3)、
// 共线零面积均无效;相邻重复点去重后仍有效(4 点菱形面积=2);
// computeWorkspaceEnvelope 在六轴设备上算出非零尺寸包络;
// 取消返回无效且不发布部分几何;非有限关节限位同样不产出几何。
static int testWorkspaceEnvelopeHelpers ()
{
    using namespace rws;
    using namespace rw::kinematics;
    using namespace rw::math;
    using namespace rw::models;

    AnalysisEnvelopeData direct;
    direct.projection = VisualProjection::XY;
    direct.boundary = {
        QPointF (1.0, 0.0),
        QPointF (0.0, 1.0),
        QPointF (-1.0, 0.0),
        QPointF (0.0, -1.0)
    };
    updateEnvelopeDimensions (direct);
    if (const int rc = require (direct.valid, "manual envelope is valid"))
        return rc;
    if (const int rc = assertNear (direct.maxRadius, 1.0, 1e-12, "manual max radius"))
        return rc;

    StateStructure::Ptr stateStructure = rw::core::ownedPtr (new StateStructure ());
    const rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    const rw::kinematics::State state = stateStructure->getDefaultState ();
    WorkspaceEnvelopeConfig config;
    config.projection = VisualProjection::XY;
    config.angularDirections = 24;
    config.coordinateIterations = 4;
    const AnalysisEnvelopeData envelope =
        computeWorkspaceEnvelope (device.get (), device->getEnd (), state, config);

    if (const int rc = require (envelope.valid, "computed generic six axis envelope is valid"))
        return rc;
    if (const int rc = require (envelope.boundary.size () >= 12,
                                "computed envelope has multiple boundary points"))
        return rc;
    if (const int rc = require (envelope.width > 0.1 && envelope.height > 0.1,
                                "computed envelope has nonzero dimensions"))
        return rc;

    // ---- 非法几何验证 ----

    // 少于 3 个点 → 无效
    {
        AnalysisEnvelopeData few;
        few.boundary = { QPointF (0.0, 0.0), QPointF (1.0, 0.0) };
        updateEnvelopeDimensions (few);
        if (const int rc = require (!few.valid, "fewer than 3 points is invalid"))
            return rc;
    }

    // NaN/Inf 点被移除 → 无效
    {
        AnalysisEnvelopeData nanData;
        nanData.boundary = {
            QPointF (0.0, 0.0),
            QPointF (std::numeric_limits< double >::quiet_NaN (), 1.0),
            QPointF (2.0, std::numeric_limits< double >::infinity ()),
            QPointF (0.0, 2.0)
        };
        updateEnvelopeDimensions (nanData);
        if (const int rc = require (!nanData.valid, "nan/inf filtered makes envelope invalid"))
            return rc;
        // 验证非有限点已被移除
        if (const int rc = require (nanData.boundary.size () == 2,
                                    "non-finite points removed from boundary, 2 remain"))
            return rc;
    }

    // 零面积（共线）→ 无效
    {
        AnalysisEnvelopeData collinear;
        collinear.boundary = {
            QPointF (0.0, 0.0),
            QPointF (1.0, 0.0),
            QPointF (2.0, 0.0),
            QPointF (3.0, 0.0)
        };
        updateEnvelopeDimensions (collinear);
        if (const int rc = require (!collinear.valid, "collinear zero-area boundary is invalid"))
            return rc;
    }

    // 重复点被归一化 → 有效（4个点去重后还有2个菱形端点，但要有至少3个点）
    // 测试相邻重复点去重
    {
        AnalysisEnvelopeData dup;
        dup.boundary = {
            QPointF (0.0, 0.0),
            QPointF (0.0, 0.0),
            QPointF (2.0, 0.0),
            QPointF (1.0, 1.0),
            QPointF (0.0, 2.0)
        };
        updateEnvelopeDimensions (dup);
        // 去重后应有 4 个点:(0,0),(2,0),(1,1),(0,2) — 面积=2 > 0
        if (const int rc = require (dup.valid, "dup-removed boundary is valid"))
            return rc;
        if (const int rc = require (dup.boundary.size () == 4,
                                    "adjacent duplicate points removed"))
            return rc;
    }

    // 文字描述验证
    {
        if (const int rc = require (
                rws::visualRenderModeText (VisualRenderMode::Envelope) ==
                    QStringLiteral ("Approximate outer envelope"),
                "display text identifies approximate outer envelope mode"))
            return rc;
    }

    // ---- cancellation ----
    {
        WorkspaceEnvelopeConfig cancelled;
        cancelled.cancel->store (true);
        const AnalysisEnvelopeData cancelledEnvelope =
            computeWorkspaceEnvelope (device.get (), device->getEnd (), state, cancelled);
        if (const int rc = require (!cancelledEnvelope.valid,
                                    "cancelled envelope computation returns invalid result"))
            return rc;
        if (const int rc = require (cancelledEnvelope.boundary.empty (),
                                    "cancelled envelope computation does not publish partial geometry"))
            return rc;
    }

    // ---- invalid joint bounds ----
    {
        StateStructure::Ptr invalidStateStructure =
            rw::core::ownedPtr (new StateStructure ());
        const rw::models::SerialDevice::Ptr invalidDevice =
            makeGenericSixAxis (*invalidStateStructure);
        std::pair< Q, Q > invalidBounds = invalidDevice->getBounds ();
        invalidBounds.first[0] = std::numeric_limits< double >::quiet_NaN ();
        invalidDevice->setBounds (invalidBounds);
        const AnalysisEnvelopeData invalidEnvelope =
            computeWorkspaceEnvelope (
                invalidDevice.get (), invalidDevice->getEnd (),
                invalidStateStructure->getDefaultState (), config);
        if (const int rc = require (!invalidEnvelope.valid,
                                    "non-finite joint bounds make envelope invalid"))
            return rc;
        if (const int rc = require (invalidEnvelope.boundary.empty (),
                                    "non-finite joint bounds do not publish geometry"))
            return rc;
    }

    return 0;
}

// 子套件 包络渲染布局:验证 computeEnvelopePlotLayout 在小(320x220)与大
// (1400x900)两种画布尺寸下,绘图区、标题、宽/高标签与说明文字矩形都保持
// 在图片范围内,防止缩放时控件溢出。
// 补充说明:布局契约——绘图区/标题/宽高标签/说明文字矩形都必须落在画布内,
// 320x220 与 1400x900 两种尺寸都验证,防止缩放时控件溢出。
static int testWorkspaceEnvelopeRenderingLayout ()
{
    using namespace rws;

    const QSizeF titleSize (260.0, 18.0);
    const QSizeF widthLabelSize (90.0, 18.0);
    const QSizeF heightLabelSize (70.0, 18.0);
    const QSizeF captionSize (300.0, 18.0);

    for (const QSize size : {QSize (320, 220), QSize (1400, 900)}) {
        const QRect area (QPoint (0, 0), size);
        const EnvelopePlotLayout layout = computeEnvelopePlotLayout (
            area, titleSize, widthLabelSize, heightLabelSize, captionSize);
        if (const int rc = require (layout.valid, "envelope layout is valid"))
            return rc;
        if (const int rc = require (area.contains (layout.plotRect.toAlignedRect ()),
                                    "envelope plot rect stays inside image"))
            return rc;
        if (const int rc = require (area.contains (layout.titleRect.toAlignedRect ()),
                                    "envelope title rect stays inside image"))
            return rc;
        if (const int rc = require (area.contains (layout.widthLabelRect.toAlignedRect ()),
                                    "envelope width label rect stays inside image"))
            return rc;
        if (const int rc = require (area.contains (layout.heightLabelRect.toAlignedRect ()),
                                    "envelope height label rect stays inside image"))
            return rc;
        if (const int rc = require (area.contains (layout.captionRect.toAlignedRect ()),
                                    "envelope caption rect stays inside image"))
            return rc;
    }

    return 0;
}

// 子套件 可视化数据:验证从任务点/工作区采样/位姿可达性三类结果转换到
// 可视化点的标量值、位置、标签、tooltip 与代表 Q 保真;投影函数与标量模式
// 支持集(如 PoseReachability 默认 Coverage、拒绝 Condition);视觉汇总的过滤
// 计数;以及图例预留宽度与边距在不同画布宽度下的边界行为。
// 补充说明:三类结果 -> 可视化点的映射逐字段验证——task 用 Condition 标量(42)、
// workspace 用 Collision 标量(1)并保留采样 Q、pose 用 Coverage 标量(0.3)并带
// 代表 Q/方向索引 tooltip;projectVisualPoint 的 XZ 投影;
// 标量模式支持集(PoseReachability 拒绝 Condition、默认 Coverage);
// 视觉汇总的过滤计数;图例预留宽度:宽画布右缘少 128px、窄画布不预留隐藏图例但
// 保留左右/底部边距;P8 tooltip 关键内容断言。
static int testVisualizationData ()
{
    using namespace rws;

    TaskPointReachabilityResult task;
    task.status = AnalysisStatus::Warning;
    task.taskPoint.id = "TP_A";
    task.taskPoint.name = "Task A";
    task.taskPoint.position = {{1.0, 2.0, 3.0}};
    task.failureReasons.push_back (KinematicFailureReason::NearJointLimit);
    task.ik.rawCandidateCount = 3;
    task.ik.usableSolutionCount = 1;
    KinematicIkSolution taskSolution;
    taskSolution.status = AnalysisStatus::Warning;
    taskSolution.manipulability = 0.25;
    taskSolution.conditionNumber = 42.0;
    taskSolution.minJointLimitMargin = 0.08;
    taskSolution.positionErrorMeters = 0.0005;
    taskSolution.orientationErrorDeg = 0.2;
    taskSolution.inCollision = false;
    taskSolution.q = std::vector< double > {0.1, 0.2, 0.3};
    task.ik.solutions.push_back (taskSolution);

    const AnalysisVisualData taskData = visualDataFromTaskPointResults (
        std::vector< TaskPointReachabilityResult > {task},
        VisualScalarMode::Condition);
    if (const int rc = require (taskData.points.size () == 1,
                                "task result creates one visual point"))
        return rc;
    if (const int rc = require (taskData.hasFiniteScalar,
                                "task visual data has finite scalar"))
        return rc;
    if (const int rc = assertNear (taskData.points[0].position[0], 1.0, 1e-12,
                                   "task visual x"))
        return rc;
    if (const int rc = assertNear (taskData.points[0].scalar, 42.0, 1e-12,
                                   "task condition scalar"))
        return rc;
    if (const int rc = require (taskData.points[0].label == QStringLiteral ("TP_A"),
                                "task label uses id"))
        return rc;
    if (const int rc = require (taskData.points[0].hasQ,
                                "task visual point carries best usable Q"))
        return rc;
    if (const int rc = require (taskData.points[0].q == taskSolution.q,
                                "task visual point Q matches best usable solution"))
        return rc;
    if (const int rc = require (taskData.points[0].tooltip.contains (QStringLiteral ("NearJointLimit")),
                                "task tooltip contains failure reason"))
        return rc;

    WorkspaceSample workspace;
    workspace.tcpPosition = {{-1.0, 0.5, 2.5}};
    workspace.status = AnalysisStatus::Pass;
    workspace.manipulability = 0.75;
    workspace.q = std::vector< double > {-0.1, -0.2, -0.3};
    workspace.conditionNumber = 12.0;
    workspace.minJointLimitMargin = 0.2;
    workspace.inCollision = true;
    const AnalysisVisualData workspaceData = visualDataFromWorkspaceSamples (
        std::vector< WorkspaceSample > {workspace},
        VisualScalarMode::Collision);
    if (const int rc = require (workspaceData.points.size () == 1,
                                "workspace creates one visual point"))
        return rc;
    if (const int rc = require (workspaceData.points[0].inCollision,
                                "workspace collision flag is preserved"))
        return rc;
    if (const int rc = assertNear (workspaceData.points[0].scalar, 1.0, 1e-12,
                                   "workspace collision scalar"))
        return rc;
    if (const int rc = require (workspaceData.points[0].hasQ,
                                "workspace visual point carries sampled Q"))
        return rc;
    if (const int rc = require (workspaceData.points[0].q == workspace.q,
                                "workspace visual point Q matches workspace sample"))
        return rc;

    PoseReachabilitySample pose;
    pose.position = {{4.0, 5.0, 6.0}};
    pose.status = AnalysisStatus::Fail;
    pose.sampledDirections = 10;
    pose.reachableDirections = 3;
    pose.coverage = 0.3;
    pose.hasRepresentativeQ = true;
    pose.representativeQ = std::vector< double > {0.4, 0.5, 0.6};
    pose.representativeDirectionIndex = 2;
    pose.representativeRollIndex = 1;
    const AnalysisVisualData poseData = visualDataFromPoseReachabilitySamples (
        std::vector< PoseReachabilitySample > {pose},
        VisualScalarMode::Coverage);
    if (const int rc = require (poseData.points.size () == 1,
                                "pose reachability creates one visual point"))
        return rc;
    if (const int rc = assertNear (poseData.points[0].scalar, 0.3, 1e-12,
                                   "pose coverage scalar"))
        return rc;
    if (const int rc = require (poseData.points[0].hasQ,
                                "pose visual point carries representative Q"))
        return rc;
    if (const int rc = require (poseData.points[0].q == pose.representativeQ,
                                "pose visual point Q matches representative Q"))
        return rc;
    if (const int rc = require (
            poseData.points[0].tooltip.contains (QStringLiteral ("Representative direction: 2")),
            "pose tooltip includes representative direction"))
        return rc;

    const QPointF projected = projectVisualPoint (poseData.points[0], VisualProjection::XZ);
    if (const int rc = assertNear (projected.x (), 4.0, 1e-12, "XZ projection x"))
        return rc;
    if (const int rc = assertNear (projected.y (), 6.0, 1e-12, "XZ projection z"))
        return rc;

    {
        const std::vector< rws::VisualScalarMode > taskModes =
            rws::supportedVisualScalarModes (rws::VisualPointSource::TaskPoint);
        if (const int rc = require (
                std::find (taskModes.begin (), taskModes.end (),
                           rws::VisualScalarMode::PositionError) != taskModes.end (),
                "task visualization supports position error"))
            return rc;
        if (const int rc = require (
                rws::visualScalarModeSupported (
                    rws::VisualPointSource::Workspace,
                    rws::VisualScalarMode::Condition),
                "workspace visualization supports condition"))
            return rc;
        if (const int rc = require (
                !rws::visualScalarModeSupported (
                    rws::VisualPointSource::PoseReachability,
                    rws::VisualScalarMode::Condition),
                "pose visualization rejects condition scalar"))
            return rc;
        if (const int rc = require (
                rws::defaultVisualScalarModeForSource (
                    rws::VisualPointSource::PoseReachability) ==
                    rws::VisualScalarMode::Coverage,
                "pose visualization defaults to coverage"))
            return rc;
    }

    {
        rws::AnalysisVisualData mixed;
        mixed.points = taskData.points;
        mixed.points.push_back (workspaceData.points[0]);
        mixed.points.push_back (poseData.points[0]);

        rws::AnalysisVisualFilters filters;
        filters.showWarning = false;
        const rws::AnalysisVisualStatusSummary summary =
            rws::summarizeVisualData (mixed, filters);
        if (const int rc = require (summary.totalCount == 3,
                                    "visual summary total count"))
            return rc;
        if (const int rc = require (summary.visibleCount == 2,
                                    "visual summary respects warning filter"))
            return rc;
        if (const int rc = require (summary.collisionCount == 1,
                                    "visual summary collision count"))
            return rc;
    }

    {
        rws::AnalysisVisualFilters filters;
        const rws::AnalysisVisualBounds bounds =
            rws::projectedVisualBounds (poseData, rws::VisualProjection::YZ,
                                        filters);
        const QRect wideArea (0, 0, 720, 420);
        const QRectF wideWithLegend =
            rws::visualPlotArea (wideArea, true);
        const QRectF wideWithoutLegend =
            rws::visualPlotArea (wideArea, false);
        if (const int rc = assertNear (
                wideWithLegend.left (), wideWithoutLegend.left (), 1e-12,
                "visual plot left is independent of legend"))
            return rc;
        if (const int rc = assertNear (
                wideWithLegend.right (), wideWithoutLegend.right () - 128.0,
                1e-12, "visual plot reserves legend width"))
            return rc;

        const QRect narrowArea (0, 0, 400, 300);
        const QRectF narrowWithLegend =
            rws::visualPlotArea (narrowArea, true);
        const QRectF narrowWithoutLegend =
            rws::visualPlotArea (narrowArea, false);
        if (const int rc = require (
                narrowWithoutLegend.left () >= 40.0,
                "narrow visual plot reserves left margin for labels"))
            return rc;
        if (const int rc = require (
                narrowWithoutLegend.bottom () <= 256.0,
                "narrow visual plot reserves bottom margin for axis labels"))
            return rc;
        if (const int rc = assertNear (
                narrowWithLegend.right (), narrowWithoutLegend.right (), 1e-12,
                "narrow visual plot does not reserve hidden legend width"))
            return rc;

        // P8:tooltip content assertions
        if (const int rc = require (
                taskData.points[0].tooltip.contains (QStringLiteral ("Position:")),
                "task tooltip contains position"))
            return rc;
        if (const int rc = require (
                taskData.points[0].tooltip.contains (QStringLiteral ("Scalar:")),
                "task tooltip contains scalar"))
            return rc;
        if (const int rc = require (
                workspaceData.points[0].tooltip.contains (QStringLiteral ("TCP")),
                "workspace tooltip identifies tcp"))
            return rc;
        if (const int rc = require (
                poseData.points[0].tooltip.contains (QStringLiteral ("Reachable: 3 / 10")),
                "pose tooltip preserves reachable ratio"))
            return rc;

        if (const int rc = require (bounds.valid, "visual bounds valid"))
            return rc;
        if (const int rc = assertNear (bounds.minX, 5.0, 1e-12,
                                       "visual YZ bounds x"))
            return rc;
        if (const int rc = assertNear (bounds.minY, 6.0, 1e-12,
                                       "visual YZ bounds y"))
            return rc;
    }

    return 0;
}

// 子套件 JSON 与碰撞辅助:验证 jsonValueFromDouble 对有限值保持数值、对正负无穷
// 与 NaN 分别导出为 "inf"/"-inf"/"nan" 字符串(JSON 无法表示非有限数的约定);
// NULL workcell 不创建碰撞检测器;包络数据类型/渲染数据类型的尺寸计算与
// VisualRenderMode 文案。
// 补充说明:jsonValueFromDouble 对有限值保留数值、对 inf/-inf/NaN 分别导出为
// "inf"/"-inf"/"nan" 字符串(JSON 无法表示非有限数的约定);
// NULL workcell 不得创建碰撞检测器;
// 包络多边形宽/高计算与渲染模式文案的稳定输出。
static int testJsonAndCollisionHelpers ()
{
    {
        const QJsonValue finite = rws::jsonValueFromDouble (3.25);
        if (!finite.isDouble ())
            return fail ("finite json value should remain numeric");
        if (const int rc = assertNear (finite.toDouble (), 3.25, 1e-12,
                                       "finite json value"))
            return rc;
    }
    {
        const QJsonValue posInf = rws::jsonValueFromDouble (
            std::numeric_limits< double >::infinity ());
        if (!posInf.isString () || posInf.toString () != QStringLiteral ("inf"))
            return fail ("positive infinity should export as string inf");
    }
    {
        const QJsonValue negInf = rws::jsonValueFromDouble (
            -std::numeric_limits< double >::infinity ());
        if (!negInf.isString () || negInf.toString () != QStringLiteral ("-inf"))
            return fail ("negative infinity should export as string -inf");
    }
    {
        const QJsonValue nanValue = rws::jsonValueFromDouble (
            std::numeric_limits< double >::quiet_NaN ());
        if (!nanValue.isString () || nanValue.toString () != QStringLiteral ("nan"))
            return fail ("NaN should export as string nan");
    }
    {
        const rw::core::Ptr< rw::proximity::CollisionDetector > detector =
            rws::makeKinematicAnalysisCollisionDetector (NULL);
        if (detector != NULL)
            return fail ("null workcell should not create a collision detector");
    }

    // ---- envelope data types (Task 1) ----
    {
        rws::AnalysisEnvelopeData envelope;
        envelope.projection = rws::VisualProjection::XY;
        envelope.boundary.push_back (QPointF (-1.0, -2.0));
        envelope.boundary.push_back (QPointF (3.0, -2.0));
        envelope.boundary.push_back (QPointF (3.0, 4.0));
        envelope.boundary.push_back (QPointF (-1.0, 4.0));
        rws::updateEnvelopeDimensions (envelope);

        if (const int rc = require (envelope.valid, "envelope dimensions mark polygon valid"))
            return rc;
        if (const int rc = assertNear (envelope.width, 4.0, 1e-12, "envelope width"))
            return rc;
        if (const int rc = assertNear (envelope.height, 6.0, 1e-12, "envelope height"))
            return rc;
        if (const int rc = require (
                rws::visualRenderModeText (rws::VisualRenderMode::Envelope) ==
                    QStringLiteral ("Approximate outer envelope"),
                "visual render mode text"))
            return rc;
    }

    // ---- envelope render data (Task 3) ----
    {
        rws::AnalysisVisualData envelopeVisual;
        envelopeVisual.renderMode = rws::VisualRenderMode::Envelope;
        envelopeVisual.envelope.projection = rws::VisualProjection::XZ;
        envelopeVisual.envelope.boundary = {
            QPointF (-2.0, 0.0),
            QPointF (0.0, 2.0),
            QPointF (2.0, 0.0),
            QPointF (0.0, -1.0)
        };
        rws::updateEnvelopeDimensions (envelopeVisual.envelope);
        if (const int rc = require (
                rws::visualRenderModeText (envelopeVisual.renderMode) ==
                    QStringLiteral ("Approximate outer envelope"),
                "envelope render mode is selected"))
            return rc;
        if (const int rc = assertNear (
                envelopeVisual.envelope.width, 4.0, 1e-12,
                "envelope render width"))
            return rc;
    }
    return 0;
}

// 子套件 托管项目门禁:构造一个只有源文件与模型文件、尚未生成托管 WorkCell 的
// 机器人项目(通过真实 RobWorkStudio + ProjectManager + 项目文档提供器装载),
// 点击"导入冻结需求"按钮时,必须显示明确的 WorkCell 就绪门禁文案
// ("Review the model ... Save and Load first"),禁止在模型未发布前进入分析。
// 该用例需独立运行(需 QApplication 与完整 Studio 栈)。
// 补充说明:通过 ProjectManager 创建一个只有源(URDF)与模型资源、没有托管 WorkCell
// 的项目(RobotDraft),注册模型文档提供器并打开;此时主 WorkCell 资源为空,
// 点击"导入冻结需求"必须给出明确的就绪门禁文案(要求先在 RobotModelBuilder
// 中 Save and Load),禁止在模型未发布前进入分析。该用例需真实 RobWorkStudio 栈。
static int testManagedRobotProjectRequiresPublishedWorkCell ()
{
    QTemporaryDir directory;
    if (!directory.isValid ())
        return fail ("could not create managed project gate fixture");
    const QString projectPath = directory.filePath ("RobotDraft/RobotDraft.rwproj");
    rws::ProjectManifest manifest;
    manifest.project.id = QStringLiteral ("robot-draft");
    manifest.project.name = QStringLiteral ("RobotDraft");
    rws::ProjectResource source;
    source.id = QStringLiteral ("robot-source.main");
    source.kind = QStringLiteral ("robwork.passive-asset");
    source.path = QStringLiteral ("sources/robot.urdf");
    source.ownership = QStringLiteral ("project");
    source.required = false;
    rws::ProjectResource model;
    model.id = QStringLiteral ("robot-model.main");
    model.kind = QStringLiteral ("robwork.robot-model");
    model.path = QStringLiteral ("generated/robot-models/robot.rmb.json");
    model.ownership = QStringLiteral ("generated");
    model.required = false;
    model.dependencies = {source.id};
    manifest.resources = {source, model};
    manifest.entryPoints.insert (QStringLiteral ("robotSource"), source.id);
    QString error;
    rws::ProjectManager manager;
    if (!manager.createProject (projectPath, manifest, &error))
        return fail ("could not create managed project gate fixture: " + error.toStdString ());
    manager.closeProject ();

    const QString projectRoot = QFileInfo (projectPath).absolutePath ();
    QDir ().mkpath (QFileInfo (QDir (projectRoot).filePath (source.path)).absolutePath ());
    QFile sourceFile (QDir (projectRoot).filePath (source.path));
    if (!sourceFile.open (QIODevice::WriteOnly) ||
        sourceFile.write ("<robot name=\"RobotDraft\"/>") <= 0)
        return fail ("could not write managed project source fixture");
    sourceFile.close ();
    QDir ().mkpath (QFileInfo (QDir (projectRoot).filePath (model.path)).absolutePath ());
    QFile modelFile (QDir (projectRoot).filePath (model.path));
    if (!modelFile.open (QIODevice::WriteOnly) || modelFile.write ("{}") != 2)
        return fail ("could not write managed project model fixture");
    modelFile.close ();

    rw::core::PropertyMap properties;
    rws::RobWorkStudio studio (properties);
    rws::CallbackProjectDocumentProvider modelProvider (
        QStringLiteral ("test.robot-model"), QStringLiteral ("robwork.robot-model"),
        [] (const QString&, const rws::ProjectDocumentContext&, QString*) { return true; },
        [] (const QString&, const rws::ProjectDocumentContext&, QString*) { return true; });
    if (!studio.registerProjectDocumentProvider (&modelProvider, &error))
        return fail ("could not register managed project model fixture provider");
    studio.openFile (projectPath.toStdString ());
    if (studio.projectDirectory ().isEmpty () || !studio.mainWorkCellResourceId ().isEmpty ())
        return fail ("managed project gate fixture did not open without mainWorkCell");

    rw::models::WorkCell::Ptr placeholder =
        rw::core::ownedPtr (new rw::models::WorkCell ("RobotDraft"));
    rws::KinematicAnalysisWidget widget;
    widget.setRobWorkStudio (&studio);
    widget.setWorkCell (placeholder.get ());
    QPushButton* import =
        widget.findChild< QPushButton* > (QStringLiteral ("importFrozenRequirementsButton"));
    if (import == nullptr)
        return fail ("managed frozen requirement import button was not found");
    import->click ();
    const QString expected = QStringLiteral (
        "The robot project has not generated its managed WorkCell. Review the model in "
        "RobotModelBuilder and run Save and Load first.");
    if (widget.statusMessage () != expected)
        return fail ("kinematic import did not report the managed WorkCell readiness gate");
    studio.close ();
    return 0;
}

// 子套件 工作流 UI 状态机:驱动 KinematicAnalysisWidget 的三个页签
// (Diagnose / Validate Requirements / Explore Capability),验证:
//   - WorkCell/设备/TCP 任一缺失时各命令按钮按预期禁用并给出可操作状态文案;
//   - 未加载冻结工件时 Validate 仅允许 Load;加载 v4 执行契约后允许 Run,
//     Must/Should 结果表与区域单元格展示 Feasibility/Quality/EvidenceStage(Verified);
//   - 旧 v3 工件被拒绝并报告 REQ_V3_REQUIRES_REFREEZE;
//   - Explore 运行/取消期间的状态迁移,取消完成后报告部分采样 DataInsufficient,
//     修改采样设置重新武装 Run;卸载 WorkCell 时所有命令安全禁用。
// 补充说明:这是最长的 UI 集成用例,分几个阶段:
//   (1) Header Report 菜单动作与过滤状态保持;
//   (2) 三模式外壳(Diagnose / Validate / Explore)互斥切换与窄窗口(300px/320px)布局;
//   (3) Diagnose 页:IK 输入一列排布、健康框不溢出、设备/TCP 选择可用;
//   (4) IK Solve 后候选/详情表、同步当前 TCP、编辑目标使结果 stale 并禁用 Apply;
//   (5) Local Task Points 模型路由、Validate Selected 只更新选中稳定 ID;
//   (6) Validate 页:无冻结工件时仅 Load 可用;v3 拒绝并报 REQ_V3_REQUIRES_REFREEZE;
//       v4 执行契约加载后 Run 可用,Must/Should 任务结果与区域单元格显示
//       Feasibility/Quality/Verified;选中冻结任务/区域替换全量结果;
//   (7) Explore 页:Run/Cancel 状态机、取消后 DataInsufficient、改设置重新武装 Run;
//   (8) 卸载 WorkCell 时所有命令安全禁用。
static int testWorkflowUiStates ()
{
    rws::KinematicAnalysisWidget widget;

    QPushButton* reportButton = widget.findChild< QPushButton* > (
        QStringLiteral ("reportButton"));
    if (const int rc = require (reportButton != nullptr && reportButton->menu () != nullptr &&
                                    reportButton->isEnabled (),
                                "Header Report is an enabled text menu"))
        return rc;
    const QList< QAction* > reportActions = reportButton->menu ()->actions ();
    if (const int rc = require (reportActions.size () == 4 &&
                                    reportActions.at (0)->text () == QStringLiteral ("Refresh report") &&
                                    reportActions.at (1)->text () == QStringLiteral ("Export JSON") &&
                                    reportActions.at (2)->text () == QStringLiteral ("Export summary CSV") &&
                                    reportActions.at (3)->text () == QStringLiteral ("Export task-results CSV"),
                                "Header Report menu exposes every report action"))
        return rc;
    QComboBox* reportStageFilter = widget.findChild< QComboBox* > (
        QStringLiteral ("reportStageFilter"));
    if (const int rc = require (reportStageFilter != nullptr,
                                "report filter state remains available to Header actions"))
        return rc;
    reportStageFilter->setCurrentIndex (2);
    reportActions.at (0)->trigger ();
    if (const int rc = require (reportStageFilter->currentIndex () == 2,
                                "refreshing through Header Report retains filtering state"))
        return rc;

    QWidget* modeSelector =
        widget.findChild< QWidget* > (QStringLiteral ("kinematicModeSelector"));
    QStackedWidget* modeStack =
        widget.findChild< QStackedWidget* > (QStringLiteral ("kinematicModeStack"));
    if (const int rc = require (modeSelector != nullptr && modeStack != nullptr,
                                "three-mode shell exposes stable object names"))
        return rc;
    const QList< QToolButton* > modeButtons = modeSelector->findChildren< QToolButton* > ();
    if (const int rc = require (modeButtons.size () == 3 && modeStack->count () == 3,
                                "three exclusive modes control exactly three stack pages"))
        return rc;
    for (int index = 0; index < modeButtons.size (); ++index) {
        QToolButton* button = modeButtons.at (index);
        if (const int rc = require (button->isCheckable () && !button->text ().isEmpty (),
                                    "mode selectors are checkable text buttons"))
            return rc;
        button->click ();
        QCoreApplication::processEvents ();
        int checkedCount = 0;
        for (QToolButton* candidate : modeButtons)
            checkedCount += candidate->isChecked () ? 1 : 0;
        if (const int rc = require (checkedCount == 1 && modeStack->currentIndex () == index,
                                    "mode selector is exclusive and changes the visible page"))
            return rc;
    }
    const QStringList modeLabels = {QStringLiteral ("Diagnose"),
                                    QStringLiteral ("Validate"),
                                    QStringLiteral ("Explore")};
    const QStringList modeDescriptions = {QStringLiteral ("Diagnose"),
                                          QStringLiteral ("Validate Requirements"),
                                          QStringLiteral ("Explore Capability")};
    for (int index = 0; index < modeButtons.size (); ++index) {
        if (const int rc = require (
                modeButtons.at (index)->text () == modeLabels.at (index) &&
                    modeButtons.at (index)->toolTip () == modeDescriptions.at (index) &&
                    modeButtons.at (index)->accessibleName () == modeDescriptions.at (index),
                "narrow mode selectors keep short labels with full accessible descriptions"))
            return rc;
    }
    if (const int rc = require (
            widget.findChild< QTabWidget* > (QStringLiteral ("workflowTabs")) == nullptr &&
                widget.findChildren< QTabWidget* > ().isEmpty (),
            "workflow and compatibility tabs are absent from the visible hierarchy"))
        return rc;

    QPushButton* diagnoseRefresh = widget.findChild< QPushButton* > (
        QStringLiteral ("diagnoseRefreshButton"));
    QComboBox* deviceCombo = widget.findChild< QComboBox* > (
        QStringLiteral ("deviceCombo"));
    QComboBox* tcpCombo = widget.findChild< QComboBox* > (
        QStringLiteral ("tcpFrameCombo"));
    QComboBox* lengthUnitCombo = widget.findChild< QComboBox* > (
        QStringLiteral ("lengthUnitCombo"));
    QComboBox* angleUnitCombo = widget.findChild< QComboBox* > (
        QStringLiteral ("angleUnitCombo"));
    QPushButton* thresholdSettings = widget.findChild< QPushButton* > (
        QStringLiteral ("thresholdSettingsButton"));
    QPushButton* report = widget.findChild< QPushButton* > (QStringLiteral ("reportButton"));
    QLineEdit* status = widget.findChild< QLineEdit* > (QStringLiteral ("kinematicStatus"));
    if (const int rc = require (
            diagnoseRefresh != nullptr && deviceCombo != nullptr && tcpCombo != nullptr &&
                lengthUnitCombo != nullptr && angleUnitCombo != nullptr &&
                thresholdSettings != nullptr && report != nullptr && status != nullptr,
            "fixed shell controls expose stable object names"))
        return rc;

    QWidget* diagnosePage = modeStack->widget (0)->findChild< QWidget* > (
        QStringLiteral ("diagnoseWorkflowPage"));
    QWidget* currentPosePage = widget.findChild< QWidget* > (
        QStringLiteral ("currentPoseTab"));
    QWidget* ikPage = widget.findChild< QWidget* > (QStringLiteral ("ikTab"));
    QFrame* healthFrame = widget.findChild< QFrame* > (
        QStringLiteral ("currentPoseHealthFrame"));
    QDoubleSpinBox* ikX = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikXSpin"));
    QDoubleSpinBox* ikY = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikYSpin"));
    QDoubleSpinBox* ikZ = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikZSpin"));
    QDoubleSpinBox* ikRoll = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikRollSpin"));
    QDoubleSpinBox* ikPitch = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikPitchSpin"));
    QDoubleSpinBox* ikYaw = widget.findChild< QDoubleSpinBox* > (QStringLiteral ("ikYawSpin"));
    QPushButton* ikSync = widget.findChild< QPushButton* > (
        QStringLiteral ("ikSyncCurrentTcpButton"));
    QPushButton* ikSolve = widget.findChild< QPushButton* > (QStringLiteral ("ikSolveButton"));
    QPushButton* ikApply = widget.findChild< QPushButton* > (QStringLiteral ("ikApplyButton"));
    QCheckBox* ikCollision = widget.findChild< QCheckBox* > (
        QStringLiteral ("ikCollisionCheck"));
    QTableWidget* ikCandidates = widget.findChild< QTableWidget* > (
        QStringLiteral ("ikSolutionTable"));
    QTableWidget* ikDetails = widget.findChild< QTableWidget* > (
        QStringLiteral ("ikDetailTable"));
    QToolButton* diagnostics = widget.findChild< QToolButton* > (
        QStringLiteral ("advancedDiagnosticsToggle"));
    if (const int rc = require (
            diagnosePage != nullptr && currentPosePage != nullptr && ikPage != nullptr &&
                healthFrame != nullptr && ikX != nullptr && ikY != nullptr && ikZ != nullptr &&
                ikRoll != nullptr && ikPitch != nullptr && ikYaw != nullptr && ikSync != nullptr &&
                ikSolve != nullptr && ikApply != nullptr && ikCollision != nullptr &&
                ikCandidates != nullptr &&
                ikDetails != nullptr && diagnostics != nullptr,
            "Diagnose owns current pose health and IK controls"))
        return rc;
    if (const int rc = require (
            currentPosePage->parentWidget () == diagnosePage && ikPage->parentWidget () == diagnosePage,
            "current pose and IK pages share the Diagnose page"))
        return rc;
    if (const int rc = require (
            ikCandidates->maximumHeight () > 0 && ikDetails->maximumHeight () > 0 &&
                !diagnostics->isChecked (),
            "IK tables are bounded and advanced diagnostics starts collapsed"))
        return rc;

    widget.resize (300, 620);
    widget.show ();
    modeButtons.at (0)->click ();
    QCoreApplication::processEvents ();
    if (const int rc = require (widget.width () == 300,
                                "narrow-width checks run on a 300px dock"))
        return rc;
    const QList< QDoubleSpinBox* > targetSpins = {ikX, ikY, ikZ, ikRoll, ikPitch, ikYaw};
    for (int index = 1; index < targetSpins.size (); ++index) {
        const QRect previous = targetSpins.at (index - 1)->geometry ();
        const QRect current = targetSpins.at (index)->geometry ();
        if (const int rc = require (
                current.top () > previous.top () &&
                    std::abs (current.left () - previous.left ()) <= 1,
                "IK target controls use one vertical column"))
            return rc;
    }
    for (QDoubleSpinBox* targetSpin : targetSpins) {
        if (const int rc = require (
                targetSpin->geometry ().left () >= 0 &&
                    targetSpin->geometry ().right () < ikPage->width (),
                "IK target controls do not overflow a 300px Diagnose page"))
            return rc;
    }
    if (const int rc = require (healthFrame->geometry ().left () >= 0 &&
                                    healthFrame->geometry ().right () < currentPosePage->width (),
                                "current-state health frame does not overflow a 300px Diagnose page"))
        return rc;
    if (const int rc = require (deviceCombo->width () >= 24 && tcpCombo->width () >= 24,
                                "Device and TCP remain usable in a 300px header"))
        return rc;
    const QList< QWidget* > fixedControls = {
        deviceCombo, tcpCombo, lengthUnitCombo, angleUnitCombo, diagnoseRefresh,
        thresholdSettings, report, status, modeSelector};
    for (QToolButton* button : modeButtons) {
        button->click ();
        QCoreApplication::processEvents ();
        for (QWidget* control : fixedControls) {
            const QPoint topLeft = widget.mapFromGlobal (control->mapToGlobal (QPoint (0, 0)));
            const QRect geometry (topLeft, control->size ());
            if (const int rc = require (!geometry.isEmpty () && widget.rect ().contains (geometry),
                                        "fixed shell controls fit inside a 300x620 dock"))
                return rc;
        }
        const QPoint modeTopLeft = widget.mapFromGlobal (
            button->mapToGlobal (QPoint (0, 0)));
        if (const int rc = require (!button->geometry ().isEmpty () &&
                                        widget.rect ().contains (QRect (modeTopLeft, button->size ())),
                                    "every mode selector fits inside a 300px dock"))
            return rc;
    }
    if (const int rc = require (
            lengthUnitCombo->minimumWidth () == 0 && angleUnitCombo->minimumWidth () == 0 &&
                lengthUnitCombo->sizePolicy ().horizontalPolicy () == QSizePolicy::Ignored &&
                angleUnitCombo->sizePolicy ().horizontalPolicy () == QSizePolicy::Ignored,
            "unit controls can shrink inside the fixed two-row header"))
        return rc;
    widget.resize (320, 620);
    widget.show ();
    QCoreApplication::processEvents ();
    for (QToolButton* button : modeButtons) {
        button->click ();
        QCoreApplication::processEvents ();
        for (QWidget* control : fixedControls) {
            const QPoint topLeft = widget.mapFromGlobal (control->mapToGlobal (QPoint (0, 0)));
            if (const int rc = require (!control->geometry ().isEmpty () &&
                                            widget.rect ().contains (QRect (topLeft, control->size ())),
                                        "fixed shell controls fit inside a 320x620 dock"))
                return rc;
        }
        const QPoint modeTopLeft = widget.mapFromGlobal (
            button->mapToGlobal (QPoint (0, 0)));
        if (const int rc = require (!button->geometry ().isEmpty () &&
                                        widget.rect ().contains (QRect (modeTopLeft, button->size ())),
                                    "every mode selector fits inside a 320px dock"))
            return rc;
    }

    QPushButton* validateLoad = widget.findChild< QPushButton* > (
        QStringLiteral ("validateLoadRequirementsButton"));
    QPushButton* validateRun = widget.findChild< QPushButton* > (
        QStringLiteral ("validateRunButton"));
    QPushButton* validateExport = widget.findChild< QPushButton* > (
        QStringLiteral ("validateExportButton"));
    QLabel* validateState = widget.findChild< QLabel* > (
        QStringLiteral ("validateRequirementStateLabel"));
    QPushButton* exploreRun = widget.findChild< QPushButton* > (
        QStringLiteral ("exploreRunButton"));
    QPushButton* exploreCancel = widget.findChild< QPushButton* > (
        QStringLiteral ("exploreCancelButton"));
    QSpinBox* exploreSamples = widget.findChild< QSpinBox* > (
        QStringLiteral ("exploreSamplesSpin"));
    QLabel* exploreState = widget.findChild< QLabel* > (
        QStringLiteral ("exploreStateLabel"));
    QComboBox* exploreMode = widget.findChild< QComboBox* > (
        QStringLiteral ("exploreModeCombo"));
    QSpinBox* exploreSeed = widget.findChild< QSpinBox* > (
        QStringLiteral ("exploreSeedSpin"));
    QSpinBox* exploreGrid = widget.findChild< QSpinBox* > (
        QStringLiteral ("exploreGridStepsSpin"));
    QSpinBox* exploreDirections = widget.findChild< QSpinBox* > (
        QStringLiteral ("exploreDirectionSamplesSpin"));
    QSpinBox* exploreRolls = widget.findChild< QSpinBox* > (
        QStringLiteral ("exploreRollSamplesSpin"));
    QLabel* exploreSamplesLabel = widget.findChild< QLabel* > (
        QStringLiteral ("exploreSamplesLabel"));
    QLabel* exploreSeedLabel = widget.findChild< QLabel* > (
        QStringLiteral ("exploreSeedLabel"));
    QLabel* exploreGridLabel = widget.findChild< QLabel* > (
        QStringLiteral ("exploreGridStepsLabel"));
    QLabel* exploreDirectionsLabel = widget.findChild< QLabel* > (
        QStringLiteral ("exploreDirectionsLabel"));
    QLabel* exploreRollsLabel = widget.findChild< QLabel* > (
        QStringLiteral ("exploreRollsLabel"));
    if (const int rc = require (
            exploreMode != nullptr && exploreSamplesLabel != nullptr &&
                exploreSeed != nullptr && exploreSeedLabel != nullptr &&
                exploreGrid != nullptr && exploreGridLabel != nullptr &&
                exploreDirections != nullptr && exploreDirectionsLabel != nullptr &&
                exploreRolls != nullptr && exploreRollsLabel != nullptr,
            "Explore Capability exposes sampling and orientation controls"))
        return rc;
    QComboBox* exploreSamplingMode = exploreMode;
    QWidget* workspacePage = widget.findChild< QWidget* > (QStringLiteral ("workspaceTab"));
    QWidget* poseReachPage = widget.findChild< QWidget* > (QStringLiteral ("poseReachTab"));
    QPushButton* legacyWorkspaceRun = widget.findChild< QPushButton* > (
        QStringLiteral ("workspaceRunButton"));
    QPushButton* legacyWorkspaceCancel = widget.findChild< QPushButton* > (
        QStringLiteral ("workspaceCancelButton"));
    QComboBox* legacyWorkspaceMode = widget.findChild< QComboBox* > (
        QStringLiteral ("workspaceModeCombo"));
    QPushButton* legacyPoseRun = widget.findChild< QPushButton* > (
        QStringLiteral ("poseRunButton"));
    QPushButton* legacyPoseCancel = widget.findChild< QPushButton* > (
        QStringLiteral ("poseCancelButton"));
    QSpinBox* legacyPoseDirections = widget.findChild< QSpinBox* > (
        QStringLiteral ("poseDirectionSamplesSpin"));
    QSpinBox* legacyPoseRolls = widget.findChild< QSpinBox* > (
        QStringLiteral ("poseRollSamplesSpin"));
    rws::KinematicAnalysisPlotWidget* embeddedPlot =
        widget.findChild< rws::KinematicAnalysisPlotWidget* > ();
    QPushButton* openPlot = widget.findChild< QPushButton* > (
        QStringLiteral ("visualizationOpenPlotButton"));
    if (const int rc = require (
            exploreSamplingMode->count () == 3 &&
                exploreSamplingMode->itemText (0) == QStringLiteral ("Random") &&
                exploreSamplingMode->itemData (0).toInt () == 0 &&
                exploreSamplingMode->itemText (1) == QStringLiteral ("Joint Grid") &&
                exploreSamplingMode->itemData (1).toInt () == 1 &&
                exploreSamplingMode->itemText (2) == QStringLiteral ("Pose Reachability") &&
                exploreSamplingMode->itemData (2).toInt () == 2 &&
                workspacePage != nullptr && poseReachPage != nullptr && embeddedPlot != nullptr &&
                openPlot != nullptr && legacyWorkspaceRun != nullptr &&
                legacyWorkspaceCancel != nullptr && legacyWorkspaceMode != nullptr &&
                legacyPoseRun != nullptr && legacyPoseCancel != nullptr &&
                legacyPoseDirections != nullptr && legacyPoseRolls != nullptr &&
                embeddedPlot->maximumHeight () == 160,
            "Explore exposes Random, Grid and Pose Reachability with compact visualization"))
        return rc;
    modeButtons.at (2)->click ();
    exploreSamplingMode->setCurrentIndex (0);
    if (const int rc = require (
            exploreSamples->isVisible () && exploreSamplesLabel->isVisible () &&
                exploreSeed->isVisible () && exploreSeedLabel->isVisible () &&
                !exploreGrid->isVisible () && !exploreGridLabel->isVisible () &&
                !exploreDirections->isVisible () && !exploreDirectionsLabel->isVisible () &&
                !exploreRolls->isVisible () && !exploreRollsLabel->isVisible () &&
                workspacePage->isVisible () && !poseReachPage->isVisible (),
            "Random mode shows only Samples and Seed workspace parameters"))
        return rc;
    if (const int rc = require (
            !legacyWorkspaceRun->isVisible () && !legacyWorkspaceCancel->isVisible () &&
                !legacyWorkspaceMode->isVisible () && !legacyPoseRun->isVisible () &&
                !legacyPoseCancel->isVisible (),
            "Explore keeps the legacy workspace and pose Run, Cancel, and mode entry points hidden"))
        return rc;
    exploreSamplingMode->setCurrentIndex (1);
    if (const int rc = require (
            !exploreSamples->isVisible () && !exploreSamplesLabel->isVisible () &&
                !exploreSeed->isVisible () && !exploreSeedLabel->isVisible () &&
                exploreGrid->isVisible () && exploreGridLabel->isVisible () &&
                !exploreDirections->isVisible () && !exploreDirectionsLabel->isVisible () &&
                !exploreRolls->isVisible () && !exploreRollsLabel->isVisible (),
            "Joint Grid mode shows only Grid Steps workspace parameters"))
        return rc;
    exploreSamplingMode->setCurrentIndex (2);
    if (const int rc = require (
            !exploreSamples->isVisible () && !exploreSamplesLabel->isVisible () &&
                !exploreSeed->isVisible () && !exploreSeedLabel->isVisible () &&
                !exploreGrid->isVisible () && !exploreGridLabel->isVisible () &&
                exploreDirections->isVisible () && exploreDirectionsLabel->isVisible () &&
                exploreRolls->isVisible () && exploreRollsLabel->isVisible () &&
                !workspacePage->isVisible () && poseReachPage->isVisible (),
            "Pose Reachability mode shows only pose orientation parameters"))
        return rc;
    exploreDirections->setValue (37);
    exploreRolls->setValue (5);
    if (const int rc = require (
            legacyPoseDirections->value () == 37 && legacyPoseRolls->value () == 5 &&
                !legacyPoseDirections->isVisible () && !legacyPoseRolls->isVisible (),
            "Pose orientation parameters are synchronized to the existing pipeline without duplicate controls"))
        return rc;
    exploreSamplingMode->setCurrentIndex (0);
    if (const int rc = require (
            exploreState != nullptr &&
                exploreState->text ().contains (QStringLiteral ("Estimated")),
            "Explore Capability labels its evidence as Estimated"))
        return rc;

    QComboBox* mode2Source = widget.findChild< QComboBox* > (
        QStringLiteral ("mode2DataSourceCombo"));
    QWidget* localTasksPage = widget.findChild< QWidget* > (
        QStringLiteral ("localTasksPage"));
    QTableWidget* frozenTasks = widget.findChild< QTableWidget* > (
        QStringLiteral ("validateTaskResultTable"));
    QTableWidget* frozenRegions = widget.findChild< QTableWidget* > (
        QStringLiteral ("validateRegionCellTable"));
    QLabel* taskSummary = widget.findChild< QLabel* > (
        QStringLiteral ("taskPointSummaryLabel"));
    QTableWidget* taskMore = widget.findChild< QTableWidget* > (
        QStringLiteral ("taskPointMoreTable"));
    if (const int rc = require (
            mode2Source != nullptr && mode2Source->count () == 2 &&
                localTasksPage != nullptr && frozenTasks != nullptr && frozenRegions != nullptr &&
                taskSummary != nullptr && taskMore != nullptr &&
                frozenTasks->editTriggers () == QAbstractItemView::NoEditTriggers &&
                frozenRegions->editTriggers () == QAbstractItemView::NoEditTriggers,
            "Mode 2 exposes local and frozen data sources with read-only frozen results"))
        return rc;
    modeButtons.at (1)->click ();
    QCoreApplication::processEvents ();
    mode2Source->setCurrentIndex (0);
    if (const int rc = require (localTasksPage->isVisible () && !frozenTasks->isVisible () &&
                                    !frozenRegions->isVisible (),
                                "Local Tasks source shows editable task data only"))
        return rc;

    QPushButton* mode2Load = widget.findChild< QPushButton* > (
        QStringLiteral ("mode2LoadJsonButton"));
    QPushButton* mode2ValidateAll = widget.findChild< QPushButton* > (
        QStringLiteral ("mode2ValidateAllButton"));
    QPushButton* mode2ValidateSelected = widget.findChild< QPushButton* > (
        QStringLiteral ("mode2ValidateSelectedButton"));
    QPushButton* mode2Add = widget.findChild< QPushButton* > (
        QStringLiteral ("mode2AddButton"));
    QPushButton* mode2Remove = widget.findChild< QPushButton* > (
        QStringLiteral ("mode2RemoveButton"));
    QTableView* localTaskTable = localTasksPage->findChild< QTableView* > ();
    if (const int rc = require (
            mode2Load != nullptr && mode2ValidateAll != nullptr &&
                mode2ValidateSelected != nullptr && mode2Add != nullptr &&
                mode2Remove != nullptr && localTaskTable != nullptr &&
                localTaskTable->model ()->columnCount () == 27 && mode2Add->isEnabled () &&
                !mode2Remove->isEnabled () && localTaskTable->isEnabled (),
            "Local Tasks exposes the compact text toolbar and remains the editable 27-column source"))
        return rc;
    mode2Source->setCurrentIndex (1);
    if (const int rc = require (!localTasksPage->isVisible () && frozenTasks->isVisible () &&
                                    !frozenRegions->isVisible (),
                                "Frozen Requirements keeps cell-level diagnostics collapsed by default"))
        return rc;
    if (const int rc = require (
            !mode2Add->isEnabled () && !mode2Remove->isEnabled () &&
                !localTaskTable->isEnabled () &&
                frozenTasks->editTriggers () == QAbstractItemView::NoEditTriggers &&
                frozenRegions->editTriggers () == QAbstractItemView::NoEditTriggers &&
                frozenTasks->columnCount () <= 6 && frozenRegions->columnCount () <= 9,
            "Frozen Requirements disables local editing and keeps compact read-only task and region summaries"))
        return rc;
    mode2Source->setCurrentIndex (0);

    widget.setWorkCell (nullptr);
    if (const int rc = require (
            !diagnoseRefresh->isEnabled (),
            "no WorkCell disables Diagnose refresh"))
        return rc;
    if (const int rc = require (
            widget.statusMessage ().contains (QStringLiteral ("No WorkCell")),
            "no WorkCell reports an actionable status"))
        return rc;

    rw::models::WorkCell::Ptr noDevice =
        rw::core::ownedPtr (new rw::models::WorkCell ("NoDevice"));
    widget.setWorkCell (noDevice.get ());
    if (const int rc = require (
            !diagnoseRefresh->isEnabled (),
            "no Device disables Diagnose refresh"))
        return rc;
    if (const int rc = require (
            widget.statusMessage ().contains (QStringLiteral ("No device")),
            "no Device reports an actionable status"))
        return rc;

    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "WorkflowUi", ""));
    workcell->addDevice (device);
    widget.setWorkCell (workcell.get ());
    if (const int rc = require (tcpCombo->count () > 0, "valid WorkCell exposes a TCP"))
        return rc;

    tcpCombo->setCurrentIndex (-1);
    const bool refreshedWithoutTcp = QMetaObject::invokeMethod (
        &widget, "refreshWorkflowControls", Qt::DirectConnection);
    if (const int rc = require (refreshedWithoutTcp,
                                "workflow state refresh is invokable"))
        return rc;
    if (const int rc = require (
            !diagnoseRefresh->isEnabled (),
            "no TCP disables Diagnose refresh"))
        return rc;
    if (const int rc = require (
            widget.statusMessage ().contains (QStringLiteral ("TCP")),
            "no TCP reports an actionable status"))
        return rc;

    tcpCombo->setCurrentIndex (0);
    QMetaObject::invokeMethod (&widget, "refreshWorkflowControls", Qt::DirectConnection);
    if (const int rc = require (
            diagnoseRefresh->isEnabled (),
            "valid WorkCell, Device and TCP enable Diagnose refresh"))
        return rc;
    diagnoseRefresh->click ();
    QLabel* currentPoseStatus = widget.findChild< QLabel* > (
        QStringLiteral ("currentPoseStatusLabel"));
    if (const int rc = require (currentPoseStatus != nullptr,
                                "current-state health status has a stable object name"))
        return rc;
    const QString healthBeforeSync = currentPoseStatus->text ();
    ikSync->click ();
    if (const int rc = require (currentPoseStatus->text () == healthBeforeSync,
                                "Sync current TCP copies IK inputs without changing current-state health"))
        return rc;
    ikCollision->setChecked (false);
    for (QComboBox* combo : widget.findChildren< QComboBox* > ()) {
        if (combo->currentText () == QStringLiteral ("Exclude failed"))
            combo->setCurrentIndex (2);
    }
    // 在有效 WorkCell/设备/TCP 下执行 IK Solve:候选表 5 列、详情表 2 列;
    // 每行候选在 tooltip 里保留完整 Q;选中候选后详情表第 6 行展示 Q;
    // 之后编辑目标位姿会使先前结果变 stale 并禁用 Apply,防止应用过时解。
    ikSolve->click ();
    QCoreApplication::processEvents ();
    if (const int rc = require (ikCandidates->columnCount () == 5 &&
                                    ikDetails->columnCount () == 2,
                                "Solve exposes candidate and selected-detail tables"))
        return rc;
    if (ikCandidates->rowCount () > 0) {
        for (int row = 0; row < ikCandidates->rowCount (); ++row) {
            QTableWidgetItem* currentQ = ikCandidates->item (row, 3);
            if (const int rc = require (currentQ != nullptr && !currentQ->toolTip ().isEmpty (),
                                        "candidate rows retain complete Q values in tooltips"))
                return rc;
        }
        ikCandidates->selectRow (0);
        QCoreApplication::processEvents ();
        if (const int rc = require (ikDetails->item (6, 1) != nullptr &&
                                        !ikDetails->item (6, 1)->text ().isEmpty (),
                                    "selected candidate details retain Q values"))
            return rc;
    }
    const double ikXBeforeEdit = ikX->value ();
    ikX->setValue (ikXBeforeEdit < ikX->maximum () ? ikXBeforeEdit + 0.01
                                                    : ikXBeforeEdit - 0.01);
    if (const int rc = require (!ikApply->isEnabled () &&
                                    widget.statusMessage ().contains (QStringLiteral ("stale"),
                                                                       Qt::CaseInsensitive),
                                "editing a target invalidates the previous IK presentation"))
        return rc;

    rws::TaskPointTableModel* taskModel = widget.findChild< rws::TaskPointTableModel* > ();
    QTableView* taskTable = nullptr;
    for (QTableView* candidate : widget.findChildren< QTableView* > ()) {
        if (candidate != nullptr && candidate->model () == taskModel) {
            taskTable = candidate;
            break;
        }
    }
    QScrollArea* diagnoseScroll = widget.findChild< QScrollArea* > (
        QStringLiteral ("diagnoseScroll"));
    if (const int rc = require (taskModel != nullptr && taskTable != nullptr &&
                                    taskTable->selectionModel () != nullptr && diagnoseScroll != nullptr,
                                "Local Task Points can route into the Diagnose scroll area"))
        return rc;
    const rw::kinematics::State taskState = workcell->getDefaultState ();
    const rw::math::Transform3D<> baseTtcp = rw::kinematics::Kinematics::frameTframe (
        device->getBase (), device->getEnd (), taskState);
    const rw::math::RPY<> taskRpy (baseTtcp.R ());
    rws::TaskPoint localTask;
    localTask.id = "workflow-local-task";
    localTask.name = "Workflow local task";
    localTask.enabled = true;
    localTask.refFrame = device->getBase ()->getName ();
    localTask.tcpFrame = device->getEnd ()->getName ();
    localTask.position = {{baseTtcp.P ()[0], baseTtcp.P ()[1], baseTtcp.P ()[2]}};
    localTask.rpyDeg = {{taskRpy (0) * rw::math::Rad2Deg,
                         taskRpy (1) * rw::math::Rad2Deg,
                         taskRpy (2) * rw::math::Rad2Deg}};
    const int localTaskRow = taskModel->appendTaskPoint (localTask);
    if (const int rc = require (
            taskModel->data (taskModel->index (localTaskRow, rws::ColId), Qt::UserRole)
                    .toString () == QStringLiteral ("workflow-local-task"),
            "Local task rows expose their stable ID independently of the visual row"))
        return rc;
    rws::TaskPoint untouchedTask = localTask;
    untouchedTask.id = "workflow-untouched-task";
    untouchedTask.name = "Workflow Untouched";
    taskModel->setRowsFromTaskPoints ({untouchedTask, localTask});
    rws::TaskPointReachabilityResult untouchedResult;
    untouchedResult.taskPoint = untouchedTask;
    untouchedResult.status = rws::AnalysisStatus::Pass;
    taskModel->setResultForRow (0, untouchedResult);
    const int reorderedLocalTaskRow = 1;
    taskTable->setCurrentIndex (taskModel->index (reorderedLocalTaskRow, 0));
    taskTable->selectRow (reorderedLocalTaskRow);
    QCoreApplication::processEvents ();
    modeButtons.at (1)->click ();
    mode2Source->setCurrentIndex (0);
    mode2ValidateSelected->click ();
    QCoreApplication::processEvents ();
    if (const int rc = require (
            taskModel->resultAt (0).taskPoint.id == untouchedTask.id &&
                taskModel->resultAt (0).status == rws::AnalysisStatus::Pass &&
                taskModel->resultAt (reorderedLocalTaskRow).taskPoint.id == localTask.id,
            "Local Validate Selected updates only the selected stable task ID after visual reordering"))
        return rc;
    QToolButton* taskMoreActions = widget.findChild< QToolButton* > (
        QStringLiteral ("taskPointMoreActions"));
    QPushButton* oldFrozenImport = widget.findChild< QPushButton* > (
        QStringLiteral ("importFrozenRequirementsButton"));
    bool frozenImportInMore = false;
    if (taskMoreActions != nullptr && taskMoreActions->menu () != nullptr) {
        for (QAction* action : taskMoreActions->menu ()->actions ())
            frozenImportInMore = frozenImportInMore ||
                action->text ().contains (QStringLiteral ("frozen"), Qt::CaseInsensitive);
    }
    if (const int rc = require (
            taskMoreActions != nullptr && oldFrozenImport != nullptr && !oldFrozenImport->isVisible () &&
                !frozenImportInMore,
            "Local More does not expose frozen-requirement import into editable tasks"))
        return rc;
    ikCandidates->insertRow (0);
    ikCandidates->setItem (0, 0, new QTableWidgetItem (QStringLiteral ("previous candidate")));
    ikDetails->setRowCount (1);
    ikDetails->setItem (0, 0, new QTableWidgetItem (QStringLiteral ("Previous detail")));
    ikApply->setEnabled (true);
    modeButtons.at (2)->click ();
    const bool openedInIk = QMetaObject::invokeMethod (
        &widget, "openSelectedTaskPointInIk", Qt::DirectConnection);
    QCoreApplication::processEvents ();
    const QRect ikInViewport (
        diagnoseScroll->viewport ()->mapFromGlobal (ikPage->mapToGlobal (QPoint (0, 0))),
        ikPage->size ());
    if (const int rc = require (openedInIk && modeStack->currentIndex () == 0 &&
                                    ikInViewport.intersects (diagnoseScroll->viewport ()->rect ()) &&
                                    ikCandidates->rowCount () == 0 && ikDetails->rowCount () == 1 &&
                                    !ikApply->isEnabled () &&
                                    widget.statusMessage ().contains (QStringLiteral ("stale"),
                                                                       Qt::CaseInsensitive),
                                "opening a Local Task Point routes to visible stale Diagnose IK"))
        return rc;

    const rws::KinematicAnalysisReport localReport = widget.buildReportForExport ();
    bool hasUntouchedPass = false;
    bool hasLocalTask = false;
    for (const rws::TargetEvaluation& result : localReport.taskResults) {
        hasUntouchedPass = hasUntouchedPass ||
            (result.target.id == untouchedTask.id &&
             result.feasibility == rws::Feasibility::Feasible);
        hasLocalTask = hasLocalTask || result.target.id == localTask.id;
    }
    if (const int rc = require (localReport.taskResults.size () == 2 && hasUntouchedPass &&
                                hasLocalTask,
                                "local selected analysis rebuilds the report cache by current stable task IDs"))
        return rc;
    if (const int rc = require (taskModel->removeRows (reorderedLocalTaskRow, 1),
                                "local task removal succeeds"))
        return rc;
    const rws::KinematicAnalysisReport afterRemovalReport = widget.buildReportForExport ();
    if (const int rc = require (afterRemovalReport.taskResults.size () == 1 &&
                                afterRemovalReport.taskResults.front ().target.id == untouchedTask.id &&
                                afterRemovalReport.taskResults.front ().feasibility ==
                                    rws::Feasibility::Feasible,
                                "removing a task prunes stale report IDs while preserving current task results"))
        return rc;
    taskModel->setRowsFromTaskPoints ({untouchedTask});
    if (const int rc = require (widget.buildReportForExport ().taskResults.empty (),
                                "replacing rows clears report cache entries with no current model result"))
        return rc;

    if (const int rc = require (
            validateLoad != nullptr && validateRun != nullptr &&
                validateExport != nullptr && validateState != nullptr &&
                !validateLoad->isVisible () && !validateRun->isVisible () &&
                !validateExport->isVisible (),
            "legacy frozen commands are hidden outside the single Mode 2 toolbar"))
        return rc;
    widget.setWorkCell (nullptr);
    if (const int rc = require (
            !validateLoad->isEnabled () && !validateRun->isEnabled () &&
                !validateExport->isEnabled (),
            "no WorkCell disables validation commands"))
        return rc;
    widget.setWorkCell (noDevice.get ());
    if (const int rc = require (
            !validateLoad->isEnabled () && !validateRun->isEnabled (),
            "no Device disables validation commands"))
        return rc;
    widget.setWorkCell (workcell.get ());
    tcpCombo->setCurrentIndex (-1);
    QMetaObject::invokeMethod (&widget, "refreshWorkflowControls", Qt::DirectConnection);
    if (const int rc = require (
            !validateRun->isEnabled (),
            "no TCP disables requirement validation"))
        return rc;
    tcpCombo->setCurrentIndex (0);
    QMetaObject::invokeMethod (&widget, "refreshWorkflowControls", Qt::DirectConnection);
    if (const int rc = require (
            validateLoad->isEnabled () && !validateRun->isEnabled () &&
                !validateExport->isEnabled () &&
                validateState->text ().contains (QStringLiteral ("No frozen")),
            "validation waits for a frozen requirement artifact"))
        return rc;

    bool acceptedLegacy = true;
    const QByteArray legacyV3 ("{\"schemaVersion\":3}");
    const bool legacyInvoked = QMetaObject::invokeMethod (
        &widget, "loadFrozenRequirementDocument", Qt::DirectConnection,
        Q_RETURN_ARG (bool, acceptedLegacy), Q_ARG (QByteArray, legacyV3));
    if (const int rc = require (legacyInvoked && !acceptedLegacy,
                                "legacy v3 loading is rejected by the validation workflow"))
        return rc;
    if (const int rc = require (
            widget.statusMessage ().contains (
                QStringLiteral ("REQ_V3_REQUIRES_REFREEZE")) &&
                !validateRun->isEnabled (),
            "legacy v3 rejection reports the stable refreeze code"))
        return rc;

    const rw::kinematics::State validationState = workcell->getDefaultState ();
    const rw::math::Transform3D<> worldTtcp = rw::kinematics::Kinematics::frameTframe (
        workcell->getWorldFrame (), device->getEnd (), validationState);
    const rw::math::RPY<> validationRpy (worldTtcp.R ());
    rws::RequirementExecutionSet execution;
    execution.provenance.requirementFingerprint = "workflow-ui-v4";
    execution.provenance.robotModelFingerprint = "workflow-ui-model";
    execution.provenance.environmentFingerprint = "workflow-ui-environment";
    rws::RequirementExecutionTask mustTask;
    mustTask.id = "workflow-must";
    mustTask.name = "Workflow Must";
    mustTask.level = rws::RequirementExecutionLevel::Must;
    mustTask.compileState = rws::RequirementExecutionCompileState::Included;
    mustTask.refFrame = "WORLD";
    mustTask.tcpFrame = device->getEnd ()->getName ();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        mustTask.position[axis] = worldTtcp.P ()[axis];
        mustTask.rpyDeg[axis] = validationRpy (axis) * rw::math::Rad2Deg;
    }
    mustTask.positionToleranceMeters = 1e-5;
    mustTask.orientationToleranceDeg = 1e-3;
    mustTask.collisionFreeRequired = false;
    execution.tasks.push_back (mustTask);
    rws::RequirementExecutionTask shouldTask = mustTask;
    shouldTask.id = "workflow-should";
    shouldTask.name = "Workflow Should";
    shouldTask.level = rws::RequirementExecutionLevel::Should;
    execution.tasks.push_back (shouldTask);
    rws::RequirementExecutionRegion region;
    region.id = "workflow-region";
    region.name = "Workflow Region";
    region.level = rws::RequirementExecutionLevel::Must;
    region.compileState = rws::RequirementExecutionCompileState::Included;
    region.refFrame = "WORLD";
    region.tcpFrame = device->getEnd ()->getName ();
    for (std::size_t axis = 0; axis < 3; ++axis) {
        region.center[axis] = worldTtcp.P ()[axis];
        region.fixedRpyDeg[axis] = mustTask.rpyDeg[axis];
    }
    region.size = {{0.001, 0.001, 0.001}};
    region.samplesPerAxis = 2;
    region.orientationMode = rws::RequirementExecutionOrientationMode::Fixed;
    region.directionSamples = 1;
    region.rollSamples = 1;
    region.minimumCoverage = 1.0;
    region.minimumOrientationCoverage = 1.0;
    region.minimumVerificationStage = rws::RequirementExecutionStage::Verified;
    region.collisionFreeRequired = false;
    execution.workspaceRegions.push_back (region);
    rws::RequirementExecutionRegion secondRegion = region;
    secondRegion.id = "workflow-region-second";
    secondRegion.name = "Workflow Region Second";
    secondRegion.directionSamples = 3;
    secondRegion.rollSamples = 2;
    execution.workspaceRegions.push_back (secondRegion);

    // 构造并加载一个 v4 执行契约(Must 任务 + Should 任务 + 两个 Must 区域),任务取
    // 当前世界坐标系下的 TCP 位姿以保证可达、容差取极小值;加载成功后 Run 才可用。
    bool executionAccepted = false;
    const bool executionInvoked = QMetaObject::invokeMethod (
        &widget, "setRequirementExecutionDocument", Qt::DirectConnection,
        Q_RETURN_ARG (bool, executionAccepted),
        Q_ARG (QJsonObject, rws::RequirementExecutionJson::toObject (execution)));
    if (const int rc = require (executionInvoked && executionAccepted &&
                                    validateRun->isEnabled (),
                                "v4 execution contract enables validation"))
        return rc;
    QTableWidget* taskResults = widget.findChild< QTableWidget* > (
        QStringLiteral ("validateTaskResultTable"));
    QTableWidget* regionCells = widget.findChild< QTableWidget* > (
        QStringLiteral ("validateRegionCellTable"));
    QTableWidget* regionSummary = widget.findChild< QTableWidget* > (
        QStringLiteral ("validateRegionSummaryTable"));
    if (const int rc = require (
            taskResults != nullptr && regionCells != nullptr && regionSummary != nullptr &&
                taskResults->rowCount () == 2 && regionSummary->rowCount () == 2 &&
                taskResults->item (1, 0) != nullptr &&
                taskResults->item (1, 0)->data (Qt::UserRole).toString () ==
                    QStringLiteral ("workflow-should") &&
                validateState->text ().contains (QStringLiteral ("not yet validated"),
                                                  Qt::CaseInsensitive),
            "loaded frozen requirements expose selectable unvalidated task and region sources"))
        return rc;
    mode2Source->setCurrentIndex (1);
    taskResults->selectRow (1);
    mode2ValidateSelected->click ();
    QApplication::processEvents ();
    const rws::KinematicAnalysisReport preFullSelectedTaskReport =
        widget.buildReportForExport ();
    if (const int rc = require (
            taskResults->rowCount () == 1 &&
                preFullSelectedTaskReport.taskResults.size () == 1 &&
                preFullSelectedTaskReport.taskResults.front ().target.id == "workflow-should" &&
                preFullSelectedTaskReport.regionResults.empty (),
            "selected frozen task validates before a full frozen validation"))
        return rc;
    mode2ValidateAll->click ();
    QApplication::processEvents ();
    if (const int rc = require (
            taskResults != nullptr && taskResults->rowCount () == 2,
            "Must and Should task results are displayed separately"))
        return rc;
    if (const int rc = require (
            taskResults->item (0, 2) != nullptr &&
                !taskResults->item (0, 2)->text ().isEmpty () &&
                taskResults->item (0, 3) != nullptr &&
                !taskResults->item (0, 3)->text ().isEmpty () &&
                taskResults->item (0, 4) != nullptr &&
                taskResults->item (0, 4)->text () == QStringLiteral ("Verified"),
            "task result displays Feasibility, Quality and EvidenceStage"))
        return rc;
    if (const int rc = require (
            taskResults->item (0, 2)->text () != QStringLiteral ("DataInsufficient"),
            "task without a collision requirement is not blocked by a missing detector"))
        return rc;
    if (const int rc = require (
            regionCells != nullptr && regionCells->rowCount () >= 1 &&
                regionCells->item (0, 5) != nullptr &&
                !regionCells->item (0, 5)->text ().isEmpty () &&
                regionCells->item (0, 6) != nullptr &&
                !regionCells->item (0, 6)->text ().isEmpty () &&
                regionCells->item (0, 7) != nullptr &&
                regionCells->item (0, 7)->text () == QStringLiteral ("Verified"),
            "region cells display Feasibility, Quality and EvidenceStage"))
        return rc;
    QLabel* orientationProbe = widget.findChild< QLabel* > (
        QStringLiteral ("validateOrientationProbeLabel"));
    QLabel* provenance = widget.findChild< QLabel* > (
        QStringLiteral ("validateProvenanceLabel"));
    QToolButton* frozenDiagnostics = widget.findChild< QToolButton* > (
        QStringLiteral ("validateDiagnosticsToggle"));
    modeButtons.at (1)->click ();
    mode2Source->setCurrentIndex (1);
    QApplication::processEvents ();
    if (const int rc = require (
            regionSummary != nullptr && regionSummary->rowCount () == 2 &&
                regionSummary->columnCount () <= 6 && orientationProbe != nullptr &&
                orientationProbe->isVisible () && provenance != nullptr &&
                frozenDiagnostics != nullptr &&
                !frozenDiagnostics->isChecked (),
            "frozen requirements expose compact region summaries and collapsed provenance diagnostics"))
        return rc;
    regionSummary->selectRow (1);
    QApplication::processEvents ();
    if (const int rc = require (
            orientationProbe->text ().contains (QStringLiteral ("3")) &&
                orientationProbe->text ().contains (QStringLiteral ("2")),
            "selected frozen region exposes its own multi-orientation probe"))
        return rc;
    taskResults->selectRow (1);
    mode2ValidateSelected->click ();
    QApplication::processEvents ();
    if (const int rc = require (
            taskResults->rowCount () == 1 && taskResults->item (0, 0) != nullptr &&
                taskResults->item (0, 0)->text () == QStringLiteral ("workflow-should") &&
                regionCells->rowCount () == 0 && regionSummary->rowCount () == 0 &&
                widget.statusMessage ().contains (QStringLiteral ("selected frozen task")),
            "selected frozen task replaces visible results from the previous full validation"))
        return rc;
    const rws::KinematicAnalysisReport selectedTaskReport = widget.buildReportForExport ();
    if (const int rc = require (
            selectedTaskReport.taskResults.size () == 1 &&
                selectedTaskReport.taskResults.front ().target.id == "workflow-should" &&
                selectedTaskReport.regionResults.empty (),
            "selected frozen task replaces the report summary with its readonly subset"))
        return rc;
    mode2ValidateAll->click ();
    QApplication::processEvents ();
    regionSummary->selectRow (1);
    mode2ValidateSelected->click ();
    QApplication::processEvents ();
    if (const int rc = require (
            regionCells->rowCount () >= 1 && regionCells->item (0, 0) != nullptr &&
                regionCells->item (0, 0)->text () == QStringLiteral ("workflow-region-second") &&
                regionSummary->rowCount () == 1 && regionSummary->item (0, 0) != nullptr &&
                regionSummary->item (0, 0)->data (Qt::UserRole).toString () ==
                    QStringLiteral ("workflow-region-second") && taskResults->rowCount () == 0 &&
                widget.statusMessage ().contains (QStringLiteral ("selected frozen region")),
            "selected frozen region replaces visible results from the previous full validation"))
        return rc;
    const rws::KinematicAnalysisReport selectedRegionReport = widget.buildReportForExport ();
    if (const int rc = require (
            selectedRegionReport.taskResults.empty () &&
                selectedRegionReport.regionResults.size () == 1 &&
                selectedRegionReport.regionResults.front ().regionId == "workflow-region-second",
            "selected frozen region replaces the report summary with its readonly subset"))
        return rc;
    if (const int rc = require (
            validateExport->isEnabled (),
            "validated requirement results enable report export"))
        return rc;
    mode2Source->setCurrentIndex (0);
    taskTable->setCurrentIndex (taskModel->index (0, 0));
    taskTable->selectRow (0);
    mode2ValidateSelected->click ();
    QApplication::processEvents ();
    const rws::KinematicAnalysisReport localAfterFrozenReport = widget.buildReportForExport ();
    if (const int rc = require (
            localAfterFrozenReport.analysisId == "interactive-analysis" &&
                localAfterFrozenReport.taskResults.size () == 1 &&
                localAfterFrozenReport.taskResults.front ().target.id == untouchedTask.id &&
                localAfterFrozenReport.regionResults.empty (),
            "local validation supersedes frozen selected-report authority"))
        return rc;

    if (const int rc = require (
            exploreRun != nullptr && exploreCancel != nullptr &&
                exploreSamples != nullptr && exploreState != nullptr,
            "Explore Capability controls expose stable object names"))
        return rc;

    widget.setWorkCell (nullptr);
    if (const int rc = require (
            !exploreRun->isEnabled () && !exploreCancel->isEnabled (),
            "no WorkCell disables capability commands"))
        return rc;
    widget.setWorkCell (noDevice.get ());
    if (const int rc = require (
            !exploreRun->isEnabled (),
            "no Device disables capability exploration"))
        return rc;
    widget.setWorkCell (workcell.get ());
    tcpCombo->setCurrentIndex (-1);
    QMetaObject::invokeMethod (&widget, "refreshWorkflowControls", Qt::DirectConnection);
    if (const int rc = require (
            !exploreRun->isEnabled (),
            "no TCP disables capability exploration"))
        return rc;
    tcpCombo->setCurrentIndex (0);
    QMetaObject::invokeMethod (&widget, "refreshWorkflowControls", Qt::DirectConnection);

    // Explore 运行状态机:运行中禁用 Run、启用 Cancel,状态含 Running/Estimated;
    // 取消后进入 Cancelling/Cancellation requested,并最终报告部分采样
    // DataInsufficient(含 "sample" 字样);改采样设置重新武装 Run。
    exploreSamples->setValue (1000);
    exploreRun->click ();
    QApplication::processEvents ();
    if (const int rc = require (
            !exploreRun->isEnabled () && exploreCancel->isEnabled () &&
                exploreState->text ().contains (QStringLiteral ("Running")) &&
                exploreState->text ().contains (QStringLiteral ("Estimated")),
            "running state disables Run and enables Cancel"))
        return rc;

    exploreCancel->click ();
    QApplication::processEvents ();
    if (const int rc = require (
            !exploreRun->isEnabled () && !exploreCancel->isEnabled () &&
            (exploreState->text ().contains (QStringLiteral ("Cancelling")) ||
                 exploreState->text ().contains (
                     QStringLiteral ("Cancellation requested")) ||
                 exploreState->text ().contains (QStringLiteral ("DataInsufficient"))),
            "cancelling state disables duplicate commands and reports progress"))
        return rc;
    QElapsedTimer cancellationTimer;
    cancellationTimer.start ();
    while (!exploreState->text ().contains (QStringLiteral ("DataInsufficient")) &&
           cancellationTimer.elapsed () < 2000) {
        QApplication::processEvents (QEventLoop::AllEvents, 20);
    }
    if (const int rc = require (
            exploreState->text ().contains (QStringLiteral ("DataInsufficient")) &&
                exploreState->text ().contains (QStringLiteral ("sample")),
            "cancelled exploration reports partial samples as DataInsufficient"))
        return rc;
    exploreSamples->setValue (std::max (1, exploreSamples->value () - 1));
    if (const int rc = require (
            exploreRun->isEnabled (),
            "changing exploration settings re-arms the Run command"))
        return rc;

    // 在取消进行中卸载 WorkCell:所有命令(Diagnose/Validate/Explore)必须安全禁用,
    // 且后台取消完成不得覆盖"已卸载 WorkCell"的状态文案。
    widget.setWorkCell (nullptr);
    workcell = nullptr;
    device = nullptr;
    stateStructure = nullptr;
    QApplication::processEvents ();
    if (const int rc = require (
            !diagnoseRefresh->isEnabled () && !validateRun->isEnabled () &&
                !exploreRun->isEnabled () && !exploreCancel->isEnabled (),
            "unloading WorkCell during cancellation leaves the UI safely disabled"))
        return rc;
    return require (
        widget.statusMessage ().contains (QStringLiteral ("No WorkCell")),
        "background cancellation does not overwrite the unloaded WorkCell status");
}

// The modeless plot window is a view over the Widget-owned visualization
// snapshot.  Reopening it must reuse the same window, while unloading the
// WorkCell must clear the view and tolerate deletion safely.
// 补充说明(中文):无模式绘图窗口是 Widget 可视化快照的视图;重复打开必须复用同一
// 窗口(保留用户改过的尺寸 930x710),卸载 WorkCell 必须清空视图;任务点/投影/
// 标量模式/网格开关双向同步(Widget->Dialog 与 Dialog->Widget),Dialog 请求的
// 不支持的标量模式回退到 Widget 状态;关闭后 QPointer 变 null 以允许安全析构。
static int testKinematicPlotDialogLifecycle ()
{
    rws::KinematicAnalysisWidget widget;
    widget.resize (640, 480);
    widget.show ();
    QApplication::processEvents ();

    QPushButton* openButton = widget.findChild< QPushButton* > (
        QStringLiteral ("visualizationOpenPlotButton"));
    if (const int rc = require (openButton != nullptr,
                                "visualization exposes the plot-window action"))
        return rc;

    const bool opened = QMetaObject::invokeMethod (
        &widget, "openKinematicPlotDialog", Qt::DirectConnection);
    if (const int rc = require (opened, "plot window open action is invokable"))
        return rc;
    QApplication::processEvents ();

    rws::KinematicPlotDialog* dialog = widget.findChild< rws::KinematicPlotDialog* > (
        QStringLiteral ("kinematicPlotDialog"));
    if (const int rc = require (dialog != nullptr && dialog->isVisible (),
                                "first plot open creates a visible modeless dialog"))
        return rc;
    if (const int rc = require (dialog->size () == QSize (800, 600) &&
                                    dialog->windowFlags ().testFlag (Qt::WindowStaysOnTopHint),
                                "plot dialog has its required initial size and stay-on-top flag"))
        return rc;
    if (const int rc = require (
            dialog->findChild< QComboBox* > (QStringLiteral ("plotProjectionCombo")) != nullptr &&
                dialog->findChild< QComboBox* > (QStringLiteral ("plotScalarModeCombo")) != nullptr &&
                dialog->findChild< QComboBox* > (QStringLiteral ("plotRenderModeCombo")) != nullptr &&
                dialog->findChild< QPushButton* > (QStringLiteral ("plotFitButton")) != nullptr &&
                dialog->findChild< QPushButton* > (QStringLiteral ("plotExportPngButton")) != nullptr &&
                dialog->plotWidget () != nullptr,
            "plot dialog exposes text controls and the shared plot widget"))
        return rc;

    QPointer< rws::KinematicPlotDialog > guardedDialog = dialog;
    dialog->resize (930, 710);
    QMetaObject::invokeMethod (&widget, "openKinematicPlotDialog", Qt::DirectConnection);
    QApplication::processEvents ();
    if (const int rc = require (guardedDialog != nullptr && guardedDialog->size () == QSize (930, 710),
                                "reopening reuses the existing resizable dialog"))
        return rc;

    rw::kinematics::StateStructure::Ptr stateStructure =
        rw::core::ownedPtr (new rw::kinematics::StateStructure ());
    rw::models::SerialDevice::Ptr device = makeGenericSixAxis (*stateStructure);
    rw::models::WorkCell::Ptr workcell = rw::core::ownedPtr (
        new rw::models::WorkCell (stateStructure, "PlotDialog", ""));
    workcell->addDevice (device);
    widget.setWorkCell (workcell.get ());

    rws::TaskPointTableModel* taskModel = widget.findChild< rws::TaskPointTableModel* > ();
    if (const int rc = require (taskModel != nullptr, "visualization owns a task point model"))
        return rc;
    rws::TaskPoint point;
    point.name = "Plot propagation point";
    point.enabled = true;
    taskModel->appendTaskPoint (point);
    QApplication::processEvents ();
    if (const int rc = require (dialog->visualData ().points.size () == 1,
                                "task sample changes propagate to the open plot dialog"))
        return rc;

    QComboBox* projection = widget.findChild< QComboBox* > (
        QStringLiteral ("visualizationProjectionCombo"));
    QComboBox* scalarMode = widget.findChild< QComboBox* > (
        QStringLiteral ("visualizationScalarModeCombo"));
    QCheckBox* showGrid = widget.findChild< QCheckBox* > (
        QStringLiteral ("visualizationShowGridCheck"));
    if (const int rc = require (projection != nullptr && scalarMode != nullptr && showGrid != nullptr,
                                "visualization controls have stable object names"))
        return rc;
    projection->setCurrentIndex (projection->findData (static_cast<int> (rws::VisualProjection::YZ)));
    scalarMode->setCurrentIndex (scalarMode->findData (static_cast<int> (rws::VisualScalarMode::Status)));
    showGrid->setChecked (false);
    QApplication::processEvents ();
    if (const int rc = require (dialog->projection () == rws::VisualProjection::YZ &&
                                    !dialog->showGrid () &&
                                    dialog->visualData ().scalarMode == rws::VisualScalarMode::Status,
                                "projection scalar mode and display state propagate to the dialog"))
        return rc;

    dialog->findChild< QComboBox* > (QStringLiteral ("plotProjectionCombo"))
        ->setCurrentIndex (dialog->findChild< QComboBox* > (
            QStringLiteral ("plotProjectionCombo"))->findData (
            static_cast<int> (rws::VisualProjection::XZ)));
    QApplication::processEvents ();
    if (const int rc = require (
            projection->currentData ().toInt () == static_cast<int> (rws::VisualProjection::XZ),
            "dialog projection requests update the Widget-owned display state"))
        return rc;

    QComboBox* dialogScalarMode = dialog->findChild< QComboBox* > (
        QStringLiteral ("plotScalarModeCombo"));
    dialogScalarMode->setCurrentIndex (dialogScalarMode->findData (
        static_cast<int> (rws::VisualScalarMode::Coverage)));
    QApplication::processEvents ();
    if (const int rc = require (
            dialogScalarMode->currentData ().toInt () == scalarMode->currentData ().toInt (),
            "unsupported dialog scalar modes revert to the Widget-owned display state"))
        return rc;

    dialog->plotWidget ()->visualPointClicked (rws::AnalysisVisualPoint ());
    if (const int rc = require (
            widget.statusMessage ().contains (QStringLiteral ("no saved reachable Q")),
            "dialog plot point clicks are forwarded to the Widget"))
        return rc;

    widget.setWorkCell (nullptr);
    QApplication::processEvents ();
    if (const int rc = require (dialog->visualData ().points.empty (),
                                "unloading the WorkCell clears open plot data"))
        return rc;

    dialog->close ();
    QApplication::processEvents ();
    if (const int rc = require (guardedDialog.isNull (),
                                "closing the modeless plot dialog clears the guarded pointer"))
        return rc;
    return 0;
}

// Threshold dialog transaction: values are copied in, displayed in selected
// units, and only a valid accepted dialog exposes the edited eight-value set.
// 补充说明(中文):阈值对话框是事务式编辑——初值按所选显示单位(厘米/弧度)换算显示,
// 但内部仍以米/度保存;reject 不改变源阈值;accept 后只有通过校验的八值集合可读;
// 条件数警告 >= 失败时拒绝(Rejected)并显示校验文案。
static int testKinematicThresholdsDialog ()
{
    rws::KinematicThresholds source;
    source.nearJointLimitRatio = 0.12;
    source.conditionWarning = 120.0;
    source.conditionFail = 1200.0;
    source.singularValueWarning = 2e-4;
    source.manipulabilityWarning = 3e-5;
    source.positionToleranceMeters = 0.002;
    source.orientationToleranceDeg = 2.0;
    source.ikDuplicateQThreshold = 0.003;

    rws::KinematicThresholdsDialog dialog (
        source, rws::KinematicLengthUnit::Centimeters,
        rws::KinematicAngleUnit::Radians);
    if (const int rc = require (dialog.isModal (), "threshold dialog is modal"))
        return rc;
    const QList< QDoubleSpinBox* > spins = dialog.findChildren< QDoubleSpinBox* > ();
    if (const int rc = require (spins.size () == 8, "threshold dialog has eight spin boxes"))
        return rc;
    QDoubleSpinBox* nearLimit = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("nearJointLimitRatioSpin"));
    QDoubleSpinBox* conditionWarning = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("conditionWarningSpin"));
    QDoubleSpinBox* conditionFail = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("conditionFailSpin"));
    QDoubleSpinBox* singularWarning = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("singularValueWarningSpin"));
    QDoubleSpinBox* manipulabilityWarning = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("manipulabilityWarningSpin"));
    QDoubleSpinBox* positionTolerance = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("positionToleranceSpin"));
    QDoubleSpinBox* orientationTolerance = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("orientationToleranceSpin"));
    QDoubleSpinBox* duplicateQ = dialog.findChild< QDoubleSpinBox* > (
        QStringLiteral ("ikDuplicateQThresholdSpin"));
    if (const int rc = require (
            nearLimit != nullptr && conditionWarning != nullptr && conditionFail != nullptr &&
                singularWarning != nullptr && manipulabilityWarning != nullptr &&
                positionTolerance != nullptr && orientationTolerance != nullptr &&
                duplicateQ != nullptr,
            "threshold dialog exposes stable spin names"))
        return rc;
    if (const int rc = assertNear (nearLimit->value (), source.nearJointLimitRatio, 1e-12,
                                   "near-limit initial value"))
        return rc;
    if (const int rc = assertNear (conditionWarning->value (), source.conditionWarning, 1e-12,
                                   "condition-warning initial value"))
        return rc;
    if (const int rc = assertNear (conditionFail->value (), source.conditionFail, 1e-12,
                                   "condition-fail initial value"))
        return rc;
    if (const int rc = assertNear (singularWarning->value (), source.singularValueWarning, 1e-12,
                                   "singular-warning initial value"))
        return rc;
    if (const int rc = assertNear (manipulabilityWarning->value (),
                                   source.manipulabilityWarning, 1e-12,
                                   "manipulability-warning initial value"))
        return rc;
    if (const int rc = assertNear (positionTolerance->value (), 0.2, 1e-9,
                                   "position tolerance displayed in centimeters"))
        return rc;
    if (const int rc = assertNear (orientationTolerance->value (),
                                   source.orientationToleranceDeg * 3.14159265358979323846 / 180.0,
                                   1e-9, "orientation tolerance displayed in radians"))
        return rc;
    if (const int rc = assertNear (duplicateQ->value (), source.ikDuplicateQThreshold, 1e-12,
                                   "duplicate-Q initial value"))
        return rc;

    nearLimit->setValue (0.25);
    dialog.reject ();
    if (const int rc = assertNear (source.nearJointLimitRatio, 0.12, 1e-12,
                                   "cancel leaves source threshold unchanged"))
        return rc;

    rws::KinematicThresholdsDialog accepted (
        source, rws::KinematicLengthUnit::Meters, rws::KinematicAngleUnit::Degrees);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("nearJointLimitRatioSpin"))
        ->setValue (0.25);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("conditionWarningSpin"))
        ->setValue (150.0);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("conditionFailSpin"))
        ->setValue (1500.0);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("singularValueWarningSpin"))
        ->setValue (4e-4);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("manipulabilityWarningSpin"))
        ->setValue (5e-5);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("positionToleranceSpin"))
        ->setValue (0.004);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("orientationToleranceSpin"))
        ->setValue (4.0);
    accepted.findChild< QDoubleSpinBox* > (QStringLiteral ("ikDuplicateQThresholdSpin"))
        ->setValue (0.006);
    accepted.accept ();
    if (const int rc = require (accepted.result () == QDialog::Accepted,
                                "valid threshold dialog accepts"))
        return rc;
    const rws::KinematicThresholds edited = accepted.thresholds ();
    if (const int rc = assertNear (edited.nearJointLimitRatio, 0.25, 1e-12,
                                   "accepted near-limit ratio")) return rc;
    if (const int rc = assertNear (edited.conditionWarning, 150.0, 1e-12,
                                   "accepted condition warning")) return rc;
    if (const int rc = assertNear (edited.conditionFail, 1500.0, 1e-12,
                                   "accepted condition fail")) return rc;
    if (const int rc = assertNear (edited.singularValueWarning, 4e-4, 1e-12,
                                   "accepted singular warning")) return rc;
    if (const int rc = assertNear (edited.manipulabilityWarning, 5e-5, 1e-12,
                                   "accepted manipulability warning")) return rc;
    if (const int rc = assertNear (edited.positionToleranceMeters, 0.004, 1e-12,
                                   "accepted position tolerance")) return rc;
    if (const int rc = assertNear (edited.orientationToleranceDeg, 4.0, 1e-12,
                                   "accepted orientation tolerance")) return rc;
    if (const int rc = assertNear (edited.ikDuplicateQThreshold, 0.006, 1e-12,
                                   "accepted duplicate-Q threshold")) return rc;

    rws::KinematicThresholdsDialog invalid (
        source, rws::KinematicLengthUnit::Meters, rws::KinematicAngleUnit::Degrees);
    invalid.findChild< QDoubleSpinBox* > (QStringLiteral ("conditionWarningSpin"))
        ->setValue (2000.0);
    invalid.findChild< QDoubleSpinBox* > (QStringLiteral ("conditionFailSpin"))
        ->setValue (1000.0);
    invalid.accept ();
    if (const int rc = require (invalid.result () == QDialog::Rejected,
                                "condition warning at or above fail is rejected"))
        return rc;
    QLabel* validation = invalid.findChild< QLabel* > (QStringLiteral ("validationLabel"));
    return require (validation != nullptr && !validation->text ().isEmpty (),
                    "invalid threshold feedback is visible");
}

// runAll:把所有子套件串行跑一遍,首个失败立即返回,便于 CTest 精确定位首个失败点;
// 其中需要 QApplication 的四个用例(managed_project_gate / workflow_ui / thresholds /
// plot_dialog)由 main 独立分发,不在此列表内。
static int runAll ()
{
    if (const int rc = testHistoricalFrozenRequirementAdapterAbiRemainsLinkable ())
        return rc;
    if (const int rc = testTypes ())
        return rc;
    if (const int rc = testAnalysisContext ())
        return rc;
    if (const int rc = testConfigurationEvaluator ())
        return rc;
    if (const int rc = testMetrics ())
        return rc;
    if (const int rc = testCurrentPose ())
        return rc;
    if (const int rc = testTargetEvaluator ())
        return rc;
    if (const int rc = testTargetCandidateOrdering ())
        return rc;
    if (const int rc = testKinematicBatchRunner ())
        return rc;
    if (const int rc = testRequirementValidationSummary ())
        return rc;
    if (const int rc = testKinematicAnalyzerRequirementValidationForwarding ())
        return rc;
    if (const int rc = testVerifiedRegionGridGeneration ())
        return rc;
    if (const int rc = testVerifiedRegionTargetEvaluation ())
        return rc;
    if (const int rc = testTargetValidationAndResidual ())
        return rc;
    if (const int rc = testPoseUnitConversions ())
        return rc;
    if (const int rc = testIkRanking ())
        return rc;
    if (const int rc = testIkIncludesCurrentQForCurrentTcpTarget ())
        return rc;
    if (const int rc = testIkDuplicateThresholdControlsCandidateMerging ())
        return rc;
    if (const int rc = testTaskPointReachableRate ())
        return rc;
    if (const int rc = testTaskPointResolver ())
        return rc;
    if (const int rc = testWorkcellAwareAnalyzeTaskPoint ())
        return rc;
    if (const int rc = testTaskPointUiLogic ())
        return rc;
    if (const int rc = testTaskPointModel ())
        return rc;
    if (const int rc = testFrozenRequirementArtifactImportsIntoKinematicTasks ())
        return rc;
    if (const int rc = testVisualizationData ())
        return rc;
    if (const int rc = testWorkspaceEnvelopeHelpers ())
        return rc;
    if (const int rc = testWorkspaceEnvelopeRenderingLayout ())
        return rc;
    if (const int rc = testWorkspaceHelpers ())
        return rc;
    if (const int rc = testWorkspaceSampling ())
        return rc;
    if (const int rc = testPoseReachabilityHelpers ())
        return rc;
    if (const int rc = testOrientationCoverageSampling ())
        return rc;
    if (const int rc = testPoseReachability ())
        return rc;
    if (const int rc = testJsonAndCollisionHelpers ())
        return rc;
    if (const int rc = testProjectDocumentRoundTrip ())
        return rc;
    return testAggregateResult ();
}

// main:argv[1] 选子套件("all" / 各具体名),默认 "all"。
// QCoreApplication 是为了让 Q_OBJECT 相关初始化(QFile/QString)能正常工作。
// 补充说明:四个需要 QApplication(完整 Qt Widget 栈)的子套件(managed_project_gate /
// workflow_ui / thresholds / plot_dialog)与基于 QCoreApplication 的纯算法子套件在
// main 中分别分支;每个子套件由 argv[1] 名字选择,默认 "all";"frozen_requirements"
// 与 "adapter" 别名指向同一个集成用例。
int main (int argc, char** argv)
{
    const std::string requestedSuite = argc > 1 ? argv[1] : "all";
    if (requestedSuite == "managed_project_gate") {
        QApplication app (argc, argv);
        const int rc = testManagedRobotProjectRequiresPublishedWorkCell ();
        if (rc == 0)
            std::cout << "KinematicAnalysis managed_project_gate test passed." << std::endl;
        return rc;
    }
    if (requestedSuite == "workflow_ui") {
        QApplication app (argc, argv);
        const int rc = testWorkflowUiStates ();
        if (rc == 0)
            std::cout << "KinematicAnalysis workflow_ui test passed." << std::endl;
        return rc;
    }
    if (requestedSuite == "thresholds") {
        QApplication app (argc, argv);
        const int rc = testKinematicThresholdsDialog ();
        if (rc == 0)
            std::cout << "KinematicAnalysis thresholds test passed." << std::endl;
        return rc;
    }
    if (requestedSuite == "plot_dialog") {
        QApplication app (argc, argv);
        const int rc = testKinematicPlotDialogLifecycle ();
        if (rc == 0)
            std::cout << "KinematicAnalysis plot_dialog test passed." << std::endl;
        return rc;
    }
    QCoreApplication app (argc, argv);
    const std::string suite = requestedSuite;
    int rc                 = 0;
    if (suite == "all")
        rc = runAll ();
    else if (suite == "abi")
        rc = testHistoricalFrozenRequirementAdapterAbiRemainsLinkable ();
    else if (suite == "types")
        rc = testTypes ();
    else if (suite == "context")
        rc = testAnalysisContext ();
    else if (suite == "configuration")
        rc = testConfigurationEvaluator ();
    else if (suite == "metrics")
        rc = testMetrics ();
    else if (suite == "current_pose")
        rc = testCurrentPose ();
    else if (suite == "target")
        rc = testTargetEvaluator ();
    else if (suite == "target_sort")
        rc = testTargetCandidateOrdering ();
    else if (suite == "batch")
        rc = testKinematicBatchRunner ();
    else if (suite == "cancellation")
        rc = testKinematicBatchRunnerCancellation ();
    else if (suite == "requirement_summary")
        rc = testRequirementValidationSummary ();
    else if (suite == "batch_forwarding")
        rc = testKinematicAnalyzerRequirementValidationForwarding ();
    else if (suite == "verified_region_grid")
        rc = testVerifiedRegionGridGeneration ();
    else if (suite == "verified_region_ik")
        rc = testVerifiedRegionTargetEvaluation ();
    else if (suite == "target_validation")
        rc = testTargetValidationAndResidual ();
    else if (suite == "pose_units")
        rc = testPoseUnitConversions ();
    else if (suite == "ik")
        rc = testIkRanking ();
    else if (suite == "ik_current_target")
        rc = testIkIncludesCurrentQForCurrentTcpTarget ();
    else if (suite == "ik_dedup")
        rc = testIkDuplicateThresholdControlsCandidateMerging ();
    else if (suite == "task_points")
        rc = testTaskPointReachableRate ();
    else if (suite == "task_point_resolver")
        rc = testTaskPointResolver ();
    else if (suite == "task_point_workcell")
        rc = testWorkcellAwareAnalyzeTaskPoint ();
    else if (suite == "task_point_ui")
        rc = testTaskPointUiLogic ();
    else if (suite == "task_point_model")
        rc = testTaskPointModel ();
    else if (suite == "frozen_requirements" || suite == "adapter")
        rc = testFrozenRequirementArtifactImportsIntoKinematicTasks ();
    else if (suite == "visualization_data")
        rc = testVisualizationData ();
    else if (suite == "workspace_envelope")
        rc = testWorkspaceEnvelopeHelpers ();
    else if (suite == "workspace_envelope_layout")
        rc = testWorkspaceEnvelopeRenderingLayout ();
    else if (suite == "workspace_helpers")
        rc = testWorkspaceHelpers ();
    else if (suite == "workspace")
        rc = testWorkspaceSampling ();
    else if (suite == "pose_reachability_helpers" || suite == "pose_reachability_legacy")
        rc = testPoseReachabilityHelpers ();
    else if (suite == "orientation_sampling")
        rc = testOrientationCoverageSampling ();
    else if (suite == "report")
        rc = testReportJsonRoundTrip ();
    else if (suite == "cache")
        rc = testBatchCacheKey ();
    else if (suite == "pose_reachability" || suite == "pose")
        rc = testPoseReachability ();
    else if (suite == "helpers")
        rc = testJsonAndCollisionHelpers ();
    else if (suite == "project_document")
        rc = testProjectDocumentRoundTrip ();
    else if (suite == "aggregate")
        rc = testAggregateResult ();
    else
        return fail ("Unknown KinematicAnalysis test suite: " + suite);
    if (rc != 0)
        return rc;
    std::cout << "KinematicAnalysis " << suite << " test passed." << std::endl;
    return 0;
}
