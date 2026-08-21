#include "CandidatePatchMerge.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace rws {
namespace {

struct TargetLess
{
    bool operator()(const ReadWriteTarget& first, const ReadWriteTarget& second) const
    {
        if (first.objectType != second.objectType)
            return first.objectType < second.objectType;
        if (first.objectId != second.objectId)
            return first.objectId < second.objectId;
        if (first.propertyId != second.propertyId)
            return first.propertyId < second.propertyId;
        return first.coordinateFrameId < second.coordinateFrameId;
    }
};

bool sameValue(const CandidatePatchValue& first, const CandidatePatchValue& second)
{
    if (first.kind != second.kind)
        return false;
    if (first.kind == CandidatePatchValue::Kind::Scalar)
        return std::isfinite(first.scalarValue) && std::isfinite(second.scalarValue) &&
               first.scalarValue == second.scalarValue;
    return first.textValue == second.textValue;
}

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const CandidatePatch& patch, const ReadWriteTarget& target,
              const char* code, const char* message)
{
    diagnostics.push_back(makeAdapterDiagnostic(
        patch.adapterId, patch.bindingId, target.objectId, "writes",
        code, message));
}

void appendUnique(std::vector< std::string >& values, const std::string& value)
{
    if (std::find(values.begin(), values.end(), value) == values.end())
        values.push_back(value);
}

}    // namespace

CandidatePatchMergeResult CandidatePatchMerger::merge(
    const std::vector< CandidatePatch >& patches)
{
    CandidatePatchMergeResult result;
    result.patch.adapterId = "CandidatePatchMerger";
    result.patch.adapterVersion = 1;
    result.patch.bindingId = "merged";

    std::map< ReadWriteTarget, CandidatePatchWrite, TargetLess > writes;
    std::map< ReadWriteTarget, std::string, TargetLess > poseGroupsByTarget;
    bool structuralCapability = false;
    PoseDeltaComposition composition = PoseDeltaComposition::Unknown;
    std::set< std::string > poseGroups;

    for (const CandidatePatch& patch : patches) {
        structuralCapability = structuralCapability || patch.affectsStructuralCapability;
        if (patch.poseDeltaComposition != PoseDeltaComposition::Unknown) {
            if (composition == PoseDeltaComposition::Unknown)
                composition = patch.poseDeltaComposition;
            else if (composition != patch.poseDeltaComposition) {
                result.diagnostics.push_back(makeAdapterDiagnostic(
                    patch.adapterId, patch.bindingId, "", "poseDeltaComposition",
                    "CANDIDATE_PATCH_POSE_COMPOSITION_CONFLICT",
                    "Merged patches must use one pose-delta composition convention."));
            }
        }
        if (!patch.poseDeltaGroupId.empty()) {
            poseGroups.insert(patch.poseDeltaGroupId);
        }
        result.diagnostics.insert(result.diagnostics.end(), patch.diagnostics.begin(),
                                  patch.diagnostics.end());
        for (const std::string& artifact : patch.generatedArtifacts)
            appendUnique(result.patch.generatedArtifacts, artifact);
        for (const std::string& derived : patch.derivedValueIds)
            appendUnique(result.patch.derivedValueIds, derived);

        for (const CandidatePatchWrite& write : patch.writes) {
            if (!patch.poseDeltaGroupId.empty()) {
                const auto group = poseGroupsByTarget.find(write.target);
                if (group == poseGroupsByTarget.end())
                    poseGroupsByTarget.emplace(write.target, patch.poseDeltaGroupId);
                else if (group->second != patch.poseDeltaGroupId) {
                    result.diagnostics.push_back(makeAdapterDiagnostic(
                        patch.adapterId, patch.bindingId, write.target.objectId, "poseDeltaGroupId",
                        "CANDIDATE_PATCH_POSE_GROUP_CONFLICT",
                        "One typed pose target cannot be owned by multiple pose groups."));
                }
            }
            const auto found = writes.find(write.target);
            if (found == writes.end()) {
                writes.emplace(write.target, write);
                continue;
            }
            if (!sameValue(found->second.value, write.value)) {
                addError(result.diagnostics, patch, write.target,
                         "CANDIDATE_PATCH_WRITE_CONFLICT",
                         "Two patches assign different values to the same typed target.");
            }
        }
    }

    result.patch.affectsStructuralCapability = structuralCapability;
    result.patch.poseDeltaComposition = composition;
    if (poseGroups.size() == 1)
        result.patch.poseDeltaGroupId = *poseGroups.begin();
    std::sort(result.patch.generatedArtifacts.begin(),
              result.patch.generatedArtifacts.end());
    result.patch.generatedArtifacts.erase(
        std::unique(result.patch.generatedArtifacts.begin(),
                    result.patch.generatedArtifacts.end()),
        result.patch.generatedArtifacts.end());
    std::sort(result.patch.derivedValueIds.begin(), result.patch.derivedValueIds.end());
    result.patch.derivedValueIds.erase(
        std::unique(result.patch.derivedValueIds.begin(),
                    result.patch.derivedValueIds.end()),
        result.patch.derivedValueIds.end());
    for (const auto& entry : writes)
        result.patch.writes.push_back(entry.second);

    result.ok = result.diagnostics.empty();
    result.patch.diagnostics = result.diagnostics;
    return result;
}

}    // namespace rws
