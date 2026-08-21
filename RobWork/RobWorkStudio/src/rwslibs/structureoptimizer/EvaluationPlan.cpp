#include "EvaluationPlan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace rws {
namespace {

void appendEscaped(std::ostringstream& out, const std::string& value)
{
    out << value.size() << ':' << value;
}

void diagnostic(EvaluationPlan& plan, const char* code, const char* field,
                const std::string& message, bool blocking = true)
{
    plan.diagnostics.push_back({code, field, message, blocking});
}

bool finite(double value) { return std::isfinite(value); }

std::string fingerprintFor(const EvaluationPlan& plan)
{
    std::ostringstream canonical;
    canonical << plan.schemaVersion << '|';
    appendEscaped(canonical, plan.modelFingerprint);
    appendEscaped(canonical, plan.environmentFingerprint);
    appendEscaped(canonical, plan.toolFingerprint);
    appendEscaped(canonical, plan.requirementFingerprint);
    appendEscaped(canonical, plan.evaluatorId);
    appendEscaped(canonical, plan.evaluatorVersion);
    for (const auto& metric : plan.metricIds) appendEscaped(canonical, metric);
    for (const auto& capability : plan.capabilities) appendEscaped(canonical, capability);
    for (const auto& task : plan.tasks) {
        const auto& value = task.source;
        appendEscaped(canonical, value.id);
        canonical << static_cast<int>(value.level) << static_cast<int>(value.compileState)
                  << value.position[0] << ',' << value.position[1] << ',' << value.position[2]
                  << value.rpyDeg[0] << ',' << value.rpyDeg[1] << ',' << value.rpyDeg[2]
                  << task.hardConstraint << task.evidenceRequired;
    }
    for (const auto& region : plan.regions) {
        const auto& value = region.source;
        appendEscaped(canonical, value.id);
        canonical << static_cast<int>(value.level) << static_cast<int>(value.compileState)
                  << value.samplesPerAxis << value.directionSamples << value.rollSamples
                  << value.minimumCoverage << value.minimumOrientationCoverage
                  << region.hardConstraint << region.evidenceRequired;
    }

    // Stable, dependency-free FNV-1a is sufficient for identity within this
    // execution contract; the source fingerprints remain the cryptographic
    // identity of model, scene, tool and requirements.
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char byte : canonical.str()) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    std::ostringstream hex;
    hex << std::hex << std::setw(16) << std::setfill('0') << hash;
    return hex.str();
}

} // namespace

bool EvaluationPlan::hasBlockingDiagnostics() const
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const EvaluationPlanDiagnostic& value) { return value.blocking; });
}

EvaluationPlan EvaluationPlanCompiler::compile(const RequirementExecutionSet& requirements,
                                               const EvaluationPlanCompilerOptions& options)
{
    EvaluationPlan plan;
    plan.schemaVersion = options.outputSchemaVersion;
    plan.modelFingerprint = options.modelFingerprint;
    plan.environmentFingerprint = options.environmentFingerprint;
    plan.toolFingerprint = options.toolFingerprint;
    plan.requirementFingerprint = requirements.provenance.requirementFingerprint;
    plan.evaluatorId = options.evaluatorId;
    plan.evaluatorVersion = options.evaluatorVersion;
    plan.capabilities = options.capabilities;
    plan.metricIds = options.metricIds;

    if (requirements.schemaVersion < 3)
        diagnostic(plan, "REQUIREMENT_SCHEMA_UNSUPPORTED", "schemaVersion",
                   "RequirementExecutionSet schema must be at least v3.");
    if (plan.requirementFingerprint.empty())
        diagnostic(plan, "REQUIREMENT_FINGERPRINT_MISSING", "provenance.requirementFingerprint",
                   "A frozen requirement fingerprint is required.");
    if (options.modelFingerprint.empty())
        diagnostic(plan, "MODEL_FINGERPRINT_MISSING", "modelFingerprint",
                   "A model fingerprint is required.");
    if (!requirements.provenance.robotModelFingerprint.empty() &&
        !options.modelFingerprint.empty() &&
        requirements.provenance.robotModelFingerprint != options.modelFingerprint)
        diagnostic(plan, "MODEL_FINGERPRINT_MISMATCH", "modelFingerprint",
                   "Frozen requirements belong to a different robot model.");
    if (!requirements.provenance.environmentFingerprint.empty() &&
        !options.environmentFingerprint.empty() &&
        requirements.provenance.environmentFingerprint != options.environmentFingerprint)
        diagnostic(plan, "ENVIRONMENT_FINGERPRINT_MISMATCH", "environmentFingerprint",
                   "Frozen requirements belong to a different environment.");

    for (const auto& metric : plan.metricIds) {
        if (!options.knownMetricIds.empty() && options.knownMetricIds.count(metric) == 0)
            diagnostic(plan, "METRIC_UNKNOWN", "metricIds", "Unknown metric: " + metric);
    }

    for (const auto& source : requirements.tasks) {
        if (source.compileState != RequirementExecutionCompileState::Included)
            continue;
        EvaluationPlanTask task;
        task.source = source;
        task.hardConstraint = source.level == RequirementExecutionLevel::Must;
        task.evidenceRequired = task.hardConstraint;
        if (source.collisionFreeRequired && options.capabilities.count("collision") == 0)
            diagnostic(plan, "CAPABILITY_MISSING", ("tasks." + source.id).c_str(),
                       "Collision evidence is required but the evaluator has no collision capability.");
        for (double value : source.position)
            if (!finite(value)) diagnostic(plan, "VALUE_NONFINITE", ("tasks." + source.id).c_str(),
                                           "Task position contains a non-finite value.");
        plan.tasks.push_back(std::move(task));
    }
    for (const auto& source : requirements.workspaceRegions) {
        if (source.compileState != RequirementExecutionCompileState::Included)
            continue;
        EvaluationPlanRegion region;
        region.source = source;
        region.hardConstraint = source.level == RequirementExecutionLevel::Must;
        region.evidenceRequired = region.hardConstraint ||
                                  source.minimumVerificationStage == RequirementExecutionStage::Verified;
        if (requirements.schemaVersion == 3 &&
            source.minimumVerificationStage == RequirementExecutionStage::Verified)
            diagnostic(plan, "REQUIREMENT_SCHEMA_VERIFIED_UNSUPPORTED", ("workspaceRegions." + source.id).c_str(),
                       "Schema v3 Verified regions cannot be executed by the v4 plan.");
        if (source.collisionFreeRequired && options.capabilities.count("collision") == 0)
            diagnostic(plan, "CAPABILITY_MISSING", ("workspaceRegions." + source.id).c_str(),
                       "Collision evidence is required but the evaluator has no collision capability.");

        if (source.samplesPerAxis <= 0 || source.samplesPerAxis > MaxExecutionWorkspaceSamplesPerAxis)
            diagnostic(plan, "REGION_SAMPLING_INVALID", ("workspaceRegions." + source.id).c_str(),
                       "samplesPerAxis is outside the execution safety limit.");
        plan.regions.push_back(std::move(region));
    }

    plan.status = plan.hasBlockingDiagnostics() ? EvaluationPlanStatus::Invalid
                                                : EvaluationPlanStatus::Valid;
    plan.fingerprint = fingerprintFor(plan);
    return plan;
}

const char* toString(EvaluationPlanStatus status)
{
    return status == EvaluationPlanStatus::Valid ? "Valid" : "Invalid";
}

} // namespace rws
