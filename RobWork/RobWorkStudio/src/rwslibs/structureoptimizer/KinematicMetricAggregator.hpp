#ifndef RWS_STRUCTUREOPTIMIZATION_KINEMATICMETRICAGGREGATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_KINEMATICMETRICAGGREGATOR_HPP

#include "MetricRegistry.hpp"

#include <string>
#include <vector>

namespace rws {

struct KinematicMetricSample {
    std::string evidenceId;
    double manipulability = 0.0;
    double jointMargin = 0.0;
    bool collisionChecked = false;
    bool inCollision = false;
    bool hasMinimumDistance = false;
    double minimumDistanceMeters = 0.0;
};

struct KinematicMetricAggregate {
    MetricResult manipulabilityP10;
    MetricResult manipulabilityP90;
    MetricResult manipulabilityP10Normalized;
    MetricResult manipulabilityP90Normalized;
    MetricResult jointMarginP10;
    MetricResult collisionRate;
    MetricResult minimumDistance;
};

class KinematicMetricAggregator {
  public:
    static KinematicMetricAggregate aggregate(
        const std::vector<KinematicMetricSample>& samples,
        std::size_t requestedCount = 0,
        double characteristicLengthMeters = 1.0);
};

} // namespace rws

#endif
