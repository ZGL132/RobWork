#include "CanonicalModelShadowService.hpp"

#include "KinematicBaselineSnapshot.hpp"

namespace rws {
namespace {

bool matchesSnapshot(const KinematicBaselineSnapshot& saved,
                     const KinematicBaselineSnapshot& recomputed)
{
    return saved.schemaVersion == recomputed.schemaVersion &&
           saved.fingerprintAlgorithmId == recomputed.fingerprintAlgorithmId &&
           saved.serializationVersion == recomputed.serializationVersion &&
           saved.modelFingerprint == recomputed.modelFingerprint &&
           saved.environmentFingerprint == recomputed.environmentFingerprint &&
           saved.toolFingerprint == recomputed.toolFingerprint;
}

std::string firstMessage(const std::vector< StructureOptimizationDiagnostic >& diagnostics,
                         const std::string& fallback)
{
    return diagnostics.empty() ? fallback : diagnostics.front().code + ": " +
                                                diagnostics.front().message;
}

}    // namespace

bool CanonicalModelShadowService::attach(const KinematicImportRequest& request,
                                         StructureOptimizationProblem& problem,
                                         std::string* error)
{
    const KinematicImportResult imported = KinematicModelImporter::import(request);
    if (!imported.ok) {
        if (error != nullptr)
            *error = firstMessage(imported.diagnostics, "Canonical kinematic import failed.");
        return false;
    }
    const KinematicBaselineSnapshotResult baseline =
        KinematicBaselineSnapshot::create(imported.model);
    if (!baseline.ok) {
        if (error != nullptr)
            *error = firstMessage(baseline.diagnostics,
                                  "Canonical kinematic snapshot creation failed.");
        return false;
    }
    StructureOptimizationProblem updated = problem;
    updated.canonicalModelShadow.status = CanonicalModelShadowStatus::Current;
    updated.canonicalModelShadow.snapshot =
        std::make_shared< KinematicBaselineSnapshot >(baseline.snapshot);
    problem = std::move(updated);
    if (error != nullptr) error->clear();
    return true;
}

CanonicalModelShadowStatus CanonicalModelShadowService::assess(
    const CanonicalModelShadow& shadow, const CanonicalKinematicModel& currentModel)
{
    if (!shadow.hasSnapshot())
        return CanonicalModelShadowStatus::CanonicalModelMissing;

    const KinematicBaselineSnapshotResult saved =
        KinematicBaselineSnapshot::create(shadow.snapshot->model);
    if (!saved.ok || !matchesSnapshot(*shadow.snapshot, saved.snapshot))
        return CanonicalModelShadowStatus::Invalid;

    const KinematicBaselineSnapshotResult current =
        KinematicBaselineSnapshot::create(currentModel);
    if (!current.ok)
        return CanonicalModelShadowStatus::Invalid;

    return matchesSnapshot(saved.snapshot, current.snapshot) ?
        CanonicalModelShadowStatus::Current : CanonicalModelShadowStatus::Stale;
}

}    // namespace rws
