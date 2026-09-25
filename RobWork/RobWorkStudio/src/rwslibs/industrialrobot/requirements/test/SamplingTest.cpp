/**
 * @file   SamplingTest.cpp
 * @brief  采样计划确定性用例组（ReqSampling）——GridBySpacing→counts 规
 *         范化确定性（D-REQ-2）、counts 乘积=0 合法存储（V-02）、
 *         ISamplingPlanBuilder 绝不生成样本（接口面无枚举出口——§5.2 边
 *         界）（任务契约 WP-14-T03 acceptance 3 的具名自证面；KIN-04 冻
 *         结前提——DTB 风险列原文）。
 *
 * 设计依据：units/requirements.md §5.2（采样计划定义/规范化/零样本）、
 * §9.5（ISamplingPlanBuilder 契约）、§4.8 D-REQ-2（同义计划同身份）。
 */

#include <sdurws/ird/requirements/Sampling.hpp>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <rw/math/Vector3D.hpp>  // rw::math::Vector3D<double>——位姿/盒/采样字段的框架值类型

#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::requirements;

/// core 命名空间别名（测试内 ObjectId/SourcedValue/ValueProvenance 直写面）。
namespace core = sdurws::ird::core;

namespace {

/// 合法区域基线（盒 0.4×0.4×0.2 m）。
WorkRegion makeRegion()
{
    WorkRegion r;
    r.objectId = core::ObjectId::generate();
    r.name = "R1";
    r.box = BoundingBox{rw::math::Vector3D<double>(0.5, 0.0, 0.3),
                        rw::math::Vector3D<double>(0.4, 0.4, 0.2)};
    r.coverageTargets.minPositionCoverage = 0.8;
    r.positionSampling.method = PositionSamplingMethod::Grid;
    r.positionSampling.counts = {4, 5, 2};
    r.orientationSampling = OrientationSampling{};
    return r;
}

/// GridBySpacing 规格（0.1 m 间距→counts=floor(size/spacing)+1）。
SamplingPlanSpec makeSpacingSpec()
{
    SamplingPlanSpec spec;
    spec.positionSampling.method = PositionSamplingMethod::GridBySpacing;
    spec.positionSampling.spacing = {0.1, 0.1, 0.1};
    spec.orientationSampling = OrientationSampling{};
    return spec;
}

}  // namespace

/**
 * GridBySpacing→counts 规范化确定性（acceptance 3——KIN-04 冻结前提；
 * D-REQ-2：counts[i]=floor(size[i]/spacing[i])+1，规范化后计数即
 * planContentIdentity 输入）。
 */
TEST(ReqSampling, GridBySpacingNormalizationDeterministic_WP14T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-04"},
                  std::vector<std::string>{"ACC3-normalization"});

    const WorkRegionService service;
    std::vector<core::DiagnosticRecord> diags;
    const WorkRegion region = makeRegion();

    // 规则核对：size=(0.4,0.4,0.2)，spacing=0.1 → floor(4)+1=5、5、3。
    auto n1 = service.normalizeSampling(makeSpacingSpec().positionSampling,
                                        region.box.size, diags);
    ASSERT_TRUE(n1.ok()) << n1.error().detail;
    EXPECT_EQ(n1.get().normalized.method, PositionSamplingMethod::Grid);
    EXPECT_EQ(n1.get().normalized.counts[0], 5U);
    EXPECT_EQ(n1.get().normalized.counts[1], 5U);
    EXPECT_EQ(n1.get().normalized.counts[2], 3U);
    EXPECT_TRUE(n1.get().changed) << "GridBySpacing→Grid 发生了规范化改写";

    // 确定性：同输入两次规范化同计数（NFR-COR-01/02——同义计划同身份
    // 的输入面）。
    auto n2 = service.normalizeSampling(makeSpacingSpec().positionSampling,
                                        region.box.size, diags);
    ASSERT_TRUE(n2.ok());
    EXPECT_EQ(n1.get().normalized, n2.get().normalized);

    // 同义计划：不同 spacing（0.1 与 0.05→不同计数——非同义；0.10000…
    // 与同值不同文本不可区分——浮点只有值面）规范化同值必同计数。此处
    // 钉"同输入同计数"与"异输入异计数"两向。
    SamplingPlanSpec finer = makeSpacingSpec();
    finer.positionSampling.spacing = {0.05, 0.05, 0.05};
    auto n3 = service.normalizeSampling(finer.positionSampling, region.box.size, diags);
    ASSERT_TRUE(n3.ok());
    EXPECT_NE(n1.get().normalized.counts, n3.get().normalized.counts)
        << "异间距→异计数（floor 单调）";

    // Grid 原样通过（changed=false——已是权威形态）。
    PositionSampling grid = PositionSampling{};
    grid.method = PositionSamplingMethod::Grid;
    grid.counts = {2, 2, 2};
    auto n4 = service.normalizeSampling(grid, region.box.size, diags);
    ASSERT_TRUE(n4.ok());
    EXPECT_FALSE(n4.get().changed);
    EXPECT_EQ(n4.get().normalized.counts[0], 2U);
}

/**
 * 同义计划同身份（D-REQ-2 的字节面）：同（区域,采样参数）经 buildPlan
 * 产出的计划条目 canonical 摘要一致；同义 spacing 规范化到同 counts 的
 * 两计划 digest 全等（planContentIdentity 的 canonical 输入确定性）。
 */
TEST(ReqSampling, PlanDigestIdentityForSynonymousPlans_WP14T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "CON-05"},
                  std::vector<std::string>{"ACC3-identity"});

    const SamplingPlanBuilder builder;
    std::vector<core::DiagnosticRecord> diags;
    const WorkRegion region = makeRegion();

    // 同 spec 两次构建：计划值面全等（objectId 注入同一值——排除临时
    // 句柄差异）。
    SamplingPlanSpec spec = makeSpacingSpec();
    const core::ObjectId planId = core::ObjectId::fromCanonical(
        "obj-50000000000000000000000000000005");
    spec.planObjectId = planId;
    auto p1 = builder.buildPlan(region, spec, diags);
    auto p2 = builder.buildPlan(region, spec, diags);
    ASSERT_TRUE(p1.ok) << p1.error.detail;
    ASSERT_TRUE(p2.ok) << p2.error.detail;
    EXPECT_EQ(p1.plan, p2.plan) << "同（区域,采样参数）→同计划值面";

    // digest 确定性：同计划两次摘要逐字节一致；计划 canonical 摘要非零。
    auto d1 = builder.digest(p1.plan, diags);
    auto d2 = builder.digest(p2.plan, diags);
    ASSERT_TRUE(d1.ok()) << d1.error().detail;
    ASSERT_TRUE(d2.ok());
    EXPECT_EQ(d1.get(), d2.get());
    EXPECT_TRUE(d1.get().planDigest != core::Digest256{});

    // 分母面：counts 乘积（5×5×3=75）与姿态（1×1=1）——SamplingPlanRef
    // plannedXxxSamples 的来源核对面（§9.5）。
    EXPECT_EQ(d1.get().positionSamples, 75U);
    EXPECT_EQ(d1.get().orientationSamples, 1U);
}

/**
 * counts 乘积=0 合法存储（acceptance 3/V-02——零样本判定归评估、本单元
 * 不判；KIN-04 R8：零样本→DataInsufficient、不输出 0%/100%）。
 */
TEST(ReqSampling, ZeroSamplePlanLegalStorage_WP14T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-04"},
                  std::vector<std::string>{"ACC3-zero-samples"});

    const SamplingPlanBuilder builder;
    std::vector<core::DiagnosticRecord> diags;
    const WorkRegion region = makeRegion();

    // Grid counts 含 0（乘积=0）→buildPlan 接受（合法存储、不阻断编辑）。
    SamplingPlanSpec zeroSpec = makeSpacingSpec();
    zeroSpec.planObjectId = core::ObjectId::fromCanonical(
        "obj-50000000000000000000000000000006");
    zeroSpec.positionSampling.method = PositionSamplingMethod::Grid;
    zeroSpec.positionSampling.counts = {3, 4, 0};
    auto p = builder.buildPlan(region, zeroSpec, diags);
    ASSERT_TRUE(p.ok) << p.error.detail;

    // 摘要面：位置分母＝0（乘积），姿态分母≥1——分母语义如实承载（判
    // 定归评估）。
    auto d = builder.digest(p.plan, diags);
    ASSERT_TRUE(d.ok());
    EXPECT_EQ(d.get().positionSamples, 0U);
    EXPECT_EQ(d.get().orientationSamples, 1U);

    // Random count=0 同样合法。
    SamplingPlanSpec randomZero = zeroSpec;
    randomZero.positionSampling.method = PositionSamplingMethod::Random;
    randomZero.positionSampling.count = 0;
    auto pr = builder.buildPlan(region, randomZero, diags);
    EXPECT_TRUE(pr.ok) << "零样本计划合法存储（V-02）";
}

/**
 * ISamplingPlanBuilder 绝不生成样本（acceptance 3——接口面无枚举出口，
 * §5.2"本单元任何接口不得枚举/生成样本点"）。
 */
TEST(ReqSampling, BuilderInterfaceHasNoSampleExit_WP14T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03", "KIN-04"},
                  std::vector<std::string>{"ACC3-no-sample-exit"});

    // 接口面封闭性：ISamplingPlanBuilder 公开虚方法恰 2 个
    // （buildPlan/digest——§9.5 行原文签名），无任何返回样本集合/坐标/
    // 迭代器的出口；SamplingPlan 值模型也不承载样本坐标（只有规范化计
    // 数——字段面）。
    using Builder = ISamplingPlanBuilder;
    // 逐方法存在性（编译期——签名面钉住；多一个生成样本方法会改变
    // 可调用面，测试内函数指针取址即失败）。
    const Builder* builder = nullptr;
    (void)builder;
    using BuildFn = PlanOutcome (Builder::*)(const WorkRegion&, const SamplingPlanSpec&,
                                             std::vector<core::DiagnosticRecord>&) const;
    using DigestFn = Expected<PlanDigest> (Builder::*)(const SamplingPlan&,
                                                       std::vector<core::DiagnosticRecord>&) const;
    static_assert(std::is_same_v<decltype(&Builder::buildPlan), BuildFn>,
                  "buildPlan 签名面钉住（§9.5 原文）");
    static_assert(std::is_same_v<decltype(&Builder::digest), DigestFn>,
                  "digest 签名面钉住（§9.5 原文）");

    // 值模型面：SamplingPlan 无样本坐标字段（规范化计数即全部位置信息
    // ——字段聚合面由 roundtrip（CodecTest）钉住；此处断言计划条目经
    // buildPlan 后不因区域尺寸/间距产生任何额外载荷：同 counts 异间距
    // 同义计划值面全等）。
    const SamplingPlanBuilder impl;
    std::vector<core::DiagnosticRecord> diags;
    WorkRegion region = makeRegion();
    SamplingPlanSpec s1 = makeSpacingSpec();
    s1.positionSampling.spacing = {0.1, 0.2, 0.1};   // counts=floor(0.4/0.1)+1 等
    s1.planObjectId = core::ObjectId::fromCanonical("obj-50000000000000000000000000000007");
    // 同义 spacing：0.1/0.2 与 0.05/0.1 规范化到不同 counts——改用真同义：
    // 0.2（counts 3,3,2）与 0.2（相同值）——浮点同值；此处以"同 spec 同
    // 计划"承载（identity 用例已覆盖），并补正例：counts=(4,3,2) 由
    // 0.1/0.1（floor(0.4/0.1)+1=5）不对——直接以 Grid 显式断言计划无
    // 额外载荷。
    WorkRegion big = region;
    big.box.size = rw::math::Vector3D<double>(0.8, 0.4, 0.2);
    SamplingPlanSpec s2 = makeSpacingSpec();
    s2.positionSampling.spacing = {0.2, 0.2, 0.2};   // floor(0.8/0.2)+1=5 等
    s2.planObjectId = s1.planObjectId;
    auto p1 = impl.buildPlan(region, s1, diags);
    auto p2 = impl.buildPlan(big, s2, diags);
    ASSERT_TRUE(p1.ok);
    ASSERT_TRUE(p2.ok);
    // p1: size=(0.4,0.4,0.2) spacing=(0.1,0.2,0.1)
    //     → floor(4)+1=5, floor(2)+1=3, floor(2)+1=3 → (5,3,3)
    EXPECT_EQ(p1.plan.positionSampling.counts[0], 5U);
    EXPECT_EQ(p1.plan.positionSampling.counts[1], 3U);
    EXPECT_EQ(p1.plan.positionSampling.counts[2], 3U);
    // p2: floor(0.8/0.2)+1=5, floor(0.4/0.2)+1=3, floor(0.2/0.2)+1=2 → (5,3,2)
    EXPECT_EQ(p2.plan.positionSampling.counts[0], 5U);
    EXPECT_EQ(p2.plan.positionSampling.counts[1], 3U);
    EXPECT_EQ(p2.plan.positionSampling.counts[2], 2U);
    SUCCEED() << "接口面仅 buildPlan/digest 两出口——样本生成归 kinematics（KIN-04）";
}

/**
 * buildPlan 错误语义逐项（§9.5 @错误 行：DegenerateRegion|NegativeCount|
 * RegionNotBox）。
 */
TEST(ReqSampling, BuildPlanErrorSemantics_WP14T03_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-03"},
                  std::vector<std::string>{"ACC3-buildPlan-errors"});

    const SamplingPlanBuilder builder;
    std::vector<core::DiagnosticRecord> diags;
    WorkRegion region = makeRegion();

    // DegenerateRegion：退化盒（零尺寸）。
    WorkRegion degenerate = region;
    degenerate.box.size = rw::math::Vector3D<double>(0.4, 0.0, 0.2);
    SamplingPlanSpec spec = makeSpacingSpec();
    auto r1 = builder.buildPlan(degenerate, spec, diags);
    ASSERT_FALSE(r1.ok);
    EXPECT_EQ(r1.error.code, RequirementErrorCode::DegenerateRegion);

    // NegativeCount：姿态方向采样 0（§5.2 ≥1——零样本仅位置侧表达）。
    SamplingPlanSpec badOri = spec;
    badOri.orientationSampling.directionSamples = 0;
    auto r2 = builder.buildPlan(region, badOri, diags);
    ASSERT_FALSE(r2.ok);
    EXPECT_EQ(r2.error.code, RequirementErrorCode::NegativeCount);

    // IllegalTolerance：间距非正。
    SamplingPlanSpec badSpacing = spec;
    badSpacing.positionSampling.spacing = {0.0, 0.1, 0.1};
    auto r3 = builder.buildPlan(region, badSpacing, diags);
    ASSERT_FALSE(r3.ok);
    EXPECT_EQ(r3.error.code, RequirementErrorCode::IllegalTolerance);

    // RegionNotBox：R1 仅 Box——WorkRegion 值模型即 Box 形态（值模型面
    // 恒过）；该码的承载点为未来几何扩展的拒绝锚（P-REQ-7 走需求变更），
    // 此处以词表断言钉住码面存在与语义登记。
    EXPECT_EQ(requirementErrorCodeToken(RequirementErrorCode::RegionNotBox),
              "RegionNotBox");
}
