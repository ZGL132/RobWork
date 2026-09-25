/**
 * @file   CommandPipelineContractTest.cpp
 * @brief  建模命令管线契约测试（MdlCommandPipelineContract）——契约
 *         tasks/foundation/WP-13-T08.json acceptance 5 的具名自证：
 *
 *   V-19 未就绪不编译：含 Blocking 项（continuous 工作范围未确认——解码门
 *        可过、断言域阻断）→prepare RejectedHardAssert 且 IModelCompilePort
 *        未被调用（mock 计数观测）；无修订力（计划被清空）
 *   V-20 双编译失败面（modeling 侧可执行范围）：prepare 计划面正确声明
 *        requiresDualCompile=true 且零编译调用；按 project.md §6.6 的 S5
 *        编排形态把计划闭包装入 CompileRequest 消费 mock 端口——故障注入
 *        ok=false＋RT-* 诊断→ok==false（不提交面）；prepare 产出的 MDL-*
 *        预告诊断与编译 RT-* 诊断共存于同一结果收集面（"RT-*＋MDL-* 诊断
 *        一并入目录"的数据流衔接面；目录落盘编排与"HEAD 不变/修订计数
 *        不变"归 project S5 已验契约——PRJ-TX-3，本单元不可经公共面驱动
 *        全管线，见单元卡 §14.6 v0.9 偏差登记）
 *   V-22 stale 面建模侧：expectedRevision 与基线不一致→处理器防御性复核
 *        fail-fast（std::invalid_argument——S2 已拦截过期基线的纵深防线；
 *        草稿 apply-retained 保留语义归 project S2/DraftService 已验契约）
 *   稳定码注册 UT：§9.5 T08 行九码＋T09 行三码经 IDiagnosticRegistry 注册
 *        可查＋未到任务行不预建（T13/T18 行缺席——注册簿纪律；T09 行三码
 *        随 WP-13-T09 在 DiagCodes.cpp 合法登记后在册——验收 attempt 1
 *        B-1 返工同步：原"T09 行不预建"钉住断言随登记过期，分期口径与
 *        DiagCodesTest FactoryScopeIsStagedRows_WP13T09 对齐）
 *
 * 设计依据：units/modeling.md §9.3/§9.5、units/project.md §5.3.6/§6.6/
 * §6.7、units/policy.md §9.4；shared 夹具＝../test/CommandFixtures.hpp
 * （测试域替身不入公共面——project/test/StubHandlers.hpp 先例）。
 */

#include "../test/CommandFixtures.hpp"

#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // StableCodeRegistry（注册权威——§9.5）
#include <sdurws/ird/modeling/DiagCodes.hpp>     // 被测注册函数＋码常量
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace sdurws::ird::modeling::testfixture;
namespace modeling = ::sdurws::ird::modeling;  // 全局域测试——被测类型所在命名空间别名
namespace core = ::sdurws::ird::core;          // core 侧类型（身份/诊断）
namespace diagnostics = ::sdurws::ird::diagnostics;  // diagnostics 侧（稳定码注册表）
namespace policy = ::sdurws::ird::policy;      // policy 侧类型（行程评估器/策略集）
namespace project = ::sdurws::ird::project;    // project 侧类型（命令契约）
using namespace modeling;                     // 被测面（Checker/Handlers/值模型）

namespace {

/// 夹具：服务集＋基线闭包（空闭包＝首应用场景）。
struct PipelineFixture {
    std::unique_ptr<policy::IJointLimitEvaluator> evaluator =
        policy::makeJointLimitEvaluator();
    TestNameContext names;
    TestPolicyProvider provider;
    TestQueryPort query;
    MockCompilePort compile;
    HandlerServices services{
        AssertionSuite::Ports{evaluator.get(), &names}, &provider, makeOid(),
        std::nullopt};

    void bindNames(const RobotDesign& design)
    {
        for (const JointEntry& j : design.joints) {
            names.byId[j.objectId] = "Robot/" + j.localName;
        }
    }
};

/// 新对象槽（全零身份——prepare 取号回填）。
inline PayloadObjectSlot newSlot(std::string token, const ObjectVariant& object)
{
    PayloadObjectSlot slot;
    slot.allocateNew = true;
    slot.objectId = core::ObjectId{};
    slot.objectTypeToken = std::move(token);
    slot.objectBytes = encodeVariant(object);
    return slot;
}

}  // namespace

// =====================================================================
// V-19：未就绪不编译（AT-01）
// =====================================================================

/**
 * @brief V-19：含 Blocking 项的候选→prepare RejectedHardAssert＋编译端口
 *        零调用（mock 计数观测）＋计划清空（无修订力）。
 *
 * 阻断面＝continuous 工作范围未确认（MDL-12）——解码门可过的合法字节，
 * 由断言域（与就绪校验共用 AssertionSuite）就地阻止。
 */
TEST(MdlCommandPipelineContract, V19NotReadyDoesNotCompileAndPlanEmptied)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "MDL-12", "ARC-01"},
                  std::vector<std::string>{"AT-01"});

    PipelineFixture f;
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    RobotDesign design;
    design.joints.push_back(makeContinuousJoint(makeOid(), "Jspin", false));  // 未确认
    design.links.push_back(makeLink(makeOid(), "base"));
    design.links.push_back(makeLink(makeOid(), "link1"));
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, diags);

    // 就地阻止＋精确定位（MDL-ASSERT-RANGE-NOT-FINITE）。
    ASSERT_EQ(outcome, project::PrepareOutcome::RejectedHardAssert);
    bool found = false;
    for (const auto& d : diags) {
        if (d.code == std::string(kMdlAssertRangeNotFinite)) { found = true; }
    }
    EXPECT_TRUE(found);
    // 编译端口零调用（未就绪不编译——mock 观测；S5 编排归 project，
    // 处理器自身亦不触编译）。
    EXPECT_EQ(f.compile.callCount, 0);
    // 无修订力：拒绝态计划清空（project 不消费）。
    EXPECT_TRUE(plan.objectWrites.empty());
}

// =====================================================================
// V-20：双编译失败面（modeling 侧可执行范围——§14.6 v0.9 偏差登记）
// =====================================================================

/**
 * @brief V-20：合法候选→Planned＋requiresDualCompile=true（S5 触发声明）；
 *        计划闭包按 §6.6 形态装入 CompileRequest 消费编译端口——故障注入
 *        ok=false＋RT-* 诊断→ok==false（不提交面）；prepare 的 MDL-* 预告
 *        诊断与编译 RT-* 诊断在同一结果收集面共存（目录合并的数据流衔接）。
 *
 * 范围登记（单元卡 §14.6 v0.9）："HEAD 不变、修订计数不变、目录落盘编排"
 * 归 project S5 已验契约（PRJ-TX-3 双编译桩三路）——project 注册表无公共
 * 注入面（R-2 禁跨单元私有头），本单元以 HandlerContext 接缝验证计划面
 * 与数据流衔接，不冒充全管线已执行。
 */
TEST(MdlCommandPipelineContract, V20CompileFailureInjectsAndDiagnosticsCoexist)
{
    IRD_TEST_INFO(std::vector<std::string>{"MDL-06", "ARC-01"},
                  std::vector<std::string>{"AT-01"});

    PipelineFixture f;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));  // emplace——策略对象不可赋值（const 成员）
    f.compile.failWithRtDiagnostic = true;  // 故障注入（S5 失败面）

    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;

    // 合法候选：±2π 旋转关节（行程 4π 边界合规）＋物性缺失（预告面）。
    const RobotDesign design = makeSingleJointDesign(-2.0 * kPi, 2.0 * kPi);
    f.bindNames(design);
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> prepareDiags;
    const auto outcome = handler.prepare(
        ctx, makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload),
        baseline, plan, prepareDiags);

    // 计划面：Planned＋双编译声明（S5 触发前提）；prepare 自身零编译调用。
    ASSERT_EQ(outcome, project::PrepareOutcome::Planned);
    EXPECT_TRUE(plan.requiresDualCompile);
    EXPECT_EQ(f.compile.callCount, 0);
    // prepare 的 MDL-* 预告诊断（物性缺失——DataInsufficient 面）已产出。
    bool hasMdlWarning = false;
    for (const auto& d : prepareDiags) {
        if (d.code == std::string(kMdlReadinessPhysicsMissing)) { hasMdlWarning = true; }
    }
    EXPECT_TRUE(hasMdlWarning);

    // —— S5 编排形态（project.md §6.6）：计划闭包装入 CompileRequest ——
    project::CompileRequest request{f.query, plan.objectWrites, baseline.id};
    const project::CompileResult result = f.compile.compileWorkCellAndDwc(request);
    // 故障注入：ok=false（不提交面——修订不产生，原子性归 S5 顺序）＋
    // RT-* 诊断携带。
    EXPECT_FALSE(result.ok);
    ASSERT_EQ(result.diagnostics.size(), std::size_t{1});
    EXPECT_EQ(result.diagnostics[0].code, std::string("RT-COMPILE-FAILED"));
    // 计划闭包确实到达编译请求（数据流衔接）。
    EXPECT_EQ(f.compile.lastRequestWrites.size(), plan.objectWrites.size());
    // RT-*（编译面）与 MDL-*（prepare 面）在同一收集面共存——目录合并的
    // 两侧素材（落盘编排归 project/L5 sink）。
    std::vector<core::DiagnosticRecord> merged = prepareDiags;
    merged.insert(merged.end(), result.diagnostics.begin(), result.diagnostics.end());
    bool hasRt = false;
    bool hasMdl = false;
    for (const auto& d : merged) {
        if (d.code.rfind("RT-", 0) == 0) { hasRt = true; }
        if (d.code.rfind("MDL-", 0) == 0) { hasMdl = true; }
    }
    EXPECT_TRUE(hasRt);
    EXPECT_TRUE(hasMdl);
}

// =====================================================================
// V-22：stale 面（建模侧——防御性复核纵深）
// =====================================================================

/**
 * @brief V-22 建模侧：expectedRevision 与基线不一致→防御性复核 fail-fast
 *        （std::invalid_argument）。过期基线的 S2 拦截（PRJ-STALE-REVISION-
 *        REJECTED）与草稿 apply-retained 保留语义归 project 已验契约——
 *        本断言钉住处理器内纵深防线（直接调用 prepare 的契约遵守面）。
 */
TEST(MdlCommandPipelineContract, V22StaleBaselineFailsFastDefensively)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-04"}, std::vector<std::string>{});

    PipelineFixture f;
    const project::RevisionView baseline = makeBaselineView(core::RevisionId::generate());
    f.query.view = baseline;
    f.provider.policyToReturn.emplace(makePolicy(4.0 * kPi));  // ④端口应答（根命令行程校验域）

    const RobotDesign design = makeSingleJointDesign(-1.0, 1.0);
    f.bindNames(design);  // 行程评估的 runtimeName 解析源（R-4 注入面）
    CommandPayload payload;
    payload.objects.push_back(newSlot(std::string(kRobotDesignObjectType), design));

    ApplyRobotDesignHandler handler(f.services);
    project::HandlerContext ctx(f.query, &f.compile, nullptr);
    project::CommandPlan plan;
    std::vector<core::DiagnosticRecord> diags;

    project::CommandEnvelope envelope =
        makeEnvelope(baseline, std::string(kCmdApplyRobotDesign), payload);
    envelope.expectedRevision = core::RevisionId::generate();  // 过期基线（≠tip）
    EXPECT_THROW((void)handler.prepare(ctx, envelope, baseline, plan, diags),
                 std::invalid_argument);
    // 处理器可复用（无半成品状态——拒绝后可基于新 tip 重提，PM-04）。
    envelope.expectedRevision = baseline.id;
    const auto outcome = handler.prepare(ctx, envelope, baseline, plan, diags);
    EXPECT_EQ(outcome, project::PrepareOutcome::Planned);
}

// =====================================================================
// 稳定码注册 UT（§9.5 T08 行九码＋T09 行三码——不预建余行）
// =====================================================================

/**
 * @brief §9.5 T08 行九码＋T09 行三码经 IDiagnosticRegistry 注册可查（描述
 *        符字段与卡面登记一致——confirmable/requiresComparison 对账）＋
 *        未到任务行不预建（T13 包族/T18 耦合族缺席——注册簿纪律）＋重复
 *        注册边界拒绝（装配期 fail-fast）。
 *
 * 分期口径（验收 attempt 1 B-1 返工登记）：已到任务行（T02/T05/T06/T07/
 * T08/T09）全部在册，缺席断言仅钉未到任务行——与 DiagCodesTest
 * FactoryScopeIsStagedRows_WP13T09 的分期封闭性同源对齐；后续任务行登记
 * 时按同款"缺席→在册"推进（T08→T09 本次推进为先例）。
 */
TEST(MdlCommandPipelineContract, StableCodesRegisteredWithoutPrebuilding_WP13T09)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    diagnostics::StableCodeRegistry registry;
    registerModelingCodes(registry);

    // —— T08 行九码全部可查（码值＝DiagCodes.hpp 常量同源对账）。——
    const std::vector<std::string_view> t08Codes = {
        kMdl06TravelLimit,        kMdlAssertMassNonpositive,
        kMdlAssertInertiaNotSpd,  kMdlAssertInertiaTriangle,
        kMdlAssertLimitInterval,  kMdlAssertRangeNotFinite,
        kMdlReadinessRefMissing,  kMdlReadinessResourceState,
        kMdlReadinessPhysicsMissing,
    };
    for (const std::string_view code : t08Codes) {
        const diagnostics::CodeDescriptor* d = registry.find(code);
        ASSERT_NE(d, nullptr) << "T08 行码未注册: " << code;
        EXPECT_EQ(d->ownerUnit, "modeling");
    }
    // 行程确认码：confirmable=true 且比较型强制（SA-15 注册面强化）。
    const diagnostics::CodeDescriptor* travel = registry.find(kMdl06TravelLimit);
    ASSERT_NE(travel, nullptr);
    EXPECT_TRUE(travel->confirmable);
    EXPECT_TRUE(travel->requiresComparison);
    // 硬断言码：不可确认（无放行分支）。
    const diagnostics::CodeDescriptor* mass = registry.find(kMdlAssertMassNonpositive);
    ASSERT_NE(mass, nullptr);
    EXPECT_FALSE(mass->confirmable);

    // —— T09 行三码在册（WP-13-T09 登记 §9.5 转换族——返工 B-1：原
    //    "T09 行不预建"钉住断言随本任务合法登记过期，改为在册断言）。
    //    码值＝DiagCodes.hpp 常量同源对账（与 T08 行同款纪律）。——
    const std::vector<std::string_view> t09Codes = {
        kMdlDhNotExpressible,
        kMdlDhApproximate,
        kMdlDhAnalysisFailed,
    };
    for (const std::string_view code : t09Codes) {
        const diagnostics::CodeDescriptor* d = registry.find(code);
        ASSERT_NE(d, nullptr) << "T09 行码未注册: " << code;
        EXPECT_EQ(d->ownerUnit, "modeling");
    }
    // 终判码（NOT-EXPRESSIBLE）：不可确认（§9.5 T09 行 confirmable 列
    // false——结构前提终判无放行分支，C-3 权威切换拒绝面依赖此语义）。
    const diagnostics::CodeDescriptor* notExpressible =
        registry.find(kMdlDhNotExpressible);
    ASSERT_NE(notExpressible, nullptr);
    EXPECT_FALSE(notExpressible->confirmable);
    // 近似码（APPROXIMATE）：比较型强制（E 度量 vs 附录 D 第 5 项上界的
    // 比对面——requiresComparison 与卡面"转换/Warning"比语义一致）。
    const diagnostics::CodeDescriptor* approximate =
        registry.find(kMdlDhApproximate);
    ASSERT_NE(approximate, nullptr);
    EXPECT_TRUE(approximate->requiresComparison);

    // —— T13 行在册断言（"T13 行不预建"钉住断言随 WP-13-T13 合法登记过期
    //    改为在册断言——T09 行同款推进先例）：两码 ownerUnit=modeling；
    //    UNKNOWN 不可确认（引导语义）、EXPORT-FAILED 执行失败轴且不可确认。——
    const diagnostics::CodeDescriptor* packageUnknown =
        registry.find(kMdlImportPackageUnknown);
    ASSERT_NE(packageUnknown, nullptr) << "T13 行码未注册: MDL-IMPORT-PACKAGE-UNKNOWN";
    EXPECT_EQ(packageUnknown->ownerUnit, "modeling");
    EXPECT_FALSE(packageUnknown->confirmable);
    const diagnostics::CodeDescriptor* exportFailed = registry.find(kMdlExportFailed);
    ASSERT_NE(exportFailed, nullptr) << "T13 行码未注册: MDL-EXPORT-FAILED";
    EXPECT_EQ(exportFailed->ownerUnit, "modeling");
    EXPECT_FALSE(exportFailed->confirmable);
    // —— 未到任务行不预建（仅余 T18 行缺席；T09/T13 行已随 WP-13-T09/T13
    //    登记在册——见上两段在册断言）。——
    EXPECT_EQ(registry.find("MDL-21-COUPLING-STAGE-LOCKED"), nullptr);  // T18（R2——契约 note ④）

    // —— 重复注册＝边界拒绝（码唯一键——注册表纪律）。——
    diagnostics::CodeDescriptor duplicate;
    duplicate.code = std::string(kMdl06TravelLimit);
    duplicate.ownerUnit = "modeling";
    duplicate.titleKey = "diag.dup.title";
    duplicate.detailKey = "diag.dup.detail";
    duplicate.paramSchema = "[]";
    EXPECT_THROW(registry.registerCode(duplicate), diagnostics::DiagnosticsError);
}
