#ifndef RWS_STRUCTUREOPTIMIZATION_WORKFLOWRESOLVER_HPP
#define RWS_STRUCTUREOPTIMIZATION_WORKFLOWRESOLVER_HPP

#include <rws/WorkflowBinding.hpp>

#include <QStringList>

namespace rws {

struct StructureOptimizationWorkflowInputs {
    bool projectOpen = false;
    WorkflowBinding binding;
    QString currentProjectId;
    QString currentModelFingerprint;
    QString currentSceneFingerprint;
    QString currentEnvironmentFingerprint;
    QString currentRequirementFingerprint;
    QString currentKinematicValidationFingerprint;
    QString currentTcpFrame;
    QString persistedModelFingerprint;
    QString persistedSceneFingerprint;
    QString persistedEnvironmentFingerprint;
    QString persistedRequirementFingerprint;
    QString persistedKinematicValidationFingerprint;
    QString persistedTcpFrame;
    QString currentEvaluatorVersion;
    QString persistedEvaluatorVersion;
    QString currentCompilerVersion;
    QString persistedCompilerVersion;
    QString currentAdapterRegistryFingerprint;
    QString persistedAdapterRegistryFingerprint;
};

struct OptimizationRunPreconditions {
    bool canStart = false;
    bool cacheReusable = false;
    bool historicalRunsReadable = false;
    QString projectId;
    QString targetDevice;
    QString tcpFrame;
    QStringList blockingCodes;
    QStringList staleCodes;
};

class StructureOptimizationWorkflowResolver {
  public:
    static OptimizationRunPreconditions resolve(const StructureOptimizationWorkflowInputs& inputs);
};

} // namespace rws

#endif
