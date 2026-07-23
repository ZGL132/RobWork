#include "StructureCandidateEvaluator.hpp"
#include "StructureCandidateCache.hpp"
#include "StructureDesignMutator.hpp"
#include "CandidateModelFactory.hpp"
#include "StructureObjectiveScorer.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalyzer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace rws {

// =============================================================================
//  Anonymous helpers
// =============================================================================
namespace {

// ---------------------------------------------------------------------------
//  computeEngineeringPreference �?how close values are to preferred [0, 1]
// ---------------------------------------------------------------------------
double computeEngineeringPreference(
    const std::vector<StructureDesignVariable>& variables,
    const std::vector<double>& values)
{
    if (variables.empty() || values.empty())
        return 1.0;

    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (std::size_t i = 0; i < variables.size() && i < values.size(); ++i)
    {
        const auto& var = variables[i];
        if (var.preferenceWeight <= 0.0 || !var.enabled)
            continue;

        double range = var.maximum - var.minimum;
        if (range <= 0.0)
            continue;

        double deviation = std::abs(values[i] - var.preferredValue);
        double fit       = 1.0 - std::min(deviation / range, 1.0);
        weightedSum += fit * var.preferenceWeight;
        totalWeight += var.preferenceWeight;
    }

    return (totalWeight > 0.0) ? (weightedSum / totalWeight) : 1.0;
}

// ---------------------------------------------------------------------------
//  estimateTotalLength �?sum of translation magnitudes of transform joints
// ---------------------------------------------------------------------------
double estimateTotalLength(const RobotModelSpec& spec)
{
    double sum = 0.0;
    for (const auto& jt : spec.transformJoints)
    {
        sum += std::sqrt(jt.pos[0] * jt.pos[0] +
                         jt.pos[1] * jt.pos[1] +
                         jt.pos[2] * jt.pos[2]);
    }
    return sum;
}

// ---------------------------------------------------------------------------
//  estimateBaseHeight �?Z of the first transform joint
// ---------------------------------------------------------------------------
double estimateBaseHeight(const RobotModelSpec& spec)
{
    if (spec.transformJoints.empty())
        return 0.0;
    return spec.transformJoints[0].pos[2];
}

// ---------------------------------------------------------------------------
//  estimateMaxCrossSection �?max link radius^2 * pi (cylinder assumption)
// ---------------------------------------------------------------------------
double estimateMaxCrossSection(const RobotModelSpec& spec)
{
    double maxArea = 0.0;
    for (const auto& draw : spec.drawables)
    {
        double r = draw.radius;           // metres
        double a = 3.14159265358979323846 * r * r;
        if (a > maxArea)
            maxArea = a;
    }
    return maxArea;
}

// ---------------------------------------------------------------------------
//  estimateMaxSlenderness �?height / diameter of the tallest link
// ---------------------------------------------------------------------------
double estimateMaxSlenderness(const RobotModelSpec& spec)
{
    double maxSlender = 0.0;
    for (const auto& draw : spec.drawables)
    {
        double d = 2.0 * draw.radius;
        if (d > 1e-12)
        {
            double s = draw.length / d;
            if (s > maxSlender)
                maxSlender = s;
        }
    }
    return maxSlender;
}

// ---------------------------------------------------------------------------
//  failureReasonString �?convert KinematicFailureReason to a short ASCII
// ---------------------------------------------------------------------------
const char* failureReasonString(KinematicFailureReason r)
{
    switch (r)
    {
    case KinematicFailureReason::None:             return "";
    case KinematicFailureReason::NoDevice:         return "NoDevice";
    case KinematicFailureReason::NoTcpFrame:       return "NoTcpFrame";
    case KinematicFailureReason::IkNoSolution:     return "IkNoSolution";
    case KinematicFailureReason::Collision:        return "Collision";
    case KinematicFailureReason::TargetResidual:   return "TargetResidual";
    case KinematicFailureReason::JointLimit:       return "JointLimit";
    case KinematicFailureReason::NearJointLimit:   return "NearJointLimit";
    case KinematicFailureReason::Singular:         return "Singular";
    case KinematicFailureReason::NearSingular:     return "NearSingular";
    case KinematicFailureReason::InvalidTarget:    return "InvalidTarget";
    case KinematicFailureReason::SolverError:      return "SolverError";
    default:                                       return "Unknown";
    }
}

} // anonymous namespace

// ===========================================================================
//  evaluate()
// ===========================================================================
void StructureCandidateEvaluator::evaluate(
    const StructureOptimizationProblem& problem,
    StructureCandidateResult& candidate,
    StructureEvaluationStage stage,
    const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    // ── 0.  Cache lookup ────────────────────────────────────────────────
    if (cache)
    {
        StructureCandidateResult cached;
        if (cache->find(problem, candidate.values, stage, cached))
        {
            candidate = cached;
            return;
        }
    }

    // ── 1.  Candidate setup ────────────────────────────────────────────
    candidate.stage  = stage;
    candidate.status = StructureCandidateStatus::Pending;

    auto tOverall = std::chrono::steady_clock::now();

    // ── 2.  Apply mutator ──────────────────────────────────────────────
    auto tModelStart = std::chrono::steady_clock::now();

    StructureMutationResult mutResult = StructureDesignMutator::apply(
        problem.context.modelSpec, problem.variables, candidate.values);

    if (!mutResult.ok)
    {
        candidate.status = StructureCandidateStatus::Failed;
        candidate.warnings.push_back("Mutator apply() returned ok=false");
        for (const auto& w : mutResult.warnings)
            candidate.warnings.push_back(w.message);

        // Cache the failed result too so we don't retry
        if (cache)
            cache->put(problem, candidate.values, stage, candidate);
        return;
    }

    for (const auto& w : mutResult.warnings)
        candidate.warnings.push_back(w.message);

    // ── 3.  Build WorkCell ─────────────────────────────────────────────
    CandidateModelBuildRequest buildReq;
    buildReq.spec           = mutResult.spec;
    buildReq.deviceName     = problem.context.deviceName;
    buildReq.tcpFrame       = problem.context.tcpFrame;
    buildReq.checkCollision = problem.evaluation.checkCollision &&
                              (stage == StructureEvaluationStage::Verified);

    CandidateModelFactory      factory;
    CandidateModelBuildResult  buildResult = factory.build(buildReq);

    if (!buildResult.ok)
    {
        candidate.status = StructureCandidateStatus::Failed;
        candidate.warnings.push_back("CandidateModelFactory.build() failed");
        if (cache)
            cache->put(problem, candidate.values, stage, candidate);
        return;
    }

    double modelBuildSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - tModelStart).count();

    // Collision detector (null if disabled)
    rw::core::Ptr<rw::proximity::CollisionDetector> colDetector;
    if (buildReq.checkCollision)
        colDetector = buildResult.artifact.collisionDetector;

    if (callbacks.isCancellationRequested &&
        callbacks.isCancellationRequested())
    {
        candidate.status = StructureCandidateStatus::Canceled;
        return;
    }

    // ── 4.  KinematicAnalyzer setup ────────────────────────────────────
    KinematicAnalyzer analyzer;
    analyzer.setThresholds(problem.evaluation.thresholds);

    auto tKinStart = std::chrono::steady_clock::now();

    // ── 5.  Evaluate each task point ───────────────────────────────────
    std::vector<StructureTaskMetric> taskMetrics;
    taskMetrics.reserve(problem.tasks.size());

    for (const auto& optTask : problem.tasks)
    {
        if (callbacks.isCancellationRequested &&
            callbacks.isCancellationRequested())
        {
            candidate.status = StructureCandidateStatus::Canceled;
            return;
        }

        if (callbacks.waitIfPaused)
            callbacks.waitIfPaused();

        StructureTaskMetric tm;
        tm.taskId   = optTask.point.id;
        tm.taskName = optTask.point.name;
        tm.required = optTask.required;
        tm.weight   = optTask.point.weight;

        // IK analysis via KinematicAnalyzer
        KinematicIkAnalysisResult ikResult = analyzer.analyzeIk(
            buildResult.artifact.device,
            buildResult.artifact.tcpFrame,
            buildResult.artifact.state,
            optTask.point,
            colDetector);

        tm.usableSolutionCount = static_cast<int>(ikResult.usableSolutionCount);
        tm.reachable           = (ikResult.usableSolutionCount > 0);

        // Best manipulability and joint margin among solutions
        double bestManip  = 0.0;
        double bestMargin = 0.0;
        bool   anyColl    = false;

        for (const auto& sol : ikResult.solutions)
        {
            if (sol.manipulability > bestManip)
                bestManip = sol.manipulability;
            if (sol.minJointLimitMargin > bestMargin)
                bestMargin = sol.minJointLimitMargin;
            if (sol.inCollision)
                anyColl = true;
        }

        tm.manipulability = bestManip;
        tm.jointMargin    = bestMargin;
        tm.inCollision    = anyColl;

        if (!tm.reachable &&
            ikResult.failureReason != KinematicFailureReason::None)
            tm.failure = failureReasonString(ikResult.failureReason);

        taskMetrics.push_back(tm);
    }

    double kinematicSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - tKinStart).count();

    // ── 6.  Raw metrics ────────────────────────────────────────────────
    StructureRawMetrics raw;
    raw.modelValid = true;

    int requiredCount = 0, requiredReachable = 0;
    int optionalCount = 0, optionalReachable = 0;
    int reachableCount = 0;

    std::vector<double> manipulabilities;
    std::vector<double> jointMargins;

    for (const auto& tm : taskMetrics)
    {
        if (tm.required)
        {
            ++requiredCount;
            if (tm.reachable) ++requiredReachable;
        }
        else
        {
            ++optionalCount;
            if (tm.reachable) ++optionalReachable;
        }

        if (tm.reachable)
        {
            manipulabilities.push_back(tm.manipulability);
            jointMargins.push_back(tm.jointMargin);
            ++reachableCount;
        }
    }

    raw.requiredTaskCount      = requiredCount;
    raw.requiredReachableCount = requiredReachable;
    raw.optionalTaskCount      = optionalCount;
    raw.optionalReachableCount = optionalReachable;

    // Weighted reachability
    if (requiredCount > 0)
        raw.weightedReachability =
            static_cast<double>(requiredReachable) /
            static_cast<double>(requiredCount);
    else if (optionalCount > 0)
        raw.weightedReachability =
            static_cast<double>(optionalReachable) /
            static_cast<double>(optionalCount);
    else
        raw.weightedReachability = 1.0;

    // 10th percentiles
    raw.manipulabilityP10 = StructureObjectiveScorer::percentile10(manipulabilities);
    raw.jointMarginP10    = StructureObjectiveScorer::percentile10(jointMargins);

    // Global minimum joint margin
    if (!jointMargins.empty())
    {
        auto it = std::min_element(jointMargins.begin(), jointMargins.end());
        raw.minimumJointMargin = *it;
    }

    // Collision-free rate
    int collisionFree = 0;
    for (const auto& tm : taskMetrics)
        if (tm.reachable && !tm.inCollision)
            ++collisionFree;

    raw.collisionFreeRate = (reachableCount > 0)
        ? static_cast<double>(collisionFree) /
          static_cast<double>(reachableCount)
        : 0.0;

    // Physical dimensions from spec
    raw.totalKinematicLength = estimateTotalLength(mutResult.spec);
    raw.baseHeight           = estimateBaseHeight(mutResult.spec);
    raw.maxCrossSection      = estimateMaxCrossSection(mutResult.spec);
    raw.maxLinkSlenderness   = estimateMaxSlenderness(mutResult.spec);

    // Engineering preference
    raw.engineeringPreference = computeEngineeringPreference(
        problem.variables, candidate.values);

    // Timing
    raw.modelBuildSeconds          = modelBuildSeconds;
    raw.kinematicEvaluationSeconds = kinematicSeconds;
    raw.taskMetrics                = std::move(taskMetrics);

    candidate.raw = raw;

    // ── 7.  Score ──────────────────────────────────────────────────────
    StructureObjectiveScorer scorer;
    scorer.score(problem, candidate);

    // ── 8.  Cache ──────────────────────────────────────────────────────
    if (cache)
        cache->put(problem, candidate.values, stage, candidate);
}

} // namespace rws
