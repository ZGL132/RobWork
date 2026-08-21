#ifndef RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP

#include "CanonicalKinematicModel.hpp"

#include <map>

namespace rws {

/** Immutable FK output indexed by canonical frame ID. */
struct CanonicalForwardKinematicsResult
{
    bool valid = false;
    std::map< std::string, rw::math::Transform3D<> > frameTransforms;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/**
 * @brief Evaluates the canonical SE(3) chain without changing model or Q.
 *
 * The sole joint formula is delegated to KinematicConventions.  Operational
 * limits are intentionally not applied here; validation/evaluation layers own
 * limit policy and must report, rather than silently clamp, an input Q.
 */
class CanonicalForwardKinematics
{
  public:
    static CanonicalForwardKinematicsResult evaluate(const CanonicalKinematicModel& model,
                                                     const std::vector< double >& q);

    static bool frameTransform(const CanonicalForwardKinematicsResult& result,
                               const std::string& frameId,
                               rw::math::Transform3D<>& transform,
                               StructureOptimizationDiagnostic* diagnostic = nullptr);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANONICALFORWARDKINEMATICS_HPP
