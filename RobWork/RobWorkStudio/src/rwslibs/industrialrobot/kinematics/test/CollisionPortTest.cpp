/**
 * @file   CollisionPortTest.cpp
 * @brief  ④端口碰撞接入的真实路径用例组（KinCollisionPort，WP-15-T07）
 *         ——请求方场景装配（acceptance 1）、会话接线三态（acceptance 2/5）、
 *         真实后端构型判定（V-08/KIN-05）、无二次缓存运行断言（V-25，
 *         acceptance 3）与设施异常三态映射（acceptance 2）。
 *
 * 设计依据：
 *   - units/kinematics.md §8.1（与 policy 的交接——装配者语义/边界图 C8/
 *     取消失败行/策略版本行）、§9.6（碰撞两码的素材面）
 *   - policy 冻结契约（POL-T06/POL-T07）：CollisionScene 装配契约（CR-04）、
 *     CollisionEvaluationSession::evaluate（§6.3 状态机/§6.6 时序）、
 *     RobWorkCollisionEvaluator（§6.5 唯一构造入口）
 *   - 任务契约 tasks/foundation/WP-15-T07.json acceptance 1/2/3/5
 *
 * 测试设施与替身边界（对齐 policy/CollisionEvaluationTest 的 POL-TD-1
 * 口径——随套件留痕）：
 *   - 真实后端解析算例：makeRobWorkCollisionEvaluator（内置
 *     ProximityStrategyRW）对程序化构造的 WorkCell（1 自由度
 *     RevoluteJoint 串链＋两个 0.2 m 立方体碰撞几何；q=0 两盒重合
 *     〔相交〕、q=π 分离〔间隙 0.8 m〕——解析已知答案）验证装配→会话→
 *     判定的完整链路；
 *   - 受控替身：CountingCollisionBackend（doInCollision 计数＋可注入
 *     rw::common::Exception——无二次缓存计数与设施异常注入；替身应答
 *     不构成碰撞算法正确性证明）；
 *   - 本套件为**集成模式专属**（消费 rw 非模板类 WorkCell/SerialDevice/
 *     CollisionStrategy——冒烟模式不编译本文件，CMake gating 与产品 TU
 *     src/Collision.cpp 同因）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/kinematics/Collision.hpp>
#include <sdurws/ird/kinematics/KinTypes.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/runtime/CanonicalModel.hpp>
#include <sdurws/ird/runtime/Resource.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/common/macros.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/ProximityModel.hpp>
#include <rw/proximity/ProximityStrategyData.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core = sdurws::ird::core;
namespace policy = sdurws::ird::policy;
namespace rt = sdurws::ird::runtime;
namespace kin = sdurws::ird::kinematics;

namespace {

// =====================================================================
// 解析算例装置常量（1 DOF 串链＋双 0.2 m 立方体——解析已知答案）
// =====================================================================

/// 立方体全边长（SI m）。
inline constexpr double kBoxSize = 0.2;
/// J1→Tool 固定偏移（SI m，X 向）。
inline constexpr double kArmLength = 0.5;
/// 分离构型（rad，≈π——工具盒旋至 (-0.5,0,0)，与环境盒间隙 0.8 m）。
inline constexpr double kClearQ = 3.141592653589793;
/// 装置设备自由度（1——查询构型维度核对锚）。
inline constexpr std::size_t kDeviceDof = 1;

/// 手工构造的非全零对象身份（种子摘要前 16 字节——字节字典序可控）。
template <typename Id>
Id seededObjectId(const std::string& seed)
{
    core::ContentDigester d;
    d.update(seed.data(), seed.size());
    const core::Digest256 digest = d.finalize();
    Id id;
    std::copy(digest.begin(), digest.begin()
                + static_cast<std::ptrdiff_t>(id.bytes.size()),
              id.bytes.begin());
    return id;
}

/// 手工构造的非全零内容身份（场景/名称映射身份——CR-04 值传递契约）。
core::ContentIdentity taggedContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

// =====================================================================
// 真实装置（程序化 WorkCell＋对应规范模型＋名称映射——三面一致）
// =====================================================================

/// 装置对象身份集。
struct RigIds {
    core::ObjectId device = seededObjectId<core::ObjectId>("kin-port-robot");  ///< 主链设备
    core::ObjectId link0 = seededObjectId<core::ObjectId>("kin-port-l0");      ///< 基座连杆
    core::ObjectId link1 = seededObjectId<core::ObjectId>("kin-port-l1");      ///< 末连杆（带盒几何）
    core::ObjectId tool = seededObjectId<core::ObjectId>("kin-port-tool");     ///< 工具
    core::ObjectId env = seededObjectId<core::ObjectId>("kin-port-env");       ///< 环境盒
};

/// 名称映射替身（IPolicyNameContext——⑥端口转发语义，CR-04 适配形态）。
class TestNameContext final : public policy::IPolicyNameContext {
public:
    std::map<std::string, core::ObjectId> byName;
    std::map<core::ObjectId, std::string> byId;
    core::ContentIdentity mapIdentity;

    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        const auto it = byName.find(runtimeName);
        return it == byName.end() ? std::nullopt : std::optional<core::ObjectId>(it->second);
    }
    std::optional<std::string> tryRuntimeName(core::ObjectId object) const override
    {
        const auto it = byId.find(object);
        return it == byId.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    core::ContentIdentity nameMapContentIdentity() const override { return mapIdentity; }
};

/// 发布闭包应答替身（IPolicyValidationContext——解析⑤对象存在性/角色）。
class TestValidationContext final : public policy::IPolicyValidationContext {
public:
    std::map<core::ObjectId, bool> existingObjects;
    std::map<core::ObjectId, std::string> roles;

    bool objectExists(core::ObjectId object) const override
    {
        const auto it = existingObjects.find(object);
        return it != existingObjects.end() && it->second;
    }
    std::optional<std::string> objectRole(core::ObjectId object) const override
    {
        const auto it = roles.find(object);
        return it == roles.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool groupDefined(std::string_view) const override { return false; }
};

/// 快照事实端口替身（IKinCollisionSceneSource——O-37 宿主注入形态的测试
/// 承载；WC 共享只读别名＝直接共享装置 WorkCell 所有权）。
class TestSceneSource final : public kin::IKinCollisionSceneSource {
public:
    std::shared_ptr<const rw::models::WorkCell> workcell;
    core::ContentIdentity identity;
    TestNameContext names;

    std::shared_ptr<const rw::models::WorkCell> sharedWorkCell() const override
    {
        return workcell;
    }
    core::ContentIdentity workCellCompileIdentity() const override { return identity; }
    std::optional<core::ObjectId> tryObjectId(const std::string& runtimeName) const override
    {
        return names.tryObjectId(runtimeName);
    }
    std::optional<std::string> tryRuntimeName(const core::ObjectId& object) const override
    {
        return names.tryRuntimeName(object);
    }
    core::ContentIdentity nameMapIdentity() const override
    {
        return names.nameMapContentIdentity();
    }
};

/// 装置规范模型的只读视图（IKinRuntimeView 最小投影——装配入参）。
class RigView final : public kin::IKinRuntimeView {
public:
    explicit RigView(const rt::CanonicalModel& model) : m_model(model) {}
    const rt::CanonicalModel& model() const override { return m_model; }
    rw::math::Transform3D<double> worldToBase() const override
    {
        return rw::math::Transform3D<double>::identity();  // 地面安装（恒等）
    }

private:
    const rt::CanonicalModel& m_model;
};

struct CollisionRig {
    RigIds ids;
    std::shared_ptr<rw::models::WorkCell> workcell;
    std::optional<rt::CanonicalModel> model;  ///< 构建后填充（builder 产物——无默认构造）
    TestSceneSource source;
    TestValidationContext validation;
};

/**
 * @brief 构造解析算例装置（真实 WorkCell＋SerialDevice＋RigidObject 几何
 *        ＋对应 CanonicalModel——运动学：Base→J1（Z 轴）→Tool（偏移
 *        kArmLength）；环境盒挂 World 下 "EnvBox1"（与 Tool 同位）。
 *        解析答案：q=0 → 工具盒与环境盒重合（相交）；q=kClearQ → 中心距
 *        1.0 m、盒间隙 0.8 m（清晰）。规范模型侧：link1 声明碰撞几何
 *        引用（与 WC 侧 Tool 帧的 RigidObject 对应——会话评估半区按
 *        "声明有几何"注册后端）。
 */
CollisionRig makeRig()
{
    CollisionRig rig;
    const RigIds ids;
    rig.ids = ids;

    // ---- 真实 WorkCell（帧树＋设备＋几何 Object）----
    rig.workcell = std::make_shared<rw::models::WorkCell>("KinPortRigWC");
    rw::core::Ptr<rw::kinematics::FixedFrame> base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Base", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Tool", rw::math::Transform3D<>(
                                                  rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j1);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox1",
            rw::math::Transform3D<>(rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(envFrame);
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));

    // 碰撞几何（RigidObject 承载——编译产物以 Object 挂接几何的形态）。
    const auto attachBox = [&](const rw::kinematics::Frame::Ptr& frame) {
        rw::core::Ptr<rw::models::RigidObject> object =
            rw::core::ownedPtr(new rw::models::RigidObject(frame));
        object->addGeometry(rw::core::ownedPtr(new rw::geometry::Geometry(
            rw::core::ownedPtr(new rw::geometry::Box(kBoxSize, kBoxSize, kBoxSize)))));
        rig.workcell->add(object);
    };
    attachBox(tool);
    attachBox(envFrame);

    // ---- 规范模型（与 WorkCell 三面一致——对象身份/名称/几何事实）----
    rt::CanonicalModelHeader header;
    header.project = seededObjectId<core::ProjectId>("kin-port-prj");
    header.branch = seededObjectId<core::BranchId>("kin-port-brn");
    header.revision = seededObjectId<core::RevisionId>("kin-port-rev");
    header.revisionSeq = 1;
    header.descriptionContractVersion = 1;
    header.compilerContractVersion = 1;
    header.builtFrom = [&] {
        core::Digest256 digest;
        core::ContentDigester d;
        const std::string seed = "kin-port-description";
        d.update(seed.data(), seed.size());
        digest = d.finalize();
        return digest;
    }();

    const auto objectRefOf = [](const core::ObjectId& id, const char* seed,
                                const char* token) {
        rt::ObjectRefEntry e;
        e.objectId = id;
        core::ContentVersion v;
        core::ContentDigester d;
        const std::string s = std::string{seed};
        d.update(s.data(), s.size());
        v.bytes = d.finalize();
        e.contentVersion = v;
        e.objectTypeToken = token;
        core::Digest256 digest;
        core::ContentDigester d2;
        const std::string s2 = std::string{seed} + "-bytes";
        d2.update(s2.data(), s2.size());
        digest = d2.finalize();
        e.digest = digest;
        return e;
    };
    header.objectRefs = {
        objectRefOf(ids.device, "kin-port-robot", "robot-design"),
        objectRefOf(seededObjectId<core::ObjectId>("kin-port-j1"), "kin-port-j1",
                    "joint"),
        objectRefOf(ids.link0, "kin-port-l0", "link"),
        objectRefOf(ids.link1, "kin-port-l1", "link"),
        objectRefOf(ids.tool, "kin-port-tool", "tool"),
        objectRefOf(ids.env, "kin-port-env", "scene-object"),
    };

    // 资源清单（link1 碰撞几何引用——须命中 manifest：resourceId＋digest）。
    rt::ResourceRef link1Collision;
    link1Collision.resourceId = seededObjectId<core::ObjectId>("kin-port-l1-geo");
    {
        core::ContentDigester d;
        const std::string s = "kin-port-l1-geo-bytes";
        d.update(s.data(), s.size());
        link1Collision.contentDigest = d.finalize();
    }
    link1Collision.state = rt::ResourceState::Recorded;
    link1Collision.accessVersion = 1;

    rt::RobotChain chain;
    chain.robotObjectId = ids.device;
    chain.robotLocalName = "KinPortRig";
    chain.deviceName = "Robot";
    rt::CanonicalJoint joint;
    joint.objectId = seededObjectId<core::ObjectId>("kin-port-j1");
    joint.localName = "J1";
    joint.type = rt::JointType::Revolute;
    joint.axis = rw::math::Vector3D<double>(0.0, 0.0, 1.0);
    joint.origin = rw::math::Transform3D<double>::identity();
    joint.bounds = rt::JointBounds{-3.5, 3.5};  // 单位 rad（覆盖 ±π 行程）
    chain.joints = {joint};
    rt::CanonicalLink baseLink;
    baseLink.objectId = ids.link0;
    baseLink.localName = "link_0";
    rt::CanonicalLink flangeLink;
    flangeLink.objectId = ids.link1;
    flangeLink.localName = "link_1";
    flangeLink.collision = link1Collision;  // 声明碰撞几何（与 WC Object 对应）
    chain.links = {baseLink, flangeLink};

    rt::CanonicalTool toolEntry;
    toolEntry.objectId = ids.tool;
    toolEntry.localName = "tool_1";
    toolEntry.tcpOffset = rw::math::Transform3D<double>::identity();

    rt::CanonicalSceneObject envEntry;
    envEntry.objectId = ids.env;
    envEntry.localName = "env_box";
    envEntry.worldPose = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(kArmLength, 0.0, 0.0));
    envEntry.geometry = link1Collision;  // 环境几何必有——复用清单条目（夹具）

    rt::WorldPlacement world;
    world.T_world_base = rw::math::Transform3D<double>::identity();

    rig.model = rt::CanonicalModelBuilder()
                    .setHeader(header)
                    .setWorld(world)
                    .setChain(chain)
                    .setTools({toolEntry})
                    .setDefaultTcpIndex(0U)
                    .setScene({envEntry})
                    .setResourceManifest({link1Collision})
                    .build();

    // ---- 快照事实端口（WC 别名＋身份＋名称映射）----
    rig.source.workcell = rig.workcell;
    rig.source.identity = taggedContentIdentity(0xC7);
    rig.source.names.mapIdentity = taggedContentIdentity(0x6E);
    rig.source.names.byName["Robot"] = ids.device;
    rig.source.names.byName["Tool"] = ids.link1;
    rig.source.names.byName["EnvBox1"] = ids.env;
    rig.source.names.byId[ids.device] = "Robot";
    rig.source.names.byId[ids.link0] = "Base";
    rig.source.names.byId[ids.link1] = "Tool";
    rig.source.names.byId[ids.tool] = "Tool";
    rig.source.names.byId[ids.env] = "EnvBox1";

    // ---- 发布闭包应答（对象存在＋角色与场景一致）----
    const std::pair<const core::ObjectId*, const char*> entries[] = {
        {&ids.device, "RobotLink"},        {&ids.link0, "RobotLink"},
        {&ids.link1, "RobotLink"},         {&ids.tool, "Tool"},
        {&ids.env, "EnvironmentObject"},
    };
    for (const auto& entry : entries) {
        rig.validation.existingObjects[*entry.first] = true;
        rig.validation.roles[*entry.first] = entry.second;
    }
    return rig;
}

/// 经七段解析管线发布策略（唯一发布路径——内容身份由管线计算）。
policy::EngineeringPolicySet publishPolicy(const TestValidationContext& validation,
                                           bool enabled)
{
    policy::RawPolicyInput in;
    in.schemaVersion = policy::PolicySchema::currentVersion;
    in.policyObject = seededObjectId<core::ObjectId>("kin-port-policy");
    in.collision.enabled = enabled;
    if (enabled) {
        in.collision.enabledDomains = {policy::CollisionDomain::Self,
                                       policy::CollisionDomain::Environment,
                                       policy::CollisionDomain::Tool};
        in.collision.safetyClearance = policy::RawThresholdInput{0.0, "m"};
        in.collision.excludeAdjacentLinksByDefault = true;
    }
    in.jointThresholds.nearLimitRatio = policy::RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = policy::RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit =
        policy::RawThresholdInput{12.566370614359172, "rad"};
    const policy::PolicyParseResult res = policy::resolvePolicy(in, validation);
    if (!res.policy.has_value()) {
        throw std::runtime_error("夹具策略应可发布，首条诊断: "
                                 + (res.diagnostics.empty()
                                        ? std::string{"-"}
                                        : res.diagnostics.front().code));
    }
    return *res.policy;
}

/// 装配装置场景（夹具自检失败即抛——测试 fail-fast）。
policy::CollisionScene assembleRigScene(const CollisionRig& rig)
{
    RigView view(*rig.model);
    const rt::Expected<policy::CollisionScene, kin::KinematicsError> scene =
        kin::assembleCollisionScene(view, rig.source);
    if (!scene.ok()) {
        throw std::runtime_error("夹具场景装配应成功（装置良构前提）");
    }
    return scene.get();
}

/// 检查评价产出是否为碰撞（Evaluated＋inCollision——断言可读性辅助）。
bool evaluatedCollision(const kin::IkCollisionVerdict& verdict)
{
    return verdict.state == kin::IkCollisionEvaluationState::Evaluated
        && verdict.inCollision;
}

// =====================================================================
// 计数/故障注入后端替身（doInCollision 计数＋可注入 rw 异常——无二次
// 缓存的运行断言面与设施异常注入面；替身应答不构成碰撞算法证明）
// =====================================================================

class CountingCollisionBackend : public rw::proximity::CollisionStrategy {
public:
    mutable std::size_t collisionQueries = 0;  ///< doInCollision 调用计数
    bool throwInCollision = false;             ///< 故障注入：抛真实 rw 异常

    rw::proximity::ProximityModel::Ptr createModel() override
    {
        return rw::core::ownedPtr(new rw::proximity::ProximityModel(this));
    }
    void destroyModel(rw::proximity::ProximityModel*) override {}
    bool addGeometry(rw::proximity::ProximityModel*, const rw::geometry::Geometry&) override
    {
        return true;
    }
    bool addGeometry(rw::proximity::ProximityModel*,
                     rw::core::Ptr<rw::geometry::Geometry>, bool) override
    {
        return true;
    }
    bool removeGeometry(rw::proximity::ProximityModel*, const std::string&) override
    {
        return true;
    }
    std::vector<std::string> getGeometryIDs(rw::proximity::ProximityModel*) override
    {
        return {};
    }
    std::vector<rw::core::Ptr<rw::geometry::Geometry>>
    getGeometrys(rw::proximity::ProximityModel*) override
    {
        return {};
    }
    void clear() override {}

    bool doInCollision(rw::proximity::ProximityModel::Ptr, const rw::math::Transform3D<>&,
                       rw::proximity::ProximityModel::Ptr, const rw::math::Transform3D<>&,
                       rw::proximity::ProximityStrategyData&) override
    {
        ++collisionQueries;
        if (throwInCollision) {
            RW_THROW("CountingCollisionBackend: scripted detector fault (KIN-05)");
        }
        return false;  // 恒无碰撞应答（计数与异常注入是本替身的断言面）
    }
    void getCollisionContacts(std::vector<rw::proximity::CollisionStrategy::Contact>&,
                              rw::proximity::ProximityStrategyData&) override
    {
        throw std::logic_error("替身边界：getCollisionContacts 不被评估消费");
    }
};

/// 直构脚本化会话（计数后端注入；场景＝装配产物裁剪面——link1/env 两
/// 对象带几何事实，作用域含 (link1,env) 环境对）。描述符为测试值，非
/// 复现要素主张。
std::shared_ptr<const policy::CollisionEvaluationSession>
buildCountedSession(const CollisionRig& rig, const policy::EngineeringPolicySet& policy,
                    const std::shared_ptr<CountingCollisionBackend>& backend)
{
    policy::CollisionScene scene = assembleRigScene(rig);
    // 裁剪到带几何事实的两对象（计数断言聚焦 (link1,env) 单对——作用域
    // 确定性与查询计数一一对应；声明无几何对象不参与查询，policy §7.5）。
    std::vector<policy::SceneObjectEntry> geometric;
    for (const policy::SceneObjectEntry& entry : scene.objects) {
        if (entry.hasCollisionGeometry) {
            geometric.push_back(entry);
        }
    }
    scene.objects = std::move(geometric);
    scene.adjacentLinkPairs.clear();  // (link1,env) 非相邻——清单置空等价

    policy::CollisionBackendDescriptor descriptor;
    descriptor.backendId = "test.counting-backend";
    descriptor.backendVersion = "0.0-test";
    descriptor.toleranceModel = "scripted/test-double(not-a-real-backend)";
    return std::make_shared<const policy::CollisionEvaluationSession>(
        policy, scene, rig.source.names, descriptor, backend);
}

}  // namespace

// =====================================================================
// acceptance 1——请求方场景装配（policy.md §6.1"谁装配"语义）
// =====================================================================

TEST(KinCollisionPort, AssembleSceneFromSnapshotFacts_WP15T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "NFR-COR-05"},
                  std::vector<std::string>{"AT-19"});

    const CollisionRig rig = makeRig();
    RigView view(*rig.model);
    const rt::Expected<policy::CollisionScene, kin::KinematicsError> scene =
        kin::assembleCollisionScene(view, rig.source);
    ASSERT_TRUE(scene.ok()) << "良构装置的装配必须成功";

    // 主链设备＝规范模型机器人对象（ObjectId——R-4：场景值无名称身份）。
    EXPECT_EQ(scene.get().primaryDevice, rig.ids.device);
    // 场景内容身份＝注入的 WC 编译身份（CR-04 值传递——policy 不重算）。
    EXPECT_EQ(scene.get().sceneContentIdentity, rig.source.identity);
    // WC 共享只读别名（同一编译产物——shared_ptr 别名保活）。
    EXPECT_EQ(scene.get().workcell.get(), rig.workcell.get());

    // 对象清单＝三段投影：2 链连杆＋1 工具＋1 场景对象；角色与几何事实
    // 逐项核对（link1 有碰撞几何引用→true；场景对象几何必有→true；
    // KIN-05——几何事实只登记，不收窄作用域）。
    ASSERT_EQ(scene.get().objects.size(), 4U);
    EXPECT_EQ(scene.get().objects[0].objectId, rig.ids.link0);
    EXPECT_EQ(scene.get().objects[0].role, policy::SceneObjectRole::RobotLink);
    EXPECT_EQ(scene.get().objects[0].hasCollisionGeometry, false);
    EXPECT_EQ(scene.get().objects[1].objectId, rig.ids.link1);
    EXPECT_EQ(scene.get().objects[1].role, policy::SceneObjectRole::RobotLink);
    EXPECT_EQ(scene.get().objects[1].hasCollisionGeometry, true);
    EXPECT_EQ(scene.get().objects[2].objectId, rig.ids.tool);
    EXPECT_EQ(scene.get().objects[2].role, policy::SceneObjectRole::Tool);
    EXPECT_EQ(scene.get().objects[2].hasCollisionGeometry, false);
    EXPECT_EQ(scene.get().objects[3].objectId, rig.ids.env);
    EXPECT_EQ(scene.get().objects[3].role, policy::SceneObjectRole::EnvironmentObject);
    EXPECT_EQ(scene.get().objects[3].hasCollisionGeometry, true);

    // 相邻对＝模型事实：链序 (link0, link1)＋工具安装对 (link1, tool)。
    ASSERT_EQ(scene.get().adjacentLinkPairs.size(), 2U);
    EXPECT_EQ(scene.get().adjacentLinkPairs[0],
              std::make_pair(rig.ids.link0, rig.ids.link1));
    EXPECT_EQ(scene.get().adjacentLinkPairs[1],
              std::make_pair(rig.ids.link1, rig.ids.tool));
}

// =====================================================================
// acceptance 5——接线三态：策略未启用（V13-01 唯一开关读取源）
// =====================================================================

TEST(KinCollisionPort, WiringPolicyDisabledAndDetectorArms_WP15T07_ACC5_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05"},
                  std::vector<std::string>{"V13-01"});

    const CollisionRig rig = makeRig();
    const policy::EngineeringPolicySet disabled = publishPolicy(rig.validation, false);
    const policy::EngineeringPolicySet enabled = publishPolicy(rig.validation, true);
    RigView view(*rig.model);

    // 禁用臂：唯一差异＝policy.collision.enabled——可用性随之翻转（启用
    // 状态只读自 policy 的行为证据；本单元 schema 无开关字段）。
    const kin::CollisionSessionWiring wiredDisabled =
        kin::makeCollisionSession(view, rig.source, disabled, nullptr);
    EXPECT_EQ(wiredDisabled.availability,
              kin::CollisionSessionAvailability::PolicyDisabled);
    EXPECT_EQ(wiredDisabled.session, nullptr);
    EXPECT_EQ(wiredDisabled.policyContentIdentity, disabled.contentIdentity);

    // 检测器臂：enabled 策略但评估器空 → 检测器不可用（KIN-05 证据缺失
    // 轨——原因素材非空，绝不视为无碰撞）。
    const kin::CollisionSessionWiring wiredNoDetector =
        kin::makeCollisionSession(view, rig.source, enabled, nullptr);
    EXPECT_EQ(wiredNoDetector.availability,
              kin::CollisionSessionAvailability::DetectorUnavailable);
    EXPECT_EQ(wiredNoDetector.session, nullptr);
    EXPECT_FALSE(wiredNoDetector.detail.empty());
}

// =====================================================================
// acceptance 1/3——真实④端口路径：装配→会话→构型判定（V-08 解析算例）
// =====================================================================

TEST(KinCollisionPort, RealSessionVerdictAndConsistency_WP15T07_ACC1_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "NFR-COR-05"},
                  std::vector<std::string>{"AT-19", "V-08"});

    const CollisionRig rig = makeRig();
    const policy::EngineeringPolicySet policy = publishPolicy(rig.validation, true);
    const std::unique_ptr<policy::ICollisionEvaluator> evaluator =
        policy::makeRobWorkCollisionEvaluator();

    RigView view(*rig.model);
    const kin::CollisionSessionWiring wired =
        kin::makeCollisionSession(view, rig.source, policy, evaluator.get());
    ASSERT_EQ(wired.availability, kin::CollisionSessionAvailability::SessionReady);
    ASSERT_NE(wired.session, nullptr);

    // CON-06 观测面：适配器暴露的策略内容身份＝resolved policy 身份
    // （策略版本经④端口可观测——policy.resolved 依赖键＋快照 policyRef
    // 的对端；策略变更→新会话身份→切片失效）。
    EXPECT_EQ(wired.policyContentIdentity, policy.contentIdentity);
    EXPECT_EQ(wired.session->policyContentIdentity(), policy.contentIdentity);

    // 构型级判定（解析算例）：q=0 相交（对象对非空）；q=π 分离（评价为
    // 无碰撞——Evaluated 态才允许该解读，KIN-05）。
    const kin::IkCollisionVerdict colliding = wired.session->evaluate({0.0});
    EXPECT_TRUE(evaluatedCollision(colliding));
    EXPECT_FALSE(colliding.objectIdPairs.empty());
    // 对象对一律 ObjectId（R-4——判定明细含环境对象端）。
    bool envInPairs = false;
    for (std::size_t i = 0; i + 1 < colliding.objectIdPairs.size(); i += 2) {
        if (colliding.objectIdPairs[i] == rig.ids.env
            || colliding.objectIdPairs[i + 1] == rig.ids.env) {
            envInPairs = true;
        }
    }
    EXPECT_TRUE(envInPairs) << "碰撞对象对应含环境盒端（ObjectId 明细）";

    const kin::IkCollisionVerdict clean = wired.session->evaluate({kClearQ});
    EXPECT_EQ(clean.state, kin::IkCollisionEvaluationState::Evaluated);
    EXPECT_FALSE(clean.inCollision);
    EXPECT_TRUE(clean.objectIdPairs.empty());

    // 三入口一致（AT-19 运行断言半区）：同 (policy, 场景, 名称映射, 评估
    // 器) 重复接线 → 等价会话身份（同一 resolved policy＋同一评估器实例
    // 语义——会话身份确定性 §6.4 的直通核对；无缓存断言见下用例）。
    const kin::CollisionSessionWiring wiredAgain =
        kin::makeCollisionSession(view, rig.source, policy, evaluator.get());
    ASSERT_EQ(wiredAgain.availability, kin::CollisionSessionAvailability::SessionReady);
    EXPECT_EQ(wired.session->sessionIdentity(), wiredAgain.session->sessionIdentity());
    // 同构型判定跨会话一致（NFR-COR-05 三入口同源）。
    const kin::IkCollisionVerdict cleanAgain = wiredAgain.session->evaluate({kClearQ});
    EXPECT_EQ(cleanAgain.state, clean.state);
    EXPECT_EQ(cleanAgain.inCollision, clean.inCollision);
}

// =====================================================================
// acceptance 3——无二次缓存的运行断言（V-25：每查询直穿会话）
// =====================================================================

TEST(KinCollisionPort, AdapterForwardsEveryQueryNoCache_WP15T07_ACC3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05", "NFR-COR-05"},
                  std::vector<std::string>{"AT-19", "V-25"});

    const CollisionRig rig = makeRig();
    const policy::EngineeringPolicySet policy = publishPolicy(rig.validation, true);
    const auto backend = std::make_shared<CountingCollisionBackend>();
    const std::shared_ptr<const policy::CollisionEvaluationSession> session =
        buildCountedSession(rig, policy, backend);

    kin::PolicyCollisionSessionAdapter adapter(session);
    EXPECT_EQ(backend->collisionQueries, 0U);

    // 5 次同构型查询（含重复 q）→ 底层后端恰 5 次逐对查询：适配器零
    // 记忆化、零本地判定副本（不做二次缓存判定的运行断言；静态扫描
    // 半区见 CollisionConsistencyContractTest）。
    const std::vector<double> q(kDeviceDof, 0.0);
    for (std::size_t i = 0; i < 5; ++i) {
        const kin::IkCollisionVerdict verdict = adapter.evaluate(q);
        EXPECT_EQ(verdict.state, kin::IkCollisionEvaluationState::Evaluated);
        EXPECT_FALSE(verdict.inCollision);
    }
    EXPECT_EQ(backend->collisionQueries, 5U)
        << "N 次适配器查询必须恰产生 N 次底层会话查询（V-25 无二次缓存）";
}

// =====================================================================
// acceptance 2——设施异常映射（该解证据缺失，绝不视为无碰撞）
// =====================================================================

TEST(KinCollisionPort, FacilityFailureMapsToFacilityFailed_WP15T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05"},
                  std::vector<std::string>{});

    const CollisionRig rig = makeRig();
    const policy::EngineeringPolicySet policy = publishPolicy(rig.validation, true);

    // 故障注入后端（doInCollision 抛真实 rw::common::Exception——评估期
    // 异常轨的注入面；会话评估捕获转 Failed＋POLICY-CLL-* 诊断）。
    const auto backend = std::make_shared<CountingCollisionBackend>();
    backend->throwInCollision = true;
    const std::shared_ptr<const policy::CollisionEvaluationSession> session =
        buildCountedSession(rig, policy, backend);

    kin::PolicyCollisionSessionAdapter adapter(session);
    const kin::IkCollisionVerdict verdict =
        adapter.evaluate(std::vector<double>(kDeviceDof, 0.0));

    // 设施异常 → FacilityFailed（非 Evaluated——绝不允许"无碰撞"解读；
    // 原因素材非空——该解 DataInsufficient 素材＋诊断的承载）。
    EXPECT_EQ(verdict.state, kin::IkCollisionEvaluationState::FacilityFailed);
    EXPECT_FALSE(verdict.inCollision) << "失败态不得给出碰撞判定值";
    EXPECT_FALSE(verdict.statusDetail.empty()) << "设施异常应携带会话诊断原因素材";
}

// =====================================================================
// acceptance 2——策略禁用被误调用的保守映射（fail-safe 轨）
// =====================================================================

TEST(KinCollisionPort, DisabledPolicySessionMapsToEvidenceMissing_WP15T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-05"},
                  std::vector<std::string>{});

    const CollisionRig rig = makeRig();
    // 禁用策略直接构建会话（§9.3：构建不因禁用失败——禁用是评估期
    // 适用性语义；接线入口已按 enabled 分流，此用例钉住 fail-safe 面：
    // 即使被误调用也绝不产出"无碰撞"判定）。
    const policy::EngineeringPolicySet disabled = publishPolicy(rig.validation, false);
    const auto backend = std::make_shared<CountingCollisionBackend>();
    const std::shared_ptr<const policy::CollisionEvaluationSession> session =
        buildCountedSession(rig, disabled, backend);

    kin::PolicyCollisionSessionAdapter adapter(session);
    const kin::IkCollisionVerdict verdict =
        adapter.evaluate(std::vector<double>(kDeviceDof, 0.0));
    EXPECT_EQ(verdict.state, kin::IkCollisionEvaluationState::EvidenceMissing);
    EXPECT_FALSE(verdict.inCollision);
    EXPECT_FALSE(verdict.statusDetail.empty());
}
