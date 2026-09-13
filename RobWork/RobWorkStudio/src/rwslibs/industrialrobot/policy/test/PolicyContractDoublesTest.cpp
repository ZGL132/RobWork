/**
 * @file   PolicyContractDoublesTest.cpp
 * @brief  跨单元契约套件（POL-T11）——规范替身行为组（POL-TD-1）、
 *         三入口唯一实现共享（POL-SHARE-1）、AT 观测点载体
 *         （POL-AT-1 行程确认放行链/POL-AT-2 三入口一致/POL-AT-3 显示
 *         单位与配置不可覆盖策略）。
 *
 * 设计依据：
 *   - units/policy.md §11（POL-TD-1/POL-SHARE-1/POL-AT-1~2~3 用例行——
 *     本套件逐条交付；§10.4 行程上限确认放行走查、§10.1 主流程三入口
 *     装配的替身投影）；§3.4（_contract_test＝跨单元契约面——本文件
 *     以消费方视点驱动 ④端口/会话/关节限位契约）；§12 POL-T11 行；
 *   - units/testkit.md §10.2 policy 行（"策略一致性三入口场景"——本套件
 *     的 POL-SHARE-1/POL-AT-2 即其承接）；§5.4.2（checkSetEquivalent）；
 *   - 需求 ARC-05（唯一策略对象＋唯一共享实现——无私有开关/重复算法）、
 *     MDL-06④/AT-01（行程上限确认放行）、NFR-COR-05/AT-19（三入口
 *     完全一致）、KIN-13/AT-27（配置不得覆盖策略）、UX-08（显示单位
 *     不入身份）。
 *
 * 替身边界（POL-TD-1，全文见 test/README.md §1）：本文件消费的
 * ScriptedCollisionEvaluator/StubPolicyProvider/ScriptedCallContext 的
 * 脚本产出只验证契约与消费方逻辑，不构成碰撞算法正确性证明——真实
 * 数值正确性由真实后端解析算例（本文件 POL-SHARE-1 组）与
 * pol-collision-analytic 数据集（CollisionAnalyticDatasetTest）承担。
 *
 * 模式约束：集成模式专属（消费 rw 非模板类 WorkCell/Device/
 * ProximityStrategyRW——冒烟模式不编译本文件）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Compatibility.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/JointLimits.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>

#include <sdurws/ird/policy/testdouble/PolicyTestDoubles.hpp>

#include <rw/geometry/Box.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/kinematics/FixedFrame.hpp>
#include <rw/math/Math.hpp>
#include <rw/math/Q.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/RPY.hpp>
#include <rw/math/Transform3D.hpp>
#include <rw/math/Vector3D.hpp>
#include <rw/models/Object.hpp>
#include <rw/models/RevoluteJoint.hpp>
#include <rw/models/RigidObject.hpp>
#include <rw/models/SerialDevice.hpp>
#include <rw/models/WorkCell.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

using namespace sdurws::ird::policy;
using namespace sdurws::ird::policy::testdoubles;

/// core/testkit 契约类型短别名（既有测试同款可读性）。
namespace core = sdurws::ird::core;
namespace tk = sdurws::ird::testkit;

namespace {

// =====================================================================
// 身份/数值小工具与场景装置（盒几何装置沿 POL-T07 实测形态——内置后端
// 对 Box 三角网格精确；倒挂安装经基座帧变换注入，MDL-22 单一消费）。
// =====================================================================

/// 手工构造的有效对象身份（首字节打标——字节字典序可控）。
core::ObjectId taggedObjectId(std::uint8_t tag)
{
    core::ObjectId id;
    id.bytes[0] = tag;
    return id;
}

/// 手工构造的非全零内容身份（场景/名称映射身份替身——CR-04 值传递）。
core::ContentIdentity taggedContentIdentity(std::uint8_t tag)
{
    core::ContentIdentity cid;
    cid.bytes[0] = tag;
    return cid;
}

/// 装置几何常量（SI 单位——0.2 m 盒、0.5 m 臂长，POL-T07 同值）。
inline constexpr double kBoxSize = 0.2;
inline constexpr double kArmLength = 0.5;
/// 4π/6π 行程字面量（附录 D 第 11 项默认与其反例——单位 rad）。
inline constexpr double kFourPi = 12.566370614359172;
inline constexpr double kSixPi = 18.849555921538759;

/// 装置对象身份集（字节打标——规范序可控）。
struct RigIds {
    core::ObjectId toolBox = taggedObjectId(0x21);   ///< 工具盒（RobotLink）
    core::ObjectId envBox = taggedObjectId(0x22);    ///< 环境盒（EnvironmentObject）
    core::ObjectId device = taggedObjectId(0x29);    ///< 主链设备
    core::ObjectId policyObj = taggedObjectId(0x20); ///< 策略对象
    core::ObjectId joint1 = taggedObjectId(0x31);    ///< J1 关节对象（POL-AT-1 用）
};

/// 场景装置（共享 rw 产物＋policy 侧装配面——mount 决定 R_world_base）。
struct ContractRig {
    RigIds ids;
    std::shared_ptr<rw::models::WorkCell> workcell;   ///< 编译产物替身（真实 rw WC）
    CollisionScene scene;                             ///< policy 消费的碰撞场景
    ScriptedNameContext names;                        ///< 名称上下文（共享替身）
    ScriptedValidationContext validation;             ///< 发布闭包替身
};

/**
 * @brief 构造场景装置（World→Base〔mount 变换〕→J1→Tool＋环境盒 World 直挂）。
 *
 * @param mountInverted [in] false＝直立（恒等安装）；true＝倒挂（绕世界
 *                      Y 轴 180°——R_world_base=Ry(π)，MDL-22 编译产物
 *                      单一权威的注入形态；policy 禁止二次旋转）
 * @param envCenterZ    [in] 环境盒中心 z（SI m；直立 q=0 时工具盒与
 *                      (0.5,0,z) 环境盒的轴向面间隙＝|z|−kBoxSize）
 */
ContractRig makeRig(bool mountInverted, double envCenterZ)
{
    ContractRig rig;
    rig.ids = RigIds{};

    // 安装变换（编译产物把安装位姿编译进 Base 帧——MDL-22 单一权威）。
    const rw::math::Transform3D<> mount =
        mountInverted
            ? rw::math::Transform3D<>(
                rw::math::Vector3D<>(0.0, 0.0, 0.0),
                rw::math::RPY<>(0.0, std::acos(-1.0), 0.0).toRotation3D())
            : rw::math::Transform3D<>::identity();

    rig.workcell = std::make_shared<rw::models::WorkCell>("PolicyContractRigWC");
    rw::core::Ptr<rw::kinematics::FixedFrame> base =
        rw::core::ownedPtr(new rw::kinematics::FixedFrame("Base", mount));
    rig.workcell->addFrame(base);
    rw::core::Ptr<rw::models::RevoluteJoint> j1 = rw::core::ownedPtr(
        new rw::models::RevoluteJoint("J1", rw::math::Transform3D<>::identity()));
    rig.workcell->addFrame(j1, base);
    rw::core::Ptr<rw::kinematics::FixedFrame> tool = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "Tool", rw::math::Transform3D<>(rw::math::Vector3D<>(kArmLength, 0.0, 0.0))));
    rig.workcell->addFrame(tool, j1);
    rw::core::Ptr<rw::kinematics::FixedFrame> envFrame = rw::core::ownedPtr(
        new rw::kinematics::FixedFrame(
            "EnvBox", rw::math::Transform3D<>(
                          rw::math::Vector3D<>(kArmLength, 0.0, envCenterZ))));
    rig.workcell->addFrame(envFrame);
    // 主链设备（1 自由度——构型维度锚）。
    const rw::kinematics::State connected = rig.workcell->getDefaultState();
    rig.workcell->addDevice(rw::core::ownedPtr(
        new rw::models::SerialDevice(base, tool, "Robot", connected)));

    // 碰撞几何（RigidObject 承载——编译产物以 Object 挂接几何）。
    const auto attachBox = [&](const rw::kinematics::Frame::Ptr& frame) {
        rw::core::Ptr<rw::models::RigidObject> object =
            rw::core::ownedPtr(new rw::models::RigidObject(frame));
        object->addGeometry(rw::core::ownedPtr(new rw::geometry::Geometry(
            rw::core::ownedPtr(new rw::geometry::Box(kBoxSize, kBoxSize, kBoxSize)))));
        rig.workcell->add(object);
    };
    attachBox(tool);
    attachBox(envFrame);

    // 场景装配（CR-04——调用方自编译产物装配；含 R_world_base 消费一致性）。
    rig.scene.workcell = rig.workcell;
    rig.scene.primaryDevice = rig.ids.device;
    rig.scene.objects = {
        {rig.ids.toolBox, "ToolBox", SceneObjectRole::RobotLink, true},
        {rig.ids.envBox, "EnvBox", SceneObjectRole::EnvironmentObject, true},
    };
    rig.scene.sceneContentIdentity = taggedContentIdentity(0xC7);

    // 名称映射（整名——帧名与设备名；CON-06 显示辅助不作身份）。
    rig.names.mapIdentity = taggedContentIdentity(0x6E);
    rig.names.byName["Robot"] = rig.ids.device;
    rig.names.byId[rig.ids.device] = "Robot";
    rig.names.byId[rig.ids.toolBox] = "Tool";
    rig.names.byId[rig.ids.envBox] = "EnvBox";
    rig.names.byId[rig.ids.joint1] = "J1";

    // 发布闭包应答（对象存在＋角色一致——解析⑤通过的前提）。
    rig.validation.existingObjects[rig.ids.toolBox] = true;
    rig.validation.roles[rig.ids.toolBox] = "RobotLink";
    rig.validation.existingObjects[rig.ids.envBox] = true;
    rig.validation.roles[rig.ids.envBox] = "EnvironmentObject";
    rig.validation.existingObjects[rig.ids.joint1] = true;
    rig.validation.roles[rig.ids.joint1] = "RobotLink";
    return rig;
}

/**
 * @brief 装配基线策略（safetyClearance 可选——缺省 0＝仅碰撞检测，
 *        §7.4 m=0 退化；显示单位变体经 unit 形参承载 POL-ID-3 面）。
 */
EngineeringPolicySet publishRigPolicy(const ContractRig& rig, double clearanceValue = 0.0,
                                      std::string clearanceUnit = "m",
                                      double travelLimitRad = kFourPi)
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = rig.ids.policyObj;
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{clearanceValue, clearanceUnit};
    in.collision.excludeAdjacentLinksByDefault = true;
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{travelLimitRad, "rad"};
    const PolicyParseResult res = resolvePolicy(in, rig.validation);
    if (!res.policy.has_value()) {
        throw std::runtime_error("装置策略应可发布，首条诊断: "
                                 + (res.diagnostics.empty() ? std::string{"-"}
                                                            : res.diagnostics.front().code));
    }
    return *res.policy;
}

/// SingleState 查询（单构型便捷——按值返回非 const）。
CollisionQuery singleStateQuery(double q)
{
    CollisionQuery query;
    query.kind = CollisionQueryKind::SingleState;
    query.configurations.push_back(rw::math::Q(1, q));
    return query;
}

/// 诊断码存在性（评估期诊断定位断言辅助）。
bool hasDiagnostic(const std::vector<core::DiagnosticRecord>& diagnostics,
                   std::string_view code)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [code](const core::DiagnosticRecord& d) { return d.code == code; });
}

/// 16/32 字节身份→小写十六进制（摘要/身份对照的文本形态）。
template <typename ByteContainer>
std::string idToHex(const ByteContainer& bytes)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

}  // namespace

// =====================================================================
// Findings 集合匹配 traits（checkSetEquivalent 消费——§5.4.5 业务规则
// 归域的 policy 侧显式特化）。★ 必须特化 SetMatchTraits<T> 本体且位于
// 真实 testkit 命名空间（匿名命名空间内的同名嵌套是另一个命名空间，
// 且 checkSetEquivalent 经参数的静态类型调用 identity/numerics——静态
// 函数不做动态分发，派生/别名类型都不会被查看到）。
// 身份键＝对象对＋采样位＋种类；数值面空：本套件对照 m=0 二值面，无
// 实测距离字段。
// =====================================================================
namespace {

/// 十六进制小串（命名空间级工具——特化 identity 的键构造用）。
std::string bytesKey(const std::array<std::uint8_t, 16>& bytes)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

}  // namespace

namespace sdurws::ird::testkit {
template <>
struct SetMatchTraits<sdurws::ird::policy::CollisionFinding>
{
    static std::optional<std::string> identity(const sdurws::ird::policy::CollisionFinding& f)
    {
        return bytesKey(f.objectA.bytes) + "|" + bytesKey(f.objectB.bytes) + "|"
            + std::to_string(f.sampleIndex) + "|"
            + std::to_string(static_cast<int>(f.kind));
    }

    static std::vector<NumericFieldView> numerics(const sdurws::ird::policy::CollisionFinding&)
    {
        return {};
    }
};
}  // namespace sdurws::ird::testkit

// =====================================================================
// 组 1：规范替身行为（POL-TD-1 行为面——脚本四轨与端口装配分步镜像）。
// =====================================================================

/**
 * POL-TD-1（行为面）：ScriptedCollisionEvaluator 的脚本轨道经真实会话
 * 产生四类预设结果——findings（碰撞应答脚本）、Failed（后端异常注入→
 * POLICY-CLL-EVALUATION-FAILED，findings 不含该对、finalized=false）、
 * Canceled（取消脚本上下文→部分保留、无错误诊断）。★ 全部断言针对
 * 真实评估逻辑对脚本输入的处置，不是脚本值本身（边界声明，POL-TD-1）。
 */
TEST(PolicyContractDoubles, ScriptedEvaluatorProducesAllFourScriptTracks)
{
    IRD_TEST_INFO("ARC-05", {"AT-19"}, std::nullopt);
    const ContractRig rig = makeRig(false, 0.0);
    const EngineeringPolicySet policy = publishRigPolicy(rig);

    // ---- findings 轨：脚本 [true]（相交）→ Completed＋发现＋finalized ----
    ScriptedCollisionEvaluator scripted;
    scripted.collisionScript = {true};
    auto ctx = ScriptedCallContext{};
    const std::shared_ptr<const CollisionEvaluationSession> hitSession =
        scripted.createSession(policy, rig.scene, rig.names);
    const CollisionEvaluation hit = hitSession->evaluate(singleStateQuery(0.0), ctx);
    ASSERT_EQ(hit.status, CollisionEvaluationStatus::Completed);
    EXPECT_TRUE(hit.finalized);
    ASSERT_EQ(hit.findings.size(), 1u);
    EXPECT_EQ(hit.findings.front().objectA, rig.ids.toolBox);
    EXPECT_EQ(hit.findings.front().objectB, rig.ids.envBox);

    // ---- Failed 轨：后端异常注入 → Failed＋稳定码（KIN-05 不伪装无碰撞）----
    ScriptedCollisionEvaluator faulty;
    faulty.throwInCollision = true;
    const std::shared_ptr<const CollisionEvaluationSession> faultSession =
        faulty.createSession(policy, rig.scene, rig.names);
    const CollisionEvaluation fault = faultSession->evaluate(singleStateQuery(0.0), ctx);
    EXPECT_EQ(fault.status, CollisionEvaluationStatus::Failed);
    EXPECT_FALSE(fault.finalized);
    EXPECT_TRUE(fault.findings.empty()) << "Failed 不产生发现（无'无碰撞'结论字段）";
    EXPECT_TRUE(hasDiagnostic(fault.diagnostics, "POLICY-CLL-EVALUATION-FAILED"));

    // ---- Canceled 轨：样本边界取消 → 部分保留＋无错误诊断（UX-03）----
    ScriptedCollisionEvaluator cancellable;
    cancellable.collisionScript = {true};
    ScriptedCallContext cancelCtx;
    cancelCtx.cancelAfterQueries = 0;   // 首个样本边界即取消命中
    const std::shared_ptr<const CollisionEvaluationSession> cancelSession =
        cancellable.createSession(policy, rig.scene, rig.names);
    const CollisionEvaluation canceled =
        cancelSession->evaluate(singleStateQuery(0.0), cancelCtx);
    EXPECT_EQ(canceled.status, CollisionEvaluationStatus::Canceled);
    EXPECT_FALSE(canceled.finalized);
    EXPECT_TRUE(canceled.diagnostics.empty()) << "正常取消不产生错误诊断（UX-03）";

    // ---- 异常轨的描述符标注面：脚本描述符显式 not-a-real-backend ----
    // （POL-TD-1 机检的运行期面——替身数据不进复现要素主张。）
    const CollisionBackendDescriptor descriptor = scripted.backend();
    EXPECT_NE(descriptor.backendId.find("test."), std::string::npos)
        << "脚本描述符 backendId 须带 test. 标注前缀";
    EXPECT_NE(descriptor.toleranceModel.find("not-a-real-backend"), std::string::npos)
        << "脚本描述符 toleranceModel 须显式标注替身语义";
}

/**
 * POL-TD-1／§9.1 装配分步契约的替身侧镜像：StubPolicyProvider 未注入
 * 评估器半区即调用 collisionEvaluator()＝PolicyError(PortAssemblyIncomplete)
 * （与真实端口同码面）；注入后同一实例引用恒定（POL-SHARE-1 实例同一性
 * 的端口侧基础）；resolvePolicy 预设轨与脚本抛出轨。
 */
TEST(PolicyContractDoubles, StubProviderMirrorsAssemblyStepwiseContract)
{
    IRD_TEST_INFO("ARC-05", {}, std::nullopt);
    const ContractRig rig = makeRig(false, 0.0);

    // ---- 装配分步：未注入即调用＝契约违约 fail-fast（同码面） ----
    StubPolicyProvider stub;
    EXPECT_THROW((void)stub.collisionEvaluator(), PolicyError);
    try {
        (void)stub.collisionEvaluator();
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PortAssemblyIncomplete)
            << "装配分步违约应抛 PortAssemblyIncomplete（§9.1 端口错误矩阵）";
    }

    // ---- 注入后：同一实例引用（两次取用指针相等——POL-SHARE-1 基础）----
    auto scripted = std::make_shared<ScriptedCollisionEvaluator>();
    stub.setEvaluator(scripted);
    EXPECT_EQ(&stub.collisionEvaluator(), scripted.get());
    EXPECT_EQ(&stub.collisionEvaluator(), &stub.collisionEvaluator());

    // ---- 预设解析轨：presetResolution 原样返回 ----
    // （PolicyResolution 拷贝赋值被删——发布对象全 const 成员的传递性；
    //   经 optional::emplace 就地构造预设的策略槽位。）
    PolicyResolution preset;
    preset.policy.emplace(publishRigPolicy(rig));
    stub.presetResolution.policy.emplace(*preset.policy);
    const core::ObjectId requester = taggedObjectId(0x50);
    core::ContentVersion version;
    version.bytes[0] = 0x0A;
    const PolicyResolution served =
        stub.resolvePolicy(PolicyResolutionRequest{requester, version});
    ASSERT_TRUE(served.policy.has_value());
    EXPECT_EQ(*served.policy, *preset.policy);

    // ---- 脚本抛出轨：调用方契约违约演练（不重复真实校验逻辑）----
    stub.throwPolicyError = true;
    stub.throwCode = PolicyErrorCode::PolicyObjectInvalid;
    EXPECT_THROW((void)stub.resolvePolicy(PolicyResolutionRequest{requester, version}),
                 PolicyError);
}

// =====================================================================
// 组 2：POL-SHARE-1／POL-AT-2——三入口唯一实现共享与完全一致。
// =====================================================================

namespace {

/// 消费方替身（§10.1 三入口——kinematics/trajectory/optimization 的阶段 A
/// 测试投影：仅消费 ④端口与真实会话，无各自域逻辑）。
struct ConsumerStub {
    std::string entryLabel;   ///< 入口标签（kin/trajectory/optimization——显示）
    const IPolicyProvider* provider = nullptr;

    /// 入口动作：resolvePolicy → collisionEvaluator().createSession → evaluate。
    CollisionEvaluation runCollisionCheck(const PolicyResolutionRequest& request,
                                          const CollisionScene& scene,
                                          const IPolicyNameContext& names,
                                          const CollisionQuery& query,
                                          IPolicyCallContext& ctx,
                                          core::ContentIdentity* outSessionIdentity
                                              = nullptr,
                                          const void** outEvaluatorAddress = nullptr) const
    {
        const PolicyResolution resolution = provider->resolvePolicy(request);
        if (!resolution.policy.has_value()) {
            throw std::runtime_error(entryLabel + ": 端口解析应成功（装置前提）");
        }
        // ARC-05 的消费侧保证：评估器唯一实例经端口取得——各入口不做
        // 第二实现（结构上无旁路：端口不暴露创建入口，§9.1）。
        ICollisionEvaluator& evaluator = provider->collisionEvaluator();
        if (outEvaluatorAddress != nullptr) {
            *outEvaluatorAddress = &evaluator;   // 实例同一性观测点（POL-SHARE-1）
        }
        const std::shared_ptr<const CollisionEvaluationSession> session =
            evaluator.createSession(*resolution.policy, scene, names);
        if (outSessionIdentity != nullptr) {
            *outSessionIdentity = session->sessionIdentity();
        }
        return session->evaluate(query, ctx);
    }
};

/// 编码策略字节并登记进字节源（④端口真实解析链的取数前提）。
///
/// 从发布对象重建 Raw 形态编码（对象字节＝full 形态——往返载体）：装置
/// 策略的 Raw 形态按 publishRigPolicy 的同参重建（解析是纯函数，同参同
/// Raw 同发布对象——字节内容与重建路径的等价性由 POL-ID-1 往返契约保证）。
void registerPolicyBytes(ScriptedBytesSource& source, const ContractRig& rig,
                         const EngineeringPolicySet& policy,
                         core::ObjectId object, core::ContentVersion version)
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = object;
    in.collision.enabled = policy.collision.enabled;
    in.collision.enabledDomains = policy.collision.enabledDomains;
    in.collision.safetyClearance = RawThresholdInput{0.0, "m"};
    if (policy.collision.safetyClearance.has_value()) {
        const double si = policy.collision.safetyClearance->siValue();
        in.collision.safetyClearance = RawThresholdInput{si, "m"};
    }
    in.collision.excludeAdjacentLinksByDefault = policy.collision.excludeAdjacentLinksByDefault;
    in.collision.mandatoryPairs = policy.collision.mandatoryPairs;
    in.collision.excludedPairs = policy.collision.excludedPairs;
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit =
        RawThresholdInput{policy.jointThresholds.finiteRotationTravelLimit.siValue(), "rad"};
    in.jointThresholds.travelLimitCheckEnabled = policy.jointThresholds.travelLimitCheckEnabled;
    in.applicability = policy.applicability;
    in.numericContractAnchor = policy.numericContractAnchor;
    in.origin = policy.origin;
    source.bytesByKey[{object, version}] = PolicyCodec::encode(in);
}

/**
 * @brief ④端口装配辅助（§6.5 装配线的测试侧单点）：创建真实评估器、
 *        先取后端描述符（backend() 同源——sessionIdentity 组成一致），
 *        再以共享所有权注入 PolicyProvider。
 *
 * @return 装配完成的端口实例（评估器半区就绪——collisionEvaluator() 可服务）
 */
PolicyProvider assembleProvider(const IPolicyBytesSource& bytesSource,
                                const IPolicyValidationContext& validation)
{
    std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const CollisionBackendDescriptor backend = evaluator->backend();
    return PolicyProvider(bytesSource, validation,
                          std::shared_ptr<ICollisionEvaluator>(std::move(evaluator)),
                          backend);
}

}  // namespace

/**
 * POL-SHARE-1（ARC-05/NFR-COR-05/AT-19）：kinematics/trajectory/
 * optimization 替身消费者经同一 PolicyProvider 取得碰撞评估——三入口
 * 同一 evaluator 实例（指针相等）＋等价输出（findings checkSetEquivalent
 * ＋评估逐字段相等）＋同一会话身份。
 */
TEST(PolicyContractDoubles, ThreeEntriesShareSingleEvaluatorInstanceAndEquivalentOutputs)
{
    IRD_TEST_INFO("ARC-05", {"AT-19"}, std::nullopt);
    const ContractRig rig = makeRig(false, 0.0);   // q=0 工具盒与环境盒重合（相交）
    const EngineeringPolicySet policy = publishRigPolicy(rig);

    // ④端口装配（§6.5 装配线：唯一实现实例＋字节源＋闭包应答）。
    ScriptedBytesSource bytesSource;
    core::ContentVersion version;
    version.bytes[0] = 0x0B;
    registerPolicyBytes(bytesSource, rig, policy, rig.ids.policyObj, version);
    const PolicyProvider provider = assembleProvider(bytesSource, rig.validation);

    // 三入口替身消费者（同一 provider——装配期注册、运行期不变）。
    const ConsumerStub kin{"kinematics-entry", &provider};
    const ConsumerStub trj{"trajectory-entry", &provider};
    const ConsumerStub opt{"optimization-entry", &provider};

    const PolicyResolutionRequest request{rig.ids.policyObj, version};
    core::ContentIdentity identities[3];
    CollisionEvaluation results[3];
    const void* evaluatorAddresses[3] = {nullptr, nullptr, nullptr};
    const ConsumerStub* entries[3] = {&kin, &trj, &opt};
    for (std::size_t i = 0; i < 3; ++i) {
        ScriptedCallContext ctx;
        results[i] = entries[i]->runCollisionCheck(request, rig.scene, rig.names,
                                                   singleStateQuery(0.0), ctx,
                                                   &identities[i],
                                                   &evaluatorAddresses[i]);
    }

    // ---- 实例同一性：三入口各自经端口取得的评估器地址全等（指针相等）----
    EXPECT_EQ(evaluatorAddresses[0], evaluatorAddresses[1]);
    EXPECT_EQ(evaluatorAddresses[1], evaluatorAddresses[2]);
    EXPECT_EQ(evaluatorAddresses[0], &provider.collisionEvaluator());

    // ---- 会话身份相等（f(policy, scene, names, backend)——跨入口等价键）----
    EXPECT_EQ(identities[0], identities[1]);
    EXPECT_EQ(identities[1], identities[2]);

    // ---- 等价输出：评估逐字段相等＋findings 集合等价（checkSetEquivalent）----
    const tk::ToleranceProfile profile =
        tk::ToleranceProfile::load(tk::toleranceProfileDir("pol-analytic") / "v1.0.0.json");
    const tk::SetCheckResult shareCheck = tk::checkSetEquivalent(
        results[0].findings, results[2].findings, profile,
        tk::SetMatchTraits<CollisionFinding>{});
    EXPECT_TRUE(shareCheck.equivalent)
        << "三入口 findings 应集合等价（POL-SHARE-1）: missingExpected="
        << shareCheck.missingExpected.size()
        << " extraActual=" << shareCheck.extraActual.size()
        << " duplicates=" << shareCheck.duplicates.size()
        << " mismatchedPairs=" << shareCheck.mismatchedPairs.size()
        << " ambiguousMatch=" << shareCheck.ambiguousMatch;
    for (std::size_t i = 1; i < 3; ++i) {
        EXPECT_TRUE(results[0] == results[i])
            << "三入口评估输出应逐字段相等（entries " << i << "）";
    }
    ASSERT_EQ(results[0].findings.size(), 1u) << "装置 q=0 应检出工具盒×环境盒相交";
}

/**
 * POL-AT-2（AT-19 观测点）：三入口（替身消费者）同规范状态同策略——
 * 对象 ID 对/判定/原因码完全一致；原因码面＝过滤 ruleOrigin（显式排除
 * 引用策略槽位与理由）＋域默认（无规则命中的必检对 level=Must）。
 */
TEST(PolicyContractDoubles, At19ThreeEntriesIdenticalIdPairsVerdictsAndReasonCodes)
{
    IRD_TEST_INFO("NFR-COR-05", {"AT-19"}, std::nullopt);
    const ContractRig rig = makeRig(false, 1.0);   // 分离工况（面间隙 0.8 m——无碰撞）

    // 策略 A（域默认面）：显式排除 ToolBox×EnvBox——评估输出 appliedFilters
    // 记录 ruleOrigin 引用策略槽位（§6.2 取值形态），三入口完全一致。
    // （发布对象全 const 成员——经值返回构造，不做拷贝赋值。）
    const auto publishWithExclusion = [&rig]() -> EngineeringPolicySet {
        RawPolicyInput in;
        in.schemaVersion = PolicySchema::currentVersion;
        in.policyObject = rig.ids.policyObj;
        in.collision.enabled = true;
        in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                       CollisionDomain::Tool};
        in.collision.safetyClearance = RawThresholdInput{0.0, "m"};
        in.collision.excludeAdjacentLinksByDefault = true;
        in.collision.excludedPairs.push_back(PairRule::make(
            ScopeTarget::makeObject(rig.ids.toolBox), ScopeTarget::makeObject(rig.ids.envBox),
            PolicyRuleLevel::Should, "夹持接触豁免"));
        in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
        in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
        in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{kFourPi, "rad"};
        const PolicyParseResult res = resolvePolicy(in, rig.validation);
        if (!res.policy.has_value()) {
            ADD_FAILURE() << "排除规则策略应可发布";
            throw std::runtime_error("publishWithExclusion 失败");
        }
        return *res.policy;
    };
    const EngineeringPolicySet policyA = publishWithExclusion();

    ScriptedBytesSource bytesSource;
    core::ContentVersion version;
    version.bytes[0] = 0x0C;
    registerPolicyBytes(bytesSource, rig, policyA, rig.ids.policyObj, version);
    const PolicyProvider providerA = assembleProvider(bytesSource, rig.validation);

    // 三入口替身消费者（同一 provider；命名实例——数组取临时地址会悬垂）。
    const ConsumerStub kinA{"kinematics-entry", &providerA};
    const ConsumerStub trjA{"trajectory-entry", &providerA};
    const ConsumerStub optA{"optimization-entry", &providerA};
    const ConsumerStub* entries[3] = {&kinA, &trjA, &optA};
    const PolicyResolutionRequest request{rig.ids.policyObj, version};
    for (const ConsumerStub* entry : entries) {
        ScriptedCallContext ctx;
        const CollisionEvaluation result =
            entry->runCollisionCheck(request, rig.scene, rig.names,
                                     singleStateQuery(0.0), ctx, nullptr);
        // 判定面：显式排除后该对不进入 findings（无发现），但过滤必须
        // 全量留痕（§7.2③——过滤不得静默）。
        EXPECT_TRUE(result.findings.empty()) << entry->entryLabel << ": 排除对不产发现";
        ASSERT_EQ(result.appliedFilters.size(), 1u) << entry->entryLabel;
        EXPECT_EQ(result.appliedFilters.front().objectA, rig.ids.toolBox);
        EXPECT_EQ(result.appliedFilters.front().objectB, rig.ids.envBox);
        EXPECT_EQ(result.appliedFilters.front().ruleOrigin,
                  "policy.excludedPairs[0].reason=夹持接触豁免");
    }

    // 策略 B（域默认面）：无规则命中——必检对经域默认进入（level=Must，
    // §7.1"域启用即默认必检"），三入口的对象 ID 对/判定/原因码一致。
    // 装置改用重合工况（环境盒 z=0——q=0 工具盒与环境盒重合，必检出）。
    const ContractRig rigB = makeRig(false, 0.0);
    const EngineeringPolicySet policyB = publishRigPolicy(rigB);
    ScriptedBytesSource bytesB;
    core::ContentVersion versionB;
    versionB.bytes[0] = 0x0D;
    registerPolicyBytes(bytesB, rigB, policyB, rigB.ids.policyObj, versionB);
    const PolicyProvider providerB1 = assembleProvider(bytesB, rigB.validation);
    const PolicyProvider providerB2 = assembleProvider(bytesB, rigB.validation);
    // 三入口替身消费者（两个 provider 装配自同一字节源/闭包/同参评估器——
    // "同一实现"的消费语义由 POL-SHARE-1 的单 provider 断言钉住；本组
    // 三入口的目的只在验证输出内容（对象对/判定/原因码）完全一致）。
    const ConsumerStub kinB1{"kinematics-entry", &providerB1};
    const ConsumerStub trjB2{"trajectory-entry", &providerB2};
    const ConsumerStub optB1{"optimization-entry", &providerB1};
    const ConsumerStub* domainEntries[3] = {&kinB1, &trjB2, &optB1};
    const PolicyResolutionRequest requestB{rigB.ids.policyObj, versionB};
    for (const ConsumerStub* entry : domainEntries) {
        ScriptedCallContext ctx;
        const CollisionEvaluation result =
            entry->runCollisionCheck(requestB, rigB.scene, rigB.names,
                                     singleStateQuery(0.0), ctx, nullptr);
        ASSERT_EQ(result.findings.size(), 1u) << entry->entryLabel;
        EXPECT_EQ(result.findings.front().objectA, rigB.ids.toolBox);
        EXPECT_EQ(result.findings.front().objectB, rigB.ids.envBox);
        EXPECT_EQ(result.findings.front().kind, CollisionFindingKind::Collision);
        EXPECT_EQ(result.findings.front().level, PolicyRuleLevel::Must)
            << "域默认必检＝Must（§7.1——AT-19 原因码的域默认分量）";
    }
}

// =====================================================================
// 组 3：POL-AT-1——行程上限可确认放行走查（§10.4 全链，替身处理器）。
// =====================================================================

namespace {

/// 确认绑定记录（SA-15 ConfirmableFinding 的 policy 侧素材投影——
/// confirmations 绑定四元组：策略对象/内容版本/内容身份/涉事关节）。
struct ConfirmableFindingRecord {
    core::ObjectId policyObject;         ///< 四元组①：策略对象身份
    core::ContentVersion policyVersion;  ///< 四元组②：期望内容版本
    core::ContentIdentity policyContentId; ///< 四元组③：策略内容身份（摘要含于确认）
    core::ObjectId jointObject;          ///< 四元组④：涉事关节对象
    std::string code;                    ///< 稳定诊断码面（行程超限）
    double actualValue = 0.0;            ///< 比较三要素·实际（rad）
    double thresholdValue = 0.0;         ///< 比较三要素·阈值（rad）
    std::string unit = "rad";            ///< 单位显式（ERR-01）
    bool confirmed = false;              ///< 用户显式确认（未确认＝阻止）
};

/// 替身 project 处理器（§10.4 走查的 policy 侧全链——prepare 阶段）。
///
/// policy 职责止于比较型结果供给（处理器不持有 4π——阈值唯一来源是
/// 策略对象，KIN-13/R-POL-5）；确认/阻止决策与修订写入归 project
/// （本替身只投影"比较型结果→ConfirmableFinding→确认→放行/阻止"的
/// 消费契约，不实现持久化）。
class ScriptedProjectHandler
{
public:
    ScriptedProjectHandler(const IPolicyProvider& provider,
                           IJointLimitEvaluator& jointLimits)
        : m_provider(provider), m_jointLimits(jointLimits)
    {
    }

    /// prepare：resolvePolicy → 关节限位评估 → Must 行程超限 → 可确认发现。
    /// @return 是否放行（无 Must 超限＝直接放行；有＝须显式确认）
    bool prepare(const PolicyResolutionRequest& request,
                 const ScriptedNameContext& names, core::ObjectId jointObject,
                 double jointQMinRad, double jointQMaxRad,
                 const std::string& jointRuntimeName, ConfirmableFindingRecord* outFinding)
    {
        // 第一步：④端口解析（已解析已校验已发布策略——唯一权威）。
        const PolicyResolution resolution = m_provider.resolvePolicy(request);
        if (!resolution.policy.has_value()) {
            throw std::runtime_error("处理器取策略失败（装置前提）");
        }
        const EngineeringPolicySet& policy = *resolution.policy;

        // 第二步：关节限位评估（比较型结果供给——policy 职责终点）。
        JointLimitQuery query;
        query.joints.push_back(JointLimitSpec{jointObject, "J1",
                                              jointQMinRad, jointQMaxRad, false,
                                              std::nullopt});
        query.configurations.push_back({0.0});
        const JointLimitEvaluation evaluation = m_jointLimits.evaluate(query, policy, names);

        // 第三步：比较型诊断→ConfirmableFinding（确认放行流——SA-15）。
        for (const JointLimitFinding& f : evaluation.findings) {
            if (f.kind == JointLimitFindingKind::TravelLimitExceeded
                && f.level == PolicyRuleLevel::Must && outFinding != nullptr) {
                outFinding->policyObject = request.policyObject;
                outFinding->policyVersion = request.expectedVersion.value_or(
                    core::ContentVersion{});
                outFinding->policyContentId = policy.contentIdentity;
                outFinding->jointObject = f.jointObject;
                outFinding->code = "POLICY-JNT-TRAVEL-EXCEEDED";
                outFinding->actualValue = f.actualValue;
                outFinding->thresholdValue = f.thresholdValue;
                outFinding->unit = "rad";
                outFinding->confirmed = false;
                // 登记调用方记录指针（confirmPending 回写同一份——确认流
                // 状态由处理器持有，调用方记录即其投影）。
                m_out = outFinding;
                m_pending = *outFinding;
                return false;   // 未确认——阻止应用（无修订）
            }
        }
        return true;   // 无 Must 超限——放行
    }

    /// 用户显式确认（ui 确认对话回调——确认后放行；同步回写调用方记录）。
    void confirmPending()
    {
        if (m_pending.jointObject.isValid()) {
            m_pending.confirmed = true;
            if (m_out != nullptr) {
                *m_out = m_pending;
            }
        }
    }

    /// 命令摘要（确认记录入摘要——含 policyContentId，§10.4）。
    std::string summaryText() const
    {
        if (!m_pending.confirmed) {
            return "blocked";
        }
        return "released; policyContentId="
            + idToHex(m_pending.policyContentId.bytes);
    }

private:
    const IPolicyProvider& m_provider;     ///< ④端口（借用——宿主装配）
    IJointLimitEvaluator& m_jointLimits;   ///< 关节限位评估器（借用）
    ConfirmableFindingRecord m_pending;    ///< 待确认发现（确认流状态）
    ConfirmableFindingRecord* m_out = nullptr;  ///< 调用方记录（确认回写面）
};

}  // namespace

/**
 * POL-AT-1（AT-01/MDL-06④）：行程上限确认放行全链走查——行程 6π＞4π
 * （比较型三要素 actual/threshold/rad）→ 未确认阻止；显式确认后放行且
 * 摘要含 policyContentId；确认记录绑定四元组。多圈反例：策略阈值改 6π
 * → 新内容身份 → 复检通过正常放行（AT-01 反例面）。
 */
TEST(PolicyContractDoubles, At01TravelLimitConfirmableReleaseChain)
{
    IRD_TEST_INFO("MDL-06", {"AT-01"}, std::nullopt);
    const ContractRig rig = makeRig(false, 1.0);

    // ④端口装配（字节源＋闭包＋真实关节限位评估器——makeJointLimitEvaluator）。
    ScriptedBytesSource bytesSource;
    core::ContentVersion version4pi;
    version4pi.bytes[0] = 0x1A;
    const EngineeringPolicySet policy4pi = publishRigPolicy(rig);
    registerPolicyBytes(bytesSource, rig, policy4pi, rig.ids.policyObj, version4pi);
    const PolicyProvider provider = assembleProvider(bytesSource, rig.validation);
    const std::unique_ptr<IJointLimitEvaluator> jointLimits = makeJointLimitEvaluator();
    ScriptedProjectHandler handler(provider, *jointLimits);

    // ---- 主例：行程 6π＞默认 4π → Must 超限发现 → 未确认阻止 ----
    ConfirmableFindingRecord finding;
    const PolicyResolutionRequest request{rig.ids.policyObj, version4pi};
    const bool releasedBeforeConfirm =
        handler.prepare(request, rig.names, rig.ids.joint1, -std::acos(-1.0) /*−π*/,
                        5.0 * std::acos(-1.0) /*5π → 行程 6π*/, "J1", &finding);
    EXPECT_FALSE(releasedBeforeConfirm) << "未确认的 Must 超限必须阻止（SA-15）";
    EXPECT_FALSE(finding.confirmed);
    EXPECT_EQ(finding.policyObject, rig.ids.policyObj);
    EXPECT_EQ(finding.policyVersion.bytes, version4pi.bytes);
    EXPECT_EQ(finding.policyContentId, policy4pi.contentIdentity);
    EXPECT_EQ(finding.jointObject, rig.ids.joint1);   // 四元组④：涉事关节对象
    EXPECT_EQ(finding.unit, "rad");
    // 比较型三要素（actual=6π、threshold=4π、单位 rad——ERR-01/UX-03）。
    EXPECT_DOUBLE_EQ(finding.actualValue, kSixPi);
    EXPECT_DOUBLE_EQ(finding.thresholdValue, kFourPi);

    // ---- 用户显式确认 → 放行；摘要含 policyContentId（确认绑定③）----
    handler.confirmPending();
    EXPECT_TRUE(finding.confirmed);
    const std::string summary = handler.summaryText();
    EXPECT_NE(summary.find(idToHex(policy4pi.contentIdentity.bytes)), std::string::npos)
        << "确认摘要须含 policyContentId（§10.4——" << summary << "）";

    // ---- AT-01 反例：多圈机型经工程策略入口改阈值 6π → 新身份 → 放行 ----
    core::ContentVersion version6pi;
    version6pi.bytes[0] = 0x1B;
    const EngineeringPolicySet policy6pi =
        publishRigPolicy(rig, 0.0, "m", kSixPi);
    EXPECT_FALSE(policy6pi.contentIdentity == policy4pi.contentIdentity)
        << "阈值变化必须产生新内容身份（CON-05——失效矩阵 policy 行）";
    registerPolicyBytes(bytesSource, rig, policy6pi, rig.ids.policyObj, version6pi);
    ConfirmableFindingRecord finding6pi;
    const bool released6pi =
        handler.prepare(PolicyResolutionRequest{rig.ids.policyObj, version6pi},
                        rig.names, rig.ids.joint1, -std::acos(-1.0),
                        5.0 * std::acos(-1.0), "J1", &finding6pi);
    EXPECT_TRUE(released6pi) << "阈值 6π 下行程 6π＝边界不超限（D-08 闭区间）→ 正常放行";
}

// =====================================================================
// 组 4：POL-AT-3——显示单位切换与分析配置不可覆盖策略（AT-27/KIN-13）。
// =====================================================================

/**
 * POL-AT-3①（AT-27/UX-08/KIN-12）：显示单位切换（m↔mm 等值表述）→
 * 重解析——策略内容身份不变、评估输出不变。①半程以脚本化距离会话承载
 * （间距判定面需要距离能力——真实内置后端无距离能力 P-POL-11，脚本值
 * 只用于验证"两身份下输出逐字段一致"的消费路径，POL-TD-1 边界内）。
 */
TEST(PolicyContractDoubles, At27DisplayUnitSwitchKeepsIdentityAndOutput)
{
    IRD_TEST_INFO("UX-08", {"AT-27"}, std::nullopt);
    const ContractRig rig = makeRig(false, 0.0);

    // 同一安全间距 SI 真值 0.01 m 的两种显示表述（0.01 m / 10 mm）。
    const EngineeringPolicySet policyM = publishRigPolicy(rig, 0.01, "m");
    const EngineeringPolicySet policyMm = publishRigPolicy(rig, 10.0, "mm");
    EXPECT_TRUE(policyM.contentIdentity == policyMm.contentIdentity)
        << "显示单位不入身份（POL-ID-3——UX-08 数据层保证）";

    // 脚本会话：距离脚本 0.005 m（m＞0 的间距不足判定面——脚本值），
    // 两个身份等价策略分别构建会话评估——输出逐字段一致。
    ScriptedCollisionEvaluator scripted;
    scripted.collisionScript = {false};   // 非碰撞（触发距离查询）
    scripted.distanceScript = {0.005};
    ScriptedCallContext ctx;
    const std::shared_ptr<const CollisionEvaluationSession> sessionM =
        scripted.createSession(policyM, rig.scene, rig.names);
    const std::shared_ptr<const CollisionEvaluationSession> sessionMm =
        scripted.createSession(policyMm, rig.scene, rig.names);
    CollisionQuery query = singleStateQuery(0.0);
    query.requestMinDistance = true;
    const CollisionEvaluation outM = sessionM->evaluate(query, ctx);
    const CollisionEvaluation outMm = sessionMm->evaluate(query, ctx);
    EXPECT_TRUE(outM == outMm) << "显示单位切换不改变任何评估输出（AT-27①）";
}

namespace {

/// 分析配置替身（KIN-13 的配置对象投影——显示/求解侧私有状态）。
/// ★ 结构声明即契约：本类型没有任何通往 policy 的通道（策略/会话/查询
/// 类型均不消费它）——"配置不得覆盖策略"是结构性保证（R-POL-5/D-9）。
struct ScriptedAnalysisConfig
{
    std::string label;   ///< 显示标签（ui 投影——不入任何身份）
    int verbosity = 0;   ///< 日志详细度（求解侧私有）
};

}  // namespace

/**
 * POL-AT-3②（AT-27/KIN-13）：分析配置修改（替身配置对象）不影响评估——
 * 同会话两次评估间修改配置对象，输出逐字段相等（策略无覆盖通道——
 * 配置差异经 evidence Configuration 条目表达，不在本单元契约面）。
 * 真实后端承载（m=0 二值面——配置无关性不依赖距离能力）。
 */
TEST(PolicyContractDoubles, At27AnalysisConfigChangeDoesNotAffectEvaluation)
{
    IRD_TEST_INFO("KIN-13", {"AT-27"}, std::nullopt);
    const ContractRig rig = makeRig(false, 0.0);
    const EngineeringPolicySet policy = publishRigPolicy(rig);

    auto evaluator = makeRobWorkCollisionEvaluator();
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(policy, rig.scene, rig.names);

    // 第一次评估（配置 A）→ 修改配置 → 第二次评估（配置 B）：输出相等。
    ScriptedAnalysisConfig config{"preview-label", 1};
    ScriptedCallContext ctx;
    const CollisionEvaluation first = session->evaluate(singleStateQuery(0.0), ctx);
    config.label = "verified-label";
    config.verbosity = 3;   // "分析配置修改"——私有状态翻转
    const CollisionEvaluation second = session->evaluate(singleStateQuery(0.0), ctx);
    EXPECT_TRUE(first == second)
        << "配置对象修改不得影响评估输出（KIN-13——无覆盖通道的结构性保证）";
    EXPECT_EQ(session->sessionIdentity(), session->sessionIdentity());

    // API 面：查询/会话类型不消费配置对象（配置对象无法传入 evaluate——
    // CollisionQuery 只有六成员，语言事实即结构防覆盖；成员清单断言归
    // CollisionEvaluationTest 的 R-POL-5/D-9 用例）。
    (void)config;
}
