/**
 * @file   ParallelThroughputBenchmarkTest.cpp
 * @brief  并行吞吐基准（performance-baseline §2 B-06／需求 NFR-PERF-05）——
 *         固定候选集 StageD 联合评估上测量"8 工作线程相对单线程的中位吞吐
 *         比 ≥4 倍（不含首次编译与导入）"，并钉住吞吐运行中的并行归约正确性
 *         （NFR-COR-02：等价候选集合＋稳定排序＋附录 D 第 8 项容差 1×10⁻¹²
 *         逐元素）（WP-23-T07；DTB §2.24 行——横切 execution/各域）。
 *
 * 设计依据：
 *   - doc/industrial-robot-design/performance-baseline.md §2 B-06 行（达标
 *     口径照抄需求原文——≥4 倍中位吞吐、不含首次编译与导入，零私改）、
 *     §4.1（硬件/软件版本登记）、§4.2（样本纪律——每组有效 ≥15、剔除各组
 *     首次预热、无效样本不计入分母并留痕）、§4.5（并行吞吐口径——统计量
 *     为中位数〔需求原文"中位吞吐"〕；分子分母同候选集同硬件同软件版本；
 *     候选集构成由本卡登记并冻结进数据集 manifest；排除项＝首次编译与
 *     导入，正式计时前执行预热运行）、§4.6（并行归约容差——附录 D 第 8
 *     项 1×10⁻¹²，通用比较公式 |a−b| ≤ ε_rel·|ref|＋ε_abs 逐元素判定；
 *     ε_abs 由黄金算例逐例声明）、§5.6（本卡挂点）；
 *   - 需求 NFR-PERF-05（REQUIREMENTS §19.3 原文——"固定候选评估基准上，
 *     8 个工作线程相对单线程中位吞吐提升 ≥4 倍（不含首次编译与导入）"）；
 *   - 需求 NFR-COR-02（REQUIREMENTS §19.1 原文——"同一输入、软件版本、
 *     配置、线程数和随机种子产生等价候选集合与稳定排序；并行归约数值满
 *     足容差，不要求浮点文件逐字节相同"）；
 *   - units/optimization.md §9.2（并行分片——worker 内并行线程数来自
 *     config.opt 入身份；按批序稳定合并与线程数无关；并行归约满足附录 D
 *     第 8 项容差——WP-23-T07 基准；§9.2 规模化口径锚"达标归 WP-23 对应
 *     基准任务"即本卡）＋§13.2 OPT-VER-238（R2 并行吞吐——进程级 WP-23-T07）；
 *   - 任务契约 tasks/foundation/WP-23-T07.json（≙DTB §2.24 行）。
 *
 * 被测对象（与功能用例的分工——为什么基准长这样）：
 *   WP-21-T04 交付的 `JointRunOrchestrator`（optimization.hpp JointRun.hpp
 *   ——§8.5 外层两级批量编排＋§9.2 并行分片/按批序稳定合并）是"固定候选
 *   评估"的并行执行本体：候选批固定容量划分〔kBatchCapacity=4，与线程数
 *   无关〕、config.parallel.threadCount 派生 worker 线程（每 worker 各持
 *   JointLayeredChecker——§6.7 SingleThread 口径）、批内串行批间并行、按
 *   批序稳定合并。本基准以两组真实编排运行（threadCount=1 vs threadCount=8）
 *   测吞吐比——"协议对"的正确性已由 JointRunTest OPT-VER-220~222 承载
 *   （ManualClock 无关——本基准全部真实 steady_clock 墙钟），本基准补的
 *   是"规模化达标"证据：吞吐数字必须真实产生。
 *
 * 测量口径（datasetId ird-perf-b06-parallel-throughput@1——manifest 冻结
 * 于 testdata/performance/b06-parallel-throughput-manifest.json，本处常量
 * 与 manifest 逐字一致，报告输出参数供漂移核对）：
 *   - 固定候选集＝seeded-lhs 策略（seed 7）×单连续绑定
 *     "mdl.drivetrain.ratio[1]"〔0.8~1.2 无量纲〕×maxCandidates 128×
 *     maxVerifiedCandidates 4——同基线根身份（CandidateId 公式输入一致）
 *     使两组腿的候选集合恒等；批划分 128/4＝Quick 32 批＋Verified 1 批，
 *     8 线程在 Quick 相满员并行（并行度＝min(threadCount, 相内批数)）；
 *   - 计时区间＝JointRunOrchestrator::run() 全程墙钟（steady_clock）——
 *     候选集构造/快照组装/注册表装配/编排器构造全部在区间外；各组首跑
 *     预热剔除（§4.5"正式计时前执行预热运行"＝"不含首次编译与导入"的
 *     本基准承载——首次运行承担全部冷路径：注册表/分配器/代码路径首次
 *     触达，其样本不计入统计）；
 *   - 吞吐＝每有效样本 (quickEvaluated＋verifiedEvaluated)/墙钟秒
 *     （单位 candidates/s——候选集固定故与"每秒评估工作量"同序）；判定
 *     统计量＝两组有效样本吞吐**中位数**之比（B-06 为中位吞吐类判定，
 *     performance-baseline §2 统计口径区分行明文与 P95 互不替代——禁以
 *     P95/最大值表述达标）；
 *   - 样本量：每组 1 次预热（剔除）＋15 有效样本（§4.2"每组有效样本
 *     ≥15"——T02/T03/T05/T06 先例同数自律）；无效样本（运行未完成或
 *     评估计数漂移）不计入分母、留痕原因、断言 invalidCount==0 收口；
 *   - 并行归约正确性（NFR-COR-02/禁止项栏）：每个并行有效样本的结论面
 *     对照单线程参照逐元素判定——候选序（生成序＝合并序，稳定排序）、
 *     候选状态、八项指标（|a−b| ≤ 1×10⁻¹²·|ref|＋ε_abs，ε_abs 声明为 0
 *     ——黄金算例声明：本编排零浮点重排结构〔JointRun.hpp 文件头②〕下
 *     实测逐位相等，声明 0 即"任何非零差即违约"的最严口径）、Pareto 序、
 *     审计计数、评估工作汇总量（确定性浮点链的位模式累加——计算确实
 *     发生且两腿逐位一致）。
 *
 * 替身边界声明（§11 同源纪律——替身不构成业务证明）：
 *   - 内层四域评估器＝确定性浮点迭代链测试替身（每派发 kWorkIterations
 *     次串行 FP 乘加依赖链——模拟"评估计算量主导"的固定成本；结果位模
 *     式沉入进程级原子汇防消除）。它证明的是"编排并行分片/稳定合并的
 *     吞吐规模化与归约等价"，不是业务评估器（trj/dyn/sel 真实现）的算
 *     法吞吐画像——业务评估器端到端吞吐归集成模式扩展测量挂点（首测报
 *     告登记，集成构建由合入批执行）；
 *   - 指标投影缝＝P-OPT-2 测试面替身（JointRunTest 同款接缝纪律——候选
 *     补丁值的确定性纯函数投影，无状态并发安全）；
 *   - 归约等价的"同实现"前提：两组腿同进程同二进制同候选集同种子，唯
 *     threadCount 不同（入 config.opt canonical＝新身份——KIN-13 同型；
 *     结论面等价恰是 NFR-COR-02"与线程数无关"的需求语义，身份不同不妨
 *     等价判定）。
 *
 * 测量纪律（performance-baseline §4/optimization.md §9.2 锚）：本用例
 * 串行执行（gtest 单进程单线程驱动，run 内部并行除外）；禁止与其他测量
 * 负载并发跑分；样本全量留痕（预热/无效不删除，只标记与登记原因）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>
#include <sdurws/ird/optimization/JointRun.hpp>
#include <sdurws/ird/optimization/JointStrategy.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Pareto.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>   // kOptProfileId——opt Profile 域 id 常量
#include <sdurws/ird/optimization/Types.hpp>
#include <sdurws/ird/testkit/PerfBenchmark.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::CandidateStatus;
using optimization::JointCandidateFacts;
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

// =====================================================================
// 数据集冻结参数（datasetId＝ird-perf-b06-parallel-throughput，version 1
// ——manifest 权威文件 testdata/performance/b06-parallel-throughput-
// manifest.json；本处常量必须与 manifest 逐字一致，报告输出参数供漂移核对）
// =====================================================================

/// LHS 采样与候选生成种子（config.seed——入 config.opt canonical 身份；
/// manifest 冻结）。
constexpr std::uint64_t kSeed = 7;
/// 候选预算（budget.maxCandidates——去重后 Quick 相候选数；批容量 4 ⇒
/// Quick 32 批，8 线程满员并行；manifest 冻结）。
constexpr std::uint32_t kMaxCandidates = 128;
/// 复核预算（budget.maxVerifiedCandidates——Verified 相候选数＝基线席＋
/// 稳定序补位；1 批＝串行尾部占比小，与生产形态一致；manifest 冻结）。
constexpr std::uint32_t kMaxVerifiedCandidates = 4;
/// 单线程腿线程数（吞吐比分母——NFR-PERF-05 原文"单线程"；manifest 冻结）。
constexpr std::uint32_t kThreadCountSerial = 1;
/// 并行腿线程数（吞吐比分子——NFR-PERF-05 原文"8 个工作线程"；manifest
/// 冻结）。
constexpr std::uint32_t kThreadCountParallel = 8;
/// 每组预热次数（剔除不计入统计——§4.2/§4.5"正式计时前执行预热运行"；
/// manifest 冻结）。
constexpr std::size_t kWarmupRuns = 1;
/// 每组有效样本数（§4.2 每组有效 ≥15——T02/T03/T05/T06 同数自律；manifest
/// 冻结）。
constexpr std::size_t kValidSamples = 15;
/// 无效样本安全界（夹具自保——testkit runInteractionGroup 同款 10× 上限；
/// 超界显性失败不静默补采）。
constexpr std::size_t kInvalidCeiling = kValidSamples * 10;
/// 每次内层评估派发的确定性工作量（次——串行 FP 乘加依赖链迭代数；工作
/// 量取"单次全量运行数百毫秒"量级：远大于计时/调度噪声、远小于套件节奏
/// 可承受；manifest 冻结）。
constexpr std::uint32_t kWorkIterations = 400000;
/// 每运行样本的期望评估计数（quickEvaluated＋verifiedEvaluated——固定候
/// 选集下恒为 maxCandidates＋maxVerifiedCandidates；偏离即样本无效）。
constexpr std::uint64_t kExpectedEvaluations =
    static_cast<std::uint64_t>(kMaxCandidates) + static_cast<std::uint64_t>(kMaxVerifiedCandidates);
/// 吞吐比达标下限（NFR-PERF-05 原文"≥4 倍"——照抄零私改；判定方向 ≥）。
constexpr double kRatioThreshold = 4.0;
/// 并行归约相对容差（附录 D 第 8 项 P-01 冻结值 1×10⁻¹²——照抄零私改；
/// 通用比较公式 ε_rel）。
constexpr double kReductionRelTol = 1e-12;
/// 并行归约绝对容差（黄金算例逐例声明值——0.0：本编排零浮点重排结构
/// 〔JointRun.hpp 文件头②确定性合并〕下要求实测逐位相等，任何非零差即
/// 违约的最严声明口径；manifest/报告同值登记）。
constexpr double kReductionAbsTol = 0.0;

/// 进程级评估工作汇（防消除沉没点——每次内层评估派发把确定性浮点链结果
/// 的 IEEE-754 位模式累加进来；两腿各样本的增量必须逐位一致——计算确实
/// 发生且与线程数无关的第 4 个确定性观测面）。relaxed 序足够：只做"事后
/// 单调累加值对账"，不作同步。
std::atomic<std::uint64_t> g_workSink{0};

// =====================================================================
// 通用替身与构造辅助（JointRunTest 同款形态——去观测面、加确定性工作量）
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

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 构造冻结快照（SnapshotBuilder 唯一合法生产者——身份为 builder 计算值；
/// JointRunTest 同款最小形态）。
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
    repro.productVersion = "perf-b06";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
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
/// 使 LHS 候选互异＝固定候选集的生成基座；JointRunTest 同款）。
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

/// StageD 已校验配置（八项目标默认激活＋并行度入参——R2 解锁面；manifest
/// 冻结的 seed/预算常量在本处组装——调用方只给 threadCount 一根轴）。
OptimizationConfiguration makeStageDConfig(std::uint32_t threadCount)
{
    OptimizationConfiguration c;
    c.stage = OptimizationStage::StageD;
    c.seed = kSeed;
    c.parallel.threadCount = threadCount;  // R2 并行度（入 config.opt canonical）
    c.budget.maxCandidates = kMaxCandidates;
    c.budget.maxVerifiedCandidates = kMaxVerifiedCandidates;
    c.strategyId = std::string(optimization::kStrategySeededLhs);
    c.objectives = optimization::defaultObjectives(OptimizationStage::StageD);
    c.variables = {makeLengthBinding()};
    optimization::validateConfiguration(c);
    return c;
}

/// 基线根内容版本（CandidateId 公式输入——非保留值；JointRunTest 同款）。
core::ContentVersion baselineCv()
{
    return core::ContentVersion::fromCanonical(
        "cv-00000000000000000000000000000000000000000000000000000000000000cc");
}

// ---- 确定性浮点工作链（内层评估替身的"固定评估成本"承载）----------------

/**
 * @brief 确定性浮点乘加依赖链（每次内层评估派发执行的"评估工作量"替身）。
 *
 * 为什么是串行 FP 依赖链：x = x·a＋c 的循环携带依赖使编译器无法向量化/
 * 归约折叠（/fp:precise 下浮点重排被禁——NFR-COR-02 同源纪律），每次迭
 * 代的耗时稳定且与线程数无关——"计算量主导的评估成本"的最小忠实替身。
 * 确定性：同迭代数 ⇒ 同结果位模式（IEEE-754 双精度基本运算，同进程同
 * 二进制无交叉实现差异）——两腿对账的第 4 观测面（g_workSink）因此成立。
 *
 * @param iterations [in] 迭代次数（kWorkIterations——manifest 冻结）
 * @return 链终值（有限——a>1 收敛于不动点 a·x＋c 的稳定邻域，量级 O(1)）
 */
double benchWorkChain(std::uint32_t iterations)
{
    double x = 0.5;
    constexpr double kA = 1.0 + 1.0 / 1048576.0;  // 步进因子 1+2⁻²⁰（近 1——
                                                  //  迭代不发散不塌缩）
    for (std::uint32_t i = 0; i < iterations; ++i) {
        x = x * kA + 1.0e-9;  // 串行依赖——不可向量化/不可折叠（类注）
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

// ---- ③端口内层评估替身（确定性工作量版——无锁并发安全）------------------

/**
 * @brief 内层评估器替身（四域各一注册键——每派发执行 benchWorkChain）。
 *
 * 并发契约：evaluate() 只触自身成员（m_last）与进程级原子汇——零共享可
 * 变非原子态；每 worker 经各自 JointLayeredChecker 经工厂 create() 取独
 * 立实例（SingleThread 口径）——8 线程并发派发无锁无竞争。与 JointRun
 * Test 的脚本替身的差异：本替身不做 Must 违例/搜索未果脚本（基准场景全
 * Feasible——吞吐测量的纯净负载），也不写派发日志（日志锁会把并行测量
 * 串行化——观测面归功能用例，不归本基准）。
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
        // 确定性工作量＋防消除沉没：链终值同时写实例成员与原子汇（两次
        // 消费使编译器无法把循环折叠为闭式——结果必须真实算出）。
        const double x = benchWorkChain(kWorkIterations);
        m_last = x;
        g_workSink.fetch_add(bitsOf(x), std::memory_order_relaxed);
        return {};  // 零违例零搜索未果——候选 Feasible（吞吐纯净负载）
    }

    /// 最近一次链终值（防消除的第二消费点；对账不读它——原子汇足够）。
    double last() const noexcept { return m_last; }

private:
    evidence::EvaluatorDescriptor m_descriptor; ///< 稳定存储（descriptor 引用所指）
    double m_last = 0.0;                        ///< 实例私有沉没位（单线程域——
                                                ///  SingleThread 口径无并发写）
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

// ---- 指标投影替身（P-OPT-2 缝——候选补丁值的确定性纯函数）---------------

/**
 * @brief 联合指标投影替身（P-OPT-2 裁决前测试供给——JointRunTest 同款接
 *        缝纪律的"逐候选互异"版）。
 *
 * 为什么逐候选互异而不是全候选同事实：固定候选集的结论面若全同值，Pareto
 * 前沿/支配比较退化为平凡形态——投影把补丁标量值线性映射进 D-only 五项
 * （纯函数，量级 O(1)），使候选指标互异、Pareto 非平凡，等价判定（候选
 * 序/指标逐元素/Pareto 序）因此覆盖真实结构。并发安全：project() 只读
 * 入参（纯函数）——8 worker 并发调用无共享可变态。
 */
class BenchProjector final : public optimization::IJointMetricProjector {
public:
    JointCandidateFacts project(const CandidatePatch& patch,
                                const std::vector<optimization::VariableBinding>&) const override
    {
        // 提取补丁标量（基线候选空补丁 ⇒ 默认值 1.0；命中绑定 id 才取值——
        // items 按 bindingId 字典序，线性扫描 O(项数)=O(1)）。
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
        // D-only 五项＝t 的确定性线性映射（全部有限——NonFiniteInput 轨不
        // 触发；全部有值——SourceMissing 轨不触发；单位语义见各字段注）。
        facts.jointFacts.cycleTimeS = 8.0 + 5.0 * t;            // s（trj 层投影口径）
        facts.jointFacts.deviceCost = 7000.0 + 3000.0 * t;      // 目录货币单位（sel 层）
        facts.jointFacts.deviceMassKg = 40.0 + 10.0 * t;        // kg（sel 层）
        facts.jointFacts.jointPositiveWorkJ = 250.0 + 150.0 * t; // J（dyn 层）
        facts.jointFacts.minDriveMargin = 1.2 + 1.2 * t;        // 无量纲（sel 层）
        return facts;
    }
};

// ---- 层切片供给替身（无观测面版——纯函数并发安全）------------------------

/**
 * @brief 层切片供给替身（JointRunTest ScriptedLayerSlices 的无日志版）。
 *
 * 并发安全：零共享可变态（probe 值由入参派生——层 token＋上游数＋末条上
 * 游 sliceId；切片构建用局部 SliceBuilder）——8 worker 并发调用无锁。
 * probe 值携带上游身份使层切片身份随上游状态变化（与生产适配器同构的
 * 可冻结可区分面——sliceId 身份链真实参与）。
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
        // 探针条目：定位 token＋上游引用数＋末条上游 sliceId（同 JointRun
        // Test——层序与上游状态变化都反映进切片身份）。
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

// ---- 基准夹具（真注册表＋opt Profile＋四域工作量替身）--------------------

/// Pareto 稳定序单点（buildFront 纯函数——共享实例与 JointRunTest 同款）。
optimization::ParetoFrontBuilder& sharedParetoBuilder()
{
    static optimization::ParetoFrontBuilder builder;  // 函数级单例（纯函数面）
    return builder;
}

/**
 * @brief 并行吞吐基准夹具（真 evidence 注册表＋四域工作量替身注册）。
 *
 * 每腿一个实例（注册表/快照在腿内跨样本复用——"同一候选集、同一硬件、
 * 同一软件版本"的组内固定；组间重建实例＝全新装配路径，归入该组首跑预
 * 热剔除——"不含首次编译与导入"的组内承载）。实例非线程安全——run()
 * 由驱动线程串行调用（run 内部按 threadCount 派生 worker 并 join）。
 */
struct BenchJointFixture {
    evidence::EvidenceProfileRegistry profiles;  ///< Profile 注册权威（opt 域）
    evidence::EvaluatorRegistry evaluators{profiles}; ///< 评估器注册表（共享锁）
    BenchLayerSlices layerSlices;                ///< 层切片供给（纯函数）
    NopEvaluationContext ctx;                    ///< 调用上下文（零取消零进度）
    evidence::AnalysisSnapshot snapshotHolder;   ///< 冻结快照（fixture 单例）

    BenchJointFixture()
        : snapshotHolder(makeSnapshot(makeTwoMandatoryCases()))
    {
        layerSlices.snapshot = &snapshotHolder;
        // Profile 注册在前（evidence §13 时序——opt 域空必需项集）。
        evidence::RequiredEvidenceProfile profile;
        profile.profileId = std::string(optimization::kOptProfileId);
        profile.version = "1.0";
        profiles.registerProfile(profile);
        // 四内层评估器注册（各域登记键——kebab 落位偏差消费拼写；全部挂
        // 工作量替身工厂——描述符 SingleThread＝§6.7 口径，由"每 worker
        // 各持 checker＋create 独立实例"结构兑现）。
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

    /// 联合编排依赖面（JointRunTest 同款四指针装配）。
    JointLayeredCheckDeps checkerDeps()
    {
        JointLayeredCheckDeps d;
        d.evaluators = &evaluators;
        d.layerSlices = &layerSlices;
        d.producers = &evaluators;  // EvaluatorRegistry 实现 IProducerRegistryView
        d.profiles = &profiles;     // EvidenceProfileRegistry 实现 IProfileRegistryView
        return d;
    }

    /// 就绪联合请求（快照/覆盖/任务面就绪；基线根身份由调用方传入——两腿
    /// 同一根身份是"同候选集合"的锚：CandidateId＝f(baselineRoot,
    /// baselineCv, patch)，跨腿钉住后并行样本与串行参照逐候选可比——
    /// JointRunTest 恢复用例"基线身份钉住"同款纪律）。
    JointRunRequest readyRequest(const OptimizationConfiguration& config,
                                 const core::ObjectId& baselineRoot) const
    {
        JointRunRequest req;
        req.config = config;
        req.baselineRoot = baselineRoot;
        req.baselineCv = baselineCv();
        req.snapshot = snapshotHolder;
        req.task = {core::ProjectId::generate(), core::BranchId::generate(),
                    core::RevisionId::generate(), core::RunId::generate(),
                    core::AttemptId{1U}};
        req.coverage = makeCoverage(snapshotHolder);
        return req;
    }
};

// =====================================================================
// 单样本执行与等价判定
// =====================================================================

/**
 * @brief 单个运行样本（原始观测——有效/预热/无效三态全量留痕）。
 *
 * 协议依据（§4.2）：预热样本不计入统计但留痕；无效样本（运行未完成或评
 * 估计数漂移——测量区间不完整）不计入分母并留痕原因。
 */
struct ThroughputSample {
    testkit::InteractionSample sample = {};  ///< 时长与三态标记（ms——steady_clock）
    JointRunResult result;                   ///< 结论面（有效样本有效；等价判
                                             ///  定与吞吐计数来源）
    std::uint64_t evaluatedCount = 0;        ///< quickEvaluated＋verifiedEvaluated
    std::uint64_t workDelta = 0;             ///< 本运行的工作汇增量（位模式累加
                                             ///  ——确定性第 4 观测面）
    double throughput = 0.0;                 ///< 评估吞吐（candidates/s——有效
                                             ///  样本回填）
};

/**
 * @brief 执行一次固定候选集联合评估并采样（计时区间＝run() 全程）。
 *
 * 区间纪律（§4.5"不含首次编译与导入"的本基准承载）：配置组装/请求组装/
 * 编排器构造全部在 t0 之前；计时只罩 JointRunOrchestrator::run()——首次
 * 运行承担的冷路径（注册表首查/分配器预热/代码路径首触）由每组首跑预热
 * 剔除兜底（§4.5"正式计时前执行预热运行"）。
 *
 * @param fx           [in] 夹具（注册表/快照/上下文——调用方保证存活）
 * @param threadCount  [in] 本样本并行度（1＝分母腿；8＝分子腿）
 * @param baselineRoot [in] 基线根身份（两腿同值——CandidateId 公式锚）
 * @param warmup       [in] 是否预热样本（标记不计入统计）
 * @return 样本（valid=false 时 result/workDelta 无效——原因在 sample 内）
 */
ThroughputSample runOneSample(BenchJointFixture& fx, std::uint32_t threadCount,
                              const core::ObjectId& baselineRoot, bool warmup)
{
    ThroughputSample out;
    out.sample.excluded = warmup;

    // ---- 计时区间外的装配面（不构成吞吐——§4.5 排除项的区间承载）----
    const OptimizationConfiguration config = makeStageDConfig(threadCount);
    JointRunRequest req = fx.readyRequest(config, baselineRoot);
    JointLayeredCheckDeps deps = fx.checkerDeps();
    optimization::JointRunOrchestratorDeps d;
    d.checkerDeps = &deps;
    BenchProjector projector;
    d.projector = &projector;
    d.paretoBuilder = &sharedParetoBuilder();
    JointRunOrchestrator orch(OptimizationStage::StageD, d);

    // ---- 计时区间＝run() 全程（真实 steady_clock 墙钟——B-06 吞吐证据）----
    const std::uint64_t sinkBefore = g_workSink.load(std::memory_order_relaxed);
    const auto t0 = std::chrono::steady_clock::now();
    try {
        out.result = orch.run(req, fx.ctx);
        const auto t1 = std::chrono::steady_clock::now();
        out.sample.durationMs
            = std::chrono::duration<double, std::milli>(t1 - t0).count();

        // 有效判据①（§4.2"运行完整结束"）：正常完成相。
        if (!out.result.runCompleted || out.result.runPhase != RunPhase::Completed) {
            out.sample.valid = false;
            out.sample.invalidReason = "run 未完整结束（runCompleted=false 或 phase!=Completed）";
        }
        // 有效判据②：评估计数恰为固定候选集规模（漂移即测量对象失真）。
        out.evaluatedCount = static_cast<std::uint64_t>(out.result.audit.base.quickEvaluated)
                             + static_cast<std::uint64_t>(out.result.audit.base.verifiedEvaluated);
        if (out.sample.valid && out.evaluatedCount != kExpectedEvaluations) {
            out.sample.valid = false;
            out.sample.invalidReason = "评估计数漂移（期望 "
                                       + std::to_string(kExpectedEvaluations) + " 实测 "
                                       + std::to_string(out.evaluatedCount) + "）";
        }
        // 吞吐回填（candidates/s——候选集固定故评估计数恒定，吞吐与
        // 1/时长同序；中位数在统计面取）。
        if (out.sample.valid && out.sample.durationMs > 0.0) {
            out.throughput = static_cast<double>(out.evaluatedCount)
                             / (out.sample.durationMs / 1000.0);
        }
    } catch (const std::exception& e) {
        const auto t1 = std::chrono::steady_clock::now();
        out.sample.durationMs
            = std::chrono::duration<double, std::milli>(t1 - t0).count();
        out.sample.valid = false;
        out.sample.invalidReason = e.what();  // 测量区间不完整——留痕原因
    }
    out.workDelta = g_workSink.load(std::memory_order_relaxed) - sinkBefore;
    return out;
}

/**
 * @brief 执行一组采样（预热剔除＋有效样本循环——testkit runInteractionGroup
 *        的基准侧同构纪律；样本对象承载结论面故本处自持循环）。
 *
 * 无效样本不计入分母、逐例留痕并打到 stderr（testkit 同款即时可见）；
 * 超安全界（10×）显性失败——操作持续失败属夹具缺陷，补采无意义。
 *
 * @param fx           [in] 夹具
 * @param threadCount  [in] 本组并行度
 * @param baselineRoot [in] 基线根身份（两腿同值——CandidateId 公式锚）
 * @param opId         [in] 组标识（报告用——"t1"/"t8"）
 * @return 全量执行序列（执行序：预热在前、采样在后）
 */
std::vector<ThroughputSample> runSampleGroup(BenchJointFixture& fx,
                                             std::uint32_t threadCount,
                                             const core::ObjectId& baselineRoot,
                                             const std::string& opId)
{
    std::vector<ThroughputSample> samples;
    // ---- 预热腿（§4.2 剔除各组首次预热——样本留痕、标记不计入统计）----
    for (std::size_t i = 0; i < kWarmupRuns; ++i) {
        samples.push_back(runOneSample(fx, threadCount, baselineRoot, /*warmup=*/true));
    }
    // ---- 采样腿（循环到有效样本达标——无效不计入分母但逐例留痕）----
    std::size_t invalidCount = 0;
    while (std::count_if(samples.begin(), samples.end(),
                         [](const ThroughputSample& s) {
                             return s.sample.valid && !s.sample.excluded;
                         })
           < kValidSamples) {
        ThroughputSample s = runOneSample(fx, threadCount, baselineRoot, /*warmup=*/false);
        if (!s.sample.valid) {
            ++invalidCount;
            std::fprintf(stderr,
                         "[perf-b06] invalid sample (op=%s nth=%zu): %s\n",
                         opId.c_str(), invalidCount, s.sample.invalidReason.c_str());
            std::fflush(stderr);
            if (invalidCount > kInvalidCeiling) {
                ADD_FAILURE() << "无效样本超过安全界（op=" << opId
                              << " invalid=" << invalidCount
                              << "）——持续失败属夹具缺陷，最后原因："
                              << s.sample.invalidReason;
                break;
            }
        }
        samples.push_back(std::move(s));
    }
    return samples;
}

/**
 * @brief 结论面等价判定汇总（逐元素对账的观测面——断言面在调用方）。
 */
struct EquivalenceSummary {
    std::size_t checkedRecords = 0;  ///< 已对账记录数（Quick＋Verified）
    std::size_t checkedMetrics = 0;  ///< 已对账指标元素数
    double maxRelDiff = 0.0;         ///< 最大逐元素相对差（ref≠0 元素——
                                     ///  |a−b|/|ref|；容差判定 1×10⁻¹²）
    bool bitwiseAll = true;          ///< 全部数值元素逐位相等（零重排结构的
                                     ///  实测形态——ε_abs=0 声明的实测佐证）
    bool ok = true;                  ///< 全部结构等价（计数/序/状态/审计）
    std::string firstMismatch;       ///< 首个失配描述（ok==false 时非空）
};

/**
 * @brief 并行样本结论面对照单线程参照（NFR-COR-02/附录 D 第 8 项的逐元素
 *        判定——吞吐运行中并行归约正确性的承载）。
 *
 * 判定面五层：①记录计数与候选序（生成序＝合并序——稳定排序与线程数无
 * 关）；②逐候选状态；③八项指标逐元素 |a−b| ≤ ε_rel·|ref|＋ε_abs（ε_abs
 * ＝黄金算例声明值 kReductionAbsTol——零参考元素退化为要求精确相等，
 * C4 公式原文）；④Pareto 序与可行集规模；⑤审计计数（与线程数无关的
 * 确定性账目——工作汇增量对账在调用方采样层）。任一失配即 ok=false＋
 * 首失配描述（调用方 ADD_FAILURE——本函数不直接断言，保持纯判定）。
 */
EquivalenceSummary compareConclusions(const JointRunResult& par, const JointRunResult& ref)
{
    EquivalenceSummary sum;
    auto fail = [&sum](const std::string& why) {
        sum.ok = false;
        if (sum.firstMismatch.empty()) {
            sum.firstMismatch = why;
        }
    };

    // ① 记录计数与候选序（Quick 与 Verified 两相分别对账）。
    if (par.quickRecords.size() != ref.quickRecords.size()) {
        fail("Quick 记录数失配（ref=" + std::to_string(ref.quickRecords.size())
             + " par=" + std::to_string(par.quickRecords.size()) + "）");
    } else if (par.verifiedRecords.size() != ref.verifiedRecords.size()) {
        fail("Verified 记录数失配（ref=" + std::to_string(ref.verifiedRecords.size())
             + " par=" + std::to_string(par.verifiedRecords.size()) + "）");
    }
    if (!sum.ok) {
        return sum;  // 计数失配无从逐元素——提前返回（首失配已留痕）
    }

    // ②③ 两相逐记录：候选序（candidateId）＋状态＋八项指标逐元素。
    const std::vector<optimization::JointRunRecord>* phases[2] = {
        &ref.quickRecords, &ref.verifiedRecords};
    const std::vector<optimization::JointRunRecord>* parPhases[2] = {
        &par.quickRecords, &par.verifiedRecords};
    for (int phase = 0; phase < 2; ++phase) {
        const auto& refRecords = *phases[phase];
        const auto& parRecords = *parPhases[phase];
        for (std::size_t i = 0; i < refRecords.size(); ++i) {
            const auto& r = refRecords[i];
            const auto& p = parRecords[i];
            ++sum.checkedRecords;
            if (!(r.base.candidateId == p.base.candidateId)) {
                fail("候选序漂移（相 " + std::to_string(phase) + " 下标 "
                     + std::to_string(i) + "——并行合并必须与串行同序）");
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
                // 附录 D 第 8 项通用比较公式（C4）：|a−b| ≤ ε_rel·|ref|＋
                // ε_abs；零参考元素退化为 ε_abs（声明 0 ⇒ 要求精确相等）。
                if (!(diff <= kReductionRelTol * refAbs + kReductionAbsTol)) {
                    fail("归约超容差（下标 " + std::to_string(i) + " 指标 "
                         + std::to_string(m) + " diff=" + std::to_string(diff)
                         + "）");
                    return sum;
                }
                if (diff > sum.maxRelDiff && refAbs > 0.0) {
                    sum.maxRelDiff = diff / refAbs;  // 相对差观测面（ref≠0）
                }
                if (diff != 0.0) {
                    sum.bitwiseAll = false;  // 逐位相等观测面（实测应恒 true）
                }
            }
        }
    }

    // ④ Pareto 序与可行集规模（等价集合＋稳定排序的结论面承载）。
    if (par.pareto.entries.size() != ref.pareto.entries.size()) {
        fail("Pareto 条目数失配（ref=" + std::to_string(ref.pareto.entries.size())
             + " par=" + std::to_string(par.pareto.entries.size()) + "）");
        return sum;
    }
    for (std::size_t i = 0; i < ref.pareto.entries.size(); ++i) {
        if (!(ref.pareto.entries[i].candidateId == par.pareto.entries[i].candidateId)) {
            fail("Pareto 序漂移（下标 " + std::to_string(i) + "）");
            return sum;
        }
    }
    if (par.pareto.feasibleIds.size() != ref.pareto.feasibleIds.size()) {
        fail("可行集规模失配");
        return sum;
    }

    // ⑤ 审计计数（与线程数无关——I-J2 记账纪律；工作汇增量对账在调用方
    // ThroughputSample 层执行——workDelta 属采样载体不属结论面）。
    if (par.audit.base.candidatesGenerated != ref.audit.base.candidatesGenerated
        || par.audit.base.quickEvaluated != ref.audit.base.quickEvaluated
        || par.audit.base.quickScreenedOut != ref.audit.base.quickScreenedOut
        || par.audit.base.verifiedEvaluated != ref.audit.base.verifiedEvaluated) {
        fail("审计计数失配（与线程数无关性违约）");
        return sum;
    }
    return sum;
}

// =====================================================================
// 统计面与报告组装（testkit PerfSessionReport 复用——协议 §4.8 留痕面）
// =====================================================================

/// 环境事实读取（§4.1 登记清单的环境变量可得面——缺失如实留空）。
std::string envOr(const char* name, const char* fallback = "")
{
    const char* v = std::getenv(name);
    return (v != nullptr && *v != '\0') ? std::string(v) : std::string(fallback);
}

/// 有效样本吞吐中位数（升序排序；奇数取中位元素——本基准 15 样本恒走奇
/// 数路径；偶数路径＝两中位均值，登记于报告 medianConvention 备查）。
double medianOf(std::vector<double> values)
{
    if (values.empty()) {
        return 0.0;  // 调用方保证非空（有效样本 ≥15 达标后才进统计面）
    }
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if (n % 2 == 1) {
        return values[n / 2];
    }
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

/// 由原始样本序列组装分组报告（统计面经 testkit 单点函数——P95 仅作分布
/// 参考；判定统计量＝吞吐中位数比，在判定处另行计算；调用方保证有效非空）。
testkit::InteractionGroupReport makeGroupReport(const std::string& opId,
                                                const std::string& stateId,
                                                const std::vector<ThroughputSample>& samples)
{
    testkit::InteractionGroupReport g;
    g.opId = opId;
    g.stateId = stateId;
    std::vector<double> validMs;
    for (const ThroughputSample& s : samples) {
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
        // P95/min/max 一并报告仅作分布参考（禁以分位数表述达标——B-06 非
        // P95 类基准，统计口径区分行）。
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
    r.baselineDoc = "performance-baseline #2 B-06";
    // 软件版本锁定（§4.1）：commit 由执行方经 IRD_PERF_GIT_COMMIT 注入
    //（缺省如实留空——commit SHA 在首测报告文档登记）。
    r.gitCommit = envOr("IRD_PERF_GIT_COMMIT");
    r.buildConfig = "Release";
    r.datasetId = "ird-perf-b06-parallel-throughput";
    r.datasetVersion = "1";
    r.machineName = envOr("COMPUTERNAME");
    r.cpuInfo = envOr("PROCESSOR_IDENTIFIER")
                + " x" + envOr("NUMBER_OF_PROCESSORS", "?");
    r.osInfo = envOr("OS", "Windows") + " " + envOr("PROCESSOR_ARCHITECTURE");
    // 内存/存储/电源：环境变量不可得——如实指向首测报告的硬件清单。
    r.memoryInfo = "见首测报告硬件清单（traceability/benchmarks/"
                   "wp23-t07-parallel-throughput-baseline.md §2）";
    r.storageInfo = "见首测报告硬件清单";
    r.powerPolicyNote = "见首测报告硬件清单";
    r.backgroundNote = "单进程单测试串行执行（run 内部并行除外）；测量期间"
                       "无其他基准并行（performance-baseline §4 测量纪律——"
                       "串行跑分）";
    // 口径登记（报告自含——验收对照面）。
    r.notes.push_back(
        "B-06 为中位吞吐类判定（performance-baseline §2 统计口径区分）：判定"
        "统计量＝两组有效样本吞吐中位数之比（需求 NFR-PERF-05 原文『中位吞吐』"
        "——与 P95 类基准互不替代，禁以 P95/最大值表述达标）；"
        "verdicts.measuredP95Ms 字段沿夹具 schema 承载判定统计量（腿一 verdict"
        "＝吞吐比〔无量纲〕、腿二 verdict＝最大逐元素相对差〔无量纲〕——非毫秒，"
        "字段名沿 schema 的登记性复用，T03/T06 同款纪律）");
    r.notes.push_back(
        "medianConvention：升序排序后奇数样本取中位元素、偶数样本取两中位均值"
        "（本基准每组 15 有效样本恒走奇数路径）；吞吐＝每有效样本"
        "(quickEvaluated＋verifiedEvaluated)/墙钟秒（candidates/s）");
    r.notes.push_back(
        "排除项口径（NFR-PERF-05 原文『不含首次编译与导入』）：计时区间＝"
        "JointRunOrchestrator::run() 全程（steady_clock）——候选集构造/快照组装/"
        "注册表装配/编排器构造在区间外；每组首跑预热剔除（§4.5『正式计时前执行"
        "预热运行』）承担冷路径（注册表首查/分配器/代码路径首触）");
    r.notes.push_back(
        "并行归约容差（NFR-COR-02/附录 D 第 8 项 P-01 冻结）：|a−b| ≤ 1e-12·|ref|"
        "＋ε_abs 逐元素；ε_abs 黄金算例声明值＝0.0（零浮点重排结构下最严口径）；"
        "等价面另含候选序/状态/Pareto 序/审计计数/工作汇增量五层");
    r.notes.push_back(
        "替身边界：内层四域评估器＝确定性浮点迭代链替身（每派发 "
        + std::to_string(kWorkIterations)
        + " 次串行 FP 乘加）＋P-OPT-2 投影替身——证明编排并行分片/稳定合并的"
        "吞吐规模化与归约等价，非业务评估器算法吞吐画像（归集成模式扩展测量"
        "挂点——首测报告登记，集成构建由合入批执行）");
    r.notes.push_back(
        "数据集 manifest＝testdata/performance/b06-parallel-throughput-manifest"
        ".json（datasetId ird-perf-b06-parallel-throughput@1——参数冻结，升级"
        "换版本）");
    return r;
}

}  // namespace

// =====================================================================
// B-06 达标判定用例（固定候选评估 8 线程 ≥4× 中位吞吐＋并行归约容差）
// =====================================================================

TEST(OptParallelThroughput, FixedCandidateMedianThroughputRatio_WP23T07_B06)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-PERF-05", "NFR-COR-02"},
                  std::vector<std::string>{"AT-35"});

    // 基线根身份（两腿同值——CandidateId＝f(baselineRoot, baselineCv,
    // patch) 的公式锚：同一根身份 ⇒ 两腿候选集合逐 ID 恒等，等价判定
    // 才有逐候选可比性——JointRunTest 恢复用例"基线身份钉住"同款纪律）。
    const core::ObjectId sharedBaselineRoot = core::ObjectId::generate();

    // ---- 腿一：单线程组（吞吐比分母——NFR-PERF-05 原文"单线程"）--------
    // 同一 fixture 跨样本复用（组内"同一候选集/硬件/软件版本"固定）；
    // 首跑预热承担组内冷路径（§4.5 排除项）。
    BenchJointFixture serialFx;
    const std::vector<ThroughputSample> serialSamples
        = runSampleGroup(serialFx, kThreadCountSerial, sharedBaselineRoot, "t1");

    // 参照结论面＝首个有效样本（确定性 ⇒ 后续有效样本逐位同面——组内
    // 一致性对账顺带钉住 NFR-COR-02 的"同种子重放"半边）。
    const JointRunResult* reference = nullptr;
    for (const ThroughputSample& s : serialSamples) {
        if (s.sample.valid && !s.sample.excluded) {
            reference = &s.result;
            break;
        }
    }
    ASSERT_NE(reference, nullptr) << "单线程组无有效样本（协议 §4.2 违约）";
    for (const ThroughputSample& s : serialSamples) {
        if (!s.sample.valid || s.sample.excluded) {
            continue;  // 预热/无效样本不入组内一致性对账（无效另由组规模断言收口）
        }
        const EquivalenceSummary self = compareConclusions(s.result, *reference);
        EXPECT_TRUE(self.ok)
            << "单线程组内确定性失配（同种子重放须同结论面——NFR-COR-02）："
            << self.firstMismatch;
    }
    // 工作汇增量组内一致（确定性链——计算确实发生且恒量）。
    std::uint64_t serialWork = 0;
    for (const ThroughputSample& s : serialSamples) {
        if (s.sample.valid && !s.sample.excluded) {
            if (serialWork == 0) {
                serialWork = s.workDelta;
            } else {
                EXPECT_EQ(s.workDelta, serialWork) << "单线程组工作汇漂移";
            }
        }
    }
    EXPECT_NE(serialWork, std::uint64_t{0}) << "工作汇为零——工作量未真实发生";

    // ---- 腿二：8 线程组（吞吐比分子——NFR-PERF-05 原文"8 个工作线程"）----
    // 全新 fixture＝全新装配路径（组间冷路径由本组首跑预热剔除）；基线
    // 根身份与腿一同值（同候选集合的锚——见 sharedBaselineRoot 注）。
    BenchJointFixture parFx;
    const std::vector<ThroughputSample> parSamples
        = runSampleGroup(parFx, kThreadCountParallel, sharedBaselineRoot, "t8");

    // ---- 并行归约正确性（每个并行有效样本对照单线程参照——逐元素）----
    EquivalenceSummary worst;  // 全样本最坏观测（报告回填）
    std::uint64_t parWork = 0;
    for (const ThroughputSample& s : parSamples) {
        if (!s.sample.valid || s.sample.excluded) {
            continue;
        }
        const EquivalenceSummary eq = compareConclusions(s.result, *reference);
        EXPECT_TRUE(eq.ok) << "并行归约等价失配（NFR-COR-02/附录 D 第 8 项）："
                           << eq.firstMismatch;
        if (eq.maxRelDiff > worst.maxRelDiff) {
            worst.maxRelDiff = eq.maxRelDiff;
        }
        worst.checkedRecords += eq.checkedRecords;
        worst.checkedMetrics += eq.checkedMetrics;
        worst.bitwiseAll = worst.bitwiseAll && eq.bitwiseAll;
        worst.ok = worst.ok && eq.ok;
        if (parWork == 0) {
            parWork = s.workDelta;
        } else {
            EXPECT_EQ(s.workDelta, parWork) << "并行组工作汇漂移";
        }
    }
    EXPECT_EQ(parWork, serialWork)
        << "两腿工作汇增量不一致（确定性工作量与线程数无关性违约）";
    EXPECT_TRUE(worst.ok) << "并行归约等价面存在失配";
    // ε_abs=0 声明的实测佐证（零重排结构下应逐位相等——如实断言；若未来
    // 实现引入重排，本断言与容差断言分离：容差内仍达标、逐位面如实翻红
    // 提示黄金算例声明须按实况修订）。
    EXPECT_TRUE(worst.bitwiseAll)
        << "出现容差内非逐位相等（黄金算例 ε_abs=0 声明与实测不符——须按实况"
           "修订声明并留痕，不得静默）";

    // ---- 统计面：吞吐中位数与比值判定（B-06 唯一达标口径）--------------
    std::vector<double> serialThr;
    std::vector<double> parThr;
    for (const ThroughputSample& s : serialSamples) {
        if (s.sample.valid && !s.sample.excluded) {
            serialThr.push_back(s.throughput);
        }
    }
    for (const ThroughputSample& s : parSamples) {
        if (s.sample.valid && !s.sample.excluded) {
            parThr.push_back(s.throughput);
        }
    }
    ASSERT_GE(serialThr.size(), kValidSamples) << "单线程组有效样本不足（§4.2）";
    ASSERT_GE(parThr.size(), kValidSamples) << "8 线程组有效样本不足（§4.2）";
    const double medianSerial = medianOf(serialThr);
    const double medianParallel = medianOf(parThr);
    ASSERT_GT(medianSerial, 0.0) << "单线程中位吞吐非正（统计面失真）";
    const double ratio = medianParallel / medianSerial;

    // 达标判定（NFR-PERF-05 原文——≥4 倍，照抄零私改；阈值来源随报告
    // thresholdSource 登记）。
    EXPECT_GE(ratio, kRatioThreshold)
        << "并行吞吐未达标（8 线程中位吞吐/单线程中位吞吐 = " << ratio
        << " < " << kRatioThreshold << "——NFR-PERF-05）";

    // 两腿零无效样本收口（§4.2 无效留痕纪律的判定面——有无效即如实翻红）。
    EXPECT_EQ(serialSamples.size() - kWarmupRuns, kValidSamples)
        << "单线程组采样序列长度异常";
    EXPECT_EQ(parSamples.size() - kWarmupRuns, kValidSamples)
        << "8 线程组采样序列长度异常";

    // 测量摘要打到控制台（gtest 日志——证据留痕的即时可读面）。
    std::printf(
        "[perf-b06] 固定候选集：candidates=%u verified=%u seed=%llu "
        "workIter=%u\n"
        "[perf-b06] 单线程：n=%zu 中位吞吐=%.2f candidates/s\n"
        "[perf-b06] 8 线程：n=%zu 中位吞吐=%.2f candidates/s\n"
        "[perf-b06] 中位吞吐比=%.3f（阈值 ≥%.1f——NFR-PERF-05）\n"
        "[perf-b06] 归约等价：records=%zu metrics=%zu maxRelDiff=%.3e "
        "bitwise=%s\n",
        kMaxCandidates, kMaxVerifiedCandidates,
        static_cast<unsigned long long>(kSeed), kWorkIterations,
        serialThr.size(), medianSerial, parThr.size(), medianParallel, ratio,
        kRatioThreshold, worst.checkedRecords, worst.checkedMetrics,
        worst.maxRelDiff, worst.bitwiseAll ? "true" : "false");
    std::fflush(stdout);

    // ---- 报告落盘（拖挂——IRD_PERF_REPORT_DIR 未设则跳过）--------------
    if (const auto dir = testkit::perfReportDirFromEnv()) {
        testkit::PerfSessionReport session = makeSessionHeader("b06-parallel-throughput");
        session.groups.push_back(
            makeGroupReport("t1", "fixed-candidates", serialSamples));
        session.groups.push_back(
            makeGroupReport("t8", "fixed-candidates", parSamples));
        // 腿一判定：吞吐比（阈值 4.0＝NFR-PERF-05 原文——零私改；字段沿
        // schema 的登记性复用见 notes 口径声明）。
        testkit::PerfThresholdVerdict v1;
        v1.opId = "t8-vs-t1";
        v1.stateId = "fixed-candidates";
        v1.thresholdMs = kRatioThreshold;
        v1.thresholdSource
            = "REQUIREMENTS NFR-PERF-05 原文（固定候选评估基准上，8 个工作"
              "线程相对单线程中位吞吐提升 ≥4 倍〔不含首次编译与导入〕——零私改）";
        v1.measuredP95Ms = ratio;  // 判定统计量＝吞吐比（见 notes）
        v1.pass = ratio >= kRatioThreshold;
        session.verdicts.push_back(v1);
        // 腿二判定：并行归约最大逐元素相对差（阈值 1×10⁻¹²＝附录 D 第 8 项
        // P-01 冻结值；ε_abs=0 黄金算例声明随 notes 登记）。
        testkit::PerfThresholdVerdict v2;
        v2.opId = "reduction-tolerance";
        v2.stateId = "fixed-candidates";
        v2.thresholdMs = kReductionRelTol;
        v2.thresholdSource
            = "REQUIREMENTS 附录 D 第 8 项（P-01 冻结——并行归约相对容差 "
              "1×10⁻¹²，通用比较公式逐元素）；ε_abs 黄金算例声明值＝0.0";
        v2.measuredP95Ms = worst.maxRelDiff;  // 判定统计量＝最大相对差（见 notes）
        v2.pass = worst.maxRelDiff <= kReductionRelTol && worst.ok;
        session.verdicts.push_back(v2);
        session.allPassed = v1.pass && v2.pass;
        // 样本纪律注记（原始序列全量在 groups——§4.8"不得只报统计值"）。
        session.notes.push_back(
            "样本纪律：每组 1 预热（剔除）＋" + std::to_string(kValidSamples)
            + " 有效样本；无效样本零容忍（invalidCount==0 收口）——§4.2");
        EXPECT_TRUE(testkit::writePerfReportJson(*dir, session))
            << "报告写失败（拖挂不阻断——检查 IRD_PERF_REPORT_DIR）";
    }
}
