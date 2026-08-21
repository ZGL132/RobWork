#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNVECTOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNVECTOR_HPP

#include "CompiledDesignSpace.hpp"

namespace rws {

/** A schema-positioned optimizer input.  Discrete selections use stable option IDs. */
struct NormalizedDesignValue
{
    double normalizedValue = 0.0;
    std::string discreteOptionId;
};

/** Engineering-unit input for a schema-positioned value. */
struct EngineeringDesignValue
{
    std::string variableId;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
    double engineeringValue = 0.0;
    std::string discreteOptionId;
};

/** Immutable-by-convention, independent-only candidate design state. */
struct DesignVectorValue
{
    std::string variableId;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
    double engineeringValue = 0.0;
    std::string discreteOptionId;
};

struct DesignVector
{
    int schemaVersion = 1;
    std::string designSpaceFingerprint;
    std::vector< DesignVectorValue > values;
    std::string canonicalBytes;
    std::string fingerprint;
};

struct DesignVectorResult
{
    bool ok = false;
    DesignVector vector;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

struct NormalizedDesignVectorResult
{
    bool ok = false;
    std::string designSpaceFingerprint;
    std::vector< NormalizedDesignValue > values;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Strict codec for normalized, engineering-unit, and canonical vector forms. */
class DesignVectorCodec
{
  public:
    static DesignVectorResult fromNormalized(
        const CompiledDesignSpace& designSpace,
        const std::vector< NormalizedDesignValue >& normalizedValues);
    static DesignVectorResult fromEngineering(
        const CompiledDesignSpace& designSpace,
        const std::vector< EngineeringDesignValue >& engineeringValues);
    static NormalizedDesignVectorResult toNormalized(
        const CompiledDesignSpace& designSpace, const DesignVector& vector);
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNVECTOR_HPP
