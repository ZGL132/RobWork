#ifndef RWS_STRUCTUREOPTIMIZATION_ADAPTERCAPABILITY_HPP
#define RWS_STRUCTUREOPTIMIZATION_ADAPTERCAPABILITY_HPP

#include "ParameterBinding.hpp"

#include <map>
#include <set>
#include <string>

namespace rws {

/** Declared adapter support, separate from a model object's nominal numeric value. */
enum class AdapterCapability
{
    JointOrigin,
    ParameterizedLink,
    JointAxisTilt,
    JointZeroOffset,
    JointLimits,
    BasePlacement,
    TcpPose,
    FlangePose,
    ParameterizedGeometry,
    ParameterizedCollision
};

/** Pure query table populated by adapter declarations; it owns no adapter pointer. */
class AdapterCapabilityQuery
{
  public:
    void grant(TargetObjectType objectType, const std::string& objectId,
               AdapterCapability capability);
    bool supports(TargetObjectType objectType, const std::string& objectId,
                  AdapterCapability capability) const;
    /** Versioned, presentation-independent material for a design-space fingerprint. */
    std::string fingerprintMaterial() const;

  private:
    typedef std::pair< TargetObjectType, std::string > ObjectKey;
    std::map< ObjectKey, std::set< AdapterCapability > > _capabilities;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_ADAPTERCAPABILITY_HPP
