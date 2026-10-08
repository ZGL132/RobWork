/**
 * @file   Envelope.cpp
 * @brief  峰值/RMS 统计计算器实现（units/dynamics.md §7.2/§7.3/§10.4）——
 *         逐关节逐量峰值（值＋发生时间＋所在段＋持续等值窗）与完整循环
 *         时间加权力矩 RMS（含驻留）。
 *
 * 设计依据（契约面见同名公共头 Envelope.hpp 文件头）：
 *   - units/dynamics.md §7.2（峰值三要素＋持续时间窗＝峰值样本连续等值
 *     run——D-DYN-8 无阈值化）、§7.3（RMS 时间加权梯形积分含驻留——
 *     D-DYN-9 禁样本平均）、§7.6（统计完整性——稳定排序/Empty 不产出）、
 *     §10.4（契约表）、§4.5（量纲——转动 N·m/移动 N 类型化逐关节）
 *   - 需求 DYN-03（峰值/RMS 口径）、NFR-COR-02（确定性）、NFR-COR-03
 *     （非有限不静默——非 Ok 行不进统计）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1/2）
 *
 * 实现要点：
 *   1. 峰值主扫两遍：第一遍按关节分组并求六量极值（严格大于才替换——
 *      并列峰值取时间轴首个，tPeakS 确定性口径）；第二遍对每个峰值从
 *      其样本位置向两侧扩展位等值 run（windowStartS/windowEndS）。
 *   2. Ok 行的字段有限性由上游保证（评估器契约：非有限行恒标记非
 *      Ok）；标记矛盾（Ok 行携带 NaN）属上游缺陷——NaN 经比较语义
 *      自然扩散到峰值输出（NaN 不大于任何值，极值保持首行位——显性
 *      暴露，统计器不做二次静默过滤，也不伪造 0）。
 *   3. 确定性（NFR-COR-02）：遍历序＝series 行序（构建器产物已按
 *      (t, jointIndex) 稳定排序）、关节集合升序处理、无环境依赖——
 *      同输入逐位同输出。
 */

#include <sdurws/ird/dynamics/Envelope.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sdurws::ird::dynamics {

namespace {

/// 关节 Ok 行下标列表（series.samples 的下标——不拷贝行本体）。
using RowIndices = std::vector<std::size_t>;

/// 六统计量的 token 序号（与 Envelope.hpp token 表一一对应——唯一书写点）。
enum PeakToken {
    kTauPositive = 0,    ///< τ_max⁺＝max(τ)（N·m 或 N——带符号实际值）
    kTauNegative = 1,    ///< τ_max⁻＝max(−τ)（反向幅值形态）
    kVelocity = 2,       ///< max|q̇|（rad/s 或 m/s）
    kAcceleration = 3,   ///< max|q̈|（rad/s² 或 m/s²）
    kPowerPositive = 4,  ///< max(P)（W——带符号实际值）
    kPowerNegative = 5,  ///< max(−P)（反向幅值形态）
};

/**
 * @brief 样本行在某 token 下的统计量值 v（峰值/等值 run 的比较量——
 *        符号语义见 PeakRecord 注释：力矩/功率分列正反向、速度/加速度
 *        取幅值）。
 *
 * @param r      [in] 样本行（只读）
 * @param token  [in] 统计量 token（0..5——kPeakTokenCount 内）
 * @return 统计量值（量纲随 token 与 jointType——§4.5 表；Ok 行字段有限
 *         性由上游保证——文件头实现要点 2）
 */
double statValue(const DynamicsSample& r, int token)
{
    switch (token) {
        case kTauPositive:   return r.tauTotal;              // τ（N·m 或 N）
        case kTauNegative:   return -r.tauTotal;             // −τ（反向幅值形态）
        case kVelocity:      return std::abs(r.qd);          // |q̇|（rad/s 或 m/s）
        case kAcceleration:  return std::abs(r.qdd);         // |q̈|（rad/s² 或 m/s²）
        case kPowerPositive: return r.mechanicalPower;       // P（W）
        case kPowerNegative: return -r.mechanicalPower;      // −P（反向幅值形态）
        default:             return std::numeric_limits<double>::quiet_NaN();
    }
}

}  // namespace

// =====================================================================
// computePeaks（公共头契约的实现——逐关节六量峰值＋等值窗）。
// =====================================================================

std::vector<PeakRecord> DynamicsEnvelopeCalculator::computePeaks(
    const DynamicsSeries& series) const
{
    // ---- 第 1 步：按关节分组 Ok 行（NFR-COR-03：非 Ok 行的 NaN 不进
    //      统计；Empty 序列/无 Ok 行→空 vector——§7.6 Empty 不产出）----
    std::vector<std::uint32_t> jointIds;               ///< 出现的关节集合（升序——输出序）
    std::vector<RowIndices> jointRows;                 ///< 与 jointIds 平行的行下标表
    for (std::size_t k = 0; k < series.samples.size(); ++k) {
        if (series.samples[k].numericState != SampleNumericState::Ok) {
            continue;  // 非 Ok 行（NonFiniteInput/Output/Overflow）不进统计
        }
        const std::uint32_t j = series.samples[k].jointIndex;
        // 关节集合有序插入（lower_bound 定位——关节数≤链长，成本可忽略；
        // 行下标表 jointRows 与 jointIds 同位平行维护）。
        auto it = std::lower_bound(jointIds.begin(), jointIds.end(), j);
        if (it == jointIds.end() || *it != j) {
            it = jointIds.insert(it, j);
            jointRows.insert(jointRows.begin() + (it - jointIds.begin()), RowIndices{});
        }
        jointRows[static_cast<std::size_t>(it - jointIds.begin())].push_back(k);
    }
    if (jointIds.empty()) {
        return {};  // Empty 语义——不产出统计，绝不 0 值伪装（§7.6）
    }

    // ---- 第 2 步：逐关节逐 token 求峰＋等值窗（输出行序＝(jointIndex
    //      升序, token 序)——§7.6 全序的落地面）----
    std::vector<PeakRecord> peaks;
    peaks.reserve(jointIds.size() * kPeaksPerJoint);
    for (std::size_t jRow = 0; jRow < jointIds.size(); ++jRow) {
        const RowIndices& rows = jointRows[jRow];  // 该关节 Ok 行（时间升序）
        for (int token = 0; token < kPeakTokenCount; ++token) {
            // 2a. 求峰：严格大于才替换——并列峰值保留**时间轴首个**样本
            //     （tPeakS 确定性口径；NaN 语义见文件头实现要点 2）。
            std::size_t peakPos = rows.front();
            double peakVal = statValue(series.samples[peakPos], token);
            for (const std::size_t k : rows) {
                const double v = statValue(series.samples[k], token);
                if (v > peakVal) {
                    peakVal = v;
                    peakPos = k;
                }
            }
            // 2b. 持续时间窗（D-DYN-8）：从峰值样本向两侧扩展"统计量位
            //     等值"的连续 run——样本级精确、无比例阈值；平顶自然
            //     覆盖整段，孤立峰值窗宽为零。run 在**该关节时间序**
            //     （rows）上扩展——series 全局行序是多关节交错行，跨关
            //     节相邻行不构成该关节的连续样本。
            const DynamicsSample& peakRow = series.samples[peakPos];
            const std::size_t pos = static_cast<std::size_t>(
                std::find(rows.begin(), rows.end(), peakPos) - rows.begin());
            std::size_t lo = pos;
            std::size_t hi = pos;
            auto equalAt = [&](std::size_t rowsPos) {
                return statValue(series.samples[rows[rowsPos]], token) == peakVal;  // 位等值——无容差
            };
            while (lo > 0 && equalAt(lo - 1)) {
                --lo;  // 向时间减小方向扩展（同关节相邻 Ok 行——run 连续性）
            }
            while (hi + 1 < rows.size() && equalAt(hi + 1)) {
                ++hi;  // 向时间增大方向扩展
            }
            // 2c. 组装行（DYN-03 三要素＋窗＋来源工况——PeakRecord 契约）。
            PeakRecord rec;
            rec.value = peakVal;
            rec.tPeakS = peakRow.t;                    // s（首个达到峰值样本时刻）
            rec.segmentIndex = peakRow.segmentIndex;   // 所在轨迹段（0 基直通）
            rec.windowStartS = series.samples[rows[lo]].t;  // 窗起点（run 首 t）
            rec.windowEndS = series.samples[rows[hi]].t;    // 窗终点（run 末 t）
            rec.conditionId = series.conditionId;      // 来源工况（包络合并供给源）
            peaks.push_back(std::move(rec));
        }
    }
    return peaks;
}

// =====================================================================
// computeRms（公共头契约的实现——时间加权含驻留；D-DYN-9 禁样本平均）。
// =====================================================================

double DynamicsEnvelopeCalculator::computeRms(const DynamicsSeries& series,
                                              std::uint32_t jointIndex) const
{
    // ---- 第 1 步：收集该关节 Ok 行（§4.6：非 Ok 行不进统计；缺口不
    //      插值——缺口时长保留在相邻有效行的 Δt 中）----
    double firstT = 0.0;             // 有效行首时刻，s
    double lastT = 0.0;              // 有效行末时刻，s
    double prevTau = 0.0;            // 前一有效行 τ_total²（梯形累加状态）
    bool havePrev = false;           // 梯形状态就绪位
    double weightedSum = 0.0;        // ∫τ²dt 的梯形累加（单位 量纲²·s）
    for (const DynamicsSample& r : series.samples) {
        if (r.jointIndex != jointIndex || r.numericState != SampleNumericState::Ok) {
            continue;  // 非本关节行 / 非 Ok 行——不进统计（NFR-COR-03）
        }
        const double tau2 = r.tauTotal * r.tauTotal;  // v_i(t)²——τ_total 为统计量（§7.3）
        if (!havePrev) {
            // 首个有效行＝积分下界 t₀（驻留段在序列头部同样进入边界）。
            firstT = r.t;
            prevTau = tau2;
            havePrev = true;
        } else {
            // 梯形时间加权：½(τ_k²＋τ_{k−1}²)·Δt——相邻**有效**行对；
            // 缺口/非 Ok 行被跳过但其时间差计入 Δt（无数据贡献 0——
            // 保守低估，公共头 computeRms 注释口径）。
            weightedSum += 0.5 * (prevTau + tau2) * (r.t - lastT);
            prevTau = tau2;
        }
        lastT = r.t;  // 积分上界推进（末个有效行＝t_N）
    }

    // ---- 第 2 步：无效情形显式 NaN（§4.6/§7.3——不伪造 0）：无 Ok 行
    //      （Empty 语义）/有效行 <2（无时间区间）/跨度 0（不可时间加权）----
    if (!havePrev || firstT == lastT) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    // ---- 第 3 步：T_cycle＝有效行全跨度 t_N−t₀（完整循环含驻留——
    //      驻留段时间权重进入分母；§7.3 原文口径）----
    const double cycleS = lastT - firstT;  // s（>0——第 2 步已排除 0）
    return std::sqrt(weightedSum / cycleS);  // 量纲随 jointType：N·m 或 N（§4.5）
}

}  // namespace sdurws::ird::dynamics
