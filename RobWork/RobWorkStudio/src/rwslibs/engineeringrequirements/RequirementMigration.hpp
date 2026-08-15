#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTMIGRATION_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTMIGRATION_HPP

#include "EngineeringRequirementTypes.hpp"

#include <QJsonObject>

#include <string>
#include <vector>

namespace rws {

struct RequirementDocumentMigrationResult {
    QJsonObject document;
    bool migrated = false;
    std::vector<std::string> warnings;
};

/**
 * Normalize historical complete requirement documents before RequirementSet parsing.
 * frozenArtifact is a document-envelope field: the canonical representation is the
 * top-level field, while the legacy extensions.frozenArtifact copy is promoted or removed.
 */
bool migrateRequirementDocument(const QJsonObject& input,
                                RequirementDocumentMigrationResult& result,
                                std::string* error = nullptr);

/**
 * Convert a v3 frozen requirement artifact to the v4 execution contract.
 * The input object is never modified. v3 workspace regions are deliberately
 * downgraded to Quick and produce REQ_V3_REQUIRES_REFREEZE diagnostics.
 */
bool migrateRequirementArtifact(const QJsonObject& input,
                                QJsonObject& output,
                                std::vector<RequirementDiagnostic>& diagnostics,
                                std::string* error = nullptr);

} // namespace rws

#endif
