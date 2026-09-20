/**
 * @file   StatusWordModelTest.cpp
 * @brief  UI-T04 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         七态×权威词表映射表逐行断言（UI-STG-3）、求值优先级链、
 *         NotEvaluable 呈现口径（P-UI-2）、显示纪律数据面与九态短标签键。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T04.json acceptance 1（UI-STG-3 映射表
 *     逐行＋不新增状态词＋NotEvaluable 归 results-stale＋「无法判定」原因）、
 *     acceptance 2（显示纪律：FormalPassEligibility 五条件唯一放行、计算中
 *     附进度阶段与取消、过期附原因、失败附定位、九态短标签文案键
 *     state.<token>.label 就位）、acceptance 3（O-31：映射触发数据源经 ui
 *     自有当前性端口＋值投影承载——本文件全部输入均为 ui 值投影/core 词表，
 *     零对端 include 即为该处置的直接实证面）；
 *   - units/ui.md §6.3（词表/映射表/优先级原文）、§6.8（正交关系表与禁令）、
 *     §12.3 UI-STG-3 行（前置＝"桩投影数据集（含 NotEvaluable）"、观测点＝
 *     "StatusWordProjection 输出"）、§3.5（state.<token>.label 键约定）；
 *   - 先例：ShellModelTest.cpp 的模型层形态（纯函数矩阵断言，零 Widget——
 *     AGENTS 模型测试豁免）。
 *
 * 为什么映射用例放模型层而非 GUI 层：七态求值是纯函数面（evaluateStatusWord
 * 同输入同输出），按 §12.1 分层归 sdurws_ird_ui_test；阶段视图/徽标的控件
 * 呈现随 UI-T09 消费时在其 GUI 用例覆盖（§12.3 UI-STG-1/2 同款分工）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CurrentnessProjection;
using ui::EvidenceItemProjection;
using ui::EvidenceManifestProjection;
using ui::FormalPassEligibilityProjection;
using ui::NotEvaluableCause;
using ui::ReadinessProjection;
using ui::StatusFacts;
using ui::StatusWord;
using ui::StatusWordProjection;
using ui::TaskActivityProjection;

// =====================================================================
// 桩投影数据集（UI-STG-3 前置列"桩投影数据集（含 NotEvaluable）"——
// 全部为 ui 值投影/core 词表值，L5 装配后由对端适配器填充的形态）
// =====================================================================

/// 基线事实：项目已打开、就绪有效、无任务/无判定/无当前性（computable 兜底前态）。
StatusFacts baselineFacts()
{
    StatusFacts facts;
    facts.projectOpen = true;
    facts.readiness.valid = true;
    facts.currentness.status = CurrentnessProjection::Status::Current;
    return facts;
}

/// 构造 Superseded 当前性（附两条逐条目失效原因——呈现"附原因"的数据面）。
CurrentnessProjection supersededCurrentness()
{
    CurrentnessProjection currentness;
    currentness.status = CurrentnessProjection::Status::Superseded;
    CurrentnessProjection::Reason objectChanged;
    objectChanged.dependencyKey = "robot.base";
    objectChanged.kindToken = "object-content-changed";
    objectChanged.detail = "对象内容变化（cv-0001 → cv-0002）";
    CurrentnessProjection::Reason configChanged;
    configChanged.dependencyKey = "";
    configChanged.kindToken = "configuration-changed";
    configChanged.detail = "求解配置子集变化";
    currentness.reasons = {objectChanged, configChanged};
    return currentness;
}

/// 构造 NotEvaluable 当前性（status=空＋成因——"计算结果形态"非第三持久态）。
CurrentnessProjection notEvaluableCurrentness(NotEvaluableCause cause)
{
    CurrentnessProjection currentness;
    currentness.status = std::nullopt;  // 无默认 Current——evidence §8.1 规则行 4
    currentness.unevaluableCause = cause;
    return currentness;
}

/// 构造五条件资格投影（§7.2 FormalPassEligibility 的 ui 值投影形态）。
FormalPassEligibilityProjection formalPass(bool available, bool eligible,
                                           std::vector<std::string> unmet = {})
{
    FormalPassEligibilityProjection eligibility;
    eligibility.available = available;
    eligibility.eligible = eligible;
    eligibility.unmetConditions = std::move(unmet);
    return eligibility;
}

/// 构造最小失败诊断投影项（failed 态"附对象定位与修复建议"载体）。
diagnostics::DiagProjectionItem failedTaskDiag()
{
    diagnostics::DiagProjectionItem item;
    item.entryId = 42;
    item.code = "EX-WORKER-CRASHED";
    item.titleKey = "diag.ex-worker-crashed.title";
    item.detailKey = "diag.ex-worker-crashed.detail";
    item.severity = diagnostics::DiagnosticSeverity::Error;
    item.subject = core::ObjectId::generate();
    item.localName = "运动学求解任务";
    item.actionKind = "retry";
    return item;
}

// =====================================================================
// 用例组一：七态×权威词表映射表逐行断言（acceptance 1——UI-STG-3）
// =====================================================================

/**
 * §6.3 七态×权威词表映射表逐行断言（七行各一断言段——观测点＝
 * StatusWordProjection 输出）：
 *   - 行 1 empty-project：会话 NoProject——任务/结局/判定/当前性全"—"；
 *   - 行 2 incomplete：就绪投影无效（ReadinessSummary.valid==false）；
 *   - 行 3 computing：九态非终态逐值（Queued/Preparing/Running/Paused/
 *     Canceling——非终态无 outcome）；
 *   - 行 4 computable：无活跃任务＋Completed（可选）＋Feasible（可选）＋
 *     Current（可选）——含全缺省形态（尚无结果）；
 *   - 行 5 results-stale：无活跃任务＋Completed＋判定任一（不触发更高
 *     优先态者）＋Superseded；
 *   - 行 6 data-insufficient：无活跃任务＋Completed＋DataInsufficient；
 *   - 行 7 failed：终态 Failed＋Outcome Failed＋NotApplicable（表 3 约束）。
 */
TEST(StatusWordModel, SevenStateMappingTableRows_UISTG3_UI_T04_ACC1)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    // ---- 行 1 empty-project：无项目（触发轴 UiSessionState ∈ {NoProject}）。
    {
        StatusFacts facts;  // projectOpen=false——其余轴即使有值也不参与本行
        facts.tasks.activeStates = {core::TaskState::Running};
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::EmptyProject);
        // 空项目态无伴随数据（映射表行 1 全"—"——不携带计算中/过期/失败数据）。
        EXPECT_TRUE(projection.staleReasons.empty());
        EXPECT_FALSE(projection.notEvaluableCause.has_value());
        EXPECT_FALSE(projection.cancelAvailable);
    }

    // ---- 行 2 incomplete：就绪投影无效（输入级"未完成"）。
    {
        StatusFacts facts = baselineFacts();
        facts.readiness.valid = false;
        facts.readiness.invalidMustKeys = {"req.must.payload"};
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Incomplete);
    }

    // ---- 行 3 computing：非终态九态逐值（五值全命中——缺一即映射残缺）。
    {
        const std::vector<core::TaskState> nonTerminal = {
            core::TaskState::Queued,   core::TaskState::Preparing,
            core::TaskState::Running,  core::TaskState::Paused,
            core::TaskState::Canceling,
        };
        for (const core::TaskState state : nonTerminal) {
            StatusFacts facts = baselineFacts();
            facts.tasks.activeStates = {state};
            const StatusWordProjection projection = ui::evaluateStatusWord(facts);
            EXPECT_EQ(projection.word, StatusWord::Computing)
                << "非终态 " << core::toToken(state) << " 应呈现 computing";
        }
    }

    // ---- 行 4 computable：就绪有效 ∧ 无活跃任务 ∧（可选三事实齐备）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Completed;
        facts.tasks.latestEngineeringStatus = core::EngineeringStatus::Feasible;
        facts.currentness.status = CurrentnessProjection::Status::Current;
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Computable);

        // 可选事实全缺省（尚无结果）——同为 computable（"Current（可选）"）。
        StatusFacts bare = baselineFacts();
        EXPECT_EQ(ui::evaluateStatusWord(bare).word, StatusWord::Computable);
    }

    // ---- 行 5 results-stale：Superseded（工程判定轴"任一"——本段验证不
    //      触发更高优先态的取值：Feasible/EngineeringInfeasible/NotApplicable）。
    {
        for (const core::EngineeringStatus status : {
                 core::EngineeringStatus::Feasible,
                 core::EngineeringStatus::EngineeringInfeasible,
                 core::EngineeringStatus::NotApplicable,
             }) {
            StatusFacts facts = baselineFacts();
            facts.tasks.latestOutcome = core::TaskOutcome::Completed;
            facts.tasks.latestEngineeringStatus = status;
            facts.currentness = supersededCurrentness();
            const StatusWordProjection projection = ui::evaluateStatusWord(facts);
            EXPECT_EQ(projection.word, StatusWord::ResultsStale)
                << "Superseded（判定 " << core::toToken(status)
                << "）应呈现 results-stale";
        }
    }

    // ---- 行 6 data-insufficient：最近正式评估判定级 DataInsufficient。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Completed;
        facts.tasks.latestEngineeringStatus =
            core::EngineeringStatus::DataInsufficient;
        EvidenceManifestProjection manifest;
        EvidenceItemProjection missing;
        missing.itemId = "kinematics.path-coverage";
        missing.status = EvidenceItemProjection::Status::Missing;
        manifest.items = {missing};
        facts.evidence = manifest;
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::DataInsufficient);
    }

    // ---- 行 7 failed：终态 Failed＋Outcome Failed＋NotApplicable（表 3
    //      约束——取消/失败/中断无工程判定，ui 不复判）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Failed;
        facts.tasks.latestEngineeringStatus =
            core::EngineeringStatus::NotApplicable;
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Failed);
    }
}

/**
 * 求值优先级链逐对断言（§6.3 冻结序：empty-project＞computing＞incomplete
 * ＞failed＞data-insufficient＞results-stale＞computable——相邻对全部验证，
 * 防实现漏短路）＋ Error 级诊断活跃触发面＋工作台总徽标聚合。
 */
TEST(StatusWordModel, SevenStatePriorityChain_UISTG3_UI_T04_ACC1)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    // empty-project ＞ computing：无项目＋在途任务→仍呈现空项目。
    {
        StatusFacts facts;
        facts.tasks.activeStates = {core::TaskState::Running};
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::EmptyProject);
    }
    // computing ＞ incomplete：在途任务＋就绪无效→计算中最先呈现（UX-10）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.activeStates = {core::TaskState::Preparing};
        facts.readiness.valid = false;
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::Computing);
    }
    // incomplete ＞ failed：就绪无效＋最近任务失败→未完成阻断语义优先。
    {
        StatusFacts facts = baselineFacts();
        facts.readiness.valid = false;
        facts.tasks.latestOutcome = core::TaskOutcome::Failed;
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::Incomplete);
    }
    // failed ＞ data-insufficient：失败结局＋数据不足判定→失败优先。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Failed;
        facts.tasks.latestEngineeringStatus =
            core::EngineeringStatus::DataInsufficient;
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::Failed);
    }
    // data-insufficient ＞ results-stale：数据不足＋过期→数据不足优先。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Completed;
        facts.tasks.latestEngineeringStatus =
            core::EngineeringStatus::DataInsufficient;
        facts.currentness = supersededCurrentness();
        EXPECT_EQ(ui::evaluateStatusWord(facts).word,
                  StatusWord::DataInsufficient);
    }
    // results-stale ＞ computable：过期兜底在可计算之前（过期仍可查看历史）。
    {
        StatusFacts facts = baselineFacts();
        facts.currentness = supersededCurrentness();
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::ResultsStale);
    }
    // failed 触发面之二：Error 级诊断活跃（无失败结局）→ failed（§6.3
    // failed 行触发数据源原文第二分句）。
    {
        StatusFacts facts = baselineFacts();
        facts.errorDiagActive = true;
        EXPECT_EQ(ui::evaluateStatusWord(facts).word, StatusWord::Failed);
    }
    // 工作台总徽标＝取各活跃阶段中优先级最高者（§6.3 原文）。
    EXPECT_EQ(ui::dominantStatusWord({StatusWord::Computable,
                                      StatusWord::ResultsStale}),
              StatusWord::ResultsStale);
    EXPECT_EQ(ui::dominantStatusWord({StatusWord::ResultsStale,
                                      StatusWord::Computing}),
              StatusWord::Computing);
    EXPECT_EQ(ui::dominantStatusWord({StatusWord::Computable}),
              StatusWord::Computable);
    // 空输入不虚构状态词（无活跃阶段→nullopt）。
    EXPECT_FALSE(ui::dominantStatusWord({}).has_value());
}

/**
 * 不新增状态词（R-2 红线——七态为呈现投影而非新判定）：token 全集必须与
 * §6.3 冻结词表逐字一致（多一词＝私扩状态机；少一词＝漏登记）。
 */
TEST(StatusWordModel, FrozenVocabularyNoNewWords_UI_T04_ACC1)
{
    IRD_TEST_INFO("UX-06", {}, std::nullopt);

    // §6.3 词表 token 列原文（冻结稿——P-UI-1 处置：闭合前不改词表）。
    const std::vector<std::string> frozen = {
        "empty-project", "incomplete",      "computing", "results-stale",
        "data-insufficient", "failed",      "computable",
    };
    ASSERT_EQ(frozen.size(), std::size_t{7});

    // 全七枚举的 token 落入冻结集且一一对应（集合相等＋逐行相等双断言）。
    std::vector<std::string> produced;
    for (const int raw : {0, 1, 2, 3, 4, 5, 6}) {
        const auto word = static_cast<StatusWord>(raw);
        const std::string token = ui::statusWordToken(word);
        produced.push_back(token);
        // 键派生同源：state.<token>.label（§3.5 键约定）。
        EXPECT_EQ(ui::statusWordLabelKey(word),
                  "state." + token + ".label");
    }
    EXPECT_EQ(produced, frozen);
}

/**
 * NotEvaluable 呈现口径（P-UI-2/P-EV-4——§6.3 建议值，冻结前不私定）：
 * status==nullopt（计算形态，非第三持久态）归 results-stale＋「无法判定」
 * 原因；两种成因（跨上下文/依赖无法解析）都不得显示为 Current/通过。
 */
TEST(StatusWordModel, NotEvaluablePresentedAsStale_UI_T04_ACC1)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    for (const NotEvaluableCause cause :
         {NotEvaluableCause::UnresolvedDependency, NotEvaluableCause::CrossContext}) {
        StatusFacts facts = baselineFacts();
        facts.currentness = notEvaluableCurrentness(cause);
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);

        // 归入 results-stale 呈现（P-UI-2 建议口径——不新增状态词）。
        EXPECT_EQ(projection.word, StatusWord::ResultsStale);
        // 附「无法判定」原因（成因位＋文案键/过渡文本就位）。
        ASSERT_TRUE(projection.notEvaluableCause.has_value());
        EXPECT_EQ(*projection.notEvaluableCause, cause);
        EXPECT_EQ(ui::currentnessUnevaluableLabelKey(cause).find(
                      "state.currentness.unevaluable."),
                  std::size_t{0})
            << "「无法判定」原因须走 state.currentness.unevaluable.* 文案键";
        EXPECT_FALSE(
            ui::currentnessUnevaluableTransitionalLabel(cause).empty());
        // 无逐条目失效原因（原因清单是 Superseded 的伴随数据——NotEvaluable
        // 走 notEvaluableCause 位，两载体互斥防呈现层混淆）。
        EXPECT_TRUE(projection.staleReasons.empty());
    }

    // presence 纪律：两持久态下 notEvaluableCause 必无值（对端锚点同约束）。
    {
        StatusFacts current = baselineFacts();  // Current
        EXPECT_FALSE(ui::evaluateStatusWord(current).notEvaluableCause.has_value());
        StatusFacts stale = baselineFacts();
        stale.currentness = supersededCurrentness();
        EXPECT_FALSE(ui::evaluateStatusWord(stale).notEvaluableCause.has_value());
    }
}

// =====================================================================
// 用例组二：显示纪律（acceptance 2——§6.3/§6.8）
// =====================================================================

/**
 * 「正式通过」字样渲染放行门：FormalPassEligibility 五条件为唯一放行数据
 * 源——资格不可得（available=false）或任一条件未满足（eligible=false）时
 * 禁止渲染（含"证据不足"条件 evidence-incomplete——§6.3 显示纪律原文）；
 * ui 不得自行由 outcome/工程状态组合出"通过"结论（放行位只来自资格投影）。
 */
TEST(StatusWordModel, FormalPassDisplayGate_UI_T04_ACC2)
{
    IRD_TEST_INFO("EVI-01", {}, std::nullopt);

    // 五条件全满足（eligible＋available）→ 唯一放行形态。
    EXPECT_TRUE(ui::formalPassRenderable(formalPass(true, true)));

    // 资格事实面不可得（尚无正式评估）→ 禁止（不虚构资格）。
    EXPECT_FALSE(ui::formalPassRenderable(formalPass(false, false)));

    // 五条件未全满足逐 token 禁止（§7.2 unmetConditions 稳定词表——含
    // "证据不足不得显示通过"的 evidence-incomplete 与 status-not-feasible）。
    for (const std::string& unmet :
         {"mode-not-verified", "outcome-not-completed", "coverage-incomplete",
          "evidence-incomplete", "status-not-feasible"}) {
        EXPECT_FALSE(ui::formalPassRenderable(formalPass(true, false, {unmet})))
            << "条件未满足（" << unmet << "）必须禁止渲染正式通过";
    }

    // 资格数据恒随七态投影同行（同快照——呈现层无需二次取数拼接）：
    // 即使命中 computable（"可显示正式通过结论"的组合呈现行），放行与否
    // 也只看 formalPass 字段（§6.8"须 FormalPassEligibility 五条件齐"）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Completed;
        facts.tasks.latestEngineeringStatus = core::EngineeringStatus::Feasible;
        facts.formalPass = formalPass(true, false, {"evidence-incomplete"});
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Computable);
        EXPECT_FALSE(ui::formalPassRenderable(projection.formalPass));
    }
}

/**
 * 计算中附进度阶段与取消、过期附原因（InvalidationReason 清单）、失败附
 * 定位（诊断投影项）、数据不足附缺失项全量清单——伴随呈现数据逐面断言。
 */
TEST(StatusWordModel, DisplayDisciplineCompanions_UI_T04_ACC2)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    // 计算中：进度阶段文案键＋取消可用位随投影输出（§9.4 phaseToken 承载）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.activeStates = {core::TaskState::Running};
        facts.tasks.progressStageKey = "stage.batch.3of7";
        facts.tasks.cancelAvailable = true;
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Computing);
        EXPECT_EQ(projection.progressStageKey, std::string("stage.batch.3of7"));
        EXPECT_TRUE(projection.cancelAvailable);
    }

    // 过期：逐条目失效原因清单原样随投影（"附 InvalidationReason 清单"——
    // ui 不改写对端产出的原因文本，UX-02 原样呈现）。
    {
        StatusFacts facts = baselineFacts();
        facts.currentness = supersededCurrentness();
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        ASSERT_EQ(projection.staleReasons.size(), std::size_t{2});
        EXPECT_EQ(projection.staleReasons[0].dependencyKey, "robot.base");
        EXPECT_EQ(projection.staleReasons[0].kindToken,
                  std::string("object-content-changed"));
        EXPECT_EQ(projection.staleReasons[1].kindToken,
                  std::string("configuration-changed"));
    }

    // 失败：诊断投影项原样随投影（"附对象定位与修复建议"——subject/
    // localName 定位与 actionKind 处置入口逐字保留）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Failed;
        facts.failureDiagnostics = {failedTaskDiag()};
        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::Failed);
        ASSERT_EQ(projection.failureDiagnostics.size(), std::size_t{1});
        EXPECT_EQ(projection.failureDiagnostics[0].code, "EX-WORKER-CRASHED");
        ASSERT_TRUE(projection.failureDiagnostics[0].subject.has_value());
        EXPECT_EQ(projection.failureDiagnostics[0].actionKind, "retry");
    }

    // 数据不足：缺失项全量清单（Missing/Invalid/Unverified 全列——不短路；
    // Satisfied/NotApplicable 不列——NotApplicable 显式不适用不计缺失）。
    {
        StatusFacts facts = baselineFacts();
        facts.tasks.latestOutcome = core::TaskOutcome::Completed;
        facts.tasks.latestEngineeringStatus =
            core::EngineeringStatus::DataInsufficient;

        EvidenceItemProjection satisfied;
        satisfied.itemId = "kin.path-coverage";
        satisfied.status = EvidenceItemProjection::Status::Satisfied;
        EvidenceItemProjection missing;
        missing.itemId = "dyn.payload-mass";
        missing.status = EvidenceItemProjection::Status::Missing;
        EvidenceItemProjection invalid;
        invalid.itemId = "col.safety-sweep";
        invalid.status = EvidenceItemProjection::Status::Invalid;
        invalid.note = "绑定校验失败（快照身份不匹配）";
        EvidenceItemProjection unverified;
        unverified.itemId = "opt.quick-proxy";
        unverified.status = EvidenceItemProjection::Status::Unverified;
        EvidenceItemProjection notApplicable;
        notApplicable.itemId = "drv.regenerative-brake";
        notApplicable.status = EvidenceItemProjection::Status::NotApplicable;
        notApplicable.note = "显式不适用（配置未启用该驱动）";

        EvidenceManifestProjection manifest;
        manifest.items = {satisfied, missing, invalid, unverified, notApplicable};
        facts.evidence = manifest;

        const StatusWordProjection projection = ui::evaluateStatusWord(facts);
        EXPECT_EQ(projection.word, StatusWord::DataInsufficient);
        // 全量清单恰为三不满足态、原序保留（Satisfied/NotApplicable 不列）。
        ASSERT_EQ(projection.unsatisfiedEvidence.size(), std::size_t{3});
        EXPECT_EQ(projection.unsatisfiedEvidence[0].itemId, "dyn.payload-mass");
        EXPECT_EQ(projection.unsatisfiedEvidence[1].itemId, "col.safety-sweep");
        EXPECT_EQ(projection.unsatisfiedEvidence[2].itemId, "opt.quick-proxy");
        // 清单工具与求值路径同源（unsatisfiedEvidenceItems 直调结果一致）。
        EXPECT_EQ(ui::unsatisfiedEvidenceItems(manifest).size(),
                  projection.unsatisfiedEvidence.size());
    }
}

// =====================================================================
// 用例组三：九态短标签文案键（acceptance 2——PM-03/PM-11）
// =====================================================================

/**
 * 九态短标签文案键 state.<token>.label 就位：全九态键逐一断言，token 段与
 * core::toToken(TaskState) 词表逐字对断（键由 core 持久化 token 派生——
 * 单一权威，防本单元私立第二份九态 token 表）；过渡中文值与 §6.3 九态短
 * 标签行原文一致（排队中/准备中/计算中/已暂停/取消中/已取消/已完成/失败/
 * 已中断——UI-T09 UiText 资源化前过渡承载）。
 */
TEST(StatusWordModel, NineStateLabelKeys_PM03_UI_T04_ACC2)
{
    IRD_TEST_INFO("PM-03", {}, std::nullopt);

    // {九态, 冻结 token, §6.3 中文短标签}全表（9 行——缺一即文案键残缺）。
    const std::vector<std::tuple<core::TaskState, const char*, const char*>>
        expected = {
            {core::TaskState::Queued,      "queued",      u8"排队中"},
            {core::TaskState::Preparing,   "preparing",   u8"准备中"},
            {core::TaskState::Running,     "running",     u8"计算中"},
            {core::TaskState::Paused,      "paused",      u8"已暂停"},
            {core::TaskState::Canceling,   "canceling",   u8"取消中"},
            {core::TaskState::Canceled,    "canceled",    u8"已取消"},
            {core::TaskState::Completed,   "completed",   u8"已完成"},
            {core::TaskState::Failed,      "failed",      u8"失败"},
            {core::TaskState::Interrupted, "interrupted", u8"已中断"},
        };
    ASSERT_EQ(expected.size(), std::size_t{9});

    for (const auto& [state, token, label] : expected) {
        // 键＝state.<core token>.label（§3.5 键约定；token 与 core 一致）。
        EXPECT_EQ(ui::taskStateLabelKey(state),
                  std::string("state.") + token + ".label")
            << "九态键 token 段必须与 core::toToken 一致: " << token;
        // 过渡中文值＝§6.3 九态短标签行原文。
        EXPECT_EQ(ui::taskStateTransitionalLabel(state), std::string(label));
    }

    // 键/值分离自查：七态与九态各自走同族键约定（两词表 failed 重名不
    // 冲突——中文同为"失败"，其余键互异）。
    EXPECT_EQ(ui::statusWordLabelKey(StatusWord::Failed),
              std::string("state.failed.label"));
    EXPECT_EQ(ui::taskStateLabelKey(core::TaskState::Failed),
              std::string("state.failed.label"));
}

/**
 * C-7 当前性端口（acceptance 3——O-31 裁决载体）：端口形状可经 ui 自有
 * 替身实现（L5 适配 evidence 只读面），三事实面（当前性/清单/资格）经值
 * 投影回传，供七态求值触发数据源组装——产品面对 evidence 零 include 的
 * 消费形态实证（红线扫描另有 BuildRedLineTest 常驻守卫）。
 */
TEST(StatusWordModel, CurrentnessPortValueCarriers_O31_UI_T04_ACC3)
{
    IRD_TEST_INFO("UX-10", {}, std::nullopt);

    /// ui 自有替身（§3.1"ui 测试以可控替身承载"）——模拟 L5 适配器把
    /// evidence 判定结果翻译成值投影后的形态。
    struct StubCurrentnessSource final : ui::IUiCurrentnessSource
    {
        CurrentnessProjection currentnessResult;
        EvidenceManifestProjection manifest;
        FormalPassEligibilityProjection eligibility;

        CurrentnessProjection currentness() const override
        {
            return currentnessResult;
        }
        EvidenceManifestProjection evidenceManifest() const override
        {
            return manifest;
        }
        FormalPassEligibilityProjection formalPassEligibility() const override
        {
            return eligibility;
        }
    };

    StubCurrentnessSource source;
    source.currentnessResult = supersededCurrentness();
    EvidenceItemProjection missing;
    missing.itemId = "kin.path-coverage";
    missing.status = EvidenceItemProjection::Status::Missing;
    source.manifest.items = {missing};
    source.eligibility = formalPass(true, false, {"evidence-incomplete"});

    // 端口三面经 IUiCurrentnessSource 抽象消费（多态调用面成立）。
    const ui::IUiCurrentnessSource& port = source;
    const CurrentnessProjection currentness = port.currentness();
    ASSERT_TRUE(currentness.status.has_value());
    EXPECT_EQ(*currentness.status, CurrentnessProjection::Status::Superseded);
    EXPECT_EQ(port.evidenceManifest().items.size(), std::size_t{1});
    EXPECT_FALSE(ui::formalPassRenderable(port.formalPassEligibility()));

    // 端口回传值可直接组装 StatusFacts 并驱动七态求值（触发数据源链路通）。
    StatusFacts facts = baselineFacts();
    facts.tasks.latestOutcome = core::TaskOutcome::Completed;
    facts.currentness = currentness;
    facts.evidence = port.evidenceManifest();
    facts.formalPass = port.formalPassEligibility();
    const StatusWordProjection projection = ui::evaluateStatusWord(facts);
    EXPECT_EQ(projection.word, StatusWord::ResultsStale);
    EXPECT_FALSE(ui::formalPassRenderable(projection.formalPass));
}

}  // namespace
