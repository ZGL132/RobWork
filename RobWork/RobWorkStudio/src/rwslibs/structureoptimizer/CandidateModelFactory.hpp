#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATEMODELFACTORY_HPP

#include "StructureOptimizationTypes.hpp"

#include <rw/core/Ptr.hpp>
#include <rw/kinematics/State.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionDetector.hpp>

#include <memory>
#include <QTemporaryDir>
#include <string>
#include <vector>

namespace rw { namespace kinematics { class Frame; } }
namespace rw { namespace models { class Device; } }

namespace rws {

struct CandidateModelArtifact {
    rw::core::Ptr<rw::models::WorkCell> workcell;
    rw::core::Ptr<rw::models::Device> device;
    rw::kinematics::State state;
    rw::core::Ptr<const rw::kinematics::Frame> tcpFrame;
    rw::core::Ptr<rw::proximity::CollisionDetector> collisionDetector;
    std::shared_ptr<QTemporaryDir> temporaryDirectory;
};

struct CandidateModelBuildRequest {
    RobotModelSpec spec;
    std::string deviceName;
    std::string tcpFrame;
    bool checkCollision = true;
    // 指针仅在本次同步 build() 调用期间借用。场景快照属于优化问题，工厂不会保存它，
    // 从而避免候选并行评价时共享和修改同一份 WorkCell 状态。
    const StructureOptimizationScenarioSnapshot* scenarioSnapshot = nullptr;
    std::string scenarioBaseDirectory;
};

struct CandidateModelBuildResult {
    bool ok = false;
    CandidateModelArtifact artifact;
    std::vector<AnalysisWarning> warnings;
};

class CandidateModelFactory {
  public:
    //! Resolves model-relative external geometry paths before changing saveDirectory.
    static void resolveExternalAssetPaths(
        RobotModelSpec& spec, const std::string& baseDirectory = {});

    /**
     * @brief 将冻结需求中的工装/工件场景合入候选机器人规格。
     *
     * 候选评价、预览和最终 XML 导出必须调用同一个合并入口：仅保留候选机器人经过
     * 设计变量变异后的本体，同时从快照复制外部 Frame、场景几何和相应碰撞模型。
     * 这样可避免评价使用一个场景、导出模型使用另一个场景的不可审计偏差。
     */
    static void applyScenarioSnapshot(
        RobotModelSpec& spec,
        const StructureOptimizationScenarioSnapshot& snapshot,
        const std::string& baseDirectory = {});

    CandidateModelBuildResult build(const CandidateModelBuildRequest& request);
};

} // namespace rws
#endif
