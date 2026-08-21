#ifndef RWS_STRUCTUREOPTIMIZATION_PARAMETERIZEDGEOMETRYADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_PARAMETERIZEDGEOMETRYADAPTER_HPP

#include "ModelParameterAdapter.hpp"

namespace rws {

/** Rebuilds explicit optimization-owned visual primitives; it never edits user geometry. */
class ParameterizedGeometryAdapter : public IModelParameterAdapter
{
  public:
    std::string adapterId() const override;
    int adapterVersion() const override;
    std::vector< SemanticKind > supportedSemanticKinds() const override;
    std::vector< AdapterCapability > requiredCapabilities() const override;
    AdapterBindingValidationResult validateBinding(
        const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const override;
    std::vector< ReadWriteTarget > declaredReadSet(const ParameterBinding& binding) const override;
    std::vector< ReadWriteTarget > declaredWriteSet(const ParameterBinding& binding) const override;
    AdapterPatchCompileResult compilePatch(const AdapterPatchCompileRequest& request) const override;
    std::string describeEffect(const ParameterBinding& binding) const override;

    /** Aligns the primitive local +Z length axis to an actual frame-to-frame delta. */
    static rw::math::Transform3D<> segmentTransform(const rw::math::Vector3D<>& start,
                                                     const rw::math::Vector3D<>& end);
};

}    // namespace rws

#endif
