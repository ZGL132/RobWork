#ifndef RWS_ROBOTANALYSISCORE_REQUIREMENTEXECUTIONTYPES_HPP
#define RWS_ROBOTANALYSISCORE_REQUIREMENTEXECUTIONTYPES_HPP

#include <array>
#include <QJsonObject>
#include <string>
#include <vector>

namespace rws {

// 执行契约工作区采样的安全上限(独立于工程需求编译器的 MaxWorkspace* 常量)。
// 即使工件在本进程内没有经过工程需求编译器，直接消费执行契约的下游也会被这些
// 上限约束，避免畸形或篡改的执行契约引发无界采样计算。
constexpr int MaxExecutionWorkspaceSamplesPerAxis = 64;
constexpr int MaxExecutionWorkspaceDirectionSamples = 1000;
constexpr int MaxExecutionWorkspaceRollSamples = 360;

enum class RequirementExecutionLevel { Must, Should, Info };
enum class RequirementExecutionStage { Quick, Verified };
enum class RequirementExecutionCompileState { Included, Excluded, Invalid };
enum class RequirementExecutionDiagnosticSeverity { Info, Warning, Error };
enum class RequirementExecutionOrientationMode {
    Fixed,
    AlignFrame,
    AlignGeometryNormal,
    PointAtTarget
};
enum class RequirementExecutionOffsetAxis { ToolZ, ReferenceZ };
enum class RequirementExecutionProcessType {
    Generic,
    Pick,
    Place,
    MachineLoad,
    MachineUnload,
    Inspect,
    WeldStart,
    WeldEnd,
    ToolChange,
    SafeStandby,
    Handover
};

struct RequirementExecutionProvenance {
    std::string requirementFingerprint;
    std::string robotModelFingerprint;
    std::string workcellFingerprint;
    std::string environmentFingerprint;
    std::string compilerVersion;
    std::string frozenAt;
    std::string sourcePath;
};

struct RequirementExecutionDiagnostic {
    std::string code;
    RequirementExecutionDiagnosticSeverity severity =
        RequirementExecutionDiagnosticSeverity::Info;
    std::string requirementId;
    std::string field;
    std::string message;
    std::string source;
};

/**
 * @brief 执行契约中逐条保留的"条目级溯源"信息(执行态)。
 *
 * 由编译态 CompiledRequirementItemProvenance 在冻结时投影而来：除来源 id/种类外，
 * 还携带与该条目相关的执行诊断(供下游审计条目为何被排除/降级)。
 */
struct RequirementItemProvenance {
    std::string sourceId;                                     ///< 源编辑态条目 id
    std::string sourceKind;                                   ///< 来源种类(如 PoseTaskSource 文本 / "BoxRegion")
    std::vector<RequirementExecutionDiagnostic> diagnostics;  ///< 关联的执行诊断
};

struct RequirementExecutionPathRule {
    bool enabled = false;
    RequirementExecutionOffsetAxis axis = RequirementExecutionOffsetAxis::ToolZ;
    double distanceMeters = 0.0;
    bool collisionFreeRequired = true;
};

struct RequirementExecutionTask {
    std::string id;
    std::string name;
    RequirementExecutionLevel level = RequirementExecutionLevel::Must;
    RequirementExecutionCompileState compileState =
        RequirementExecutionCompileState::Included;
    RequirementExecutionProcessType processType = RequirementExecutionProcessType::Generic;
    std::string excludedReason;
    RequirementItemProvenance provenance; ///< 执行态条目溯源
    std::string refFrame = "WORLD";
    std::string tcpFrame;
    std::array<double, 3> position = {{0.0, 0.0, 0.0}};
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};
    double positionToleranceMeters = 0.001;
    double orientationToleranceDeg = 1.0;
    bool allowToolRollFree = false;
    RequirementExecutionOrientationMode orientationMode =
        RequirementExecutionOrientationMode::Fixed;
    std::string orientationTargetFrame;
    std::string orientationTargetGeometry;
    std::string orientationTargetPoint;
    bool invertNormal = false;
    double rollMinimumDeg = -180.0;
    double rollMaximumDeg = 180.0;
    bool collisionFreeRequired = true;
    double minimumJointMargin = 0.0;
    double minimumManipulability = 0.0;
    std::string resolutionEvidence;
    RequirementExecutionPathRule approach;
    RequirementExecutionPathRule retract;
    // Compatibility summary retained for consumers that only understand the
    // original execution contract. The detailed rules above are authoritative.
    bool pathValidationPending = false;
    std::vector<RequirementExecutionDiagnostic> diagnostics;
    QJsonObject extensions; ///< 未知 JSON 字段，供未来版本往返保留
};

struct RequirementExecutionRegion {
    std::string id;
    std::string name;
    RequirementExecutionLevel level = RequirementExecutionLevel::Must;
    RequirementExecutionCompileState compileState =
        RequirementExecutionCompileState::Included;
    std::string excludedReason;
    RequirementItemProvenance provenance; ///< 执行态条目溯源
    std::string refFrame = "WORLD";
    std::string tcpFrame;
    std::array<double, 3> center = {{0.0, 0.0, 0.0}};
    std::array<double, 3> size = {{0.1, 0.1, 0.1}};
    double minimumCoverage = 0.8;
    // 编辑态的间距与冻结时解析出的网格点数均显式写入执行契约，避免下游
    // 因浮点向上取整规则不同而得到不一致的覆盖率结果。
    std::array<double, 3> sampleSpacingMeters = {{0.025, 0.025, 0.025}};
    std::array<int, 3> sampleCounts = {{5, 5, 5}};
    RequirementExecutionOrientationMode orientationMode =
        RequirementExecutionOrientationMode::Fixed;
    std::string orientationTargetFrame;
    std::string orientationTargetGeometry;
    std::string orientationTargetPoint;
    std::array<double, 3> fixedRpyDeg = {{0.0, 0.0, 0.0}};
    int directionSamples = 1;
    int rollSamples = 1;
    double minimumOrientationCoverage = 0.0;
    RequirementExecutionStage minimumVerificationStage =
        RequirementExecutionStage::Verified;
    bool collisionFreeRequired = true;
    double positionToleranceMeters = 0.001;
    double orientationToleranceDeg = 1.0;
    double minimumJointMargin = 0.0;
    double minimumManipulability = 0.0;
    std::vector<RequirementExecutionDiagnostic> diagnostics;
    QJsonObject extensions; ///< 未知 JSON 字段，供未来版本往返保留
};

struct RequirementExecutionSet {
    int schemaVersion = 1;
    RequirementExecutionProvenance provenance;
    std::vector<RequirementExecutionTask> tasks;
    std::vector<RequirementExecutionRegion> workspaceRegions;
    std::vector<RequirementExecutionDiagnostic> diagnostics;
    QJsonObject extensions; ///< 未知 JSON 字段，供未来版本往返保留
};

} // namespace rws

#endif
