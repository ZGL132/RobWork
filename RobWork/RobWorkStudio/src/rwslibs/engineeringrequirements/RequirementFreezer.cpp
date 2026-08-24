#include "RequirementFreezer.hpp"

#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementSetJson.hpp"
#include "WorkspaceSamplingGrid.hpp"
#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

#include <rw/kinematics/Kinematics.hpp>
#include <rw/math/RPY.hpp>
#include <rw/models/Device.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/JointDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelSpecJson.hpp>
#include <rwslibs/robotmodelbuilder/WorkCellConverter.hpp>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace rws {
namespace {

bool isWorld(const std::string& name)
{
    return name.empty() || name == "WORLD";
}

rw::kinematics::Frame* findFrame(const rw::models::WorkCell& workcell, const std::string& name)
{
    return isWorld(name) ? workcell.getWorldFrame() : workcell.findFrame(name);
}

void addEnvironmentDiagnostic(std::vector<RequirementDiagnostic>& diagnostics, const std::string& id,
                              RequirementLevel level, const std::string& message,
                              const std::string& code = "REQ_FRAME_NOT_FOUND")
{
    RequirementDiagnostic diagnostic;
    diagnostic.requirementId = id;
    diagnostic.level = level;
    diagnostic.severity = level == RequirementLevel::Must ? RequirementDiagnosticSeverity::Error :
        (level == RequirementLevel::Info ? RequirementDiagnosticSeverity::Info :
                                           RequirementDiagnosticSeverity::Warning);
    diagnostic.field = code == "REQ_FRAME_NOT_FOUND" ? "refFrame" :
        ((code == "REQ_TCP_FRAME_NOT_FOUND" || code == "REQ_TCP_FRAME_WRONG_DEVICE") ? "tcpFrame" :
         (code == "REQ_GEOMETRY_TARGET_NOT_FOUND" ? "geometryFeature" : "orientation"));
    diagnostic.message = message;
    diagnostic.code = code;
    diagnostic.source = "engineeringrequirements.freezer";
    diagnostic.blocking = level == RequirementLevel::Must;
    diagnostics.push_back(diagnostic);
}

bool hasDiagnostic(const std::vector<RequirementDiagnostic>& diagnostics, const std::string& id)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&] (const RequirementDiagnostic& diagnostic) {
        return diagnostic.requirementId == id;
    });
}

/**
 * @brief 将当前 WorkCell 和冻结时 State 规约为稳定哈希输入。
 *
 * 同一个 Frame 名称在不同夹具位置或不同关节状态下不能被视为同一工程环境，因此
 * 对每个 Frame 记录其世界变换。数值使用高精度文本写入，避免显示层单位转换影响哈希。
 */
bool belongsToDevice(const rw::kinematics::Frame* frame,
                     const rw::kinematics::Frame* deviceBase,
                     const rw::kinematics::State& state)
{
    for (const rw::kinematics::Frame* current = frame; current != nullptr;
         current = current->getParent(state)) {
        if (current == deviceBase) return true;
    }
    return false;
}

std::string kinematicFingerprint(const rw::models::WorkCell& workcell,
                                 const rw::kinematics::State& state,
                                 const std::string& deviceName)
{
    const rw::models::Device::Ptr device = workcell.findDevice(deviceName);
    if (device == nullptr || device->getBase() == nullptr || device->getEnd() == nullptr)
        return std::string();

    std::ostringstream stream;
    stream << std::setprecision(17) << device->getName() << '\n'
           << device->getBase()->getName() << '\n'
           << device->getEnd()->getName() << '\n';
    const rw::models::JointDevice* jointDevice = dynamic_cast<const rw::models::JointDevice*>(device.get());
    if (jointDevice == nullptr) return std::string();
    const std::vector<rw::models::Joint*>& joints = jointDevice->getJoints();
    stream << joints.size() << '\n';
    for (const rw::models::Joint* joint : joints) {
        if (joint == nullptr) return std::string();
        const rw::math::Transform3D<> transform = joint->getFixedTransform();
        stream << joint->getName();
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                stream << ' ' << transform.R()(row, column);
        for (int axis = 0; axis < 3; ++axis)
            stream << ' ' << transform.P()[axis];
        const std::pair<rw::math::Q, rw::math::Q> bounds = joint->getBounds();
        for (std::size_t index = 0; index < bounds.first.size(); ++index)
            stream << ' ' << bounds.first[index];
        for (std::size_t index = 0; index < bounds.second.size(); ++index)
            stream << ' ' << bounds.second[index];
        const rw::math::Q velocity = joint->getMaxVelocity();
        const rw::math::Q acceleration = joint->getMaxAcceleration();
        for (std::size_t index = 0; index < velocity.size(); ++index)
            stream << ' ' << velocity[index];
        for (std::size_t index = 0; index < acceleration.size(); ++index)
            stream << ' ' << acceleration[index];
        stream << '\n';
    }

    // Joint transforms encode the live Q and are deliberately excluded here.
    // Every other frame in the analysed device subtree is static installation
    // evidence, including TCP and tool mounting frames.
    std::vector<rw::kinematics::Frame*> frames =
        rw::kinematics::Kinematics::findAllFrames(device->getBase(), state);
    std::sort(frames.begin(), frames.end(), [] (const rw::kinematics::Frame* left,
                                                const rw::kinematics::Frame* right) {
        return left->getName() < right->getName();
    });
    stream << "frames " << frames.size() << '\n';
    for (const rw::kinematics::Frame* frame : frames) {
        if (frame == nullptr) return std::string();
        const rw::kinematics::Frame* parent = frame->getParent(state);
        stream << frame->getName() << ' '
               << (parent == nullptr ? std::string("<none>") : parent->getName());
        if (dynamic_cast<const rw::models::Joint*>(frame) != nullptr) {
            stream << " joint\n";
            continue;
        }
        const rw::math::Transform3D<> transform = frame->getTransform(state);
        stream << " frame";
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                stream << ' ' << transform.R()(row, column);
        for (int axis = 0; axis < 3; ++axis)
            stream << ' ' << transform.P()[axis];
        stream << '\n';
    }
    return QCryptographicHash::hash(QByteArray::fromStdString(stream.str()), QCryptographicHash::Sha256)
        .toHex().toStdString();
}

FrozenRobotStateSnapshot captureRobotState(const rw::models::WorkCell& workcell,
                                           const rw::kinematics::State& state,
                                           const std::string& deviceName)
{
    FrozenRobotStateSnapshot snapshot;
    snapshot.deviceName = deviceName;
    const rw::models::Device::Ptr device = workcell.findDevice(deviceName);
    if (device == nullptr || device->getEnd() == nullptr) return snapshot;
    snapshot.tcpFrameName = device->getEnd()->getName();
    snapshot.kinematicFingerprint = kinematicFingerprint(workcell, state, deviceName);

    const rw::math::Q q = device->getQ(state);
    snapshot.q.reserve(q.size());
    for (std::size_t index = 0; index < q.size(); ++index)
        snapshot.q.push_back(q[index]);
    const rw::math::Transform3D<> tcp = rw::kinematics::Kinematics::worldTframe(device->getEnd(), state);
    std::size_t index = 0;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            snapshot.tcpWorldPose[index++] = tcp.R()(row, column);
    for (int axis = 0; axis < 3; ++axis)
        snapshot.tcpWorldPose[index++] = tcp.P()[axis];
    snapshot.tcpWorldPose[index++] = 0.0;
    snapshot.tcpWorldPose[index++] = 0.0;
    snapshot.tcpWorldPose[index++] = 0.0;
    snapshot.tcpWorldPose[index] = 1.0;
    return snapshot;
}

bool sameQ(const FrozenRobotStateSnapshot& left, const FrozenRobotStateSnapshot& right)
{
    return left.deviceName == right.deviceName && left.q == right.q;
}

std::string environmentFingerprint(const rw::models::WorkCell& workcell,
                                   const rw::kinematics::State& state,
                                   const std::string& deviceName)
{
    const rw::models::Device::Ptr device = workcell.findDevice(deviceName);
    const rw::kinematics::Frame* deviceBase = device == nullptr ? nullptr : device->getBase();
    std::vector<rw::kinematics::Frame*> frames = workcell.getFrames();
    std::sort(frames.begin(), frames.end(), [] (const rw::kinematics::Frame* left,
                                                const rw::kinematics::Frame* right) {
        return left->getName() < right->getName();
    });
    std::ostringstream stream;
    stream << std::setprecision(17) << workcell.getName() << '\n';
    for (rw::kinematics::Frame* frame : frames) {
        if (frame == nullptr) continue;
        if (deviceBase != nullptr && belongsToDevice(frame, deviceBase, state)) continue;
        const rw::math::Transform3D<> transform = rw::kinematics::Kinematics::worldTframe(frame, state);
        const rw::kinematics::Frame* parent = frame->getParent(state);
        stream << frame->getName() << ' ' << (parent == nullptr ? std::string("<none>") : parent->getName());
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column)
                stream << ' ' << transform.R()(row, column);
        for (int axis = 0; axis < 3; ++axis)
            stream << ' ' << transform.P()[axis];
        stream << '\n';
    }
    // The frame graph alone cannot detect a fixture mesh, collision model, or
    // attachment-resource change. Reuse the existing WorkCell conversion to
    // canonicalize those scene inputs while excluding the live joint state.
    QStringList conversionWarnings;
    const RobotModelSpec scene = WorkCellConverter::convert(workcell, state, std::string(), conversionWarnings);
    const QJsonObject sceneObject = RobotModelSpecJson::toObject(scene);
    QJsonObject sceneEvidence;
    sceneEvidence["sceneFrames"] = sceneObject.value("sceneFrames");
    sceneEvidence["sceneGeometries"] = sceneObject.value("sceneGeometries");
    sceneEvidence["drawables"] = sceneObject.value("drawables");
    sceneEvidence["collisionModels"] = sceneObject.value("collisionModels");
    sceneEvidence["collisionSetup"] = sceneObject.value("collisionSetup");
    sceneEvidence["proximitySetup"] = sceneObject.value("proximitySetup");
    stream << QJsonDocument(sceneEvidence).toJson(QJsonDocument::Compact).toStdString();
    return QCryptographicHash::hash(QByteArray::fromStdString(stream.str()), QCryptographicHash::Sha256)
        .toHex().toStdString();
}

/**
 * @brief 对磁盘上的场景源文件计算内容指纹。
 *
 * 仅记录路径不能识别“同一路径下的场景被替换”这一常见工程变更；因此在源文件存在时
 * 读取原始字节计算 SHA-256。内存构造的测试 WorkCell 或无来源场景允许返回空指纹，
 * 但它们仍受场景状态指纹与序列化快照的双重约束。
 */
std::string fileFingerprint(const std::string& path)
{
    if (path.empty()) return std::string();
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) return std::string();
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
        .toHex().toStdString();
}

/**
 * @brief 为场景快照本身生成稳定指纹。
 *
 * 快照的设备名称、冻结状态和场景几何都参与哈希，避免 JSON 被手工篡改后仍沿用旧的
 * WorkCell 指纹。使用 RobotModelSpec 的规范 JSON 而不是内存地址，确保跨进程、跨
 * 机器可复现。
 */
std::string scenarioFingerprint(const FrozenWorkCellScenarioSnapshot& scenario)
{
    QJsonObject object;
    object["schemaVersion"] = scenario.schemaVersion;
    object["sourceWorkCellPath"] = QString::fromStdString(scenario.sourceWorkCellPath);
    object["sourceFileFingerprint"] = QString::fromStdString(scenario.sourceFileFingerprint);
    object["deviceName"] = QString::fromStdString(scenario.deviceName);
    object["environmentFingerprint"] = QString::fromStdString(scenario.environmentFingerprint);
    object["stateFingerprint"] = QString::fromStdString(scenario.stateFingerprint);
    object["sceneSpec"] = RobotModelSpecJson::toObject(scenario.sceneSpec);
    return QCryptographicHash::hash(QJsonDocument(object).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256)
        .toHex().toStdString();
}

bool makeProjectRelative(const QString& projectRoot, std::string& value)
{
    const QString raw = QString::fromStdString(value).trimmed();
    if (projectRoot.isEmpty() || raw.isEmpty() || !QFileInfo(raw).isAbsolute())
        return false;
    const QDir root(QFileInfo(projectRoot).absoluteFilePath());
    const QString relative = QDir::fromNativeSeparators(
        root.relativeFilePath(QFileInfo(raw).absoluteFilePath()));
    if (relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) ||
        QFileInfo(relative).isAbsolute())
        return false;
    value = QDir::cleanPath(relative).toStdString();
    return true;
}

void makeScenarioPortable(FrozenWorkCellScenarioSnapshot& snapshot, const QString& projectRoot)
{
    if (projectRoot.trimmed().isEmpty())
        return;
    makeProjectRelative(projectRoot, snapshot.sourceWorkCellPath);
    makeProjectRelative(projectRoot, snapshot.sceneSpec.saveDirectory);
    for (DrawableSpec& drawable : snapshot.sceneSpec.drawables)
        makeProjectRelative(projectRoot, drawable.filePath);
    for (CollisionModelSpec& collision : snapshot.sceneSpec.collisionModels)
        makeProjectRelative(projectRoot, collision.filePath);
    for (SceneGeometrySpec& geometry : snapshot.sceneSpec.sceneGeometries)
        makeProjectRelative(projectRoot, geometry.file);
    for (IncludeSpec& include : snapshot.sceneSpec.includes)
        makeProjectRelative(projectRoot, include.file);
    makeProjectRelative(projectRoot, snapshot.sceneSpec.imported.sourceSceneFile);
    makeProjectRelative(projectRoot, snapshot.sceneSpec.imported.sourceDeviceFile);
    makeProjectRelative(projectRoot, snapshot.sceneSpec.imported.sourceCollisionSetupFile);
    makeProjectRelative(projectRoot, snapshot.sceneSpec.imported.sourceProximitySetupFile);
}

/**
 * @brief 从冻结时的真实 WorkCell 提取候选评价可重建的场景快照。
 *
 * WorkCellConverter 会保留外部 Frame、场景几何、碰撞模型以及它们在冻结 State 下的
 * 位姿。候选模型工厂之后只替换机器人结构部分，工装和工件仍使用此处冻结的真实场景。
 */
FrozenWorkCellScenarioSnapshot makeScenarioSnapshot(const rw::models::WorkCell& workcell,
                                                      const rw::kinematics::State& state,
                                                      const RobotModelSpec& model,
                                                      const std::string& projectRoot)
{
    FrozenWorkCellScenarioSnapshot snapshot;
    snapshot.sourceWorkCellPath = WorkCellConverter::inferWorkCellFilePath(workcell);
    if (!snapshot.sourceWorkCellPath.empty()) {
        snapshot.sourceWorkCellPath = QFileInfo(QString::fromStdString(snapshot.sourceWorkCellPath))
                                         .absoluteFilePath().toStdString();
    }
    snapshot.sourceFileFingerprint = fileFingerprint(snapshot.sourceWorkCellPath);
    snapshot.deviceName = model.robotName;
    snapshot.environmentFingerprint = environmentFingerprint(workcell, state, snapshot.deviceName);
    snapshot.stateFingerprint = snapshot.environmentFingerprint;
    QStringList conversionWarnings;
    const QString sceneDirectory = snapshot.sourceWorkCellPath.empty()
        ? QString::fromStdString(model.saveDirectory)
        : QFileInfo(QString::fromStdString(snapshot.sourceWorkCellPath)).absolutePath();
    snapshot.sceneSpec = WorkCellConverter::convert(workcell, state,
                                                     sceneDirectory.toStdString(), conversionWarnings);
    makeScenarioPortable(snapshot, QString::fromStdString(projectRoot));
    snapshot.snapshotFingerprint = scenarioFingerprint(snapshot);
    return snapshot;
}

RequirementSet compiledSnapshot(const CompiledRequirementSet& compiled)
{
    RequirementSet snapshot;
    snapshot.frozen = true;
    snapshot.modelBinding = compiled.modelBinding;
    for (const CompiledPoseTask& item : compiled.poseTasks) {
        PoseTask task;
        task.id = item.id;
        task.name = item.name;
        task.level = item.level;
        task.refFrame = item.refFrame;
        task.tcpFrame = item.tcpFrame;
        task.position = item.position;
        task.rpyDeg = item.rpyDeg;
        task.tolerance = item.tolerance;
        task.processType = item.processType;
        task.geometryFeature = item.geometryFeature;
        task.orientation = item.orientation;
        task.validation = item.validation;
        task.approach = item.approach;
        task.retract = item.retract;
        // 从条目级溯源还原工位的来源种类：编译快照只存文本形式的 sourceKind，
        // 重新投影回编辑态快照时用它还原 PoseTaskSource，保证往返不丢失来源语义。
        PoseTaskSource source = PoseTaskSource::Manual;
        if (!item.provenance.sourceKind.empty() &&
            poseTaskSourceFromString(item.provenance.sourceKind, source))
            task.source = source;
        snapshot.poseTasks.push_back(task);
    }
    for (const WorkspaceDemandRegion& item : compiled.workspaceRegions) {
        BoxRegion region;
        region.id = item.id;
        region.name = item.name;
        region.level = item.level;
        region.refFrame = item.refFrame;
        region.center = item.center;
        region.size = item.size;
        region.minimumCoverage = item.minimumCoverage;
        region.sampleSpacingMeters = item.sampleSpacingMeters;
        region.tcpFrame = item.tcpFrame;
        region.orientationMode = item.orientationMode;
        region.orientationTargetFrame = item.orientationTargetFrame;
        region.orientationTargetGeometry = item.orientationTargetGeometry;
        region.orientationTargetPoint = item.orientationTargetPoint;
        region.fixedRpyDeg = item.fixedRpyDeg;
        region.directionSamples = item.directionSamples;
        region.rollSamples = item.rollSamples;
        region.minimumOrientationCoverage = item.minimumOrientationCoverage;
        region.minimumVerificationStage = item.minimumVerificationStage;
        region.collisionFreeRequired = item.collisionFreeRequired;
        region.positionToleranceMeters = item.positionToleranceMeters;
        region.orientationToleranceDeg = item.orientationToleranceDeg;
        region.minimumJointMargin = item.minimumJointMargin;
        region.minimumManipulability = item.minimumManipulability;
        snapshot.boxRegions.push_back(region);
    }
    return snapshot;
}

RequirementExecutionDiagnostic executionDiagnostic(const RequirementDiagnostic& diagnostic)
{
    RequirementExecutionDiagnostic result;
    result.code = diagnostic.code.empty() ? "REQ_INVALID" : diagnostic.code;
    switch (diagnostic.severity) {
    case RequirementDiagnosticSeverity::Info:
        result.severity = RequirementExecutionDiagnosticSeverity::Info;
        break;
    case RequirementDiagnosticSeverity::Warning:
        result.severity = RequirementExecutionDiagnosticSeverity::Warning;
        break;
    case RequirementDiagnosticSeverity::Error:
        result.severity = RequirementExecutionDiagnosticSeverity::Error;
        break;
    }
    result.requirementId = diagnostic.requirementId;
    result.field = diagnostic.field;
    result.message = diagnostic.message;
    result.source = diagnostic.source.empty() ? "engineeringrequirements" : diagnostic.source;
    return result;
}

// 把编译态条目溯源投影为执行态条目溯源：携带源 id/种类，并把与该条目相关的
// 全部编译诊断转换(executionDiagnostic)后写入 provenance.diagnostics。
RequirementItemProvenance executionProvenance(const CompiledRequirementItemProvenance& source,
                                              const std::vector<RequirementDiagnostic>& diagnostics)
{
    RequirementItemProvenance result;
    result.sourceId = source.sourceId;
    result.sourceKind = source.sourceKind;
    if (!source.diagnostics.empty()) {
        for (const RequirementDiagnostic& diagnostic : source.diagnostics)
            result.diagnostics.push_back(executionDiagnostic(diagnostic));
    } else {
        for (const RequirementDiagnostic& diagnostic : diagnostics)
            if (diagnostic.requirementId == source.sourceId)
                result.diagnostics.push_back(executionDiagnostic(diagnostic));
    }
    return result;
}

RequirementExecutionSet makeExecution(const FrozenRequirementArtifact& artifact)
{
    RequirementExecutionSet execution;
    execution.schemaVersion = 1;
    execution.provenance.requirementFingerprint = artifact.requirementFingerprint;
    execution.provenance.robotModelFingerprint = artifact.modelBinding.robotModelFingerprint;
    execution.provenance.workcellFingerprint = artifact.workcellFingerprint;
    execution.provenance.environmentFingerprint = artifact.environmentFingerprint;
    execution.provenance.compilerVersion = artifact.compilerVersion;
    execution.provenance.frozenAt = artifact.frozenAt;
    execution.provenance.sourcePath = artifact.modelBinding.sourcePath;
    for (const RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
        execution.diagnostics.push_back(executionDiagnostic(diagnostic));
    // 执行契约只承载 Included 的工位：被排除(Excluded/Invalid)的条目保留在编译
    // 审计快照与溯源中，但不进入下游执行的工位清单，避免把审计项当成执行任务。
    for (const CompiledPoseTask& item : artifact.compiled.poseTasks) {
        if (item.compileState != RequirementCompileState::Included)
            continue;
        RequirementExecutionTask task;
        task.id = item.id;
        task.name = item.name;
        task.level = static_cast<RequirementExecutionLevel>(item.level);
        task.compileState = static_cast<RequirementExecutionCompileState>(item.compileState);
        task.processType = static_cast<RequirementExecutionProcessType>(item.processType);
        task.excludedReason = item.excludedReason;
        // 逐工位携带执行态溯源(来源 + 相关诊断)。
        task.provenance = executionProvenance(item.provenance, artifact.compiled.diagnostics);
        task.refFrame = item.refFrame;
        task.tcpFrame = item.tcpFrame;
        task.position = item.position;
        task.rpyDeg = item.rpyDeg;
        task.positionToleranceMeters = item.tolerance.positionMeters;
        task.orientationToleranceDeg = item.tolerance.orientationDeg;
        task.allowToolRollFree = item.tolerance.allowToolRollFree;
        task.orientationMode = static_cast<RequirementExecutionOrientationMode>(item.orientation.mode);
        task.orientationTargetFrame = item.orientation.targetFrame;
        task.orientationTargetGeometry = item.orientation.targetGeometry;
        task.orientationTargetPoint = item.orientation.targetPoint;
        task.invertNormal = item.orientation.invertNormal;
        task.rollMinimumDeg = item.orientation.rollMinimumDeg;
        task.rollMaximumDeg = item.orientation.rollMaximumDeg;
        task.collisionFreeRequired = item.validation.collisionFreeRequired;
        task.minimumJointMargin = item.validation.minimumJointMargin;
        task.minimumManipulability = item.validation.minimumManipulability;
        task.resolutionEvidence = item.orientation.resolutionEvidence;
        task.approach.enabled = item.approach.enabled;
        task.approach.axis = item.approach.axis == OffsetAxis::ReferenceZ
            ? RequirementExecutionOffsetAxis::ReferenceZ : RequirementExecutionOffsetAxis::ToolZ;
        task.approach.distanceMeters = item.approach.distanceMeters;
        task.approach.collisionFreeRequired = item.approach.collisionFreeRequired;
        task.retract.enabled = item.retract.enabled;
        task.retract.axis = item.retract.axis == OffsetAxis::ReferenceZ
            ? RequirementExecutionOffsetAxis::ReferenceZ : RequirementExecutionOffsetAxis::ToolZ;
        task.retract.distanceMeters = item.retract.distanceMeters;
        task.retract.collisionFreeRequired = item.retract.collisionFreeRequired;
        task.pathValidationPending = item.pathValidationPending;
        for (const RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
            if (diagnostic.requirementId == item.id)
                task.diagnostics.push_back(executionDiagnostic(diagnostic));
        execution.tasks.push_back(task);
    }
    // 覆盖盒与工位采用相同的过滤规则：只有 Included 的覆盖盒进入执行契约。
    for (const WorkspaceDemandRegion& item : artifact.compiled.workspaceRegions) {
        if (item.compileState != RequirementCompileState::Included)
            continue;
        RequirementExecutionRegion region;
        region.id = item.id;
        region.name = item.name;
        region.level = static_cast<RequirementExecutionLevel>(item.level);
        region.compileState = static_cast<RequirementExecutionCompileState>(item.compileState);
        region.excludedReason = item.excludedReason;
        // 逐覆盖盒携带执行态溯源。
        region.provenance = executionProvenance(item.provenance, artifact.compiled.diagnostics);
        region.refFrame = item.refFrame;
        region.tcpFrame = item.tcpFrame;
        region.center = item.center;
        region.size = item.size;
        region.minimumCoverage = item.minimumCoverage;
        region.sampleSpacingMeters = item.sampleSpacingMeters;
        WorkspaceSamplingGrid samplingGrid;
        if (!resolveWorkspaceSamplingGrid(item.size, item.sampleSpacingMeters,
                                          item.minimumVerificationStage, samplingGrid, nullptr))
            continue;
        region.sampleCounts = samplingGrid.pointCounts;
        region.orientationMode = static_cast<RequirementExecutionOrientationMode>(item.orientationMode);
        region.orientationTargetFrame = item.orientationTargetFrame;
        region.orientationTargetGeometry = item.orientationTargetGeometry;
        region.orientationTargetPoint = item.orientationTargetPoint;
        region.fixedRpyDeg = item.fixedRpyDeg;
        region.directionSamples = item.directionSamples;
        region.rollSamples = item.rollSamples;
        region.minimumOrientationCoverage = item.minimumOrientationCoverage;
        region.minimumVerificationStage = static_cast<RequirementExecutionStage>(item.minimumVerificationStage);
        region.collisionFreeRequired = item.collisionFreeRequired;
        region.positionToleranceMeters = item.positionToleranceMeters;
        region.orientationToleranceDeg = item.orientationToleranceDeg;
        region.minimumJointMargin = item.minimumJointMargin;
        region.minimumManipulability = item.minimumManipulability;
        for (const RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
            if (diagnostic.requirementId == item.id)
                region.diagnostics.push_back(executionDiagnostic(diagnostic));
        execution.workspaceRegions.push_back(region);
    }
    return execution;
}

std::vector<FrozenCompiledItemState> snapshotCompiledItems(const CompiledRequirementSet& compiled)
{
    std::vector<FrozenCompiledItemState> result;
    for (const CompiledPoseTask& task : compiled.poseTasks)
        result.push_back({"PoseTask", task.id, task.compileState, task.excludedReason, task.provenance});
    for (const WorkspaceDemandRegion& region : compiled.workspaceRegions)
        result.push_back({"WorkspaceRegion", region.id, region.compileState,
                          region.excludedReason, region.provenance});
    return result;
}

QJsonObject diagnosticToObject(const RequirementDiagnostic& diagnostic)
{
    QJsonObject object;
    object["code"] = QString::fromStdString(diagnostic.code);
    object["requirementId"] = QString::fromStdString(diagnostic.requirementId);
    object["level"] = QString::fromLatin1(toString(diagnostic.level));
    object["severity"] = diagnostic.severity == RequirementDiagnosticSeverity::Error ? "Error" :
        (diagnostic.severity == RequirementDiagnosticSeverity::Warning ? "Warning" : "Info");
    object["field"] = QString::fromStdString(diagnostic.field);
    object["message"] = QString::fromStdString(diagnostic.message);
    object["source"] = QString::fromStdString(diagnostic.source);
    object["blocking"] = diagnostic.blocking;
    return object;
}

// 严格反序列化冻结工件诊断：所有文本字段必须显式存在且为字符串，code/message
// 不得为空，level 必须合法，blocking 必须是布尔且与 level 语义一致(Must ⇔ blocking)。
// 拒绝缺省回填与自相矛盾的诊断，防止损坏/篡改的审计记录被静默接受。
bool diagnosticFromObject(const QJsonObject& object, RequirementDiagnostic& diagnostic, std::string* error)
{
    for (const char* key : {"code", "requirementId", "level", "message"}) {
        if (!object.contains(key) || !object.value(key).isString()) {
            if (error != nullptr) *error = std::string("Frozen artifact diagnostic field is missing or has the wrong type: ") + key;
            return false;
        }
    }
    if (!object.contains("blocking") || !object.value("blocking").isBool()) {
        if (error != nullptr) *error = "Frozen artifact diagnostic blocking field is missing or has the wrong type.";
        return false;
    }
    diagnostic.code = object.value("code").toString().toStdString();
    diagnostic.requirementId = object.value("requirementId").toString().toStdString();
    if (diagnostic.code.empty() || !requirementLevelFromString(object.value("level").toString().toStdString(), diagnostic.level)) {
        if (error != nullptr) *error = "Frozen artifact diagnostic level is invalid.";
        return false;
    }
    diagnostic.message = object.value("message").toString().toStdString();
    if (diagnostic.message.empty()) {
        if (error != nullptr) *error = "Frozen artifact diagnostic message is required.";
        return false;
    }
    diagnostic.blocking = object.value("blocking").toBool();
    if (object.contains("field") && !object.value("field").isString()) {
        if (error != nullptr) *error = "Frozen artifact diagnostic field has the wrong type.";
        return false;
    }
    if (object.contains("source") && !object.value("source").isString()) {
        if (error != nullptr) *error = "Frozen artifact diagnostic source has the wrong type.";
        return false;
    }
    diagnostic.field = object.value("field").toString().toStdString();
    diagnostic.source = object.value("source").toString("engineeringrequirements.legacy").toStdString();
    if (object.contains("severity")) {
        if (!object.value("severity").isString()) {
            if (error != nullptr) *error = "Frozen artifact diagnostic severity has the wrong type.";
            return false;
        }
        const QString severity = object.value("severity").toString();
        if (severity == "Info") diagnostic.severity = RequirementDiagnosticSeverity::Info;
        else if (severity == "Warning") diagnostic.severity = RequirementDiagnosticSeverity::Warning;
        else if (severity == "Error") diagnostic.severity = RequirementDiagnosticSeverity::Error;
        else {
            if (error != nullptr) *error = "Frozen artifact diagnostic severity is invalid.";
            return false;
        }
    } else {
        diagnostic.severity = diagnostic.blocking ? RequirementDiagnosticSeverity::Error :
            (diagnostic.level == RequirementLevel::Info ? RequirementDiagnosticSeverity::Info :
                                                          RequirementDiagnosticSeverity::Warning);
    }
    // blocking 必须与 level 一致：Must 级诊断必须阻塞，其余必须非阻塞。
    if ((diagnostic.level == RequirementLevel::Must) != diagnostic.blocking) {
        if (error != nullptr) *error = "Frozen artifact diagnostic blocking does not match its level.";
        return false;
    }
    return true;
}

const char* compileStateToString(RequirementCompileState state)
{
    switch (state) {
    case RequirementCompileState::Included: return "Included";
    case RequirementCompileState::Excluded: return "Excluded";
    case RequirementCompileState::Invalid: return "Invalid";
    }
    return "Invalid";
}

bool compileStateFromString(const QString& value, RequirementCompileState& state)
{
    if (value == "Included") state = RequirementCompileState::Included;
    else if (value == "Excluded") state = RequirementCompileState::Excluded;
    else if (value == "Invalid") state = RequirementCompileState::Invalid;
    else return false;
    return true;
}

QJsonObject compiledItemStateToObject(const FrozenCompiledItemState& item)
{
    QJsonObject object;
    object["kind"] = QString::fromStdString(item.kind);
    object["id"] = QString::fromStdString(item.id);
    object["compileState"] = compileStateToString(item.compileState);
    object["excludedReason"] = QString::fromStdString(item.excludedReason);
    QJsonObject provenance;
    provenance["sourceId"] = QString::fromStdString(item.provenance.sourceId);
    provenance["sourceKind"] = QString::fromStdString(item.provenance.sourceKind);
    QJsonArray diagnostics;
    for (const RequirementDiagnostic& diagnostic : item.provenance.diagnostics)
        diagnostics.append(diagnosticToObject(diagnostic));
    provenance["diagnostics"] = diagnostics;
    object["provenance"] = provenance;
    return object;
}

bool compiledItemStateFromObject(const QJsonObject& object, FrozenCompiledItemState& item,
                                 std::string* error)
{
    for (const char* key : {"kind", "id", "compileState", "excludedReason"}) {
        if (!object.value(key).isString()) {
            if (error != nullptr) *error = std::string("Frozen compiled item field has the wrong type: ") + key;
            return false;
        }
    }
    if (!object.value("provenance").isObject()) {
        if (error != nullptr) *error = "Frozen compiled item provenance must be an object.";
        return false;
    }
    const QJsonObject provenance = object.value("provenance").toObject();
    for (const char* key : {"sourceId", "sourceKind"}) {
        if (!provenance.value(key).isString()) {
            if (error != nullptr) *error = std::string("Frozen compiled item provenance field has the wrong type: ") + key;
            return false;
        }
    }
    if (!provenance.value("diagnostics").isArray()) {
        if (error != nullptr) *error = "Frozen compiled item provenance diagnostics must be an array.";
        return false;
    }
    item.kind = object.value("kind").toString().toStdString();
    item.id = object.value("id").toString().toStdString();
    item.excludedReason = object.value("excludedReason").toString().toStdString();
    if ((item.kind != "PoseTask" && item.kind != "WorkspaceRegion") || item.id.empty() ||
        !compileStateFromString(object.value("compileState").toString(), item.compileState)) {
        if (error != nullptr) *error = "Frozen compiled item identity or state is invalid.";
        return false;
    }
    item.provenance.sourceId = provenance.value("sourceId").toString().toStdString();
    item.provenance.sourceKind = provenance.value("sourceKind").toString().toStdString();
    item.provenance.diagnostics.clear();
    item.provenance.diagnosticCodes.clear();
    for (const QJsonValue& value : provenance.value("diagnostics").toArray()) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Frozen compiled item provenance diagnostic must be an object.";
            return false;
        }
        RequirementDiagnostic diagnostic;
        if (!diagnosticFromObject(value.toObject(), diagnostic, error)) return false;
        item.provenance.diagnostics.push_back(diagnostic);
        if (!diagnostic.code.empty()) item.provenance.diagnosticCodes.push_back(diagnostic.code);
    }
    return true;
}

} // namespace

bool RequirementFreezer::validateExecutionConsistency(const FrozenRequirementArtifact& artifact,
                                                       std::string* error)
{
    if (error != nullptr) error->clear();
    // v3 及更早版本尚无执行契约，直接视为一致；只有 v4 才要求完整的执行契约审计。
    if (artifact.schemaVersion < 4) return true;
    if (artifact.schemaVersion != 4) {
        if (error != nullptr) *error = "Frozen requirement artifact schemaVersion is unsupported.";
        return false;
    }
    // ① 执行契约本体必须完整、未被篡改，且通过结构校验。
    // 若执行指纹缺失或与 execution 重算结果不符，或结构校验失败，即判定契约缺失/被改。
    if (artifact.executionFingerprint.empty() ||
        artifact.executionFingerprint != RequirementExecutionJson::fingerprint(artifact.execution) ||
        !RequirementExecutionJson::validate(artifact.execution, error)) {
        if (error != nullptr && error->empty())
            *error = "Requirement execution contract is missing or has been modified.";
        return false;
    }
    // ② 执行契约的来源(provenance)必须与工件顶层审计字段逐项一致，
    // 防止"执行契约复制自另一份工件"或"工件指纹变更后未重新冻结"。
    const RequirementExecutionProvenance& provenance = artifact.execution.provenance;
    if (provenance.requirementFingerprint != artifact.requirementFingerprint ||
        provenance.robotModelFingerprint != artifact.modelBinding.robotModelFingerprint ||
        provenance.workcellFingerprint != artifact.workcellFingerprint ||
        provenance.environmentFingerprint != artifact.environmentFingerprint ||
        provenance.compilerVersion != artifact.compilerVersion ||
        provenance.frozenAt != artifact.frozenAt ||
        provenance.sourcePath != artifact.modelBinding.sourcePath) {
        if (error != nullptr)
            *error = "Requirement execution contract provenance does not match the frozen artifact.";
        return false;
    }
    // ③ 由编译快照投影出的执行契约必须与存档执行契约指纹一致，
    // 确保编译快照与执行契约没有各自独立漂移。
    RequirementExecutionSet expectedExecution = makeExecution(artifact);
    expectedExecution.extensions = artifact.execution.extensions;
    for (RequirementExecutionTask& expectedTask : expectedExecution.tasks) {
        for (const RequirementExecutionTask& storedTask : artifact.execution.tasks) {
            if (storedTask.id == expectedTask.id) {
                expectedTask.extensions = storedTask.extensions;
                break;
            }
        }
    }
    for (RequirementExecutionRegion& expectedRegion : expectedExecution.workspaceRegions) {
        for (const RequirementExecutionRegion& storedRegion : artifact.execution.workspaceRegions) {
            if (storedRegion.id == expectedRegion.id) {
                expectedRegion.extensions = storedRegion.extensions;
                break;
            }
        }
    }
    if (RequirementExecutionJson::fingerprint(expectedExecution) != artifact.executionFingerprint) {
        if (error != nullptr)
            *error = "Frozen requirement compiled snapshot does not match execution.";
        return false;
    }
    return true;
}

bool RequirementFreezer::freeze(const RequirementSet& requirements,
                                 const rw::models::WorkCell& workcell,
                                 const rw::kinematics::State& state,
                                 const RobotModelSpec& model,
                                 FrozenRequirementArtifact& artifact,
                                 std::string* error)
{
    return freeze(requirements, workcell, state, model, artifact, error, std::string());
}

bool RequirementFreezer::freeze(const RequirementSet& requirements, const rw::models::WorkCell& workcell,
                                 const rw::kinematics::State& state, const RobotModelSpec& model,
                                 FrozenRequirementArtifact& artifact, std::string* error,
                                 const std::string& projectRoot)
{
    // 需求身份描述工程师冻结前提交的意图，而非随后由解析器补入的代表姿态和证据。
    // 这样 isCurrent() 使用原始 RequirementSet 复核时，动态姿态工位不会被误判为已变更。
    const std::string sourceRequirementFingerprint = RequirementCompiler::fingerprint(requirements);
    const std::string expectedModelFingerprint = RobotModelFingerprint::canonicalSha256(model);
    if (requirements.modelBinding.robotModelFingerprint.empty() ||
        requirements.modelBinding.robotModelFingerprint != expectedModelFingerprint ||
        (!requirements.modelBinding.robotName.empty() && requirements.modelBinding.robotName != model.robotName)) {
        if (error != nullptr) *error = "The bound RobotModelSpec does not match the model used for freezing.";
        return false;
    }
    const rw::models::Device::Ptr device = workcell.findDevice(model.robotName);
    if (device == nullptr || device->getBase() == nullptr || device->getEnd() == nullptr) {
        if (error != nullptr)
            *error = "The WorkCell does not contain the robot device required for freezing: " + model.robotName;
        return false;
    }

    // TCP 归属校验辅助：判断给定 Frame 是否属于"绑定机器人"设备(沿父链向上
    // 能到达该设备的 Base 帧)。工位/覆盖盒的 TCP 必须落在绑定设备内，否则把另一
    // 台设备的末端当作本设备 TCP 会把错误的运动链端点送进后续 IK/采样分析。
    const auto tcpBelongsToBoundDevice = [&] (const std::string& frameName) {
        const rw::kinematics::Frame* frame = findFrame(workcell, frameName);
        return frame != nullptr && belongsToDevice(frame, device->getBase(), state);
    };

    RequirementSet resolved = requirements;
    std::vector<RequirementDiagnostic> environmentDiagnostics;
    for (PoseTask& task : resolved.poseTasks) {
        if (findFrame(workcell, task.refFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station reference frame is unavailable in the current WorkCell: " + task.refFrame,
                                     "REQ_FRAME_NOT_FOUND");
        if (task.tcpFrame.empty() || findFrame(workcell, task.tcpFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station TCP frame is unavailable in the current WorkCell: " + task.tcpFrame,
                                     "REQ_TCP_FRAME_NOT_FOUND");
        // TCP Frame 虽在场景中存在但不属于绑定设备：生成 WRONG_DEVICE 环境诊断，
        // 防止把别的机器人末端误当作本设备的运动学端点。
        else if (!tcpBelongsToBoundDevice(task.tcpFrame))
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station TCP frame does not belong to the bound robot device: " + task.tcpFrame,
                                     "REQ_TCP_FRAME_WRONG_DEVICE");
        if ((task.orientation.mode == OrientationMode::AlignFrame || task.orientation.mode == OrientationMode::PointAtTarget) &&
            !task.orientation.targetFrame.empty() && findFrame(workcell, task.orientation.targetFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station orientation target frame is unavailable in the current WorkCell: " + task.orientation.targetFrame,
                                     "REQ_ORIENTATION_TARGET_NOT_FOUND");
        if (task.orientation.mode == OrientationMode::AlignGeometryNormal &&
            (!task.orientation.targetFrame.empty() && findFrame(workcell, task.orientation.targetFrame) == nullptr))
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station geometry orientation target frame is unavailable in the current WorkCell: " + task.orientation.targetFrame,
                                     "REQ_ORIENTATION_TARGET_NOT_FOUND");
        if (task.orientation.mode == OrientationMode::AlignGeometryNormal &&
            task.orientation.targetGeometry.rfind("frame:", 0) == 0 &&
            findFrame(workcell, task.orientation.targetGeometry.substr(6)) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station geometry target is unavailable in the current WorkCell: " + task.orientation.targetGeometry,
                                     "REQ_GEOMETRY_TARGET_NOT_FOUND");
        if (task.source == PoseTaskSource::GeometryFeature) {
            std::string resolutionError;
            if (!GeometryFeatureResolver::applyToStation(task.geometryFeature, workcell, state, task, &resolutionError))
                addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                         "Key station geometry feature cannot be resolved: " + resolutionError,
                                         "REQ_GEOMETRY_TARGET_NOT_FOUND");
        }
        std::string orientationError;
        if (!OrientationRuleResolver::applyToStation(task, workcell, state, &orientationError))
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station orientation rule cannot be resolved: " + orientationError,
                                     "REQ_ORIENTATION_TARGET_NOT_FOUND");
    }
    for (const BoxRegion& region : resolved.boxRegions) {
        if (findFrame(workcell, region.refFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region reference frame is unavailable in the current WorkCell: " + region.refFrame,
                                     "REQ_FRAME_NOT_FOUND");
        if (region.tcpFrame.empty() || findFrame(workcell, region.tcpFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region TCP frame is unavailable in the current WorkCell: " + region.tcpFrame,
                                     "REQ_TCP_FRAME_NOT_FOUND");
        // 覆盖盒 TCP 同样要求归属于绑定设备，与工位的 WRONG_DEVICE 规则保持一致。
        else if (!tcpBelongsToBoundDevice(region.tcpFrame))
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region TCP frame does not belong to the bound robot device: " + region.tcpFrame,
                                     "REQ_TCP_FRAME_WRONG_DEVICE");
        if ((region.orientationMode == OrientationMode::AlignFrame ||
             region.orientationMode == OrientationMode::AlignGeometryNormal ||
             (region.orientationMode == OrientationMode::PointAtTarget &&
              !region.orientationTargetFrame.empty())) &&
            (region.orientationTargetFrame.empty() ||
            findFrame(workcell, region.orientationTargetFrame) == nullptr))
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region orientation target frame is unavailable in the current WorkCell: " + region.orientationTargetFrame,
                                     "REQ_ORIENTATION_TARGET_NOT_FOUND");
        if (region.orientationMode == OrientationMode::AlignGeometryNormal &&
            region.orientationTargetGeometry.rfind("frame:", 0) == 0 &&
            findFrame(workcell, region.orientationTargetGeometry.substr(6)) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region geometry target is unavailable in the current WorkCell: " + region.orientationTargetGeometry,
                                     "REQ_GEOMETRY_TARGET_NOT_FOUND");
    }
    for (const RequirementDiagnostic& diagnostic : environmentDiagnostics) {
        if (diagnostic.blocking) {
            if (error != nullptr) *error = diagnostic.message;
            return false;
        }
    }

    CompiledRequirementSet compiled;
    if (!RequirementCompiler::compile(resolved, compiled, error)) return false;
    compiled.diagnostics.insert(compiled.diagnostics.end(), environmentDiagnostics.begin(), environmentDiagnostics.end());
    // 编译器已记录编辑态诊断；冻结阶段新增的 WorkCell 诊断也必须进入逐项
    // provenance，才能让执行契约准确定位 refFrame/tcpFrame/姿态目标错误。
    const auto appendEnvironmentProvenance = [&environmentDiagnostics] (auto& items) {
        for (auto& item : items) {
            for (const RequirementDiagnostic& diagnostic : environmentDiagnostics) {
                if (diagnostic.requirementId != item.id) continue;
                item.provenance.diagnostics.push_back(diagnostic);
                if (!diagnostic.code.empty())
                    item.provenance.diagnosticCodes.push_back(diagnostic.code);
            }
        }
    };
    appendEnvironmentProvenance(compiled.poseTasks);
    appendEnvironmentProvenance(compiled.workspaceRegions);
    // 环境诊断命中的工位/覆盖盒不再从 compiled 中删除，而是标记为 Excluded 并写入
    // excludedReason。这样被排除项仍保留在审计记录中(可追溯"为什么没进优化")，
    // 同时下游只消费 Included 项，语义与旧"直接擦除"行为等价但信息更完整。
    for (CompiledPoseTask& task : compiled.poseTasks) {
        if (hasDiagnostic(environmentDiagnostics, task.id)) {
            task.compileState = RequirementCompileState::Excluded;
            task.excludedReason = "Excluded because the current WorkCell cannot resolve the requirement environment.";
        }
    }
    for (WorkspaceDemandRegion& region : compiled.workspaceRegions) {
        if (hasDiagnostic(environmentDiagnostics, region.id)) {
            region.compileState = RequirementCompileState::Excluded;
            region.excludedReason = "Excluded because the current WorkCell cannot resolve the requirement environment.";
        }
    }

    artifact = FrozenRequirementArtifact();
    artifact.schemaVersion = 4;
    artifact.requirementFingerprint = sourceRequirementFingerprint;
    artifact.environmentFingerprint = environmentFingerprint(workcell, state, model.robotName);
    artifact.workcellFingerprint = artifact.environmentFingerprint;
    // 使用 UTC ISO-8601 毫秒时间戳，使审计记录可跨时区比较且不依赖本机显示格式。
    artifact.frozenAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    artifact.modelBinding = resolved.modelBinding;
    artifact.frozenRobotState = captureRobotState(workcell, state, model.robotName);
    artifact.frozenRobotState.capturedAt = artifact.frozenAt;
    // 场景快照必须在完成 Frame/几何/姿态解析之后生成，使其与本次编译输入使用的
    // WorkCell State 完全一致。后续候选工厂由该快照重建工装与工件，而不依赖当前 UI。
    artifact.scenario = makeScenarioSnapshot(workcell, state, model, projectRoot);
    artifact.scenario.environmentFingerprint = artifact.environmentFingerprint;
    artifact.scenario.stateFingerprint = artifact.environmentFingerprint;
    artifact.scenario.snapshotFingerprint = scenarioFingerprint(artifact.scenario);
    artifact.compiled = compiled;
    artifact.compiled.requirementFingerprint = artifact.requirementFingerprint;
    artifact.compiledItems = snapshotCompiledItems(artifact.compiled);
    artifact.execution = makeExecution(artifact);
    artifact.executionFingerprint = RequirementExecutionJson::fingerprint(artifact.execution);
    if (error != nullptr) error->clear();
    return true;
}

bool RequirementFreezer::isCurrent(const FrozenRequirementArtifact& artifact,
                                   const RequirementSet& requirements,
                                   const rw::models::WorkCell& workcell,
                                   const rw::kinematics::State& state,
                                   const RobotModelSpec& model,
                                   std::string* error)
{
    return isCurrent(artifact, requirements, workcell, state, model, error, std::string(),
                     nullptr);
}

bool RequirementFreezer::isCurrent(const FrozenRequirementArtifact& artifact,
                                   const RequirementSet& requirements,
                                   const rw::models::WorkCell& workcell,
                                   const rw::kinematics::State& state,
                                   const RobotModelSpec& model,
                                   std::string* error,
                                   const std::string& artifactBaseDirectory,
                                   FrozenRequirementValidationResult* validationResult)
{
    if (validationResult != nullptr)
        *validationResult = FrozenRequirementValidationResult();

    // 顶层绑定与已编译快照都要一致。双重检查能识别手工编辑 JSON 时只改了
    // 其中一层的情况，避免下游消费者读到互相矛盾的模型身份信息。
    if ((artifact.schemaVersion != 3 && artifact.schemaVersion != 4) || artifact.requirementFingerprint.empty() ||
        artifact.environmentFingerprint.empty()) {
        if (error != nullptr) *error = "Frozen requirement artifact uses legacy state-based evidence. Validate and freeze the requirements again.";
        return false;
    }
    if (artifact.modelBinding.robotModelFingerprint.empty() ||
        artifact.compiled.modelBinding.robotModelFingerprint != artifact.modelBinding.robotModelFingerprint ||
        artifact.compiled.modelBinding.robotName != artifact.modelBinding.robotName) {
        if (error != nullptr) *error = "Frozen requirement artifact model binding is internally inconsistent.";
        return false;
    }

    const std::string expectedModelFingerprint = RobotModelFingerprint::canonicalSha256(model);
    if (artifact.modelBinding.robotModelFingerprint != expectedModelFingerprint ||
        requirements.modelBinding.robotModelFingerprint != expectedModelFingerprint ||
        (!artifact.modelBinding.robotName.empty() && artifact.modelBinding.robotName != model.robotName) ||
        (!requirements.modelBinding.robotName.empty() && requirements.modelBinding.robotName != model.robotName)) {
        if (error != nullptr) *error = "Frozen requirement artifact does not match the current RobotModelSpec.";
        return false;
    }

    const std::string expectedRequirementFingerprint = RequirementCompiler::fingerprint(requirements);
    if (artifact.requirementFingerprint != expectedRequirementFingerprint ||
        artifact.compiled.requirementFingerprint != artifact.requirementFingerprint) {
        if (error != nullptr) *error = "Frozen requirement artifact does not match the current requirement set.";
        return false;
    }

    // v4 执行契约本体与来源审计：执行子契约 schema 必须为 1、执行指纹必须存在且
    // 与 execution 重算一致，来源指纹须与工件顶层字段一致。v4 工件在执行契约缺失
    // 或被动过手脚时一律判定为过期，而不再像旧逻辑那样允许"无执行契约"绕过。
    if (artifact.schemaVersion >= 4 &&
        (artifact.execution.schemaVersion != 1 || artifact.executionFingerprint.empty() ||
         artifact.executionFingerprint != RequirementExecutionJson::fingerprint(artifact.execution) ||
         artifact.execution.provenance.requirementFingerprint != artifact.requirementFingerprint ||
         artifact.execution.provenance.robotModelFingerprint != artifact.modelBinding.robotModelFingerprint ||
         artifact.execution.provenance.workcellFingerprint != artifact.workcellFingerprint ||
         artifact.execution.provenance.environmentFingerprint != artifact.environmentFingerprint ||
         artifact.execution.provenance.compilerVersion != artifact.compilerVersion ||
         artifact.execution.provenance.frozenAt != artifact.frozenAt)) {
        if (error != nullptr) *error = "Frozen requirement execution is missing or has been modified.";
        return false;
    }
    // 交叉一致性：由 compiled 编译快照投影出的执行契约指纹必须与存档执行契约一致，
    // 防止编译快照与执行契约各自漂移导致下游拿到互相矛盾的两份数据。
    if (artifact.schemaVersion >= 4 &&
        RequirementExecutionJson::fingerprint(makeExecution(artifact)) != artifact.executionFingerprint) {
        if (error != nullptr) *error = "Frozen requirement compiled snapshot does not match execution.";
        return false;
    }

    if (!validateScenario(artifact, workcell, state, validationResult, error,
                          artifactBaseDirectory))
        return false;

    if (error != nullptr) error->clear();
    return true;
}

bool RequirementFreezer::isScenarioCurrent(const FrozenRequirementArtifact& artifact,
                                           const rw::models::WorkCell& workcell,
                                           const rw::kinematics::State& state,
                                           std::string* error)
{
    return isScenarioCurrent(artifact, workcell, state, error, std::string());
}

bool RequirementFreezer::isScenarioCurrent(const FrozenRequirementArtifact& artifact,
                                           const rw::models::WorkCell& workcell,
                                           const rw::kinematics::State& state,
                                           std::string* error,
                                           const std::string& artifactBaseDirectory)
{
    return validateScenario(
        artifact, workcell, state, nullptr, error, artifactBaseDirectory);
}

bool RequirementFreezer::validateScenario(const FrozenRequirementArtifact& artifact,
                                          const rw::models::WorkCell& workcell,
                                          const rw::kinematics::State& state,
                                          FrozenRequirementValidationResult* result,
                                          std::string* error)
{
    return validateScenario(artifact, workcell, state, result, error, std::string());
}

bool RequirementFreezer::validateScenario(const FrozenRequirementArtifact& artifact,
                                          const rw::models::WorkCell& workcell,
                                          const rw::kinematics::State& state,
                                          FrozenRequirementValidationResult* result,
                                          std::string* error,
                                          const std::string& artifactBaseDirectory)
{
    if (result != nullptr) *result = FrozenRequirementValidationResult();
    if (artifact.schemaVersion != 3 && artifact.schemaVersion != 4) {
        if (error != nullptr) *error = "Frozen requirement artifact uses legacy state-based evidence. Validate and freeze the requirements again.";
        return false;
    }
    if (artifact.environmentFingerprint.empty() || artifact.frozenRobotState.deviceName.empty() ||
        artifact.frozenRobotState.tcpFrameName.empty() ||
        artifact.frozenRobotState.kinematicFingerprint.empty() ||
        artifact.scenario.schemaVersion != 2 || artifact.scenario.environmentFingerprint.empty() ||
        artifact.scenario.snapshotFingerprint.empty() ||
        artifact.scenario.snapshotFingerprint != scenarioFingerprint(artifact.scenario)) {
        if (error != nullptr) *error = "Frozen requirement artifact scene snapshot is incomplete or has been modified.";
        return false;
    }
    const FrozenRobotStateSnapshot currentRobotState = captureRobotState(
        workcell, state, artifact.frozenRobotState.deviceName);
    if (currentRobotState.tcpFrameName != artifact.frozenRobotState.tcpFrameName ||
        currentRobotState.kinematicFingerprint != artifact.frozenRobotState.kinematicFingerprint) {
        if (error != nullptr)
            *error = "Robot model or TCP configuration has changed. Validate and freeze the requirements again.";
        return false;
    }
    const std::string currentEnvironment = environmentFingerprint(workcell, state, artifact.frozenRobotState.deviceName);
    if (artifact.environmentFingerprint != currentEnvironment ||
        artifact.scenario.environmentFingerprint != artifact.environmentFingerprint) {
        if (error != nullptr) *error = "Fixture or external environment position has changed. Validate and freeze the requirements again.";
        return false;
    }
    if (!artifact.scenario.sourceWorkCellPath.empty()) {
        QString sourcePath = QString::fromStdString(artifact.scenario.sourceWorkCellPath);
        if (QFileInfo(sourcePath).isRelative()) {
            if (artifactBaseDirectory.empty()) {
                if (result != nullptr) {
                    result->warnings.push_back(
                        "Frozen requirement artifact source WorkCell path is relative, but no "
                        "base directory was provided; source provenance could not be verified.");
                }
                sourcePath.clear();
            } else {
                sourcePath = QDir(QString::fromStdString(artifactBaseDirectory))
                                 .absoluteFilePath(sourcePath);
            }
        }
        const std::string sourceFingerprint = sourcePath.isEmpty() ? std::string() :
            fileFingerprint(sourcePath.toStdString());
        if (!sourcePath.isEmpty() &&
            (sourceFingerprint.empty() ||
             sourceFingerprint != artifact.scenario.sourceFileFingerprint)) {
            if (result != nullptr) {
                result->warnings.push_back(
                    "Frozen requirement artifact source WorkCell file is missing or has changed; "
                    "the active model, TCP, and environment still match the frozen evidence.");
            }
        }
    }
    if (result != nullptr) {
        result->frozenRobotState = artifact.frozenRobotState;
        result->currentRobotState = currentRobotState;
        result->robotStateChanged = !sameQ(result->frozenRobotState, result->currentRobotState);
    }
    if (error != nullptr) error->clear();
    return true;
}

QJsonObject FrozenRequirementArtifactJson::toObject(const FrozenRequirementArtifact& artifact)
{
    QJsonObject object;
    object["type"] = "FrozenEngineeringRequirementArtifact";
    object["schemaVersion"] = artifact.schemaVersion;
    object["requirementFingerprint"] = QString::fromStdString(artifact.requirementFingerprint);
    RequirementExecutionSet serializedExecution;
    if (artifact.schemaVersion >= 4) {
        serializedExecution = artifact.executionFingerprint.empty()
            ? makeExecution(artifact) : artifact.execution;
        object["executionFingerprint"] = QString::fromStdString(
            artifact.executionFingerprint.empty()
                ? RequirementExecutionJson::fingerprint(serializedExecution)
                : artifact.executionFingerprint);
    }
    object["environmentFingerprint"] = QString::fromStdString(artifact.environmentFingerprint);
    object["workcellFingerprint"] = QString::fromStdString(artifact.workcellFingerprint);
    object["compilerVersion"] = QString::fromStdString(artifact.compilerVersion);
    object["frozenAt"] = QString::fromStdString(artifact.frozenAt);
    if (!artifact.publication.revisionId.empty() || artifact.publication.revisionNumber > 0 ||
        !artifact.publication.state.empty()) {
        QJsonObject publication;
        publication["revisionNumber"] = artifact.publication.revisionNumber;
        publication["revisionId"] = QString::fromStdString(artifact.publication.revisionId);
        publication["state"] = QString::fromStdString(artifact.publication.state);
        publication["publishedAt"] = QString::fromStdString(artifact.publication.publishedAt);
        publication["parentRevisionId"] = QString::fromStdString(artifact.publication.parentRevisionId);
        object["publication"] = publication;
    }
    QJsonObject binding;
    binding["sourcePath"] = QString::fromStdString(artifact.modelBinding.sourcePath);
    binding["robotModelFingerprint"] = QString::fromStdString(artifact.modelBinding.robotModelFingerprint);
    binding["robotName"] = QString::fromStdString(artifact.modelBinding.robotName);
    object["modelBinding"] = binding;
    if (artifact.schemaVersion >= 2) {
        QJsonObject scenario;
        scenario["schemaVersion"] = artifact.scenario.schemaVersion;
        scenario["sourceWorkCellPath"] = QString::fromStdString(artifact.scenario.sourceWorkCellPath);
        scenario["sourceFileFingerprint"] = QString::fromStdString(artifact.scenario.sourceFileFingerprint);
        scenario["snapshotFingerprint"] = QString::fromStdString(artifact.scenario.snapshotFingerprint);
        scenario["deviceName"] = QString::fromStdString(artifact.scenario.deviceName);
        scenario["environmentFingerprint"] = QString::fromStdString(artifact.scenario.environmentFingerprint);
        scenario["stateFingerprint"] = QString::fromStdString(artifact.scenario.stateFingerprint);
        scenario["sceneSpec"] = RobotModelSpecJson::toObject(artifact.scenario.sceneSpec);
        object["scenario"] = scenario;
    }
    if (artifact.schemaVersion >= 3) {
        QJsonObject robotState;
        robotState["deviceName"] = QString::fromStdString(artifact.frozenRobotState.deviceName);
        robotState["tcpFrameName"] = QString::fromStdString(artifact.frozenRobotState.tcpFrameName);
        robotState["kinematicFingerprint"] = QString::fromStdString(artifact.frozenRobotState.kinematicFingerprint);
        QJsonArray q;
        for (const double value : artifact.frozenRobotState.q) q.append(value);
        robotState["q"] = q;
        QJsonArray tcpWorldPose;
        for (const double value : artifact.frozenRobotState.tcpWorldPose) tcpWorldPose.append(value);
        robotState["tcpWorldPose"] = tcpWorldPose;
        robotState["capturedAt"] = QString::fromStdString(artifact.frozenRobotState.capturedAt);
        object["frozenRobotState"] = robotState;
    }
    object["compiledRequirements"] = RequirementSetJson::toObject(compiledSnapshot(artifact.compiled));
    QJsonArray compiledItems;
    const std::vector<FrozenCompiledItemState> itemStates = artifact.compiledItems.empty()
        ? snapshotCompiledItems(artifact.compiled) : artifact.compiledItems;
    for (const FrozenCompiledItemState& item : itemStates)
        compiledItems.append(compiledItemStateToObject(item));
    object["compiledItems"] = compiledItems;
    QJsonArray diagnostics;
    for (const RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
        diagnostics.append(diagnosticToObject(diagnostic));
    object["diagnostics"] = diagnostics;
    if (artifact.schemaVersion >= 4)
        object["execution"] = RequirementExecutionJson::toObject(serializedExecution);
    return object;
}

bool FrozenRequirementArtifactJson::fromObject(const QJsonObject& object,
                                                FrozenRequirementArtifact& artifact,
                                                std::string* error)
{
    if (object.value("type").toString() != "FrozenEngineeringRequirementArtifact") {
        if (error != nullptr) *error = "Frozen requirement artifact JSON has an unsupported schema.";
        return false;
    }
    // schemaVersion 必须显式存在；v4 起对工件做严格的字段完整性校验。
    if (!object.contains("schemaVersion") || !object.value("schemaVersion").isDouble()) {
        if (error != nullptr) *error = "Frozen requirement artifact schemaVersion is required.";
        return false;
    }
    const int inputSchemaVersion = object.value("schemaVersion").toInt();
    // —— v4 字段完整性门禁 ——
    // v4 工件是下游分析的唯一可信输入，顶层审计指纹、模型绑定、场景快照、冻结
    // 机器人状态与执行契约都必须显式存在且类型正确，缺失/空值/类型错误一律拒绝，
    // 防止不完整的旧工件或损坏数据被当作已验证输入继续使用。
    if (inputSchemaVersion >= 4) {
        // 顶层指纹与审计字段必须存在且非空字符串。
        for (const char* key : {"requirementFingerprint", "environmentFingerprint",
                                "workcellFingerprint", "compilerVersion", "frozenAt"}) {
            if (!object.contains(key) || !object.value(key).isString() ||
                object.value(key).toString().trimmed().isEmpty()) {
                if (error != nullptr) *error = std::string("Frozen requirement artifact field is missing or empty: ") + key;
                return false;
            }
        }
        // 四个核心子对象必须存在。
        if (!object.value("modelBinding").isObject() ||
            !object.value("scenario").isObject() ||
            !object.value("frozenRobotState").isObject() ||
            !object.value("execution").isObject()) {
            if (error != nullptr) *error = "Frozen requirement artifact v4 requires modelBinding, scenario, frozenRobotState and execution objects.";
            return false;
        }
        // modelBinding 三字段必须为字符串，且 robotModelFingerprint 非空。
        const QJsonObject binding = object.value("modelBinding").toObject();
        for (const char* key : {"sourcePath", "robotModelFingerprint", "robotName"}) {
            if (!binding.contains(key) || !binding.value(key).isString()) {
                if (error != nullptr) *error = std::string("Frozen requirement artifact modelBinding field has the wrong type: ") + key;
                return false;
            }
        }
        if (!binding.value("robotModelFingerprint").isString() ||
            binding.value("robotModelFingerprint").toString().trimmed().isEmpty()) {
            if (error != nullptr) *error = "Frozen requirement artifact modelBinding.robotModelFingerprint is required.";
            return false;
        }
        // scenario 各字段必须为字符串，sceneSpec 与 schemaVersion 必须存在。
        const QJsonObject scenario = object.value("scenario").toObject();
        for (const char* key : {"sourceWorkCellPath", "sourceFileFingerprint", "snapshotFingerprint",
                                "deviceName", "environmentFingerprint", "stateFingerprint"}) {
            if (!scenario.contains(key) || !scenario.value(key).isString()) {
                if (error != nullptr) *error = std::string("Frozen requirement artifact scenario field has the wrong type: ") + key;
                return false;
            }
        }
        if (!scenario.value("sceneSpec").isObject() || !scenario.value("schemaVersion").isDouble()) {
            if (error != nullptr) *error = "Frozen requirement artifact scenario is incomplete.";
            return false;
        }
        // frozenRobotState 字段必须为字符串，q 与 tcpWorldPose 必须为数组。
        const QJsonObject robotState = object.value("frozenRobotState").toObject();
        for (const char* key : {"deviceName", "tcpFrameName", "kinematicFingerprint", "capturedAt"}) {
            if (!robotState.contains(key) || !robotState.value(key).isString()) {
                if (error != nullptr) *error = std::string("Frozen requirement artifact robot state field has the wrong type: ") + key;
                return false;
            }
        }
        if (!robotState.value("q").isArray() || !robotState.value("tcpWorldPose").isArray()) {
            if (error != nullptr) *error = "Frozen requirement artifact robot state arrays are required.";
            return false;
        }
    }
    RequirementSet snapshot;
    if (!RequirementSetJson::fromObject(object.value("compiledRequirements").toObject(), snapshot, error)) return false;
    // v3 快照迁移：旧版编译快照不携带工作区 TCP 字段，也不记录验证阶段。
    // 为保持历史语义并让 v4 对字段保持严格：
    //   - 空 TCP 一律回填 "TCP"；
    //   - 覆盖盒验证阶段统一回填为 Quick(旧快照缺乏 Verified 证据，不允许
    //     以未经验证的状态当作 Verified 证据进入下游优化)。
    if (inputSchemaVersion == 3) {
        for (PoseTask& task : snapshot.poseTasks)
            if (task.tcpFrame.empty()) task.tcpFrame = "TCP";
        for (BoxRegion& region : snapshot.boxRegions) {
            if (region.tcpFrame.empty()) region.tcpFrame = "TCP";
            region.minimumVerificationStage = RequirementVerificationStage::Quick;
        }
    }
    CompiledRequirementSet compiled;
    if (!RequirementCompiler::compile(snapshot, compiled, error)) return false;
    FrozenRequirementArtifact parsed;
    parsed.schemaVersion = object.value("schemaVersion").toInt(1);
    if (parsed.schemaVersion != 3 && parsed.schemaVersion != 4) {
        if (error != nullptr) *error = "Frozen requirement artifact uses legacy state-based evidence. Validate and freeze the requirements again.";
        return false;
    }
    parsed.requirementFingerprint = object.value("requirementFingerprint").toString().toStdString();
    parsed.executionFingerprint = object.value("executionFingerprint").toString().toStdString();
    parsed.environmentFingerprint = object.value("environmentFingerprint").toString().toStdString();
    parsed.workcellFingerprint = object.value("workcellFingerprint").toString().toStdString();
    parsed.compilerVersion = object.value("compilerVersion").toString("EngineeringRequirements.Freezer.1").toStdString();
    parsed.frozenAt = object.value("frozenAt").toString().toStdString();
    if (object.value("publication").isObject()) {
        const QJsonObject publication = object.value("publication").toObject();
        parsed.publication.revisionNumber = publication.value("revisionNumber").toInt();
        parsed.publication.revisionId = publication.value("revisionId").toString().toStdString();
        parsed.publication.state = publication.value("state").toString().toStdString();
        parsed.publication.publishedAt = publication.value("publishedAt").toString().toStdString();
        parsed.publication.parentRevisionId = publication.value("parentRevisionId").toString().toStdString();
    }
    const QJsonObject binding = object.value("modelBinding").toObject();
    parsed.modelBinding.sourcePath = binding.value("sourcePath").toString().toStdString();
    parsed.modelBinding.robotModelFingerprint = binding.value("robotModelFingerprint").toString().toStdString();
    parsed.modelBinding.robotName = binding.value("robotName").toString().toStdString();
    if (parsed.schemaVersion >= 2) {
        const QJsonObject scenario = object.value("scenario").toObject();
        parsed.scenario.schemaVersion = scenario.value("schemaVersion").toInt(1);
        parsed.scenario.sourceWorkCellPath = scenario.value("sourceWorkCellPath").toString().toStdString();
        parsed.scenario.sourceFileFingerprint = scenario.value("sourceFileFingerprint").toString().toStdString();
        parsed.scenario.snapshotFingerprint = scenario.value("snapshotFingerprint").toString().toStdString();
        parsed.scenario.deviceName = scenario.value("deviceName").toString().toStdString();
        parsed.scenario.environmentFingerprint = scenario.value("environmentFingerprint").toString().toStdString();
        parsed.scenario.stateFingerprint = scenario.value("stateFingerprint").toString().toStdString();
        if (!scenario.value("sceneSpec").isObject() ||
            !RobotModelSpecJson::fromObject(scenario.value("sceneSpec").toObject(), parsed.scenario.sceneSpec, error))
            return false;
    }
    const QJsonObject robotState = object.value("frozenRobotState").toObject();
    parsed.frozenRobotState.deviceName = robotState.value("deviceName").toString().toStdString();
    parsed.frozenRobotState.tcpFrameName = robotState.value("tcpFrameName").toString().toStdString();
    parsed.frozenRobotState.kinematicFingerprint =
        robotState.value("kinematicFingerprint").toString().toStdString();
    for (const QJsonValue& value : robotState.value("q").toArray()) {
        // 严格类型校验：关节值必须是数字；非数字 JSON 值会在 toDouble 时被静默
        // 转为 0，掩盖损坏的工件，因此先显式检查元素类型。
        if (!value.isDouble()) {
            if (error != nullptr) *error = "Frozen requirement artifact robot state joint value has the wrong type.";
            return false;
        }
        const double q = value.toDouble();
        if (!std::isfinite(q)) {
            if (error != nullptr) *error = "Frozen requirement artifact robot state contains a non-finite joint value.";
            return false;
        }
        parsed.frozenRobotState.q.push_back(q);
    }
    const QJsonArray tcpWorldPose = robotState.value("tcpWorldPose").toArray();
    if (parsed.environmentFingerprint.empty() || parsed.frozenRobotState.deviceName.empty() ||
        parsed.frozenRobotState.tcpFrameName.empty() ||
        parsed.frozenRobotState.kinematicFingerprint.empty() ||
        tcpWorldPose.size() != static_cast<int>(parsed.frozenRobotState.tcpWorldPose.size())) {
        if (error != nullptr) *error = "Frozen requirement artifact v3 evidence is incomplete.";
        return false;
    }
    for (int index = 0; index < tcpWorldPose.size(); ++index) {
        // 与关节值相同的严格类型校验：TCP 位姿元素必须是数字，拒绝被 toDouble
        // 静默吞掉的非数字损坏数据。
        if (!tcpWorldPose[index].isDouble()) {
            if (error != nullptr) *error = "Frozen requirement artifact robot state TCP pose value has the wrong type.";
            return false;
        }
        const double value = tcpWorldPose[index].toDouble();
        if (!std::isfinite(value)) {
            if (error != nullptr) *error = "Frozen requirement artifact robot state contains a non-finite TCP pose value.";
            return false;
        }
        parsed.frozenRobotState.tcpWorldPose[static_cast<std::size_t>(index)] = value;
    }
    parsed.frozenRobotState.capturedAt = robotState.value("capturedAt").toString().toStdString();
    if (parsed.frozenRobotState.capturedAt.empty()) {
        if (error != nullptr) *error = "Frozen requirement artifact v3 robot state timestamp is missing.";
        return false;
    }
    parsed.compiled = compiled;
    parsed.compiled.requirementFingerprint = parsed.requirementFingerprint;
    // 序列化诊断是冻结时刻的审计记录。上面的编译只用于重建结构投影；若沿用本次
    // 重新编译生成的诊断，会重复产生建议项诊断，并在重载时改变 v4 执行指纹。
    // 因此这里强制要求工件自带 diagnostics 数组，并用它整体覆盖 compiled 诊断。
    if (!object.contains("diagnostics") || !object.value("diagnostics").isArray()) {
        if (error != nullptr) *error = "Frozen requirement artifact diagnostics must be an array.";
        return false;
    }
    parsed.compiled.diagnostics.clear();
    for (const QJsonValue& value : object.value("diagnostics").toArray()) {
        if (!value.isObject()) {
            if (error != nullptr) *error = "Frozen requirement artifact diagnostic must be an object.";
            return false;
        }
        RequirementDiagnostic diagnostic;
        if (!diagnosticFromObject(value.toObject(), diagnostic, error)) return false;
        parsed.compiled.diagnostics.push_back(diagnostic);
    }
    // v4 新工件以 compiledItems 为编译状态的权威来源。诊断码仅用于人类审计，
    // 不得决定 Included/Excluded/Invalid；只有旧工件缺少该快照时才走兼容回退。
    const bool hasExplicitCompiledItems = object.contains("compiledItems");
    if (parsed.schemaVersion >= 4 && !hasExplicitCompiledItems) {
        if (error != nullptr) *error = "Frozen v4 requirement artifact is missing compiledItems.";
        return false;
    }
    if (hasExplicitCompiledItems) {
        if (!object.value("compiledItems").isArray()) {
            if (error != nullptr) *error = "Frozen requirement artifact compiledItems must be an array.";
            return false;
        }
        std::set<std::pair<std::string, std::string>> compiledItemKeys;
        for (const QJsonValue& value : object.value("compiledItems").toArray()) {
            if (!value.isObject()) {
                if (error != nullptr) *error = "Frozen requirement artifact compiled item must be an object.";
                return false;
            }
            FrozenCompiledItemState item;
            if (!compiledItemStateFromObject(value.toObject(), item, error)) return false;
            if (!compiledItemKeys.insert(std::make_pair(item.kind, item.id)).second) {
                if (error != nullptr) *error = "Frozen requirement artifact contains duplicate compiledItems.";
                return false;
            }
            bool found = false;
            if (item.kind == "PoseTask") {
                for (CompiledPoseTask& task : parsed.compiled.poseTasks) {
                    if (task.id != item.id) continue;
                    task.compileState = item.compileState;
                    task.excludedReason = item.excludedReason;
                    task.provenance = item.provenance;
                    found = true;
                    break;
                }
            } else {
                for (WorkspaceDemandRegion& region : parsed.compiled.workspaceRegions) {
                    if (region.id != item.id) continue;
                    region.compileState = item.compileState;
                    region.excludedReason = item.excludedReason;
                    region.provenance = item.provenance;
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (error != nullptr) *error = "Frozen compiled item does not exist in compiledRequirements.";
                return false;
            }
            parsed.compiledItems.push_back(item);
        }
        const std::size_t expectedItems = parsed.compiled.poseTasks.size() +
            parsed.compiled.workspaceRegions.size();
        if (parsed.compiledItems.size() != expectedItems) {
            if (error != nullptr) *error = "Frozen requirement artifact compiledItems is incomplete.";
            return false;
        }
    }
    // 依据保留的冻结诊断重建"被排除"项：编译快照(compiledRequirements)是
    // 传统 RequirementSet 投影，本身不携带 compileState/excludedReason。凡被环境类
    // 诊断(可选条目被排除/引用帧/TCP/错误设备/朝向目标/几何目标缺失)命中的工位，
    // 重新标记为 Excluded 并回填诊断消息作为排除原因，使重载后的审计语义与冻结
    // 时刻一致。REQ_OPTIONAL_ITEM_EXCLUDED 同样作为被排除依据(Info/有问题的 Should)。
    for (CompiledPoseTask& task : parsed.compiled.poseTasks) {
        if (hasExplicitCompiledItems) continue;
        for (const RequirementDiagnostic& diagnostic : parsed.compiled.diagnostics) {
            if (diagnostic.requirementId == task.id &&
                 (diagnostic.code == "REQ_OPTIONAL_ITEM_EXCLUDED" ||
                 diagnostic.code == "REQ_FRAME_NOT_FOUND" ||
                 diagnostic.code == "REQ_TCP_FRAME_NOT_FOUND" ||
                 diagnostic.code == "REQ_TCP_FRAME_WRONG_DEVICE" ||
                 diagnostic.code == "REQ_ORIENTATION_TARGET_NOT_FOUND" ||
                 diagnostic.code == "REQ_GEOMETRY_TARGET_NOT_FOUND")) {
                task.compileState = RequirementCompileState::Excluded;
                task.excludedReason = diagnostic.message;
                break;
            }
        }
    }
    // 覆盖盒(WorkspaceDemandRegion)采用与工位完全相同的重建规则。
    for (WorkspaceDemandRegion& region : parsed.compiled.workspaceRegions) {
        if (hasExplicitCompiledItems) continue;
        for (const RequirementDiagnostic& diagnostic : parsed.compiled.diagnostics) {
            if (diagnostic.requirementId == region.id &&
                 (diagnostic.code == "REQ_OPTIONAL_ITEM_EXCLUDED" ||
                 diagnostic.code == "REQ_FRAME_NOT_FOUND" ||
                 diagnostic.code == "REQ_TCP_FRAME_NOT_FOUND" ||
                 diagnostic.code == "REQ_TCP_FRAME_WRONG_DEVICE" ||
                 diagnostic.code == "REQ_ORIENTATION_TARGET_NOT_FOUND" ||
                 diagnostic.code == "REQ_GEOMETRY_TARGET_NOT_FOUND")) {
                region.compileState = RequirementCompileState::Excluded;
                region.excludedReason = diagnostic.message;
                break;
            }
        }
    }
    if (!hasExplicitCompiledItems)
        parsed.compiledItems = snapshotCompiledItems(parsed.compiled);
    if (parsed.schemaVersion >= 4) {
        if (!object.value("execution").isObject() ||
            !RequirementExecutionJson::fromObject(object.value("execution").toObject(),
                                                   parsed.execution, error))
            return false;
        // compiledRequirements 是传统 RequirementSet 投影，本身不带 compileState/
        // excludedReason，这两个字段仅由上面保留的冻结诊断重建。执行契约在下面针对
        // 这份权威投影做一致性校验。注意：散列前绝不能把执行状态复制回 compiled 状态，
        // 否则会掩盖编译快照与执行契约之间本应暴露的漂移。
        if (!RequirementFreezer::validateExecutionConsistency(parsed, error)) return false;
    }
    else {
        parsed.execution = makeExecution(parsed);
        parsed.executionFingerprint = RequirementExecutionJson::fingerprint(parsed.execution);
    }
    artifact = parsed;
    if (error != nullptr) error->clear();
    return true;
}

std::string FrozenRequirementArtifactJson::toJson(const FrozenRequirementArtifact& artifact)
{
    return QJsonDocument(toObject(artifact)).toJson(QJsonDocument::Compact).toStdString();
}

bool FrozenRequirementArtifactJson::fromJson(const std::string& json, FrozenRequirementArtifact& artifact,
                                              std::string* error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error != nullptr) *error = "Frozen requirement artifact JSON is not an object: " + parseError.errorString().toStdString();
        return false;
    }
    return fromObject(document.object(), artifact, error);
}

} // namespace rws
