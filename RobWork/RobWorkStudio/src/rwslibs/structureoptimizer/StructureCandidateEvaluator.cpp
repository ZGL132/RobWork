#include "KinematicEngineeringEvaluator.hpp"
#include "StructureCandidateCache.hpp"
#include "StructureDesignMutator.hpp"
#include "CandidateModelFactory.hpp"
#include "StructureObjectiveScorer.hpp"
#include "StructureWorkspaceCoverage.hpp"
#include <rwslibs/kinematicanalysis/KinematicAnalysisContext.hpp>
#include <rwslibs/kinematicanalysis/KinematicAnalyzer.hpp>
#include <rwslibs/kinematicanalysis/KinematicBatchRunner.hpp>
#include <rwslibs/kinematicanalysis/RegionCoverageEvaluator.hpp>
#include <rwslibs/kinematicanalysis/TargetEvaluator.hpp>
#include <rwslibs/robotmodelbuilder/RobotModelFingerprint.hpp>
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
// 工程偏好吻合度计算：衡量设计变量取值与工程师指定偏好值的接近程度。
// 仅统计已启用且权重大于 0 的变量，偏差按变量取值范围归一化后加权平均，
// 结果归一化到 [0, 1]；没有任何带偏好变量时返回 1.0，表示不施加偏好惩罚。
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
// 估算运动链总长度：累加所有平移关节位移向量的模长。这是"紧凑度"类约束与
// 目标的基础度量，直接反映机器人本体在空间中的扫掠尺寸。
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
// 估算基座高度：取第一个平移关节的 Z 坐标作为机器人安装高度，
// 用于约束过高基座在紧凑工位或入口高度受限场景中的可行性。
double estimateBaseHeight(const RobotModelSpec& spec)
{
    if (spec.transformJoints.empty())
        return 0.0;
    return spec.transformJoints[0].pos[2];
}

// ---------------------------------------------------------------------------
//  estimateMaxCrossSection �?max link radius^2 * pi (cylinder assumption)
// ---------------------------------------------------------------------------
// 估算最大横截面积：按"连杆为圆柱体"假设计算每个几何体截面面积并取最大值，
// 用于约束机器人在狭窄通道或密集工位中的扫掠直径。
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
// 估算最大长细比：取所有几何体中高度/直径比的最大值，长细比过大意味着
// 连杆细长，结构刚度与抗弯能力不足，运动载荷下易发生明显变形。
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
// 把运动学失败原因枚举映射为稳定的短字符串。以文本而非枚举数值序列化，
// 避免枚举顺序调整后报告与缓存内容发生不可控变化。
const char* failureReasonString(KinematicFailureReason r)
{
    switch (r)
    {
    case KinematicFailureReason::None:             return "";
    case KinematicFailureReason::NoDevice:         return "NoDevice";
    case KinematicFailureReason::NoTcpFrame:       return "NoTcpFrame";
    case KinematicFailureReason::IkNoSolution:     return "IkNoSolution";
    case KinematicFailureReason::Collision:        return "Collision";
    case KinematicFailureReason::CollisionDetectorUnavailable:
                                                     return "CollisionDetectorUnavailable";
    case KinematicFailureReason::TargetResidual:   return "TargetResidual";
    case KinematicFailureReason::JointLimit:       return "JointLimit";
    case KinematicFailureReason::NearJointLimit:   return "NearJointLimit";
    case KinematicFailureReason::Singular:         return "Singular";
    case KinematicFailureReason::NearSingular:     return "NearSingular";
    case KinematicFailureReason::InvalidTarget:    return "InvalidTarget";
    case KinematicFailureReason::SolverError:      return "SolverError";
    case KinematicFailureReason::FrameNotFound:    return "FrameNotFound";
    default:                                       return "Unknown";
    }
}

// 工作空间采样取消回调的上下文：把优化回调包装成 void* 用户数据，
// 以便接入只接受 C 风格 userData 的采样接口。
struct WorkspaceSamplingCallbackContext
{
    const StructureOptimizationCallbacks* callbacks = nullptr;
};

// 供采样器轮询的取消判断：透传给优化层回调，使长耗时的关节空间采样
// 能在用户发起取消时及时中断并释放计算资源。
bool isWorkspaceSamplingCancellationRequested(void* userData)
{
    const WorkspaceSamplingCallbackContext* context =
        static_cast<const WorkspaceSamplingCallbackContext*>(userData);
    return context != nullptr && context->callbacks != nullptr &&
           context->callbacks->isCancellationRequested &&
           context->callbacks->isCancellationRequested();
}

// 将单次目标点评价结果折叠为优化内部的任务指标：统计可用 IK 解数量、
// 可达性、可操作度与最小关节裕度，并保留失败原因供报告展示。
StructureTaskMetric makeTaskMetric(const TargetEvaluation& result,
                                   bool required,
                                   double weight)
{
    StructureTaskMetric metric;
    metric.taskId = result.target.id;
    metric.taskName = result.target.name;
    metric.required = required;
    metric.weight = weight;
    metric.usableSolutionCount = static_cast<int>(std::count_if(
        result.candidates.begin(), result.candidates.end(),
        [] (const TargetCandidate& candidate) {
            return candidate.configuration.feasibility == Feasibility::Feasible;
        }));
    metric.reachable = result.feasibility == Feasibility::Feasible;
    if (!result.candidates.empty()) {
        const ConfigurationEvaluation& selected =
            result.candidates.front().configuration;
        metric.manipulability = selected.manipulability;
        metric.jointMargin = selected.minimumJointMargin;
        metric.inCollision = selected.inCollision;
    }
    if (!metric.reachable && !result.failureReasons.empty())
        metric.failure = failureReasonString(result.failureReasons.front());
    return metric;
}

} // anonymous namespace

// ===========================================================================
//  evaluate()
// ===========================================================================
// 候选解主评价流程：给定一组设计变量取值，依次完成
//   0 缓存查找 -> 1 候选初始化 -> 2 应用变异器生成模型规格
//   -> 3 构建 WorkCell/碰撞模型 -> 4 配置运动学分析器
//   -> 5 逐任务点评价 + 工作空间覆盖率采样 -> 6 汇总原始指标
//   -> 7 多目标评分 -> 8 结果写回缓存。
// Verified 阶段优先消费冻结的需求执行契约：只有 Must 级任务/区域作为硬约束，
// Should/Info 级仅留作审计记录，不阻断候选进入后续流程。
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
    // 收集冻结需求中 Must 级且已编译的覆盖区域；这些区域在 Verified 阶段
    // 接受高精度验证，任一区域不可行都会直接导致候选判为 Infeasible。
    std::vector<RequirementExecutionRegion> verifiedRegions;
    if (stage == StructureEvaluationStage::Verified) {
        for (const RequirementExecutionRegion& region :
             problem.requirementExecution.workspaceRegions) {
            if (region.level == RequirementExecutionLevel::Must &&
                region.compileState == RequirementExecutionCompileState::Included) {
                verifiedRegions.push_back(region);
            }
        }
    }
    // 需求执行契约消费开关：Verified 阶段且契约携带已编译任务时，改由契约驱动评价；
    // 是否要求碰撞/无碰撞证据也从契约任务与区域中推导，避免与旧字段重复判断。
    const bool useRequirementExecutionTasks =
        stage == StructureEvaluationStage::Verified &&
        !problem.requirementExecution.tasks.empty();
    const bool useRequirementExecutionContract =
        useRequirementExecutionTasks || !verifiedRegions.empty();
    const bool requirementCollisionRequired =
        (useRequirementExecutionTasks && std::any_of(
            problem.requirementExecution.tasks.begin(),
            problem.requirementExecution.tasks.end(),
            [] (const RequirementExecutionTask& task) {
                return task.compileState == RequirementExecutionCompileState::Included &&
                       task.collisionFreeRequired;
            })) ||
        std::any_of(verifiedRegions.begin(), verifiedRegions.end(),
            [] (const RequirementExecutionRegion& region) {
                return region.collisionFreeRequired;
            });
    const bool evaluateVerifiedRegions = !verifiedRegions.empty();
    const bool evaluateEstimatedWorkspaceCoverage =
        !coverageBoxes.empty() && !evaluateVerifiedRegions;
    const bool evaluateWorkspaceCoverage =
        evaluateEstimatedWorkspaceCoverage || evaluateVerifiedRegions;
    // ── 0.  Cache lookup ────────────────────────────────────────────────
    // 以"问题 + 变量取值 + 阶段"为键查询缓存：命中则直接复用历史评价结果，
    // 避免重复构建模型与运动学求解，是优化迭代中的主要性能保障。
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
    // 先置为待评估状态；后续任何失败分支都会改写 status，便于上层区分
    // 失败（Failed）、取消（Canceled）与不可行（Infeasible）三种结局。
    candidate.stage  = stage;
    candidate.status = StructureCandidateStatus::Pending;

    auto tOverall = std::chrono::steady_clock::now();

    // ── 2.  Apply mutator ──────────────────────────────────────────────
    auto tModelStart = std::chrono::steady_clock::now();

    // 把连续变量取值映射为合法模型规格：校验取值范围、同步关联几何等。
    // 变异失败视为候选不可行而非异常，失败结果同样写入缓存避免重复尝试。
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
    // 依据模型规格实例化设备与场景快照；是否启用碰撞检测由评价阶段与需求
    // 契约共同决定：Verified 且需求要求碰撞/无碰撞证据时必然开启。
    CandidateModelBuildRequest buildReq;
    buildReq.spec           = mutResult.spec;
    buildReq.deviceName     = problem.context.deviceName;
    buildReq.tcpFrame       = problem.context.tcpFrame;
    buildReq.scenarioSnapshot = problem.scenarioSnapshot.available()
        ? &problem.scenarioSnapshot : nullptr;
    buildReq.scenarioBaseDirectory = problem.scenarioSnapshot.baseDirectory;
    buildReq.checkCollision =
        (stage == StructureEvaluationStage::Verified &&
         (problem.evaluation.checkCollision || requirementCollisionRequired)) ||
        (problem.evaluation.checkCollision && evaluateWorkspaceCoverage &&
         configuredWorkspaceSampling.checkCollision);

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
    // 装配运动学分析上下文：注入工作单元、设备、TCP、基态与模型指纹。
    // 指纹参与缓存键与溯源，保证同一模型规格获得一致的评估语义。
    KinematicAnalyzer analyzer;
    analyzer.setThresholds(problem.evaluation.thresholds);

    AnalysisContextInput contextInput;
    contextInput.workcell = buildResult.artifact.workcell;
    contextInput.device = buildResult.artifact.device;
    contextInput.tcpFrame = buildResult.artifact.tcpFrame;
    contextInput.baseState = buildResult.artifact.state;
    contextInput.collisionDetector = taskCollisionDetector;
    contextInput.deviceName = buildResult.artifact.device->getName();
    contextInput.tcpFrameName = buildResult.artifact.tcpFrame->getName();
    contextInput.modelFingerprint =
        RobotModelFingerprint::canonicalSha256(mutResult.spec);
    if (contextInput.modelFingerprint.empty())
        contextInput.modelFingerprint = "structureoptimizer.candidate";
    contextInput.environmentFingerprint =
        problem.requirementProvenance.environmentFingerprint.empty()
            ? "structureoptimizer.environment"
            : problem.requirementProvenance.environmentFingerprint;
    contextInput.thresholds = problem.evaluation.thresholds;
    contextInput.collisionRequired =
        !useRequirementExecutionContract &&
        stage == StructureEvaluationStage::Verified && problem.evaluation.checkCollision;
    AnalysisContext analysisContext;
    std::string contextError;
    if (!makeAnalysisContext(contextInput, analysisContext, &contextError)) {
        candidate.status = StructureCandidateStatus::Failed;
        candidate.warnings.push_back(contextError);
        if (cache)
            cache->put(problem, candidate.values, stage, candidate);
        return;
    }
    auto tKinStart = std::chrono::steady_clock::now();

    // ── 5.  Evaluate each task point ───────────────────────────────────
    // 优先按冻结需求契约批量验证任务（KinematicBatchRunner）；否则回退到
    // 传统 OptimizationTaskPoint 逐点评价。两条路径都产出 StructureTaskMetric。
    std::vector<StructureTaskMetric> taskMetrics;
    taskMetrics.reserve(useRequirementExecutionTasks
        ? problem.requirementExecution.tasks.size() : problem.tasks.size());
    bool taskEvaluationDataInsufficient = false;
    WorkspaceSamplingCallbackContext cancellationContext;
    cancellationContext.callbacks = &callbacks;
    CancellationToken cancellation;
    cancellation.isCancellationRequested = &isWorkspaceSamplingCancellationRequested;
    cancellation.userData = &cancellationContext;

    if (useRequirementExecutionTasks) {
        if (callbacks.waitIfPaused)
            callbacks.waitIfPaused();
        BatchRunOptions batchOptions;
        batchOptions.evidenceStage = AnalysisEvidenceStage::Verified;
        batchOptions.targetOptions.evidenceStage = AnalysisEvidenceStage::Verified;
        batchOptions.targetOptions.checkCollision = contextInput.collisionRequired;
        const RequirementValidationSummary summary =
            KinematicBatchRunner().validateRequirements(
                analysisContext, problem.requirementExecution, batchOptions, cancellation);
        const std::size_t count = std::min(
            problem.requirementExecution.tasks.size(), summary.taskResults.size());
        for (std::size_t i = 0; i < count; ++i) {
            const RequirementExecutionTask& task = problem.requirementExecution.tasks[i];
            if (task.compileState != RequirementExecutionCompileState::Included)
                continue;
            const TargetEvaluation& result = summary.taskResults[i];
            taskMetrics.push_back(makeTaskMetric(
                result, task.level == RequirementExecutionLevel::Must,
                task.level == RequirementExecutionLevel::Must ? 1.0 : 0.5));
            taskEvaluationDataInsufficient = taskEvaluationDataInsufficient ||
                (task.level == RequirementExecutionLevel::Must &&
                 result.feasibility == Feasibility::DataInsufficient);
        }
    }
    else {
        TargetEvaluator targetEvaluator;
        for (const OptimizationTaskPoint& optTask : problem.tasks) {
            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested()) {
                candidate.status = StructureCandidateStatus::Canceled;
                return;
            }
            if (callbacks.waitIfPaused)
                callbacks.waitIfPaused();

            TargetEvaluationOptions targetOptions;
            targetOptions.evidenceStage = stage == StructureEvaluationStage::Verified
                ? AnalysisEvidenceStage::Verified : AnalysisEvidenceStage::Quick;
            targetOptions.checkCollision = contextInput.collisionRequired;
            targetOptions.requireCollisionFree = contextInput.collisionRequired;
            targetOptions.positionToleranceMeters = optTask.point.tolerance.positionMeters;
            targetOptions.orientationToleranceDeg = optTask.point.tolerance.orientationDeg;
            const TargetEvaluation targetResult =
                targetEvaluator.evaluate(analysisContext, optTask.point, targetOptions);
            taskMetrics.push_back(makeTaskMetric(
                targetResult, optTask.required, optTask.point.weight));
            taskEvaluationDataInsufficient = taskEvaluationDataInsufficient ||
                (optTask.required &&
                 targetResult.feasibility == Feasibility::DataInsufficient);
        }
    }

    if (callbacks.isCancellationRequested && callbacks.isCancellationRequested()) {
        candidate.status = StructureCandidateStatus::Canceled;
        return;
    }

    double kinematicSeconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - tKinStart).count();

    std::vector<WorkspaceSample> workspaceSamples;
    double workspaceSeconds = 0.0;
    bool workspaceCoverageDataInsufficient = false;
    bool verifiedRegionInfeasible = false;
    StructureWorkspaceCoverageResult workspaceCoverage;
    std::vector<StructureWorkspaceRegionMetric> workspaceRegionMetrics;
    // 旧式覆盖盒估算路径：对关节空间采样后，把 TCP 样本转换到每个覆盖盒的
    // 参考系再逐盒独立统计；汇总保留"最差区域"作为保守总览，避免单区域
    // 覆盖率不足被其他区域的高覆盖掩盖。
    if (evaluateEstimatedWorkspaceCoverage)
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

    // 冻结需求 Verified 区域验证路径：逐区域调用 RegionCoverageEvaluator 做高精度
    // 覆盖验证；任一 Must 区域不可行即整候选不可行，证据不足则标记为数据不足。
    if (evaluateVerifiedRegions)
    {
        const auto tWorkspaceStart = std::chrono::steady_clock::now();
        workspaceCoverage.coverage = 1.0;
        bool hasCoverageResult = false;

        for (const RequirementExecutionRegion& region : verifiedRegions) {
            StructureWorkspaceRegionMetric metric;
            metric.id = region.id;
            metric.referenceFrame = region.refFrame;

            const RegionCoverageResult result =
                RegionCoverageEvaluator().evaluate(analysisContext, region, cancellation);
            metric.coverage = result.positionCoverage;
            metric.occupiedCellCount =
                static_cast<std::size_t>(result.reachableCells);
            metric.totalCellCount = static_cast<std::size_t>(result.totalCells);
            for (const AnalysisWarning& warning : result.warnings)
                candidate.warnings.push_back(warning.code + ": " + warning.message);

            if (result.feasibility == Feasibility::DataInsufficient)
                workspaceCoverageDataInsufficient = true;
            else if (result.feasibility == Feasibility::Infeasible)
                verifiedRegionInfeasible = true;

            if (!hasCoverageResult || metric.coverage < workspaceCoverage.coverage) {
                workspaceCoverage.coverage = metric.coverage;
                workspaceCoverage.occupiedCellCount = metric.occupiedCellCount;
                workspaceCoverage.totalCellCount = metric.totalCellCount;
                hasCoverageResult = true;
            }
            workspaceRegionMetrics.push_back(metric);

            if (callbacks.isCancellationRequested &&
                callbacks.isCancellationRequested()) {
                candidate.status = StructureCandidateStatus::Canceled;
                return;
            }
        }
        workspaceSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - tWorkspaceStart).count();
        if (!hasCoverageResult)
            workspaceCoverageDataInsufficient = true;
    }

    // ── 6.  Raw metrics ────────────────────────────────────────────────
    // 把逐任务指标与工作空间结果聚合为标量原始指标，供约束判定与评分器消费。
    StructureRawMetrics raw;
    raw.modelValid = true;
    raw.taskEvaluationDataInsufficient = taskEvaluationDataInsufficient;

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
    // 按多目标权重对原始指标打分；任一 Verified 区域不可行时强制候选不可行，
    // 避免区域证据缺失被高分掩盖。
    StructureObjectiveScorer scorer;
    scorer.score(problem, candidate);
    if (verifiedRegionInfeasible) {
        candidate.feasible = false;
        candidate.status = StructureCandidateStatus::Infeasible;
    }

    // ── 8.  Cache ──────────────────────────────────────────────────────
    // 评价完成后写回缓存，供后续同参候选直接复用评价结果。
    if (cache)
        cache->put(problem, candidate.values, stage, candidate);
}

} // namespace rws
