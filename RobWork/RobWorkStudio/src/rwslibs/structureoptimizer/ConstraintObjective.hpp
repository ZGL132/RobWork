#ifndef RWS_STRUCTUREOPTIMIZATION_CONSTRAINTOBJECTIVE_HPP
#define RWS_STRUCTUREOPTIMIZATION_CONSTRAINTOBJECTIVE_HPP

#include "MetricRegistry.hpp"

#include <limits>
#include <string>
#include <vector>

namespace rws {

enum class ConstraintComparison { LessThanOrEqual, GreaterThanOrEqual, Equal, InRange };

struct ConstraintSpec {
    std::string id;
    std::string metricId;
    ConstraintComparison comparison = ConstraintComparison::GreaterThanOrEqual;
    double threshold = 0.0;
    double upperThreshold = 0.0;
    double tolerance = 0.0;
    bool hard = true;
    bool enabled = true;
    bool safety = false;
    bool softAllowed = true;
    std::string requiredEvidenceStage;
    int priority = 0;
};

struct ConstraintResult {
    std::string id;
    bool enabled = false;
    bool hard = false;
    bool satisfied = false;
    bool evidenceAvailable = false;
    double normalizedViolation = std::numeric_limits<double>::quiet_NaN();
    int priority = 0;
    std::string evidenceStage;
    std::string diagnostic;
};

class ConstraintEvaluator {
  public:
    static ConstraintResult evaluate(const ConstraintSpec& spec, const MetricResult& metric);
};

enum class ObjectiveDirection { HigherIsBetter, LowerIsBetter };
enum class ObjectiveNormalization { AlreadyNormalized, FixedRange, BaselineRelative };

struct ObjectiveSpec {
    std::string id;
    std::string metricId;
    ObjectiveDirection direction = ObjectiveDirection::HigherIsBetter;
    ObjectiveNormalization normalization = ObjectiveNormalization::AlreadyNormalized;
    double good = 1.0;
    double bad = 0.0;
    double weight = 0.0;
    bool enabled = true;
};

struct ObjectiveResult {
    std::string id;
    bool usable = false;
    double normalized = 0.0;
    double contribution = 0.0;
    std::string diagnostic;
};

struct ObjectiveAggregate {
    bool feasible = true;
    double score = 0.0;
    std::vector<ConstraintResult> constraints;
    std::vector<ObjectiveResult> objectives;
};

class ObjectiveAggregator {
  public:
    static ObjectiveResult evaluate(const ObjectiveSpec& spec, const MetricResult& metric,
                                    const MetricResult* baseline = nullptr);
    static ObjectiveAggregate aggregate(const std::vector<ConstraintResult>& constraints,
                                        const std::vector<ObjectiveResult>& objectives);
};

} // namespace rws

#endif
