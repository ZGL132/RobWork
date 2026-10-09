/**
 * @file   GateAdviceFixtureContractTest.cpp
 * @brief  workflow 门控/建议/级联映射纯函数面的黄金夹具契约测试
 *         （WfGateAdviceFixture——units/workflow.md §11.0"门控/建议用固定
 *         投影夹具（contract-fixture 类——golden wf-*，随 WP-22-T13 登记）"
 *         的消费面＋任务契约 WP-22-T13 acceptance 2"门控/建议/映射纯函数
 *         面固定投影夹具（contract-fixture）确定性重放用例通过"的执行面）。
 *
 * 设计依据：
 *   - units/workflow.md §11.0（黄金数据集需求——contract-fixture 类 golden
 *     wf-* 随本任务登记；确定性重放）、§11.1（WF-VER-107 确定性重放——
 *     同夹具双跑门控/建议输出一致的黄金数据承载面）、§4.3/§4.4/§5.2/§6.3
 *     （判定语义——独立生成器与产品实现共同的对表权威）
 *   - 需求 UX-12（七阶段解锁/锁定＋级联提示）、UX-01（四要素）、UX-02
 *     （工程用语键）、NFR-COR-02（同输入同输出——确定性重放）
 *   - 任务契约 tasks/foundation/WP-22-T13.json acceptance 2（固定投影夹具
 *     确定性重放用例通过）；requirements UX-12/UX-01/NFR-COR-02（数据集
 *     manifest 登记面）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"；selection
 * SelGoldenDatasetContractTest 同款数据资产登记契约形态）：
 *   - **真实 testkit 装载器**：GoldenDataset::load/ToleranceProfile::load
 *     不桩化——schema→完整性（SHA-256/size 逐文件）→交叉校验全链真实执行
 *     （数据缺陷按 testkit §7.2 DatasetInvalid 暴露，不伪装算法回归）；
 *   - **双实现互证**：黄金期望由 generate/make_wf_gate_advice_fixture.mjs
 *     独立参考实现按单元卡冻结语义直写（与产品 Gate.cpp/Advice.cpp 零共享
 *     代码）——本文件把夹具输入经 workflow 公共接口（PureStageGateService::
 *     evaluate／PureNextStepAdvisor::adviseFor／IStageGateService::
 *     mapInvalidation）重放后与黄金逐字段对照：任一侧漂移即显性失败；
 *   - **接口消费路径**：门控/建议经 IStageGateService&／INextStepAdvisor&
 *     抽象基类引用虚派发消费（WP-20-T03 首轮接口盲区教训的常设对正面——
 *     黄金重放不留只测自由函数的盲区）；
 *   - **确定性重放**：同算例双跑以值相等（operator==）全字段复核——
 *     WF-VER-107 同型语义的契约承载（模型半区已钉，本组钉黄金数据通道）。
 *
 * 线程约束：gtest 用例天然串行；workflow 纯函数面 const 只读（D-WF-3）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/Dataset.hpp>            // GoldenDataset（装载全链真实执行）
#include <sdurws/ird/testkit/JsonLite.hpp>           // 夹具 JSON 解析（testkit §4.1 设施）
#include <sdurws/ird/testkit/TestPaths.hpp>          // goldenDataRoot（数据根唯一入口）
#include <sdurws/ird/testkit/ToleranceProfile.hpp>   // ToleranceProfile（档案通道）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp> // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/core/Events.hpp>            // DomainEvent 工厂（mapInvalidation 输入）
#include <sdurws/ird/core/Identity.hpp>          // Id128（载荷身份夹具值）
#include <sdurws/ird/evidence/Currentness.hpp>   // InvalidationReason（原因结构化词表）
#include <sdurws/ird/ui/IStageNavigationModel.hpp> // stageIdSequence/stageToken（冻结序权威）
#include <sdurws/ird/workflow/Advice.hpp>
#include <sdurws/ird/workflow/Gate.hpp>
#include <sdurws/ird/workflow/Types.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
namespace tk = sdurws::ird::testkit;
using workflow::GateDecision;
using workflow::GateEvent;
using workflow::GateInputs;
using workflow::StageAdvice;
using workflow::StageGatingState;
using workflow::StaleHint;

/// 被测数据集与档案引用（WP-22-T13 登记的 golden wf-*——当前一件）。
constexpr const char* kDatasetId = "wf-gate-advice-fixture";
constexpr const char* kDatasetVersion = "1.0.0";
constexpr const char* kProfileId = "wf-fixture";
constexpr const char* kProfileVersion = "1.0.0";

// =====================================================================
// JSON 词形 → 产品值映射（夹具稳定词形的唯一消费点——输入重组与黄金展开
// 共用同一映射表：两侧词形解释一致，漂移即黄金对照显性失败）。
// =====================================================================

/// 阶段 token → StageId（ui §6.4 冻结序词形；夹具词形错误即测试失败）。
ui::StageId stageFromToken(const std::string& token)
{
    const std::vector<ui::StageId>& sequence = ui::stageIdSequence();
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        if (token == ui::stageToken(sequence[i])) {
            return sequence[i];
        }
    }
    ADD_FAILURE() << "夹具词形错误：未知阶段 token " << token;
    return ui::StageId::Modeling;
}

/// 门控状态 token → ui::StageViewStatus（evaluate 五产出值词形——view-only
/// 不产，P-WF-2 谈判起点词表）。
ui::StageViewStatus statusFromToken(const std::string& token)
{
    if (token == "completed") { return ui::StageViewStatus::Completed; }
    if (token == "in-progress") { return ui::StageViewStatus::InProgress; }
    if (token == "blocked") { return ui::StageViewStatus::Blocked; }
    if (token == "unavailable") { return ui::StageViewStatus::Unavailable; }
    if (token == "not-started") { return ui::StageViewStatus::NotStarted; }
    ADD_FAILURE() << "夹具词形错误：未知门控状态 token " << token;
    return ui::StageViewStatus::NotStarted;
}

/// 事件类别 token → core::DomainEventKind（core §4.9 token 列逐字）。
core::DomainEventKind eventKindFromToken(const std::string& token)
{
    const std::optional<core::DomainEventKind> kind =
        core::domainEventKindFromToken(token);
    EXPECT_TRUE(kind.has_value()) << "夹具词形错误：未知事件类别 token " << token;
    return kind.value_or(core::DomainEventKind::ResultArchived);
}

/**
 * @brief 原因 token → InvalidationReason（evidence InvalidationKind 枚举
 *        注释词形的 kebab-case token；dependencyKey 取自夹具词表登记；
 *        detail 恒空——旧→新差异摘要为呈现层知识，夹具不伪造）。
 *
 * 词形映射为显式表（evidence 公共头无 token 转换函数——映射只存在于测试
 * 夹具词形层，产品面零改动；夹具只登记产品词表内词形，越界词形即失败）。
 */
evidence::InvalidationReason reasonFromToken(const std::string& token,
                                             const tk::JsonValue& vocabulary)
{
    // 词表登记面＝values 数组（{value, key} 条目序）——按 value 词形线性
    // 检索（数组非对象键，find 不适用）。
    const tk::JsonValue* entry = nullptr;
    if (const tk::JsonValue* values = vocabulary.find("values")) {
        for (const tk::JsonValue& item : values->items) {
            if (item.find("value") != nullptr && item.find("value")->text == token) {
                entry = &item;
                break;
            }
        }
    }
    if (entry == nullptr) {
        ADD_FAILURE() << "夹具词形错误：原因 token 未在词表登记 " << token;
        return evidence::InvalidationReason{};
    }
    evidence::InvalidationReason reason;
    reason.dependencyKey = entry->find("key")->text;
    const std::string kindToken = entry->find("value")->text;
    // 词形 → 枚举（evidence InvalidationKind 词表内 token——夹具只承载
    // 词表值，产品词表外词形即夹具缺陷）。
    static const std::map<std::string, evidence::InvalidationKind> kKinds = {
        {"evaluator-contract-changed", evidence::InvalidationKind::EvaluatorContractChanged},
        {"object-content-changed", evidence::InvalidationKind::ObjectContentChanged},
        {"configuration-changed", evidence::InvalidationKind::ConfigurationChanged},
        {"policy-changed", evidence::InvalidationKind::PolicyChanged},
        {"name-map-changed", evidence::InvalidationKind::NameMapChanged},
        {"sample-baseline-changed", evidence::InvalidationKind::SampleBaselineChanged},
        {"condition-flipped", evidence::InvalidationKind::ConditionFlipped},
        {"upstream-result-changed", evidence::InvalidationKind::UpstreamResultChanged},
        {"environment-changed", evidence::InvalidationKind::EnvironmentChanged},
        {"slice-content-changed", evidence::InvalidationKind::SliceContentChanged},
    };
    const auto it = kKinds.find(kindToken);
    if (it == kKinds.end()) {
        ADD_FAILURE() << "夹具词形错误：原因类别不在 evidence 词表 " << kindToken;
    } else {
        reason.kind = it->second;
    }
    reason.detail.clear();
    return reason;
}

/// 结构化原因数组展开（token 词形数组 → InvalidationReason 向量——顺序
/// 即透传序，门控零加工 D-WF-2）。
std::vector<evidence::InvalidationReason> expandReasons(
    const tk::JsonValue& tokens, const tk::JsonValue& vocabulary)
{
    std::vector<evidence::InvalidationReason> reasons;
    for (const tk::JsonValue& t : tokens.items) {
        reasons.push_back(reasonFromToken(t.text, vocabulary));
    }
    return reasons;
}

// =====================================================================
// 夹具装载（每用例独立装载——golden fixture 无共享可变状态）
// =====================================================================

/// 装载黄金数据集（GoldenDataset::load 全链校验——失败抛 TestKitError 即
/// 数据资产缺陷：本任务交付的数据集不应有装载缺陷，失败而非跳过）。
tk::GoldenDataset loadFixture()
{
    return tk::GoldenDataset::load({kDatasetId, kDatasetVersion});
}

/// 读数据文件全文（resolveInput/resolveExpected 相对路径解析——越界路径
/// 由装载器拒绝）。
std::string readText(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    EXPECT_TRUE(in.is_open()) << "夹具数据文件不可读: " << file.string();
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

// =====================================================================
// 输入重组（inputs JSON → GateInputs / core 事件 / InvalidationSignal）
// =====================================================================

/// 域就绪项重组（ui §6.5 形状；verdict 不参与门控判定 I-WF-2——夹具按
/// inputComplete 给 Feasible/NotApplicable 占位，与 GateAdviceTest 夹具同口径）。
ui::DomainReadinessItem itemFromJson(const tk::JsonValue& node)
{
    ui::DomainReadinessItem item;
    item.domainKey = node.find("domainKey")->text;
    item.inputComplete = node.find("inputComplete")->boolean;
    for (const tk::JsonValue& key : node.find("missingItemKeys")->items) {
        item.missingItemKeys.push_back(key.text);
    }
    item.hasActiveTask = node.find("hasActiveTask")->boolean;
    item.verdict = item.inputComplete ? core::EngineeringStatus::Feasible
                                      : core::EngineeringStatus::NotApplicable;
    return item;
}

/// 七阶段投影板＋事件窗口＋水位 → GateInputs（快照级纪元＝算例级 epoch——
/// 夹具"同拍汇聚＝同纪元"的注入规则，与独立生成器同款）。
GateInputs boardFromJson(const tk::JsonValue& caseNode, const tk::JsonValue& vocabulary)
{
    GateInputs inputs;
    inputs.lastEpoch = static_cast<std::uint64_t>(
        caseNode.find("lastEpoch")->number);
    const std::uint64_t epoch =
        static_cast<std::uint64_t>(caseNode.find("epoch")->number);
    std::size_t i = 0;
    for (const tk::JsonValue& snap : caseNode.find("snapshots")->items) {
        if (i >= inputs.snapshots.size()) {
            ADD_FAILURE() << "夹具错误：快照板超七阶段";
            return inputs;
        }
        ui::StageReadinessSnapshot& snapshot = inputs.snapshots[i++];
        snapshot.stage = stageFromToken(snap.find("stage")->text);
        snapshot.epoch = epoch;
        for (const tk::JsonValue& domain : snap.find("domains")->items) {
            snapshot.domains.push_back(itemFromJson(domain));
        }
    }
    if (i != inputs.snapshots.size()) {
        ADD_FAILURE() << "夹具错误：快照板不足七阶段";
    }
    for (const tk::JsonValue& ev : caseNode.find("events")->items) {
        GateEvent event;
        event.kind = eventKindFromToken(ev.find("kind")->text);
        for (const tk::JsonValue& d : ev.find("domainKeys")->items) {
            event.domainKeys.push_back(d.text);
        }
        event.reasons = expandReasons(*ev.find("reasonTokens"), vocabulary);
        inputs.events.push_back(std::move(event));
    }
    return inputs;
}

/// 黄金 GateDecision 展开（expected JSON 词形 → 值对象——供 operator==
/// 全字段对照；reasons 由词表展开，与输入重组同映射表）。
GateDecision decisionFromJson(const tk::JsonValue& node, const tk::JsonValue& vocabulary)
{
    GateDecision decision;
    decision.stage = stageFromToken(node.find("stage")->text);
    decision.status = statusFromToken(node.find("status")->text);
    decision.unlocked = node.find("unlocked")->boolean;
    for (const tk::JsonValue& k : node.find("reasonKeys")->items) {
        decision.reasonKeys.push_back(k.text);
    }
    if (!node.find("unlockHintKey")->isNull()) {
        decision.unlockHintKey = node.find("unlockHintKey")->text;
    }
    for (const tk::JsonValue& k : node.find("missingItemKeys")->items) {
        decision.missingItemKeys.push_back(k.text);
    }
    for (const tk::JsonValue& h : node.find("staleHints")->items) {
        StaleHint hint;
        hint.stage = stageFromToken(h.find("stage")->text);
        hint.domainKey = h.find("domainKey")->text;
        hint.reasons = expandReasons(*h.find("reasons"), vocabulary);
        hint.actionKey = h.find("actionKey")->text;
        decision.staleHints.push_back(std::move(hint));
    }
    decision.epoch = static_cast<std::uint64_t>(node.find("epoch")->number);
    return decision;
}

/// 黄金建议条目展开（expected 词形 → NextStepAdvice）。
workflow::NextStepAdvice stepFromJson(const tk::JsonValue& node)
{
    workflow::NextStepAdvice step;
    step.actionKey = node.find("actionKey")->text;
    step.targetStage = stageFromToken(node.find("targetStage")->text);
    step.targetObjectId = node.find("targetObjectId")->text;
    step.titleKey = node.find("titleKey")->text;
    for (const tk::JsonValue& k : node.find("detailKeys")->items) {
        step.detailKeys.push_back(k.text);
    }
    return step;
}

/// 黄金 StageAdvice 展开（UX-01 四要素全字段）。
StageAdvice adviceFromJson(const tk::JsonValue& node)
{
    StageAdvice advice;
    advice.goalKey = node.find("goalKey")->text;
    for (const tk::JsonValue& k : node.find("missingItemKeys")->items) {
        advice.missingItemKeys.push_back(k.text);
    }
    for (const tk::JsonValue& k : node.find("problemKeys")->items) {
        advice.problemKeys.push_back(k.text);
    }
    for (const tk::JsonValue& s : node.find("steps")->items) {
        advice.steps.push_back(stepFromJson(s));
    }
    return advice;
}

/// 算例输入中取某阶段的汇聚快照（建议/映射用例的 snapshot 实参——与
/// boardFromJson 同注入规则：快照纪元＝算例级 epoch；定义于本匿名
/// namespace——对文件后续用例经闭合处隐式 using 可见，链接面自洽）。
ui::StageReadinessSnapshot boardSnapshotOf(const tk::JsonValue& caseNode,
                                           ui::StageId stage)
{
    const std::string token = ui::stageToken(stage);
    for (const tk::JsonValue& snap : caseNode.find("snapshots")->items) {
        if (snap.find("stage")->text == token) {
            ui::StageReadinessSnapshot snapshot;
            snapshot.stage = stage;
            snapshot.epoch =
                static_cast<std::uint64_t>(caseNode.find("epoch")->number);
            for (const tk::JsonValue& domain : snap.find("domains")->items) {
                snapshot.domains.push_back(itemFromJson(domain));
            }
            return snapshot;
        }
    }
    ADD_FAILURE() << "夹具错误：算例无阶段快照 " << token;
    return ui::StageReadinessSnapshot{};
}

}  // namespace

// =====================================================================
// 登记契约：manifest 字段落值＋装载即全量校验（数据资产登记面——
// SelGoldenDatasetContract 同款形态）
// =====================================================================

/**
 * golden wf-* 登记契约（acceptance 2 的机器断言面）：
 *   - 装载本身即全量校验（schema→integrity SHA-256/size→交叉校验）——
 *     load 返回即"登记完整且未被篡改"的构造性证明；
 *   - kind=contract-fixture（§11.0 登记口径）；coveredRequirements 覆盖
 *     UX-12/UX-01（门控/建议）与 NFR-COR-02（确定性）；
 *   - 档案引用 wf-fixture@1.0.0；生成器入库＋首版 history；inputs/expected
 *     各 ≥1；完整性清单覆盖 inputs＋expected＋generate 全部文件。
 */
TEST(WfGateAdviceFixture, ManifestRegistered_WP22T13_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-12", "UX-01", "UX-02", "NFR-COR-02"}),
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = loadFixture();
    const tk::DatasetManifest& m = ds.manifest();
    EXPECT_EQ(m.schemaVersion, "ird-golden-manifest/1");
    EXPECT_EQ(m.datasetId, kDatasetId);
    EXPECT_EQ(m.version, kDatasetVersion);
    EXPECT_EQ(m.kind, tk::DatasetKind::ContractFixture)
        << "§11.0 登记口径：门控/建议夹具为 contract-fixture 类";
    EXPECT_EQ(m.scenarioCategory, "workflow/gate-advice-mapping-fixture");

    // 登记要求覆盖（UX-12 门控/UX-01 四要素/NFR-COR-02 确定性——词表命中）。
    const auto has = [&m](const std::string& r) {
        return std::find(m.coveredRequirements.begin(), m.coveredRequirements.end(), r)
            != m.coveredRequirements.end();
    };
    EXPECT_TRUE(has("UX-12")) << "门控需求未登记";
    EXPECT_TRUE(has("UX-01")) << "建议四要素需求未登记";
    EXPECT_TRUE(has("NFR-COR-02")) << "确定性需求未登记";

    // 档案引用（零容差声明档案——判定面无浮点的档案化表达）。
    EXPECT_EQ(m.toleranceProfileId, std::string(kProfileId));
    EXPECT_EQ(m.toleranceProfileVersion, std::string(kProfileVersion));

    // 独立参考实现声明（双实现互证的登记面——期望值与产品实现零共享）。
    ASSERT_TRUE(m.referenceSourcePresent);
    EXPECT_TRUE(m.referenceSource.independentOfProductionImpl)
        << "黄金期望必须独立于产品实现（双实现互证——任一侧漂移即显性失败）";

    // 生成器入库＋首版 history＋inputs/expected 下限。
    ASSERT_TRUE(m.generatorPresent);
    EXPECT_TRUE(m.generatorCommitted);
    EXPECT_NE(m.generatorScript.find("generate/"), std::string::npos);
    ASSERT_FALSE(m.history.empty());
    EXPECT_EQ(m.history.front().version, std::string(kDatasetVersion));
    EXPECT_GE(m.inputs.size(), 1U);
    EXPECT_GE(m.expected.size(), 1U);

    // 完整性覆盖关系（load 只校验已登记条目——本用例断言清单完备性）。
    const auto covered = [&m](const std::string& rel) {
        return std::any_of(m.integrity.begin(), m.integrity.end(),
                           [&](const tk::IntegrityEntry& e) { return e.path == rel; });
    };
    for (const std::string& rel : m.inputs) {
        EXPECT_TRUE(covered(rel)) << "inputs 文件未登记完整性: " << rel;
    }
    for (const std::string& rel : m.expected) {
        EXPECT_TRUE(covered(rel)) << "expected 文件未登记完整性: " << rel;
    }
    EXPECT_TRUE(covered(m.generatorScript)) << "生成脚本未登记完整性";
}

// =====================================================================
// 门控确定性重放：夹具输入经 evaluate 与黄金七阶段判定逐字段对照
// =====================================================================

/**
 * 门控黄金重放（acceptance 2 主链——门控纯函数面）：逐算例重组 GateInputs
 * →IStageGateService::evaluate（接口虚派发）→与黄金 decisions 逐条值相等；
 * epoch-behind 算例断言异常轨（I-WF-4 旧投影水位违约＝WorkflowError
 * fail-fast）。判定面全部为布尔/枚举/键串事实——精确等值断言零容差。
 */
TEST(WfGateAdviceFixture, GateReplay_MatchesGolden_WP22T13_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-12", "NFR-COR-02"}),
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = loadFixture();
    const tk::JsonValue inputs =
        tk::parseJson(readText(ds.resolveInput("inputs/gate-advice-fixtures.json")));
    const tk::JsonValue expected =
        tk::parseJson(readText(ds.resolveExpected("expected/gate-advice-expected.json")));
    const tk::JsonValue& vocabulary = *inputs.find("reasonTokenVocabulary");

    workflow::PureStageGateService gateService;  // 非 const（接口引用消费——虚派发路径）
    const std::vector<tk::JsonValue>& inCases = inputs.find("cases")->items;
    const std::vector<tk::JsonValue>& goldenCases = expected.find("cases")->items;
    ASSERT_EQ(inCases.size(), goldenCases.size()) << "输入/黄金算例数不一致";

    for (std::size_t c = 0; c < inCases.size(); ++c) {
        const tk::JsonValue& in = inCases[c];
        const tk::JsonValue& golden = goldenCases[c];
        SCOPED_TRACE(std::string{"case "} + in.find("id")->text);
        ASSERT_EQ(in.find("id")->text, golden.find("id")->text) << "算例序错位";

        // throws=true＝期望 WorkflowError 拒绝（I-WF-4——异常轨黄金）。
        if (golden.find("gating")->find("throws")->boolean) {
            const GateInputs board = boardFromJson(in, vocabulary);
            EXPECT_THROW((void)gateService.evaluate(board), workflow::WorkflowError)
                << "旧投影水位违约应被拒绝判定";
            continue;
        }

        // 常规轨：经接口重放＋全字段黄金对照。
        const GateInputs board = boardFromJson(in, vocabulary);
        workflow::IStageGateService& gate = gateService;  // 接口消费路径（虚派发）
        const StageGatingState state = gate.evaluate(board);
        const std::vector<tk::JsonValue>& goldenDecisions =
            golden.find("gating")->find("decisions")->items;
        ASSERT_EQ(state.decisions.size(), goldenDecisions.size());
        for (std::size_t i = 0; i < state.decisions.size(); ++i) {
            SCOPED_TRACE(std::string{"stage "} + ui::stageToken(state.decisions[i].stage));
            const GateDecision goldenDecision =
                decisionFromJson(goldenDecisions[i], vocabulary);
            EXPECT_EQ(state.decisions[i], goldenDecision)
                << "门控判定与黄金期望漂移（双实现互证——产品侧或生成器侧"
                   "有一方偏离单元卡冻结语义）";
        }
    }
}

// =====================================================================
// 建议确定性重放：夹具输入经 adviseFor 与黄金四要素逐字段对照
// =====================================================================

/**
 * 建议黄金重放（acceptance 2——建议纯函数面）：逐算例逐建议（门控状态＝
 * 同算例 evaluate 产出——§6.4 数据流）→INextStepAdvisor::adviseFor（接口
 * 虚派发）→与黄金 StageAdvice 全字段值相等（UX-01 四要素＋八规则优先级
 * 序的双实现互证）。
 */
TEST(WfGateAdviceFixture, AdviceReplay_MatchesGolden_WP22T13_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-01", "UX-02", "NFR-COR-02"}),
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = loadFixture();
    const tk::JsonValue inputs =
        tk::parseJson(readText(ds.resolveInput("inputs/gate-advice-fixtures.json")));
    const tk::JsonValue expected =
        tk::parseJson(readText(ds.resolveExpected("expected/gate-advice-expected.json")));
    const tk::JsonValue& vocabulary = *inputs.find("reasonTokenVocabulary");

    workflow::PureStageGateService gateService;  // 非 const（接口引用消费——虚派发路径）
    workflow::PureNextStepAdvisor advisorImpl;
    const std::vector<tk::JsonValue>& inCases = inputs.find("cases")->items;
    const std::vector<tk::JsonValue>& goldenCases = expected.find("cases")->items;
    ASSERT_EQ(inCases.size(), goldenCases.size());

    for (std::size_t c = 0; c < inCases.size(); ++c) {
        const tk::JsonValue& in = inCases[c];
        const tk::JsonValue& golden = goldenCases[c];
        SCOPED_TRACE(std::string{"case "} + in.find("id")->text);
        if (golden.find("gating")->find("throws")->boolean) {
            EXPECT_TRUE(golden.find("advice")->items.empty())
                << "异常轨算例无建议黄金（拒判后无门控状态可消费）";
            continue;
        }

        // 门控状态＝同算例 evaluate 产出（建议消费门控产出——不重判）。
        const StageGatingState state =
            gateService.evaluate(boardFromJson(in, vocabulary));
        const std::vector<tk::JsonValue>& goldenAdvices = golden.find("advice")->items;
        ASSERT_EQ(in.find("advice")->items.size(), goldenAdvices.size())
            << "输入/黄金建议条目数不一致";
        for (std::size_t a = 0; a < goldenAdvices.size(); ++a) {
            const tk::JsonValue& spec = in.find("advice")->items[a];
            const tk::JsonValue& goldenAdvice = goldenAdvices[a];
            SCOPED_TRACE(std::string{"advice "} + goldenAdvice.find("id")->text);

            // AdviceInputs 四事实重组（恢复场景/阻塞诊断/归档结果/只读——
            // §6.4 数据流图的会话态两路承载）。
            workflow::AdviceInputs adviceInputs;
            for (const tk::JsonValue& k : spec.find("recoveryBannerKeys")->items) {
                adviceInputs.recoveryBannerKeys.push_back(k.text);
            }
            for (const tk::JsonValue& k : spec.find("blockingDiagnosticKeys")->items) {
                adviceInputs.blockingDiagnosticKeys.push_back(k.text);
            }
            adviceInputs.hasArchivedResult = spec.find("hasArchivedResult")->boolean;
            adviceInputs.sessionReadOnly = spec.find("sessionReadOnly")->boolean;

            // 经接口重放（虚派发——接口消费路径钉扎）。
            const ui::StageId stage = stageFromToken(spec.find("stage")->text);
            workflow::INextStepAdvisor& advisor = advisorImpl;
            const StageAdvice advice = advisor.adviseFor(
                stage, state, boardSnapshotOf(in, stage), adviceInputs);
            const StageAdvice goldenValue = adviceFromJson(goldenAdvice);
            EXPECT_EQ(advice, goldenValue)
                << "建议四要素与黄金期望漂移（规则优先级/键形/详情透传——"
                   "双实现互证面）";
        }
    }
}

// =====================================================================
// 级联映射确定性重放：失效信号经 mapInvalidation 与黄金提示清单对照
// =====================================================================

/**
 * 级联映射黄金重放（acceptance 2——映射纯函数面）：逐算例（有 invalidation
 * 场景者）构造 DependencyInvalidated 事件（core 工厂——仅身份载荷）＋
 * InvalidationSignal（受影响域＋原因透传）→IStageGateService::mapInvalidation
 * →与黄金 hints 逐条值相等（输出序＝(阶段序, 域键字典序) 的确定性序一并
 * 由序贯对照承载）。
 */
TEST(WfGateAdviceFixture, InvalidationMapping_MatchesGolden_WP22T13_ACC2)
{
    IRD_TEST_INFO((std::vector<std::string>{"UX-12", "NFR-COR-02"}),
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = loadFixture();
    const tk::JsonValue inputs =
        tk::parseJson(readText(ds.resolveInput("inputs/gate-advice-fixtures.json")));
    const tk::JsonValue expected =
        tk::parseJson(readText(ds.resolveExpected("expected/gate-advice-expected.json")));
    const tk::JsonValue& vocabulary = *inputs.find("reasonTokenVocabulary");

    workflow::PureStageGateService gateImpl;
    const std::vector<tk::JsonValue>& inCases = inputs.find("cases")->items;
    const std::vector<tk::JsonValue>& goldenCases = expected.find("cases")->items;

    for (std::size_t c = 0; c < inCases.size(); ++c) {
        const tk::JsonValue& in = inCases[c];
        const tk::JsonValue& golden = goldenCases[c];
        const bool hasScenario = golden.find("invalidation") != nullptr
            && !golden.find("invalidation")->isNull();
        if (!hasScenario) {
            continue;  // 无失效场景算例不跑映射（evaluate 黄金已覆盖）
        }
        SCOPED_TRACE(std::string{"case "} + in.find("id")->text);

        // 门控状态＝同算例 evaluate 产出（下游级联的 Completed 判定输入）。
        const StageGatingState state =
            gateImpl.evaluate(boardFromJson(in, vocabulary));

        // 失效事件（仅身份——载荷三元组为夹具固定值；域键与原因经信号
        // 传入，D-09 事件不携带数据）。
        core::DependencyInvalidatedPayload payload;
        payload.project = core::ProjectId::generate();
        payload.branch = core::BranchId::generate();
        payload.revision = core::RevisionId::generate();
        const core::DomainEvent ev = core::DomainEvent::make(payload);

        // 失效信号（受影响域＋原因——evidence 判定结果的测试等价物）。
        const tk::JsonValue& inv = *in.find("invalidation");
        workflow::InvalidationSignal signal;
        for (const tk::JsonValue& d : inv.find("signalDomainKeys")->items) {
            signal.domainKeys.push_back(d.text);
        }
        signal.reasons = expandReasons(*inv.find("reasonTokens"), vocabulary);

        // 快照输入（epoch 对齐——取任一就绪阶段的当前快照，纪元＝板纪元）。
        const ui::StageReadinessSnapshot& snapshot =
            boardSnapshotOf(in, ui::StageId::Modeling);

        workflow::IStageGateService& gate = gateImpl;  // 接口消费路径
        const std::vector<StaleHint> hints =
            gate.mapInvalidation(ev, snapshot, signal, state);

        const std::vector<tk::JsonValue>& goldenHints =
            golden.find("invalidation")->find("hints")->items;
        ASSERT_EQ(hints.size(), goldenHints.size())
            << "级联提示条数与黄金期望不一致（受影响域映射/下游 Completed 级联）";
        for (std::size_t h = 0; h < hints.size(); ++h) {
            // 黄金词条目展开（stage/domainKey/reasons/actionKey）→ 值相等
            // 对照——序贯对照同时钉住确定性输出序。
            StaleHint goldenHint;
            goldenHint.stage = stageFromToken(goldenHints[h].find("stage")->text);
            goldenHint.domainKey = goldenHints[h].find("domainKey")->text;
            goldenHint.reasons =
                expandReasons(*goldenHints[h].find("reasons"), vocabulary);
            goldenHint.actionKey = goldenHints[h].find("actionKey")->text;
            // 分维度预检（漂移定位面：域键/原因条数/动作键逐项报告）。
            EXPECT_EQ(hints[h].stage, goldenHint.stage) << "第 " << h << " 条 stage 漂移";
            EXPECT_EQ(hints[h].domainKey, goldenHint.domainKey)
                << "第 " << h << " 条 domainKey 漂移";
            ASSERT_EQ(hints[h].reasons.size(), goldenHint.reasons.size())
                << "第 " << h << " 条 reasons 条数漂移";
            for (std::size_t r = 0; r < hints[h].reasons.size(); ++r) {
                EXPECT_EQ(hints[h].reasons[r], goldenHint.reasons[r])
                    << "第 " << h << " 条第 " << r << " 原因漂移（key="
                    << hints[h].reasons[r].dependencyKey << "/"
                    << goldenHint.reasons[r].dependencyKey << "）";
            }
            EXPECT_EQ(hints[h].actionKey, goldenHint.actionKey)
                << "第 " << h << " 条 actionKey 漂移";
        }
    }
}

// =====================================================================
// 确定性重放（WF-VER-107 同型契约面）：同夹具双跑输出全等
// =====================================================================

/**
 * 同夹具双跑确定性（NFR-COR-02 同型——黄金数据通道承载）：逐算例对门控/
 * 建议/映射各跑两遍，两次输出以 operator== 全字段相等（黄金对照之外的第
 * 二重保证：即使黄金值整体更新，双跑不一致的纯函数面违约仍被本用例捕获）。
 */
TEST(WfGateAdviceFixture, DeterministicReplay_DoubleRunIdentical_WP22T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    const tk::GoldenDataset ds = loadFixture();
    const tk::JsonValue inputs =
        tk::parseJson(readText(ds.resolveInput("inputs/gate-advice-fixtures.json")));
    const tk::JsonValue& vocabulary = *inputs.find("reasonTokenVocabulary");

    workflow::PureStageGateService gateImpl;
    workflow::PureNextStepAdvisor advisorImpl;
    workflow::IStageGateService& gate = gateImpl;
    workflow::INextStepAdvisor& advisor = advisorImpl;

    for (const tk::JsonValue& in : inputs.find("cases")->items) {
        SCOPED_TRACE(std::string{"case "} + in.find("id")->text);
        const GateInputs board = boardFromJson(in, vocabulary);

        // 异常轨算例（epoch-behind——I-WF-4 拒判）：双跑确定性的异常面＝
        // 两次都 WorkflowError（不落常规轨，不因拒判而误报不一致）。
        if (in.find("epoch")->number < in.find("lastEpoch")->number) {
            EXPECT_THROW((void)gate.evaluate(board), workflow::WorkflowError);
            EXPECT_THROW((void)gate.evaluate(boardFromJson(in, vocabulary)),
                         workflow::WorkflowError);
            continue;
        }

        // 第一跑。
        StageGatingState first = gate.evaluate(board);
        // 第二跑（全新输入板——排除输入对象被共享修改的假一致性）。
        StageGatingState second = gate.evaluate(boardFromJson(in, vocabulary));
        ASSERT_EQ(first, second) << "门控双跑输出不一致（纯函数面违约——D-WF-3）";
        // 建议：逐阶段双跑（AdviceInputs 取默认基线——规则触发的确定性
        // 已由 Advice 黄金用例覆盖，本用例钉双跑一致）。
        for (std::size_t i = 0; i < first.decisions.size(); ++i) {
            const StageAdvice a1 = advisor.adviseFor(
                first.decisions[i].stage, first,
                boardSnapshotOf(in, first.decisions[i].stage), workflow::AdviceInputs{});
            const StageAdvice a2 = advisor.adviseFor(
                second.decisions[i].stage, second,
                boardSnapshotOf(in, second.decisions[i].stage), workflow::AdviceInputs{});
            ASSERT_EQ(a1, a2) << "建议双跑输出不一致（阶段 "
                              << ui::stageToken(first.decisions[i].stage) << "）";
        }
    }
}

// =====================================================================
// 档案通道：wf-fixture 零容差声明档案就绪与保守纪律
// =====================================================================

/**
 * 容差档案登记契约：档案装载通过（schema ird-tolerance-profile/1）＋
 * 零容差纪律钉扎——判定面为布尔/枚举/键串事实（无浮点），唯一条目
 * tolerance 与 allowedMax 全零（宽于零＝伪造近似等值）；档案不承载任何
 * 运行时阈值语义（selection sel-golden 档案同款保守纪律）。
 */
TEST(WfGateAdviceFixture, ToleranceProfileZeroToleranceChannel_WP22T13_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"},
                  std::vector<std::string>{});

    const auto root = tk::goldenDataRoot();
    const auto profilePath = root / "tolerance" / kProfileId
        / (std::string{"v"} + kProfileVersion + ".json");
    const tk::ToleranceProfile profile = tk::ToleranceProfile::load(profilePath);
    EXPECT_EQ(profile.profileId, std::string(kProfileId));
    EXPECT_EQ(profile.version, std::string(kProfileVersion));
    ASSERT_GE(profile.entries.size(), 1U) << "档案至少一条零容差声明条目";

    for (const tk::ToleranceEntry& e : profile.entries) {
        EXPECT_EQ(e.tolerance.relative, 0.0) << "条目 " << e.fieldPath << " 相对容差须为零";
        EXPECT_EQ(e.tolerance.absolute, 0.0) << "条目 " << e.fieldPath << " 绝对容差须为零";
        ASSERT_TRUE(e.allowedMax.has_value()) << "dataset-declared 条目 allowedMax 必填";
        EXPECT_EQ(e.allowedMax->relative, 0.0);
        EXPECT_EQ(e.allowedMax->absolute, 0.0);
    }
    // fieldPath 通道命中（消费测试经档案通道引用时的解析面）。
    EXPECT_NO_THROW((void)profile.resolve("fixture.replay.fact-equality"));
}

// =====================================================================
// 输入重组辅助（放在文件尾——仅被上方用例消费的私有小件）
// =====================================================================

