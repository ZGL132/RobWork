#include "WriteSetValidator.hpp"

#include <map>

namespace rws {
namespace {

struct WriteTargetKey
{
    TargetObjectType objectType = TargetObjectType::Unknown;
    std::string objectId;
    TargetPropertyId propertyId = TargetPropertyId::Unknown;

    bool operator<(const WriteTargetKey& other) const
    {
        return objectType != other.objectType ? objectType < other.objectType :
            objectId != other.objectId ? objectId < other.objectId : propertyId < other.propertyId;
    }
};

void addError(WriteSetValidationResult& result, const std::string& code,
              const std::string& fieldPath, const std::string& message)
{
    result.valid = false;
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-space";
    diagnostic.stage = "write-set-validation";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

}    // namespace

WriteSetValidationResult WriteSetValidator::validate(
    const std::vector< DesignVariableDefinition >& variables,
    const std::vector< ParameterBinding >& bindings)
{
    WriteSetValidationResult result;
    std::map< std::string, const ParameterBinding* > bindingById;
    for (const ParameterBinding& binding : bindings)
        bindingById[binding.id] = &binding;
    std::map< WriteTargetKey, const ParameterBinding* > writerByTarget;
    for (const DesignVariableDefinition& variable : variables) {
        if (!variable.enabled || variable.status == DesignVariableStatus::DisabledByParameterization)
            continue;
        const auto binding = bindingById.find(variable.bindingId);
        if (binding == bindingById.end()) {
            addError(result, "PARAMETER_BINDING_MISSING", variable.id,
                     "An active variable must have a declared parameter binding.");
            continue;
        }
        if (variable.role == VariableRole::Derived && binding->second->ownerAdapterId.empty()) {
            addError(result, "PARAMETER_DERIVED_OWNER_MISSING", variable.id,
                     "A derived variable may write only through its declared owner adapter.");
            continue;
        }
        for (const ReadWriteTarget& target : binding->second->writeSet) {
            const WriteTargetKey key = {target.objectType, target.objectId, target.propertyId};
            const auto writer = writerByTarget.find(key);
            if (writer != writerByTarget.end()) {
                addError(result, "PARAMETER_WRITE_CONFLICT", variable.id,
                         "Two active parameter bindings claim the same physical write target.");
                continue;
            }
            writerByTarget[key] = binding->second;
        }
    }
    return result;
}

}    // namespace rws
