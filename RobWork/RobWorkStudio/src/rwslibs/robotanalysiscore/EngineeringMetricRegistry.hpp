#ifndef RWS_ROBOTANALYSISCORE_ENGINEERINGMETRICREGISTRY_HPP
#define RWS_ROBOTANALYSISCORE_ENGINEERINGMETRICREGISTRY_HPP

#include "EngineeringEvaluationTypes.hpp"

#include <map>

namespace rws {

struct EngineeringMetricDefinition
{
    std::string metricId;
    std::string displayName;
    std::string unit;
    std::string group;
};

class EngineeringMetricRegistry
{
public:
    static const EngineeringMetricRegistry& standard();

    const EngineeringMetricDefinition* find(const std::string& metricId) const;
    bool validate(const EngineeringEvaluationResult& result,
                  std::vector<AnalysisWarning>* warnings = nullptr) const;

private:
    EngineeringMetricRegistry();
    std::map<std::string, EngineeringMetricDefinition> _definitions;
};

} // namespace rws

#endif // RWS_ROBOTANALYSISCORE_ENGINEERINGMETRICREGISTRY_HPP
