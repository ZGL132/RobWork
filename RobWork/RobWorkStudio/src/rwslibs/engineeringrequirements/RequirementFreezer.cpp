#include "RequirementFreezer.hpp"

#include "GeometryFeatureResolver.hpp"
#include "OrientationRuleResolver.hpp"
#include "RequirementCompiler.hpp"
#include "RequirementSetJson.hpp"

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
                              RequirementLevel level, const std::string& message)
{
    RequirementDiagnostic diagnostic;
    diagnostic.requirementId = id;
    diagnostic.level = level;
    diagnostic.message = message;
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
        task.approach.enabled = item.pathValidationPending;
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
        region.samplesPerAxis = item.samplesPerAxis;
        snapshot.boxRegions.push_back(region);
    }
    return snapshot;
}

QJsonObject diagnosticToObject(const RequirementDiagnostic& diagnostic)
{
    QJsonObject object;
    object["requirementId"] = QString::fromStdString(diagnostic.requirementId);
    object["level"] = QString::fromLatin1(toString(diagnostic.level));
    object["message"] = QString::fromStdString(diagnostic.message);
    object["blocking"] = diagnostic.blocking;
    return object;
}

bool diagnosticFromObject(const QJsonObject& object, RequirementDiagnostic& diagnostic, std::string* error)
{
    diagnostic.requirementId = object.value("requirementId").toString().toStdString();
    if (!requirementLevelFromString(object.value("level").toString("Must").toStdString(), diagnostic.level)) {
        if (error != nullptr) *error = "Frozen artifact diagnostic level is invalid.";
        return false;
    }
    diagnostic.message = object.value("message").toString().toStdString();
    diagnostic.blocking = object.value("blocking").toBool(diagnostic.level == RequirementLevel::Must);
    return true;
}

} // namespace

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

    RequirementSet resolved = requirements;
    std::vector<RequirementDiagnostic> environmentDiagnostics;
    for (PoseTask& task : resolved.poseTasks) {
        if (findFrame(workcell, task.refFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station reference frame is unavailable in the current WorkCell: " + task.refFrame);
        if (workcell.findFrame(task.tcpFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station TCP frame is unavailable in the current WorkCell: " + task.tcpFrame);
        if ((task.orientation.mode == OrientationMode::AlignFrame || task.orientation.mode == OrientationMode::PointAtTarget) &&
            !task.orientation.targetFrame.empty() && workcell.findFrame(task.orientation.targetFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station orientation target frame is unavailable in the current WorkCell: " + task.orientation.targetFrame);
        if (task.source == PoseTaskSource::GeometryFeature) {
            std::string resolutionError;
            if (!GeometryFeatureResolver::applyToStation(task.geometryFeature, workcell, state, task, &resolutionError))
                addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                         "Key station geometry feature cannot be resolved: " + resolutionError);
        }
        std::string orientationError;
        if (!OrientationRuleResolver::applyToStation(task, workcell, state, &orientationError))
            addEnvironmentDiagnostic(environmentDiagnostics, task.id, task.level,
                                     "Key station orientation rule cannot be resolved: " + orientationError);
    }
    for (const BoxRegion& region : resolved.boxRegions) {
        if (findFrame(workcell, region.refFrame) == nullptr)
            addEnvironmentDiagnostic(environmentDiagnostics, region.id, region.level,
                                     "Workspace region reference frame is unavailable in the current WorkCell: " + region.refFrame);
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
    compiled.poseTasks.erase(std::remove_if(compiled.poseTasks.begin(), compiled.poseTasks.end(),
        [&] (const CompiledPoseTask& task) { return hasDiagnostic(environmentDiagnostics, task.id); }),
        compiled.poseTasks.end());
    compiled.workspaceRegions.erase(std::remove_if(compiled.workspaceRegions.begin(), compiled.workspaceRegions.end(),
        [&] (const WorkspaceDemandRegion& region) { return hasDiagnostic(environmentDiagnostics, region.id); }),
        compiled.workspaceRegions.end());

    artifact = FrozenRequirementArtifact();
    artifact.schemaVersion = 3;
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
    if (error != nullptr) error->clear();
    return true;
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
    if (artifact.schemaVersion != 3 || artifact.requirementFingerprint.empty() ||
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

    if (!validateScenario(artifact, workcell, state, validationResult, error,
                          artifactBaseDirectory))
        return false;

    if (error != nullptr) error->clear();
    return true;
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
                                          std::string* error,
                                          const std::string& artifactBaseDirectory)
{
    if (result != nullptr) *result = FrozenRequirementValidationResult();
    if (artifact.schemaVersion != 3) {
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
    object["environmentFingerprint"] = QString::fromStdString(artifact.environmentFingerprint);
    object["workcellFingerprint"] = QString::fromStdString(artifact.workcellFingerprint);
    object["compilerVersion"] = QString::fromStdString(artifact.compilerVersion);
    object["frozenAt"] = QString::fromStdString(artifact.frozenAt);
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
    QJsonArray diagnostics;
    for (const RequirementDiagnostic& diagnostic : artifact.compiled.diagnostics)
        diagnostics.append(diagnosticToObject(diagnostic));
    object["diagnostics"] = diagnostics;
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
    RequirementSet snapshot;
    if (!RequirementSetJson::fromObject(object.value("compiledRequirements").toObject(), snapshot, error)) return false;
    CompiledRequirementSet compiled;
    if (!RequirementCompiler::compile(snapshot, compiled, error)) return false;
    FrozenRequirementArtifact parsed;
    parsed.schemaVersion = object.value("schemaVersion").toInt(1);
    if (parsed.schemaVersion != 3) {
        if (error != nullptr) *error = "Frozen requirement artifact uses legacy state-based evidence. Validate and freeze the requirements again.";
        return false;
    }
    parsed.requirementFingerprint = object.value("requirementFingerprint").toString().toStdString();
    parsed.environmentFingerprint = object.value("environmentFingerprint").toString().toStdString();
    parsed.workcellFingerprint = object.value("workcellFingerprint").toString().toStdString();
    parsed.compilerVersion = object.value("compilerVersion").toString("EngineeringRequirements.Freezer.1").toStdString();
    parsed.frozenAt = object.value("frozenAt").toString().toStdString();
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
    for (const QJsonValue& value : object.value("diagnostics").toArray()) {
        RequirementDiagnostic diagnostic;
        if (!diagnosticFromObject(value.toObject(), diagnostic, error)) return false;
        parsed.compiled.diagnostics.push_back(diagnostic);
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
