/**
 * @file   ReadinessTest.cpp
 * @brief  需求就绪校验器用例组（ReqReadiness）——R0~R9 分层正反例
 *         （短路优先＋Blocking/Warning/NotApplicable 分级断言）、
 *         ReadinessSummary 数据源（evidence ①级形状逐字段一致）、
 *         Must/Should 分级判定、预览纯函数语义与 P-REQ-6 边界声明
 *         （任务契约 WP-14-T05 acceptance 1~5/7 的逐条具名自证）。
 *
 * 设计依据：
 *   - units/requirements.md §8.1（分层表逐行——每层正反例的对错基准）、
 *     §8.3（状态正交——V-12：需求未就绪不产生任何工程判定结论）、
 *     §7.5（预览与正式分离——EVI-01 表 1）、§9.5（接口契约）、§4.3
 *     （enabled 行——"未启用条目不进入正式就绪判定"）
 *   - units/evidence.md §6.4①（ReadinessSummary 数据形状对端锚——
 *     {valid, invalidMustItems[]}）
 *   - 任务契约 tasks/foundation/WP-14-T05.json acceptance 1~5/7
 *
 * 夹具纪律：工作集直接构造＋sortEntriesByObjectId 规范化（绕过编辑器
 * 的违约路径恰是 R2/R3 防御面的测试目标）；闭包元数据与工作集引用逐
 * 一对应（token 命名＝ObjectTypes.hpp 五 token＋modeling 侧三 token 字面
 * ——R-1 允许的元数据交叉引用，不 include modeling 头）。
 *
 * 断言纪律：每用例名带 ACC 序号（验收对照表锚点）；稳定码断言一律以
 * §9.6 原文字面对照（DiagCodesTest 负责注册面，本文件负责产码面）。
 */

#include <sdurws/ird/requirements/Readiness.hpp>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>
#include <sdurws/ird/requirements/ObjectTypes.hpp>
#include <sdurws/ird/requirements/RequirementTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include <rw/math/Vector3D.hpp>

using namespace sdurws::ird;

namespace {

using requirements::AppliesTo;
using requirements::AppliesToScope;
using requirements::BoundingBox;
using requirements::CheckContext;
using requirements::ConditionSet;
using requirements::DomainReadinessItem;
using requirements::IRequirementReadinessChecker;
using requirements::PlanSet;
using requirements::PointSet;
using requirements::ReadinessCheckLayer;
using requirements::ReadinessFindingLevel;
using requirements::RequirementLevel;
using requirements::RequirementReadinessChecker;
using requirements::RequirementReadinessReport;
using requirements::RequirementReference;
using requirements::RequirementRefKind;
using requirements::RequirementSet;
using requirements::RequirementWorkingSet;
using requirements::SamplingPlan;
using requirements::TaskPoint;
using requirements::WorkRegion;

// ---------------------------------------------------------------------
// 夹具辅助（值模型直接构造——绕过编辑器的路径即防御面测试目标）
// ---------------------------------------------------------------------

/// 测试用对象身份（随机生成——排序断言以同次运行内相对序为准）。
core::ObjectId makeOid()
{
    return core::ObjectId::generate();
}

/// 用户输入来源标记（methodTag 语法合规——modeling 夹具同款）。
core::ValueProvenance userProvenance()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided,
                                       std::nullopt, std::nullopt,
                                       std::string("test-fixture"));
}

/// 非全零内容版本（闭包引用键——测试不复核摘要真实性）。
core::ContentVersion testContentVersion(std::uint8_t tag)
{
    core::ContentVersion cv;
    cv.bytes[0] = tag;
    return cv;
}

/// 世界系参考（RequirementReference 缺省＝World——合法缺省引用）。
RequirementReference worldFrame()
{
    return RequirementReference{};  // kind=World，无载荷
}

/// 合法任务点（World 系/受约束 Z 分量/有限位置/默认容差/三段 work 启用）。
TaskPoint makeHealthyPoint(const std::string& name)
{
    TaskPoint p;
    p.objectId = makeOid();
    p.name = name;
    p.level = RequirementLevel::Must;
    p.enabled = true;
    p.refFrame = worldFrame();
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.1, 0.2, 0.3), userProvenance());
    p.pose.constrainedDof.z = true;  // 至少一真（I-REQ-5）
    p.pose.orientation.kind = requirements::OrientationRuleKind::Fixed;
    p.pose.orientation.fixedRpy = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    p.tolerance = requirements::ToleranceSpec{};  // 默认 1e-3 m / 1°
    p.work.enabled = true;                        // work 段恒启用（§4.3）
    p.work.distanceM = 1.0;                       // m（正有限——R3 段门）
    return p;
}

/// 合法工作区域（非退化盒/默认覆盖率/显式计数采样）。
WorkRegion makeHealthyRegion(const std::string& name)
{
    WorkRegion r;
    r.objectId = makeOid();
    r.name = name;
    r.level = RequirementLevel::Must;
    r.enabled = true;
    r.refFrame = worldFrame();
    r.box = BoundingBox{};
    r.box.size = rw::math::Vector3D<double>(1.0, 1.0, 1.0);  // m（非退化）
    r.positionSampling.method = requirements::PositionSamplingMethod::Grid;
    r.positionSampling.counts = {2, 2, 2};
    r.orientationSampling.directionSamples = 1;
    r.orientationSampling.rollSamples = 1;
    return r;
}

/// 合法工况（AllStations/无事件——必验集合的 Must 级参与者）。
requirements::OperatingCondition makeHealthyCondition(const std::string& name)
{
    requirements::OperatingCondition c;
    c.objectId = makeOid();
    c.name = name;
    c.level = RequirementLevel::Must;
    c.enabled = true;
    c.appliesTo = AppliesTo{};  // AllStations 缺省
    return c;
}

/// 合法采样计划（指向区域条目/规范化 Grid）。
SamplingPlan makeHealthyPlan(const core::ObjectId& regionId)
{
    SamplingPlan plan;
    plan.objectId = makeOid();
    plan.regionRef = regionId;
    plan.positionSampling.method = requirements::PositionSamplingMethod::Grid;
    plan.positionSampling.counts = {3, 3, 3};
    plan.orientationSampling.directionSamples = 1;
    plan.orientationSampling.rollSamples = 1;
    return plan;
}

/// 规范化健康工作集（根＋一点一区域一工况一计划——全 Must 全启用；
/// 集合按 ObjectId 规范序排序＝I-REQ-1 前置）。
RequirementWorkingSet makeHealthyWorkingSet()
{
    RequirementWorkingSet ws;

    TaskPoint p = makeHealthyPoint("P1");
    WorkRegion r = makeHealthyRegion("R1");
    requirements::OperatingCondition c = makeHealthyCondition("C1");
    SamplingPlan plan = makeHealthyPlan(r.objectId);

    ws.points.entries.push_back(p);
    ws.regions.entries.push_back(r);
    ws.conditions.entries.push_back(c);
    ws.plans.entries.push_back(plan);
    requirements::sortEntriesByObjectId(ws.points.entries);
    requirements::sortEntriesByObjectId(ws.regions.entries);
    requirements::sortEntriesByObjectId(ws.conditions.entries);
    requirements::sortEntriesByObjectId(ws.plans.entries);

    ws.root.name = "T05 健康需求集";
    ws.root.pointSetRef = makeOid();
    ws.root.regionSetRef = makeOid();
    ws.root.conditionSetRef = makeOid();
    ws.root.planSetRef = makeOid();
    return ws;
}

/// 闭包元数据（与工作集根引用表对齐的集合对象〔槽可空——对齐缺省槽〕
/// ＋额外登记建模对象的通用入口——R0/R1/R8 的浅校验数据面）。
CheckContext makeHealthyContext(const RequirementWorkingSet& ws)
{
    CheckContext ctx;
    const auto add = [&ctx](core::ObjectId oid, std::string token) {
        project::ObjectRef ref;
        ref.objectId = oid;
        ref.contentVersion = testContentVersion(
            static_cast<std::uint8_t>(ctx.closureRefs.size() + 1U));
        ref.objectTypeToken = std::move(token);
        ref.digest256 = std::string(64, '0');
        ctx.closureRefs.push_back(std::move(ref));
    };
    if (ws.root.pointSetRef) {
        add(*ws.root.pointSetRef, std::string(requirements::kReqPointSetObjectType));
    }
    if (ws.root.regionSetRef) {
        add(*ws.root.regionSetRef, std::string(requirements::kReqRegionSetObjectType));
    }
    if (ws.root.conditionSetRef) {
        add(*ws.root.conditionSetRef, std::string(requirements::kReqConditionSetObjectType));
    }
    if (ws.root.planSetRef) {
        add(*ws.root.planSetRef, std::string(requirements::kReqPlanSetObjectType));
    }
    return ctx;
}

/// 按层取发现（过滤辅助——测试定位面）。
std::vector<const DomainReadinessItem*> itemsOfLayer(const RequirementReadinessReport& report,
                                                     ReadinessCheckLayer layer)
{
    std::vector<const DomainReadinessItem*> out;
    for (const DomainReadinessItem& item : report.items) {
        if (item.layer == layer) { out.push_back(&item); }
    }
    return out;
}

/// 是否存在指定层＋级别的发现（code 为 nullptr 时只按层＋级别匹配——
/// NotApplicable 标记项的码不作为判据）。
bool hasFinding(const RequirementReadinessReport& report, ReadinessCheckLayer layer,
                ReadinessFindingLevel level, const char* code)
{
    for (const DomainReadinessItem* item : itemsOfLayer(report, layer)) {
        if (item->level == level
            && (code == nullptr || item->diag.code == code)) {
            return true;
        }
    }
    return false;
}

/// 要求投影的便捷封装（被测契约的调用形状——readinessSummary(check(ws,ctx))）。
evidence::ReadinessSummary projectOf(const RequirementWorkingSet& ws,
                                     const CheckContext& ctx,
                                     const IRequirementReadinessChecker& checker)
{
    return checker.readinessSummary(checker.check(ws, ctx));
}

}  // namespace

// =====================================================================
// acceptance 1——R0~R9 分层正反例（短路优先＋三级分级断言）
// =====================================================================

/**
 * 健康工作集全层通过（ACC1 正例半边）：零 Blocking；R6/R7/R8 的检查域
 * 缺席面以 NotApplicable 显式标记（R7 无顺序声明/R8 无工具环境引用面；
 * R6 有区域有计划→无标记）；R5 必验非空→无警告；投影 valid=true。
 */
TEST(ReqReadiness, HealthySetPassesAllLayers_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    const RequirementWorkingSet ws = makeHealthyWorkingSet();
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_FALSE(report.hasBlocking()) << "健康工作集不得有 Blocking 发现";
    EXPECT_TRUE(report.invalidMustItems.empty()) << "零 Blocking→Must 清单为空";

    // 分级断言：检查域缺席层的 NotApplicable 显式标记。
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::NotApplicable, nullptr) == false)
        << "有区域有计划——R6 不适用标记不应出现";
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R7,
                           ReadinessFindingLevel::NotApplicable, nullptr))
        << "无顺序声明——R7 应有 NotApplicable 显式标记";
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R8,
                           ReadinessFindingLevel::NotApplicable, nullptr))
        << "无工具/环境引用——R8 应有 NotApplicable 显式标记";
    EXPECT_FALSE(hasFinding(report, ReadinessCheckLayer::R5,
                            ReadinessFindingLevel::Warning, nullptr))
        << "必验集合非空——R5 无警告";

    // 投影正例（ACC2 正例半边——valid=true 且清单空）。
    const evidence::ReadinessSummary summary = projectOf(ws, ctx, checker);
    EXPECT_TRUE(summary.valid);
    EXPECT_TRUE(summary.invalidMustItems.empty());
}

/**
 * R0 结构完整反例（ACC1）：根引用表悬空槽与 token 失配均为 Blocking
 * REF-MISSING；短路优先——Blocking 出现后 R1~R9 不执行（items 全部
 * 停留在 R0）；投影 valid=false（集合级非法使输入不可消费——保守门禁）。
 */
TEST(ReqReadiness, R0RootRefDanglingAndTokenMismatch_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    const RequirementWorkingSet ws = makeHealthyWorkingSet();
    const RequirementReadinessChecker checker;

    // 反例 A：pointSetRef 悬空（闭包无该对象）。
    CheckContext ctxA = makeHealthyContext(ws);
    ctxA.closureRefs.erase(
        std::remove_if(ctxA.closureRefs.begin(), ctxA.closureRefs.end(),
                       [&](const project::ObjectRef& ref) {
                           return ref.objectId == *ws.root.pointSetRef;
                       }),
        ctxA.closureRefs.end());
    const RequirementReadinessReport reportA = checker.check(ws, ctxA);
    ASSERT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R0,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"))
        << "悬空槽应为 R0 Blocking REF-MISSING";
    for (const DomainReadinessItem& item : reportA.items) {
        EXPECT_EQ(item.layer, ReadinessCheckLayer::R0)
            << "短路优先：R0 Blocking 后不得出现后层发现";
    }
    EXPECT_EQ(projectOf(ws, ctxA, checker).valid, false);

    // 反例 B：planSetRef 登记的 token 失配（登记成 point-set token）。
    CheckContext ctxB = makeHealthyContext(ws);
    for (project::ObjectRef& ref : ctxB.closureRefs) {
        if (ref.objectId == *ws.root.planSetRef) {
            ref.objectTypeToken = std::string(requirements::kReqPointSetObjectType);
        }
    }
    const RequirementReadinessReport reportB = checker.check(ws, ctxB);
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R0,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"))
        << "token 失配应为 R0 Blocking REF-MISSING";
}

/**
 * R0 结构完整反例（ACC1）：根引用表两槽引用同一对象（集合对象每需求集
 * 至多一份——§4.1）为 Blocking。
 */
TEST(ReqReadiness, R0DuplicateSlotReference_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    ws.root.conditionSetRef = ws.root.pointSetRef;  // 两槽同对象
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R0,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
}

/**
 * R1 引用完整反例（ACC1）：refFrame 的 ModelFrame 目标不在闭包＝悬空
 * （Blocking REF-MISSING，浅校验——仅查 objectRefs 元数据）。
 */
TEST(ReqReadiness, R1RefFrameTargetMissing_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    TaskPoint& p = ws.points.entries.front();
    p.refFrame.kind = RequirementRefKind::ModelFrame;
    p.refFrame.objectId = makeOid();  // 不登记进闭包＝悬空
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    ASSERT_TRUE(hasFinding(report, ReadinessCheckLayer::R1,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
    ASSERT_TRUE(report.items.front().diag.subject.has_value());
    EXPECT_TRUE(*report.items.front().diag.subject == p.objectId)
        << "发现应定位到涉事条目（subject=条目 oid）";
    EXPECT_EQ(projectOf(ws, ctx, checker).valid, false);
}

/**
 * R1 引用完整反例（ACC1）：AlignGeometryNormal 目标场景对象在闭包中
 * 登记 token 失配（登记为 robot-design 而非 scene-object）＝Blocking。
 */
TEST(ReqReadiness, R1OrientationRuleTargetTokenMismatch_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-09"},
                  std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    TaskPoint& p = ws.points.entries.front();
    const core::ObjectId sceneObj = makeOid();
    p.pose.orientation.kind = requirements::OrientationRuleKind::AlignGeometryNormal;
    p.pose.orientation.feature = requirements::OrientationFeature::FramePlaneNormal;
    p.pose.orientation.targetSceneObject = sceneObj;

    CheckContext ctx = makeHealthyContext(ws);
    project::ObjectRef ref;
    ref.objectId = sceneObj;
    ref.contentVersion = testContentVersion(99);
    ref.objectTypeToken = "robot-design";  // token 失配（期望 scene-object）
    ref.digest256 = std::string(64, '0');
    ctx.closureRefs.push_back(ref);

    const RequirementReadinessChecker checker;
    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R1,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
}

/**
 * R2 坐标系有效反例（ACC1）：refFrame 引用种类与场景槽失配（Tool 出现
 * 在场景槽——直接构造绕过构造边界的防御面）为 Blocking。
 */
TEST(ReqReadiness, R2RefFrameKindIllegalInSceneSlot_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    TaskPoint& p = ws.points.entries.front();
    p.refFrame.kind = RequirementRefKind::Tool;  // 场景槽禁止 Tool
    p.refFrame.objectId = makeOid();
    p.refFrame.tcpKey = "tcp";
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R2,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
}

/**
 * R3 位姿合法反例族（ACC1）：非有限位置分量/容差非正/受约束分量全
 * false/零向量目标——均为 Blocking POSE-ILLEGAL（I-REQ-5 值面的就绪
 * 层定位）。
 */
TEST(ReqReadiness, R3PoseValueViolations_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-01"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 反例 A：位置含 NaN（数值有限——§8.1 R3 行）。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    TaskPoint& pA = wsA.points.entries.front();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    pA.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(nan, 0.2, 0.3), userProvenance());
    const RequirementReadinessReport reportA = checker.check(wsA, makeHealthyContext(wsA));
    EXPECT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R3,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-POSE-ILLEGAL"));

    // 反例 B：容差非正（>0——I-REQ-5 后半）。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    wsB.points.entries.front().tolerance.positionTolerance = 0.0;
    const RequirementReadinessReport reportB = checker.check(wsB, makeHealthyContext(wsB));
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R3,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-POSE-ILLEGAL"));

    // 反例 C：constrainedDof 全 false（至少一真——I-REQ-5）。
    RequirementWorkingSet wsC = makeHealthyWorkingSet();
    wsC.points.entries.front().pose.constrainedDof = requirements::ConstrainedDof{};
    const RequirementReadinessReport reportC = checker.check(wsC, makeHealthyContext(wsC));
    EXPECT_TRUE(hasFinding(reportC, ReadinessCheckLayer::R3,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-POSE-ILLEGAL"));

    // 反例 D：PointAtTarget 零向量目标（§5.3——不退化为零向量点）。
    RequirementWorkingSet wsD = makeHealthyWorkingSet();
    TaskPoint& pD = wsD.points.entries.front();
    pD.pose.orientation.kind = requirements::OrientationRuleKind::PointAtTarget;
    pD.pose.orientation.targetPoint = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    const RequirementReadinessReport reportD = checker.check(wsD, makeHealthyContext(wsD));
    EXPECT_TRUE(hasFinding(reportD, ReadinessCheckLayer::R3,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-POSE-ILLEGAL"));
}

/**
 * R4 工况绑定完整反例（ACC1）：appliesTo.stations 与 events.stationRef
 * 指向不存在的任务点条目——均为 Blocking REF-MISSING（层标注 R4）。
 */
TEST(ReqReadiness, R4StationBindingDangling_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-04"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 反例 A：appliesTo.stations 悬空。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    requirements::OperatingCondition& cA = wsA.conditions.entries.front();
    cA.appliesTo.scope = AppliesToScope::Stations;
    cA.appliesTo.stations = {makeOid()};  // 不存在的任务点条目
    const RequirementReadinessReport reportA = checker.check(wsA, makeHealthyContext(wsA));
    EXPECT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R4,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));

    // 反例 B：events.stationRef 悬空。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    requirements::OperatingCondition& cB = wsB.conditions.entries.front();
    requirements::ConditionEvent ev;
    ev.type = requirements::ConditionEventType::Grasp;
    ev.stationRef = makeOid();  // 不存在的任务点条目
    cB.events.push_back(ev);
    const RequirementReadinessReport reportB = checker.check(wsB, makeHealthyContext(wsB));
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R4,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
}

/**
 * R5 必验范围明确（ACC1 Warning 分支）：必验集合（enabled∧Must）为空
 * →Warning NO-REQUIRED-CASE，不阻断应用（投影 valid 保持 true——Warning
 * 不改变就绪结论，正式拦截归 evidence P-EV-7）。
 */
TEST(ReqReadiness, R5EmptyRequiredSetWarning_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    requirements::OperatingCondition& c = ws.conditions.entries.front();
    c.enabled = false;  // 唯一工况被停用→必验集合为空
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R5,
                           ReadinessFindingLevel::Warning,
                           "REQ-READY-NO-REQUIRED-CASE"));
    EXPECT_FALSE(report.hasBlocking()) << "R5 Warning 不阻断应用";
    const evidence::ReadinessSummary summary = projectOf(ws, ctx, checker);
    EXPECT_TRUE(summary.valid) << "Warning 不使输入未完成";
}

/**
 * R6 采样计划有效反例族（ACC1 Blocking 分支）：区域盒退化/计划悬空
 * 区域引用/一区多计划——均为 Blocking PLAN-DEGENERATE。
 */
TEST(ReqReadiness, R6DegenerateAndPlanMismatch_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-03"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 反例 A：盒退化（size 含零分量——I-REQ-6）。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    wsA.regions.entries.front().box.size = rw::math::Vector3D<double>(1.0, 0.0, 1.0);
    const RequirementReadinessReport reportA = checker.check(wsA, makeHealthyContext(wsA));
    EXPECT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-PLAN-DEGENERATE"));

    // 反例 B：计划指向不存在的区域条目（计划-区域失配）。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    wsB.plans.entries.front().regionRef = makeOid();
    const RequirementReadinessReport reportB = checker.check(wsB, makeHealthyContext(wsB));
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-PLAN-DEGENERATE"));

    // 反例 C：一区多计划（一一对应违例——§8.1 R6"计划-区域一一对应"）。
    RequirementWorkingSet wsC = makeHealthyWorkingSet();
    const core::ObjectId regionId = wsC.regions.entries.front().objectId;
    SamplingPlan second = makeHealthyPlan(regionId);  // 同区域第二个计划
    wsC.plans.entries.push_back(second);
    requirements::sortEntriesByObjectId(wsC.plans.entries);
    const RequirementReadinessReport reportC = checker.check(wsC, makeHealthyContext(wsC));
    EXPECT_TRUE(hasFinding(reportC, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-PLAN-DEGENERATE"));
}

/**
 * R6 采样计划有效（ACC1 Warning/NotApplicable 分支）：区域非空而计划
 * 为空→Warning PLAN-MISSING（零计划预告）；无区域且无计划→NotApplicable
 * 显式标记（"无区域任务"——§8.1 级别行原文示例）。
 */
TEST(ReqReadiness, R6ZeroPlanWarningAndNoRegionNotApplicable_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-03"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 分支 A：有区域无计划→Warning（可应用，登记——正式判定归评估）。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    wsA.plans.entries.clear();
    wsA.root.planSetRef = std::nullopt;
    const CheckContext ctxA = makeHealthyContext(wsA);
    const RequirementReadinessReport reportA = checker.check(wsA, ctxA);
    EXPECT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::Warning,
                           "REQ-READY-PLAN-MISSING"));
    EXPECT_FALSE(reportA.hasBlocking()) << "零计划预告不阻断应用";
    EXPECT_TRUE(projectOf(wsA, ctxA, checker).valid);

    // 分支 B：无区域且无计划→NotApplicable（显式标记，非违例）。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    wsB.regions.entries.clear();
    wsB.plans.entries.clear();
    wsB.root.regionSetRef = std::nullopt;
    wsB.root.planSetRef = std::nullopt;
    const RequirementReadinessReport reportB = checker.check(wsB, makeHealthyContext(wsB));
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R6,
                           ReadinessFindingLevel::NotApplicable, nullptr));
    EXPECT_FALSE(reportB.hasBlocking());
}

/**
 * R7 任务顺序无环反例族（ACC1）：成环（P1→P2→P1）/重复键（同前驱两
 * 后继）/悬空前驱——均为 Blocking SEQ-CYCLE（R7 层码承载，cause 区分）。
 */
TEST(ReqReadiness, R7SequenceViolations_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-02"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 反例 A：顺序环。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    wsA.points.entries.front().sequenceKey = std::string("P1");  // 自环（前驱=自身名）
    const RequirementReadinessReport reportA = checker.check(wsA, makeHealthyContext(wsA));
    ASSERT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R7,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-SEQ-CYCLE"));

    // 反例 B：悬空前驱名。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    wsB.points.entries.front().sequenceKey = std::string("不存在的前驱");
    const RequirementReadinessReport reportB = checker.check(wsB, makeHealthyContext(wsB));
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R7,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-SEQ-CYCLE"));

    // 反例 C：两区域声明同一前驱（重复键——区域顺序键同口径）。
    RequirementWorkingSet wsC = makeHealthyWorkingSet();
    WorkRegion second = makeHealthyRegion("R2");
    wsC.regions.entries.front().sequenceKey = std::string("起点");
    second.sequenceKey = std::string("起点");
    wsC.regions.entries.push_back(second);
    requirements::sortEntriesByObjectId(wsC.regions.entries);
    const RequirementReadinessReport reportC = checker.check(wsC, makeHealthyContext(wsC));
    EXPECT_TRUE(hasFinding(reportC, ReadinessCheckLayer::R7,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-SEQ-CYCLE"));
}

/**
 * R8 工具/模型引用存在反例（ACC1）：tcpRef Tool 目标悬空与工况环境
 * 引用 token 失配——Blocking REF-MISSING（§8.1 R8 行字面域）。
 */
TEST(ReqReadiness, R8ToolAndEnvironmentRefDangling_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "REQ-01"},
                  std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // 反例 A：任务点 tcpRef Tool 目标不在闭包（悬空）。
    RequirementWorkingSet wsA = makeHealthyWorkingSet();
    TaskPoint& pA = wsA.points.entries.front();
    pA.tcpRef = RequirementReference{};
    pA.tcpRef->kind = RequirementRefKind::Tool;
    pA.tcpRef->objectId = makeOid();
    pA.tcpRef->tcpKey = "tcp1";
    const RequirementReadinessReport reportA = checker.check(wsA, makeHealthyContext(wsA));
    EXPECT_TRUE(hasFinding(reportA, ReadinessCheckLayer::R8,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));

    // 反例 B：工况环境引用登记 token 失配（期望 scene-object）。
    RequirementWorkingSet wsB = makeHealthyWorkingSet();
    requirements::OperatingCondition& cB = wsB.conditions.entries.front();
    const core::ObjectId envObj = makeOid();
    cB.environmentRefs.push_back(envObj);
    CheckContext ctxB = makeHealthyContext(wsB);
    project::ObjectRef ref;
    ref.objectId = envObj;
    ref.contentVersion = testContentVersion(98);
    ref.objectTypeToken = "robot-design";  // 失配
    ref.digest256 = std::string(64, '0');
    ctxB.closureRefs.push_back(ref);
    const RequirementReadinessReport reportB = checker.check(wsB, ctxB);
    EXPECT_TRUE(hasFinding(reportB, ReadinessCheckLayer::R8,
                           ReadinessFindingLevel::Blocking,
                           "REQ-READY-REF-MISSING"));
}

/**
 * R9 可生成切片反例（ACC1）：对象 schema 主版本超出支持——Blocking
 * REQ-SCHEMA-UNSUPPORTED（§9.6 T02/T03 行码；NFR-DEP-04 稳定拒绝面）。
 */
TEST(ReqReadiness, R9SchemaVersionUnsupported_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "NFR-DEP-04"},
                  std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    ws.points.schemaVersion = requirements::kReqPointSetSchemaVersion + 1U;  // 未来版本
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    EXPECT_TRUE(hasFinding(report, ReadinessCheckLayer::R9,
                           ReadinessFindingLevel::Blocking,
                           "REQ-SCHEMA-UNSUPPORTED"));
    EXPECT_EQ(projectOf(ws, ctx, checker).valid, false);
}

/**
 * 短路优先（ACC1 执行序断言）：R3 Blocking 出现后，R4~R9 不执行——
 * 报告中不存在后层发现（已执行层的 R5 Warning 类事实不适用——短路即停）。
 */
TEST(ReqReadiness, ShortCircuitStopsAfterFirstBlockingLayer_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    ws.points.entries.front().tolerance.positionTolerance = -1.0;  // R3 Blocking
    // 同时制造 R7 违例——短路后不得出现。
    ws.points.entries.front().sequenceKey = std::string("不存在");
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementReadinessReport report = checker.check(ws, ctx);
    ASSERT_TRUE(report.hasBlocking());
    ReadinessCheckLayer maxLayer = ReadinessCheckLayer::R0;
    for (const DomainReadinessItem& item : report.items) {
        maxLayer = static_cast<ReadinessCheckLayer>(
            static_cast<std::uint8_t>(maxLayer) < static_cast<std::uint8_t>(item.layer)
                ? item.layer
                : maxLayer);
    }
    EXPECT_EQ(maxLayer, ReadinessCheckLayer::R3)
        << "短路优先：最高发现层＝首个 Blocking 层（R3），后层不执行";
}

// =====================================================================
// acceptance 2/3——ReadinessSummary 数据源与 Must/Should 分级判定
// =====================================================================

/**
 * ReadinessSummary 数据源（ACC2）：任一启用 Must 条目非法→valid=false
 * ＋invalidMustItems 全量清单（不抽样——两条非法 Must 点必须全部在
 * 清单）；清单内容＝条目 ObjectId 规范文本（升序——确定性）。
 */
TEST(ReqReadiness, SummaryListsAllInvalidMustItems_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "EVI-01"},
                  std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    // 两个启用 Must 点同时非法（一条容差非法＋一条掩码全空）。
    TaskPoint second = makeHealthyPoint("P2");
    ws.points.entries.push_back(second);
    requirements::sortEntriesByObjectId(ws.points.entries);
    ws.points.entries[0].tolerance.positionTolerance = 0.0;
    ws.points.entries[1].pose.constrainedDof = requirements::ConstrainedDof{};

    const std::string id0 = ws.points.entries[0].objectId.toCanonical();
    const std::string id1 = ws.points.entries[1].objectId.toCanonical();

    const RequirementReadinessChecker checker;
    const evidence::ReadinessSummary summary = projectOf(ws, makeHealthyContext(ws), checker);

    EXPECT_FALSE(summary.valid) << "任一启用 Must 条目非法→输入未完成";
    ASSERT_EQ(summary.invalidMustItems.size(), 2U)
        << "invalidMustItems 必须全量列出（不抽样）";
    EXPECT_EQ(summary.invalidMustItems[0], id0);
    EXPECT_EQ(summary.invalidMustItems[1], id1);
}

/**
 * ReadinessSummary 数据形状逐字段一致（ACC2——evidence §6.4① 对端值
 * 类型消费）：投影结果与 evidence::ReadinessSummary 聚合字面逐字段
 * 相等（valid/invalidMustItems 两字段——多一字段少一字段都无法编译
 * 通过该聚合比较，即形状一致性 proof）。
 */
TEST(ReqReadiness, SummaryProjectionMatchesEvidenceShape_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"EVI-01"}, std::vector<std::string>{});

    const RequirementWorkingSet ws = makeHealthyWorkingSet();
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    // 正面：投影 == 字面聚合（两字段逐字段一致）。
    const evidence::ReadinessSummary summary = projectOf(ws, ctx, checker);
    const evidence::ReadinessSummary expected{true, {}};
    EXPECT_TRUE(summary == expected) << "①级数据形状＝{valid, invalidMustItems[]}";

    // 反面：非法输入投影与字面聚合（valid=false＋清单）逐字段一致。
    RequirementWorkingSet bad = ws;
    bad.points.entries.front().tolerance.positionTolerance = 0.0;
    const evidence::ReadinessSummary badSummary =
        projectOf(bad, makeHealthyContext(bad), checker);
    const evidence::ReadinessSummary badExpected{
        false, {bad.points.entries.front().objectId.toCanonical()}};
    EXPECT_TRUE(badSummary == badExpected);
}

/**
 * Must/Should 分级判定（ACC3——DTB 完成条件原文）：启用 Must 非法→
 * valid=false；Should 条目数据非法仍属结构性 Blocking（输入不可消费）
 * 但不进 Must 清单（REQ-06 词表口径）；enabled=false 条目完全不进入
 * 判定（非法也不阻断、不列清单）；需求未就绪不产生任何工程判定结论
 * （V-12——就绪面无 EngineeringStatus 输出，见本文件结构注）。
 */
TEST(ReqReadiness, MustShouldGrading_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    const RequirementReadinessChecker checker;

    // ① 启用 Must 非法→valid=false＋清单含之。
    RequirementWorkingSet wsMust = makeHealthyWorkingSet();
    wsMust.points.entries.front().tolerance.positionTolerance = 0.0;
    const std::string mustId = wsMust.points.entries.front().objectId.toCanonical();
    const evidence::ReadinessSummary s1 =
        projectOf(wsMust, makeHealthyContext(wsMust), checker);
    EXPECT_FALSE(s1.valid);
    ASSERT_EQ(s1.invalidMustItems.size(), 1U);
    EXPECT_EQ(s1.invalidMustItems.front(), mustId);

    // ② Should 条目数据非法：结构性 Blocking（valid=false——非法数据
    //    不可编码成正式切片的保守门禁）但不进 Must 清单（REQ-06 口径）。
    RequirementWorkingSet wsShould = makeHealthyWorkingSet();
    requirements::OperatingCondition& c = wsShould.conditions.entries.front();
    c.level = RequirementLevel::Should;  // 降级为 Should
    c.appliesTo.scope = AppliesToScope::Stations;
    c.appliesTo.stations = {makeOid()};  // 悬空绑定——R4 Blocking
    const CheckContext ctxShould = makeHealthyContext(wsShould);
    const RequirementReadinessReport reportShould = checker.check(wsShould, ctxShould);
    EXPECT_TRUE(reportShould.hasBlocking());
    const evidence::ReadinessSummary s2 = projectOf(wsShould, ctxShould, checker);
    EXPECT_FALSE(s2.valid);
    EXPECT_TRUE(s2.invalidMustItems.empty())
        << "Should 条目非法不进 Must 清单（明细在 report.items）";

    // ③ enabled=false 条目不进入判定：Must 点停用后其非法数据既不阻断
    //    （valid=true）也不列清单（「任一启用的 Must 条目」口径）。
    RequirementWorkingSet wsDisabled = makeHealthyWorkingSet();
    TaskPoint& dp = wsDisabled.points.entries.front();
    dp.enabled = false;
    dp.tolerance.positionTolerance = 0.0;  // 非法但条目已停用
    const evidence::ReadinessSummary s3 =
        projectOf(wsDisabled, makeHealthyContext(wsDisabled), checker);
    EXPECT_TRUE(s3.valid) << "未启用条目不进入就绪判定（§4.3 enabled 行）";
    EXPECT_TRUE(s3.invalidMustItems.empty());
}

// =====================================================================
// acceptance 4/5——预览纯函数语义与 O-39 投影面
// =====================================================================

/**
 * 预览不产生正式证据与结果对象（ACC4——EVI-01 表 1/V-20）：check 是
 * 纯函数——重复执行结果逐字段相等；输入（工作集/上下文）零变化；输出
 * 报告与投影不含任何修订/内容身份/证据对象字段（预览输入无内容身份
 * 承诺——evidence §4.1.1 未应用草稿预览行口径）。
 */
TEST(ReqReadiness, PreviewIsPureWithoutEvidenceObjects_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "EVI-01"},
                  std::vector<std::string>{"V-20"});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    const RequirementWorkingSet wsBefore = ws;  // 值快照（不可变观测基准）
    const CheckContext ctxBefore = ctx;

    const RequirementReadinessReport r1 = checker.check(ws, ctx);
    const RequirementReadinessReport r2 = checker.check(ws, ctx);
    EXPECT_TRUE(r1 == r2) << "重复预览结果逐字段相等（可重入无副作用）";
    EXPECT_TRUE(ws == wsBefore) << "预览零写入——工作集不变";
    EXPECT_TRUE(ctx == ctxBefore) << "预览零写入——上下文不变";

    // 预览输入无内容身份承诺：报告/投影类型面无 ContentIdentity/
    // RevisionId 载荷（静态断言——类型即契约，EVI-01 表 1 预览行）。
    static_assert(std::is_same<std::decay_t<decltype(r1.items)>,
                               std::vector<DomainReadinessItem>>::value,
                  "报告只承载发现清单（无证据/修订对象字段）");
    static_assert(
        std::is_same<decltype(projectOf(ws, ctx, checker)),
                     evidence::ReadinessSummary>::value,
        "投影＝evidence ①级两字段值（无内容身份承诺字段）");
}

/**
 * O-39 处置自证（ACC5——现场重算纯函数投影面）：readinessSummary
 * (check(ws,ctx)) 可重入——不同校验器实例、不同调用次序同结果；无
 * 持久化就绪记录（返回值即全部产出，无 out 参数——类型面静态断言）。
 */
TEST(ReqReadiness, O39ReentrantProjectionWithoutPersistence_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "NFR-MNT-04"},
                  std::vector<std::string>{});

    const RequirementWorkingSet ws = makeHealthyWorkingSet();
    const CheckContext ctx = makeHealthyContext(ws);

    // 两个独立实例（无状态——可默认构造）交差调用同结果。
    const RequirementReadinessChecker checkerA;
    const RequirementReadinessChecker checkerB;
    const RequirementReadinessReport reportA = checkerA.check(ws, ctx);
    const evidence::ReadinessSummary sA = checkerA.readinessSummary(reportA);
    const evidence::ReadinessSummary sB = checkerB.readinessSummary(checkerB.check(ws, ctx));
    const evidence::ReadinessSummary sA2 = checkerA.readinessSummary(checkerA.check(ws, ctx));
    EXPECT_TRUE(sA == sB) << "不同实例同结果（判定无隐藏状态）";
    EXPECT_TRUE(sA == sA2) << "同实例重入同结果（现场重算可重复）";

    // 投影纯值面：evidence::ReadinessSummary 为两字段聚合（O-39 裁决的
    // "修订内不持久化就绪结论"——本类型不承载修订/身份语义，字段类型
    // 静态断言即持久化面缺席的证明）。
    static_assert(std::is_same<std::decay_t<decltype(sA.valid)>, bool>::value,
                  "valid 为标量事实（无持久化记录载荷）");
    static_assert(std::is_same<std::decay_t<decltype(sA.invalidMustItems)>,
                               std::vector<std::string>>::value,
                  "invalidMustItems 为字符串清单（无持久化记录载荷）");
}

// =====================================================================
// acceptance 7——P-REQ-6 边界声明（门控拦截归 workflow/ui）
// =====================================================================

/**
 * P-REQ-6 边界声明（ACC7）：就绪报告不含任何门控动作语义——发现项
 * 恰三字段（层/级别/诊断，结构化绑定即形状证明）；报告唯一判定查询
 * 是事实型 hasBlocking()；全部产码均为已注册稳定码（校验面板呈现面
 * 的登记完整性——门控判定所需的"动作"语义不在本单元）。
 */
TEST(ReqReadiness, PReq6ReportCarriesNoGateAction_ACC7)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "UX-12"},
                  std::vector<std::string>{});

    // 构造含三级发现的报告（Blocking＋Warning＋NotApplicable 同报）。
    RequirementWorkingSet ws = makeHealthyWorkingSet();
    ws.points.entries.front().tolerance.positionTolerance = 0.0;  // R3 Blocking
    ws.conditions.entries.clear();                                 // R5 Warning
    ws.root.conditionSetRef = std::nullopt;
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;
    const RequirementReadinessReport report = checker.check(ws, ctx);

    ASSERT_FALSE(report.items.empty());
    for (const DomainReadinessItem& item : report.items) {
        // 恰三字段：结构化绑定编译期即证明（多字段/少字段编译失败）。
        auto [layer, level, diag] = item;
        (void)layer;
        (void)level;
        // 诊断记录只携呈现语义（稳定码/定位/中文原因与建议）——无门控
        // 动作字段；建议动作文本是呈现面（校验面板仅呈现），不是阶段
        // 拦截判定。
        EXPECT_FALSE(diag.code.empty());
        EXPECT_FALSE(diag.context.empty());
        EXPECT_FALSE(diag.cause.empty());
    }
    // 报告无"阶段门控"字段：字段面＝{items, invalidMustItems}（方法仅
    // hasBlocking 事实查询——RequirementReadinessReport 定义处静态证明，
    // 此处以聚合比较复核无隐藏状态）。
    RequirementReadinessReport copy = report;
    EXPECT_TRUE(copy == report) << "报告为纯值（无门控执行态）";

    // 全部产码已注册（diagnostics StableCodeRegistry 权威——登记完整性）。
    diagnostics::StableCodeRegistry registry;
    requirements::registerRequirementCodes(registry);
    std::set<std::string> codes;
    for (const DomainReadinessItem& item : report.items) {
        codes.insert(item.diag.code);
        EXPECT_NE(registry.find(item.diag.code), nullptr)
            << "发现码必须已注册（不私定码值）: " << item.diag.code;
    }
    EXPECT_GE(codes.size(), 1U);
}

/**
 * 工作集不变量前置（R0 不变量半区——调用方契约违约 fail-fast）：绕过
 * 合法生产者直接构造违约工作集（名称重复）→std::invalid_argument，
 * 不产诊断（错误二分：调用方错误 fail-fast）。
 */
TEST(ReqReadiness, InvariantViolationFailsFast_R0Precondition)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06", "I-REQ-3"},
                  std::vector<std::string>{});

    RequirementWorkingSet ws = makeHealthyWorkingSet();
    TaskPoint second = makeHealthyPoint("P1");  // 与既有条目同名（I-REQ-3 违约）
    ws.points.entries.push_back(second);
    requirements::sortEntriesByObjectId(ws.points.entries);
    const CheckContext ctx = makeHealthyContext(ws);
    const RequirementReadinessChecker checker;

    EXPECT_THROW(checker.check(ws, ctx), std::invalid_argument);
}
