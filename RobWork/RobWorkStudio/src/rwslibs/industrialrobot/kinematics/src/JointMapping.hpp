/**
 * @file   JointMapping.hpp
 * @brief  权威关节角 ↔ 运行时关节坐标映射（REUSE-T18/T51）——单元私有纯函数。
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
 * REUSE-T51（WP-04）：IK 完整位姿模式迁移上游 JacobianIKSolver（迭代核经
 * T17 锚的 Device/任务私有 State 消费上游 WC 坐标），收敛解 q_rw 须回到
 * 权威角才能进产品解集/指标链——同头补互逆方向 qAuth＝q_rw＋zeroOffset，
 * 两方向共用本头作唯一映射点（单点 q 映射，禁第二套换算）。
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

/**
 * @brief 运行时（WC）关节坐标 → 权威关节角：q_auth＝q_rw＋zeroOffset
 *        （REUSE-T51 表尾追加——上行映射的唯一互逆方向）。
 *
 * @param joint [in] 规范关节（只读借用；仅 movable 关节调用——与
 *                  qRuntimeFromAuthoritative 同口径，Fixed 不入 q）
 * @param qRw [in] 上游 WC 内关节坐标（SI：转动 rad／移动 m——Device::getQ
 *                 返回值逐轴；来源如 IK 收敛解的上游 State）
 * @return 权威关节角（同单位——rad／m；产品解集/指标链的对外契约面）
 *
 * 纯函数；不抛；有限性由调用方先行保证（上游 State 的 q_rw 合法性归
 * 设施面）。与 qRuntimeFromAuthoritative 互为逆运算（同一 zeroOffset
 * 先减后加——浮点序固定，往返非位精确但误差 ≤1ulp 且不进入本单元的
 * 确定性承诺面：同输入同输出仍逐位成立）。
 */
inline double qAuthoritativeFromRuntime(const runtime::CanonicalJoint& joint,
                                        double qRw)
{
    return qRw + joint.zeroOffset;
}

}  // namespace jointmapping
}  // namespace sdurws::ird::kinematics

#endif  // SDURWS_IRD_KINEMATICS_SRC_JOINTMAPPING_HPP

