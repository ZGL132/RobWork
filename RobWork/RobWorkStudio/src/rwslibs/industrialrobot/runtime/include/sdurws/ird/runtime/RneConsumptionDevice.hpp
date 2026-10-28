/**
 * @file   RneConsumptionDevice.hpp
 * @brief  RNE 可消费一次性设备工厂（runtime 唯一模型 owner 的构造面——
 *         WP-07 逆动力学上游 RecursiveNewtonEuler 消费载体；WP-09 正动力学
 *         引擎会话预留同一设施）。
 *
 * 设计依据与裁决脉络：
 *   - migration §13.3 WP-07（tasks/REUSE-T52.json）：dynamics 逆动力学换核
 *     上游 rwsim::util::RecursiveNewtonEuler（H1 直接 API）；
 *   - GAP-RNE-FRAME（REUSE-T30 登记、functions/runtime.json 同口径）：上游
 *     RNE 把 BodyInfo.masscenter/getInertia 按**关节帧**解释，而 canonical
 *     值为权威连杆系表达——权威轴≠z 时质心世界位置错位（探针 τ_x=±5.886）；
 *   - A2 裁决（2026-10-26）：消费缺口采薄适配方案（B1 不批）——§8.2
 *     canonical 逐字断言、modeling DWC 导出、S7 产品映射三处零触碰；本工厂
 *     只在构造 RNE 载体时做换算（消费边界落点经 2026-10-26 所有者裁决上收
 *     runtime 出口——模型构造权归 runtime 的机器门禁与架构红线的更干净
 *     表达，dynamics 经已登记 dynamics->runtime 边消费，请求 DTO 零变化）。
 *
 * 所有权与生命周期（ARCH §3.6 任务私有会话）：返回值为调用方私有的一次性
 * 计算载体——stateStructure 拥有全部帧，device/rigidDevice 经引用计数持有
 * 同一帧树，state 为默认 State 值；载体随调用方（单次 evaluate）销毁整链
 * 消亡，不进快照、不共享、无产品面名称出口（帧名 ird-rne/* 私有名空间）。
 * 线程约束：每次调用独立构造，跨线程不得共享同一载体。
 *
 * 纯 L2 零 Qt；仅集成与冒烟的框架链接模式可用（rw/rwsim 真实符号）。
 */

#ifndef IRD_RUNTIME_RNECONSUMPTIONDEVICE_HPP
#define IRD_RUNTIME_RNECONSUMPTIONDEVICE_HPP

#include <sdurws/ird/runtime/CanonicalModel.hpp> // runtime::CanonicalModel（唯一输入权威）

#include <rw/kinematics/State.hpp>
#include <rw/math/InertiaMatrix.hpp>
#include <rw/math/Rotation3D.hpp>
#include <rw/math/Vector3D.hpp>

#include <vector>

namespace rw { namespace kinematics {
    class StateStructure;
}}    // namespace rw::kinematics
namespace rw { namespace models {
    class Joint;
    class SerialDevice;
}}    // namespace rw::models
namespace rwsim { namespace dynamics {
    class RigidDevice;
}}    // namespace rwsim::dynamics

namespace sdurws { namespace ird { namespace runtime {

/**
 * @brief RNE 可消费一次性设备载体（值语义聚合；所有权见文件头）。
 *
 * 消费方（dynamics 评估器）按需取用：state/device 供上游 solve（q_rw 换算
 * 由消费方执行——q_auth＝zeroOffset+q_rw 的 T13 语义，设备关节变换已烘焙
 * offset）；rigidDevice 供末端变体合成回写（末连杆 BodyInfo 经非 const
 * getInfo 访问器）；joints/prismatic 供按关节类型投影；flange* 为 §5.4
 * 工具/负载平行轴合成的基线与换算基（法兰系表达，换算单点在消费方写回处）。
 */
struct RneConsumptionDevice {
    rw::core::Ptr<rw::kinematics::StateStructure> stateStructure; ///< 帧所有权根（最先销毁序末位）
    rw::core::Ptr<rw::models::SerialDevice> device;               ///< 关节设备（SerialDevice——上游 validate 前置）
    rw::core::Ptr<rwsim::dynamics::RigidDevice> rigidDevice;      ///< 刚体设备（连杆体序列宿主）
    rw::kinematics::State state;                                  ///< 默认 State（q_rw=0；消费方逐样本 setQ）

    std::vector<rw::models::Joint*> joints;    ///< 设备关节序（链序；投影轴取 frameTframe(base,joint) z 列）
    std::vector<bool> prismatic;               ///< 逐关节移动标记（true＝Prismatic——广义力取沿轴力分量）
    std::vector<double> zeroOffsets;           ///< 逐关节零位偏置（rad/m；q_rw＝q_auth−offset 换算分量）

    rw::math::Rotation3D<double> flangeAlignInverse; ///< 末关节 R_alignᵀ（权威法兰系→关节帧坐标变换基）
    /// F01：末固定段在末关节帧中的平移（m），仅用于消费载体的质心坐标映射。
    rw::math::Vector3D<double> flangeTranslation{};
    double flangeBaseMass = 0.0;                     ///< 法兰连杆基线质量，单位 kg（§5.4 合成基项）
    rw::math::Vector3D<double> flangeBaseCom{};      ///< 基线质心（法兰系，m）
    rw::math::InertiaMatrix<double> flangeBaseInertia{}; ///< 基线惯量（质心系、法兰系姿态，kg·m²）
};

/**
 * @brief 从规范模型构造 RNE 可消费一次性设备（文件头裁决脉络；模型只读）。
 *
 * 帧树与 S6 编译器同源语义（每关节两帧）：关节帧 tAligned＝origin·T(R_align)
 * 且 zeroOffset 烘焙进 fixed transform（q_rw=0 静态结果——q_auth＝offset+
 * q_rw 在 State 域保持）；连杆帧尾巴 R_alignᵀ（共轭恒等式使权威 FK 无残差）。
 * GAP-RNE-FRAME 薄适配：连杆 BodyInfo 按**关节帧**承载——masscenter＝
 * R_alignᵀ·com、inertia＝R_alignᵀ·I·R_align（权威轴=z 面退化为恒等/翻转）。
 * 物性缺失（NotProvided）按 0/保守值填充（§5.5 降级语义由消费方承载证据面，
 * 数值继续）。基座体为 FixedBody 零物性占位（links[0] 固连基座不入递推）。
 *
 * @param model [in] 快照规范模型（只读——唯一输入权威）
 * @return 就绪载体（SerialDevice＋全转动/移动关节——上游 validate 形态由
 *         构造保证）
 *
 * @throws RuntimeError InputInvalid：空链／链携带 Fixed 关节（RNE 面仅支持
 *         三个可动类型）／关节轴非单位向量／零位偏置非有限（公共边界防御；
 *         产品路径由消费方前置校验先行，本层校验为不可达防御——NFR-COR-03）
 */
RneConsumptionDevice buildRneConsumptionDevice(const CanonicalModel& model);

}}}  // namespace sdurws::ird::runtime

#endif  // IRD_RUNTIME_RNECONSUMPTIONDEVICE_HPP
