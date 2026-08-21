#include "KinematicMetricAggregator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rws {
namespace {

MetricResult unavailable(const std::string& id, const std::string& unit,
                         MetricAvailability availability, const std::string& diagnostic)
{
    MetricResult result;
    result.metricId = id;
    result.unit = unit;
    result.availability = availability;
    result.diagnostic = diagnostic;
    return result;
}

double percentile(std::vector<double> values, double ratio)
{
    std::sort(values.begin(), values.end());
    if (values.empty())
        return 0.0;
    const std::size_t index = static_cast<std::size_t>(std::floor(
        ratio * static_cast<double>(values.size() - 1)));
    return values[index];
}

MetricResult aggregatePercentile(const std::string& id, const std::string& unit,
                                 const std::vector<double>& values, double ratio,
                                 std::size_t requestedCount,
                                 const std::vector<std::string>& ids)
{
    if (values.empty())
        return unavailable(id, unit, MetricAvailability::InsufficientData,
                           "No finite evidence was available.");
    MetricResult result;
    result.metricId = id;
    result.unit = unit;
    result.value = percentile(values, ratio);
    result.sampleCount = values.size();
    result.evidenceStage = "Verified";
    result.evidenceIds = ids;
    result.availability = requestedCount > values.size() ? MetricAvailability::Partial
                                                         : MetricAvailability::Available;
    if (result.availability == MetricAvailability::Partial)
        result.diagnostic = "Only a partial sample set was available.";
    return result;
}

void collect(const std::vector<KinematicMetricSample>& samples,
             double characteristicLengthMeters,
             std::vector<double>& raw, std::vector<double>& normalized,
             std::vector<double>& margins, std::vector<std::string>& ids)
{
    const double scale = characteristicLengthMeters > 0.0 &&
                                 std::isfinite(characteristicLengthMeters)
                             ? characteristicLengthMeters * characteristicLengthMeters *
                                   characteristicLengthMeters
                             : std::numeric_limits<double>::quiet_NaN();
    for (const KinematicMetricSample& sample : samples) {
        if (!std::isfinite(sample.manipulability) || !std::isfinite(sample.jointMargin))
            continue;
        raw.push_back(sample.manipulability);
        if (std::isfinite(scale) && scale > 0.0)
            normalized.push_back(sample.manipulability / scale);
        margins.push_back(sample.jointMargin);
        ids.push_back(sample.evidenceId);
    }
}

} // namespace

KinematicMetricAggregate KinematicMetricAggregator::aggregate(
    const std::vector<KinematicMetricSample>& samples,
    std::size_t requestedCount,
    double characteristicLengthMeters)
{
    KinematicMetricAggregate result;
    std::vector<double> raw, normalized, margins;
    std::vector<std::string> ids;
    collect(samples, characteristicLengthMeters, raw, normalized, margins, ids);
    if (requestedCount == 0)
        requestedCount = samples.size();

    result.manipulabilityP10 = aggregatePercentile(
        "kinematics.manipulability.p10", "raw", raw, 0.10, requestedCount, ids);
    result.manipulabilityP90 = aggregatePercentile(
        "kinematics.manipulability.p90", "raw", raw, 0.90, requestedCount, ids);
    result.manipulabilityP10Normalized = aggregatePercentile(
        "kinematics.manipulability_normalized.p10", "normalized", normalized, 0.10,
        requestedCount, ids);
    result.manipulabilityP90Normalized = aggregatePercentile(
        "kinematics.manipulability_normalized.p90", "normalized", normalized, 0.90,
        requestedCount, ids);
    result.jointMarginP10 = aggregatePercentile(
        "kinematics.joint_margin.p10", "ratio", margins, 0.10, requestedCount, ids);

    std::size_t collisionSamples = 0;
    std::size_t collisions = 0;
    std::vector<std::string> collisionIds;
    std::vector<double> distances;
    std::vector<std::string> distanceIds;
    for (const KinematicMetricSample& sample : samples) {
        if (sample.collisionChecked) {
            ++collisionSamples;
            if (sample.inCollision)
                ++collisions;
            collisionIds.push_back(sample.evidenceId);
        }
        if (sample.hasMinimumDistance && std::isfinite(sample.minimumDistanceMeters)) {
            distances.push_back(sample.minimumDistanceMeters);
            distanceIds.push_back(sample.evidenceId);
        }
    }
    if (collisionSamples == 0) {
        result.collisionRate = unavailable("kinematics.collision.rate", "ratio",
                                           MetricAvailability::InsufficientData,
                                           "Collision was not checked for any sample.");
    }
    else {
        result.collisionRate.metricId = "kinematics.collision.rate";
        result.collisionRate.unit = "ratio";
        result.collisionRate.value = static_cast<double>(collisions) /
                                     static_cast<double>(collisionSamples);
        result.collisionRate.sampleCount = collisionSamples;
        result.collisionRate.evidenceIds = collisionIds;
        result.collisionRate.evidenceStage = "Verified";
        result.collisionRate.availability = requestedCount > collisionSamples
                                                 ? MetricAvailability::Partial
                                                 : MetricAvailability::Available;
    }
    if (distances.empty())
        result.minimumDistance = unavailable("kinematics.collision.minimum_distance", "m",
                                             MetricAvailability::InsufficientData,
                                             "Minimum distance evidence is unavailable.");
    else {
        result.minimumDistance.metricId = "kinematics.collision.minimum_distance";
        result.minimumDistance.unit = "m";
        result.minimumDistance.value = *std::min_element(distances.begin(), distances.end());
        result.minimumDistance.sampleCount = distances.size();
        result.minimumDistance.evidenceIds = distanceIds;
        result.minimumDistance.evidenceStage = "Verified";
        result.minimumDistance.availability = requestedCount > distances.size()
                                                   ? MetricAvailability::Partial
                                                   : MetricAvailability::Available;
    }
    return result;
}

} // namespace rws
