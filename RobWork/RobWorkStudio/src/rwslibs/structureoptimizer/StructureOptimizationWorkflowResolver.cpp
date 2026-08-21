#include "StructureOptimizationWorkflowResolver.hpp"

namespace rws {

namespace {
void block(OptimizationRunPreconditions& result, const QString& code)
{
    if (!result.blockingCodes.contains(code)) result.blockingCodes.append(code);
}

void stale(OptimizationRunPreconditions& result, const QString& code)
{
    if (!result.staleCodes.contains(code)) result.staleCodes.append(code);
}

void compareRequired(OptimizationRunPreconditions& result, const QString& current,
                     const QString& persisted, const QString& code)
{
    if (current.isEmpty() || persisted.isEmpty()) block(result, code + QStringLiteral("Missing"));
    else if (current != persisted) block(result, code);
}
}

OptimizationRunPreconditions StructureOptimizationWorkflowResolver::resolve(
    const StructureOptimizationWorkflowInputs& inputs)
{
    OptimizationRunPreconditions result;
    result.projectId = inputs.currentProjectId;
    result.targetDevice = inputs.binding.targetDevice;
    result.tcpFrame = inputs.currentTcpFrame;
    result.historicalRunsReadable = true;
    if (!inputs.projectOpen) block(result, QStringLiteral("ProjectClosed"));
    QString bindingError;
    if (!inputs.binding.isValid(&bindingError)) block(result, QStringLiteral("WorkflowBindingInvalid"));
    if (!inputs.currentProjectId.isEmpty() && inputs.binding.projectId != inputs.currentProjectId)
        block(result, QStringLiteral("WorkflowBindingProjectMismatch"));
    if (inputs.binding.targetDevice.isEmpty()) block(result, QStringLiteral("TargetDeviceMissing"));
    if (inputs.binding.tcpFrame.isEmpty() || inputs.currentTcpFrame.isEmpty()) block(result, QStringLiteral("TcpFrameMissing"));
    if (inputs.binding.sceneResourceId.isEmpty()) block(result, QStringLiteral("SceneResourceMissing"));
    if (inputs.binding.modelResourceId.isEmpty()) block(result, QStringLiteral("ModelResourceMissing"));
    compareRequired(result, inputs.currentModelFingerprint, inputs.persistedModelFingerprint, QStringLiteral("ModelFingerprintMismatch"));
    compareRequired(result, inputs.currentSceneFingerprint, inputs.persistedSceneFingerprint, QStringLiteral("SceneFingerprintMismatch"));
    compareRequired(result, inputs.currentEnvironmentFingerprint, inputs.persistedEnvironmentFingerprint, QStringLiteral("EnvironmentFingerprintMismatch"));
    compareRequired(result, inputs.currentRequirementFingerprint, inputs.persistedRequirementFingerprint, QStringLiteral("RequirementFingerprintMismatch"));
    compareRequired(result, inputs.currentKinematicValidationFingerprint, inputs.persistedKinematicValidationFingerprint, QStringLiteral("KinematicValidationStale"));
    if (!inputs.persistedTcpFrame.isEmpty() && inputs.currentTcpFrame != inputs.persistedTcpFrame)
        block(result, QStringLiteral("TcpChanged"));
    if (inputs.currentEvaluatorVersion != inputs.persistedEvaluatorVersion) stale(result, QStringLiteral("EvaluatorVersionChanged"));
    if (inputs.currentCompilerVersion != inputs.persistedCompilerVersion) stale(result, QStringLiteral("CompilerVersionChanged"));
    if (inputs.currentAdapterRegistryFingerprint != inputs.persistedAdapterRegistryFingerprint) stale(result, QStringLiteral("AdapterRegistryChanged"));
    result.cacheReusable = result.blockingCodes.isEmpty() && result.staleCodes.isEmpty();
    result.canStart = result.blockingCodes.isEmpty();
    return result;
}

} // namespace rws
