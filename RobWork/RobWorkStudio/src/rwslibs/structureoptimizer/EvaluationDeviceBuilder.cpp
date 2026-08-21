#include "EvaluationDeviceBuilder.hpp"

namespace rws {
namespace {

void addError(EvaluationDeviceBuildResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "structure-optimizer";
    diagnostic.stage = "evaluation-device-build";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

}    // namespace

EvaluationDeviceBuildResult EvaluationDeviceBuilder::build(
    const EvaluationDeviceBuildRequest& request)
{
    EvaluationDeviceBuildResult result;
    if (request.model == nullptr) {
        addError(result, "S38_BUILD_MODEL_REQUIRED", "model",
                 "A canonical model is required to build an evaluation device.");
        return result;
    }
    const RobotModelSpecProjectionResult projection =
        RobotModelSpecProjectionAdapter::project({request.model, request.deviceName, {},
                                                  request.sourceSnapshot});
    if (!projection.ok) {
        result.diagnostics = projection.diagnostics;
        return result;
    }
    result.projectedSpec = projection.spec;

    CandidateModelFactory factory;
    CandidateModelBuildRequest buildRequest;
    buildRequest.spec = projection.spec;
    buildRequest.deviceName = projection.spec.robotName;
    // CandidateModelFactory's empty TCP request deliberately uses the
    // selected Device end frame, avoiding a second independent frame lookup
    // while retaining an explicit post-build name check below.
    buildRequest.tcpFrame.clear();
    buildRequest.checkCollision = request.checkCollision;
    const CandidateModelBuildResult built = factory.build(buildRequest);
    if (!built.ok) {
        for (const AnalysisWarning& warning : built.warnings) {
            StructureOptimizationDiagnostic diagnostic;
            diagnostic.code = warning.code;
            diagnostic.severity = warning.severity == AnalysisStatus::Fail ? "Error" : "Warning";
            diagnostic.subsystem = "structure-optimizer";
            diagnostic.stage = "evaluation-device-build";
            diagnostic.message = warning.message;
            result.diagnostics.push_back(diagnostic);
        }
        return result;
    }
    if (!request.tcpFrame.empty() &&
        (built.artifact.tcpFrame.isNull() ||
         (built.artifact.tcpFrame->getName() != request.tcpFrame &&
          built.artifact.tcpFrame->getName().find("." + request.tcpFrame) == std::string::npos))) {
        addError(result, "S38_BUILD_TCP_MISMATCH", "tcpFrame",
                 "The generated Device end frame does not match the requested canonical TCP frame: " +
                 (built.artifact.tcpFrame.isNull() ? std::string("<null>") :
                  built.artifact.tcpFrame->getName()));
        return result;
    }
    result.ok = true;
    result.artifact = built.artifact;
    return result;
}

}    // namespace rws
