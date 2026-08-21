#include "LegacyDesignSpaceAdapter.hpp"

#include "DesignSpaceRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace rws {
namespace {

void addDiagnostic(LegacyDesignSpaceMigrationPreview& result, const std::string& code,
                   const std::string& fieldPath, const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Warning";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "legacy-migration";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

std::string normalizedUnit(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) { return static_cast< char >(std::tolower(character)); });
    return value;
}

bool unitScale(const std::string& legacyUnit, const DesignVariableUnit targetUnit, double& scale)
{
    const std::string unit = normalizedUnit(legacyUnit);
    switch (targetUnit) {
    case DesignVariableUnit::Metres:
        if (unit == "m") { scale = 1.0; return true; }
        if (unit == "mm") { scale = 0.001; return true; }
        if (unit == "cm") { scale = 0.01; return true; }
        return false;
    case DesignVariableUnit::Radians:
        if (unit == "rad") { scale = 1.0; return true; }
        if (unit == "deg") { scale = 3.14159265358979323846 / 180.0; return true; }
        return false;
    case DesignVariableUnit::Unitless:
        if (unit.empty() || unit == "1" || unit == "unitless") { scale = 1.0; return true; }
        return false;
    default:
        return false;
    }
}

VariableDomain convertDomain(const DesignVariableDomain domain)
{
    switch (domain) {
    case DesignVariableDomain::Integer: return VariableDomain::Integer;
    case DesignVariableDomain::Discrete: return VariableDomain::Discrete;
    case DesignVariableDomain::Continuous: return VariableDomain::Continuous;
    }
    return VariableDomain::Continuous;
}

void disable(LegacyDesignSpaceMigrationPreview& result, LegacyDesignSpaceMigrationEntry& entry,
             const std::string& disposition, const std::string& code, const std::string& message)
{
    entry.mapped = false;
    entry.disposition = disposition;
    entry.variable.id = entry.source.id;
    entry.variable.displayName = entry.source.label;
    entry.variable.source = DesignVariableSource::Legacy;
    entry.variable.enabled = false;
    entry.variable.status = DesignVariableStatus::Inapplicable;
    entry.variable.applicability = disposition;
    addDiagnostic(result, code, entry.source.id, message);
}

bool validHint(const LegacyDesignSpaceBindingHint& hint)
{
    return !hint.legacyVariableId.empty() && hint.semanticKind != SemanticKind::Unknown &&
           hint.binding.semanticKind == hint.semanticKind &&
           ParameterBindingValidator::validate(hint.binding).valid;
}

}    // namespace

LegacyDesignSpaceMigrationPreview LegacyDesignSpaceAdapter::preview(
    const StructureOptimizationProblem& legacyProblem,
    const std::vector< LegacyDesignSpaceBindingHint >& bindingHints)
{
    LegacyDesignSpaceMigrationPreview result;
    std::map< std::string, const LegacyDesignSpaceBindingHint* > hints;
    for (const LegacyDesignSpaceBindingHint& hint : bindingHints)
        if (!hint.legacyVariableId.empty() && hints.find(hint.legacyVariableId) == hints.end())
            hints[hint.legacyVariableId] = &hint;
    const DesignSpaceRegistry registry = DesignSpaceRegistry::firstPhase();
    for (const StructureDesignVariable& source : legacyProblem.variables) {
        LegacyDesignSpaceMigrationEntry entry;
        entry.source = source;
        if (source.kind == StructureVariableKind::DhA || source.kind == StructureVariableKind::DhD) {
            disable(result, entry, "legacy/projection-only", "LEGACY_DH_PROJECTION_ONLY",
                    "DH A/D remains a read-only legacy projection and is not a canonical design variable.");
            result.entries.push_back(entry);
            continue;
        }
        SemanticKind semantic = SemanticKind::Unknown;
        ParameterBinding binding;
        const auto hint = hints.find(source.id);
        if (hint != hints.end()) {
            if (!validHint(*hint->second)) {
                disable(result, entry, "legacy/unbound", "LEGACY_BINDING_HINT_INVALID",
                        "The explicit legacy binding hint is incomplete or invalid.");
                result.entries.push_back(entry);
                continue;
            }
            semantic = hint->second->semanticKind;
            binding = hint->second->binding;
        } else if (source.kind == StructureVariableKind::BaseHeight && !source.targetName.empty()) {
            semantic = SemanticKind::BaseTz;
            binding.id = "binding:legacy:" + source.id;
            binding.semanticKind = semantic;
            binding.targetObjectType = TargetObjectType::Frame;
            binding.targetObjectId = source.targetName;
            binding.targetPropertyId = TargetPropertyId::BaseTranslationZ;
            binding.coordinateFrameId = "WORLD";
            binding.ownerAdapterId = "BasePlacementAdapter";
            binding.ownerAdapterVersion = 1;
            binding.writeSet = {{TargetObjectType::Frame, source.targetName,
                                 TargetPropertyId::BaseTranslationZ, binding.coordinateFrameId}};
        } else {
            disable(result, entry, "legacy/unbound", "LEGACY_VARIABLE_UNBOUND",
                    "Legacy variable has no reliable typed canonical binding.");
            result.entries.push_back(entry);
            continue;
        }
        const SemanticMetadata* const metadata = registry.find(semantic);
        if (metadata == nullptr) {
            disable(result, entry, "legacy/unbound", "LEGACY_SEMANTIC_UNSUPPORTED",
                    "Legacy variable maps to a semantic unavailable in the first-phase registry.");
            result.entries.push_back(entry);
            continue;
        }
        if (source.targetName.empty()) {
            disable(result, entry, "legacy/unbound", "LEGACY_VARIABLE_UNBOUND",
                    "Legacy variable has no target name for its typed canonical binding.");
            result.entries.push_back(entry);
            continue;
        }
        double scale = 1.0;
        if (!unitScale(source.unit, metadata->unit, scale)) {
            disable(result, entry, "legacy/unbound", "LEGACY_VARIABLE_UNIT_UNSUPPORTED",
                    "Legacy variable unit cannot be converted to the canonical semantic unit.");
            result.entries.push_back(entry);
            continue;
        }
        if (!std::isfinite(source.currentValue) || !std::isfinite(source.minimum) ||
            !std::isfinite(source.maximum) || !std::isfinite(source.step)) {
            disable(result, entry, "legacy/unbound", "LEGACY_VARIABLE_NONFINITE",
                    "Legacy variable numeric fields must be finite before migration preview.");
            result.entries.push_back(entry);
            continue;
        }
        entry.mapped = true;
        entry.disposition = "mapped";
        entry.binding = binding;
        entry.variable.id = source.id;
        entry.variable.displayName = source.label;
        entry.variable.semanticKind = semantic;
        entry.variable.role = VariableRole::Independent;
        entry.variable.nominalValue = source.currentValue * scale;
        entry.variable.currentValue = source.currentValue * scale;
        entry.variable.minimum = source.minimum * scale;
        entry.variable.maximum = source.maximum * scale;
        entry.variable.step = source.step * scale;
        entry.variable.domain = convertDomain(source.domainDefinition.domain);
        entry.variable.unit = metadata->unit;
        entry.variable.frameId = binding.coordinateFrameId;
        entry.variable.enabled = source.enabled;
        entry.variable.bindingId = binding.id;
        entry.variable.source = DesignVariableSource::Legacy;
        entry.variable.applicability = "legacy/mapped";
        entry.variable.status = DesignVariableStatus::Available;
        if (entry.variable.domain == VariableDomain::Discrete) {
            for (const std::string& option : source.domainDefinition.discreteOptions)
                entry.variable.discreteOptions.push_back({option, option, option});
        }
        result.mappedVariables.push_back(entry.variable);
        result.bindings.push_back(entry.binding);
        result.entries.push_back(entry);
    }
    return result;
}

}    // namespace rws
