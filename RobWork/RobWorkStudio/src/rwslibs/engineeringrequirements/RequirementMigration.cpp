#include "RequirementMigration.hpp"

#include "RequirementFreezer.hpp"

#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

namespace rws {
namespace {

void addMigrationDiagnostic(std::vector<RequirementDiagnostic>& diagnostics,
                            const std::string& code,
                            const std::string& message,
                            const std::string& requirementId = std::string())
{
    RequirementDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.requirementId = requirementId;
    diagnostic.level = RequirementLevel::Should;
    diagnostic.message = message;
    diagnostic.blocking = false;
    diagnostics.push_back(diagnostic);
}

} // namespace

bool migrateRequirementArtifact(const QJsonObject& input,
                                QJsonObject& output,
                                std::vector<RequirementDiagnostic>& diagnostics,
                                std::string* error)
{
    diagnostics.clear();
    if (input.value("type").toString() != "FrozenEngineeringRequirementArtifact") {
        addMigrationDiagnostic(diagnostics, "REQ_SCHEMA_UNSUPPORTED",
                               "Input is not a frozen engineering requirement artifact.");
        if (error != nullptr) *error = "Unsupported frozen requirement artifact type.";
        return false;
    }

    const int schemaVersion = input.value("schemaVersion").toInt(1);
    if (schemaVersion == 4) {
        output = input;
        if (error != nullptr) error->clear();
        return true;
    }
    if (schemaVersion != 3) {
        addMigrationDiagnostic(diagnostics, "REQ_SCHEMA_UNSUPPORTED",
                               "Only frozen requirement artifact schema v3 can be migrated.");
        if (error != nullptr) *error = "Unsupported frozen requirement artifact schema.";
        return false;
    }

    FrozenRequirementArtifact artifact;
    std::string parseError;
    if (!FrozenRequirementArtifactJson::fromObject(input, artifact, &parseError)) {
        addMigrationDiagnostic(diagnostics, "REQ_SCHEMA_UNSUPPORTED",
                               parseError.empty() ? "The v3 artifact could not be parsed." : parseError);
        if (error != nullptr) *error = parseError;
        return false;
    }

    artifact.schemaVersion = 4;
    for (WorkspaceDemandRegion& region : artifact.compiled.workspaceRegions) {
        region.minimumVerificationStage = RequirementVerificationStage::Quick;
        RequirementDiagnostic migrationDiagnostic;
        migrationDiagnostic.code = "REQ_V3_REQUIRES_REFREEZE";
        migrationDiagnostic.requirementId = region.id;
        migrationDiagnostic.level = RequirementLevel::Should;
        migrationDiagnostic.message =
            "The v3 workspace region is available for Quick analysis only; refreeze before Verified acceptance.";
        migrationDiagnostic.blocking = false;
        artifact.compiled.diagnostics.push_back(migrationDiagnostic);
        diagnostics.push_back(migrationDiagnostic);
    }
    for (RequirementExecutionRegion& region : artifact.execution.workspaceRegions) {
        region.minimumVerificationStage = RequirementExecutionStage::Quick;
        RequirementExecutionDiagnostic executionDiagnostic;
        executionDiagnostic.code = "REQ_V3_REQUIRES_REFREEZE";
        executionDiagnostic.requirementId = region.id;
        executionDiagnostic.severity = RequirementExecutionDiagnosticSeverity::Warning;
        executionDiagnostic.message =
            "The v3 workspace region is available for Quick analysis only; refreeze before Verified acceptance.";
        executionDiagnostic.source = "engineeringrequirements.migration";
        region.diagnostics.push_back(executionDiagnostic);
    }
    RequirementExecutionDiagnostic setDiagnostic;
    setDiagnostic.code = "REQ_V3_REQUIRES_REFREEZE";
    setDiagnostic.severity = RequirementExecutionDiagnosticSeverity::Warning;
    setDiagnostic.message =
        "The v3 workspace region is available for Quick analysis only; refreeze before Verified acceptance.";
    setDiagnostic.source = "engineeringrequirements.migration";
    artifact.execution.diagnostics.push_back(setDiagnostic);

    artifact.schemaVersion = 4;
    artifact.executionFingerprint = RequirementExecutionJson::fingerprint(artifact.execution);
    output = FrozenRequirementArtifactJson::toObject(artifact);
    if (error != nullptr) error->clear();
    return true;
}

} // namespace rws
