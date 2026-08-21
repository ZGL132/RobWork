#ifndef RWS_STRUCTUREOPTIMIZATION_METRICREGISTRY_HPP
#define RWS_STRUCTUREOPTIMIZATION_METRICREGISTRY_HPP

#include <map>
#include <string>
#include <vector>

namespace rws {

enum class MetricAvailability { Available, Unavailable, InsufficientData, Partial };

struct MetricDefinition {
    std::string id;
    std::string unit;
    std::string producer;
    bool higherIsBetter = true;
    bool allowedAsObjective = false;
    bool allowedAsHardConstraint = false;
    bool allowedAsSoftConstraint = false;
};

struct MetricResult {
    std::string metricId;
    double value = 0.0;
    std::string unit;
    MetricAvailability availability = MetricAvailability::Unavailable;
    std::size_t sampleCount = 0;
    std::string evidenceStage;
    std::vector<std::string> evidenceIds;
    std::string diagnostic;

    bool usable() const { return availability == MetricAvailability::Available; }
};

class MetricRegistry {
  public:
    bool registerMetric(const MetricDefinition& definition, std::string* error = nullptr);
    const MetricDefinition* find(const std::string& id) const;
    bool contains(const std::string& id) const { return find(id) != nullptr; }
    std::vector<MetricDefinition> definitions() const;
    static MetricRegistry standard();

  private:
    std::map<std::string, MetricDefinition> _definitions;
};

const char* toString(MetricAvailability availability);

} // namespace rws

#endif
