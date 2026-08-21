#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICIMPORTRESULT_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICIMPORTRESULT_HPP

#include "CanonicalKinematicModel.hpp"

#include <rwslibs/robotmodelbuilder/RobotModelSpec.hpp>

#include <string>
#include <vector>

namespace rws {

/** Traceability from an imported canonical item back to its RobWork source. */
struct KinematicSourceMapping
{
    std::string canonicalId;
    std::string sourceObjectId;
    std::string sourceKind;
    std::string fieldPath;
};

/** Immutable provenance captured at the WorkCell/Device/TCP import boundary. */
struct KinematicImportProvenance
{
    std::string workcellName;
    std::string deviceId;
    std::string tcpFrameId;
    std::string sourceFingerprint;
    std::string environmentFingerprint;
};

/** Result of a pure import operation; no source-object ownership is retained. */
struct KinematicImportResult
{
    bool ok = false;
    CanonicalKinematicModel model;
    KinematicImportProvenance provenance;
    bool hasSourceSnapshot = false;
    RobotModelSpec sourceSnapshot;
    std::vector< KinematicSourceMapping > sourceMappings;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_KINEMATICIMPORTRESULT_HPP
