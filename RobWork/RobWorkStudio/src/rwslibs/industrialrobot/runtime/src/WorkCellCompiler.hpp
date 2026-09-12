/**
 * @file   WorkCellCompiler.hpp
 * @brief  S6 WorkCell 编译器（单元内私有头）——CanonicalModel →
 *         rw::models::WorkCell 的确定性编译入口与产物结构。
 *
 * 设计依据：
 *   - units/runtime.md §5.2 S6 行（输入/输出/失败条件/临时对象——"WC＋
 *     Frame/Joint/Geometry 对象〔瞬态，失败即弃〕"、"同输入重试结果一致"）、
 *     §8.1（P-RT-3——rw kinematics/models 为已登记 L1 依赖）、§8.2（所有权
 *     与只读包装——可变句柄只在编译器 src/ 内部使用）、§6.3（BaseMount
 *     唯一写入点）、§7.2（编译器写入 WC 的名字必须与映射输出逐一相等）
 *   - 需求 ARC-03（唯一规范模型——WC 是派生产物）、MDL-06（编译原子性——
 *     任一失败不发布半成品，RobWork 对象全部析构）、MDL-14（名称生成）
 *   - 任务契约 tasks/foundation/RT-T07.json（deliverable：WC 编译实现——
 *     Frame 树/Device/Joint/BaseMount 唯一写入）
 *
 * 私有头说明（R-2）：本头只被 src/WorkCellCompiler.cpp 与单元内测试
 * （test/ 与 src/ 同权，CMake 已建 include 路径）include——十段编译链
 * （RT-T11）是 S6 的唯一产品调用方，跨单元不暴露（跨单元协作面是
 * RT-T11 的 Compiler.hpp CompileOutcome，非本结构）。
 *
 * 线程安全：compileWorkCell 为可重入纯函数（无共享可变状态；§5.5"编译器
 * 实例无共享可变状态"——每次调用独立构造全部 RobWork 对象）；非线程安全
 * 的只有基线对象自身的构造期（对象在调用线程内构造并返回，发布后只读）。
 */

#ifndef SDURWS_IRD_RUNTIME_WORKCELLCOMPILER_HPP
#define SDURWS_IRD_RUNTIME_WORKCELLCOMPILER_HPP

#include <string>
#include <vector>

#include <rw/core/Ptr.hpp>
#include <rw/models/WorkCell.hpp>

#include <sdurws/ird/runtime/CanonicalModel.hpp>  // 输入：CanonicalModel

namespace sdurws::ird::runtime {

/**
 * @brief S6 编译产物（§5.2 S6 行输出列的 C++ 承载）。
 *
 * 所有权与生命周期（§8.2/§5.2"临时对象"行）：workCell 由本结构以引用计数
 * 持有——值语义随结构走；编译失败（异常）时局部对象析构＝WC＋全部
 * Frame/Joint/Device 瞬态对象随之释放（基线 WorkCell 析构联动 StateStructure
 * 释放帧——MDL-06"失败时 S6/S7 的 RobWork 对象全部析构"的本任务落点；
 * CompileTransaction 的整体持有归 RT-T11 链）。
 *
 * 确定性：同 model 重复调用产出的 WC 结构相等（Frame 树同构同名、Device
 * 同名、bounds/限速逐关节相等——§5.5"同输入重复编译"行的 WC 面；RT-EQ
 * 契约测试钉住）。注意 WC 不含内容身份（身份在 CanonicalModel/NameMap——
 * WC 是编译产物标签层，§8.2"RobWork 对象名不作业务身份"）。
 *
 * 线程安全：产物 WC 发布后只读（编译器不再触碰——§8.7）；结构可跨线程
 * 以只读方式共享（经 WorkCellConstView 包装——公共面不暴露本结构的
 * 可变 Ptr，R-2 下仅单元内可见）。
 */
struct WorkCellCompileOutcome {
    /// 编译产物 WorkCell（引用计数持有；失败轨不产出——异常抛出）。
    rw::core::Ptr<rw::models::WorkCell> workCell;
    /// 设备名（映射 Device 条目消歧后全名——与 model.chain().deviceName 的
    /// 一致性归 S8 交叉校验，PA-1：名称唯一权威＝RuntimeNameMap）。
    std::string deviceName;
    /**
     * WC 实际对象名集合（编译器写入的帧名＋设备名；S8 交叉校验
     * crossCheckRuntimeNames 的 actualNames 输入——RT-T11 调用）。
     * 口径：只含编译器写入的帧（不含基线 WORLD 根帧——名称保留字 WORLD
     * 不入映射，§7.2；根帧是基线树锚点不是编译产物）＋设备名；顺序＝
     * 写入序（确定性——同输入同序，S8 断言信息稳定）。
     */
    std::vector<std::string> runtimeNames;
};

/**
 * @brief S6 WorkCell 编译（§5.2 S6 行——CanonicalModel → WorkCell 纯变换）。
 *
 * 编译步骤（实现体内逐步注释对应）：
 *   1. 名称源唯一化：buildRuntimeNameMap(model)（§7.2——写入 WC 的名字必须
 *      与映射逐一相等；映射是纯函数、与 S8 同源）；
 *   2. 入口防御复核：checkWorldBaseTransform（§5.2 S6 失败条件"T_world_base
 *      非法旋转→InputInvalid"——S5 已拦，此处对 S5 产物的防御性复核）；
 *   3. Frame 树构建（RAII 瞬态）：WORLD→BaseMount(T_world_base)→BaseFrame
 *      →[Joint_i→LinkFrame_i]→Flange＋Tcp（挂 Flange）＋SceneObject（挂
 *      WORLD——世界系固连，禁止项 3：不预乘安装旋转）；
 *   4. 逐关节显式设值（§8.2"显式设值全部被消费字段"——bounds 按零位偏置
 *      映射、maxVelocity/maxAcceleration 缺省显式无限，杜绝 RobWork 构造
 *      默认〔±DBL_MAX/1〕悄悄生效——RT-AD-2）；
 *   5. SerialDevice 构造（基座→法兰链；Device 名＝映射消歧名）并 addDevice；
 *   6. S9 防御性自检（§12 RT-T07 测试列"RT-BW-4〔S9 部分〕"——BaseMount
 *      读回值 vs T_world_base，checkBaseMountConsistency 不一致即
 *      BaseWorldInconsistent：实现缺陷类失败，不得放行）；
 *   7. 全程 try/catch：RobWork 异常/bad_alloc/其他 std 异常经
 *      translateRobWorkError（§8.4）转译后以 RuntimeError fail-fast——
 *      转译不吞异常、不发布半成品（MDL-06）。
 *
 * 任意轴关节的基线映射（实现层选型，§15.4 已登记）：基线 RevoluteJoint/
 * PrismaticJoint 固定绕局部 Z 轴旋转。编译器将权威（轴 axis、零位偏置
 * offset）编码为关节静态变换 T_static = T_parent_joint · R_axis(offset) ·
 * R_align（R_align·ez = axis，确定性构造），并在关节后的连杆 FixedFrame
 * 上放置补偿旋转 R_alignᵀ——由共轭恒等式 R_align·Rz(q_rw)·R_alignᵀ =
 * R_axis(q_rw)，链上任意连杆系满足 F(link_i) = F(link_{i-1}) ·
 * T_parent_joint_i · R_axis(offset_i + q_rw_i)（＝权威 FK，q_authoritative
 * ＝ zeroOffset + q_rw），法兰系无残差；关节 Frame 本身带固定旋转尾巴
 * R_align·Rz(q_rw)，但其轴线/原点与权威逐元素一致（绕轴旋转不动轴与原点
 * ——RT-EQ-1 的断言面）。
 *
 * @param model [in] 规范模型（应为 CanonicalModelBuilder 产物——S5 构造
 *              不变量成立；只读，本函数不修改）
 *
 * @return 编译产物（workCell 非空；值语义独立——每次调用全新对象树）
 *
 * @throws RuntimeError 码＝InputInvalid（T_world_base 非法旋转——防御面）、
 *         WorkCellCompileFailed（RobWork 异常/空句柄/层级无效——§8.4 转译，
 *         detail 含 stage 与消息摘要）、ResourceBudget（std::bad_alloc——
 *         §5.5）、BaseWorldInconsistent（S9 自检失败——实现缺陷类，§6.6）
 *
 * 复杂度：O(链长＋场景＋工具)（RobWork 对象构造线性；无迭代求解）。
 */
WorkCellCompileOutcome compileWorkCell(const CanonicalModel& model);

}  // namespace sdurws::ird::runtime

#endif  // SDURWS_IRD_RUNTIME_WORKCELLCOMPILER_HPP
