/**
 * @file   JointRun.cpp
 * @brief  StageD 联合运行编排实现——八项指标全量计算、检查点域中间状态
 *         codec（IRDOPTC1 检查点域）与联合两级批量编排（并行分片/稳定
 *         合并/恢复不重复统计）（WP-21-T04；设计依据见 JointRun.hpp 文件头）。
 *
 * 实现口径登记（DTB §5.4——均为卡面语义的显式化，不改语义）：
 *   I-J1 批容量固定常数 kBatchCapacity=4（头文件类注——卡面无批容量字段；
 *        批容量只影响批界划分〔检查点粒度/派发粒度〕，结论面与批容量
 *        无关——测试钉扎）；末批可短；
 *   I-J2 审计增量只在编排线程合并时记账（worker 线程只产记录不碰审计）
 *        ——按批序合并 ⇒ 与线程数无关的确定性计数（NFR-COR-02/AT-35）；
 *   I-J3 恢复基点的账目处理：已完成批（resumeFrom.completedBatchIndices）
 *        的记录仍按确定性重推进（记录载荷不在检查点——§9.2 ResultBatch
 *        分批回传语义下载荷已流出进程，编排内记录需重推导；确定性同输入
 *        ⇒ 同记录，P-EX-9「宁可重算不可错续」的编排侧同源取向），但其
 *        审计增量与检查点状态产出**一律跳过**（账目已在 resumeFrom.audit
 *        基点内——继续不重复统计，AT-35）；resumedBatchesSkipped 记账；
 *   I-J4 completedUnits/totalUnits 的相内连续计数：批索引跨相连续（Quick
 *        批 [0,Q)、Verified 批 [Q,Q+V)）；Quick 相检查点 totalUnits=Q
 *        （Verified 批数未定，不预估——不私设），Verified 相检查点
 *        totalUnits=Q+V；completedUnits 恒为累计完成批数（单调）；
 *   I-J5 缓存消费位如实为零查找（门面 sliceId 六层完成前不存在——查找
 *        先于评估在联合键上结构不可能；命中面留待装配批次，审计缓存四
 *        计数恒 0 不伪造命中——JointRun.hpp 文件头不落项登记）。
 */

#include <sdurws/ird/optimization/JointRun.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

#include <sdurws/ird/optimization/DiagCodes.hpp>  // OPT-* 码值常量（唯一书写点）
#include <sdurws/ird/optimization/Pareto.hpp>     // IParetoFrontBuilder/FeasibleCandidate

namespace sdurws::ird::optimization {

namespace {

// ---------------------------------------------------------------------
// 局部辅助（EvaluatorPorts.cpp 同款语义——本文件自持，防跨文件耦合）
// ---------------------------------------------------------------------

/// 小端写入辅助（codec 编码——确定性字节序）。
void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFU));
}

void putU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFU));
    }
}

/// 小端读取辅助（越界即抛——截断载荷绝不部分采纳）。
std::uint32_t getU32(const std::vector<std::uint8_t>& in, std::size_t& pos)
{
    if (pos + 4 > in.size()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷截断（u32 越界）——"
                                "恢复拒绝（P-EX-9 宁重算不错续）");
    }
    const std::uint32_t v = static_cast<std::uint32_t>(in[pos])
                            | (static_cast<std::uint32_t>(in[pos + 1]) << 8)
                            | (static_cast<std::uint32_t>(in[pos + 2]) << 16)
                            | (static_cast<std::uint32_t>(in[pos + 3]) << 24);
    pos += 4;
    return v;
}

std::uint64_t getU64(const std::vector<std::uint8_t>& in, std::size_t& pos)
{
    std::uint64_t v = 0;
    if (pos + 8 > in.size()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷截断（u64 越界）——"
                                "恢复拒绝（P-EX-9 宁重算不错续）");
    }
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(in[pos + static_cast<std::size_t>(i)]) << (8 * i);
    }
    pos += 8;
    return v;
}

/// 激活目标槽位是否全部有值（§7.4"缺失指标候选不参与支配比较"的编排侧
/// 判定——与 EvaluatorPorts.cpp 同名辅助同语义同实现，消费者零漂移）。
bool activeMetricsComplete(const StaticMetricResult& metrics, const ObjectiveSet& objectives)
{
    for (const auto& entry : objectives.entries) {
        if (!metrics.valueOf(entry.metricId).has_value()) {
            return false;
        }
    }
    return true;
}

/// 由指标事实组装八槽 FeasibleCandidate（槽序＝MetricId 枚举序——buildFront
/// @pre 输入契约；调用方保证激活槽位完整）。
FeasibleCandidate toFeasibleCandidate(const TwoStageRunRecord& record)
{
    FeasibleCandidate fc;
    fc.candidateId = record.candidateId;
    fc.isBaseline = record.isBaseline;
    fc.metricValues.reserve(kMetricCount);
    for (int i = 0; i < static_cast<int>(kMetricCount); ++i) {
        fc.metricValues.push_back(record.metrics.valueOf(static_cast<MetricId>(i)));
    }
    return fc;
}

/// Quick 筛选淘汰原因（预算线——§8.4"保守淘汰（ScreenedOut，含原因）"；
/// 与 EvaluatorPorts.cpp 同名辅助同语义）。
RejectionReason makeQuickScreenRejection(OptimizationStage stage)
{
    RejectionReason r;
    r.stage = stage;
    r.sourceId = "opt.pipeline.quick-screen";   // 筛选步骤定位（编排层锚）
    r.reasonToken = std::string(kRejectQuickScreenedBudget);
    r.subject = {};                              // 集合级淘汰——无单对象定位
    r.mode = core::EvaluationMode::Quick;        // 发生模式＝Quick（§8.3 第 9 步）
    return r;
}

/// 检查点状态头常量（DOPT-10 登记魔数——与 config.opt canonical 同名异域，
/// EvaluatorPorts.hpp 编码布局注同源登记）。
constexpr char kCheckpointMagic[8] = {'I', 'R', 'D', 'O', 'P', 'T', 'C', '1'};

}  // namespace

// =====================================================================
// 八项指标全量计算（§7.1 全表——前三项复用 T05 单点，后五项 StageD 补算）
// =====================================================================

StaticMetricResult computeJointMetrics(OptimizationStage stage,
                                       const JointCandidateFacts& facts)
{
    // 前三项（§7.1 行 1~3）＋ StageB 的后五项 StageNotComputable 缺失语义
    // ——同一实现同一缺失语义（T05 单点零漂移）。
    StaticMetricResult result = computeStaticMetrics(stage, facts.staticFacts);
    if (stage != OptimizationStage::StageD) {
        return result;  // StageB：后五项恒"—"（§7.1「阶段可算」列——不估算）
    }

    // StageD：后五项补算（§7.1 行 4~8）。逐项同一规则：事实缺失 ⇒
    // SourceMissing（"—"——§7.1 缺失语义列：无时间参数化/无可行组合/成本
    // 项缺失/降级标记，绝不输出 0 冒充——NFR-COR-03）；事实非有限 ⇒
    // NonFiniteInput（§7.2：指标值非有限→候选评估失败处理——候选级后果
    // 由编排侧映射，计算面只如实标记）；否则输出值。
    auto setMetric = [&](std::size_t idx, MetricId id, const std::optional<double>& v,
                         const char* detail) {
        MetricComputation& m = result.metrics[idx];
        m.metricId = id;
        m.detail = detail;
        if (!v.has_value()) {
            m.valueSi.reset();
            m.gapToken = kMetricGapSourceMissing;
            return;
        }
        if (!std::isfinite(*v)) {
            m.valueSi.reset();
            m.gapToken = kMetricGapNonFinite;
            return;
        }
        m.valueSi = *v;
        m.gapToken = {};
    };
    setMetric(3, MetricId::CycleTime, facts.jointFacts.cycleTimeS,
              "节拍＝trj 层 totalDurationS 投影（§7.1 行 4——WP-21-T04）");
    setMetric(4, MetricId::DeviceCost, facts.jointFacts.deviceCost,
              "器件成本＝sel 层 SEL-06 事实投影（§7.1 行 5；货币口径 P-OPT-5）");
    setMetric(5, MetricId::DeviceMass, facts.jointFacts.deviceMassKg,
              "器件质量＝sel 层目录质量合成投影（§7.1 行 6）");
    setMetric(6, MetricId::JointPositiveWork, facts.jointFacts.jointPositiveWorkJ,
              "正机械功＝dyn 层 positiveEnergyJ 投影（§7.1 行 7）");
    setMetric(7, MetricId::MinDriveMargin, facts.jointFacts.minDriveMargin,
              "驱动裕量＝sel 层裕量事实投影（§7.1 行 8；比值定义 P-OPT-5）");
    return result;
}

// =====================================================================
// 检查点域中间状态（isValid/encode/decode——IRDOPTC1 检查点域）
// =====================================================================

bool JointCheckpointState::isValid() const noexcept
{
    if (codecVersion < 1U) {
        return false;
    }
    // 批索引/待评估队列：升序且无重复（确定性序契约——乱序即结构损坏）。
    auto strictlyAscending = [](const std::vector<std::uint32_t>& v) {
        for (std::size_t i = 1; i < v.size(); ++i) {
            if (v[i] <= v[i - 1]) {
                return false;
            }
        }
        return true;
    };
    if (!strictlyAscending(completedBatchIndices)
        || !strictlyAscending(pendingCandidateIndices)) {
        return false;
    }
    return completedUnits <= totalUnits;
}

std::vector<std::uint8_t>
encodeJointCheckpointState(const JointCheckpointState& state)
{
    // 写出前置校验（结构非法＝调用方契约违约 fail-fast——不产出损坏载荷）。
    if (!state.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点中间状态结构非法"
                                "（版本/索引序/计数关系——encodeJointCheckpointState）");
    }
    std::vector<std::uint8_t> out;
    out.reserve(64 + 4 * (state.completedBatchIndices.size()
                          + state.pendingCandidateIndices.size()));
    out.insert(out.end(), std::begin(kCheckpointMagic), std::end(kCheckpointMagic));
    putU32(out, state.codecVersion);
    out.push_back(static_cast<std::uint8_t>(state.phase));  // u8 相（Quick/Verified）
    putU32(out, state.generation);
    putU64(out, state.completedUnits);
    putU64(out, state.totalUnits);
    putU32(out, static_cast<std::uint32_t>(state.completedBatchIndices.size()));
    for (const std::uint32_t idx : state.completedBatchIndices) {
        putU32(out, idx);
    }
    putU32(out, static_cast<std::uint32_t>(state.pendingCandidateIndices.size()));
    for (const std::uint32_t idx : state.pendingCandidateIndices) {
        putU32(out, idx);
    }
    // 审计段（九计数依次 u32——字段序＝TwoStageRunAuditCounts 声明序，冻结）。
    putU32(out, state.audit.candidatesGenerated);
    putU32(out, state.audit.quickEvaluated);
    putU32(out, state.audit.quickScreenedOut);
    putU32(out, state.audit.verifiedEvaluated);
    putU32(out, state.audit.cacheLookups);
    putU32(out, state.audit.cacheFullHits);
    putU32(out, state.audit.cacheDiagnosticOnly);
    putU32(out, state.audit.cacheIncompatible);
    putU32(out, state.audit.cacheReplayUnavailable);
    return out;
}

JointCheckpointState
decodeJointCheckpointState(const std::vector<std::uint8_t>& bytes)
{
    // 魔数（8 字节逐字节比对——不符即不可读，绝不部分采纳）。
    if (bytes.size() < 8
        || !std::equal(std::begin(kCheckpointMagic), std::end(kCheckpointMagic),
                       bytes.begin())) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷魔数不符"
                                "（非 IRDOPTC1 检查点域字节——恢复拒绝，P-EX-9）");
    }
    JointCheckpointState state;
    std::size_t pos = 8;
    state.codecVersion = getU32(bytes, pos);
    // 版本支持区间（单点＝当前 codec 版本——旧版本不迁移，P-EX-9）。
    if (state.codecVersion != kJointCheckpointCodecVersion) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点 codec 版本不支持（载入 "
                                + std::to_string(state.codecVersion) + "，支持 "
                                + std::to_string(kJointCheckpointCodecVersion)
                                + "）——Incompatible 拒绝恢复，不迁移（P-EX-9）");
    }
    if (pos >= bytes.size()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷截断（phase 越界）");
    }
    const auto phaseByte = bytes[pos++];
    if (phaseByte != static_cast<std::uint8_t>(core::EvaluationMode::Quick)
        && phaseByte != static_cast<std::uint8_t>(core::EvaluationMode::Verified)) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点相字段非法（词表外位型）");
    }
    state.phase = static_cast<core::EvaluationMode>(phaseByte);
    state.generation = getU32(bytes, pos);
    state.completedUnits = getU64(bytes, pos);
    state.totalUnits = getU64(bytes, pos);
    const std::uint32_t completedCount = getU32(bytes, pos);
    state.completedBatchIndices.reserve(completedCount);
    for (std::uint32_t i = 0; i < completedCount; ++i) {
        state.completedBatchIndices.push_back(getU32(bytes, pos));
    }
    const std::uint32_t pendingCount = getU32(bytes, pos);
    state.pendingCandidateIndices.reserve(pendingCount);
    for (std::uint32_t i = 0; i < pendingCount; ++i) {
        state.pendingCandidateIndices.push_back(getU32(bytes, pos));
    }
    // 审计段（九计数——截断即抛）。
    state.audit.candidatesGenerated = getU32(bytes, pos);
    state.audit.quickEvaluated = getU32(bytes, pos);
    state.audit.quickScreenedOut = getU32(bytes, pos);
    state.audit.verifiedEvaluated = getU32(bytes, pos);
    state.audit.cacheLookups = getU32(bytes, pos);
    state.audit.cacheFullHits = getU32(bytes, pos);
    state.audit.cacheDiagnosticOnly = getU32(bytes, pos);
    state.audit.cacheIncompatible = getU32(bytes, pos);
    state.audit.cacheReplayUnavailable = getU32(bytes, pos);
    if (pos != bytes.size()) {
        // 尾随字节＝编码面漂移（版本内布局被改动）——拒绝，不猜。
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷尾随字节（布局漂移）——"
                                "恢复拒绝（P-EX-9）");
    }
    if (!state.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 检查点载荷结构非法"
                                "（索引序/计数关系——恢复拒绝）");
    }
    return state;
}

// =====================================================================
// 联合两级批量编排器
// =====================================================================

JointRunOrchestrator::JointRunOrchestrator(OptimizationStage stage,
                                           JointRunOrchestratorDeps deps)
    : m_stage(stage), m_deps(deps)
{
    // 装配校验（构造期 fail-fast）：内层依赖面/指标投影缝/Pareto 单点必填。
    if (m_deps.checkerDeps == nullptr || m_deps.projector == nullptr
        || m_deps.paretoBuilder == nullptr) {
        throw std::invalid_argument(
            "JointRunOrchestrator 装配违约：deps 必填项为空（checkerDeps/"
            "projector/paretoBuilder 非空）");
    }
    // 内层依赖面自身校验提前到构造（JointLayeredChecker 构造同款——并行
    // 路径下每 worker 的 checker 构造在 run 内进行，装配缺陷在编排构造期
    // 就暴露，不带病进入并行段）。
    if (m_deps.checkerDeps->evaluators == nullptr
        || m_deps.checkerDeps->layerSlices == nullptr
        || m_deps.checkerDeps->producers == nullptr
        || m_deps.checkerDeps->profiles == nullptr) {
        throw std::invalid_argument(
            "JointRunOrchestrator 装配违约：checkerDeps 必填项为空（evaluators/"
            "layerSlices/producers/profiles 非空）");
    }
}

JointRunRecord
JointRunOrchestrator::evaluateCandidate(const JointRunRequest& request,
                                        core::EvaluationMode jointMode,
                                        const CandidatePatch& patch,
                                        JointLayeredChecker& checker,
                                        evidence::IEvaluationContext& ctx) const
{
    // 候选身份（§4.2 公式——同基线同补丁必同身份，跨运行稳定）。
    const CandidateId candidateId
        = candidateIdOf(request.baselineRoot, request.baselineCv, patch);
    const bool isBaseline = patch.items.empty();  // 空补丁＝基线候选（§8.2/§5.5）

    // ---- 内层联合闭环（T03 单点——六层执行序＋联合硬约束汇总；候选级
    // 失败走结构化返回，编排器零判定复制 PA-1/N8）。
    JointLayeredCheckInput input;
    input.candidateId = candidateId;
    input.isBaseline = isBaseline;
    input.patch = patch;
    input.bindings = request.config.variables;
    input.config = request.config;
    input.jointMode = jointMode;
    input.snapshot = request.snapshot;
    input.task = request.task;
    // 覆盖矩阵（EVI-02 分母）两相同一投影：联合管线的 Quick 是内层六层中
    // 的筛选级〔trj/dyn Quick〕，不是覆盖降级——全必验分母两相一致。
    input.coverage = request.coverage;
    input.regionCoverages = request.regionCoverages;

    JointLayeredCheckResult check = checker.check(input, ctx);

    // ---- 八项指标全量（投影缝——P-OPT-2 裁决前由调用方/测试供给）。
    const JointCandidateFacts facts
        = m_deps.projector->project(patch, request.config.variables);
    const StaticMetricResult metrics = computeJointMetrics(m_stage, facts);

    JointRunRecord record;
    record.base.candidateId = candidateId;
    record.base.sliceId = check.facadeSliceId;  // 门面切片身份（提前终止为保留值）
    record.base.isBaseline = isBaseline;
    record.base.patch = patch;
    record.base.mode = jointMode;
    record.base.screeningOnly = (jointMode == core::EvaluationMode::Quick);
    record.base.status = check.record.status;
    record.base.engineeringStatus = check.record.engineeringStatus;
    record.base.formalPassEligible = check.record.formalPassEligible;
    record.base.formalPassUnmetConditions = check.record.formalPassUnmetConditions;
    record.base.evaluation = std::move(check.record);
    record.base.metrics = metrics;
    record.layers = std::move(check.layers);
    record.allLayersCompleted = check.allLayersCompleted;
    record.facadeSliceId = check.facadeSliceId;
    record.base.cacheHit = false;  // I-J5：缓存消费位如实为零查找（不伪造命中）

    // ---- 激活目标槽位缺失的 Feasible → DataInsufficient（§7.4"缺失指标
    // 候选不参与支配比较"的编排侧执行点——T06 同款；绝不冒充 Infeasible，
    // ERR-01"指标缺失不是失败"）。
    if (record.base.status == CandidateStatus::Feasible
        && !activeMetricsComplete(record.base.metrics, request.config.objectives)) {
        record.base.status = CandidateStatus::DataInsufficient;
        record.base.evaluation.status = CandidateStatus::DataInsufficient;  // 双字段一致
    }
    return record;
}

JointRunResult JointRunOrchestrator::run(const JointRunRequest& request,
                                         evidence::IEvaluationContext& ctx)
{
    // ---- 第 1 步：请求校验（调用方契约违约 fail-fast——确定性首错）。
    validateConfiguration(request.config);  // StageD 并行面解锁后的同一校验单点
    if (m_stage != OptimizationStage::StageD) {
        // 阶段锁（§5.7/§6.5——运行启动阻塞非候选淘汰；R1 面归两级编排，
        // 双编排同一码面）。
        throw OptimizationError(kOptStageLocked,
                                "联合两级编排仅实现 stage-d（R1 面归两级编排）——"
                                "运行启动阻塞（§6.5）");
    }
    if (request.config.stage != OptimizationStage::StageD) {
        throw OptimizationError(kOptStageLocked,
                                "请求 config.stage 非 stage-d——联合编排与配置阶段"
                                "不一致（装配违约，§5.7）");
    }
    if (!request.baselineRoot.isValid() || !request.baselineCv.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: baselineRoot/baselineCv 非法"
                                "（CandidateId 公式输入须非保留值）");
    }
    if (!request.task.isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: task 五元组非法（③端口绑定面）");
    }
    // 恢复基点结构核对（宿主侧已过 judgeCheckpointCompatibility 兼容判定
    // ——此处只做编排侧结构一致性）。
    if (request.resumeFrom.has_value() && !request.resumeFrom->isValid()) {
        throw OptimizationError(kOptInputInvalid,
                                "OPT-INPUT-INVALID: 恢复基点结构非法");
    }

    JointRunResult result;

    // ---- 第 2 步：候选生成与去重（生成唯一经 generateStageDJointCandidates
    // 单点——基线恒批首；§8.3 第 11 步同 CandidateId 去重保留首次）。
    result.runPhase = RunPhase::QuickScreening;
    const std::vector<CandidatePatch> generated
        = generateStageDJointCandidates(request.config, request.combinations);
    result.audit.base.candidatesGenerated
        = static_cast<std::uint32_t>(generated.size());

    std::vector<const CandidatePatch*> deduped;   // 去重后候选序列（生成序）
    std::vector<CandidateId> dedupedIds;          // 平行身份表（批划分与幸存集锚）
    {
        std::vector<CandidateId> seen;
        seen.reserve(generated.size());
        for (const auto& patch : generated) {
            const CandidateId id
                = candidateIdOf(request.baselineRoot, request.baselineCv, patch);
            const bool duplicate
                = std::any_of(seen.begin(), seen.end(),
                              [&](const CandidateId& s) { return s == id; });
            if (duplicate) {
                result.audit.candidatesDeduped += 1;  // §8.3 第 11 步（保留首次）
                continue;
            }
            seen.push_back(id);
            deduped.push_back(&patch);
            dedupedIds.push_back(id);
        }
    }
    if (deduped.empty()) {
        // 去重后空集＝编排不变量破坏（预算≥1 已校验——至少基线候选）
        // ——fail-fast 不静默产出空运行。
        throw OptimizationError(kOptSearchEmpty,
                                "OPT-SEARCH-EMPTY: 候选去重后为空集（生成面缺陷"
                                "——预算≥1 保证至少基线候选，编排不变量破坏）");
    }

    // 批划分（I-J1：固定容量常数——与线程数无关，末批可短）。
    auto partition = [](std::uint32_t totalCount) {
        std::vector<std::vector<std::uint32_t>> batches;
        for (std::uint32_t i = 0; i < totalCount; i += kBatchCapacity) {
            std::vector<std::uint32_t> batch;
            const std::uint32_t end = std::min(i + kBatchCapacity, totalCount);
            for (std::uint32_t j = i; j < end; ++j) {
                batch.push_back(j);
            }
            batches.push_back(std::move(batch));
        }
        return batches;
    };
    const std::uint32_t candidateCount = static_cast<std::uint32_t>(deduped.size());
    const std::vector<std::vector<std::uint32_t>> quickBatches = partition(candidateCount);
    const std::uint32_t quickBatchCount = static_cast<std::uint32_t>(quickBatches.size());

    // 恢复基点展开（空恢复＝空完成集＋零审计基点）。
    std::vector<std::uint32_t> completedBatches;  // 跨相累计完成批索引（升序）
    if (request.resumeFrom.has_value()) {
        completedBatches = request.resumeFrom->completedBatchIndices;
    }
    auto isLedgered = [&completedBatches](std::uint32_t batchIdx) {
        return std::binary_search(completedBatches.begin(), completedBatches.end(),
                                  batchIdx);
    };

    JointRunAuditCounts& audit = result.audit;
    if (request.resumeFrom.has_value()) {
        audit.base = request.resumeFrom->audit;  // 审计自检查点基点累计（AT-35）
    }

    std::vector<JointCheckpointState>& states = result.checkpointStates;
    std::uint64_t completedUnits = request.resumeFrom.has_value()
                                       ? request.resumeFrom->completedUnits
                                       : 0;  // 恢复基点累计（I-J4 单调）

    // ---- 批相推进引擎（Quick/Verified 两相同型——单份循环体参数化）。
    // 语义（I-J2/I-J3）：worker 线程只产记录；编排线程按批序合并并记账
    // （重建批不计账）；每新完成批产出检查点状态（批边界——暂停确认边
    // 界素材，V12-06）；取消时产出当前状态并返回 false。
    //   jointMode    [in] 本相联合请求模式（Quick=筛选/Verified=正式面）
    //   phaseBatches [in] 本相批划分（局部批索引；全局索引＝基址＋局部）
    //   phaseBaseIdx [in] 本相全局批索引基址（Quick=0；Verified=quickBatchCount）
    //   sink         [out] 本相记录汇（quickRecords/verifiedRecords）
    //   totalUnits   [in] 本相检查点 totalUnits 语义（I-J4——Quick 相传 Q、
    //                     Verified 相传 Q+V）
    //   返回 false＝取消（runPhase/runCompleted 由调用方置终态后返回）。
    auto sweepPhase = [&](core::EvaluationMode jointMode,
                          const std::vector<std::vector<std::uint32_t>>& phaseBatches,
                          std::uint32_t phaseBaseIdx, std::vector<JointRunRecord>& sink,
                          std::uint64_t totalUnits) -> bool {
        if (phaseBatches.empty()) {
            return true;
        }
        const std::size_t workers
            = std::min<std::size_t>(std::max(1U, request.config.parallel.threadCount),
                                    phaseBatches.size());
        std::vector<std::vector<JointRunRecord>> perBatch(phaseBatches.size());
        std::atomic<std::size_t> nextBatch{0};
        std::atomic<bool> cancelFlag{false};

        // worker 主循环：原子领批→（批间）取消查询→批内候选串行推进（每
        // worker 持各自 JointLayeredChecker——卡 §6.7 SingleThread 口径；
        // 派发游标原子——批间不重叠，合并序与派发序解耦）。
        auto workerFn = [&]() {
            JointLayeredChecker checker(m_stage, *m_deps.checkerDeps);
            while (true) {
                if (cancelFlag.load(std::memory_order_acquire)) {
                    return;  // 已观测取消——不再领新批（在途批不受影响）
                }
                const std::size_t i = nextBatch.fetch_add(1);
                if (i >= phaseBatches.size()) {
                    return;  // 批已领尽
                }
                if (i > 0 && ctx.cancellationRequested()) {
                    // 批间协作取消（TASK-01——停止派发，已回传批保留；
                    // 在途批完成后照常合并，不失败在途任务）。
                    cancelFlag.store(true, std::memory_order_release);
                    return;
                }
                auto& batchSink = perBatch[i];
                batchSink.reserve(phaseBatches[i].size());
                for (const std::uint32_t candIdx : phaseBatches[i]) {
                    batchSink.push_back(
                        evaluateCandidate(request, jointMode, *deduped[candIdx], checker, ctx));
                }
            }
        };

        if (workers <= 1) {
            workerFn();  // 串行路径——与并行同代码同序（线程数无关性承载）
        } else {
            std::vector<std::thread> pool;
            pool.reserve(workers);
            for (std::size_t w = 0; w < workers; ++w) {
                pool.emplace_back(workerFn);
            }
            for (auto& t : pool) {
                t.join();
            }
        }

        // 合并（编排线程——批序即确定性序）＋记账＋检查点状态产出。
        bool canceled = false;
        for (std::size_t i = 0; i < phaseBatches.size(); ++i) {
            if (perBatch[i].empty()) {
                canceled = true;  // 取消后未派发批——其后批全为空
                continue;
            }
            const std::uint32_t globalIdx = phaseBaseIdx + static_cast<std::uint32_t>(i);
            const bool ledgered = !isLedgered(globalIdx);
            for (auto& rec : perBatch[i]) {
                if (ledgered) {
                    // 评估数记账（I-J3：重建批不计——账面已含，AT-35）。
                    if (jointMode == core::EvaluationMode::Quick) {
                        audit.base.quickEvaluated += 1;
                    } else {
                        audit.base.verifiedEvaluated += 1;
                    }
                }
                sink.push_back(std::move(rec));
            }
            if (ledgered) {
                // 新完成批：完成集插入＋检查点状态产出（批边界）。
                completedBatches.push_back(globalIdx);
                std::sort(completedBatches.begin(), completedBatches.end());
                completedUnits += 1;
                audit.batchesCompleted += 1;
                JointCheckpointState st;
                st.phase = jointMode;
                st.completedBatchIndices = completedBatches;
                // 待评估队列（§9.2 四要素之二）：本相批划分中尚未落在任何
                // 已完成批内的候选下标（升序——批划分确定性 ⇒ 队列确定性）。
                for (std::size_t b = i + 1; b < phaseBatches.size(); ++b) {
                    st.pendingCandidateIndices.insert(st.pendingCandidateIndices.end(),
                                                      phaseBatches[b].begin(),
                                                      phaseBatches[b].end());
                }
                std::sort(st.pendingCandidateIndices.begin(),
                          st.pendingCandidateIndices.end());
                st.completedUnits = completedUnits;
                st.totalUnits = totalUnits;  // I-J4 相内连续计数
                st.audit = audit.base;
                states.push_back(std::move(st));
                audit.checkpointStatesEmitted += 1;
            } else {
                audit.resumedBatchesSkipped += 1;  // 恢复基点已记账批（I-J3）
            }
        }
        if (canceled) {
            // 取消终态：产出当前检查点状态（宿主可写检查点——TASK-01 部分
            // 批保留＋§9.2 暂停中取消保检查点素材）。
            JointCheckpointState st;
            st.phase = jointMode;
            st.completedBatchIndices = completedBatches;
            st.completedUnits = completedUnits;
            st.totalUnits = totalUnits;
            st.audit = audit.base;
            states.push_back(std::move(st));
            audit.checkpointStatesEmitted += 1;
            return false;
        }
        return true;
    };

    // ---- 第 3 步：Quick 批相（jointMode=Quick——screening-only 效力面）。
    {
        const std::uint64_t totalUnits = quickBatchCount;  // I-J4：不预估 Verified 批数
        if (!sweepPhase(core::EvaluationMode::Quick, quickBatches, 0, result.quickRecords,
                        totalUnits)) {
            result.runPhase = RunPhase::Canceled;
            result.runCompleted = false;
            return result;
        }
    }

    // ---- 第 4 步：Quick 保守筛选（§8.4 规则——基线恒占席＋稳定序补位）。
    std::vector<FeasibleCandidate> quickFeasible;
    for (const auto& rec : result.quickRecords) {
        if (rec.base.status == CandidateStatus::Feasible) {
            quickFeasible.push_back(toFeasibleCandidate(rec.base));
        }
    }
    std::vector<CandidateId> survivorIds;
    if (!quickFeasible.empty()) {
        // 稳定序筛选（buildFront 三键序——rank→目标声明序→CandidateId）。
        // 同一 Pareto builder 服务 Quick 筛选与 Verified 前沿——排序单点。
        const ParetoFrontResult front
            = m_deps.paretoBuilder->buildFront(quickFeasible, request.config.objectives);
        const std::uint32_t cap = request.config.budget.maxVerifiedCandidates;
        for (const auto& entry : front.entries) {
            if (entry.isBaseline) {
                survivorIds.push_back(entry.candidateId);
                break;  // 基线在去重集中至多一条（空补丁同身份天然去重）
            }
        }
        for (const auto& entry : front.entries) {
            if (static_cast<std::uint32_t>(survivorIds.size()) >= cap) {
                break;  // 预算线（§8.4 V1 上界刚性）
            }
            if (entry.isBaseline) {
                continue;  // 基线席已处理
            }
            const bool already = std::any_of(survivorIds.begin(), survivorIds.end(),
                                             [&](const CandidateId& id) {
                                                 return id == entry.candidateId;
                                             });
            if (!already) {
                survivorIds.push_back(entry.candidateId);
            }
        }
        // 未入选的 Quick-Feasible → ScreenedOut＋预算线淘汰原因（非 Feasible
        // 保持自有状态不冒充——§7.5 区分表）。
        for (auto& rec : result.quickRecords) {
            if (rec.base.status != CandidateStatus::Feasible) {
                continue;
            }
            const bool survivor = std::any_of(survivorIds.begin(), survivorIds.end(),
                                              [&](const CandidateId& id) {
                                                  return id == rec.base.candidateId;
                                              });
            if (!survivor) {
                rec.base.status = CandidateStatus::ScreenedOut;
                rec.base.evaluation.status = CandidateStatus::ScreenedOut;  // 双字段一致
                rec.base.extraRejections.push_back(makeQuickScreenRejection(m_stage));
                audit.base.quickScreenedOut += 1;
            }
        }
    }

    // ---- 第 5 步：幸存集为空 → 搜索空（OPT-VER-120——Completed 正常完成
    // 非任务不可行；不产 Pareto）。
    if (survivorIds.empty()) {
        result.runPhase = RunPhase::Completed;
        result.runCompleted = true;
        result.searchEmpty = true;
        result.searchEmptyToken = std::string(kOptSearchEmpty);
        return result;
    }

    // ---- 第 6 步：Verified 批相（幸存集按生成序分批——批索引基址
    // quickBatchCount；恢复：已记账 Verified 批重建不计账——I-J3）。
    std::uint32_t verifiedBatchCount = 0;
    {
        std::vector<std::uint32_t> survivorIdxs;  // 幸存者在去重序列中的下标（生成序）
        for (std::uint32_t c = 0; c < candidateCount; ++c) {
            const bool survivor
                = std::any_of(survivorIds.begin(), survivorIds.end(),
                              [&](const CandidateId& id) { return id == dedupedIds[c]; });
            if (survivor) {
                survivorIdxs.push_back(c);
            }
        }
        const std::vector<std::vector<std::uint32_t>> verifiedBatches
            = partition(static_cast<std::uint32_t>(survivorIdxs.size()));
        verifiedBatchCount = static_cast<std::uint32_t>(verifiedBatches.size());

        // 幸存集局部下标 → 去重序列下标的批划分（worker 按 localIdx 取候选）。
        std::vector<std::vector<std::uint32_t>> localBatches;
        localBatches.reserve(verifiedBatches.size());
        for (const auto& vb : verifiedBatches) {
            std::vector<std::uint32_t> lb;
            lb.reserve(vb.size());
            for (const std::uint32_t v : vb) {
                lb.push_back(survivorIdxs[v]);
            }
            localBatches.push_back(std::move(lb));
        }

        const std::uint64_t totalUnits
            = static_cast<std::uint64_t>(quickBatchCount) + verifiedBatchCount;  // I-J4
        if (!sweepPhase(core::EvaluationMode::Verified, localBatches, quickBatchCount,
                        result.verifiedRecords, totalUnits)) {
            result.runPhase = RunPhase::Canceled;
            result.runCompleted = false;
            return result;
        }
        audit.batchesTotal = quickBatchCount + verifiedBatchCount;
    }

    // ---- 第 7 步：可行集→Pareto（仅 Verified-Feasible 且激活指标完整候选
    // ——Quick 记录绝不进入本输入的类型学屏障，EVI-01/P-EV-8）。
    std::vector<FeasibleCandidate> verifiedFeasible;
    for (const auto& rec : result.verifiedRecords) {
        if (rec.base.status == CandidateStatus::Feasible) {
            verifiedFeasible.push_back(toFeasibleCandidate(rec.base));
        }
    }
    if (!verifiedFeasible.empty()) {
        result.pareto = m_deps.paretoBuilder->buildFront(verifiedFeasible,
                                                         request.config.objectives);
    }
    if (result.pareto.feasibleIds.empty()) {
        // 复核批无可行候选 → 搜索空（OPT-VER-120 语义——Completed 正常完成）。
        result.searchEmpty = true;
        result.searchEmptyToken = std::string(kOptSearchEmpty);
    }
    result.runPhase = RunPhase::Completed;
    result.runCompleted = true;
    return result;
}

}  // namespace sdurws::ird::optimization
