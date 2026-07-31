// =============================================================================
//  文件: CandidateModelFactory.cpp
//  说明: 为 StructureOptimizer 创建候选模型的工厂实现。
//
//  核心职责:
//    将 RobotModelSpec 序列化至临时目录中的 XML 文件,重新加载为 WorkCell,
//    并提取 Device、State、TCP Frame 及 CollisionDetector 等运行时构件。
// =============================================================================

#include "CandidateModelFactory.hpp"
#include <rwslibs/robotmodelbuilder/RobotModelXmlWriter.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalysisCollision.hpp>
#include <rw/loaders/WorkCellLoader.hpp>
#include <rw/models/Device.hpp>
#include <rw/kinematics/Frame.hpp>
#include <QDir>
#include <QFileInfo>

#include <set>

using namespace rws;

namespace {

/**
 * @brief 将冻结快照中的外部场景注入候选机器人规格。
 *
 * 快照中的完整 RobotModelSpec 只是序列化容器，不能直接替代候选规格；否则会把未变异的
 * 机器人关节重新写回场景。此处仅复制非机器人场景 Frame、场景几何和外部碰撞模型，保留
 * request.spec 中已经由优化变量变异后的关节、TCP 和连杆碰撞模型。
 */
void mergeFrozenScenario(RobotModelSpec& candidate,
                         const StructureOptimizationScenarioSnapshot& snapshot)
{
    RobotModelSpec scene = snapshot.sceneSpec;
    CandidateModelFactory::resolveExternalAssetPaths(scene);

    candidate.sceneFrames.clear();
    for (const FrameSpec& frame : scene.sceneFrames) {
        // RobotBase 由候选机器人自身的场景定义创建，复制会造成重复 Frame 名称。
        if (frame.name.empty() || frame.name == "RobotBase") continue;
        candidate.sceneFrames.push_back(frame);
    }
    candidate.sceneGeometries = scene.sceneGeometries;

    std::set<std::string> externalFrames = {"WORLD", "RobotBase"};
    for (const FrameSpec& frame : candidate.sceneFrames)
        externalFrames.insert(frame.name);
    std::set<std::string> collisionNames;
    for (const CollisionModelSpec& collision : candidate.collisionModels)
        collisionNames.insert(collision.name);
    for (const CollisionModelSpec& collision : scene.collisionModels) {
        if (externalFrames.find(collision.refFrame) == externalFrames.end() ||
            !collisionNames.insert(collision.name).second) {
            continue;
        }
        candidate.collisionModels.push_back(collision);
    }
    // SceneGeometrySpec 自带 collisionModel 标记，能够覆盖多数工装/工件碰撞体；
    // 额外 CollisionModelSpec 则保留网格和独立碰撞模型的原始语义。
    candidate.generateScene = true;
}

} // namespace

void CandidateModelFactory::resolveExternalAssetPaths (RobotModelSpec& spec)
{
    const QDir sourceDirectory (QString::fromStdString (spec.saveDirectory));
    const auto resolve = [&sourceDirectory] (std::string& path) {
        const QString raw = QString::fromStdString (path).trimmed ();
        if (raw.isEmpty () || QFileInfo (raw).isAbsolute ())
            return;
        path = sourceDirectory.absoluteFilePath (raw).toStdString ();
    };

    for (DrawableSpec& drawable : spec.drawables)
        resolve (drawable.filePath);
    for (CollisionModelSpec& collision : spec.collisionModels)
        resolve (collision.filePath);
    for (SceneGeometrySpec& geometry : spec.sceneGeometries)
        resolve (geometry.file);
}

void CandidateModelFactory::applyScenarioSnapshot(
    RobotModelSpec& spec,
    const StructureOptimizationScenarioSnapshot& snapshot)
{
    // 不可用快照代表旧项目或纯机器人项目。保持原规格不变，继续兼容既有导出流程。
    if (!snapshot.available())
        return;

    // 这里刻意复用候选模型工厂内部的唯一合并实现；导出器不复制这段逻辑，后续增加
    // 场景元素时不会出现“评价能看到、导出看不到”的分叉。
    mergeFrozenScenario(spec, snapshot);
}

// =============================================================================
//  CandidateModelFactory::build
//  说明: 将 RobotModelSpec 转化为可用的 WorkCell 运行时模型。
//
//  步骤:
//    1. 创建 QTemporaryDir 工作目录
//    2. 将 spec.saveDirectory 指向临时目录,强制 generateScene = true
//    3. 调用 RobotModelXmlWriter::saveFiles 输出所有 XML 文件
//    4. 通过 WorkCellLoader::Factory::load 加载场景文件
//    5. 按名称查找 Device
//    6. 获取默认 State
//    7. 解析 TCP 帧 (指定名称 / device->getEnd 回退)
//    8. 按需创建 CollisionDetector
//    9. 组装并返回 CandidateModelBuildResult
// =============================================================================
CandidateModelBuildResult CandidateModelFactory::build (
    const CandidateModelBuildRequest& request)
{
    CandidateModelBuildResult result;

    // -------------------------------------------------------------------------
    //  1. 创建临时目录
    // -------------------------------------------------------------------------
    auto tempDir = std::make_shared< QTemporaryDir > ();
    if (!tempDir->isValid ()) {
        AnalysisWarning w;
        w.code     = "StructureOptimizer.Model.TempDirectoryFailed";
        w.message  = "Failed to create temporary directory for model files.";
        w.source   = "CandidateModelFactory";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // -------------------------------------------------------------------------
    //  2. 配置 spec,指向临时目录并启用场景生成
    // -------------------------------------------------------------------------
    RobotModelSpec spec = request.spec;
    resolveExternalAssetPaths (spec);
    if (request.scenarioSnapshot != nullptr)
        applyScenarioSnapshot(spec, *request.scenarioSnapshot);
    spec.saveDirectory  = tempDir->path ().toStdString ();
    spec.generateScene  = true;

    // -------------------------------------------------------------------------
    //  3. 写入 XML 文件
    // -------------------------------------------------------------------------
    {
        QStringList saveErrors;
        if (!RobotModelXmlWriter::saveFiles (spec, saveErrors)) {
            AnalysisWarning w;
            w.code     = "StructureOptimizer.Model.SaveFailed";
            w.message  = "Failed to save model XML files: "
                         + saveErrors.join ("; ").toStdString ();
            w.source   = "CandidateModelFactory";
            w.severity = AnalysisStatus::Fail;
            result.warnings.push_back (w);
            return result;
        }
    }

    // -------------------------------------------------------------------------
    //  4. 加载 WorkCell
    // -------------------------------------------------------------------------
    rw::core::Ptr< rw::models::WorkCell > wc;
    {
        const QString scenePath = RobotModelXmlWriter::sceneFilePath (spec);
        try {
            wc = rw::loaders::WorkCellLoader::Factory::load (
                scenePath.toStdString ());
        }
        catch (const std::exception& e) {
            AnalysisWarning w;
            w.code     = "StructureOptimizer.Model.LoadFailed";
            w.message  = "Exception while loading WorkCell from "
                         + scenePath.toStdString () + ": " + e.what ();
            w.source   = "CandidateModelFactory";
            w.severity = AnalysisStatus::Fail;
            result.warnings.push_back (w);
            return result;
        }

        if (wc.isNull ()) {
            AnalysisWarning w;
            w.code     = "StructureOptimizer.Model.LoadFailed";
            w.message  = "WorkCellLoader returned null for scene file: "
                         + scenePath.toStdString ();
            w.source   = "CandidateModelFactory";
            w.severity = AnalysisStatus::Fail;
            result.warnings.push_back (w);
            return result;
        }
    }

    // -------------------------------------------------------------------------
    //  5. 查找 Device
    // -------------------------------------------------------------------------
    rw::core::Ptr< rw::models::Device > device =
        wc->findDevice (request.deviceName);
    if (device.isNull ()) {
        AnalysisWarning w;
        w.code     = "StructureOptimizer.Model.DeviceMissing";
        w.message  = "Device '" + request.deviceName
                     + "' not found in loaded WorkCell.";
        w.source   = "CandidateModelFactory";
        w.severity = AnalysisStatus::Fail;
        result.warnings.push_back (w);
        return result;
    }

    // -------------------------------------------------------------------------
    //  6. 获取默认 State
    // -------------------------------------------------------------------------
    const rw::kinematics::State state = wc->getDefaultState ();

    // -------------------------------------------------------------------------
    //  7. 解析 TCP 帧
    // -------------------------------------------------------------------------
    rw::core::Ptr< const rw::kinematics::Frame > tcpFrame;
    if (!request.tcpFrame.empty ()) {
        const rw::kinematics::Frame* frame =
            wc->findFrame (request.tcpFrame);
        if (frame == NULL) {
            AnalysisWarning w;
            w.code     = "StructureOptimizer.Model.TcpMissing";
            w.message  = "TCP frame '" + request.tcpFrame
                         + "' not found in WorkCell.";
            w.source   = "CandidateModelFactory";
            w.severity = AnalysisStatus::Fail;
            result.warnings.push_back (w);
            return result;
        }
        tcpFrame = frame;
    }
    else {
        tcpFrame = device->getEnd ();
    }

    // -------------------------------------------------------------------------
    //  8. 创建碰撞检测器 (可选)
    // -------------------------------------------------------------------------
    rw::core::Ptr< rw::proximity::CollisionDetector > collisionDetector;
    if (request.checkCollision) {
        collisionDetector = makeKinematicAnalysisCollisionDetector (wc);
    }

    // -------------------------------------------------------------------------
    //  9. 组装结果
    // -------------------------------------------------------------------------
    result.ok = true;
    result.artifact.workcell           = wc;
    result.artifact.device             = device;
    result.artifact.state              = state;
    result.artifact.tcpFrame           = tcpFrame;
    result.artifact.collisionDetector  = collisionDetector;
    result.artifact.temporaryDirectory = tempDir;

    return result;
}
