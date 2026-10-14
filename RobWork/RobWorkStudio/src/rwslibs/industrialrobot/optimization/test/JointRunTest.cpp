/**
 * @file   JointRunTest.cpp
 * @brief  联合运行编排模型测试（OptJointRun 组）——八项指标全量可算
 *         （OPT-VER-210~214）、联合硬约束逐域逐约束记录（OPT-VER-209）、
 *         并行分片/稳定合并/归约等价（OPT-VER-220~222——NFR-COR-02 与
 *         线程数无关）、检查点域 codec 与恢复统计不重复（OPT-VER-223~
 *         227——AT-35 核心验收；真 execution::CheckpointCoordinator 集成）
 *         ——任务契约 WP-21-T04 acceptance 1~3。
 *
 * 设计依据：
 *   - units/optimization.md §7.1 行 4~8（D-only 五项唯一数据来源口径）、
 *     §7.2（缺失"—"不显示零；非有限不静默转 0——NFR-COR-03）、§8.3 第
 *     11 步（CandidateId 合并去重）、§8.4（Quick 保守筛选——基线恒占席）、
 *     §9.2（并行分片——worker 内线程数来自 config.opt 入身份；检查点域
 *     中间状态 codec magic IRDOPTC1；completedUnits/totalUnits＝候选批
 *     计数〔续跑统计基点〕；恢复不兼容必须拒绝〔P-EX-9〕）、§9.3（恢复
 *     不重复统计——AT-35）、§13.2（OPT-VER-209/210~214/220~227 用例矩阵）
 *   - 需求 OPT-03（联合硬约束——各域 Must 违例 ⇒ Infeasible 逐域逐约束
 *     记录互不覆盖）、OPT-06 D 部分（完整并行评估与检查点恢复）、
 *     NFR-COR-02（等价候选集合与稳定排序与线程数无关；并行归约满足
 *     附录 D 第 8 项相对 1×10⁻¹² 逐元素）、AT-35（并行与检查点恢复统计
 *     不重复）
 *   - 附录 D 第 8 项（并行归约容差 1×10⁻¹²——通用比较公式逐元素判定）
 *
 * 测试形态（模型测试＝直调计算库，NFR-MNT-01）：内层评估器以脚本化替身
 * 注册进**真 evidence::EvaluatorRegistry**（JointStrategyTest 同款形态，
 * 并发安全加固——并行用例多线程派发；共享观测面〔派发日志/请求序日志〕
 * 全部"数据与锁捆绑于同一对象"，写入方与快照方共用同一把互斥锁——
 * WP-21-T04 attempt 1 验收 B-1 返工：四个域脚本各持自有脚本锁并发写
 * 同一日志 vector 曾构成数据竞争〔非确定性 SIGSEGV〕，此处不可回退）；
 * 指标投影以无状态替身注入（P-OPT-2
 * 裁决前接缝）；检查点集成用例以**真 execution::CheckpointCoordinator**
 * ＋写真实临时文件的归档端口替身（CheckpointCacheTest FakeArchiveDisk
 * 同款形态——EX-CKP 观测面）承载宿主（派发编排）角色：本域只产中间状态
 * 字节、消费恢复规格（N8——磁盘写出/装载/摘要全归 execution）。
 *
 * 时钟纪律（testkit §6.5）：检查点集成用例注入固定时钟，不 sleep。
 */

#include <sdurws/ird/optimization/JointRun.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/execution/Checkpoint.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>   // kOptProfileId——opt Profile 域 id 常量
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateStatus;
using optimization::JointCandidateFacts;
using optimization::JointCheckpointState;
using optimization::JointLayeredCheckDeps;
using optimization::JointMetricFacts;
using optimization::JointRunOrchestrator;
using optimization::JointRunOrchestratorDeps;
using optimization::JointRunRecord;
using optimization::JointRunRequest;
using optimization::JointUpstreamRef;
using optimization::MetricId;
using optimization::OptimizationConfiguration;
using optimization::OptimizationError;
using optimization::OptimizationStage;
using optimization::RunPhase;
using optimization::StaticMetricFacts;
using optimization::StaticMetricResult;

namespace fs = std::filesystem;

namespace {

// =====================================================================
// 通用替身与构造辅助（JointStrategyTest 同款形态＋并发安全加固）
// =====================================================================

/// 修订闭包来源替身（快照组装协议——全部 (oid,cv) 声明在册）。
class AllPresentClosure final : public evidence::IRevisionClosureSource {
public:
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 评估调用上下文替身（零取消/零进度——并发安全：只读恒 false）。
class NopEvaluationContext final : public evidence::IEvaluationContext {
public:
    bool cancellationRequested() const override { return false; }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }
};

/// 第 N 次查询后取消的上下文替身（批间协作取消——TASK-01；并发安全：
/// 原子计数）。
class CancelAfterBatchesContext final : public evidence::IEvaluationContext {
public:
    explicit CancelAfterBatchesContext(std::uint32_t afterQueries)
        : m_after(afterQueries)
    {
    }
    bool cancellationRequested() const override
    {
        return m_queries.fetch_add(1) >= m_after;  // 第 m_after+1 次起 true
    }
    void reportProgress(std::uint8_t, std::string_view) override {}
    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId, core::ContentVersion) const override
    {
        return std::nullopt;
    }

private:
    std::uint32_t m_after;                    ///< 放行查询数（之后即取消）
    mutable std::atomic<std::uint32_t> m_queries{0}; ///< 已发生查询数
};

/// 第 N 次查询后置暂停的信号替身（批间用户暂停——§9.2/WP-21-T07；并发
/// 安全：原子计数。接口契约"置位后不回退"由单调计数天然满足）。
class PauseAfterBoundariesSignal final : public optimization::IPauseSignal {
public:
    explicit PauseAfterBoundariesSignal(std::uint32_t afterQueries)
        : m_after(afterQueries)
    {
    }
    bool pauseRequested() const override
    {
        return m_queries.fetch_add(1) >= m_after;  // 第 m_after+1 次起 true
    }
    /// 已发生查询数（断言面——批边界查询确实发生）。
    std::uint32_t queries() const noexcept
    {
        return m_queries.load(std::memory_order_acquire);
    }

private:
    std::uint32_t m_after;                    ///< 放行查询数（之后即暂停）
    mutable std::atomic<std::uint32_t> m_queries{0}; ///< 已发生查询数
};

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 构造冻结快照（SnapshotBuilder 唯一合法生产者——身份为 builder 计算值）。
evidence::AnalysisSnapshot makeSnapshot(const std::vector<evidence::CaseEntry>& cases)
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    evidence::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000a1");
    design.objectTypeToken = "robot-design";
    design.digest = design.contentVersion.bytes;
    builder.addObjectRef(design);
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : cases) {
        builder.addCase(c);
    }
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    return builder.build(AllPresentClosure{});
}

/// 两工况（均 enabled+mandatory——覆盖矩阵分母）。
std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

/// 覆盖矩阵（全必验工况执行——EVI-02 完备分母投影）。
evidence::CaseCoverageMatrix makeCoverage(const evidence::AnalysisSnapshot& snapshot)
{
    evidence::CaseCoverageMatrix matrix;
    matrix.requiredCaseSetId = snapshot.caseSet.requiredCaseSetId;
    for (const auto& entry : snapshot.caseSet.entries) {
        evidence::CaseCoverageEntry row;
        row.caseId = entry.caseId;
        row.status = evidence::CaseExecutionStatus::Executed;
        row.runId = core::RunId::generate();
        row.resultSliceId = makeContentIdentity(6);
        matrix.entries.push_back(std::move(row));
    }
    return matrix;
}

/// 单连续绑定（传动比，无量纲——§5.3 #9 唯一 StageD 启用的连续条目；
/// 采样维度使 LHS 候选互异）。
optimization::VariableBinding makeLengthBinding()
{
    optimization::VariableBinding b;
    b.bindingId = "mdl.drivetrain.ratio[1]";
    b.kind = optimization::VariableKind::Continuous;
    b.unit = core::UnitToken::find("1").value();
    b.lowerBound = 0.8;    // 无量纲（比值 >0 且有限——I-MDL-11）
    b.upperBound = 1.2;    // 无量纲
    b.defaultValue = 1.0;  // 无量纲
    b.authorized = true;
    b.locked = false;
    b.authorityFieldPath = "robot-design/joints[2]/dh/a";
    b.diagSubject = core::ObjectId::generate().toCanonical();
    return b;
}

/// StageD 已校验配置（八项目标默认激活＋并行度可调——R2 解锁面；单连续
/// 采样维——去重后候选数＝maxCandidates，批划分多批可观测）。
OptimizationConfiguration makeStageDConfig(std::uint32_t threadCount = 1,
                                           std::uint64_t seed = 7,
                                           std::uint32_t maxCandidates = 9)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageD;
    c.seed = seed;
    c.parallel.threadCount = threadCount;  // R2 并行度（入 config.opt canonical）
    c.budget.maxCandidates = maxCandidates;
    c.budget.maxVerifiedCandidates = 4;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageD);
    c.variables = {makeLengthBinding()};
    optimization::validateConfiguration(c);
    return c;
}

/// 基线根身份（CandidateId 公式输入——非保留值）。
CandidateId baselineCandidateId()
{
    return optimization::candidateIdOf(
        core::ObjectId::generate(),
        core::ContentVersion::fromCanonical(
            "cv-00000000000000000000000000000000000000000000000000000000000000cc"),
        CandidatePatch{});
}

/// 完整八项事实投影值（指标计算断言的期望底座——确定性数值）。
JointCandidateFacts makeFullFacts()
{
    JointCandidateFacts facts;
    StaticMetricFacts& s = facts.staticFacts;
    s.envelope = optimization::EnvelopeFacts{0.0, 0.0, 0.0, 0.5, 0.4, 0.3};  // 和＝1.2 m
    s.linkMasses.push_back({"obj-link-1", 10.0, "provided"});                 // 合成 30 kg
    s.linkMasses.push_back({"obj-link-2", 20.0, "provided"});
    s.pointMargins.push_back({"case-1", 0.9, "1"});                           // 最小 0.7
    s.pointMargins.push_back({"case-2", 0.7, "1"});
    facts.jointFacts.cycleTimeS = 12.5;            // s（trj 层投影）
    facts.jointFacts.deviceCost = 8500.0;          // 目录货币单位（sel 层投影）
    facts.jointFacts.deviceMassKg = 45.5;          // kg（sel 层投影）
    facts.jointFacts.jointPositiveWorkJ = 320.75;  // J（dyn 层投影）
    facts.jointFacts.minDriveMargin = 1.8;         // 无量纲（sel 层投影）
    return facts;
}

/// 无状态指标投影替身（P-OPT-2 缝——全候选同事实；const 只读＝并发安全）。
class StubProjector final : public optimization::IJointMetricProjector {
public:
    explicit StubProjector(JointCandidateFacts facts)
        : m_facts(std::move(facts))
    {
    }
    JointCandidateFacts project(const CandidatePatch&,
                                const std::vector<optimization::VariableBinding>&) const override
    {
        return m_facts;
    }

private:
    JointCandidateFacts m_facts;  ///< 全候选同一投影事实（值语义）
};

// ---- ③端口脚本化内层评估器（线程安全版——并行用例多线程派发）----

/// 一次层派发的观测（评估键＋请求模式）。
struct DispatchObservation {
    std::string evaluationKey;
    core::EvaluationMode mode;
};

/// 派发日志槽（fixture 单例）——观测 vector 与其互斥锁**结构性捆绑**。
///
/// 并发契约（WP-21-T04 attempt 1 验收 B-1 返工点）：threadCount=4 时
/// 不同 worker 经**不同**域脚本并发派发，若只依赖各脚本自有的
/// InnerScript::mutex，则四把不同的锁保护同一非线程安全 vector＝零真实
/// 互斥，并发 push_back 造成堆损坏（曾致并行用例非确定性 SIGSEGV）。
/// 修复后所有写入方（各域 evaluate）与快照方（JointFixture::logSnapshot）
/// 串行化于本槽内唯一一把锁；锁序固定为 InnerScript::mutex（外）→
/// 本锁（叶）——本锁持有期间绝不反向获取脚本锁，无死锁环。
struct DispatchLogSink {
    mutable std::mutex mutex;                  ///< 写入/快照唯一互斥（叶锁）
    std::vector<DispatchObservation> entries;  ///< 派发序观测记录（非线程安全，靠本槽锁）
};

/// 内层脚本状态（并发安全：脚本锁只保护脚本字段读取；派发日志写归槽锁）。
struct InnerScript {
    mutable std::mutex mutex;                        ///< 同域并发派发互斥（脚本字段读取面）
    evidence::DomainVerdictInputs mustScript;        ///< Must 违例脚本（非空即随产出返回）
    std::optional<evidence::SearchExhaustedRecord>
        searchScript;                                ///< 搜索未果脚本（有值即返回）
    bool failScript = false;                         ///< 失败脚本（true 即抛 EvidenceError）
    DispatchLogSink* log = nullptr;                  ///< 派发日志槽（fixture 持有；写入持槽内共享锁）
    std::string evaluationKey;                       ///< 本域评估键
};

/// 脚本化内层评估器（记录派发〔脚本锁＋日志槽锁——B-1 返工双锁序〕；
/// 按共享脚本返回违例/搜索未果/抛错）。
class ScriptedInnerEvaluator final : public evidence::IEngineeringEvaluator {
public:
    ScriptedInnerEvaluator(evidence::EvaluatorDescriptor descriptor,
                           const InnerScript& script)
        : m_descriptor(std::move(descriptor))
        , m_script(script)
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest& request,
                                        evidence::IEvaluationContext&) override
    {
        // 本锁保护脚本字段读取（同域并发派发串行化）。
        std::lock_guard<std::mutex> lock(m_script.mutex);
        if (m_script.log != nullptr) {
            // 派发日志写入必须持槽内共享锁：日志槽是全 fixture 单例，本脚本
            // 的脚本锁只互斥"同域"派发；threadCount=4 下跨域并发（四域各持
            // 不同脚本锁）只能靠槽锁串行化——attempt 1 B-1 数据竞争根因即
            // "写同一 vector 却只锁各自脚本锁"，此处锁面不可回退为脚本锁。
            // 锁序：脚本锁（外）→ 槽锁（叶），槽锁内不取脚本锁，无死锁环。
            std::lock_guard<std::mutex> logLock(m_script.log->mutex);
            m_script.log->entries.push_back({m_descriptor.key, request.mode});
        }
        if (m_script.failScript) {
            // 脚本化注入：环境轨失败（EvidenceError——I-C2 错误轨）。
            throw evidence::EvidenceError(evidence::EvidenceErrorCode::EvidenceMissing,
                                          "脚本化注入：内层评估失败（环境轨）");
        }
        evidence::EvaluationOutput output;
        output.verdictInputs = m_script.mustScript;
        output.searchRecord = m_script.searchScript;
        return output;
    }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor 引用所指）
    const InnerScript& m_script;                ///< 共享脚本（fixture 持有，非拥有）
};

/// 脚本化内层评估器工厂（create 每次新建实例——独占所有权契约）。
class ScriptedInnerFactory final : public evidence::IEvaluatorFactory {
public:
    ScriptedInnerFactory(evidence::EvaluatorDescriptor descriptor, const InnerScript& script)
        : m_descriptor(std::move(descriptor))
        , m_script(script)
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<ScriptedInnerEvaluator>(m_descriptor, m_script);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（注册面）
    const InnerScript& m_script;                ///< 共享脚本（非拥有）
};

/// 脚本化层切片供给（并发安全：互斥保护请求序日志；切片构建用局部
/// SliceBuilder——无共享可变状态）。
class ScriptedLayerSlices final : public optimization::IJointLayerSliceProvider {
public:
    const evidence::AnalysisSnapshot* snapshot = nullptr;  ///< 冻结快照（build 基准）
    mutable std::mutex mutex;                              ///< 并行请求互斥
    mutable std::vector<std::string> layerTokens;          ///< 请求序（断言面）

    evidence::InputSlice layerSlice(const std::string& layerToken,
                                    const std::string& evaluationKey,
                                    const CandidatePatch&,
                                    const std::vector<optimization::VariableBinding>&,
                                    const std::vector<JointUpstreamRef>& upstreams) const override
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            layerTokens.push_back(layerToken);
        }
        evidence::SliceBuilder builder;
        builder.setEvaluation(evaluationKey, 1U);
        // 探针条目：定位 token＋上游引用数＋末条上游 sliceId——层序与上游
        // 状态变化都反映进切片身份（测试面互异性；生产适配器按各域
        // descriptor 组装真实条目——本替身只保证可冻结可区分）。
        evidence::DependencyEntry probe;
        probe.key = "test.layer-probe";
        probe.kind = evidence::DependencyKind::Environment;
        evidence::EnvironmentDependencyPayload payload;
        payload.token = "test.layer-probe";
        std::string value = layerToken + "|" + std::to_string(upstreams.size());
        if (!upstreams.empty()) {
            value += "|" + upstreams.back().upstreamSliceId.toCanonical();
        }
        payload.valueToken = value;
        probe.payload = payload;
        builder.addEntry(std::move(probe));
        return builder.build(*snapshot);
    }
};

/// 联合编排完整依赖面（真注册表＋opt Profile＋脚本化内层评估器组——
/// 并发安全加固版）。
struct JointFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    DispatchLogSink dispatchLog;               ///< 派发日志槽（锁与 vector 捆绑——B-1 返工单锁化）
    ScriptedLayerSlices layerSlices;
    NopEvaluationContext ctx;
    core::ContentIdentity optProfileIdentity;  ///< 注册权威值（构造期回填）
    evidence::AnalysisSnapshot snapshotHolder; ///< 冻结快照（fixture 单例）

    JointFixture()
        : snapshotHolder(makeSnapshot(makeTwoMandatoryCases()))
    {
        layerSlices.snapshot = &snapshotHolder;
        // Profile 注册在前（evidence §13 时序——opt 域空必需项集）。
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = std::string(optimization::kOptProfileId);
        profile.version = "1.0";
        profiles.registerProfile(profile);
        optProfileIdentity
            = profiles.findProfile(std::string(optimization::kOptProfileId), "1.0")
                  ->contentIdentity;
        // 四内层评估器注册（各域登记键——kebab 落位偏差消费拼写）。
        for (const auto* key :
             {&optimization::kInnerTrjPlanKey, &optimization::kInnerDynRneaKey,
              &optimization::kInnerDtMappingKey,
              &optimization::kInnerSelCombinationKey}) {
            auto script = std::make_unique<InnerScript>();
            script->log = &dispatchLog;
            script->evaluationKey = std::string(*key);
            evidence::EvaluatorDescriptor d;
            d.key = std::string(*key);
            d.contractVersion = 1U;
            d.profile.profileId = std::string(optimization::kOptProfileId);
            d.profile.version = "1.0";
            d.supportedModes = {core::EvaluationMode::Quick,
                                core::EvaluationMode::Verified};
            d.stateless = true;
            d.threadSafety = evidence::ThreadSafety::SingleThread;
            evaluators.registerEvaluator(
                std::make_unique<ScriptedInnerFactory>(d, *script), {});
            registeredKeys.push_back(std::string(*key));
            scripts.push_back(std::move(script));
        }
    }

    /// 按评估键定位脚本（脚本注入面）。
    InnerScript& script(std::string_view key)
    {
        for (std::size_t i = 0; i < registeredKeys.size(); ++i) {
            if (registeredKeys[i] == key) {
                return *scripts[i];
            }
        }
        throw std::logic_error("fixture: 未注册评估器键 " + std::string(key));
    }

    /// 派发日志快照（与写入方共用槽内唯一锁拷贝——断言面）。
    std::vector<DispatchObservation> logSnapshot() const
    {
        std::lock_guard<std::mutex> lock(dispatchLog.mutex);
        return dispatchLog.entries;
    }

    /// 联合编排依赖面。
    JointLayeredCheckDeps checkerDeps()
    {
        JointLayeredCheckDeps d;
        d.evaluators = &evaluators;
        d.layerSlices = &layerSlices;
        d.producers = &evaluators;  // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;     // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }

    /// 就绪联合请求（快照/覆盖/任务面就绪＋候选预算——正例基线）。
    JointRunRequest readyRequest(const OptimizationConfiguration& config) const
    {
        JointRunRequest req;
        req.config = config;
        req.baselineRoot = core::ObjectId::generate();
        req.baselineCv = core::ContentVersion::fromCanonical(
            "cv-00000000000000000000000000000000000000000000000000000000000000cc");
        req.snapshot = snapshotHolder;
        req.task = {core::ProjectId::generate(), core::BranchId::generate(),
                    core::RevisionId::generate(), core::RunId::generate(),
                    core::AttemptId{1U}};
        req.coverage = makeCoverage(snapshotHolder);
        return req;
    }

    std::vector<std::string> registeredKeys;           ///< 注册序键表
    std::vector<std::unique_ptr<InnerScript>> scripts; ///< 脚本状态（fixture 持有）
};

// =====================================================================
// 检查点集成替身（CheckpointCacheTest FakeArchiveDisk 同款形态——写真实
// 临时文件；EX-CKP 观测面）
// =====================================================================

/// 结构化诊断收集 sink（观测面——不校验）。
class CollectingSink final : public execution::IExecutionDiagnosticsSink {
public:
    void report(const core::DiagnosticRecord& record) override
    {
        records.push_back(record);
    }
    void reportDev(const std::string& channel, const std::string& message) override
    {
        devLog.push_back({channel, message});  // 开发级自由文本收集（观测面）
    }
    std::vector<core::DiagnosticRecord> records; ///< 已上报诊断
    std::vector<std::pair<std::string, std::string>> devLog; ///< 开发级文本
};

/// 归档端口替身：begin 记录请求、writeBatch 把批次字节写进真实磁盘
/// runDir、finalize 写 manifest.json 标记"正式"。
class FakeArchiveDisk final : public project::IResultArchivePort {
public:
    int beginCalls = 0;                                   ///< begin 调用计数
    std::vector<fs::path> openDirs;                       ///< 最近 begin 目录

    project::ArchiveSessionRef begin(const project::ArchiveRequest& request) override
    {
        ++beginCalls;
        openDirs.push_back(request.runDir);
        // 默认空句柄＝本替身的自持令牌（协调器原样回传消费）。
        return project::ArchiveSessionRef{};
    }

    project::ArchiveStatus writeBatch(project::ArchiveSessionRef session,
                                      const project::ArchiveBatch& batch) override
    {
        (void)session;
        const fs::path dir = openDirs.back();
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec) {
            return project::ArchiveStatus{false,
                                          project::StoreError{project::StoreErrorCode::DiskFull,
                                                              "fake: 创建目录失败"}};
        }
        for (const project::ArchiveItem& item : batch.items) {
            std::ofstream out(dir / item.relPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(item.bytes.data()),
                      static_cast<std::streamsize>(item.bytes.size()));
            if (!out) {
                return project::ArchiveStatus{false,
                                              project::StoreError{project::StoreErrorCode::DiskFull,
                                                                  "fake: 写文件失败"}};
            }
        }
        return project::ArchiveStatus{true, std::nullopt};
    }

    project::ArchiveStatus finalize(project::ArchiveSessionRef session,
                                    const project::RunManifest& manifest) override
    {
        (void)session;
        (void)manifest;
        // manifest.json 原子发布的最小替身（存在性即"正式"标记）。
        const fs::path dir = openDirs.back();
        std::ofstream out(dir / "manifest.json", std::ios::binary);
        out << "{\"fake\":true}";
        return project::ArchiveStatus{true, std::nullopt};
    }

    void abandon(project::ArchiveSessionRef session,
                 project::ArchiveEndReason reason) override
    {
        (void)session;
        (void)reason;
    }
};

/// 取指定指标值或哨兵（" fascination —"比较辅助——nullopt 归一）。
std::string valueOrGap(const StaticMetricResult& r, MetricId id)
{
    const auto v = r.valueOf(id);
    if (v.has_value()) {
        char buf[32] = {};
        std::snprintf(buf, sizeof(buf), "%.17g", *v);
        return buf;
    }
    for (const auto& m : r.metrics) {
        if (m.metricId == id) {
            return std::string("gap:") + std::string(m.gapToken);
        }
    }
    return "missing-metric";
}

}  // namespace

// =====================================================================
// 八项指标全量可算（OPT-VER-210~214——acceptance 3）
// =====================================================================

TEST(OptJointRun, JointMetricsAllEightComputableInStageD_WP21T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07", "OPT-03"},
                  std::vector<std::string>{"AT-35"});

    // StageD：八项全量可算——前三项复用 T05 口径（期望值给解析计算式），
    // 后五项（节拍/成本/器件质量/正机械功/驱动裕量）取投影值（§7.1 行
    // 4~8 唯一来源口径——OPT-VER-210~214）。
    const auto facts = makeFullFacts();
    const StaticMetricResult r
        = optimization::computeJointMetrics(OptimizationStage::StageD, facts);
    ASSERT_EQ(r.metrics.size(), 8U);
    // 1 尺寸包络（m）＝三向尺寸之和 0.5+0.4+0.3＝1.2。
    EXPECT_EQ(valueOrGap(r, MetricId::Envelope), valueOrGap(r, MetricId::Envelope));
    EXPECT_NEAR(*r.valueOf(MetricId::Envelope), 1.2, 1e-12);
    // 2 结构质量（kg）＝10+20＝30（求和顺序＝投影表序——NFR-COR-02）。
    EXPECT_NEAR(*r.valueOf(MetricId::StructuralMass), 30.0, 1e-12);
    // 3 最小关节裕量＝min(0.9,0.7)＝0.7（最大化方向——§7.1 行 3）。
    EXPECT_NEAR(*r.valueOf(MetricId::MinJointMargin), 0.7, 1e-12);
    // 4 节拍（s）＝trj 层 totalDurationS 投影（含驻留）。
    EXPECT_NEAR(*r.valueOf(MetricId::CycleTime), 12.5, 1e-12);
    // 5 器件成本＝sel 层 SEL-06 事实投影。
    EXPECT_NEAR(*r.valueOf(MetricId::DeviceCost), 8500.0, 1e-12);
    // 6 器件质量（kg）＝sel 层目录质量合成投影。
    EXPECT_NEAR(*r.valueOf(MetricId::DeviceMass), 45.5, 1e-12);
    // 7 正机械功（J）＝dyn 层 positiveEnergyJ 投影（E⁺ 完整循环含驻留）。
    EXPECT_NEAR(*r.valueOf(MetricId::JointPositiveWork), 320.75, 1e-12);
    // 8 最小驱动裕量（无量纲）＝sel 层裕量事实投影。
    EXPECT_NEAR(*r.valueOf(MetricId::MinDriveMargin), 1.8, 1e-12);
}

TEST(OptJointRun, JointMetricsMissingFactsStayGapNeverZero_WP21T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07"}, std::vector<std::string>{});

    // D 事实缺失 ⇒ 该指标"—"（SourceMissing）——绝不输出 0 冒充
    // （NFR-COR-03/§7.1 缺失语义列：无时间参数化/无可行组合不伪造数值）。
    auto facts = makeFullFacts();
    facts.jointFacts = JointMetricFacts{};  // 五项全缺
    const StaticMetricResult r
        = optimization::computeJointMetrics(OptimizationStage::StageD, facts);
    for (const MetricId id :
         {MetricId::CycleTime, MetricId::DeviceCost, MetricId::DeviceMass,
          MetricId::JointPositiveWork, MetricId::MinDriveMargin}) {
        EXPECT_FALSE(r.valueOf(id).has_value())
            << "缺失事实不得产出数值（指标 " << optimization::toToken(id) << "）";
        EXPECT_EQ(valueOrGap(r, id),
                  "gap:opt.metric-gap.source-missing")
            << "缺失原因须 SourceMissing（指标 " << optimization::toToken(id) << "）";
    }
    // 前三项不受 D 事实缺失影响（投影面分立）。
    EXPECT_NEAR(*r.valueOf(MetricId::Envelope), 1.2, 1e-12);
}

TEST(OptJointRun, JointMetricsNonFiniteStaysGap_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"}, std::vector<std::string>{});

    // 投影值非有限 ⇒ NonFiniteInput（§7.2：指标值非有限→候选评估失败
    // 处理——计算面如实标记，不静默转 0）。
    auto facts = makeFullFacts();
    facts.jointFacts.cycleTimeS = std::numeric_limits<double>::quiet_NaN();
    const StaticMetricResult r
        = optimization::computeJointMetrics(OptimizationStage::StageD, facts);
    EXPECT_FALSE(r.valueOf(MetricId::CycleTime).has_value());
    EXPECT_EQ(valueOrGap(r, MetricId::CycleTime), "gap:opt.metric-gap.non-finite");
    // 其余 D 指标不受污染（逐项独立）。
    EXPECT_NEAR(*r.valueOf(MetricId::DeviceCost), 8500.0, 1e-12);
}

TEST(OptJointRun, JointMetricsStageBKeepsStageNotComputable_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-07"}, std::vector<std::string>{});

    // StageB：后五项恒 StageNotComputable（§7.1"阶段可算"列——不估算，
    // T05 缺失语义零漂移）；前三项照常可算。
    const StaticMetricResult r
        = optimization::computeJointMetrics(OptimizationStage::StageB, makeFullFacts());
    EXPECT_NEAR(*r.valueOf(MetricId::Envelope), 1.2, 1e-12);
    for (const MetricId id :
         {MetricId::CycleTime, MetricId::DeviceCost, MetricId::DeviceMass,
          MetricId::JointPositiveWork, MetricId::MinDriveMargin}) {
        EXPECT_FALSE(r.valueOf(id).has_value());
        EXPECT_EQ(valueOrGap(r, id), "gap:opt.metric-gap.stage")
            << "StageB 缺失语义＝stage 类（指标 " << optimization::toToken(id) << "）";
    }
}

// =====================================================================
// 联合硬约束逐域逐约束记录（OPT-VER-209——acceptance 1）
// =====================================================================

TEST(OptJointRun, JointHardConstraintPerDomainRejections_WP21T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{"AT-35"});

    // 各域 Must 违例 ⇒ 候选 Infeasible；逐域逐约束记录互不覆盖：轨迹层
    // 与 selection 层同时注入 Must 违例，两条淘汰原因（sourceId 各含本层
    // 评估键＋违例项 id）并存在联合记录中。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    evidence::DomainVerdictInputs trjMust;
    evidence::DomainVerdictViolation trjV;
    trjV.itemId = "trj.must-segment-collision";
    trjV.detail = "轨迹段碰撞违例（脚本注入）";
    trjMust.mustViolations.push_back(trjV);
    evidence::DomainVerdictInputs selMust;
    evidence::DomainVerdictViolation selV;
    selV.itemId = "sel.must-combination-incompatible";
    selV.detail = "器件组合兼容违例（脚本注入）";
    selMust.mustViolations.push_back(selV);
    fx.script(optimization::kInnerTrjPlanKey).mustScript = trjMust;
    fx.script(optimization::kInnerSelCombinationKey).mustScript = selMust;

    const auto facts = makeFullFacts();
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{facts}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    auto req = fx.readyRequest(makeStageDConfig());
    const optimization::JointRunResult result = orch.run(req, fx.ctx);

    // 运行完成；候选判 Infeasible 不进可行集（搜索空语义——Completed 非
    // 任务不可行）。
    EXPECT_TRUE(result.runCompleted);
    EXPECT_EQ(result.runPhase, RunPhase::Completed);
    ASSERT_EQ(result.pareto.feasibleIds.size(), 0U);
    EXPECT_TRUE(result.searchEmpty);
    // 逐域逐约束：Quick＋Verified 两相各产生两条违例记录（层评估键分账）。
    const auto log = fx.logSnapshot();
    int trjViolations = 0;
    int selViolations = 0;
    for (const auto& phaseRecords : {std::cref(result.quickRecords),
                                     std::cref(result.verifiedRecords)}) {
        for (const auto& rec : phaseRecords.get()) {
            EXPECT_EQ(rec.base.status, CandidateStatus::Infeasible);
            for (const auto& rej : rec.base.evaluation.rejections) {
                // sourceId＝违例项 id（域词表指称——JointLayeredChecker 逐层
                // 逐约束分账）；subject＝违例文案。逐域各计一条即互不覆盖。
                if (rej.sourceId == "trj.must-segment-collision") {
                    trjViolations += 1;
                }
                if (rej.sourceId == "sel.must-combination-incompatible") {
                    selViolations += 1;
                }
            }
        }
    }
    EXPECT_GE(trjViolations, 1) << "轨迹域违例记录必须在场（逐域分账）";
    EXPECT_GE(selViolations, 1) << "selection 域违例记录必须在场（互不覆盖）";
}

// =====================================================================
// 并行分片/稳定合并/归约等价（OPT-VER-220/221/222——acceptance 2）
// =====================================================================

TEST(OptJointRun, ParallelShardEquivalenceAndStableMerge_WP21T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "NFR-COR-02"},
                  std::vector<std::string>{"AT-35"});

    // 双跑（threadCount=1 串行 vs threadCount=4 并行）：等价候选集合＋
    // 稳定排序＋逐指标 1×10⁻¹² 内等价（附录 D 第 8 项——通用比较公式
    // 逐元素判定；本编排零浮点重排，实测逐位相等）＋审计计数一致
    // （OPT-VER-220 分片不丢候选/221 稳定合并/222 归约容差）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    const auto facts = makeFullFacts();
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{facts}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    const auto serialCfg = makeStageDConfig(/*threadCount=*/1);
    const auto parCfg = makeStageDConfig(/*threadCount=*/4);
    auto serialReq = fx.readyRequest(serialCfg);
    auto parReq = fx.readyRequest(parCfg);
    // 同基线根身份（同候选集合的锚——CandidateId 公式输入一致）。
    parReq.baselineRoot = serialReq.baselineRoot;
    parReq.baselineCv = serialReq.baselineCv;
    parReq.coverage = serialReq.coverage;

    const optimization::JointRunResult serial = orch.run(serialReq, fx.ctx);
    const optimization::JointRunResult par = orch.run(parReq, fx.ctx);

    ASSERT_TRUE(serial.runCompleted);
    ASSERT_TRUE(par.runCompleted);
    // 并行分片覆盖全候选（OPT-VER-220）：批分配总 candidatesGenerated 一致、
    // 记录数一致（去重后候选全集）。
    EXPECT_EQ(serial.audit.base.candidatesGenerated, par.audit.base.candidatesGenerated);
    ASSERT_EQ(serial.quickRecords.size(), par.quickRecords.size());
    // 稳定合并（OPT-VER-221）：记录序逐位一致（候选生成序＝合并序——
    // 批序合并的确定性；NFR-COR-02 等价集合＋稳定排序与线程数无关）。
    for (std::size_t i = 0; i < serial.quickRecords.size(); ++i) {
        EXPECT_TRUE(serial.quickRecords[i].base.candidateId
                    == par.quickRecords[i].base.candidateId)
            << "记录序漂移（下标 " << i << "——并行合并必须与串行同序）";
        EXPECT_EQ(serial.quickRecords[i].base.status, par.quickRecords[i].base.status);
        // 归约容差（OPT-VER-222/附录 D 第 8 项）：逐指标 |a−b| ≤ 1e-12·|ref|
        // ＋ε_abs（ε_abs=0——同实现零重排，实测逐位相等）。
        for (int m = 0; m < static_cast<int>(optimization::kMetricCount); ++m) {
            const auto a = serial.quickRecords[i].base.metrics.valueOf(
                static_cast<MetricId>(m));
            const auto b = par.quickRecords[i].base.metrics.valueOf(
                static_cast<MetricId>(m));
            ASSERT_EQ(a.has_value(), b.has_value());
            if (a.has_value()) {
                const double ref = std::abs(*a);
                const double diff = std::abs(*a - *b);
                EXPECT_LE(diff, 1e-12 * ref)
                    << "归约超容差（指标 " << m << "，下标 " << i << "）";
            }
        }
    }
    // Pareto 前沿一致（等价集合＋稳定排序——NFR-COR-02 承载）。
    ASSERT_EQ(serial.pareto.entries.size(), par.pareto.entries.size());
    for (std::size_t i = 0; i < serial.pareto.entries.size(); ++i) {
        EXPECT_TRUE(serial.pareto.entries[i].candidateId == par.pareto.entries[i].candidateId);
    }
    // 审计计数与线程数无关（I-J2 记账纪律）。
    EXPECT_EQ(serial.audit.base.quickEvaluated, par.audit.base.quickEvaluated);
    EXPECT_EQ(serial.audit.base.verifiedEvaluated, par.audit.base.verifiedEvaluated);
}

// =====================================================================
// 检查点域 codec（OPT-VER-223/225 codec 面——acceptance 2）
// =====================================================================

TEST(OptJointRun, CheckpointCodecRoundTripAndRejects_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06"}, std::vector<std::string>{});

    // round-trip：encode→decode 逐字段还原（确定性 NFR-COR-02）。
    JointCheckpointState st;
    st.phase = core::EvaluationMode::Verified;
    st.completedBatchIndices = {0, 1, 3};
    st.pendingCandidateIndices = {5, 6, 8};
    st.generation = 0;
    st.completedUnits = 3;
    st.totalUnits = 5;
    st.audit.candidatesGenerated = 9;
    st.audit.quickEvaluated = 9;
    st.audit.quickScreenedOut = 5;
    st.audit.verifiedEvaluated = 2;
    const auto bytes = optimization::encodeJointCheckpointState(st);
    const auto back = optimization::decodeJointCheckpointState(bytes);
    EXPECT_EQ(back.codecVersion, st.codecVersion);
    EXPECT_TRUE(back.phase == st.phase);
    EXPECT_EQ(back.completedBatchIndices, st.completedBatchIndices);
    EXPECT_EQ(back.pendingCandidateIndices, st.pendingCandidateIndices);
    EXPECT_EQ(back.generation, st.generation);
    EXPECT_EQ(back.completedUnits, st.completedUnits);
    EXPECT_EQ(back.totalUnits, st.totalUnits);
    EXPECT_EQ(back.audit.candidatesGenerated, st.audit.candidatesGenerated);
    EXPECT_EQ(back.audit.quickEvaluated, st.audit.quickEvaluated);
    EXPECT_EQ(back.audit.quickScreenedOut, st.audit.quickScreenedOut);
    EXPECT_EQ(back.audit.verifiedEvaluated, st.audit.verifiedEvaluated);
    // 编码确定性：同状态任意成员录入序同字节（重编码逐位一致）。
    const auto bytes2 = optimization::encodeJointCheckpointState(back);
    EXPECT_TRUE(bytes == bytes2);

    // 拒绝面：魔数不符/截断/版本漂移一律 OPT-INPUT-INVALID（P-EX-9 不
    // 部分采纳不迁移）。
    auto expectThrows = [](const std::vector<std::uint8_t>& payload) {
        bool threw = false;
        try {
            (void)optimization::decodeJointCheckpointState(payload);
        } catch (const OptimizationError& e) {
            threw = true;
            EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
        }
        EXPECT_TRUE(threw) << "损坏载荷必须拒绝恢复";
    };
    std::vector<std::uint8_t> badMagic = bytes;
    badMagic[0] = 'X';
    expectThrows(badMagic);
    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + bytes.size() - 3);
    expectThrows(truncated);
    // 版本漂移（codec v2 载荷——旧版本不迁移，P-EX-9）。
    std::vector<std::uint8_t> drifted = bytes;
    drifted[8] = 2;
    drifted[9] = 0;
    drifted[10] = 0;
    drifted[11] = 0;
    expectThrows(drifted);
    // 结构非法（completedUnits>totalUnits）在编码端即拒（调用方契约）。
    JointCheckpointState bad = st;
    bad.completedUnits = 9;
    bad.totalUnits = 3;
    bool threw = false;
    try {
        (void)optimization::encodeJointCheckpointState(bad);
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(threw) << "结构非法状态必须编码拒绝";
}

// =====================================================================
// 检查点恢复统计不重复（OPT-VER-223/226/227——AT-35 核心，真
// CheckpointCoordinator 集成；acceptance 2/3）
// =====================================================================

/// 检查点集成夹具（临时目录＋固定时钟——CheckpointCoordinatorTest 同款）。
class JointCheckpointEnv {
public:
    JointCheckpointEnv()
    {
        std::error_code ec;
#ifdef _WIN32
        base = fs::temp_directory_path(ec) / "ird_opt_jointrun_ckpt"
               / std::to_string(::GetCurrentProcessId());
#else
        base = fs::temp_directory_path(ec) / "ird_opt_jointrun_ckpt";
#endif
        fs::create_directories(base, ec);
        caseDir = base / ("case" + std::to_string(++counter));
        fs::create_directories(caseDir, ec);
        port = std::make_unique<FakeArchiveDisk>();
        sink = std::make_unique<CollectingSink>();
        coordinator = std::make_unique<execution::CheckpointCoordinator>(
            *port, *sink, [] { return std::chrono::system_clock::time_point{}; });
        task = {core::ProjectId::generate(), core::BranchId::generate(),
                core::RevisionId::generate(), core::RunId::generate(),
                core::AttemptId{1U}};
    }
    ~JointCheckpointEnv()
    {
        std::error_code ec;
        fs::remove_all(caseDir, ec);
    }

    /// 宿主角色：把编排产出的检查点状态经真协调器写出（N8——本域只产
    /// 字节；磁盘编址/manifest/摘要全归 execution）。
    execution::CheckpointId writeState(const JointCheckpointState& st,
                                       const core::TaskIdentity& identity,
                                       const core::ContentIdentity& snapshotId,
                                       const core::ContentIdentity& sliceId,
                                       std::uint64_t seq)
    {
        execution::CheckpointWriteRequest req;
        req.record.checkpointId = execution::CheckpointId{identity.run, seq};
        req.record.task = identity;
        req.record.snapshotId = snapshotId;
        req.record.sliceId = sliceId;
        req.record.evaluatorKey = std::string(optimization::kOptJointFullKey);
        req.record.evaluatorContractVersion = optimization::kOptJointFullContractVersion;
        req.record.policyIdentity = makeContentIdentity(0x21);
        req.record.nameMapIdentity = makeContentIdentity(0x22);
        req.record.randomSeed = 7;
        req.record.threadCount = 1;
        req.record.completedUnits = st.completedUnits;
        req.record.totalUnits = st.totalUnits;
        req.record.resumable = true;
        execution::CheckpointBatchFile f;
        f.relPath = "joint-state.bin";
        f.bytes = optimization::encodeJointCheckpointState(st);
        req.files.push_back(std::move(f));
        req.archiveDir = caseDir / "checkpoints" / identity.run.toCanonical()
                         / std::to_string(seq);
        const execution::CheckpointWriteResult r = coordinator->writeCheckpoint(req);
        EXPECT_EQ(r.outcome, execution::CheckpointWriteResult::Outcome::Published);
        return r.checkpointId;
    }

    fs::path base;    ///< 临时根
    fs::path caseDir; ///< 用例目录
    std::unique_ptr<FakeArchiveDisk> port;
    std::unique_ptr<CollectingSink> sink;
    std::unique_ptr<execution::CheckpointCoordinator> coordinator;
    core::TaskIdentity task;
    static int counter;
};
int JointCheckpointEnv::counter = 0;

TEST(OptJointRun, CheckpointResumeStatisticsNeverDuplicated_WP21T04_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "AT-35"},
                  std::vector<std::string>{"AT-35"});

    // 场景（OPT-VER-223/226/227——AT-35 核心）：批 1 完成后协作取消 →
    // 检查点经真协调器落盘 → 恢复（新 AttemptId）续跑至完成 → 最终审计
    // ＝单次全量运行的审计（统计不重复）；已完成批在恢复运行中按基点
    // 记账（resumedBatchesSkipped），评估数不双计。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    const auto facts = makeFullFacts();
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{facts}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    const auto cfg = makeStageDConfig(/*threadCount=*/1, /*seed=*/7, /*maxCandidates=*/9);
    auto req = fx.readyRequest(cfg);

    // 参照组：单次全量运行（无中断）——终态审计的比对底座。
    const optimization::JointRunResult full = orch.run(req, fx.ctx);
    ASSERT_TRUE(full.runCompleted);

    // 中断腿：第 2 批边界后取消（批容量 4——批 1 完成、批 2 在取消查询
    // 处截停）；检查点经真协调器落盘（OPT-VER-223 正常可恢复）。
    JointFixture fx2;
    JointLayeredCheckDeps deps2;
    deps2 = fx2.checkerDeps();
    JointRunOrchestratorDeps d2;
    d2.checkerDeps = &deps2;
    StubProjector projector2{facts}; d2.projector = &projector2;
    d2.paretoBuilder = &pareto;
    JointRunOrchestrator orch2(OptimizationStage::StageD, d2);

    auto req2 = fx2.readyRequest(cfg);
    req2.baselineRoot = req.baselineRoot;  // 同候选集合（同一研究的两次尝试）
    req2.baselineCv = req.baselineCv;
    // 覆盖矩阵用 fx2 自身快照的分母（快照绑定——跨 fixture 搬运会因
    // requiredCaseSetId 错配触发 ②级门禁全判 DataInsufficient）；候选集
    // 合等价只由 baselineRoot/baselineCv/config 决定（CandidateId 公式）。
    req2.task.run = core::RunId::generate();  // 新运行身份（中断腿）
    // 取消时机（查询计数制）：内层 JointLayeredChecker 每层起点查询一次
    // 取消（六层）——批 0 的 4 候选共 24 次层查询全部放行；第 25 次＝批 1
    // 派发的批间查询（i>0）即取消（批 0 完整、批 1~2 未派发——TASK-01）。
    CancelAfterBatchesContext cancelCtx(/*afterQueries=*/24);
    const optimization::JointRunResult partial = orch2.run(req2, cancelCtx);
    ASSERT_FALSE(partial.runCompleted) << "取消腿必须被截停";
    EXPECT_EQ(partial.runPhase, RunPhase::Canceled);
    ASSERT_FALSE(partial.checkpointStates.empty()) << "取消必须产出当前检查点状态";

    // 宿主写出（真协调器——Published＋磁盘三文件）。
    JointCheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x31);
    const auto sliceId = makeContentIdentity(0x32);
    const execution::CheckpointId ckptId
        = env.writeState(partial.checkpointStates.back(), env.task, snapshotId, sliceId, 1);
    EXPECT_EQ(env.coordinator->indexedCount(), 1U);

    // 恢复（真协调器 restore——Restored 规格；新 AttemptId＝宿主 +1 语义）。
    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.checkpointId = ckptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion = optimization::kOptJointFullContractVersion;
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    ASSERT_EQ(restored.outcome, execution::CheckpointRestoreResult::Outcome::Restored);
    EXPECT_EQ(restored.record.completedUnits, partial.checkpointStates.back().completedUnits);
    // 中间状态字节装载→域解码（round-trip 完整性——宿主恢复面）。
    std::vector<std::uint8_t> stateBytes;
    for (const auto& dirEntry : fs::recursive_directory_iterator(
             env.caseDir / "checkpoints" / env.task.run.toCanonical())) {
        if (dirEntry.is_regular_file()
            && dirEntry.path().filename() == "joint-state.bin") {
            std::ifstream in(dirEntry.path(), std::ios::binary);
            stateBytes.assign(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
        }
    }
    ASSERT_FALSE(stateBytes.empty());
    const JointCheckpointState resumeState = optimization::decodeJointCheckpointState(stateBytes);
    EXPECT_EQ(resumeState.completedUnits, partial.checkpointStates.back().completedUnits);

    // 恢复腿（新 AttemptId——恢复语义 §9.2；审计自检查点基点累计）。
    JointFixture fx3;
    JointLayeredCheckDeps deps3;
    deps3 = fx3.checkerDeps();
    JointRunOrchestratorDeps d3;
    d3.checkerDeps = &deps3;
    StubProjector projector3{facts}; d3.projector = &projector3;
    d3.paretoBuilder = &pareto;
    JointRunOrchestrator orch3(OptimizationStage::StageD, d3);
    auto req3 = fx3.readyRequest(cfg);
    req3.baselineRoot = req.baselineRoot;
    req3.baselineCv = req.baselineCv;
    // 覆盖矩阵用 fx3 自身快照分母（快照绑定——同 req2 注）；任务身份取
    // 检查点记录五元组＋AttemptId+1（恢复语义 §9.2）。
    req3.task = env.task;
    req3.task.attempt = core::AttemptId{2U};  // AttemptId+1（恢复语义）
    req3.resumeFrom = resumeState;
    const optimization::JointRunResult resumed = orch3.run(req3, fx3.ctx);
    ASSERT_TRUE(resumed.runCompleted);

    // AT-35：恢复后统计不重复——总审计（检查点基点＋恢复增量）＝单次全量。
    EXPECT_EQ(resumed.audit.base.candidatesGenerated,
              full.audit.base.candidatesGenerated);
    EXPECT_EQ(resumed.audit.base.quickEvaluated, full.audit.base.quickEvaluated)
        << "Quick 评估数必须不双计（基点已含前缀账目——OPT-VER-227）";
    EXPECT_EQ(resumed.audit.base.quickScreenedOut, full.audit.base.quickScreenedOut);
    EXPECT_EQ(resumed.audit.base.verifiedEvaluated, full.audit.base.verifiedEvaluated);
    EXPECT_GT(resumed.audit.resumedBatchesSkipped, 0U)
        << "已完成批必须按基点记账（不重复累计）";
    // 终态结论面等价（NFR-COR-02——恢复不改变结论）：可行集/Pareto 与全
    // 量运行一致。
    ASSERT_EQ(resumed.pareto.entries.size(), full.pareto.entries.size());
    for (std::size_t i = 0; i < full.pareto.entries.size(); ++i) {
        EXPECT_TRUE(resumed.pareto.entries[i].candidateId == full.pareto.entries[i].candidateId);
    }
}

TEST(OptJointRun, CheckpointCorruptRejectedByCoordinator_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "CON-04"}, std::vector<std::string>{});

    // OPT-VER-224：注入损坏（磁盘字节翻转）→ 真 restore 判 Corrupt
    // （EX-CHECKPOINT-CORRUPT——只读废弃标记，原检查点保留）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{makeFullFacts()}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    auto req = fx.readyRequest(makeStageDConfig());
    CancelAfterBatchesContext cancelCtx(/*afterQueries=*/0);
    const optimization::JointRunResult partial = orch.run(req, cancelCtx);
    ASSERT_FALSE(partial.checkpointStates.empty());

    JointCheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x41);
    const auto sliceId = makeContentIdentity(0x42);
    const execution::CheckpointId ckptId
        = env.writeState(partial.checkpointStates.back(), env.task, snapshotId, sliceId, 1);

    // 磁盘字节翻转（摘要核对判据面——EX-CKP-1）。
    fs::path target;
    for (const auto& dirEntry : fs::recursive_directory_iterator(
             env.caseDir / "checkpoints" / env.task.run.toCanonical())) {
        if (dirEntry.is_regular_file()
            && dirEntry.path().filename() == "joint-state.bin") {
            target = dirEntry.path();
        }
    }
    ASSERT_FALSE(target.empty());
    {
        std::fstream f(target, std::ios::binary | std::ios::in | std::ios::out);
        f.seekg(0);
        char c = 0;
        f.read(&c, 1);
        f.seekp(0);
        f.put(static_cast<char>(c ^ 0xFF));
    }

    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.checkpointId = ckptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion = optimization::kOptJointFullContractVersion;
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    EXPECT_EQ(restored.outcome, execution::CheckpointRestoreResult::Outcome::Corrupt);
    bool hasCorruptCode = false;
    for (const auto& diag : restored.diagnostics) {
        if (diag.code.find("EX-CHECKPOINT-CORRUPT") != std::string::npos) {
            hasCorruptCode = true;
        }
    }
    EXPECT_TRUE(hasCorruptCode) << "损坏拒绝必须携带 EX-CHECKPOINT-CORRUPT 稳定码";
}

TEST(OptJointRun, CheckpointVersionIncompatibleNoMigration_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "CON-04"},
                  std::vector<std::string>{"P-EX-9"});

    // OPT-VER-225：契约版本漂移 → 真 restore 判 Incompatible＋reasons
    // （旧版本检查点不迁移——P-EX-9 宁可重算不可错续）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{makeFullFacts()}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    auto req = fx.readyRequest(makeStageDConfig());
    CancelAfterBatchesContext cancelCtx(/*afterQueries=*/0);
    const optimization::JointRunResult partial = orch.run(req, cancelCtx);
    ASSERT_FALSE(partial.checkpointStates.empty());

    JointCheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x51);
    const auto sliceId = makeContentIdentity(0x52);
    const execution::CheckpointId ckptId
        = env.writeState(partial.checkpointStates.back(), env.task, snapshotId, sliceId, 1);

    // 恢复请求携带漂移后的契约版本（变更 codec 后恢复——判定第三条件）。
    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.checkpointId = ckptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion
        = optimization::kOptJointFullContractVersion + 1U;  // 漂移
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    EXPECT_EQ(restored.outcome,
              execution::CheckpointRestoreResult::Outcome::Incompatible);
    EXPECT_FALSE(restored.reasons.empty()) << "不兼容拒绝必须携带 reasons（evidence 判定式）";
}

// =====================================================================
// 阶段锁与基线席（编排边界——acceptance 1/2）
// =====================================================================

TEST(OptJointRun, StageBRequestRejectedAtStart_WP21T04_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-03"}, std::vector<std::string>{});

    // StageB 请求联合编排＝运行启动阻塞（OPT-STAGE-LOCKED——§6.5；R1 面
    // 归两级编排，双编排同一码面）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{makeFullFacts()}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageB, d);
    auto cfg = makeStageDConfig();
    cfg.stage = OptimizationStage::StageB;
    bool threw = false;
    try {
        NopEvaluationContext ctx;
        (void)orch.run(fx.readyRequest(cfg), ctx);
    } catch (const OptimizationError& e) {
        threw = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptStageLocked));
    }
    EXPECT_TRUE(threw) << "StageB 请求必须 OPT-STAGE-LOCKED（§5.7/§6.5）";
}

TEST(OptJointRun, BaselineCandidateAlwaysVerifiedSeat_WP21T04_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{});

    // 基线候选（空补丁恒批首——§8.2）Quick-Feasible 恒占 Verified 一席
    // （OPT-11 比较基准保守化——§8.4 选席规则）；运行正常完成且基线在
    // 复核批记录中。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{makeFullFacts()}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    auto req = fx.readyRequest(makeStageDConfig());
    const optimization::JointRunResult result = orch.run(req, fx.ctx);
    ASSERT_TRUE(result.runCompleted);
    ASSERT_FALSE(result.quickRecords.empty());
    EXPECT_TRUE(result.quickRecords.front().base.isBaseline) << "基线恒批首（§8.2）";
    EXPECT_TRUE(result.quickRecords.front().base.screeningOnly);
    // 基线进入 Verified 批（基线席保守化）。
    bool baselineVerified = false;
    for (const auto& rec : result.verifiedRecords) {
        if (rec.base.isBaseline) {
            baselineVerified = true;
            EXPECT_FALSE(rec.base.screeningOnly);
        }
    }
    EXPECT_TRUE(baselineVerified) << "基线 Quick-Feasible 恒占一席（OPT-11）";
}

// =====================================================================
// Paused 态消费（WP-21-T07——OPT-VER-227~231/AT-35/V12-06）：批边界暂停
// 确认（暂停确认边界＝批边界检查点）、继续不重复统计（resumeFrom 审计
// 基点）、不改变输入快照（sliceId 身份不变式）、暂停中取消保检查点
// （取消后检查点仍可续跑且统计链一致）。
// =====================================================================

TEST(OptJointRun, PauseAtBatchBoundaryEmitsCheckpointState_WP21T07_OPTVER229)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "AT-35"},
                  std::vector<std::string>{"AT-35"});

    // 场景（OPT-VER-229 暂停半边——§9.2/AT-35/V12-06）：批 0 完成后的批
    // 间边界置暂停 → 编排截停于该批界 → runPhase=Paused＋runCompleted=
    // false；**暂停确认边界＝截停批界的检查点状态**（checkpointStates
    // 末份：completedBatchIndices=[0]——宿主写出并确认后任务态转 Paused
    // 归 execution T10）。批 0 内 4 候选完整保留（已回传批保留——TASK-01
    // 同型）；单批界查询即截停（不派发批 1/2）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    StubProjector projector{makeFullFacts()}; d.projector = &projector;
    d.paretoBuilder = &pareto;
    PauseAfterBoundariesSignal pauseSignal(/*afterQueries=*/0);  // 首个批界（i=1）即暂停
    d.pauseSignal = &pauseSignal;
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    // 9 候选×批容量 4 → Quick 3 批（4/4/1）；threadCount=1 串行路径——
    // 批界查询次数确定（每批间恰一次），暂停生效批界确定。
    auto req = fx.readyRequest(makeStageDConfig(/*threadCount=*/1, /*seed=*/7, /*maxCandidates=*/9));
    const optimization::JointRunResult paused = orch.run(req, fx.ctx);

    EXPECT_FALSE(paused.runCompleted) << "暂停腿必须被截停";
    EXPECT_EQ(paused.runPhase, RunPhase::Paused) << "暂停产出 Paused 相（表尾追加值）";
    EXPECT_EQ(optimization::toToken(RunPhase::Paused), "paused")
        << "稳定 token 书写面同步（运行记录/导出/审计确定性）";
    ASSERT_GT(pauseSignal.queries(), 0U) << "批边界暂停查询必须发生（i>0 界）";
    // 批 0 完整保留＋其后批未派发（停止派发语义）。
    ASSERT_EQ(paused.quickRecords.size(), 4U)
        << "批 0（4 候选）完整保留——已回传批保留（TASK-01 同型）";
    // 暂停确认边界凭据：检查点状态序列末份＝截停批界快照。
    ASSERT_FALSE(paused.checkpointStates.empty())
        << "暂停必须产出当前检查点状态（暂停确认边界＝批边界检查点，V12-06）";
    const JointCheckpointState& confirm = paused.checkpointStates.back();
    EXPECT_EQ(confirm.phase, core::EvaluationMode::Quick);
    ASSERT_EQ(confirm.completedBatchIndices.size(), 1U);
    EXPECT_EQ(confirm.completedBatchIndices[0], 0U) << "截停批界＝批 0 完成边界";
    EXPECT_EQ(confirm.completedUnits, 1U) << "completedUnits＝候选批计数（续跑统计基点）";
    EXPECT_GT(paused.audit.checkpointStatesEmitted, 0U);
    EXPECT_EQ(paused.audit.base.quickEvaluated, 4U) << "只记批 0 增量（不冒记未派发批）";
}

TEST(OptJointRun, ResumeAfterPauseStatsAndIdentityNeverDuplicated_WP21T07_OPTVER227_228_230)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-06", "AT-35"},
                  std::vector<std::string>{"AT-35"});

    // 场景（OPT-VER-227/228/230 续跑半边——AT-35/I-OPT-4）：同一 fixture
    //（同快照——sliceId 可跨腿比较）三腿对账：①全量参照；②批 0 界暂停
    // →检查点经**真协调器**落盘→装载解码；③resumeFrom 续跑至完成。
    // 断言四条：统计不重复（审计对账＝全量——OPT-VER-227）、恢复跳批记
    // 账（resumedBatchesSkipped）、身份不变（逐候选 facadeSliceId 与全量
    // 一致——I-OPT-4"恢复不改输入"：snapshot/slice/config 不变——
    // OPT-VER-228）、结论面等价（Pareto 序一致——NFR-COR-02）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    const auto facts = makeFullFacts();
    const auto cfg = makeStageDConfig(/*threadCount=*/1, /*seed=*/7, /*maxCandidates=*/9);

    // ---- 腿①：全量参照（无暂停信号——R1 装配形态永不暂停的回归面）。
    JointRunOrchestratorDeps dFull;
    dFull.checkerDeps = &deps;
    StubProjector projectorFull{facts}; dFull.projector = &projectorFull;
    dFull.paretoBuilder = &pareto;
    JointRunOrchestrator orchFull(OptimizationStage::StageD, dFull);
    auto reqFull = fx.readyRequest(cfg);
    const optimization::JointRunResult full = orchFull.run(reqFull, fx.ctx);
    ASSERT_TRUE(full.runCompleted);

    // ---- 腿②：批 0 界暂停（同 fixture——快照/覆盖/注册表共享，sliceId
    // 跨腿可比；基线身份钉住 reqFull——同候选集合＝同一研究的两次尝试，
    // CandidateId 公式输入一致，T04 恢复用例同款纪律）。
    JointRunOrchestratorDeps dPause;
    dPause.checkerDeps = &deps;
    StubProjector projectorPause{facts}; dPause.projector = &projectorPause;
    dPause.paretoBuilder = &pareto;
    PauseAfterBoundariesSignal pauseSignal(/*afterQueries=*/0);
    dPause.pauseSignal = &pauseSignal;
    JointRunOrchestrator orchPause(OptimizationStage::StageD, dPause);
    auto reqPause = fx.readyRequest(cfg);
    reqPause.baselineRoot = reqFull.baselineRoot;  // 同候选集合（两次尝试）
    reqPause.baselineCv = reqFull.baselineCv;
    reqPause.task.run = core::RunId::generate();  // 新运行身份（暂停腿）
    const optimization::JointRunResult paused = orchPause.run(reqPause, fx.ctx);
    ASSERT_EQ(paused.runPhase, RunPhase::Paused);
    ASSERT_FALSE(paused.checkpointStates.empty());

    // 宿主写出（真协调器——Published）→装载→域解码（round-trip 完整）。
    JointCheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x41);
    const auto sliceId = makeContentIdentity(0x42);
    const execution::CheckpointId ckptId
        = env.writeState(paused.checkpointStates.back(), env.task, snapshotId, sliceId, 1);
    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.checkpointId = ckptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion = optimization::kOptJointFullContractVersion;
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    ASSERT_EQ(restored.outcome, execution::CheckpointRestoreResult::Outcome::Restored);
    EXPECT_EQ(restored.record.completedUnits, paused.checkpointStates.back().completedUnits);
    // 检查点中间状态字节装载→域解码（同 T04 恢复腿形态——真磁盘读回）。
    std::vector<std::uint8_t> stateBytes;
    for (const auto& dirEntry : fs::recursive_directory_iterator(
             env.caseDir / "checkpoints" / env.task.run.toCanonical())) {
        if (dirEntry.is_regular_file()
            && dirEntry.path().filename() == "joint-state.bin") {
            std::ifstream in(dirEntry.path(), std::ios::binary);
            stateBytes.assign(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
        }
    }
    ASSERT_FALSE(stateBytes.empty());
    const JointCheckpointState resumeState = optimization::decodeJointCheckpointState(stateBytes);
    EXPECT_EQ(resumeState.completedBatchIndices,
              paused.checkpointStates.back().completedBatchIndices);

    // ---- 腿③：resumeFrom 续跑（恢复语义——AttemptId+1；审计自基点累计）。
    JointRunOrchestratorDeps dResume;
    dResume.checkerDeps = &deps;
    StubProjector projectorResume{facts}; dResume.projector = &projectorResume;
    dResume.paretoBuilder = &pareto;
    JointRunOrchestrator orchResume(OptimizationStage::StageD, dResume);
    auto reqResume = fx.readyRequest(cfg);
    reqResume.baselineRoot = reqFull.baselineRoot;  // 同候选集合（第三次尝试）
    reqResume.baselineCv = reqFull.baselineCv;
    reqResume.task = env.task;
    reqResume.task.attempt = core::AttemptId{2U};  // AttemptId+1（恢复语义 §9.2）
    reqResume.resumeFrom = resumeState;
    const optimization::JointRunResult resumed = orchResume.run(reqResume, fx.ctx);
    ASSERT_TRUE(resumed.runCompleted);

    // AT-35（OPT-VER-227）：暂停→继续统计不重复——审计对账＝全量运行。
    EXPECT_EQ(resumed.audit.base.candidatesGenerated,
              full.audit.base.candidatesGenerated);
    EXPECT_EQ(resumed.audit.base.quickEvaluated, full.audit.base.quickEvaluated)
        << "Quick 评估数必须不双计（检查点基点已含批 0 账目——OPT-VER-227）";
    EXPECT_EQ(resumed.audit.base.quickScreenedOut, full.audit.base.quickScreenedOut);
    EXPECT_EQ(resumed.audit.base.verifiedEvaluated, full.audit.base.verifiedEvaluated);
    EXPECT_GT(resumed.audit.resumedBatchesSkipped, 0U)
        << "暂停前已完成批必须按基点记账（不重复累计）";
    EXPECT_EQ(resumed.audit.base.cacheFullHits, full.audit.base.cacheFullHits);

    // OPT-VER-228（I-OPT-4 恢复不改输入）：逐候选 facadeSliceId 与全量一致
    // ——恢复仍绑定原 slice 身份（同 fixture 同快照 ⇒ 同层切片身份；config
    // 由同一对象拷贝构造，CandidateId 公式输入不变）。身份漂移即恢复换了
    // 输入——直接违反"不改变输入快照"。
    ASSERT_EQ(resumed.quickRecords.size(), full.quickRecords.size());
    for (std::size_t i = 0; i < full.quickRecords.size(); ++i) {
        EXPECT_TRUE(resumed.quickRecords[i].base.candidateId
                    == full.quickRecords[i].base.candidateId)
            << "候选序一致（生成序确定性）";
        EXPECT_TRUE(resumed.quickRecords[i].facadeSliceId == full.quickRecords[i].facadeSliceId)
            << "门面切片身份必须逐候选一致（sliceId 不变式——OPT-VER-228）";
    }
    // 结论面等价（NFR-COR-02——恢复不改变结论）：Pareto 序一致。
    ASSERT_EQ(resumed.pareto.entries.size(), full.pareto.entries.size());
    for (std::size_t i = 0; i < full.pareto.entries.size(); ++i) {
        EXPECT_TRUE(resumed.pareto.entries[i].candidateId == full.pareto.entries[i].candidateId);
    }
}

TEST(OptJointRun, CancelAfterPauseKeepsCheckpointResumable_WP21T07_OPTVER231)
{
    IRD_TEST_INFO(std::vector<std::string>{"AT-35"},
                  std::vector<std::string>{"AT-35"});

    // 场景（OPT-VER-231 暂停中取消——优化域半边；执行侧 T12 见
    // CancelProtocolTest）：暂停批 0 界 → 检查点 P 落盘 → 续跑腿在批 1 界
    // 协作取消（取消终态产出当前检查点状态 C——TASK-01 同型）→ **取消不
    // 作废暂停检查点**（协调器零废弃——indexedCount 不变；P 仍可 restore）
    // → 自取消状态 C 再续跑至完成 → 全链统计＝全量运行（暂停→继续→取消
    // →再继续的统计链一致——AT-35/V12-06"暂停中取消保检查点（可续跑）"）。
    JointFixture fx;
    JointLayeredCheckDeps deps;
    deps = fx.checkerDeps();
    static optimization::ParetoFrontBuilder pareto;
    const auto facts = makeFullFacts();
    const auto cfg = makeStageDConfig(/*threadCount=*/1, /*seed=*/7, /*maxCandidates=*/9);

    // 全量参照。
    JointRunOrchestratorDeps dFull;
    dFull.checkerDeps = &deps;
    StubProjector projectorFull{facts}; dFull.projector = &projectorFull;
    dFull.paretoBuilder = &pareto;
    JointRunOrchestrator orchFull(OptimizationStage::StageD, dFull);
    auto reqFull = fx.readyRequest(cfg);
    const optimization::JointRunResult full = orchFull.run(reqFull, fx.ctx);
    ASSERT_TRUE(full.runCompleted);

    // 腿 1：暂停（批 0 界）→ 检查点 P 经真协调器落盘（基线身份钉住
    // reqFull——同候选集合，T04 恢复用例同款纪律）。
    JointRunOrchestratorDeps dPause;
    dPause.checkerDeps = &deps;
    StubProjector projectorPause{facts}; dPause.projector = &projectorPause;
    dPause.paretoBuilder = &pareto;
    PauseAfterBoundariesSignal pauseSignal(/*afterQueries=*/0);
    dPause.pauseSignal = &pauseSignal;
    JointRunOrchestrator orchPause(OptimizationStage::StageD, dPause);
    auto reqPause = fx.readyRequest(cfg);
    reqPause.baselineRoot = reqFull.baselineRoot;
    reqPause.baselineCv = reqFull.baselineCv;
    reqPause.task.run = core::RunId::generate();
    const optimization::JointRunResult paused = orchPause.run(reqPause, fx.ctx);
    ASSERT_EQ(paused.runPhase, RunPhase::Paused);
    ASSERT_FALSE(paused.checkpointStates.empty());

    JointCheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x43);
    const auto sliceId = makeContentIdentity(0x44);
    const execution::CheckpointId pauseCkptId
        = env.writeState(paused.checkpointStates.back(), env.task, snapshotId, sliceId, 1);
    ASSERT_EQ(env.coordinator->indexedCount(), 1U);

    // 腿 2：续跑（AttemptId+1）＋批 1 界协作取消（批 0 重推进 24 次层查询
    // 放行后，第 25 次＝批 1 派发的批间查询即取消——批 0 不重记账）。
    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.checkpointId = pauseCkptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion = optimization::kOptJointFullContractVersion;
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    ASSERT_EQ(restored.outcome, execution::CheckpointRestoreResult::Outcome::Restored);
    std::vector<std::uint8_t> pauseBytes;
    for (const auto& dirEntry : fs::recursive_directory_iterator(
             env.caseDir / "checkpoints" / env.task.run.toCanonical())) {
        if (dirEntry.is_regular_file()
            && dirEntry.path().filename() == "joint-state.bin") {
            std::ifstream in(dirEntry.path(), std::ios::binary);
            pauseBytes.assign(std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>());
        }
    }
    ASSERT_FALSE(pauseBytes.empty());
    const JointCheckpointState pauseState = optimization::decodeJointCheckpointState(pauseBytes);

    JointRunOrchestratorDeps dCancel;
    dCancel.checkerDeps = &deps;
    StubProjector projectorCancel{facts}; dCancel.projector = &projectorCancel;
    dCancel.paretoBuilder = &pareto;
    JointRunOrchestrator orchCancel(OptimizationStage::StageD, dCancel);
    auto reqCancel = fx.readyRequest(cfg);
    reqCancel.baselineRoot = reqFull.baselineRoot;
    reqCancel.baselineCv = reqFull.baselineCv;
    reqCancel.task = env.task;
    reqCancel.task.attempt = core::AttemptId{2U};
    reqCancel.resumeFrom = pauseState;
    // 取消时机（同 T04 取消腿口径）：批 0 六层×4 候选＝24 次层查询全部放
    // 行；第 25 次批间查询即取消。批 0 已在检查点基点（不重记账），取消
    // 状态 C 的完成集/审计与暂停状态 P 同基（增量只来自批 1 之后的空集）。
    CancelAfterBatchesContext cancelCtx(/*afterQueries=*/24);
    const optimization::JointRunResult canceled = orchCancel.run(reqCancel, cancelCtx);
    ASSERT_FALSE(canceled.runCompleted);
    EXPECT_EQ(canceled.runPhase, RunPhase::Canceled);
    ASSERT_FALSE(canceled.checkpointStates.empty())
        << "取消必须产出当前检查点状态（TASK-01 部分 批保留）";

    // 暂停检查点未被取消作废（保检查点——协调器零废弃；restore 仍通过）。
    EXPECT_EQ(env.coordinator->indexedCount(), 1U)
        << "取消路径必须零废弃动作（检查点保留——ARCH §4.3 A5/T12）";
    const execution::CheckpointRestoreResult recheck = env.coordinator->restore(restoreReq);
    EXPECT_EQ(recheck.outcome, execution::CheckpointRestoreResult::Outcome::Restored)
        << "暂停检查点在取消后仍可恢复（可续跑——V12-06）";

    // 腿 3：自取消状态 C 再续跑（AttemptId 再 +1）至完成——统计链一致。
    // （取消状态以编排产出内存值为续跑输入——宿主写盘/装载协议已由腿 1
    // 与 T04 检查点用例覆盖，此处不重复；C 的完成集/审计与暂停基点同源：
    // 批 0 重推进不记账〔I-J3〕，故 C.completedBatchIndices==P 的一致性
    // 直接断言如下。）
    const JointCheckpointState cancelState = canceled.checkpointStates.back();
    EXPECT_EQ(cancelState.completedBatchIndices, pauseState.completedBatchIndices)
        << "取消状态完成集与暂停基点同源（批 0 重推进不重复记账）";
    EXPECT_EQ(cancelState.completedUnits, pauseState.completedUnits);

    JointRunOrchestratorDeps dFinal;
    dFinal.checkerDeps = &deps;
    StubProjector projectorFinal{facts}; dFinal.projector = &projectorFinal;
    dFinal.paretoBuilder = &pareto;
    JointRunOrchestrator orchFinal(OptimizationStage::StageD, dFinal);
    auto reqFinal = fx.readyRequest(cfg);
    reqFinal.baselineRoot = reqFull.baselineRoot;
    reqFinal.baselineCv = reqFull.baselineCv;
    reqFinal.task = env.task;
    reqFinal.task.attempt = core::AttemptId{3U};
    reqFinal.resumeFrom = cancelState;
    const optimization::JointRunResult finalRun = orchFinal.run(reqFinal, fx.ctx);
    ASSERT_TRUE(finalRun.runCompleted);

    // 全链统计对账（AT-35）：暂停→继续→取消→再继续的最终审计＝全量运行。
    EXPECT_EQ(finalRun.audit.base.candidatesGenerated,
              full.audit.base.candidatesGenerated);
    EXPECT_EQ(finalRun.audit.base.quickEvaluated, full.audit.base.quickEvaluated)
        << "暂停/取消链上的 Quick 评估数必须不双计";
    EXPECT_EQ(finalRun.audit.base.quickScreenedOut, full.audit.base.quickScreenedOut);
    EXPECT_EQ(finalRun.audit.base.verifiedEvaluated, full.audit.base.verifiedEvaluated);
    EXPECT_GT(finalRun.audit.resumedBatchesSkipped, 0U);
    ASSERT_EQ(finalRun.pareto.entries.size(), full.pareto.entries.size());
    for (std::size_t i = 0; i < full.pareto.entries.size(); ++i) {
        EXPECT_TRUE(finalRun.pareto.entries[i].candidateId == full.pareto.entries[i].candidateId);
    }
}
