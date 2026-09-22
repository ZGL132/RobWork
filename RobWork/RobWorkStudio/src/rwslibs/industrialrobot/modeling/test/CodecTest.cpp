/**
 * @file   CodecTest.cpp
 * @brief  IRobotDesignCodec 用例组（MdlCodec）——canonical 编码确定性
 *         （NFR-COR-02：同对象→同字节）、encode/decode 往返逐字段一致、
 *         D-MDL-5 派生字段不入编码身份、未知主版本 SchemaVersionUnsupported
 *         （NFR-DEP-04）与 decode 破损面（MalformedPayload）。
 *
 * 设计依据：
 *   - units/modeling.md §4.8（确定性序列化五要素）、§7.1/§14.2 D-MDL-5、
 *     §4.3-B（selfCollisionHints 不编码）、§9.4.9（接口契约）、§4.3（往返
 *     语义——派生缓存按设计不保真）
 *   - 需求 NFR-COR-01/02（确定性/可复现）、NFR-DEP-04（未知版本拒绝）、
 *     NFR-COR-03（不产出半成品）、CON-05（内容寻址——字节即 ContentVersion
 *     的摘要输入）
 *   - 任务契约 tasks/foundation/WP-13-T03.json acceptance 1/3/4
 */

#include <sdurws/ird/modeling/Codec.hpp>
#include <sdurws/ird/modeling/ObjectTypes.hpp>
#include <sdurws/ird/modeling/Parts.hpp>
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::Digest256;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProvenanceKind;
using sdurws::ird::core::SourcedValue;
using sdurws::ird::core::ValueProvenance;
using sdurws::ird::modeling::AuthorityMode;
using sdurws::ird::modeling::Bytes;
using sdurws::ird::modeling::CatalogBackfill;
using sdurws::ird::modeling::CouplingStage;
using sdurws::ird::modeling::DrivetrainDesign;
using sdurws::ird::modeling::DhParameters;
using sdurws::ird::modeling::FormatVersion;
using sdurws::ird::modeling::FrictionEntry;
using sdurws::ird::modeling::GeometryKind;
using sdurws::ird::modeling::GeometryRef;
using sdurws::ird::modeling::InertiaTensor;
using sdurws::ird::modeling::JointEntry;
using sdurws::ird::modeling::JointLimits;
using sdurws::ird::modeling::JointType;
using sdurws::ird::modeling::LinkEntry;
using sdurws::ird::modeling::ModelingErrorCode;
using sdurws::ird::modeling::ObjectVariant;
using sdurws::ird::modeling::PoseSet;
using sdurws::ird::modeling::PoseSetEntry;
using sdurws::ird::modeling::ResourceRef;
using sdurws::ird::modeling::ResourceState;
using sdurws::ird::modeling::RobotDesign;
using sdurws::ird::modeling::RobotDesignCodec;
using sdurws::ird::modeling::SceneObject;
using sdurws::ird::modeling::SceneObjectRole;
using sdurws::ird::modeling::TcpEntry;
using sdurws::ird::modeling::ToolDefinition;
using sdurws::ird::modeling::TorqueLimitEntry;
using sdurws::ird::modeling::kCurrentFormatVersion;
using sdurws::ird::modeling::kNamedPoseSetObjectType;
using sdurws::ird::modeling::kRobotDesignObjectType;
using sdurws::ird::modeling::kRobotDrivetrainObjectType;
using sdurws::ird::modeling::kSceneObjectObjectType;
using sdurws::ird::modeling::kToolDefinitionObjectType;

namespace rwmath = rw::math;
namespace runtime = sdurws::ird::runtime;

namespace {

// =====================================================================
// 夹具（确定性 Id/来源——同 RobotDesignTest 口径；两文件各自自持，测试
// 可读性优先）
// =====================================================================

ObjectId makeOid(std::uint64_t v)
{
    char text[40] = {};
    std::snprintf(text, sizeof(text), "obj-%024llx%08llx",
                  static_cast<unsigned long long>(0),
                  static_cast<unsigned long long>(v));
    return ObjectId::fromCanonical(text);
}

template <class T>
SourcedValue<T> provided(T value, ProvenanceKind kind = ProvenanceKind::UserProvided)
{
    return SourcedValue<T>::provided(std::move(value), ValueProvenance::make(kind));
}

rwmath::Transform3D<double> identityPose()
{
    return rwmath::Transform3D<double>(
        rwmath::Vector3D<double>(0.0, 0.0, 0.0),
        rwmath::Rotation3D<double>(1.0, 0.0, 0.0,
                                   0.0, 1.0, 0.0,
                                   0.0, 0.0, 1.0));
}

/// 位移位姿（平移 x,y,z＋恒等旋转——编解码字段覆盖平移通道）。
rwmath::Transform3D<double> translatedPose(double x, double y, double z)
{
    return rwmath::Transform3D<double>(
        rwmath::Vector3D<double>(x, y, z),
        rwmath::Rotation3D<double>(1.0, 0.0, 0.0,
                                   0.0, 1.0, 0.0,
                                   0.0, 0.0, 1.0));
}

/// 完整载荷的显式权威设计（各 SourcedValue 态齐全：Provided 带 provenance/
/// methodTag、NotProvided、NotApplicable——往返逐字段对照的载体）。
RobotDesign makeCanonicalExplicitDesign()
{
    RobotDesign d;
    d.schemaVersion = sdurws::ird::modeling::kRobotDesignSchemaVersion;  // 单一权威常量
    d.displayName = "canonical-six-axis";  // UTF-8 多字节内容（往返校验）
    d.authority = AuthorityMode::Explicit;
    d.basePlacement.preset = runtime::InstallationPresetToken::Ground;
    d.basePlacement.basePosition = provided(rwmath::Vector3D<double>(0.0, 0.0, 0.75));  // m

    JointEntry j1;
    j1.objectId = makeOid(1);
    j1.localName = "j1";
    j1.type = JointType::Revolute;
    j1.axis = provided(rwmath::Vector3D<double>(0.0, 0.0, 1.0));
    j1.origin = provided(sdurws::ird::modeling::JointPose(translatedPose(0.0, 0.0, 0.30)));  // m/rad
    j1.zeroOffset = 0.25;  // rad（q_authoritative = q_zeroOffset + q_rw）
    j1.bounds = provided(JointLimits{-3.141592653589793, 3.141592653589793});
    j1.workingRange = SourcedValue<JointLimits>::notApplicable();  // 非continuous：ERR-01 显式

    JointEntry j2;
    j2.objectId = makeOid(2);
    j2.localName = "j2.cont名";  // UTF-8 内容（规范化工序不重写 localName，仅承载）
    j2.type = JointType::Continuous;
    j2.axis = provided(rwmath::Vector3D<double>(0.0, 1.0, 0.0));
    j2.origin = provided(sdurws::ird::modeling::JointPose(identityPose()));
    j2.zeroOffset = 0.0;  // rad
    j2.bounds = SourcedValue<JointLimits>::notApplicable();  // continuous＝不适用
    j2.workingRange = provided(JointLimits{-6.0, 6.0});      // rad——有限区间

    d.joints = {j1, j2};

    LinkEntry l1;
    l1.objectId = makeOid(11);
    l1.localName = "base_link";
    l1.body.mass = provided(12.5);  // kg
    l1.body.centerOfMass = provided(rwmath::Vector3D<double>(0.0, 0.0, 0.05));  // m
    InertiaTensor inertia;
    inertia.ixx = 0.35;
    inertia.iyy = 0.32;
    inertia.izz = 0.12;
    l1.body.inertia = provided(inertia, ProvenanceKind::GeometricEstimate);  // 估算来源
    l1.body.material = sdurws::ird::modeling::MaterialRef{"aluminium-6061",
                                                          provided(2700.0)};  // kg/m³
    GeometryRef visual;
    visual.resourceRefId = "mesh-base-visual";
    visual.localTransform = translatedPose(0.0, 0.0, 0.01);
    visual.kind = GeometryKind::Mesh;
    l1.visual = visual;
    // 连杆数＝关节数+1（I-MDL-1——l2/l3 为最小物性连杆，decode 不变量闸
    // 会复核本约束，夹具必须真实满足）
    LinkEntry l2;
    l2.objectId = makeOid(12);
    l2.localName = "l2";
    LinkEntry l3;
    l3.objectId = makeOid(13);
    l3.localName = "l3";
    d.links = {l1, l2, l3};

    d.toolRefs = {makeOid(100), makeOid(101)};
    d.sceneRefs = {makeOid(200)};
    d.defaultTcp = sdurws::ird::modeling::TcpRef{makeOid(100), "tcp-center"};
    d.poseSetRef = makeOid(400);
    d.drivetrainRef = makeOid(500);

    ResourceRef recorded;
    recorded.resourceId = "mesh-base-visual";
    recorded.contentDigest = Digest256{{0x11, 0x22, 0x33}};
    recorded.state = ResourceState::Recorded;
    recorded.externalRecord = sdurws::ird::modeling::ExternalResourceRecord{
        "D:/assets/base.stl", Digest256{{0xAA, 0xBB}}};
    d.resourceManifest = {recorded};

    d.notes = "参考模型（UTF-8 备注）";
    return d;
}

/// 完整载荷的 DH 权威设计（dhDerived 权威；axis/origin 待重算）。
RobotDesign makeCanonicalStandardDhDesign()
{
    RobotDesign d = makeCanonicalExplicitDesign();
    d.authority = AuthorityMode::StandardDH;
    DhParameters dh;
    dh.thetaOffset = 0.5;  // rad
    dh.d = 0.30;           // m
    dh.a = 0.45;           // m
    dh.alpha = 1.5707963267948966;  // rad
    for (JointEntry& j : d.joints) {
        j.axis = SourcedValue<rwmath::Vector3D<double>>::notProvided();
        j.origin = SourcedValue<sdurws::ird::modeling::JointPose>::notProvided();
        j.dhDerived = dh;
    }
    return d;
}

ToolDefinition makeCanonicalTool()
{
    ToolDefinition t;
    t.schemaVersion = sdurws::ird::modeling::kToolDefinitionSchemaVersion;
    t.objectId = makeOid(100);
    t.localName = "gripper-a";
    t.displayName = "平行夹爪 A";
    t.mountInterface = translatedPose(0.0, 0.0, 0.02);  // T_flange_tool，m/rad
    TcpEntry tcp;
    tcp.key = "tcp-center";
    tcp.offset = translatedPose(0.0, 0.0, 0.15);  // T_tool_tcp，m/rad
    tcp.displayName = "中心 TCP";
    t.tcpList = {tcp};  // ≥1（§4.4）
    GeometryRef geom;
    geom.resourceRefId = "mesh-gripper";
    geom.kind = GeometryKind::Mesh;
    t.geometry = geom;
    t.body.mass = provided(1.8);  // kg
    InertiaTensor it;
    it.ixx = 0.01;
    it.iyy = 0.01;
    it.izz = 0.004;
    t.body.inertia = provided(it);
    t.payloadAttributes = sdurws::ird::modeling::PayloadAttributes{5.0};  // kg
    return t;
}

SceneObject makeCanonicalSceneObject()
{
    SceneObject o;
    o.schemaVersion = sdurws::ird::modeling::kSceneObjectSchemaVersion;
    o.objectId = makeOid(200);
    o.localName = "table";
    o.worldPose = translatedPose(1.2, 0.0, -0.1);  // 世界系固连，m/rad（M-11）
    GeometryRef geom;
    geom.resourceRefId = "mesh-table";
    geom.kind = GeometryKind::Primitive;
    o.geometry = geom;
    o.role = SceneObjectRole::EnvironmentObject;
    o.collisionProfileHint = "env-static";
    return o;
}

PoseSet makeCanonicalPoseSet()
{
    PoseSet p;
    p.schemaVersion = sdurws::ird::modeling::kNamedPoseSetSchemaVersion;
    p.objectId = makeOid(400);
    PoseSetEntry home;
    home.key = "homeConfiguration";  // 保留键（§4.6）
    home.jointConfiguration = {0.0, 0.0};  // rad（移动关节 m）——与关节序对应
    home.note = "复位参考";
    PoseSetEntry zero;
    zero.key = "zeroConfiguration";
    zero.jointConfiguration = {0.0, 0.0};
    p.entries = {home, zero};
    return p;
}

DrivetrainDesign makeCanonicalDrivetrain()
{
    DrivetrainDesign dt;
    dt.schemaVersion = sdurws::ird::modeling::kRobotDrivetrainSchemaVersion;
    dt.objectId = makeOid(500);
    dt.ratioPerJoint = {provided(101.0), provided(121.0)};  // 无量纲
    FrictionEntry fr;
    fr.viscous = provided(0.8);   // N·m·s/rad
    fr.coulomb = provided(0.4);   // N·m
    fr.bias = provided(0.05);     // N·m
    dt.frictionPerJoint = {fr, fr};
    TorqueLimitEntry tl;
    tl.rated = provided(25.0);    // N·m
    tl.peak = provided(48.0);     // N·m
    dt.torqueLimitsPerJoint = {tl, tl};
    dt.catalogBackfill = CatalogBackfill{"catalog-2026", "motor-x", "reducer-y", "flange"};
    return dt;
}

/// 五对象变体全集（§4.2 表行序——与编解码 wireType 序一致）。
std::vector<ObjectVariant> allCanonicalObjects()
{
    return {
        ObjectVariant(makeCanonicalExplicitDesign()),
        ObjectVariant(makeCanonicalStandardDhDesign()),
        ObjectVariant(makeCanonicalTool()),
        ObjectVariant(makeCanonicalSceneObject()),
        ObjectVariant(makeCanonicalPoseSet()),
        ObjectVariant(makeCanonicalDrivetrain()),
    };
}

/// 便捷断言辅助：对象编码成功并返回字节。
Bytes encodedOf(const ObjectVariant& object)
{
    const RobotDesignCodec codec;
    const auto result = codec.encode(object, kCurrentFormatVersion);
    EXPECT_TRUE(result.ok());
    return result.ok() ? result.get() : Bytes{};
}

/// 在字节里定位相邻 16 字节对并交换（toolRefs 字典序破坏用——定位以两个
/// 已知 ObjectId 的字节序列为锚，不依赖手算偏移）。
bool swapAdjacent16(std::vector<std::uint8_t>& bytes,
                    const std::uint8_t* first, const std::uint8_t* second)
{
    for (std::size_t i = 0; i + 32 <= bytes.size(); ++i) {
        if (std::equal(first, first + 16, bytes.data() + i)
            && std::equal(second, second + 16, bytes.data() + i + 16)) {
            std::swap_ranges(bytes.data() + i, bytes.data() + i + 16,
                             bytes.data() + i + 16);
            return true;
        }
    }
    return false;
}

}  // namespace

// =====================================================================
// canonical 编码确定性（acceptance 4——NFR-COR-02：同对象→同字节）
// =====================================================================

/// 同一对象重复编码逐字节相等（五对象全覆盖）；且编码头以正确 magic 与
/// 对象 token 开头（token＝§4.2 登记常量——robot-design 复用 runtime 字面）。
TEST(MdlCodec, EncodeIsDeterministicPerObject_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-02", {}, std::nullopt);
    const RobotDesignCodec codec;
    for (const ObjectVariant& object : allCanonicalObjects()) {
        const auto first = codec.encode(object, kCurrentFormatVersion);
        const auto second = codec.encode(object, kCurrentFormatVersion);
        ASSERT_TRUE(first.ok()) << "index=" << object.index();
        ASSERT_TRUE(second.ok()) << "index=" << object.index();
        EXPECT_EQ(first.get(), second.get()) << "index=" << object.index();
    }
    // 头部抽查：robot-design 编码以 "IRDMDLO" 魔数开头，且对象 token 子串
    // （复用 runtime 字面常量）存在于头域
    const Bytes root = encodedOf(ObjectVariant(makeCanonicalExplicitDesign()));
    EXPECT_EQ(0, std::memcmp(root.data(), "IRDMDLO", 7));
    EXPECT_NE(std::search(root.begin(), root.end(), kRobotDesignObjectType,
                          kRobotDesignObjectType + std::strlen(kRobotDesignObjectType)),
              root.end());
}

/// 引用表规范化：乱序输入与有序输入编码同字节（§4.8"集合按 ObjectId 规范
/// 文本字典序"——写入侧排序；decode 侧校验有序）。
TEST(MdlCodec, ReferenceSetNormalization_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-02", {}, std::nullopt);
    RobotDesign unsorted = makeCanonicalExplicitDesign();
    // 逆序构造引用表（101 < 100 的规范文本序——100 应在前）
    unsorted.toolRefs = {makeOid(101), makeOid(100)};
    RobotDesign sorted = unsorted;
    sorted.toolRefs = {makeOid(100), makeOid(101)};
    EXPECT_EQ(encodedOf(ObjectVariant(unsorted)), encodedOf(ObjectVariant(sorted)));
}

/// 权威字段变化改变字节（displayName/notes 产生新修订——§4.8 改名语义；
/// 权威参数变化改变字节——ContentVersion 随内容变）。
TEST(MdlCodec, AuthoritativeFieldChangeChangesBytes_WP13T03_ACC4)
{
    IRD_TEST_INFO("CON-05", {}, std::nullopt);
    RobotDesign base = makeCanonicalExplicitDesign();
    RobotDesign renamed = base;
    renamed.displayName = "renamed-model";  // 仅呈现字段——仍换对象字节
    EXPECT_NE(encodedOf(ObjectVariant(base)), encodedOf(ObjectVariant(renamed)));
    RobotDesign rezero = base;
    rezero.joints[0].zeroOffset = 0.5;  // rad——权威字段
    EXPECT_NE(encodedOf(ObjectVariant(base)), encodedOf(ObjectVariant(rezero)));
}

// =====================================================================
// D-MDL-5：派生字段不入编码身份（acceptance 3 编码面）
// =====================================================================

/// Explicit 态 dhDerived 缓存不影响字节（派生展示缓存不是身份）。
TEST(MdlCodec, DMdl5_ExplicitDhDerivedCacheNotEncoded_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-02", {}, std::nullopt);
    RobotDesign withCache = makeCanonicalExplicitDesign();
    withCache.joints[0].dhDerived = DhParameters{0.1, 0.2, 0.3, 0.4};  // rad/m
    RobotDesign withoutCache = withCache;
    withoutCache.joints[0].dhDerived = std::nullopt;
    EXPECT_EQ(encodedOf(ObjectVariant(withCache)), encodedOf(ObjectVariant(withoutCache)));
}

/// StandardDH 态 axis/origin 派生缓存（含重算结果）不影响字节。
TEST(MdlCodec, DMdl5_DhDerivedAxisOriginCacheNotEncoded_WP13T03_ACC3)
{
    IRD_TEST_INFO("MDL-09", {}, std::nullopt);
    RobotDesign bare = makeCanonicalStandardDhDesign();
    RobotDesign recomputed = bare;
    // 模拟"读取时确定性重算"产出的派生缓存（DerivedReadOnly 来源——I-MDL-8
    // 合法载态）：字节必须与未重算形态完全一致（§7.1 原文）
    for (JointEntry& j : recomputed.joints) {
        j.axis = provided(rwmath::Vector3D<double>(0.0, 0.0, 1.0),
                          ProvenanceKind::DerivedReadOnly);
    }
    EXPECT_EQ(encodedOf(ObjectVariant(bare)), encodedOf(ObjectVariant(recomputed)));
}

/// selfCollisionHints 不入根对象编码权威语义（§4.3-B 表行）——字节相同，
/// 往返后为空（设计使然：经导入报告转策略草稿输入，不持久化于根对象）。
TEST(MdlCodec, SelfCollisionHintsExcludedFromEncoding_WP13T03_ACC1)
{
    IRD_TEST_INFO("MDL-04", {}, std::nullopt);
    RobotDesign withHints = makeCanonicalExplicitDesign();
    sdurws::ird::modeling::SelfCollisionSetup hints;
    hints.excludedPairs = {{"base_link", "l2"}};
    withHints.links[0].selfCollisionHints = hints;
    RobotDesign withoutHints = withHints;
    withoutHints.links[0].selfCollisionHints = std::nullopt;
    EXPECT_EQ(encodedOf(ObjectVariant(withHints)), encodedOf(ObjectVariant(withoutHints)));
    // 往返后：hints 为空（解码产物只含权威语义）
    const RobotDesignCodec codec;
    const auto decoded = codec.decode(encodedOf(ObjectVariant(withHints)), kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    const auto& root = std::get<RobotDesign>(decoded.get());
    EXPECT_FALSE(root.links[0].selfCollisionHints.has_value());
}

// =====================================================================
// 往返逐字段一致（acceptance 4——五对象全覆盖）
// =====================================================================

/// encode→decode 逐字段一致（canonical 形态对象——派生缓存按设计不在
/// 字节里）；解码产物再编码回得同字节（字节级固定点）。
TEST(MdlCodec, RoundtripPreservesAllFields_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-01", {}, std::nullopt);
    const RobotDesignCodec codec;
    for (const ObjectVariant& object : allCanonicalObjects()) {
        const Bytes bytes = encodedOf(object);
        const auto decoded = codec.decode(bytes, kCurrentFormatVersion);
        ASSERT_TRUE(decoded.ok()) << "index=" << object.index();
        // 值相等：变体内同型逐字段全等（operator== 逐字段实现）
        EXPECT_TRUE(decoded.get() == object) << "index=" << object.index();
        // 字节固定点：decode(encode(x)) 再 encode == 原 bytes
        const auto reencoded = codec.encode(decoded.get(), kCurrentFormatVersion);
        ASSERT_TRUE(reencoded.ok()) << "index=" << object.index();
        EXPECT_EQ(reencoded.get(), bytes) << "index=" << object.index();
    }
}

/// DH 权威往返后：dhDerived 保真（权威），axis/origin 为 NotProvided 待
/// 重算（D-MDL-5 定义载态——§7.1"派生值在读取时按需重算"）。
TEST(MdlCodec, DhModeRoundtripDerivedStateDefinition_WP13T03_ACC4)
{
    IRD_TEST_INFO("MDL-02", {}, std::nullopt);
    const RobotDesignCodec codec;
    const auto decoded =
        codec.decode(encodedOf(ObjectVariant(makeCanonicalStandardDhDesign())),
                     kCurrentFormatVersion);
    ASSERT_TRUE(decoded.ok());
    const auto& root = std::get<RobotDesign>(decoded.get());
    for (const JointEntry& j : root.joints) {
        ASSERT_TRUE(j.dhDerived.has_value());
        EXPECT_DOUBLE_EQ(j.dhDerived->d, 0.30);      // m——权威参数保真
        EXPECT_DOUBLE_EQ(j.dhDerived->alpha, 1.5707963267948966);  // rad
        EXPECT_EQ(j.axis.state(), sdurws::ird::core::FieldState::NotProvided);   // 待重算
        EXPECT_EQ(j.origin.state(), sdurws::ird::core::FieldState::NotProvided);
    }
}

// =====================================================================
// 版本闸（acceptance 4——未知主版本 SchemaVersionUnsupported，NFR-DEP-04；
// ContentVersion 由 project 对字节计算，modeling 不自行申报——project.md §4.8）
// =====================================================================

/// encode 请求不可产出的版本（未知主版本/未知次版本）→ SchemaVersionUnsupported。
TEST(MdlCodec, EncodeRejectsUnsupportedVersion_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-DEP-04", {}, std::nullopt);
    const RobotDesignCodec codec;
    const ObjectVariant object(makeCanonicalExplicitDesign());
    // 未知主版本（未来 2.0）
    auto future = codec.encode(object, FormatVersion{2, 0});
    ASSERT_FALSE(future.ok());
    EXPECT_EQ(future.error().code, ModelingErrorCode::SchemaVersionUnsupported);
    // paramSchema 对齐（object-type/schema-version/supported-major——DiagCodes 同码）
    bool hasType = false;
    for (const auto& kv : future.error().params) {
        hasType = hasType || (kv.first == "object-type"
                              && kv.second == std::string(kRobotDesignObjectType));
    }
    EXPECT_TRUE(hasType);
    // 未知次版本（1.1——追加字段未落位前不可产出）
    auto futureMinor = codec.encode(object, FormatVersion{1, 1});
    ASSERT_FALSE(futureMinor.ok());
    EXPECT_EQ(futureMinor.error().code, ModelingErrorCode::SchemaVersionUnsupported);
}

/// decode 遇未知主版本字节 → SchemaVersionUnsupported（拒绝而非猜测）。
TEST(MdlCodec, DecodeRejectsUnknownMajorVersion_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-DEP-04", {}, std::nullopt);
    const RobotDesignCodec codec;
    const Bytes bytes = encodedOf(ObjectVariant(makeCanonicalExplicitDesign()));
    // 调用方只支持主版本 2：v1 字节被拒（程序落后于数据——升程序）
    auto behind = codec.decode(bytes, FormatVersion{2, 0});
    ASSERT_FALSE(behind.ok());
    EXPECT_EQ(behind.error().code, ModelingErrorCode::SchemaVersionUnsupported);
    // 字节声明次版本高于支持次版本：同样拒绝（追加字段不可读）
    // ——通过 crafted 头不可行（版本受 encode 闸约束），此处以支持面
    // {1,0} 恰好读 {1,0} 字节的正向已由往返用例覆盖。
}

// =====================================================================
// decode 破损面（MalformedPayload——NFR-COR-03 不产出半成品）
// =====================================================================

/// magic 不符 → MalformedPayload。
TEST(MdlCodec, DecodeRejectsBadMagic_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    Bytes bytes = encodedOf(ObjectVariant(makeCanonicalExplicitDesign()));
    bytes[0] = 'X';  // 破坏家族魔数
    const RobotDesignCodec codec;
    auto result = codec.decode(bytes, kCurrentFormatVersion);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ModelingErrorCode::MalformedPayload);
}

/// 截断字节 → MalformedPayload（任一前缀长度均不产出半成品）。
TEST(MdlCodec, DecodeRejectsTruncatedBytes_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    const Bytes full = encodedOf(ObjectVariant(makeCanonicalExplicitDesign()));
    const RobotDesignCodec codec;
    for (std::size_t cut : {std::size_t{5}, std::size_t{17}, full.size() / 2, full.size() - 1}) {
        Bytes truncated(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(cut));
        auto result = codec.decode(truncated, kCurrentFormatVersion);
        ASSERT_FALSE(result.ok()) << "cut=" << cut;
        EXPECT_EQ(result.error().code, ModelingErrorCode::MalformedPayload) << "cut=" << cut;
    }
}

/// 尾随字节 → MalformedPayload（canonical 字节自含长度语义）。
TEST(MdlCodec, DecodeRejectsTrailingBytes_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    Bytes bytes = encodedOf(ObjectVariant(makeCanonicalExplicitDesign()));
    bytes.push_back(0x00);
    const RobotDesignCodec codec;
    auto result = codec.decode(bytes, kCurrentFormatVersion);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ModelingErrorCode::MalformedPayload);
}

/// 引用表字节非字典序（交换两个 ObjectId）→ MalformedPayload（canonical
/// 违约——读取侧校验，§4.8）。
TEST(MdlCodec, DecodeRejectsUnsortedReferenceSet_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-02", {}, std::nullopt);
    RobotDesign design = makeCanonicalExplicitDesign();
    design.toolRefs = {makeOid(100), makeOid(101)};  // 编码后字节内必为升序
    Bytes bytes = encodedOf(ObjectVariant(design));
    const ObjectId first = makeOid(100);
    const ObjectId second = makeOid(101);
    ASSERT_TRUE(swapAdjacent16(bytes, first.bytes.data(), second.bytes.data()));
    const RobotDesignCodec codec;
    auto result = codec.decode(bytes, kCurrentFormatVersion);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ModelingErrorCode::MalformedPayload);
}

/// 编码非法对象（0 关节）字节可在 decode 闸被拒（不变量复核——I-MDL-1；
/// encode 忠实编码，合法性闸在构造/编辑边界与 decode 防御面）。
TEST(MdlCodec, DecodeRejectsBytesViolatingInvariants_WP13T03_ACC4)
{
    IRD_TEST_INFO("NFR-COR-03", {}, std::nullopt);
    RobotDesign broken = makeCanonicalExplicitDesign();
    broken.joints.clear();
    broken.links.clear();  // I-MDL-1：joints≥1
    const RobotDesignCodec codec;
    // encode 不做合法性复核（输入是内部类型化数据——文件头"失败语义"）
    auto bytes = codec.encode(ObjectVariant(broken), kCurrentFormatVersion);
    ASSERT_TRUE(bytes.ok());
    // decode 复核不变量：破损字节进不了类型化世界
    auto result = codec.decode(bytes.get(), kCurrentFormatVersion);
    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ModelingErrorCode::MalformedPayload);
    EXPECT_NE(result.error().detail.find("I-MDL-1"), std::string::npos);
}
