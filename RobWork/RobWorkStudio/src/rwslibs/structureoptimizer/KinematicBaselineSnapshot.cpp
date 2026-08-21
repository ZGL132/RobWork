#include "KinematicBaselineSnapshot.hpp"

namespace rws {

KinematicBaselineSnapshotResult KinematicBaselineSnapshot::create(
    const CanonicalKinematicModel& model)
{
    KinematicBaselineSnapshotResult result;
    const KinematicFingerprintResult fingerprint = KinematicFingerprint::forModel(model);
    if (!fingerprint.ok) {
        result.diagnostics = fingerprint.diagnostics;
        return result;
    }
    const KinematicFingerprintResult environment = KinematicFingerprint::forEnvironment(model);
    if (!environment.ok) {
        result.diagnostics = environment.diagnostics;
        return result;
    }
    const KinematicFingerprintResult tool = KinematicFingerprint::forTool(model);
    if (!tool.ok) {
        result.diagnostics = tool.diagnostics;
        return result;
    }
    result.snapshot.fingerprintAlgorithmId = fingerprint.algorithmId;
    result.snapshot.serializationVersion = fingerprint.serializationVersion;
    result.snapshot.modelFingerprint = fingerprint.value;
    result.snapshot.environmentFingerprint = environment.value;
    result.snapshot.toolFingerprint = tool.value;
    result.snapshot.model = model;
    result.ok = true;
    return result;
}

}    // namespace rws
