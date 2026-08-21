#ifndef RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP
#define RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP

#include "StructureOptimizationContracts.hpp"

#include <string>
#include <vector>

namespace rws {

enum class VariableRole { Independent, Derived };
enum class VariableDomain { Continuous, Integer, Discrete };
enum class DesignVariableUnit { Unitless, Metres, Radians, Degrees, Kilograms, NewtonMetres };
enum class DesignVariableSource { User, Template, Imported, Legacy };
enum class DesignVariableStatus { Available, DisabledByParameterization, Inapplicable, Invalid };

/** Stable semantic identity used by the design-space compiler and adapters. */
enum class SemanticKind
{
    Unknown,
    LinkLength,
    JointOriginOffsetX, JointOriginOffsetY, JointOriginOffsetZ,
    JointOffsetAlongAxis,
    JointAxisTiltU, JointAxisTiltV,
    JointZeroOffset, JointLimitLower, JointLimitUpper,
    BaseTx, BaseTy, BaseTz,
    BaseRotationVectorX, BaseRotationVectorY, BaseRotationVectorZ,
    TcpTx, TcpTy, TcpTz,
    TcpRotationVectorX, TcpRotationVectorY, TcpRotationVectorZ,
    FlangeTx, FlangeTy, FlangeTz,
    FlangeRotationVectorX, FlangeRotationVectorY, FlangeRotationVectorZ,
    LinkRadius, LinkWidth, LinkHeight, LinkCrossSectionX, LinkCrossSectionY,
    LinkWallThickness, LinkScale, GeometryRadius, GeometryLength, GeometryWidth, GeometryHeight,
    GeometryDepth, GeometryWallThickness, GeometryRigidTransform,
    ParameterizedMaterial
};

struct DiscreteOption
{
    std::string id;
    std::string displayName;
    std::string payloadReference;
};

/** Core-only definition; legacy Qt table rows remain separate during Phase 2. */
struct DesignVariableDefinition
{
    std::string id;
    std::string displayName;
    SemanticKind semanticKind = SemanticKind::Unknown;
    VariableRole role = VariableRole::Independent;
    std::string groupId;
    std::string parameterizationModeId;
    double nominalValue = 0.0;
    double currentValue = 0.0;
    VariableDomain domain = VariableDomain::Continuous;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
    std::vector< DiscreteOption > discreteOptions;
    DesignVariableUnit unit = DesignVariableUnit::Unitless;
    std::string frameId;
    bool enabled = true;
    std::vector< std::string > dependencies;
    std::string derivedExpressionId;
    std::string bindingId;
    DesignVariableSource source = DesignVariableSource::User;
    std::string applicability;
    std::string description;
    DesignVariableStatus status = DesignVariableStatus::Available;
};

struct DesignVariableValidationResult
{
    bool valid = true;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

class DesignVariableValidator
{
  public:
    static DesignVariableValidationResult validate(
        const std::vector< DesignVariableDefinition >& variables);
};

std::string variableRoleToString(VariableRole role);
bool variableRoleFromString(const std::string& value, VariableRole& role);
std::string variableDomainToString(VariableDomain domain);
bool variableDomainFromString(const std::string& value, VariableDomain& domain);
std::string semanticKindToString(SemanticKind kind);
bool semanticKindFromString(const std::string& value, SemanticKind& kind);
std::string designVariableUnitToString(DesignVariableUnit unit);
bool designVariableUnitFromString(const std::string& value, DesignVariableUnit& unit);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_DESIGNVARIABLE_HPP
