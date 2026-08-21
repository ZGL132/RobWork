#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP

#include "CanonicalKinematicModel.hpp"

#include <string>
#include <vector>

namespace rws {

/** Deterministic content-fingerprint outcome for canonical model data. */
struct KinematicFingerprintResult
{
    bool ok = false;
    std::string algorithmId = "fnv1a-64";
    std::string serializationVersion = "canonical-kinematic-model-v1";
    std::string value;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Versioned canonical serialization and hash functions. */
class KinematicFingerprint
{
  public:
    // CanonicalKinematicModel deliberately has no presentation-colour field.
    // Display-only colour therefore cannot invalidate model/tool/environment data.
    static constexpr bool visualColorAffectsFingerprint() { return false; }

    static KinematicFingerprintResult forModel(const CanonicalKinematicModel& model);
    static KinematicFingerprintResult forEnvironment(const CanonicalKinematicModel& model);
    static KinematicFingerprintResult forTool(const CanonicalKinematicModel& model);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICFINGERPRINT_HPP
