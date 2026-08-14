#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTFREEZER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTFREEZER_HPP

#include "EngineeringRequirementTypes.hpp"
#include <rwslibs/robotanalysiscore/RequirementExecutionTypes.hpp>

#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

#include <array>
#include <string>
#include <vector>

class QJsonObject;

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

struct RobotModelSpec;

/**
 * @brief 冻结时提取的 WorkCell 场景快照。
 *
 * 优化器不能只把相对 Frame 的任务点提前换算为 WORLD 坐标，因为那会丢失工装、
 * 工件和碰撞几何。该快照保存将候选机器人重新放回冻结场景所需的纯数据场景描述，
 * 同时记录来源文件和内容指纹，用于发现冻结后发生的场景文件替换或手工修改。
 */
struct FrozenWorkCellScenarioSnapshot {
    int schemaVersion = 2;
    std::string sourceWorkCellPath;
    std::string sourceFileFingerprint;
    std::string snapshotFingerprint;
    std::string deviceName;
    std::string environmentFingerprint;
    std::string stateFingerprint;
    RobotModelSpec sceneSpec;

    FrozenWorkCellScenarioSnapshot() = default;

    FrozenWorkCellScenarioSnapshot(const FrozenWorkCellScenarioSnapshot& other)
    {
        *this = other;
    }

    FrozenWorkCellScenarioSnapshot& operator=(const FrozenWorkCellScenarioSnapshot& other)
    {
        if (this == &other) return *this;
        schemaVersion = other.schemaVersion;
        sourceWorkCellPath = other.sourceWorkCellPath;
        sourceFileFingerprint = other.sourceFileFingerprint;
        snapshotFingerprint = other.snapshotFingerprint;
        deviceName = other.deviceName;
        environmentFingerprint = other.environmentFingerprint;
        stateFingerprint = other.stateFingerprint;
        sceneSpec = other.sceneSpec;
        return *this;
    }
};

struct FrozenRobotStateSnapshot {
    std::string deviceName;
    std::string tcpFrameName;
    std::string kinematicFingerprint;
    std::vector<double> q;
    std::array<double, 16> tcpWorldPose = {{0.0}};
    std::string capturedAt;
};

/** 冻结工件中逐项持久化的编译审计状态，独立于诊断码的未来演进。 */
struct FrozenCompiledItemState {
    std::string kind; // "PoseTask" or "WorkspaceRegion"
    std::string id;
    RequirementCompileState compileState = RequirementCompileState::Included;
    std::string excludedReason;
    CompiledRequirementItemProvenance provenance;
};

struct FrozenRequirementValidationResult {
    bool robotStateChanged = false;
    std::vector<std::string> warnings;
    FrozenRobotStateSnapshot frozenRobotState;
    FrozenRobotStateSnapshot currentRobotState;
};

/**
 * @brief 可交接、可审计的冻结需求工件。
 *
 * RequirementSet 是允许工程师持续修改的编辑态意图；本结构则是某一时刻在明确
 * RobotModelSpec、WorkCell 与运动学 State 下完成环境解析后的只读输入。下游优化器
 * 只能消费该工件，避免将“尚未解析的名称字符串”误当成已经满足的工程条件。
 */
/** User-facing publication identity kept alongside technical evidence. */
struct RequirementPublication {
    int revisionNumber = 0;
    std::string revisionId;
    std::string state;
    std::string publishedAt;
    std::string parentRevisionId;
};

struct FrozenRequirementArtifact {
    int schemaVersion = 4;
    std::string requirementFingerprint;
    std::string executionFingerprint;
    std::string environmentFingerprint;
    std::string workcellFingerprint;
    std::string compilerVersion = "EngineeringRequirements.Freezer.1";
    std::string frozenAt;
    RequirementPublication publication;
    RobotModelBinding modelBinding;
    FrozenRobotStateSnapshot frozenRobotState;
    FrozenWorkCellScenarioSnapshot scenario;
    CompiledRequirementSet compiled;
    std::vector<FrozenCompiledItemState> compiledItems;
    RequirementExecutionSet execution;
};

/**
 * @brief 将编辑态需求冻结为与真实工程环境绑定的执行态工件。
 *
 * 冻结门禁同时检查模型指纹、Frame/TCP/姿态目标/覆盖盒引用和几何特征。在 Must
 * 需求存在任何问题时失败；Should 需求会留下诊断并从 compiled 输入中排除，确保
 * 下游不会将未验证建议项当作真实任务。
 */
class RequirementFreezer {
  public:
    /**
     * @brief 校验 v4 执行契约是否为冻结编译快照及其来源的精确投影。
     *
     * 对 schemaVersion == 4 的工件执行完整一致性审计：
     *   1) executionFingerprint 存在、非空且与 execution 重新计算的结果一致；
     *   2) execution 本身通过 RequirementExecutionJson::validate 结构校验；
     *   3) execution.provenance 的每一项(需求指纹/模型指纹/工作单元指纹/
     *      环境指纹/编译器版本/冻结时间/源路径)与工件顶层字段逐项一致；
     *   4) 由 compiled 快照投影生成的执行契约(makeExecution)与存档的执行契约
     *      指纹一致，确保编译快照未被篡改而 execution 未同步更新。
     * 任何一环不符即返回 false 并经 error 回填原因，防止"编译快照与执行契约
     * 各自独立修改"造成的静默漂移。
     */
    static bool validateExecutionConsistency(const FrozenRequirementArtifact& artifact,
                                             std::string* error = nullptr);

    static bool freeze(const RequirementSet& requirements, const rw::models::WorkCell& workcell,
                       const rw::kinematics::State& state, const RobotModelSpec& model,
                       FrozenRequirementArtifact& artifact, std::string* error = nullptr);
    static bool freeze(const RequirementSet& requirements, const rw::models::WorkCell& workcell,
                       const rw::kinematics::State& state, const RobotModelSpec& model,
                       FrozenRequirementArtifact& artifact, std::string* error,
                       const std::string& projectRoot);

    /**
     * @brief 校验一个已冻结工件是否仍对应当前需求、模型与工程场景。
     *
     * 此接口用于项目重载和下游交接前的审计门禁：即使 JSON 中存在冻结标记，
     * 只要需求内容、RobotModelSpec 或 WorkCell/State 任一项发生变化，工件就
     * 必须被判定为过期，不能作为“已验证”的优化输入继续使用。
     */
    static bool isCurrent(const FrozenRequirementArtifact& artifact,
                          const RequirementSet& requirements,
                          const rw::models::WorkCell& workcell,
                          const rw::kinematics::State& state,
                          const RobotModelSpec& model,
                          std::string* error = nullptr);
    static bool isCurrent(const FrozenRequirementArtifact& artifact,
                          const RequirementSet& requirements,
                          const rw::models::WorkCell& workcell,
                          const rw::kinematics::State& state,
                          const RobotModelSpec& model,
                          std::string* error,
                          const std::string& artifactBaseDirectory,
                          FrozenRequirementValidationResult* validationResult = nullptr);

    /**
     * @brief 仅复核冻结工件与当前 WorkCell 场景是否仍一致。
     *
     * 该接口服务于运动学分析等不持有编辑态 RequirementSet 的下游模块。它校验冻结
     * State、来源场景文件内容和快照本身的完整性；调用方仍需按自己的输入契约决定
     * 是否额外复核 RobotModelSpec。
     */
    static bool isScenarioCurrent(const FrozenRequirementArtifact& artifact,
                                  const rw::models::WorkCell& workcell,
                                  const rw::kinematics::State& state,
                                  std::string* error = nullptr);
    static bool isScenarioCurrent(const FrozenRequirementArtifact& artifact,
                                  const rw::models::WorkCell& workcell,
                                  const rw::kinematics::State& state,
                                  std::string* error,
                                  const std::string& artifactBaseDirectory);

    static bool validateScenario(const FrozenRequirementArtifact& artifact,
                                 const rw::models::WorkCell& workcell,
                                 const rw::kinematics::State& state,
                                 FrozenRequirementValidationResult* result = nullptr,
                                 std::string* error = nullptr);
    static bool validateScenario(const FrozenRequirementArtifact& artifact,
                                 const rw::models::WorkCell& workcell,
                                 const rw::kinematics::State& state,
                                 FrozenRequirementValidationResult* result,
                                 std::string* error,
                                 const std::string& artifactBaseDirectory);
};

/**
 * @brief 冻结需求工件的稳定 JSON 读写器。
 *
 * 该格式与编辑态 RequirementSet JSON 分离，专门持久化编译后的任务、环境指纹和
 * 非阻断诊断，使项目重新打开后仍能准确知道当时哪些 Should 工位被排除。
 */
class FrozenRequirementArtifactJson {
  public:
    /**
     * @brief 将冻结工件转换为可嵌入项目 JSON 的对象。
     *
     * RequirementSetJson 仍保持编辑态格式兼容；项目保存器可将本对象作为其
     * 可选 frozenArtifact 字段，从而同时保留编辑意图和冻结时的审计证据。
     */
    static QJsonObject toObject(const FrozenRequirementArtifact& artifact);
    static bool fromObject(const QJsonObject& object, FrozenRequirementArtifact& artifact,
                           std::string* error = nullptr);
    static std::string toJson(const FrozenRequirementArtifact& artifact);
    static bool fromJson(const std::string& json, FrozenRequirementArtifact& artifact,
                         std::string* error = nullptr);
};

} // namespace rws

#endif
