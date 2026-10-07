/**
 * @file   SystemDefaultPolicy.hpp
 * @brief  系统缺省策略集与其 ④ 端口供给器（宿主装配的缺省策略源）。
 *
 * 设计依据：
 *   - ARC-05（策略单一权威、唯一默认＝附录 D 第 11 项——4π 行程上限，
 *     origin=DefaultAppendixD；PolicySet.hpp §4.4 冻结原文）
 *   - JointThresholds 类型默认成员初始化（finiteRotationTravelLimit 为带
 *     4π 默认的值字段；nearLimitRatio/conditionNumberWarning nullopt＝
 *     「显式不适用」——P-POL-2 未裁决阈值不发明数值的保守口径）
 *   - PolicyOriginKind::SystemDefault（§4.2 origin 词表——设计预埋的
 *     系统缺省来源类别）
 *   - findings F-536／DTB §4.2 O-46 裁决（2026-10-07，出路②-scope）：
 *     ④行程校验唯一消费面＝行程上限阈值——宿主在工程内无策略对象生命
 *     周期的现阶段，以本缺省源装配 ④ 端口（真实对象生命周期落地后可
 *     换装 PolicyProvider 存储背书形态，本供给器退役或保留为兜底）
 *
 * 背景说明：建模 apply-robot-design 的行程校验（MDL-06④）经
 * HandlerServices.policyProvider 解析策略；宿主装配此前恒为 nullptr
 * （C-10 诚实基线），行程相关 apply 在真实宿主恒被拒（装配缺陷形态）。
 * 本文件给出不动 P-POL-2、不预造对象生命周期、不降级 ④ 语义的缺省源：
 * 行程上限取唯一冻结默认 4π rad，其余阈值保持「显式不适用」。
 *
 * 线程约束：全部接口并发只读安全（不可变值／函数局部静态常量）。
 */

#ifndef RWS_IRD_POLICY_SYSTEMDEFAULTPOLICY_HPP
#define RWS_IRD_POLICY_SYSTEMDEFAULTPOLICY_HPP

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/policy/PolicyPort.hpp>
#include <sdurws/ird/policy/PolicySet.hpp>

namespace sdurws::ird::policy {

/**
 * @brief 系统缺省策略的保留对象身份（well-known 常量——非项目对象）。
 *
 * 语义：本身份**不指向** project 存储中的任何对象（系统缺省集为编译期
 * 常量、无存储背书），仅作为 ④ 端口解析请求的锚定键与 HandlerServices
 * policyObject 槽位的非零填充（全零保留值在请求契约与发布门均为违约）。
 * 固定字面量「obj-00000000000000000000000000000001」＝全 1 最低位——保留
 * 值纪律只排除全零，本值与 ObjectId::generate()（随机 2^128 空间）的撞
 * 概率可忽略，且该身份永不入 project 存储。
 *
 * @return 常量引用（函数局部静态——线程安全初始化；进程期不变）。
 */
const core::ObjectId& systemDefaultPolicyObjectId();

/**
 * @brief 构造系统缺省策略集（附录 D 冻结默认的唯一消费面）。
 *
 * 构成（全冻结常量，零发明数值）：
 *   - jointThresholds＝JointThresholds 默认成员初始化：行程上限 4π rad
 *     （DefaultAppendixD）；nearLimitRatio/conditionNumberWarning 保持
 *     nullopt＝显式不适用（P-POL-2 未裁决——O-10 口径不变，本工厂不预
 *     填任何未冻结阈值）；travelLimitCheckEnabled＝true（④校验执行）。
 *   - collision＝disabled（碰撞策略无冻结默认——enabled=true 需
 *     safetyClearance（无默认即抛），false＝碰撞策略显式不适用；④行程
 *     校验不消费碰撞面）。
 *   - applicability＝空集（全部模式/对象适用——§4.2.1 空集语义）。
 *   - origin＝SystemDefault＋审计备注；validationState＝Valid（发布门）。
 *   - contentIdentity＝PolicyCodec::contentIdentity 对语义闭包现算（§5.3
 *     ——发布门「调用方不可申报假身份」的合规满足）。
 *
 * 确定性：同 schema 常量下逐字段确定（4π 以 double 字面量×4 计算——
 * IEEE754 逐位一致），contentIdentity 对同闭包恒同（CON-05/06）。
 *
 * @return 已发布策略集（make 发布门产出——Valid 态）。
 *
 * @throws PolicyError 发布门任一校验失败（构造常量违约＝代码缺陷，fail-fast）
 */
EngineeringPolicySet makeSystemDefaultPolicySet();

/**
 * @brief 系统缺省策略供给器（IPolicyProvider 的宿主装配形态——解析半区
 *        专用）。
 *
 * 与存储背书的 PolicyProvider（PolicyPort.hpp）的差异（本类契约，非
 * 错误矩阵偏离的隐瞒——逐条登记）：
 *   - resolvePolicy：仅对 systemDefaultPolicyObjectId() 应答，返回
 *     makeSystemDefaultPolicySet() 结果；expectedVersion **不消费**（系
 *     统缺省集为编译期常量、无存储版本演进——「期望版本」语义不适用）。
 *     其余对象身份→空 policy＋POLICY-OBJECT-MISSING 诊断（错误矩阵第 3
 *     格同码面）；全零身份→按请求契约 fail-fast 抛 PolicyError。
 *   - collisionEvaluator()/collisionBackend()：抛
 *     PolicyError(PortAssemblyIncomplete)——本供给器为解析半区专用（④
 *     行程校验只走 resolvePolicy），碰撞评估半区未装配（装配分步语义的
 *     fail-fast 面，PolicyPort.hpp 装配契约原文）；调用的宿主形态即装
 *     配缺陷，fail-fast 不静默。
 *
 * 线程安全：并发只读安全（应答为不可变常量值拷贝；无共享可变态）。
 * 生命周期：宿主装配期构造、成员锚定存活期（HandlerServices 持裸指针
 * ——借用契约，存活期覆盖处理器使用期）。
 */
class SystemDefaultPolicyProvider final : public IPolicyProvider {
public:
    SystemDefaultPolicyProvider() = default;
    ~SystemDefaultPolicyProvider() override = default;

    /// 端口单例纪律（与 PolicyProvider 同款——复制无意义且破坏运行期不变）。
    SystemDefaultPolicyProvider(const SystemDefaultPolicyProvider&) = delete;
    SystemDefaultPolicyProvider& operator=(const SystemDefaultPolicyProvider&) = delete;

    /// @copydoc IPolicyProvider::resolvePolicy
    PolicyResolution resolvePolicy(const PolicyResolutionRequest& request) const override;

    /// @copydoc IPolicyProvider::collisionEvaluator（解析半区专用——fail-fast）
    ICollisionEvaluator& collisionEvaluator() const override;

    /// @copydoc IPolicyProvider::collisionBackend（解析半区专用——fail-fast）
    CollisionBackendDescriptor collisionBackend() const override;
};

}  // namespace sdurws::ird::policy

#endif  // RWS_IRD_POLICY_SYSTEMDEFAULTPOLICY_HPP
