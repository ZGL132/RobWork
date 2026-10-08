/**
 * @file   Pareto.cpp
 * @brief  Pareto 非支配集构建实现（WP-20-T05）——支配判定、非支配分层、
 *         去重与稳定排序。
 *
 * 设计依据：Pareto.hpp 文件头（设计依据与四件事背景说明）；本文件只补
 * 实现层决策（比较器构造/分层扫描序/去重保首见等确定性约定），语义锚
 * 不重复。
 *
 * 确定性：支配判定纯函数；分层按剩余集输入序扫描；排序比较器全序
 * （CandidateId 字节序兜底）——同输入同输出、与线程数无关（NFR-COR-02）。
 */

#include <sdurws/ird/optimization/Pareto.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include <sdurws/ird/core/Compare.hpp>            // core::closeWithin——C4 公式
                                                  // 唯一实现（消费不复制——SA-12 精神）
#include <sdurws/ird/optimization/DiagCodes.hpp>  // kOptInputInvalid——fail-fast 码面

namespace sdurws::ird::optimization {

namespace {

/// 激活目标槽位取值（FeasibleCandidate 八槽 → 指标值；越界/缺失由调用方
/// 前置校验保证——本辅助不做校验，直接取 optional 载荷）。
double slotValue(const FeasibleCandidate& c, MetricId id) noexcept
{
    return *c.metricValues[static_cast<std::size_t>(id)];
}

/// "aᵢ 不劣于 bᵢ（按方向 dᵢ，容差 tᵢ）"（§7.4 定义式——min/max 对称展开；
/// closeWithin 的参考元＝bᵢ，卡 §7.4 字面）。
bool notWorse(double a, double b, MetricDirection d, const core::Tolerance& t) noexcept
{
    if (d == MetricDirection::Minimize) {
        // min 方向：a ≤ b 即不劣；近似相等（容差内）同样视为不劣——
        // 零容差时 closeWithin 退化为 a == b（浮点全序，DOPT-5）。
        return a <= b || core::closeWithin(a, b, t);
    }
    // max 方向：a ≥ b 即不劣（对称）。
    return a >= b || core::closeWithin(a, b, t);
}

/// "aᵢ 严格优于 bᵢ（按方向 dᵢ，容差 tᵢ）"——差异须**超出容差**的优
/// （容差内的优不算严格优：保证"逐元素近似相等 ⇒ 互不支配"自洽）。
bool strictlyBetter(double a, double b, MetricDirection d,
                    const core::Tolerance& t) noexcept
{
    if (d == MetricDirection::Minimize) {
        return a < b && !core::closeWithin(a, b, t);
    }
    return a > b && !core::closeWithin(a, b, t);
}

}  // namespace

// =====================================================================
// 支配判定（Pareto.hpp 契约——noexcept 纯函数）
// =====================================================================

bool dominates(const FeasibleCandidate& a, const FeasibleCandidate& b,
               const ObjectiveSet& objectives) noexcept
{
    // 第一步：∀i —— aᵢ 不劣于 bᵢ（任一目标劣即不支配——短路返回）。
    for (const auto& entry : objectives.entries) {
        // 方向经 metricDirectionOf 零分配取向（词表方向实现单点——
        // 热路径友好；noexcept 函数内禁用会分配的 metricDefinitions()）。
        const MetricDirection d = metricDirectionOf(entry.metricId);
        if (!notWorse(slotValue(a, entry.metricId), slotValue(b, entry.metricId),
                      d, entry.tolerance)) {
            return false;
        }
    }
    // 第二步：∃j —— aⱼ 严格优于 bⱼ（全不劣但无一严格优＝互不支配——
    // 逐元素近似相等的候选对在此归为互不支配，§7.4 自洽性）。
    for (const auto& entry : objectives.entries) {
        const MetricDirection d = metricDirectionOf(entry.metricId);
        if (strictlyBetter(slotValue(a, entry.metricId),
                           slotValue(b, entry.metricId), d, entry.tolerance)) {
            return true;
        }
    }
    return false;
}

// =====================================================================
// buildFront（执行序见 Pareto.hpp——五步固定）
// =====================================================================

ParetoFrontResult ParetoFrontBuilder::buildFront(
    const std::vector<FeasibleCandidate>& candidates,
    const ObjectiveSet& objectives) const
{
    // ---- 第 1 步：目标集与输入校验（调用方契约违约 fail-fast——§12.3；
    //      错误归类：目标集/槽位形状/激活指标缺失或非有限＝调用方错误
    //      OPT-INPUT-INVALID——"—"候选在管线侧排除，到达本面即违约，
    //      文件头注④） ----
    {
        // 目标集自身合法性（空/重复/容差——与 Objective 侧同源口径：
        // 这里以最小面复验，防绕过 makeObjectiveSet 的构造路径）。
        if (objectives.entries.empty()) {
            throw OptimizationError(kOptInputInvalid,
                                    "buildFront: 激活目标集为空——无支配比较基础");
        }
        for (std::size_t i = 0; i < objectives.entries.size(); ++i) {
            for (std::size_t j = i + 1; j < objectives.entries.size(); ++j) {
                if (objectives.entries[i].metricId
                    == objectives.entries[j].metricId) {
                    throw OptimizationError(
                        kOptInputInvalid,
                        "buildFront: 激活目标重复（"
                            + std::string(toToken(objectives.entries[i].metricId))
                            + "）");
                }
            }
            const core::Tolerance& t = objectives.entries[i].tolerance;
            if (!std::isfinite(t.relative) || !std::isfinite(t.absolute)
                || t.relative < 0.0 || t.absolute < 0.0) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "buildFront: 支配容差非法（须 ≥0 且有限，指标 "
                        + std::string(toToken(objectives.entries[i].metricId))
                        + "）");
            }
        }
        // 逐候选形状与激活指标校验（输入序遍历——首错定位确定性）。
        for (const auto& c : candidates) {
            if (c.metricValues.size() != kMetricCount) {
                throw OptimizationError(
                    kOptInputInvalid,
                    "buildFront: 候选指标槽数须为八（MetricId 枚举序），实际 "
                        + std::to_string(c.metricValues.size()) + "（候选 "
                        + c.candidateId.toCanonical() + "）");
            }
            for (const auto& entry : objectives.entries) {
                const std::optional<double>& v =
                    c.metricValues[static_cast<std::size_t>(entry.metricId)];
                if (!v.has_value()) {
                    // 激活目标槽位缺失＝"—"候选未被管线侧排除（§7.4 缺失
                    // 指标不参与支配比较的实现位置——@pre 防线）。
                    throw OptimizationError(
                        kOptInputInvalid,
                        "buildFront: 激活目标指标缺失（not-computable）——"
                        "候选 "
                            + c.candidateId.toCanonical() + " 指标 "
                            + std::string(toToken(entry.metricId))
                            + "；'—'候选应在管线侧排除（DataInsufficient）");
                }
                if (!std::isfinite(*v)) {
                    // 非有限值（NaN/Inf）＝上游指标评估缺陷（§7.2：该候选
                    // 应已按评估失败处理）——不静默比较、不静默转 0。
                    throw OptimizationError(
                        kOptInputInvalid,
                        "buildFront: 指标值非有限——候选 "
                            + c.candidateId.toCanonical() + " 指标 "
                            + std::string(toToken(entry.metricId))
                            + "；上游应按评估失败处理（§7.2）");
                }
            }
        }
    }

    // ---- 第 2 步：去重（CandidateId 相同 → 保留输入序首见，丢弃计数——
    //      §7.4"重复 CandidateId → 去重为单候选（保留首次评估，dedupCount
    //      计数入审计）"；同身份不同指标向量按同规去重——身份是内容寻址
    //      权威，差异本身属上游一致性缺陷，计数如实暴露） ----
    std::vector<const FeasibleCandidate*> unique;  ///< 去重后（保持首见序）
    unique.reserve(candidates.size());
    std::uint32_t dropped = 0;
    for (const auto& c : candidates) {
        bool seen = false;
        for (const auto* u : unique) {
            if (u->candidateId == c.candidateId) {
                seen = true;
                break;
            }
        }
        if (seen) {
            ++dropped;  // 重复丢弃——审计计数（不抛：同身份重复是合法输入
                        // 形态——跨批合并的常态，§8.3 第 11 步）。
            continue;
        }
        unique.push_back(&c);
    }

    // ---- 第 3 步：非支配分层（逐层筛选——文件头注②；每轮在剩余集中
    //      取"不被任何同轮候选支配"者为当前层；扫描序＝去重后首见序，
    //      层内收集序＝首见序——两序都只是中间态，最终序由第 4 步全序
    //      比较器定死，NFR-COR-02） ----
    const std::size_t n = unique.size();
    std::vector<std::size_t> rank(n, 0);     ///< 每候选非支配 rank（0 基）
    std::vector<bool> assigned(n, false);    ///< 是否已入层
    std::size_t remaining = n;               ///< 剩余未分层候选数
    for (std::size_t currentRank = 0; remaining > 0; ++currentRank) {
        // 当前层：未被层外剩余集中任何未分层候选支配的候选。
        // O(剩余²) 支配判定——R1 预算 ≤256 候选（卡 §4.3 OptimizationBudget
        // 默认），最坏 6.5 万次纯函数调用，无性能顾虑。
        std::vector<std::size_t> front;
        for (std::size_t i = 0; i < n; ++i) {
            if (assigned[i]) {
                continue;  // 已入前序层——不再参与本轮筛选。
            }
            bool dominatedByAny = false;
            for (std::size_t j = 0; j < n; ++j) {
                if (j == i || assigned[j]) {
                    continue;  // 同候选/已分层候选不参与支配本轮。
                }
                if (dominates(*unique[j], *unique[i], objectives)) {
                    dominatedByAny = true;
                    break;
                }
            }
            if (!dominatedByAny) {
                front.push_back(i);
            }
        }
        // front 非空恒成立（有限偏序下剩余集中至少一个极小元——若为空
        // 属算法缺陷，fail-fast 不死循环）。
        if (front.empty()) {
            throw OptimizationError(kOptInputInvalid,
                                    "buildFront: 非支配分层异常（空层）——"
                                    "内部一致性缺陷");
        }
        for (const std::size_t i : front) {
            rank[i] = currentRank;
            assigned[i] = true;
            --remaining;
        }
    }

    // ---- 第 4 步：稳定排序（三键全序——§7.4/DOPT-8；键②用**严格浮点
    //      比较**，容差不进排序键——文件头注①：容差近似相等非传递，混入
    //      即破坏全序；键③ CandidateId 字节字典序兜底——无巧合平局） ----
    const std::vector<MetricDefinition> defs = metricDefinitions();  // 排序前
    // 一次构建（比较器内查表——方向权威消费面；非热路径逐次构造）。
    std::vector<std::size_t> order(n);
    for (std::size_t i = 0; i < n; ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(),
              [&](std::size_t lhs, std::size_t rhs) {
                  // 键①：非支配 rank 升序（前沿层序——第一前沿最先）。
                  if (rank[lhs] != rank[rhs]) {
                      return rank[lhs] < rank[rhs];
                  }
                  // 键②：激活目标值依次按方向严格比较（目标次序＝
                  // objectives 声明序——§7.4 稳定排序原文）。
                  for (const auto& entry : objectives.entries) {
                      const MetricDirection d =
                          defs[static_cast<std::size_t>(entry.metricId)]
                              .direction;
                      const double vl = slotValue(*unique[lhs], entry.metricId);
                      const double vr = slotValue(*unique[rhs], entry.metricId);
                      if (vl < vr) {
                          return d == MetricDirection::Minimize;
                      }
                      if (vr < vl) {
                          return d == MetricDirection::Maximize;
                      }
                      // 相等（浮点严格相等）→ 下一目标键。
                  }
                  // 键③：CandidateId 字节字典序终键（全序保证——同指标
                  // 向量候选由此定序，确定性无平局）。
                  return unique[lhs]->candidateId < unique[rhs]->candidateId;
              });

    // ---- 第 5 步：组装结果（entries 按稳定序；双集合＝rank 0 子集与
    //      全体，均保持 entries 序——§7.4 双标记显式呈现） ----
    ParetoFrontResult result;
    result.duplicatesDropped = dropped;
    result.entries.reserve(n);
    result.nondominatedIds.reserve(n);
    result.feasibleIds.reserve(n);
    for (const std::size_t i : order) {
        ParetoFrontEntry entry;
        entry.candidateId = unique[i]->candidateId;
        entry.isBaseline = unique[i]->isBaseline;
        entry.nondominationRank = rank[i];
        entry.paretoNondominated = (rank[i] == 0);  // 第一前沿＝非支配集
        result.entries.push_back(entry);
        result.feasibleIds.push_back(entry.candidateId);
        if (entry.paretoNondominated) {
            result.nondominatedIds.push_back(entry.candidateId);
        }
    }
    return result;
}

}  // namespace sdurws::ird::optimization
