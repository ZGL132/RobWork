/**
 * @file   ComparisonContractTest.cpp
 * @brief  方案比较视图的契约测试（WF-VER-224/225——units/workflow.md
 *         §11.2 生命周期主线；与真实 project store 联合的编排契约面）。
 *
 * 设计依据：
 *   - units/workflow.md §8.1/§8.2（方案选择 2~4〔分支清单与 baseRevisionId
 *     来自 project ProjectMetadata 查询〕；基准一致性检查前置——不一致
 *     拒绝＋原因提示；八项指标取数为只读投影；Model Diff 呈现分组＋点击
 *     定位锚）、§11.2（WF-VER-224 观测点＝八项指标差异高亮＋diff 分组
 *     呈现＋点击定位〔经名称端口——呈现面归 ui 宿主，本契约承载编排
 *     数据面〕；WF-VER-225 观测点＝拒绝＋原因提示〔不产出混基准比较〕）、
 *     §11.3（AT-12 承接行——方案比较呈现侧）
 *   - REQUIREMENTS.md §18 UX-13 原文、§15.0 OPT-07（不可算项显示"—"）、
 *     EVI-02/RPT-04（一致基准）、MDL-08（数据实体归 modeling——V15-02
 *     M-5 分工）、P-OPT-8（范围外给警告不虚构差异）
 *   - project.md §5.2（branchTips 查询面——INV-M3 权威元数据；label 一次
 *     写入 P-PR-8）、§5.3.2（CommandPlan.metadataChange.createBranchWithBase
 *     建支声明面）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：
 *   - **真实 project store**：ProjectStoreFactory::createNew 真实落盘＋
 *     建支替身经真实命令服务提交（CommandPlan.metadataChange 声明面——
 *     ApplierContractTest 同款先例；替身 token "test-create-branch" 无点
 *     词形，仅测试注册）——方案分支的**存在性与 label 是真实 project
 *     事实**（branchTips() 权威元数据取数，INV-M3）。
 *   - **指标/diff 端口桥＝L5 装配桥的测试等价物**（诚实登记，同
 *     optimization T07 契约测试替身口径）：①指标桥的 label 半区真实
 *     （经 store 查询面现取），指标列/基准/当前性半区为测试预置的
 *     "各域归档结果投影等价物"——真实各域归档投影随阶段 C 各域任务
 *     落位（workflow R-1 禁链业务域，投影面经端口进入是唯一通道）；
 *     ②diff 桥为 modeling IModelDiffService 桥的测试等价物（workflow
 *     测试目标零 modeling 链接边——T 规则"测试目标仅允许同单元产品
 *     目标"），diff 事实由测试预置、编排面透传契约是本契约会话的被测面。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/project/CommandService.hpp>   // HandlerRegistry/CommandEnvelope/submit
#include <sdurws/ird/project/ProjectStore.hpp>     // ProjectStoreFactory/OpenStoreResult
#include <sdurws/ird/project/QueryPort.hpp>        // IProjectQueryPort（branchTips）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Comparison.hpp>
#include <sdurws/ird/workflow/Types.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::ComparisonOutcome;
using workflow::IComparisonDiffPort;
using workflow::ISchemeComparisonController;
using workflow::ISchemeMetricPort;
using workflow::SchemeDiffFacts;
using workflow::SchemeMetricFacts;
using workflow::WorkflowError;
using core::BranchId;
using project::CommandEnvelope;
using project::CommandResult;
using project::HandlerRegistry;
using project::ICommandHandler;
using project::PrepareOutcome;
using project::ProjectStoreFactory;

// =====================================================================
// 建支替身（ApplierContractTest 同款先例——机制真值全在 project 生产
// 代码：CommandPlan.metadataChange 声明面；替身 token 仅测试注册）
// =====================================================================

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

// =====================================================================
// 端口桥（L5 装配桥的测试等价物——文件头口径登记）
// =====================================================================

/**
 * @brief 指标端口桥：label 半区**真实**（store->query().branchTips() 现取
 *        ——INV-M3 权威元数据），指标列/基准/当前性半区为测试预置的
 *        "各域归档结果投影等价物"（按 branch 查表；未预置＝分支无归档
 *        结果→available=false）。
 */
class ContractMetricBridge final : public ISchemeMetricPort {
public:
    /// 预置投影源（branch → 指标列素材——归档结果投影等价物）。
    struct Projection {
        std::vector<SchemeMetricFacts::Metric> metrics;
        evidence::ComparisonBaseline baseline;
        ui::CurrentnessProjection currentness;
    };
    std::map<std::string, Projection> byBranch;  // 键＝branch 规范文本

    /// project 查询面（存活期覆盖本桥——构造注入，非 owning）。
    project::IProjectQueryPort* query = nullptr;

    std::vector<SchemeMetricFacts> collect(
        const std::vector<BranchId>& schemes) const override
    {
        std::vector<SchemeMetricFacts> out;
        out.reserve(schemes.size());
        for (const BranchId& b : schemes) {
            SchemeMetricFacts f;
            f.branch = b;
            // ---- label 半区：真实 store 取数（分支不存在→available=false
            // ——编排核 fail-fast 的判定材料由真实 project 事实承载）。
            bool branchReal = false;
            if (query != nullptr) {
                for (const project::BranchTip& tip : query->branchTips()) {
                    if (tip.id == b) {
                        f.label = tip.label;
                        branchReal = true;
                        break;
                    }
                }
            }
            // ---- 指标/基准/当前性半区：预置投影等价物（未预置＝无归档
            // 结果——同 available=false；与分支不存在同一失败面）。
            const auto it = byBranch.find(b.toCanonical());
            if (!branchReal || it == byBranch.end()) {
                f.available = false;
                out.push_back(std::move(f));
                continue;
            }
            f.available = true;
            f.metrics = it->second.metrics;
            f.baseline = it->second.baseline;
            f.currentness = it->second.currentness;
            out.push_back(std::move(f));
        }
        return out;
    }
};

/**
 * @brief diff 端口桥：modeling IModelDiffService 桥的测试等价物（预置
 *        diff 事实按 (baseline, candidate) 查表；未预置＝通道未装配→
 *        available=false——降级警告契约面的判定材料）。
 */
class ContractDiffBridge final : public IComparisonDiffPort {
public:
    std::map<std::string, SchemeDiffFacts> byPair;  // 键＝"base|cand" 规范文本
    /// 触达计数（"拒绝先于 diff 取数"契约的观测点——编排核在拒绝分支
    /// 不得触达 diff 端口）。
    mutable int calls = 0;

    SchemeDiffFacts diff(const BranchId& baseline,
                         const BranchId& candidate) const override
    {
        ++calls;
        const auto it = byPair.find(baseline.toCanonical() + "|"
                                    + candidate.toCanonical());
        if (it == byPair.end()) {
            return {};   // available=false——通道未装配等价物
        }
        return it->second;
    }
};

// =====================================================================
// 共享夹具（真实 project store——createNew 临时目录＋建支替身装配）
// =====================================================================

class ComparisonContract : public ::testing::Test {
public:
    std::filesystem::path dir;
    std::unique_ptr<project::OpenStoreResult> opened;
    ContractMetricBridge metricBridge;
    ContractDiffBridge diffBridge;
    std::unique_ptr<ISchemeComparisonController> controller;

    void SetUp() override
    {
        static std::atomic<unsigned long long> seq{0};
        dir = std::filesystem::temp_directory_path()
            / ("ird-wf-t11-"
               + std::to_string(seq.fetch_add(1)) + "-"
               + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        opened = std::make_unique<project::OpenStoreResult>(
            ProjectStoreFactory::createNew(dir, "wf t11 contract", nullptr,
                                           nullptr));

        // 建支替身装配期注册（真实命令服务编排 S1~S7——机制真值全在
        // project 生产代码；替身只声明元数据变更）。
        project::HandlerRegistry& registry = opened->store->handlerRegistry();
        registry.registerHandler(std::make_unique<CreateBranchStubHandler>());

        // 端口桥接线（label 半区经真实 store 查询面）。
        metricBridge.query = &opened->store->query();

        // 控制器经接口引用装配（L5 形态——产品面只见接口）。
        controller = std::make_unique<workflow::SchemeComparisonController>(
            metricBridge, diffBridge);
    }

    void TearDown() override
    {
        controller.reset();
        opened.reset();  // 析构释放锁句柄（先于目录清理——project 先例同序）
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    /// 主分支身份（createNew 初始分支——label＝"main"，P-PR-8 一次写入）。
    [[nodiscard]] core::BranchId mainBranch() const
    {
        return opened->store->query().currentMetadata().record.primaryBranchId;
    }

    /// 经真实命令服务建支（label 一次写入——P-PR-8；返回新分支身份——
    /// branchTips 增量定位，ApplierContractTest 同款先例）。
    [[nodiscard]] core::BranchId createSchemeBranch(const std::string& label)
    {
        CommandEnvelope env;
        env.branch = mainBranch();
        env.commandType = "test-create-branch";
        env.payloadFormatVersion = 1;
        env.payloadCanonical.assign(label.begin(), label.end());
        const CommandResult r = opened->store->commands().submit(env);
        EXPECT_TRUE(r.committed()) << "建支必须成功（前置事实）";
        for (const project::BranchTip& tip : opened->store->query().branchTips()) {
            if (tip.label == label) {
                return tip.id;
            }
        }
        ADD_FAILURE() << "建支后 branchTips 未找到新分支（label=" << label << "）";
        return core::BranchId{};
    }

    /// 预置一份归档结果投影等价物（指标三列＋基准＋当前性）。
    void primeProjection(const BranchId& b, const std::string& baselineCidHex)
    {
        ContractMetricBridge::Projection p;
        // 三指标列（首列三方案差异素材、中列同值、末列不可算——OPT-07
        // "—"的契约面素材；列头键形＝对端词表投影）。
        p.metrics.push_back(makeMetric("m-envelope", 1.0));
        p.metrics.push_back(makeMetric("m-mass", 42.0));
        p.metrics.push_back(makeMetric("m-cycle", std::nullopt));
        p.baseline.inputBaselineId = core::ContentIdentity::fromCanonical(
            "cid-" + baselineCidHex);
        byBranchBaselines[b.toCanonical()] = baselineCidHex;
        metricBridge.byBranch[b.toCanonical()] = std::move(p);
    }

    /// 预置投影的基准身份（第二个方案注入异基准时改写）。
    std::map<std::string, std::string> byBranchBaselines;

    /// 指标格构造（对端词表投影词形）。
    static SchemeMetricFacts::Metric makeMetric(const std::string& key,
                                                std::optional<double> v)
    {
        SchemeMetricFacts::Metric m;
        m.metricKey = key;
        m.labelKey = "metric." + key + ".label";
        m.unitToken = "1";
        m.value = std::move(v);
        m.stale = false;
        return m;
    }

    /// 预置一条 diff 事实（结构组条目＋可选 P-OPT-8 警告）。
    void primeDiff(const BranchId& base, const BranchId& cand,
                   const std::vector<std::string>& warnings)
    {
        workflow::SchemeDiffEntry e;
        e.group = "structure";
        e.kind = "modified";
        e.objectId = core::ObjectId::fromCanonical(
            "obj-00000000000000000000000000000be1");
        e.subjectPath = "root.displayName";
        e.field = "displayName";
        e.valueChanged = true;
        e.baselineText = "wf t11 contract";
        e.candidateText = "wf t11 contract（候选）";
        SchemeDiffFacts f;
        f.available = true;
        f.entries = {e};
        f.warnings = warnings;
        diffBridge.byPair[base.toCanonical() + "|" + cand.toCanonical()] = f;
    }
};

// =====================================================================
// WF-VER-224：方案比较与差异定位（真实 project 分支联合契约）
// =====================================================================

/**
 * @brief WF-VER-224：2~4 方案比较真实编排（AT-12 呈现侧的编排数据面——
 *        真实建支方案分支＋真实 branchTips label＋八项指标高亮行＋diff
 *        分组块＋P-OPT-8 警告透传）。
 */
TEST_F(ComparisonContract, Compare_RealSchemeBranches_WF_VER_224)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "MDL-08"},
                  std::vector<std::string>{"WF-VER-224", "AT-12"});

    // ---- 前置（真实 project 事实）：主分支＋两个方案分支（真实建支——
    // label 经真实命令服务一次写入 P-PR-8）。
    const BranchId main = mainBranch();
    const BranchId schemeB = createSchemeBranch("方案乙");
    const BranchId schemeC = createSchemeBranch("方案丙");
    ASSERT_TRUE(schemeB.isValid());
    ASSERT_TRUE(schemeC.isValid());

    // 归档结果投影等价物预置（同基准——EVI-02 通过面；三指标列：首列
    // 差异、中列同值、末列不可算）。
    const std::string cidHex = std::string(60, '3') + "0100";
    primeProjection(main, cidHex);
    primeProjection(schemeB, cidHex);
    primeProjection(schemeC, cidHex);

    // diff 事实预置（两对：主→乙带 P-OPT-8 警告；主→丙为条目但无警告——
    // 两块都以预置事实逐条断言）。
    primeDiff(main, schemeB, {workflow::kComparisonWarningRatioOutOfScope});
    primeDiff(main, schemeC, {});

    // ---- 经接口引用执行编排（消费路径钉扎——WP-20-T03 教训）。
    ISchemeComparisonController& cmp = *controller;
    const ComparisonOutcome outcome = cmp.buildComparison({main, schemeB, schemeC});

    // ---- 接受态：view 齐备、rejection 无值（互斥契约）。
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    EXPECT_FALSE(outcome.rejection.has_value());
    const workflow::ComparisonViewData& view = *outcome.view;

    // ---- 方案列头＝真实 project label（branchTips 权威元数据取数——
    // 主分支 label "main"〔kPrimaryBranchLabel 词形〕＋建支 label 原文）。
    ASSERT_EQ(view.schemeLabels.size(), 3u);
    EXPECT_EQ(view.schemeLabels[0], "main");
    EXPECT_EQ(view.schemeLabels[1], "方案乙");
    EXPECT_EQ(view.schemeLabels[2], "方案丙");

    // ---- 指标行：3 行全部展示（OPT-07"全部展示"——不可算行不缺席）；
    // 首行高亮（本预置三方案同值——需第二方案差异素材才亮；此处预置
    // 同值→首行不高亮，行存在性与"—"是断言面）。
    ASSERT_EQ(view.metricRows.size(), 3u);
    EXPECT_EQ(view.metricRows[0].metricKey, "m-envelope");
    EXPECT_EQ(view.metricRows[2].metricKey, "m-cycle");
    // 不可算行：value 无值＋displayText＝"—"（OPT-07；'"—'语义 null≠0"）。
    EXPECT_FALSE(view.metricRows[2].cells[0].value.has_value());
    EXPECT_EQ(view.metricRows[2].cells[0].displayText,
              workflow::kMetricUnavailableDisplay);
    // 同值行不高亮（零容差精确相等）。
    EXPECT_FALSE(view.metricRows[0].differs);

    // ---- diff 块：n-1＝2 块；基线＝首方案（主分支）；乙块条目＋P-OPT-8
    // 警告透传；丙块按预置事实逐条断言（1 条结构组条目、零警告）。
    ASSERT_EQ(view.diffs.size(), 2u);
    EXPECT_TRUE(view.diffs[0].baseline == main);
    EXPECT_TRUE(view.diffs[0].candidate == schemeB);
    ASSERT_EQ(view.diffs[0].structure.size(), 1u);
    // 点击定位锚可达（V15-02——objectId 强类型直通 ui 名称端口消费面）。
    EXPECT_TRUE(view.diffs[0].structure[0].objectId.isValid());
    ASSERT_EQ(view.diffs[0].warnings.size(), 1u);
    EXPECT_EQ(view.diffs[0].warnings[0], "ratio-diff-out-of-scope");
    EXPECT_TRUE(view.diffs[1].candidate == schemeC);
    ASSERT_EQ(view.diffs[1].structure.size(), 1u);
    EXPECT_TRUE(view.diffs[1].warnings.empty());

    // ---- 真实落盘复核：建支产生修订（项目历史前进——建支是真实命令
    // 事实，非测试内虚构分支）。
    EXPECT_TRUE(opened->store->query().currentMetadata().record.branches.size() >= 3u);
}

/**
 * @brief WF-VER-224 差异高亮面（首行差异素材——第二方案指标值不同→
 *        高亮位 true；同值行不高亮——零容差）。
 */
TEST_F(ComparisonContract, HighlightDiffersOnValueDelta_WF_VER_224)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    const BranchId main = mainBranch();
    const BranchId schemeB = createSchemeBranch("方案乙");
    const std::string cidHex = std::string(60, '4') + "0200";
    primeProjection(main, cidHex);
    primeProjection(schemeB, cidHex);
    primeDiff(main, schemeB, {});

    // 注入差异值（第二方案首指标 7.25≠1.0——高亮素材）。
    metricBridge.byBranch[schemeB.toCanonical()].metrics[0].value = 7.25;

    const ComparisonOutcome outcome = controller->buildComparison({main, schemeB});
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    ASSERT_EQ(outcome.view->metricRows.size(), 3u);
    // 首行：值不同→高亮 true＋display 文本渲染（"1" vs "7.25"）。
    EXPECT_TRUE(outcome.view->metricRows[0].differs);
    EXPECT_EQ(outcome.view->metricRows[0].cells[0].displayText, "1");
    EXPECT_EQ(outcome.view->metricRows[0].cells[1].displayText, "7.25");
    // 中行：同值 42.0→不高亮。
    EXPECT_FALSE(outcome.view->metricRows[1].differs);
}

// =====================================================================
// WF-VER-225：比较基准不一致拒绝（EVI-02/RPT-04——不产出混基准比较）
// =====================================================================

/**
 * @brief WF-VER-225：混基准拒绝契约（一方案归档结果的 inputBaselineId
 *        不同→拒绝＋原因键＋维度明细键；diff 端口零触达——不产出任何
 *        混基准比较数据；检查经 evidence 公共契约）。
 */
TEST_F(ComparisonContract, RejectsInconsistentBaseline_WF_VER_225)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-02", "RPT-04"},
                  std::vector<std::string>{"WF-VER-225"});

    const BranchId main = mainBranch();
    const BranchId schemeB = createSchemeBranch("方案乙");
    const std::string cidA = std::string(60, '5') + "0300";
    const std::string cidB = std::string(60, '6') + "0300";
    primeProjection(main, cidA);
    primeProjection(schemeB, cidB);   // 异基准（模型/需求/工况集基准不同）
    primeDiff(main, schemeB, {});     // 若被错误触达即契约破坏（断言零调用）

    const ComparisonOutcome outcome = controller->buildComparison({main, schemeB});

    // 拒绝态：不产出比较数据（互斥契约——view 无值）。
    ASSERT_FALSE(outcome.accepted);
    EXPECT_FALSE(outcome.view.has_value());
    ASSERT_TRUE(outcome.rejection.has_value());

    // 原因三字段（UX-03）：主原因键＋建议动作键＋差异维度明细键——
    // 维度＝evidence 检查产出透传（InputBaseline 命中）。
    EXPECT_EQ(outcome.rejection->reasonKey, workflow::kComparisonBaselineMismatchKey);
    EXPECT_EQ(outcome.rejection->actionKey,
              workflow::kComparisonBaselineMismatchActionKey);
    ASSERT_EQ(outcome.rejection->dimensions.size(), 1u);
    EXPECT_EQ(outcome.rejection->dimensions[0],
              evidence::BaselineDifferenceDimension::InputBaseline);
    ASSERT_EQ(outcome.rejection->detailKeys.size(), 1u);
    EXPECT_EQ(outcome.rejection->detailKeys[0],
              "comparison.baseline-mismatch.dim.input-baseline");

    // diff 端口零触达（拒绝先于 diff 取数——不产出任何 diff 块；
    // 触达计数由端口桥观测）。
    EXPECT_EQ(diffBridge.calls, 0);
}

/**
 * @brief 分支不可用 fail-fast 契约（@pre"方案分支存在且可读"——端口桥对
 *        不存在分支返回 available=false〔真实 branchTips 无命中〕→编排核
 *        WorkflowError；对端稳定呈现由宿主面负责，编排面零半成品）。
 */
TEST_F(ComparisonContract, UnknownBranchFailsFast_PresentTense)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    const BranchId main = mainBranch();
    // 未建支也未预置投影的陌生分支（brn- 规范词形——真实不存在的身份）。
    const BranchId ghost = core::BranchId::fromCanonical(
        "brn-0000000000000000000000000000f000");
    const std::string cidHex = std::string(60, '7') + "0400";
    primeProjection(main, cidHex);

    // 调用方错误（分支不存在）→ fail-fast（不产出缺列比较数据）。
    EXPECT_THROW(controller->buildComparison({main, ghost}), WorkflowError);
}

/**
 * @brief P-OPT-8 契约面（范围外给警告不虚构差异——警告透传＋条目集零
 *        补造的双面复核；diff 通道未装配＝降级警告）。
 */
TEST_F(ComparisonContract, Popt8WarningPassThroughAndDegradedChannel)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-224"});

    const BranchId main = mainBranch();
    const BranchId schemeB = createSchemeBranch("方案乙");
    const std::string cidHex = std::string(60, '8') + "0500";
    primeProjection(main, cidHex);
    primeProjection(schemeB, cidHex);
    primeDiff(main, schemeB, {workflow::kComparisonWarningRatioOutOfScope});

    // ---- 面 1：警告透传（条目仍来自端口产出——零补造）。
    const ComparisonOutcome outcome = controller->buildComparison({main, schemeB});
    ASSERT_TRUE(outcome.accepted);
    ASSERT_TRUE(outcome.view.has_value());
    ASSERT_EQ(outcome.view->diffs.size(), 1u);
    ASSERT_EQ(outcome.view->diffs[0].warnings.size(), 1u);
    EXPECT_EQ(outcome.view->diffs[0].warnings[0], "ratio-diff-out-of-scope");
    ASSERT_EQ(outcome.view->diffs[0].structure.size(), 1u);  // 端口条目原样
    EXPECT_TRUE(outcome.view->diffs[0].parameters.empty());  // 无补造条目

    // ---- 面 2：通道未装配（未预置 diff 事实的另一对）→降级警告＋空条目
    // （不虚构差异——诚实呈现"差异面可能不全"）。
    const BranchId schemeC = createSchemeBranch("方案丙");
    primeProjection(schemeC, cidHex);
    // 主→丙不预置 diff 事实（available=false）。
    const ComparisonOutcome outcome2 =
        controller->buildComparison({main, schemeC});
    ASSERT_TRUE(outcome2.accepted);
    ASSERT_TRUE(outcome2.view.has_value());
    ASSERT_EQ(outcome2.view->diffs.size(), 1u);
    EXPECT_TRUE(outcome2.view->diffs[0].structure.empty());
    ASSERT_EQ(outcome2.view->diffs[0].warnings.size(), 1u);
    EXPECT_EQ(outcome2.view->diffs[0].warnings[0],
              workflow::kComparisonWarningDiffSourceUnavailable);
}

}  // namespace
