/**
 * @file   CollisionSessionTest.cpp
 * @brief  碰撞场景与会话构建期用例组（POL-T06）——POL-SCOPE-1/2 构建期部分
 *         （作用域矩阵展开与冲突拒绝）＋场景校验守卫＋会话身份确定性＋
 *         检测器初始化（R-POL-3/POL-EVAL-8 构建期前提）。
 *
 * 设计依据：
 *   - units/policy.md §11（POL-SCOPE-1/2 用例行——本套件交付其**构建期**
 *     部分：作用域展开产物与冲突拒绝；运行期②段归 POL-T07 评估套件）、
 *     §6.1（场景校验四项）、§6.4（会话三步与确定性）、§7.1（作用域矩阵）、
 *     §7.2（两层防御——解析期可检子集之后的会话构建同码复核）、§9.3
 *     （唯一实现入口与复现要素）、§12 POL-T06 行（完成条件＝作用域矩阵
 *     展开与冲突拒绝用例通过）
 *   - traceability/foundation-api-diff.md CR-04（CollisionScene/
 *     IPolicyNameContext 映射——替身按映射语义应答：Expected 错误→nullopt、
 *     WorkCell 共享只读、sceneContentIdentity 由"runtime 侧"值传入）
 *   - 需求 MDL-04（自碰撞配置——Self 域）、MDL-15（环境对象显式引用参与
 *     ——Environment 域）、ARC-05（唯一实现）、ARC-04（不可解析不猜测）、
 *     KIN-05（无几何≠无碰撞——构建期不据几何收窄）
 *   - 任务契约 tasks/foundation/POL-T06.json acceptance 1/2：
 *     ①POL-SCOPE-1/2 构建期用例；②CR-04 映射适配——契约夹具以替身先行，
 *     policy 零 runtime 编译依赖（BuildRedLineTest NoCrossUnitInclude 对
 *     新头自动扫描——白名单 {core, policy}；本文件不 include 任何 runtime
 *     /rwsim 头，WorkCell 为 RobWork 基线类型——CR-04 值传递契约面）
 *
 * 用例追溯命名（DTB §5.5）：用例名尾部带需求/用例组编号，断言处注明验证
 * 条款。替身边界声明（POL-TD-1 精神）：本套件的受控替身只替代"名称映射"
 * （TestNameContext——模拟 runtime ⑥端口应答）与"主链设备"（FakeDevice
 * ——只提供设备名注册，不提供运动学；会话构建只消费 findDevice 存在性），
 * 不冒充碰撞算法/几何语义——真实数值正确性归 POL-T07 内置后端解析算例。
 * 本套件为**集成模式专属**（消费 rw 非模板类 WorkCell/Device——冒烟模式
 * 不编译本文件，CMake POL-T06 增列注释同源）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyInput.hpp>
#include <sdurws/ird/policy/PolicyParsing.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

#include <rw/models/Device.hpp>
#include <rw/models/WorkCell.hpp>
#include <rw/proximity/ProximitySetup.hpp>
#include <rw/proximity/ProximitySetupRule.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace sdurws::ird::policy;

/// core 契约类型的短别名（测试可读性，PolicyPortTest 同款）。
namespace core = sdurws::ird::core;

namespace {

// =====================================================================
// 受控替身（test-local——POL-TD-1 边界见文件头）。
// =====================================================================

/// 手工构造的有效对象身份（首字节打标——非零有效且字节字典序可控）。
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
 * @brief 名称映射替身（IPolicyNameContext 测试实现——CR-04 适配语义演示：
 *        内部表模拟 runtime IRuntimeNameResolver 应答，Expected 错误→
 *        nullopt，无前缀拼拆——R-4；与 PolicyPortTest 同款结构）。
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
 * @brief 修订闭包替身（IPolicyValidationContext 测试实现——发布路径
 *        resolvePolicy 的应答表；角色应答与场景角色**可故意不一致**——
 *        POL-SCOPE-2 两层防御用例的差异面注入点）。
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
 * @brief 主链设备替身（rw::models::Device 测试实现——只提供设备名注册；
 *        会话构建仅消费 WorkCell::findDevice 的存在性核对（§6.1 主链设备
 *        解析），不消费任何运动学能力——未实现方法以 std::logic_error
 *        显性暴露"替身边界外调用"（POL-TD-1：不冒充运动学语义）。
 */
class FakeDevice final : public rw::models::Device {
public:
    explicit FakeDevice(const std::string& name)
        : rw::models::Device(name)
    {
    }

    std::size_t getDOF() const override { return 0; }   ///< 名义设备——零自由度（诚实值）

    // ---- 以下方法会话构建期不可达：越界调用＝替身边界违约（显性失败） ----
    void setQ(const rw::math::Q&, rw::kinematics::State&) const override
    {
        throw std::logic_error("FakeDevice 替身边界：setQ 不被会话构建消费");
    }
    rw::math::Q getQ(const rw::kinematics::State&) const override
    {
        throw std::logic_error("FakeDevice 替身边界：getQ 不被会话构建消费");
    }
    QBox getBounds() const override
    {
        throw std::logic_error("FakeDevice 替身边界：getBounds 不被会话构建消费");
    }
    void setBounds(const QBox&) override
    {
        throw std::logic_error("FakeDevice 替身边界：setBounds 不被会话构建消费");
    }
    rw::math::Q getVelocityLimits() const override
    {
        throw std::logic_error("FakeDevice 替身边界：getVelocityLimits 不被会话构建消费");
    }
    void setVelocityLimits(const rw::math::Q&) override
    {
        throw std::logic_error("FakeDevice 替身边界：setVelocityLimits 不被会话构建消费");
    }
    rw::math::Q getAccelerationLimits() const override
    {
        throw std::logic_error("FakeDevice 替身边界：getAccelerationLimits 不被会话构建消费");
    }
    void setAccelerationLimits(const rw::math::Q&) override
    {
        throw std::logic_error("FakeDevice 替身边界：setAccelerationLimits 不被会话构建消费");
    }
    rw::kinematics::Frame* getBase() override
    {
        throw std::logic_error("FakeDevice 替身边界：getBase 不被会话构建消费");
    }
    const rw::kinematics::Frame* getBase() const override
    {
        throw std::logic_error("FakeDevice 替身边界：getBase(const) 不被会话构建消费");
    }
    rw::kinematics::Frame* getEnd() override
    {
        throw std::logic_error("FakeDevice 替身边界：getEnd 不被会话构建消费");
    }
    const rw::kinematics::Frame* getEnd() const override
    {
        throw std::logic_error("FakeDevice 替身边界：getEnd(const) 不被会话构建消费");
    }
    rw::math::Jacobian baseJend(const rw::kinematics::State&) const override
    {
        throw std::logic_error("FakeDevice 替身边界：baseJend 不被会话构建消费");
    }
    rw::core::Ptr<rw::models::JacobianCalculator>
    baseJCframes(const std::vector<rw::kinematics::Frame*>&,
                 const rw::kinematics::State&) const override
    {
        throw std::logic_error("FakeDevice 替身边界：baseJCframes 不被会话构建消费");
    }
};

// =====================================================================
// 场景/夹具装配（CR-04 值传递契约的测试形态：场景对象清单、相邻对、
// sceneContentIdentity 均由"调用方"（本夹具扮演 L5/请求方）装配传入）。
// =====================================================================

/// 标准场景对象身份集（字节打标——跨用例稳定，定位可复现）。
struct SceneIds {
    core::ObjectId link1 = taggedObjectId(0x11);      ///< 机器人连杆 1（RobotLink）
    core::ObjectId link2 = taggedObjectId(0x12);      ///< 机器人连杆 2（RobotLink）
    core::ObjectId link3 = taggedObjectId(0x13);      ///< 机器人连杆 3（RobotLink）
    core::ObjectId tool = taggedObjectId(0x14);       ///< 工具（Tool）
    core::ObjectId payload = taggedObjectId(0x15);    ///< 负载（Payload）
    core::ObjectId env1 = taggedObjectId(0x16);       ///< 环境对象 1（EnvironmentObject）
    core::ObjectId env2 = taggedObjectId(0x17);       ///< 环境对象 2（EnvironmentObject）
    core::ObjectId workpiece = taggedObjectId(0x18);  ///< 工件（Workpiece）
    core::ObjectId device = taggedObjectId(0x19);     ///< 主链设备对象（经名称上下文解析）
};

/**
 * @brief 装配标准碰撞场景（§6.1 五字段全量——CR-04 映射的测试承载：
 *        workcell 共享只读指针、"runtime 计算的"场景内容身份值传递、
 *        对象清单/相邻对自"编译产物事实"装配）。
 *
 * @param ids        [in] 对象身份集
 * @param deviceName [in] 主链设备在 workcell 内的注册名（默认 "Robot"——
 *                   NAME-UNRESOLVED 断链用例传其他值）
 * @param mapDevice  [in] 名称映射是否登记主链设备（默认 true）
 */
CollisionScene makeScene(const SceneIds& ids, const std::string& deviceName = "Robot",
                         bool mapDevice = true)
{
    // WorkCell 替身场景：真实基线类型（集成模式）＋FakeDevice 注册——
    // 会话构建只消费 findDevice 的名称存在性（§6.1 主链设备核对）。
    auto workcell = std::make_shared<rw::models::WorkCell>("PolicySessionTestWC");
    workcell->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("Robot")));

    CollisionScene scene;
    scene.workcell = workcell;                    // CR-04：共享只读（替身直接持有）
    scene.primaryDevice = ids.device;
    scene.objects = {
        {ids.link1, "Link1", SceneObjectRole::RobotLink, true},
        {ids.link2, "Link2", SceneObjectRole::RobotLink, true},
        {ids.link3, "Link3", SceneObjectRole::RobotLink, true},
        {ids.tool, "Tool", SceneObjectRole::Tool, true},
        {ids.payload, "Payload", SceneObjectRole::Payload, true},
        {ids.env1, "Env1", SceneObjectRole::EnvironmentObject, true},
        {ids.env2, "Env2", SceneObjectRole::EnvironmentObject, true},
        {ids.workpiece, "Workpiece", SceneObjectRole::Workpiece, true},
    };
    // 运动学相邻事实（模型事实——相邻链 l1-l2-l3；§7.1 默认相邻过滤消费源）。
    scene.adjacentLinkPairs = {{ids.link1, ids.link2}, {ids.link2, ids.link3}};
    // 场景内容身份（CR-04 裁决：值＝"runtime" workCellCompileIdentity——
    // 夹具以打标值扮演 runtime 计算结果，policy 不重算）。
    scene.sceneContentIdentity = taggedContentIdentity(0xC0);
    // deviceName/mapDevice 供断链用例注入（非默认装配形态——见参数注释）。
    static_cast<void>(deviceName);
    static_cast<void>(mapDevice);
    return scene;
}

/// 装配标准名称映射（全部场景对象＋主链设备的双向映射；身份打标 0x5E）。
TestNameContext makeNames(const SceneIds& ids, const std::string& deviceName = "Robot")
{
    TestNameContext names;
    names.mapIdentity = taggedContentIdentity(0x5E);
    names.byName["Robot"] = ids.device;
    names.byId[ids.device] = deviceName;
    names.byId[ids.link1] = "Robot/Link1";
    names.byId[ids.link2] = "Robot/Link2";
    names.byId[ids.link3] = "Robot/Link3";
    names.byId[ids.tool] = "Tool";
    names.byId[ids.payload] = "Payload";
    names.byId[ids.env1] = "Env1";
    names.byId[ids.env2] = "Env2";
    names.byId[ids.workpiece] = "Workpiece";
    return names;
}

/**
 * @brief 装配标准闭包应答表（全部场景对象存在＋角色与场景一致——
 *        解析⑤对象存在性/角色核对的正向应答；两层防御用例在此注入差异）。
 */
TestValidationContext makeContext(const SceneIds& ids)
{
    TestValidationContext ctx;
    const std::pair<const core::ObjectId*, const char*> entries[] = {
        {&ids.link1, "RobotLink"},   {&ids.link2, "RobotLink"},
        {&ids.link3, "RobotLink"},   {&ids.tool, "Tool"},
        {&ids.payload, "Payload"},   {&ids.env1, "EnvironmentObject"},
        {&ids.env2, "EnvironmentObject"}, {&ids.workpiece, "Workpiece"},
        {&ids.device, "RobotLink"},
    };
    for (const auto& [id, role] : entries) {
        ctx.existingObjects[*id] = true;
        ctx.roles[*id] = role;
    }
    return ctx;
}

/**
 * @brief 基线策略输入（可发布形态——显式安全间距避免 Info 级默认告知；
 *        规则/域由用例参数化后填入）。
 */
RawPolicyInput baseInput(const SceneIds& ids)
{
    RawPolicyInput in;
    in.schemaVersion = PolicySchema::currentVersion;
    in.policyObject = taggedObjectId(0x10);
    in.collision.enabled = true;
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool};
    in.collision.safetyClearance = RawThresholdInput{0.02, "m"};
    in.collision.excludeAdjacentLinksByDefault = true;
    // 阈值全显式（无 Info 噪声——发布判定纯净）。
    in.jointThresholds.nearLimitRatio = RawThresholdInput{0.05, ""};
    in.jointThresholds.conditionNumberWarning = RawThresholdInput{10.0, ""};
    in.jointThresholds.finiteRotationTravelLimit = RawThresholdInput{12.566370614359172, "rad"};
    return in;
}

/// 经七段解析管线发布策略（§5.1 唯一发布路径——内容身份由管线⑥计算）。
/// 夹具自检失败以异常显性暴露（gtest 捕获后判该用例失败——可归因）。
EngineeringPolicySet publishPolicy(const RawPolicyInput& in, const TestValidationContext& ctx)
{
    const PolicyParseResult res = resolvePolicy(in, ctx);
    if (!res.policy.has_value()) {
        throw std::runtime_error(
            "夹具策略应可发布，首条诊断: "
            + (res.diagnostics.empty() ? std::string{"-"} : res.diagnostics.front().code));
    }
    return *res.policy;
}

/// 构建会话的便捷入口（唯一实现入口 makeRobWorkCollisionEvaluator——§6.5；
/// 断言会话非空后返回构建期访问器可用的共享指针）。
std::shared_ptr<const CollisionEvaluationSession>
buildSession(const EngineeringPolicySet& policy, const CollisionScene& scene,
             const IPolicyNameContext& names)
{
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    return evaluator->createSession(policy, scene, names);
}

/// 在规范对集中查找指定无序对象对（断言可读性辅助；未找到返回 nullptr）。
const ScopedCollisionPair* findPair(const ResolvedCollisionScope& scope,
                                    const core::ObjectId& a, const core::ObjectId& b)
{
    for (const ScopedCollisionPair& p : scope.pairs) {
        if ((p.objectA == a && p.objectB == b) || (p.objectA == b && p.objectB == a)) {
            return &p;
        }
    }
    return nullptr;
}

/// 取异常的稳定码辅助已在各用例内联（try/catch 断言）——无共享形式。

}  // namespace

// =====================================================================
// POL-SCOPE-1 构建期：作用域矩阵展开（域区分/coverage 分域计数素材）。
// =====================================================================

/**
 * POL-SCOPE-1 构建期（MDL-04/15/§7.1）：策略仅启用 Self 域——展开后仅
 * 连杆对进入作用域（pairsInScope 分域素材），工具/负载/环境/工件对零
 * 输出资格（Environment/Tool/Scene 域计数全零）；相邻对被默认过滤并留痕
 * （ruleOrigin="default.adjacent-links"），非相邻连杆对默认必检。
 */
TEST(CollisionSessionScope, SelfDomainOnlyExpandsLinkPairsOnly_POL_SCOPE_1)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);

    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Self};   // 仅启用 Self 域
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, makeScene(ids), names);

    const ResolvedCollisionScope& scope = session->scope();
    // 域区分：全部展开对的域＝Self 且两端角色均为 RobotLink（§7.1 行 1/2）。
    for (const ScopedCollisionPair& p : scope.pairs) {
        EXPECT_EQ(p.domain, CollisionDomain::Self) << "仅 Self 域可有展开对";
        EXPECT_EQ(p.roleA, SceneObjectRole::RobotLink);
        EXPECT_EQ(p.roleB, SceneObjectRole::RobotLink);
    }
    // 连杆对全集＝C(3,2)=3：非相邻 1 对默认必检＋相邻 2 对默认过滤。
    EXPECT_EQ(scope.pairs.size(), std::size_t{3});
    EXPECT_EQ(scope.inScopePairCount(), std::size_t{1});
    EXPECT_EQ(scope.excludedPairCount(), std::size_t{2});
    // 非相邻连杆对（link1-link3）默认必检。
    const ScopedCollisionPair* nonAdjacent = findPair(scope, ids.link1, ids.link3);
    ASSERT_NE(nonAdjacent, nullptr);
    EXPECT_EQ(nonAdjacent->status, ScopePairStatus::InScopeDefault);
    // 相邻连杆对默认过滤＋留痕（§7.2 ruleOrigin="default.adjacent-links"；
    // 模型相邻事实消费——MDL-04 自碰撞配置经域启用表达）。
    const ScopedCollisionPair* adj12 = findPair(scope, ids.link1, ids.link2);
    ASSERT_NE(adj12, nullptr);
    EXPECT_EQ(adj12->status, ScopePairStatus::ExcludedByAdjacencyDefault);
    EXPECT_EQ(adj12->filterReason, "default.adjacent-links");
    const ScopedCollisionPair* adj23 = findPair(scope, ids.link2, ids.link3);
    ASSERT_NE(adj23, nullptr);
    EXPECT_EQ(adj23->status, ScopePairStatus::ExcludedByAdjacencyDefault);
    // Environment/Tool/Scene 对零输出资格（§11 POL-SCOPE-1 观测点——
    // 分域计数全零；无几何/域禁用的收窄归评估期 coverage，构建期不收窄）。
    EXPECT_EQ(scope.inScopePairCount(CollisionDomain::Environment), std::size_t{0});
    EXPECT_EQ(scope.inScopePairCount(CollisionDomain::Tool), std::size_t{0});
    EXPECT_EQ(scope.inScopePairCount(CollisionDomain::Scene), std::size_t{0});
    // 域禁用 voids 该域全部对（§7.1 不适用条件列——工具×工件对随 Tool 域
    // 禁用整体不入作用域，规则是否引用不再有语义）。
    EXPECT_EQ(findPair(scope, ids.tool, ids.workpiece), nullptr);
    // collisionEnabled 如实投影（§4.3 总开关）。
    EXPECT_TRUE(session->collisionEnabled());
}

/**
 * POL-SCOPE-1 构建期（§7.1 矩阵逐行）：三域启用（Self/Tool/Environment、
 * Scene 未启用）——矩阵行归属逐对断言（工具/负载×连杆→Tool；工具/负载×
 * 工件→Tool；连杆×环境/工件→Environment；环境×工件→Scene〔未启用不入〕；
 * 工具×负载矩阵未列不入）；相邻对默认过滤仅及 Self/Tool 域。
 */
TEST(CollisionSessionScope, ThreeDomainMatrixRowAssignment_POL_SCOPE_1)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    // 场景裁剪：link1-link2 相邻＋tool＋payload＋env1＋workpiece（6 对象）。
    CollisionScene scene;
    auto workcell = std::make_shared<rw::models::WorkCell>("PolicySessionTestWC");
    workcell->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("Robot")));
    scene.workcell = workcell;   // 共享只读指针（const 化）——CR-04 消费形态
    scene.primaryDevice = ids.device;
    scene.objects = {
        {ids.link1, "Link1", SceneObjectRole::RobotLink, true},
        {ids.link2, "Link2", SceneObjectRole::RobotLink, true},
        {ids.tool, "Tool", SceneObjectRole::Tool, true},
        {ids.payload, "Payload", SceneObjectRole::Payload, true},
        {ids.env1, "Env1", SceneObjectRole::EnvironmentObject, true},
        {ids.workpiece, "Workpiece", SceneObjectRole::Workpiece, true},
    };
    scene.adjacentLinkPairs = {{ids.link1, ids.link2}};
    scene.sceneContentIdentity = taggedContentIdentity(0xC0);

    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Tool,
                                   CollisionDomain::Environment};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, scene, makeNames(ids));
    const ResolvedCollisionScope& scope = session->scope();

    // 矩阵行归属逐对断言（§7.1 行 1～6——含工件角色归环境侧的 Tool 行）。
    const ScopedCollisionPair* toolVsLink = findPair(scope, ids.link1, ids.tool);
    ASSERT_NE(toolVsLink, nullptr);
    EXPECT_EQ(toolVsLink->domain, CollisionDomain::Tool);        // 工具 vs 非相邻连杆
    EXPECT_EQ(toolVsLink->status, ScopePairStatus::InScopeDefault);
    const ScopedCollisionPair* toolVsWorkpiece = findPair(scope, ids.tool, ids.workpiece);
    ASSERT_NE(toolVsWorkpiece, nullptr);
    EXPECT_EQ(toolVsWorkpiece->domain, CollisionDomain::Tool);   // 工具 vs 工件（Tool 显式行）
    const ScopedCollisionPair* linkVsEnv = findPair(scope, ids.link2, ids.env1);
    ASSERT_NE(linkVsEnv, nullptr);
    EXPECT_EQ(linkVsEnv->domain, CollisionDomain::Environment);  // 连杆 vs 环境对象
    const ScopedCollisionPair* toolVsEnv = findPair(scope, ids.tool, ids.env1);
    ASSERT_NE(toolVsEnv, nullptr);
    EXPECT_EQ(toolVsEnv->domain, CollisionDomain::Environment);  // 机器人侧 vs 环境对象
    const ScopedCollisionPair* linkVsWorkpiece = findPair(scope, ids.link1, ids.workpiece);
    ASSERT_NE(linkVsWorkpiece, nullptr);
    EXPECT_EQ(linkVsWorkpiece->domain, CollisionDomain::Environment);  // 连杆 vs 工件
    const ScopedCollisionPair* payloadVsWorkpiece = findPair(scope, ids.payload, ids.workpiece);
    ASSERT_NE(payloadVsWorkpiece, nullptr);
    EXPECT_EQ(payloadVsWorkpiece->domain, CollisionDomain::Tool);      // 负载 vs 工件
    // 相邻过滤仅及 Self/Tool 域（§7.1：Environment 行只允许逐对显式排除）
    // ——本场景唯一相邻对为 Self 域连杆对。
    const ScopedCollisionPair* adjacent = findPair(scope, ids.link1, ids.link2);
    ASSERT_NE(adjacent, nullptr);
    EXPECT_EQ(adjacent->status, ScopePairStatus::ExcludedByAdjacencyDefault);
    // 矩阵未列/未启用域的组合不入作用域（工具×负载；Scene 域未启用——
    // 环境×工件对零资格）。
    EXPECT_EQ(findPair(scope, ids.tool, ids.payload), nullptr)
        << "工具×负载＝§7.1 矩阵未列组合";
    EXPECT_EQ(findPair(scope, ids.env1, ids.workpiece), nullptr)
        << "Scene 域未启用——环境×工件不入";
    // 展开对全量规范序（字节字典序——NFR-COR-02 稳定排序基础）。
    for (std::size_t i = 1; i < scope.pairs.size(); ++i) {
        const ScopedCollisionPair& prev = scope.pairs[i - 1];
        const ScopedCollisionPair& curr = scope.pairs[i];
        EXPECT_TRUE(prev.objectA < curr.objectA
                    || (prev.objectA == curr.objectA && prev.objectB < curr.objectB))
            << "展开产物应按 (objectA, objectB) 规范序";
    }
}

/**
 * POL-SCOPE-1 构建期（§7.1 Scene 行）：环境域对默认不检（静态场景无相对
 * 运动），显式 mandatoryPairs 可启用——Scene 域仅必检对进入作用域
 * （InScopeDefault 在 Scene 域不存在）。
 */
TEST(CollisionSessionScope, SceneDomainOnlyMandatoryEnters_POL_SCOPE_1)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    // 场景裁剪：link1＋env1＋env2＋workpiece。
    CollisionScene scene;
    auto workcell = std::make_shared<rw::models::WorkCell>("PolicySessionTestWC");
    workcell->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("Robot")));
    scene.workcell = workcell;   // 共享只读指针（const 化）——CR-04 消费形态
    scene.primaryDevice = ids.device;
    scene.objects = {
        {ids.link1, "Link1", SceneObjectRole::RobotLink, true},
        {ids.env1, "Env1", SceneObjectRole::EnvironmentObject, true},
        {ids.env2, "Env2", SceneObjectRole::EnvironmentObject, true},
        {ids.workpiece, "Workpiece", SceneObjectRole::Workpiece, true},
    };
    scene.sceneContentIdentity = taggedContentIdentity(0xC0);

    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Self, CollisionDomain::Environment,
                                   CollisionDomain::Tool, CollisionDomain::Scene};
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.env1), ScopeTarget::makeObject(ids.env2),
                       PolicyRuleLevel::Must, "工艺干涉必检")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, scene, names);
    const ResolvedCollisionScope& scope = session->scope();

    // 环境对象间（Scene 域）：仅显式必检对进入，状态＝Mandatory。
    const ScopedCollisionPair* mandated = findPair(scope, ids.env1, ids.env2);
    ASSERT_NE(mandated, nullptr);
    EXPECT_EQ(mandated->domain, CollisionDomain::Scene);
    EXPECT_EQ(mandated->status, ScopePairStatus::Mandatory);
    EXPECT_EQ(mandated->ruleIndex, std::size_t{0});
    // 其余 Scene 对（env×workpiece）未命中必检——不入作用域（默认不检）。
    EXPECT_EQ(findPair(scope, ids.env1, ids.workpiece), nullptr);
    EXPECT_EQ(findPair(scope, ids.env2, ids.workpiece), nullptr);
    // Environment 域对照组：连杆×环境对默认必检（域启用即默认必检——§7.1）。
    EXPECT_EQ(scope.inScopePairCount(CollisionDomain::Environment), std::size_t{3});
    EXPECT_EQ(scope.inScopePairCount(CollisionDomain::Scene), std::size_t{1});
}

// =====================================================================
// POL-SCOPE-2 构建期：过滤留痕与冲突拒绝（两层防御）。
// =====================================================================

/**
 * POL-SCOPE-2 构建期（§7.2①前半）：显式排除对在解析期未被拒（目标不同
 * 形）时——展开后按 ExcludedByRule 留痕（ruleIndex＋策略理由原文），策略
 * 生成的 ProximitySetup 携带精确名 EXCLUDE 规则（R-POL-3：仅策略规则，
 * 无 RobWork 默认规则——静态对排除显式关闭）。
 */
TEST(CollisionSessionGuards, ExcludedByRuleKeepsTraceAndSetupExclude_POL_SCOPE_2)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    // 场景裁剪：link1＋tool＋workpiece（排除对＝工具×工件——夹持接触豁免）。
    CollisionScene scene;
    auto workcell = std::make_shared<rw::models::WorkCell>("PolicySessionTestWC");
    workcell->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("Robot")));
    scene.workcell = workcell;   // 共享只读指针（const 化）——CR-04 消费形态
    scene.primaryDevice = ids.device;
    scene.objects = {
        {ids.link1, "Link1", SceneObjectRole::RobotLink, true},
        {ids.tool, "Tool", SceneObjectRole::Tool, true},
        {ids.workpiece, "Workpiece", SceneObjectRole::Workpiece, true},
    };
    scene.sceneContentIdentity = taggedContentIdentity(0xC0);

    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Tool};
    in.collision.excludedPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.tool),
                       ScopeTarget::makeObject(ids.workpiece),
                       PolicyRuleLevel::Should, "夹持接触豁免")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, scene, names);
    const ResolvedCollisionScope& scope = session->scope();

    // 排除对留痕（§7.2②：AppliedFilterRecord 素材——ruleIndex＋理由原文；
    // "过滤不得隐藏"的结构保证：被排除对以 Excluded 状态显式在册）。
    const ScopedCollisionPair* excluded = findPair(scope, ids.tool, ids.workpiece);
    ASSERT_NE(excluded, nullptr);
    EXPECT_EQ(excluded->status, ScopePairStatus::ExcludedByRule);
    EXPECT_EQ(excluded->ruleIndex, std::size_t{0});
    EXPECT_EQ(excluded->filterReason, "夹持接触豁免");
    // 作用域内对照：工具×连杆未被排除——默认必检。
    const ScopedCollisionPair* inScope = findPair(scope, ids.link1, ids.tool);
    ASSERT_NE(inScope, nullptr);
    EXPECT_EQ(inScope->status, ScopePairStatus::InScopeDefault);
    EXPECT_EQ(scope.inScopePairCount(), std::size_t{1});
    EXPECT_EQ(scope.excludedPairCount(), std::size_t{1});

    // 检测器初始化核对（§6.4——ProximitySetup 由策略规则生成；精确完整名，
    // 无模式拼接——R-4）。本场景 setup 恰 1 条规则＝EXCLUDE(Tool, Workpiece)。
    const rw::proximity::ProximitySetup& setup = session->proximitySetup();
    ASSERT_EQ(setup.getProximitySetupRules().size(), std::size_t{1});
    const rw::proximity::ProximitySetupRule& rule = setup.getProximitySetupRules().front();
    EXPECT_EQ(rule.type(), rw::proximity::ProximitySetupRule::EXCLUDE_RULE);
    EXPECT_EQ(rule.getPatterns().first, "Tool");
    EXPECT_EQ(rule.getPatterns().second, "Workpiece");
    // R-POL-3：RobWork 默认过滤不进产品策略——静态对排除显式关闭。
    EXPECT_FALSE(setup.useExcludeStaticPairs());
}

/**
 * POL-SCOPE-2 构建期（§7.1 行 2/§7.2①）：相邻对被显式必检（mandatoryPairs
 * 声明）时不可被默认相邻过滤覆盖——状态＝Mandatory（"结构上不存在可隐藏
 * 必检的合法策略"），setup 以 INCLUDE 规则承载（基线过滤层纵深防御）；
 * 未必检的相邻对仍默认过滤（EXCLUDE 规则）。
 */
TEST(CollisionSessionGuards, MandatoryAdjacentPairSurvivesDefaultFilter_POL_SCOPE_2)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Self};
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.link1), ScopeTarget::makeObject(ids.link2),
                       PolicyRuleLevel::Must, "安装连接干涉必检")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, makeScene(ids), names);
    const ResolvedCollisionScope& scope = session->scope();

    // 必检相邻对：状态 Mandatory（默认相邻过滤不可覆盖——§7.2 必检对集）。
    const ScopedCollisionPair* mandated = findPair(scope, ids.link1, ids.link2);
    ASSERT_NE(mandated, nullptr);
    EXPECT_EQ(mandated->status, ScopePairStatus::Mandatory);
    EXPECT_EQ(mandated->ruleIndex, std::size_t{0});
    // 未必检相邻对：仍被默认过滤（模型事实消费——§7.1 行 2 可过滤列）。
    const ScopedCollisionPair* filtered = findPair(scope, ids.link2, ids.link3);
    ASSERT_NE(filtered, nullptr);
    EXPECT_EQ(filtered->status, ScopePairStatus::ExcludedByAdjacencyDefault);

    // setup 规则序＝展开产物规范序：INCLUDE(link1,link2) 在前、
    // EXCLUDE(link2,link3) 在后（规范序 0x11<0x12<0x13）。
    const rw::proximity::ProximitySetup& setup = session->proximitySetup();
    ASSERT_EQ(setup.getProximitySetupRules().size(), std::size_t{2});
    const rw::proximity::ProximitySetupRule& includeRule = setup.getProximitySetupRules()[0];
    EXPECT_EQ(includeRule.type(), rw::proximity::ProximitySetupRule::INCLUDE_RULE);
    EXPECT_EQ(includeRule.getPatterns().first, "Robot/Link1");
    EXPECT_EQ(includeRule.getPatterns().second, "Robot/Link2");
    const rw::proximity::ProximitySetupRule& excludeRule = setup.getProximitySetupRules()[1];
    EXPECT_EQ(excludeRule.type(), rw::proximity::ProximitySetupRule::EXCLUDE_RULE);
    EXPECT_EQ(excludeRule.getPatterns().first, "Robot/Link2");
    EXPECT_EQ(excludeRule.getPatterns().second, "Robot/Link3");
}

/**
 * POL-SCOPE-2 ①（§5.2 行 8/POL-PARSE-4——引用性锚定）：排除∩必检的直接
 * 对端冲突在解析期即拒绝发布（POL-T04 落位的同码面）——无可发布策略，
 * 会话构建不发生（两层防御的第一层，本用例回归钉住其码面）。
 */
TEST(CollisionSessionGuards, ParseTimeConflictStillRejected_POL_SCOPE_2)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    RawPolicyInput in = baseInput(ids);
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.tool), ScopeTarget::makeObject(ids.workpiece),
                       PolicyRuleLevel::Must, "干涉必检")};
    in.collision.excludedPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.tool), ScopeTarget::makeObject(ids.workpiece),
                       PolicyRuleLevel::Should, "夹持接触豁免")};
    const PolicyParseResult res = resolvePolicy(in, ctx);
    EXPECT_FALSE(res.policy.has_value()) << "冲突策略不得发布（§7.2 结构保证）";
    bool hasConflict = false;
    for (const core::DiagnosticRecord& d : res.diagnostics) {
        if (d.code == "POLICY-RULE-CONFLICT") {
            hasConflict = true;
        }
    }
    EXPECT_TRUE(hasConflict) << "同码面 POLICY-RULE-CONFLICT（§5.2 行 8）";
}

/**
 * POL-SCOPE-2 构建期（§7.2 会话构建期同码复核——两层防御第二层）：解析期
 * 上下文应答（objO 角色＝"Payload"）与场景装配事实（objO 角色＝Tool）不
 * 一致时，解析期不可判定的排除∩必检成员级相交在会话构建期以同码
 * POLICY-RULE-CONFLICT 拦截（"排除工具类"不得暗中吞掉必检对——policy.md
 * v0.5 ④登记的本层落点）。
 */
TEST(CollisionSessionGuards, SceneExpansionConflictRecheckRejectsTwoLayerDefense_POL_SCOPE_2)
{
    const SceneIds ids;
    // 场景裁剪：objO（Tool 角色）＋objB（RobotLink）＋link3。
    CollisionScene scene;
    auto workcell = std::make_shared<rw::models::WorkCell>("PolicySessionTestWC");
    workcell->addDevice(rw::core::Ptr<rw::models::Device>(new FakeDevice("Robot")));
    scene.workcell = workcell;   // 共享只读指针（const 化）——CR-04 消费形态
    scene.primaryDevice = ids.device;
    scene.objects = {
        {ids.tool, "ToolObject", SceneObjectRole::Tool, true},      // objO＝ids.tool
        {ids.link2, "LinkB", SceneObjectRole::RobotLink, true},     // objB＝ids.link2
        {ids.link3, "LinkC", SceneObjectRole::RobotLink, true},
    };
    scene.sceneContentIdentity = taggedContentIdentity(0xC0);

    // 解析期上下文：objO 应答"Payload"（与场景装配事实 Tool 不一致——
    // 差异注入点；解析期 Object×Role 覆盖等价判定据此放行）。
    TestValidationContext ctx = makeContext(ids);
    ctx.roles[ids.tool] = "Payload";

    RawPolicyInput in = baseInput(ids);
    in.collision.enabledDomains = {CollisionDomain::Tool, CollisionDomain::Self};
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.tool), ScopeTarget::makeObject(ids.link2),
                       PolicyRuleLevel::Must, "干涉必检")};
    in.collision.excludedPairs = {
        PairRule::make(ScopeTarget::makeRole("Tool"), ScopeTarget::makeObject(ids.link2),
                       PolicyRuleLevel::Should, "工具类豁免")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);

    // 会话构建：排除规则展开（场景内 Tool 角色对象＝{objO}）×objB ＝
    // {objO×objB} ＝ 必检展开集 → 相交即同码拒绝（ARC-05"过滤不得隐藏
    // 必检"的会话构建期机械保证）。
    try {
        buildSession(policy, scene, makeNames(ids));
        FAIL() << "展开集相交必须拒绝会话构建（§7.2 同码复核）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::RuleConflict);
        // 定位信息：双方规则下标＋理由＋冲突对象对（cause 可追溯——ERR-01）。
        const std::string what = e.what();
        EXPECT_NE(what.find("工具类豁免"), std::string::npos) << what;
        EXPECT_NE(what.find("干涉必检"), std::string::npos) << what;
        EXPECT_NE(what.find("mandatoryPairs"), std::string::npos) << what;
    }
}

// =====================================================================
// 场景校验守卫（§6.1 四项＋保守拒绝——会话构建失败 fail-fast 轨）。
// =====================================================================

/** §6.1 ②：策略规则引用对象不在场景清单（解析期闭包核对通过——场景装配
 *  缺失该对象）→ POLICY-CLL-SCENE-INVALID＋定位（清单/下标/对象规范文本）。 */
TEST(CollisionSessionGuards, RuleReferenceMissingFromSceneRejected)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    RawPolicyInput in = baseInput(ids);
    in.collision.mandatoryPairs = {
        PairRule::make(ScopeTarget::makeObject(ids.env1), ScopeTarget::makeObject(ids.link1),
                       PolicyRuleLevel::Must, "工艺干涉必检")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    // 场景装配遗漏 env1（解析期上下文应答其存在——两层事实源的差异在此
    // 由场景校验②拦截，定位信息指向缺失对象）。
    CollisionScene scene = makeScene(ids);
    auto& objects = scene.objects;
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&ids](const SceneObjectEntry& e) {
                                     return e.objectId == ids.env1;
                                 }),
                  objects.end());
    try {
        buildSession(policy, scene, names);
        FAIL() << "规则引用对象缺失必须拒绝会话构建（§6.1 ②）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SceneInvalid);
        const std::string what = e.what();
        EXPECT_NE(what.find("mandatoryPairs[0]"), std::string::npos) << what;
        EXPECT_NE(what.find(ids.env1.toCanonical()), std::string::npos) << what;
    }
}

/** §6.1 ①：objects 的 ObjectId 重复 → POLICY-CLL-SCENE-INVALID（subject
 *  定位由 cause 双 localName 承载）。 */
TEST(CollisionSessionGuards, DuplicateSceneObjectIdsRejected)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    CollisionScene scene = makeScene(ids);
    scene.objects.push_back({ids.link2, "Link2Dup", SceneObjectRole::RobotLink, true});
    try {
        buildSession(policy, scene, names);
        FAIL() << "对象身份重复必须拒绝会话构建（§6.1 ①）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SceneInvalid);
        EXPECT_NE(std::string(e.what()).find("Link2Dup"), std::string::npos);
    }
}

/** §6.1 ④：sceneContentIdentity 非空核对失败（全零保留值）→ 场景校验
 *  拒绝（CR-04：值由调用方传入，policy 只核对不重算）。 */
TEST(CollisionSessionGuards, EmptySceneContentIdentityRejected)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    CollisionScene scene = makeScene(ids);
    scene.sceneContentIdentity = core::ContentIdentity{};   // 全零＝未计算
    try {
        buildSession(policy, scene, names);
        FAIL() << "空场景内容身份必须拒绝会话构建（§6.1 ④）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SceneInvalid);
    }
}

/** §6.1 前提：workcell 编译产物缺失（空指针）→ 场景校验拒绝（§6.1 输入
 *  模型"输入是运行时快照（编译产物）"）。 */
TEST(CollisionSessionGuards, NullWorkcellRejected)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    CollisionScene scene = makeScene(ids);
    scene.workcell = nullptr;
    try {
        buildSession(policy, scene, names);
        FAIL() << "workcell 缺失必须拒绝会话构建（§6.1 输入模型前提）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SceneInvalid);
    }
}

/** §6.1 ③＋CR-04：primaryDevice 经名称上下文不可解析（Expected 错误→
 *  nullopt 原样呈现）→ POLICY-CLL-NAME-UNRESOLVED（不猜测——ARC-04）。 */
TEST(CollisionSessionGuards, PrimaryDeviceUnresolvableRejected_NAME_UNRESOLVED)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    TestNameContext names = makeNames(ids);
    names.byId.erase(ids.device);   // 映射缺失主链设备（runtime Expected 错误）
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    try {
        buildSession(policy, makeScene(ids), names);
        FAIL() << "主链设备不可解析必须拒绝会话构建（§6.1 ③）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::NameUnresolved);
    }
}

/** §7.5"名称不可解析"行：解析名在 workcell 内无对应设备（场景 Frame↔
 *  对象 ID 断链）→ 同码 POLICY-CLL-NAME-UNRESOLVED（findDevice 核对）。 */
TEST(CollisionSessionGuards, PrimaryDeviceNameNotADeviceRejected_NAME_UNRESOLVED)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    // 名称映射登记了 "GhostDevice"，workcell 内只有 "Robot"——映射与编译
    // 产物断链（装配侧不一致）。
    const TestNameContext names = makeNames(ids, "GhostDevice");
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    try {
        buildSession(policy, makeScene(ids, "GhostDevice"), names);
        FAIL() << "解析名无对应设备必须拒绝会话构建（§7.5 名称不可解析）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::NameUnresolved);
        EXPECT_NE(std::string(e.what()).find("GhostDevice"), std::string::npos);
    }
}

/** §6.1 ②Group 分支（policy.md v0.5 ⑥登记缺口的保守拒绝口径）：已定义组
 *  引用可发布（解析期 groupDefined 核对通过），但组成员数据不在会话输入
 *  内——会话构建保守拒绝（POLICY-CLL-SCENE-INVALID），不猜测、不静默
 *  收窄（ARC-04/"过滤不得隐藏必检"）；cause 携带组名与规则定位。 */
TEST(CollisionSessionGuards, GroupTargetConservativelyRejected)
{
    const SceneIds ids;
    TestValidationContext ctx = makeContext(ids);
    ctx.definedGroups = {{"jigs", true}};   // 组定义由装配侧持有（v0.5 ⑥）
    const TestNameContext names = makeNames(ids);
    RawPolicyInput in = baseInput(ids);
    in.collision.excludedPairs = {
        PairRule::make(ScopeTarget::makeGroup("jigs"), ScopeTarget::makeObject(ids.workpiece),
                       PolicyRuleLevel::Should, "工装豁免")};
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    try {
        buildSession(policy, makeScene(ids), names);
        FAIL() << "Group 目标在会话内不可展开——必须保守拒绝";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::SceneInvalid);
        const std::string what = e.what();
        EXPECT_NE(what.find("jigs"), std::string::npos) << what;
        EXPECT_NE(what.find("excludedPairs[0]"), std::string::npos) << what;
    }
}

/** 装配守卫：后端实例缺失（空 strategy——§6.4"检测器两半"的后端半区）→
 *  fail-fast（PolicyObjectInvalid——装配违约面）。 */
TEST(CollisionSessionGuards, NullBackendStrategyRejected)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    const CollisionScene scene = makeScene(ids);
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    // 直构会话（public 构造＝同参数等价会话入口）注入空后端——装配违约。
    try {
        const CollisionEvaluationSession session(policy, scene, names,
                                                 evaluator->backend(), nullptr);
        static_cast<void>(session);
        FAIL() << "空后端实例必须拒绝（装配违约 fail-fast）";
    }
    catch (const PolicyError& e) {
        EXPECT_EQ(e.code(), PolicyErrorCode::PolicyObjectInvalid);
    }
}

/** §4.3"enabled=false 整体停用"：会话可构建（§9.3 前置行只要求已发布＋
 *  场景校验），作用域为空、开关如实投影——"禁用被误调用"的评估期应答
 *  （CollisionDisabledByPolicy）归 POL-T07。 */
TEST(CollisionSessionScope, DisabledPolicyYieldsEmptyScope)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    RawPolicyInput in = baseInput(ids);
    in.collision.enabled = false;
    const EngineeringPolicySet policy = publishPolicy(in, ctx);
    const std::shared_ptr<const CollisionEvaluationSession> session =
        buildSession(policy, makeScene(ids), names);
    EXPECT_FALSE(session->collisionEnabled());
    EXPECT_TRUE(session->scope().pairs.empty());
    EXPECT_EQ(session->scope().inScopePairCount(), std::size_t{0});
}

// =====================================================================
// 会话身份与确定性（§6.4——POL-SHARE-1 的一致性基础）。
// =====================================================================

/** §6.4 确定性：同 (policy, scene, names, backend) 重复构建 → 会话身份
 *  与作用域展开逐字段相等（POL-SHARE-1 的等价会话基础）。 */
TEST(CollisionSessionIdentity, SameInputsYieldEqualSessionIdentity)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    const CollisionScene scene = makeScene(ids);
    const std::shared_ptr<const CollisionEvaluationSession> s1 =
        buildSession(policy, scene, names);
    const std::shared_ptr<const CollisionEvaluationSession> s2 =
        buildSession(policy, scene, names);
    EXPECT_EQ(s1->sessionIdentity(), s2->sessionIdentity());
    EXPECT_EQ(s1->sessionIdentity().isValid(), true) << "会话身份非全零（SHA-256 输出）";
    ASSERT_EQ(s1->scope().pairs.size(), s2->scope().pairs.size());
    for (std::size_t i = 0; i < s1->scope().pairs.size(); ++i) {
        EXPECT_EQ(s1->scope().pairs[i], s2->scope().pairs[i]) << "展开产物逐字段相等";
    }
    // 回填素材一致性（§6.2 场景/名称映射身份回填——CR-04 值传递原样）。
    EXPECT_EQ(s1->sceneIdentity(), scene.sceneContentIdentity);
    EXPECT_EQ(s1->nameMapIdentity(), names.mapIdentity);
}

/** §6.4 组成敏感性：场景身份/名称映射身份/策略内容任一变化 → 会话身份
 *  变化（证据绑定与缓存键可区分的前提——CON-06）。 */
TEST(CollisionSessionIdentity, CompositionChangesChangeSessionIdentity)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    const core::ContentIdentity baseIdentity =
        buildSession(policy, makeScene(ids), names)->sessionIdentity();

    // 场景身份变化（同模型不同编译批次——CR-04 值传递的新值）。
    CollisionScene scene2 = makeScene(ids);
    scene2.sceneContentIdentity = taggedContentIdentity(0xC1);
    EXPECT_NE(buildSession(policy, scene2, names)->sessionIdentity(), baseIdentity);

    // 名称映射身份变化（runtime 映射重建——CON-06 失效联动素材）。
    TestNameContext names2 = makeNames(ids);
    names2.mapIdentity = taggedContentIdentity(0x5F);
    EXPECT_NE(buildSession(policy, makeScene(ids), names2)->sessionIdentity(), baseIdentity);

    // 策略内容变化（安全间距 0.02→0.03——语义闭包变化，POL-ID-4 同源）。
    RawPolicyInput in2 = baseInput(ids);
    in2.collision.safetyClearance = RawThresholdInput{0.03, "m"};
    const EngineeringPolicySet policy2 = publishPolicy(in2, ctx);
    EXPECT_NE(buildSession(policy2, makeScene(ids), names)->sessionIdentity(), baseIdentity);
}

/** §6.4/展开确定性：场景对象清单承载序打乱 → 展开产物逐字节一致（规范
 *  序与承载序无关——同语义场景等价会话）。 */
TEST(CollisionSessionIdentity, SceneListOrderDoesNotChangeExpansion)
{
    const SceneIds ids;
    const TestValidationContext ctx = makeContext(ids);
    const TestNameContext names = makeNames(ids);
    const EngineeringPolicySet policy = publishPolicy(baseInput(ids), ctx);
    const CollisionScene scene = makeScene(ids);
    CollisionScene reversed = scene;
    std::reverse(reversed.objects.begin(), reversed.objects.end());
    const std::shared_ptr<const CollisionEvaluationSession> s1 =
        buildSession(policy, scene, names);
    const std::shared_ptr<const CollisionEvaluationSession> s2 =
        buildSession(policy, reversed, names);
    ASSERT_EQ(s1->scope().pairs.size(), s2->scope().pairs.size());
    for (std::size_t i = 0; i < s1->scope().pairs.size(); ++i) {
        EXPECT_EQ(s1->scope().pairs[i], s2->scope().pairs[i]) << "规范序与承载序无关";
    }
    EXPECT_EQ(s1->sessionIdentity(), s2->sessionIdentity());
}

// =====================================================================
// 唯一实现入口与复现要素（§6.5/§9.3/P-POL-5）。
// =====================================================================

/** §6.5/§8.1/P-POL-5：唯一构造入口产出唯一实现——backend 描述符字段冻结
 *  （backendId="rw.proximity.builtin-rw"；backendVersion 非〔RobWork 构建
 *  版本〕；toleranceModel 为登记串非工程阈值）；createSession 返回可构建
 *  会话（构建期能力正向链路）。 */
TEST(CollisionSessionDetector, UniqueEntryProducesBuiltinBackend)
{
    const std::unique_ptr<ICollisionEvaluator> evaluator = makeRobWorkCollisionEvaluator();
    const CollisionBackendDescriptor backend = evaluator->backend();
    EXPECT_EQ(backend.backendId, "rw.proximity.builtin-rw");
    EXPECT_FALSE(backend.backendVersion.empty());
    EXPECT_FALSE(backend.toleranceModel.empty());
    // 构建期正向链路（§9.3 createSession——同参数等价会话由确定性用例钉）。
    const SceneIds ids;
    const std::shared_ptr<const CollisionEvaluationSession> session =
        evaluator->createSession(publishPolicy(baseInput(ids), makeContext(ids)),
                                 makeScene(ids), makeNames(ids));
    ASSERT_TRUE(session != nullptr);
    EXPECT_EQ(session->backendDescriptor(), backend) << "会话复现要素与评估器同源";
}

/** 角色词表转发表（§6.1 同源词表——token↔枚举双向；精确等值、词表外
 *  nullopt 不猜测——ARC-04；与解析期 sceneObjectRoleTokens 同串面）。 */
TEST(CollisionSessionTokens, RoleTokenRoundTripExact)
{
    EXPECT_EQ(sceneObjectRoleToken(SceneObjectRole::RobotLink), "RobotLink");
    EXPECT_EQ(sceneObjectRoleToken(SceneObjectRole::Tool), "Tool");
    EXPECT_EQ(sceneObjectRoleToken(SceneObjectRole::Payload), "Payload");
    EXPECT_EQ(sceneObjectRoleToken(SceneObjectRole::EnvironmentObject), "EnvironmentObject");
    EXPECT_EQ(sceneObjectRoleToken(SceneObjectRole::Workpiece), "Workpiece");
    EXPECT_EQ(trySceneObjectRole("RobotLink"), std::optional<SceneObjectRole>(
                                                  SceneObjectRole::RobotLink));
    EXPECT_EQ(trySceneObjectRole("Workpiece"),
              std::optional<SceneObjectRole>(SceneObjectRole::Workpiece));
    // 精确等值（附录 D 第 12 项）：大小写/空白变体一律词表外。
    EXPECT_EQ(trySceneObjectRole("robotlink"), std::nullopt);
    EXPECT_EQ(trySceneObjectRole("RobotLink "), std::nullopt);
    EXPECT_EQ(trySceneObjectRole(""), std::nullopt);
}
