/**
 * @file   Envelope.cpp
 * @brief  峰值/RMS/多工况包络计算器实现（units/dynamics.md §7.2/§7.3/
 *         §7.4/§10.4）——逐关节逐量峰值（值＋发生时间＋所在段＋持续等值
 *         窗）、完整循环时间加权力矩 RMS（含驻留）与跨工况包络合并
 *         （DYN-07：来源工况集＋完整性传播＋EVI-02 覆盖呈现参考）。
 *
 * 设计依据（契约面见同名公共头 Envelope.hpp 文件头）：
 *   - units/dynamics.md §7.2（峰值三要素＋持续时间窗＝峰值样本连续等值
 *     run——D-DYN-8 无阈值化）、§7.3（RMS 时间加权梯形积分含驻留——
 *     D-DYN-9 禁样本平均）、§7.4（多工况包络——max⁺/max⁻ 方向分列/
 *     来源工况/envelopeComplete 传播/Empty 不产出）、§8.3（多工况覆盖
 *     和包络统计图——包络合并与覆盖矩阵独立呈现，EVI-02）、§7.6（统计
 *     完整性——稳定排序/Empty 不产出）、§10.4（契约表）、§4.5（量纲——
 *     转动 N·m/移动 N 类型化逐关节）
 *   - 需求 DYN-03（峰值/RMS 口径）、DYN-07＋EVI-02（包络合并不替代必验
 *     工况覆盖——coversAllMandatory 仅为呈现参考，分母＝快照冻结
 *     RequiredCaseSet）、NFR-COR-02（确定性）、NFR-COR-03（非有限不
 *     静默——非 Ok 行不进统计）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1/2——峰值/
 *     RMS）、tasks/foundation/WP-17-T07.json（acceptance 1/2/3——多工况
 *     与包络合并/覆盖口径不替代/P-DYN-4 统一 RNEA 不自设构造语义）
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
 *   4. 包络合并（WP-17-T07）：工况处理序＝conditionId 字典序升序（输出
 *      与入参顺序无关）；逐关节六 token 跨工况严格大于才替换（并列取
 *      conditionId 更小工况——合并序确定性）；RMS 跨工况 max 且 NaN 不
 *      参与（显式无效不伪装 0）；包络 canonical 摘要（magic
 *      IRDDYVE1）编码规则见 mergeEnvelope 实现段头注。
 */

#include <sdurws/ird/dynamics/Envelope.hpp>

#include "CanonicalDigest.hpp" // 域内私有摘要流写入器（WP-17-T07 抽取——
                               //   与序列摘要同一套编码工具）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
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
    //      插值——缺口区间零贡献，缺口时长仍保留在有效行全跨度分母中）----
    double firstT = 0.0;             // 有效行首时刻，s
    double lastT = 0.0;              // 有效行末时刻，s
    double prevTau = 0.0;            // 前一有效行 τ_total²（梯形累加状态）
    bool havePrev = false;           // 梯形状态就绪位
    bool gapSincePrevOk = false;     // 前一 Ok 行之后是否出现过本关节非 Ok 行
                                     //  （缺口证据——缺口区间零贡献标记）
    double weightedSum = 0.0;        // ∫τ²dt 的分段累加（单位 量纲²·s）
    for (const DynamicsSample& r : series.samples) {
        if (r.jointIndex != jointIndex) {
            continue;  // 非本关节行——与本关节统计无关（其他关节的非 Ok 行
                       //  不构成本关节的缺口证据：多关节行按 (t,jointIndex)
                       //  交错是正常采样形态，不是缺口）
        }
        if (r.numericState != SampleNumericState::Ok) {
            // 本关节缺口行（NFR-COR-03）：不进统计，但作为"缺口证据"标记
            // ——下一 Ok 行与上一 Ok 行之间的区间不得再按梯形插值补值。
            gapSincePrevOk = true;
            continue;
        }
        const double tau2 = r.tauTotal * r.tauTotal;  // v_i(t)²——τ_total 为统计量（§7.3）
        if (!havePrev) {
            // 首个有效行＝积分下界 t₀（驻留段在序列头部同样进入边界；
            //  下界之前的缺口区间本就不在积分域内——清除证据标记）。
            firstT = r.t;
            prevTau = tau2;
            havePrev = true;
            gapSincePrevOk = false;
        } else if (gapSincePrevOk) {
            // 缺口区间零贡献（公共头 computeRms 契约原文"缺口区间不插值，
            // 无数据贡献 0"——audit F-589 修复：原实现按
            // ½(τ_k²＋τ_{k−1}²)·Δt 跨缺口梯形插值，等于假设 τ 在无数据
            // 区间线性过渡，与登记口径相悖且高估 RMS）。分母 T_cycle 仍取
            // 有效行全跨度——缺口时长保留在分母中，RMS 保守低估（完整
            // 循环统计以 Complete 序列为准）。
            prevTau = tau2;
            gapSincePrevOk = false;
        } else {
            // 正常采样区间梯形时间加权：½(τ_k²＋τ_{k−1}²)·Δt——相邻有效
            // 行对（中间无本关节缺口行）。
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

// =====================================================================
// mergeEnvelope（公共头契约的实现——多工况包络合并，DYN-07）。
//
// ★ 包络 canonical 摘要编码规则（DynamicsEnvelope.contentIdentity 的
//   唯一实现点——消费方不得另写第二套编码；工具＝canonical_detail::
//   DigestWriter，与序列摘要同一套显式小端编码）：
//   1. 域分隔 magic kEnvelopeContentMagic（"IRDDYVE1"，8 字节 ASCII）
//      起头——防与序列（IRDDYNS1）/证据（IRDDYEV1）摘要混同；
//   2. 字段序＝DynamicsEnvelope 原文字段序：conditionCount→
//      coversAllMandatory→逐关节行（jointIndex 升序；行内＝jointIndex→
//      jointObjectId→五峰值记录槽〔§7.2 token 序：力矩正/反、速度、
//      加速度、功率幅值——token 4/5 并入 powerPeak 单记录位〕→rmsTau→
//      contributingConditions〔升序、长度前缀〕→envelopeComplete）——
//      与 DynTypes.hpp 逐字段对应，review 可对账；
//   3. 标量编码显式小端（同序列编码规则 3）；NaN 位模式在同一二进制内
//      确定（rmsTau 无效＝quiet_NaN 单点产出——同输入同摘要）；
//   4. contributingConditions 编码前已按 ObjectId 字典序升序（构造序
//      即编码序——清单等价于集合：同集合同摘要）。
// =====================================================================

DynamicsEnvelope DynamicsEnvelopeCalculator::mergeEnvelope(
    const std::vector<OperatingConditionResult>& results,
    const evidence::RequiredCaseSet& coverage) const
{
    // ---- 第 1 步：入参契约校验（调用方错误 fail-fast——§10.0；DynamicsError
    //      不发稳定码：装配错误应修复调用而不是重试）----
    // 1a. 逐工况：身份恒同＋validity 透传恒同＋无重复 conditionId。
    //     DynamicsValidity 未定义 operator==（纯聚合、无比较契约）——逐
    //     字段比较（completeness/计数/标记/forwardCheck 全字段）。
    auto validityEqual = [](const DynamicsValidity& a, const DynamicsValidity& b) {
        return a.completeness == b.completeness
            && a.plannedSampleCount == b.plannedSampleCount
            && a.actualSampleCount == b.actualSampleCount
            && a.nonFiniteCount == b.nonFiniteCount
            && a.estimatedLinkCount == b.estimatedLinkCount
            && a.estimatedPayloadCount == b.estimatedPayloadCount
            && a.frictionMissing == b.frictionMissing
            && a.externalValidationPending == b.externalValidationPending
            && a.forwardCheck == b.forwardCheck;
    };
    for (std::size_t r = 0; r < results.size(); ++r) {
        const OperatingConditionResult& res = results[r];
        if (!res.conditionId.isValid()) {
            throw DynamicsError("input-invalid",
                                "包络合并入参：第 " + std::to_string(r)
                                    + " 个工况结果 conditionId 为空——拒绝合并（§4.4 身份契约）");
        }
        if (!(res.conditionId == res.series.conditionId)) {
            throw DynamicsError("input-invalid",
                                "包络合并入参：第 " + std::to_string(r)
                                    + " 个工况结果 conditionId 与 series.conditionId 不一致——"
                                    "拒绝合并（§4.4 恒同契约）");
        }
        if (!validityEqual(res.validity, res.series.validity)) {
            throw DynamicsError("input-invalid",
                                "包络合并入参：第 " + std::to_string(r)
                                    + " 个工况结果 validity 与 series.validity 不一致——"
                                    "拒绝合并（§4.4 透传契约）");
        }
        for (std::size_t q = 0; q < r; ++q) {
            if (res.conditionId == results[q].conditionId) {
                throw DynamicsError("input-invalid",
                                    "包络合并入参：工况 conditionId 重复（第 " + std::to_string(q)
                                        + " 与第 " + std::to_string(r) + " 条）——"
                                        "同一工况两次入合并属装配歧义，拒绝");
            }
        }
    }
    // 1b. 覆盖分母：coverage 条目 caseId 非空且不重复（evidence §4.1.3
    //     "caseId 须 isValid——保留值拒绝"同口径＋冻结集唯一性的防御面）。
    for (std::size_t e = 0; e < coverage.entries.size(); ++e) {
        if (!coverage.entries[e].caseId.isValid()) {
            throw DynamicsError("input-invalid",
                                "包络合并入参：覆盖分母 RequiredCaseSet 第 " + std::to_string(e)
                                    + " 条 caseId 为空——拒绝合并（evidence §4.1.3）");
        }
        for (std::size_t f = 0; f < e; ++f) {
            if (coverage.entries[e].caseId == coverage.entries[f].caseId) {
                throw DynamicsError("input-invalid",
                                    "包络合并入参：覆盖分母 RequiredCaseSet caseId 重复（第 "
                                        + std::to_string(f) + " 与第 " + std::to_string(e)
                                        + " 条）——冻结集唯一性违约，拒绝");
            }
        }
    }

    // ---- 第 2 步：工况处理序＝conditionId 字典序升序（NFR-COR-02——
    //      输出与入参顺序无关；并列峰值的"取更小 conditionId"由此成立）----
    std::vector<std::size_t> order(results.size());
    for (std::size_t k = 0; k < order.size(); ++k) { order[k] = k; }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return results[a].conditionId < results[b].conditionId;
    });

    // ---- 第 3 步：逐工况装配一致性校验＋关节结构解析（合并的对齐面）----
    //      peaks 必须逐位等于 computePeaks(series)（§10.4 峰值统计口径唯一
    //      实现点产出——DynTypes.hpp 对 OperatingConditionResult.peaks 的
    //      生产路径契约）；关节结构（含 Ok 行关节升序表＋jointObjectId）
    //      从序列解析——peaks 行无 jointIndex 字段（卡面设计基线），行组
    //      与关节的对齐只能经此结构（每关节恒 kPeaksPerJoint 行）。
    struct ConditionStructure {
        std::vector<std::uint32_t> jointIds;        ///< 含 Ok 行的关节（升序）
        std::vector<core::ObjectId> jointObjectIds; ///< 与 jointIds 平行的关节对象 ID
        std::vector<PeakRecord> peaks;              ///< 复算峰值（与 result.peaks 恒同）
    };
    std::vector<ConditionStructure> structures(results.size());
    for (const std::size_t r : order) {
        const OperatingConditionResult& res = results[r];
        ConditionStructure& st = structures[r];

        // 3a. 复算峰值并逐位比对（装配不一致＝调用方错误——合成峰值与
        //     序列不同源会使包络失去可追溯性，拒绝而非择一采信）。
        st.peaks = computePeaks(res.series);
        const bool assembledMatches = (res.peaks.size() == st.peaks.size())
            && std::equal(st.peaks.begin(), st.peaks.end(), res.peaks.begin(),
                          [](const PeakRecord& a, const PeakRecord& b) {
                              return a.value == b.value && a.tPeakS == b.tPeakS
                                  && a.segmentIndex == b.segmentIndex
                                  && a.windowStartS == b.windowStartS
                                  && a.windowEndS == b.windowEndS
                                  && a.conditionId == b.conditionId;
                          });
        if (!assembledMatches) {
            throw DynamicsError("input-invalid",
                                "包络合并入参：工况 results[" + std::to_string(r)
                                    + "].peaks 与 computePeaks(series) 不一致（实得 " + std::to_string(res.peaks.size())
                                    + " 行，复算 " + std::to_string(st.peaks.size())
                                    + " 行或逐位不等）——峰值装配必须产自统计口径唯一实现点"
                                    "（§10.4），拒绝合并");
        }

        // 3b. 关节结构解析：Ok 行关节升序＋jointObjectId 一致性（同关节
        //     行的 jointObjectId 恒同——跨行不一致属输入模型矛盾，拒绝）。
        for (const DynamicsSample& row : res.series.samples) {
            if (row.numericState != SampleNumericState::Ok) {
                continue;  // 非 Ok 行不进关节结构（与 computePeaks 分组同规）
            }
            if (st.jointIds.empty() || st.jointIds.back() != row.jointIndex) {
                // 序列行序 (t, jointIndex) 保证同关节行连续出现（构建器产
                // 物）——但合并是防御面：不依赖连续性，升序查重插入。
                auto it = std::lower_bound(st.jointIds.begin(), st.jointIds.end(),
                                           row.jointIndex);
                if (it != st.jointIds.end() && *it == row.jointIndex) {
                    const core::ObjectId& known =
                        st.jointObjectIds[static_cast<std::size_t>(it - st.jointIds.begin())];
                    if (!(known == row.jointObjectId)) {
                        throw DynamicsError("input-invalid",
                                            "包络合并入参：工况 results[" + std::to_string(r)
                                                + "] 同一关节 index=" + std::to_string(row.jointIndex)
                                                + " 携带不同 jointObjectId——样本行身份矛盾，拒绝");
                    }
                    continue;  // 已登记关节——结构无变化
                }
                // 新关节登记：先算平行插入位（it 失效前），再同步插入两表
                // （jointIds 与 jointObjectIds 同位平行——computePeaks 分组
                // 同款纪律）。
                const auto pos = it - st.jointIds.begin();
                st.jointIds.insert(it, row.jointIndex);
                st.jointObjectIds.insert(st.jointObjectIds.begin() + pos, row.jointObjectId);
            }
        }
    }

    // ---- 第 4 步：逐关节跨工况合并（§7.4——逐量纲 max、来源工况集、
    //      完整性传播）----
    //      累加器按关节升序维护（与 computePeaks 分组同款的有序插入——
    //      输出行序＝jointIndex 升序，§7.6 全序）。
    struct JointAccumulator {
        std::uint32_t jointIndex{};              ///< 关节序号（0 基）
        core::ObjectId jointObjectId;            ///< 关节对象 ID（跨工况一致——第 3 步已校验）
        PeakRecord tauMaxPositive;               ///< token 0 跨工况 max
        PeakRecord tauMaxNegative;               ///< token 1 跨工况 max
        PeakRecord velocityPeak;                 ///< token 2 跨工况 max
        PeakRecord accelerationPeak;             ///< token 3 跨工况 max
        PeakRecord powerPeak;                    ///< token 4/5 合并幅值跨工况 max
        double rmsTau = 0.0;                     ///< 跨工况 max（NaN 不参与）
        bool rmsValid = false;                   ///< 尚无有效 RMS（全无效→NaN）
        std::vector<core::ObjectId> contributing; ///< 来源工况（升序追加——处理序
                                                  ///<   已按 conditionId 升序）
        bool allComplete = true;                 ///< 全部来源 Complete（Partial/
                                                 ///<   Failed 即 false——§7.4）
    };
    std::vector<JointAccumulator> acc;           ///< 关节累加器（jointIndex 升序）

    // 峰值槽位严格大于替换（并列保留先到者——处理序 conditionId 升序，
    // 即并列取 conditionId 更小工况的记录；NaN 不大于任何值永不替换，
    // 与 computePeaks 同语义）。
    auto offer = [](PeakRecord& slot, const PeakRecord& cand) {
        if (cand.value > slot.value) { slot = cand; }
    };

    for (const std::size_t r : order) {
        const OperatingConditionResult& res = results[r];
        const ConditionStructure& st = structures[r];
        const bool conditionComplete =
            res.validity.completeness == DynamicsValidity::Completeness::Complete;

        for (std::size_t g = 0; g < st.jointIds.size(); ++g) {
            const std::uint32_t j = st.jointIds[g];
            // 定位/创建该关节累加器（acc 升序——lower_bound 插入）。
            auto it = std::lower_bound(acc.begin(), acc.end(), j,
                                       [](const JointAccumulator& a, std::uint32_t key) {
                                           return a.jointIndex < key;
                                       });
            if (it == acc.end() || it->jointIndex != j) {
                JointAccumulator fresh;
                fresh.jointIndex = j;
                fresh.jointObjectId = st.jointObjectIds[g];
                // 首个贡献工况的六行直接落槽（后续工况严格大于才替换——
                // 首槽值即比较基线；conditionId 升序保证并列取最小）。
                const std::size_t base = g * kPeaksPerJoint;
                fresh.tauMaxPositive = st.peaks[base + kTauPositive];
                fresh.tauMaxNegative = st.peaks[base + kTauNegative];
                fresh.velocityPeak = st.peaks[base + kVelocity];
                fresh.accelerationPeak = st.peaks[base + kAcceleration];
                // 功率单记录位＝合并幅值（§4.4"含正负"——逐工况取
                // max(max(P), max(−P))；胜者值恒为非负幅值：max(P) 只有在
                // ≥max(−P)≥0 时才胜出，见公共头 mergeEnvelope 契约注）。
                const PeakRecord& pPos = st.peaks[base + kPowerPositive];
                const PeakRecord& pNeg = st.peaks[base + kPowerNegative];
                fresh.powerPeak = (pPos.value >= pNeg.value) ? pPos : pNeg;
                it = acc.insert(it, fresh);
            } else {
                // 后续贡献工况：逐 token 严格大于替换（值/时间/段/窗/
                // 来源工况随胜者整体携带——PeakRecord 值语义）。
                const std::size_t base = g * kPeaksPerJoint;
                offer(it->tauMaxPositive, st.peaks[base + kTauPositive]);
                offer(it->tauMaxNegative, st.peaks[base + kTauNegative]);
                offer(it->velocityPeak, st.peaks[base + kVelocity]);
                offer(it->accelerationPeak, st.peaks[base + kAcceleration]);
                const PeakRecord& pPos = st.peaks[base + kPowerPositive];
                const PeakRecord& pNeg = st.peaks[base + kPowerNegative];
                offer(it->powerPeak, (pPos.value >= pNeg.value) ? pPos : pNeg);
            }

            // RMS 跨工况 max（包络＝最坏工况呈现——见 DynTypes.hpp
            // rmsTau 注：RMS 不可跨工况时间加权合并，max 是无新语义的
            // 唯一合并；NaN＝显式无效，不参与 max 也不清零已有值）。
            const double rms = computeRms(res.series, j);
            if (!std::isnan(rms) && (!it->rmsValid || rms > it->rmsTau)) {
                it->rmsTau = rms;
                it->rmsValid = true;
            }

            // 来源工况集（处理序升序追加＝ObjectId 字典序——稳定排序）
            // ＋完整性传播（任一来源非 Complete→false，§7.4）。
            it->contributing.push_back(res.conditionId);
            it->allComplete = it->allComplete && conditionComplete;
        }
    }

    // ---- 第 5 步：包络组装（Empty 语义：无任何关节行＝"包络不产出"，
    //      绝不 0 值伪装——§7.4/§7.6；身份字段照算，包络仍可寻址）----
    DynamicsEnvelope envelope;
    envelope.conditionCount = results.size();  // 参与合并的工况数（含未产出
                                               //   统计行的工况——计数如实）
    envelope.joints.reserve(acc.size());
    for (JointAccumulator& a : acc) {
        DynamicsEnvelope::JointEnvelope row;
        row.jointIndex = a.jointIndex;
        row.jointObjectId = a.jointObjectId;
        row.tauMaxPositive = a.tauMaxPositive;
        row.tauMaxNegative = a.tauMaxNegative;
        row.velocityPeak = a.velocityPeak;
        row.accelerationPeak = a.accelerationPeak;
        row.powerPeak = a.powerPeak;
        // 全部来源 RMS 无效→NaN（显式无效——不伪造 0，§7.3）；rmsValid
        // 为 true 时 rmsTau 已持最大有效值。
        row.rmsTau = a.rmsValid ? a.rmsTau : std::numeric_limits<double>::quiet_NaN();
        row.contributingConditions = std::move(a.contributing);
        row.envelopeComplete = a.allComplete;
        envelope.joints.push_back(std::move(row));
    }

    // ---- 第 6 步：coversAllMandatory（EVI-02 呈现参考——覆盖判定归
    //      evidence 覆盖矩阵，本布尔不替代：分母＝冻结 RequiredCaseSet 的
    //      enabled∧mandatory 条目；分子＝产出了样本〔completeness≠Empty〕
    //      的工况集；空分母→true＝平凡完备〔P-EV-7〕）----
    {
        // 分子：有样本产出的工况集（Empty 完整度的结果未产出任何统计——
        // 计入会虚报覆盖；保守不计，缺项方向只会低报不会高报）。
        std::vector<core::ObjectId> covered;
        covered.reserve(results.size());
        for (const OperatingConditionResult& res : results) {
            if (res.validity.completeness != DynamicsValidity::Completeness::Empty) {
                covered.push_back(res.conditionId);
            }
        }
        std::sort(covered.begin(), covered.end());
        envelope.coversAllMandatory = true;   // 空分母平凡完备（逐条要求 vacuous）
        for (const evidence::CaseEntry& entry : coverage.entries) {
            if (!entry.enabled || !entry.mandatory) {
                continue;  // 非必验/未启用工况不在覆盖义务内（§8.1 矩阵：
                          //   disabled→NotApplicable 不参与）
            }
            if (!std::binary_search(covered.begin(), covered.end(), entry.caseId)) {
                envelope.coversAllMandatory = false;  // 漏验如实呈现（V-25）
            }
        }
    }

    // ---- 第 7 步：canonical 内容身份（编码规则见本实现段头注；同输入
    //      必得同摘要——NFR-COR-02）----
    {
        core::ContentDigester d;
        canonical_detail::DigestWriter w(d);
        w.magic(kEnvelopeContentMagic, 8);    // 域分隔 magic（包络 v1）
        w.u64(envelope.conditionCount);
        w.u8(envelope.coversAllMandatory ? 1u : 0u);
        w.u64(envelope.joints.size());
        for (const DynamicsEnvelope::JointEnvelope& j : envelope.joints) {
            w.u32(j.jointIndex);
            w.id16(j.jointObjectId);
            // 五个峰值记录槽按 §7.2 token 序编码（力矩正/反、速度、加速度、
            // 功率幅值——token 4/5 已并入 powerPeak 单记录位〔§4.4 包络
            // 结构仅 5 个 PeakRecord 槽〕；行内＝PeakRecord 原文字段序：
            // 值/发生时间/段/窗起点/窗终点/来源工况）。
            const PeakRecord* slot[kPeaksPerJoint - 1] = {&j.tauMaxPositive, &j.tauMaxNegative,
                                                          &j.velocityPeak, &j.accelerationPeak,
                                                          &j.powerPeak};
            for (const PeakRecord* p : slot) {
                w.f64(p->value);
                w.f64(p->tPeakS);
                w.u32(p->segmentIndex);
                w.f64(p->windowStartS);
                w.f64(p->windowEndS);
                w.id16(p->conditionId);
            }
            w.f64(j.rmsTau);
            w.u32(static_cast<std::uint32_t>(j.contributingConditions.size()));
            for (const core::ObjectId& cid : j.contributingConditions) { w.id16(cid); }
            w.u8(j.envelopeComplete ? 1u : 0u);
        }
        envelope.contentIdentity.bytes = d.finalize();
    }

    return envelope;
}

}  // namespace sdurws::ird::dynamics
