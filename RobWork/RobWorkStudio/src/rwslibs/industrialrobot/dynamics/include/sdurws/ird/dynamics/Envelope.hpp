/**
 * @file   Envelope.hpp
 * @brief  峰值/RMS/多工况包络计算器（units/dynamics.md §10.4
 *         IDynamicsEnvelopeCalculator 的实现载体）——工况级峰值（含持续
 *         时间窗与所在轨迹段）、完整任务循环时间加权 RMS（含驻留）与
 *         多工况包络合并的**唯一实现点**（§7.2/§7.3/§7.4 统计口径）。
 *
 * 设计依据：
 *   - units/dynamics.md §7.2（峰值与持续时间窗——DYN-03 硬性口径：峰值
 *     发生时间＋所在轨迹段必填；持续时间窗＝|值|达到峰值的连续样本集合
 *     的最小覆盖时间区间，样本级精确、不引入比例阈值）、§7.3（RMS 与
 *     完整任务循环——时间加权梯形积分、含驻留、禁样本平均）、§7.4
 *     （多工况包络——DYN-07 合并规则与"不替代必验工况"纪律）、§8.3
 *     （多工况覆盖和包络统计图）、§7.6（统计完整性规则——稳定排序/
 *     Empty 不产出）、§10.4（契约表）、§4.5（量纲表——转动 N·m/移动 N
 *     类型化）
 *   - 需求 DYN-03（输出峰值和 RMS 包络；峰值必须报告持续时间窗和所在
 *     轨迹段，RMS 基于完整任务循环〔含驻留〕）、DYN-07（多负载工况/
 *     急停保持设计工况＋结果包络合并；包络合并不替代必验工况覆盖规则）、
 *     EVI-02（正式计算与正式判定必须覆盖全部启用的必验工况——包络合并
 *     仅为呈现方式）、NFR-COR-02（稳定排序/确定性）、NFR-COR-03（非有限
 *     数拒绝——非 Ok 行不进统计/Empty 不 0 值伪装）
 *   - 决策 D-DYN-8（峰值持续时间窗＝峰值样本连续覆盖区间——无阈值化）、
 *     D-DYN-9（RMS 时间加权〔梯形积分〕含驻留；禁样本平均）、D-DYN-4
 *     （类型化广义力＝存储 SI double＋jointType 标签——统计逐关节独立，
 *     转动 N·m/移动 N 不混算）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（computePeaks/computeRms）、
 *     tasks/foundation/WP-17-T07.json（mergeEnvelope——多工况与包络合并）
 *
 * ★ T04/T07 分界（诚实登记——单元卡 §12 既有口径）：§10.4 接口三方法中
 *   computePeaks/computeRms 归 WP-17-T04（本头）；mergeEnvelope（多工况
 *   包络合并，返回 DynamicsEnvelope）归 WP-17-T07（DTB §2.18：T04＝序列
 *   ＋峰值/RMS，T07＝多工况与包络合并）——T07 已在本头增列（原"不预建
 *   占位"注的历史口径见任务留痕）。
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
 * 确定性：同输入→同输出（逐位——遍历序固定、合并序按 conditionId
 *   字典序、无环境依赖，NFR-COR-02）。
 */

#ifndef IRD_DYNAMICS_ENVELOPE_HPP
#define IRD_DYNAMICS_ENVELOPE_HPP

#include <cstdint>
#include <vector>

#include <sdurws/ird/dynamics/DynTypes.hpp> // DynamicsSeries/PeakRecord/
                                            //   OperatingConditionResult/
                                            //   DynamicsEnvelope/DynJointType
#include <sdurws/ird/dynamics/Errors.hpp>   // DynamicsError（mergeEnvelope
                                            //   入参契约违约的 fail-fast 异常轨）
#include <sdurws/ird/evidence/Snapshot.hpp> // evidence::RequiredCaseSet（§4.1.3
                                            //   快照冻结必验工况集——mergeEnvelope
                                            //   覆盖分母口径；已登记 evidence 编译边）

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

/**
 * @brief 包络 canonical 内容身份的域分隔 magic（DynamicsEnvelope.
 *        contentIdentity 编码起头——8 字节 ASCII"IRDDYVE1"，v1）。域分隔
 *        防止包络摘要与序列（IRDDYNS1）/证据（IRDDYEV1）等其他域摘要
 *        混同（同 SeriesBuilder.cpp 编码规则 1 的登记精神）；词面冻结＝
 *        契约测试静态钉扎面——改动即跨版本摘要断链，必须走单元卡增量
 *        修订。
 */
inline constexpr char kEnvelopeContentMagic[8] = {'I', 'R', 'D', 'D', 'Y', 'V', 'E', '1'};

// =====================================================================
// 峰值/RMS/多工况包络计算器（§10.4 IDynamicsEnvelopeCalculator 的具体
// 实现载体——域内具体类，同 T03 先例；mergeEnvelope 随 WP-17-T07 增列）。
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

    /**
     * @brief 多工况包络合并（§10.4 mergeEnvelope——DYN-07 的唯一计算入口；
     *        [in] 两参只读）。
     *
     * 合并规则（§7.4/§8.3 原文口径）：
     *   - 关节行集＝各工况序列含 Ok 行关节的并集（行序 jointIndex 升序）；
     *   - 逐关节逐量纲跨工况取 max：力矩正/反、速度、加速度各取 winning
     *     PeakRecord（值/发生时间/段/窗/来源工况随胜者）；并列峰值取
     *     conditionId 字典序更小工况的记录（确定性——与入参顺序无关）；
     *   - powerPeak＝合并幅值 max(|P|)：逐工况取 max(max(P), max(−P)) 再
     *     跨工况取 max（幅值恒非负——PowerEnergySummary.powerPeak 同口径）；
     *   - rmsTau＝跨工况 computeRms 的 max（包络＝最坏工况呈现；NaN 不
     *     参与 max，全部无效→NaN 显式无效）；
     *   - contributingConditions＝该关节有峰值贡献的工况集（ObjectId 字典
     *     序升序——稳定排序）；
     *   - envelopeComplete＝该关节全部来源工况 completeness==Complete
     *     （任一 Partial/Failed→false——§7.4"不输出看似完整的包络"）。
     *
     * 覆盖口径（EVI-02，acceptance 2——覆盖矩阵分母按快照冻结
     * RequiredCaseSet）：coversAllMandatory＝分母（coverage 中 enabled∧
     * mandatory 条目）是否全部出现在分子（results 中 completeness≠Empty
     * 的 conditionId 集）。★ 仅为呈现参考（§4.4 原文）：逐工况 Executed
     * 覆盖判定归 evidence 覆盖矩阵，包络合并不替代必验工况覆盖规则——
     * 本布尔只按"工况是否在入参中且产出了样本"计，不解读执行五态
     * （Executed/Failed 的区分属编排层词表，统计器无从判定）；空分母→
     * true（平凡完备——P-EV-7；保守处置归 evidence 汇总判定层）。
     *
     * Empty 语义（§7.4/§7.6）：无任一入参产出统计行（results 空或全部
     * 序列无 Ok 行）→joints 空（"包络不产出"）——绝不返回 0 值或默认
     * 通过包络（NFR-COR-03）；身份块仍完整（conditionCount/coversAll
     * Mandatory/contentIdentity 照算——包络可寻址）。
     *
     * 入参契约（调用方错误 fail-fast——§10.0，DynamicsError，不发稳定码）：
     *   - 逐工况：conditionId 非空且与 series.conditionId 恒同；validity
     *     与 series.validity 恒同（§4.4 透传契约）；results 内 conditionId
     *     无重复（同一工况两次入合并＝装配歧义）；
     *   - 峰值装配一致性：result.peaks 必须逐位等于 computePeaks(result.
     *     series)（§10.4 峰值统计口径唯一实现点产出——DynTypes.hpp 对
     *     OperatingConditionResult.peaks 的生产路径契约）；序列含 Ok 行而
     *     peaks 为空＝装配缺失，同拒；
     *   - coverage 条目：caseId 非空（evidence §4.1.3 保留值拒绝同口径）
     *     且 entries 内 caseId 无重复（冻结集唯一性契约的防御面）。
     *
     * @param results  [in] 已执行工况的结果集（编排层按 caseSubset 分批
     *                 后的 Executed 集合——§8.3；顺序任意，输出与顺序
     *                 无关）
     * @param coverage [in] 快照冻结必验工况集（evidence §4.1.3——覆盖
     *                 分母口径；§10.4 草案名 CaseCoverageSnapshot 的
     *                 落地面：该类型未在 evidence 卡产出，以冻结
     *                 RequiredCaseSet 承载分母——DTB §5.4 偏差，登记于
     *                 单元卡 §1.2 T07 注）
     * @return 合并包络（量纲 §4.5 类型化逐关节；contentIdentity＝
     *         canonical SHA-256，magic kEnvelopeContentMagic，字段序＝
     *         conditionCount→coversAllMandatory→逐关节行〔jointIndex/
     *         jointObjectId/六峰值/rmsTau/contributingConditions〔升序〕/
     *         envelopeComplete〕；NaN 位模式同二进制内确定——同输入必得
     *         同摘要，NFR-COR-02）
     *
     * 复杂度：O(Σn_c·(1+峰值复算)＋J·C)——n_c 为各工况样本行数、J 关节
     * 数、C 工况数（峰值装配一致性校验复算一遍 computePeaks）。
     */
    DynamicsEnvelope mergeEnvelope(const std::vector<OperatingConditionResult>& results,
                                   const evidence::RequiredCaseSet& coverage) const;
};

}  // namespace sdurws::ird::dynamics

#endif  // IRD_DYNAMICS_ENVELOPE_HPP
