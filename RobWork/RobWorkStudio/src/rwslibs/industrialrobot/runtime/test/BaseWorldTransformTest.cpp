/**
 * @file   BaseWorldTransformTest.cpp
 * @brief  基座—世界变换规则用例组（RT-T06）——§6.5 最小数值验证例、
 *         四预设精确矩阵、正反一致、默认地面、非法变换反例、S9 二次叠加
 *         反例（规则部分）。
 *
 * 设计依据：
 *   - units/runtime.md §6 全章（§6.2 预设精确定义——P-RT-4 冻结矩阵、
 *     §6.3 唯一存储/唯一写入、§6.5 数值验证例、§6.6 错误表与 AT-37
 *     反例观测点）、§4.2（BasePlacementDescription 编辑表示）、§4.3.2
 *     （WorldPlacement 合法域）、§11（RT-BW-1/2/3/6 用例行）
 *   - 需求 MDL-22（安装姿态单一变换/默认地面）、DYN-01（重力投影）、
 *     AT-37（一致消费与倒挂反例）、V15-04（未配置默认地面）、
 *     NFR-COR-02（确定性）/NFR-COR-03（非有限拒绝）
 *   - 任务契约 tasks/foundation/RT-T06.json（acceptance 1～3 的被测主体）
 *
 * ★ 覆盖面声明（防验收误读——RT-T03 校验器测试同款纪律）：本文件覆盖
 *   RT-BW-1/2/3/6 的**规则部分**（§6 纯函数面）与 acceptance 2 的判定
 *   规则（S9 检查的数学面）——
 *   - RT-BW-1 的"编译＋view 读取"全链形态、RT-BW-4 的 S9 编译链拦截、
 *     RT-BW-5 的四消费方替身一致性，依赖 IRuntimeModelView/WC 编译
 *     （RT-T07/T11 交付）——此处以等价纯函数断言到规则面为止；
 *   - RT-BW-3 的"编译"前置在阶段 A 以 resolveWorldBaseTransform 规则面
 *     ＋CanonicalModelBuilder（RT-T04 既有用例回归）共同承载；
 *   - RT-BW-6 的"场景位姿非有限/循环 Frame"切面归 S3 校验器（RT-T03
 *     已交付）与 S6（RT-T07）——本文件只覆盖 T_world_base 变换面。
 *   上述边界随测试文档留痕（RT-STUB-0 同款纪律）。
 *
 * 容差纪律：数值断言一律经 core::closeWithin（附录 D C4 公式——比较
 * 公式单点实现，SA-12）；绝对容差 1×10⁻⁹（附录 D 第 4 项 rt profile，
 * §6.5④ 同尺度）；预设矩阵元素以 EXPECT_EQ 精确位比较（§6.2"编码无
 * 舍入"——近似断言在此反而是缺陷）。
 */

#include <sdurws/ird/runtime/BaseWorldTransform.hpp>

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/runtime/Description.hpp>
#include <sdurws/ird/runtime/Errors.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird::runtime;
namespace core = sdurws::ird::core;

/// 双精度非有限常量（NFR-COR-03 反例注入——NaN 与 ±Inf 两族）。
const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

/// 数值断言容差：附录 D 第 4 项 1×10⁻⁹ m／1×10⁻⁹ rad（§6.5④）。
core::Tolerance tightTol()
{
    return core::Tolerance::make(0.0, 1e-9);
}

// =====================================================================
// 夹具工具（§6.2/§6.5 冻结值——不重复实现规则，只固化测试已知量）。
// =====================================================================

/// 用户输入来源记录（SourcedValue Provided 态必带——core P-1 契约）。
core::ValueProvenance userProv()
{
    return core::ValueProvenance::make(core::ProvenanceKind::UserProvided);
}

/// Provided 的 customEaa 值注入（单位 rad 的旋转矢量）。
core::SourcedValue<rw::math::Vector3D<double>> providedEaa(double x, double y, double z)
{
    return core::SourcedValue<rw::math::Vector3D<double>>::provided(
        rw::math::Vector3D<double>(x, y, z), userProv());
}

/// 逐元素构造旋转（测试夹具禁用外联 identity()/rot*——RT-T03 同款纪律）。
rw::math::Rotation3D<double> makeRotation(double r00, double r01, double r02,
                                          double r10, double r11, double r12,
                                          double r20, double r21, double r22)
{
    return rw::math::Rotation3D<double>(r00, r01, r02, r10, r11, r12,
                                        r20, r21, r22);
}

/// §6.5 最小数值验证例的 T_world_base：倒挂预设、吊装高度 2.0 m。
rw::math::Transform3D<double> numericExampleTransform()
{
    return rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 2.0),
        makeRotation(1, 0, 0, 0, -1, 0, 0, 0, -1));  // R_x(π)——P-RT-4 冻结矩阵
}

/// 断言三维向量逐元素落在容差内（core::closeWithin 逐元素应用）。
void expectVectorClose(const rw::math::Vector3D<double>& actual,
                       const rw::math::Vector3D<double>& expected,
                       core::Tolerance tol, const char* what)
{
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(core::closeWithin(actual(i), expected(i), tol))
            << what << " 分量(" << i << ") 实测 " << actual(i) << " 期望 "
            << expected(i);
    }
}

/// 断言旋转逐元素落在容差内（C4 公式逐元素应用——防正负抵消）。
void expectRotationClose(const rw::math::Rotation3D<double>& actual,
                         const rw::math::Rotation3D<double>& expected,
                         core::Tolerance tol, const char* what)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            EXPECT_TRUE(core::closeWithin(actual(i, j), expected(i, j), tol))
                << what << " 元素(" << i << "," << j << ") 实测 " << actual(i, j)
                << " 期望 " << expected(i, j);
        }
    }
}

/// 断言变换的平移与旋转均落在容差内。
void expectTransformClose(const rw::math::Transform3D<double>& actual,
                          const rw::math::Transform3D<double>& expected,
                          core::Tolerance tol, const char* what)
{
    expectVectorClose(actual.P(), expected.P(), tol, what);
    expectRotationClose(actual.R(), expected.R(), tol, what);
}

// =====================================================================
// 组 1：预设→矩阵（RT-BW-2 精确面＋P-RT-4 冻结留痕断言）。
// =====================================================================

/** RT-BW-2：Ground 预设＝单位阵（§6.2 表第 1 行——元素精确）。 */
TEST(PresetRotationTest, GroundIsExactlyIdentity_RT_BW_2)
{
    const rw::math::Rotation3D<double> r =
        installationPresetRotation(InstallationPresetToken::Ground);
    // 元素级精确比较（§6.2"编码无舍入"——预设矩阵不用容差断言）。
    EXPECT_EQ(r(0, 0), 1.0);
    EXPECT_EQ(r(1, 1), 1.0);
    EXPECT_EQ(r(2, 2), 1.0);
    EXPECT_EQ(r(0, 1), 0.0);
    EXPECT_EQ(r(0, 2), 0.0);
    EXPECT_EQ(r(1, 0), 0.0);
    EXPECT_EQ(r(1, 2), 0.0);
    EXPECT_EQ(r(2, 0), 0.0);
    EXPECT_EQ(r(2, 1), 0.0);
}

/** RT-BW-2／P-RT-4：Inverted＝R_x(π)＝diag(1,−1,−1)（冻结矩阵逐位）。 */
TEST(PresetRotationTest, InvertedIsFrozenRxPi_RT_BW_2_P_RT_4)
{
    const rw::math::Rotation3D<double> r =
        installationPresetRotation(InstallationPresetToken::Inverted);
    // P-RT-4 冻结留痕断言（acceptance 3 的可测面）：倒挂轴向＝绕 X——
    // §6.2 按"基座 Z 反向"冻结为 diag(1,−1,−1)，与 modeling 卡交叉核对
    // 的对照基准即本断言的期望值（分歧处置见 §15.3 P-RT-4）。
    EXPECT_EQ(r(0, 0), 1.0);
    EXPECT_EQ(r(1, 1), -1.0);
    EXPECT_EQ(r(2, 2), -1.0);
    EXPECT_EQ(r(0, 1), 0.0);
    EXPECT_EQ(r(0, 2), 0.0);
    EXPECT_EQ(r(1, 0), 0.0);
    EXPECT_EQ(r(1, 2), 0.0);
    EXPECT_EQ(r(2, 0), 0.0);
    EXPECT_EQ(r(2, 1), 0.0);
}

/** RT-BW-2／P-RT-4：Wall＝R_y(π/2)（第 1 行 [0,0,1]、第 3 行 [−1,0,0]）。 */
TEST(PresetRotationTest, WallIsFrozenRyPiHalf_RT_BW_2_P_RT_4)
{
    const rw::math::Rotation3D<double> r =
        installationPresetRotation(InstallationPresetToken::Wall);
    // §6.2 表第 3 行的显式矩阵形态（非 rotY 数值近似——元素精确 ±1/0）。
    EXPECT_EQ(r(0, 0), 0.0);
    EXPECT_EQ(r(0, 1), 0.0);
    EXPECT_EQ(r(0, 2), 1.0);
    EXPECT_EQ(r(1, 0), 0.0);
    EXPECT_EQ(r(1, 1), 1.0);
    EXPECT_EQ(r(1, 2), 0.0);
    EXPECT_EQ(r(2, 0), -1.0);
    EXPECT_EQ(r(2, 1), 0.0);
    EXPECT_EQ(r(2, 2), 0.0);
}

/** 语义断言（§6.2"语义（基座 Z 轴指向）"列）：倒挂时基座 +Z 指世界 −Z。 */
TEST(PresetRotationTest, InvertedBaseZPointsWorldDown_P_RT_4)
{
    // 基座 Z 轴在世界系的像＝R·(0,0,1)＝R 的第 3 列（列向量映像的矩阵
    // 定义——逐元素读取，不经 rw 的 R*v 外联乘法，冒烟 header-only 纪律）；
    // 倒挂必须是世界 −Z（竖直向下，吊装语义）。这也是"绕 X 转 π"的
    // P-RT-4 冻结选择的语义锚（§15.3：分歧时以建模侧口径为准）。
    const rw::math::Rotation3D<double> r =
        installationPresetRotation(InstallationPresetToken::Inverted);
    expectVectorClose(rw::math::Vector3D<double>(r(0, 2), r(1, 2), r(2, 2)),
                      rw::math::Vector3D<double>(0.0, 0.0, -1.0),
                      tightTol(), "倒挂基座 Z 轴像");
}

/** 语义断言：壁装时基座 +Z 指世界水平方向（R_y(π/2) 下＝世界 +X）。 */
TEST(PresetRotationTest, WallBaseZPointsWorldHorizontal_P_RT_4)
{
    // 同上取 R 第 3 列：R_y(π/2) 的第 3 行 [−1,0,0] 说明世界 Z 在基座系
    // −X；其列像 (1,0,0) 即基座 Z 的世界像——水平（壁装语义，§6.2 表）。
    const rw::math::Rotation3D<double> r =
        installationPresetRotation(InstallationPresetToken::Wall);
    expectVectorClose(rw::math::Vector3D<double>(r(0, 2), r(1, 2), r(2, 2)),
                      rw::math::Vector3D<double>(1.0, 0.0, 0.0),
                      tightTol(), "壁装基座 Z 轴像");
}

/** RT-BW-2：三固定预设的矩阵元素全部 ∈ {0,±1}（§6.2"编码无舍入"）。 */
TEST(PresetRotationTest, FixedPresetElementsExactlySignedUnit_RT_BW_2)
{
    // 预设矩阵元素全为 {0,±1} 是编码确定性的前提（身份编码无舍入——
    // §6.2）；逐元素按值域精确检查（位相等，非近似）。
    for (const auto preset : {InstallationPresetToken::Ground,
                              InstallationPresetToken::Inverted,
                              InstallationPresetToken::Wall}) {
        const rw::math::Rotation3D<double> r = installationPresetRotation(preset);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                const double v = r(i, j);
                ASSERT_TRUE(v == 0.0 || v == 1.0 || v == -1.0)
                    << "预设(" << static_cast<int>(preset) << ") 元素("
                    << i << "," << j << ")=" << v << " ∉ {0,±1}";
            }
        }
    }
}

/** Custom 无固定预设矩阵——fail-fast 拒绝（NFR-COR-03 不伪造占位阵）。 */
TEST(PresetRotationTest, CustomPresetHasNoFixedMatrix_Throws)
{
    // §6.2 表：Custom 的 R 为"任意欧拉角编辑结果"——取"预设矩阵"属
    // 调用方误用，InputInvalid fail-fast（矩阵应经 rotationFromCustomEaa）。
    EXPECT_THROW(installationPresetRotation(InstallationPresetToken::Custom),
                 RuntimeError);
    try {
        installationPresetRotation(InstallationPresetToken::Custom);
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::InputInvalid);
    }
}

// =====================================================================
// 组 2：Custom 编辑表示换算（EAA→R，Rodrigues）。
// =====================================================================

/** 零旋转矢量＝恒等（θ＝0 的数学极限；合法性归一致性面判定）。 */
TEST(CustomEaaTest, ZeroEaaYieldsIdentity)
{
    const rw::math::Rotation3D<double> r =
        rotationFromCustomEaa(rw::math::Vector3D<double>(0.0, 0.0, 0.0));
    expectRotationClose(r, makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1),
                        core::Tolerance::make(0.0, 0.0), "零 EAA");
}

/** 解析对照：绕 Z 转 +π/2 的标准矩阵（cos/sin 解析值，1×10⁻¹²）。 */
TEST(CustomEaaTest, QuarterTurnAboutZMatchesAnalytic)
{
    const double halfPi = std::acos(-1.0) / 2.0;  // π/2（rad）
    const rw::math::Rotation3D<double> r =
        rotationFromCustomEaa(rw::math::Vector3D<double>(0.0, 0.0, halfPi));
    // R_z(π/2)＝[[0,−1,0],[1,0,0],[0,0,1]]——Rodrigues 与解析矩阵互证；
    // 容差 1×10⁻¹²（附录 D 第 6 项同尺度——三角函数路径的浮点表示误差）。
    expectRotationClose(r,
                        makeRotation(0, -1, 0, 1, 0, 0, 0, 0, 1),
                        core::Tolerance::make(0.0, 1e-12), "R_z(π/2)");
}

/** 数学互证（P-RT-4）：EAA(π,0,0) 的 Rodrigues 结果＝冻结矩阵 R_x(π)。 */
TEST(CustomEaaTest, HalfTurnAboutXMatchesFrozenMatrix_P_RT_4)
{
    const double pi = std::acos(-1.0);  // π（rad）
    const rw::math::Rotation3D<double> r =
        rotationFromCustomEaa(rw::math::Vector3D<double>(pi, 0.0, 0.0));
    // 绕 X 转 π 的数学结果与 §6.2 冻结矩阵 diag(1,−1,−1) 在 1×10⁻¹² 内
    // 一致——证明冻结矩阵确为"倒挂 180°"的精确表示（P-RT-4 轴向选择
    // 的数学自洽性；预设函数本身不走三角函数，见实现文件）。
    expectRotationClose(r, makeRotation(1, 0, 0, 0, -1, 0, 0, 0, -1),
                        core::Tolerance::make(0.0, 1e-12), "EAA(π,0,0)");
}

/** NFR-COR-03：非有限 EAA 分量拒绝（NaN 与 +Inf 两族）。 */
TEST(CustomEaaTest, NonFiniteEaaThrowsInputInvalid)
{
    EXPECT_THROW(rotationFromCustomEaa(
                     rw::math::Vector3D<double>(kNaN, 0.0, 0.0)),
                 RuntimeError);
    EXPECT_THROW(rotationFromCustomEaa(
                     rw::math::Vector3D<double>(0.0, kInf, 0.0)),
                 RuntimeError);
    try {
        rotationFromCustomEaa(rw::math::Vector3D<double>(kNaN, 0.0, 0.0));
    } catch (const RuntimeError& e) {
        EXPECT_EQ(e.code(), RuntimeErrorCode::InputInvalid);
    }
}

/** NFR-COR-02：确定性——同输入两次调用逐位同输出（位相等）。 */
TEST(CustomEaaTest, DeterministicBitwiseRepeat)
{
    const rw::math::Vector3D<double> eaa(0.3, -0.7, 1.1);  // 任意有限 rad 值
    const rw::math::Rotation3D<double> a = rotationFromCustomEaa(eaa);
    const rw::math::Rotation3D<double> b = rotationFromCustomEaa(eaa);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ASSERT_EQ(a(i, j), b(i, j)) << "元素(" << i << "," << j << ")";
        }
    }
}

// =====================================================================
// 组 3：正解（§6.1 正向——编辑表示→T_world_base；RT-BW-3 默认地面）。
// =====================================================================

/** RT-BW-3：未配置 base 的默认解析＝地面安装（R=I、t=0）——确定性默认
 *  非错误（V15-04；Description.base 的缺省值路径）。 */
TEST(ResolveTest, DefaultBaseResolvesGroundIdentity_RT_BW_3)
{
    // 默认构造的 BasePlacementDescription：preset=Ground、customEaa 未
    // 提供、basePosition=0——即"未显式配置"形态（URDF/Xacro 导入与
    // 空白模板同路径，V15-04）。
    const BasePlacementDescription base;
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_TRUE(resolved.ok()) << "默认基座布置必须可解析（确定性默认）";
    const rw::math::Transform3D<double> t = resolved.get();
    expectTransformClose(t, rw::math::Transform3D<double>(
                                rw::math::Vector3D<double>(0, 0, 0),
                                makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1)),
                          tightTol(), "默认地面 T_world_base");
    // 解析结果同时通过两层校验（矩阵层＋一致性层——Ground 无一致性约束，
    // RT-BW-3"无诊断 error"的规则面）。
    EXPECT_FALSE(checkWorldBaseTransform(t).has_value());
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Ground, t).has_value());
}

/** Ground＋布置平移：R=I、t=basePosition 逐分量保留（单位 m）。 */
TEST(ResolveTest, GroundCarriesBasePosition)
{
    BasePlacementDescription base;
    base.basePosition = rw::math::Vector3D<double>(0.5, -1.2, 3.0);
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_TRUE(resolved.ok());
    expectVectorClose(resolved.get().P(),
                      rw::math::Vector3D<double>(0.5, -1.2, 3.0), tightTol(),
                      "Ground 平移");
    expectRotationClose(resolved.get().R(), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1),
                        tightTol(), "Ground 旋转");
}

/** Inverted：平移保留＋旋转＝P-RT-4 冻结矩阵（正解走 §6.2 权威映射）。 */
TEST(ResolveTest, InvertedResolvesFrozenMatrix)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Inverted;
    base.basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_TRUE(resolved.ok());
    expectRotationClose(resolved.get().R(),
                        installationPresetRotation(InstallationPresetToken::Inverted),
                        core::Tolerance::make(0.0, 0.0), "Inverted 旋转（精确）");
    // 正解结果过自身一致性校验（Inverted↔R_x(π) 匹配——规则链自洽）。
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Inverted, resolved.get())
            .has_value());
}

/** Wall：平移保留＋旋转＝R_y(π/2)（同上，P-RT-4 第二冻结值）。 */
TEST(ResolveTest, WallResolvesFrozenMatrix)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Wall;
    base.basePosition = rw::math::Vector3D<double>(1.0, 0.0, 0.5);
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_TRUE(resolved.ok());
    expectRotationClose(resolved.get().R(),
                        installationPresetRotation(InstallationPresetToken::Wall),
                        core::Tolerance::make(0.0, 0.0), "Wall 旋转（精确）");
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Wall, resolved.get())
            .has_value());
}

/** Custom 而 customEaa 未提供→err InputInvalid（§4.2"Custom 时必填"；缺失不伪造）。 */
TEST(ResolveTest, CustomWithoutEaaRejected)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Custom;
    // customEaa 保持 NotProvided——SourcedValue 缺省态。
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_FALSE(resolved.ok()) << "Custom 缺 EAA 必须拒绝";
    EXPECT_EQ(resolved.error().code(), RuntimeErrorCode::InputInvalid);
    // 中文定位细节经 what()（"token: detail"形态——Errors.hpp 前缀约定）。
    EXPECT_FALSE(std::string(resolved.error().what()).empty());
}

/** Custom 经 Rodrigues 换算：与 rotationFromCustomEaa 逐位一致（同一实现点）。 */
TEST(ResolveTest, CustomResolvesViaRodrigues)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Custom;
    base.customEaa = providedEaa(0.0, 0.0, std::acos(-1.0) / 2.0);
    base.basePosition = rw::math::Vector3D<double>(0.1, 0.2, 0.3);
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_TRUE(resolved.ok());
    // 与直接换算结果位相等（单一实现点——正解不私设第二套换算）。
    const rw::math::Rotation3D<double> direct = rotationFromCustomEaa(
        rw::math::Vector3D<double>(0.0, 0.0, std::acos(-1.0) / 2.0));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ASSERT_EQ(resolved.get().R()(i, j), direct(i, j));
        }
    }
    // Custom 非恒等旋转过一致性校验（§4.3.2 合法域正向例）。
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Custom, resolved.get())
            .has_value());
}

/** NFR-COR-03／RT-BW-6 切面：basePosition 含 NaN→err InputInvalid。 */
TEST(ResolveTest, NonFinitePositionRejected)
{
    BasePlacementDescription base;
    base.basePosition = rw::math::Vector3D<double>(0.0, kNaN, 0.0);
    const auto resolved = resolveWorldBaseTransform(base);
    ASSERT_FALSE(resolved.ok());
    EXPECT_EQ(resolved.error().code(), RuntimeErrorCode::InputInvalid);
}

// =====================================================================
// 组 4：T_world_base 合法性校验（RT-BW-6 变换面反例——§4.3.2/§6.6）。
// =====================================================================

/** 正向例：恒等变换（＋任意有限平移）通过（返回 nullopt）。 */
TEST(ValidateTest, WellFormedTransformPasses)
{
    const rw::math::Transform3D<double> t(
        rw::math::Vector3D<double>(1.5, -2.5, 3.5), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1));
    EXPECT_FALSE(checkWorldBaseTransform(t).has_value());
}

/** RT-BW-6：反射矩阵（diag(1,1,−1)——正交但 det=−1）拒绝。 */
TEST(ValidateTest, ReflectionMatrixRejected_RT_BW_6)
{
    // 反射变换会翻转手性（§4.3.2"det＝+1"）——RT-BW-6 反例族之一；
    // detail 须携带实测 det 值（§6.6"实测偏差值的比较型诊断"）。
    const rw::math::Transform3D<double> reflection(
        rw::math::Vector3D<double>(0, 0, 0), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, -1));
    const auto err = checkWorldBaseTransform(reflection);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
    // RuntimeError 的中文细节在 what() 消息内（"token: detail"形态——
    // Errors.hpp 消息前缀约定）；断言其中携带实测 det 值的比较型素材。
    EXPECT_NE(std::string(err->what()).find("det"), std::string::npos)
        << "what() 应含实测行列式值: " << err->what();
}

/** RT-BW-6：非正交矩阵拒绝（what() 含实测最大偏差——比较型）。 */
TEST(ValidateTest, NonOrthogonalMatrixRejected_RT_BW_6)
{
    // 在单位阵上叠加 1×10⁻⁶ 量级扰动——远超 1×10⁻¹² 容差（§6.6），
    // 必须拒绝且 what() 回显实测偏差。
    rw::math::Rotation3D<double> r = makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
    r(0, 1) = 1e-6;
    const rw::math::Transform3D<double> t(rw::math::Vector3D<double>(0, 0, 0), r);
    const auto err = checkWorldBaseTransform(t);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
    EXPECT_NE(std::string(err->what()).find("非正交"), std::string::npos)
        << "what() 应说明违例项: " << err->what();
}

/** RT-BW-6：非有限分量拒绝（旋转 NaN／平移 Inf 两族）。 */
TEST(ValidateTest, NonFiniteComponentsRejected_RT_BW_6)
{
    // 旋转 NaN：非有限拦截先于正交性（NaN 使比较无意义——§6.6"奇异
    // 姿态……复核矩阵有限性"）。
    rw::math::Rotation3D<double> rNaN = makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
    rNaN(2, 2) = kNaN;
    auto err = checkWorldBaseTransform(
        rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0), rNaN));
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);

    // 平移 +Inf（单位 m——量纲异常经由同一合法域拦截）。
    err = checkWorldBaseTransform(rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kInf, 0, 0), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1)));
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
}

/** 容差不收窄：正交性偏差落在 1×10⁻¹² 内的近正交阵必须通过。 */
TEST(ValidateTest, NearOrthogonalWithinTolerancePasses)
{
    // 元素偏差 5×10⁻¹³：正交性偏差同量级（≤1×10⁻¹²）——§6.6 容差的
    // 边界内行为（拒绝阈值的"不误判"面）。
    rw::math::Rotation3D<double> r = makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
    r(0, 1) = 5e-13;
    const auto err = checkWorldBaseTransform(
        rw::math::Transform3D<double>(rw::math::Vector3D<double>(0, 0, 0), r));
    EXPECT_FALSE(err.has_value())
        << "容差内偏差不得拒绝"
        << (err.has_value() ? "——" + std::string(err->what()) : "");
}

// =====================================================================
// 组 5：预设一致性（§6.2 S5 构造校验规则面＋§4.3.2 合法域）。
// =====================================================================

/** Inverted＋冻结矩阵：一致通过（§4.3.2 合法域正向例）。 */
TEST(PresetConsistencyTest, InvertedWithFrozenMatrixPasses)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Inverted;
    const auto t = resolveWorldBaseTransform(base);
    ASSERT_TRUE(t.ok());
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Inverted, t.get())
            .has_value());
}

/** §6.2 括号规则：Inverted 而 R=I→InputInvalid（"preset≠ground 而 R=I"）。 */
TEST(PresetConsistencyTest, InvertedWithIdentityRejected)
{
    const rw::math::Transform3D<double> t(
        rw::math::Vector3D<double>(0, 0, 2), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1));
    const auto err = checkPresetConsistency(InstallationPresetToken::Inverted, t);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
}

/** Wall＋R_x(π)：预设矩阵失配拒绝（来源记录与实际矩阵矛盾）。 */
TEST(PresetConsistencyTest, WallWithWrongPresetMatrixRejected)
{
    const rw::math::Transform3D<double> t(
        rw::math::Vector3D<double>(0, 0, 0),
        installationPresetRotation(InstallationPresetToken::Inverted));  // 声明 Wall 实为倒挂矩阵
    const auto err = checkPresetConsistency(InstallationPresetToken::Wall, t);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
}

/** §4.3.2：Custom 而 R≈I→InputInvalid（明示非法域——与 builder 同源容差）。 */
TEST(PresetConsistencyTest, CustomWithIdentityRejected)
{
    const rw::math::Transform3D<double> t(
        rw::math::Vector3D<double>(0.5, 0, 0), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1));
    const auto err = checkPresetConsistency(InstallationPresetToken::Custom, t);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->code(), RuntimeErrorCode::InputInvalid);
}

/** Custom＋任意非恒等旋转：一致通过（合法域正向例）。 */
TEST(PresetConsistencyTest, CustomWithRotatedMatrixPasses)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Custom;
    base.customEaa = providedEaa(0.2, 0.4, -0.6);
    const auto t = resolveWorldBaseTransform(base);
    ASSERT_TRUE(t.ok());
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Custom, t.get())
            .has_value());
}

/** Ground 无一致性约束（§6.2 括号规则只覆盖非 ground 侧——不扩大语义）。 */
TEST(PresetConsistencyTest, GroundAcceptsIdentityAndArbitraryRotation)
{
    // R=I（默认地面）与 R=任意正交阵均不因 Ground 声明被拒——Ground
    // 兼作未显式配置的默认解释（V15-04），一致性拒绝会误伤默认路径。
    const rw::math::Transform3D<double> ground(
        rw::math::Vector3D<double>(0, 0, 0), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1));
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Ground, ground).has_value());
    const rw::math::Transform3D<double> arbitrary(
        rw::math::Vector3D<double>(0, 0, 0),
        rotationFromCustomEaa(rw::math::Vector3D<double>(0.0, 0.5, 0.0)));
    EXPECT_FALSE(
        checkPresetConsistency(InstallationPresetToken::Ground, arbitrary).has_value());
}

// =====================================================================
// 组 6：正反解与数值例（RT-BW-1 ①②③＋RT-BW-2 正反一致）。
// =====================================================================

/** RT-BW-1①（§6.5 手算例）：倒挂 T 下 p_base=(0.3,0.2,0.5)→p_world=
 *  (0.3,−0.2,1.5)——Z 分量反号＋吊装高度 2.0 m。 */
TEST(InverseAndPointTest, NumericExamplePointTransform_RT_BW_1)
{
    const rw::math::Vector3D<double> pWorld = transformBasePointToWorld(
        numericExampleTransform(), rw::math::Vector3D<double>(0.3, 0.2, 0.5));
    expectVectorClose(pWorld, rw::math::Vector3D<double>(0.3, -0.2, 1.5),
                      tightTol(), "§6.5① p_world");
}

/** RT-BW-1②（§6.5 手算例）：逆变换平移＝(0,0,+2)；p_world 回代与输入
 *  逐位一致（正反闭环）。
 *
 * ★ 卡文笔误修正留痕（DTB §5.4，随 units/runtime.md §6.5②/§15.4 v0.7）：
 *   卡文中间式 "t'＝−Rᵀt＝(0,0,−2.0)" 的数值为转写笔误——代入
 *   Rᵀ＝diag(1,−1,−1)、t＝(0,0,2) 得 −Rᵀt＝(0,0,+2)；且同一手算例的
 *   闭环断言（p_base'＝(0.3,0.2,0.5) 与输入逐位一致）唯一要求 t'＝+2
 *   （若 t'＝−2 则回代得 (0.3,0.2,−3.5)，与断言矛盾）。本用例按数学
 *   正确值断言并验证闭环。 */
TEST(InverseAndPointTest, NumericExampleInverseRoundtrip_RT_BW_1)
{
    const rw::math::Transform3D<double> tInv = invertWorldBase(numericExampleTransform());
    // 逆平移 t'＝−Rᵀ·t＝−(0,0,−2)＝(0,0,+2)（见上：卡文笔误修正说明）。
    expectVectorClose(tInv.P(), rw::math::Vector3D<double>(0, 0, 2.0),
                      tightTol(), "§6.5② t_base_world");
    // 逆旋转＝Rᵀ＝R_x(π)（对合）——与冻结矩阵精确一致。
    expectRotationClose(tInv.R(),
                        installationPresetRotation(InstallationPresetToken::Inverted),
                        core::Tolerance::make(0.0, 0.0), "§6.5② R_base_world");
    // 点回代：p_base'＝T_base_world·p_world 与输入逐位一致（§6.5②"✔
    // 与输入逐位一致"——变换后先减平移再转置投影，浮点上对称）。
    const rw::math::Vector3D<double> pBase = transformWorldPointToBase(
        numericExampleTransform(), rw::math::Vector3D<double>(0.3, -0.2, 1.5));
    expectVectorClose(pBase, rw::math::Vector3D<double>(0.3, 0.2, 0.5),
                      tightTol(), "§6.5② p_base 回代");
}

/** RT-BW-1③（§6.5 手算例）：重力投影 g_base＝Rᵀ·g_W——倒挂时
 *  (0,0,−9.81)→(0,0,+9.81)（基座系中重力沿 +Z_base——DYN-01）。 */
TEST(InverseAndPointTest, NumericExampleGravityProjection_RT_BW_1)
{
    const rw::math::Vector3D<double> gBase = gravityToBase(
        numericExampleTransform().R(),
        rw::math::Vector3D<double>(0.0, 0.0, -9.81));  // 世界系重力，单位 m/s²
    expectVectorClose(gBase, rw::math::Vector3D<double>(0.0, 0.0, 9.81),
                      tightTol(), "§6.5③ g_base");
    // 语义断言（禁止清单 2 的对照面）：投影结果方向翻转来自 Rᵀ 数学，
    // 不是"就地取反"——地面安装（R=I）时投影必须恒等（若实现是取反
    // 伪装，本断言失败）。
    const rw::math::Vector3D<double> gGround = gravityToBase(
        makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1),
        rw::math::Vector3D<double>(0.0, 0.0, -9.81));
    expectVectorClose(gGround, rw::math::Vector3D<double>(0.0, 0.0, -9.81),
                      tightTol(), "地面安装重力投影恒等");
}

/** RT-BW-2：四预设 T·inverse(T)==I（容差断言）；Custom 取非恒等 EAA。 */
TEST(InverseAndPointTest, InverseRoundtripAllFourPresets_RT_BW_2)
{
    // 四预设各构造 T（Ground/Inverted/Wall 走正解；Custom 取 3-2-1 欧拉
    // 顺序的等价轴角——任意非恒等旋转代表"自定义"合法域）。
    std::vector<BasePlacementDescription> presets(3);
    presets[0].preset = InstallationPresetToken::Ground;
    presets[0].basePosition = rw::math::Vector3D<double>(0.5, 0.5, 0.0);
    presets[1].preset = InstallationPresetToken::Inverted;
    presets[1].basePosition = rw::math::Vector3D<double>(0.0, 0.0, 2.0);
    presets[2].preset = InstallationPresetToken::Wall;
    presets[2].basePosition = rw::math::Vector3D<double>(1.0, 1.0, 0.5);
    BasePlacementDescription custom;
    custom.preset = InstallationPresetToken::Custom;
    custom.customEaa = providedEaa(0.3, -0.5, 1.2);

    for (const auto* base : {&presets[0], &presets[1], &presets[2], &custom}) {
        const auto resolved = resolveWorldBaseTransform(*base);
        ASSERT_TRUE(resolved.ok()) << "预设(" << static_cast<int>(base->preset)
                                   << ") 正解失败";
        // T·inverse(T)＝I（§6.1 反向定义；容差＝附录 D 第 4 项）。
        const rw::math::Transform3D<double> identity =
            composeWorldBaseTcp(resolved.get(), invertWorldBase(resolved.get()));
        expectTransformClose(identity,
                             rw::math::Transform3D<double>(
                                 rw::math::Vector3D<double>(0, 0, 0),
                                 makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1)),
                             tightTol(), "T·inverse(T)");
    }
}

/** 逆变换对合性：倒挂逆的旋转与冻结矩阵逐位一致（R_x(π)⁻¹=R_x(π)）。 */
TEST(InverseAndPointTest, InverseOfInvertedMatchesFrozenMatrix)
{
    BasePlacementDescription base;
    base.preset = InstallationPresetToken::Inverted;
    const auto t = resolveWorldBaseTransform(base);
    ASSERT_TRUE(t.ok());
    const rw::math::Transform3D<double> tInv = invertWorldBase(t.get());
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            ASSERT_EQ(tInv.R()(i, j),
                      installationPresetRotation(InstallationPresetToken::Inverted)(
                          i, j))
                << "对合元素(" << i << "," << j << ")";
        }
    }
}

/** 防御面：非正交输入调 invertWorldBase→fail-fast（调用方契约违约）。 */
TEST(InverseAndPointTest, InverseRejectsNonOrthogonalInput)
{
    rw::math::Rotation3D<double> r = makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1);
    r(1, 0) = 1e-6;  // 破坏正交性（偏差 1e-6 ≫ 1e-12）
    EXPECT_THROW(invertWorldBase(rw::math::Transform3D<double>(
                     rw::math::Vector3D<double>(0, 0, 0), r)),
                 RuntimeError);
}

// =====================================================================
// 组 7：FK 组合与结合律（RT-BW-1④——§6.5④ 容差）。
// =====================================================================

/** RT-BW-1④：三因子两种结合顺序逐元素一致（T·T·T，容差 1×10⁻⁹）。 */
TEST(ComposeTest, AssociativityTwoGroupings_RT_BW_1)
{
    // 三因子＝T_world_base（倒挂）·T_base_j1（设备链第一级示意）·T_j1_tcp
    // （链内末级＋TCP 偏置示意）——§6.5④ 的结合律断言只需任意三个刚体
    // 变换（结合律是乘法代数性质；消费链的真实因子随 RT-T07 Device FK）。
    const rw::math::Transform3D<double> tWorldBase = numericExampleTransform();
    const rw::math::Transform3D<double> tBaseJ1(
        rw::math::Vector3D<double>(0.1, 0.0, 0.3),
        rotationFromCustomEaa(rw::math::Vector3D<double>(0.0, 0.0, 0.7)));
    const rw::math::Transform3D<double> tJ1Tcp(
        rw::math::Vector3D<double>(0.0, 0.2, 0.5),
        rotationFromCustomEaa(rw::math::Vector3D<double>(0.4, 0.0, 0.0)));

    // 结合顺序 A：(T·T1)·T2；结合顺序 B：T·(T1·T2)——逐元素一致。
    const rw::math::Transform3D<double> groupingA =
        composeWorldBaseTcp(composeWorldBaseTcp(tWorldBase, tBaseJ1), tJ1Tcp);
    const rw::math::Transform3D<double> groupingB =
        composeWorldBaseTcp(tWorldBase, composeWorldBaseTcp(tBaseJ1, tJ1Tcp));
    expectTransformClose(groupingA, groupingB, tightTol(), "结合律两顺序");
}

/** 组合与手算一致：p_world(T_world_tcp.P)＝R_wb·t_bc＋t_wb（逐元素）。 */
TEST(ComposeTest, ComposedTranslationMatchesManualFormula)
{
    const rw::math::Transform3D<double> tWorldBase = numericExampleTransform();
    const rw::math::Transform3D<double> tBaseTcp(
        rw::math::Vector3D<double>(0.3, 0.2, 0.5), makeRotation(1, 0, 0, 0, 1, 0, 0, 0, 1));
    const rw::math::Transform3D<double> tWorldTcp =
        composeWorldBaseTcp(tWorldBase, tBaseTcp);
    // 手算：R_x(π)·(0.3,0.2,0.5)＋(0,0,2)＝(0.3,−0.2,1.5)（§6.5① 同构——
    // 平移链与点变换公式一致是复合正确性的直接证据）。
    expectVectorClose(tWorldTcp.P(),
                      rw::math::Vector3D<double>(0.3, -0.2, 1.5), tightTol(),
                      "T_world_tcp 平移");
    expectRotationClose(tWorldTcp.R(), tWorldBase.R(), tightTol(), "T_world_tcp 旋转");
}

// =====================================================================
// 组 8：S9 一致性检查（acceptance 2／RT-BW-4 规则部分——二次叠加反例）。
// =====================================================================

/** 一致路径：候选＝权威 T→nullopt（唯一写入点产物必须逐位一致）。 */
TEST(BaseMountConsistencyTest, ExactMatchConsistent)
{
    const rw::math::Transform3D<double> t = numericExampleTransform();
    EXPECT_FALSE(checkBaseMountConsistency(t, t).has_value());
}

/** RT-BW-4 规则面（AT-37 反例）：候选＝T·T（下游二次叠加）→检出不一致
 *  且形态标记 doubleAppliedPattern＝true（§6.6"含 ≈T·T 形态"）。 */
TEST(BaseMountConsistencyTest, DoubleApplicationDetected_RT_BW_4)
{
    const rw::math::Transform3D<double> t = numericExampleTransform();
    // 替身"缺陷实现"：把安装变换又乘一次（R²＝I——渲染回正、重力矩符号
    // 翻转的 AT-37 反例形态）。
    const rw::math::Transform3D<double> doubleApplied =
        composeWorldBaseTcp(t, t);
    const auto dev = checkBaseMountConsistency(doubleApplied, t);
    ASSERT_TRUE(dev.has_value()) << "二次叠加必须被检出（§6.6 S9 规则）";
    EXPECT_TRUE(dev->doubleAppliedPattern);
    EXPECT_GT(dev->maxPositionDeviation, 1e-9);
    EXPECT_GT(dev->maxRotationDeviation, 1e-9);
}

/** 非叠加形态的不一致：换用别的旋转（非 T² 形态）→检出且标记＝false。 */
TEST(BaseMountConsistencyTest, WrongRotationNotMarkedDoubleApplied)
{
    const rw::math::Transform3D<double> t = numericExampleTransform();
    const rw::math::Transform3D<double> wrong(
        t.P(), installationPresetRotation(InstallationPresetToken::Wall));
    const auto dev = checkBaseMountConsistency(wrong, t);
    ASSERT_TRUE(dev.has_value());
    EXPECT_FALSE(dev->doubleAppliedPattern)
        << "Wall 矩阵不呈 T² 形态——标记必须为 false";
}

/** 容差内偏差通过：元素级 1×10⁻¹⁰ 扰动（≤1×10⁻⁹）不误判（附录 D 第 4 项）。 */
TEST(BaseMountConsistencyTest, WithinToleranceDeviationPasses)
{
    const rw::math::Transform3D<double> t = numericExampleTransform();
    rw::math::Transform3D<double> candidate = t;
    candidate.P()(2) = t.P()(2) + 1e-10;   // 平移 1×10⁻¹⁰ m（容差内）
    candidate.R()(0, 1) = t.R()(0, 1) + 1e-10;  // 旋转元素 1×10⁻¹⁰（容差内）
    EXPECT_FALSE(checkBaseMountConsistency(candidate, t).has_value());
}

}  // namespace
