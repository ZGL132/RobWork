#ifndef RWS_STRUCTUREOPTIMIZATION_PARAMETERIZEDLINKADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_PARAMETERIZEDLINKADAPTER_HPP

#include "ModelParameterAdapter.hpp"

namespace rws {

/** Baseline-relative explicit-direction LinkLength adapter. */
class ParameterizedLinkAdapter : public IModelParameterAdapter
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
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_PARAMETERIZEDLINKADAPTER_HPP
