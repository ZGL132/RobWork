#include "KinematicEngineeringEvaluator.hpp"
#include "StructureCandidateCache.hpp"
#include "StructureDesignMutator.hpp"
#include "CandidateModelFactory.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureWorkspaceCoverage.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalyzer.hpp>
#include <rw/kinematics/Kinematics.hpp>
#include <rw/kinematics/Frame.hpp>

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

struct WorkspaceSamplingCallbackContext
{
    const StructureOptimizationCallbacks* callbacks = nullptr;
};

bool isWorkspaceSamplingCancellationRequested(void* userData)
{
    const WorkspaceSamplingCallbackContext* context =
        static_cast<const WorkspaceSamplingCallbackContext*>(userData);
    return context != nullptr && context->callbacks != nullptr &&
           context->callbacks->isCancellationRequested &&
           context->callbacks->isCancellationRequested();
}

} // anonymous namespace

// ===========================================================================
//  evaluate()
// ===========================================================================
void KinematicEngineeringEvaluator::evaluateLegacy(
    StructureCandidateResult& candidate,
    StructureEvaluationStage stage,
    const StructureOptimizationCallbacks& callbacks,
    StructureCandidateCache* cache)
{
    const StructureOptimizationProblem& problem = _problem;
    const WorkspaceSamplingConfig& configuredWorkspaceSampling =
        stage == StructureEvaluationStage::Verified
            ? problem.evaluation.verifiedWorkspace
            : problem.evaluation.quickWorkspace;
    // 冻结需求可携带多个 Must 覆盖区域。旧项目仍可能只写 coverageBox，因此在此处
    // 统一成评价列表：新格式优先，旧格式作为兼容回退，后续逻辑不再假设只有一个盒子。
    std::vector<WorkspaceCoverageBox> coverageBoxes = problem.evaluation.coverageBoxes;
    if (coverageBoxes.empty() && problem.evaluation.coverageBox.enabled)
        coverageBoxes.push_back(problem.evaluation.coverageBox);
    const bool evaluateWorkspaceCoverage = !coverageBoxes.empty();
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
    buildReq.scenarioSnapshot = problem.scenarioSnapshot.available()
        ? &problem.scenarioSnapshot : nullptr;
    buildReq.scenarioBaseDirectory = problem.scenarioSnapshot.baseDirectory;
    buildReq.checkCollision = problem.evaluation.checkCollision &&
                              (stage == StructureEvaluationStage::Verified ||
                               (evaluateWorkspaceCoverage &&
                                configuredWorkspaceSampling.checkCollision));

    CandidateModelFactory      factory;
    CandidateModelBuildResult  buildResult = factory.build(buildReq);

    if (!buildResult.ok)
    {
        candidate.status = StructureCandidateStatus::Failed;
        candidate.warnings.push_back("CandidateModelFactory.build() failed");
        for (const AnalysisWarning& warning : buildResult.warnings)
            candidate.warnings.push_back(warning.code + ": " + warning.message);
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
    const rw::core::Ptr<rw::proximity::CollisionDetector> taskCollisionDetector =
        stage == StructureEvaluationStage::Verified ? colDetector : nullptr;

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

        const TaskPointReachabilityResult taskResult = analyzer.analyzeTaskPoint(
            buildResult.artifact.workcell.get(),
            buildResult.artifact.device,
            buildResult.artifact.tcpFrame,
            buildResult.artifact.state,
            optTask.point,
            taskCollisionDetector);
        const KinematicIkAnalysisResult& ikResult = taskResult.ik;

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

        if (!tm.reachable && taskResult.primaryFailure != KinematicFailureReason::None)
            tm.failure = failureReasonString(taskResult.primaryFailure);

        taskMetrics.push_back(tm);
    }

    double kinematicSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - tKinStart).count();

    std::vector<WorkspaceSample> workspaceSamples;
    double workspaceSeconds = 0.0;
    bool workspaceCoverageDataInsufficient = false;
    StructureWorkspaceCoverageResult workspaceCoverage;
    std::vector<StructureWorkspaceRegionMetric> workspaceRegionMetrics;
    if (evaluateWorkspaceCoverage)
    {
        WorkspaceSamplingConfig workspaceSampling = configuredWorkspaceSampling;
        if (!problem.evaluation.checkCollision)
            workspaceSampling.checkCollision = false;

        WorkspaceSamplingCallbackContext samplingContext;
        samplingContext.callbacks = &callbacks;
        WorkspaceSamplingRunCallbacks samplingCallbacks;
        samplingCallbacks.isCancellationRequested =
            &isWorkspaceSamplingCancellationRequested;
        samplingCallbacks.userData = &samplingContext;

        const auto tWorkspaceStart = std::chrono::steady_clock::now();
        workspaceSamples = analyzer.sampleWorkspace(
            buildResult.artifact.device,
            buildResult.artifact.tcpFrame,
            buildResult.artifact.state,
            workspaceSampling,
            colDetector,
            samplingCallbacks);
        workspaceSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - tWorkspaceStart).count();

        if (callbacks.isCancellationRequested &&
            callbacks.isCancellationRequested())
        {
            candidate.status = StructureCandidateStatus::Canceled;
            return;
        }

        if (workspaceSamples.empty()) {
            workspaceCoverageDataInsufficient = true;
        }
        else {
            // 同一批关节空间采样可以服务多个需求区域；每个区域仅将 TCP 样本坐标
            // 转换到它自己的参考系后独立统计，不把工装局部盒错误地视作 WORLD 盒。
            workspaceCoverage.coverage = 1.0;
            bool hasCoverageResult = false;
            for (const WorkspaceCoverageBox& box : coverageBoxes) {
                std::vector<WorkspaceSample> samplesInRegionFrame = workspaceSamples;
                const rw::kinematics::Frame* referenceFrame = nullptr;
                if (box.referenceFrame.empty() || box.referenceFrame == "WORLD")
                    referenceFrame = buildResult.artifact.workcell->getWorldFrame();
                else
                    referenceFrame = buildResult.artifact.workcell->findFrame(box.referenceFrame);
                if (referenceFrame == nullptr) {
                    workspaceCoverageDataInsufficient = true;
                    candidate.warnings.push_back("Workspace coverage reference frame is unavailable: " +
                                                 box.referenceFrame);
                    continue;
                }
                const rw::math::Transform3D<> worldTframe =
                    rw::kinematics::Kinematics::worldTframe(referenceFrame, buildResult.artifact.state);
                // RobWork 的 Transform3D 不定义“变换 * 点”的统一运算符。显式写成
                // p_frame = R_world_frame^T * (p_world - t_world_frame)，既避免 API
                // 版本差异，也明确这里转换的是位置而非完整位姿。
                const rw::math::Rotation3D<> frameRworld = rw::math::inverse(worldTframe.R());
                for (WorkspaceSample& sample : samplesInRegionFrame) {
                    const rw::math::Vector3D<> worldPoint(sample.tcpPosition[0],
                                                          sample.tcpPosition[1],
                                                          sample.tcpPosition[2]);
                    const rw::math::Vector3D<> localPoint =
                        frameRworld * (worldPoint - worldTframe.P());
                    for (std::size_t axis = 0; axis < sample.tcpPosition.size(); ++axis)
                        sample.tcpPosition[axis] = localPoint[axis];
                }
                const StructureWorkspaceCoverageResult regionCoverage =
                    StructureWorkspaceCoverage::analyze(samplesInRegionFrame, box);
                StructureWorkspaceRegionMetric metric;
                metric.id = box.id;
                metric.referenceFrame = box.referenceFrame;
                metric.coverage = regionCoverage.coverage;
                metric.occupiedCellCount = regionCoverage.occupiedCellCount;
                metric.totalCellCount = regionCoverage.totalCellCount;
                // 旧版汇总字段保留“最差区域”作为保守总览，保证任何一个 Must 区域
                // 不足时均不会被其余区域的高覆盖率掩盖；逐区域明细仍完整保存在 raw 中。
                if (!hasCoverageResult || regionCoverage.coverage < workspaceCoverage.coverage) {
                    workspaceCoverage = regionCoverage;
                    hasCoverageResult = true;
                }
                workspaceRegionMetrics.push_back(metric);
            }
            if (!hasCoverageResult)
                workspaceCoverage.coverage = 0.0;
        }
    }

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
    raw.workspaceCoverageDataInsufficient = workspaceCoverageDataInsufficient;
    raw.workspaceCoverage = workspaceCoverage.coverage;
    raw.workspaceOccupiedCellCount = workspaceCoverage.occupiedCellCount;
    raw.workspaceTotalCellCount = workspaceCoverage.totalCellCount;
    raw.workspaceRegionMetrics = std::move(workspaceRegionMetrics);

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
    raw.workspaceEvaluationSeconds = workspaceSeconds;
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
