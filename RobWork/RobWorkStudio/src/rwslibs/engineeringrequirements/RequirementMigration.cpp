#include "RequirementMigration.hpp"

#include "RequirementFreezer.hpp"

#include <rwslibs/robotanalysiscore/RequirementExecutionJson.hpp>

#include <cmath>
#include <limits>

namespace rws {
namespace {

void addMigrationDiagnostic(std::vector<RequirementDiagnostic>& diagnostics,
                            const std::string& code,
                            const std::string& message,
                            const std::string& requirementId = std::string())
{
    RequirementDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = RequirementDiagnosticSeverity::Warning;
    diagnostic.requirementId = requirementId;
    diagnostic.level = RequirementLevel::Should;
    diagnostic.field = "schemaVersion";
    diagnostic.message = message;
    diagnostic.source = "engineeringrequirements.migration";
    diagnostic.blocking = false;
    diagnostics.push_back(diagnostic);
}

bool readArtifactHeader(const QJsonObject& input, int& schemaVersion, std::string* error)
{
    const QJsonValue type = input.value("type");
    if (!type.isString()) {
        if (error != nullptr)
            *error = "Frozen artifact header field 'type' must be a string.";
        return false;
    }
    if (type.toString() != "FrozenEngineeringRequirementArtifact") {
        if (error != nullptr)
            *error = "Frozen artifact header field 'type' is unsupported.";
        return false;
    }

    const QJsonValue version = input.value("schemaVersion");
    if (!version.isDouble()) {
        if (error != nullptr)
            *error = "Frozen artifact header field 'schemaVersion' must be a JSON number.";
        return false;
    }
    const double numericVersion = version.toDouble();
    if (!std::isfinite(numericVersion) || std::floor(numericVersion) != numericVersion ||
        numericVersion < static_cast<double>(std::numeric_limits<int>::min()) ||
        numericVersion > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error != nullptr)
            *error = "Frozen artifact header field 'schemaVersion' must be a finite integer.";
        return false;
    }
    schemaVersion = static_cast<int>(numericVersion);
    return true;
}

} // namespace

bool migrateRequirementArtifact(const QJsonObject& input,
                                QJsonObject& output,
                                std::vector<RequirementDiagnostic>& diagnostics,
                                std::string* error)
{
    diagnostics.clear();
    int schemaVersion = 0;
    std::string headerError;
    if (!readArtifactHeader(input, schemaVersion, &headerError)) {
        addMigrationDiagnostic(diagnostics, "REQ_SCHEMA_UNSUPPORTED",
                               headerError);
        if (error != nullptr) *error = headerError;
        return false;
    }

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
        migrationDiagnostic.severity = RequirementDiagnosticSeverity::Warning;
        migrationDiagnostic.requirementId = region.id;
        migrationDiagnostic.level = RequirementLevel::Should;
        migrationDiagnostic.field = "minimumVerificationStage";
        migrationDiagnostic.message =
            "The v3 workspace region is available for Quick analysis only; refreeze before Verified acceptance.";
        migrationDiagnostic.source = "engineeringrequirements.migration";
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
