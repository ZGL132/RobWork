/**
 * @file   PreflightTest.cpp
 * @brief  优化预检 Preflight 模型测试（WP-20-T08）——20 检查项逐项定位、
 *         allowances 五元组、OptimizationRunSpec fail-fast 面、P-OPT-6
 *         会话态承载（纯函数只读）。
 *
 * 追溯：OPT-11（Preflight/基线评估）、REQUIREMENTS §15 OPT-11 行、
 * units/optimization.md §6.4（检查项全表）/§4.3（运行描述）/§16.3
 * P-OPT-6；测试矩阵 OPT-VER-112（输入前置阻断）/113（拓扑非法）/
 * 135（Preflight 阻塞项）/136（Preflight 警告项）/201（P-04 未冻结）。
 * 契约 acceptance 1（五元组可定位）/3（P-OPT-6 会话态＋ird_gates 零命中
 * ——留痕面）。
 *
 * 消费口径（T03 B-1 教训钉扎）：全部经 IOptimizationPreflightService
 * **虚接口**消费——不留只测自由函数/内部函数的盲区。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Evaluator.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/optimization/DiagCodes.hpp>
#include <sdurws/ird/optimization/Objective.hpp>
#include <sdurws/ird/optimization/Preflight.hpp>
#include <sdurws/ird/optimization/Variable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <string>
#include <vector>

namespace optimization = sdurws::ird::optimization;
namespace evidence = sdurws::ird::evidence;
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 夹具辅助（T06 EvaluatorPortsTest 同款形态——真注册表＋脚本化投影面）
// =====================================================================

/// 修订闭包真值源（全部 (oid,cv) 存在——测试组装的合法入口；T06 同款）。
struct AllPresentClosure final : public evidence::IRevisionClosureSource {
    bool objectInRevision(core::RevisionId, core::ObjectId,
                          core::ContentVersion) const override
    {
        return true;
    }
};

/// 非零内容身份（测试构造——避免保留值）。
core::ContentIdentity makeContentIdentity(unsigned char seed)
{
    core::ContentIdentity id;
    id.bytes.fill(seed);
    return id;
}

/// 闭包条目（对象身份随机、内容版本固定形态、digest=cv 字节——builder
/// 一致性闸门要求 digest 与 contentVersion 同源）。
evidence::ObjectRefEntry makeClosureEntry(std::string typeToken)
{
    evidence::ObjectRefEntry entry;
    entry.objectId = core::ObjectId::generate();
    entry.contentVersion = core::ContentVersion::fromCanonical(
        "cv-0000000000000000000000000000000000000000000000000000000000000001");
    entry.objectTypeToken = std::move(typeToken);
    entry.digest = entry.contentVersion.bytes;
    return entry;
}

/// 两必验工况（enabled+mandatory——覆盖矩阵分母；检查 6 的正例面）。
std::vector<evidence::CaseEntry> makeTwoMandatoryCases()
{
    return {{core::ObjectId::generate(), "额定工况", true, true},
            {core::ObjectId::generate(), "极限工况", true, true}};
}

/// 组装"工件齐全"的冻结快照：modeling 两工件（robot-design/robot-
/// drivetrain）＋requirements 五对象（req-set＋四集合）＋已解析策略＋
/// 名称映射＋必验工况。检查 4/5/8 的正例基线（缺项场景在用例内移除）。
evidence::AnalysisSnapshot makeCompleteSnapshot(
    const std::vector<evidence::CaseEntry>& cases = makeTwoMandatoryCases())
{
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    for (const std::string_view token :
         {optimization::kObjectTypeRobotDesign,
          optimization::kObjectTypeRobotDrivetrain,
          optimization::kObjectTypeReqRootSet,
          optimization::kObjectTypeReqPointSet,
          optimization::kObjectTypeReqRegionSet,
          optimization::kObjectTypeReqConditionSet,
          optimization::kObjectTypeReqPlanSet}) {
        builder.addObjectRef(makeClosureEntry(std::string(token)));
    }
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

/// 注册 "opt" v1.0 Profile（空必需项集——完备性核对平凡通过；T06 kin 同款
/// 精简。Profile 先于评估器注册——§6.7 顺序约束）。
void registerOptProfile(evidence::EvidenceProfileRegistry& profiles)
{
    evidence::RequiredEvidenceProfile profile;
    profile.profileId = std::string(optimization::kOptProfileId);
    profile.version = "1.0";
    profiles.registerProfile(profile);
}

/// opt-static-screen 描述符（§6.7 表：契约版本 1、modes={Quick,Verified}、
/// stateless——R1 声明不含 Preview ⇒ allowPreview 恒 false）。
evidence::EvaluatorDescriptor makeOptStaticScreenDescriptor()
{
    evidence::EvaluatorDescriptor d;
    d.key = std::string(optimization::kOptStaticScreenKey);
    d.contractVersion = optimization::kOptStaticScreenContractVersion;
    d.profile.profileId = std::string(optimization::kOptProfileId);
    d.profile.version = "1.0";
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::FullyThreadSafe;
    return d;
}

/// 空 evaluate 替身评估器（Preflight 只查注册清单 descriptor——评估行为
/// 不在本面；T06 ScriptedKin 同款精简）。
class NopEvaluator final : public evidence::IEngineeringEvaluator {
public:
    explicit NopEvaluator(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    evidence::EvaluationOutput evaluate(const evidence::EvaluationRequest&,
                                        evidence::IEvaluationContext&) override
    {
        return {};
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

class NopFactory final : public evidence::IEvaluatorFactory {
public:
    explicit NopFactory(evidence::EvaluatorDescriptor descriptor)
        : m_descriptor(std::move(descriptor))
    {
    }
    const evidence::EvaluatorDescriptor& descriptor() const override
    {
        return m_descriptor;
    }
    std::unique_ptr<evidence::IEngineeringEvaluator> create() const override
    {
        return std::make_unique<NopEvaluator>(m_descriptor);
    }

private:
    evidence::EvaluatorDescriptor m_descriptor;
};

/// Preflight 装配夹具（真注册表——Profile 先行＋opt-static-screen 注册；
/// 注册表指针与注册表生存期同域——成员声明序保证）。
struct PreflightFixture {
    evidence::EvidenceProfileRegistry profiles;
    evidence::EvaluatorRegistry evaluators{profiles};
    core::ContentIdentity optProfileIdentity{};

    PreflightFixture()
    {
        registerOptProfile(profiles);
        optProfileIdentity
            = profiles.findProfile(std::string(optimization::kOptProfileId), "1.0")
                  ->contentIdentity;
        evaluators.registerEvaluator(
            std::make_unique<NopFactory>(makeOptStaticScreenDescriptor()), {});
    }

    /// 合法检查输入（基线投影与研究身份一致——检查 1/2/13 的正例面）。
    optimization::PreflightInputs makeInputs(const evidence::AnalysisSnapshot& s) const
    {
        optimization::PreflightInputs inputs;
        inputs.snapshot = s;
        inputs.baseline.branch = s.branch;
        inputs.baseline.tip = s.revision;
        inputs.baseline.writable = true;
        inputs.evaluatorRegistry = &evaluators;
        inputs.profileRegistry = &profiles;
        inputs.baselineChainInEnabledScope = true;
        inputs.p04Frozen = false;
        return inputs;
    }
};

/// 合法运行描述（身份与快照一致、Profile 指向注册权威、config＝StageB
/// 默认三项静态目标＋单连续绑定——20 检查项的正例基线）。
optimization::OptimizationRunSpec makeSpec(const evidence::AnalysisSnapshot& s,
                                           const PreflightFixture& f)
{
    optimization::OptimizationRunSpec spec;
    spec.project = s.project;
    spec.branch = s.branch;
    spec.revision = s.revision;
    spec.snapshotId = s.snapshotId;
    spec.config.stage = optimization::OptimizationStage::StageB;
    spec.config.seed = 7;
    spec.config.objectives
        = optimization::defaultObjectives(optimization::OptimizationStage::StageB);
    optimization::VariableBinding binding;
    binding.bindingId = "mdl.joint[2].dh.a";
    binding.kind = optimization::VariableKind::Continuous;
    binding.unit = core::UnitToken::find("m").value();
    binding.lowerBound = 0.2;   // m
    binding.upperBound = 0.8;   // m
    binding.defaultValue = 0.5; // m
    binding.authorized = true;
    binding.locked = false;
    binding.authorityFieldPath = "robot-design/joints[2]/dh/a";
    // diagSubject 必须指向快照闭包内对象（T03 绑定校验的闭包核对——随机
    // 身份会产生检查 3 的悬空定位阻塞，污染正例基线）。
    for (const auto& entry : s.objectClosure) {
        if (entry.objectTypeToken == optimization::kObjectTypeRobotDesign) {
            binding.diagSubject = entry.objectId.toCanonical();
            break;
        }
    }
    spec.config.variables = {binding};
    spec.profile.profileId = std::string(optimization::kOptProfileId);
    spec.profile.version = "1.0";
    spec.profile.contentIdentity = f.optProfileIdentity;
    spec.createdBy = "preflight-test";
    return spec;
}

/// 按检查项编号＋级别查找发现（定位断言的辅助）。
const optimization::PreflightFinding*
findFinding(const optimization::PreflightReport& report, std::uint8_t checkId,
            optimization::PreflightLevel level)
{
    for (const auto& f : report.findings) {
        if (f.checkId == checkId && f.level == level) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace

// =====================================================================
// 正例：工件齐全的研究一次通过（acceptance 1 的基线面）
// =====================================================================

TEST(OptPreflight, CleanStudyPassesWithAllowances_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    // 经 IOptimizationPreflightService 虚接口消费（接口路径钉扎——T03 B-1 教训）。
    optimization::OptimizationPreflightService impl(f.makeInputs(snapshot));
    const optimization::IOptimizationPreflightService& svc = impl;
    const auto report = svc.preflight(makeSpec(snapshot, f));

    // 全齐研究：零阻塞零警告；findings 空。
    EXPECT_EQ(report.blockerCount, 0U);
    EXPECT_EQ(report.warningCount, 0U);
    EXPECT_TRUE(report.findings.empty());
    // allowances 五元组：启动/Quick/Verified/正式导出全放开。
    EXPECT_TRUE(report.allowances.allowStart);
    EXPECT_TRUE(report.allowances.allowQuick);
    EXPECT_TRUE(report.allowances.allowVerified);
    EXPECT_TRUE(report.allowances.allowFormalExport);
    // R1 opt-static-screen 声明 {Quick,Verified} 不含 Preview ⇒ allowPreview
    // 恒 false（按注册 descriptor 事实查询——不伪造预览能力）。
    EXPECT_FALSE(report.allowances.allowPreview);
    // 评估器集摘要已登记（EvaluatorSetId 承载——非保留值）。
    EXPECT_TRUE(report.evaluatorSetDigest.isValid());
}

// =====================================================================
// 检查 1/2：修订可读与写集冲突（§6.4 行 1/2——PM-04/CON-01）
// =====================================================================

TEST(OptPreflight, StaleTipAndMissingBranchLocated_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));

    // 写集冲突：基线投影 tip 已前进（≠ 研究修订）→ 检查 2 阻塞，建议中
    // 携带 PRJ-STALE-REVISION-REJECTED 风险码名（卡面 #2 括号语义）。
    auto inputs = f.makeInputs(snapshot);
    inputs.baseline.tip = core::RevisionId::generate();
    optimization::OptimizationPreflightService staleSvc(inputs);
    auto report = staleSvc.preflight(makeSpec(snapshot, f));
    const auto* stale = findFinding(report, optimization::kPreflightCheckWriteSetConflict,
                                    optimization::PreflightLevel::Blocking);
    ASSERT_NE(stale, nullptr) << "tip 前进必须产生检查 2 阻塞";
    EXPECT_EQ(stale->basis, "PM-04");
    EXPECT_NE(stale->suggestion.find(optimization::kPrjStaleRevisionRejectedCode),
              std::string::npos)
        << "建议必须提示 StaleRevisionRejected 风险";
    EXPECT_FALSE(report.allowances.allowQuick);
    EXPECT_EQ(report.blockerCount, 1U);

    // 分支失配：基线投影自另一分支取得 → 检查 1 阻塞（CON-01、②端口）。
    auto inputs2 = f.makeInputs(snapshot);
    inputs2.baseline.branch = core::BranchId::generate();
    optimization::OptimizationPreflightService branchSvc(inputs2);
    auto report2 = branchSvc.preflight(makeSpec(snapshot, f));
    const auto* unreadable
        = findFinding(report2, optimization::kPreflightCheckRevisionReadable,
                      optimization::PreflightLevel::Blocking);
    ASSERT_NE(unreadable, nullptr) << "分支失配必须产生检查 1 阻塞";
    EXPECT_EQ(unreadable->basis, "CON-01、②端口");
}

// =====================================================================
// 检查 16：资源预算不合法走报告轨（§4.3——不抛异常，逐项定位）
// =====================================================================

TEST(OptPreflight, BudgetInvalidReportedNotThrown_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);
    spec.config.seed = 0;  // I-OPT-2：种子 0 非法——Preflight 面应报告不抛

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    // 报告轨：预算缺陷逐项定位（一次给全缺项清单——UX-10 同型），不抛。
    auto report = svc.preflight(spec);
    const auto* budget = findFinding(report, optimization::kPreflightCheckBudgetInvalid,
                                     optimization::PreflightLevel::Blocking);
    ASSERT_NE(budget, nullptr) << "种子 0 必须以检查 16 阻塞定位（报告轨）";
    EXPECT_EQ(budget->basis, "§4.3");
    EXPECT_NE(budget->suggestion.find("≥1"), std::string::npos)
        << "建议必须携带合法域说明（ERR-01 比较型定位）";
    EXPECT_FALSE(report.allowances.allowStart);

    // 独立 fail-fast 面对照：validateRunSpec 直接消费时同缺陷抛异常
    // （组装面快速失败——validateRunSpec 注"双轨分工"）。
    bool thrown = false;
    try {
        optimization::validateRunSpec(spec);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(thrown) << "validateRunSpec 独立消费必须 fail-fast";
}

// =====================================================================
// 检查 3/14：未登记绑定与不支持的变量（§5.2/§5.7——OPT-VER-112/108）
// =====================================================================

TEST(OptPreflight, UnregisteredAndStageLockedBindings_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "OPT-02"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);

    // 未登记绑定（词表无匹配 token）→ 检查 3 阻塞（subject＝绑定 token）。
    optimization::VariableBinding unknown;
    unknown.bindingId = "mdl.unknown.field";
    // StageB 激活电机型号绑定（OPT-VER-108 观测点 Preflight #10/#14）→
    // 检查 14 阻塞（阶段锁——不降级不丢弃）。
    optimization::VariableBinding motor;
    motor.bindingId = "mdl.drivetrain.motor-key[1]";
    motor.kind = optimization::VariableKind::DiscreteDevice;
    motor.authorized = true;
    motor.locked = false;
    // 1b 截面类型（词表登记但两阶段均未启用——P-OPT-4）：T03 校验面对
    // 未启用条目统一产 kOptStageLocked（阶段锁阻塞——不降级不丢弃），
    // 经本服务分派为检查 14 阻塞。§6.4 #14 的警告分支（"绑定存在但未
    // 激活"，kOptVarUnbindable 语义位）为 T03 BindingValidationReport 的
    // 分级预留——其产出需要基线事实核验（Mesh 基线截面等），随 P-OPT-2
    // 裁决落位；R1 校验面无产出点（诚实登记，本用例不伪造警告断言）。
    optimization::VariableBinding sectionType;
    sectionType.bindingId = "mdl.link[2].section.type";
    sectionType.kind = optimization::VariableKind::Enumeration;
    sectionType.enumValues = {"SolidCylinder", "HollowCylinder", "Box"};
    sectionType.defaultValueIndex = 0;
    sectionType.authorized = true;
    sectionType.locked = false;
    spec.config.variables.push_back(unknown);
    spec.config.variables.push_back(motor);
    spec.config.variables.push_back(sectionType);

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(spec);

    const auto* unregistered
        = findFinding(report, optimization::kPreflightCheckUnregisteredBinding,
                      optimization::PreflightLevel::Blocking);
    ASSERT_NE(unregistered, nullptr);
    EXPECT_EQ(unregistered->subject, "mdl.unknown.field");
    EXPECT_EQ(unregistered->basis, "§5.2、I-OPT-7");

    const auto* stageLocked = findFinding(
        report, optimization::kPreflightCheckUnsupportedVariable,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(stageLocked, nullptr) << "StageB 电机型号必须阻塞（阶段锁）";
    EXPECT_EQ(stageLocked->subject, "mdl.drivetrain.motor-key[1]");
    EXPECT_EQ(stageLocked->basis, "§5.3/§5.7");

    EXPECT_FALSE(report.allowances.allowQuick);
    // 检查 14 同编号可携带多条发现（阶段锁阻塞逐绑定定位——1b 截面与
    // 电机型号各一条）。
    std::size_t blockedVariables = 0;
    for (const auto& finding : report.findings) {
        if (finding.checkId == optimization::kPreflightCheckUnsupportedVariable
            && finding.level == optimization::PreflightLevel::Blocking) {
            ++blockedVariables;
        }
    }
    EXPECT_EQ(blockedVariables, 2U)
        << "电机型号与 1b 截面类型各产生一条检查 14 阻塞";
}

// =====================================================================
// 检查 10/17：阶段能力冲突与目标不可计算（OPT-03/§15.0/OPT-07）
// =====================================================================

TEST(OptPreflight, StageLockedObjectiveLocated_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "OPT-07"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);
    // StageB 激活节拍目标（§7.3 行 1——StageB 仅 1~3 项）→ 检查 10 阻塞
    // （kOptStageLocked 来源；不静默降级为可算子集——§6.5）。
    std::vector<optimization::ObjectiveEntry> entries
        = spec.config.objectives.entries;
    optimization::ObjectiveEntry cycle;
    cycle.metricId = optimization::MetricId::CycleTime;  // 指标方向＝词表权威
    entries.push_back(cycle);
    spec.config.objectives
        = optimization::makeObjectiveSet(entries);

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(spec);
    const auto* locked = findFinding(report,
                                     optimization::kPreflightCheckStageCapability,
                                     optimization::PreflightLevel::Blocking);
    ASSERT_NE(locked, nullptr) << "StageB 激活节拍必须产生检查 10 阻塞";
    EXPECT_EQ(locked->subject, "opt.metric.cycle-time");
    EXPECT_EQ(locked->basis, "OPT-03/§15.0");
    EXPECT_FALSE(report.allowances.allowStart);
}

// =====================================================================
// 检查 4/5：缺失 modeling/requirements 工件（OPT-VER-112 观测点 #4/#5）
// =====================================================================

TEST(OptPreflight, MissingArtifactsLocated_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    // 快照缺 robot-drivetrain 与 req-point-set（闭包重组装——只含其余工件）。
    evidence::SnapshotBuilder builder;
    builder.setIdentity(core::ProjectId::generate(), core::BranchId::generate(),
                        core::RevisionId::generate(), 1U);
    for (const std::string_view token :
         {optimization::kObjectTypeRobotDesign,
          optimization::kObjectTypeReqRootSet,
          optimization::kObjectTypeReqRegionSet,
          optimization::kObjectTypeReqConditionSet,
          optimization::kObjectTypeReqPlanSet}) {
        builder.addObjectRef(makeClosureEntry(std::string(token)));
    }
    evidence::PolicyRef policy;
    policy.policyContentIdentity = makeContentIdentity(4);
    builder.setPolicyRef(policy);
    evidence::NameMapRef nameMap;
    nameMap.nameMapContentIdentity = makeContentIdentity(5);
    builder.setNameMapRef(nameMap);
    for (const auto& c : makeTwoMandatoryCases()) {
        builder.addCase(c);
    }
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    const auto snapshot = builder.build(AllPresentClosure{});

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(makeSpec(snapshot, f));

    // 缺传动工件 → 检查 4 阻塞，subject＝缺失的 token（逐项定位）。
    const auto* drivetrain = findFinding(
        report, optimization::kPreflightCheckModelingArtifacts,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(drivetrain, nullptr);
    EXPECT_EQ(drivetrain->subject, "robot-drivetrain");
    EXPECT_EQ(drivetrain->basis, "§5.3、P-MDL-6");
    // 缺任务点集合 → 检查 5 阻塞（根集/其余三集合在场不误报）。
    const auto* points = findFinding(
        report, optimization::kPreflightCheckRequirementArtifacts,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(points, nullptr);
    EXPECT_EQ(points->subject, "req-point-set");
    EXPECT_EQ(points->basis, "requirements.md §8.2");
    EXPECT_FALSE(report.allowances.allowQuick);
}

// =====================================================================
// 检查 6：必验工况为空/全禁用（EVI-02/REQ-06——P-EV-7 同源保守）
// =====================================================================

TEST(OptPreflight, DisabledRequiredCasesBlock_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "EVI-02"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    // 全禁用工况（条目在场但 enabled=false——"全禁用"分支）。
    const auto snapshot = makeCompleteSnapshot(
        {{core::ObjectId::generate(), "停用工况", false, false}});

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(makeSpec(snapshot, f));
    const auto* cases = findFinding(report,
                                    optimization::kPreflightCheckRequiredCases,
                                    optimization::PreflightLevel::Blocking);
    ASSERT_NE(cases, nullptr) << "全禁用必验集必须阻塞（保守口径）";
    EXPECT_EQ(cases->basis, "EVI-02、REQ-06");
    // allowVerified 的"必验工况非空"条件独立成立（§6.4 冗余防御）。
    EXPECT_FALSE(report.allowances.allowVerified);
}

// =====================================================================
// 检查 7：缺少评估器/契约版本不符（OPT-VER-135 人为缺评估器面）
// =====================================================================

TEST(OptPreflight, MissingEvaluatorAndContractMismatch_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();

    // 注册表整体缺失（未装配）→ 检查 7 阻塞（如实报告缺项——注册表可空）。
    auto inputs = f.makeInputs(snapshot);
    inputs.evaluatorRegistry = nullptr;
    optimization::OptimizationPreflightService noRegistry(inputs);
    auto report = noRegistry.preflight(makeSpec(snapshot, f));
    const auto* missing = findFinding(report,
                                      optimization::kPreflightCheckEvaluatorMissing,
                                      optimization::PreflightLevel::Blocking);
    ASSERT_NE(missing, nullptr);
    EXPECT_EQ(missing->subject, "opt-static-screen");
    EXPECT_EQ(missing->basis, "§6.1、EvaluatorSetId");
    EXPECT_FALSE(report.evaluatorSetDigest.isValid())
        << "注册表缺失时摘要为保留值（无从计算——不伪造）";

    // 契约版本不符（注册 contractVersion=2 ≠ §6.7 声明的 1）→ 比较型定位。
    evidence::EvaluatorDescriptor stale = makeOptStaticScreenDescriptor();
    stale.contractVersion = 2U;
    evidence::EvidenceProfileRegistry profiles2;
    registerOptProfile(profiles2);
    evidence::EvaluatorRegistry evaluators2{profiles2};
    evaluators2.registerEvaluator(std::make_unique<NopFactory>(stale), {});
    auto inputs2 = f.makeInputs(snapshot);
    inputs2.evaluatorRegistry = &evaluators2;
    inputs2.profileRegistry = &profiles2;
    optimization::OptimizationPreflightService staleSvc(inputs2);
    auto report2 = staleSvc.preflight(makeSpec(snapshot, f));
    const auto* mismatch = findFinding(
        report2, optimization::kPreflightCheckEvaluatorMissing,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(mismatch, nullptr);
    EXPECT_NE(mismatch->suggestion.find("契约版本"), std::string::npos)
        << "建议必须比较型定位（期望 1 / 实际 2）";
}

// =====================================================================
// 检查 8/9：缺少 policy 与 RequiredEvidenceProfile（CON-06/EVI-01）
// =====================================================================

TEST(OptPreflight, MissingPolicyAndProfile_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);

    // 检查 8（缺少 policy——CON-06）：preflight 的检查输入可能来自任意
    // 来源（如反序列化恢复面），对"绕过 SnapshotBuilder 的损坏快照"做
    // 防御性复验——直接构造 policyRef 保留值的快照实例（AnalysisSnapshot
    // 是 plain struct，冻结纪律约束生产路径；检查面对任意输入如实报告）。
    auto brokenSnapshot = snapshot;
    brokenSnapshot.policyRef.policyContentIdentity = core::ContentIdentity{};
    optimization::OptimizationPreflightService brokenSvc(f.makeInputs(brokenSnapshot));
    const auto brokenReport = brokenSvc.preflight(spec);
    const auto* policy
        = findFinding(brokenReport, optimization::kPreflightCheckPolicyMissing,
                      optimization::PreflightLevel::Blocking);
    ASSERT_NE(policy, nullptr) << "policyRef 保留值必须阻塞（CON-06）";
    EXPECT_EQ(policy->basis, "CON-06");

    // 检查 9（缺少 RequiredEvidenceProfile——EVI-01 表 4）：注册表未装配
    // → "opt" Profile 不可解析 → 阻塞，subject＝profileId/version。
    auto inputs = f.makeInputs(snapshot);
    inputs.profileRegistry = nullptr;
    optimization::OptimizationPreflightService svc(inputs);
    const auto report = svc.preflight(spec);
    const auto* profile = findFinding(report,
                                      optimization::kPreflightCheckProfileMissing,
                                      optimization::PreflightLevel::Blocking);
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->subject, "opt/1.0");
    EXPECT_EQ(profile->basis, "EVI-01 表 4");

    // Profile 身份漂移（spec 引用 ≠ 注册表权威）→ 检查 9 阻塞（身份不符）。
    auto spec2 = makeSpec(snapshot, f);
    spec2.profile.contentIdentity = makeContentIdentity(9);
    optimization::OptimizationPreflightService svc2(f.makeInputs(snapshot));
    const auto report2 = svc2.preflight(spec2);
    const auto* drift = findFinding(report2,
                                    optimization::kPreflightCheckProfileMissing,
                                    optimization::PreflightLevel::Blocking);
    ASSERT_NE(drift, nullptr) << "Profile 身份漂移必须阻塞";
    EXPECT_NE(drift->suggestion.find("权威身份"), std::string::npos);
}

// =====================================================================
// 检查 7/11/12：StageD 能力面（OPT-VER-201——P-04 未冻结即阻塞）
// =====================================================================

TEST(OptPreflight, StageDBlockedByP04AndJointEvaluator_WP20T08_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "OPT-09"},
                  std::vector<std::string>{"AT-35"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);
    spec.config.stage = optimization::OptimizationStage::StageD;
    spec.config.objectives
        = optimization::defaultObjectives(optimization::OptimizationStage::StageD);

    // R1 期 p04Frozen 恒 false——StageD 正式联合优化恒阻塞：
    //   #7（联合评估器 WP-21 登记前不可用）＋ #11（P-04 未冻结）。
    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(spec);
    EXPECT_NE(findFinding(report, optimization::kPreflightCheckEvaluatorMissing,
                          optimization::PreflightLevel::Blocking),
              nullptr)
        << "StageD 联合评估器未登记必须阻塞（OPT-EVALUATOR-MISSING 语义）";
    const auto* p04 = findFinding(report, optimization::kPreflightCheckP04NotFrozen,
                                  optimization::PreflightLevel::Blocking);
    ASSERT_NE(p04, nullptr) << "P-04 未冻结请求 OPT-D 必须阻塞（OPT-VER-201）";
    EXPECT_EQ(p04->basis, "附录 C（O-28）、OPT-09");
    EXPECT_FALSE(report.allowances.allowStart);

    // 离散绑定目录值域缺失 → 检查 12 阻塞（SEL-08）。
    optimization::VariableBinding motor;
    motor.bindingId = "mdl.drivetrain.motor-key[1]";
    motor.kind = optimization::VariableKind::DiscreteDevice;
    motor.authorized = true;
    motor.locked = false;
    spec.config.variables.push_back(motor);
    const auto report2 = svc.preflight(spec);
    const auto* catalog = findFinding(
        report2, optimization::kPreflightCheckCatalogMissing,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(catalog, nullptr);
    EXPECT_EQ(catalog->subject, "mdl.drivetrain.motor-key[1]");
    EXPECT_EQ(catalog->basis, "SEL-08");
}

// =====================================================================
// 检查 13：输入身份不一致（§4.4 I-OPT-1）
// =====================================================================

TEST(OptPreflight, IdentityMismatchDetected_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);
    spec.revision = core::RevisionId::generate();  // 与快照锚定修订失配

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(spec);
    const auto* mismatch = findFinding(
        report, optimization::kPreflightCheckIdentityMismatch,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(mismatch, nullptr) << "修订失配必须阻塞（I-OPT-1 输入冻结）";
    EXPECT_EQ(mismatch->basis, "§4.4 I-OPT-1");
}

// =====================================================================
// 检查 15：不支持的链型（OPT-VER-113——§2.1/MDL-12）
// =====================================================================

TEST(OptPreflight, UnsupportedChainBlocks_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "OPT-03"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto inputs = f.makeInputs(snapshot);
    inputs.baselineChainInEnabledScope = false;  // 基线含 prismatic（R1 场景）

    optimization::OptimizationPreflightService svc(inputs);
    const auto report = svc.preflight(makeSpec(snapshot, f));
    const auto* chain = findFinding(report,
                                    optimization::kPreflightCheckUnsupportedChain,
                                    optimization::PreflightLevel::Blocking);
    ASSERT_NE(chain, nullptr) << "R1 基线含 prismatic 必须阻塞（OPT-VER-113）";
    EXPECT_EQ(chain->basis, "§2.1 支持矩阵、MDL-12");
    EXPECT_FALSE(report.allowances.allowQuick);
}

// =====================================================================
// 检查 18：外部资源未固化（CON-03/PM-01——allowVerified 联动）
// =====================================================================

TEST(OptPreflight, UnsolidifiedExternalResourceBlocks_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "CON-03"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    // 快照冻结后不可变——外部资源面经重新组装注入（Recorded 态资源）。
    evidence::SnapshotBuilder builder;
    builder.setIdentity(snapshot.project, snapshot.branch, snapshot.revision, 1U);
    for (const auto& entry : snapshot.objectClosure) {
        builder.addObjectRef(entry);
    }
    builder.setPolicyRef(snapshot.policyRef);
    builder.setNameMapRef(snapshot.nameMapRef);
    for (const auto& c : snapshot.caseSet.entries) {
        builder.addCase(c);
    }
    evidence::ExternalResourceState resource;
    resource.resourceId = core::ObjectId::generate();
    resource.state = evidence::ExternalResourceStatus::Recorded;  // 未固化
    builder.addExternalResource(resource);
    evidence::ReproductionBlock repro;
    repro.productVersion = "test";
    repro.evidenceContractVersion = "1";
    builder.setReproduction(repro);
    const auto snapshotWithResource = builder.build(AllPresentClosure{});

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshotWithResource));
    const auto report = svc.preflight(makeSpec(snapshotWithResource, f));
    const auto* external = findFinding(
        report, optimization::kPreflightCheckExternalResource,
        optimization::PreflightLevel::Blocking);
    ASSERT_NE(external, nullptr) << "Recorded 外部资源必须阻塞（CON-03）";
    EXPECT_EQ(external->basis, "CON-03、PM-01");
    EXPECT_NE(external->subject.find("obj-"), std::string::npos)
        << "subject 必须定位到资源对象（obj- 规范文本）";
    EXPECT_FALSE(report.allowances.allowVerified);
}

// =====================================================================
// 检查 19：预估候选量超出预算（OPT-VER-136——警告不阻塞）
// =====================================================================

TEST(OptPreflight, EnumEstimateOverBudgetWarns_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    auto spec = makeSpec(snapshot, f);
    spec.config.budget.maxCandidates = 8;  // 预算 8 < 组合数 15（5 材料×3 预设）
    // 复核预算同步收缩（幸存集上界 ≤ 候选总预算——检查 16 的预算自洽面；
    // 本用例只应触发 #19 警告，预算矛盾会以 #16 阻塞污染计数）。
    spec.config.budget.maxVerifiedCandidates = 2;
    // 全枚举采样维度：材料（5 值）＋基座预设（3 值）——组合空间有限。
    optimization::VariableBinding material;
    material.bindingId = "mdl.link[2].material";
    material.kind = optimization::VariableKind::Enumeration;
    material.enumValues = {"steel", "aluminum", "cast-iron", "titanium-alloy",
                           "engineering-plastic"};
    material.defaultValueIndex = 0;
    material.authorized = true;
    material.locked = false;
    optimization::VariableBinding preset;
    preset.bindingId = "mdl.base.orientation.preset";
    preset.kind = optimization::VariableKind::Enumeration;
    preset.enumValues = {"ground", "inverted", "wall"};
    preset.defaultValueIndex = 0;
    preset.authorized = true;
    preset.locked = false;
    spec.config.variables = {material, preset};

    optimization::OptimizationPreflightService svc(f.makeInputs(snapshot));
    const auto report = svc.preflight(spec);
    const auto* estimate = findFinding(
        report, optimization::kPreflightCheckEstimateOverBudget,
        optimization::PreflightLevel::Warning);
    ASSERT_NE(estimate, nullptr) << "15 > 8 必须产生检查 19 警告";
    EXPECT_EQ(estimate->basis, "§4.3");
    EXPECT_NE(estimate->suggestion.find("15"), std::string::npos)
        << "警告须携带估算值（比较型定位）";
    // 警告不阻塞：allowStart/allowQuick 仍为 true（截断并计数语义）。
    EXPECT_EQ(report.blockerCount, 0U);
    EXPECT_TRUE(report.allowances.allowQuick);

    // 连续维度研究（LHS 预算内采样）不触发——无有限组合上限语义。
    auto spec2 = makeSpec(snapshot, f);
    spec2.config.budget.maxCandidates = 1;  // 只装基线——连续维度不估算
    const auto report2 = svc.preflight(spec2);
    EXPECT_EQ(findFinding(report2, optimization::kPreflightCheckEstimateOverBudget,
                          optimization::PreflightLevel::Warning),
              nullptr)
        << "连续/量化维度的 LHS 研究不触发 #19（无组合上限语义）";
}

// =====================================================================
// 检查 20：版本兼容提示（EV-COV-4——警告不可直接比较）
// =====================================================================

TEST(OptPreflight, VersionCompatibilityHints_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "EVI-02"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    const auto spec = makeSpec(snapshot, f);

    // 上次运行身份与本次一致 → 无兼容警告。
    auto sameInputs = f.makeInputs(snapshot);
    optimization::LastRunIdentity same;
    same.evaluatorSetDigest = sameInputs.evaluatorRegistry->manifest().digest;
    same.profileContentIdentity = f.optProfileIdentity;
    sameInputs.lastRun = same;
    optimization::OptimizationPreflightService sameSvc(sameInputs);
    const auto sameReport = sameSvc.preflight(spec);
    EXPECT_EQ(findFinding(sameReport,
                          optimization::kPreflightCheckVersionCompatibility,
                          optimization::PreflightLevel::Warning),
              nullptr)
        << "同装配双跑不产生兼容警告";

    // 评估器集摘要漂移（装配清单变化）→ 检查 20 警告（评估器集面）。
    auto driftedInputs = f.makeInputs(snapshot);
    optimization::LastRunIdentity drifted;
    drifted.evaluatorSetDigest = makeContentIdentity(1);  // ≠ 本次 manifest
    drifted.profileContentIdentity = f.optProfileIdentity;
    driftedInputs.lastRun = drifted;
    optimization::OptimizationPreflightService driftedSvc(driftedInputs);
    const auto driftedReport = driftedSvc.preflight(spec);
    const auto* hint = findFinding(
        driftedReport, optimization::kPreflightCheckVersionCompatibility,
        optimization::PreflightLevel::Warning);
    ASSERT_NE(hint, nullptr);
    EXPECT_EQ(hint->subject, "评估器集摘要");
    EXPECT_EQ(hint->basis, "EV-COV-4");
    EXPECT_EQ(driftedReport.blockerCount, 0U) << "兼容提示是警告——不阻塞";

    // Profile 漂移 → 独立警告（分面定位）。
    auto profileDriftInputs = f.makeInputs(snapshot);
    optimization::LastRunIdentity profileDrift;
    profileDrift.evaluatorSetDigest
        = profileDriftInputs.evaluatorRegistry->manifest().digest;
    profileDrift.profileContentIdentity = makeContentIdentity(2);
    profileDriftInputs.lastRun = profileDrift;
    optimization::OptimizationPreflightService profileSvc(profileDriftInputs);
    const auto profileReport = profileSvc.preflight(spec);
    const auto* profileHint = findFinding(
        profileReport, optimization::kPreflightCheckVersionCompatibility,
        optimization::PreflightLevel::Warning);
    ASSERT_NE(profileHint, nullptr);
    EXPECT_EQ(profileHint->subject, "Profile 内容身份");
}

// =====================================================================
// 报告不变式：findings 按 checkId 升序＋计数一致（NFR-COR-02 确定性）
// =====================================================================

TEST(OptPreflight, FindingsSortedAndCountsConsistent_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11", "NFR-COR-02"},
                  std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    // 多缺陷研究：tip 前进（#2）＋链型越界（#15）＋枚举超预算警告（#19）
    // ＋版本漂移警告（#20）——阻塞与警告混合。
    const auto snapshot = makeCompleteSnapshot();
    auto inputs = f.makeInputs(snapshot);
    inputs.baseline.tip = core::RevisionId::generate();
    inputs.baselineChainInEnabledScope = false;
    optimization::LastRunIdentity drifted;
    drifted.evaluatorSetDigest = makeContentIdentity(3);
    drifted.profileContentIdentity = f.optProfileIdentity;
    inputs.lastRun = drifted;
    optimization::OptimizationPreflightService svc(inputs);

    auto spec = makeSpec(snapshot, f);
    spec.config.budget.maxCandidates = 2;
    spec.config.budget.maxVerifiedCandidates = 1;  // 预算自洽（≤ 总预算）
    optimization::VariableBinding material;
    material.bindingId = "mdl.link[2].material";
    material.kind = optimization::VariableKind::Enumeration;
    material.enumValues = {"steel", "aluminum", "cast-iron", "titanium-alloy",
                           "engineering-plastic"};
    material.defaultValueIndex = 0;
    spec.config.variables = {material};
    const auto specSnapshot = spec;  // 值拷贝——双跑同输入

    const auto report = svc.preflight(spec);
    const auto reportAgain = svc.preflight(specSnapshot);
    EXPECT_EQ(report.findings.size(), reportAgain.findings.size());
    for (std::size_t i = 0; i < report.findings.size(); ++i) {
        EXPECT_EQ(report.findings[i].checkId, reportAgain.findings[i].checkId);
        EXPECT_EQ(report.findings[i].subject, reportAgain.findings[i].subject);
    }
    // checkId 升序（报告排序契约）。
    for (std::size_t i = 1; i < report.findings.size(); ++i) {
        EXPECT_LE(report.findings[i - 1].checkId, report.findings[i].checkId);
    }
    // 计数与 findings 一致（不变式）。
    std::uint32_t blockers = 0;
    std::uint32_t warnings = 0;
    for (const auto& finding : report.findings) {
        if (finding.level == optimization::PreflightLevel::Blocking) {
            ++blockers;
        } else {
            ++warnings;
        }
    }
    EXPECT_EQ(report.blockerCount, blockers);
    EXPECT_EQ(report.warningCount, warnings);
    EXPECT_EQ(report.blockerCount, 2U);  // #2 ＋ #15
    EXPECT_EQ(report.warningCount, 2U);  // #19 ＋ #20
}

// =====================================================================
// validateRunSpec：独立 fail-fast 面（身份/Profile 组装不完整即抛）
// =====================================================================

TEST(OptPreflight, ValidateRunSpecFailFast_WP20T08_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{"AT-09"});

    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    const auto valid = makeSpec(snapshot, f);

    // 合法 spec 直通（不抛）。
    EXPECT_NO_THROW(optimization::validateRunSpec(valid));

    // ① 保留值身份 → 抛 kOptInputInvalid。
    auto noProject = valid;
    noProject.project = core::ProjectId{};
    bool thrown = false;
    try {
        optimization::validateRunSpec(noProject);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_EQ(e.stableCode(), std::string(optimization::kOptInputInvalid));
    }
    EXPECT_TRUE(thrown);

    // ② 保留值快照身份 → 抛（I-OPT-1 无凭据）。
    auto noSnapshot = valid;
    noSnapshot.snapshotId = core::ContentIdentity{};
    thrown = false;
    try {
        optimization::validateRunSpec(noSnapshot);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown);

    // ④ 他域 Profile id → 抛（运行 Profile 必须本域 "opt"）。
    auto wrongDomain = valid;
    wrongDomain.profile.profileId = "kin";
    thrown = false;
    try {
        optimization::validateRunSpec(wrongDomain);
    } catch (const optimization::OptimizationError& e) {
        thrown = true;
        EXPECT_NE(std::string(e.what()).find("opt"), std::string::npos);
    }
    EXPECT_TRUE(thrown);

    // ⑤ 空 version → 抛（三元组解析键缺失）。
    auto noVersion = valid;
    noVersion.profile.version = "";
    thrown = false;
    try {
        optimization::validateRunSpec(noVersion);
    } catch (const optimization::OptimizationError&) {
        thrown = true;
    }
    EXPECT_TRUE(thrown);

    // 装配校验：保留值快照的检查输入 → std::invalid_argument（构造抛）。
    optimization::PreflightInputs badInputs;
    bool argThrown = false;
    try {
        optimization::OptimizationPreflightService bad(badInputs);
    } catch (const std::invalid_argument&) {
        argThrown = true;
    }
    EXPECT_TRUE(argThrown) << "装配违约必须 fail-fast（快照未冻结）";
}

// =====================================================================
// P-OPT-6：会话态承载——preflight 纯函数只读（零写盘零项目修改）
// =====================================================================

TEST(OptPreflight, PreflightIsPureAndReadonly_WP20T08_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{});

    // P-OPT-6 裁决前口径（§16.3）：研究配置持久化通道＝会话态＋导出副本
    // 承载，不入 .rwdesign。检查面的表达：preflight 是纯函数只读面——
    // 调用前后检查输入（快照/基线投影）逐字段不变、零副作用；研究定义
    // （OptimizationRunSpec）为会话态值类型，本单元零持久化写路径
    // （导出副本归 WP-20-T09 §11.4 工件 1——编译面零 io 消费由契约
    // 测试源码扫描钉扎）。
    PreflightFixture f;
    const auto snapshot = makeCompleteSnapshot();
    const auto inputs = f.makeInputs(snapshot);
    const auto spec = makeSpec(snapshot, f);

    optimization::OptimizationPreflightService svc(inputs);
    const auto first = svc.preflight(spec);
    // 调用后检查输入逐字段不变（@post"不修改任何项目状态"的承载）。
    EXPECT_EQ(inputs.snapshot, f.makeInputs(snapshot).snapshot);
    EXPECT_TRUE(inputs.baseline.branch == snapshot.branch);
    EXPECT_EQ(inputs.baseline.tip, snapshot.revision);
    EXPECT_EQ(inputs.baseline.writable, true);
    // 同输入双跑同报告（@determinism 纯函数面）。
    const auto second = svc.preflight(spec);
    EXPECT_EQ(first.blockerCount, second.blockerCount);
    EXPECT_EQ(first.warningCount, second.warningCount);
    ASSERT_EQ(first.findings.size(), second.findings.size());
    for (std::size_t i = 0; i < first.findings.size(); ++i) {
        EXPECT_EQ(first.findings[i].checkId, second.findings[i].checkId);
        EXPECT_EQ(first.findings[i].level, second.findings[i].level);
        EXPECT_EQ(first.findings[i].subject, second.findings[i].subject);
        EXPECT_EQ(first.findings[i].basis, second.findings[i].basis);
        EXPECT_EQ(first.findings[i].suggestion, second.findings[i].suggestion);
    }
}

// =====================================================================
// 词表钉扎：PreflightLevel token（报告/导出的确定性书写）
// =====================================================================

TEST(OptPreflight, LevelTokens_WP20T08)
{
    IRD_TEST_INFO(std::vector<std::string>{"OPT-11"}, std::vector<std::string>{});

    EXPECT_EQ(optimization::toToken(optimization::PreflightLevel::Blocking),
              "blocking");
    EXPECT_EQ(optimization::toToken(optimization::PreflightLevel::Warning),
              "warning");
    // 跨单元字面常量钉扎（R-1 字面承载的契约面——权威在各自主单元）。
    EXPECT_EQ(optimization::kObjectTypeRobotDesign, "robot-design");
    EXPECT_EQ(optimization::kObjectTypeRobotDrivetrain, "robot-drivetrain");
    EXPECT_EQ(optimization::kObjectTypeReqRootSet, "req-set");
    EXPECT_EQ(optimization::kPrjStaleRevisionRejectedCode,
              "PRJ-STALE-REVISION-REJECTED");
    EXPECT_EQ(optimization::kOptProfileId, "opt");
}
