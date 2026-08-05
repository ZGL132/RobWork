#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTMIGRATION_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTMIGRATION_HPP

#include "EngineeringRequirementTypes.hpp"

#include <QJsonObject>

#include <string>
#include <vector>

namespace rws {

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
