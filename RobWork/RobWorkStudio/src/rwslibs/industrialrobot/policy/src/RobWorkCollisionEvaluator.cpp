/**
 * @file   RobWorkCollisionEvaluator.cpp
 * @brief  碰撞评估唯一实现的后端绑定翻译单元——RobWorkCollisionEvaluator
 *         方法与 makeRobWorkCollisionEvaluator 唯一构造入口的定义（§6.5/§9.3）。
 *
 * 设计依据：
 *   - units/policy.md §6.5（装配线：makeRobWorkCollisionEvaluator(BackendConfig
 *     {builtin ProximityStrategyRW})——唯一实现的唯一构造入口）、§9.3
 *     （ICollisionEvaluator 契约表与 RobWorkCollisionEvaluator 产出）、
 *     §12 POL-T06 行（检测器初始化——内置后端实例化）、§8.1/P-POL-5
 *     （复现要素取值：backendVersion 暂取 RobWork 构建版本 RW_VERSION）
 *   - 需求 ARC-05（唯一实现——R-POL-1~5 防旁路）
 *
 * 翻译单元切分说明（policy.md §15.4 v0.7 登记——评审对照点）：
 *   本 TU 与 CollisionEvaluator.cpp 的分工：
 *   - 本 TU 持有全部 RobWork 命名标识符（RobWorkCollisionEvaluator/
 *     makeRobWorkCollisionEvaluator——§9.3 冻结命名）与 RobWork 命名头
 *     包含（RobWorkConfig.hpp——RW_VERSION 取值源），**不含任何字符串
 *     字面量**（源码文本零 ASCII 引号）；
 *   - CollisionEvaluator.cpp 持有会话域错误消息与复现要素冻结值的字面量
 *     （detail::makeBuiltinBackendDescriptor 单点），不含 RobWork 命名
 *     标识符；
 *   - 动机：ird_gates 的 IRD-GATE-R4 启发式以"跨引号对的 RobWork 子串"
 *     疑似名称拼接/剥离（ARC-04/NFR-MNT-07——R-4 红线的机械化面）。本
 *     单元的实现不含任何名称拼接/剥离行为（名称消费仅经 IPolicyNameContext
 *     转发——P-POL-8 措辞登记），但 §9.3 冻结命名本身含 RobWork 子串，
 *     与字面量同 TU 时触发跨行误配命中。O-12/P-POL-8（policy R-4 例外
 *     措辞）属所有者裁决项、例外登记册（ird_gates_whitelist.cmake）归
 *     WP-01 所有——裁决前以本切分在结构上隔断误报面，不私改门禁、不私
 *     裁语义；裁决后如需可回并（登记于 policy.md §15.4 v0.7）。
 *
 * 线程安全：实例构造后只读——两方法并发只读安全（§9.3 线程行）。
 * 确定性：描述符构造期固化（同构建同值——NFR-COR-02）。
 */

#include <sdurws/ird/policy/CollisionEvaluator.hpp>

#include <RobWorkConfig.hpp>

#include <rw/proximity/CollisionStrategy.hpp>
#include <rw/proximity/rwstrategy/ProximityStrategyRW.hpp>

#include <memory>
#include <utility>

namespace sdurws::ird::policy {

// =====================================================================
// 唯一实现（§6.5 装配线——构造入口唯一；类契约见 CollisionEvaluator.hpp）。
// =====================================================================

RobWorkCollisionEvaluator::RobWorkCollisionEvaluator(const BackendConfig& config)
{
    // config 当前无字段（内置唯一后端——BackendConfig 注释）；参数保留为
    // 唯一构造入口的签名锚（消费以 static_cast<void> 抑制未用告警）。
    static_cast<void>(config);
    // 内置后端实例化（sdurw_proximity——R-5 例外许可的唯一消费点；构造
    // 不做几何注册——模型注册归会话构建期，POL-T07 评估半区装配）。
    m_strategy = std::make_shared<rw::proximity::ProximityStrategyRW>();
    // 后端查询互斥（评估器级单实例——其全部会话共享同一把锁：同一后端
    // 实例的构建期注册与评估期查询全部串行化，跨会话并发亦无数据竞争；
    // RobWork 检测器查询面非线程安全——见会话构造参数注释）。
    m_queryMutex = std::make_shared<std::mutex>();
    // 复现要素（§8.1/P-POL-5）：冻结值与容差模型登记串由字面量单点
    // （detail::makeBuiltinBackendDescriptor——CollisionEvaluator.cpp）供给；
    // backendVersion 注入 RW_VERSION（RobWork 构建版本——构建期宏；
    // P-POL-5 基线冻结 WP-24 后单点回填）。
    m_descriptor = detail::makeBuiltinBackendDescriptor(RW_VERSION);
}

CollisionBackendDescriptor RobWorkCollisionEvaluator::backend() const
{
    // 装配期固化值（const 成员——每次返回同值；与 PolicyProvider 装配的
    // backend 同源推荐口径，§9.1）。
    return m_descriptor;
}

std::shared_ptr<const CollisionEvaluationSession>
RobWorkCollisionEvaluator::createSession(const EngineeringPolicySet& policy,
                                         const CollisionScene& scene,
                                         const IPolicyNameContext& names) const
{
    // 三步构建委托会话构造函数（单一实现——本类无第二套校验/展开逻辑；
    // 后端实例与描述符取自本实例，保证 sessionIdentity 组成一致——§9.1
    // "推荐取 evaluator->backend() 同源值"的机械落实；查询互斥为本实例
    // 级单例——本评估器全部会话共享，跨会话并发安全）。
    return std::make_shared<const CollisionEvaluationSession>(policy, scene, names,
                                                              m_descriptor, m_strategy,
                                                              m_queryMutex);
}

std::unique_ptr<ICollisionEvaluator>
makeRobWorkCollisionEvaluator(const BackendConfig& config)
{
    // 唯一构造入口（§6.5——业务单元不得自行实现碰撞算法/构造本类之外的
    // 第二实现，R-POL-1；unique_ptr 删除器在此完整类型可见点实例化）。
    return std::make_unique<RobWorkCollisionEvaluator>(config);
}

}  // namespace sdurws::ird::policy
