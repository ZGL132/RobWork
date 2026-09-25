/**
 * @file   ServicesTest.cpp
 * @brief  领域服务构造校验用例组（ReqServices）——createPoint/
 *         createRegion/createCondition 错误语义逐项、resolveRequiredCases
 *         §6.2 冻结 schema、五姿态规则非法参数拒绝、I-REQ-7 顺序拓扑、
 *         I-REQ-9 必验派生（任务契约 WP-14-T03 acceptance 4 的具名自证
 *         面；acceptance 1 的 I-REQ-7/9 行为面）。
 *
 * 设计依据：units/requirements.md §9.4（接口契约与 @错误 行）、§6.2（必验
 * 冻结 schema——P-EV-9 唯一实现点的 requirements 侧断言，V-05）、§5.3
 * （五规则参数合法性——V-03 构造侧）、§4.7（I-REQ-7/9）。
 */

#include <sdurws/ird/requirements/Services.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位姿/盒/采样字段的框架值类型

#include <cmath>
#include <string>
#include <vector>

using namespace std::string_view_literals;
using namespace sdurws::ird::requirements;

/// core 命名空间别名（测试内 ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 默认合法点规格（反例在其上单字段变异）。
TaskPointSpec makeSpec()
{
    TaskPointSpec spec;
    spec.name = "P1";
    spec.pose.constrainedDof.z = true;
    spec.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.1, 0.2, 0.3),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    spec.approach = TaskSegment{true, SegmentAxis::ToolZ, 0.05};
    spec.retract = TaskSegment{true, SegmentAxis::ToolZ, 0.05};
    spec.siblingNames = {"P0"};
    return spec;
}

}  // namespace

// =====================================================================
// createPoint 错误语义逐项（§9.4 @错误 行：DuplicateName|IllegalTolerance|
// ZeroVectorTarget|AllDofFree）
// =====================================================================

TEST(ReqServices, CreatePointErrorSemanticsPerItem_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01", "NFR-COR-03"},
                  std::vector<std::string>{"ACC4-createPoint"});

    const TaskPointService service;
    std::vector<core::DiagnosticRecord> diags;

    // 正例：合法规格——接受＋临时句柄分配＋work 段恒启用（§4.3）。
    auto ok = service.createPoint(makeSpec(), diags);
    ASSERT_TRUE(ok.ok) << ok.error.detail;
    EXPECT_TRUE(ok.point.objectId.isValid()) << "临时句柄（正式分配权归 project——O-36）";
    EXPECT_TRUE(ok.point.work.enabled);
    EXPECT_TRUE(diags.empty()) << "本单元服务无已登记诊断码——diags 恒空（值面返回）";

    // DuplicateName：与 siblingNames 重复——拒绝且不改写（无后缀产物）。
    TaskPointSpec dup = makeSpec();
    dup.name = "P0";
    auto r1 = service.createPoint(dup, diags);
    ASSERT_FALSE(r1.ok);
    EXPECT_EQ(r1.error.code, RequirementErrorCode::DuplicateName);

    // IllegalTolerance：位置容差非正。
    TaskPointSpec tol = makeSpec();
    tol.tolerance.positionTolerance = 0.0;
    auto r2 = service.createPoint(tol, diags);
    ASSERT_FALSE(r2.ok);
    EXPECT_EQ(r2.error.code, RequirementErrorCode::IllegalTolerance);

    // ZeroVectorTarget：PointAtTarget 零向量目标（§5.3）。
    TaskPointSpec zv = makeSpec();
    zv.pose.orientation.kind = OrientationRuleKind::PointAtTarget;
    zv.pose.orientation.targetPoint = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    auto r3 = service.createPoint(zv, diags);
    ASSERT_FALSE(r3.ok);
    EXPECT_EQ(r3.error.code, RequirementErrorCode::ZeroVectorTarget);

    // AllDofFree：constrainedDof 全 false（I-REQ-5）。
    TaskPointSpec free = makeSpec();
    free.pose.constrainedDof = ConstrainedDof{};
    auto r4 = service.createPoint(free, diags);
    ASSERT_FALSE(r4.ok);
    EXPECT_EQ(r4.error.code, RequirementErrorCode::AllDofFree);

    // 确定性：同输入（同错误面）两次拒绝同错误码同 detail（NFR-COR-01）。
    auto r5 = service.createPoint(dup, diags);
    ASSERT_FALSE(r5.ok);
    EXPECT_EQ(r5.error.code, r1.error.code);
    EXPECT_EQ(r5.error.detail, r1.error.detail);
}

// =====================================================================
// createRegion 错误语义（DegenerateRegion 族——I-REQ-6 构造面）
// =====================================================================

TEST(ReqServices, CreateRegionErrorSemantics_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03"},
                  std::vector<std::string>{"ACC4-createRegion"});

    const WorkRegionService service;
    std::vector<core::DiagnosticRecord> diags;
    WorkRegionSpec spec;
    spec.name = "R1";
    spec.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                           rw::math::Vector3D<double>(0.4, 0.4, 0.2)};
    spec.positionSampling.method = PositionSamplingMethod::GridBySpacing;
    spec.positionSampling.spacing = {0.1, 0.1, 0.1};

    // 正例：合法区域——接受。
    auto ok = service.createRegion(spec, diags);
    ASSERT_TRUE(ok.ok) << ok.error.detail;
    EXPECT_TRUE(ok.region.objectId.isValid());

    // DegenerateRegion：盒零尺寸（I-REQ-6——零体积区域）。
    WorkRegionSpec degenerate = spec;
    degenerate.box.size = rw::math::Vector3D<double>(0.4, 0.0, 0.2);
    auto r1 = service.createRegion(degenerate, diags);
    ASSERT_FALSE(r1.ok);
    EXPECT_EQ(r1.error.code, RequirementErrorCode::DegenerateRegion);

    // DegenerateRegion：覆盖率越界。
    WorkRegionSpec cov = spec;
    cov.coverageTargets.minPositionCoverage = 1.2;
    auto r2 = service.createRegion(cov, diags);
    ASSERT_FALSE(r2.ok);
    EXPECT_EQ(r2.error.code, RequirementErrorCode::DegenerateRegion);

    // DuplicateName：集合内重名。
    WorkRegionSpec dup = spec;
    dup.name = "R0";
    dup.siblingNames = {"R0"};
    auto r3 = service.createRegion(dup, diags);
    ASSERT_FALSE(r3.ok);
    EXPECT_EQ(r3.error.code, RequirementErrorCode::DuplicateName);

    // IllegalTolerance：间距非正（GridBySpacing 非法间距）。
    WorkRegionSpec spacing = spec;
    spacing.positionSampling.spacing = {0.1, -1.0, 0.1};
    auto r4 = service.createRegion(spacing, diags);
    ASSERT_FALSE(r4.ok);
    EXPECT_EQ(r4.error.code, RequirementErrorCode::IllegalTolerance);
}

// =====================================================================
// createCondition 错误语义＋适用范围校验（IllegalTolerance 族）
// =====================================================================

TEST(ReqServices, CreateConditionErrorSemantics_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04"},
                  std::vector<std::string>{"ACC4-createCondition"});

    const OperatingConditionService service;
    std::vector<core::DiagnosticRecord> diags;
    OperatingConditionSpec spec;
    spec.name = "C1";
    spec.targetCycleTimeS = 18.0;

    // 正例：合法工况——接受。
    auto ok = service.createCondition(spec, diags);
    ASSERT_TRUE(ok.ok) << ok.error.detail;

    // IllegalTolerance：appliesTo.scope=Stations 且清单空（§4.7 非法组合）。
    OperatingConditionSpec emptyStations = spec;
    emptyStations.appliesTo.scope = AppliesToScope::Stations;
    auto r1 = service.createCondition(emptyStations, diags);
    ASSERT_FALSE(r1.ok);
    EXPECT_EQ(r1.error.code, RequirementErrorCode::IllegalTolerance);

    // 正例：None＋enabled 合法（§6.1——"当前不适用"的显式标记，不阻断）。
    OperatingConditionSpec noneScope = spec;
    noneScope.appliesTo.scope = AppliesToScope::None;
    auto r2 = service.createCondition(noneScope, diags);
    ASSERT_TRUE(r2.ok) << r2.error.detail;

    // DuplicateName：重名。
    OperatingConditionSpec dup = spec;
    dup.name = "C0";
    dup.siblingNames = {"C0"};
    auto r3 = service.createCondition(dup, diags);
    ASSERT_FALSE(r3.ok);
    EXPECT_EQ(r3.error.code, RequirementErrorCode::DuplicateName);
}

// =====================================================================
// resolveRequiredCases——§6.2 冻结 schema 逐条一致（P-EV-9 唯一实现点；
// V-05 requirements 侧断言；evidence EV-CASET 对端断言属联合观测不写
// 为通过——契约 acceptance 4 原文口径）
// =====================================================================

TEST(ReqServices, ResolveRequiredCasesFrozenSchema_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04", "P-EV-9"},
                  std::vector<std::string>{"I-REQ-9", "ACC4-resolve"});

    const OperatingConditionService service;
    // 四象限工况：enabled∧Must（必验）/enabled∧Should/未启用∧Must/未启用∧Should。
    std::vector<OperatingCondition> conds;
    auto make = [&](const char* name, RequirementLevel level, bool enabled) {
        OperatingCondition c;
        c.objectId = core::ObjectId::generate();
        c.name = name;
        c.level = level;
        c.enabled = enabled;
        return c;
    };
    // 乱序输入（解析必须规范化——确定性）。
    conds.push_back(make("C-should-on", RequirementLevel::Should, true));
    conds.push_back(make("C-must-on", RequirementLevel::Must, true));
    conds.push_back(make("C-must-off", RequirementLevel::Must, false));
    conds.push_back(make("C-should-off", RequirementLevel::Should, false));

    const auto res = service.resolveRequiredCases(conds);

    // schema 行 1：mandatory ≡ (level==Must)——逐条投影核对（I-REQ-9）。
    ASSERT_EQ(res.entries.size(), 4U);
    for (const auto& e : res.entries) {
        const OperatingCondition* src = nullptr;
        for (const auto& c : conds) {
            if (c.objectId == e.caseId) { src = &c; }
        }
        ASSERT_NE(src, nullptr);
        EXPECT_EQ(e.label, src->name) << "schema：label := name";
        EXPECT_EQ(e.enabled, src->enabled) << "schema：enabled := enabled";
        EXPECT_EQ(e.mandatory, src->level == RequirementLevel::Must)
            << "schema：mandatory := (level==Must)——无独立第三必验开关";
    }
    // schema 行 2：必验集合＝enabled∧Must——恰 1 条（C-must-on）。
    EXPECT_EQ(res.requiredCount, 1U);
    std::size_t mustOn = 0;
    for (const auto& e : res.entries) {
        if (e.enabled && e.mandatory) { ++mustOn; }
    }
    EXPECT_EQ(mustOn, res.requiredCount) << "entries 与 requiredCount 口径一致";

    // 确定性：乱序输入两次解析投影逐字段一致（§6.1"本单元保证 entries
    // 解析确定性"——evidence requiredCaseSetId 对账前提）。
    std::vector<OperatingCondition> shuffled(conds.rbegin(), conds.rend());
    const auto res2 = service.resolveRequiredCases(shuffled);
    EXPECT_EQ(res, res2) << "同集合任意输入序必得同投影（规范序）";
}

// =====================================================================
// 五姿态规则非法参数构造边界拒绝（V-03 构造侧——acceptance 4 原文：
// 零向量/rollRange 逆序/缺 feature/非有限角）
// =====================================================================

TEST(ReqServices, OrientationRuleIllegalParamsRejected_WP14T03_ACC4)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-09"},
                  std::vector<std::string>{"ACC4-orientation-rules"});

    const TaskPointService service;
    std::vector<core::DiagnosticRecord> diags;

    // Fixed 非有限角——拒绝。
    TaskPointSpec badFixed = makeSpec();
    badFixed.pose.orientation.kind = OrientationRuleKind::Fixed;
    badFixed.pose.orientation.fixedRpy = rw::math::Vector3D<double>(
        0.0, std::nan(""), 0.0);
    auto r1 = service.createPoint(badFixed, diags);
    ASSERT_FALSE(r1.ok);

    // AlignFrame 引用结构违约（缺载荷）——拒绝。
    TaskPointSpec badFrame = makeSpec();
    badFrame.pose.orientation.kind = OrientationRuleKind::AlignFrame;
    // targetFrame 缺省 World——非 ModelFrame/SceneObject，违约。
    auto r2 = service.createPoint(badFrame, diags);
    ASSERT_FALSE(r2.ok);

    // AlignFrame 正例（合法 ModelFrame 目标）——通过。
    TaskPointSpec goodFrame = makeSpec();
    goodFrame.pose.orientation.kind = OrientationRuleKind::AlignFrame;
    goodFrame.pose.orientation.targetFrame.kind = RequirementRefKind::ModelFrame;
    goodFrame.pose.orientation.targetFrame.objectId = core::ObjectId::generate();
    auto r3 = service.createPoint(goodFrame, diags);
    ASSERT_TRUE(r3.ok) << r3.error.detail;

    // AlignGeometryNormal 缺 feature——拒绝（acceptance 4"缺 feature"）。
    TaskPointSpec badFeature = makeSpec();
    badFeature.pose.orientation.kind = OrientationRuleKind::AlignGeometryNormal;
    badFeature.pose.orientation.targetSceneObject = core::ObjectId::generate();
    auto r4 = service.createPoint(badFeature, diags);
    ASSERT_FALSE(r4.ok);

    // PointAtTarget 零向量——ZeroVectorTarget（前组已钉；此处正例补充：
    // 非零目标通过）。
    TaskPointSpec goodPoint = makeSpec();
    goodPoint.pose.orientation.kind = OrientationRuleKind::PointAtTarget;
    goodPoint.pose.orientation.targetPoint = rw::math::Vector3D<double>(1.0, 0.0, 0.0);
    auto r5 = service.createPoint(goodPoint, diags);
    ASSERT_TRUE(r5.ok) << r5.error.detail;

    // ToolRollFree rollRange 逆序——拒绝（acceptance 4"rollRange 逆序"）。
    TaskPointSpec badRoll = makeSpec();
    badRoll.pose.orientation.kind = OrientationRuleKind::ToolRollFree;
    badRoll.pose.orientation.rollRange = RollRange{1.0, -1.0};
    auto r6 = service.createPoint(badRoll, diags);
    ASSERT_FALSE(r6.ok);

    // ToolRollFree 正例：默认 [−π,π] 通过；等值区间（空）拒绝。
    TaskPointSpec goodRoll = makeSpec();
    goodRoll.pose.orientation.kind = OrientationRuleKind::ToolRollFree;
    auto r7 = service.createPoint(goodRoll, diags);
    ASSERT_TRUE(r7.ok) << r7.error.detail;
    TaskPointSpec emptyRoll = makeSpec();
    emptyRoll.pose.orientation.kind = OrientationRuleKind::ToolRollFree;
    emptyRoll.pose.orientation.rollRange = RollRange{0.5, 0.5};
    auto r8 = service.createPoint(emptyRoll, diags);
    ASSERT_FALSE(r8.ok);
}

// =====================================================================
// I-REQ-7 行为面：checkSequence 拓扑校验（无环/无重复/无悬空——R7 面）
// =====================================================================

namespace {

/// 构造顺序链任务点（name→sequenceKey 前驱名）。
TaskPoint pointWith(const std::string& name, const std::string& prev)
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = name;
    p.pose.constrainedDof.z = true;
    if (!prev.empty()) {
        p.sequenceKey = prev;
    }
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

}  // namespace

TEST(ReqServices, IREQ7_SequenceAcyclicPositive_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-02"},
                  std::vector<std::string>{"I-REQ-7"});

    const TaskPointService service;
    // 正例：线性链 A→B→C（B 前驱 A、C 前驱 B）＋无序键点——无违例。
    std::vector<TaskPoint> chain = {
        pointWith("A", ""),
        pointWith("B", "A"),
        pointWith("C", "B"),
        pointWith("Solo", ""),
    };
    const auto res = service.checkSequence(chain);
    EXPECT_TRUE(res.acyclic);
    EXPECT_TRUE(res.duplicateKeys.empty());
    EXPECT_TRUE(res.danglingKeys.empty());
    EXPECT_TRUE(res.cycleNodes.empty());
}

TEST(ReqServices, IREQ7_SequenceCycleAndDuplicateAndDangling_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-02"},
                  std::vector<std::string>{"I-REQ-7"});

    const TaskPointService service;
    // 反例 1：环 A→B→A（A 前驱 B、B 前驱 A）——环检出。
    std::vector<TaskPoint> cycle = {
        pointWith("A", "B"),
        pointWith("B", "A"),
    };
    const auto r1 = service.checkSequence(cycle);
    EXPECT_FALSE(r1.acyclic);
    EXPECT_EQ(r1.cycleNodes.size(), 2U) << "环上两节点全部报出";

    // 反例 2：重复键（两条目同前驱 A）——重复检出。
    std::vector<TaskPoint> dup = {
        pointWith("A", ""),
        pointWith("B", "A"),
        pointWith("C", "A"),
    };
    const auto r2 = service.checkSequence(dup);
    EXPECT_FALSE(r2.acyclic);
    ASSERT_EQ(r2.duplicateKeys.size(), 1U);
    EXPECT_EQ(r2.duplicateKeys[0], "A");

    // 反例 3：悬空前驱（引用不存在的条目名）——悬空检出。
    std::vector<TaskPoint> dangling = {
        pointWith("A", "Ghost"),
    };
    const auto r3 = service.checkSequence(dangling);
    EXPECT_FALSE(r3.acyclic);
    ASSERT_EQ(r3.danglingKeys.size(), 1U);
    EXPECT_EQ(r3.danglingKeys[0], "Ghost");

    // 确定性：同输入两次校验同结果（NFR-COR-01/02）。
    EXPECT_EQ(service.checkSequence(cycle), r1);
}

// =====================================================================
// O-36 口径（acceptance 5）：子条目 ObjectId 为模型内标识——诊断
// subjectObjectId 锚与该口径一致性（集合字节内嵌面由 CodecTest roundtrip
// 钉；此处钉服务侧：临时句柄由服务分配、编辑器 upsert 不改 id）。
// =====================================================================

TEST(ReqServices, O36_SubEntryObjectIdModelInternalAnchor_WP14T03_ACC5)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04"},
                  std::vector<std::string>{"ACC5-O36"});

    const TaskPointService service;
    std::vector<core::DiagnosticRecord> diags;
    // 服务产出的临时句柄：唯一（两次生成不同）＋有效——模型内标识面。
    auto a = service.createPoint(makeSpec(), diags);
    auto b = service.createPoint(makeSpec(), diags);
    ASSERT_TRUE(a.ok);
    ASSERT_TRUE(b.ok);
    EXPECT_TRUE(a.point.objectId.isValid());
    EXPECT_FALSE(a.point.objectId == b.point.objectId) << "临时句柄唯一（碰撞面）";
    // 口径断言：条目 id 不入 objectRefs 复核范围——五真实存储对象 token
    // 恰为 ObjectTypes 登记簿五值（scope 面；objectRefs 复核范围＝五个
    // 真实存储对象，2026-09-22 O-36 裁决）。
    EXPECT_EQ(kReqSetObjectType, "req-set"sv);
    EXPECT_EQ(kReqPointSetObjectType, "req-point-set"sv);
    EXPECT_EQ(kReqRegionSetObjectType, "req-region-set"sv);
    EXPECT_EQ(kReqConditionSetObjectType, "req-condition-set"sv);
    EXPECT_EQ(kReqPlanSetObjectType, "req-plan-set"sv);
}
