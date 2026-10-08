/**
 * @file   PlannerAvoidanceTest.cpp
 * @brief  避障路径搜索接入的真实路径用例组（TrjPlannerAvoidance，
 *         WP-16-T06）——RobWork 规划器调用接入基准（acceptance 1/V-10/
 *         V-12）、直连碰撞→淘汰→重规划成功的 C8 反例（acceptance 2）、
 *         取消传播与 KIN-05 不采信语义、选型 fail-fast 与④端口直穿
 *         （AT-19 无本地判定副本）。
 *
 * 设计依据：
 *   - units/trajectory.md §10.3/§10.4（policy 交接与避障重规划流程——
 *     单障碍场景"直连碰撞→避障→候选淘汰记录＋替代采纳；不判任务不可行"
 *     的 V-10 观测点；密闭场景搜索未果的 V-11/V-12 观测点）、§17.2
 *     （V-10/V-11/V-12 行——"需真实 planner/后端"清单 §17.3：本套件的
 *     基准/预算/取消用例以真实 RRT-Connect＋真实 policy 碰撞后端送验，
 *     替身仅用于直穿计数断言的受控观测）
 *   - 需求 TRJ-03（规划器调用接入、规划器与参数可配置）、REQUIREMENTS
 *     §8.1 C8 v1.16（候选路径碰撞＝该路径淘汰并触发重规划，不判任务
 *     不可行；"初始候选路径碰撞、重规划成功"＝AT-06 明文反例）、KIN-05
 *     （证据缺口绝不视为无碰撞）、AT-19/NFR-COR-05（三入口一致）、
 *     NFR-COR-02（同种子确定性——R-TRJ-1 种子化验证）
 *   - 任务契约 tasks/foundation/WP-16-T06.json acceptance 1/2/3
 *
 * 测试设施与替身边界（对齐 kinematics/CollisionPortTest 的 POL-TD-1
 * 口径——随套件留痕）：
 *   - 真实装置：程序化 WorkCell（1/2 自由度 RevoluteJoint 串链＋显式
 *     bounds＋0.2 m 立方体碰撞几何）——直连路径与障碍的相交/分离均为
 *     解析已知答案（装置常量注释给出构型—位置解析式）；
 *   - 真实规划器：makePathPlannerAdapter（RRT-Connect，经
 *     rw::math::Math::seed 种子化——同种子等价候选集合的黄金验证在
 *     Determinism 用例）；
 *   - 真实碰撞后端：makeRobWorkCollisionEvaluator→createSession（内置
 *     ProximityStrategyRW）——碰撞判定真实性由 policy 唯一实现承载；
 *   - 受控替身：CountingCollisionBackend（doInCollision 计数——直连
 *     PathSequence 复核的"每样本恰一次后端查询"直穿断言；替身应答不构
 *     成碰撞算法正确性证明）；
 *   - 本套件为**集成模式专属**（消费 rw 非模板类 WorkCell/SerialDevice/
 *     QConstraint/RRTPlanner——冒烟模式不编译本文件，CMake gating 与
 *     产品 TU src/Planner.cpp 同因）。
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
#include <sdurws/ird/trajectory/Planner.hpp>
#include <sdurws/ird/trajectory/Errors.hpp>     // TrajectoryError（fail-fast 断言面）
#include <sdurws/ird/trajectory/TrjTypes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <rw/core/Ptr.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Q.hpp>
#include <rw/models/Joint.hpp>
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
// 解析算例装置常量（构型—位置解析式：平面臂 Tool 位置可手算——
// 1DOF: (L·cos q, L·sin q)；2DOF: (L·cos q1+L·cos(q1+q2), L·sin q1+L·
// sin(q1+q2))——全部 SI：长度 m、角度 rad）
// =====================================================================

/// 立方体全边长（SI m——工具盒与环境盒同尺寸）。
inline constexpr double kBoxSize = 0.2;
/// 臂段长（SI m，X 向——基座→J1 偏移与 J1→Tool 偏移）。
inline constexpr double kArmLength = 0.5;
/// 1DOF 装置的障碍方位角（rad——障碍盒中心在 Tool 圆轨迹的该角度处）。
inline constexpr double kBlockedAngle = 2.0;  // ≈114.6°，∈(π/2, 3π/4)
/// 2DOF 装置关节限位（rad——显式设置，采样盒有限；±6.5 rad 提供大绕行
/// 余地，C-free 占比高使 RRT-Connect 快速收敛）。
inline constexpr double kJointLimit = 6.5;
/// 规划器选型参数（黄金算例锁定值——extend/resolution 单位 rad（逐轴
/// 欧氏度量），attempts 无量纲；随 plannerSelection.params 进身份）。
inline constexpr double kExtend = 0.3;
inline constexpr double kEdgeResolution = 0.05;
inline constexpr std::uint32_t kAttempts = 3;

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
// 名称与发布闭包替身（⑥端口转发语义——CR-04 适配形态；照抄
// kinematics/CollisionPortTest 同款结构）
// =====================================================================

/// 名称映射替身（policy::IPolicyNameContext——⑥端口转发）。
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

/// 发布闭包应答替身（policy::IPolicyValidationContext——解析⑤对象存在
/// 性/角色应答）。
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

/**
 * @brief 装置集合（ avoidance 测试的全部输入面——两自由度与单自由度两
 *        种装置共用本结构）。
 *
 * 手工装配说明（R-1 红线）：trajectory 测试**不消费** kinematics 的
 * assembleCollisionScene（业务域互链禁止——测试目标链接面同禁）；场景
 * 按 policy::CollisionScene 契约（§6.1"谁装配＝请求方"）直接填值——
 * 对象只放带几何事实的两个（工具盒/环境盒），作用域恰 1 对
 * （Environment 域 tool×env——计数断言的确定性基础）。
 */
struct AvoidanceRig {
    core::ObjectId device;        ///< 主链设备身份（primaryDevice）
    core::ObjectId toolObject;    ///< 工具盒对象（Tool 角色，有几何）
    core::ObjectId envObject;     ///< 环境盒对象（EnvironmentObject，有几何）
    std::shared_ptr<rw::models::WorkCell> workcell;             ///< 编译产物（测试自持）
    std::shared_ptr<const rw::models::WorkCell> constWorkcell;  ///< 共享只读别名
    std::optional<rt::WorkCellConstView> view;                  ///< 只读视图（零自建面）
    RigNameContext names;         ///< 名称映射替身
    RigValidationContext validation;  ///< 发布闭包替身
    /// 已发布策略（Valid；shared_ptr 形态——EngineeringPolicySet 无拷贝
    /// 赋值〔内容身份纪律〕，装置结构以共享持有规避赋值面）。
    std::shared_ptr<const policy::EngineeringPolicySet> policySet;
    std::shared_ptr<const policy::CollisionEvaluationSession> session;  ///< 真实会话
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

/// 经七段解析管线发布启用碰撞的策略（唯一发布路径——内容身份管线计算）。
policy::EngineeringPolicySet publishEnabledPolicy(const RigValidationContext& validation,
                                                  const core::ObjectId& policyObject)
{
    policy::RawPolicyInput in;
    in.schemaVersion = policy::PolicySchema::currentVersion;
    in.policyObject = policyObject;
    in.collision.enabled = true;
    in.collision.enabledDomains = {policy::CollisionDomain::Self,
                                   policy::CollisionDomain::Environment,
                                   policy::CollisionDomain::Tool};
    in.collision.safetyClearance = policy::RawThresholdInput{0.0, "m"};
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
 * @brief 构造真实会话（场景装配→唯一实现入口三步构建；任一步失败即抛
 *        ——夹具自检 fail-fast）。
 *
 * @param rig           [in,out] 装置（policySet/session 字段被填充）
 * @param deviceName    [in] 设备运行时名（⑥端口解析键——WC addDevice 名）
 * @param toolFrameName [in] 工具盒挂接帧名（名称映射双向登记）
 * @param envFrameName  [in] 环境盒挂接帧名
 */
void buildSession(AvoidanceRig& rig, const std::string& deviceName,
                  const std::string& toolFrameName, const std::string& envFrameName)
{
    // 名称映射（⑥端口替身——对象↔整名双向；身份为 ObjectId，名称仅
    // 解析辅助——R-4）。
    rig.names.mapIdentity = taggedContentIdentity(0x6E);
    rig.names.byName[deviceName] = rig.device;
    rig.names.byName[toolFrameName] = rig.toolObject;
    rig.names.byName[envFrameName] = rig.envObject;
    rig.names.byId[rig.device] = deviceName;
    rig.names.byId[rig.toolObject] = toolFrameName;
    rig.names.byId[rig.envObject] = envFrameName;
    // 发布闭包应答（角色与场景清单一致——会话构建校验②的应答源）。
    rig.validation.existingObjects[rig.toolObject] = true;
    rig.validation.roles[rig.toolObject] = "Tool";
    rig.validation.existingObjects[rig.envObject] = true;
    rig.validation.roles[rig.envObject] = "EnvironmentObject";

    // 策略发布（Valid 态——会话构建第一步复检通过的前提；shared_ptr 持
    // 有——EngineeringPolicySet 无拷贝赋值）。
    rig.policySet = std::make_shared<const policy::EngineeringPolicySet>(
        publishEnabledPolicy(rig.validation,
                             seededObjectId<core::ObjectId>("trj-planner-policy")));

    // 场景装配（请求方语义——对象只放带几何的两个；adjacentLinkPairs 空
    // ——(tool,env) 非相邻，作用域恰 1 对，见 AvoidanceRig 注）。
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
    scene.sceneContentIdentity = taggedContentIdentity(0xA6);

    // 真实会话（唯一实现入口——内置 ProximityStrategyRW 后端）。
    const std::unique_ptr<policy::ICollisionEvaluator> evaluator =
        policy::makeRobWorkCollisionEvaluator();
    rig.session = evaluator->createSession(*rig.policySet, scene, rig.names);
    if (rig.session == nullptr) {
        throw std::runtime_error("夹具会话应构建成功（装置良构前提）");
    }
}

/**
 * @brief 构造单自由度装置（密闭障碍场景——V-11/V-12 的搜索未果算例）。
 *
 * 几何（解析已知答案）：Base→J1（Z 轴）→Tool（偏移 kArmLength）；工具盒
 * 随 J1 旋转，Tool 位置 (kArmLength·cos q, kArmLength·sin q)；环境盒中心
 * 置于方位角 kBlockedAngle 的同半径处——q=kBlockedAngle 时两盒重合（相
 * 交）。关节空间是一维直线：起点 q=0 与终点 q=π 均清晰（距障碍中心
 * 0.93/0.54 m），但二者之间的直连必穿过 q≈kBlockedAngle 邻域的碰撞区，
 * 且**不存在任何绕行**（1DOF 无旁路）——RRT-Connect 预算内必然搜索未果。
 */
AvoidanceRig makeOneDofRig()
{
    AvoidanceRig rig;
    rig.device = seededObjectId<core::ObjectId>("trj-planner-robot-1dof");
    rig.toolObject = seededObjectId<core::ObjectId>("trj-planner-tool-1dof");
    rig.envObject = seededObjectId<core::ObjectId>("trj-planner-env-1dof");

    rig.workcell = std::make_shared<rw::models::WorkCell>("TrjPlannerRig1Dof");
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
    // 环境盒：挂在根帧（T_world_base=identity——世界系与基座系重合，
    // MDL-22 单一不变量下无二次变换）。
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
    buildSession(rig, "Robot", "Tool", "EnvBox");
    return rig;
}

/**
 * @brief 构造两自由度装置（绕行成功基准——V-10/acceptance 1 主算例）。
 *
 * 几何（解析已知答案）：Base→J1（Z 轴）→Link1（偏移 kArmLength）→J2
 * （Z 轴）→Tool（偏移 kArmLength）。Tool 位置：
 *   (kArmLength·(cos q1 + cos(q1+q2)), kArmLength·(sin q1 + sin(q1+q2)))。
 * 起点构型 (0,0)→Tool=(1,0)；终点构型 (π/2,0)→Tool=(0,1)；**直连路径**
 * （q1: 0→π/2 线性、q2≡0）的 Tool 沿单位圆弧扫过 45° 处 (0.7071,0.7071)，
 * 与环境盒（中心 (0.72,0.72)，盒心距 0.018 m≪两盒半边和 0.2 m）深相交
 * ——直连候选必被 policy 复核淘汰。绕行空间：关节限位 ±kJointLimit 的
 * 大方形 C-space 中障碍 C 区只是一小簇（例如 (0,0)→(0,-π/2)→(π/2,-π/2)
 * →(π/2,0) 的折线路径全程远离障碍）——C-free 连通且占比极高，种子化
 * RRT-Connect 快速找到替代路径。
 */
AvoidanceRig makeTwoDofRig()
{
    AvoidanceRig rig;
    rig.device = seededObjectId<core::ObjectId>("trj-planner-robot-2dof");
    rig.toolObject = seededObjectId<core::ObjectId>("trj-planner-tool-2dof");
    rig.envObject = seededObjectId<core::ObjectId>("trj-planner-env-2dof");

    rig.workcell = std::make_shared<rw::models::WorkCell>("TrjPlannerRig2Dof");
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
    // 环境盒：45° 方向、略在单位圆弧外侧（盒覆盖直连弧的 45° 邻域）。
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
    buildSession(rig, "Robot", "Tool", "EnvBox");
    return rig;
}

// =====================================================================
// 请求构造辅助（黄金算例锁定值——与装置注释的解析答案配套）
// =====================================================================

/// 规划器参数表（白名单三键——kTrjPlannerParam* 常量；值文本进身份）。
std::map<std::string, std::string> goldenPlannerParams()
{
    return {{trj::kTrjPlannerParamExtend, "0.3"},
            {trj::kTrjPlannerParamEdgeResolution, "0.05"},
            {trj::kTrjPlannerParamCandidateAttempts, "3"}};
}

/// 组装编排请求（碰撞检查适用＋真实会话/视图——acceptance 1/2 主形态）。
trj::AvoidancePlanRequest makeAvoidRequest(const AvoidanceRig& rig,
                                           const rw::math::Q& startQ,
                                           const rw::math::Q& endQ,
                                           std::uint64_t seed)
{
    trj::AvoidancePlanRequest request;
    request.startQ = startQ;
    request.endQ = endQ;
    request.lowerBoundQ = rw::math::Q(startQ.size(), -kJointLimit);
    request.upperBoundQ = rw::math::Q(startQ.size(), kJointLimit);
    request.constraint.limitsScaleFactor = 1.0;
    request.constraint.cartesianSampleStep = 0.05;
    request.constraint.collisionCheckApplicable = true;
    // 端点词表：Start/End（序列级转移段——无来源任务点义务，来源字段
    // 保持空与 PtpRequest 同款不变量；需要 TaskPoint 端点的用例自行覆写
    // 并给出 sourceTaskPoint）。
    request.startKind = trj::WaypointKind::Start;
    request.endKind = trj::WaypointKind::End;
    request.tcpRef = rig.toolObject;
    request.plannerFamilyToken = trj::kTrjPlannerFamilyRrtConnect;
    request.plannerParams = goldenPlannerParams();
    request.timeBudgetS = 5.0;
    request.seed = seed;
    request.policySession = rig.session;
    request.workCell = &rig.view.value();
    request.deviceRuntimeName = "Robot";
    return request;
}

// =====================================================================
// 受控替身——计数后端（直穿断言的观测面；照抄 kinematics/CollisionPortTest
// 同款形态。替身应答不构成碰撞算法正确性证明）
// =====================================================================

class CountingCollisionBackend : public rw::proximity::CollisionStrategy {
public:
    mutable std::size_t collisionQueries = 0;  ///< doInCollision 调用计数

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
        ++collisionQueries;  // 计数为本替身的唯一断言面（恒"无碰撞"应答）
        return false;
    }
    void getCollisionContacts(std::vector<rw::proximity::CollisionStrategy::Contact>&,
                              rw::proximity::ProximityStrategyData&) override
    {
        throw std::logic_error("替身边界：getCollisionContacts 不被评估消费");
    }
};

/**
 * @brief 直构计数会话（真实装置场景＋计数后端——直穿断言的载体；场景
 *        与 buildSession 同源装配，仅后端换替身。描述符为测试值）。
 */
std::shared_ptr<const policy::CollisionEvaluationSession>
buildCountedSession(const AvoidanceRig& rig,
                    const std::shared_ptr<CountingCollisionBackend>& backend)
{
    policy::CollisionScene scene;
    scene.workcell = rig.constWorkcell;
    scene.primaryDevice = rig.device;
    policy::SceneObjectEntry toolEntry;
    toolEntry.objectId = rig.toolObject;
    toolEntry.localName = "Tool";
    toolEntry.role = policy::SceneObjectRole::Tool;
    toolEntry.hasCollisionGeometry = true;
    policy::SceneObjectEntry envEntry;
    envEntry.objectId = rig.envObject;
    envEntry.localName = "EnvBox";
    envEntry.role = policy::SceneObjectRole::EnvironmentObject;
    envEntry.hasCollisionGeometry = true;
    scene.objects = {toolEntry, envEntry};
    scene.sceneContentIdentity = taggedContentIdentity(0xA6);

    policy::CollisionBackendDescriptor descriptor;
    descriptor.backendId = "test.counting-backend";
    descriptor.backendVersion = "0.0-test";
    descriptor.toleranceModel = "scripted/test-double(not-a-real-backend)";
    return std::make_shared<const policy::CollisionEvaluationSession>(
        *rig.policySet, scene, rig.names, descriptor, backend);
}

}  // namespace

// =====================================================================
// acceptance 1——直连无碰撞基准（真实 planner 链路的最短路径形态）
// =====================================================================

TEST(TrjPlannerAvoidance, DirectClearAdoptsStraightPath_WP16T06_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{"AT-06"});

    const AvoidanceRig rig = makeTwoDofRig();
    auto adapter = trj::makePathPlannerAdapter();

    // 起终点选在直连清晰的方向（q2 0→π/2：Tool 从 (1,0) 经 (0.854,0.354)
    // 到 (0.5,0.5)——全程距障碍中心 ≥0.39 m，解析清晰）。
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0, 0.0), rw::math::Q(0.0, 1.5707963267948966),
                         1U);
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    // 直连采纳：未经搜索（directPathAdopted）＋段为两端点 JointLinear。
    ASSERT_EQ(result.status, trj::AvoidanceStatus::Ok);
    EXPECT_TRUE(result.directPathAdopted);
    EXPECT_FALSE(result.adoptedCandidateIndex.has_value());
    EXPECT_FALSE(result.directCollision.has_value());
    ASSERT_EQ(result.segment.waypoints.size(), 2U);
    EXPECT_EQ(result.segment.spaceType, trj::SegmentSpaceType::JointLinear);
    // 端点还原（V-01 同源语义——解析对照，附录 D 精确等值（构造性写入））。
    EXPECT_EQ(result.segment.waypoints.front().q, request.startQ);
    EXPECT_EQ(result.segment.waypoints.back().q, request.endQ);
    // 路点语义：起点/终点种类透传；段身份字段齐备。
    EXPECT_EQ(result.segment.waypoints.front().kind, trj::WaypointKind::Start);
    EXPECT_EQ(result.segment.waypoints.back().kind, trj::WaypointKind::End);
    EXPECT_EQ(result.segment.tcpRef, rig.toolObject);
    // 段几何路径长度＝逐轴 |Δq| 之和（解析值：π/2）。
    EXPECT_DOUBLE_EQ(result.segment.pathLengthJoint, 1.5707963267948966);
}

// =====================================================================
// acceptance 1/2——V-10 主算例：直连碰撞→淘汰→重规划成功（全真实链路）
// =====================================================================

TEST(TrjPlannerAvoidance, CollisionThenReworkSucceeds_WP16T06_ACC1_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{"AT-06", "V-10"});

    const AvoidanceRig rig = makeTwoDofRig();
    auto adapter = trj::makePathPlannerAdapter();

    // 直连必碰的构型对（装置注释解析：(0,0)→(π/2,0) 的直连弧穿 45° 处
    // 环境盒——直连候选被 policy PathSequence 复核淘汰）。
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0, 0.0), rw::math::Q(1.5707963267948966, 0.0),
                         42U);
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    // C8 反例（AT-06"初始候选路径碰撞、重规划成功"）：直连淘汰记录在案
    // ＋替代路径采纳＋**不判任务不可行**（Ok 态——AvoidanceStatus 词表
    // 结构上无不可行值，素材轨不上升任务结论）。
    ASSERT_EQ(result.status, trj::AvoidanceStatus::Ok);
    EXPECT_FALSE(result.directPathAdopted);
    ASSERT_TRUE(result.directCollision.has_value());
    // 直连淘汰记录：对象对含环境盒端（ObjectId——NFR-COR-05）＋段内定位
    // pathParameter（TRJ-04 同款必填）。
    const trj::CandidateCollisionRecord& direct = result.directCollision.value();
    EXPECT_TRUE(direct.objectA == rig.envObject || direct.objectB == rig.envObject);
    ASSERT_TRUE(direct.pathParameter.has_value());
    EXPECT_GE(*direct.pathParameter, 0.0);
    EXPECT_LE(*direct.pathParameter, 1.0);

    // 替代路径：经避障搜索采纳（非直连）＋段有效＋端点还原（路点数量
    // 由规划器产出决定——形状自由，只断言结构约束）。
    ASSERT_TRUE(result.adoptedCandidateIndex.has_value());
    EXPECT_GE(result.segment.waypoints.size(), 2U);
    EXPECT_EQ(result.segment.waypoints.front().q, request.startQ);
    EXPECT_EQ(result.segment.waypoints.back().q, request.endQ);
    // 中间路点（若有）必须是 Via 种类（非必经状态——C8 语义）。
    for (std::size_t i = 1; i + 1 < result.segment.waypoints.size(); ++i) {
        EXPECT_EQ(result.segment.waypoints[i].kind, trj::WaypointKind::Via);
    }
    EXPECT_EQ(result.segment.spaceType, trj::SegmentSpaceType::JointLinear);

    // 确定性（R-TRJ-1 种子化验证——同种子两次完整编排的采纳段逐点相等：
    // Math::seed 重置＋RRT 顺序执行＋稳定选择序全序的结构结论）。
    const trj::AvoidancePlanResult replay =
        trj::planPtpWithObstacleAvoidance(request, *adapter);
    ASSERT_EQ(replay.status, trj::AvoidanceStatus::Ok);
    EXPECT_FALSE(replay.directPathAdopted);
    ASSERT_EQ(replay.segment.waypoints.size(), result.segment.waypoints.size());
    for (std::size_t i = 0; i < result.segment.waypoints.size(); ++i) {
        EXPECT_EQ(replay.segment.waypoints[i].q, result.segment.waypoints[i].q)
            << "同种子重放的采纳段在第 " << i << " 个路点偏离（确定性违约）";
    }
}

// =====================================================================
// acceptance 2——V-11/V-12：搜索未果（密闭障碍）→素材轨、不判不可行
// =====================================================================

TEST(TrjPlannerAvoidance, BudgetExhaustedYieldsMaterialNotInfeasible_WP16T06_ACC2)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{"AT-06", "V-11", "V-12"});

    const AvoidanceRig rig = makeOneDofRig();
    auto adapter = trj::makePathPlannerAdapter();

    // 密闭场景：1DOF 直连必穿碰撞区且无绕行——预算/轮数耗尽前 RRT 找不到
    // 任何候选。预算取小值（0.3 s）＋两轮——用例耗时可控且必然未果。
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0), rw::math::Q(3.141592653589793), 7U);
    request.timeBudgetS = 0.3;
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    // 搜索未果素材轨：SearchExhausted＋记录齐备（预算/已试轮数）＋失败
    // 定位素材（TRJ-NO-PATH——§14.4 行 2"§10 规划失败段定位"）。
    ASSERT_EQ(result.status, trj::AvoidanceStatus::SearchExhausted);
    ASSERT_TRUE(result.searchRecord.has_value());
    // 已试轮数＝预算与 K 的双上界先到先停（0.3 s 预算下每轮 query 撑满
    // 剩余时间——密闭空间无解可寻，轮数由预算决定而非 K 上限）。
    EXPECT_GE(result.searchRecord->attemptedCandidates, 1U);
    EXPECT_GT(result.searchRecord->consumedBudgetS, 0.0);
    EXPECT_LE(result.searchRecord->consumedBudgetS, request.timeBudgetS + 1.0);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->phaseToken, trj::kPhasePlanAvoid);
    EXPECT_EQ(result.failure->reasonToken, trj::kTrjNoPath);
    EXPECT_FALSE(result.failure->cause.empty());
    EXPECT_FALSE(result.failure->recommendedAction.empty());
    // 直连淘汰记录在案（直连筛查先检出碰撞——V-11 的初始淘汰证据）。
    EXPECT_TRUE(result.directCollision.has_value());
    // ★ 不判不可行（C8/C5）：结果词表与字段面上**不存在任何任务级不可行
    // 表达**——搜索未果只产出素材，判定归 evidence（N7 边界）。
    EXPECT_FALSE(result.adoptedCandidateIndex.has_value());
    EXPECT_EQ(result.segment.waypoints.size(), 0U);  // 无段产出
}

// =====================================================================
// 取消传播（TASK-01/UX-03——取消不是错误）与 KIN-05 不采信语义
// =====================================================================

TEST(TrjPlannerAvoidance, CancelBeforePlanningYieldsCleanCancel_WP16T06_TASK01)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03", "TASK-01"},
                  std::vector<std::string>{"AT-34"});

    const AvoidanceRig rig = makeTwoDofRig();
    auto adapter = trj::makePathPlannerAdapter();

    // 取消在一切计算前命中（恒真信号）→ Canceled＋零错误素材（UX-03：
    // 取消不是错误——无 failure/无淘汰记录/无搜索记录）。
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0, 0.0), rw::math::Q(1.5707963267948966, 0.0),
                         42U);
    request.cancel = [] { return true; };
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    EXPECT_EQ(result.status, trj::AvoidanceStatus::Canceled);
    EXPECT_FALSE(result.failure.has_value());      // 零错误素材
    EXPECT_FALSE(result.directCollision.has_value());
    EXPECT_FALSE(result.searchRecord.has_value());
    EXPECT_EQ(result.segment.waypoints.size(), 0U);
}

TEST(TrjPlannerAvoidance, UntrustedReviewNotTreatedAsCollision_WP16T06_KIN05)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03", "KIN-05"},
                  std::vector<std::string>{"AT-06", "V-20"});

    const AvoidanceRig rig = makeTwoDofRig();
    auto adapter = trj::makePathPlannerAdapter();

    // 取消计数信号：第一次轮询放行、其后恒取消——直连 PathSequence 复核
    // 在样本边界收到取消（policy §6.6）→ finalized=false → **不可采信**。
    struct CountingCancel {
        std::size_t calls = 0;
        bool operator()()
        {
            ++calls;
            return calls >= 2;  // 第 1 次 false，之后 true
        }
    };
    auto guard = std::make_shared<CountingCancel>();
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0, 0.0), rw::math::Q(1.5707963267948966, 0.0),
                         42U);
    request.cancel = [guard] { return (*guard)(); };
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);

    // 行为断言：不可采信的直连复核**不当作碰撞淘汰**（KIN-05 铁律——
    // 证据缺口≠碰撞证据），编排层把取消观测归位为 Canceled（非错误）。
    EXPECT_EQ(result.status, trj::AvoidanceStatus::Canceled);
    EXPECT_FALSE(result.directCollision.has_value());
    EXPECT_FALSE(result.failure.has_value());
}

// =====================================================================
// 选型校验（§10.2 词表/参数白名单——调用方配置错误 fail-fast 轨）
// =====================================================================

TEST(TrjPlannerAvoidance, SelectionValidationFailFast_WP16T06_TRJ03)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03"},
                  std::vector<std::string>{});

    const AvoidanceRig rig = makeTwoDofRig();
    auto adapter = trj::makePathPlannerAdapter();
    const rw::math::Q start(0.0, 0.0);
    const rw::math::Q end(1.5707963267948966, 0.0);

    // ① family 词表外（§10.2 封闭词表——未登记选型拒绝，不猜测）。
    trj::AvoidancePlanRequest unknownFamily =
        makeAvoidRequest(rig, start, end, 42U);
    unknownFamily.plannerFamilyToken = "prm";
    try {
        trj::planPtpWithObstacleAvoidance(unknownFamily, *adapter);
        FAIL() << "词表外 family 必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/planner/selection-family");
    }

    // ② 参数键白名单外（键集合封闭——未知键拒绝）。
    trj::AvoidancePlanRequest unknownKey = makeAvoidRequest(rig, start, end, 42U);
    unknownKey.plannerParams["goal-bias"] = "0.3";
    try {
        trj::planPtpWithObstacleAvoidance(unknownKey, *adapter);
        FAIL() << "白名单外参数键必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/planner/selection-param-key");
    }

    // ③ 三键缺一（缺省补全＝配置外隐式数值——D-TRJ-5 纪律，拒绝）。
    trj::AvoidancePlanRequest missingKey = makeAvoidRequest(rig, start, end, 42U);
    missingKey.plannerParams.erase(trj::kTrjPlannerParamCandidateAttempts);
    try {
        trj::planPtpWithObstacleAvoidance(missingKey, *adapter);
        FAIL() << "缺键必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/planner/selection-param-missing");
    }

    // ④ 种子 0 非法（I-KIN-4 同款——NFR-COR-02 种子纪律）。
    trj::AvoidancePlanRequest zeroSeed = makeAvoidRequest(rig, start, end, 0U);
    try {
        trj::planPtpWithObstacleAvoidance(zeroSeed, *adapter);
        FAIL() << "种子 0 必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/avoid/seed");
    }

    // ⑤ 碰撞检查适用但会话缺失（R-5——唯一碰撞权威不可缺席）。
    trj::AvoidancePlanRequest missingSession = makeAvoidRequest(rig, start, end, 42U);
    missingSession.policySession = nullptr;
    try {
        trj::planPtpWithObstacleAvoidance(missingSession, *adapter);
        FAIL() << "碰撞检查适用时会话缺失必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/avoid/collision-wiring");
    }

    // ⑥ 反向一致性：碰撞检查不适用却携带会话（双口径防线——fail-fast）。
    trj::AvoidancePlanRequest inconsistent =
        makeAvoidRequest(rig, start, end, 42U);
    inconsistent.constraint.collisionCheckApplicable = false;
    try {
        trj::planPtpWithObstacleAvoidance(inconsistent, *adapter);
        FAIL() << "不适用却带会话必须被拒绝";
    } catch (const trj::TrajectoryError& e) {
        EXPECT_EQ(e.token(), "trajectory/avoid/collision-wiring");
    }

    // ⑦ 碰撞检查不适用的合法形态：无会话/无视图＋直连采纳（V13-01 同款
    // 唯一开关语义——"不在范围"非降级；段约束标记自明）。
    trj::AvoidancePlanRequest notApplicable =
        makeAvoidRequest(rig, start, end, 42U);
    notApplicable.constraint.collisionCheckApplicable = false;
    notApplicable.policySession = nullptr;
    notApplicable.workCell = nullptr;
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(notApplicable, *adapter);
    ASSERT_EQ(result.status, trj::AvoidanceStatus::Ok);
    EXPECT_TRUE(result.directPathAdopted);
    EXPECT_EQ(result.segment.constraint.collisionCheckApplicable, false);
    EXPECT_EQ(result.segment.waypoints.size(), 2U);
}

// =====================================================================
// ④端口直穿断言（AT-19——无本地判定副本/零缓存；受控替身观测）
// =====================================================================

TEST(TrjPlannerAvoidance, SessionPassthroughNoLocalCache_WP16T06_AT19)
{
    IRD_TEST_INFO(std::vector<std::string>{"TRJ-03", "NFR-COR-05"},
                  std::vector<std::string>{"AT-19"});

    const AvoidanceRig rig = makeTwoDofRig();
    const auto backend = std::make_shared<CountingCollisionBackend>();
    const std::shared_ptr<const policy::CollisionEvaluationSession> session =
        buildCountedSession(rig, backend);
    auto adapter = trj::makePathPlannerAdapter();

    // 直连清晰的构型对（q2 0→π/2——同 acceptance 1 基准；直连采纳路径上
    // 恰好一次 PathSequence 复核）。
    trj::AvoidancePlanRequest request =
        makeAvoidRequest(rig, rw::math::Q(0.0, 0.0), rw::math::Q(0.0, 1.5707963267948966),
                         1U);
    request.policySession = session;
    const trj::AvoidancePlanResult result =
        trj::planPtpWithObstacleAvoidance(request, *adapter);
    ASSERT_EQ(result.status, trj::AvoidanceStatus::Ok);
    EXPECT_TRUE(result.directPathAdopted);

    // 直穿计数（无本地判定副本的行为证据）：
    //   直连采样样本数 N = ceil(L/resolution) + 1，L = π/2、resolution =
    //   0.05 → N = ceil(31.4159…) + 1 = 33；
    //   作用域恰 1 对（tool×env，双端有几何）且直连全无碰撞（替身恒
    //   "无碰撞"应答）→ 每样本恰 1 次后端查询 → 计数恒等 N。
    // 任何本地缓存/记忆化都会使计数小于 N；任何本地二次判定都不会发生
    // （适配器零策略逻辑——§10.3）。
    const std::size_t expectedSamples =
        static_cast<std::size_t>(std::ceil(1.5707963267948966 / kEdgeResolution)) + 1U;
    EXPECT_EQ(backend->collisionQueries, expectedSamples);
}
