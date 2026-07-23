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

using namespace rws;

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
