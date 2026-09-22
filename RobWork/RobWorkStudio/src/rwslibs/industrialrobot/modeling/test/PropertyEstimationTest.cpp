/**
 * @file   PropertyEstimationTest.cpp
 * @brief  物性估算唯一公式表用例组（MdlPropertyEstimation/MdlDrivetrainSchema）
 *         ——契约 tasks/foundation/WP-13-T04.json acceptance 1~5 逐条具名自证：
 *           ACC1 三段元闭式公式手算对照＋来源标记（公式表行逐项双精度比对）
 *           ACC2 多段平行轴合成（M-2 基准）＋输出自检反例拒收
 *           ACC3 质心修改确认流三分支（V-17——迁移/覆盖/拒绝）
 *           ACC4 材料密度默认表查询＋物性缺失 NotProvided 不断言（DYN-06/M-8
 *                建模侧闭环——V-15 第④例建模面）
 *           ACC5 DrivetrainDesign schema 字段约束与来源标记（MDL-16）
 *
 * 设计依据：units/modeling.md §5.3（唯一公式表）、§9.4.6（IPropertyEstimator）、
 * §4.7（DrivetrainDesign）；需求 MDL-05/MDL-16/DYN-06。
 *
 * 比对口径：期望值全部按卡 §5.3 公式表**独立抄写闭式**计算（实现与测试
 * 各写一份算式——同一张表的两份转写，互为核对）；双精度逐项比对，容差
 * 1×10⁻¹²（绝对量级 ≥1 时相对）——这是比对容差而非舍入声明（实现不
 * 舍入，卡 §5.3"全 SI、双精度、不舍入"）。
 */

#include <sdurws/ird/modeling/Parts.hpp>              // DrivetrainDesign 族（ACC5 schema 面）
#include <sdurws/ird/modeling/PropertyEstimation.hpp>  // 被测主面（估算器/确认流/密度表）
#include <sdurws/ird/modeling/RobotDesign.hpp>        // BodyData/InertiaTensor/checkInvariants（NotProvided 面）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include "../src/InertiaMath.hpp"  // 单元内私有头（runtime/reporting 同款先例）——自检分量级反例与 NotProvided 断言跳过

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using sdurws::ird::core::FieldState;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProvenanceKind;
using sdurws::ird::core::SourcedValue;
using sdurws::ird::core::ValueProvenance;
namespace rwmath = rw::math;
using sdurws::ird::modeling::BodyData;
using sdurws::ird::modeling::BoxSpec;
using sdurws::ird::modeling::CentroidEditResolution;
using sdurws::ird::modeling::DrivetrainDesign;
using sdurws::ird::modeling::EstimateError;
using sdurws::ird::modeling::EstimateErrorCode;
using sdurws::ird::modeling::FrictionEntry;
using sdurws::ird::modeling::HollowCylinderSpec;
using sdurws::ird::modeling::InertiaTensor;
using sdurws::ird::modeling::InvariantId;
using sdurws::ird::modeling::MaterialRef;
using sdurws::ird::modeling::PropertyEstimator;
using sdurws::ird::modeling::SegmentSpec;
using sdurws::ird::modeling::SolidCylinderSpec;
using sdurws::ird::modeling::TorqueLimitEntry;
using sdurws::ird::modeling::applyCentroidEdit;
using sdurws::ird::modeling::checkInvariants;
using sdurws::ird::modeling::defaultMaterialDensity;
using sdurws::ird::modeling::estimateErrorCodeToken;
using sdurws::ird::modeling::kPropertyFormulaVersion;

namespace {

/// 比对容差（期望值量级 ≥1 时为 1×10⁻¹² 相对；小量时 1×10⁻¹² 绝对——
/// 双精度闭合式的实现/测试两份转写差异在 ~1e-15 相对，容差留足余量且
/// 远小于任何工程容差）。
double closeTolerance(double expected)
{
    return 1e-12 * std::max(1.0, std::abs(expected));
}

/// 密度已提供的材料（自定义值——覆盖默认表路径的输入形态）。
MaterialRef materialWithDensity(std::string id, double densityKgPerM3)
{
    MaterialRef m;
    m.materialId = std::move(id);
    m.density = SourcedValue<double>::provided(
        densityKgPerM3, ValueProvenance::make(ProvenanceKind::UserProvided));
    return m;
}

/// 只挂默认表键的材料（density 缺省＝NotProvided——走默认表查询路径）。
MaterialRef materialByTable(std::string id)
{
    MaterialRef m;
    m.materialId = std::move(id);
    return m;
}

/// 实心圆柱段元（位姿＝平移 p＋可选旋转——段系原点在几何中心，§5.3）。
SegmentSpec solidCylinder(double radiusM, double lengthM, MaterialRef material,
                          rwmath::Vector3D<double> translation =
                              rwmath::Vector3D<double>(0.0, 0.0, 0.0),
                          const rwmath::Rotation3D<double>& rotation =
                              rwmath::Rotation3D<double>(1.0, 0.0, 0.0,
                                                         0.0, 1.0, 0.0,
                                                         0.0, 0.0, 1.0))
{
    SegmentSpec s;
    s.primitive = SolidCylinderSpec{radiusM, lengthM};
    s.linkFromSegment = rwmath::Transform3D<double>(translation, rotation);
    s.material = std::move(material);
    return s;
}

/// 空心圆柱段元。
SegmentSpec hollowCylinder(double rOutM, double rInM, double lengthM, MaterialRef material)
{
    SegmentSpec s;
    s.primitive = HollowCylinderSpec{rOutM, rInM, lengthM};
    s.material = std::move(material);
    return s;
}

/// 长方体段元。
SegmentSpec box(double aM, double bM, double lengthM, MaterialRef material,
                rwmath::Vector3D<double> translation =
                    rwmath::Vector3D<double>(0.0, 0.0, 0.0),
                const rwmath::Rotation3D<double>& rotation =
                    rwmath::Rotation3D<double>(1.0, 0.0, 0.0,
                                               0.0, 1.0, 0.0,
                                               0.0, 0.0, 1.0))
{
    SegmentSpec s;
    s.primitive = BoxSpec{aM, bM, lengthM};
    s.linkFromSegment = rwmath::Transform3D<double>(translation, rotation);
    s.material = std::move(material);
    return s;
}

/// 跑一次单段估算并断言成功（返回成功载荷——各公式用例的公共入口）。
sdurws::ird::modeling::EstimatedLinkProperties estimateOne(const SegmentSpec& segment)
{
    const PropertyEstimator estimator;
    const std::vector<SegmentSpec> segments{segment};
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const auto outcome = estimator.estimateLink(segments, diags);
    EXPECT_TRUE(outcome.ok()) << "单段估算意外失败（阶段公因）";
    return outcome.get();
}

/// 已估算连杆的物性组（ACC3 确认流基底——V-17 前置"已估算连杆"：
/// 三字段均 Provided，来源＝几何估算，张量 SPD＋三角不等式通过）。
BodyData estimatedBody()
{
    BodyData body;
    body.mass = SourcedValue<double>::provided(
        2.0, ValueProvenance::make(ProvenanceKind::GeometricEstimate, std::nullopt,
                                   std::nullopt, std::string(kPropertyFormulaVersion)));
    body.centerOfMass = SourcedValue<rwmath::Vector3D<double>>::provided(
        rwmath::Vector3D<double>(0.1, 0.05, 0.0),  // m，连杆系
        ValueProvenance::make(ProvenanceKind::GeometricEstimate, std::nullopt,
                              std::nullopt, std::string(kPropertyFormulaVersion)));
    InertiaTensor t;  // kg·m²——近对角 SPD（对角 0.01/0.01/0.004＋小积项）
    t.ixx = 0.01;
    t.iyy = 0.01;
    t.izz = 0.004;
    t.ixy = 0.001;
    t.ixz = 0.0;
    t.iyz = 0.002;
    body.inertia = SourcedValue<InertiaTensor>::provided(
        t, ValueProvenance::make(ProvenanceKind::GeometricEstimate, std::nullopt,
                                 std::nullopt, std::string(kPropertyFormulaVersion)));
    return body;
}

/// 由 64 位序号构造确定 ObjectId（同单元内 RobotDesignTest 口径）。
ObjectId makeOid(std::uint64_t v)
{
    char text[40] = {};
    std::snprintf(text, sizeof(text), "obj-%024llx%08llx",
                  static_cast<unsigned long long>(0),
                  static_cast<unsigned long long>(v));
    return ObjectId::fromCanonical(text);
}

/// 合法关节（Revolute，axis=+Z——I-MDL-6；±π/2 有限限位——I-MDL-4）。
sdurws::ird::modeling::JointEntry makeJoint(std::uint64_t id, std::string name)
{
    sdurws::ird::modeling::JointEntry j;
    j.objectId = makeOid(id);
    j.localName = std::move(name);
    j.type = sdurws::ird::modeling::JointType::Revolute;
    j.axis = SourcedValue<rwmath::Vector3D<double>>::provided(
        rwmath::Vector3D<double>(0.0, 0.0, 1.0),
        ValueProvenance::make(ProvenanceKind::UserProvided));
    j.origin = SourcedValue<sdurws::ird::modeling::JointPose>::provided(
        sdurws::ird::modeling::JointPose(),
        ValueProvenance::make(ProvenanceKind::UserProvided));
    j.zeroOffset = 0.0;  // rad
    j.bounds = SourcedValue<sdurws::ird::modeling::JointLimits>::provided(
        sdurws::ird::modeling::JointLimits(-1.5707963267948966, 1.5707963267948966),
        ValueProvenance::make(ProvenanceKind::UserProvided));
    return j;
}

/// 物性全缺失连杆（BodyData 默认＝全 NotProvided——ACC4 的 V-15 第④例面）。
sdurws::ird::modeling::LinkEntry makeEmptyBodyLink(std::uint64_t id, std::string name)
{
    sdurws::ird::modeling::LinkEntry l;
    l.objectId = makeOid(id);
    l.localName = std::move(name);
    return l;  // body 四字段全缺省（mass/com/inertia/material＝NotProvided/空）
}

/// 断言辅助：违例清单恰含指定不变量与定位子串（RobotDesignTest 同款）。
::testing::AssertionResult hasViolation(const std::vector<sdurws::ird::modeling::InvariantViolation>& v,
                                         InvariantId id, const std::string& subjectPart)
{
    for (const auto& item : v) {
        if (item.id == id && item.subject.find(subjectPart) != std::string::npos) {
            return ::testing::AssertionSuccess();
        }
    }
    return ::testing::AssertionFailure()
           << "未找到违例 " << sdurws::ird::modeling::invariantIdToken(id)
           << "（" << subjectPart << "）";
}

}  // namespace

// =====================================================================
// acceptance 1——三段元闭式公式手算对照＋来源标记（§5.3 公式表逐行）
// =====================================================================

/// 公式表版本与来源标记（§9.4.6 formulaVersion＋§5.3 规则 1：估算结果
/// 一律 GeometricEstimate＋methodTag="mdl-property-formula/1"）。
TEST(MdlPropertyEstimation, FormulaVersionAndProvenanceMarking_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const PropertyEstimator estimator;
    // 版本单一来源：接口返回值与公共常量同串（methodTag 同一常量——无第二处字面量）。
    EXPECT_EQ(estimator.formulaVersion(), kPropertyFormulaVersion);
    EXPECT_EQ(kPropertyFormulaVersion, "mdl-property-formula/1");  // 卡面原文串

    const auto props = estimateOne(
        solidCylinder(0.05, 0.30, materialByTable("steel")));
    EXPECT_EQ(props.provenance.kind, ProvenanceKind::GeometricEstimate);
    ASSERT_TRUE(props.provenance.methodTag.has_value());
    EXPECT_EQ(*props.provenance.methodTag, "mdl-property-formula/1");
    EXPECT_FALSE(props.provenance.sourceObject.has_value());  // 来源是公式表而非存储对象

    // 错误码 token 全表（3 值——§9.4.6 @错误 行成员名原文）。
    EXPECT_EQ(estimateErrorCodeToken(EstimateErrorCode::IllegalDimension), "IllegalDimension");
    EXPECT_EQ(estimateErrorCodeToken(EstimateErrorCode::MaterialDensityMissing), "MaterialDensityMissing");
    EXPECT_EQ(estimateErrorCodeToken(EstimateErrorCode::SynthesisFailed), "SynthesisFailed");
}

/// 空段元列表拒绝（§9.4.6 @pre"segments 非空"——值面 IllegalDimension）。
TEST(MdlPropertyEstimation, EmptySegmentsRejectedIllegalDimension_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const PropertyEstimator estimator;
    const std::vector<SegmentSpec> empty;
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const auto outcome = estimator.estimateLink(empty, diags);
    ASSERT_FALSE(outcome.ok());
    EXPECT_EQ(outcome.error().code, EstimateErrorCode::IllegalDimension);
}

/// 公式表第 1 行（实心圆柱 r,L）：m=ρπr²L；Ixx=Iyy=m(3r²+L²)/12；Izz=mr²/2。
/// 单段恒等位姿下合成质心＝段平移、合成张量＝段张量（M-2 基准自检的退化面）。
TEST(MdlPropertyEstimation, SolidCylinderClosedForm_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const double r = 0.05;    // m
    const double l = 0.30;    // m
    const double rho = 7850.0;  // kg/m³（钢——默认表路径）
    const auto props = estimateOne(solidCylinder(r, l, materialByTable("steel")));

    // 手算闭式（独立抄写卡 §5.3 公式表第 1 行）。
    const double pi = 3.14159265358979323846;
    const double m = rho * pi * r * r * l;
    EXPECT_NEAR(props.massKg, m, closeTolerance(m));
    const double ixx = m * (3.0 * r * r + l * l) / 12.0;
    EXPECT_NEAR(props.inertia.ixx, ixx, closeTolerance(ixx));
    EXPECT_NEAR(props.inertia.iyy, ixx, closeTolerance(ixx));  // 轴对称 Ixx=Iyy
    const double izz = m * r * r / 2.0;
    EXPECT_NEAR(props.inertia.izz, izz, closeTolerance(izz));
    // 单段恒等位姿：积项恒零、质心＝段平移（本用例未设平移）。
    EXPECT_EQ(props.inertia.ixy, 0.0);
    EXPECT_EQ(props.inertia.ixz, 0.0);
    EXPECT_EQ(props.inertia.iyz, 0.0);
    EXPECT_NEAR(props.centerOfMass[0], 0.0, closeTolerance(0.0));
    EXPECT_NEAR(props.centerOfMass[1], 0.0, closeTolerance(0.0));
    EXPECT_NEAR(props.centerOfMass[2], 0.0, closeTolerance(0.0));
}

/// 公式表第 2 行（空心圆柱 rOut,rIn,L）：m=ρπ(rOut²−rIn²)L；
/// Ixx=Iyy=m(3(rOut²+rIn²)+L²)/12；Izz=m(rOut²+rIn²)/2。
TEST(MdlPropertyEstimation, HollowCylinderClosedForm_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const double rOut = 0.06;   // m
    const double rIn = 0.045;   // m
    const double l = 0.25;      // m
    const double rho = 4430.0;  // kg/m³（钛合金——默认表路径）
    const auto props = estimateOne(hollowCylinder(rOut, rIn, l, materialByTable("titanium-alloy")));

    const double pi = 3.14159265358979323846;
    const double m = rho * pi * (rOut * rOut - rIn * rIn) * l;
    EXPECT_NEAR(props.massKg, m, closeTolerance(m));
    const double ixx = m * (3.0 * (rOut * rOut + rIn * rIn) + l * l) / 12.0;
    EXPECT_NEAR(props.inertia.ixx, ixx, closeTolerance(ixx));
    EXPECT_NEAR(props.inertia.iyy, ixx, closeTolerance(ixx));
    const double izz = m * (rOut * rOut + rIn * rIn) / 2.0;
    EXPECT_NEAR(props.inertia.izz, izz, closeTolerance(izz));
}

/// 公式表第 3 行（长方体 a,b,L；z 沿 L）：m=ρabL；Ixx=m(b²+L²)/12；
/// Izz=m(a²+b²)/12；Iyy=m(a²+L²)/12（列头"Ixx=Iyy"为轴对称两行的合并列——
/// 长方体 a≠b 时 Ixx≠Iyy 是解析事实；Iyy 落位澄清随单元卡 §15 增量登记）。
TEST(MdlPropertyEstimation, BoxClosedForm_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const double a = 0.10;      // m（x 向）
    const double b = 0.08;      // m（y 向）
    const double l = 0.40;      // m（z 向）
    const double rho = 7200.0;  // kg/m³（铸铁——默认表路径）
    const auto props = estimateOne(box(a, b, l, materialByTable("cast-iron")));

    const double m = rho * a * b * l;  // 手算闭式 ρabL
    EXPECT_NEAR(props.massKg, m, closeTolerance(m));
    const double ixx = m * (b * b + l * l) / 12.0;   // 卡面 Ixx 列值
    EXPECT_NEAR(props.inertia.ixx, ixx, closeTolerance(ixx));
    const double iyy = m * (a * a + l * l) / 12.0;   // 同式换轴（澄清落位面）
    EXPECT_NEAR(props.inertia.iyy, iyy, closeTolerance(iyy));
    EXPECT_GT(std::abs(props.inertia.ixx - props.inertia.iyy), 1e-6)  // a≠b ⇒ Ixx≠Iyy
        << "长方体非轴对称轴惯量不应相等（列头合并列的澄清验证面）";
    const double izz = m * (a * a + b * b) / 12.0;   // 卡面 Izz 列值
    EXPECT_NEAR(props.inertia.izz, izz, closeTolerance(izz));
}

/// 密度解析序（§9.4.6 @pre）：MaterialRef.density 已提供值优先于默认表——
/// 显式提供的权威值不被表值静默覆盖（NFR-COR-03；表只服务"未提供"侧）。
TEST(MdlPropertyEstimation, DensityResolutionProvidedOverridesTable_WP13T04_ACC1)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const double r = 0.05;  // m
    const double l = 0.30;  // m
    const double pi = 3.14159265358979323846;
    // 已提供 999 kg/m³：即便键命中默认表（steel=7850）也用已提供值。
    const auto withProvided = estimateOne(
        solidCylinder(r, l, materialWithDensity("steel", 999.0)));
    const double mProvided = 999.0 * pi * r * r * l;
    EXPECT_NEAR(withProvided.massKg, mProvided, closeTolerance(mProvided));
    // 键不在词表但密度已提供：估算不依赖默认表命中（提供即足）。
    const auto unknownKey = estimateOne(
        solidCylinder(r, l, materialWithDensity("lunar-regolith", 1500.0)));
    const double mUnknown = 1500.0 * pi * r * r * l;
    EXPECT_NEAR(unknownKey.massKg, mUnknown, closeTolerance(mUnknown));
}

// =====================================================================
// acceptance 2——多段合成（平行轴定理；M-2 基准）＋输出自检反例拒收
// =====================================================================

/// 两段合成：I_C=Σ[Rᵢ·Iᵢ·Rᵢᵀ+mᵢ((dᵢ·dᵢ)E−dᵢdᵢᵀ)]，C=Σmᵢcᵢ/m_link。
/// 期望张量按卡面公式在测试侧独立装配（含第二段的 Rz(90°) 旋转）。
TEST(MdlPropertyEstimation, TwoSegmentParallelAxisSynthesis_WP13T04_ACC2)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    // 段1：实心圆柱（钢），恒等位姿、平移 (0,0,0.1)。
    // 段2：长方体（铝），平移 (0.3,0,0.2)、绕 z 转 90°（局部 x→连杆 +y、
    // 局部 y→连杆 −x——对角项 x/y 互换，积项恒零）。
    const double pi = 3.14159265358979323846;
    const double r1 = 0.05, l1 = 0.2;      // m
    const double m1 = 7850.0 * pi * r1 * r1 * l1;          // kg
    const double ixx1 = m1 * (3.0 * r1 * r1 + l1 * l1) / 12.0;
    const double izz1 = m1 * r1 * r1 / 2.0;
    const double a2 = 0.1, b2 = 0.08, l2 = 0.15;           // m
    const double m2 = 2700.0 * a2 * b2 * l2;               // kg
    const double ixA2 = m2 * (b2 * b2 + l2 * l2) / 12.0;   // 绕局部 x
    const double iyA2 = m2 * (a2 * a2 + l2 * l2) / 12.0;   // 绕局部 y
    const double izA2 = m2 * (a2 * a2 + b2 * b2) / 12.0;   // 绕局部 z
    // Rz(90°) 显式 9 元（行主序——rw Rotation3D 构造序）：局部对角
    // (ixA2,iyA2,izA2) 经 R·D·Rᵀ 映为连杆系对角 (iyA2,ixA2,izA2)。
    const rwmath::Rotation3D<double> rz90(0.0, -1.0, 0.0,
                                          1.0, 0.0, 0.0,
                                          0.0, 0.0, 1.0);
    const PropertyEstimator estimator;
    std::vector<SegmentSpec> segments;
    segments.push_back(solidCylinder(r1, l1, materialByTable("steel"),
                                     rwmath::Vector3D<double>(0.0, 0.0, 0.1)));
    segments.push_back(box(a2, b2, l2, materialByTable("aluminum"),
                           rwmath::Vector3D<double>(0.3, 0.0, 0.2), rz90));
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const auto outcome = estimator.estimateLink(segments, diags);
    ASSERT_TRUE(outcome.ok());
    const auto& props = outcome.get();

    // 合成质心 C=Σmᵢcᵢ/m_link（手算闭式）。
    const double mLink = m1 + m2;
    EXPECT_NEAR(props.massKg, mLink, closeTolerance(mLink));
    const double cx = (m1 * 0.0 + m2 * 0.3) / mLink;   // m
    const double cy = 0.0;                              // m（两段 y 平移均为 0）
    const double cz = (m1 * 0.1 + m2 * 0.2) / mLink;    // m
    EXPECT_NEAR(props.centerOfMass[0], cx, closeTolerance(cx));
    EXPECT_NEAR(props.centerOfMass[1], cy, closeTolerance(cy));
    EXPECT_NEAR(props.centerOfMass[2], cz, closeTolerance(cz));

    // dᵢ＝cᵢ−C（m）——测试侧独立装配 I_C 的六分量（含平行轴项展开）。
    const double d1[3] = {0.0 - cx, 0.0 - cy, 0.1 - cz};
    const double d2[3] = {0.3 - cx, 0.0 - cy, 0.2 - cz};
    const double dd1 = d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2];
    const double dd2 = d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2];
    // 段1 旋转后对角 (ixx1,ixx1,izz1)；段2 旋转后对角 (iyA2,ixA2,izA2)；积项全零。
    const double diag1[3] = {ixx1, ixx1, izz1};
    const double diag2[3] = {iyA2, ixA2, izA2};
    for (int row = 0; row < 3; ++row) {
        for (int col = row; col < 3; ++col) {  // 对称——只比对上三角＋对角
            const double e = diag1[row] * (row == col ? 1.0 : 0.0)
                           + m1 * ((row == col ? dd1 : 0.0) - d1[row] * d1[col])
                           + diag2[row] * (row == col ? 1.0 : 0.0)
                           + m2 * ((row == col ? dd2 : 0.0) - d2[row] * d2[col]);
            const double actual =
                row == 0 ? (col == 0 ? props.inertia.ixx
                          : col == 1 ? props.inertia.ixy
                                     : props.inertia.ixz)
                : row == 1 ? (col == 1 ? props.inertia.iyy : props.inertia.iyz)
                           : props.inertia.izz;
            EXPECT_NEAR(actual, e, closeTolerance(e))
                << "I_C 分量 (" << row << "," << col << ") 与手算闭式不符";
        }
    }

    // 确定性（NFR-COR-02，卡 §5.3 规则 4"估算算法确定性"）：同输入重复
    // 调用逐位相等（EXPECT_EQ 为精确比较——不经容差）。
    std::vector<sdurws::ird::core::DiagnosticRecord> diagsAgain;
    const auto again = estimator.estimateLink(segments, diagsAgain);
    ASSERT_TRUE(again.ok());
    EXPECT_EQ(again.get().massKg, props.massKg);
    EXPECT_TRUE(again.get().centerOfMass == props.centerOfMass);
    EXPECT_TRUE(again.get().inertia == props.inertia);
    EXPECT_TRUE(again.get().provenance == props.provenance);
}

/// 参考姿态＝连杆坐标系（M-2）的单段钉子面：绕 x 转 90° 的实心圆柱
/// （局部 z 轴→连杆 −y）：连杆系下 Iyy 与 Izz 互换（Ixx 不变）——
/// 张量随段姿态旋转，而非按段局部数值原样抄写。
TEST(MdlPropertyEstimation, SingleSegmentRotatedTensorInLinkFrame_WP13T04_ACC2)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    const double r = 0.06;   // m
    const double l = 0.24;   // m
    const double pi = 3.14159265358979323846;
    const double m = 7850.0 * pi * r * r * l;               // kg
    const double ixxLocal = m * (3.0 * r * r + l * l) / 12.0;  // 绕局部 x（含轴向）
    const double izzLocal = m * r * r / 2.0;                   // 绕局部 z（截面）
    // Rx(90°)：局部 z→连杆 −y、局部 y→连杆 +z。
    const rwmath::Rotation3D<double> rx90(1.0, 0.0, 0.0,
                                          0.0, 0.0, -1.0,
                                          0.0, 1.0, 0.0);
    const auto props = estimateOne(solidCylinder(r, l, materialByTable("steel"),
                                                 rwmath::Vector3D<double>(0.02, 0.0, 0.0),
                                                 rx90));
    // 连杆系期望：Ixx=ixxLocal（绕连杆 x＝绕局部 x）；Iyy=izzLocal；Izz=ixxLocal。
    EXPECT_NEAR(props.inertia.ixx, ixxLocal, closeTolerance(ixxLocal));
    EXPECT_NEAR(props.inertia.iyy, izzLocal, closeTolerance(izzLocal));
    EXPECT_NEAR(props.inertia.izz, ixxLocal, closeTolerance(ixxLocal));
    // 单段：合成质心＝段平移（C 即段质心——d=0，平行轴项恒零）。
    EXPECT_NEAR(props.centerOfMass[0], 0.02, closeTolerance(0.02));
    EXPECT_NEAR(props.centerOfMass[1], 0.0, closeTolerance(0.0));
    EXPECT_NEAR(props.centerOfMass[2], 0.0, closeTolerance(0.0));
}

/// 输出自检反例拒收（§9.4.6 @post"自检失败=内部错误码，不输出非法张量"）：
///   分量级反例（私有头直检——对称超差/非正定/三角不等式逐条触发，
///   容差边界恰可过）；公共面反例（尺寸合法但算术溢出→SynthesisFailed，
///   成功载荷不可观测——get() 抛契约违约）。
TEST(MdlPropertyEstimation, SynthesisSelfCheckRejectsCounterexamples_WP13T04_ACC2)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    using sdurws::ird::modeling::inertiamath::synthesisSelfCheck;
    // 基线：单位阵（SPD、对称、三角不等式成立）→通过。
    const std::array<std::array<double, 3>, 3> zero = [] {
        std::array<std::array<double, 3>, 3> m{};
        for (int i = 0; i < 3; ++i) { m[i][i] = 1.0; }
        return m;
    }();
    EXPECT_EQ(synthesisSelfCheck(zero), "");

    // 反例①对称超差：非对角两三角差 1×10⁻⁶ ≫ 1×10⁻¹² →拒收。
    auto asymmetric = zero;
    asymmetric[0][1] = 1e-6;
    asymmetric[1][0] = 0.0;
    EXPECT_EQ(synthesisSelfCheck(asymmetric), "asymmetric-beyond-tolerance");

    // 容差边界：两三角差恰为 1×10⁻¹²（≤ 容差）→通过（闭边界钉子）。
    auto atTolerance = zero;
    atTolerance[0][1] = 1e-12;
    atTolerance[1][0] = 0.0;
    EXPECT_EQ(synthesisSelfCheck(atTolerance), "");

    // 反例②非正定：对角含 0（最小特征值不严格>0）→拒收。
    auto notSpd = zero;
    notSpd[2][2] = 0.0;
    EXPECT_EQ(synthesisSelfCheck(notSpd), "not-positive-definite");

    // 反例③三角不等式：diag(1,1,10)（λmax=10>1+1——真实刚体不可能）→拒收。
    auto triangle = zero;
    triangle[2][2] = 10.0;
    EXPECT_EQ(synthesisSelfCheck(triangle), "triangle-inequality-violated");

    // 公共面反例：尺寸合法（1e200 m 有限且>0）但 r² 溢出到 Inf——合成
    // 结果非有限，自检必须以内部错误码拒收，且不交付任何张量。
    const PropertyEstimator estimator;
    std::vector<SegmentSpec> segments;
    segments.push_back(solidCylinder(1e200, 1e200, materialByTable("steel")));
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const auto outcome = estimator.estimateLink(segments, diags);
    ASSERT_FALSE(outcome.ok());
    EXPECT_EQ(outcome.error().code, EstimateErrorCode::SynthesisFailed);
    EXPECT_FALSE(outcome.error().detail.empty());
    EXPECT_THROW((void)outcome.get(), std::logic_error);  // 非法张量不可观测
}

// =====================================================================
// acceptance 3——质心修改确认流三分支（V-17；MDL-05/M-2）
// =====================================================================

/// 分支 (a) 平行轴迁移：接受；com→UserProvided；惯量按 I+m((d·d)E−ddᵀ)
/// 迁移且保留原 provenance（迁移不改变来源事实——头注契约）。
/// 手算（m=2 kg，d=(0.2,0.1,0.1) m）：
///   Ixx'=0.01+2(0.1²+0.1²)=0.05；Iyy'=0.01+2(0.2²+0.1²)=0.11；
///   Izz'=0.004+2(0.2²+0.1²)=0.104；Ixy'=0.001−2·0.2·0.1=−0.039；
///   Ixz'=0−2·0.2·0.1=−0.04；Iyz'=0.002−2·0.1·0.1=−0.018。
TEST(MdlPropertyEstimation, CentroidEditMigrateBranch_WP13T04_ACC3)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    BodyData body = estimatedBody();
    const BodyData before = body;
    const rwmath::Vector3D<double> newCom(0.3, 0.15, 0.1);  // m（d=(0.2,0.1,0.1)）
    const auto error = applyCentroidEdit(body, newCom, CentroidEditResolution::MigrateInertia, {});
    EXPECT_FALSE(error.has_value()) << "迁移分支应接受（V-17 分支 a）";

    // 新 com＝用户值，来源 UserProvided（§5.3 规则 1——用户修改覆盖估算标记）。
    ASSERT_TRUE(body.centerOfMass.state() == FieldState::Provided);
    EXPECT_NEAR(body.centerOfMass.value()[0], 0.3, closeTolerance(0.3));
    EXPECT_NEAR(body.centerOfMass.value()[1], 0.15, closeTolerance(0.15));
    EXPECT_NEAR(body.centerOfMass.value()[2], 0.1, closeTolerance(0.1));
    EXPECT_EQ(body.centerOfMass.provenance().kind, ProvenanceKind::UserProvided);

    // 惯量六分量逐项手算对照（上式）＋来源保留（GeometricEstimate）。
    ASSERT_TRUE(body.inertia.state() == FieldState::Provided);
    const InertiaTensor& migrated = body.inertia.value();
    EXPECT_NEAR(migrated.ixx, 0.05, closeTolerance(0.05));
    EXPECT_NEAR(migrated.iyy, 0.11, closeTolerance(0.11));
    EXPECT_NEAR(migrated.izz, 0.104, closeTolerance(0.104));
    EXPECT_NEAR(migrated.ixy, -0.039, closeTolerance(0.039));
    EXPECT_NEAR(migrated.ixz, -0.04, closeTolerance(0.04));
    EXPECT_NEAR(migrated.iyz, -0.018, closeTolerance(0.018));
    EXPECT_EQ(body.inertia.provenance().kind, ProvenanceKind::GeometricEstimate);
    // 迁移公式与估算器平行轴实现同源（单一实现点——数值应与直接调用一致）。
    const PropertyEstimator estimator;
    const rwmath::Vector3D<double> delta = newCom - before.centerOfMass.value();
    const InertiaTensor direct =
        estimator.migrateByParallelAxis(before.inertia.value(), before.mass.value(), delta);
    EXPECT_TRUE(migrated == direct);
    // 质量与材料不被确认流触碰。
    EXPECT_TRUE(body.mass == before.mass);
    EXPECT_TRUE(body.material == before.material);
}

/// 分支 (b) 强制覆盖完整张量：接受；com/inertia 均 UserProvided；
/// 新张量逐分量精确落位（六分量表示即"完整张量"——无半覆盖通道）。
TEST(MdlPropertyEstimation, CentroidEditOverwriteBranch_WP13T04_ACC3)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    BodyData body = estimatedBody();
    InertiaTensor replacement;  // kg·m²（用户输入全六分量——SPD）
    replacement.ixx = 0.02;
    replacement.iyy = 0.03;
    replacement.izz = 0.04;
    replacement.ixy = -0.001;
    replacement.ixz = 0.002;
    replacement.iyz = -0.003;
    const rwmath::Vector3D<double> newCom(0.0, 0.0, 0.05);  // m
    const auto error = applyCentroidEdit(body, newCom, CentroidEditResolution::OverwriteInertia,
                                         replacement);
    EXPECT_FALSE(error.has_value()) << "覆盖分支应接受（V-17 分支 b）";
    ASSERT_TRUE(body.centerOfMass.state() == FieldState::Provided);
    EXPECT_NEAR(body.centerOfMass.value()[0], 0.0, closeTolerance(0.0));
    EXPECT_NEAR(body.centerOfMass.value()[2], 0.05, closeTolerance(0.05));
    EXPECT_EQ(body.centerOfMass.provenance().kind, ProvenanceKind::UserProvided);
    ASSERT_TRUE(body.inertia.state() == FieldState::Provided);
    EXPECT_TRUE(body.inertia.value() == replacement)  // 逐分量精确（拷贝语义）
        << "覆盖分支必须整体替换张量（不允许部分分量保留旧值）";
    EXPECT_EQ(body.inertia.provenance().kind, ProvenanceKind::UserProvided);
}

/// 分支 (c) 既不迁移也不覆盖→CentroidEditUnresolved 调用方错误拒绝；
/// body 逐字节保持原值——质心与惯量基准不静默脱钩（DTB 禁止项）。
TEST(MdlPropertyEstimation, CentroidEditUnresolvedRejected_WP13T04_ACC3)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    BodyData body = estimatedBody();
    const BodyData before = body;
    const auto error = applyCentroidEdit(
        body, rwmath::Vector3D<double>(0.5, 0.0, 0.0), {}, {});
    ASSERT_TRUE(error.has_value()) << "未决议的质心修改必须拒绝（V-17 分支 c）";
    EXPECT_EQ(error->code, sdurws::ird::modeling::ModelingErrorCode::CentroidEditUnresolved);
    EXPECT_EQ(sdurws::ird::modeling::modelingErrorCodeToken(error->code),
              "CentroidEditUnresolved");  // 稳定 token（卡 V-17 行原文错误名）
    EXPECT_TRUE(body == before) << "拒绝路径必须零副作用（工作集字节不变——V-14 同款纪律）";

    // 分支 token（编辑差值/命令摘要留痕的机器判别串——表值钉子）。
    EXPECT_EQ(sdurws::ird::modeling::centroidEditResolutionToken(CentroidEditResolution::MigrateInertia),
              "migrate-inertia");
    EXPECT_EQ(sdurws::ird::modeling::centroidEditResolutionToken(CentroidEditResolution::OverwriteInertia),
              "overwrite-inertia");
}

/// 前置边界（fail-fast 轨）：无物性基准确认流＝调用方契约违约；
/// 覆盖分支缺张量输入同（AGENTS 错误语义——调用方错误走异常 fail-fast）。
TEST(MdlPropertyEstimation, CentroidEditPreconditionsFailFast_WP13T04_ACC3)
{
    IRD_TEST_INFO("MDL-05", {}, std::nullopt);
    BodyData noInertia = estimatedBody();
    noInertia.inertia = SourcedValue<InertiaTensor>::notProvided();
    EXPECT_THROW((void)applyCentroidEdit(noInertia,
                                         rwmath::Vector3D<double>(0.2, 0.0, 0.0),
                                         CentroidEditResolution::MigrateInertia, {}),
                 std::invalid_argument);

    BodyData body = estimatedBody();
    EXPECT_THROW((void)applyCentroidEdit(body,
                                         rwmath::Vector3D<double>(0.2, 0.0, 0.0),
                                         CentroidEditResolution::OverwriteInertia, {}),
                 std::invalid_argument);  // 覆盖分支未给 replacementTensor
}

// =====================================================================
// acceptance 4——材料密度默认表＋物性缺失 NotProvided 不断言（DYN-06/M-8
// 建模侧闭环——V-15 第④例建模面；可信等级判定不归 modeling）
// =====================================================================

/// 密度默认表查询（§5.3 输入节设计默认值；§14.4 第 3 项登记面）：
/// 五键逐项命中精确值；键外/空串/大小写变体一律未命中（不猜测）。
TEST(MdlPropertyEstimation, MaterialDensityTableDefaults_WP13T04_ACC4)
{
    IRD_TEST_INFO("MDL-16", {}, std::nullopt);
    // 五键逐项（kg/m³——设计默认值；黄金数据集锁定随 WP-13-T16）。
    ASSERT_TRUE(defaultMaterialDensity("steel").has_value());
    EXPECT_EQ(*defaultMaterialDensity("steel"), 7850.0);
    ASSERT_TRUE(defaultMaterialDensity("aluminum").has_value());
    EXPECT_EQ(*defaultMaterialDensity("aluminum"), 2700.0);
    ASSERT_TRUE(defaultMaterialDensity("cast-iron").has_value());
    EXPECT_EQ(*defaultMaterialDensity("cast-iron"), 7200.0);
    ASSERT_TRUE(defaultMaterialDensity("titanium-alloy").has_value());
    EXPECT_EQ(*defaultMaterialDensity("titanium-alloy"), 4430.0);
    ASSERT_TRUE(defaultMaterialDensity("engineering-plastic").has_value());
    EXPECT_EQ(*defaultMaterialDensity("engineering-plastic"), 1200.0);
    // 词表外未命中（ARC-04"不猜测"——精确等值匹配）。
    EXPECT_FALSE(defaultMaterialDensity("unobtainium").has_value());
    EXPECT_FALSE(defaultMaterialDensity("").has_value());
    EXPECT_FALSE(defaultMaterialDensity("Steel").has_value());  // 大小写变体≠键
    EXPECT_FALSE(defaultMaterialDensity(" steel").has_value()); // 前导空白≠键
}

/// 物性缺失面（DYN-06/M-8 modeling 侧闭环）：
///   ①估算输入密度不可解析→值面 MaterialDensityMissing 失败（不抛异常、
///     不触发断言；诊断目录面按分批注册纪律留 T08）；
///   ②物性全缺失（NotProvided）连杆不触发 I-MDL-5 断言（全量核查通过——
///     V-15 第④例建模面"缺失不阻断"）；
///   ③可信等级判定不归 modeling（DYN-06 归 dynamics/evidence）——本单元
///     不存在任何可信等级/降级判定接口（review 面：估算器只有来源标记与
///     NotProvided 事实，无判定函数——以接口面不存在性声明，非行为断言）。
TEST(MdlPropertyEstimation, DensityMissingValueFaceAndNotProvidedNoAssertion_WP13T04_ACC4)
{
    IRD_TEST_INFO("DYN-06", {}, std::nullopt);
    // ①密度不可解析→值面失败（调用正常返回——非异常/非断言路径）。
    const PropertyEstimator estimator;
    std::vector<SegmentSpec> segments;
    segments.push_back(solidCylinder(0.05, 0.3, materialByTable("unobtainium")));
    std::vector<sdurws::ird::core::DiagnosticRecord> diags;
    const auto outcome = estimator.estimateLink(segments, diags);
    ASSERT_FALSE(outcome.ok());
    EXPECT_EQ(outcome.error().code, EstimateErrorCode::MaterialDensityMissing);
    EXPECT_NE(outcome.error().detail.find("unobtainium"), std::string::npos)
        << "失败细节应携材料键定位（内部诊断链用）";
    // 当前 §9.5 无 T04 行码（分批注册、不预建）——估算失败不入诊断目录。
    EXPECT_TRUE(diags.empty());

    // ②物性全缺失连杆：全量不变量核查通过（I-MDL-5 只作用于已提供值——
    //   断言①~③对 NotProvided 跳过；1 关节＋2 连杆最小合法链）。
    sdurws::ird::modeling::RobotDesign design;
    design.joints = {makeJoint(1, "j1")};
    design.links = {makeEmptyBodyLink(11, "base_link"), makeEmptyBodyLink(12, "l2")};
    const auto violations = checkInvariants(design);
    EXPECT_TRUE(violations.empty())
        << "物性全缺失不触发断言（V-15 第④例建模面）——实际违例数 "
        << violations.size();

    // 分量级补钉：NotProvided 惯量直接过断言②③核查（单一实现的跳过分支）。
    std::vector<sdurws::ird::modeling::InvariantViolation> out;
    sdurws::ird::modeling::inertiamath::checkInertiaAssertions(
        out, SourcedValue<InertiaTensor>::notProvided(), "links[0]");
    EXPECT_TRUE(out.empty());
}

// =====================================================================
// acceptance 5——DrivetrainDesign schema 字段约束与来源标记（§4.7，MDL-16；
// schema 于 T03 落位（Parts.hpp），本任务交付字段约束/来源标记 UT 面——
// 回填命令归属阶段 C（卡 §12.1），此处只验证 schema 承载
// =====================================================================

/// 字段约束（I-MDL-11 面）与来源标记（SourcedValue 四态＋ValueProvenance）：
///   ratio 逐项 >0 且有限（0/负/Inf 违例）；ratio/friction/torque 支持
///   全部来源类别标记（UserProvided/ImportMapped/CatalogBackfill——§5.4
///   "逐项 SourcedValue 来源标记"）；catalogBackfill 字段承载与相等语义。
TEST(MdlDrivetrainSchema, FieldConstraintsAndProvenanceMarking_WP13T04_ACC5)
{
    IRD_TEST_INFO("MDL-16", {}, std::nullopt);
    // 传动比：来源标记 CatalogBackfill（SEL-10 回填路径——§4.7 行原文）
    // 的合法值通过 I-MDL-11；约束违例（0/负/Inf）逐个暴露。
    auto sourced = [](double v, ProvenanceKind kind) {
        return SourcedValue<double>::provided(v, ValueProvenance::make(kind));
    };
    sdurws::ird::modeling::DrivetrainDesign backfilled;
    backfilled.objectId = makeOid(501);
    backfilled.ratioPerJoint = {sourced(101.0, ProvenanceKind::CatalogBackfill),   // 无量纲
                                sourced(121.0, ProvenanceKind::ImportMapped)};
    EXPECT_TRUE(checkInvariants(backfilled, sdurws::ird::modeling::CouplingStage::R1Locked).empty());

    const double badRatios[] = {0.0, -101.0, std::numeric_limits<double>::infinity()};
    for (const double bad : badRatios) {
        sdurws::ird::modeling::DrivetrainDesign badDt = backfilled;
        badDt.ratioPerJoint = {sourced(bad, ProvenanceKind::UserProvided)};
        EXPECT_TRUE(hasViolation(checkInvariants(badDt, sdurws::ird::modeling::CouplingStage::R1Locked),
                                 InvariantId::IMdl11, "ratioPerJoint"))
            << "传动比 " << bad << " 应触发 I-MDL-11（>0 且有限——§4.7 行约束）";
    }

    // 摩擦逐项 SourcedValue 来源标记（fv/fc/bias 三字段——§4.7 行"全
    // SourcedValue"）：三种来源类别各承一字段，状态与标记可查证。
    sdurws::ird::modeling::DrivetrainDesign dt = backfilled;
    FrictionEntry friction;
    friction.viscous = sourced(0.5, ProvenanceKind::UserProvided);    // fv：N·m·s/rad（转动）
    friction.coulomb = sourced(2.0, ProvenanceKind::CatalogBackfill); // fc：N·m
    friction.bias = sourced(0.1, ProvenanceKind::ImportMapped);       // 偏置：N·m
    dt.frictionPerJoint = {friction, friction};
    EXPECT_EQ(dt.frictionPerJoint[0].viscous.state(), FieldState::Provided);
    EXPECT_EQ(dt.frictionPerJoint[0].viscous.provenance().kind, ProvenanceKind::UserProvided);
    EXPECT_EQ(dt.frictionPerJoint[0].coulomb.provenance().kind, ProvenanceKind::CatalogBackfill);
    EXPECT_EQ(dt.frictionPerJoint[1].bias.provenance().kind, ProvenanceKind::ImportMapped);
    EXPECT_NEAR(dt.frictionPerJoint[0].coulomb.value(), 2.0, closeTolerance(2.0));

    // 力矩限值（rated/peak——N·m 转动/N 移动；不进 CanonicalModel——消费方
    // SEL/DYN）：SourcedValue 承载与标记查证。
    TorqueLimitEntry limits;
    limits.rated = sourced(200.0, ProvenanceKind::CatalogBackfill);  // N·m
    limits.peak = sourced(400.0, ProvenanceKind::CatalogBackfill);   // N·m
    dt.torqueLimitsPerJoint = {limits};
    EXPECT_EQ(dt.torqueLimitsPerJoint[0].rated.state(), FieldState::Provided);
    EXPECT_EQ(dt.torqueLimitsPerJoint[0].peak.provenance().kind, ProvenanceKind::CatalogBackfill);

    // catalogBackfill schema 字段（§4.7 行——阶段 C 由 selection 命令写入，
    // schema 本卡登记）：四字段承载＋相等语义。
    sdurws::ird::modeling::CatalogBackfill backfill{"catalog-v1", "motor-x", "reducer-y", "flange"};
    dt.catalogBackfill = backfill;
    EXPECT_TRUE(dt.catalogBackfill == backfill);
    sdurws::ird::modeling::DrivetrainDesign copy = dt;
    EXPECT_TRUE(copy == dt)  // 值相等含 schema 字段（编解码字段序一致性基础）
        << "传动对象值相等应覆盖全字段（含 catalogBackfill）";

    // schema 主版本单一权威（ObjectTypes.hpp 常量——禁写字面量纪律）。
    EXPECT_EQ(dt.schemaVersion, sdurws::ird::modeling::kRobotDrivetrainSchemaVersion);
}

/// 摩擦/力矩限值缺失（NotProvided）不触发传动不变量违例——DYN-06/M-8
/// 降级链的建模侧事实承载（V-15 第④例传动面；可信等级判定归下游）。
TEST(MdlDrivetrainSchema, FrictionTorqueNotProvidedAccepted_WP13T04_ACC5)
{
    IRD_TEST_INFO("DYN-06", {}, std::nullopt);
    sdurws::ird::modeling::DrivetrainDesign dt;
    dt.objectId = makeOid(502);
    dt.ratioPerJoint = {SourcedValue<double>::provided(
        100.0, ValueProvenance::make(ProvenanceKind::UserProvided))};
    // frictionPerJoint/torqueLimitsPerJoint 留空＝逐关节未填写（NotProvided
    // 语义的集合面——DYN-02 输入链的缺失标记由此触达下游）。
    EXPECT_TRUE(checkInvariants(dt, sdurws::ird::modeling::CouplingStage::R1Locked).empty())
        << "摩擦/力矩限值缺失不得触发传动不变量（缺失走 DataInsufficient 降级预告）";
    // 逐字段 NotProvided 形态同样接受（部分提供——只填 fv 一项的中间态）。
    FrictionEntry partial;
    partial.viscous = SourcedValue<double>::provided(
        0.5, ValueProvenance::make(ProvenanceKind::UserProvided));
    dt.frictionPerJoint = {partial};
    EXPECT_EQ(dt.frictionPerJoint[0].coulomb.state(), FieldState::NotProvided);
    EXPECT_EQ(dt.frictionPerJoint[0].bias.state(), FieldState::NotProvided);
    EXPECT_TRUE(checkInvariants(dt, sdurws::ird::modeling::CouplingStage::R1Locked).empty());
}
