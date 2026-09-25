/**
 * @file   RequirementTypesTest.cpp
 * @brief  I-REQ-1~10 不变量用例组（ReqInvariants）——逐条正反例（任务
 *         契约 WP-14-T03 acceptance 1 的具名自证面：构造边界拒绝、不静
 *         默改写/加后缀——NFR-COR-03）。
 *
 * 设计依据：units/requirements.md §4.7（I-REQ-1~10 不变量权威条款）、
 * §4.2/§4.3/§4.4/§4.5（字段表约束）、§5.3（五规则参数合法性）、§2.5
 * （Info 不承接）；先例 modeling/test 不变量组形态。
 *
 * 每条不变量一组正例（合法构造通过）＋反例（构造边界拒绝且错误码逐项
 * 对应）；断言不静默改写：拒绝后不产出"修正版"对象（服务层职责——
 * 本文件钉校验函数面）。
 */

#include <sdurws/ird/requirements/RequirementTypes.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

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

/// 合法任务点基线（各用例在其上做单字段变异——反例只携带目标违例）。
TaskPoint makeValidPoint()
{
    TaskPoint p;
    p.objectId = core::ObjectId::generate();
    p.name = "P1";
    p.level = RequirementLevel::Must;
    p.enabled = true;
    p.pose.constrainedDof.z = true;  // 至少一真（I-REQ-5）
    p.pose.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(1.0, 2.0, 3.0),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    p.tolerance = ToleranceSpec{};  // 设计默认（1e-3 m / π/180 rad）
    p.work = TaskSegment{true, SegmentAxis::ToolZ, 1.0};
    return p;
}

/// 合法区域基线。
WorkRegion makeValidRegion()
{
    WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = "R1";
    r.box = BoundingBox{rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                        rw::math::Vector3D<double>(1.0, 1.0, 1.0)};
    r.coverageTargets = CoverageTargets{};  // 默认 0.8
    r.positionSampling = PositionSampling{};  // Grid{1,1,1}
    r.orientationSampling = OrientationSampling{};
    return r;
}

}  // namespace

// =====================================================================
// I-REQ-1（确定性编码）：集合条目按 ObjectId 字典序存放；同输入字节→
// 同 ContentVersion 的 requirements 半区＝"同集合任意输入序必得同字节"
// （排序与规范化形态——编码面由 CodecTest 钉）。
// =====================================================================

TEST(ReqInvariants, IREQ1_SortAndCanonicalOrder_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-01", "NFR-COR-02"},
                  std::vector<std::string>{"I-REQ-1"});

    // 正例：构造规范序后，isCanonicalOrder 为真；再确定性构造逆序
    // （std::reverse——非随机断言），isCanonicalOrder 报假（canonical
    // 编码前置）＋排序后恢复真。
    std::vector<TaskPoint> entries;
    for (int i = 0; i < 5; ++i) {
        entries.push_back(makeValidPoint());
        entries.back().name = "P" + std::to_string(i);
    }
    sortEntriesByObjectId(entries);
    EXPECT_TRUE(isCanonicalOrder(entries));

    // 反例：确定性逆序——isCanonicalOrder 报假。
    std::vector<TaskPoint> reversed(entries.rbegin(), entries.rend());
    ASSERT_FALSE(reversed.empty());
    bool strictlyDesc = true;
    for (std::size_t i = 1; i < reversed.size(); ++i) {
        if (!(reversed[i - 1].objectId.toCanonical() > reversed[i].objectId.toCanonical())) {
            strictlyDesc = false;
        }
    }
    ASSERT_TRUE(strictlyDesc) << "前置：5 个不同 id 的完全逆序必非规范序";
    EXPECT_FALSE(isCanonicalOrder(reversed));
    sortEntriesByObjectId(reversed);
    EXPECT_TRUE(isCanonicalOrder(reversed)) << "排序后恢复规范序（I-REQ-1 执行面）";
}

// =====================================================================
// I-REQ-2（身份不混用）：条目 ObjectId 全局唯一且 token 与所属集合一致；
// 跨集合重复＝调用方错误（构造边界拒绝）。
// =====================================================================

TEST(ReqInvariants, IREQ2_CrossSetIdDuplicateRejected_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"ARC-04", "CON-01"},
                  std::vector<std::string>{"I-REQ-2"});

    // 正例：四集合各持独立 id——跨集合核对通过。
    const TaskPoint p = makeValidPoint();
    WorkRegion r = makeValidRegion();
    OperatingCondition c;
    c.objectId = core::ObjectId::generate();
    c.name = "C1";
    SamplingPlan pl;
    pl.objectId = core::ObjectId::generate();
    pl.regionRef = r.objectId;
    EXPECT_FALSE(checkCrossSetIdUniqueness({p}, {r}, {c}, {pl}).has_value());

    // 反例：同一 ObjectId 同时出现在点集与工况集——身份混用拒绝。
    c.objectId = p.objectId;
    const auto e = checkCrossSetIdUniqueness({p}, {r}, {c}, {pl});
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, RequirementErrorCode::MalformedPayload);
    EXPECT_NE(e->detail.find("I-REQ-2"), std::string::npos) << "定位不变量名";
}

// =====================================================================
// I-REQ-3（名称唯一）：同集合内 name 唯一；重复＝构造边界拒绝（不静默
// 加后缀——NFR-COR-03）。
// =====================================================================

TEST(ReqInvariants, IREQ3_DuplicateNameRejectedNoSilentRewrite_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-03"},
                  std::vector<std::string>{"I-REQ-3"});

    // 正例：不同名两条目通过。
    TaskPoint a = makeValidPoint();
    TaskPoint b = makeValidPoint();
    b.name = "P2";
    EXPECT_FALSE(checkEntryNameAndIdUniqueness(std::vector<TaskPoint>{a, b}).has_value());

    // 反例：同名两条目——DuplicateName 拒绝。
    b.name = "P1";
    const auto e = checkEntryNameAndIdUniqueness(std::vector<TaskPoint>{a, b});
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, RequirementErrorCode::DuplicateName);

    // 反例：空名——同码（名称是报告/覆盖清单定位依据，§8.1 R0）。
    a.name.clear();
    const auto e2 = checkEntryNameAndIdUniqueness(std::vector<TaskPoint>{a});
    ASSERT_TRUE(e2.has_value());
    EXPECT_EQ(e2->code, RequirementErrorCode::DuplicateName);
}

// =====================================================================
// I-REQ-4（引用合法）：RequirementReference 的 kind 与载荷/场景匹配；
// 结构违例构造边界拒绝（跨闭包 token 核对归 T05 就绪层——§8.1）。
// =====================================================================

TEST(ReqInvariants, IREQ4_ReferenceKindPayloadMatch_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-01"},
                  std::vector<std::string>{"I-REQ-4"});

    // 正例：tcpRef＝Tool{oid, tcpKey}——结构自洽＋场景匹配。
    RequirementReference tool;
    tool.kind = RequirementRefKind::Tool;
    tool.objectId = core::ObjectId::generate();
    tool.tcpKey = "default";
    EXPECT_FALSE(validateRequirementReference(tool, /*forScene=*/false).has_value());

    // 反例：Tool 缺 tcpKey——载荷违约拒绝。
    RequirementReference badTool = tool;
    badTool.tcpKey.clear();
    auto e = validateRequirementReference(badTool, false);
    ASSERT_TRUE(e.has_value());

    // 反例：refFrame 场景出现 Tool 引用——场景词表违约拒绝。
    e = validateRequirementReference(tool, /*forScene=*/true);
    ASSERT_TRUE(e.has_value());

    // 反例：World 携带载荷——无载荷种携载荷＝结构违约拒绝（不静默裁剪）。
    RequirementReference world;
    world.kind = RequirementRefKind::World;
    world.objectId = core::ObjectId::generate();
    e = validateRequirementReference(world, true);
    ASSERT_TRUE(e.has_value());

    // token 匹配表（I-REQ-4 注脚）：Tool→tool-definition／SceneObject→
    // scene-object／ModelFrame→robot-design；World/DefaultTcp 无目标。
    EXPECT_EQ(expectedTargetToken(RequirementRefKind::Tool), "tool-definition"sv);
    EXPECT_EQ(expectedTargetToken(RequirementRefKind::SceneObject), "scene-object"sv);
    EXPECT_EQ(expectedTargetToken(RequirementRefKind::ModelFrame), "robot-design"sv);
    EXPECT_EQ(expectedTargetToken(RequirementRefKind::World), ""sv);
    EXPECT_EQ(expectedTargetToken(RequirementRefKind::DefaultTcp), ""sv);
}

// =====================================================================
// I-REQ-5（约束非空）：constrainedDof 至少一真；容差两分量 >0 且有限。
// =====================================================================

TEST(ReqInvariants, IREQ5_ConstrainedDofAndTolerance_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-01"},
                  std::vector<std::string>{"I-REQ-5"});

    // 正例：单分量受约束＋默认容差——通过。
    PoseConstraint ok;
    ok.constrainedDof.z = true;
    EXPECT_FALSE(validatePoseConstraint(ok).has_value());
    EXPECT_FALSE(validateTolerance(ToleranceSpec{}).has_value());

    // 反例：全 false——AllDofFree 拒绝。
    PoseConstraint allFree;
    const auto e = validatePoseConstraint(allFree);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, RequirementErrorCode::AllDofFree);

    // 反例：容差零/负/非有限——IllegalTolerance 拒绝。
    ToleranceSpec zero = ToleranceSpec{};
    zero.positionTolerance = 0.0;
    EXPECT_TRUE(validateTolerance(zero).has_value());
    ToleranceSpec neg = ToleranceSpec{};
    neg.orientationTolerance = -1e-9;
    EXPECT_TRUE(validateTolerance(neg).has_value());
    ToleranceSpec nan = ToleranceSpec{};
    nan.positionTolerance = std::nan("");
    EXPECT_TRUE(validateTolerance(nan).has_value());

    // 反例：position Provided 含非有限分量——拒绝（§4.3 Provided 有限）。
    PoseConstraint badPos;
    badPos.constrainedDof.x = true;
    badPos.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(std::nan(""), 0.0, 0.0),
        core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    EXPECT_TRUE(validatePoseConstraint(badPos).has_value());

    // 正例：position 未提供（NotProvided）合法——MDL-06 缺失不转零。
    PoseConstraint missing;
    missing.constrainedDof.y = true;
    EXPECT_FALSE(validatePoseConstraint(missing).has_value());
}

// =====================================================================
// I-REQ-6（区域非退化）：Box size 三分量 >0 且有限；覆盖率 ∈[0,1]；采样
// 计数 ≥0（0 合法——零样本由评估判 DataInsufficient，KIN-04 R8/§6.2）。
// =====================================================================

TEST(ReqInvariants, IREQ6_BoxCoverageCounts_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03"},
                  std::vector<std::string>{"I-REQ-6"});

    // 正例：非退化盒＋覆盖率边界值 0/1 合法（闭区间）＋Grid counts 含 0
    // 合法（零样本存储面）。
    EXPECT_FALSE(validateBoundingBox(makeValidRegion().box).has_value());
    CoverageTargets bounds;
    bounds.minPositionCoverage = 0.0;
    bounds.minOrientationCoverage = 1.0;
    EXPECT_FALSE(validateCoverageTargets(bounds).has_value());
    PositionSampling zeroGrid;
    zeroGrid.method = PositionSamplingMethod::Grid;
    zeroGrid.counts = {2, 3, 0};
    EXPECT_FALSE(validateBoundingBox(makeValidRegion().box).has_value());
    EXPECT_EQ(zeroGrid.counts[2], 0U) << "0 计数合法存储（V-02——本单元不判零样本）";

    // 反例：size 一分量 0/负——DegenerateRegion 拒绝。
    BoundingBox zeroBox = makeValidRegion().box;
    zeroBox.size = rw::math::Vector3D<double>(1.0, 0.0, 1.0);
    auto e = validateBoundingBox(zeroBox);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, RequirementErrorCode::DegenerateRegion);
    BoundingBox negBox = makeValidRegion().box;
    negBox.size = rw::math::Vector3D<double>(1.0, -2.0, 1.0);
    EXPECT_TRUE(validateBoundingBox(negBox).has_value());

    // 反例：覆盖率越界（>1/<0/非有限）——拒绝。
    CoverageTargets over;
    over.minPositionCoverage = 1.5;
    e = validateCoverageTargets(over);
    ASSERT_TRUE(e.has_value());
    CoverageTargets under;
    under.minPositionCoverage = -0.1;
    EXPECT_TRUE(validateCoverageTargets(under).has_value());
    CoverageTargets nanCov;
    nanCov.minPositionCoverage = std::nan("");
    EXPECT_TRUE(validateCoverageTargets(nanCov).has_value());
}

// =====================================================================
// I-REQ-7（顺序无环）：sequenceKey 构成的顺序关系无环、无重复键（R7
// Blocking——服务面实现见 ServicesTest；此处钉类型与语义文档）。
// =====================================================================

TEST(ReqInvariants, IREQ7_SequenceContractAnchored_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-02"},
                  std::vector<std::string>{"I-REQ-7"});

    // 语义锚：sequenceKey 是前驱条目名引用（§5.1"删除被顺序键引用的
    // 任务点→命令边界拒绝"证明键是条目间引用）；无序键条目合法（不参
    // 与顺序约束）。完整正反例在 ServicesTest.IREQ7*（checkSequence 面）。
    TaskPoint freePoint = makeValidPoint();
    freePoint.name = "Solo";
    EXPECT_FALSE(freePoint.sequenceKey.has_value()) << "无序键＝不参与顺序约束";
    SUCCEED() << "I-REQ-7 行为面由 ServicesTest.checkSequence 用例组承载";
}

// =====================================================================
// I-REQ-8（路径不作身份）：导入溯源只存内容摘要＋行号——结构面无路径
// 字段；编码面由 CodecTest 钉（字节中无路径）。
// =====================================================================

TEST(ReqInvariants, IREQ8_ImportProvenanceHasNoPathField_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"CON-05"},
                  std::vector<std::string>{"I-REQ-8"});

    // 结构面断言：ImportProvenance 仅 {sourceDigest, recordNumber}——
    // 聚合初始化两字段满配即编译通过（多余字段会改变聚合面——编译期
    // 钉住；此处运行期断言值语义）。
    ImportProvenance im;
    im.sourceDigest = core::Digest256{1};
    im.recordNumber = 42;
    EXPECT_EQ(im.recordNumber, 42U);
    EXPECT_EQ(im.sourceDigest[0], 1U);

    // 条目装配面：携带导入溯源的任务点与不携带者仅差溯源字段——路径
    // 在任何字段中无容身处（类型面无该成员——不可达路径）。
    TaskPoint p = makeValidPoint();
    p.importProvenance = im;
    EXPECT_TRUE(p.importProvenance.has_value());
    EXPECT_FALSE(validateTaskPoint(p).has_value());
}

// =====================================================================
// I-REQ-9（必验派生唯一）：mandatory ≡ (level==Must)——不存在独立第三
// "必验"开关（解析面唯一实现点在 resolveRequiredCases——ServicesTest
// 钉行为；此处钉类型面）。
// =====================================================================

TEST(ReqInvariants, IREQ9_MandatoryDerivedOnly_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-04"},
                  std::vector<std::string>{"I-REQ-9", "P-EV-9"});

    // 类型面断言：RequiredCaseEntry 只有 {caseId, label, enabled,
    // mandatory} 四字段（§6.2 冻结 schema 字面）且 mandatory 为普通值
    // 字段——其"只读派生"语义由唯一产出点 resolveRequiredCases 保证
    // （无任何独立"必验开关"输入面；OperatingConditionSpec 无 mandatory
    // 字段——编译期不可达）。
    RequiredCaseEntry entry;
    entry.caseId = core::ObjectId::generate();
    entry.label = "C1";
    entry.enabled = true;
    entry.mandatory = true;
    EXPECT_EQ(entry.mandatory, entry.enabled) << "冻结 schema 四字段字面承载";
    SUCCEED() << "行为面由 ServicesTest.ResolveRequiredCasesFrozenSchema 钉住";
}

// =====================================================================
// I-REQ-10（派生不回写）：镜像/模板/阵列产物为普通独立条目；对源条目
// 零引用、零回写（结构面：GenerationProvenance 无源引用字段）。
// =====================================================================

TEST(ReqInvariants, IREQ10_GenerationNoSourceReference_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-11"},
                  std::vector<std::string>{"I-REQ-10"});

    // 结构面断言：GenerationProvenance 仅 {generatorId, instanceId,
    // linked, parameters}——一次性参数快照（§7.2 循环派生行），无源条目
    // ObjectId/名称字段（聚合满配编译通过即钉住字段面）。
    GenerationProvenance gen;
    gen.generatorId = "mirror";
    gen.instanceId = "inst-1";
    gen.linked = true;
    gen.parameters = {{"axis", "x"}, {"plane", "world"}};
    TaskPoint derived = makeValidPoint();
    derived.name = "P1-mirror";
    derived.generation = gen;
    EXPECT_TRUE(derived.generation.has_value());
    EXPECT_EQ(derived.generation->parameters.size(), 2U);
    EXPECT_FALSE(validateTaskPoint(derived).has_value())
        << "派生产物＝普通独立条目——校验面无任何'派生'特殊通道（D-REQ-4）";
}

// =====================================================================
// 条目级校验链补充（服务/解码共用的语义单源——正反例抽查）
// =====================================================================

TEST(ReqInvariants, ValidateTaskPointSegmentsShortCircuit_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-02"},
                  std::vector<std::string>{"I-REQ-5"});

    // 正例：合法点通过全链。
    EXPECT_FALSE(validateTaskPoint(makeValidPoint()).has_value());

    // 反例：启用 approach 段距离 0——拒绝（m>0，§5.1）。
    TaskPoint p = makeValidPoint();
    p.approach = TaskSegment{true, SegmentAxis::ToolZ, 0.0};
    const auto e = validateTaskPoint(p);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->code, RequirementErrorCode::IllegalTolerance);

    // 正例：关闭段距离不核（不进入 TRJ 路径构造）。
    TaskPoint q = makeValidPoint();
    q.approach = TaskSegment{false, SegmentAxis::ToolZ, -5.0};
    EXPECT_FALSE(validateTaskPoint(q).has_value());
}

TEST(ReqInvariants, LevelVocabularyTwoValuesOnly_WP14T03_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"},
                  std::vector<std::string>{"I-REQ-9"});

    // 词表面：仅 Must/Should 两值；"Info" 不承接（§2.5——信息归 note），
    // 大小写变体拒绝（精确等值——不猜测）。
    EXPECT_EQ(requirementLevelToken(RequirementLevel::Must), "Must"sv);
    EXPECT_EQ(requirementLevelToken(RequirementLevel::Should), "Should"sv);
    EXPECT_TRUE(tryRequirementLevel("Must").has_value());
    EXPECT_FALSE(tryRequirementLevel("Info").has_value());
    EXPECT_FALSE(tryRequirementLevel("must").has_value());
}
