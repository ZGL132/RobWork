/**
 * @file   PolicyTestDoubles.hpp
 * @brief  policy 单元规范测试替身（§11 测试设施）——脚本化碰撞评估器
 *         ScriptedCollisionEvaluator／④端口替身 StubPolicyProvider 及其
 *         配套注入面替身（§9.3/§9.1 消费契约的可观测承载）。
 *
 * 设计依据：
 *   - units/policy.md §11 测试设施行：替身（header-only，
 *     policy/test/sdurws/ird/policy/testdouble/）——ScriptedCollisionEvaluator
 *     （按脚本返回预设 findings/Failed/Canceled/异常）、StubPolicyProvider；
 *     POL-TD-1 行（替身边界——见本文件头"替身边界声明"与 test/README.md）；
 *   - units/policy.md §9.1（IPolicyProvider 三方法契约）、§9.3
 *     （ICollisionEvaluator/CollisionEvaluationSession 契约）、§3.3/§9.7
 *     （四个最小注入接口——替身逐一适配其只读面）；
 *   - 任务契约 tasks/foundation/POL-T11.json（≙WP-07-T11）acceptance 1~3：
 *     §11 矩阵逐条留痕、POL-TD-1 边界声明在案、契约夹具数据集交付；
 *   - 命名空间与结构先例：runtime/test/RuntimeTestDoubles.hpp、
 *     evidence/test/EvidenceTestDoubles.hpp（同款 testdoubles 命名空间纪律）。
 *
 * ★ 替身边界声明（POL-TD-1，全文见 test/README.md §1）：
 *   本头全部类型只存在于测试面（policy/test/，不进产品库源码面——
 *   PolicyTestDoubleBoundary 机检用例扫描 src/＋include/ 零命中钉住）。
 *   ScriptedCollisionEvaluator 的脚本应答（碰撞/距离值、异常注入）是
 *   **契约形态输入**，仅用于验证评估状态机、稳定排序、过滤与消费方逻辑，
 *   **不构成任何碰撞算法正确性证明**——真实算法数值正确性由内置后端对
 *   构造场景（已知相交/分离几何）的解析算例验证（POL-T07 真实后端算例＋
 *   testdata/golden/pol-collision-analytic analytic-case 数据集）。替身
 *   不得被任何产品路径引用；脚本值不得被解读为几何真值。
 *
 * 为什么 ScriptedCollisionEvaluator 在"真实会话"上脚本化（设计取舍说明）：
 *   §9.3 的评估执行面是**具体类** CollisionEvaluationSession::evaluate
 *   （非虚函数——消费方按值/指针直接调用，无法整会话替换）。因此替身的
 *   脚本注入点选在会话的后端查询面（CollisionStrategy/DistanceStrategy
 *   ——RobWork 的策略接口，§5.1"RobWork 碰撞接口只经适配层接触"的适配
 *   缝）：ScriptedCollisionEvaluator 以脚本化后端构建**真实会话**，作用
 *   域展开/冲突复核/状态机/稳定排序全部走产品代码，只有几何应答（相交
 *   与否、距离值）被脚本化——"预设 findings/Failed/Canceled/异常"由此
 *   经真实评估逻辑自然产生（后端应答脚本→findings；脚本抛 rw 异常→
 *   Failed＋POLICY-CLL-EVALUATION-FAILED；消费方传入取消脚本上下文→
 *   Canceled）。这保证替身验证的是真实契约路径，而非替身自造的第二套
 *   评估逻辑（ARC-05 不做第二算法在测试面的延伸）。
 *
 * 与 CollisionEvaluationTest.cpp TU 内脚本后端的关系（POL-T07 既有设施）：
 *   本头是 §11 命名的**共享规范替身**（供契约套件与后续阶段 B 各业务域
 *   测试消费）；POL-T07 的 TU 内同型类按"既有验收面零改动"纪律保留——
 *   两者的替身边界声明同源（POL-TD-1），职责面不同（整评估器/端口替身
 *   vs 单后端脚本），不构成副本关系（收拢如需另立任务，不在 POL-T11
 *   allowedFiles 的行为面内）。
 *
 * 线程约束：本头全部替身为单线程设施（每实例仅在其所属测试线程使用——
 * §11 替身用例无并发面；并发面归 CollisionEvaluationTest 的 POL-CONC-1，
 * 其用真实后端＋互斥承载）。确定性：脚本按构造序回放，无时间/随机源
 * （NFR-COR-02 同源纪律）。
 */

#ifndef SDURWS_IRD_POLICY_TESTDOUBLE_POLICYSERVICETESTDOUBLES_HPP
#define SDURWS_IRD_POLICY_TESTDOUBLE_POLICYSERVICETESTDOUBLES_HPP

#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <rw/common/macros.hpp>
#include <rw/geometry/Geometry.hpp>
#include <rw/proximity/CollisionStrategy.hpp>
#include <rw/proximity/DistanceStrategy.hpp>
#include <rw/proximity/ProximityModel.hpp>
#include <rw/proximity/ProximityStrategyData.hpp>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <sdurws/ird/policy/CollisionEvaluator.hpp>
#include <sdurws/ird/policy/CollisionQuery.hpp>
#include <sdurws/ird/policy/Contexts.hpp>
#include <sdurws/ird/policy/Errors.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>

namespace sdurws::ird::policy::testdoubles {

// =====================================================================
// 注入面替身（§3.3 四接口的最小脚本实现——④端口装配与消费链的测试承载）。
// =====================================================================

/**
 * @brief 名称映射替身（IPolicyNameContext——§3.3 原文三查询的内存实现）。
 *
 * 双向表由测试用例直接填充；nameMapContentIdentity 返回预设身份（CON-06
 * 映射变化可观测的对照物）。线程约束：单线程（文件头声明）。
 */
class ScriptedNameContext final : public IPolicyNameContext {
public:
    /// 运行时名→对象身份（tryObjectId 应答表）。
    std::map<std::string, core::ObjectId> byName;
    /// 对象身份→运行时名（tryRuntimeName 应答表）。
    std::map<core::ObjectId, std::string> byId;
    /// 名称映射内容身份预设（nameMapContentIdentity 应答值）。
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

/**
 * @brief 修订闭包替身（IPolicyValidationContext——解析④⑤步闭包查询的
 *        应答表实现；发布路径 resolvePolicy 与 PolicyProvider 共用）。
 *
 * 应答确定性（§9.7 行——记忆化确定性的前提）：三张应答表在解析期间不变
 * 即满足"同键稳定"；测试负责在两次解析间不改表。
 */
class ScriptedValidationContext final : public IPolicyValidationContext {
public:
    /// objectExists 应答表（缺省键应答 false——对象不在闭包内）。
    std::map<core::ObjectId, bool> existingObjects;
    /// objectRole 应答表（角色 token——"RobotLink" 等场景角色词表串）。
    std::map<core::ObjectId, std::string> roles;
    /// groupDefined 应答表（显式组名→是否已定义）。
    std::map<std::string, bool> definedGroups;

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
 * @brief 对象字节源替身（IPolicyBytesSource——project ②端口 tryObject 的
 *        一比一内存投影；CR-03 适配语义的测试承载）。
 *
 * 键＝(ObjectId, ContentVersion) 字节对（内容编址——CON-05）；测试把
 * PolicyCodec::encode 的产物按版本登记，供 PolicyProvider 真实解析链
 * （取字节→decode→七段管线）消费。
 */
class ScriptedBytesSource final : public IPolicyBytesSource {
public:
    /// 字节登记表（键的 bytes 对——Id128 既有 operator< 保证确定序）。
    std::map<std::pair<core::ObjectId, core::ContentVersion>,
             std::vector<std::uint8_t>>
        bytesByKey;

    std::optional<std::vector<std::uint8_t>>
    tryObjectBytes(core::ObjectId object, core::ContentVersion version) const override
    {
        const auto it = bytesByKey.find({object, version});
        return it == bytesByKey.end() ? std::nullopt : std::optional<std::vector<std::uint8_t>>(it->second);
    }
};

/**
 * @brief 调用上下文替身（IPolicyCallContext——协作取消脚本＋存活标志）。
 *
 * 取消脚本语义（与 POL-T07 ScriptedCallContext 同口径）：cancellationRequested()
 * 第 1..cancelAfterQueries 次调用返回 false，此后恒 true——evaluate 在每
 * 样本边界恰查询一次，故 cancelAfterQueries=N ⇔ 样本 0..N-1 执行、样本 N
 * 起取消命中（§11 POL-EVAL-6"第 N 样本后 cancellationRequested=true"的
 * 注入点）。aliveFlag=false 复现迟到调用（POL-LATE-1）。
 */
class ScriptedCallContext final : public IPolicyCallContext {
public:
    /// 取消阈值（第 cancelAfterQueries 次查询之后取消命中；max()＝永不取消）。
    std::size_t cancelAfterQueries = std::numeric_limits<std::size_t>::max();
    /// 存活标志（false＝上下文已失效——迟到调用拒绝的注入面）。
    bool aliveFlag = true;
    /// 查询计数（可观测——断言"样本边界恰一次查询"时使用）。
    mutable std::size_t queryCount = 0;

    bool cancellationRequested() const override
    {
        ++queryCount;
        return queryCount > cancelAfterQueries;
    }

    bool alive() const override { return aliveFlag; }
};

// =====================================================================
// 脚本化碰撞/距离后端（§11"按脚本"的几何应答承载——真实会话的注入缝）。
// =====================================================================

/**
 * @brief 碰撞后端替身（CollisionStrategy 测试实现——碰撞应答脚本化）。
 *
 * 脚本面：
 *   - collisionScript：FIFO 应答队列（按会话评估的逐对查询序消费；耗尽后
 *     回退 defaultCollision）——"预设 findings"的来源；
 *   - throwInCollision：置位时 doInCollision 抛真实 rw::common::Exception
 *     （POL-EVAL-5"替身后端抛 rw 异常→Failed"的故障注入点——"异常"轨）。
 * 模型管理沿用基类 ProximityStrategy 的登记面（会话构建期注册行为的
 * 核对面）。★ 本替身应答为脚本值，不冒充碰撞算法（POL-TD-1）。
 */
class ScriptedCollisionBackend : public rw::proximity::CollisionStrategy {
public:
    /// FIFO 碰撞应答脚本（true＝相交；耗尽→defaultCollision）。
    std::deque<bool> collisionScript;
    /// 缺省碰撞应答（脚本耗尽后的恒定应答——false＝"无碰撞"应答）。
    bool defaultCollision = false;
    /// 故障注入：doInCollision 抛 rw::common::Exception（真实基线异常类型）。
    bool throwInCollision = false;
    /// 已注册几何 id（会话构建期注册行为的核对面）。
    std::vector<std::string> registeredGeometryIds;

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
            // 真实基线异常类型（§6.3"RobWork 异常必须捕获转 Failed"的注入面
            // ——替身只负责把异常送进真实捕获路径，捕获逻辑是产品代码）。
            RW_THROW("ScriptedCollisionBackend: scripted detector fault (POL-TD-1/EVAL-5)");
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
        // 评估实现不消费接触点（penetrationDepth 显式空——不伪造）；越界
        // 调用＝替身边界违约，显性失败（POL-TD-1 机检语义的运行期面）。
        throw std::logic_error("ScriptedCollisionBackend 替身边界：getCollisionContacts 不被评估消费");
    }
};

/**
 * @brief 距离能力后端替身（ScriptedCollisionBackend＋DistanceStrategy——
 *        RobWork 多接口策略惯用法；距离能力探测 dynamic_cast 的命中面）。
 *
 * distanceScript 为 FIFO 实测值队列（单位 m；耗尽→defaultDistance），可
 * 注入 NaN/±Inf 复现 §7.4 行 5（非有限实测值→评估失败）。★ 脚本实测值
 * 只承载决策表的输入，不是几何真值——边界值决策与间距判定验证的是评估
 * 器的判定逻辑（POL-TD-1）。
 */
class ScriptedDistanceBackend final : public ScriptedCollisionBackend,
                                      public rw::proximity::DistanceStrategy {
public:
    /// FIFO 实测距离脚本（SI m；耗尽→defaultDistance）。
    std::deque<double> distanceScript;
    /// 缺省实测距离（SI m——默认取远大于任何工程阈值的清晰值）。
    double defaultDistance = 1.0;

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
    /// 结果载体（doDistance 契约要求返回引用——每次调用整体重置）。
    rw::proximity::DistanceStrategy::Result m_lastResult;
};

// =====================================================================
// ScriptedCollisionEvaluator——§11 命名替身（ICollisionEvaluator 实现）。
// =====================================================================

/**
 * @brief 脚本化碰撞评估器（§11 测试设施行——ICollisionEvaluator 的测试
 *        实现：按脚本产生预设 findings/Failed/Canceled/异常）。
 *
 * 结构：
 *   - backend() 返回脚本描述符（backendId 带 "test." 前缀＋toleranceModel
 *     显式标注 not-a-real-backend——替身数据不冒充真实后端复现要素，
 *     POL-TD-1；该描述符进 sessionIdentity 仅用于测试内等价比较）；
 *   - createSession(policy, scene, names) 以**真实** CollisionEvaluationSession
 *     三步构建（作用域展开/冲突复核/检测器注册＋sessionIdentity 全部是
 *     产品代码——见文件头"设计取舍"），几何应答由内置脚本后端供给：
 *     逐对查询按脚本回放（findings 轨）、throwInCollision/非有限距离注入
 *     （Failed 轨）、消费方 ScriptedCallContext 取消脚本（Canceled 轨）。
 *
 * 生命周期与所有权：脚本后端由本实例以 shared_ptr 持有（会话经
 * CollisionEvaluationSession 构造的共享形态保活——POL-T07 实测的所有权
 * 口径）；消费方持 createSession 返回的会话 shared_ptr。同一实例反复
 * createSession 各自独立构建会话（脚本队列按会话消费——多会话串行
 * 使用时由测试负责重填脚本）。
 * 线程约束：单线程（文件头）；createSession 本身无共享可变状态（脚本
 * 队列在被构建的会话消费期间由该会话独占）。
 */
class ScriptedCollisionEvaluator final : public ICollisionEvaluator {
public:
    /// 碰撞应答脚本（FIFO——进入会话的逐对查询序）。
    std::deque<bool> collisionScript;
    /// 脚本耗尽后的缺省碰撞应答。
    bool defaultCollision = false;
    /// 实测距离脚本（SI m；FIFO——requestMinDistance/间距检查的查询序）。
    std::deque<double> distanceScript;
    /// 脚本耗尽后的缺省实测距离（SI m）。
    double defaultDistance = 1.0;
    /// 故障注入：碰撞查询抛 rw 异常（Failed 轨）。
    bool throwInCollision = false;
    /// 故障注入：距离查询抛 rw 异常（Failed 轨——距离侧异常面）。
    bool throwInDistance = false;

    /**
     * @brief 构造脚本化评估器（脚本面经公有成员直接编程——构造后可继续
     *        修改脚本，下次 createSession 生效）。
     *
     * @param descriptor [in] 脚本描述符（缺省＝"test.scripted-evaluator"
     *                   系——POL-TD-1 显式标注面；同值进 sessionIdentity）
     */
    explicit ScriptedCollisionEvaluator(
        CollisionBackendDescriptor descriptor = defaultDescriptor())
        : m_descriptor(std::move(descriptor))
    {
    }

    /// @copydoc ICollisionEvaluator::backend（脚本描述符——非复现要素主张）。
    CollisionBackendDescriptor backend() const override { return m_descriptor; }

    /**
     * @brief 构建评估会话（真实三步构建＋脚本后端注入——§9.3 契约路径）。
     *
     * @param policy [in] 已发布策略对象（Valid——会话构造复检，违约即
     *               PolicyError fail-fast：真实契约路径的演练）
     * @param scene  [in] 碰撞场景（§6.1 校验在真实会话构造内执行——场景
     *               违约同样走真实 PolicyError 码面，不替身化错误语义）
     * @param names  [in] 名称上下文（借用——构建期存活即可）
     * @return 不可变会话（真实 CollisionEvaluationSession——评估走产品
     *         状态机/稳定排序，几何应答按脚本）
     *
     * @throws PolicyError 同真实会话构造（三步任一失败——替身不拦截、
     *         不翻译错误：契约错误语义本身就是被验证对象）
     */
    std::shared_ptr<const CollisionEvaluationSession>
    createSession(const EngineeringPolicySet& policy, const CollisionScene& scene,
                  const IPolicyNameContext& names) const override
    {
        // 脚本后端按当前脚本面组装（每次 createSession 一个新实例——脚本
        // 队列的消费起点一致，避免跨会话串扰）。
        auto backend = std::make_shared<ScriptedDistanceBackend>();
        backend->collisionScript = collisionScript;
        backend->defaultCollision = defaultCollision;
        backend->distanceScript = distanceScript;
        backend->defaultDistance = defaultDistance;
        backend->throwInCollision = throwInCollision;
        // 距离侧故障注入共享同一开关槽（throwInDistance 置位时碰撞应答
        // 不注入——两轨互斥由测试保证，替身不做隐式组合）。
        backend->throwInCollision = throwInCollision || throwInDistance;
        // 真实会话构建（缺省参数＝会话自建查询互斥——单会话形态）。
        return std::make_shared<const CollisionEvaluationSession>(
            policy, scene, names, m_descriptor, backend);
    }

    /// 缺省脚本描述符（backendId/toleranceModel 显式标注替身——POL-TD-1）。
    static CollisionBackendDescriptor defaultDescriptor()
    {
        CollisionBackendDescriptor d;
        d.backendId = "test.scripted-evaluator";
        d.backendVersion = "0.0-testdouble";
        d.toleranceModel = "scripted/test-double(not-a-real-backend)";
        return d;
    }

private:
    /// 脚本描述符（构造期固化——backend() 与 sessionIdentity 组成同源）。
    CollisionBackendDescriptor m_descriptor;
};

// =====================================================================
// StubPolicyProvider——§11 命名替身（IPolicyProvider 实现）。
// =====================================================================

/**
 * @brief ④策略端口替身（§11 测试设施行——IPolicyProvider 的测试实现）。
 *
 * 脚本面（三方法逐一）：
 *   - resolvePolicy：返回预设 PolicyResolution（成功＝presetResolution 内
 *     的策略＋Info 诊断；失败＝空 policy＋错误诊断——由测试构造），或
 *     throwPolicyError 置位时抛预设码 PolicyError（调用方契约违约轨的
 *     演练——不重复真实实现的校验逻辑，只提供"端口抛错"的可注入面）；
 *   - collisionEvaluator()：返回注入的评估器实例（**每次同一引用**——
 *     POL-SHARE-1"三入口同一 evaluator 实例（指针相等）"的断言承载）；
 *     未注入即调用＝抛 PolicyError(PortAssemblyIncomplete)（与真实端口
 *     同码面——装配分步契约的替身侧镜像）；
 *   - collisionBackend()：返回预设描述符（进 sessionIdentity/复现块的
 *     测试值——backendId 带 "test." 前缀标注替身）。
 *
 * ★ 替身不做记忆化（§9.1 记忆化语义的验证归 PolicyPortTest 的真实
 *   PolicyProvider——替身只验证消费方对端口契约的依赖形态，POL-TD-1）。
 * 线程约束：单线程（文件头）。
 */
class StubPolicyProvider final : public IPolicyProvider {
public:
    /// 预设解析结果（resolvePolicy 的应答值——成功/失败形态由测试构造）。
    PolicyResolution presetResolution;
    /// 故障注入：resolvePolicy 改为抛 PolicyError（throwCode/throwDetail）。
    bool throwPolicyError = false;
    /// 抛出码面（调用方契约违约轨的演练值——默认对象身份无效）。
    PolicyErrorCode throwCode = PolicyErrorCode::PolicyObjectInvalid;
    /// 抛出细节（人读定位——测试断言消息面）。
    std::string throwDetail = "StubPolicyProvider: scripted port failure";
    /// 后端复现要素预设（collisionBackend 应答值——测试值，非复现主张）。
    CollisionBackendDescriptor presetBackend = defaultDescriptor();

    /**
     * @brief 构造端口替身（评估器半区可后注入——装配分步契约的演练面）。
     *
     * @param evaluator [in] 碰撞评估器实例（共享持有；nullopt＝待注入——
     *                  注入前 collisionEvaluator() 抛 PortAssemblyIncomplete，
     *                  与真实 PolicyProvider 同码面）
     */
    explicit StubPolicyProvider(std::shared_ptr<ICollisionEvaluator> evaluator = nullptr)
        : m_evaluator(std::move(evaluator))
    {
    }

    /// 评估器半区注入（装配分步——注入后 collisionEvaluator() 可服务）。
    void setEvaluator(std::shared_ptr<ICollisionEvaluator> evaluator)
    {
        m_evaluator = std::move(evaluator);
    }

    /// @copydoc IPolicyProvider::resolvePolicy（预设应答或脚本抛错）。
    PolicyResolution resolvePolicy(const PolicyResolutionRequest&) const override
    {
        if (throwPolicyError) {
            // 脚本抛出轨（调用方契约违约的演练——真实校验逻辑不在替身内）。
            throw PolicyError(throwCode, throwDetail);
        }
        return presetResolution;
    }

    /// @copydoc IPolicyProvider::collisionEvaluator（同一实例引用——
    /// POL-SHARE-1 实例同一性断言的承载；未装配即 PortAssemblyIncomplete）。
    ICollisionEvaluator& collisionEvaluator() const override
    {
        if (!m_evaluator) {
            throw PolicyError(PolicyErrorCode::PortAssemblyIncomplete,
                              "StubPolicyProvider: 评估器半区未注入（装配分步契约镜像）");
        }
        return *m_evaluator;
    }

    /// @copydoc IPolicyProvider::collisionBackend（预设描述符）。
    CollisionBackendDescriptor collisionBackend() const override { return presetBackend; }

    /// 缺省脚本描述符（backendId 显式标注替身——POL-TD-1）。
    static CollisionBackendDescriptor defaultDescriptor()
    {
        CollisionBackendDescriptor d;
        d.backendId = "test.stub-policy-provider";
        d.backendVersion = "0.0-testdouble";
        d.toleranceModel = "scripted/test-double(not-a-real-backend)";
        return d;
    }

private:
    /// 注入的评估器（共享持有；空＝评估器半区待注入——装配分步）。
    std::shared_ptr<ICollisionEvaluator> m_evaluator;
};

}  // namespace sdurws::ird::policy::testdoubles

#endif  // SDURWS_IRD_POLICY_TESTDOUBLE_POLICYSERVICETESTDOUBLES_HPP
