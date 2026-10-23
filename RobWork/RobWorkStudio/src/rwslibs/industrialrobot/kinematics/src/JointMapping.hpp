/**
 * @file   JointMapping.hpp
 * @brief  权威关节角 ↔ 运行时关节坐标映射（REUSE-T18）——单元私有纯函数。
 *
 * 设计依据：
 *   - units/kinematics.md §5.1：q_authoritative＝q_zeroOffset＋q_rw——本单元
 *     对外契约一律消费权威 q（调用方直入，无单位换算面）；
 *   - runtime 编译链（WorkCellCompiler.cpp 第 5 步，REUSE-T13）：上游关节
 *     承载零位偏置——WC 关节坐标＝q_rw，State 内关节变量从 q_rw=0 起；
 *   - CanonicalModel.hpp CanonicalJoint.zeroOffset 字段注释（"权威角换算
 *     分量"，rad／移动关节 m）。
 *
 * 背景（为什么是本单元私有头）：全产品在 REUSE-T18 之前不存在 q_auth→q_rw
 * 的公共转换入口（dynamics 侧 zeroOffset 为快照透传事实、不参与本域计算；
 * runtime 侧映射归编译器 owner）——T18 让 FK 消费已编译 WC 后，运动学侧
 * 第一次需要在调用 setQ 前做该换算。三行纯算术按 H1"薄适配"落位为本单元
 * 私有头（R-2：实现头不入 include/），不建跨单元共享面、不建缓存。
 *
 * 确定性：纯函数、单精度序固定——同输入同输出（NFR-COR-01）。
 */

#ifndef SDURWS_IRD_KINEMATICS_SRC_JOINTMAPPING_HPP
#define SDURWS_IRD_KINEMATICS_SRC_JOINTMAPPING_HPP

#include <sdurws/ird/runtime/CanonicalModel.hpp>

namespace sdurws::ird::kinematics {
namespace jointmapping {

/**
 * @brief 权威关节角 → 运行时（WC）关节坐标：q_rw＝q_auth−zeroOffset。
 *
 * @param joint [in] 规范关节（只读借用；仅 movable 关节调用——Fixed 无
 *                  q 分量，其 zeroOffset 不消费——编译器同口径）
 * @param qAuth [in] 权威关节角（SI：转动 rad／移动 m——§5.1）
 * @return 该关节在上游 WC 内的关节坐标 q_rw（同单位——rad／m）
 *
 * 纯函数；不抛；有限性由调用方（FkEvaluator 非法输入守卫）先行保证。
 */
inline double qRuntimeFromAuthoritative(const runtime::CanonicalJoint& joint,
                                        double qAuth)
{
    return qAuth - joint.zeroOffset;
}

}  // namespace jointmapping
}  // namespace sdurws::ird::kinematics

#endif  // SDURWS_IRD_KINEMATICS_SRC_JOINTMAPPING_HPP
