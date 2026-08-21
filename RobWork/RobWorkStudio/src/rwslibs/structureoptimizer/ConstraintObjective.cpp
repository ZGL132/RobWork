#include "ConstraintObjective.hpp"

#include <algorithm>
#include <cmath>

namespace rws {
namespace {
bool finite(double value) { return std::isfinite(value); }
int evidenceRank(const std::string& stage)
{
    if (stage == "Verified") return 2;
    if (stage == "Quick") return 1;
    if (stage == "Estimated") return 0;
    return -1;
}

bool evidenceSatisfies(const std::string& actual, const std::string& required)
{
    return required.empty() || (evidenceRank(actual) >= 0 &&
                                evidenceRank(actual) >= evidenceRank(required));
}

double scale(double value, double good, double bad)
{
    if (!finite(good) || !finite(bad) || good == bad) return std::numeric_limits<double>::quiet_NaN();
    return std::clamp((value - bad) / (good - bad), 0.0, 1.0);
}
}

ConstraintResult ConstraintEvaluator::evaluate(const ConstraintSpec& spec, const MetricResult& metric)
{
    ConstraintResult result;
    result.id = spec.id;
    result.enabled = spec.enabled;
    result.hard = spec.hard;
    result.priority = spec.priority;
    if (!spec.enabled) { result.satisfied = true; return result; }
    if (spec.safety && !spec.softAllowed && !spec.hard) {
        result.diagnostic = "Safety constraints cannot be soft.";
        return result;
    }
    result.evidenceStage = metric.evidenceStage;
    if (!metric.usable() || !finite(metric.value)) {
        result.diagnostic = "Metric evidence is unavailable.";
        return result;
    }
    if (!evidenceSatisfies(metric.evidenceStage, spec.requiredEvidenceStage)) {
        result.diagnostic = "Metric evidence stage is weaker than required.";
        return result;
    }
    result.evidenceAvailable = true;
    const double value = metric.value;
    double violation = 0.0;
    switch (spec.comparison) {
    case ConstraintComparison::LessThanOrEqual:
        violation = std::max(0.0, value - spec.threshold - spec.tolerance);
        break;
    case ConstraintComparison::GreaterThanOrEqual:
        violation = std::max(0.0, spec.threshold - spec.tolerance - value);
        break;
    case ConstraintComparison::Equal:
        violation = std::max(0.0, std::abs(value - spec.threshold) - spec.tolerance);
        break;
    case ConstraintComparison::InRange:
        violation = std::max({0.0, spec.threshold - value, value - spec.upperThreshold}) - spec.tolerance;
        violation = std::max(0.0, violation);
        break;
    }
    double normalization = 1.0;
    if (spec.comparison == ConstraintComparison::InRange)
        normalization = std::max(std::abs(spec.threshold), std::abs(spec.upperThreshold));
    else
        normalization = std::abs(spec.threshold);
    if (!finite(normalization) || normalization <= 0.0)
        normalization = 1.0;
    result.normalizedViolation = violation / normalization;
    result.satisfied = violation == 0.0;
    return result;
}

ObjectiveResult ObjectiveAggregator::evaluate(const ObjectiveSpec& spec, const MetricResult& metric,
                                              const MetricResult* baseline)
{
    ObjectiveResult result;
    result.id = spec.id;
    if (!spec.enabled) { result.usable = true; return result; }
    if (!finite(spec.weight) || spec.weight < 0.0) {
        result.diagnostic = "Objective weight must be finite and non-negative.";
        return result;
    }
    if (!metric.usable() || !finite(metric.value)) {
        result.diagnostic = "Metric evidence is unavailable.";
        return result;
    }
    double value = metric.value;
    if (spec.normalization == ObjectiveNormalization::BaselineRelative) {
        if (!baseline || !baseline->usable() || !finite(baseline->value) || baseline->value == 0.0) {
            result.diagnostic = "Baseline evidence is unavailable.";
            return result;
        }
        value = value / baseline->value;
    }
    if (spec.normalization == ObjectiveNormalization::AlreadyNormalized)
        result.normalized = std::clamp(value, 0.0, 1.0);
    else {
        result.normalized = scale(value, spec.good, spec.bad);
        if (!finite(result.normalized)) {
            result.diagnostic = "Objective normalization range is invalid.";
            return result;
        }
    }
    if (spec.direction == ObjectiveDirection::LowerIsBetter)
        result.normalized = 1.0 - result.normalized;
    result.usable = true;
    result.contribution = result.normalized * spec.weight;
    return result;
}

ObjectiveAggregate ObjectiveAggregator::aggregate(const std::vector<ConstraintResult>& constraints,
                                                  const std::vector<ObjectiveResult>& objectives)
{
    ObjectiveAggregate aggregate;
    aggregate.constraints = constraints;
    std::stable_sort(aggregate.constraints.begin(), aggregate.constraints.end(),
                     [](const ConstraintResult& left, const ConstraintResult& right) {
                         return left.priority > right.priority;
                     });
    aggregate.objectives = objectives;
    for (const auto& constraint : constraints) {
        if (constraint.hard && (!constraint.satisfied || !constraint.evidenceAvailable))
            aggregate.feasible = false;
    }
    for (const auto& objective : objectives) {
        if (objective.usable) aggregate.score += objective.contribution;
    }
    return aggregate;
}

} // namespace rws
