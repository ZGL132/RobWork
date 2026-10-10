/**
 * @file   RecheckTest.cpp
 * @brief  TRJ-04 复检协议的运行期测试（WP-16-T07 复检半区）——AT-06 三
 *         反例的真实 policy 会话链路锁定（段内碰撞检出/预算耗尽/验证器缺
 *         失）＋限位违例检出＋重规划闭环（复检检出→回退候选→重规划成功）
 *         ＋取消/确定性/前置 fail-fast/采样器契约。
 *
 * 设计依据：
 *   - units/trajectory.md §11.1/§11.3/§15.6（协议全要素）、§17.2 V-14
 *     （平滑后重新碰撞——真实后端）/V-15（平滑后限位复检）/V-19（P-06
 *     预算耗尽——附实际步长＋预算占用）/V-20（验证器缺失——KIN-05 不得
 *     视为无碰撞）行、§17.3（V-14/V-19 属"需真实 planner/后端"清单——本
 *     套件以真实 policy 会话〔内置 ProximityStrategyRW〕送验替身纪律）、
 *     §10.4（重规划反例——AT-06 第三反例的编排闭环）
 *   - 需求 TRJ-04（复检协议 R2/R9 全要素）、AT-06（三反例）、KIN-05（验
 *     证器缺失不视为无碰撞）、ARC-05（间距阈值消费 policy——safetyClearance
 *     >0 会话触发距离能力缺失链路，即 V-20 的真实注入途径）、C8（候选路
 *     径碰撞仅淘汰该路径——重规划闭环）
 *   - 装置先例：PlannerAvoidanceTest.cpp（真实 WorkCell/SerialDevice＋手
 *     工装配 policy::CollisionScene＋真实会话；测试文件自持装置不跨文件
 *     共享——同款拷贝精简）；平滑半区在 test/SmoothTest.cpp（零 policy
 *     装置——两提交拆分的测试面分界）
 *   - 任务契约 tasks/foundation/WP-16-T07.json（acceptance 1/2）
 *
 * 本套件为**集成模式专属**（消费 rw 非模板符号 Q/policy 会话/WorkCell
 * ——PtpSequenceTest 同款 gating；冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/runtime/Adapter.hpp>
#include <sdurws/ird/trajectory/DiagCodes.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>
#include <sdurws/ird/trajectory/Planner.hpp>
#include <sdurws/ird/trajectory/Recheck.hpp>
#include <sdurws/ird/trajectory/Smooth.hpp>
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/Joint.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace core = sdurws::ird::core;
namespace policy = sdurws::ird::policy;
namespace rt = sdurws::ird::runtime;
namespace trj = sdurws::ird::trajectory;

namespace {

// =====================================================================
// 装置常量（与 PlannerAvoidanceTest 同源解析算例——全部 SI：m/rad）
// =====================================================================

/// 立方体全边长（m——工具盒与环境盒同尺寸）。
inline constexpr double kBoxSize = 0.2;
/// 臂段长（m，X 向）。
inline constexpr double kArmLength = 0.5;
/// 1DOF 装置的障碍方位角（rad——q=kBlockedAngle 时工具盒与环境盒重合；
/// 碰撞区 q∈(kBlockedAngle−0.403, kBlockedAngle+0.403)——2·asin(0.2) 邻域）。
inline constexpr double kBlockedAngle = 2.0;
/// 2DOF 装置关节限位（rad——显式设置）。
inline constexpr double kJointLimit = 6.5;

/// 手工构造的非全零对象身份（种子摘要前 16 字节——PlannerAvoidanceTest 同款）。
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
// 名称与发布闭包替身（⑥端口转发——PlannerAvoidanceTest 同款结构）
// =====================================================================

class RigNameContext final : public policy::IPolicyNameContext {
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

class RigValidationContext final : public policy::IPolicyValidationContext {
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

// =====================================================================
// 装置（真实 WorkCell＋手工装配 policy::CollisionScene＋真实会话）
// =====================================================================

struct RecheckRig {
    core::ObjectId device;
    core::ObjectId toolObject;
    core::ObjectId envObject;
    std::shared_ptr<rw::models::WorkCell> workcell;
    std::shared_ptr<const rw::models::WorkCell> constWorkcell;
    std::optional<rt::WorkCellConstView> view;
    RigNameContext names;
    RigValidationContext validation;
    std::shared_ptr<const policy::EngineeringPolicySet> policySet;
    std::shared_ptr<const policy::CollisionEvaluationSession> session;
};

/// 挂 0.2 m 立方体碰撞几何到帧（RigidObject 承载——编译产物形态）。
void attachBox(const std::shared_ptr<rw::models::WorkCell>& workcell,
               const rw::kinematics::Frame::Ptr& frame)
{
    rw::core::Ptr<rw::models::RigidObject> object =
        rw::core::ownedPtr(new rw::models::RigidObject(frame));
    object->addGeometry(rw::core::ownedPtr(
        new rw::geometry::Geometry(rw::core::ownedPtr(
            new rw::geometry::Box(kBoxSize, kBoxSize, kBoxSize)))));
    workcell->add(object);
}

/**
 * @brief 经七段解析管线发布启用碰撞的策略（safetyClearance 参数化——
 *        V-20 的注入途径：间距>0 触发距离查询，内置二值后端无距离能力
 *        →evaluate Failed＋POLICY-CLL-DETECTOR-UNAVAILABLE→复检不可采信
 *        →DataInsufficient；语义权威见 policy P-POL-11）。
 */
policy::EngineeringPolicySet publishEnabledPolicy(const RigValidationContext& validation,
                                                  const core::ObjectId& policyObject,
                                                  double safetyClearance)
{
    policy::RawPolicyInput in;
    in.schemaVersion = policy::PolicySchema::currentVersion;
    in.policyObject = policyObject;
    in.collision.enabled = true;
    in.collision.enabledDomains = {policy::CollisionDomain::Self,
                                   policy::CollisionDomain::Environment,
                                   policy::CollisionDomain::Tool};
    in.collision.safetyClearance = policy::RawThresholdInput{safetyClearance, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
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

/**
 * @brief 构造真实会话（场景装配→唯一实现入口三步构建；safetyClearance
 *        参数化见 publishEnabledPolicy 注）。
 */
void buildSession(RecheckRig& rig, const std::string& deviceName,
                  const std::string& toolFrameName, const std::string& envFrameName,
                  double safetyClearance)
{
    rig.names.mapIdentity = taggedContentIdentity(0x7E);
    rig.names.byName[deviceName] = rig.device;
    rig.names.byName[toolFrameName] = rig.toolObject;
    rig.names.byName[envFrameName] = rig.envObject;
    rig.names.byId[rig.device] = deviceName;
    rig.names.byId[rig.toolObject] = toolFrameName;
    rig.names.byId[rig.envObject] = envFrameName;
    rig.validation.existingObjects[rig.toolObject] = true;
    rig.validation.roles[rig.toolObject] = "Tool";
    rig.validation.existingObjects[rig.envObject] = true;
    rig.validation.roles[rig.envObject] = "EnvironmentObject";

    rig.policySet = std::make_shared<const policy::EngineeringPolicySet>(
        publishEnabledPolicy(rig.validation,
                             seededObjectId<core::ObjectId>("trj-recheck-policy"),
                             safetyClearance));

    policy::CollisionScene scene;
    scene.workcell = rig.constWorkcell;
    scene.primaryDevice = rig.device;
    policy::SceneObjectEntry toolEntry;
    toolEntry.objectId = rig.toolObject;
    toolEntry.localName = toolFrameName;
    toolEntry.role = policy::SceneObjectRole::Tool;
    toolEntry.hasCollisionGeometry = true;
    policy::SceneObjectEntry envEntry;
    envEntry.objectId = rig.envObject;
    envEntry.localName = envFrameName;
    envEntry.role = policy::SceneObjectRole::EnvironmentObject;
    envEntry.hasCollisionGeometry = true;
    scene.objects = {toolEntry, envEntry};
    scene.sceneContentIdentity = taggedContentIdentity(0xA7);

    const std::unique_ptr<policy::ICollisionEvaluator> evaluator =
        policy::makeRobWorkCollisionEvaluator();
    rig.session = evaluator->createSession(*rig.policySet, scene, rig.names);
    if (rig.session == nullptr) {
        throw std::runtime_error("夹具会话应构建成功（装置良构前提）");
    }
}

/**
 * @brief 构造单自由度装置（1DOF：Tool 位置 (L·cos q, L·sin q)；环境盒在
 *        方位角 kBlockedAngle 同半径处——q∈kBlockedAngle 邻域必碰）。
 */
RecheckRig makeOneDofRig(double safetyClearance)
{
    RecheckRig rig;
    rig.device = seededObjectId<core::ObjectId>("trj-recheck-robot-1dof");
    rig.toolObject = seededObjectId<core::ObjectId>("trj-recheck-tool-1dof");
    rig.envObject = seededObjectId<core::ObjectId>("trj-recheck-env-1dof");

    rig.workcell = std::make_shared<rw::models::WorkCell>("TrjRecheckRig1Dof");
    rw::core::Ptr<rw::kinematics::FixedFrame> base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Base", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    j1->setBounds(rw::math::Q(1, -3.5), rw::math::Q(1, 3.5));  // 单位 rad
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Tool", rw::math::Transform3D<>(
                                                  rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j1);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox", rw::math::Transform3D<>(
                          rw::math::Vector3D<>(kArmLength * std::cos(kBlockedAngle),
                                               kArmLength * std::sin(kBlockedAngle), 0.0))));
    rig.workcell->addFrame(envFrame);
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));
    attachBox(rig.workcell, tool);
    attachBox(rig.workcell, envFrame);

    rig.constWorkcell = rig.workcell;
    rig.view.emplace(rw::core::Ptr<const rw::models::WorkCell>(
        std::shared_ptr<const rw::models::WorkCell>(rig.workcell)));
    buildSession(rig, "Robot", "Tool", "EnvBox", safetyClearance);
    return rig;
}

/**
 * @brief 构造两自由度装置（2DOF：Tool 位置 (L·(cos q1+cos(q1+q2)), L·(sin
 *        q1+sin(q1+q2)))；环境盒 (0.72,0.72) 覆盖直连弧 45° 邻域——与
 *        PlannerAvoidanceTest 同一解析算例）。
 */
RecheckRig makeTwoDofRig(double safetyClearance)
{
    RecheckRig rig;
    rig.device = seededObjectId<core::ObjectId>("trj-recheck-robot-2dof");
    rig.toolObject = seededObjectId<core::ObjectId>("trj-recheck-tool-2dof");
    rig.envObject = seededObjectId<core::ObjectId>("trj-recheck-env-2dof");

    rig.workcell = std::make_shared<rw::models::WorkCell>("TrjRecheckRig2Dof");
    rw::core::Ptr<rw::kinematics::FixedFrame> base = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Base", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    j1->setBounds(rw::math::Q(1, -kJointLimit), rw::math::Q(1, kJointLimit));
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> link1 = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Link1",
                                       rw::math::Transform3D<>(
                                           rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(link1, j1);
    rw::core::Ptr<rw::models::RevoluteJoint> j2 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J2", rw::math::Transform3D<>::identity()));
    j2->setBounds(rw::math::Q(1, -kJointLimit), rw::math::Q(1, kJointLimit));
    rig.workcell->addFrame(j2, link1);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("Tool", rw::math::Transform3D<>(
                                                  rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j2);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox", rw::math::Transform3D<>(rw::math::Vector3D<>(0.72, 0.72, 0.0))));
    rig.workcell->addFrame(envFrame);
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));
    attachBox(rig.workcell, tool);
    attachBox(rig.workcell, envFrame);

    rig.constWorkcell = rig.workcell;
    rig.view.emplace(rw::core::Ptr<const rw::models::WorkCell>(
        std::shared_ptr<const rw::models::WorkCell>(rig.workcell)));
    buildSession(rig, "Robot", "Tool", "EnvBox", safetyClearance);
    return rig;
}

// =====================================================================
// 受控替身——代表点集采样器（P-06 行 9 规则的测试物化；替身只承载采样
// 协议/等长契约——碰撞判定全部由真实会话承担）
// =====================================================================

/**
 * @brief 单点恒零采样器：代表点集＝{原点}——位移恒 0（笛卡尔界恒满足）。
 *        用于"纯关节界驱动细分"的算例（预算耗尽/限位检出）。
 */
class TcpOnlySampler final : public trj::IRepresentPointSampler {
public:
    std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q&) const override
    {
        return {rw::math::Vector3D<double>(0.0, 0.0, 0.0)};
    }
};

/**
 * @brief 点数漂移采样器：首次调用返回 1 点、此后全部返回 2 点——同一子段
 *        两端构型的点数必然漂移（第二次调用起），装配面等长契约违约的
 *        fail-fast 断言面。
 */
class DriftingSampler final : public trj::IRepresentPointSampler {
public:
    mutable int calls = 0;
    std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q&) const override
    {
        ++calls;
        if (calls <= 1) {
            return {rw::math::Vector3D<double>(0.0, 0.0, 0.0)};
        }
        return {rw::math::Vector3D<double>(0.0, 0.0, 0.0),
                rw::math::Vector3D<double>(1.0, 0.0, 0.0)};
    }
};

/**
 * @brief 1DOF 装置解析采样器：代表点集＝{Tool 位置}（复现装置解析几何
 *        (L·cos q, L·sin q)——段内碰撞算例的笛卡尔界驱动）。
 */
class OneDofToolSampler final : public trj::IRepresentPointSampler {
public:
    std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q& q) const override
    {
        return {rw::math::Vector3D<double>(kArmLength * std::cos(q[0]),
                                           kArmLength * std::sin(q[0]), 0.0)};
    }
};

/**
 * @brief 2DOF 装置解析采样器：代表点集＝{Tool 位置}（解析式
 *        (L·(cos q1+cos(q1+q2)), L·(sin q1+sin(q1+q2)))）。
 */
class TwoDofToolSampler final : public trj::IRepresentPointSampler {
public:
    std::vector<rw::math::Vector3D<double>> sample(const rw::math::Q& q) const override
    {
        return {rw::math::Vector3D<double>(
            kArmLength * (std::cos(q[0]) + std::cos(q[0] + q[1])),
            kArmLength * (std::sin(q[0]) + std::sin(q[0] + q[1])), 0.0)};
    }
};

// =====================================================================
// 受控替身——λ 几何（任意采样函数的段几何——限位违例算例）
// =====================================================================

/// 采样函数驱动的段几何（限位违例算例：中点鼓包越限）。
class LambdaGeometry final : public trj::IPathGeometry {
public:
    explicit LambdaGeometry(std::function<rw::math::Q(double)> fn)
        : m_fn(std::move(fn))
    {
    }
    rw::math::Q sampleAt(double s) const override
    {
        if (!std::isfinite(s) || s < 0.0 || s > 1.0) {
            throw trj::TrajectoryError("trajectory/test/geometry-param",
                                       "测试几何参数越界");
        }
        return m_fn(s);
    }
private:
    std::function<rw::math::Q(double)> m_fn;
};

// =====================================================================
// 请求构造辅助
// =====================================================================

/// 复检默认参数（P-06 冻结表——makeP06FrozenRecheckParameters 直取）。
trj::RecheckParameters frozenParams() { return trj::makeP06FrozenRecheckParameters(); }

/// 构造最简复检请求（折线几何＋必检参数 [0,1]＋单旋转轴——按用例覆写）。
trj::RecheckRequest baseRecheckRequest(const RecheckRig& rig,
                                       const trj::IPathGeometry* geometry,
                                       trj::IRepresentPointSampler* sampler)
{
    trj::RecheckRequest request;
    request.path = geometry;
    request.mandatoryParameters = {0.0, 1.0};
    request.jointTypes = {trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(1, -100.0);
    request.upperBoundQ = rw::math::Q(1, 100.0);
    request.params = frozenParams();
    request.sampler = sampler;
    request.session = rig.session;
    request.segmentIndex = 0;
    return request;
}

}  // namespace

// =====================================================================
// 复检半区（§15.6——真实 policy 会话链路；AT-06 三反例）
// =====================================================================

/**
 * 复检通过基准（零细分）：2DOF 装置空旷区短路径（关节步长 0.002≤0.05、
 * TCP 位移≈0.001≤0.005——双上界初判满足，0 层细分）→Passed＋coverage
 * 已查＋预算账目未耗尽＋实际步长已记录（§11.5 Provided 语义）。
 */
TEST(SmoothRecheckTest, RecheckPassedBaselineZeroSubdivision_WP16T07_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeTwoDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(2, 0.001, 0.0),
                                           rw::math::Q(2, 0.003, 0.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TwoDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    ASSERT_EQ(outcome.status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.conclusion, trj::RecheckConclusion::Passed);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.budget.budgetExhausted);
    EXPECT_EQ(outcome.budget.subdivisionDepthUsed, 0U);
    ASSERT_TRUE(outcome.budget.actualMaxJointStep.tryValue().has_value());
    EXPECT_NEAR(*outcome.budget.actualMaxJointStep.tryValue(), 0.002, 1e-12);
    EXPECT_GT(outcome.coverage.pairsEvaluated, 0U) << "碰撞查询应已执行";
}

/**
 * AT-06 反例①（段内碰撞检出——端点无碰撞、段内碰撞必须被复检检出；
 * V-14 真实后端）：1DOF 装置 q:1.2→2.8 直连——两端构型距障碍盒中心
 * 0.389 m＞两盒半边和 0.2 m（清晰），途中 q∈(1.597,2.403) 邻域必穿——
 * 细分至双上界满足（关节 2⁸ 段、代表点弦位移 ≤0.005 m）后 PathSequence
 * 在障碍邻域检出→Collision＋检出记录携带 pathParameter＋TRJ-RECHECK-
 * COLLISION 素材。
 */
TEST(SmoothRecheckTest, RecheckDetectsMidPathCollision_WP16T07_AT06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeOneDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(1, 1.2),
                                           rw::math::Q(1, 2.8)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    OneDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    ASSERT_EQ(outcome.status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.conclusion, trj::RecheckConclusion::Collision);
    ASSERT_FALSE(outcome.collisionRecords.empty());
    ASSERT_TRUE(outcome.collisionRecords.front().pathParameter.has_value());
    EXPECT_GT(outcome.budget.subdivisionDepthUsed, 0U) << "细分应已发生";
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->reasonToken, std::string(trj::kTrjRecheckCollision));
    EXPECT_EQ(outcome.failure->phaseToken, std::string(trj::kPhaseRecheck));
    EXPECT_EQ(outcome.failure->segmentIndex, 0U);
}

/**
 * AT-06 反例②（细分预算耗尽——R9；V-19）：1DOF 装置 60 rad 大行程路径
 * （限位输入放宽到 ±100——评价区间投影与装置限位无关）＋恒零代表点采样
 * 器（纯关节界驱动细分）：60/2¹⁰≈0.0586>0.05 达 10 层预算仍超→DataInsufficient
 * （BudgetExhausted）＋预算账目（10 层/2047 子段/耗尽标记）＋实际最大步
 * 长与比较型素材；**不执行碰撞查询**（coverage 零值——未查部分如实）。
 */
TEST(SmoothRecheckTest, RecheckBudgetExhaustedDataInsufficient_WP16T07_AT06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeOneDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(1, 0.0), rw::math::Q(1, 60.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TcpOnlySampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    ASSERT_EQ(outcome.status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.conclusion, trj::RecheckConclusion::DataInsufficient);
    EXPECT_EQ(outcome.dataInsufficientReason,
              trj::RecheckDataInsufficientReason::BudgetExhausted);
    // R9 预算账目：10 层二分、逐层 1+2+…+1024=2047 子段、耗尽标记。
    EXPECT_EQ(outcome.budget.subdivisionDepthUsed, 10U);
    EXPECT_EQ(outcome.budget.subdivisionBudget, 10U);
    EXPECT_EQ(outcome.budget.subsegmentsExamined, 2047U);
    EXPECT_TRUE(outcome.budget.budgetExhausted);
    // 实际达到的最大步长（停止层 1024 子段的实测——60/1024，非初始粗子段）。
    ASSERT_TRUE(outcome.budget.actualMaxJointStep.tryValue().has_value());
    EXPECT_NEAR(*outcome.budget.actualMaxJointStep.tryValue(), 60.0 / 1024.0, 1e-12);
    // 比较型素材（实际步长/预算——TRJ-RECHECK-BUDGET-EXHAUSTED）。
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->reasonToken,
              std::string(trj::kTrjRecheckBudgetExhausted));
    ASSERT_TRUE(outcome.failure->comparison.has_value());
    EXPECT_NEAR(outcome.failure->comparison->actual.quantity.value(), 60.0 / 1024.0,
                1e-12);
    EXPECT_NEAR(outcome.failure->comparison->expected.quantity.value(), 0.05, 1e-12);
    // 未执行碰撞查询（预算耗尽分支——验证覆盖已判不充分）。
    EXPECT_EQ(outcome.coverage.pairsEvaluated, 0U);
    EXPECT_TRUE(outcome.collisionRecords.empty());
}

/**
 * AT-06 反例③前半（验证器缺失——KIN-05；V-20 真实注入）：safetyClearance
 * =0.01 的策略会话（间距检查需距离查询）＋内置二值后端无距离能力→
 * evaluate Failed＋DETECTOR-UNAVAILABLE→复检不可采信→DataInsufficient
 * （EvidenceUnavailable）＋TRJ-RECHECK-DATA-INSUFFICIENT 素材；**不得视为
 * 无碰撞**（结论非 Passed）。
 */
TEST(SmoothRecheckTest, RecheckEvidenceUnavailableDataInsufficient_WP16T07_AT06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "KIN-05"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeTwoDofRig(0.01);  // 间距阈值>0——消费 policy 工程策略
    const std::vector<rw::math::Q> path = {rw::math::Q(2, 0.001, 0.0),
                                           rw::math::Q(2, 0.003, 0.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TwoDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    ASSERT_EQ(outcome.status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.conclusion, trj::RecheckConclusion::DataInsufficient);
    EXPECT_EQ(outcome.dataInsufficientReason,
              trj::RecheckDataInsufficientReason::EvidenceUnavailable);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->reasonToken,
              std::string(trj::kTrjRecheckDataInsufficient));
    // KIN-05 铁律：不得视为无碰撞。
    EXPECT_NE(outcome.conclusion, trj::RecheckConclusion::Passed);
}

/**
 * 限位违例检出（§11.3"平滑后逐采样限位检查"；V-15）：鼓包路点（s=0.5
 * 必检参数）越上界→限位复检检出→Collision＋逐条违例记录＋TRJ-LIMIT-
 * EXCEEDED 比较型素材（实际/限界/单位）。
 */
TEST(SmoothRecheckTest, RecheckDetectsLimitViolation_WP16T07_V15)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeTwoDofRig(0.0);
    // 双轴鼓包几何（q2 恒 0 对齐装置自由度）：q1(0)=0.005、q1(0.5)=0.5、
    // q1(1)=0.005——上界 0.03 被中段越过（平滑可能越出原路径包络的语义
    // 等价物）。
    auto bump = std::make_shared<LambdaGeometry>(
        [](double s) {
            return rw::math::Q(2, 0.005 + 0.495 * std::sin(3.14159265358979323846 * s),
                               0.0);
        });
    TcpOnlySampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, bump.get(), &sampler);
    // 鼓包顶点 s=0.5 是平滑产物路点——必检参数（§11.1"端点复检（各段端
    // 点/路点）"）——限位复检的强制采样点；两端子段步长 0.495>0.05 同时
    // 驱动细分。
    request.mandatoryParameters = {0.0, 0.5, 1.0};
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, 0.0, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, 0.03, kJointLimit);

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    ASSERT_EQ(outcome.status, trj::RecheckStatus::Completed);
    EXPECT_EQ(outcome.conclusion, trj::RecheckConclusion::Collision);
    ASSERT_FALSE(outcome.limitViolations.empty());
    const trj::RecheckLimitViolationRecord& violation = outcome.limitViolations.front();
    EXPECT_EQ(violation.axisKind, trj::JointTypeKind::Revolute);
    EXPECT_TRUE(violation.upper);
    EXPECT_GT(violation.actualQ, 0.03);
    ASSERT_TRUE(violation.record.comparison.has_value());
    EXPECT_NEAR(violation.record.comparison->expected.quantity.value(), 0.03, 1e-12);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->reasonToken, std::string(trj::kTrjLimitExceeded));
}

/**
 * 取消（UX-03）：细分层边界取消观测命中→Canceled＋零素材（账目如实携带
 * 已执行部分）。
 */
TEST(SmoothRecheckTest, RecheckCanceledZeroMaterial_WP16T07_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{"AT-34"});

    RecheckRig rig = makeOneDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(1, 0.0), rw::math::Q(1, 60.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TcpOnlySampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.cancel = []() { return true; };  // 层边界立即命中

    const trj::RecheckOutcome outcome = trj::recheckSegment(request);
    EXPECT_EQ(outcome.status, trj::RecheckStatus::Canceled);
    EXPECT_TRUE(outcome.collisionRecords.empty());
    EXPECT_TRUE(outcome.limitViolations.empty());
    EXPECT_FALSE(outcome.failure.has_value());  // 零错误素材
}

/**
 * 确定性重放（NFR-COR-02）：同请求两次复检逐字段等价（结论/记录/账目/
 * coverage 全量）。
 */
TEST(SmoothRecheckTest, RecheckDeterministicReplay_WP16T07_NFRCOR02)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04", "NFR-COR-02"},
                  std::vector<std::string>{});

    RecheckRig rig = makeTwoDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(2, 0.0, 0.0),
                                           rw::math::Q(2, 0.003, 0.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TwoDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);

    const trj::RecheckOutcome first = trj::recheckSegment(request);
    const trj::RecheckOutcome second = trj::recheckSegment(request);
    EXPECT_EQ(first.status, second.status);
    EXPECT_EQ(first.conclusion, second.conclusion);
    EXPECT_EQ(first.collisionRecords, second.collisionRecords);
    EXPECT_EQ(first.limitViolations.size(), second.limitViolations.size());
    EXPECT_TRUE(first.budget == second.budget);
    EXPECT_TRUE(first.coverage == second.coverage);
}

/**
 * 采样器等长契约违约（装配面义务面）：逐构型点数漂移→fail-fast
 * （TrajectoryError——程序缺陷不是用户数据结局）。
 */
TEST(SmoothRecheckTest, RecheckSamplerContractViolation_WP16T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    RecheckRig rig = makeTwoDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(2, 0.0, 0.0),
                                           rw::math::Q(2, 0.1, 0.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    DriftingSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);
    EXPECT_THROW(trj::recheckSegment(request), trj::TrajectoryError);
}

/**
 * 前置 fail-fast 组（§15.6 非法示例面）：越顶步长参数/采样器缺失/会话缺
 * 失/必检参数不含端点——四组调用方契约违约全部拒绝。
 */
TEST(SmoothRecheckTest, RecheckPreflightFailsFast_WP16T07_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-04"}, std::vector<std::string>{});

    RecheckRig rig = makeTwoDofRig(0.0);
    const std::vector<rw::math::Q> path = {rw::math::Q(2, 0.0, 0.0),
                                           rw::math::Q(2, 0.1, 0.0)};
    auto linear = trj::makeLinearJointPathGeometry(path);
    TwoDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);

    {  // 越顶步长（policy 放宽超过 P-06 上界——R-POL-5 封顶语义）。
        trj::RecheckRequest bad = request;
        bad.params.maxCartesianStep = 0.03;  // > 行 6 上界 0.02
        EXPECT_THROW(trj::recheckSegment(bad), trj::TrajectoryError);
    }
    {  // 采样器缺失（R9 度量对象缺失）。
        trj::RecheckRequest bad = request;
        bad.sampler = nullptr;
        EXPECT_THROW(trj::recheckSegment(bad), trj::TrajectoryError);
    }
    {  // 会话缺失（复检协议＝碰撞验证协议）。
        trj::RecheckRequest bad = request;
        bad.session = nullptr;
        EXPECT_THROW(trj::recheckSegment(bad), trj::TrajectoryError);
    }
    {  // 必检参数不含段端点（R2①/P-06 行 9"段端点必含"）。
        trj::RecheckRequest bad = request;
        bad.mandatoryParameters = {0.25, 0.75};
        EXPECT_THROW(trj::recheckSegment(bad), trj::TrajectoryError);
    }
}

/**
 * AT-06 反例③后半（重规划闭环——"初始候选路径碰撞、重规划成功"，C8）：
 * 2DOF 装置直连候选 (0,0)→(π/2,0) 的关节空间折线被复检检出碰撞（真实后
 * 端）→回退保留候选路径→planPtpWithObstacleAvoidance 重规划（种子化
 * RRT-Connect）→Ok 且**不判任务不可行**（status==Ok——C8 作用域）。
 */
TEST(SmoothRecheckTest, RecheckCollisionThenReplanSucceeds_WP16T07_AT06)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03", "TRJ-04"},
                  std::vector<std::string>{"AT-06"});

    RecheckRig rig = makeTwoDofRig(0.0);
    // 直连候选路径（关节空间折线——T04/T06 的段几何形态）。
    const std::vector<rw::math::Q> candidate = {rw::math::Q(2, 0.0, 0.0),
                                                rw::math::Q(2, 1.5707963267948966,
                                                            0.0)};
    auto linear = trj::makeLinearJointPathGeometry(candidate);

    // 第 1 步：复检候选路径——直连穿 45° 障碍，检出碰撞。
    TwoDofToolSampler sampler;
    trj::RecheckRequest request = baseRecheckRequest(rig, linear.get(), &sampler);
    request.jointTypes = {trj::JointTypeKind::Revolute, trj::JointTypeKind::Revolute};
    request.lowerBoundQ = rw::math::Q(2, -kJointLimit, -kJointLimit);
    request.upperBoundQ = rw::math::Q(2, kJointLimit, kJointLimit);
    const trj::RecheckOutcome rechecked = trj::recheckSegment(request);
    ASSERT_EQ(rechecked.status, trj::RecheckStatus::Completed);
    ASSERT_EQ(rechecked.conclusion, trj::RecheckConclusion::Collision)
        << "直连候选应被复检检出（AT-06 反例③的初始碰撞半区）";

    // 第 2 步：回退保留候选路径→重规划（§10.4 编排——与 T06 V-10 同款
    // 装置参数；种子化 RRT-Connect 大方形 C-space 快速绕行）。
    trj::AvoidancePlanRequest avoid;
    avoid.startQ = candidate.front();
    avoid.endQ = candidate.back();
    avoid.lowerBoundQ = request.lowerBoundQ;
    avoid.upperBoundQ = request.upperBoundQ;
    avoid.constraint.collisionCheckApplicable = true;
    avoid.constraint.cartesianSampleStep = 0.05;
    avoid.startKind = trj::WaypointKind::Start;
    avoid.endKind = trj::WaypointKind::End;
    avoid.tcpRef = rig.toolObject;
    avoid.plannerFamilyToken = trj::kTrjPlannerFamilyRrtConnect;
    avoid.plannerParams = {{trj::kTrjPlannerParamExtend, "0.3"},
                           {trj::kTrjPlannerParamEdgeResolution, "0.05"},
                           {trj::kTrjPlannerParamCandidateAttempts, "3"}};
    avoid.timeBudgetS = 5.0;
    avoid.seed = 20261010U;
    avoid.policySession = rig.session;
    avoid.workCell = &rig.view.value();
    avoid.deviceRuntimeName = "Robot";
    auto adapter = trj::makePathPlannerAdapter();
    const trj::AvoidancePlanResult replanned =
        trj::planPtpWithObstacleAvoidance(avoid, *adapter);
    // C8：候选路径碰撞仅淘汰该路径——重规划成功、不判任务不可行。
    EXPECT_EQ(replanned.status, trj::AvoidanceStatus::Ok);
    EXPECT_FALSE(replanned.directPathAdopted);
    ASSERT_TRUE(replanned.adoptedCandidateIndex.has_value());
}
