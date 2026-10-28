/**
 * @file   OptRunTimeBenchmarkTest.cpp
 * @brief  优化运行时限基准（performance-baseline §2 B-07／需求 NFR-PERF-06）——
 *         10,000 Quick＋100 Verified 候选标准优化基准 8 小时内结束（端到端
 *         墙钟时限判定）＋运行期间交互响应满足 NFR-PERF-01（五类交互运行窗
 *         内 P95 ≤200 ms 抽查）＋可从同版本兼容检查点恢复（真
 *         execution::CheckpointCoordinator 集成——AT-35 统计不重复＋结论面
 *         等价）（WP-23-T08；DTB §2.24 行——横切 optimization）。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-07 行（达标
 *     口径照抄需求原文——"10,000 Quick＋100 Verified 候选标准优化基准 8 小
 *     时内结束；运行期间满足 NFR-PERF-01；可从同版本兼容检查点恢复"，零私
 *     改）、§2 统计口径区分行（B-07 为时序/规模阈值类判定，不涉及分位数统
 *     计；运行期交互抽查的 P95 判定属 NFR-PERF-01 自身口径——两套口径各归
 *     其位，不互相换轨）、§4.1（硬件/软件版本登记）、§4.2（样本纪律——每
 *     组有效 ≥15、剔除首次预热、无效样本不计入分母并留痕）、§4.3（交互响
 *     应测量口径——运行期间交互响应按本条口径抽查：五类操作端到端响应、
 *     无 >2 s 无响应窗口、>1 s 工作转后台）、§4.7（B-07 运行时限——8 小时
 *     为端到端墙钟时限；检查点恢复限定同版本兼容）、§4.8（报告留痕）、
 *     §5.7（本卡挂点）；
 *   - 需求 NFR-PERF-06（REQUIREMENTS §19.3 原文）＋NFR-PERF-01（REQUIREMENTS
 *     §19.3 原文——"运行期间满足 NFR-PERF-01"的口径出处：P95 ≤200 ms／无
 *     >2 s 无响应窗口／>1 s 转后台三值照抄，零私改）；
 *   - units/optimization.md §9.2（被测对象＝WP-21-T04 交付的 JointRun
 *     Orchestrator 两级批量编排——并行分片〔threadCount 来自 config.opt 入
 *     身份〕＋分批流式＋检查点域中间状态〔域自编 codec magic IRDOPTC1，
 *     checkpointGranularity=Batch，completedUnits/totalUnits＝候选批计数〕
 *     ＋恢复语义〔恢复后新 AttemptId；不兼容检查点必须拒绝——P-EX-9；
 *     检查点恢复不等于优化成功〕；§9.2 规模化口径锚"达标归 WP-23 对应基准
 *     任务"即本卡）；§13.2 OPT-VER-238 同族（进程级规模化基准归 WP-23）；
 *   - 任务契约 tasks/foundation/WP-23-T08.json（≙DTB §2.24 行）。
 *
 * 达标口径三面与本文件的用例映射：
 *   ①基准时限（用例 1）：16 次真实全量运行（1 预热剔除＋15 有效样本）——
 *     每样本计时区间＝JointRunOrchestrator::run() 全程端到端墙钟
 *     （steady_clock），判定统计量＝有效样本最大值 ≤28,800,000 ms（8 小时
 *     ——B-07 为时序阈值类判定，T03/T06 阈值类先例同口径，非 P95 非中位数）；
 *   ②运行期间满足 PERF-01（用例 1）：独立常驻采样线程扮演"UI 线程测试形
 *     态"，在运行窗旗标开门期间循环执行五类交互的无头模型层等价操作
 *     （WP-23-T02 B-01 同款驱动形态）——仅"开始与结束都落在运行窗内"的
 *     样本计入统计（运行期间语义的门控承载）；逐操作有效样本 ≥15、
 *     P95 ≤200 ms（线性插值——NFR-PERF-01 口径，禁 3 次中位数替代）、
 *     单样本 <2 s（无响应窗口）；"超过 1 s 的工作转后台"＝结构承载：优化
 *     运行的评估工作全部发生在编排器 worker 线程（config.parallel.
 *     threadCount=8）与驱动/采样线程分立，五类操作自身不执行任何评估工作；
 *   ③同版本检查点恢复（用例 2）：协作取消在 Quick 相约半程截停 → 检查点
 *     域中间状态经**真 execution::CheckpointCoordinator** 写出真实磁盘文件
 *     （JointRunTest OPT-VER-223 宿主角色同款）→ **同版本**恢复（restore
 *     契约版本＝kOptJointFullContractVersion，codec IRDOPTC1 v1）→ 新
 *     AttemptId 续跑至完成 → AT-35 审计不重复（终态审计＝单次全量运行的
 *     审计）＋结论面等价（候选序/状态/八项指标 1×10⁻¹²·|ref|＋ε_abs=0 逐
 *     元素/Pareto 序——NFR-COR-02）＋物理工作对账（工作汇增量相等——跳过
 *     批的记录按确定性重推进 I-J3，账面不双计而计算真实发生）。
 *
 * 替身边界声明（§11 同源纪律——替身不构成业务证明，T07 同款）：
 *   - 内层四域评估器＝确定性浮点迭代链测试替身（工作量参数与 B-06 冻结值
 *     同源 400,000 次串行 FP 乘加/派发）——本基准证明的是"编排器在
 *     10,000＋100 候选规模下两级批量推进＋并行分片＋检查点产出/恢复的端到
 *     端墙钟规模化"，不是业务评估器（trj/dyn/sel 真实现）算力成本下的
 *     8 小时时限画像——业务评估器端到端时限归集成模式扩展测量挂点（首测
 *     报告登记，集成构建由合入批执行）；
 *   - 指标投影缝＝P-OPT-2 测试面替身（knownPitfalls 登记缝——JointRunTest/
 *     ParallelThroughputBenchmarkTest 同款接缝纪律）；
 *   - 运行期交互抽查的 ui 侧数据集＝B-01 数据集生成器参数同参复刻
 *     （1,000 对象/100 修订语境——ird-perf-b01-interaction@1 同参，非同库
 *     混用：本基准数据集 ird-perf-b07-opt-run-time@1 自持全量参数冻结）；
 *     后台负载与 B-01 后台态的形态差异如实申报：B-01 后台＝独立 worker 进
 *     程评估批次；本基准后台＝优化运行本体（进程内编排 worker 线程）——
 *     这正是 NFR-PERF-06"运行期间"的负载语义。
 *
 * 分段留痕口径（任务口径"启动/检查点/恢复/结束各段日志"）：本基准全程会话
 *   内串行完成（分钟量级，远小于 8 小时会话预算）——启动/结束段由逐样本
 *   完整墙钟承载（报告 groups 原始样本序列，§4.8"不得只报统计值"）；检查
 *   点/恢复段由用例 2 报告 notes 的分段日志行承载（写盘 Published→
 *   Restored→续跑 Completed 的实测数值逐段登记）。8 小时级跨会话后台运行
 *   形态在本基准中不被触发——如实登记，不虚称。
 *
 * 测量纪律（performance-baseline §4/optimization.md §9.2 锚）：本用例串行
 *   执行（gtest 单进程串行驱动；run 内部 8 worker 并行与常驻采样线程除外
 *   ——采样线程是测量面本体，非并发测量负载）；禁止与其他测量负载并发跑
 *   分；样本全量留痕（预热/无效不删除，只标记与登记原因）；禁止伪造样本。
 */

#define QT_NO_KEYWORDS 1
#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/execution/Checkpoint.hpp>
#include <sdurws/ird/execution/Ports.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/JointRun.hpp>
#include <sdurws/ird/optimization/JointStrategy.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>   // kOptProfileId——opt Profile 域 id 常量
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/testkit/PerfBenchmark.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>
#include <sdurws/ird/ui/IStageNavigationModel.hpp>
#include <sdurws/ird/ui/IndustrialProjectTree.hpp>
#include <sdurws/ird/ui/SelectionService.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateStatus;
using optimization::JointCandidateFacts;
using optimization::JointCheckpointState;
using optimization::JointLayeredCheckDeps;
using optimization::JointRunOrchestrator;
using optimization::JointRunOrchestratorDeps;
using optimization::JointRunRequest;
using optimization::JointRunResult;
using optimization::JointUpstreamRef;
using optimization::MetricId;
using optimization::OptimizationConfiguration;
using optimization::OptimizationStage;
using optimization::RunPhase;
using optimization::StaticMetricFacts;
using optimization::StaticMetricResult;
using ui::StageId;

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b07-opt-run-time，version 1
// ——manifest 权威文件 testdata/performance/b07-opt-run-time-manifest.json；
// 本处常量必须与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// LHS 采样与候选生成种子（config.seed——入 config.opt canonical 身份；
/// manifest 冻结，与 B-06 数据集同源取值）。
constexpr std::uint64_t kSeed = 7;
/// Quick 相候选预算（budget.maxCandidates——NFR-PERF-06 原文"10,000 Quick"
/// 照抄零私改；批容量 4 ⇒ Quick 2,500 批；manifest 冻结）。
constexpr std::uint32_t kMaxCandidates = 10000;
/// Verified 相候选预算（budget.maxVerifiedCandidates——NFR-PERF-06 原文
/// "100 Verified" 照抄零私改；幸存集 100 ⇒ 25 批；manifest 冻结）。
constexpr std::uint32_t kMaxVerifiedCandidates = 100;
/// 标准运行并行度（config.parallel.threadCount——R2 生产形态：并行分片入
/// 身份；manifest 冻结。需求未规定标准基准线程数，本值为本卡登记并冻结的
/// 生成器参数——R2 交付形态即并行评估〔§9.2/§9.4〕）。
constexpr std::uint32_t kThreadCount = 8;
/// 时限腿预热次数（剔除不计入统计——§4.2/§4.5 预热口径；manifest 冻结）。
constexpr std::size_t kWarmupRuns = 1;
/// 时限腿有效样本数（§4.2 每组有效 ≥15——T02/T03/T05/T06/T07 同数自律；
/// manifest 冻结）。
constexpr std::size_t kValidSamples = 15;
/// 无效样本安全界（夹具自保——10× 上限，超界显性失败不静默补采）。
constexpr std::size_t kInvalidCeiling = kValidSamples * 10;
/// 每次内层评估派发的确定性工作量（次——串行 FP 乘加依赖链迭代数；与
/// B-06 数据集冻结工作量同源：单候选六层派发，全量运行恒 60,600 次派发；
/// manifest 冻结）。
constexpr std::uint32_t kWorkIterations = 400000;
/// 每次全量运行的期望评估计数（quickEvaluated＋verifiedEvaluated——固定
/// 候选集下恒为 maxCandidates＋maxVerifiedCandidates；偏离即样本无效）。
constexpr std::uint64_t kExpectedEvaluations =
    static_cast<std::uint64_t>(kMaxCandidates) + static_cast<std::uint64_t>(kMaxVerifiedCandidates);
/// 运行墙钟时限（ms——NFR-PERF-06 原文"8 小时内结束"照抄零私改；
/// performance-baseline §4.7：8 小时为端到端墙钟时限。28,800 s×1,000）。
constexpr double kRunWallClockLimitMs = 28800000.0;
/// 交互响应 P95 上限（ms——NFR-PERF-01 原文"拟订 P95 响应 ≤200 ms"照抄零
/// 私改；运行期抽查判定用，线性插值分位数）。
constexpr double kInteractionP95LimitMs = 200.0;
/// 无响应窗口上限（ms——NFR-PERF-01 原文"不出现超过 2 s 的无响应窗口"
/// 照抄零私改；逐样本判定）。
constexpr double kUnresponsiveWindowMs = 2000.0;
/// 恢复腿协作取消的查询计数阈值（次——取消查询＝每候选六层起点各一次＋
/// 每批领取一次（i>0）；Quick 相层查询总量≈60,000，取 31,000＝约半程截停；
/// 多 worker 在途批使实际截停点有 ±8 候选级漂移——断言面不依赖精确半程；
/// manifest 冻结）。
constexpr std::uint32_t kCancelAfterQueries = 31000;
/// 交互抽查的轮转节奏（ms——每完成一轮五操作后静默间隔；真实用户交互的
/// 间隙形态：用户不会无间隙连发操作，节奏化采样既贴近真实又把每运行窗
/// 每操作样本量控制在 ~200 量级〔≈10 s 运行窗×每 50 ms 一轮〕——单窗即
/// 独立满足协议 §4.2 每组有效 ≥15，报告 JSON 体量可控；manifest 冻结）。
constexpr std::uint32_t kInteractionCyclePaceMs = 50;
/// 并行归约相对容差（附录 D 第 8 项 P-01 冻结值 1×10⁻¹²——照抄零私改；
/// 恢复腿结论面等价判定的 ε_rel）。
constexpr double kReductionRelTol = 1e-12;
/// 并行归约绝对容差（黄金算例逐例声明值——0.0：零浮点重排结构下要求实测
/// 逐位相等的最严声明口径；manifest/报告同值登记。B-06 同值先例）。
constexpr double kReductionAbsTol = 0.0;

// ---- 运行期交互抽查的 ui 侧数据集参数（B-01 数据集同参复刻——manifest
//      generator.duringRunInteraction 节冻结；常量与 b01-interaction@1
//      manifest 逐字一致，防两处漂移）------------------------------------

/// 黄金规模（PM-16 原文）：对象数（树节点/选择全域共用一份）。
constexpr std::size_t kGoldenObjectCount = 1000;
/// 黄金规模（PM-16 原文）：修订数（数据集修订语境条数）。
constexpr std::size_t kGoldenRevisionCount = 100;
/// 选择操作单次选中规模（50/1000——工业交互的批量选中形态）。
constexpr std::size_t kSelectionBatchSize = 50;
/// 编辑表单字段数（单对象参数表的黄金形态：16 长度＋8 角度）。
constexpr std::size_t kFormFieldCount = 24;
/// 筛选搜索词（"obj-007"——命中 11/1000 的窄子串，过滤语义真实发生）。
constexpr const char* kFilterNeedle = "obj-007";
/// 对象显示名前缀（筛选匹配目标——桩名称源生成参数）。
constexpr const char* kObjectNamePrefix = "obj-";
/// 五分组节点分布（和恰为 kGoldenObjectCount——B1-SPEC §3.1 五分组封闭清单）。
constexpr std::size_t kGroupNodeCounts[5] = {10, 420, 380, 90, 100};

/// 进程级评估工作汇（防消除沉没点——每次内层评估派发把确定性浮点链结果
/// 的 IEEE-754 位模式累加进来；时限腿各样本间、恢复腿与参照腿之间的增量
/// 必须逐位一致——计算确实发生且与线程数/恢复无关的第 4 个确定性观测面）。
std::atomic<std::uint64_t> g_workSink{0};

// =====================================================================
// 通用替身与构造辅助（ParallelThroughputBenchmarkTest/JointRunTest 同款
// 形态——本 TU 自持副本：perf 目标各 TU 自包含先例，不跨 TU 共享夹具）
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

/// 评估调用上下文替身（零取消/零进度——并发安全：只读恒 false；8 worker
/// 并发查询无共享可变态）。
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

/// 第 N 次查询后取消的上下文替身（批间协作取消——TASK-01；并发安全：原子
/// 计数。JointRunTest CancelAfterBatchesContext 同款——恢复腿的截停承载）。
class CancelAfterNContext final : public evidence::IEvaluationContext {
public:
    explicit CancelAfterNContext(std::uint32_t afterQueries)
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
    std::uint32_t m_after;                          ///< 放行查询数（之后即取消）
    mutable std::atomic<std::uint32_t> m_queries{0}; ///< 已发生查询数（原子——
                                                     ///  8 worker 并发查询）
};

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 构造冻结快照（SnapshotBuilder 唯一合法生产者——身份为 builder 计算值）。
evidence::AnalysisSnapshot makeSnapshot(const std::vector<evidence::CaseEntry>& cases,
    const OptimizationConfiguration* configuration=nullptr)
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    evidence::ObjectRefEntry design;
    design.objectId = core::ObjectId::generate();
    design.contentVersion = core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000b7");
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
    repro.productVersion = "perf-b07";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    if(configuration) builder.addConfiguration({"opt.config",optimization::canonicalizeRunConfiguration(*configuration),{}});
    return builder.build(AllPresentClosure{});
}

/// 两工况（均 enabled+mandatory——覆盖矩阵分母；JointRunTest 同款）。
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

/// 单连续绑定（传动比，无量纲——§5.3 #9 StageD 启用的连续条目；采样维度
/// 使 LHS 候选互异＝固定候选集的生成基座）。
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

/// 标准运行配置（八项目标默认激活＋并行度 8——R2 生产形态；manifest 冻结
/// 的 seed/预算常量在本处组装，调用方零参数——标准基准只此一配置）。
OptimizationConfiguration makeStandardRunConfig()
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageD;
    c.seed = kSeed;
    c.parallel.threadCount = kThreadCount;
    c.budget.maxCandidates = kMaxCandidates;
    c.budget.maxVerifiedCandidates = kMaxVerifiedCandidates;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageD);
    c.variables = {makeLengthBinding()};
    optimization::validateConfiguration(c);
    return c;
}

/// 基线根内容版本（CandidateId 公式输入——非保留值）。
core::ContentVersion baselineCv()
{
    return core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000d7");
}

// ---- 确定性浮点工作链（内层评估替身的"固定评估成本"承载——B-06 同源）----

/**
 * @brief 确定性浮点乘加依赖链（每次内层评估派发执行的"评估工作量"替身）。
 *
 * 串行 FP 依赖链使编译器无法向量化/折叠，单次迭代耗时稳定且与线程数无关；
 * 同迭代数 ⇒ 同结果位模式（确定性 NFR-COR-02）。与 B-06 数据集同源同参。
 *
 * @param iterations [in] 迭代次数（kWorkIterations——manifest 冻结）
 * @return 链终值（有限——近 1 步进因子使迭代收敛于不动点邻域）
 */
double benchWorkChain(std::uint32_t iterations)
{
    double x = 0.5;
    constexpr double kA = 1.0 + 1.0 / 1048576.0;  // 步进因子 1+2⁻²⁰（近 1）
    for (std::uint32_t i = 0; i < iterations; ++i) {
        x = x * kA + 1.0e-9;  // 串行依赖——不可向量化/不可折叠
    }
    return x;
}

/// double 位模式读取（确定性汇的载荷——IEEE-754 位模式逐位可比）。
std::uint64_t bitsOf(double v)
{
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return bits;
}

// ---- ③端口内层评估替身（确定性工作量版——无锁并发安全，B-06 同源）----

/**
 * @brief 内层评估器替身（四域各一注册键——每派发执行 benchWorkChain）。
 *
 * 并发契约：evaluate() 只触自身成员与进程级原子汇——8 worker 并发派发无锁
 * 无竞争；基准场景全 Feasible（纯负载——时限测量的规模化承载）。
 */
class BenchInnerEvaluator final : public evidence::IEngineeringEvaluator {
public:
    explicit BenchInnerEvaluator(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        // 确定性工作量＋防消除沉没（链终值两次消费使循环不可折叠）。
        const double x = benchWorkChain(kWorkIterations);
        m_last = x;
        g_workSink.fetch_add(bitsOf(x), std::memory_order_relaxed);
        return {};  // 零违例零搜索未果——候选 Feasible（纯负载）
    }

    /// 最近一次链终值（防消除的第二消费点；对账不读它）。
    double last() const noexcept { return m_last; }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor 引用所指）
    double m_last = 0.0;                        ///< 实例私有沉没位（单线程域）
};

/// 评估器工厂（create 每次新建实例——独占所有权契约；自身无状态并发安全）。
class BenchInnerFactory final : public evidence::IEvaluatorFactory {
public:
    explicit BenchInnerFactory(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }

    const evidence::EvaluatorDescriptor& descriptor() const override { return m_descriptor; }

    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<BenchInnerEvaluator>(m_descriptor);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（注册面）
};

// ---- 指标投影替身（P-OPT-2 缝——候选补丁值的确定性纯函数，B-06 同源）----

/**
 * @brief 联合指标投影替身（P-OPT-2 裁决前测试供给——knownPitfalls 登记缝）。
 *
 * 逐候选互异（补丁标量线性映射进 D-only 五项）使 Pareto 非平凡；并发安全：
 * project() 只读入参（纯函数）——8 worker 并发调用无共享可变态。
 */
class BenchProjector final : public optimization::IJointMetricProjector {
public:
    JointCandidateFacts project(const CandidatePatch& patch,
                                const std::vector<optimization::VariableBinding>&) const override
    {
        // 提取补丁标量（基线候选空补丁 ⇒ 默认值 1.0；items 按 bindingId
        // 字典序，线性扫描 O(项数)=O(1)）。
        double v = 1.0;
        for (const auto& item : patch.items) {
            if (item.bindingId == "mdl.drivetrain.ratio[1]") {
                v = item.scalarValue;
                break;
            }
        }
        // 行程归一 t∈[0,1]（绑定域 [0.8,1.2]——构造边界保证在界内）。
        const double t = (v - 0.8) / 0.4;

        JointCandidateFacts facts;
        StaticMetricFacts& s = facts.staticFacts;
        s.envelope = optimization::EnvelopeFacts{0.0, 0.0, 0.0, 0.5, 0.4, 0.3};  // 和＝1.2 m
        s.linkMasses.push_back({"obj-link-1", 10.0, "provided"});                 // 合成 30 kg
        s.linkMasses.push_back({"obj-link-2", 20.0, "provided"});
        s.pointMargins.push_back({"case-1", 0.9, "1"});                           // 最小 0.7
        s.pointMargins.push_back({"case-2", 0.7, "1"});
        // D-only 五项＝t 的确定性线性映射（全部有限有值——NonFiniteInput/
        // SourceMissing 轨不触发；单位语义见各字段注）。
        facts.jointFacts.cycleTimeS = 8.0 + 5.0 * t;             // s（trj 层口径）
        facts.jointFacts.deviceCost = 7000.0 + 3000.0 * t;       // 目录货币单位（sel 层）
        facts.jointFacts.deviceMassKg = 40.0 + 10.0 * t;         // kg（sel 层）
        facts.jointFacts.jointPositiveWorkJ = 250.0 + 150.0 * t; // J（dyn 层）
        facts.jointFacts.minDriveMargin = 1.2 + 1.2 * t;         // 无量纲（sel 层）
        return facts;
    }
};

// ---- 层切片供给替身（无观测面版——纯函数并发安全，B-06 同源）----------

/**
 * @brief 层切片供给替身（层 token＋上游身份进探针条目——切片身份链真实
 *        参与与生产适配器同构；并发安全：零共享可变态）。
 */
class BenchLayerSlices final : public optimization::IJointLayerSliceProvider {
public:
    const evidence::AnalysisSnapshot* snapshot = nullptr;  ///< 冻结快照（build 基准）

    evidence::InputSlice layerSlice(const std::string& layerToken,
                                    const std::string& evaluationKey,
                                    const CandidatePatch&,
                                    const std::vector<optimization::VariableBinding>&,
                                    const std::vector<JointUpstreamRef>& upstreams) const override
    {
        evidence::SliceBuilder builder;
        builder.setEvaluation(evaluationKey, 1U);
        evidence::DependencyEntry probe;
        probe.key = "perf.layer-probe";
        probe.kind = evidence::DependencyKind::Environment;
        evidence::EnvironmentDependencyPayload payload;
        payload.token = "perf.layer-probe";
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

// ---- 基准夹具（真注册表＋opt Profile＋四域工作量替身——B-06 同源）------

/// Pareto 稳定序单点（buildFront 纯函数——共享实例）。
optimization::ParetoFrontBuilder& sharedParetoBuilder()
{
    static optimization::ParetoFrontBuilder builder;  // 函数级单例（纯函数面）
    return builder;
}

/**
 * @brief 标准运行基准夹具（真 evidence 注册表＋四域工作量替身注册）。
 *
 * 每腿一个实例（注册表/快照在腿内复用——"同一候选集、同一硬件、同一软件
 * 版本"的组内固定）。实例非线程安全——run() 由驱动线程串行调用（run 内部
 * 按 threadCount 派生 worker 并 join）。
 */
struct BenchJointFixture {
    evidence::EvidenceProfileRegistry profiles;   ///< Profile 注册权威（opt 域）
    evidence::EvaluatorRegistry evaluators{profiles}; ///< 评估器注册表（共享锁）
    BenchLayerSlices layerSlices;                 ///< 层切片供给（纯函数）
    NopEvaluationContext ctx;                     ///< 调用上下文（零取消零进度）
    evidence::AnalysisSnapshot snapshotHolder;    ///< 冻结快照（fixture 单例）

    BenchJointFixture()
        : snapshotHolder(makeSnapshot(makeTwoMandatoryCases()))
    {
        layerSlices.snapshot = &snapshotHolder;
        // Profile 注册在前（evidence §13 时序——opt 域空必需项集）。
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = std::string(optimization::kOptProfileId);
        profile.version = "1.0";
        profiles.registerProfile(profile);
        // 四内层评估器注册（各域登记键；全部挂工作量替身工厂——描述符
        // SingleThread＝§6.7 口径，由"每 worker 各持 checker＋create 独立
        // 实例"结构兑现）。
        for (const auto* key :
             {&optimization::kInnerTrjPlanKey, &optimization::kInnerDynRneaKey,
              &optimization::kInnerDtMappingKey,
              &optimization::kInnerSelCombinationKey}) {
            evidence::EvaluatorDescriptor d;
            d.key = std::string(*key);
            d.contractVersion = 1U;
            d.profile.profileId = std::string(optimization::kOptProfileId);
            d.profile.version = "1.0";
            d.supportedModes = {core::EvaluationMode::Quick,
                                core::EvaluationMode::Verified};
            d.stateless = true;
            d.threadSafety = evidence::ThreadSafety::SingleThread;
            evaluators.registerEvaluator(std::make_unique<BenchInnerFactory>(d), {});
        }
    }

    /// 联合编排依赖面（四指针装配）。
    JointLayeredCheckDeps checkerDeps()
    {
        JointLayeredCheckDeps d;
        d.evaluators = &evaluators;
        d.layerSlices = &layerSlices;
        d.producers = &evaluators;  // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;     // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }

    /// 就绪联合请求（快照/覆盖/任务面就绪；基线根身份由调用方传入——跨腿
    /// 钉住后候选集合逐 ID 恒等，等价判定才有逐候选可比性）。
    JointRunRequest readyRequest(const OptimizationConfiguration& config,
                                 const core::ObjectId& baselineRoot)
    {
        snapshotHolder=makeSnapshot(makeTwoMandatoryCases(),&config); // EV-T13：配置与冻结输入同源。
        JointRunRequest req;
        req.config = config;
        req.baselineRoot = baselineRoot;
        req.baselineCv = baselineCv();
        req.snapshot = snapshotHolder;
        req.task = {snapshotHolder.project, snapshotHolder.branch,
                    snapshotHolder.revision, core::RunId::generate(),
                    core::AttemptId{1U}};
        req.coverage = makeCoverage(snapshotHolder);
        return req;
    }
};

// =====================================================================
// 运行期交互抽查采样器（五类交互无头模型层驱动——B-01 同款驱动形态＋
// 运行窗旗标门控；常驻独立采样线程＝"UI 线程测试形态"）
// =====================================================================

// ---- 黄金规模数据集（B-01 同参复刻——只读共享，无 QObject）-----------

/// 显示名构造：obj-0000 … obj-0999（四位零填充——筛选子串的匹配面）。
std::string goldenObjectName(std::size_t index)
{
    std::string name = kObjectNamePrefix;
    const std::string digits = std::to_string(index);
    while (digits.size() + name.size() < std::string{kObjectNamePrefix}.size() + 4) {
        name.push_back('0');
    }
    return name + digits;
}

/**
 * @brief 黄金规模数据集（一次构建、采样线程只读——值面零互访；纯数据无
 *        QObject，跨线程 const 共享安全）。
 */
struct GoldenDataset {
    std::vector<core::ObjectId> objects;               ///< 1,000 业务对象身份
    std::map<core::ObjectId, std::string> nameById;    ///< 身份→显示名（筛选/名称端口共用）
    std::map<std::string, core::ObjectId> idByName;    ///< 运行时名→身份（反解桩用）

    GoldenDataset()
    {
        objects.reserve(kGoldenObjectCount);
        for (std::size_t i = 0; i < kGoldenObjectCount; ++i) {
            core::ObjectId id = core::ObjectId::generate();
            const std::string name = goldenObjectName(i);
            nameById.emplace(id, name);
            idByName.emplace(name, id);
            objects.push_back(std::move(id));
        }
    }
};

// ---- ui 模型层桩族（B-01 同款——名称解析/名称映射/高亮出口/门控/域就绪）-

/// 名称解析端口桩（IUiNameResolver——O(1) 哈希查表，快速返回纪律与生产
/// NameMap 同型）。
class StubNameResolver final : public ui::IUiNameResolver {
public:
    explicit StubNameResolver(const GoldenDataset& data) : m_data(data) {}

    std::optional<std::string> resolveObjectId(core::ObjectId id) const override
    {
        const auto it = m_data.nameById.find(id);
        if (it != m_data.nameById.end()) { return it->second; }
        return std::nullopt;
    }

private:
    const GoldenDataset& m_data;  ///< 数据集只读借用（调用方保证存活期覆盖）
};

/// 运行时名称映射端口桩（IUiRuntimeNameMapPort——SA-05 双射两向；黄金数据
/// 集视全部对象为"已应用"——选择 L2 高亮分支真实到达）。
class StubRuntimeNameMap final : public ui::IUiRuntimeNameMapPort {
public:
    explicit StubRuntimeNameMap(const GoldenDataset& data) : m_data(data) {}

    std::optional<core::ObjectId>
    resolveObjectIdFromRuntimeName(const std::string& runtimeName) const override
    {
        const auto it = m_data.idByName.find(runtimeName);
        if (it != m_data.idByName.end()) { return it->second; }
        return std::nullopt;
    }

    std::optional<std::string>
    resolveRuntimeName(const core::ObjectId& id) const override
    {
        const auto it = m_data.nameById.find(id);
        if (it != m_data.nameById.end()) { return it->second; }
        return std::nullopt;
    }

private:
    const GoldenDataset& m_data;
};

/// 三维高亮出口桩（IUiHighlightOutlet——记录动作次数；零三维知识）。
class StubHighlightOutlet final : public ui::IUiHighlightOutlet {
public:
    std::atomic<int> highlightCalls{0};  ///< highlight 动作计数（观测面）
    std::atomic<int> clearCalls{0};      ///< clear 动作计数

    void highlightRuntimeObject(const std::string&) override { ++highlightCalls; }
    void clearHighlight() override { ++clearCalls; }
};

/// 阶段门控桩（IUiStageGate——本基准放行全部导航：测导航链路时延，非门控
/// 判定语义）。
class AllowAllStageGate final : public ui::IUiStageGate {
public:
    ui::StageGateView presentStage(StageId) const override { return {}; }

    ui::StageGateDecision evaluate(const ui::StageReadinessSnapshot&) const override
    {
        ui::StageGateDecision d;
        d.allowed = true;  // 全放行——导航时延含门控评估调用的真实路径
        return d;
    }
};

/// 域就绪源桩（IUiDomainReadinessSource——每源一域；黄金规模下就绪项按阶段
/// 返回固定清单——汇聚路径真实执行）。
class StubDomainSource final : public ui::IUiDomainReadinessSource {
public:
    explicit StubDomainSource(std::string key) : m_key(std::move(key)) {}

    std::vector<ui::DomainReadinessItem> domainReadiness(StageId) const override
    {
        ui::DomainReadinessItem item;
        item.domainKey = m_key;
        item.verdict = core::EngineeringStatus::NotApplicable;
        item.inputComplete = true;
        return {item};
    }

private:
    std::string m_key;  ///< 域注册键（"modeling"/"requirements"）
};

// ---- 五操作驱动器（B-01 同款——全部操作在采样线程执行）----------------

/**
 * @brief 五操作驱动器（一次装配、逐操作闭包——B-01 InteractionDrivers 同款
 *        形态同参复刻：导航/选择/筛选/编辑/视图切换的产品模型本体全路径）。
 *
 * 线程约束：全部成员只被构造它的采样线程触达（QObject 亲缘性＝创建线程）；
 * 构造即装配黄金规模模型面（树 rebuild 1,000 节点、选择服务、导航模型、
 * 24 字段编辑会话）。操作闭包抛异常＝无效样本（夹具漂移显性化，不静默）。
 */
class InteractionDrivers
{
public:
    explicit InteractionDrivers(const GoldenDataset& data) : m_data(data)
    {
        buildTreeModel();
        buildSelectionService();
        buildNavigationModel();
        buildEditModel();
    }

    // ---- 操作 1：导航（requestNavigate 七阶段轮转——模型全路径）----
    void navigateNext()
    {
        const auto stages = ui::stageIdSequence();
        const StageId target = stages[m_navCursor % stages.size()];
        ++m_navCursor;
        const ui::NavigateResult result = m_navigation->requestNavigate(target);
        if (!result.navigated()) {
            throw std::runtime_error("benchmark: requestNavigate 未放行");
        }
    }

    // ---- 操作 2：选择（selectBusiness 50/1000 旋转子集＋观察者消费）----
    void selectNext()
    {
        std::vector<core::ObjectId> batch;
        batch.reserve(kSelectionBatchSize);
        for (std::size_t i = 0; i < kSelectionBatchSize; ++i) {
            batch.push_back(
                m_data.objects[(m_selectCursor + i) % m_data.objects.size()]);
        }
        m_selectCursor = (m_selectCursor + kSelectionBatchSize) % m_data.objects.size();
        // 唯一业务写入口：去重保序＋L2 正向存在性＋SelectionChange 广播。
        m_selection->selectBusiness(std::move(batch),
                                    ui::SelectionSource::ProjectTree);
    }

    // ---- 操作 3：筛选（applyFilter 规则的模型侧等价施加——B-01 口径②）----
    void filterPass()
    {
        // 大小写不敏感子串（ASCII——与面板 Qt::CaseInsensitive 语义对齐）。
        const std::string needle = kFilterNeedle;
        const auto lowerOf = [](std::string s) {
            for (char& c : s) {
                if (c >= 'A' && c <= 'Z') { c = static_cast<char>(c - 'A' + 'a'); }
            }
            return s;
        };
        const std::string needleLower = lowerOf(needle);
        std::size_t visibleTotal = 0;
        // 逐组取节点序（模型查询）→ 逐节点名称端口解析 → 子串匹配 → 组内
        // 可见计数——规则三段与 IndustrialProjectTree::applyFilter 逐条对齐。
        for (const int g : {0, 1, 2, 3, 4}) {
            const auto group = static_cast<ui::ProjectTreeGroup>(g);
            const std::vector<core::ObjectId> nodes = m_tree->nodesInGroup(group);
            std::size_t visible = 0;
            for (const core::ObjectId& id : nodes) {
                const std::optional<std::string> name = m_nameResolver->resolveObjectId(id);
                if (name.has_value()
                    && lowerOf(*name).find(needleLower) != std::string::npos) {
                    ++visible;
                }
            }
            visibleTotal += visible;
        }
        if (visibleTotal == 0) {
            throw std::runtime_error("benchmark: 筛选零命中（夹具漂移）");
        }
    }

    // ---- 操作 4：编辑（参数表编辑会话全路径：暂存→确认→移交出口）----
    void editNext()
    {
        // 8 字段旋转编辑，表单级确认应用移交编辑出口（SI 真值）。数值经
        // 调用序号单调推进——与基线恒不等值（同值再输入＝无效编辑）。
        ++m_editSequence;
        for (std::size_t k = 0; k < 8; ++k) {
            const std::size_t field = (m_editCursor + k) % kFormFieldCount;
            const double displayValue =
                12.0 + static_cast<double>(k)
                + static_cast<double>(m_editSequence) * 0.5;  // 显示单位数值
            const std::string text = std::to_string(displayValue);
            if (!m_editModel->setEditText(m_fieldKeys[field], text)) {
                throw std::runtime_error("benchmark: 编辑解析被拒（夹具漂移）");
            }
        }
        m_editCursor = (m_editCursor + 8) % kFormFieldCount;
        const ui::ConfirmApplyResult applied =
            m_editModel->confirmApply(*m_editOutlet);
        if (!applied.ok) {
            throw std::runtime_error("benchmark: 确认应用未移交（" + applied.reason
                                     + "）");
        }
    }

    // ---- 操作 5：视图切换（显示单位制式批量切换——KIN-12 仅投影）----
    void viewSwitchNext()
    {
        // 长度制式 mm↔cm 交替（会话显示设置——零脏零移交），随后全字段
        // 显示投影重读（KIN-12 唯一格式化出口的重算面）。
        const core::UnitToken nextUnit =
            m_viewSwitchToggle ? (*core::UnitToken::find("cm")) : (*core::UnitToken::find("mm"));
        m_viewSwitchToggle = !m_viewSwitchToggle;
        m_editModel->setDisplayUnitForKind(core::QuantityKind::Length, nextUnit);
        double checksum = 0.0;
        for (const std::string& key : m_fieldKeys) {
            checksum += static_cast<double>(m_editModel->displayValueText(key).size());
        }
        if (checksum <= 0.0) {
            throw std::runtime_error("benchmark: 显示投影空读（夹具漂移）");
        }
    }

    /// 排队事件消化（会话半区——本基准无跨线程排队事件，近零开销的形式性
    /// 消化，与 B-01 形态对齐）。
    void drainQueuedEvents()
    {
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::processEvents();
        }
    }

    /// 选中消费观察计数（选择广播真实到达的证据——报告登记用）。
    int selectionObserverHits() const { return m_selectionObserver.hits; }

    /// 编辑出口移交计数（确认应用真实到达的证据）。
    int editOutletHits() const { return m_editOutlet->appliedCount.load(); }

private:
    // ---- 装配（构造序——B-01 同款）----

    void buildTreeModel()
    {
        // 五分组 × 冻结分布的节点供给（B1-SPEC §5.1 Provider 协议）。
        for (int g = 0; g < 5; ++g) {
            class GroupProvider final : public ui::IUiTreeNodesProvider
            {
            public:
                GroupProvider(std::string key, int group,
                              std::vector<ui::ProjectTreeNode> nodes)
                    : m_key(std::move(key)), m_group(group),
                      m_nodes(std::move(nodes))
                {
                }

                std::string domainKey() const override { return m_key; }

                std::vector<ui::ProjectTreeNode> treeNodes() const override
                {
                    return m_nodes;
                }

            private:
                std::string m_key;
                int m_group;
                std::vector<ui::ProjectTreeNode> m_nodes;
            };

            std::vector<ui::ProjectTreeNode> nodes;
            nodes.reserve(kGroupNodeCounts[g]);
            for (std::size_t k = 0; k < kGroupNodeCounts[g]; ++k) {
                ui::ProjectTreeNode node;
                node.objectId = m_data.objects[m_treeCursor];
                node.group = static_cast<ui::ProjectTreeGroup>(g);
                node.depth = 0;  // 扁平黄金树（分组承载规模——manifest 冻结）
                ++m_treeCursor;
                nodes.push_back(std::move(node));
            }
            m_tree->addProvider(std::make_shared<GroupProvider>(
                "perf-domain-" + std::to_string(g), g, std::move(nodes)));
        }
        const ui::TreeRebuildReport report = m_tree->rebuild();
        if (!report.ok || report.nodeCount != kGoldenObjectCount) {
            throw std::runtime_error("benchmark: 树重建未达黄金规模（"
                                     + std::to_string(report.nodeCount) + "）");
        }
    }

    void buildSelectionService()
    {
        ui::SelectionService::Deps deps;
        deps.nameMap = std::make_shared<StubRuntimeNameMap>(m_data);
        deps.treeLocator = [this](const core::ObjectId& id) {
            // L3 落点缝：真实树模型定位（locate 命中即定位成功）。
            return m_tree->locate(id).has_value();
        };
        deps.highlightOutlet = m_highlightOutlet;
        deps.devLog = nullptr;  // 显式声明：无日志场景（端口可空纪律）
        m_selection = std::make_unique<ui::SelectionService>(std::move(deps));
        m_selectionSub = m_selection->subscribe(m_selectionObserver);
    }

    void buildNavigationModel()
    {
        ui::StageNavigationModelDeps deps;
        deps.gate = &m_gate;
        deps.epochSource = [] { return std::uint64_t{1}; };
        deps.writableSource = [] { return true; };
        deps.sessionNavigableSource = [] { return true; };
        deps.sevenStateFacts =
            [](StageId) { return std::optional<ui::StatusFacts>{ui::StatusFacts{}}; };
        deps.domainSources.push_back(&m_domainA);
        deps.domainSources.push_back(&m_domainB);
        deps.initialStage = StageId::Modeling;
        m_navigation = ui::createStageNavigationModel(std::move(deps));
    }

    void buildEditModel()
    {
        // 24 字段黄金参数表：16 长度（SI m／显示 mm）＋8 角度（SI rad／
        // 显示 deg）——makeQuantityFieldSpec 装配期保证单位一致性。
        std::vector<ui::QuantityFieldSpec> specs;
        specs.reserve(kFormFieldCount);
        const auto mm = core::UnitToken::find("mm");
        const auto deg = core::UnitToken::find("deg");
        const auto m = core::UnitToken::find("m");
        const auto rad = core::UnitToken::find("rad");
        if (!mm || !deg || !m || !rad) {
            throw std::runtime_error("benchmark: 单位注册表缺 token（夹具漂移）");
        }
        for (std::size_t i = 0; i < kFormFieldCount; ++i) {
            const std::string key = "field-" + std::to_string(i);
            m_fieldKeys.push_back(key);
            if (i < 16) {
                specs.push_back(ui::makeQuantityFieldSpec(
                    key, "长度参数" + std::to_string(i), core::QuantityKind::Length,
                    *m, *mm));
            } else {
                specs.push_back(ui::makeQuantityFieldSpec(
                    key, "角度参数" + std::to_string(i), core::QuantityKind::Angle,
                    *rad, *deg));
            }
        }
        m_editModel = std::make_unique<ui::ParamEditModel>(std::move(specs));
        for (std::size_t i = 0; i < kFormFieldCount; ++i) {
            m_editModel->setBaseline(m_fieldKeys[i], baselineValue(i));
        }
    }

    /// 字段基线值（SI 真值——setBaseline 契约：长度 m／角度 rad）。
    double baselineValue(std::size_t field) const
    {
        return field < 16 ? 0.5 + 0.01 * static_cast<double>(field)      // 单位 m
                          : 0.2 + 0.05 * static_cast<double>(field - 16); // 单位 rad
    }

    /// 选择观察者（广播消费计数——重入禁令下只计数不回写）。
    class CountingSelectionObserver final : public ui::IUiSelectionObserver
    {
    public:
        void onSelectionChanged(const ui::SelectionChange&) override { ++hits; }
        int hits = 0;
    };

    /// 编辑出口桩（ParamEditSet 移交接收——SI 真值落点）。
    class CountingEditOutlet final : public ui::IFormEditOutlet
    {
    public:
        void applyEdits(const ui::ParamEditSet&) override { ++appliedCount; }
        std::atomic<int> appliedCount{0};
    };

    const GoldenDataset& m_data;                        ///< 黄金数据集借用
    std::shared_ptr<StubNameResolver> m_nameResolver =  ///< 名称解析桩
        std::make_shared<StubNameResolver>(m_data);
    std::unique_ptr<ui::ProjectTreeModel> m_tree =      ///< 树模型（1,000 节点）
        std::make_unique<ui::ProjectTreeModel>();
    std::shared_ptr<StubHighlightOutlet> m_highlightOutlet = ///< 高亮出口桩
        std::make_shared<StubHighlightOutlet>();
    std::unique_ptr<ui::SelectionService> m_selection;  ///< 选择服务
    CountingSelectionObserver m_selectionObserver;      ///< 选择观察者
    std::unique_ptr<core::IEventSubscription> m_selectionSub; ///< 订阅句柄
    AllowAllStageGate m_gate;                           ///< 全放行门控桩
    StubDomainSource m_domainA{"modeling"};             ///< 域就绪源 A
    StubDomainSource m_domainB{"requirements"};         ///< 域就绪源 B
    std::unique_ptr<ui::IStageNavigationModel> m_navigation; ///< 导航模型
    std::unique_ptr<ui::ParamEditModel> m_editModel;    ///< 编辑会话模型
    std::shared_ptr<CountingEditOutlet> m_editOutlet =  ///< 编辑出口桩
        std::make_shared<CountingEditOutlet>();
    std::vector<std::string> m_fieldKeys;               ///< 字段键（注册序）
    std::size_t m_navCursor = 1;                        ///< 导航轮转游标（首步→Requirements）
    std::size_t m_selectCursor = 0;                     ///< 选中批旋转游标
    std::size_t m_editCursor = 0;                       ///< 编辑字段旋转游标
    std::size_t m_editSequence = 0;                     ///< 编辑调用序号（防同值无效编辑）
    bool m_viewSwitchToggle = false;                    ///< 制式切换交替位
    std::size_t m_treeCursor = 0;                       ///< 树节点分配游标（装配期）
};

// ---- 运行窗门控采样器 --------------------------------------------------

/**
 * @brief 运行期交互抽查采样器（常驻独立采样线程＝"UI 线程测试形态"）。
 *
 * 线程模型：驱动器（ui 模型面）在采样线程内构造与使用（QObject 亲缘性）；
 * 驱动线程在 orch.run() 前后置位/复位 runWindowActive 旗标；采样线程按
 * kInteractionCyclePaceMs 节奏轮转执行五操作（轮间静默＝真实用户交互间隙
 * 形态——单运行窗每操作样本 ~200 量级，单窗即独立满足 §4.2 每组 ≥15）——
 * 仅"开始与结束都落在运行窗内"的样本计入统计（运行期间语义的门控承载），
 * 窗外样本丢弃并计数（观测面，报告登记）。
 *
 * 逐操作预热：每组首次执行标记 excluded（§4.2"剔除各组首次预热"——发生在
 * 首个运行窗之前的夹具预热，不属于任何统计分组）。
 */
class DuringRunInteractionSampler {
public:
    /// 启动采样线程（黄金数据集 const 共享——纯数据无 QObject）。
    void start(const GoldenDataset& data)
    {
        m_thread = std::thread([this, &data] { procedure(data); });
    }

    /// 停止并汇合（幂等保护）；返回前完成逐组统计回填。
    void stopAndJoin()
    {
        if (!m_thread.joinable()) {
            return;
        }
        m_stop.store(true, std::memory_order_release);
        m_thread.join();
        finalizeGroups();
    }

    /// 采样线程是否正常启动（驱动器装配失败＝夹具缺陷——测试断言收口）。
    bool startupOk() const { return m_startupOk; }

    /// 启动失败原因（startupOk()==false 时非空）。
    const std::string& startupError() const { return m_startupError; }

    /// 运行窗旗标（驱动线程在 orch.run() 前后置位/复位——采样线程读取）。
    std::atomic<bool> runWindowActive{false};

    /// 窗外丢弃样本计数（观测面——报告登记；非统计分母）。
    std::uint64_t discardedOutOfWindow() const { return m_discarded.load(); }

    /// 逐组报告（stopAndJoin 后可读——opId 固定序 nav/select/filter/edit/
    /// viewswitch，stateId＝"during-run"）。
    const std::vector<testkit::InteractionGroupReport>& groups() const { return m_groups; }

    /// 全部操作有效样本总计（报告登记用——运行期抽查的样本规模面）。
    std::size_t totalValidSamples() const
    {
        std::size_t n = 0;
        for (const auto& g : m_groups) {
            n += g.validCount;
        }
        return n;
    }

private:
    /// 采样线程主过程（驱动器装配→逐操作预热→运行窗循环采样）。
    void procedure(const GoldenDataset& data)
    {
        try {
            InteractionDrivers drivers(data);
            m_driversOk = true;
            // 逐操作预热一次（excluded——§4.2 首次预热剔除；发生在首个运行
            // 窗之前，不属于任何统计分组——夹具首触冷路径不进统计）。
            for (int k = 0; k < 5; ++k) {
                testkit::InteractionSample s;
                s.excluded = true;
                const auto t0 = std::chrono::steady_clock::now();
                try {
                    runOp(drivers, k);
                } catch (const std::exception& e) {
                    s.valid = false;
                    s.invalidReason = e.what();
                }
                const auto t1 = std::chrono::steady_clock::now();
                s.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                m_pending[k].push_back(s);
            }
            // 运行窗循环：五操作轮转采样（节奏 kInteractionCyclePaceMs——
            // 轮间静默在计时区外），直至停止旗标。
            while (!m_stop.load(std::memory_order_acquire)) {
                for (int k = 0; k < 5 && !m_stop.load(std::memory_order_acquire); ++k) {
                    const bool startActive = runWindowActive.load(std::memory_order_acquire);
                    testkit::InteractionSample s;
                    const auto t0 = std::chrono::steady_clock::now();
                    try {
                        runOp(drivers, k);
                    } catch (const std::exception& e) {
                        s.valid = false;
                        s.invalidReason = e.what();
                    }
                    const auto t1 = std::chrono::steady_clock::now();
                    s.durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
                    const bool endActive = runWindowActive.load(std::memory_order_acquire);
                    if (startActive && endActive) {
                        m_pending[k].push_back(s);  // 运行窗内样本——计入统计
                    } else {
                        m_discarded.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                drivers.drainQueuedEvents();
                // 轮转节奏（轮间静默——真实用户交互间隙形态；停止旗标最长
                // 一个节奏周期后生效）。
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(kInteractionCyclePaceMs));
            }
            // 观测面（操作真实到达证据——报告登记）。
            m_selectionHits = drivers.selectionObserverHits();
            m_editHits = drivers.editOutletHits();
            m_startupOk = true;
        } catch (const std::exception& e) {
            m_startupError = e.what();
            m_startupOk = false;
        }
    }

    /// 按固定序分派第 k 个操作（nav/select/filter/edit/viewswitch）。
    static void runOp(InteractionDrivers& drivers, int k)
    {
        switch (k) {
        case 0: drivers.navigateNext(); break;
        case 1: drivers.selectNext(); break;
        case 2: drivers.filterPass(); break;
        case 3: drivers.editNext(); break;
        default: drivers.viewSwitchNext(); break;
        }
    }

    /// 逐组统计回填（P95 线性插值/max/min——只由 valid∧!excluded 构成；
    /// testkit 单点函数，禁 3 次中位数替代）。
    void finalizeGroups()
    {
        static const char* kOpIds[5] = {"nav", "select", "filter", "edit", "viewswitch"};
        m_groups.clear();
        for (int k = 0; k < 5; ++k) {
            testkit::InteractionGroupReport g;
            g.opId = kOpIds[k];
            g.stateId = "during-run";
            g.warmupRuns = 1;
            std::vector<double> validMs;
            for (const testkit::InteractionSample& s : m_pending[k]) {
                g.samples.push_back(s);
                if (s.valid && !s.excluded) {
                    ++g.validCount;
                    validMs.push_back(s.durationMs);
                } else if (!s.valid) {
                    ++g.invalidCount;
                }
            }
            if (!validMs.empty()) {
                g.p95Ms = testkit::percentileLinearInterpolated(validMs, 0.95);
                g.maxValidMs = *std::max_element(validMs.begin(), validMs.end());
                g.minValidMs = *std::min_element(validMs.begin(), validMs.end());
            }
            m_groups.push_back(std::move(g));
            m_pending[k].clear();
        }
    }

    std::thread m_thread;                          ///< 采样线程（start~join）
    std::atomic<bool> m_stop{false};               ///< 停止旗标
    bool m_startupOk = false;                      ///< 驱动器装配成功标记
    bool m_driversOk = false;                      ///< 驱动器在位标记（防消除）
    std::string m_startupError;                    ///< 装配失败原因
    std::vector<testkit::InteractionSample> m_pending[5]; ///< 逐操作原始序列
    std::vector<testkit::InteractionGroupReport> m_groups; ///< 逐组统计报告
    std::atomic<std::uint64_t> m_discarded{0};     ///< 窗外丢弃计数
    int m_selectionHits = 0;                       ///< 选择广播消费计数（观测面）
    int m_editHits = 0;                            ///< 编辑移交计数（观测面）

public:
    /// 观测面读取（stopAndJoin 后有效）。
    int selectionHits() const { return m_selectionHits; }
    int editHits() const { return m_editHits; }
};

// =====================================================================
// 单样本执行（时限腿——计时区间＝run() 全程端到端墙钟）
// =====================================================================

/**
 * @brief 单个标准运行样本（原始观测——有效/预热/无效三态全量留痕）。
 *
 * 只保留墙钟/计数/工作汇增量，不保留结论面（时限腿不做等价对账——结论面
 * 等价归恢复腿；16 个万级记录集的驻留会使测量进程内存无谓膨胀）。
 */
struct RunWallSample {
    testkit::InteractionSample sample = {};  ///< 墙钟与三态标记（ms——steady_clock）
    std::uint64_t evaluatedCount = 0;        ///< quickEvaluated＋verifiedEvaluated
    std::uint64_t workDelta = 0;             ///< 本运行的工作汇增量（确定性观测面）
    RunPhase endPhase = RunPhase::Draft;     ///< 终态运行状态（有效样本恒 Completed）
};

/**
 * @brief 执行一次 10,000 Quick＋100 Verified 标准运行并采样。
 *
 * 区间纪律（§4.7 端到端墙钟）：配置组装/请求组装/编排器构造全部在 t0 之前；
 * 计时只罩 JointRunOrchestrator::run() 全程——评估两相＋保守筛选＋Pareto
 * ＋检查点产出全部在区间内。运行窗旗标在 run() 前后置位/复位（交互采样器
 * 的"运行期间"门控）。
 *
 * @param fx           [in] 夹具（注册表/快照/上下文——调用方保证存活）
 * @param baselineRoot [in] 基线根身份（跨腿钉住——CandidateId 公式锚）
 * @param warmup       [in] 是否预热样本（标记不计入统计）
 * @param sampler      [in] 交互采样器（可空——恢复腿不带交互抽查）
 * @return 样本（valid=false 时其余字段无意义——原因在 sample 内）
 */
RunWallSample runOneStandardRun(BenchJointFixture& fx,
                                const core::ObjectId& baselineRoot,
                                bool warmup,
                                DuringRunInteractionSampler* sampler)
{
    RunWallSample out;
    out.sample.excluded = warmup;

    // ---- 计时区间外的装配面（不构成运行时限）----
    const OptimizationConfiguration config = makeStandardRunConfig();
    JointRunRequest req = fx.readyRequest(config, baselineRoot);
    JointLayeredCheckDeps deps = fx.checkerDeps();
    optimization::JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    BenchProjector projector;
    d.projector = &projector;
    d.paretoBuilder = &sharedParetoBuilder();
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    // ---- 计时区间＝run() 全程端到端墙钟（真实 steady_clock——B-07 证据）--
    const std::uint64_t sinkBefore = g_workSink.load(std::memory_order_relaxed);
    if (sampler != nullptr) {
        sampler->runWindowActive.store(true, std::memory_order_release);
    }
    const auto t0 = std::chrono::steady_clock::now();
    JointRunResult result;
    try {
        result = orch.run(req, fx.ctx);
        const auto t1 = std::chrono::steady_clock::now();
        if (sampler != nullptr) {
            sampler->runWindowActive.store(false, std::memory_order_release);
        }
        out.sample.durationMs
            = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 有效判据①（§4.2"运行完整结束"）：正常完成相。
        out.endPhase = result.runPhase;
        if (!result.runCompleted || result.runPhase != RunPhase::Completed) {
            out.sample.valid = false;
            out.sample.invalidReason = "run 未完整结束（runCompleted=false 或 phase!=Completed）";
        }
        // 有效判据②：评估计数恰为标准基准规模（10,000＋100——漂移即测量
        // 对象失真）。
        out.evaluatedCount = static_cast<std::uint64_t>(result.audit.base.quickEvaluated)
                             + static_cast<std::uint64_t>(result.audit.base.verifiedEvaluated);
        if (out.sample.valid && out.evaluatedCount != kExpectedEvaluations) {
            out.sample.valid = false;
            out.sample.invalidReason = "评估计数漂移（期望 "
                                       + std::to_string(kExpectedEvaluations) + " 实测 "
                                       + std::to_string(out.evaluatedCount) + "）";
        }
    } catch (const std::exception& e) {
        const auto t1 = std::chrono::steady_clock::now();
        if (sampler != nullptr) {
            sampler->runWindowActive.store(false, std::memory_order_release);
        }
        out.sample.durationMs
            = std::chrono::duration<double, std::milli>(t1 - t0).count();
        out.sample.valid = false;
        out.sample.invalidReason = e.what();  // 测量区间不完整——留痕原因
    }
    out.workDelta = g_workSink.load(std::memory_order_relaxed) - sinkBefore;
    return out;
}

// =====================================================================
// 恢复腿结论面等价判定（B-06 compareConclusions 同款——逐元素对账）
// =====================================================================

/**
 * @brief 结论面等价判定汇总（逐元素对账的观测面——断言面在调用方）。
 */
struct EquivalenceSummary {
    std::size_t checkedRecords = 0;  ///< 已对账记录数（Quick＋Verified）
    std::size_t checkedMetrics = 0;  ///< 已对账指标元素数
    double maxRelDiff = 0.0;         ///< 最大逐元素相对差（ref≠0 元素）
    bool bitwiseAll = true;          ///< 全部数值元素逐位相等（零重排结构的
                                     ///  实测形态——ε_abs=0 声明的实测佐证）
    bool ok = true;                  ///< 全部结构等价（计数/序/状态/审计）
    std::string firstMismatch;       ///< 首个失配描述（ok==false 时非空）
};

/**
 * @brief 恢复腿结论面对照参照全量运行（NFR-COR-02/附录 D 第 8 项逐元素——
 *        "检查点恢复不改变结论"的判定面）：①记录计数与候选序；②逐候选
 *        状态；③八项指标 |a−b| ≤ ε_rel·|ref|＋ε_abs 逐元素（ε_abs＝0 黄金
 *        算例声明）；④Pareto 序与可行集规模；⑤审计计数（AT-35——恢复不
 *        重复统计）。任一失配即 ok=false＋首失配描述。
 */
EquivalenceSummary compareConclusions(const JointRunResult& resumed, const JointRunResult& ref)
{
    EquivalenceSummary sum;
    auto fail = [&sum](const std::string& why) {
        sum.ok = false;
        if (sum.firstMismatch.empty()) {
            sum.firstMismatch = why;
        }
    };

    // ① 记录计数与候选序（Quick 与 Verified 两相分别对账）。
    if (resumed.quickRecords.size() != ref.quickRecords.size()) {
        fail("Quick 记录数失配（ref=" + std::to_string(ref.quickRecords.size())
             + " resumed=" + std::to_string(resumed.quickRecords.size()) + "）");
    } else if (resumed.verifiedRecords.size() != ref.verifiedRecords.size()) {
        fail("Verified 记录数失配（ref=" + std::to_string(ref.verifiedRecords.size())
             + " resumed=" + std::to_string(resumed.verifiedRecords.size()) + "）");
    }
    if (!sum.ok) {
        return sum;  // 计数失配无从逐元素——提前返回（首失配已留痕）
    }

    // ②③ 两相逐记录：候选序＋状态＋八项指标逐元素。
    const std::vector<optimization::JointRunRecord>* refPhases[2] = {
        &ref.quickRecords, &ref.verifiedRecords};
    const std::vector<optimization::JointRunRecord>* resPhases[2] = {
        &resumed.quickRecords, &resumed.verifiedRecords};
    for (int phase = 0; phase < 2; ++phase) {
        const auto& refRecords = *refPhases[phase];
        const auto& resRecords = *resPhases[phase];
        for (std::size_t i = 0; i < refRecords.size(); ++i) {
            const auto& r = refRecords[i];
            const auto& p = resRecords[i];
            ++sum.checkedRecords;
            if (!(r.base.candidateId == p.base.candidateId)) {
                fail("候选序漂移（相 " + std::to_string(phase) + " 下标 "
                     + std::to_string(i) + "——恢复必须与全量同序）");
                return sum;
            }
            if (r.base.status != p.base.status) {
                fail("候选状态失配（下标 " + std::to_string(i) + "）");
                return sum;
            }
            // 八项指标逐元素（MetricId 枚举序——kMetricCount 全覆盖）。
            for (int m = 0; m < static_cast<int>(optimization::kMetricCount); ++m) {
                const auto id = static_cast<MetricId>(m);
                const auto a = p.base.metrics.valueOf(id);
                const auto b = r.base.metrics.valueOf(id);
                ++sum.checkedMetrics;
                if (a.has_value() != b.has_value()) {
                    fail("指标在位性失配（下标 " + std::to_string(i) + " 指标 "
                         + std::to_string(m) + "）");
                    return sum;
                }
                if (!a.has_value()) {
                    continue;  // 双缺（"—"）——无数值可比，等价
                }
                const double diff = std::abs(*a - *b);
                const double refAbs = std::abs(*b);
                // 附录 D 第 8 项通用比较公式（C4）——零参考退化为 ε_abs。
                if (!(diff <= kReductionRelTol * refAbs + kReductionAbsTol)) {
                    fail("归约超容差（下标 " + std::to_string(i) + " 指标 "
                         + std::to_string(m) + " diff=" + std::to_string(diff)
                         + "）");
                    return sum;
                }
                if (diff > sum.maxRelDiff && refAbs > 0.0) {
                    sum.maxRelDiff = diff / refAbs;
                }
                if (diff != 0.0) {
                    sum.bitwiseAll = false;
                }
            }
        }
    }

    // ④ Pareto 序与可行集规模（等价集合＋稳定排序的结论面承载）。
    if (resumed.pareto.entries.size() != ref.pareto.entries.size()) {
        fail("Pareto 条目数失配（ref=" + std::to_string(ref.pareto.entries.size())
             + " resumed=" + std::to_string(resumed.pareto.entries.size()) + "）");
        return sum;
    }
    for (std::size_t i = 0; i < ref.pareto.entries.size(); ++i) {
        if (!(ref.pareto.entries[i].candidateId == resumed.pareto.entries[i].candidateId)) {
            fail("Pareto 序漂移（下标 " + std::to_string(i) + "）");
            return sum;
        }
    }
    if (resumed.pareto.feasibleIds.size() != ref.pareto.feasibleIds.size()) {
        fail("可行集规模失配");
        return sum;
    }

    // ⑤ 审计计数（AT-35——恢复不重复统计的结论面承载；工作汇增量对账在
    // 调用方采样层执行）。
    if (resumed.audit.base.candidatesGenerated != ref.audit.base.candidatesGenerated
        || resumed.audit.base.quickEvaluated != ref.audit.base.quickEvaluated
        || resumed.audit.base.quickScreenedOut != ref.audit.base.quickScreenedOut
        || resumed.audit.base.verifiedEvaluated != ref.audit.base.verifiedEvaluated) {
        fail("审计计数失配（AT-35 统计不重复违约）");
        return sum;
    }
    return sum;
}

// =====================================================================
// 检查点集成夹具（JointRunTest JointCheckpointEnv 同款——真协调器＋写真实
// 临时文件的归档端口替身承载宿主角色）
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
        devLog.push_back({channel, message});
    }
    std::vector<core::DiagnosticRecord> records; ///< 已上报诊断
    std::vector<std::pair<std::string, std::string>> devLog; ///< 开发级文本
};

/// 归档端口替身：begin 记录请求、writeBatch 把批次字节写进真实磁盘 runDir、
/// finalize 写 manifest.json 标记"正式"（JointRunTest FakeArchiveDisk 同款）。
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

/**
 * @brief 检查点集成夹具（临时目录＋固定时钟——宿主角色的替身承载）。
 *
 * N8 纪律的宿主侧形态：本域只产中间状态字节（encodeJointCheckpointState），
 * 磁盘编址/manifest 发布/摘要全归 execution::CheckpointCoordinator——本
 * 夹具把编排产出的检查点状态经**真协调器**写出真实磁盘文件并在恢复腿经
 * **真 restore** 装载（同版本判定第三条件＝契约版本一致）。
 */
class CheckpointEnv {
public:
    CheckpointEnv()
    {
        std::error_code ec;
#ifdef _WIN32
        base = fs::temp_directory_path(ec) / "ird_opt_b07_ckpt"
               / std::to_string(::GetCurrentProcessId());
#else
        base = fs::temp_directory_path(ec) / "ird_opt_b07_ckpt";
#endif
        fs::create_directories(base, ec);
        caseDir = base / ("case" + std::to_string(++counter));
        fs::create_directories(caseDir, ec);
        port = std::make_unique<FakeArchiveDisk>();
        sink = std::make_unique<CollectingSink>();
        coordinator = std::make_unique<execution::CheckpointCoordinator>(
            *port, *sink, [] { return std::chrono::system_clock::time_point{}; });
    }
    ~CheckpointEnv()
    {
        std::error_code ec;
        fs::remove_all(caseDir, ec);
    }

    /// 宿主角色：把编排产出的检查点状态经真协调器写出（N8——磁盘编址/
    /// manifest/摘要全归 execution）。
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
        req.record.randomSeed = kSeed;
        req.record.threadCount = kThreadCount;
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

    /// 读回磁盘上的域中间状态字节（round-trip 完整性的宿主恢复面）。
    std::vector<std::uint8_t> readStateBytes() const
    {
        for (const auto& dirEntry : fs::recursive_directory_iterator(
                 caseDir / "checkpoints")) {
            if (dirEntry.is_regular_file()
                && dirEntry.path().filename() == "joint-state.bin") {
                std::ifstream in(dirEntry.path(), std::ios::binary);
                return std::vector<std::uint8_t>(
                    std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>());
            }
        }
        return {};
    }

    fs::path base;    ///< 临时根
    fs::path caseDir; ///< 用例目录
    std::unique_ptr<FakeArchiveDisk> port;
    std::unique_ptr<CollectingSink> sink;
    std::unique_ptr<execution::CheckpointCoordinator> coordinator;
    static int counter;
};
int CheckpointEnv::counter = 0;

// =====================================================================
// 报告组装（testkit PerfSessionReport 复用——协议 §4.8 留痕面）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 时限腿分组报告（由原始样本序列组装——统计面经 testkit 单点函数；P95 仅
/// 作分布参考，判定统计量＝有效样本最大值在判定处另行计算）。
testkit::InteractionGroupReport makeRunGroupReport(const std::vector<RunWallSample>& samples)
{
    testkit::InteractionGroupReport g;
    g.opId = "opt-run";
    g.stateId = "standard-run-threadCount8";
    std::vector<double> validMs;
    for (const RunWallSample& s : samples) {
        testkit::InteractionSample raw;
        raw.durationMs = s.sample.durationMs;
        raw.excluded = s.sample.excluded;
        raw.valid = s.sample.valid;
        raw.invalidReason = s.sample.invalidReason;
        g.samples.push_back(raw);
        if (raw.valid && !raw.excluded) {
            ++g.validCount;
            validMs.push_back(raw.durationMs);
        } else if (!raw.valid) {
            ++g.invalidCount;
        } else {
            ++g.warmupRuns;
        }
    }
    if (!validMs.empty()) {
        g.p95Ms = testkit::percentileLinearInterpolated(validMs, 0.95);
        g.maxValidMs = *std::max_element(validMs.begin(), validMs.end());
        g.minValidMs = *std::min_element(validMs.begin(), validMs.end());
    }
    return g;
}

/// 组装会话报告头（标识/版本/数据集/环境——测量组由调用方追加）。
testkit::PerfSessionReport makeSessionHeader(const std::string& caseId)
{
    testkit::PerfSessionReport r;
    r.caseId = caseId;
    r.baselineDoc = "performance-baseline #2 B-07";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b07-opt-run-time";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单。
    r.memoryInfo = "见首测报告硬件清单（traceability/benchmarks/"
                   "wp23-t08-opt-run-time-baseline.md §2）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "单进程单测试串行执行（run 内部 8 worker 并行＋常驻"
                       "交互采样线程除外——采样线程是测量面本体）；测量期间"
                       "无其他基准并行（performance-baseline §4 测量纪律）";
    // 口径登记（报告自含——验收对照面）。
    r.notes.push_back(
        "B-07 为时序/规模阈值类判定（performance-baseline §2 统计口径区分）："
        "时限判定统计量＝有效样本运行墙钟最大值 ≤28,800,000 ms（T03/T06 阈值"
        "类先例同口径——非 P95 非中位数）；运行期交互抽查的 P95 判定属 NFR-"
        "PERF-01 自身口径（线性插值分位数——禁 3 次中位数替代），两套口径各"
        "归其位不互相换轨；verdicts.measuredP95Ms 字段沿夹具 schema 承载判定"
        "统计量（时限 verdict＝有效样本最大墙钟〔ms〕——字段名沿 schema 的"
        "登记性复用，T03/T06/T07 同款纪律）");
    r.notes.push_back(
        "替身边界：内层四域评估器＝确定性浮点迭代链替身（每派发 "
        + std::to_string(kWorkIterations)
        + " 次串行 FP 乘加——与 B-06 数据集同源工作量）＋P-OPT-2 投影替身——"
        "证明编排器在 10,000＋100 候选规模下两级批量推进＋并行分片＋检查点"
        "产出/恢复的端到端墙钟规模化，非业务评估器算力成本下的 8 小时时限"
        "画像（归集成模式扩展测量挂点——首测报告登记，集成构建由合入批执行）");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b07-opt-run-time-manifest.json"
        "（datasetId ird-perf-b07-opt-run-time@1——参数冻结，升级换版本；运行"
        "期交互抽查的 ui 侧数据集＝b01-interaction@1 同参复刻自持冻结）");
    return r;
}

}  // namespace

// =====================================================================
// B-07 达标判定用例 1：基准时限＋运行期间 PERF-01（10,000 Quick＋100
// Verified 标准运行 8 小时内结束＋运行窗内五类交互 P95 ≤200 ms）
// =====================================================================

TEST(OptRunTimeBenchmark, StandardRunTimeLimitAndInteractionDuringRun_WP23T08_B07)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-06", "NFR-PERF-01"},
                  std::vector<std::string>{"AT-35"});

    // QCoreApplication（模型测试豁免形态——ui 模型面运行期标记；TU 内自持
    // 构造/析构，不改动本目标 main 的非 Qt 形态声明——B-06 用例零影响）。
    int fakeArgc = 1;
    char fakeName[] = "sdurws_ird_optimization_perf_test";
    char* fakeArgv[] = {fakeName, nullptr};
    QCoreApplication app(fakeArgc, fakeArgv);

    // 基线根身份（全部腿同值——CandidateId 公式锚；恢复腿经检查点续跑的
    // 候选集合可比性依赖此锚）。
    const core::ObjectId sharedBaselineRoot = core::ObjectId::generate();

    // ---- 运行期交互抽查采样器（常驻线程＝"UI 线程测试形态"）------------
    // 黄金数据集在 gtest 线程构建（纯数据），驱动器在采样线程内构造
    // （QObject 亲缘性＝创建线程）。
    GoldenDataset dataset;
    DuringRunInteractionSampler sampler;
    sampler.start(dataset);

    // ---- 时限腿：预热 1＋有效 15（§4.2 样本纪律；全部运行窗对采样器开门）-
    BenchJointFixture fx;
    std::vector<RunWallSample> samples;
    std::uint64_t anchorWork = 0;  // 工作汇锚（确定性链——有效样本增量恒量）
    for (std::size_t i = 0; i < kWarmupRuns; ++i) {
        samples.push_back(runOneStandardRun(fx, sharedBaselineRoot, /*warmup=*/true,
                                            &sampler));
    }
    {
        std::size_t invalidCount = 0;
        while (std::count_if(samples.begin(), samples.end(),
                             [](const RunWallSample& s) {
                                 return s.sample.valid && !s.sample.excluded;
                             })
               < kValidSamples) {
            RunWallSample s = runOneStandardRun(fx, sharedBaselineRoot, /*warmup=*/false,
                                                &sampler);
            if (!s.sample.valid) {
                ++invalidCount;
                std::fprintf(stderr,
                             "[perf-b07] invalid run sample (nth=%zu): %s\n",
                             invalidCount, s.sample.invalidReason.c_str());
                std::fflush(stderr);
                if (invalidCount > kInvalidCeiling) {
                    ADD_FAILURE() << "无效运行样本超过安全界（invalid=" << invalidCount
                                  << "）——持续失败属夹具缺陷，最后原因："
                                  << s.sample.invalidReason;
                    break;
                }
            }
            samples.push_back(std::move(s));
        }
    }
    // 工作汇增量组内一致（确定性链——计算确实发生且恒量；预热样本同样核对）。
    for (const RunWallSample& s : samples) {
        if (!s.sample.valid) {
            continue;
        }
        if (anchorWork == 0) {
            anchorWork = s.workDelta;
        } else {
            EXPECT_EQ(s.workDelta, anchorWork) << "时限腿工作汇漂移（确定性链违约）";
        }
    }
    EXPECT_NE(anchorWork, std::uint64_t{0}) << "工作汇为零——工作量未真实发生";

    // ---- 停止采样器并取逐组报告 ----------------------------------------
    sampler.stopAndJoin();
    ASSERT_TRUE(sampler.startupOk())
        << "交互采样线程装配失败（夹具缺陷）：" << sampler.startupError();

    // ---- 达标判定①：基准时限（8 小时端到端墙钟——阈值类判定）-----------
    const testkit::InteractionGroupReport runGroup = makeRunGroupReport(samples);
    ASSERT_EQ(runGroup.validCount, kValidSamples) << "时限腿有效样本不足（§4.2）";
    ASSERT_EQ(runGroup.invalidCount, std::size_t{0}) << "时限腿存在无效样本（留痕见报告）";
    EXPECT_LE(runGroup.maxValidMs, kRunWallClockLimitMs)
        << "标准运行超时（最大有效墙钟 " << runGroup.maxValidMs
        << " ms > 8 小时——NFR-PERF-06）";

    // ---- 达标判定②：运行期间满足 PERF-01（逐操作三面）------------------
    const auto& opGroups = sampler.groups();
    ASSERT_EQ(opGroups.size(), std::size_t{5}) << "交互抽查分组数异常";
    for (const auto& g : opGroups) {
        EXPECT_GE(g.validCount, std::size_t{15})
            << g.opId << " 运行窗内有效样本不足（协议 §4.2 下限）";
        EXPECT_EQ(g.invalidCount, std::size_t{0})
            << g.opId << " 存在无效样本（操作异常——详见报告留痕）";
        EXPECT_LT(g.maxValidMs, kUnresponsiveWindowMs)
            << g.opId << " 单次交互出现 >2 s 无响应窗口（运行期间 NFR-PERF-01）";
        EXPECT_LE(g.p95Ms, kInteractionP95LimitMs)
            << g.opId << " 运行期 P95 超预算（NFR-PERF-01：≤200 ms）";
    }
    // 操作真实到达证据（选择广播/编辑移交非零——驱动器真消费，防静默空转）。
    EXPECT_GT(sampler.selectionHits(), 0) << "选择广播未消费（驱动器空转嫌疑）";
    EXPECT_GT(sampler.editHits(), 0) << "编辑出口未移交（驱动器空转嫌疑）";

    // 测量摘要打到控制台（gtest 日志——证据留痕的即时可读面）。
    std::printf(
        "[perf-b07] 标准运行：quick=%u verified=%u seed=%llu threadCount=%u "
        "workIter=%u\n"
        "[perf-b07] 时限腿：n=%zu 最大有效墙钟=%.1f ms（时限 %.0f ms＝8 小时）"
        " P95=%.1f ms\n"
        "[perf-b07] 运行期交互抽查（state=during-run）：\n",
        kMaxCandidates, kMaxVerifiedCandidates,
        static_cast<unsigned long long>(kSeed), kThreadCount, kWorkIterations,
        runGroup.validCount, runGroup.maxValidMs, kRunWallClockLimitMs,
        runGroup.p95Ms);
    for (const auto& g : opGroups) {
        std::printf("[perf-b07]   %-10s n=%3zu P95=%7.3f ms max=%7.3f ms "
                    "(limit P95<=%.0f ms, window<%.0f ms)\n",
                    g.opId.c_str(), g.validCount, g.p95Ms, g.maxValidMs,
                    kInteractionP95LimitMs, kUnresponsiveWindowMs);
    }
    std::printf("[perf-b07] 交互样本合计=%zu 窗外丢弃=%llu 选择广播=%d 编辑移交=%d\n",
                sampler.totalValidSamples(),
                static_cast<unsigned long long>(sampler.discardedOutOfWindow()),
                sampler.selectionHits(), sampler.editHits());
    std::fflush(stdout);

    // ---- 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过）--------------
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        testkit::PerfSessionReport session
            = makeSessionHeader("b07-opt-run-timelimit");
        session.groups.push_back(runGroup);
        for (const auto& g : opGroups) {
            session.groups.push_back(g);
        }
        // 判定一：运行墙钟时限（阈值 28,800,000 ms＝NFR-PERF-06 原文 8 小时
        // ——零私改；判定统计量＝有效样本最大值）。
        testkit::PerfThresholdVerdict v1;
        v1.opId = "opt-run";
        v1.stateId = "standard-run-threadCount8";
        v1.thresholdMs = kRunWallClockLimitMs;
        v1.thresholdSource
            = "REQUIREMENTS NFR-PERF-06 原文（10,000 Quick＋100 Verified 候选"
              "标准优化基准 8 小时内结束——零私改）＋performance-baseline §4.7"
              "（8 小时为端到端墙钟时限）；判定统计量＝有效样本最大值（B-07 "
              "时序阈值类非 P95——§2 统计口径区分行）";
        v1.measuredP95Ms = runGroup.maxValidMs;  // 判定统计量＝最大有效墙钟
        v1.pass = runGroup.maxValidMs <= kRunWallClockLimitMs;
        session.verdicts.push_back(v1);
        // 判定二~六：五操作运行期 P95（阈值 200 ms＝NFR-PERF-01 原文）。
        for (const auto& g : opGroups) {
            testkit::PerfThresholdVerdict v;
            v.opId = g.opId;
            v.stateId = g.stateId;
            v.thresholdMs = kInteractionP95LimitMs;
            v.thresholdSource
                = "REQUIREMENTS NFR-PERF-01 原文（P95 响应 ≤200 ms——零私改；"
                  "NFR-PERF-06『运行期间满足 NFR-PERF-01』的抽查判定）";
            v.measuredP95Ms = g.p95Ms;
            v.pass = g.p95Ms <= kInteractionP95LimitMs && g.maxValidMs < kUnresponsiveWindowMs
                     && g.invalidCount == 0 && g.validCount >= 15;
            session.verdicts.push_back(v);
        }
        session.allPassed = v1.pass
                            && std::all_of(session.verdicts.begin() + 1, session.verdicts.end(),
                                           [](const testkit::PerfThresholdVerdict& v) { return v.pass; });
        // 分段留痕与口径注记（§4.8"不得只报统计值"——原始序列全量在 groups）。
        session.notes.push_back(
            "分段留痕口径：本基准会话内串行完成（分钟量级）——启动/结束段由逐"
            "样本完整墙钟承载（groups[opt-run].samples）；检查点/恢复分段见用"
            "例 2 报告 b07-opt-run-recovery.json 的 notes 分段日志；8 小时级跨"
            "会话后台运行形态未触发——如实登记不虚称");
        session.notes.push_back(
            "运行期间语义的门控承载：运行窗旗标在 run() 前后置位/复位，仅开始"
            "与结束都落在运行窗内的交互样本计入统计（窗外丢弃 "
            + std::to_string(sampler.discardedOutOfWindow()) + " 个——非统计分"
            "母）；『>1 s 工作转后台』＝结构承载：评估工作全部在编排器 worker "
            "线程（threadCount=8），五类交互操作自身零评估工作");
        session.notes.push_back(
            "样本纪律：时限腿 1 预热（剔除）＋" + std::to_string(kValidSamples)
            + " 有效零无效；交互抽查逐操作 1 次预热（剔除）＋运行窗内有效样本"
              "（≥15 下限）零无效；工作汇增量全样本恒量（确定性链）");
        session.notes.push_back(
            "运行期交互抽查的 ui 侧数据集＝b01-interaction@1 同参复刻（1,000 "
            "对象/100 修订语境/五分组/24 字段/筛选词 obj-007/选中批量 50）；后"
            "台负载＝优化运行本体（进程内 8 worker）——与 B-01 后台态（独立 "
            "worker 进程）形态差异如实申报：本基准后台即 NFR-PERF-06『运行期"
            "间』的负载语义");
        session.notes.push_back(
            "采样节奏＝每轮五操作后静默 " + std::to_string(kInteractionCyclePaceMs)
            + " ms（轮间静默在计时区外——真实用户交互间隙形态；单运行窗每操作"
              "有效样本 ~200 量级，单窗独立满足协议 §4.2 每组 ≥15）；窗外样本"
              "丢弃计数见运行期间语义注——两者均非统计分母");
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}

// =====================================================================
// B-07 达标判定用例 2：同版本检查点恢复（协作取消半程截停→真协调器落盘→
// 同版本 restore→新 AttemptId 续跑→AT-35 审计不重复＋结论面等价）
// =====================================================================

TEST(OptRunTimeBenchmark, SameVersionCheckpointRecovery_WP23T08_B07)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-06"},
                  std::vector<std::string>{"AT-35"});

    // 基线根身份（三腿同值——CandidateId 公式锚：参照/中断/恢复的候选集合
    // 逐 ID 恒等）。
    const core::ObjectId sharedBaselineRoot = core::ObjectId::generate();

    // ---- 启动段：参照全量运行（AT-35 审计与结论面的比对底座）-----------
    BenchJointFixture refFx;
    std::uint64_t refWork = 0;
    JointRunResult full;
    {
        const OptimizationConfiguration config = makeStandardRunConfig();
        JointRunRequest req = refFx.readyRequest(config, sharedBaselineRoot);
        JointLayeredCheckDeps deps = refFx.checkerDeps();
        optimization::JointRunOrchestratorDeps d;
        d.checkerDeps = &deps;
        BenchProjector projector;
        d.projector = &projector;
        d.paretoBuilder = &sharedParetoBuilder();
        JointRunOrchestrator orch(OptimizationStage::StageD, d);
        const std::uint64_t sinkBefore = g_workSink.load(std::memory_order_relaxed);
        full = orch.run(req, refFx.ctx);
        refWork = g_workSink.load(std::memory_order_relaxed) - sinkBefore;
        ASSERT_TRUE(full.runCompleted) << "参照全量运行未完成（夹具缺陷）";
        ASSERT_EQ(full.runPhase, RunPhase::Completed);
        const std::uint64_t evaluated
            = static_cast<std::uint64_t>(full.audit.base.quickEvaluated)
              + static_cast<std::uint64_t>(full.audit.base.verifiedEvaluated);
        ASSERT_EQ(evaluated, kExpectedEvaluations) << "参照运行评估计数漂移";
        ASSERT_GT(refWork, std::uint64_t{0}) << "参照腿工作汇为零";
        std::printf(
            "[perf-b07] 启动段：参照全量运行 Completed——quick=%u verified=%u "
            "批=%u/%u 检查点产出=%u 工作汇增量=%llu\n",
            full.audit.base.quickEvaluated, full.audit.base.verifiedEvaluated,
            full.audit.batchesCompleted, full.audit.batchesTotal,
            full.audit.checkpointStatesEmitted,
            static_cast<unsigned long long>(refWork));
        std::fflush(stdout);
    }

    // ---- 中断腿：协作取消在 Quick 相约半程截停（批界检查点产出）--------
    JointRunResult partial;
    core::TaskIdentity interruptedTask{};
    {
        BenchJointFixture fxB;
        const OptimizationConfiguration config = makeStandardRunConfig();
        JointRunRequest req = fxB.readyRequest(config, sharedBaselineRoot);
        req.task.run = core::RunId::generate();  // 新运行身份（中断腿）
        interruptedTask = req.task;              // 检查点记录与恢复任务绑定此身份
        JointLayeredCheckDeps deps = fxB.checkerDeps();
        optimization::JointRunOrchestratorDeps d;
        d.checkerDeps = &deps;
        BenchProjector projector;
        d.projector = &projector;
        d.paretoBuilder = &sharedParetoBuilder();
        JointRunOrchestrator orch(OptimizationStage::StageD, d);

        CancelAfterNContext cancelCtx(kCancelAfterQueries);
        partial = orch.run(req, cancelCtx);
        ASSERT_FALSE(partial.runCompleted) << "中断腿必须被截停";
        ASSERT_EQ(partial.runPhase, RunPhase::Canceled) << "截停终态＝Canceled";
        ASSERT_FALSE(partial.checkpointStates.empty()) << "取消必须产出当前检查点状态";
        const JointCheckpointState& cp = partial.checkpointStates.back();
        ASSERT_GE(cp.completedUnits, std::uint64_t{1}) << "截停点过早（无已完成批）";
        ASSERT_LT(cp.completedUnits, cp.totalUnits) << "截停点过晚（全量已完成）";
        ASSERT_TRUE(cp.isValid()) << "截停检查点状态结构非法";
        std::printf(
            "[perf-b07] 中断腿：查询计数 %u 后 Canceled——检查点 phase=%s "
            "completedUnits=%llu/%llu 完成批=%zu 审计基点 quick=%u\n",
            kCancelAfterQueries,
            cp.phase == core::EvaluationMode::Quick ? "Quick" : "Verified",
            static_cast<unsigned long long>(cp.completedUnits),
            static_cast<unsigned long long>(cp.totalUnits),
            cp.completedBatchIndices.size(), cp.audit.quickEvaluated);
        std::fflush(stdout);
    }

    // ---- 检查点段：域中间状态经真协调器落盘（IRDOPTC1 魔数＋磁盘文件）--
    CheckpointEnv env;
    const auto snapshotId = makeContentIdentity(0x71);
    const auto sliceId = makeContentIdentity(0x72);
    const execution::CheckpointId ckptId
        = env.writeState(partial.checkpointStates.back(), interruptedTask,
                         snapshotId, sliceId, 1);
    EXPECT_EQ(env.coordinator->indexedCount(), 1U);
    // 编码面魔数核对（DOPT-10——检查点域 magic；域自编 codec 的身份面）。
    {
        const auto encoded
            = optimization::encodeJointCheckpointState(partial.checkpointStates.back());
        ASSERT_GE(encoded.size(), std::size_t{8});
        EXPECT_EQ(std::memcmp(encoded.data(), "IRDOPTC1", 8), 0)
            << "检查点域中间状态魔数不符（DOPT-10）";
    }
    std::printf("[perf-b07] 检查点段：真协调器 Published（seq=1，磁盘三文件，"
                "magic=IRDOPTC1 codecVersion=%u）\n",
                optimization::kJointCheckpointCodecVersion);
    std::fflush(stdout);

    // ---- 恢复段：同版本 restore（Restored）→ 磁盘字节解码 → 续跑规格 ---
    execution::CheckpointRestoreRequest restoreReq;
    restoreReq.requestEvaluatorKey = std::string(optimization::kOptJointFullKey);
    restoreReq.checkpointId = ckptId;
    restoreReq.requestSliceId = sliceId;
    restoreReq.requestContractVersion = optimization::kOptJointFullContractVersion; // 同版本
    const execution::CheckpointRestoreResult restored = env.coordinator->restore(restoreReq);
    ASSERT_EQ(restored.outcome, execution::CheckpointRestoreResult::Outcome::Restored)
        << "同版本检查点恢复被拒（NFR-PERF-06 恢复面违约）";
    EXPECT_EQ(restored.record.evaluatorContractVersion,
              optimization::kOptJointFullContractVersion)
        << "恢复记录契约版本必须与请求同版本（同版本兼容语义）";
    EXPECT_EQ(restored.record.completedUnits,
              partial.checkpointStates.back().completedUnits);
    // 磁盘字节装载→域解码（round-trip 完整性——恢复规格逐字段核对）。
    const std::vector<std::uint8_t> stateBytes = env.readStateBytes();
    ASSERT_FALSE(stateBytes.empty()) << "磁盘检查点字节缺失";
    const JointCheckpointState resumeState
        = optimization::decodeJointCheckpointState(stateBytes);
    EXPECT_EQ(resumeState.codecVersion, optimization::kJointCheckpointCodecVersion);
    EXPECT_EQ(resumeState.completedUnits, partial.checkpointStates.back().completedUnits);
    EXPECT_EQ(resumeState.totalUnits, partial.checkpointStates.back().totalUnits);
    ASSERT_EQ(resumeState.completedBatchIndices.size(),
              partial.checkpointStates.back().completedBatchIndices.size());

    // ---- 续跑段：新 AttemptId（恢复语义 §9.2）自检查点续跑至完成 --------
    std::uint64_t resumedWork = 0;
    JointRunResult resumed;
    {
        BenchJointFixture fxR;
        const OptimizationConfiguration config = makeStandardRunConfig();
        JointRunRequest req = fxR.readyRequest(config, sharedBaselineRoot);
        // 覆盖矩阵用 fxR 自身快照的分母（快照绑定——跨 fixture 搬运会因
        // requiredCaseSetId 错配触发门禁；候选集合等价只由 baselineRoot/
        // baselineCv/config 决定——CandidateId 公式）。
        req.task = interruptedTask;
        req.task.attempt = core::AttemptId{interruptedTask.attempt.value + 1U}; // AttemptId+1
        req.resumeFrom = resumeState;
        JointLayeredCheckDeps deps = fxR.checkerDeps();
        optimization::JointRunOrchestratorDeps d;
        d.checkerDeps = &deps;
        BenchProjector projector;
        d.projector = &projector;
        d.paretoBuilder = &sharedParetoBuilder();
        JointRunOrchestrator orch(OptimizationStage::StageD, d);
        const std::uint64_t sinkBefore = g_workSink.load(std::memory_order_relaxed);
        resumed = orch.run(req, fxR.ctx);
        resumedWork = g_workSink.load(std::memory_order_relaxed) - sinkBefore;
        ASSERT_TRUE(resumed.runCompleted) << "续跑未完成（检查点恢复≠优化成功——"
                                             "完成面须独立成立，NFR-PERF-06）";
        ASSERT_EQ(resumed.runPhase, RunPhase::Completed);
    }

    // ---- 达标判定③：恢复语义四面对账 ----------------------------------
    // AT-35：恢复后统计不重复——总审计（检查点基点＋恢复增量）＝单次全量。
    EXPECT_EQ(resumed.audit.base.candidatesGenerated,
              full.audit.base.candidatesGenerated);
    EXPECT_EQ(resumed.audit.base.quickEvaluated, full.audit.base.quickEvaluated)
        << "Quick 评估数必须不双计（基点已含前缀账目——AT-35）";
    EXPECT_EQ(resumed.audit.base.quickScreenedOut, full.audit.base.quickScreenedOut);
    EXPECT_EQ(resumed.audit.base.verifiedEvaluated, full.audit.base.verifiedEvaluated);
    // 批完成账目跨恢复边界衔接（batchesCompleted＝"本次 run 内增量"计数——
    // JointRunAuditCounts 字段口径）：检查点 completedUnits（前缀账目基点）
    // ＋续跑腿增量＝参照全量——不重不漏（AT-35）。
    EXPECT_EQ(static_cast<std::uint64_t>(resumed.audit.batchesCompleted)
                  + partial.checkpointStates.back().completedUnits,
              static_cast<std::uint64_t>(full.audit.batchesCompleted))
        << "批完成账目跨恢复边界未衔接（AT-35）";
    EXPECT_EQ(resumed.audit.batchesTotal, full.audit.batchesTotal);
    EXPECT_GT(resumed.audit.resumedBatchesSkipped, 0U)
        << "已完成批必须按基点记账（resumedBatchesSkipped>0——AT-35 观测点）";
    // 结论面等价（NFR-COR-02——恢复不改变结论：候选序/状态/八项指标/Pareto）。
    const EquivalenceSummary eq = compareConclusions(resumed, full);
    EXPECT_TRUE(eq.ok) << "恢复腿结论面失配（NFR-COR-02）：" << eq.firstMismatch;
    EXPECT_TRUE(eq.bitwiseAll)
        << "出现容差内非逐位相等（黄金算例 ε_abs=0 声明与实测不符——须按实况"
           "修订声明并留痕，不得静默）";
    // 物理工作对账（I-J3：跳过批记录确定性重推进——账面不双计而计算真实
    // 发生：续跑腿工作汇增量＝参照腿全量增量）。
    EXPECT_EQ(resumedWork, refWork)
        << "续跑腿工作汇增量与参照全量不一致（重推进语义与确定性链违约）";
    // 恢复腿评估计数恰为标准规模（终态审计独立成立——检查点恢复≠优化成功
    // 的完成面对账）。
    const std::uint64_t resumedEvaluated
        = static_cast<std::uint64_t>(resumed.audit.base.quickEvaluated)
          + static_cast<std::uint64_t>(resumed.audit.base.verifiedEvaluated);
    EXPECT_EQ(resumedEvaluated, kExpectedEvaluations);

    const std::uint64_t resumedAttempt = interruptedTask.attempt.value + 1U;
    std::printf(
        "[perf-b07] 恢复段：Restored（同版本 contractVersion=%u）→ 续跑 "
        "AttemptId=%llu Completed——resumedBatchesSkipped=%u 审计＝参照全量 "
        "（AT-35）结论面 records=%zu metrics=%zu maxRelDiff=%.3e bitwise=%s "
        "工作汇增量=%llu＝参照\n",
        optimization::kOptJointFullContractVersion,
        static_cast<unsigned long long>(resumedAttempt),
        resumed.audit.resumedBatchesSkipped, eq.checkedRecords, eq.checkedMetrics,
        eq.maxRelDiff, eq.bitwiseAll ? "true" : "false",
        static_cast<unsigned long long>(resumedWork));
    std::fflush(stdout);

    // ---- 报告落盘（拖挂——分段日志进 notes）-----------------------------
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        testkit::PerfSessionReport session
            = makeSessionHeader("b07-opt-run-recovery");
        // 行为判定 verdict（无数值阈值——恢复面为用例行为判定非阈值类；
        // thresholdMs=0 为非数值槽位的登记性承载，见 notes）。
        testkit::PerfThresholdVerdict v;
        v.opId = "same-version-checkpoint-recovery";
        v.stateId = "canceled-mid-quick-resume";
        v.thresholdMs = 0.0;
        v.thresholdSource
            = "REQUIREMENTS NFR-PERF-06 原文（可从同版本兼容检查点恢复——行为"
              "判定无数值阈值）：同版本 Restored＋codec IRDOPTC1 v1＋AT-35 审"
              "计不重复＋结论面等价（1×10⁻¹²·|ref|＋ε_abs=0 逐元素）＋工作汇"
              "对账；thresholdMs=0 为非数值判定的槽位承载（B-07 恢复面非阈值"
              "类——§2 统计口径区分行）";
        v.measuredP95Ms = 0.0;
        v.pass = eq.ok && eq.bitwiseAll && resumedWork == refWork
                 && resumed.audit.resumedBatchesSkipped > 0
                 && restored.outcome == execution::CheckpointRestoreResult::Outcome::Restored;
        session.verdicts.push_back(v);
        session.allPassed = v.pass;
        // 分段日志（启动/检查点/恢复/结束——任务口径的分段留痕承载）。
        session.notes.push_back(
            "分段日志[启动段]：参照全量运行 Completed——quickEvaluated="
            + std::to_string(full.audit.base.quickEvaluated) + " verifiedEvaluated="
            + std::to_string(full.audit.base.verifiedEvaluated) + " 批="
            + std::to_string(full.audit.batchesCompleted) + "/"
            + std::to_string(full.audit.batchesTotal) + " 工作汇增量="
            + std::to_string(refWork));
        session.notes.push_back(
            "分段日志[中断腿]：CancelAfterNContext 查询计数 "
            + std::to_string(kCancelAfterQueries) + " 后 Canceled（Quick 相约"
            "半程截停）——检查点 completedUnits="
            + std::to_string(partial.checkpointStates.back().completedUnits) + "/"
            + std::to_string(partial.checkpointStates.back().totalUnits)
            + " 完成批=" + std::to_string(partial.checkpointStates.back().completedBatchIndices.size()));
        session.notes.push_back(
            "分段日志[检查点段]：真 execution::CheckpointCoordinator writeState "
            "Published（seq=1，archiveDir 真实磁盘三文件；域中间状态编码 magic="
            "IRDOPTC1 codecVersion=1——DOPT-10）");
        session.notes.push_back(
            "分段日志[恢复段]：restore（requestContractVersion="
            + std::to_string(optimization::kOptJointFullContractVersion)
            + "＝记录同版本）→ Restored；磁盘 joint-state.bin 装载→decode "
              "round-trip 逐字段核对通过；续跑 AttemptId+1（恢复语义 §9.2）");
        session.notes.push_back(
            "分段日志[结束段]：续跑 Completed——resumedBatchesSkipped="
            + std::to_string(resumed.audit.resumedBatchesSkipped) + " 审计＝参"
            "照全量（AT-35 不双计）；结论面 records="
            + std::to_string(eq.checkedRecords) + " metrics="
            + std::to_string(eq.checkedMetrics) + " maxRelDiff="
            + std::to_string(eq.maxRelDiff) + " bitwise="
            + std::string(eq.bitwiseAll ? "true" : "false") + "；工作汇增量="
            + std::to_string(resumedWork) + "＝参照（I-J3 重推进语义对账）");
        session.notes.push_back(
            "恢复语义边界（optimization.md §9.2 原文）：检查点恢复不等于优化成"
            "功——恢复后仍走完整推进与汇总（本用例终态 Completed 为独立断言）；"
            "不兼容检查点拒绝（EX-CHECKPOINT-INCOMPATIBLE）与损坏拒绝（EX-"
            "CHECKPOINT-CORRUPT）的判定面已由 JointRunTest OPT-VER-224/225 承"
            "载——本基准聚焦同版本兼容恢复的达标面");
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}
