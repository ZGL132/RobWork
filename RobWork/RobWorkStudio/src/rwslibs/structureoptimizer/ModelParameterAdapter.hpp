#ifndef RWS_STRUCTUREOPTIMIZATION_MODELPARAMETERADAPTER_HPP
#define RWS_STRUCTUREOPTIMIZATION_MODELPARAMETERADAPTER_HPP

#include "AdapterCapability.hpp"
#include "CandidatePatch.hpp"
#include "CanonicalKinematicModel.hpp"

#include <string>
#include <vector>

namespace rws {

struct ResolvedAdapterValue
{
    std::string variableId;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
    double engineeringValue = 0.0;
    std::string discreteOptionId;
    /** Concrete group semantic; adapters use it when a binding needs sibling values. */
    SemanticKind semanticKind = SemanticKind::Unknown;
    /** Stable parameter group identity; required when an adapter consumes sibling values. */
    std::string groupId;
    /** Explicit limit identity; prevents physical/operational boundary mixing in one group. */
    JointLimitScope jointLimitScope = JointLimitScope::Unknown;
};

struct AdapterBindingValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

struct AdapterPatchCompileRequest
{
    /** Borrowed immutable inputs; adapters must not retain either pointer. */
    const CanonicalKinematicModel* baseline = nullptr;
    const ParameterBinding* binding = nullptr;
    std::vector< ResolvedAdapterValue > values;
};

struct AdapterPatchCompileResult
{
    bool ok = false;
    CandidatePatch patch;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Pure adapter contract.  Implementations compile patches, never mutate a model. */
class IModelParameterAdapter
{
  public:
    virtual ~IModelParameterAdapter() {}

    virtual std::string adapterId() const = 0;
    virtual int adapterVersion() const = 0;
    virtual std::vector< SemanticKind > supportedSemanticKinds() const = 0;
    virtual std::vector< AdapterCapability > requiredCapabilities() const = 0;
    virtual AdapterBindingValidationResult validateBinding(
        const ParameterBinding& binding, const CanonicalKinematicModel& baseline) const = 0;
    virtual std::vector< ReadWriteTarget > declaredReadSet(
        const ParameterBinding& binding) const = 0;
    virtual std::vector< ReadWriteTarget > declaredWriteSet(
        const ParameterBinding& binding) const = 0;
    virtual AdapterPatchCompileResult compilePatch(
        const AdapterPatchCompileRequest& request) const = 0;
    virtual std::string describeEffect(const ParameterBinding& binding) const = 0;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_MODELPARAMETERADAPTER_HPP
