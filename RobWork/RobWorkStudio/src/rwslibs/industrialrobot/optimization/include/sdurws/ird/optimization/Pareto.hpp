/**
 * @file   Pareto.hpp
 * @brief  Pareto 非支配集构建（OPT-04/O9）——支配判定（严格序/容差支配）、
 *         非支配分层、重复候选去重与稳定排序输出（rank→目标序→CandidateId
 *         终键，与线程数无关）。任务 WP-20-T05。
 *
 * 设计依据：
 *   - units/optimization.md §7.4（Pareto 非支配筛选——支配定义两式：严格序
 *     默认零容差浮点全序、容差支配经 core::closeWithin 逐元素；缺失指标候选
 *     不参与支配比较；相同指标向量互不支配；重复 CandidateId 去重为单候选
 *     保留首次＋dedupCount；"非支配 ≠ 工程通过"；可行集/非支配集双标记；
 *     稳定排序三键）、§12.2（IParetoFrontBuilder 签名与 @pre——输入候选均
 *     Feasible 且激活指标完整，"—"候选已在管线侧排除）、§12.3（buildFront
 *     const 只读可并发；确定性纯函数面）、DOPT-5（支配比较默认零容差——
 *     不发明阈值，P-OPT-5 登记默认值待裁决）、DOPT-8（输出序三键全序且与
 *     线程无关）、OPT-VER-131/132/133（稳定排序/容差支配/去重用例）
 *   - 需求 OPT-04（输出 Pareto 非支配候选，**不以单一加权总分代替工程取舍**
 *     ——需求附录 B 裁决排除：本头无任何权重/评分标量，多目标取舍只经
 *     支配关系）、AT-09（集合/排序满足容差支配）、NFR-COR-02（确定性——
 *     同输入同输出，与线程数无关）
 *   - 任务契约 tasks/foundation/WP-20-T05.json acceptance 1/3（容差支配
 *     显式配置进 config.opt、默认零容差；禁止单一加权总分；稳定排序输出
 *     与线程数无关）
 *
 * 背景说明（第一读者须知——四件事）：
 *   ① **支配判定与排序键分层**（AT-09"集合/排序满足容差支配"的准确语义）：
 *      容差只作用于支配判定（布尔——决定候选落入哪个非支配层）；**同层内
 *      排序键用严格浮点比较**（目标值声明序字典序＋CandidateId 终键）。为
 *      什么排序键不吃容差：容差近似相等不是传递关系（a≈b 且 b≈c 可能 a<c），
 *      混入排序键会破坏全序与确定性——卡 §7.4"稳定排序（输出顺序确定性，
 *      NFR-COR-02）"要求的全序只有严格比较能保证。
 *   ② **分层（rank）语义**：非支配 rank＝NSGA 式逐层筛选（rank 0＝第一
 *      非支配前沿——不被输入集中任何候选支配；rank k＝仅被 rank<k 层支配
 *      的候选中互不支配者）。"非支配集"＝rank 0 子集（paretoNondominated
 *      标记）；输出**保留全体可行候选**（每候选带 rank——可行集/非支配集
 *      双标记显式呈现，§7.4；Pareto 非支配仅表示多目标取舍，工程通过判定
 *      唯一归 evidence 五条件——EVI-01，本头零工程判定）。
 *   ③ **与线程数无关的实现承载**：buildFront 是纯单线程纯函数（无共享
 *      状态、无并行归约）——同输入无论调用环境线程配置如何，输出逐字段
 *      相等（NFR-COR-02/OPT-VER-131"与线程无关"的最强实现形态；R1
 *      threadCount 恒 1，R2 并行面的等价集合归并仍以本函数为确定性核）。
 *   ④ **候选缺失指标不进本面**（§7.4/"—""不参与 Pareto"的实现位置）：
 *      激活目标槽位缺失的候选在管线侧判 DataInsufficient（候选级），根本
 *      不会出现在 buildFront 输入——本面以 @pre 防线双保险：输入中激活
 *      目标槽位为 nullopt 即 OPT-INPUT-INVALID（acceptance 2"不参与
 *      Pareto"的执行证明点，测试钉住）。
 *
 * 线程约束：FeasibleCandidate/ParetoFrontEntry/ParetoFrontResult 全部纯值
 *   （可并发拷贝）；dominates/buildFront 纯函数可重入。
 * 确定性：去重保首见序、分层按剩余集输入序扫描、排序比较器全序无巧合平
 *   局（CandidateId 字节序终键兜底）——同输入同输出（NFR-COR-02）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_PARETO_HPP
#define SDURWS_IRD_OPTIMIZATION_PARETO_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <sdurws/ird/optimization/CandidatePatch.hpp>  // CandidateId——去重与
                                                       //  排序终键（字节字典序）
#include <sdurws/ird/optimization/Objective.hpp>       // ObjectiveSet/ObjectiveEntry
                                                       //  ——激活目标与支配容差
#include <sdurws/ird/optimization/Types.hpp>           // MetricId/OptimizationError

namespace sdurws::ird::optimization {

// =====================================================================
// 输入/输出数据模型
// =====================================================================

/**
 * @brief 完整可行候选（buildFront 输入条目——可行集成员）。
 *
 * **类型即契约**：可行集＝"完整可行候选全集"（§7.4）——Infeasible/
 * DataInsufficient/EvaluationFailed 候选在管线侧（§7.5 判定流）已被排除，
 * 本类型是它们到达 Pareto 面的类型学屏障（AT-09"不可行候选不进入可行集"
 * 的承载面之一；管线侧排除用例随 WP-20-T04 OPT-VER-109/115/118 已钉）。
 *
 * metricValues 槽位＝MetricId 枚举序八槽（kMetricCount——与
 * StaticMetricResult::metrics 同序；值 SI，单位随指标词表）；nullopt＝
 * not-computable（非激活槽位允许——StageB 五项 D-only 息"—"是常态；
 * **激活目标槽位必须有值**——buildFront 校验，见文件头注④）。
 *
 * 值语义；线程安全：纯值。
 */
struct FeasibleCandidate {
    CandidateId candidateId = {};  ///< 候选身份（去重锚＋排序终键）
    bool isBaseline = false;       ///< 基线候选标记（§8.2——基线参与 Pareto，
                                   ///  作为可行方案之一）
    /// 逐指标值（MetricId 枚举序八槽；nullopt＝not-computable——激活目标
    /// 槽位必须有值且有限，非激活槽位自由）
    std::vector<std::optional<double>> metricValues = {};
};

/**
 * @brief 非支配分层输出条目（每可行候选一条——全体保留，双标记呈现）。
 *
 * 值语义；线程安全：纯值。
 */
struct ParetoFrontEntry {
    CandidateId candidateId = {};    ///< 候选身份
    bool isBaseline = false;         ///< 基线候选标记（透传）
    std::size_t nondominationRank = 0; ///< 非支配 rank（0 基——rank 0＝第一
                                     ///  非支配前沿；逐层筛选语义见文件头注②）
    bool paretoNondominated = false; ///< 非支配集成员标记（rank==0——
                                     ///  "pareto-nondominated"位；与"feasible"
                                     ///  位〔输入类型承载〕构成 §7.4 双标记）
};

/**
 * @brief Pareto 筛选结果（可行集 vs 非支配集显式双集合＋稳定排序全列）。
 *
 * 值语义；线程安全：纯值。
 */
struct ParetoFrontResult {
    /// 全体可行候选（去重后）——**稳定排序序**（rank 升序 → 激活目标值
    /// 声明序严格字典序〔按方向〕→ CandidateId 字典序终键；§7.4/DOPT-8）
    std::vector<ParetoFrontEntry> entries = {};
    /// 非支配集成员身份（rank==0 子集——保持 entries 序；"非支配集"显式面）
    std::vector<CandidateId> nondominatedIds = {};
    /// 可行集成员身份（去重后全部——保持 entries 序；"可行集"显式面，
    /// 与非支配集的区别在导出与 UI 均显式呈现——§7.4）
    std::vector<CandidateId> feasibleIds = {};
    std::uint32_t duplicatesDropped = 0; ///< 去重丢弃数（dedupCount——§7.4
                                     ///  "重复 CandidateId → 去重为单候选
                                     ///  （保留首次评估，dedupCount 计数入审计）"）
};

// =====================================================================
// 支配判定（纯函数——测试与上游编排复用的观测点）
// =====================================================================

/**
 * @brief 支配判定（§7.4 定义式——a 是否支配 b；纯函数）。
 *
 * 判定式（激活目标集 O，逐目标带方向 dᵢ 与容差 tᵢ）：
 *   a 支配 b ⇔ ∀i∈O：aᵢ 不劣于 bᵢ（按方向）∧ ∃j∈O：aⱼ 严格优于 bⱼ；
 *   - "不劣于"（min）：aᵢ ≤ bᵢ ∨ core::closeWithin(aᵢ, bᵢ, tᵢ)
 *     （max 对称：aᵢ ≥ bᵢ ∨ closeWithin）——tᵢ 为零容差时 closeWithin
 *     退化为浮点精确相等（aᵢ==bᵢ），即**默认严格序·浮点全序**（DOPT-5：
 *     不发明默认阈值——P-OPT-5 登记默认值待裁决，零容差为安全默认）；
 *   - "严格优于"（min）：aᵢ < bᵢ ∧ !closeWithin(aᵢ, bᵢ, tᵢ)（max 对称）；
 *   - closeWithin 为 C4 通用公式 |value−reference| ≤ ε_rel·|reference|＋
 *     ε_abs（零参考退化 ε_abs——core::Compare 唯一实现，消费不复制）；
 *     参考元锚定**被比较方 b**（卡 §7.4 字面：closeWithin(aᵢ, bᵢ, tᵢ)）。
 *
 * 自洽性（测试钉住）：逐元素近似相等（比较容差内）⇒ 双向"不劣于"成立
 * 且"严格优于"恒假 ⇒ 互不支配（"相同指标向量（逐元素全等，比较容差内）
 * →互不支配"，§7.4）。
 *
 * 前置：两候选的激活目标槽位均有有限值（buildFront 已校验——直接调用
 * 本函数时由调用方保证；违约行为＝比较结果无意义〔非有限值上的比较不抛
 * ——纯函数 noexcept〕）。
 *
 * @param a          [in] 候选 a（潜在支配方）
 * @param b          [in] 候选 b（被比较方；容差参考元锚定点）
 * @param objectives [in] 激活目标集（方向/容差来源；声明序＝逐目标遍历序）
 * @return a 支配 b（全序布尔——a、b 互不支配时双向均 false）
 *
 * 纯函数 noexcept；线程安全（可重入）。
 */
bool dominates(const FeasibleCandidate& a, const FeasibleCandidate& b,
               const ObjectiveSet& objectives) noexcept;

// =====================================================================
// 非支配集构建接口（§12.2 IParetoFrontBuilder——O9 面）
// =====================================================================

/**
 * @brief Pareto 非支配集构建接口（§12.2 原文签名）。
 *
 * @pre 输入候选均 Feasible 且激活指标完整（"—"候选已在管线侧排除——
 *      违反即 OPT-INPUT-INVALID，见文件头注④）。
 * @determinism 同输入同输出（比较容差来自 config，随输入携带——
 *      objectives 中的逐目标容差）；与线程数无关（纯单线程实现——
 *      文件头注③）。
 */
class IParetoFrontBuilder {
public:
    virtual ~IParetoFrontBuilder() = default;

    /**
     * @brief 对完整可行候选构建非支配集与稳定排序（§7.4；去重按
     *        CandidateId）。
     *
     * 执行序（每次调用固定——确定性）：
     *   1. 目标集与输入校验（空目标集/激活目标槽位缺失或非有限→抛，
     *      调用方契约违约 fail-fast——§12.3）；
     *   2. 去重（CandidateId 相同→保留输入序首见，丢弃计数入
     *      duplicatesDropped——§7.4）；
     *   3. 非支配分层（逐层筛选：每轮取剩余集中不被任何同轮候选支配者
     *      为当前层，rank 递增——O(n²) 支配判定，R1 预算 ≤256 候选完全
     *      可行，无性能顾虑）；
     *   4. 稳定排序（rank 升序 → 激活目标值声明序严格字典序〔按方向，
     *      **不含容差**——文件头注①〕→ CandidateId 字典序终键）；
     *   5. 组装结果（entries/双集合/dedupCount）。
     *
     * @param candidates [in] 可行候选集（输入序＝首见序语义的锚；调用方
     *                    持有，调用期间有效）
     * @param objectives [in] 激活目标集（方向/容差/声明序来源——须经
     *                    makeObjectiveSet 构造）
     * @return 筛选结果（稳定排序；绝不因候选间支配关系抛异常——候选级
     *         事实走结构化输出）
     *
     * @throws OptimizationError(kOptInputInvalid) 目标集为空/重复/容差
     *         非法；候选 metricValues 槽数 ≠8；激活目标槽位缺失（nullopt）
     *         或值非有限（NaN/Inf——NFR-COR-03：不静默转 0/不静默比较）
     *
     * 线程安全：const 只读（可并发——§12.3）。
     */
    virtual ParetoFrontResult buildFront(
        const std::vector<FeasibleCandidate>& candidates,
        const ObjectiveSet& objectives) const = 0;
};

/**
 * @brief Pareto 构建唯一产品实现（§12.2 O9 面；无状态纯算法——实例
 *        进程级共享安全）。
 */
class ParetoFrontBuilder final : public IParetoFrontBuilder {
public:
    /// @copydoc IParetoFrontBuilder::buildFront
    ParetoFrontResult buildFront(const std::vector<FeasibleCandidate>& candidates,
                                 const ObjectiveSet& objectives) const override;
};

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_PARETO_HPP
