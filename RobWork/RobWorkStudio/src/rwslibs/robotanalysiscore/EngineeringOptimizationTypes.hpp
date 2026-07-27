#ifndef RWS_ROBOTANALYSISCORE_ENGINEERINGOPTIMIZATIONTYPES_HPP
#define RWS_ROBOTANALYSISCORE_ENGINEERINGOPTIMIZATIONTYPES_HPP

#include <string>
#include <vector>

namespace rws {

enum class OptimizationDirection { Maximize, Minimize };
enum class ComparisonOperator { LessThanOrEqual, GreaterThanOrEqual, Equal };
enum class DesignVariableDomain { Continuous, Integer, Discrete };

struct NormalizationRule
{
    double good = 1.0;
    double bad = 0.0;
    bool clamp = true;
};

struct ObjectiveTerm
{
    std::string metricId;
    OptimizationDirection direction = OptimizationDirection::Maximize;
    NormalizationRule normalization;
    double weight = 0.0;
    bool enabled = true;
};

struct ConstraintRule
{
    std::string metricId;
    ComparisonOperator comparison = ComparisonOperator::GreaterThanOrEqual;
    double threshold = 0.0;
    bool hard = true;
    bool enabled = true;
};

struct EngineeringVariableDomainDefinition
{
    DesignVariableDomain domain = DesignVariableDomain::Continuous;
    std::vector<std::string> discreteOptions;
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_ENGINEERINGOPTIMIZATIONTYPES_HPP
