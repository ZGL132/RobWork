/**
 * @file   Coverage.hpp
 * @brief  覆盖率计算值类型（KIN-04）——CoverageTotals/CoverageResult
 *         （分母/分子整数计数＋defined/降级/不完整标记）与区域覆盖
 *         canonical 载荷编码声明。
 *
 * 设计依据：
 *   - units/kinematics.md §3.3（公共头布局表 Coverage.hpp 行——"覆盖率
 *     计算值类型（分母/分子/降级标记）"，任务 T06）、§7.2（覆盖率图——
 *     "位置覆盖率＝|Reached 位置样本|÷计划位置样本总数（存在性口径）、
 *     姿态覆盖率＝|Reached (位置×姿态) 样本|÷计划姿态样本总数（全局
 *     口径）；数据不足样本保留在分母、不计入分子、单独计数并列入报告
 *     →结论整体降级 DataInsufficient；零样本→覆盖率不定义；取消/崩溃
 *     →partial→不产正式覆盖率"）
 *   - REQUIREMENTS KIN-04（R3/R8 口径原文——存在数据不足样本时覆盖率
 *     数值仍可计算（作参考值）但结论整体降级；零样本判 DataInsufficient
 *     并给诊断，不得输出 0% 或 100%；分母＝计划样本总数禁止按评估结果
 *     剔除）、KIN-05（缺碰撞检测器→数据不足）
 *   - evidence 卡 §6.6（RegionCoverageEvidence 校验面——本单元只产素材
 *     不裁定；downgraded 单向强制：dataInsufficient>0 ⇒ true）
 *   - 任务契约 tasks/foundation/WP-15-T06.json acceptance 1/2/4
 *
 * 背景说明（为什么本头没有比率/百分比字段——"绝不输出 0% 或 100%"的
 * 结构保证）：覆盖率数值＝分子/分母的整数计数（KIN-04 R8"整数计数无
 * 浮点容差"），比率推导是报告/汇总层的投影职责（evidence/RPT 域）——
 * 本值类型只承载计数与标记，比率字段在类型面上不存在，零样本路径
 * "绝不输出 0% 或 100%"由结构保证而非运行时判断。V-14 观测点
 * "分子分母计数"即本类型的字段面。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态）；确定性：
 * 同输入同字节（载荷编码定宽小端＋字段定序——NFR-COR-01/02）。
 */

#ifndef IRD_KINEMATICS_COVERAGE_HPP
#define IRD_KINEMATICS_COVERAGE_HPP

#include <cstdint>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>   // ContentIdentity（载荷摘要/计划身份）
#include <sdurws/ird/core/Identity.hpp> // ObjectId/TaskIdentity（绑定面）
#include <sdurws/ird/core/Evaluation.hpp>  // EvaluationMode（模式词表）

namespace sdurws::ird::kinematics {

// =====================================================================
// CoverageTotals——单一口径（位置/姿态）的计数面
// =====================================================================

/**
 * @brief 单一覆盖率口径的计数面（KIN-04 R8——分母＝计划样本总数，禁止
 *        按评估结果剔除样本；全部成员为整数计数，无量纲）。
 *
 * 守恒式（computeCoverage 保证，违例 logic_error）：planned ＝ reached
 * ＋ unreachable ＋ dataInsufficient ＋ notRun ＋ notApplicable。
 * 值语义纯结构；线程安全（并发只读）。
 */
struct CoverageTotals {
    /// 分母＝计划样本总数（SampleSet 分母计数的口径面——不可达与数据
    /// 不足样本一律保留，R8）。
    std::uint64_t planned = 0;
    /// 分子＝Reached 样本数（存在性/全局口径按所属轴解释）。
    std::uint64_t reached = 0;
    /// Unreachable 样本数（解析界限确定性证明——结局 5 素材承载）。
    std::uint64_t unreachable = 0;
    /// DataInsufficient 样本数（搜索未果/缺碰撞检测器——保留分母不计
    /// 分子、单独计数列入报告；>0 ⇒ 整体降级）。
    std::uint64_t dataInsufficient = 0;
    /// NotRun 样本数（取消/崩溃未派发——partial；>0 ⇒ 不产正式覆盖率）。
    std::uint64_t notRun = 0;
    /// NotApplicable 样本数（词表完备性保留——生成面不产，恒 0）。
    std::uint64_t notApplicable = 0;

    bool operator==(const CoverageTotals& o) const
    {
        return planned == o.planned && reached == o.reached
            && unreachable == o.unreachable && dataInsufficient == o.dataInsufficient
            && notRun == o.notRun && notApplicable == o.notApplicable;
    }
    bool operator!=(const CoverageTotals& o) const { return !(*this == o); }
};

// =====================================================================
// CoverageResult——覆盖率计算结果（双口径＋标记面）
// =====================================================================

/**
 * @brief 区域覆盖率计算结果（§7.2 覆盖率图的产出值——双口径分别统计＋
 *        defined/降级/不完整三标记；**无比率字段**——文件头注）。
 *
 * 语义（KIN-04 R3/R8 原文的值面落点；登记随卡 §14.6 v0.6）：
 *   - position（存在性口径）与 orientation（全局口径）分别统计——两轴
 *     的分子/分母互不混合（V-14"位置/姿态分别统计"）；
 *   - positionDefined/orientationDefined ＝ 对应分母 >0（零样本轴的
 *     覆盖率**不定义**——该轴既非 0% 也非 100%，比率不存在；全轴不定义
 *     即 V-13 零样本场景）；
 *   - downgraded（整体降级标记）：任一样本 DataInsufficient（任一轴）
 *     或任一轴零样本 → true——覆盖率计数仍可计算（作参考值），但正式
 *     通过结论被降级拦截（判定归 evidence 汇总，本标记是素材面）；
 *   - incomplete：NotRun>0——partial 产物，不产正式覆盖率（重跑同一
 *     冻结样本集，§7.2；调用侧据此不产 Completed envelope）。
 *
 * 值语义纯结构；线程安全（并发只读）。确定性：同输入同值（NFR-COR-01）。
 */
struct CoverageResult {
    /// 位置覆盖率计数（存在性口径——∃≥1 有效解即达）。
    CoverageTotals position;
    /// 姿态覆盖率计数（全局口径——(位置×姿态) 逐组合计数）。
    CoverageTotals orientation;
    /// 位置覆盖率已定义（分母>0；false＝位置轴零样本——比率不存在）。
    bool positionDefined = false;
    /// 姿态覆盖率已定义（分母>0；false＝姿态轴零样本——比率不存在）。
    bool orientationDefined = false;
    /// 整体降级标记（存在数据不足样本或任一轴零样本——DataInsufficient
    /// 素材面；判定归 evidence）。
    bool downgraded = false;
    /// 不完整标记（NotRun>0——partial 不产正式覆盖率；§7.2）。
    bool incomplete = false;

    bool operator==(const CoverageResult& o) const
    {
        return position == o.position && orientation == o.orientation
            && positionDefined == o.positionDefined
            && orientationDefined == o.orientationDefined
            && downgraded == o.downgraded && incomplete == o.incomplete;
    }
    bool operator!=(const CoverageResult& o) const { return !(*this == o); }
};

// =====================================================================
// 区域覆盖 canonical 载荷编码（§5.6 结果绑定＋§7.2 覆盖证据的字节面）
// =====================================================================

/**
 * @brief 将区域覆盖计算结果编码为域 canonical 字节（定宽小端＋字段
 *        定序；声明在 Coverage.hpp、实现落位 src/Sampling.cpp——采样
 *        通道单一翻译单元，与 T05 载荷编码随值面 TU 的布局同款）。
 *
 * 编码布局（codec 版本 1；magic "IRDCV01"＋版本 u32）：标记块
 * （incomplete u8＋downgraded u8＋positionDefined u8＋orientationDefined
 * u8）＋绑定块（snapshotId/sliceId/configDigest 各 32B＋mode u8＋seed
 * u64＋referenceQ u32＋f64×n＋任务五元组 4×16B＋attempt u64＋
 * evaluationKey u32＋len＋contractVersion u32——§5.6 绑定六要素）＋
 * 计划块 u32×{regionObjectId 16B＋planContentIdentity 32B＋
 * sampleSetIdentity 32B＋identityMatched u8＋plannedPositionSamples u64
 * ＋plannedPoseSamples u64}＋覆盖计数块（位置侧六计数 u64×6＋姿态侧
 * 六计数 u64×6——全集合计）＋逐样本状态表 u32×{sampleIndex u64＋
 * regionObjectId 16B＋kind u8＋state u8＋outcomeKind present u8[+u8]＋
 * collisionNotEvaluated u8＋reason u32＋len}。样本几何（位置/位姿坐标）
 * 不入载荷——样本由 (plan, budget, seed) 确定性再生（D-KIN-6），身份
 * 对账面已绑定其集合；大样本集不枚举几何与 evidence §4.1.4"身份对计划
 * 参数计算"同源。
 *
 * 采样计划几何/采样定义原文不入本载荷（归 requirements 计划对象——
 * planContentIdentity 溯源）。
 *
 * @param computation [in] 区域覆盖计算结果（冻结值——绑定块/计划块/
 *                     状态表/覆盖标记的全部来源）
 * @param task        [in] 任务五元组（绑定面——computation.task 同值，
 *                     显式入参与 T04/T05 编码器同构）
 * @return canonical 字节（确定性：同输入同字节——NFR-COR-01）
 *
 * 纯函数；线程安全；不抛（f64 位模式直写）。
 */
std::vector<std::uint8_t> encodeRegionCoveragePayloadCanonical(
    const struct RegionCoverageComputation& computation, const core::TaskIdentity& task);

}  // namespace sdurws::ird::kinematics

#endif  // IRD_KINEMATICS_COVERAGE_HPP
