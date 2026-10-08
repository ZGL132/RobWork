/**
 * @file   PowerEnergy.cpp
 * @brief  机械功率/能量摘要计算器实现（units/dynamics.md §7.5/§10.5）——
 *         逐关节正负功分项梯形积分、平均功率与功率峰值（含窗与段）。
 *
 * 设计依据（契约面见同名公共头 PowerEnergy.hpp 文件头）：
 *   - units/dynamics.md §7.5（符号约定全卡固定——P=τ_total·q̇、E⁺/E⁻/
 *     E_net、积分边界 [t₀,t_N] 含驻留、梯形时间加权）、§10.5（契约表——
 *     无效不伪造）、§4.4（PowerEnergySummary 字段）、§4.6（无时间参数
 *     行——字段显式无效）
 *   - 需求 DYN-03、NFR-COR-02（确定性）、NFR-COR-03（非有限不静默）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1）
 *
 * 实现要点：
 *   1. 单遍扫描：按行序（时间升序）逐 Ok 行推进每关节的 (E⁺, E⁻, 峰)
 *      三条累加链——梯形累加序＝时间序（确定性，NFR-COR-02）；
 *   2. 功率峰值＝max(|P|)（幅值形态——PowerEnergySummary.powerPeak 单
 *      记录位的合并口径；等值窗与 tPeakS/segmentIndex 语义同
 *      EnvelopeCalculator::computePeaks——D-DYN-8 无阈值等值 run）；
 *   3. Ok 行字段的有限性由上游保证（评估器契约）；标记矛盾（Ok 行携带
 *      NaN）属上游缺陷——NaN 经算术比较自然扩散（幂函数极值比较不进
 *      而已），统计器不做二次静默过滤（同 Envelope.cpp 口径）。
 */

#include <sdurws/ird/dynamics/PowerEnergy.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace sdurws::ird::dynamics {

namespace {

/// 单关节累加链状态（时间升序逐行推进——文件头实现要点 1）。
struct JointAccumulator {
    double firstT = 0.0;        ///< 首个有效行时刻，s（积分下界 t₀）
    double lastT = 0.0;         ///< 末个有效行时刻，s（积分上界 t_N）
    double prevP = 0.0;         ///< 前一有效行功率，W（梯形累加状态）
    bool havePrev = false;      ///< 梯形状态就绪位（首行只记边界不积分）
    bool havePeak = false;      ///< 幅值峰值已捕获位（首行无条件捕获——含 NaN 显性传播）
    double positiveJ = 0.0;     ///< E⁺ 累加，J（∫max(P,0)dt）
    double negativeJ = 0.0;     ///< E⁻ 累加，J（∫min(P,0)dt——恒 ≤0）
    double peakAbsW = 0.0;      ///< max(|P|) 幅值峰值，W（powerPeak.value 候选）
    std::size_t peakRow = 0;    ///< 峰值行下标（series.samples 内——窗/段定位）
    std::vector<std::size_t> rows; ///< 该关节 Ok 行下标（时间序——等值 run 扩展用）
};

}  // namespace

// =====================================================================
// compute（公共头契约的实现——单遍扫描＋逐关节分项积分）。
// =====================================================================

PowerEnergySummary PowerEnergyCalculator::compute(const DynamicsSeries& series) const
{
    // ---- 第 1 步：按关节分组 Ok 行并单遍推进累加链（NFR-COR-03：非
    //      Ok 行不进积分；关节集合升序——输出行序稳定）----
    std::vector<std::uint32_t> jointIds;        ///< 出现的关节集合（升序——输出序）
    std::vector<JointAccumulator> accs;         ///< 与 jointIds 平行的累加链
    for (std::size_t k = 0; k < series.samples.size(); ++k) {
        const DynamicsSample& r = series.samples[k];
        if (r.numericState != SampleNumericState::Ok) {
            continue;  // 非 Ok 行（NonFiniteInput/Output/Overflow）不进积分
        }
        const std::uint32_t j = r.jointIndex;
        auto it = std::lower_bound(jointIds.begin(), jointIds.end(), j);
        if (it == jointIds.end() || *it != j) {
            it = jointIds.insert(it, j);
            accs.insert(accs.begin() + (it - jointIds.begin()), JointAccumulator{});
        }
        JointAccumulator& a = accs[static_cast<std::size_t>(it - jointIds.begin())];
        a.rows.push_back(k);

        // 功率（W）——评估器已按 P=τ_total·q̇ 产出（§7.5）；本计算器
        // 直读不重乘（重乘需要 τ_total×q̇ 两次舍入一致——行内值即权威）。
        const double p = r.mechanicalPower;

        // 时间加权分项积分（§7.5）：E⁺ 用 max(P,0)、E⁻ 用 min(P,0)
        // 分别梯形——正负功拆分的数学形态（ crest 因子：同一区间 P 换
        // 号时两侧各取其半段贡献）。
        if (!a.havePrev) {
            a.firstT = r.t;      // 积分下界（首有效行）
            a.havePrev = true;
        } else {
            const double dt = r.t - a.lastT;  // s（相邻有效行时差——缺口时长保留）
            a.positiveJ += 0.5 * (std::max(a.prevP, 0.0) + std::max(p, 0.0)) * dt;
            a.negativeJ += 0.5 * (std::min(a.prevP, 0.0) + std::min(p, 0.0)) * dt;
        }
        a.prevP = p;
        a.lastT = r.t;

        // 功率幅值峰值（首个达到者保留——严格大于才替换，确定性口径；
        // 首行无条件捕获：NaN 位模式经此显性传播到峰值输出，不静默）。
        const double absP = std::abs(p);
        if (!a.havePeak || absP > a.peakAbsW) {
            a.havePeak = true;
            a.peakAbsW = absP;
            a.peakRow = k;
        }
    }

    // ---- 第 2 步：装配摘要（无效语义显式 NaN——不伪造 0，§4.6）----
    PowerEnergySummary out;
    out.joints.reserve(accs.size());
    bool topLevelSet = false;   ///< 顶层字段就绪位（首个可积分关节定标——见下）
    for (JointAccumulator& a : accs) {
        PowerEnergySummary::JointPowerEnergy row;
        row.jointIndex = jointIds[&a - accs.data()];

        // 时间参数可用性（逐关节判定——单序列单时间轴，正常路径各关
        // 节一致；行级 Ok 标记差异时以"首个可积分关节"定标顶层字段）。
        const bool timeOk = a.rows.size() >= 2 && a.firstT != a.lastT;
        row.positiveEnergyJ = timeOk ? a.positiveJ
                                     : std::numeric_limits<double>::quiet_NaN();
        row.negativeEnergyJ = timeOk ? a.negativeJ
                                     : std::numeric_limits<double>::quiet_NaN();
        row.netEnergyJ = timeOk ? (a.positiveJ + a.negativeJ)
                                : std::numeric_limits<double>::quiet_NaN();
        const double cycleS = a.lastT - a.firstT;   // T_cycle（s；含驻留全程）
        row.meanPowerW = timeOk ? (row.netEnergyJ / cycleS)
                                : std::numeric_limits<double>::quiet_NaN();

        // 功率峰值（含窗与段）：等值 run 在该关节时间序上扩展（同
        // Envelope 口径——位等值无阈值；幅值并列取时间轴首个）。
        const DynamicsSample& peakRow = series.samples[a.peakRow];
        const std::size_t pos = static_cast<std::size_t>(
            std::find(a.rows.begin(), a.rows.end(), a.peakRow) - a.rows.begin());
        std::size_t lo = pos;
        std::size_t hi = pos;
        auto equalAt = [&](std::size_t rowsPos) {
            return std::abs(series.samples[a.rows[rowsPos]].mechanicalPower)
                   == a.peakAbsW;  // 幅值位等值——无容差（D-DYN-8）
        };
        while (lo > 0 && equalAt(lo - 1)) { --lo; }
        while (hi + 1 < a.rows.size() && equalAt(hi + 1)) { ++hi; }
        row.powerPeak.value = a.peakAbsW;                       // W（幅值形态）
        row.powerPeak.tPeakS = peakRow.t;                       // s
        row.powerPeak.segmentIndex = peakRow.segmentIndex;      // 所在轨迹段
        row.powerPeak.windowStartS = series.samples[a.rows[lo]].t;  // 窗起点，s
        row.powerPeak.windowEndS = series.samples[a.rows[hi]].t;    // 窗终点，s
        row.powerPeak.conditionId = series.conditionId;         // 来源工况

        // 循环时长与驻留计入位（顶层字段＝序列级；由首个可积分关节
        // 定标——时间可用即积分已按 [t₀,t_N] 全程执行，上游含驻留段
        // 即自动计入；统计器无段类型信息、不做速度阈值判定）。
        if (!topLevelSet && timeOk) {
            out.timeParamAvailable = true;
            out.cycleDurationS = cycleS;
            out.includesDwell = true;
            topLevelSet = true;
        }
        out.joints.push_back(std::move(row));
    }
    if (!topLevelSet) {
        // 无任何可积分关节（空序列/单样本/零跨度）——顶层字段显式无效
        //（NotProvided 语义，不伪造 0；§4.6"无时间参数"行同口径）。
        out.timeParamAvailable = false;
        out.cycleDurationS = std::numeric_limits<double>::quiet_NaN();
        out.includesDwell = false;
    }
    return out;
}

}  // namespace sdurws::ird::dynamics
