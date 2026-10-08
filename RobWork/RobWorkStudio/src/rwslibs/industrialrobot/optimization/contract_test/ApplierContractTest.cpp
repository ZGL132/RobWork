/**
 * @file   ApplierContractTest.cpp
 * @brief  候选应用契约用例组（OptApplierContract）——OPT-VER-143~147：
 *         两步命令组合经①端口真实执行（分支创建/新修订/完整复算提示/
 *         基线字节不变）、过期基线 StaleRevisionRejected 候选保留、撤销/
 *         重做由 project 接管——任务契约 WP-20-T07 acceptance 2/3。
 *
 * 设计依据：
 *   - units/optimization.md §10.1/§10.2/§10.3（候选应用流程/命令组合与
 *     原子性/六项前置）、§13.1 用例矩阵行 OPT-VER-143（候选应用：创建
 *     分支＋新修订＋完整复算提示）/144（基线内容字节不变——对比基线
 *     修订对象字节）/145（分支记录 baseRevisionId；不复制对象）/146
 *     （过期基线：PRJ-STALE-REVISION-REJECTED；候选保留）/147（撤销/
 *     重做由 project 接管：新修订；历史不改写；optimization 无自建栈）
 *   - 需求 OPT-08（候选归属/预览不改基线/设为当前方案三件套）、AT-12
 *     （基线保护与差异呈现）、PM-04（过期基线）、PM-12（方案分支）、
 *     PM-18（撤销/重做＝逆命令新修订）、PA-2（历史只增不改）
 *   - 架构决策 DOPT-6（两步组合；optimization 零命令 token 注册——本
 *     文件的产品面零 HandlerRegistry 调用，注册只发生在测试替身）
 *
 * ★ 测试口径登记（DTB §5.4——跨单元替身的诚实边界，必读）：
 *   1. **step1 建支替身**（token "test-create-branch"）：project 内置
 *      元数据命令族（project.md §6.5 的 project.create-branch）实体尚未
 *      落位（P-PR-9 已裁决无点形态、处理器落位随 project 侧增量——
 *      PRJ-T10 登记机制面就绪）。本测试以 project 单元测试桩同款语义
 *      （CommandPlan.metadataChange.createBranchWithBase 声明面）注册
 *      无点替身，承载建支的**机制真值**（分支表增量/元数据修订发布/
 *      INV-M3 查询投影全部是 project 生产代码——替身只是触发信封）。
 *      产品面 optimization 代码零 token 注册（DOPT-6；生产接入＝L5
 *      装配层注册 project 内置处理器实体）。
 *   2. **step2 应用替身**（token "apply-robot-design"＝modeling 既有
 *      注册 token 的引用）：本单元无 modeling 编译边（R-1——白名单九
 *      边不含 modeling），不可链接真实 ApplyRobotDesignHandler；替身按
 *      其机制契约等价承载（requiresDualCompile=true＋根对象字节替换/
 *      新建＋快照式逆载荷声明——modeling §9.3 行 1/§6.9 同型），消费
 *      CandidateApplyPlan.step2 的语义字段（token/expectedRevision/
 *      payload）。真实处理器接入随 L5 装配（生产路径），契约观测点
 *      （新修订/基线不变/双编译门）由 project 生产代码兑现——不因替
 *      身而弱化。
 *   3. **编译桩**（IModelCompilePort）：PRJ-TX-3 同款（ok=true 桩——
 *      双编译事务编排是 project 生产代码；MDL-06 失败路径归 project
 *      契约测试，本组不重复）。
 *   4. 基线设计字节为自持伪 canonical（固定字节序列——project D-10
 *      透传存储不解释；建模语义级 canonical 校验随真实 modeling 处理
 *      器接入）。
 */

#include <sdurws/ird/optimization/Applier.hpp>
#include <sdurws/ird/optimization/Run.hpp>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/CommandService.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/UndoRedo.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird;
using optimization::ApplyCandidateRequest;
using optimization::CandidateApplyPlan;
using optimization::CandidateId;
using optimization::CandidatePatch;
using optimization::IOptimizationCandidateApplier;
using optimization::OptimizationCandidateApplier;
using optimization::OptimizationRunId;
using optimization::OptimizationRunResult;
using optimization::TwoStageRunRecord;
using optimization::TwoStageRunResult;
using project::CommandEnvelope;
using project::CommandResult;
using project::HandlerRegistry;
using project::ICommandHandler;
using project::PrepareOutcome;
using project::ProjectCommandService;
using project::ProjectStoreFactory;

namespace {

// =====================================================================
// 契约测试替身（文件头口径登记 1~3——机制真值全在 project 生产代码）
// =====================================================================

/// 双编译桩（PRJ-TX-3 成功路——双编译事务编排是 project 生产代码）。
class OkCompilePort final : public project::IModelCompilePort {
public:
    project::CompileResult compileWorkCellAndDwc(
        const project::CompileRequest& /*request*/) override
    {
        ++calls;
        return project::CompileResult{true, {}};
    }
    int calls = 0;  ///< 双编译编排触发计数（requiresDualCompile 观测点）
};

/// step1 建支替身（口径登记 1——CommandPlan.metadataChange 声明面）：
/// 载荷＝分支 label UTF-8 字节；建支源分支＝信封活动分支。
class CreateBranchStubHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-create-branch";  // 无点 token（O-35 裁决形态——非产品面注册）
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(project::HandlerContext& /*ctx*/,
                           const CommandEnvelope& envelope,
                           const project::RevisionView& /*baseSnapshot*/,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::MetadataChange change;
        change.createBranchWithBase = envelope.branch;  // 源分支＝活动分支
        change.label.assign(envelope.payloadCanonical.begin(),
                            envelope.payloadCanonical.end());
        out.metadataChange = std::move(change);
        out.summary = "创建方案分支（测试替身）";
        return PrepareOutcome::Planned;
    }
};

/// step2 应用替身（口径登记 2——modeling apply-robot-design 的机制等价
/// 面）：载荷＝候选设计 canonical 字节（透传写根对象——D-10）；有基线
/// 根 ⇒ 显式 oid 字节替换，无根 ⇒ allocateNew 取号；requiresDualCompile
/// ＝true；可逆（快照式逆载荷＝基线根字节——modeling §6.9 同型）。
class ApplyDesignStubHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return std::string(optimization::kApplyRobotDesignCommandToken);
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(project::HandlerContext& ctx,
                           const CommandEnvelope& envelope,
                           const project::RevisionView& baseSnapshot,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        // 定位基线根对象（RobotDesign token—— apply 槽形状语义：恰一根）。
        const project::ObjectRef* root = nullptr;
        for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
            if (ref.objectTypeToken == "RobotDesign") {
                root = &ref;
                break;
            }
        }
        project::ObjectWrite write;
        if (root != nullptr) {
            write.objectId = root->objectId;  // 字节替换（引用稳定——引用表不动）
            // 快照式逆载荷＝基线根字节（undo 经 restore 还原——§6.9）。
            out.inverseCommandType = "test-restore-robot-design";
            out.inversePayloadCanonical
                = ctx.query().object(root->objectId, root->contentVersion);
        }
        write.objectTypeToken = "RobotDesign";
        write.payloadCanonical = envelope.payloadCanonical;
        out.objectWrites.push_back(std::move(write));
        out.requiresDualCompile = true;  // MDL-06（plan.step2 的命令面承载）
        out.summary = "应用候选设计（测试替身）";
        return PrepareOutcome::Planned;
    }
};

/// 逆命令替身（restore——快照式逆放：显式 oid 根对象字节还原）。**对称
/// 可逆族**：restore 自身也声明 inverse＝apply 重放（载荷＝被撤销修订的
/// 根对象字节＝候选字节）——project 的 redo 条目数据源＝撤销修订的
/// inverse（UndoRedoServiceImpl 实现口径②），非对称声明（restore 无
/// inverse）时 redo 不入栈——本替身对称声明以兑现 OPT-VER-147 的
/// undo/redo 全链。
class RestoreDesignStubHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-restore-robot-design";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(project::HandlerContext& ctx,
                           const CommandEnvelope& envelope,
                           const project::RevisionView& baseSnapshot,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        for (const project::ObjectRef& ref : baseSnapshot.objectRefs) {
            if (ref.objectTypeToken == "RobotDesign") {
                project::ObjectWrite write;
                write.objectId = ref.objectId;
                write.objectTypeToken = "RobotDesign";
                write.payloadCanonical = envelope.payloadCanonical;
                out.objectWrites.push_back(std::move(write));
                // 对称声明面：restore 的逆＝重放被撤销的 apply（载荷＝
                // baseSnapshot 中的根对象字节——即 undo 时点的候选字节）。
                out.inverseCommandType
                    = std::string(optimization::kApplyRobotDesignCommandToken);
                out.inversePayloadCanonical
                    = ctx.query().object(ref.objectId, ref.contentVersion);
                break;
            }
        }
        out.summary = "还原基线设计（测试替身）";
        return PrepareOutcome::Planned;
    }
};

/// 基线种子替身（step0——建立 RobotDesign 根对象作基线；不可逆——
/// 首建无基线可还，与采用链无关）。
class SeedDesignStubHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "test-seed-robot-design";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(project::HandlerContext& /*ctx*/,
                           const CommandEnvelope& envelope,
                           const project::RevisionView& /*baseSnapshot*/,
                           project::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::ObjectWrite write;  // 空 objectId＝allocateNew（project 取号）
        write.objectTypeToken = "RobotDesign";
        write.payloadCanonical = envelope.payloadCanonical;
        out.objectWrites.push_back(std::move(write));
        out.summary = "建立基线设计（测试替身）";
        return PrepareOutcome::Planned;
    }
};

// =====================================================================
// 共享夹具（真实 project store——createNew 临时目录＋替身装配）
// =====================================================================

class ApplierContractFixture : public ::testing::Test {
public:
    std::filesystem::path dir;
    std::unique_ptr<project::OpenStoreResult> opened;
    OkCompilePort compilePort;

    /// 基线/候选设计字节（伪 canonical——口径登记 4；固定值保证确定性）。
    const std::vector<std::uint8_t> baselineBytes{'I', 'R', 'D', '-', 'B', '0'};
    const std::vector<std::uint8_t> candidateBytes{'I', 'R', 'D', '-', 'C', '1'};

    /// 物化缝（候选字节给定——P-OPT-3 裁决前的测试侧供给）。
    struct FixedMaterializer final : optimization::ICandidateDesignMaterializer {
        const std::vector<std::uint8_t>* payload = nullptr;
        std::vector<std::uint8_t> materialize(
            const CandidatePatch&,
            const std::vector<optimization::VariableBinding>&) const override
        {
            return *payload;
        }
    };
    FixedMaterializer materializer;

    void SetUp() override
    {
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / ("ird-opt-t07-" + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = std::make_unique<project::OpenStoreResult>(
            ProjectStoreFactory::createNew(dir, "opt t07 contract", nullptr, nullptr));

        // 装配期注册（公共通道 handlerRegistry——UI-T39 增量；口径登记
        // 1~3：替身注册只发生在测试，产品面 optimization 零注册调用）。
        project::HandlerRegistry& registry = opened->store->handlerRegistry();
        registry.registerHandler(std::make_unique<CreateBranchStubHandler>());
        registry.registerHandler(std::make_unique<ApplyDesignStubHandler>());
        registry.registerHandler(std::make_unique<RestoreDesignStubHandler>());
        registry.registerHandler(std::make_unique<SeedDesignStubHandler>());
        opened->store->commands().attachCompilePort(&compilePort);

        materializer.payload = &candidateBytes;
    }

    void TearDown() override
    {
        opened.reset();  // 析构释放锁句柄（先于目录清理——project 先例同序）
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    /// 主分支身份（createNew 初始分支）。
    [[nodiscard]] core::BranchId mainBranch() const
    {
        return opened->store->query().currentMetadata().record.primaryBranchId;
    }

    /// step0：在主分支建立基线设计（返回基线修订视图——根对象锚）。
    project::RevisionView seedBaseline()
    {
        CommandEnvelope env;
        env.branch = mainBranch();
        env.commandType = "test-seed-robot-design";
        env.payloadFormatVersion = 1;
        env.payloadCanonical = baselineBytes;
        const CommandResult r = opened->store->commands().submit(env);
        EXPECT_TRUE(r.committed()) << "step0 基线建立必须成功（前置事实）";
        return opened->store->query().head();
    }

    /// 基线修订的根对象引用（RobotDesign token——恰一根）。
    [[nodiscard]] static const project::ObjectRef& rootRefOf(
        const project::RevisionView& view)
    {
        for (const project::ObjectRef& ref : view.objectRefs) {
            if (ref.objectTypeToken == "RobotDesign") {
                return ref;
            }
        }
        ADD_FAILURE() << "修订视图缺少 RobotDesign 根引用（前置事实破坏）";
        static project::ObjectRef dummy{};
        return dummy;
    }

    /// 构造可采用运行聚合（Completed＋单一 Verified-Feasible 基线候选，
    /// 绑定修订/基线锚——候选归属容器按 assembleRunResult 组装）。
    [[nodiscard]] OptimizationRunResult makeRun(const project::RevisionView& base)
    {
        const project::ObjectRef& root = rootRefOf(base);
        TwoStageRunResult done;
        done.runPhase = optimization::RunPhase::Completed;
        done.runCompleted = true;
        TwoStageRunRecord rec;
        rec.candidateId = optimization::candidateIdOf(root.objectId,
                                                      root.contentVersion,
                                                      CandidatePatch{});
        rec.patch = CandidatePatch{};  // 空补丁（基线候选——合法形态）
        rec.isBaseline = true;
        rec.mode = core::EvaluationMode::Verified;
        rec.screeningOnly = false;
        rec.status = optimization::CandidateStatus::Feasible;
        rec.formalPassEligible = true;
        done.verifiedRecords = {rec};
        return optimization::assembleRunResult(
            OptimizationRunId::generate(),
            opened->store->projectId(),  // 项目身份（ProjectStore 公共访问器）
            mainBranch(), base.id,
            core::ContentIdentity::fromCanonical(
                "cid-0000000000000000000000000000000000000000000000000000000000000c1d"),
            root.objectId, root.contentVersion,
            optimization::OptimizationConfiguration{}, done);
    }

    /// 组装请求（基线状态面自②端口投影；携带基线设计字节——预览输入）。
    [[nodiscard]] ApplyCandidateRequest makeRequest(
        const OptimizationRunResult& run,
        const std::vector<std::uint8_t>& baselineDesignBytes) const
    {
        // 前置契约：run 由 makeRun 产出（候选恒非空——调用方保证；
        // 非断言面——本函数返回值非 void，不用 ASSERT 族）。
        ApplyCandidateRequest req;
        req.run = run;
        req.candidateId = run.candidates.front().candidateId;
        req.baseline.branch = mainBranch();
        req.baseline.tip = run.revision;   // 组装时点基线＝运行输入修订
        req.baseline.writable = opened->writable;
        req.baselineDesignCanonical = baselineDesignBytes;
        return req;
    }

    /// 执行编排（测试侧——生产路径归 ui/L5 装配；消费 plan 两步语义）：
    /// step1 建支 → branchTips 增量定位新分支 → step2 应用（branch 回填）。
    /// 返回 {step1/step2 结果, 新分支 id, step1 后的分支快照（此时 tip 仍
    /// 指向建支基——"建支不产生设计变更"的观测窗）}。
    struct TwoStepOutcome {
        CommandResult step1;
        CommandResult step2;
        core::BranchId newBranch{};
        project::BranchTip branchAfterCreate{};  ///< step1 后的新分支快照（值拷贝）
    };
    [[nodiscard]] TwoStepOutcome executePlan(const CandidateApplyPlan& plan,
                                             core::BranchId sourceBranch)
    {
        // 执行编排前置（非断言面——本函数返回值非 void，不用 ASSERT 族；
        // 违约以 ADD_FAILURE 标记并返回空结果，调用方的 ASSERT 接力终止）。
        TwoStepOutcome out;
        if (!plan.allowApply || !plan.applyDesign.has_value()) {
            ADD_FAILURE() << "执行编排前置：计划必须可应用且 step2 已组装";
            return out;
        }

        // step1：建支（信封由执行编排构造——token/payload 版本归装配面，
        // 见 Applier.hpp 落位口径）。
        CommandEnvelope env1;
        env1.branch = sourceBranch;
        env1.commandType = "test-create-branch";
        env1.payloadFormatVersion = 1;
        env1.payloadCanonical.assign(plan.createBranch.label.begin(),
                                     plan.createBranch.label.end());
        out.step1 = opened->store->commands().submit(env1);
        if (!out.step1.committed()) {
            return out;
        }

        // 新分支定位：分支表增量中 base==建支基的新条目（BranchId 由
        // project 在 S6 分配——组装期不可知，执行编排回填）。遍历取值
        // 拷贝（branchTips() 返回临时向量——引用迭代即悬空，禁止存指针）。
        const core::RevisionId base = plan.createBranch.baseRevisionId;
        for (const project::BranchTip& tip : opened->store->query().branchTips()) {
            if (tip.base == base && !(tip.id == sourceBranch)) {
                out.newBranch = tip.id;
                out.branchAfterCreate = tip;  // 建支后快照（tip 此时应＝建支基）
            }
        }
        EXPECT_TRUE(out.newBranch.isValid()) << "step1 后必须能定位新方案分支";
        // "建支不产生设计变更"（OPT-VER-145 语义面）：新分支 tip 一次写入
        // ＝建支基（project §4.5.1 走查步骤 1——在 step2 之前观测，此窗
        // 口过后 tip 将随应用前进）。
        EXPECT_EQ(out.branchAfterCreate.tip, base);
        EXPECT_EQ(out.branchAfterCreate.base, base);

        // step2：候选设计应用（branch 回填新分支；expectedRevision＝新分
        // 支 tip＝建支基——plan 语义）。
        CommandEnvelope env2;
        env2.branch = out.newBranch;
        env2.commandType = plan.applyDesign->commandType;
        env2.payloadFormatVersion = 1;  // 替身受理版本（生产＝modeling 处理器契约）
        env2.expectedRevision = plan.applyDesign->expectedRevision;
        env2.payloadCanonical = plan.applyDesign->payloadCanonical;
        out.step2 = opened->store->commands().submit(env2);
        return out;
    }

    /// 磁盘修订目录计数（"无修订落盘"观测点——project 契约测试同款）。
    [[nodiscard]] std::size_t revisionDirCount() const
    {
        std::size_t n = 0;
        std::error_code ec;
        for (auto it : std::filesystem::directory_iterator(dir / "revisions", ec)) {
            if (it.is_directory()) {
                ++n;
            }
        }
        return n;
    }
};

// =====================================================================
// OPT-VER-143/145：两步组合——分支创建＋新修订＋复算提示＋不复制对象
// =====================================================================

TEST_F(ApplierContractFixture, TwoStepApplyCreatesBranchAndRevision_WP20T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-12"}, std::vector<std::string>{"AT-12"});

    // 前置：基线修订（step0）＋运行聚合（候选归属修订）。
    const project::RevisionView base = seedBaseline();
    const project::ObjectRef& root = rootRefOf(base);
    const OptimizationRunResult run = makeRun(base);
    const std::size_t revisionsBefore = revisionDirCount();

    // 组装（六项前置——tip==运行修订、可写、物化缝在位）。
    const OptimizationCandidateApplier applier({&materializer, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(run, baselineBytes));
    ASSERT_TRUE(plan.allowApply);
    EXPECT_FALSE(plan.expectedStaleBaseline);
    EXPECT_TRUE(plan.recalcRequired) << "完整复算提示（采用三件套之三——§10.1）";
    ASSERT_TRUE(plan.applyDesign.has_value());
    EXPECT_TRUE(plan.applyDesign->requiresDualCompile);

    // 执行两步（经①端口——ProjectCommandService::submit 全流程）。
    const TwoStepOutcome out = executePlan(plan, mainBranch());
    ASSERT_TRUE(out.step1.committed()) << "step1 建支必须提交（元数据修订）";
    ASSERT_TRUE(out.step2.committed()) << "step2 应用必须提交（域修订）";
    ASSERT_TRUE(out.step2.newRevision.has_value());

    // OPT-VER-145（半区一）：分支表记录 baseRevisionId——新分支 base＝
    // 运行输入修订（建支基）；建支后 tip 一次写入同值（§4.5.1 走查 1/3
    // ——step1 后快照由 executePlan 捕获，此处逐字段核对）。
    EXPECT_EQ(out.branchAfterCreate.base, run.revision);
    EXPECT_EQ(out.branchAfterCreate.tip, run.revision)
        << "建支不产生设计变更——tip＝基（step1 后观测窗）";
    EXPECT_FALSE(out.branchAfterCreate.label.empty());

    // OPT-VER-145（半区二）：不复制对象——建支是纯元数据命令（分支表随
    // ProjectMetadata 提交，§10.2）：其修订引用集**零设计对象写入**（无
    // RobotDesign token 的引用条目——设计对象库字节面零新增，仅元数据
    // 对象前进），基线根对象的 (oid, cv) 引用保持指向既有对象。
    const project::RevisionView metaRev
        = opened->store->query().revision(*out.step1.newRevision);
    for (const project::ObjectRef& ref : metaRev.objectRefs) {
        EXPECT_NE(ref.objectTypeToken, "RobotDesign")
            << "建支修订不得写入任何设计对象（不复制对象——OPT-VER-145）";
    }
    // 元数据对象前进：建支修订携带有效的元数据引用（分支表增量——
    // ProjectMetadata 新版本随修订发布）。
    EXPECT_TRUE(metaRev.metadataRef.objectId.isValid());

    // OPT-VER-143：新修订产生——step2 修订在新分支上、根对象字节替换为
    // 候选字节（引用稳定——oid 不变、cv 内容寻址前进）。
    const project::RevisionView applyRev
        = opened->store->query().revision(*out.step2.newRevision);
    EXPECT_EQ(applyRev.branch, out.newBranch);
    const project::ObjectRef& applyRoot = rootRefOf(applyRev);
    EXPECT_EQ(applyRoot.objectId, root.objectId) << "替换式写入——对象身份稳定";
    const auto appliedBytes = opened->store->query().tryObject(
        applyRoot.objectId, applyRoot.contentVersion);
    ASSERT_TRUE(appliedBytes.has_value());
    EXPECT_EQ(*appliedBytes, candidateBytes);

    // 双编译编排随 requiresDualCompile=true 触发（MDL-06——project S5）。
    EXPECT_GE(compilePort.calls, 1)
        << "step2 声明双编译——命令服务必须编排编译端口（§6.6）";

    // 修订只增（PA-2）：step1＋step2 各恰一新修订。
    EXPECT_EQ(revisionDirCount(), revisionsBefore + 2U);

    // 新分支 tip 前进到应用修订（分支表随 step2 元数据更新——INV-M3）。
    for (const project::BranchTip& tip : opened->store->query().branchTips()) {
        if (tip.id == out.newBranch) {
            EXPECT_EQ(tip.tip, *out.step2.newRevision);
        }
    }
}

// =====================================================================
// OPT-VER-144：基线对象字节不变（AT-12 基线保护）
// =====================================================================

TEST_F(ApplierContractFixture, BaselineRevisionBytesImmutable_WP20T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "MDL-08"}, std::vector<std::string>{"AT-12"});

    const project::RevisionView base = seedBaseline();
    const project::ObjectRef& root = rootRefOf(base);
    const OptimizationRunResult run = makeRun(base);
    const project::RevisionView baseViewBefore
        = opened->store->query().revision(base.id);

    const OptimizationCandidateApplier applier({&materializer, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(run, baselineBytes));
    const TwoStepOutcome out = executePlan(plan, mainBranch());
    ASSERT_TRUE(out.step1.committed());
    ASSERT_TRUE(out.step2.committed());

    // 观测点一（修订视图不可变——§4.6）：基线修订视图逐字段与提交前相等
    // （引用集/元数据引用/摘要——历史只增不改的直接表达）。
    const project::RevisionView baseViewAfter
        = opened->store->query().revision(base.id);
    EXPECT_EQ(baseViewAfter, baseViewBefore);

    // 观测点二（对象库字节面）：基线根对象（oid0/cv0）字节与 step0 写入
    // 一致——内容寻址版本不变即字节不变（cv 是字节 SHA-256——cv0 仍可
    // 读出且等于基线字节＝内容未动）。
    const auto baselineBytesNow
        = opened->store->query().tryObject(root.objectId, root.contentVersion);
    ASSERT_TRUE(baselineBytesNow.has_value());
    EXPECT_EQ(*baselineBytesNow, baselineBytes);

    // 观测点三（主分支不受影响）：主分支 tip 仍为基线修订（应用发生在
    // 方案分支——PA-1 权威主分支字节面不变）。
    for (const project::BranchTip& tip : opened->store->query().branchTips()) {
        if (tip.id == mainBranch()) {
            EXPECT_EQ(tip.tip, base.id);
        }
    }
}

// =====================================================================
// OPT-08 组装面：预览候选不修改基线（执行面证据）
// =====================================================================

TEST_F(ApplierContractFixture, AssembleAndPreviewDoNotTouchStore_WP20T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08"}, std::vector<std::string>{"AT-12"});

    const project::RevisionView base = seedBaseline();
    const OptimizationRunResult run = makeRun(base);
    const project::RevisionView headBefore = opened->store->query().head();
    const project::RevisionView baseViewBefore
        = opened->store->query().revision(base.id);
    const std::size_t revisionsBefore = revisionDirCount();

    // 组装＋预览（buildApplyPlan——含差异预览缝调用）。
    const OptimizationCandidateApplier applier({&materializer, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(run, baselineBytes));
    ASSERT_TRUE(plan.allowApply);

    // 组装后项目态零变化：HEAD 不动、基线修订视图相等、修订目录计数
    // 不变——"预览候选不得修改基线"（OPT-08）在真实存储上的执行面证据
    // （模型面纯函数性归 test/ApplierTest）。
    EXPECT_EQ(opened->store->query().head().id, headBefore.id);
    EXPECT_EQ(opened->store->query().revision(base.id), baseViewBefore);
    EXPECT_EQ(revisionDirCount(), revisionsBefore);
}

// =====================================================================
// OPT-VER-146：过期基线——PRJ-STALE-REVISION-REJECTED 且候选保留
// =====================================================================

TEST_F(ApplierContractFixture, StaleRevisionRejectedCandidatePreserved_WP20T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-04"}, std::vector<std::string>{"AT-12"});

    const project::RevisionView base = seedBaseline();
    const OptimizationRunResult run = makeRun(base);

    const OptimizationCandidateApplier applier({&materializer, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(run, baselineBytes));
    const TwoStepOutcome out = executePlan(plan, mainBranch());
    ASSERT_TRUE(out.step1.committed());
    ASSERT_TRUE(out.step2.committed());
    const core::RevisionId appliedRev = *out.step2.newRevision;

    // 制造并发前进：新分支上再提交一次应用（tip 前进——执行编排组装
    // 时点之后基线被推进的场景还原）。
    CommandEnvelope env;
    env.branch = out.newBranch;
    env.commandType = "apply-robot-design";
    env.payloadFormatVersion = 1;
    env.payloadCanonical = candidateBytes;
    const CommandResult advance = opened->store->commands().submit(env);
    ASSERT_TRUE(advance.committed());

    // 重试应用（用户重试路径——同一候选重新组装；expectedRevision 取
    // **过期的** rev——模拟另一会话持旧视图提交）：
    //   提交期 S2 失配 ⇒ Rejected(stale-revision)＋PRJ-STALE-REVISION-
    //   REJECTED 诊断；零修订落盘。
    // 注意 branchTips() 返回临时向量——分支快照按**值**拷贝消费（指针
    // 即悬空，project 查询端口值语义纪律）。
    std::optional<project::BranchTip> staleTip;
    for (const project::BranchTip& tip : opened->store->query().branchTips()) {
        if (tip.id == out.newBranch) {
            staleTip = tip;
        }
    }
    ASSERT_TRUE(staleTip.has_value());
    ASSERT_TRUE(staleTip->tip == advance.newRevision) << "前置：tip 已前进";

    CommandEnvelope staleEnv;
    staleEnv.branch = out.newBranch;
    staleEnv.commandType = plan.applyDesign->commandType;
    staleEnv.payloadFormatVersion = 1;
    staleEnv.expectedRevision = appliedRev;  // 旧视图——已不是 tip
    staleEnv.payloadCanonical = plan.applyDesign->payloadCanonical;
    const CommandResult rejected = opened->store->commands().submit(staleEnv);

    ASSERT_TRUE(rejected.rejected());
    EXPECT_EQ(rejected.status.rejection,
              project::CommandStatus::Rejection::StaleRevision);
    EXPECT_FALSE(rejected.newRevision.has_value()) << "拒绝零修订（§5.3.1）";
    // 稳定码钉扎（PRJ-STALE-REVISION-REJECTED——project §6.2 双通道）。
    bool hasStaleCode = false;
    for (const core::DiagnosticRecord& d : rejected.diagnostics) {
        if (d.code == "PRJ-STALE-REVISION-REJECTED") {
            hasStaleCode = true;
        }
    }
    EXPECT_TRUE(hasStaleCode) << "过期基线必须携带稳定码诊断（PM-04）";

    // 候选保留（OPT-VER-146 观测点）：OptimizationRunResult 是值聚合——
    // 提交拒绝不改写运行结果；重新组装仍得完整计划，且组装面**如实标记
    // 预期拒绝**（§10.3 第 4 项：当前分支 tip ≠ 运行输入修订 ⇒
    // expectedStaleBaseline=true——不阻塞组装但预先告知；计划语义恒定
    // 锚定运行输入修订——运行绑定原修订，I-OPT-1，不随 tip 漂移）。
    const OptimizationCandidateApplier retryApplier({&materializer, nullptr});
    ApplyCandidateRequest retry = makeRequest(run, baselineBytes);
    retry.baseline.branch = out.newBranch;
    retry.baseline.tip = staleTip->tip;  // 当前权威 tip（重试方刷新投影）
    const CandidateApplyPlan retryPlan = retryApplier.buildApplyPlan(retry);
    ASSERT_TRUE(retryPlan.allowApply)
        << "过期标记不阻塞组装（候选保留可再次组装——分支残留处置＝保留）";
    EXPECT_TRUE(retryPlan.expectedStaleBaseline)
        << "当前 tip ≠ 运行输入修订——预期拒绝必须如实标记（§10.3 第 4 项）";
    ASSERT_TRUE(retryPlan.applyDesign.has_value());
    EXPECT_EQ(retryPlan.applyDesign->expectedRevision, run.revision)
        << "计划语义恒定：expectedRevision＝运行输入修订（不随 tip 漂移——"
           "过期拒绝由提交期 S2 兜底；候选与运行绑定原修订，I-OPT-1）";
}

// =====================================================================
// OPT-VER-147：撤销/重做由 project 接管（零自建栈）
// =====================================================================

TEST_F(ApplierContractFixture, UndoRedoOwnedByProject_WP20T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-08", "PM-18"}, std::vector<std::string>{"AT-12"});

    const project::RevisionView base = seedBaseline();
    const project::ObjectRef& root = rootRefOf(base);
    const OptimizationRunResult run = makeRun(base);
    const project::RevisionView appliedViewBefore = base;  // 历史不改写对照面

    const OptimizationCandidateApplier applier({&materializer, nullptr});
    const CandidateApplyPlan plan = applier.buildApplyPlan(makeRequest(run, baselineBytes));
    const TwoStepOutcome out = executePlan(plan, mainBranch());
    ASSERT_TRUE(out.step1.committed());
    ASSERT_TRUE(out.step2.committed());
    const core::RevisionId appliedRev = *out.step2.newRevision;

    // 撤销/重做经 project UndoRedoService（optimization 零自建栈——本
    // 用例全程只调 project 端口；optimization API 面无任何 undo/redo
    // 符号，BuildRedLineTest/词表扫描另有钉扎）。
    project::UndoRedoService& undoRedo = opened->store->undoRedo();
    const project::UndoRedoStatus st = undoRedo.status(out.newBranch);
    EXPECT_TRUE(st.canUndo) << "应用修订携带 inverse——可撤销（PM-18）";
    ASSERT_TRUE(st.undoSummary.has_value());

    // 撤销＝逆命令提交产生新修订（根对象字节还原为基线字节）。
    const CommandResult undone = undoRedo.undo(out.newBranch);
    ASSERT_TRUE(undone.committed());
    ASSERT_TRUE(undone.newRevision.has_value());
    EXPECT_NE(*undone.newRevision, appliedRev) << "撤销是新修订——非历史改写";
    {
        const project::RevisionView rev
            = opened->store->query().revision(*undone.newRevision);
        const project::ObjectRef& r = rootRefOf(rev);
        const auto bytes = opened->store->query().tryObject(r.objectId, r.contentVersion);
        ASSERT_TRUE(bytes.has_value());
        EXPECT_EQ(*bytes, baselineBytes) << "撤销后根对象字节＝基线字节（逆放）";
    }

    // 历史不改写（PA-2）：应用修订视图与撤销前逐字段相等。
    {
        const project::RevisionView appliedNow
            = opened->store->query().revision(appliedRev);
        EXPECT_EQ(appliedNow.branch, out.newBranch);
        EXPECT_EQ(appliedNow.commandSummary, "应用候选设计（测试替身）");
    }
    // 基线修订对照面仍不变。
    EXPECT_EQ(opened->store->query().revision(base.id), appliedViewBefore);

    // 重做＝重放被撤销命令原始载荷（新修订——字节回到候选字节）。
    const project::UndoRedoStatus stAfterUndo = undoRedo.status(out.newBranch);
    EXPECT_TRUE(stAfterUndo.canRedo) << "撤销后 redo 栈在册（会话内——D-11）";
    const CommandResult redone = undoRedo.redo(out.newBranch);
    ASSERT_TRUE(redone.committed());
    ASSERT_TRUE(redone.newRevision.has_value());
    EXPECT_NE(*redone.newRevision, appliedRev) << "重做产生新修订（非复现旧身份）";
    {
        const project::RevisionView rev
            = opened->store->query().revision(*redone.newRevision);
        const project::ObjectRef& r = rootRefOf(rev);
        const auto bytes = opened->store->query().tryObject(r.objectId, r.contentVersion);
        ASSERT_TRUE(bytes.has_value());
        EXPECT_EQ(*bytes, candidateBytes) << "重做后根对象字节＝候选字节";
    }
}

}  // namespace
