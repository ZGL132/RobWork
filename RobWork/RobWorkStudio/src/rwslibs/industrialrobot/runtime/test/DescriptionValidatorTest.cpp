/**
 * @file   DescriptionValidatorTest.cpp
 * @brief  Description 结构与单位校验器用例组（RT-T03）——§5.2 S3 硬校验
 *         反例逐条拒绝＋耦合矩阵奇异/病态阻止＋量级抽样警告。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S3（结构与单位校验原文）、§4.2/§4.4（字段
 *     合法域与单位纪律）、§4.3.3/§4.3.4（Canonical 侧合法实例口径——
 *     Description 校验与其同源）、§6.6（正交性容差 1e-12）、§11（RT-CPL-1/
 *     RT-CPX-3/RT-BW-6 用例行——校验器部分）
 *   - 需求 MDL-21（耦合矩阵 M-6/M-12 比较型诊断）、MDL-06（断言分域）、
 *     MDL-12（关节类型口径）、NFR-COR-03（非有限拒绝）、NFR-COR-02
 *     （确定性）
 *   - 任务契约 tasks/foundation/RT-T03.json（acceptance 1～3 被测主体）
 *
 * ★ 覆盖面声明（防验收误读）：本文件只覆盖 RT-CPL-1/RT-CPX-3/RT-BW-6 的
 *   **校验器部分**（Description 侧 S3 切面）——
 *   - RT-CPL-1 的"良态 C 编译入模型"断言需 CanonicalModel/编译链（RT-T04/
 *     RT-T11/RT-T12）——此处断言到"良态 C 零问题通过校验"为止；
 *   - RT-BW-6 的"循环 Frame"切面在 WC 编译段（S6——Frame 树构建时才存在，
 *     Description 层无该概念）、"T_world_base 校验"在 §6/RT-T06——不在
 *     本文件；
 *   - RT-CPX-3 的"Failed 状态＋诊断对照 RT-CPX-2"全链断言在编译链集成
 *     （RT-T11/RT-T12）——此处断言到"InputInvalid＋定位连杆字段"为止。
 *   上述边界随测试文档留痕（RT-STUB-0 同款纪律）。
 */

#include "../src/DescriptionValidator.hpp"

#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>
#include <sdurws/ird/runtime/Resource.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
namespace core = sdurws::ird::core;

/// 双精度非有限常量（NFR-COR-03 反例注入用——NaN 与 ±Inf 两族都测）。
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

// =====================================================================
// 夹具工具。
// =====================================================================

/// 用户输入来源记录（SourcedValue Provided 态必带——core P-1 契约）。
core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
}

/// Provided 值注入。
core::SourcedValue<double> val(double v)
{
    return core::SourcedValue<double>::provided(v, userProv());
}

/// 单位旋转（逐元素构造——冒烟 header-only 约束：不用外联 identity()）。
rw::math::Rotation3D<double> identityRotation()
{
    return rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1);
}

/// 单位平移变换（旋转＝单位、平移给定——夹具里"合法位姿"的缺省形态）。
rw::math::Transform3D<double> makeTransform(double tx, double ty, double tz)
{
    return rw::math::Transform3D<double>(rw::math::Vector3D<double>(tx, ty, tz),
                                         identityRotation());
}

/// 标准合法关节（Revolute、限位 ±2.97 rad——§4.3.3 合法实例口径）。
JointDescription makeJoint(const std::string& name, double ax, double ay, double az)
{
    JointDescription j;
    j.localName = name;
    j.type = JointType::Revolute;
    j.axis = rw::math::Vector3D<double>(ax, ay, az);
    j.origin = makeTransform(0.05, 0.0, 0.3);
    j.lower = val(-2.97);
    j.upper = val(2.97);
    j.maxVelocity = val(2.5);
    j.maxAcceleration = val(15.0);
    return j;
}

/// 标准合法连杆（物性齐全——RT-CPX-3 的"非法变体"从本形态改出）。
LinkDescription makeLink(const std::string& name)
{
    LinkDescription l;
    l.localName = name;
    l.mass = val(5.0);
    l.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.05), userProv());
    l.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        rw::math::InertiaMatrix<double>(0.01, 0, 0, 0, 0.02, 0, 0, 0, 0.03),
        userProv());
    return l;
}

/// 2×2 耦合矩阵（行主序数据＋适用范围注入）。
CouplingMatrix makeCoupling2x2(std::uint32_t firstIndex, double c00, double c01,
                               double c10, double c11)
{
    CouplingMatrix cm;
    cm.rows = 2;
    cm.cols = 2;
    cm.c = {c00, c01, c10, c11};
    cm.jointRange.firstIndex = firstIndex;
    cm.jointRange.count = 2;
    return cm;
}

/// 标准合法 Description：3 旋转关节＋4 连杆＋1 工具＋1 场景＋地面基座＋
/// 传动/摩擦齐全——全部硬反例从本形态"单点改坏"（保证反例只暴露目标
/// 字段的问题，定位断言无歧义）。
RobotDesignDescription makeValidDescription()
{
    RobotDesignDescription d;
    d.descriptionContractVersion = 1;
    d.robotLocalName = "IRB_Test";
    d.joints = {makeJoint("joint_1", 0, 0, 1), makeJoint("joint_2", 0, 1, 0),
                makeJoint("joint_3", 1, 0, 0)};
    d.links = {makeLink("base_link"), makeLink("link_1"), makeLink("link_2"),
               makeLink("link_3")};

    ToolDescription tool;
    tool.localName = "gripper";
    tool.mass = val(1.2);
    tool.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(0.0, 0.0, 0.02), userProv());
    tool.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        rw::math::InertiaMatrix<double>(0.001, 0, 0, 0, 0.001, 0, 0, 0, 0.002),
        userProv());
    tool.tcpOffset = makeTransform(0.0, 0.0, 0.1);
    d.tools.push_back(tool);

    SceneObjectDescription scene;
    scene.localName = "table";
    scene.worldPose = makeTransform(1.0, 0.5, 0.0);
    ResourceRef geo;
    geo.contentDigest = core::Digest256{1u, 2u, 3u}; // 非零摘要（形态合法即可）
    geo.state = ResourceState::Solidified;
    geo.accessVersion = 1;
    scene.geometry.resource = geo;
    d.scene.push_back(scene);

    d.base.preset = InstallationPresetToken::Ground;
    d.base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 0.5);

    d.drivetrain.ratioPerJoint = {val(100.0), val(100.0), val(100.0)};

    for (int i = 0; i < 3; ++i) {
        JointFrictionDescription f;
        f.viscous = val(0.5);
        f.coulomb = val(0.2);
        f.bias = val(0.05);
        d.friction.push_back(f);
    }
    return d;
}

// =====================================================================
// 断言辅助。
// =====================================================================

/// 取指定字段路径的首个问题；不存在返回 nullptr（调用方断言非空）。
const ValidationIssue* findIssue(const ValidationReport& report, const std::string& path)
{
    for (const ValidationIssue& issue : report.issues) {
        if (issue.fieldPath == path) { return &issue; }
    }
    return nullptr;
}

/// 断言"该字段被阻断级拒绝＋消息含字段路径＋错误码正确"——acceptance 2
/// "逐条拒绝且消息含字段路径"的统一承载。
void expectBlocked(const ValidationReport& report, const std::string& path,
                   RuntimeErrorCode code)
{
    EXPECT_FALSE(report.ok()) << "期望被拒绝（含 " << path << " 的阻断问题）";
    const ValidationIssue* issue = findIssue(report, path);
    ASSERT_NE(issue, nullptr) << "缺少字段路径的问题: " << path
                              << "；实际 issues 数=" << report.issues.size();
    EXPECT_EQ(issue->severity, ValidationSeverity::Error) << path;
    EXPECT_EQ(issue->code, code) << path;
    EXPECT_NE(issue->detail.find(path), std::string::npos)
        << "消息必须含字段路径（acceptance 2）: " << issue->detail;
}

/// 断言校验通过且无任何以 prefix 开头的字段路径问题（良态分支对照面）。
void expectNoIssueUnderPath(const ValidationReport& report, const std::string& prefix)
{
    for (const ValidationIssue& issue : report.issues) {
        if (issue.fieldPath.rfind(prefix, 0) == 0) {
            ADD_FAILURE() << "不应有 " << prefix << " 的问题: " << issue.fieldPath
                          << " — " << issue.detail;
        }
    }
}

// =====================================================================
// A. 合法基线与语义（§5.2 S3 的通过面＋报告语义）。
// =====================================================================

/** 合法夹具零问题通过（含零警告——量级抽样的阴性对照）。 */
TEST(DescriptionValidator, CleanFixturePassesWithNoIssues)
{
    const ValidationReport report = validateRobotDesignDescription(makeValidDescription());
    EXPECT_TRUE(report.ok());
    EXPECT_TRUE(report.issues.empty()) << "合法夹具应无任何问题（含警告）";
}

/** 能力缺失≠非法（§5.6 正交关系——RT-CPX-2 与 RT-CPX-3 分域的阴性面）：
 *  物性/限速/摩擦/传动全 NotProvided／空清单→通过（降级归 capability，
 *  S3 不报错）。 */
TEST(DescriptionValidator, MissingCapabilitiesDoNotBlock)
{
    RobotDesignDescription d = makeValidDescription();
    for (LinkDescription& link : d.links) {
        link.mass = core::SourcedValue<double>::notProvided();
        link.centerOfMass = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
        link.inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::notProvided();
    }
    for (JointDescription& joint : d.joints) {
        joint.maxVelocity = core::SourcedValue<double>::notProvided();
        joint.maxAcceleration = core::SourcedValue<double>::notProvided();
    }
    d.drivetrain.ratioPerJoint.clear();
    d.friction.clear();
    const ValidationReport report = validateRobotDesignDescription(d);
    EXPECT_TRUE(report.ok()) << "能力缺失走 capability 降级（§5.6），不得报 InputInvalid";
}

/** 确定性（NFR-COR-02）：同一输入两次校验问题清单逐项一致。 */
TEST(DescriptionValidator, DeterministicReport)
{
    RobotDesignDescription d = makeValidDescription();
    d.links[1].mass = val(-1.0); // 带一个已知问题——对比非平凡清单
    const ValidationReport a = validateRobotDesignDescription(d);
    const ValidationReport b = validateRobotDesignDescription(d);
    ASSERT_EQ(a.issues.size(), b.issues.size());
    for (std::size_t i = 0; i < a.issues.size(); ++i) {
        EXPECT_EQ(a.issues[i].severity, b.issues[i].severity);
        EXPECT_EQ(a.issues[i].code, b.issues[i].code);
        EXPECT_EQ(a.issues[i].fieldPath, b.issues[i].fieldPath);
        EXPECT_EQ(a.issues[i].detail, b.issues[i].detail);
    }
}

// =====================================================================
// B. RT-CPL-1 耦合矩阵（acceptance 1——校验器部分）。
// =====================================================================

/** RT-CPL-1 良态分支：κ≈1.77 的可逆 C 零问题通过（"编译入模型"的
 *  后续断言归 RT-T04/T11/T12——见文件头覆盖面声明）。 */
TEST(CouplingValidation, WellConditionedCPasses_RT_CPL_1)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 2.0, 1.0, 0.0, 3.0); // [[2,1],[0,3]]
    const ValidationReport report = validateRobotDesignDescription(d);
    EXPECT_TRUE(report.ok());
    expectNoIssueUnderPath(report, "drivetrain.coupling");
}

/** RT-CPL-1 奇异分支①：det=0 的 C → InputInvalid＋比较型诊断（σmin 对
 *  奇异分界），路径 drivetrain.coupling.C。 */
TEST(CouplingValidation, SingularCBlocked_RT_CPL_1)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 1.0, 2.0, 2.0, 4.0); // 行2＝行1×2，det=0
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "drivetrain.coupling.C");
    ASSERT_NE(issue->comparison, std::nullopt) << "MDL-21/M-6：阻止必须带比较型诊断";
    EXPECT_EQ(issue->comparison->quantity, "coupling-min-singular-value");
    // 奇异分界＝σmax×1e-12；该矩阵 σmax≈5 ⇒ 分界≈5e-12（相对断言防 ulp 抖动）。
    EXPECT_GT(issue->comparison->threshold, 4e-12);
    EXPECT_LT(issue->comparison->threshold, 6e-12);
    EXPECT_LE(issue->comparison->actual, 5e-12) << "真奇异矩阵 σmin 必落噪声层以下";
    EXPECT_NE(issue->detail.find("奇异"), std::string::npos);
}

/** RT-CPL-1 奇异分支②：近奇异（扰动 1e-14，κ≈2×10¹⁴ ≥1e12 判距）→
 *  奇异分档（相对精度的可分辨性依赖单侧 Jacobi——见实现文件注释）。 */
TEST(CouplingValidation, NearSingularDetApproxZeroBlocked_RT_CPL_1)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 1.0, 1.0, 1.0, 1.0 + 1e-14);
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "drivetrain.coupling.C");
    ASSERT_NE(issue->comparison, std::nullopt);
    // κ≈2e14 的矩阵其 σmin≈5e-15，远低于奇异分界 σmax×1e-12=2e-12。
    EXPECT_LE(issue->comparison->actual, 2e-12) << "σmin 应在奇异分界之下";
}

/** RT-CPL-1 病态分支：κ=1×10¹⁰ 的 C → InputInvalid＋比较型诊断
 *  （实际条件数/阈值 1×10⁸——P-RT-7"随诊断输出实际条件数"的字面承载）。 */
TEST(CouplingValidation, ConditionNumber1e10Blocked_RT_CPL_1)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 1.0, 0.0, 0.0, 1e-10); // κ=1e10
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "drivetrain.coupling.C");
    ASSERT_NE(issue->comparison, std::nullopt);
    EXPECT_EQ(issue->comparison->quantity, "coupling-condition-number");
    EXPECT_EQ(issue->comparison->threshold, 1e8) << "阈值＝P-RT-7 设计默认 1×10⁸";
    EXPECT_GE(issue->comparison->actual, 1e9) << "实测条件数须在 1e10 量级（诊断如实输出）";
    EXPECT_LE(issue->comparison->actual, 1e11);
    EXPECT_NE(issue->detail.find("条件数"), std::string::npos);
}

/** §4.3.4 非方阵拒绝（诊断明示方阵要求）。 */
TEST(CouplingValidation, NonSquareCBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 1.0, 2.0, 3.0, 4.0);
    d.drivetrain.coupling->cols = 3; // rows=2×cols=3——非方阵（数据量同步改）
    d.drivetrain.coupling->c = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
}

/** §4.3.4 方阵维度＝适用关节数（2×2 矩阵适用 3 关节→拒绝）。 */
TEST(CouplingValidation, DimensionCountMismatchBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 2.0, 1.0, 0.0, 3.0);
    d.drivetrain.coupling->jointRange.count = 3;
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
}

/** 适用范围越出关节链（StructureInvalid——范围是引用结构）。 */
TEST(CouplingValidation, RangeOutOfBoundsStructureInvalid)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(2, 2.0, 1.0, 0.0, 3.0); // [2,4) 越出 3 关节
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.jointRange", RuntimeErrorCode::StructureInvalid);
}

/** 适用关节数为 0（StructureInvalid——空适用段无矩阵意义）。 */
TEST(CouplingValidation, ZeroCountStructureInvalid)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(0, 2.0, 1.0, 0.0, 3.0);
    d.drivetrain.coupling->jointRange.count = 0;
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.jointRange", RuntimeErrorCode::StructureInvalid);
}

/** 扁平数据量失配（rows×cols 与 c.size() 不符→拒绝，防止读越界）。 */
TEST(CouplingValidation, DataSizeMismatchBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 2.0, 1.0, 0.0, 3.0);
    d.drivetrain.coupling->c.pop_back(); // 3 个元素≠2×2
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
}

/** 非有限分量拒绝（NFR-COR-03）。 */
TEST(CouplingValidation, NonFiniteEntryBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 2.0, kNaN, 0.0, 3.0);
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "drivetrain.coupling.C", RuntimeErrorCode::InputInvalid);
}

/** 申报条件数不参与判定（防申报失真绕过阻止——校验器一律重算）：
 *  良态矩阵申报 1e30 不误拒；病态矩阵申报 1.0 不放行。 */
TEST(CouplingValidation, DeclaredConditionNumberNotTrusted)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.coupling = makeCoupling2x2(1, 2.0, 1.0, 0.0, 3.0);
    d.drivetrain.coupling->conditionNumber = 1e30; // 申报失真（良态却报病态值）
    EXPECT_TRUE(validateRobotDesignDescription(d).ok())
        << "申报值不得作为判定输入（良态不应因申报被拒）";

    RobotDesignDescription bad = makeValidDescription();
    bad.drivetrain.coupling = makeCoupling2x2(1, 1.0, 0.0, 0.0, 1e-10);
    bad.drivetrain.coupling->conditionNumber = 1.0; // 申报掩饰（病态报良态值）
    EXPECT_FALSE(validateRobotDesignDescription(bad).ok())
        << "重算条件数必须揭穿申报——病态不得因申报放行（RT-CPL-1）";
}

// =====================================================================
// C. RT-CPX-3 provided 非法（acceptance 1——校验器部分）。
// =====================================================================

/** RT-CPX-3 主体：mass=−1（Provided）→ InputInvalid 精确定位 links[1].mass
 *  （消息含字段路径）；NotProvided 同字段不报（与能力缺失严格区分）。 */
TEST(ProvidedIllegal, NegativeMassLocated_RT_CPX_3)
{
    RobotDesignDescription d = makeValidDescription();
    d.links[1].mass = val(-1.0);
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "links[1].mass", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "links[1].mass");
    EXPECT_NE(issue->detail.find("-1"), std::string::npos) << "诊断应携带实测值";

    // 对照面：同字段 NotProvided→通过（能力缺失降级，非非法——§5.6 分域）。
    RobotDesignDescription missing = makeValidDescription();
    missing.links[1].mass = core::SourcedValue<double>::notProvided();
    const ValidationReport reportMissing = validateRobotDesignDescription(missing);
    EXPECT_TRUE(reportMissing.ok());
    EXPECT_EQ(findIssue(reportMissing, "links[1].mass"), nullptr);
}

/** 质量为 0 同样拒绝（m≤0 同档——§4.3.3）；工具质量同口径。 */
TEST(ProvidedIllegal, ZeroMassAndToolMassBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.links[2].mass = val(0.0);
    expectBlocked(validateRobotDesignDescription(d), "links[2].mass",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription t = makeValidDescription();
    t.tools[0].mass = val(0.0);
    expectBlocked(validateRobotDesignDescription(t), "tools[0].mass",
                  RuntimeErrorCode::InputInvalid);
}

/** 负限速（§4.3.3"provided(负数)→InputInvalid"）；负限加速度同档。 */
TEST(ProvidedIllegal, NegativeVelocityAndAccelerationBlocked)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].maxVelocity = val(-0.5);
    expectBlocked(validateRobotDesignDescription(d), "joints[0].maxVelocity",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription a = makeValidDescription();
    a.joints[1].maxAcceleration = val(-1.0);
    expectBlocked(validateRobotDesignDescription(a), "joints[1].maxAcceleration",
                  RuntimeErrorCode::InputInvalid);
}

/** 传动比须正（§4.3.4"合法：正有限值"）；摩擦三元须正（§4.3.3）。 */
TEST(ProvidedIllegal, NonPositiveRatioAndFrictionBlocked)
{
    RobotDesignDescription r = makeValidDescription();
    r.drivetrain.ratioPerJoint[0] = val(0.0);
    expectBlocked(validateRobotDesignDescription(r), "drivetrain.ratioPerJoint[0]",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription f = makeValidDescription();
    f.friction[2].viscous = val(-0.1);
    expectBlocked(validateRobotDesignDescription(f), "friction[2].viscous",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription c = makeValidDescription();
    c.friction[2].coulomb = val(-0.2);
    expectBlocked(validateRobotDesignDescription(c), "friction[2].coulomb",
                  RuntimeErrorCode::InputInvalid);
}

/** 惯量非对称/非正定（§4.3.3——比较型诊断携带实测偏差/最小特征值）。 */
TEST(ProvidedIllegal, InertiaAsymmetricAndNonSpdBlocked)
{
    // 非对称：I(0,1)=0.005 而 I(1,0)=0——最大不对称 5e-3 ≫ 1e-12。
    RobotDesignDescription a = makeValidDescription();
    a.links[1].inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        rw::math::InertiaMatrix<double>(0.01, 0.005, 0, 0, 0.02, 0, 0, 0, 0.03),
        userProv());
    const ValidationReport reportA = validateRobotDesignDescription(a);
    expectBlocked(reportA, "links[1].inertia", RuntimeErrorCode::InputInvalid);
    EXPECT_EQ(findIssue(reportA, "links[1].inertia")->comparison->quantity,
              "inertia-asymmetry-max");

    // 非正定：diag(−0.01, 0.02, 0.03) 最小特征值 −0.01 ≤ 0（对称的，先过对称关）。
    RobotDesignDescription s = makeValidDescription();
    s.links[1].inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(
        rw::math::InertiaMatrix<double>(-0.01, 0, 0, 0, 0.02, 0, 0, 0, 0.03),
        userProv());
    const ValidationReport reportS = validateRobotDesignDescription(s);
    expectBlocked(reportS, "links[1].inertia", RuntimeErrorCode::InputInvalid);
    EXPECT_EQ(findIssue(reportS, "links[1].inertia")->comparison->quantity,
              "inertia-min-eigenvalue");
}

// =====================================================================
// D. RT-BW-6 非法变换（acceptance 1——校验器部分；循环 Frame/T_world_base
//    切面归属见文件头覆盖面声明）。
// =====================================================================

/** RT-BW-6 反射矩阵：关节 origin 旋转 det=−1（正交但手性翻转）→
 *  InputInvalid＋比较型诊断（det 实测 vs 0）。 */
TEST(IllegalTransform, ReflectedJointOriginBlocked_RT_BW_6)
{
    RobotDesignDescription d = makeValidDescription();
    // R = diag(1,1,−1)：正交、det=−1——反射矩阵经典形态。
    d.joints[0].origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.05, 0.0, 0.3),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, -1));
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "joints[0].origin.R", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "joints[0].origin.R");
    ASSERT_NE(issue->comparison, std::nullopt);
    EXPECT_EQ(issue->comparison->quantity, "rotation-determinant");
    EXPECT_LT(issue->comparison->actual, 0.0);
    EXPECT_NE(issue->detail.find("反射"), std::string::npos);
}

/** RT-BW-6 非正交旋转（缩放混入）→ 正交性比较型诊断（实测偏差 vs 1e-12）。 */
TEST(IllegalTransform, NonOrthogonalJointOriginBlocked_RT_BW_6)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.05, 0.0, 0.3),
        rw::math::Rotation3D<double>(2, 0, 0, 0, 1, 0, 0, 0, 1)); // 列模 2——非正交
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "joints[0].origin.R", RuntimeErrorCode::InputInvalid);
    const ValidationIssue* issue = findIssue(report, "joints[0].origin.R");
    ASSERT_NE(issue->comparison, std::nullopt);
    EXPECT_EQ(issue->comparison->quantity, "rotation-orthogonality-max-deviation");
    EXPECT_GT(issue->comparison->actual, 1e-12);
    EXPECT_EQ(issue->comparison->threshold, 1e-12) << "容差＝§6.6 原文 1×10⁻¹²";
}

/** RT-BW-6 含 NaN 的 T：平移分量 NaN → 定位 <路径>.P（NFR-COR-03 拒绝）。 */
TEST(IllegalTransform, NaNTranslationBlocked_RT_BW_6)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].origin = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kNaN, 0.0, 0.3), identityRotation());
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "joints[0].origin.P", RuntimeErrorCode::InputInvalid);
    EXPECT_NE(findIssue(report, "joints[0].origin.P")->detail.find("非有限"),
              std::string::npos);
}

/** RT-BW-6 场景位姿非有限 → 定位 scene[0].worldPose.P（场景字面反例）。 */
TEST(IllegalTransform, ScenePoseNonFiniteBlocked_RT_BW_6)
{
    RobotDesignDescription d = makeValidDescription();
    d.scene[0].worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(1.0, kInf, 0.0), identityRotation()); // +Inf 同拒
    expectBlocked(validateRobotDesignDescription(d), "scene[0].worldPose.P",
                  RuntimeErrorCode::InputInvalid);
}

/** RT-BW-6 工具 tcpOffset 反射（KIN-14 默认 TCP 权威来源——合法性必检）。 */
TEST(IllegalTransform, ToolTcpOffsetReflectionBlocked_RT_BW_6)
{
    RobotDesignDescription d = makeValidDescription();
    d.tools[0].tcpOffset = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.1),
        rw::math::Rotation3D<double>(-1, 0, 0, 0, 1, 0, 0, 0, 1)); // det=−1
    expectBlocked(validateRobotDesignDescription(d), "tools[0].tcpOffset.R",
                  RuntimeErrorCode::InputInvalid);
}

// =====================================================================
// E. 结构硬反例逐条（acceptance 2——每条拒绝且消息含字段路径）。
// =====================================================================

/** ①关节链为空（§4.3.3 joints ≥1）。 */
TEST(StructureHard, EmptyJointsRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints.clear();
    d.links.clear();
    d.friction.clear();
    d.drivetrain.ratioPerJoint.clear();
    expectBlocked(validateRobotDesignDescription(d), "joints", RuntimeErrorCode::StructureInvalid);
}

/** ②连杆数量失配（§4.3.3 links＝joints+1）。 */
TEST(StructureHard, LinkCountMismatchRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.links.pop_back(); // 3 连杆≠3 关节+1
    expectBlocked(validateRobotDesignDescription(d), "links", RuntimeErrorCode::StructureInvalid);
}

/** ③摩擦清单长度失配（§4.2 逐关节口径；传动比清单同口径）。 */
TEST(StructureHard, FrictionLengthMismatchRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.friction.pop_back(); // 2≠3
    expectBlocked(validateRobotDesignDescription(d), "friction", RuntimeErrorCode::StructureInvalid);
}

/** ④传动比清单长度失配。 */
TEST(StructureHard, RatioLengthMismatchRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.drivetrain.ratioPerJoint.pop_back();
    expectBlocked(validateRobotDesignDescription(d), "drivetrain.ratioPerJoint",
                  RuntimeErrorCode::StructureInvalid);
}

/** ⑤契约版本 0（§4.3.1 合法 ≥1——InputInvalid 值域）。 */
TEST(StructureHard, ZeroContractVersionRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.descriptionContractVersion = 0;
    expectBlocked(validateRobotDesignDescription(d), "descriptionContractVersion",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑥机器人局部名空（§4.3.3 非法：空/含 '/'）；⑦含 '/' 同拒。 */
TEST(StructureHard, IllegalRobotLocalNameRejected)
{
    RobotDesignDescription empty = makeValidDescription();
    empty.robotLocalName.clear();
    expectBlocked(validateRobotDesignDescription(empty), "robotLocalName",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription slash = makeValidDescription();
    slash.robotLocalName = "IRB/Test";
    expectBlocked(validateRobotDesignDescription(slash), "robotLocalName",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑧关节局部名空。 */
TEST(StructureHard, EmptyJointLocalNameRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].localName.clear();
    expectBlocked(validateRobotDesignDescription(d), "joints[0].localName",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑨零轴（§4.3.3 axis 非法：零向量）；⑩轴含 NaN（NFR-COR-03）同路径。 */
TEST(StructureHard, ZeroAxisAndNonFiniteAxisRejected)
{
    RobotDesignDescription zero = makeValidDescription();
    zero.joints[0].axis = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    expectBlocked(validateRobotDesignDescription(zero), "joints[0].axis",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription nanAxis = makeValidDescription();
    nanAxis.joints[0].axis = rw::math::Vector3D<double>(1.0, kNaN, 0.0);
    expectBlocked(validateRobotDesignDescription(nanAxis), "joints[0].axis",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑪旋转关节缺下界（§4.3.3 必填——硬边界缺失走 InputInvalid 而非降级）。 */
TEST(StructureHard, MissingLowerBoundRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].lower = core::SourcedValue<double>::notProvided();
    expectBlocked(validateRobotDesignDescription(d), "joints[0].lower",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑫旋转关节缺上界。 */
TEST(StructureHard, MissingUpperBoundRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].upper = core::SourcedValue<double>::notProvided();
    expectBlocked(validateRobotDesignDescription(d), "joints[0].upper",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑬qmin≥qmax（§4.3.3 明文非法——定位于 upper，消息带两值）。 */
TEST(StructureHard, LowerAboveUpperRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].lower = val(1.0);
    d.joints[0].upper = val(-1.0);
    expectBlocked(validateRobotDesignDescription(d), "joints[0].upper",
                  RuntimeErrorCode::InputInvalid);
    // 等值边界（qmin==qmax）同为非法。
    RobotDesignDescription eq = makeValidDescription();
    eq.joints[0].lower = val(0.5);
    eq.joints[0].upper = val(0.5);
    expectBlocked(validateRobotDesignDescription(eq), "joints[0].upper",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑭Continuous 携带限位（§4.2"Continuous＝NotProvided"——两端各自拒绝）。 */
TEST(StructureHard, ContinuousWithBoundsRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].type = JointType::Continuous;
    d.joints[0].workingRange = WorkingRange{-3.14159, 3.14159};
    const ValidationReport report = validateRobotDesignDescription(d);
    expectBlocked(report, "joints[0].lower", RuntimeErrorCode::InputInvalid);
    expectBlocked(report, "joints[0].upper", RuntimeErrorCode::InputInvalid);
    EXPECT_TRUE(report.ok() == false);
    // Continuous 携带合法工作范围本身不报错（MDL-12 正道）。
    EXPECT_EQ(findIssue(report, "joints[0].workingRange"), nullptr);
}

/** ⑮非 Continuous 携带工作范围（§4.3.3"仅 Continuous"）。 */
TEST(StructureHard, WorkingRangeOnRevoluteRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].workingRange = WorkingRange{-1.0, 1.0};
    expectBlocked(validateRobotDesignDescription(d), "joints[0].workingRange",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑯工作范围区间非法（qmin≥qmax／非有限——MDL-12 有限区间）。 */
TEST(StructureHard, InvalidWorkingRangeRejected)
{
    RobotDesignDescription rev = makeValidDescription();
    rev.joints[0].type = JointType::Continuous;
    rev.joints[0].lower = core::SourcedValue<double>::notProvided();
    rev.joints[0].upper = core::SourcedValue<double>::notProvided();
    rev.joints[0].workingRange = WorkingRange{2.0, 1.0};
    expectBlocked(validateRobotDesignDescription(rev), "joints[0].workingRange",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription inf = rev;
    inf.joints[0].workingRange = WorkingRange{-kInf, 1.0};
    expectBlocked(validateRobotDesignDescription(inf), "joints[0].workingRange",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑰Custom 预设缺 EAA（§4.2"Custom 时必填"）。 */
TEST(StructureHard, CustomWithoutEaaRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.base.preset = InstallationPresetToken::Custom;
    d.base.customEaa = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
    expectBlocked(validateRobotDesignDescription(d), "base.customEaa",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑱基座位置非有限。 */
TEST(StructureHard, NonFiniteBasePositionRejected)
{
    RobotDesignDescription d = makeValidDescription();
    d.base.basePosition = rw::math::Vector3D<double>(0.0, kNaN, 0.5);
    expectBlocked(validateRobotDesignDescription(d), "base.basePosition",
                  RuntimeErrorCode::InputInvalid);
}

/** ⑲连杆/工具/场景局部名空（同关节口径）。 */
TEST(StructureHard, EmptyLinkToolSceneNamesRejected)
{
    RobotDesignDescription l = makeValidDescription();
    l.links[0].localName.clear();
    expectBlocked(validateRobotDesignDescription(l), "links[0].localName",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription t = makeValidDescription();
    t.tools[0].localName.clear();
    expectBlocked(validateRobotDesignDescription(t), "tools[0].localName",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription s = makeValidDescription();
    s.scene[0].localName.clear();
    expectBlocked(validateRobotDesignDescription(s), "scene[0].localName",
                  RuntimeErrorCode::InputInvalid);
}

// =====================================================================
// F. 单位校验切面（acceptance 3——非有限/非法单位拒绝，NFR-COR-03 编译
//    边界＋§4.4 量级抽样警告不阻断）。
// =====================================================================

/** 非有限限位拒绝（NaN 下界→InputInvalid——"非有限不得静默转 0/通过"
 *  的限位字段落点；±Inf 上界同档）。 */
TEST(UnitValidation, NonFiniteBoundsRejected_ACC3_NFR_COR_03)
{
    RobotDesignDescription nan = makeValidDescription();
    nan.joints[0].lower = val(kNaN);
    expectBlocked(validateRobotDesignDescription(nan), "joints[0].lower",
                  RuntimeErrorCode::InputInvalid);

    RobotDesignDescription inf = makeValidDescription();
    inf.joints[0].upper = val(kInf);
    expectBlocked(validateRobotDesignDescription(inf), "joints[0].upper",
                  RuntimeErrorCode::InputInvalid);
}

/** 非有限限速拒绝（maxAcceleration ±Inf 同口径——不另设用例，同一检查
 *  骨架；此处钉 maxVelocity 路径）。 */
TEST(UnitValidation, NonFiniteVelocityRejected_ACC3_NFR_COR_03)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].maxVelocity = val(kNaN);
    expectBlocked(validateRobotDesignDescription(d), "joints[0].maxVelocity",
                  RuntimeErrorCode::InputInvalid);
}

/** deg 量级限位（±180 rad>4π×10——§4.4 原文阈值）：UnitMismatch 警告、
 *  不阻断（ok() 保持 true——警告级语义的字面钉住）。 */
TEST(UnitValidation, DegreeMagnitudeBoundWarnsButDoesNotBlock_ACC3)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].lower = val(-180.0); // deg 误作 rad 的典型读数
    d.joints[0].upper = val(180.0);
    const ValidationReport report = validateRobotDesignDescription(d);
    EXPECT_TRUE(report.ok()) << "量级警告不阻断（§4.4——警告级不得翻失败）";
    const ValidationIssue* lo = findIssue(report, "joints[0].lower");
    const ValidationIssue* up = findIssue(report, "joints[0].upper");
    ASSERT_NE(lo, nullptr);
    ASSERT_NE(up, nullptr);
    EXPECT_EQ(lo->severity, ValidationSeverity::Warning);
    EXPECT_EQ(lo->code, RuntimeErrorCode::UnitMismatch);
    EXPECT_NE(lo->detail.find("rad"), std::string::npos) << "警告应提示期望单位";
    EXPECT_EQ(up->severity, ValidationSeverity::Warning);
    EXPECT_EQ(up->code, RuntimeErrorCode::UnitMismatch);
}

/** mm 量级移动限位（±1000 m>1×10²——移动量纲抽样；合法 ±0.5 m 无警告）。 */
TEST(UnitValidation, MillimeterMagnitudePrismaticWarns_ACC3)
{
    RobotDesignDescription bad = makeValidDescription();
    bad.joints[0].type = JointType::Prismatic;
    bad.joints[0].lower = val(-1000.0);
    bad.joints[0].upper = val(1000.0);
    const ValidationReport badReport = validateRobotDesignDescription(bad);
    EXPECT_TRUE(badReport.ok()) << "量级警告不阻断";
    ASSERT_NE(findIssue(badReport, "joints[0].lower"), nullptr);
    EXPECT_EQ(findIssue(badReport, "joints[0].lower")->code, RuntimeErrorCode::UnitMismatch);
    EXPECT_NE(findIssue(badReport, "joints[0].lower")->detail.find("m"), std::string::npos);

    RobotDesignDescription good = makeValidDescription();
    good.joints[0].type = JointType::Prismatic;
    good.joints[0].lower = val(-0.5);
    good.joints[0].upper = val(0.5);
    EXPECT_TRUE(validateRobotDesignDescription(good).issues.empty())
        << "正常工程量级零警告（阴性对照）";
}

/** Fixed 关节提供限位时仍复核值合法性（有限＋有序——类型未禁止但值非法
 *  照拒；不要求提供）。 */
TEST(UnitValidation, FixedJointProvidedBoundsStillValidated)
{
    RobotDesignDescription d = makeValidDescription();
    d.joints[0].type = JointType::Fixed;
    d.joints[0].lower = val(1.0);
    d.joints[0].upper = val(-1.0);
    expectBlocked(validateRobotDesignDescription(d), "joints[0].upper",
                  RuntimeErrorCode::InputInvalid);
    // Fixed 不要求限位：撤掉后干净通过。
    RobotDesignDescription none = makeValidDescription();
    none.joints[0].type = JointType::Fixed;
    none.joints[0].lower = core::SourcedValue<double>::notProvided();
    none.joints[0].upper = core::SourcedValue<double>::notProvided();
    EXPECT_TRUE(validateRobotDesignDescription(none).ok());
}

}  // namespace
