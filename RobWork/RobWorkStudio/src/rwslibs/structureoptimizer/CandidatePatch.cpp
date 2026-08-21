#include "CandidatePatch.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {

bool validTarget(const ReadWriteTarget& target)
{
    return target.objectType != TargetObjectType::Unknown && !target.objectId.empty() &&
           target.propertyId != TargetPropertyId::Unknown;
}

bool containsTarget(const std::vector< ReadWriteTarget >& targets,
                    const ReadWriteTarget& candidate)
{
    return std::find(targets.begin(), targets.end(), candidate) != targets.end();
}

void addPatchError(CandidatePatchValidationResult& result, const CandidatePatch& patch,
                   const std::string& objectId, const std::string& fieldPath,
                   const std::string& code, const std::string& message)
{
    result.valid = false;
    result.diagnostics.push_back(makeAdapterDiagnostic(
        patch.adapterId, patch.bindingId, objectId, fieldPath, code, message));
}

}    // namespace

StructureOptimizationDiagnostic makeAdapterDiagnostic(const std::string& adapterId,
                                                       const std::string& bindingId,
                                                       const std::string& objectId,
                                                       const std::string& fieldPath,
                                                       const std::string& code,
                                                       const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "candidate-compiler";
    diagnostic.stage = adapterId.empty() ? "adapter" : "adapter:" + adapterId;
    diagnostic.objectId = objectId;
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    if (!bindingId.empty())
        diagnostic.evidenceIds.push_back(bindingId);
    return diagnostic;
}

CandidatePatchValue CandidatePatchValue::scalar(double value)
{
    CandidatePatchValue result;
    result.kind = Kind::Scalar;
    result.scalarValue = value;
    return result;
}

CandidatePatchValue CandidatePatchValue::discreteOption(const std::string& optionId)
{
    CandidatePatchValue result;
    result.kind = Kind::DiscreteOption;
    result.textValue = optionId;
    return result;
}

CandidatePatchValue CandidatePatchValue::artifactReference(const std::string& reference)
{
    CandidatePatchValue result;
    result.kind = Kind::ArtifactReference;
    result.textValue = reference;
    return result;
}

CandidatePatchValidationResult CandidatePatchValidator::validate(
    const CandidatePatch& patch, const std::vector< ReadWriteTarget >& declaredWriteSet)
{
    CandidatePatchValidationResult result;
    if (patch.adapterId.empty())
        addPatchError(result, patch, "", "adapterId", "CANDIDATE_PATCH_ADAPTER_ID_REQUIRED",
                      "A candidate patch must identify its producing adapter.");
    if (patch.adapterVersion <= 0)
        addPatchError(result, patch, "", "adapterVersion",
                      "CANDIDATE_PATCH_ADAPTER_VERSION_INVALID",
                      "A candidate patch must use a positive adapter version.");
    if (patch.bindingId.empty())
        addPatchError(result, patch, "", "bindingId", "CANDIDATE_PATCH_BINDING_REQUIRED",
                      "A candidate patch must identify its binding.");
    if (declaredWriteSet.empty())
        addPatchError(result, patch, "", "declaredWriteSet",
                      "CANDIDATE_PATCH_DECLARED_WRITE_SET_REQUIRED",
                      "Patch validation requires an adapter-declared write set.");

    for (std::size_t index = 0; index < patch.writes.size(); ++index) {
        const CandidatePatchWrite& write = patch.writes[index];
        const std::string path = "writes[" + std::to_string(index) + "].target";
        if (!validTarget(write.target)) {
            addPatchError(result, patch, write.target.objectId, path,
                          "CANDIDATE_PATCH_WRITE_TARGET_INVALID",
                          "Each patch write must identify a typed object and property target.");
            continue;
        }
        if (!containsTarget(declaredWriteSet, write.target))
            addPatchError(result, patch, write.target.objectId, path,
                          "CANDIDATE_PATCH_WRITE_UNDECLARED",
                          "A patch may write only targets declared by its adapter binding.");
        if (write.value.kind == CandidatePatchValue::Kind::Scalar &&
            !std::isfinite(write.value.scalarValue))
            addPatchError(result, patch, write.target.objectId, "writes[" +
                          std::to_string(index) + "].value", "CANDIDATE_PATCH_VALUE_NONFINITE",
                          "Scalar patch values must be finite.");
        if (write.value.kind != CandidatePatchValue::Kind::Scalar && write.value.textValue.empty())
            addPatchError(result, patch, write.target.objectId, "writes[" +
                          std::to_string(index) + "].value", "CANDIDATE_PATCH_VALUE_REQUIRED",
                          "Textual patch values must not be empty.");
    }
    return result;
}

}    // namespace rws
