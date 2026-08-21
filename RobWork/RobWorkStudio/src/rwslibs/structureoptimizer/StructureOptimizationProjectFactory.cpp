#include "StructureOptimizationProjectFactory.hpp"

#include "CanonicalModelShadowService.hpp"
#include "StructureOptimizationUiLogic.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>

namespace rws {

bool StructureOptimizationProjectFactory::create(const RobotModelSpec& spec,
                                                 StructureOptimizationProblem& problem,
                                                 std::string* error)
{
    if (spec.robotName.empty() || spec.transformJoints.empty()) {
        if (error != nullptr) {
            *error = "StructureOptimization.Context.Invalid: complete RobotModelSpec is required.";
        }
        return false;
    }

    StructureOptimizationProblem created;
    created.context.modelSpec = spec;
    created.context.projectName = spec.robotName;
    created.context.robotName = spec.robotName;
    created.context.deviceName = spec.robotName;
    created.variables = StructureOptimizationUiLogic::suggestVariables(created.context);

    problem = std::move(created);
    if (error != nullptr)
        error->clear();
    return true;
}

bool StructureOptimizationProjectFactory::create(const RobotModelSpec& spec,
                                                 const QString& sourceModelPath,
                                                 StructureOptimizationProblem& problem,
                                                 std::string* error)
{
    if (!create(spec, problem, error))
        return false;

    const QString trimmedPath = sourceModelPath.trimmed();
    if (!trimmedPath.isEmpty()) {
        const std::string fingerprint = RobotModelFingerprint::canonicalSha256(spec);
        problem.context.sourceModelPath = trimmedPath.toStdString();
        problem.context.modelProvenance = {problem.context.sourceModelPath, fingerprint,
                                           fingerprint};
    }
    return true;
}

bool StructureOptimizationProjectFactory::create(const RobotModelSpec& spec,
                                                 const KinematicImportRequest& importRequest,
                                                 StructureOptimizationProblem& problem,
                                                 std::string* error)
{
    StructureOptimizationProblem created;
    if (!create(spec, created, error))
        return false;

    KinematicImportRequest request = importRequest;
    if (request.sourceSnapshot == nullptr)
        request.sourceSnapshot = &spec;
    if (request.sourceFingerprint.empty())
        request.sourceFingerprint = RobotModelFingerprint::canonicalSha256(spec);
    if (!CanonicalModelShadowService::attach(request, created, error))
        return false;

    problem = std::move(created);
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
