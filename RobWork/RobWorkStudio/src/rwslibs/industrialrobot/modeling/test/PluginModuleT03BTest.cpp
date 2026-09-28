/**
 * @file   PluginModuleT03BTest.cpp
 * @brief  建模插件界面模块（ModelingUiModule）T03b 收口测试——域就绪汇聚
 *         源/会话全同步/策略与名称适配的模块级具名自证面。
 *
 * 设计依据：契约 tasks/foundation/WP-24-T03.json acceptance 2/3/5/6；
 * units/ui.md §6.5（汇聚源端口）、§5.2/§5.4/§6.2（会话全路径同步）、
 * §16.7 v1.18（收口落位登记）；units/modeling.md §9.7.3。
 *
 * ★ gating 说明（为何本文件整体挂 TARGET sdurw_kinematics 条件——与
 *   ReadinessTest/CommandHandlersTest 同款集成模式专属口径）：被测类型
 *   ModelingUiModule 自 T03b-2b 起持有 policy 行程评估器
 *   （policy::makeJointLimitEvaluator——其实现 TU policy/src/JointLimits.cpp
 *   仅集成模式编译，原因见 policy/CMakeLists.txt 源清单注释），本文件所有
 *   用例都构造该模块——若编入冒烟树，sdurws_ird_modeling_test 将因符号
 *   不可解析而链接失败（WP-24-T03 验证修复登记的继承缺陷根因）。集成模式
 *   （契约 verify 口径）下本文件全量执行。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <vector>

#include "CommandFixtures.hpp"  // userProvenance/makePolicy（测试夹具——同单元测试面共享）

#include <sdurws/ird/core/Identity.hpp>       // core::BranchId/RevisionId/ObjectId（值面）
#include <sdurws/ird/modeling/Parts.hpp>     // DrivetrainDesign/FrictionEntry（传动编辑夹具）
#include <sdurws/ird/modeling/Template.hpp>  // RobotDesignTemplateFactory/createDraft＋ModelingChangeRecord
#include <sdurws/ird/ui/IStageNavigationModel.hpp>  // ui::StageNavigationModelDeps/createStageNavigationModel（§6.5 汇聚出口）
#include <sdurws/ird/ui/UiPorts.hpp>          // ui::IUiStageGate（汇聚模型必注入端口——桩实现于本文件）
#include "plugin/ModelingUiModule.hpp"        // 被测模块（同单元私有头）
#include "plugin/PolicyNameContexts.hpp"      // 映射转发形名称上下文（T03b——直接构造验证）

// ---- 使用声明（与被测头同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::modeling;
using namespace sdurws::ird::modeling::testfixture;  // 夹具（kPi/makePolicy/userProvenance）
namespace ui = ::sdurws::ird::ui;

namespace {

/// generic-6r 草稿的便捷创建（PluginPanelTest 同款——成功前置起步夹具）。
ModelingWorkingSet makeSixAxisDraft()
{
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    const TemplateOutcome outcome = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags);
    return outcome.get();  // 成功前置——失败即测试自身装配错误（logic_error）
}

/**
 * @brief 阶段门控桩（ui::IUiStageGate 最小实现——汇聚模型装配必注入项）。
 *
 * 纪律：门控判定权威归 workflow（N-11 无第二套状态机）——本桩零判定，
 * presentStage 恒回"尚无门控数据"安全空值（StageGateView 默认 NotStarted），
 * evaluate 恒拒绝（默认构造 decision）。本文件只消费 §6.5 汇聚投影，
 * 门控面非被测对象。
 */
class NoopStageGate final : public ui::IUiStageGate {
public:
    ui::StageGateView presentStage(ui::StageId) const override
    {
        return ui::StageGateView{};  // 默认＝NotStarted 安全空值（不虚构门控语义）
    }

    ui::StageGateDecision evaluate(const ui::StageReadinessSnapshot&) const override
    {
        return ui::StageGateDecision{};  // 默认＝拒绝（本文件不测导航，仅装配）
    }
};

/// 汇聚模型的装配依赖（必注入四源全为常量桩——只读拉取面零状态）。
ui::StageNavigationModelDeps makeAggregationDeps(ui::IUiDomainReadinessSource& source)
{
    ui::StageNavigationModelDeps deps;
    static NoopStageGate gate;  // 桩无状态——文件级单例即可（const 查询面）
    deps.gate = &gate;
    deps.epochSource = [] { return std::uint64_t{7}; };  // 常量纪元——epoch 标注断言锚
    deps.writableSource = [] { return true; };
    deps.sessionNavigableSource = [] { return true; };
    // 七态数据源：恒 nullopt＝各阶段无七态数据（§6.3——汇聚测试不触及七态面）。
    deps.sevenStateFacts = [](ui::StageId) { return std::optional<ui::StatusFacts>{}; };
    deps.domainSources = {&source};  // 建模域源注册（注册序＝快照 domains 序）
    return deps;
}

}  // namespace

// =====================================================================
// WP-24-T03b 收口——域就绪汇聚源/会话同步/策略与名称适配（模块级验证面；
// 面板未挂接＝刷新半区空操作，判定面完整可达）
// =====================================================================

/**
 * 域就绪汇聚源（契约 WP-24-T03b acceptance 3）：domainReadiness(Modeling)
 * 与 readonlyProjections 逐字段一致（同一快照同源——ACC5 零缓存）；非建
 * 模阶段＝空清单；种子后（未锚定）呈 DataInsufficient 输入不完整缺省行
 * ——零判定，汇聚输入不加工（N-11）。
 */
TEST(PluginPanelT03B, DomainReadinessSourceParity_WP24_T03B)
{
    IRD_TEST_INFO("UX-12", {}, std::nullopt);
    ModelingUiModule module;
    module.seedTemplateSession();

    // 种子后重算过就绪（L11 Blocking 态——策略未装载）：两出口同源。
    const auto viaPort = module.domainReadiness(ui::StageId::Modeling);
    const auto viaModule = module.readonlyProjections();
    ASSERT_EQ(viaPort.size(), viaModule.size());
    ASSERT_FALSE(viaPort.empty());
    EXPECT_EQ(viaPort.front().domainKey, viaModule.front().domainKey);
    EXPECT_EQ(viaPort.front().verdict, viaModule.front().verdict);
    EXPECT_EQ(viaPort.front().inputComplete, viaModule.front().inputComplete);

    // 非建模阶段＝空清单（§6.5 端口契约"空清单不计入快照"）。
    EXPECT_TRUE(module.domainReadiness(ui::StageId::Kinematics).empty());
}

/**
 * 会话脱离与修订事件（契约 WP-24-T03b acceptance 6 的模块级验证面）：
 * 锚定后 onRevisionCommitted 前移基线（信封 expectedRevision＝新 tip——
 * 再应用不误报 Stale）；非锚定分支事件忽略；onSessionDetached 清空会话
 * 态（此后 buildDraftCommand 恒 nullopt）。
 */
TEST(PluginPanelT03B, SessionDetachAndRevisionAdvance_WP24_T03B)
{
    IRD_TEST_INFO("PM-17", {}, std::nullopt);
    ModelingUiModule module;
    module.seedTemplateSession();

    // 锚定＋制造一条未应用编辑（L-2 接受流——changes 非空才有信封）。
    const core::BranchId branch = core::BranchId::generate();
    const core::RevisionId tip = core::RevisionId::generate();
    module.bindSessionAnchor(branch, tip);
    auto& workingSet = module.session();
    const RobotDesignTemplateFactory factory;
    std::vector<core::DiagnosticRecord> diags;
    workingSet.draft = factory.createDraft(
        TemplateId{kTemplateIdGeneric6R},
        runtime::InstallationPresetToken::Ground, "demo", diags).get();
    workingSet.draft.changes.push_back(
        ModelingChangeRecord{"joints[1]", "测试编辑（T03B 会话同步用例）"});

    // 撤销/重做形态的修订提交：基线前移到新 tip；编辑记录保留（与
    // noteAppliedRevision 的"应用即消费"区分）。
    const core::RevisionId newTip = core::RevisionId::generate();
    module.onRevisionCommitted(branch, newTip);
    const auto envelope = module.buildDraftCommand("modeling");
    ASSERT_TRUE(envelope.has_value());
    EXPECT_EQ(envelope->expectedRevision.has_value()
                  && envelope->expectedRevision->toCanonical() == newTip.toCanonical(),
              true) << "基线未前移到新 tip（再应用将误报 Stale）";
    EXPECT_FALSE(workingSet.draft.changes.empty())
        << "修订事件清零了编辑记录（与应用回执语义混淆）";

    // 非锚定分支事件忽略（基线不被跨分支修订污染——§6.2 对位）。
    module.onRevisionCommitted(core::BranchId::generate(),
                               core::RevisionId::generate());
    const auto envelope2 = module.buildDraftCommand("modeling");
    ASSERT_TRUE(envelope2.has_value());
    EXPECT_EQ(envelope2->expectedRevision->toCanonical(), newTip.toCanonical());

    // 会话脱离：锚清空＋changes 清空→buildDraftCommand 恒 nullopt。
    module.onSessionDetached();
    EXPECT_FALSE(module.buildDraftCommand("modeling").has_value());
    EXPECT_FALSE(module.session().baseRevision.has_value());
}

/**
 * 策略装载与名称适配（契约 WP-24-T03b acceptance 5 的模块级验证面）：
 * 未绑定映射的名称上下文如实空值轨（nullopt/全零——与退役桩可观测行为
 * 一致但已是真端口转发形）；空映射绑定后 tryObjectId 未命中仍 nullopt
 * （ARC-04 不猜测）。
 */
TEST(PluginPanelT03B, PolicyNameContextHonestEmptyPath_WP24_T03B)
{
    IRD_TEST_INFO("ARC-03", {}, std::nullopt);
    // 未绑定映射：三方法如实空值（nullopt/nullopt/全零保留值——CON-06
    // "空映射与无映射在本类型层同态"）。
    const RuntimeMapPolicyNameContext unbound(nullptr);
    EXPECT_FALSE(unbound.tryObjectId("RobotScope.Base").has_value());
    EXPECT_FALSE(unbound.tryRuntimeName(core::ObjectId::generate()).has_value());
    EXPECT_TRUE(unbound.nameMapContentIdentity() == core::ContentIdentity{})
        << "未绑定映射的内容身份非全零保留值（CON-06 口径失真）";

    // 空映射（默认构造真身）：查询全 UnknownObject——未命中如实 nullopt。
    const runtime::RuntimeNameMap emptyMap;
    const RuntimeMapPolicyNameContext bound(&emptyMap);
    EXPECT_FALSE(bound.tryObjectId("RobotScope.Base").has_value());
    EXPECT_FALSE(bound.tryRuntimeName(core::ObjectId::generate()).has_value());
}

/**
 * 模板命令执行＝会话草稿重置（契约 WP-24-T03b acceptance 2 具名用例
 * "new-from-template 执行后会话草稿重置"的模块级验证面）：seedTemplateSession
 * 是 modeling.new-from-template 处理器的域内执行半区（UiPlugin 注册的
 * handler 直调）——脏会话（未应用编辑＋物性改动）整体重置为模板初稿：
 * 编辑记录清零、设计回到与模板工厂新产草稿逐字段一致、就绪重算不残留
 * 旧判定（模板物性出厂全部 NotProvided——模板不猜测物性，Template.cpp
 * 注释原文，故重置后物性回落缺失态而非某个正值）。
 */
TEST(PluginPanelT03B, SeedTemplateResetsDraftSession_WP24_T03B)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12", "MDL-05"}, {}, std::nullopt);
    ModelingUiModule module;
    module.seedTemplateSession();

    // 第一段：把会话改"脏"——①未应用编辑入账（L-2 账面）；②首个连杆
    // 物性改成非法负质量（L-2 接受流等价的权威面改动，断言阻断级）。
    auto& ws = module.session();
    ws.draft.changes.push_back(
        ModelingChangeRecord{"links[0]", "重种子前脏编辑（T03B 重置用例）"});
    ASSERT_FALSE(ws.draft.design.links.empty()) << "模板草稿至少一连杆（模板装配错误）";
    ws.draft.design.links[0].body.mass = core::SourcedValue<double>::provided(
        -5.0, userProvenance());  // m≤0——L5 断言阻断（ReadinessTest 同款构造）
    module.recomputeReadiness();
    {
        // 重种子前：阻断在案（负质量＝NotReady——输入不完整）。
        const auto dirty = module.domainReadiness(ui::StageId::Modeling);
        ASSERT_FALSE(dirty.empty());
        EXPECT_FALSE(dirty.front().inputComplete)
            << "脏会话（负质量阻断）仍呈输入完整——就绪重算失效";
    }

    // 第二段：重种子（＝处理器执行的模板重建）——编辑记录清零＋设计回
    // 模板初稿（与工厂新产草稿逐字段一致——模板工厂确定性）＋就绪重算
    // 不残留旧判定。
    module.seedTemplateSession();
    const auto& fresh = module.session();
    EXPECT_TRUE(fresh.draft.changes.empty())
        << "重种子后编辑记录未清零（脏账残留——下一次 apply 将携带陈旧编辑）";
    const ModelingWorkingSet pristine = makeSixAxisDraft();
    EXPECT_TRUE(fresh.draft.design == pristine.design)
        << "重种子后设计与模板工厂新产草稿不一致（脏改动残留）";
    // 就绪重算已执行（种子后非空——T03b-2b：种子后就绪条不再空态）。
    EXPECT_TRUE(fresh.readiness.has_value()) << "重种子后就绪报告缺失";
    // 锚不被重种子触碰（锚定/脱离归会话路径——acceptance 6 的分工面）。
    EXPECT_FALSE(fresh.baseRevision.has_value()) << "重种子不得伪造会话锚";
}

/**
 * 汇聚快照与编辑重算联动（契约 WP-24-T03b acceptance 3 具名用例"域源
 * 注册后快照含 modeling 行且与模块 readonlyProjections 逐字段一致；编辑
 * 后缺项键集变化反映进快照"）：建模模块作为 IUiDomainReadinessSource
 * 注册进 StageNavigationModel（domainSources）——①快照含 modeling 行且
 * 与模块直读投影逐字段一致、epoch 标注装配纪元；②策略装载（L11 解除）
 * 触发重算后"策略不可解析"阻断消失、行程评估真实执行（名称上下文为空
 * 映射→评估结果 Failed＋名称不可解析诊断——ARC-04 不猜测的诚实应答，
 * 契约 note 诚实边界②：真名映射随 UI-T20 到位）；③④物性补全/清空两次
 * 编辑后，汇聚快照缺项键集随之减/增且与模块直读同拍（ACC5 零缓存）。
 */
TEST(PluginPanelT03B, StageSnapshotParityAndEditReflection_WP24_T03B)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-12", "REQ-06"}, {}, std::nullopt);
    ModelingUiModule module;
    module.seedTemplateSession();
    const auto model = ui::createStageNavigationModel(makeAggregationDeps(module));

    // ①域源注册后：快照含 modeling 行，与模块 readonlyProjections 逐字段
    // 一致（verdict/inputComplete/missingItemKeys 全量对位）；epoch＝装配
    // 纪元常量 7（快照标注构建纪元——§6.2）。策略未装载＝L11"策略不可
    // 解析——行程校验未执行"Blocking（acceptance 5 的诚实半区——未装载
    // 不伪造可行）：输入完整性结论为不完整。
    {
        const auto snap = model->readinessSnapshot(ui::StageId::Modeling);
        const auto direct = module.readonlyProjections();
        ASSERT_EQ(snap.domains.size(), std::size_t{1})
            << "汇聚快照未收入 modeling 域行（域源注册面失效）";
        EXPECT_EQ(snap.domains.front().domainKey, "modeling");
        EXPECT_EQ(snap.epoch, std::uint64_t{7});
        EXPECT_EQ(snap.domains.front().verdict, direct.front().verdict);
        EXPECT_EQ(snap.domains.front().inputComplete, direct.front().inputComplete);
        ASSERT_EQ(snap.domains.front().missingItemKeys.size(),
                  direct.front().missingItemKeys.size());
        EXPECT_FALSE(snap.domains.front().inputComplete);
        // L11 阻断语义在模块会话报告侧对账（未装载＝策略不可解析）。
        ASSERT_TRUE(module.session().readiness.has_value());
        bool policyUnresolvedBlocking = false;
        for (const auto& note : module.session().readiness->notes) {
            if (note.blocking && note.subjectPath == "resolvedPolicy") {
                policyUnresolvedBlocking = true;
            }
        }
        EXPECT_TRUE(policyUnresolvedBlocking)
            << "未装载策略时缺少 L11 策略不可解析阻断（伪造可行——ERR-01）";
    }

    // ②策略装载触发重算（bindPolicyProvider 内部即重算——T03b 收口）：
    // "策略不可解析"阻断消失，行程评估真实执行——名称上下文为空映射→
    // 评估 Failed＋名称不可解析诊断（真评估、诚实失败，不伪造可行）；
    // 输入完整性如实保持不完整（真名映射随 UI-T20 到位）。汇聚快照与
    // 模块直读同拍。键集基线在此留档（④的增长对照）。
    std::size_t keysAfterPolicyLoad = 0;
    const auto policy = makePolicy(4.0 * kPi);
    module.bindPolicyProvider([&policy] { return &policy; });
    {
        const auto snap = model->readinessSnapshot(ui::StageId::Modeling);
        const auto direct = module.readonlyProjections();
        ASSERT_EQ(snap.domains.size(), std::size_t{1});
        EXPECT_EQ(snap.domains.front().verdict, direct.front().verdict);
        EXPECT_EQ(snap.domains.front().inputComplete, direct.front().inputComplete);
        EXPECT_FALSE(snap.domains.front().inputComplete)
            << "名称映射为空时行程评估未终态化仍呈输入完整（伪造可行）";
        keysAfterPolicyLoad = snap.domains.front().missingItemKeys.size();
        // L 行评估结果的模块侧对账："策略不可解析"阻断消失＋行程评估
        // 产出的名称不可解析码在案（评估真实执行——不是静默跳过）。
        ASSERT_TRUE(module.session().readiness.has_value());
        bool policyUnresolvedGone = true;
        for (const auto& note : module.session().readiness->notes) {
            if (note.blocking && note.subjectPath == "resolvedPolicy") {
                policyUnresolvedGone = false;
            }
        }
        EXPECT_TRUE(policyUnresolvedGone) << "策略装载后 L11 策略不可解析阻断未解除";
        bool nameUnresolvedEvaluated = false;
        for (const auto& blocker : module.session().readiness->blockers) {
            if (blocker.code.find("NAME-UNRESOLVED") != std::string::npos) {
                nameUnresolvedEvaluated = true;
            }
        }
        EXPECT_TRUE(nameUnresolvedEvaluated)
            << "策略装载后行程评估未产出名称不可解析诊断（L 行未真执行）";
    }

    // ③传动配置编辑（为会话工作集挂载合法传动对象——L9"传动设计未配置"
    // 缺省预告消除）触发重算：缺项键集相对②收缩，汇聚快照与模块直读同拍。
    // （模板出厂物性缺失走 layerWarnings 呈现面、不入 notes 派生键——L5
    // 断言/预告分流见 Readiness.cpp runL5；缺项键的唯一注源＝非阻断 note。）
    {
        auto& ws = module.session();
        const core::ObjectId dtOid = core::ObjectId::generate();
        DrivetrainDesign dt;  // 六关节逐一传动比＋摩擦（BridgeFixtures 同款构造）
        dt.objectId = dtOid;
        for (std::size_t i = 0; i < ws.draft.design.joints.size(); ++i) {
            dt.ratioPerJoint.push_back(
                core::SourcedValue<double>::provided(100.0, userProvenance()));
            FrictionEntry f;
            f.viscous = core::SourcedValue<double>::provided(0.5, userProvenance());
            f.coulomb = core::SourcedValue<double>::provided(1.0, userProvenance());
            f.bias = core::SourcedValue<double>::notProvided();
            dt.frictionPerJoint.push_back(f);
        }
        ws.draft.drivetrainObject = dt;
        ws.draft.changes.push_back(
            ModelingChangeRecord{"drivetrain", "配置传动（T03B 快照联动用例）"});
        module.recomputeReadiness();
        const auto snap = model->readinessSnapshot(ui::StageId::Modeling);
        const auto direct = module.readonlyProjections();
        ASSERT_EQ(snap.domains.size(), std::size_t{1});
        EXPECT_EQ(snap.domains.front().inputComplete, direct.front().inputComplete);
        EXPECT_EQ(snap.domains.front().missingItemKeys.size(),
                  direct.front().missingItemKeys.size());
        EXPECT_LT(snap.domains.front().missingItemKeys.size(), keysAfterPolicyLoad)
            << "传动配置后缺项键集未收缩（编辑未反映进快照——ACC5 陈旧缓存）";
    }

    // ④传动清退编辑（drivetrainObject 回落未配置——缺省预告回归）触发
    // 重算：缺项键集相对③增长（编辑后缺项键集变化的正向半区）。
    {
        auto& ws = module.session();
        ws.draft.drivetrainObject.reset();
        ws.draft.changes.push_back(
            ModelingChangeRecord{"drivetrain", "清退传动（T03B 快照联动用例）"});
        module.recomputeReadiness();
        const auto snap = model->readinessSnapshot(ui::StageId::Modeling);
        const auto direct = module.readonlyProjections();
        ASSERT_EQ(snap.domains.size(), std::size_t{1});
        EXPECT_EQ(snap.domains.front().missingItemKeys.size(),
                  direct.front().missingItemKeys.size());
        EXPECT_EQ(snap.domains.front().missingItemKeys.size(), keysAfterPolicyLoad)
            << "传动清退后缺项键集未回到基线（预告 note 未派生缺项键）";
        // 逐键与模块直读一致（同源同拍——ACC5 零缓存）。
        const auto& keys = snap.domains.front().missingItemKeys;
        const auto& directKeys = direct.front().missingItemKeys;
        for (std::size_t i = 0; i < keys.size() && i < directKeys.size(); ++i) {
            EXPECT_EQ(keys[i], directKeys[i]) << "键 " << i << " 不同";
        }
    }
}
