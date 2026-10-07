/**
 * @file   PowerEnergy.hpp
 * @brief  机械功率/能量摘要计算器（units/dynamics.md §10.5
 *         IPowerEnergyCalculator 的具体实现载体）——§7.5 符号约定的
 *         **唯一实现点**：P=τ_total·q̇、E⁺=∫max(P,0)dt、E⁻=∫min(P,0)dt、
 *         E_net=E⁺+E⁻，梯形时间加权，积分边界 [t₀,t_N] 含驻留。
 *
 * 设计依据：
 *   - units/dynamics.md §7.5（机械功率、能量与符号约定——全卡固定：正
 *     号＝驱动器输出正功、负号＝制动/下降/发电；积分边界＝完整任务循
 *     环含驻留；能量分项边界＝关节侧归 dynamics、电机侧归 drivetrain
 *     不混算）、§10.5（契约表——前置"时间参数可用，否则
 *     timeParamAvailable=false＋字段显式无效——不伪造积分"）、§4.4
 *     （PowerEnergySummary 数据模型＋P-DYN-9 能量量纲口径）、§4.6
 *     （"无时间参数"行——不伪造节拍、功率积分或能量）
 *   - 需求 DYN-03（机械功率序列→峰值/包络统计面）、NFR-COR-03（非有限
 *     不静默——无效显式 NaN 不伪造 0）
 *   - 决策 D-DYN-9（时间加权——禁样本平均，同 RMS 口径）、D-DYN-5
 *     （电机侧能量分项与四象限统计归 drivetrain——本头只算关节侧，
 *     零传动映射消费）
 *   - 待裁决 P-DYN-9（能量量纲运行容差缺位——**不得预填**：本头零运
 *     行校验容差，数值正确性只能由测试黄金算例对照，附录 D C7）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1"机械功率
 *     序列输出"面）
 *
 * 背景说明（关节侧能量与样本内 energyIntegralJ 的关系）：评估器
 *   （WP-17-T03）已在样本行携带能量积分状态 E(t)=∫₀ᵗ P dτ（净能量，
 *   逐样本梯形累积）；本计算器**独立**对序列重算 E⁺/E⁻ 分项（正负功
 *   拆分需要对 max/min(P,0) 分别积分——单条净能量状态无法拆出），两
 *   条积分链的对照（E_net 与 energyIntegralJ 末样本一致）是黄金算例
 *   的被验证性质（V-22 行），不是本计算器的构造恒等式——与 §5.2
 *   τ_total 恒等式同款分层。
 *
 * 线程安全：实例无状态、方法纯函数（输入只读）。
 * 确定性：同输入→同输出（逐位——梯形累加序固定，NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_POWERENERGY_HPP
#define IRD_DYNAMICS_POWERENERGY_HPP

#include <sdurws/ird/dynamics/DynTypes.hpp> // DynamicsSeries/PowerEnergySummary

namespace sdurws::ird::dynamics {

/**
 * @brief 机械功率/能量摘要计算器（§10.5 IPowerEnergyCalculator 的具体
 *        实现载体——域内具体类，同 T03 先例；evidence 适配随 WP-17-T10）。
 */
class PowerEnergyCalculator {
public:
    PowerEnergyCalculator() = default;

    /// 可拷贝可移动（无状态——实例仅是调用边界）。
    PowerEnergyCalculator(const PowerEnergyCalculator&) = default;
    PowerEnergyCalculator& operator=(const PowerEnergyCalculator&) = default;

    /**
     * @brief 计算功率/能量摘要（§10.5 compute——[in] 序列只读）。
     *
     * 计算（§7.5，逐关节独立——类型化不混算）：
     *   1. 只消费 numericState==Ok 的样本行（非 Ok 行不进积分——
     *      NFR-COR-03；缺口不插值，缺口时长计入相邻有效行 Δt、贡献 0）；
     *   2. E⁺ ≈ Σ ½(max(P_k,0)+max(P_{k+1},0))·Δt、
     *      E⁻ ≈ Σ ½(min(P_k,0)+min(P_{k+1},0))·Δt（对采样网格的梯形
     *      时间加权——正负功拆分分别积分，P 由行内 mechanicalPower
     *      直读〔评估器已按 τ_total·q̇ 产出〕）；
     *   3. E_net=E⁺+E⁻、meanPower=E_net/T_cycle、T_cycle=t_N−t₀；
     *   4. powerPeak＝max(|P|)（幅值形态——含窗与段；正反向分列峰值由
     *      EnvelopeCalculator::computePeaks 承载，本摘要单记录位取合并
     *      幅值——单元卡 §1.2 登记口径）。
     *
     * 无效语义（§10.5 前置/§4.6）：序列无 Ok 行、或有效行 <2、或时间
     * 跨度为 0 → timeParamAvailable=false，cycleDurationS/能量/平均
     * 功率全部显式 NaN（NotProvided——**不伪造 0、不伪造积分**）；空
     * 关节集（Empty 序列）→ joints 为空 vector（Empty 不产出）。
     *
     * @param series [in] 单工况序列（行序遵守 §4.6——构建器产物）
     * @return 摘要（逐关节行 jointIndex 升序；includesDwell 语义见
     *         PowerEnergySummary 字段注释——"积分已按序列全程时间轴
     *         执行"，上游含驻留段即自动计入，统计器不做速度阈值判定）
     *
     * 复杂度：O(n)，n 为样本行数。
     */
    PowerEnergySummary compute(const DynamicsSeries& series) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_POWERENERGY_HPP
