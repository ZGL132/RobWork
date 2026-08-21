#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICBASELINESNAPSHOT_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICBASELINESNAPSHOT_HPP

#include "KinematicFingerprint.hpp"

namespace rws {

struct KinematicBaselineSnapshotResult;

/** Nominal canonical model and its content identity at an import boundary. */
struct KinematicBaselineSnapshot
{
    int schemaVersion = 1;
    std::string fingerprintAlgorithmId = "fnv1a-64";
    std::string serializationVersion = "canonical-kinematic-model-v1";
    std::string modelFingerprint;
    std::string environmentFingerprint;
    std::string toolFingerprint;
    CanonicalKinematicModel model;

    static KinematicBaselineSnapshotResult create(const CanonicalKinematicModel& model);
};

struct KinematicBaselineSnapshotResult
{
    bool ok = false;
    KinematicBaselineSnapshot snapshot;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICBASELINESNAPSHOT_HPP
