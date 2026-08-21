#include "WorkflowStageController.hpp"

namespace {

bool equalIfPresent (const QString& expected, const QString& actual)
{
    return expected.isEmpty () || expected == actual;
}

bool sceneEvidenceMatches (const rws::WorkflowProjectSnapshot& snapshot,
                           const QString& expected)
{
    if (expected.isEmpty ())
        return true;
    // Accept raw-file evidence as a compatibility path for projects written
    // before canonical XML fingerprints were persisted. The next workflow
    // publication rewrites it to the canonical value.
    return expected == snapshot.sceneFingerprint ||
           expected == snapshot.legacySceneFingerprint;
}

void deriveReasonCode (rws::WorkflowStageStatus& status)
{
    if (status.reason.contains (QStringLiteral ("not available"))) status.reasonCode = QStringLiteral ("ResourceMissing");
    else if (status.reason.contains (QStringLiteral ("stale"), Qt::CaseInsensitive)) status.reasonCode = QStringLiteral ("FingerprintMismatch");
    else if (status.reason.contains (QStringLiteral ("requirements"), Qt::CaseInsensitive)) status.reasonCode = QStringLiteral ("RequirementsNotReady");
    else if (status.reason.contains (QStringLiteral ("kinematic"), Qt::CaseInsensitive)) status.reasonCode = QStringLiteral ("KinematicValidationMissing");
    else if (status.reason.contains (QStringLiteral ("optimization"), Qt::CaseInsensitive)) status.reasonCode = QStringLiteral ("OptimizationArtifactMissing");
}

void addRequired (rws::WorkflowStageStatus& status, const QString& resourceId)
{
    if (!status.requiredResourceIds.contains (resourceId))
        status.requiredResourceIds.append (resourceId);
}

}    // namespace

namespace rws {

WorkflowStageSnapshot WorkflowStageController::evaluate (const WorkflowProjectSnapshot& snapshot)
{
    WorkflowStageSnapshot result;
    WorkflowStageStatus& modeling = result.at (WorkflowStage::Modeling);
    modeling.modelFingerprint = snapshot.modelFingerprint;
    modeling.state = snapshot.modelAvailable && snapshot.sceneAvailable
                         ? WorkflowStageState::Complete
                         : WorkflowStageState::Available;
    if (!snapshot.modelAvailable) {
        modeling.reason = QStringLiteral ("Robot model is not available yet.");
        addRequired (modeling, QStringLiteral ("robot-model.main"));
    }
    if (!snapshot.sceneAvailable) {
        modeling.reason = QStringLiteral ("Main WorkCell scene is not available yet.");
        addRequired (modeling, QStringLiteral ("scene.main"));
    }

    WorkflowStageStatus& requirements = result.at (WorkflowStage::Requirements);
    requirements.modelFingerprint = snapshot.modelFingerprint;
    requirements.requirementFingerprint = snapshot.requirementFingerprint;
    if (modeling.state != WorkflowStageState::Complete) {
        requirements.reason = QStringLiteral ("Complete modeling before editing requirements.");
    }
    else if (!snapshot.requirementsFrozen || snapshot.requirementFingerprint.isEmpty ()) {
        requirements.state = WorkflowStageState::Available;
        requirements.reason = QStringLiteral ("Freeze requirements to unlock kinematic validation.");
        addRequired (requirements, QStringLiteral ("engineering-requirements.main"));
    }
    else if (!equalIfPresent (snapshot.requirementModelFingerprint, snapshot.modelFingerprint) ||
             !sceneEvidenceMatches (snapshot, snapshot.requirementSceneFingerprint)) {
        requirements.state = WorkflowStageState::Stale;
        requirements.reason = QStringLiteral ("Requirements no longer match the current model or scene.");
    }
    else {
        requirements.state = WorkflowStageState::Complete;
    }

    WorkflowStageStatus& kinematics = result.at (WorkflowStage::Kinematics);
    kinematics.modelFingerprint = snapshot.modelFingerprint;
    kinematics.requirementFingerprint = snapshot.requirementFingerprint;
    // A stale requirement set is still actionable. Keep Kinematics open so
    // the user can re-run validation and repair the published evidence.
    if (requirements.state == WorkflowStageState::Complete ||
        requirements.state == WorkflowStageState::Stale) {
        if (!snapshot.kinematicValidationPassed || snapshot.kinematicValidationFingerprint.isEmpty ()) {
            kinematics.state = WorkflowStageState::Available;
            kinematics.reason = QStringLiteral ("Run kinematic validation before optimization.");
            addRequired (kinematics, QStringLiteral ("kinematic-analysis.main"));
        }
        else if (!equalIfPresent (snapshot.kinematicModelFingerprint, snapshot.modelFingerprint) ||
                 !equalIfPresent (snapshot.kinematicRequirementFingerprint,
                                  snapshot.requirementFingerprint) ||
                 !sceneEvidenceMatches (snapshot, snapshot.kinematicSceneFingerprint)) {
            kinematics.state = WorkflowStageState::Stale;
            kinematics.reason = QStringLiteral ("Kinematic validation is stale for the current inputs.");
        }
        else {
            kinematics.state = WorkflowStageState::Complete;
        }
    }
    else {
        kinematics.reason = QStringLiteral ("Complete and freeze requirements first.");
    }

    WorkflowStageStatus& optimization = result.at (WorkflowStage::StructuralOptimization);
    optimization.modelFingerprint = snapshot.modelFingerprint;
    optimization.requirementFingerprint = snapshot.requirementFingerprint;
    // Structural optimization can be opened as soon as requirements have
    // been published. The evaluator runs its own kinematic checks for each
    // candidate; a saved Kinematics page result is not a prerequisite.
    if (requirements.state == WorkflowStageState::Complete ||
        requirements.state == WorkflowStageState::Stale) {
        if (!snapshot.optimizationArtifactAvailable) {
            optimization.state = WorkflowStageState::Available;
            optimization.reason = QStringLiteral ("Create a structural optimization result.");
            addRequired (optimization, QStringLiteral ("structure-optimization.main"));
        }
        else if (!equalIfPresent (snapshot.optimizationModelFingerprint, snapshot.modelFingerprint) ||
                 !equalIfPresent (snapshot.optimizationRequirementFingerprint,
                                  snapshot.requirementFingerprint) ||
                 !sceneEvidenceMatches (snapshot, snapshot.optimizationSceneFingerprint)) {
            optimization.state = WorkflowStageState::Stale;
            optimization.reason = QStringLiteral ("Optimization result is stale for the current inputs.");
        }
    else {
        optimization.state = WorkflowStageState::Complete;
    }
    }
    else {
        optimization.reason = QStringLiteral ("Publish requirements before optimization.");
    }
    for (WorkflowStage stage : {WorkflowStage::Modeling, WorkflowStage::Requirements,
                                WorkflowStage::Kinematics, WorkflowStage::StructuralOptimization})
        deriveReasonCode(result.at(stage));
    return result;
}

bool WorkflowStageController::isStageAccessible (WorkflowStageState state)
{
    return state == WorkflowStageState::Available || state == WorkflowStageState::Complete ||
           state == WorkflowStageState::Stale;
}

void WorkflowStageController::invalidateFrom (WorkflowProjectSnapshot& snapshot,
                                              WorkflowStage stage)
{
    if (static_cast< int > (stage) <= static_cast< int > (WorkflowStage::Requirements)) {
        snapshot.requirementsFrozen = false;
        snapshot.requirementFingerprint.clear ();
        snapshot.requirementModelFingerprint.clear ();
        snapshot.requirementSceneFingerprint.clear ();
    }
    if (static_cast< int > (stage) <= static_cast< int > (WorkflowStage::Kinematics)) {
        snapshot.kinematicValidationPassed = false;
        snapshot.kinematicValidationFingerprint.clear ();
        snapshot.kinematicModelFingerprint.clear ();
        snapshot.kinematicRequirementFingerprint.clear ();
        snapshot.kinematicSceneFingerprint.clear ();
    }
    if (static_cast< int > (stage) <= static_cast< int > (WorkflowStage::StructuralOptimization)) {
        snapshot.optimizationArtifactAvailable = false;
        snapshot.optimizationModelFingerprint.clear ();
        snapshot.optimizationRequirementFingerprint.clear ();
        snapshot.optimizationKinematicFingerprint.clear ();
        snapshot.optimizationSceneFingerprint.clear ();
    }
}

}    // namespace rws
