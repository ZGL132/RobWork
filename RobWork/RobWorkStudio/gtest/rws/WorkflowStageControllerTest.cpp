/********************************************************************************
 * Copyright 2026 The Robotics Group, The Maersk Mc-Kinney Moller Institute,
 * Faculty of Engineering, University of Southern Denmark
 ********************************************************************************/

#include <rws/WorkflowStageController.hpp>
#include <rws/WorkflowProjectState.hpp>

#include <QJsonObject>

#include <gtest/gtest.h>

namespace {

rws::WorkflowProjectSnapshot completeModelSnapshot ()
{
    rws::WorkflowProjectSnapshot snapshot;
    snapshot.fingerprintVersion = 2;
    snapshot.modelAvailable = true;
    snapshot.sceneAvailable = true;
    snapshot.modelFingerprint = QStringLiteral ("model-v1");
    snapshot.sceneFingerprint = QStringLiteral ("scene-v1");
    return snapshot;
}

TEST (WorkflowStageController, UnlocksStagesInWorkflowOrder)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    const rws::WorkflowStageSnapshot states = rws::WorkflowStageController::evaluate (snapshot);

    EXPECT_EQ (rws::WorkflowStageState::Complete,
               states.at (rws::WorkflowStage::Modeling).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               states.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Locked,
               states.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_EQ (rws::WorkflowStageState::Locked,
               states.at (rws::WorkflowStage::StructuralOptimization).state);

    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = QStringLiteral ("model-v1");
    snapshot.requirementSceneFingerprint = QStringLiteral ("scene-v1");
    const rws::WorkflowStageSnapshot requirementsReady =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Complete,
               requirementsReady.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               requirementsReady.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               requirementsReady.at (rws::WorkflowStage::StructuralOptimization).state);

    snapshot.kinematicValidationPassed = true;
    snapshot.kinematicValidationFingerprint = QStringLiteral ("kinematics-v1");
    snapshot.kinematicModelFingerprint = QStringLiteral ("model-v1");
    snapshot.kinematicRequirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.kinematicSceneFingerprint = QStringLiteral ("scene-v1");
    const rws::WorkflowStageSnapshot kinematicsReady =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Complete,
               kinematicsReady.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               kinematicsReady.at (rws::WorkflowStage::StructuralOptimization).state);
}

TEST (WorkflowStageController, UnlocksOptimizationAfterPublishedRequirementsWithoutKinematics)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = snapshot.modelFingerprint;
    snapshot.requirementSceneFingerprint = snapshot.sceneFingerprint;

    const rws::WorkflowStageSnapshot states = rws::WorkflowStageController::evaluate (snapshot);

    EXPECT_EQ (rws::WorkflowStageState::Complete,
               states.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               states.at (rws::WorkflowStage::StructuralOptimization).state);
    EXPECT_TRUE (rws::WorkflowStageController::isStageAccessible (
        states.at (rws::WorkflowStage::StructuralOptimization).state));
}

TEST (WorkflowStageController, FailedValidationKeepsKinematicsAvailableForRepair)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = snapshot.modelFingerprint;
    snapshot.requirementSceneFingerprint = snapshot.sceneFingerprint;
    snapshot.kinematicValidationPassed = false;

    const rws::WorkflowStageSnapshot states = rws::WorkflowStageController::evaluate (snapshot);

    EXPECT_EQ (rws::WorkflowStageState::Complete,
               states.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               states.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_TRUE (rws::WorkflowStageController::isStageAccessible (
        states.at (rws::WorkflowStage::Kinematics).state));
    EXPECT_EQ (rws::WorkflowStageState::Available,
               states.at (rws::WorkflowStage::StructuralOptimization).state);
}

TEST (WorkflowStageController, DetectsStaleArtifactsWithoutRelockingAccessibleStages)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = QStringLiteral ("model-v1");
    snapshot.requirementSceneFingerprint = QStringLiteral ("scene-v1");
    snapshot.kinematicValidationPassed = true;
    snapshot.kinematicValidationFingerprint = QStringLiteral ("kinematics-v1");
    snapshot.kinematicModelFingerprint = QStringLiteral ("model-v1");
    snapshot.kinematicRequirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.kinematicSceneFingerprint = QStringLiteral ("scene-v1");
    snapshot.optimizationArtifactAvailable = true;
    snapshot.optimizationModelFingerprint = QStringLiteral ("model-v1");
    snapshot.optimizationRequirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.optimizationKinematicFingerprint = QStringLiteral ("kinematics-v1");
    snapshot.optimizationSceneFingerprint = QStringLiteral ("scene-v1");

    snapshot.modelFingerprint = QStringLiteral ("model-v2");
    const rws::WorkflowStageSnapshot modelChanged =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Stale,
               modelChanged.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Stale,
               modelChanged.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_EQ (rws::WorkflowStageState::Stale,
               modelChanged.at (rws::WorkflowStage::StructuralOptimization).state);

    snapshot = completeModelSnapshot ();
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = QStringLiteral ("model-v1");
    snapshot.requirementSceneFingerprint = QStringLiteral ("scene-v1");
    snapshot.kinematicValidationPassed = true;
    snapshot.kinematicValidationFingerprint = QStringLiteral ("kinematics-v1");
    snapshot.kinematicModelFingerprint = QStringLiteral ("model-v1");
    snapshot.kinematicRequirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.kinematicSceneFingerprint = QStringLiteral ("scene-v1");
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v2");
    const rws::WorkflowStageSnapshot requirementsChanged =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Complete,
               requirementsChanged.at (rws::WorkflowStage::Requirements).state);
    EXPECT_EQ (rws::WorkflowStageState::Stale,
               requirementsChanged.at (rws::WorkflowStage::Kinematics).state);
    EXPECT_EQ (rws::WorkflowStageState::Available,
               requirementsChanged.at (rws::WorkflowStage::StructuralOptimization).state);
}

TEST (WorkflowStageController, AcceptsLegacySceneEvidenceOnlyWhenRawFingerprintMatches)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    snapshot.fingerprintVersion = 1;
    snapshot.legacySceneFingerprint = QStringLiteral ("legacy-scene-v1");
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = snapshot.modelFingerprint;
    snapshot.requirementSceneFingerprint = snapshot.legacySceneFingerprint;

    const rws::WorkflowStageSnapshot matching =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Complete,
               matching.at (rws::WorkflowStage::Requirements).state);

    snapshot.legacySceneFingerprint = QStringLiteral ("legacy-scene-v2");
    const rws::WorkflowStageSnapshot changed =
        rws::WorkflowStageController::evaluate (snapshot);
    EXPECT_EQ (rws::WorkflowStageState::Stale,
               changed.at (rws::WorkflowStage::Requirements).state);
}

TEST (WorkflowStageController, MissingResourcesKeepLaterStagesLocked)
{
    rws::WorkflowProjectSnapshot snapshot;
    snapshot.modelAvailable = true;
    const rws::WorkflowStageSnapshot states = rws::WorkflowStageController::evaluate (snapshot);

    EXPECT_EQ (rws::WorkflowStageState::Available,
               states.at (rws::WorkflowStage::Modeling).state);
    EXPECT_EQ (rws::WorkflowStageState::Locked,
               states.at (rws::WorkflowStage::Requirements).state);
    EXPECT_FALSE (states.at (rws::WorkflowStage::Requirements).reason.isEmpty ());
}

TEST (WorkflowStageController, AllowsRepairingStaleStagesWithoutUnlockingLockedStages)
{
    EXPECT_FALSE (rws::WorkflowStageController::isStageAccessible (
        rws::WorkflowStageState::Locked));
    EXPECT_TRUE (rws::WorkflowStageController::isStageAccessible (
        rws::WorkflowStageState::Available));
    EXPECT_TRUE (rws::WorkflowStageController::isStageAccessible (
        rws::WorkflowStageState::Complete));
    EXPECT_TRUE (rws::WorkflowStageController::isStageAccessible (
        rws::WorkflowStageState::Stale));
}

TEST (WorkflowProjectState, PersistsEvidenceAndInvalidatesDownstreamStages)
{
    rws::WorkflowProjectSnapshot snapshot = completeModelSnapshot ();
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementModelFingerprint = snapshot.modelFingerprint;
    snapshot.requirementSceneFingerprint = snapshot.sceneFingerprint;
    snapshot.kinematicValidationPassed = true;
    snapshot.kinematicValidationFingerprint = QStringLiteral ("kinematics-v1");
    snapshot.kinematicModelFingerprint = snapshot.modelFingerprint;
    snapshot.kinematicRequirementFingerprint = snapshot.requirementFingerprint;
    snapshot.kinematicSceneFingerprint = snapshot.sceneFingerprint;

    QJsonObject plugins;
    rws::WorkflowProjectState::write (plugins, snapshot);

    const rws::WorkflowProjectSnapshot restored = rws::WorkflowProjectState::read (plugins);
    EXPECT_TRUE (restored.requirementsFrozen);
    EXPECT_EQ (QStringLiteral ("requirements-v1"), restored.requirementFingerprint);
    EXPECT_TRUE (restored.kinematicValidationPassed);
    EXPECT_EQ (QStringLiteral ("kinematics-v1"), restored.kinematicValidationFingerprint);

    rws::WorkflowProjectSnapshot changed = restored;
    rws::WorkflowStageController::invalidateFrom (changed, rws::WorkflowStage::Requirements);
    rws::WorkflowProjectState::write (plugins, changed);

    const rws::WorkflowProjectSnapshot invalidated = rws::WorkflowProjectState::read (plugins);
    EXPECT_FALSE (invalidated.requirementsFrozen);
    EXPECT_FALSE (invalidated.kinematicValidationPassed);
    EXPECT_FALSE (invalidated.optimizationArtifactAvailable);
}

TEST (WorkflowProjectState, PreservesLegacyFingerprintVersionUntilEvidenceIsMigrated)
{
    rws::WorkflowProjectSnapshot snapshot;
    snapshot.fingerprintVersion = 1;
    snapshot.requirementsFrozen = true;
    snapshot.requirementFingerprint = QStringLiteral ("requirements-v1");
    snapshot.requirementSceneFingerprint = QStringLiteral ("legacy-scene-v1");

    QJsonObject plugins;
    rws::WorkflowProjectState::write (plugins, snapshot);

    const rws::WorkflowProjectSnapshot restored =
        rws::WorkflowProjectState::read (plugins);
    EXPECT_EQ (1, restored.fingerprintVersion);
    EXPECT_EQ (snapshot.requirementSceneFingerprint,
               restored.requirementSceneFingerprint);
}

}    // namespace
