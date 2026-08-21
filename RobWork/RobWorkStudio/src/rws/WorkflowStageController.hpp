#ifndef RWS_WORKFLOWSTAGECONTROLLER_HPP
#define RWS_WORKFLOWSTAGECONTROLLER_HPP

#include <QString>
#include <QStringList>

#include <array>

namespace rws {

enum class WorkflowStage
{
    Modeling = 0,
    Requirements,
    Kinematics,
    StructuralOptimization
};

enum class WorkflowStageState
{
    Locked,
    Available,
    Complete,
    Stale
};

struct WorkflowProjectSnapshot
{
    int fingerprintVersion = 1;
    bool modelAvailable = false;
    bool sceneAvailable = false;
    QString modelFingerprint;
    QString sceneFingerprint;
    QString legacySceneFingerprint;
    QString environmentFingerprint;

    bool requirementsFrozen = false;
    QString requirementFingerprint;
    QString requirementModelFingerprint;
    QString requirementSceneFingerprint;
    QString requirementEnvironmentFingerprint;

    bool kinematicValidationPassed = false;
    QString kinematicValidationFingerprint;
    QString kinematicModelFingerprint;
    QString kinematicRequirementFingerprint;
    QString kinematicSceneFingerprint;
    QString kinematicEnvironmentFingerprint;

    bool optimizationArtifactAvailable = false;
    QString optimizationModelFingerprint;
    QString optimizationRequirementFingerprint;
    QString optimizationKinematicFingerprint;
    QString optimizationSceneFingerprint;
    QString optimizationEnvironmentFingerprint;
    QString optimizationEvaluatorVersion;
    QString optimizationCompilerVersion;
};

struct WorkflowStageStatus
{
    WorkflowStageState state = WorkflowStageState::Locked;
    QString reason;
    QStringList requiredResourceIds;
    QString modelFingerprint;
    QString requirementFingerprint;
    QString reasonCode;
};

class WorkflowStageSnapshot
{
  public:
    WorkflowStageStatus& at (WorkflowStage stage) { return _statuses[static_cast< int > (stage)]; }
    const WorkflowStageStatus& at (WorkflowStage stage) const
    {
        return _statuses[static_cast< int > (stage)];
    }

  private:
    std::array< WorkflowStageStatus, 4 > _statuses;
};

/** Evaluates the dependency gates between the four workflow stages. */
class WorkflowStageController
{
  public:
    static WorkflowStageSnapshot evaluate (const WorkflowProjectSnapshot& snapshot);

    /** Returns whether a stage can be opened to work on or repair its artifact. */
    static bool isStageAccessible (WorkflowStageState state);

    /** Clears completion evidence at stage and all downstream stages. */
    static void invalidateFrom (WorkflowProjectSnapshot& snapshot, WorkflowStage stage);
};

}    // namespace rws

#endif    // RWS_WORKFLOWSTAGECONTROLLER_HPP
