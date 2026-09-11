/**
 * @file   CanonicalModelTest.cpp
 * @brief  CanonicalModel 契约用例组（RT-T04）——builder 不变量（§4.3 字段表
 *         "合法与非法实例"列逐条）、能力派生（§9.6）、只读索引（§4.6）、
 *         位模式等值（§4.3.6）与 D-01 不持久化（acceptance 4）。
 *
 * 设计依据：
 *   - units/runtime.md §4.3.1～§4.3.5（各字段表反例逐条入用例）、§4.3.6
 *     （等价关系冻结）、§4.4（非有限拒绝）、§4.6（索引）、§9.6（能力派生）、
 *     §5.2 S5（构造不变量的错误归属——InputInvalid/StructureInvalid）
 *   - 需求 ARC-03/CON-05/CON-01/NFR-COR-03；决策 D-01（瞬态不持久化）、
 *     D-12（诊断/能力不入身份）
 *   - 任务契约 tasks/foundation/RT-T04.json（acceptance 3/4 的模型层用例）
 *
 * 替身边界声明（RT-STUB-0 精神）：本文件全部夹具为值构造（CanonicalModel-
 * Fixture.hpp），不伪造框架行为；断言全部针对产品实现（builder/codec）。
 */

#include "CanonicalModelFixture.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include <sdurws/ird/runtime/Codec.hpp>

namespace fs = std::filesystem;
using namespace sdurws::ird::runtime;
using namespace sdurws::ird::runtime::testfixture;

// =====================================================================
// builder 通过面（合法夹具构造成功＋派生/索引的基础形态）。
// =====================================================================

/** minimal 夹具构造成功：身份非零（builder 计算非申报——§4.3.5）、能力位
 *  与内容一致（限速 Provided→限速位 true；无物性→物性位 false）。 */
TEST(CanonicalModelBuilderTest, BuildsMinimalFixtureAndDerivesCapability)
{
    const CanonicalModel m = minimalFixture().build();
    EXPECT_TRUE(m.contentIdentity().isValid())
        << "内容身份必须非零（builder 计算非调用方申报——§4.3.5）";
    EXPECT_EQ(m.header().revisionSeq, 7u);
    EXPECT_EQ(m.chain().joints.size(), 2u);
    EXPECT_EQ(m.chain().links.size(), 3u) << "links == joints+1（§4.3.3）";

    // 能力派生（§9.6）：minimal＝限速齐、物性缺、无工具/场景/摩擦/耦合。
    const RuntimeCapability& cap = m.capabilities();
    EXPECT_TRUE(cap.hasWorkCell) << "恒 true（§9.6——无 WC 即无快照）";
    EXPECT_TRUE(cap.hasBidirectionalNameMap) << "恒 true（映射随快照必建且双射）";
    EXPECT_TRUE(cap.hasJointVelocityLimits) << "两关节 maxVelocity 均 Provided";
    EXPECT_FALSE(cap.hasFullMassInertia) << "minimal 无物性——全齐位 false";
    EXPECT_FALSE(cap.hasDynamicWorkCell) << "DWC Body 物性＝连杆三元组——缺→false";
    EXPECT_FALSE(cap.hasTools);
    EXPECT_FALSE(cap.hasScene);
    EXPECT_FALSE(cap.hasFrictionModel);
    EXPECT_FALSE(cap.hasCouplingMatrix);
    EXPECT_FALSE(cap.hasCollisionGeometry);
    EXPECT_EQ(cap.jointTypesPresent.size(), 2u);
    EXPECT_EQ(cap.jointTypesPresent[0], JointType::Revolute) << "类型按链序（V12-01）";
}

/** rich 夹具构造成功：全部能力位置位（物性/摩擦/工具/场景/碰撞/耦合）。 */
TEST(CanonicalModelBuilderTest, RichFixtureHasAllCapabilityBits)
{
    const CanonicalModel m = richFixture().build();
    const RuntimeCapability& cap = m.capabilities();
    EXPECT_TRUE(cap.hasFullMassInertia) << "全连杆＋工具物性齐备（§9.6）";
    EXPECT_TRUE(cap.hasDynamicWorkCell) << "被消费 Body（连杆）物性齐备";
    EXPECT_TRUE(cap.hasTools);
    EXPECT_TRUE(cap.hasScene);
    EXPECT_TRUE(cap.hasFrictionModel);
    EXPECT_TRUE(cap.hasCollisionGeometry) << "工具 geometry＋场景几何＋连杆 collision";
    EXPECT_EQ(cap.hasCouplingMatrix, richFixture().drivetrain.coupling.has_value())
        << "rich 夹具未设耦合——位与输入一致（耦合派生另有专例）";
}

// =====================================================================
// builder 不变量——身份与来源块（§4.3.1）。
// =====================================================================

/** 空闭包引用清单/重复 objectId → StructureInvalid（CM-0 值层面）。 */
TEST(CanonicalModelBuilderTest, RejectsEmptyAndDuplicateObjectRefs)
{
    Fixture f = minimalFixture();
    f.header.objectRefs.clear();
    expectBuildThrows(f.toBuilder(), RuntimeErrorCode::StructureInvalid,
                      "objectRefs 为空");

    Fixture g = minimalFixture();
    g.header.objectRefs.push_back(g.header.objectRefs.front());  // 重复首条
    expectBuildThrows(g.toBuilder(), RuntimeErrorCode::StructureInvalid,
                      "objectRefs 重复条目");
}

/** 空 id／契约版本 0／全零 builtFrom → InputInvalid（§4.3.1 合法域）。 */
TEST(CanonicalModelBuilderTest, RejectsInvalidHeaderFields)
{
    Fixture f = minimalFixture();
    f.header.revision = core::RevisionId{};  // 全零＝空（保留值）
    expectBuildThrows(f.toBuilder(), RuntimeErrorCode::InputInvalid, "空 revision");

    Fixture g = minimalFixture();
    g.header.descriptionContractVersion = 0;
    expectBuildThrows(g.toBuilder(), RuntimeErrorCode::InputInvalid, "契约版本 0");

    Fixture h = minimalFixture();
    h.header.builtFrom = core::Digest256{};
    expectBuildThrows(h.toBuilder(), RuntimeErrorCode::InputInvalid, "全零 builtFrom");
}

// =====================================================================
// builder 不变量——世界与基座块（§4.3.2；RT-BW-6 校验器面的构造侧）。
// =====================================================================

/** 非正交旋转（缩放 2I）／反射（det=−1）／非有限平移 → InputInvalid。 */
TEST(CanonicalModelBuilderTest, RejectsIllegalTWorldBase)
{
    Fixture scaled = minimalFixture();
    scaled.world.T_world_base = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(2, 0, 0, 0, 2, 0, 0, 0, 2));
    expectBuildThrows(scaled.toBuilder(), RuntimeErrorCode::InputInvalid, "非正交 R");

    Fixture reflect = minimalFixture();
    reflect.world.T_world_base = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(0.0, 0.0, 0.0),
        rw::math::Rotation3D<double>(-1, 0, 0, 0, 1, 0, 0, 0, 1));
    expectBuildThrows(reflect.toBuilder(), RuntimeErrorCode::InputInvalid, "反射 R");

    Fixture nanT = minimalFixture();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    nanT.world.T_world_base = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(nan, 0.0, 0.0),
        rw::math::Rotation3D<double>(1, 0, 0, 0, 1, 0, 0, 0, 1));
    expectBuildThrows(nanT.toBuilder(), RuntimeErrorCode::InputInvalid, "NaN 平移");
}

/** 全零重力／非有限重力 → InputInvalid（§4.3.2"非法：全零/非有限"）。 */
TEST(CanonicalModelBuilderTest, RejectsIllegalGravity)
{
    Fixture zero = minimalFixture();
    zero.world.gravityWorld = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    expectBuildThrows(zero.toBuilder(), RuntimeErrorCode::InputInvalid, "全零重力");

    Fixture inf = minimalFixture();
    inf.world.gravityWorld = rw::math::Vector3D<double>(
        std::numeric_limits<double>::infinity(), 0.0, 0.0);
    expectBuildThrows(inf.toBuilder(), RuntimeErrorCode::InputInvalid, "非有限重力");
}

/** preset=Custom 而 R≈I → InputInvalid（§4.3.2 一致性校验——S5 构造侧）。 */
TEST(CanonicalModelBuilderTest, RejectsCustomPresetWithIdentityRotation)
{
    Fixture f = minimalFixture();
    f.world.installPreset =
        core::SourcedValue<InstallationPresetToken>::provided(InstallationPresetToken::Custom,
                                                              userProv());
    expectBuildThrows(f.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "Custom 而 R=I（一致性失败）");
}

// =====================================================================
// builder 不变量——链结构、数值与物性（§4.3.3）。
// =====================================================================

/** 连杆数量失配 → StructureInvalid；轴零向量 → InputInvalid；轴向被规格化。 */
TEST(CanonicalModelBuilderTest, RejectsBadChainAndNormalizesAxis)
{
    Fixture f = minimalFixture();
    f.chain.links.pop_back();
    expectBuildThrows(f.toBuilder(), RuntimeErrorCode::StructureInvalid, "数量失配");

    Fixture zeroAxis = minimalFixture();
    zeroAxis.chain.joints.at(1).axis = rw::math::Vector3D<double>(0.0, 0.0, 0.0);
    expectBuildThrows(zeroAxis.toBuilder(), RuntimeErrorCode::InputInvalid, "零轴");

    // 轴向规格化：非单位轴 (0,3,0) 入库后＝(0,1,0)（§4.3.3"编译器规格化"），
    // 且规格化后模型仍稳定可重建（RT-ID-1 的构造侧前提）。
    Fixture unnormalized = minimalFixture();
    unnormalized.chain.joints.at(1).axis = rw::math::Vector3D<double>(0.0, 3.0, 0.0);
    const CanonicalModel m = unnormalized.build();
    EXPECT_DOUBLE_EQ(m.chain().joints.at(1).axis(1), 1.0);
    const CanonicalModel again = unnormalized.build();
    EXPECT_EQ(m, again) << "规格化确定性——同输入两次构造逐字段等值";
}

/** 限位规则：Revolute 必填／qmin≥qmax 非法／Continuous 必无（§4.3.3）。 */
TEST(CanonicalModelBuilderTest, RejectsIllegalBounds)
{
    Fixture missing = minimalFixture();
    missing.chain.joints.at(0).bounds.reset();
    expectBuildThrows(missing.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "旋转关节缺限位");

    Fixture inverted = minimalFixture();
    inverted.chain.joints.at(0).bounds = JointBounds{2.97, -2.97};
    expectBuildThrows(inverted.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "qmin≥qmax");

    Fixture continuous = minimalFixture();
    continuous.chain.joints.at(0).type = JointType::Continuous;
    continuous.chain.joints.at(0).bounds.reset();
    continuous.chain.joints.at(0).workingRange = WorkingRange{-3.14, 3.14};
    continuous.chain.joints.at(1).type = JointType::Continuous;
    continuous.chain.joints.at(1).bounds.reset();
    continuous.chain.joints.at(1).workingRange = WorkingRange{-3.14, 3.14};
    // 连续化后限速仍 Provided——合法（Continuous 允许限速）；工作范围替代限位。
    const CanonicalModel m = continuous.build();
    EXPECT_EQ(m.capabilities().jointTypesPresent.at(0), JointType::Continuous)
        << "类型保留（V12-01）——continuous 经 workingRange 承载（MDL-12）";

    Fixture continuousWithBounds = continuous;
    continuousWithBounds.chain.joints.at(0).bounds = JointBounds{-1.0, 1.0};
    expectBuildThrows(continuousWithBounds.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "continuous 携带限位");

    Fixture revoluteWithRange = minimalFixture();
    revoluteWithRange.chain.joints.at(0).workingRange = WorkingRange{-3.0, 3.0};
    expectBuildThrows(revoluteWithRange.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "非 continuous 携带工作范围");
}

/** Provided 非法值：负限速／零质量／非对称惯量／非正定惯量 → InputInvalid。 */
TEST(CanonicalModelBuilderTest, RejectsIllegalProvidedValues)
{
    Fixture negVel = minimalFixture();
    negVel.chain.joints.at(0).maxVelocity = val(-0.5);
    expectBuildThrows(negVel.toBuilder(), RuntimeErrorCode::InputInvalid, "负限速");

    Fixture zeroMass = richFixture();
    zeroMass.chain.links.at(1).mass = val(0.0);
    expectBuildThrows(zeroMass.toBuilder(), RuntimeErrorCode::InputInvalid, "零质量");

    Fixture asym = richFixture();
    asym.chain.links.at(1).inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::
        provided(rw::math::InertiaMatrix<double>(0.01, 0.5, 0, 0, 0.02, 0, 0, 0, 0.03),
                 userProv());
    expectBuildThrows(asym.toBuilder(), RuntimeErrorCode::InputInvalid, "非对称惯量");

    Fixture nonSpd = richFixture();
    nonSpd.chain.links.at(1).inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::
        provided(rw::math::InertiaMatrix<double>(-0.01, 0, 0, 0, 0.02, 0, 0, 0, 0.03),
                 userProv());
    expectBuildThrows(nonSpd.toBuilder(), RuntimeErrorCode::InputInvalid, "非正定惯量");
}

/** 全模型 ObjectId 唯一性／引用∈objectRefs（CM-0）→ StructureInvalid。 */
TEST(CanonicalModelBuilderTest, RejectsDuplicateAndForeignObjectIds)
{
    Fixture dup = minimalFixture();
    dup.chain.joints.at(1).objectId = dup.chain.links.at(1).objectId;  // 关节撞连杆
    expectBuildThrows(dup.toBuilder(), RuntimeErrorCode::StructureInvalid, "重复 id");

    Fixture foreign = minimalFixture();
    foreign.chain.joints.at(1).objectId = idFrom<core::ObjectId>("not-in-closure");
    expectBuildThrows(foreign.toBuilder(), RuntimeErrorCode::StructureInvalid,
                      "引用不在闭包清单");
}

/** 局部名空/含 '/' → InputInvalid（§4.3.3；消歧归 S8 不在此拒绝）。 */
TEST(CanonicalModelBuilderTest, RejectsIllegalLocalNames)
{
    Fixture empty = minimalFixture();
    empty.chain.joints.at(0).localName.clear();
    expectBuildThrows(empty.toBuilder(), RuntimeErrorCode::InputInvalid, "空名");

    Fixture slash = minimalFixture();
    slash.chain.joints.at(0).localName = "joint/1";
    expectBuildThrows(slash.toBuilder(), RuntimeErrorCode::InputInvalid, "含 '/'");

    // 同名关节不拒绝——消歧＋警告归 S8/RT-T05（RT-NM-2 口径）。
    Fixture sameName = minimalFixture();
    sameName.chain.joints.at(1).localName = sameName.chain.joints.at(0).localName;
    EXPECT_NO_THROW(sameName.build()) << "同名不阻断——S8 消歧（§4.3.3\"与兄弟同名且"
                                         "不可消歧\"才是非法）";
}

// =====================================================================
// builder 不变量——资源引用与默认 TCP（§4.3.3/§4.3.4/§4.3.5）。
// =====================================================================

/** 引用清单外的资源（digest 失配）→ StructureInvalid；全零摘要 → InputInvalid。 */
TEST(CanonicalModelBuilderTest, RejectsResourceReferenceViolations)
{
    Fixture foreignRef = richFixture();
    ResourceRef wrong = foreignRef.manifest.at(0);
    wrong.contentDigest = digestOf("some-other-bytes");  // 同 id 不同内容＝清单外
    foreignRef.chain.links.at(1).visual = wrong;
    expectBuildThrows(foreignRef.toBuilder(), RuntimeErrorCode::StructureInvalid,
                      "引用不在清单");

    Fixture zeroDigest = minimalFixture();
    ResourceRef bad = zeroDigest.manifest.at(0);
    bad.contentDigest = core::Digest256{};
    zeroDigest.manifest.clear();
    zeroDigest.manifest.push_back(bad);
    expectBuildThrows(zeroDigest.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "全零资源摘要");
}

/** 默认 TCP 规则：有工具必填且不越界；无工具不得设置（§4.3.4）。 */
TEST(CanonicalModelBuilderTest, RejectsDefaultTcpViolations)
{
    Fixture missingTcp = richFixture();
    missingTcp.defaultTcp.reset();
    expectBuildThrows(missingTcp.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "有工具缺默认 TCP");

    Fixture outOfRange = richFixture();
    outOfRange.defaultTcp = 5;
    expectBuildThrows(outOfRange.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "默认 TCP 越界");

    Fixture toolless = minimalFixture();
    toolless.defaultTcp = 0;
    expectBuildThrows(toolless.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "无工具却设默认 TCP");
}

/** error 级诊断码拒绝（§4.3.5"仅警告级"）——警告码放行。 */
TEST(CanonicalModelBuilderTest, RejectsErrorLevelDiagnostics)
{
    Fixture err = minimalFixture();
    err.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{registryCode(RuntimeErrorCode::InputInvalid)}, std::nullopt, std::string{},
        std::string{}, std::string{"上下文"}, std::string{"原因"}, std::string{"建议"}));
    expectBuildThrows(err.toBuilder(), RuntimeErrorCode::InputInvalid,
                      "error 级诊断入模");

    // 取消码（非错误路径——UX-03/D-11）与未知码不拒绝（码值权威归 diagnostics）。
    Fixture cancelled = minimalFixture();
    cancelled.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{registryCode(RuntimeErrorCode::Cancelled)}, std::nullopt, std::string{},
        std::string{}, std::string{"取消（非错误）"}, std::string{"用户取消"}, std::string{"无"}));
    EXPECT_NO_THROW(cancelled.build()) << "Cancelled 非错误路径——不在拒绝集";
}

// =====================================================================
// 能力派生细则（§9.6——构造一致性由派生保证，D-12）。
// =====================================================================

/** 缺一项物性→物性位回落；缺工具惯量仅回落 hasFullMassInertia（规则细则）。 */
TEST(CanonicalModelCapabilityTest, BitsFollowContentDerivation)
{
    Fixture missingLinkMass = richFixture();
    missingLinkMass.chain.links.at(1).mass = core::SourcedValue<double>::notProvided();
    const CanonicalModel m1 = missingLinkMass.build();
    EXPECT_FALSE(m1.capabilities().hasFullMassInertia);
    EXPECT_FALSE(m1.capabilities().hasDynamicWorkCell) << "连杆＝被消费 Body——缺→DWC 位回落";

    Fixture missingToolInertia = richFixture();
    missingToolInertia.tools.at(0).inertia = core::SourcedValue<rw::math::InertiaMatrix<double>>::
        notProvided();
    const CanonicalModel m2 = missingToolInertia.build();
    EXPECT_FALSE(m2.capabilities().hasFullMassInertia)
        << "hasFullMassInertia 要求工具物性也齐备";
    EXPECT_TRUE(m2.capabilities().hasDynamicWorkCell)
        << "DWC Body＝连杆——工具缺项不回落 DWC 位（§9.6 派生细则）";

    Fixture withCoupling = richFixture();
    CouplingMatrix c;
    c.rows = 2;
    c.cols = 2;
    c.c = {1.0, 0.0, 0.0, 1.0};  // 行主序单位阵（结构自洽；良态由 S3 判定）
    c.jointRange.firstIndex = 0;
    c.jointRange.count = 2;
    c.conditionNumber = 1.0;
    withCoupling.drivetrain.coupling = c;
    const CanonicalModel m3 = withCoupling.build();
    EXPECT_TRUE(m3.capabilities().hasCouplingMatrix);
}

// =====================================================================
// §4.6 只读索引（对象/资源——builder 构建，O(log n)）。
// =====================================================================

/** findObject 覆盖六类对象；findResource 命中清单条目；未命中返回 nullopt。 */
TEST(CanonicalModelIndexTest, ObjectAndResourceIndicesResolve)
{
    const CanonicalModel m = richFixture().build();

    const auto robot = m.findObject(idFrom<core::ObjectId>("robot"));
    ASSERT_TRUE(robot.has_value());
    EXPECT_EQ(robot->kind, CanonicalModel::ObjectKind::Robot);
    EXPECT_EQ(robot->index, 0u);

    const auto joint = m.findObject(idFrom<core::ObjectId>("j2"));
    ASSERT_TRUE(joint.has_value());
    EXPECT_EQ(joint->kind, CanonicalModel::ObjectKind::Joint);
    EXPECT_EQ(joint->index, 1u);

    const auto scene = m.findObject(idFrom<core::ObjectId>("s1"));
    ASSERT_TRUE(scene.has_value());
    EXPECT_EQ(scene->kind, CanonicalModel::ObjectKind::SceneObject);

    const auto res = m.findObject(idFrom<core::ObjectId>("res-2"));
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(res->kind, CanonicalModel::ObjectKind::Resource);

    EXPECT_FALSE(m.findObject(idFrom<core::ObjectId>("missing")).has_value());

    const auto ref = m.findResource(idFrom<core::ObjectId>("res-1"));
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->contentDigest, digestOf("mesh-1-bytes"));
    EXPECT_FALSE(m.findResource(idFrom<core::ObjectId>("res-9")).has_value());
}

// =====================================================================
// 等值语义（§4.3.6——位模式级；RT-ID-2 的模型层基础）。
// =====================================================================

/** 同夹具两次构造逐字段等值；±0.0 与 1e-15 差异不等（位模式，非容差）。 */
TEST(CanonicalModelEqualityTest, BitPatternEqualityNotTolerance)
{
    const CanonicalModel a = minimalFixture().build();
    const CanonicalModel b = minimalFixture().build();
    EXPECT_EQ(a, b) << "同输入确定性构造——逐字段等值";

    Fixture plusZero = minimalFixture();
    plusZero.chain.joints.at(0).zeroOffset = 0.0;
    const CanonicalModel pz = plusZero.build();

    Fixture minusZero = minimalFixture();
    minusZero.chain.joints.at(0).zeroOffset = -0.0;
    const CanonicalModel mz = minusZero.build();
    EXPECT_NE(pz, mz) << "+0.0 与 −0.0 位模式不同——等值不等（§4.3.6/RT-ID-2）";

    Fixture eps = minimalFixture();
    eps.chain.joints.at(0).zeroOffset = 0.0 + 1e-15;
    const CanonicalModel e = eps.build();
    EXPECT_NE(pz, e) << "1×10⁻¹⁵ 差异不等——严禁容差等值（RT-ID-2）";
}

// =====================================================================
// D-01：CanonicalModel 瞬态、不持久化（acceptance 4）。
// =====================================================================

namespace transientprobe {

/// 成员存在性探测（C++17 detection idiom——持久化成员面缺失的编译期证据）。
template <typename T, typename = void>
struct HasMemberSave : std::false_type {};
template <typename T>
struct HasMemberSave<T, std::void_t<decltype(&T::save)>> : std::true_type {};

template <typename T, typename = void>
struct HasMemberLoad : std::false_type {};
template <typename T>
struct HasMemberLoad<T, std::void_t<decltype(&T::load)>> : std::true_type {};

template <typename T, typename = void>
struct HasMemberSerialize : std::false_type {};
template <typename T>
struct HasMemberSerialize<T, std::void_t<decltype(&T::serialize)>> : std::true_type {};

template <typename T, typename = void>
struct HasMemberToFile : std::false_type {};
template <typename T>
struct HasMemberToFile<T, std::void_t<decltype(&T::toFile)>> : std::true_type {};

template <typename T, typename = void>
struct HasMemberFromDisk : std::false_type {};
template <typename T>
struct HasMemberFromDisk<T, std::void_t<decltype(&T::fromFile)>> : std::true_type {};

}  // namespace transientprobe

/** ①类型层：CanonicalModel 无 save/load/serialize/toFile/fromFile 成员——
 *  持久化入口在类型层不存在（D-01；唯一序列化面＝rtcodec 瞬态字节）。 */
TEST(CanonicalModelTransientTest, NoPersistenceMemberSurface)
{
    static_assert(!transientprobe::HasMemberSave<CanonicalModel>::value, "save 成员不得存在");
    static_assert(!transientprobe::HasMemberLoad<CanonicalModel>::value, "load 成员不得存在");
    static_assert(!transientprobe::HasMemberSerialize<CanonicalModel>::value, "serialize 成员不得存在");
    static_assert(!transientprobe::HasMemberToFile<CanonicalModel>::value, "toFile 成员不得存在");
    static_assert(!transientprobe::HasMemberFromDisk<CanonicalModel>::value, "fromFile 成员不得存在");
    static_assert(std::is_copy_constructible<CanonicalModel>::value
                      && std::is_move_constructible<CanonicalModel>::value,
                  "值语义（§4.3 结构级约定——拷贝/移动，随快照存活）");
    SUCCEED() << "编译期断言全部成立（D-01 类型层证据）";
}

/** ②API 面：codec 只进出内存字节——encode 返回 vector、parse 消费 vector，
 *  无任何路径/流/文件参数（worker 物化通道＝内存字节，§9.5）。 */
TEST(CanonicalModelTransientTest, CodecApiIsInMemoryOnly)
{
    static_assert(std::is_same<std::vector<std::uint8_t>,
                               decltype(rtcodec::encode(std::declval<const CanonicalModel&>()))>::value,
                  "encode 只产出内存字节");
    static_assert(
        std::is_same<Expected<CanonicalModel, RuntimeError>,
                     decltype(rtcodec::parse(std::declval<const std::vector<std::uint8_t>&>()))>::value,
        "parse 只消费内存字节（查询轨）");
    SUCCEED() << "签名级断言成立（无文件系统参数面）";
}

/** ③源码面：CanonicalModel/Codec 四文件零持久化 include（无 fstream/
 *  filesystem/iostream；无 project/io/modeling 单元头——零持久化通道）。 */
TEST(CanonicalModelTransientTest, SourceScanNoPersistenceIncludes)
{
    // IRD_RUNTIME_UNIT_ROOT＝单元父目录（industrialrobot/——CMake 注入口径，
    // BuildRedLineTest 同源），故补 "runtime" 段。
    const fs::path includeDir = fs::path{IRD_RUNTIME_UNIT_ROOT} / "runtime" / "include"
                                / "sdurws" / "ird" / "runtime";
    const fs::path srcDir = fs::path{IRD_RUNTIME_UNIT_ROOT} / "runtime" / "src";
    const std::vector<fs::path> files{
        includeDir / "CanonicalModel.hpp", includeDir / "Codec.hpp",
        srcDir / "CanonicalModel.cpp",     srcDir / "Codec.cpp",
    };
    // 禁止面：文件 I/O 头（D-01——瞬态模型零磁盘读写）＋持久化归属单元的头
    // （project/io——PA-1 权威唯一，模型不入项目存储）。
    const std::vector<std::string> forbidden{"<fstream>", "<filesystem>", "<iostream>",
                                             "<cstdio>", "ird/project/",   "ird/io/",
                                             "ird/modeling/"};
    for (const fs::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "无法读取（测试自检失败）: " << file.string();
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        for (const std::string& needle : forbidden) {
            EXPECT_EQ(text.find(needle), std::string::npos)
                << file.filename().string() << " 出现持久化痕迹：" << needle;
        }
    }
}
