/**
 * @file   CollisionEvaluationTest.cpp
 * @brief  碰撞评估执行用例组（POL-T07）——POL-EVAL-1~9/POL-EXC-1/
 *         POL-CONC-1（AT-19 载体）＋POL-LATE-1＋R-POL-5/D-9 结构断言：
 *         评估状态机、稳定排序、§7.4 边界值决策表、取消/异常/迟到拒绝、
 *         并发只读、API 无阈值/模式参数。
 *
 * 设计依据：
 *   - units/policy.md §11（POL-EVAL-1~9/POL-EXC-1/POL-CONC-1/POL-LATE-1
 *     用例行——本套件逐条交付；POL-EVAL-8① 的边界用例按 §7.4 决策表）、
 *     §6.3（状态机四分）、§6.4（确定性稳定排序）、§7.4（距离语义与边界
 *     值决策表——d＞m/d＝m 清晰、0≤d＜m 间距不足、后端相交＝碰撞、
 *     NaN/±Inf 评估失败、m＝0 退化）、§7.5（评估期异常处理行）、
 *     §6.5 R-POL-5（评估 API 无阈值/模式参数——D-9 结构防覆盖）、
 *     §12 POL-T07 行（完成条件＝状态机/稳定排序/边界值决策表用例通过）
 *   - 需求 NFR-COR-05/AT-19（三入口一致——本套件以同会话重复评估＋
 *     多线程并发分派承载其 policy 侧观测点）、KIN-02（构型级发现）、
 *     KIN-05（无几何≠无碰撞；缺检测器≠无碰撞）、TRJ-04（PathSequence
 *     保序＋pathParameter）、TASK-02/CON-04（取消/失败不产正式证据）
 *
 * 测试设施与替身边界声明（POL-TD-1——随套件留痕）：
 *   - 真实后端解析算例：内置 ProximityStrategyRW 对程序化构造的
 *     WorkCell（1 自由度 RevoluteJoint 串链＋两个 0.2 m 立方体碰撞几何，
 *     q=0 时两盒重合〔相交〕、q=π 时分离〔间隙 0.8 m〕——解析已知答案）
 *     验证碰撞检出与状态机（真实数值正确性的解析算例承载；analytic-case
 *     黄金数据集的 testkit manifest 登记归 POL-T11/WP-02 协同）。
 *   - 受控替身：ScriptedCollisionBackend（碰撞应答脚本化——可注入
 *     rw::common::Exception）、ScriptedDistanceBackend（同时实现
 *     DistanceStrategy——§7.4 边界值决策表的距离供给，脚本化实测值）、
 *     ScriptedCallContext（协作取消脚本＋存活标志＋模式标签——标签仅
 *     证明评估 API 无模式通道）。替身输出只验证契约与决策逻辑，
 *     **不构成碰撞算法正确性证明**（真实算法数值由真实后端解析算例
 *     与后续 analytic-case 数据集承担）。
 *   - 策略阈值说明：真实后端解析算例使用 safetyClearance=0（§4.3/§7.4
 *     "m＝0 退化为仅碰撞检测"的合法发布形态——内置二值碰撞后端无距离
 *     查询能力，见 §15.3 P-POL-11）；边界值决策表以脚本化距离后端承载
 *     全部 d/m 组合（含 m＞0），两种设施互为补充。
 *   - 本套件为**集成模式专属**（消费 rw 非模板类 WorkCell/Device/
 *     SerialDevice/CollisionStrategy——冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <rw/common/Exception.hpp>
#include <rw/common/macros.hpp>
#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/CollisionStrategy.hpp>
#include <rw/proximity/DistanceStrategy.hpp>
#include <rw/proximity/ProximityModel.hpp>
#include <rw/proximity/ProximityStrategyData.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性，CollisionSessionTest 同款）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 受控替身（test-local——POL-TD-1 边界见文件头）。
// =====================================================================

/// 手工构造的有效对象身份（首字节打标——字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 手工构造的非全零内容身份（场景/名称映射身份替身——CR-04 值传递契约）。
core::ContentIdentity taggedContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

/**
 * @brief 名称映射替身（IPolicyNameContext 测试实现——CR-04 适配语义；
 *        CollisionSessionTest 同款结构）。
 */
class TestNameContext final : public IPolicyNameContext {
public:
    std::map<std::string, core::ObjectId> byName;   ///< 运行时名→对象
    std::map<core::ObjectId, std::string> byId;     ///< 对象→运行时名
    core::ContentIdentity mapIdentity;              ///< nameMapContentIdentity 应答

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

/**
 * @brief 修订闭包替身（IPolicyValidationContext 测试实现——发布路径应答）。
 */
class TestValidationContext final : public IPolicyValidationContext {
public:
    std::map<core::ObjectId, bool> existingObjects;   ///< objectExists 应答表
    std::map<core::ObjectId, std::string> roles;      ///< objectRole 应答表
    std::map<std::string, bool> definedGroups;        ///< groupDefined 应答表

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

    bool groupDefined(std::string_view groupName) const override
    {
        const auto it = definedGroups.find(std::string{groupName});
        return it != definedGroups.end() && it->second;
    }
};

/**
 * @brief 调用上下文替身（IPolicyCallContext 测试实现——协作取消脚本＋
 *        存活标志＋模式标签）。
 *
 * 取消脚本语义：cancellationRequested() 第 1..cancelAfterQueries 次调用
 * 返回 false，此后恒 true——evaluate 在每个样本边界恰查询一次，故
 * cancelAfterQueries=N ⇔ 样本 0..N-1 执行、样本 N 起取消命中（POL-EVAL-6
 * "第 N 样本后 cancellationRequested=true"的注入点）。
 * modeLabel 仅为"模式标签"载体演示——评估 API 无模式参数（R-POL-5），
 * 标签变化不得影响任何输出（POL-EVAL-8②）。
 */
class ScriptedCallContext final : public IPolicyCallContext {
public:
    std::size_t cancelAfterQueries = std::numeric_limits<std::size_t>::max();
    bool aliveFlag = true;
    std::string modeLabel;   ///< 仅标签——API 无模式通道的对照物
    mutable std::size_t queryCount = 0;

    bool cancellationRequested() const override
    {
        ++queryCount;
        return queryCount > cancelAfterQueries;
    }

    bool alive() const override { return aliveFlag; }
};

/**
 * @brief 碰撞后端替身（CollisionStrategy 测试实现——碰撞应答脚本化）。
 *
 * 脚本面：collisionScript 为 FIFO 应答队列（耗尽后回退 defaultCollision）；
 * throwInCollision 置位时 doInCollision 抛真实 rw::common::Exception
 * （POL-EVAL-5 的故障注入点）。模型管理沿用基类 ProximityStrategy 的
 * frame→model 装配（createModel/addGeometry 由本类承载——记录几何注册
 * 以核对会话构建期的注册行为）。**替身不冒充碰撞算法**——应答为脚本值。
 */
class ScriptedCollisionBackend : public rw::proximity::CollisionStrategy {
public:
    std::deque<bool> collisionScript;   ///< FIFO 碰撞应答脚本（耗尽→defaultCollision）
    bool defaultCollision = false;      ///< 缺省碰撞应答（无脚本时）
    bool throwInCollision = false;      ///< 故障注入：doInCollision 抛 rw 异常
    std::vector<std::string> registeredGeometryIds;   ///< 已注册几何 id（核对用）

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
                     rw::core::Ptr<rw::geometry::Geometry> geom, bool) override
    {
        if (geom) {
            registeredGeometryIds.push_back(geom->getId());
        }
        return true;
    }

    bool removeGeometry(rw::proximity::ProximityModel*, const std::string&) override
    {
        return true;
    }

    std::vector<std::string> getGeometryIDs(rw::proximity::ProximityModel*) override
    {
        return registeredGeometryIds;
    }

    std::vector<rw::core::Ptr<rw::geometry::Geometry>>
    getGeometrys(rw::proximity::ProximityModel*) override
    {
        return {};
    }

    void clear() override { registeredGeometryIds.clear(); }

    bool doInCollision(rw::proximity::ProximityModel::Ptr, const rw::math::Transform3D<>&,
                       rw::proximity::ProximityModel::Ptr, const rw::math::Transform3D<>&,
                       rw::proximity::ProximityStrategyData&) override
    {
        if (throwInCollision) {
            // 真实基线异常类型（§6.3"RobWork 异常必须捕获"的注入面）。
            RW_THROW("ScriptedCollisionBackend: scripted detector fault (POL-EVAL-5)");
        }
        if (!collisionScript.empty()) {
            const bool answer = collisionScript.front();
            collisionScript.pop_front();
            return answer;
        }
        return defaultCollision;
    }

    void getCollisionContacts(std::vector<rw::proximity::CollisionStrategy::Contact>&,
                              rw::proximity::ProximityStrategyData&) override
    {
        // 评估实现不消费接触点（penetrationDepth 显式空——不伪造）；
        // 越界调用＝替身边界违约（显性失败——POL-TD-1）。
        throw std::logic_error("ScriptedCollisionBackend 替身边界：getCollisionContacts 不被评估消费");
    }
};

/**
 * @brief 距离能力后端替身（ScriptedCollisionBackend＋DistanceStrategy——
 *        RobWork 多接口策略惯用法；距离能力探测 dynamic_cast 的命中面）。
 *
 * 距离脚本：distanceScript 为 FIFO 实测值队列（耗尽→defaultDistance）；
 * 可注入 NaN/±Inf（§7.4 行 5 的故障注入点）。**脚本实测值只承载决策表
 * 的输入，不是几何真值**——边界值决策表验证的是评估器的判定逻辑。
 */
class ScriptedDistanceBackend final : public ScriptedCollisionBackend,
                                      public rw::proximity::DistanceStrategy {
public:
    std::deque<double> distanceScript;   ///< FIFO 实测距离脚本（m；耗尽→defaultDistance）
    double defaultDistance = 1.0;        ///< 缺省实测距离（m——远大于任何阈值）

    rw::proximity::DistanceStrategy::Result& doDistance(rw::proximity::ProximityModel::Ptr,
                                                        const rw::math::Transform3D<>&,
                                                        rw::proximity::ProximityModel::Ptr,
                                                        const rw::math::Transform3D<>&,
                                                        rw::proximity::ProximityStrategyData&) override
    {
        m_lastResult = rw::proximity::DistanceStrategy::Result{};
        if (!distanceScript.empty()) {
            m_lastResult.distance = distanceScript.front();
            distanceScript.pop_front();
        }
        else {
            m_lastResult.distance = defaultDistance;
        }
        return m_lastResult;
    }

private:
    rw::proximity::DistanceStrategy::Result m_lastResult;   ///< 结果载体（doDistance 返回引用）
};

// =====================================================================
// 真实后端解析算例装置（1 DOF 串链＋双 0.2 m 立方体——解析已知答案）。
// =====================================================================

/// 解析算例装置的对象身份集（字节打标——规范序：toolBox＜env1＜env2）。
struct RigIds {
    core::ObjectId toolBox = taggedObjectId(0x21);   ///< 工具侧立方体（RobotLink）
    core::ObjectId env1 = taggedObjectId(0x22);      ///< 环境立方体 1（EnvironmentObject）
    core::ObjectId env2 = taggedObjectId(0x23);      ///< 环境立方体 2（EnvironmentObject）
    core::ObjectId device = taggedObjectId(0x29);    ///< 主链设备对象
};

/**
 * @brief 碰撞几何规格（0.2 m 立方体；q=0 时工具盒与环境盒重合——相交；
 *        q=π 时工具盒中心随 J1 旋至 (-0.5,0,0)——与环境盒间隙 0.8 m）。
 */
inline constexpr double kBoxSize = 0.2;   ///< 立方体全边长（SI m）
inline constexpr double kArmLength = 0.5; ///< J1→Tool 固定偏移（SI m，X 向）
inline constexpr double kClearQ = 3.141592653589793;  ///< 分离构型（rad，≈π）

/**
 * @brief 构造解析算例装置（真实 WorkCell＋SerialDevice＋RigidObject 几何）。
 *
 * 运动学：World→Base(恒等)→J1(RevoluteJoint, Z 轴)→Tool(偏移 kArmLength)；
 * 环境盒挂接 World 下 "EnvBox1"/"EnvBox2"（同位 (kArmLength,0,0)）。
 * 解析答案：q=0 → 工具盒与环境盒重合（相交）；q=kClearQ → 中心距
 * 2·kArmLength−…即 1.0 m，盒间隙 1.0−kBoxSize＝0.8 m（清晰）。
 *
 * @param withEnv2    [in] 是否装配第二个环境盒（多发现用例）
 * @param withObjects [in] 是否装配碰撞几何 Object（false＝声明有几何而产物
 *                    无 Object——POL-EVAL-9 的缺口注入面）
 * @param declareEnv2Geometry [in] env2 的场景声明 hasCollisionGeometry 取值
 */
struct CollisionRig
{
    RigIds ids;
    std::shared_ptr<rw::models::WorkCell> workcell;
    CollisionScene scene;
    TestNameContext names;
    TestValidationContext validation;
};

CollisionRig makeRig(bool withEnv2 = true, bool withObjects = true,
                     bool declareEnv2Geometry = true)
{
    CollisionRig rig;
    const RigIds ids;
    rig.ids = ids;

    rig.workcell = std::make_shared<rw::models::WorkCell>("PolicyEvalRigWC");
    // 运动学链：Base→J1（Z 轴转动）→Tool（X 向偏移 kArmLength）。
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
    // 环境盒帧（World 直挂——静态场景事实）。
    rw::core::Ptr<rw::kinematics::FixedFrame> env1Frame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame("EnvBox1", rw::math::Transform3D<>(
                                                      rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(env1Frame);
    rw::core::Ptr<rw::kinematics::FixedFrame> env2Frame;
    if (withEnv2) {
        env2Frame = rw::core::ownedPtr(
            new rw::kinematics::FixedFrame("EnvBox2", rw::math::Transform3D<>(
                                                          rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
        rig.workcell->addFrame(env2Frame);
    }
    // 主链设备（1 自由度——构型维度校验的锚）。
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));

    // 碰撞几何（RigidObject 承载——编译产物以 Object 挂接几何的形态）。
    if (withObjects) {
        const auto attachBox = [&](const rw::kinematics::Frame::Ptr& frame) {
            rw::core::Ptr<rw::models::RigidObject> object =
                rw::core::ownedPtr(new rw::models::RigidObject(frame));
            object->addGeometry(rw::core::ownedPtr(new rw::geometry::Geometry(
                rw::core::ownedPtr(new rw::geometry::Box(kBoxSize, kBoxSize, kBoxSize)))));
            rig.workcell->add(object);   // WorkCell::add(Ptr<Object>)——产物 Object 登记
        };
        attachBox(tool);
        attachBox(env1Frame);
        if (withEnv2) {
            attachBox(env2Frame);   // env2Frame 在 withEnv2 分支内非空
        }
    }

    // 场景装配（CR-04——调用方自编译产物装配）。
    rig.scene.workcell = rig.workcell;
    rig.scene.primaryDevice = ids.device;
    rig.scene.objects = {
        {ids.toolBox, "ToolBox", SceneObjectRole::RobotLink, true},
        {ids.env1, "EnvBox1", SceneObjectRole::EnvironmentObject, true},
    };
    if (withEnv2) {
        rig.scene.objects.push_back(
            {ids.env2, "EnvBox2", SceneObjectRole::EnvironmentObject, declareEnv2Geometry});
    }
    rig.scene.sceneContentIdentity = taggedContentIdentity(0xC7);

    // 名称映射（整名——帧名与设备名）。
    rig.names.mapIdentity = taggedContentIdentity(0x6E);
    rig.names.byName["Robot"] = ids.device;
    rig.names.byId[ids.device] = "Robot";
    rig.names.byId[ids.toolBox] = "Tool";
    rig.names.byId[ids.env1] = "EnvBox1";
    rig.names.byId[ids.env2] = "EnvBox2";

    // 发布闭包应答（全部对象存在＋角色与场景一致）。
    const std::pair<const core::ObjectId*, const char*> entries[] = {
        {&ids.toolBox, "RobotLink"},   {&ids.env1, "EnvironmentObject"},
        {&ids.env2, "EnvironmentObject"}, {&ids.device, "RobotLink"},
    };
    for (const auto& [id, role] : entries) {
        rig.validation.existingObjects[*id] = true;
        rig.validation.roles[*id] = role;
    }
    return rig;
}

/**
 * @brief 发布解析算例策略（safetyClearance 可参数化——0＝仅碰撞检测）。
 */
EngineeringPolicySet publishRigPolicy(const CollisionRig& rig, double safetyClearance)
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = taggedObjectId(0x20);
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{safetyClearance, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{12.566370614359172, "rad"};
    const PolicyParseResult res = resolvePolicy(in, rig.validation);
    if (!res.policy.has_value()) {
        throw std::runtime_error("夹具策略应可发布，首条诊断: "
                                 + (res.diagnostics.empty() ? std::string{"-"}
                                                            : res.diagnostics.front().code));
    }
    return *res.policy;
}

/// 直构脚本化会话（注入替身后端——描述符为测试值，非复现要素主张）。
/// 所有权形态：替身后端以 std::shared_ptr 共享持有并静态向上转换——
/// （rw::core::Ptr 的非 owning 形态经转换会丢失所有权语义，实测产生空
/// 实例——此处以共享所有权保证会话存续期后端保活）。
std::shared_ptr<const CollisionEvaluationSession>
buildScriptedSession(const CollisionRig& rig, const EngineeringPolicySet& policy,
                     const std::shared_ptr<ScriptedCollisionBackend>& backend)
{
    CollisionBackendDescriptor descriptor;
    descriptor.backendId = "test.scripted-backend";
    descriptor.backendVersion = "0.0-test";
    descriptor.toleranceModel = "scripted/test-double(not-a-real-backend)";
    return std::make_shared<const CollisionEvaluationSession>(policy, rig.scene, rig.names,
                                                              descriptor, backend);
}

/// 构造 SingleState 查询（单构型便捷；按值返回非 const——调用方可再设标志）。
CollisionQuery singleStateQuery(double q)
{
    CollisionQuery query;
    query.kind = CollisionQueryKind::SingleState;
    query.configurations.push_back(rw::math::Q(1, q));
    return query;
}

/// 在诊断清单中查找指定建议码（存在性断言辅助）。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diagnostics, std::string_view code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const core::DiagnosticRecord& d) { return d.code == code; });
}

}  // namespace

// =====================================================================
// POL-EVAL-1 构型级输出（KIN-02/C8/AT-03）——真实后端解析算例。
// =====================================================================

/**
 * POL-EVAL-1（KIN-02/C8/AT-03）：两组构型——q=0 工具盒与环境盒重合
 * （相交）、q≈π 分离（间隙 0.8 m）。SingleState 评估分别检出/无发现；
 * 输出无任务级结论字段（类型断言——CollisionEvaluation 不含
 * Feasible/EngineeringInfeasible 语义成员，§8.3"policy 不得自行发布正式
 * 工程结论"；任务级判定归 evidence）。内置后端 m=0（仅碰撞检测——
 * §7.4"m＝0"行的合法形态）。
 */
TEST(CollisionEvaluation, SingleStateDetectsAndClearsAnalyticPair_POL_EVAL_1)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);   // 单几何对——解析答案逐对断言
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    // 相交构型：检出碰撞（解析答案——两盒重合）。
    const CollisionEvaluation hit = session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
    ASSERT_EQ(hit.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(hit.finalized) << "Completed 恒 finalized=true（§6.3）";
    EXPECT_EQ(hit.applicability, ScopeApplicability::Applicable);
    ASSERT_EQ(hit.findings.size(), std::size_t{1});
    const CollisionFinding& finding = hit.findings.front();
    // 对象 ID 对：规范序（A<B——NFR-COR-05）＋整名辅助＋构型级事实。
    EXPECT_EQ(finding.objectA, rig.ids.toolBox);
    EXPECT_EQ(finding.objectB, rig.ids.env1);
    EXPECT_EQ(finding.runtimeNameA, "Tool");
    EXPECT_EQ(finding.runtimeNameB, "EnvBox1");
    EXPECT_EQ(finding.sampleIndex, std::size_t{0});
    EXPECT_EQ(finding.kind, CollisionFindingKind::Collision);
    EXPECT_EQ(finding.pathParameter, std::nullopt) << "SingleState 无路径语义";
    EXPECT_EQ(finding.measuredClearance, std::nullopt) << "碰撞发现不携带间距";
    EXPECT_EQ(finding.penetrationDepth, std::nullopt)
        << "穿透深度后端不可提供＝显式空（不伪造——§6.2）";
    EXPECT_EQ(finding.level, PolicyRuleLevel::Must) << "域默认必检＝Must（§6.2）";
    // 覆盖事实：作用域 1 对、双端有几何、已检 1 对、无过滤。
    EXPECT_EQ(hit.coverage.pairsInScope, std::uint64_t{1});
    EXPECT_EQ(hit.coverage.pairsWithGeometry, std::uint64_t{1});
    EXPECT_EQ(hit.coverage.pairsEvaluated, std::uint64_t{1});
    EXPECT_EQ(hit.coverage.pairsExcludedByRule, std::uint64_t{0});
    EXPECT_TRUE(hit.coverage.excluded.empty());
    EXPECT_TRUE(hit.diagnostics.empty());
    // 类型断言（§8.3——编译期钉住：评估输出不存在任务级结论字段）：
    // CollisionEvaluation 的成员清单见 CollisionQuery.hpp——无
    // feasible/infeasible/conclusion 类成员；此处以编译期绑定复核成员数
    // （结构防扩展等价于结构防越权）。
    CollisionEvaluation& mutableProbe = const_cast<CollisionEvaluation&>(hit);
    auto& [status, applicability, policyCid, sceneCid, nameMapCid, findings, minDistances,
           coverage, appliedFilters, diagnostics, finalized] = mutableProbe;
    static_cast<void>(status);
    static_cast<void>(applicability);
    static_cast<void>(policyCid);
    static_cast<void>(sceneCid);
    static_cast<void>(nameMapCid);
    static_cast<void>(findings);
    static_cast<void>(minDistances);
    static_cast<void>(coverage);
    static_cast<void>(appliedFilters);
    static_cast<void>(diagnostics);
    static_cast<void>(finalized);
    EXPECT_TRUE(minDistances == std::nullopt) << "未请求最小距离＝不给出（§6.2）";

    // 分离构型：无发现（解析答案——间隙 0.8 m）；"未检出碰撞"仅指已检
    // 样本与作用域（coverage 为凭——§6.3 Completed（无发现）行）。
    const CollisionEvaluation clear =
        session->evaluate(singleStateQuery(kClearQ), ScriptedCallContext{});
    ASSERT_EQ(clear.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(clear.finalized);
    EXPECT_TRUE(clear.findings.empty()) << "分离构型无碰撞发现";
    EXPECT_EQ(clear.coverage.pairsEvaluated, std::uint64_t{1});
    EXPECT_TRUE(clear.diagnostics.empty());
}

/**
 * POL-EVAL-3（§8.1 表 2③/C8）：必经状态碰撞发现的证明素材字段齐备——
 * CollisionFinding 的对象 ID 对＋判定＋采样位置（＋段内定位）字段构成
 * evidence DeterministicInfeasibilityProof.MandatoryStateCollision.
 * collisionPairs 的映射素材（§8.4——绑定由调用方完成，policy 供素材）。
 */
TEST(CollisionEvaluation, MandatoryStateFindingCarriesProofMaterial_POL_EVAL_3)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    const CollisionEvaluation hit = session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
    ASSERT_EQ(hit.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(hit.findings.size(), std::size_t{1});
    const CollisionFinding& f = hit.findings.front();
    // 证明素材三件套逐字段核对（§8.4 映射注释逐项）：对象 ID 对（身份—
    // 非名称）、判定（kind）、采样位置（sampleIndex；路径段内另由
    // pathParameter 承载——POL-EVAL-4 单测）。
    EXPECT_TRUE(f.objectA.isValid() && f.objectB.isValid()) << "对象 ID 对有效（非全零保留值）";
    EXPECT_EQ(f.kind, CollisionFindingKind::Collision);
    EXPECT_EQ(f.sampleIndex, std::size_t{0});
    // 判定依据绑定素材随评估回填（§8.4——快照/运行身份归调用方绑定）。
    EXPECT_EQ(hit.policyContentIdentity, policy.contentIdentity);
    EXPECT_EQ(hit.sceneIdentity, rig.scene.sceneContentIdentity);
    EXPECT_EQ(hit.nameMapIdentity, rig.names.mapIdentity);
}

// =====================================================================
// POL-EVAL-2 全候选碰撞（C5/C8/AT-03）——SampleSet。
// =====================================================================

/**
 * POL-EVAL-2（C5/C8/AT-03）：全部候选构型碰撞——policy 仅输出发现集合；
 * "搜索未果/DataInsufficient"由消费方（kinematics→evidence）表达——
 * 评估器无该输出通道（类型层无不可行字段，见 POL-EVAL-1 的结构断言）。
 */
TEST(CollisionEvaluation, SampleSetAllCollidingYieldsFindingsOnly_POL_EVAL_2)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);   // 单几何对——3 样本×1 对=3 发现
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    CollisionQuery query;
    query.kind = CollisionQueryKind::SampleSet;
    query.configurations = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.0), rw::math::Q(1, 0.0)};
    const CollisionEvaluation result = session->evaluate(query, ScriptedCallContext{});

    ASSERT_EQ(result.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(result.finalized);
    ASSERT_EQ(result.findings.size(), std::size_t{3});
    // 逐样本独立归档（KIN-04 分母统计素材——sampleIndex 0/1/2）。
    for (std::size_t i = 0; i < result.findings.size(); ++i) {
        EXPECT_EQ(result.findings[i].sampleIndex, i) << "SampleSet 按样本索引独立归档";
        EXPECT_EQ(result.findings[i].kind, CollisionFindingKind::Collision);
    }
    EXPECT_TRUE(result.diagnostics.empty());
}

// =====================================================================
// POL-EVAL-4 路径 vs 构型输出差异（TRJ-04）。
// =====================================================================

/**
 * POL-EVAL-4（TRJ-04）：同一几何对在 PathSequence 与 SampleSet 查询——
 * PathSequence 发现携带 pathParameter 且保序（样本 0 碰撞、样本 1 清晰、
 * 样本 2 碰撞——pathParameters {0.0, 0.5, 1.0} 与 configurations 等长）；
 * SampleSet 按索引独立（pathParameter 恒 nullopt）。
 */
TEST(CollisionEvaluation, PathSequenceKeepsOrderAndPathParameter_POL_EVAL_4)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);   // 单几何对——保序断言清晰
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    // PathSequence：端点碰撞＋中段清晰（TRJ-04 复检的段内定位语义）。
    CollisionQuery path;
    path.kind = CollisionQueryKind::PathSequence;
    path.configurations = {rw::math::Q(1, 0.0), rw::math::Q(1, kClearQ), rw::math::Q(1, 0.0)};
    path.pathParameters = {0.0, 0.5, 1.0};
    const CollisionEvaluation pathResult = session->evaluate(path, ScriptedCallContext{});
    ASSERT_EQ(pathResult.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(pathResult.findings.size(), std::size_t{2});
    // 保序：sampleIndex 升序（0 前于 2）；段内定位随样本。
    EXPECT_EQ(pathResult.findings[0].sampleIndex, std::size_t{0});
    EXPECT_EQ(pathResult.findings[0].pathParameter, std::optional<double>(0.0));
    EXPECT_EQ(pathResult.findings[1].sampleIndex, std::size_t{2});
    EXPECT_EQ(pathResult.findings[1].pathParameter, std::optional<double>(1.0));

    // 对照：同几何对同构型的 SampleSet——pathParameter 恒 nullopt（按索
    // 引独立归档，无路径语义）。
    CollisionQuery sampleSet;
    sampleSet.kind = CollisionQueryKind::SampleSet;
    sampleSet.configurations = path.configurations;
    const CollisionEvaluation sampleResult = session->evaluate(sampleSet, ScriptedCallContext{});
    ASSERT_EQ(sampleResult.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(sampleResult.findings.size(), std::size_t{2});
    for (const CollisionFinding& f : sampleResult.findings) {
        EXPECT_EQ(f.pathParameter, std::nullopt) << "SampleSet 无路径参数";
    }
}

// =====================================================================
// POL-EVAL-5 失败≠碰撞（KIN-05/EVI）——故障注入。
// =====================================================================

/**
 * POL-EVAL-5（KIN-05/EVI）：替身后端抛真实 rw::common::Exception——评估
 * 内部捕获转 Failed＋POLICY-CLL-EVALUATION-FAILED，findings 不含该对，
 * finalized=false（非终态——不得采信为"无碰撞"，evidence Invalid/Missing
 * 口径）；无"无碰撞"字段产生（类型层保证）。
 */
TEST(CollisionEvaluation, BackendExceptionBecomesFailedNotCollision_POL_EVAL_5)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    auto backend = std::make_shared<ScriptedCollisionBackend>();
    backend->throwInCollision = true;   // 故障注入（真实 rw 异常类型）
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildScriptedSession(rig, policy, backend);

    const CollisionEvaluation result = session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
    EXPECT_EQ(result.status, CollisionEvaluationStatus::Failed);
    EXPECT_FALSE(result.finalized) << "Failed 恒 finalized=false（§6.3）";
    EXPECT_TRUE(result.findings.empty()) << "异常对不入 findings（该对未判定）";
    EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{0}) << "后端查询未完成不计数";
    ASSERT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-EVALUATION-FAILED"));
    // 诊断可定位（cause 携带注入点原文——ERR-01 可追溯；不吞不崩）。
    bool causeHasScript = false;
    for (const core::DiagnosticRecord& d : result.diagnostics) {
        if (d.code == "POLICY-CLL-EVALUATION-FAILED"
            && std::string(d.cause).find("ScriptedCollisionBackend") != std::string::npos) {
            causeHasScript = true;
        }
    }
    EXPECT_TRUE(causeHasScript) << "异常原文保留于诊断 cause";
}

// =====================================================================
// POL-EVAL-6 取消不产正式证据（TASK-02/UX-03）。
// =====================================================================

/**
 * POL-EVAL-6（TASK-02/UX-03）：第 2 样本边界后 cancellationRequested=true
 * ——status=Canceled＋部分 findings（样本 0/1）＋finalized=false；无错误
 * 诊断（正常取消≠失败——UX-03）。
 */
TEST(CollisionEvaluation, CancellationKeepsPartialFindingsWithoutErrorDiag_POL_EVAL_6)
{
    // 单几何对装置（withEnv2=false）——部分发现的样本定位断言不受第二对
    // 干扰（样本 0/1 各一条、共 2 条）。
    const CollisionRig rig = makeRig(/*withEnv2=*/false);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    // 5 样本全碰撞；取消脚本＝第 2 次边界查询起命中（样本 0/1 已执行）。
    ScriptedCallContext ctx;
    ctx.cancelAfterQueries = 2;
    CollisionQuery query;
    query.kind = CollisionQueryKind::PathSequence;
    query.configurations = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.0), rw::math::Q(1, 0.0),
                            rw::math::Q(1, 0.0), rw::math::Q(1, 0.0)};
    query.pathParameters = {0.0, 0.25, 0.5, 0.75, 1.0};
    const CollisionEvaluation result = session->evaluate(query, ctx);

    EXPECT_EQ(result.status, CollisionEvaluationStatus::Canceled);
    EXPECT_FALSE(result.finalized) << "Canceled 恒 finalized=false——取消结果不入正式报告"
                                      "/可行集/正式缓存（TASK-02/CON-04）";
    ASSERT_EQ(result.findings.size(), std::size_t{2}) << "已产生部分（样本 0/1）保留";
    EXPECT_EQ(result.findings[0].sampleIndex, std::size_t{0});
    EXPECT_EQ(result.findings[1].sampleIndex, std::size_t{1});
    EXPECT_TRUE(result.diagnostics.empty()) << "正常取消不产生错误诊断（UX-03）";
    // 覆盖事实如实：已检 2 对（同几何对的样本 0/1），未检部分不虚报。
    EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{1});
}

// =====================================================================
// POL-EVAL-7 稳定排序（NFR-COR-05/AT-19）＋POL-CONC-1 并发只读。
// =====================================================================

/**
 * POL-EVAL-7（NFR-COR-05/AT-19）：同会话同查询评估 3 次——findings/
 * appliedFilters/coverage 逐字段一致（checkStableOrder 口径的载体断言；
 * 多线程分派见 POL-CONC-1）。多发现序＝(sampleIndex, 对象对字典序)。
 */
TEST(CollisionEvaluation, RepeatedEvaluationsAreFieldIdentical_POL_EVAL_7)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/true);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    // 相交构型：双环境盒→单样本 2 发现（对象对字典序 env1(0x22)＜env2(0x23)）。
    const CollisionQuery query = singleStateQuery(0.0);
    const CollisionEvaluation first = session->evaluate(query, ScriptedCallContext{});
    const CollisionEvaluation second = session->evaluate(query, ScriptedCallContext{});
    const CollisionEvaluation third = session->evaluate(query, ScriptedCallContext{});

    EXPECT_EQ(first, second) << "同 (会话, 查询, 上下文存活) → 逐字段等价（§6.4）";
    EXPECT_EQ(second, third);
    EXPECT_EQ(first, third);
    // 多发现稳定序：对象对字典序（env1 对前于 env2 对）。
    ASSERT_EQ(first.findings.size(), std::size_t{2});
    EXPECT_EQ(first.findings[0].objectB, rig.ids.env1);
    EXPECT_EQ(first.findings[1].objectB, rig.ids.env2);
    // 过滤留痕为空集（无排除规则）——appliedFilters 稳定序在排除场景下
    // 由 scope 规范序保证（本套件以 coverage.excluded 全量输出承载）。
    EXPECT_TRUE(first.appliedFilters.empty());
}

/**
 * POL-CONC-1（NFR-COR-02）：多线程对同一会话并发 evaluate（每线程独立
 * 上下文——§9.7"每运行/调用一个实例"）——输出逐字段相等（真实内置后端
 * ＋真实几何；查询互斥串行化后端访问——并发只读可重入的执行侧证明）。
 */
TEST(CollisionEvaluation, ConcurrentEvaluateOnSharedSessionIsIdentical_POL_CONC_1)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/true);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    const CollisionQuery query = singleStateQuery(0.0);
    std::vector<CollisionEvaluation> results(4);
    std::vector<std::thread> workers;
    for (std::size_t i = 0; i < results.size(); ++i) {
        workers.emplace_back([&session, &query, &results, i]() {
            // 每运行/调用一个上下文实例（§9.7）——并发分派的契约形态。
            results[i] = session->evaluate(query, ScriptedCallContext{});
        });
    }
    for (std::thread& w : workers) {
        w.join();
    }
    for (std::size_t i = 1; i < results.size(); ++i) {
        EXPECT_EQ(results[i], results[0]) << "并发结果逐字段相等（POL-CONC-1）";
    }
    ASSERT_EQ(results[0].findings.size(), std::size_t{2}) << "并发不丢发现";
    EXPECT_TRUE(results[0].finalized);
}

// =====================================================================
// POL-EVAL-8 无隐式阈值/无模式影响（KIN-13/R-POL-3/5）＋§7.4 边界值决策表。
// =====================================================================

/**
 * POL-EVAL-8① 边界值决策表（§7.4——脚本化距离后端承载全部 d/m 组合；
 * 后端二值碰撞与实测距离分离注入——决策逻辑与后端解耦的机械验证）：
 *   d＞m 清晰；d＝m 清晰（边界含于安全侧——D-08）；0≤d＜m MarginViolation
 *   （measuredClearance=d）；后端相交＝Collision（不查距离）；d＝NaN 评估
 *   失败（原文保留）；m＝0 退化仅碰撞。
 */
TEST(CollisionEvaluation, BoundaryDecisionTableByScriptedDistance_POL_EVAL_8)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/false);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.02);   // m＝0.02 m
    auto backend = std::make_shared<ScriptedDistanceBackend>();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildScriptedSession(rig, policy, backend);
    CollisionQuery query = singleStateQuery(0.0);
    query.requestMinDistance = true;   // 逐样本摘要随间距检查一起激活（§6.2）
    ScriptedCallContext ctx;

    // —— 行 1：d＞m（0.5＞0.02）→ 无发现；minDistance 报告值保留 ——
    backend->distanceScript = {0.5};
    CollisionEvaluation r = session->evaluate(query, ctx);
    ASSERT_EQ(r.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(r.findings.empty());
    ASSERT_TRUE(r.minDistances.has_value());
    ASSERT_EQ(r.minDistances->size(), std::size_t{1});
    EXPECT_EQ(r.minDistances->front().minDistance, std::optional<double>(0.5));
    EXPECT_EQ(r.minDistances->front().nearestToA, std::optional<core::ObjectId>(rig.ids.toolBox));
    EXPECT_EQ(r.minDistances->front().nearestToB, std::optional<core::ObjectId>(rig.ids.env1));

    // —— 行 2：d＝m（0.02＝0.02）→ 清晰（边界含于安全侧——D-08）——
    backend->distanceScript = {0.02};
    r = session->evaluate(query, ctx);
    ASSERT_EQ(r.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(r.findings.empty()) << "d＝m 不得产出间距不足发现（§7.4 行 2）";

    // —— 行 3：0≤d＜m（0.01＜0.02）→ MarginViolation，measuredClearance=d ——
    backend->distanceScript = {0.01};
    r = session->evaluate(query, ctx);
    ASSERT_EQ(r.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(r.findings.size(), std::size_t{1});
    EXPECT_EQ(r.findings.front().kind, CollisionFindingKind::SafetyMarginViolation);
    EXPECT_EQ(r.findings.front().measuredClearance, std::optional<double>(0.01));
    EXPECT_EQ(r.findings.front().level, PolicyRuleLevel::Must) << "域默认＝Must";

    // —— 行 4：后端报告相交（含接触）→ Collision；距离不被查询（脚本
    //    队列保持未消费——碰撞与距离二分的机械证明）——
    backend->collisionScript = {true};
    backend->distanceScript = {0.01};   // 若被误查将消费队列元素
    r = session->evaluate(query, ctx);
    ASSERT_EQ(r.status, CollisionEvaluationStatus::Completed);
    ASSERT_EQ(r.findings.size(), std::size_t{1});
    EXPECT_EQ(r.findings.front().kind, CollisionFindingKind::Collision);
    EXPECT_EQ(r.findings.front().measuredClearance, std::nullopt);
    EXPECT_EQ(backend->distanceScript.size(), std::size_t{1})
        << "碰撞对不做距离查询——脚本元素未消费即互斥的机械证明";

    // —— 行 5：d＝NaN → 评估失败＋POLICY-CLL-EVALUATION-FAILED（原文保留）——
    backend->collisionScript.clear();
    backend->distanceScript = {std::numeric_limits<double>::quiet_NaN()};
    r = session->evaluate(query, ctx);
    EXPECT_EQ(r.status, CollisionEvaluationStatus::Failed);
    EXPECT_FALSE(r.finalized);
    EXPECT_TRUE(r.findings.empty()) << "非有限实测值不入 findings（§7.5）";
    ASSERT_TRUE(hasDiagnostic(r.diagnostics, "POLICY-CLL-EVALUATION-FAILED"));
    for (const core::DiagnosticRecord& d : r.diagnostics) {
        if (d.code == "POLICY-CLL-EVALUATION-FAILED") {
            EXPECT_NE(std::string(d.cause).find("nan"), std::string::npos)
                << "非有限值以原文保留（%.17g）: " << d.cause;
        }
    }

    // —— 行 6：m＝0 → 间距检查退化（d≥0 恒清晰）；仅碰撞检测 ——（以无
    //    距离能力的二值后端承载：若 m=0 被误判为间距检查激活，能力核对
    //    即以 DETECTOR-UNAVAILABLE 失败暴露）——
    const EngineeringPolicySet zeroClearance = publishRigPolicy(rig, 0.0);
    auto zeroBackend = std::make_shared<ScriptedCollisionBackend>();   // 无距离能力
    const std::shared_ptr<const CollisionEvaluationSession> zeroSession =
        buildScriptedSession(rig, zeroClearance, zeroBackend);
    // 纯净查询（不请求最小距离）——m=0 且无距离诉求＝纯碰撞评估。
    r = zeroSession->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
    ASSERT_EQ(r.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(r.findings.empty()) << "m＝0 时 d≥0 恒清晰（§7.4 行 6）";
}

/**
 * POL-EVAL-8②（KIN-13/EVI-01/R-POL-5）：模式语境不影响输出——"模式标签"
 * 不同的调用上下文（Preview/Quick/Verified 语境替身）下同查询输出逐字段
 * 相等（模式不是评估输入——API 类型层不存在模式成员）。
 */
TEST(CollisionEvaluation, ModeLabelContextsDoNotChangeOutput_POL_EVAL_8)
{
    const CollisionRig rig = makeRig();
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    ScriptedCallContext preview;
    preview.modeLabel = "Preview";
    ScriptedCallContext quick;
    quick.modeLabel = "Quick";
    ScriptedCallContext verified;
    verified.modeLabel = "Verified";

    const CollisionQuery query = singleStateQuery(0.0);
    const CollisionEvaluation a = session->evaluate(query, preview);
    const CollisionEvaluation b = session->evaluate(query, quick);
    const CollisionEvaluation c = session->evaluate(query, verified);
    EXPECT_EQ(a, b) << "模式标签不影响输出（KIN-13——模式不得隐式改变工程策略）";
    EXPECT_EQ(b, c);
}

/**
 * R-POL-5/D-9 结构防覆盖（任务契约 acceptance 2）：查询载体的成员清单恰
 * 为 §6.2 六字段——结构化绑定按成员数精确匹配，任何新增成员（阈值/模式/
 * 开关）即编译失败＝"结构上不可覆盖策略"的机械保证。
 */
TEST(CollisionQueryContract, QueryCarriesNoThresholdOrModeMembers_R_POL_5_D9)
{
    CollisionQuery query;
    query.kind = CollisionQueryKind::SampleSet;
    // 六成员结构化绑定（第 7 个成员将使本语句编译失败——D-9 结构防覆盖）。
    auto& [kind, configurations, baseState, pathParameters, requestMinDistance,
           stopAtFirstFinding] = query;
    static_cast<void>(kind);
    static_cast<void>(pathParameters);
    EXPECT_TRUE(configurations.empty());
    EXPECT_FALSE(baseState.has_value());
    EXPECT_FALSE(requestMinDistance) << "缺省不请求最小距离";
    EXPECT_FALSE(stopAtFirstFinding) << "缺省不提前终止";
    // 成员缺省值即 §6.2 语义（无任何策略阈值/评估模式的旁路通道）。
}

// =====================================================================
// POL-EVAL-9 几何缺失（KIN-05）。
// =====================================================================

/**
 * POL-EVAL-9（KIN-05）：作用域对象无碰撞几何（场景声明 false 与"声明有
 * 而编译产物无 Object"两形态）——Completed 但 coverage.pairsWithGeometry
 * 缺口＋POLICY-CLL-GEOMETRY-MISSING 告知性诊断；无"无碰撞"结论字段
 * （消费方判数据不足的接口就绪——coverage 显式计数）。
 */
TEST(CollisionEvaluation, GeometryGapKeepsCoverageAndDiagnostic_POL_EVAL_9)
{
    // 形态一：场景声明 env1 无几何（hasCollisionGeometry=false）。
    {
        const CollisionRig rig = makeRig(/*withEnv2=*/false, /*withObjects=*/true,
                                         /*declareEnv2Geometry=*/false);
        // 复位声明：本形态中唯一环境盒 env1 声明为无几何。
        CollisionRig declared = rig;
        declared.scene.objects[1].hasCollisionGeometry = false;
        const EngineeringPolicySet policy = publishRigPolicy(declared, 0.0);
        const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
        const std::shared_ptr<const CollisionEvaluationSession> session =
            evaluator->createSession(policy, declared.scene, declared.names);

        const CollisionEvaluation result =
            session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
        EXPECT_EQ(result.status, CollisionEvaluationStatus::Completed);
        EXPECT_TRUE(result.finalized);
        EXPECT_TRUE(result.findings.empty());
        // 覆盖缺口：作用域 1 对，双端几何 0 对，已检 0 对（不伪装已检）。
        EXPECT_EQ(result.coverage.pairsInScope, std::uint64_t{1});
        EXPECT_EQ(result.coverage.pairsWithGeometry, std::uint64_t{0});
        EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{0});
        EXPECT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-GEOMETRY-MISSING"));
        // 诊断定位到缺口对象（subject 绑定对象 ID——ERR-01）。
        bool subjectLocated = false;
        for (const core::DiagnosticRecord& d : result.diagnostics) {
            if (d.code == "POLICY-CLL-GEOMETRY-MISSING" && d.subject.has_value()
                && *d.subject == rig.ids.env1) {
                subjectLocated = true;
            }
        }
        EXPECT_TRUE(subjectLocated) << "缺口对象定位（subject=env1）";
    }

    // 形态二：场景声明有几何而编译产物无 Object（withObjects=false——
    // 注册核对失败＝同码缺口，不伪装已检、不 fail-fast——§7.5 几何缺失行）。
    {
        const CollisionRig rig = makeRig(/*withEnv2=*/false, /*withObjects=*/false);
        const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
        const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
        const std::shared_ptr<const CollisionEvaluationSession> session =
            evaluator->createSession(policy, rig.scene, rig.names);

        const CollisionEvaluation result =
            session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
        EXPECT_EQ(result.status, CollisionEvaluationStatus::Completed);
        EXPECT_EQ(result.coverage.pairsWithGeometry, std::uint64_t{0});
        EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{0});
        EXPECT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-GEOMETRY-MISSING"));
    }
}

// =====================================================================
// POL-EXC-1 RobWork 异常/资源错误/空场景/违约（NFR-COR-03/§6.3）。
// =====================================================================

/**
 * POL-EXC-1（NFR-COR-03/§6.3）：①空作用域场景→EmptyScope（非"无碰撞"——
 * 适用性字段与全零在检计数表达）；②空构型序列/样本数违约→PolicyError
 * （调用方契约违约 fail-fast——§9.3 错误类型行）；③构型维度与设备自由
 * 度不符→同码 fail-fast（setQ 前置条件的先验校验）。
 */
TEST(CollisionEvaluation, EmptyScopeAndMalformedQueries_POL_EXC_1)
{
    // ① 空作用域：策略仅启用 Self 域——环境盒对（Environment 域）全部
    //    不适用→作用域为空→EmptyScope（coverage 在检计数全零）。
    {
        const CollisionRig rig = makeRig();
        RawPolicyInput in;
        in.schemaVersion = PolicySchema::currentVersion;
        in.policyObject = taggedObjectId(0x20);
        in.collision.enabled = true;
        in.collision.enabledDomains = {CollisionDomain::Self};   // 环境对不入作用域
        in.collision.safetyClearance = RawThresholdInput{0.0, "m"};
        in.collision.excludeAdjacentLinksByDefault = true;
        in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
        in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
        in.jointThresholds.finiteRotationTravelLimit =
            RawThresholdInput{12.566370614359172, "rad"};
        const EngineeringPolicySet policy = resolvePolicy(in, rig.validation).policy.value();
        const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
        const std::shared_ptr<const CollisionEvaluationSession> session =
            evaluator->createSession(policy, rig.scene, rig.names);

        const CollisionEvaluation result =
            session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
        EXPECT_EQ(result.status, CollisionEvaluationStatus::Completed);
        EXPECT_EQ(result.applicability, ScopeApplicability::EmptyScope)
            << "空作用域≠无碰撞（§7.1/KIN-05 口径由 coverage 表达）";
        EXPECT_TRUE(result.finalized);
        EXPECT_TRUE(result.findings.empty());
        EXPECT_EQ(result.coverage.pairsInScope, std::uint64_t{0});
        EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{0});
    }

    // ②③ 查询违约矩阵（PolicyError(QueryInvalid)——fail-fast 不走诊断轨）。
    const CollisionRig rig = makeRig();
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);
    ScriptedCallContext ctx;

    // 空构型序列（POL-EXC-1"空构型序列（后者为违约）"）。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::SampleSet;   // configurations 空
        EXPECT_THROW(session->evaluate(query, ctx), PolicyError);
    }
    // SingleState 构型数≠1。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::SingleState;
        query.configurations = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.0)};
        EXPECT_THROW(session->evaluate(query, ctx), PolicyError);
    }
    // PathSequence 路径参数与构型不等长。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::PathSequence;
        query.configurations = {rw::math::Q(1, 0.0), rw::math::Q(1, 0.0)};
        query.pathParameters = {0.0};
        EXPECT_THROW(session->evaluate(query, ctx), PolicyError);
    }
    // SampleSet 携带路径参数（无路径语义）。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::SampleSet;
        query.configurations = {rw::math::Q(1, 0.0)};
        query.pathParameters = {0.0};
        EXPECT_THROW(session->evaluate(query, ctx), PolicyError);
    }
    // 构型维度与设备自由度不符（DOF=1）。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::SingleState;
        query.configurations = {rw::math::Q(2, 0.0)};
        EXPECT_THROW(session->evaluate(query, ctx), PolicyError);
    }
    // 违约码面核对（稳定 token——调用方契约违约的统一载具）。
    {
        CollisionQuery query;
        query.kind = CollisionQueryKind::SampleSet;
        try {
            session->evaluate(query, ctx);
            FAIL() << "空构型序列必须 fail-fast（§9.3）";
        }
        catch (const PolicyError& e) {
            EXPECT_EQ(e.code(), PolicyErrorCode::QueryInvalid);
            EXPECT_EQ(std::string(e.what()).find("policy/cll-query-invalid"), std::size_t{0})
                << "what() 前缀＝稳定 token";
        }
    }
}

/**
 * POL-LATE-1（§6.1/任务约束）：上下文 alive()=false 后 evaluate——Failed
 * ＋POLICY-CLL-CONTEXT-EXPIRED，无正式输出（finalized=false；迟到调用
 * 拒绝——只读生命周期第二层防护的执行点）。
 */
TEST(CollisionEvaluation, ExpiredContextIsRejected_POL_LATE_1)
{
    const CollisionRig rig = makeRig();
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    ScriptedCallContext ctx;
    ctx.aliveFlag = false;   // 快照/运行已结束（execution 置失效）
    const CollisionEvaluation result = session->evaluate(singleStateQuery(0.0), ctx);
    EXPECT_EQ(result.status, CollisionEvaluationStatus::Failed);
    EXPECT_FALSE(result.finalized);
    EXPECT_TRUE(result.findings.empty());
    EXPECT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-CONTEXT-EXPIRED"));
    EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{0}) << "未发生任何后端查询";
}

/**
 * 距离能力保守口径（§6.3"检测器不可用"触发器＋§15.3 P-POL-11）：注入无
 * 距离能力的二值碰撞后端——需要距离的评估（m＞0 的间距检查或
 * requestMinDistance）返回 Failed＋POLICY-CLL-DETECTOR-UNAVAILABLE
 * （KIN-05：缺检测器≠无碰撞——不静默降级为"仅碰撞"输出）；m＝0 且未
 * 请求距离的同后端评估不受影响（能力按需核对）。
 */
TEST(CollisionEvaluation, MissingDistanceCapabilityFailsLoudly_POL_EVAL_8)
{
    const CollisionRig rig = makeRig();
    const EngineeringPolicySet marginPolicy = publishRigPolicy(rig, 0.02);   // m＞0
    auto backend = std::make_shared<ScriptedCollisionBackend>();   // 无距离能力
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildScriptedSession(rig, marginPolicy, backend);
    ScriptedCallContext ctx;

    // m＞0：间距检查需要距离——能力缺失→Failed＋DETECTOR-UNAVAILABLE。
    CollisionEvaluation result = session->evaluate(singleStateQuery(0.0), ctx);
    EXPECT_EQ(result.status, CollisionEvaluationStatus::Failed);
    EXPECT_FALSE(result.finalized);
    EXPECT_TRUE(result.findings.empty()) << "无距离能力不得产出任何间距判定";
    EXPECT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-DETECTOR-UNAVAILABLE"));

    // requestMinDistance=true 且 m=0：摘要承诺也需要距离——同码拒绝。
    const EngineeringPolicySet zeroPolicy = publishRigPolicy(rig, 0.0);
    auto zeroBackend = std::make_shared<ScriptedCollisionBackend>();
    const std::shared_ptr<const CollisionEvaluationSession> zeroSession =
        buildScriptedSession(rig, zeroPolicy, zeroBackend);
    CollisionQuery query = singleStateQuery(0.0);
    query.requestMinDistance = true;
    result = zeroSession->evaluate(query, ctx);
    EXPECT_EQ(result.status, CollisionEvaluationStatus::Failed);
    EXPECT_TRUE(hasDiagnostic(result.diagnostics, "POLICY-CLL-DETECTOR-UNAVAILABLE"));

    // 对照：m=0 且未请求距离——纯碰撞评估不受能力缺口影响（按需核对）。
    result = zeroSession->evaluate(singleStateQuery(0.0), ctx);
    EXPECT_EQ(result.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(result.finalized);
}

/**
 * 过滤留痕（§7.2③——POL-SCOPE-2 运行期②段的评估侧落点）：显式排除对
 * 在评估输出中全量留痕（appliedFilters ruleOrigin=规则引用＋理由）；
 * 显式必检的相邻对发现被输出（过滤不得隐藏必检——发现的 level=Must）。
 */
TEST(CollisionEvaluation, AppliedFiltersTraceAndMandatorySurvive_POL_SCOPE_2)
{
    const CollisionRig rig = makeRig();
    // 排除对：toolBox×env1（夹持式豁免）；必检对：toolBox×env2（覆盖默认必检）。
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = taggedObjectId(0x20);
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{0.0, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(rig.ids.toolBox),
                       ScopeTarget::makeObject(rig.ids.env2), PolicyRuleLevel::Must,
                       "工艺干涉必检")};
    in.collision.excludedPairs = {
        PairRule::make(ScopeTarget::makeObject(rig.ids.toolBox),
                       ScopeTarget::makeObject(rig.ids.env1), PolicyRuleLevel::Should,
                       "夹持接触豁免")};
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{12.566370614359172, "rad"};
    const PolicyParseResult parsed = resolvePolicy(in, rig.validation);
    ASSERT_TRUE(parsed.policy.has_value())
        << "夹具策略应可发布: "
        << (parsed.diagnostics.empty() ? std::string{"-"}
                                       : parsed.diagnostics.front().code);
    const EngineeringPolicySet policy = *parsed.policy;

    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    const CollisionEvaluation result = session->evaluate(singleStateQuery(0.0), ScriptedCallContext{});
    ASSERT_EQ(result.status, CollisionEvaluationStatus::Completed);

    // 过滤留痕：全量输出（不因首个短路）＋ruleOrigin 引用策略槽位与理由。
    ASSERT_EQ(result.appliedFilters.size(), std::size_t{1});
    EXPECT_EQ(result.appliedFilters.front().objectA, rig.ids.toolBox);
    EXPECT_EQ(result.appliedFilters.front().objectB, rig.ids.env1);
    EXPECT_EQ(result.appliedFilters.front().ruleOrigin,
              "policy.excludedPairs[0].reason=夹持接触豁免");
    // findings 与排除集无交集：必检对（toolBox×env2）发现被输出、级别
    // ＝规则 authored Must；被排除对（toolBox×env1）无发现（其真实碰撞
    // 被合法过滤——留痕即追溯）。
    ASSERT_EQ(result.findings.size(), std::size_t{1});
    EXPECT_EQ(result.findings.front().objectA, rig.ids.toolBox);
    EXPECT_EQ(result.findings.front().objectB, rig.ids.env2);
    EXPECT_EQ(result.findings.front().level, PolicyRuleLevel::Must);
    EXPECT_EQ(result.findings.front().kind, CollisionFindingKind::Collision);
    // 覆盖计数：作用域 2 对（1 必检＋1 域默认...本场景 env2 对由必检承载，
    // env1 对被排除）——inScope=1、excluded=1、withGeometry/evaluated=1。
    EXPECT_EQ(result.coverage.pairsInScope, std::uint64_t{1});
    EXPECT_EQ(result.coverage.pairsExcludedByRule, std::uint64_t{1});
    EXPECT_EQ(result.coverage.pairsWithGeometry, std::uint64_t{1});
    EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{1});
}

/**
 * stopAtFirstFinding 提前终止（§6.2——筛选/淘汰场景；输出仍带 coverage）：
 * 首个发现后立即终止（后续样本/对不评），coverage 如实反映已查部分，
 * 输出仍为 Completed＋finalized=true（按请求协议完成的评估）。
 */
TEST(CollisionEvaluation, StopAtFirstFindingTerminatesEarlyWithCoverage)
{
    const CollisionRig rig = makeRig(/*withEnv2=*/true);
    const EngineeringPolicySet policy = publishRigPolicy(rig, 0.0);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    CollisionQuery query = singleStateQuery(0.0);
    query.stopAtFirstFinding = true;
    const CollisionEvaluation result = session->evaluate(query, ScriptedCallContext{});
    ASSERT_EQ(result.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(result.finalized) << "按请求协议完成的评估为终态（§6.3 Completed 行）";
    ASSERT_EQ(result.findings.size(), std::size_t{1}) << "首个发现即终止（单发现）";
    // 规范序首对（toolBox, env1）被查——第二对（toolBox, env2）未评：
    // pairsEvaluated=1（如实——输出仍带 coverage）。
    EXPECT_EQ(result.findings.front().objectB, rig.ids.env1);
    EXPECT_EQ(result.coverage.pairsEvaluated, std::uint64_t{1});
    EXPECT_EQ(result.coverage.pairsInScope, std::uint64_t{2});
}
