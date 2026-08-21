#ifndef RWS_STRUCTUREOPTIMIZATION_DHPROJECTION_HPP
#define RWS_STRUCTUREOPTIMIZATION_DHPROJECTION_HPP

#include "CanonicalKinematicModel.hpp"

#include <string>
#include <vector>

namespace rws {

/** Fidelity of a read-only conventional-DH compatibility view. */
enum class DhProjectionStatus
{
    Exact,
    Lossy,
    Unsupported
};

/** One conventional-DH row derived from an active canonical joint. */
struct DhProjectionRow
{
    std::string jointId;
    CanonicalJointType jointType = CanonicalJointType::Fixed;
    double thetaOffset = 0.0;
    double d = 0.0;
    double a = 0.0;
    double alpha = 0.0;
};

/** Read-only projection result; canonical SE(3) remains the source of truth. */
struct DhProjectionResult
{
    DhProjectionStatus status = DhProjectionStatus::Exact;
    std::vector< DhProjectionRow > rows;
    std::vector< std::string > lostComponents;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/**
 * Produces a restricted standard-DH view of canonical kinematics. No reverse
 * conversion or mutating API is exposed: DH is intentionally a lossy view.
 */
class DhProjection
{
  public:
    static DhProjectionResult project(const CanonicalKinematicModel& model);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DHPROJECTION_HPP
