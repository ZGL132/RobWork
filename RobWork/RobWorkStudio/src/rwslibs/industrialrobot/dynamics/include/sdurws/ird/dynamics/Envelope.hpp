/**
 * @file   Envelope.hpp
 * @brief  峰值/RMS 统计计算器（units/dynamics.md §10.4
 *         IDynamicsEnvelopeCalculator 的统计面实现载体）——工况级峰值
 *         （含持续时间窗与所在轨迹段）与完整任务循环时间加权 RMS（含
 *         驻留）的**唯一实现点**（§7.2/§7.3 统计口径）。
 *
 * 设计依据：
 *   - units/dynamics.md §7.2（峰值与持续时间窗——DYN-03 硬性口径：峰值
 *     发生时间＋所在轨迹段必填；持续时间窗＝|值|达到峰值的连续样本集合
 *     的最小覆盖时间区间，样本级精确、不引入比例阈值）、§7.3（RMS 与
 *     完整任务循环——时间加权梯形积分、含驻留、禁样本平均）、§7.6
 *     （统计完整性规则——稳定排序/Empty 不产出）、§10.4（契约表）、
 *     §4.5（量纲表——转动 N·m/移动 N 类型化）
 *   - 需求 DYN-03（输出峰值和 RMS 包络；峰值必须报告持续时间窗和所在
 *     轨迹段，RMS 基于完整任务循环〔含驻留〕）、DYN-07（多工况包络——
 *     本任务不落，见下方分界登记）、NFR-COR-02（稳定排序/确定性）、
 *     NFR-COR-03（非有限数拒绝——非 Ok 行不进统计）
 *   - 决策 D-DYN-8（峰值持续时间窗＝峰值样本连续覆盖区间——无阈值化）、
 *     D-DYN-9（RMS 时间加权〔梯形积分〕含驻留；禁样本平均）、D-DYN-4
 *     （类型化广义力＝存储 SI double＋jointType 标签——统计逐关节独立，
 *     转动 N·m/移动 N 不混算）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1/2）
 *
 * ★ T04/T07 分界（诚实登记——单元卡 §12 既有口径，非本任务新裁）：
 *   §10.4 接口三方法中 computePeaks/computeRms 归 WP-17-T04（本头），
 *   mergeEnvelope（多工况包络合并，返回 DynamicsEnvelope）归 WP-17-T07
 *   （DTB §2.18：T04＝序列＋峰值/RMS，T07＝多工况与包络合并）——本头
 *   不预建 DynamicsEnvelope 类型与 merge 占位（NFR-MNT-04），T07 在本
 *   头表尾增列即可。
 *
 * 背景说明（两个口径为什么钉死在实现注释里）：
 *   1. 持续时间窗不引入比例阈值（如"达到峰值 90% 的样本"）：比例阈值
 *      属自设数值（附录 D 无此容差，D-DYN-8 原文），窗必须由样本等值
 *      关系精确导出——"峰值样本所在的连续等值 run"；浮点等值判定用
 *      位相等（峰值本身取自样本值，等值样本位模式相同；窗容差仅在
 *      黄金算例逐例声明——测试对照口径）。
 *   2. RMS 禁样本平均（D-DYN-9）：非均匀采样下 Σ/√N 会随采样密度漂移
 *      （密采段权重虚高）；梯形时间加权对采样网格逼近 ∫v²dt/T_cycle，
 *      黄金算例对照解析积分（V-30 行）。驻留段（q̇≈0、τ＝重力保持矩）
 *      以其时间权重自然进入分母与积分——重力矩计入 RMS、速度项贡献
 *      时间权重（§7.3 原文），统计器不做任何"驻留判定"。
 *
 * 线程安全：实例无状态、方法纯函数（输入只读）；多实例并行安全。
 * 确定性：同输入→同输出（逐位——遍历序固定、无环境依赖，NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_ENVELOPE_HPP
#define IRD_DYNAMICS_ENVELOPE_HPP

#include <cstdint>
#include <vector>

#include <sdurws/ird/dynamics/DynTypes.hpp> // DynamicsSeries/PeakRecord/DynJointType

namespace sdurws::ird::dynamics {

// =====================================================================
// 峰值行序的量纲 token 表（§7.6"统计条目按 (jointIndex, 量纲 token,
// conditionId) 全序"的落地面——单工况内 conditionId 恒同，行序＝
// (jointIndex 升序, 下表 token 序)；PeakRecord 结构无 token 字段（卡面
// 设计基线），本表即行语义的唯一权威，消费方按下表下标解读）。
// =====================================================================

/**
 * @brief 逐关节峰值行序的量纲 token 表（每关节恒 6 行，行序固定——
 *        §7.2 枚举序：力矩正/反分列→速度→加速度→功率正/反分列）。
 *
 * token 序常量（数组下标即 token 序）：
 *   0 tauPositive   τ_max⁺＝max(τ)      （N·m 或 N，按 jointType——带符号）
 *   1 tauNegative   τ_max⁻＝max(−τ)     （N·m 或 N——反向幅值形态）
 *   2 velocity      max|q̇|              （rad/s 或 m/s）
 *   3 acceleration  max|q̈|              （rad/s² 或 m/s²）
 *   4 powerPositive max(P)              （W——带符号实际值）
 *   5 powerNegative max(−P)             （W——反向幅值形态）
 * 每关节 6 行常量＝kPeaksPerJoint；符号语义细则见 PeakRecord 注释。
 */
inline constexpr int kPeakTokenCount = 6;
inline constexpr std::size_t kPeaksPerJoint = 6;   ///< 每关节峰值行数（上表长度）

// =====================================================================
// 峰值/RMS 统计计算器（§10.4 IDynamicsEnvelopeCalculator 统计面的具体
// 实现载体——域内具体类，同 T03 先例；mergeEnvelope 随 T07 增列）。
// =====================================================================

/**
 * @brief 峰值/RMS 统计计算器（§7 统计口径唯一实现点）。
 */
class DynamicsEnvelopeCalculator {
public:
    DynamicsEnvelopeCalculator() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    DynamicsEnvelopeCalculator(const DynamicsEnvelopeCalculator&) = default;
    DynamicsEnvelopeCalculator& operator=(const DynamicsEnvelopeCalculator&) = default;

    /**
     * @brief 计算单工况峰值（§10.4 computePeaks——逐关节逐量，行序见
     *        token 表；[in] 序列只读）。
     *
     * 统计范围（§7.1 层级表）：轨迹段级/工况级峰值——只消费
     * numericState==Ok 的样本行（非 Ok 行的 NaN 不进 max/min——
     * NFR-COR-03；Partial 序列的峰值属于已产出区间的峰值，不阻断）。
     * 空序列或无 Ok 行→返回空 vector（Empty 语义——§7.6"空样本→Empty，
     * 不产出统计，绝不 0 值伪装"）。
     *
     * 峰值三要素（DYN-03）：每行携带 value/tPeakS/segmentIndex＋持续
     * 时间窗 [windowStartS, windowEndS]（峰值样本所在连续等值 run——
     * 位等值、无阈值，见文件头背景说明 1）＋来源工况 conditionId
     * （＝series.conditionId 透传——包络合并时必填字段的供给源）。
     *
     * @param series [in] 单工况序列（行序遵守 §4.6——构建器产物；本
     *               方法不重排、不校验身份——统计纯函数）
     * @return 峰值行（行数＝含 Ok 行的关节数×kPeaksPerJoint；行序＝
     *         (jointIndex 升序, token 序)——NFR-COR-02 全序）
     *
     * 复杂度：O(n)，n 为样本行数（两遍：求极值→定位等值 run）。
     */
    std::vector<PeakRecord> computePeaks(const DynamicsSeries& series) const;

    /**
     * @brief 计算单关节完整任务循环力矩 RMS（§10.4 computeRms——时间
     *        加权、含驻留，§7.3 口径）。
     *
     * 定义（§7.3 原文公式）：RMS_i = sqrt( (1/T_cycle)·∫ v_i(t)² dt )，
     * v_i＝该关节 τ_total 时间序列；T_cycle＝积分边界全跨度
     * t_last−t_first（完整循环含驻留——驻留段以其时间权重进入分母，
     * 重力保持矩计入积分）。数值积分＝对采样网格的梯形时间加权（相邻
     * 有效样本对 ½(τ_k²+τ_{k+1}²)·Δt 累加）——**禁止** Σ/√N 样本平均
     * （D-DYN-9；非均匀采样失真）。
     *
     * 缺口与非 Ok 行（§4.6"Partial 不阻断该工况其余统计"）：仅消费
     * numericState==Ok 的行；缺口区间不插值（无数据贡献 0——时间加权
     * 分母仍取有效行全跨度，RMS 属保守低估——完整循环统计以 Complete
     * 序列为准，此口径登记于单元卡 §1.2）。
     *
     * @param series     [in] 单工况序列（只读）
     * @param jointIndex [in] 关节序号（0 基链序；无该关节 Ok 行→无效）
     * @return 力矩 RMS（量纲随 jointType：转动/连续 N·m、移动 N——
     *         §4.5 类型化）；**无效情形返回 NaN**（显式无效——不伪造
     *         0）：无 Ok 行（Empty 语义）、有效行 <2（无时间区间）、
     *         或时间跨度为 0（不可时间加权）
     *
     * 复杂度：O(n)（单遍扫描该关节行）。
     */
    double computeRms(const DynamicsSeries& series, std::uint32_t jointIndex) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_ENVELOPE_HPP
