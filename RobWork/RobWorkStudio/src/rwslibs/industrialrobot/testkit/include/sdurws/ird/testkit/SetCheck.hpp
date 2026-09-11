/**
 * @file   SetCheck.hpp
 * @brief  稳定集合与顺序断言——一一配对（匹配）语义＋稳定排序逐位一致（TK-T06）。
 *
 * 设计依据：
 *   - units/testkit.md §5.4（容差匹配不是等价关系——一一配对＝二分图匹配；两阶段
 *     匹配规则；歧义不判失败但报告；不提供贪心最近邻；规模护栏 100,000）、
 *     §5.4.3（集合/顺序不可互替）、§5.4.4（身份匹配优先）、§5.4.5（业务适配器
 *     归域——本头零业务规则）、§8 TK-SET/TK-ORD
 *   - 需求 NFR-COR-02（稳定排序）；任务契约 tasks/foundation/TK-T06.json
 *
 * 两阶段（§5.4.2）：
 *   阶段 0 重复检测：expected 身份键重复→DatasetInvalid（参考集自相矛盾）；
 *     actual 重复→失败 reason=duplicate-identity 逐键列出；
 *   阶段 1 稳定身份匹配：身份键精确相等（O(n)）；值超容差→失败
 *     reason=identity-value-mismatch 定位到字段；
 *   阶段 2 数值一一匹配：相容边＝全部声明数值字段经 profile 容差满足（C4 逐元素）；
 *     最大匹配（增广路径）；equivalent ⇔ 覆盖双方全部元素。
 *   规模护栏：单次断言元素数 >100,000 抛 TestKitError(Usage)。
 *
 * 歧义（§5.4.4）：多个完美匹配不判失败，报告 ambiguousMatch=true；确定性探测
 * （按 expected 索引序逐配对移除后尝试再增广）。
 *
 * 实现取舍（报告 notes 同步）：最大匹配用 Kuhn 增广路径（同 Hopcroft–Karp 的
 * 最大匹配结果；复杂度 O(E·V)——测试规模与稀疏相容边下可行，实现简练性优先；
 * §5.4.2 命名 Hopcroft–Karp 为复杂度期望，非算法强制）。
 *
 * 线程安全：纯函数（profile 只读）；无共享状态。
 */

#ifndef SDURWS_IRD_TESTKIT_SETCHECK_HPP
#define SDURWS_IRD_TESTKIT_SETCHECK_HPP

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/testkit/Check.hpp>   // CompareDetail（TK-T05 交付）
#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/testkit/TestPaths.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

namespace sdurws::ird::testkit {

/// 数值字段视图（traits::numerics 的元素——{fieldPath, valueSi}）。
struct NumericFieldView {
    std::string_view fieldPath;   ///< 数值字段路径（对 profile.resolve 的键）
    double valueSi;               ///< SI 真值
};

/// 见证配对（报告用——expected 索引 ↔ actual 索引）。
struct MatchPair {
    std::size_t expectedIndex;
    std::size_t actualIndex;
    bool operator==(const MatchPair& o) const noexcept
    {
        return expectedIndex == o.expectedIndex && actualIndex == o.actualIndex;
    }
    bool operator!=(const MatchPair& o) const noexcept { return !(*this == o); }
};

/// 集合等价断言结果（§5.4.2 SetCheckResult）。
struct SetCheckResult {
    bool equivalent = true;
    std::vector<std::size_t> missingExpected;      ///< 期望侧未匹配索引
    std::vector<std::size_t> extraActual;          ///< 实际侧未匹配索引
    std::vector<MatchPair> matched;                ///< 见证配对
    bool ambiguousMatch = false;                   ///< 多个完美匹配存在（不判失败）
    std::vector<CompareDetail> mismatchedPairs;  ///< identity-value-mismatch 详情
    std::vector<std::string> duplicates;           ///< actual 重复身份键（逐键列出）
};

/// 顺序断言结果：sameOrder＋首错位索引＋逐位失败详情。
struct OrderCheckResult {
    bool sameOrder = true;
    std::size_t firstDivergence = 0;               ///< 首个错位索引（同序时无意义）
    std::vector<CompareDetail> details;
};

/**
 * @brief 匹配 traits（消费方特化——§5.4.5 业务规则归域，testkit 零业务逻辑）。
 *
 * @tparam T 元素类型；主模板缺省＝identity nullopt（数值匹配）、numerics 空。
 */
template <class T>
struct SetMatchTraits {
    /// 稳定身份键（如 ObjectId 规范文本）；无身份返回 nullopt → 数值匹配。
    static std::optional<std::string> identity(const T&) { return std::nullopt; }
    /// 数值字段视图列表（全部声明字段都须满足容差才构成相容边）。
    static std::vector<NumericFieldView> numerics(const T&) { return {}; }
};

namespace detail {

/// 预期/实际元素是否构成相容边（字段同名一一对应＋全部经 profile 容差满足 C4）。
template <class T>
bool compatible(const T& expectedElem, const T& actualElem,
                const ToleranceProfile& profile, const SetMatchTraits<T>& traits)
{
    const auto expectedFields = traits.numerics(expectedElem);
    const auto actualFields = traits.numerics(actualElem);
    if (expectedFields.empty() || actualFields.size() != expectedFields.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expectedFields.size(); ++i) {
        if (expectedFields[i].fieldPath != actualFields[i].fieldPath) { return false; }
        if (!core::closeWithin(actualFields[i].valueSi, expectedFields[i].valueSi,
                               profile.resolve(expectedFields[i].fieldPath).tolerance)) {
            return false;
        }
    }
    return true;
}

/// 增广路径搜索（Kuhn：从左点 li 出发找可增广链——递归回退原配对）。
/// edge：相容边谓词（左点位置, 右点位置）→ bool；matchR/matchL：反向/正向配对表
/// （SIZE_MAX 哨兵＝未匹配）；visited：右点访问位图。
template <class EdgeFn>
bool tryAugmentAt(std::size_t li, const EdgeFn& edge, std::size_t nR,
                  std::vector<std::size_t>& matchR, std::vector<std::size_t>& matchL,
                  std::vector<bool>& visited)
{
    constexpr std::size_t kNil = static_cast<std::size_t>(-1);
    for (std::size_t ri = 0; ri < nR; ++ri) {
        if (visited[ri] || matchR[ri] != kNil) { continue; }
        if (!edge(li, ri)) { continue; }
        visited[ri] = true;
        matchL[li] = ri;
        matchR[ri] = li;
        return true;
    }
    for (std::size_t ri = 0; ri < nR; ++ri) {
        if (visited[ri] || !edge(li, ri)) { continue; }
        visited[ri] = true;
        const auto prevL = matchR[ri];
        matchR[ri] = li;
        matchL[prevL] = kNil;
        if (tryAugmentAt(prevL, edge, nR, matchR, matchL, visited)) {
            matchL[li] = ri;
            return true;
        }
        // 回滚：恢复原配对与本点未匹配态。
        matchR[ri] = prevL;
        matchL[prevL] = ri;
        matchL[li] = kNil;
    }
    return false;
}

}  // namespace detail

/**
 * @brief 集合等价断言（§5.4.2 两阶段匹配）。
 *
 * @throws TestKitError(Usage) 元素数 >100,000；
 *         TestKitError(DatasetInvalid) expected 身份键重复；
 *         TestKitError(ToleranceUndefined) 数值字段无容差条目
 */
template <class T>
SetCheckResult checkSetEquivalent(const std::vector<T>& expected,
                                  const std::vector<T>& actual,
                                  const ToleranceProfile& profile,
                                  const SetMatchTraits<T>& traits)
{
    SetCheckResult r;
    constexpr std::size_t kMaxElements = 100000;
    if (expected.size() > kMaxElements || actual.size() > kMaxElements) {
        throw TestKitError(TestKitErrorKind::Usage,
                           "usage: 元素数超规模护栏 100000（大规模数据按索引逐对或"
                           "摘要比较——§5.4.2）");
    }
    const auto identityOf = [&traits](const T& e) { return traits.identity(e); };

    // ---- 阶段 0：重复检测 ----
    // expected 身份键重复 → DatasetInvalid（参考集自相矛盾——调用期即拒）。
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto id = identityOf(expected[i]);
        if (!id.has_value()) { continue; }
        for (std::size_t j = i + 1; j < expected.size(); ++j) {
            if (identityOf(expected[j]) == id) {
                throw TestKitError(TestKitErrorKind::DatasetInvalid,
                                   "dataset-invalid: expected 身份键重复（参考集"
                                   "自相矛盾）: " + *id);
            }
        }
    }
    // actual 身份键重复 → 失败 reason=duplicate-identity（逐键列出）。
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const auto id = identityOf(actual[i]);
        if (!id.has_value()) { continue; }
        for (std::size_t j = i + 1; j < actual.size(); ++j) {
            if (identityOf(actual[j]) == id) {
                r.duplicates.push_back(*id);
                break;   // 同键只报一次
            }
        }
    }
    if (!r.duplicates.empty()) {
        r.equivalent = false;
        return r;
    }

    // ---- 阶段 1：稳定身份匹配（O(n)；值超容差→identity-value-mismatch） ----
    std::vector<bool> expectedMatched(expected.size(), false);
    std::vector<bool> actualMatched(actual.size(), false);
    {
        std::vector<std::pair<std::string, std::size_t>> actualIds;
        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (const auto id = identityOf(actual[i]); id.has_value()) {
                actualIds.emplace_back(*id, i);
            }
        }
        for (std::size_t ei = 0; ei < expected.size(); ++ei) {
            const auto id = identityOf(expected[ei]);
            if (!id.has_value()) { continue; }
            std::size_t found = actual.size();
            for (const auto& [key, idx] : actualIds) {
                if (key == *id && !actualMatched[idx]) { found = idx; break; }
            }
            if (found == actual.size()) { continue; }   // 数值阶段再试
            // 身份相等：数值字段须逐项满足容差，否则 identity-value-mismatch。
            const auto ef = traits.numerics(expected[ei]);
            const auto af = traits.numerics(actual[found]);
            bool valueOk = ef.size() == af.size();
            for (std::size_t k = 0; valueOk && k < ef.size(); ++k) {
                if (ef[k].fieldPath != af[k].fieldPath) { valueOk = false; break; }
                const auto tol = profile.resolve(ef[k].fieldPath).tolerance;
                if (!core::closeWithin(af[k].valueSi, ef[k].valueSi, tol)) {
                    valueOk = false;
                    r.mismatchedPairs.push_back(CompareDetail{
                        std::string{ef[k].fieldPath}, k, true, af[k].valueSi,
                        ef[k].valueSi, std::fabs(af[k].valueSi - ef[k].valueSi),
                        std::string{}, tol});
                }
            }
            if (valueOk) {
                expectedMatched[ei] = true;
                actualMatched[found] = true;
                r.matched.push_back({ei, found});
            } else {
                // identity-value-mismatch：期望/实际两侧均不计配对（等价已不可能）。
                expectedMatched[ei] = false;
                actualMatched[found] = false;
                r.missingExpected.push_back(ei);
                r.extraActual.push_back(found);
                r.equivalent = false;
            }
        }
    }

    // ---- 阶段 2：数值一一匹配（剩余元素；最大匹配） ----
    std::vector<std::size_t> expectedFreeIdx;
    std::vector<std::size_t> actualFreeIdx;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (!expectedMatched[i]) { expectedFreeIdx.push_back(i); }
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        if (!actualMatched[i]) { actualFreeIdx.push_back(i); }
    }

    const std::size_t nL = expectedFreeIdx.size();
    const std::size_t nR = actualFreeIdx.size();
    // 最大匹配（Kuhn 增广路径；SIZE_MAX 哨兵＝未匹配）。
    std::vector<std::size_t> matchL(nL, static_cast<std::size_t>(-1));
    std::vector<std::size_t> matchR(nR, static_cast<std::size_t>(-1));
    const auto edge = [&](std::size_t li, std::size_t ri) {
        return detail::compatible(expected[expectedFreeIdx[li]],
                                  actual[actualFreeIdx[ri]], profile, traits);
    };
    const auto tryAugment = [&](std::size_t li, std::vector<bool>& visited) {
        return detail::tryAugmentAt(li, edge, nR, matchR, matchL, visited);
    };
    for (std::size_t li = 0; li < nL; ++li) {
        if (matchL[li] != static_cast<std::size_t>(-1)) { continue; }
        std::vector<bool> visited(nR, false);
        (void)tryAugment(li, visited);
    }
    for (std::size_t li = 0; li < nL; ++li) {
        if (matchL[li] == static_cast<std::size_t>(-1)) {
            r.missingExpected.push_back(expectedFreeIdx[li]);
        } else {
            const auto ei = expectedFreeIdx[li];
            const auto ai = actualFreeIdx[matchL[li]];
            expectedMatched[ei] = true;
            actualMatched[ai] = true;
            r.matched.push_back({ei, ai});
        }
    }
    for (std::size_t ri = 0; ri < nR; ++ri) {
        if (matchR[ri] == static_cast<std::size_t>(-1)) {
            r.extraActual.push_back(actualFreeIdx[ri]);
        }
    }

    // equivalent ⇔ 覆盖双方全部元素（阶段 1＋阶段 2 合计）。
    for (std::size_t i = 0; i < expected.size() && r.equivalent; ++i) {
        if (!expectedMatched[i]) { r.equivalent = false; }
    }
    for (std::size_t i = 0; i < actual.size() && r.equivalent; ++i) {
        if (!actualMatched[i]) { r.equivalent = false; }
    }

    // ---- 歧义检测（§5.4.4：逐配对移除后尝试再增广；首个替代即置位——确定性：
    // 按 matched 记录序＝expected 索引序探测）----
    if (r.equivalent && !r.matched.empty()) {
        // 仅阶段 2 配对可能歧义（身份配对唯一，§5.4.4"身份匹配确定性"）。
        for (std::size_t mi = r.matched.size(); mi-- > 0;) {
            const auto& pair = r.matched[mi];
            const bool inPhase2 =
                std::find(expectedFreeIdx.begin(), expectedFreeIdx.end(),
                          pair.expectedIndex) != expectedFreeIdx.end();
            if (!inPhase2) { continue; }
            std::size_t li = 0, ri = 0;
            bool foundPos = false;
            for (li = 0; li < nL; ++li) {
                if (expectedFreeIdx[li] == pair.expectedIndex) { foundPos = true; break; }
            }
            for (ri = 0; foundPos && ri < nR; ++ri) {
                if (actualFreeIdx[ri] == pair.actualIndex) { break; }
            }
            if (!foundPos || ri >= nR) { continue; }
            // 移除配对→尝试为 li 寻找其他 ri（存在＝歧义）。
            matchL[li] = static_cast<std::size_t>(-1);
            matchR[ri] = static_cast<std::size_t>(-1);
            std::vector<bool> visited(nR, false);
            if (tryAugment(li, visited)) {
                r.ambiguousMatch = true;
                // 恢复原配对（保持见证配对一致——歧义报告不改判）。
                matchL[li] = ri;
                matchR[ri] = li;
                break;
            }
            matchL[li] = ri;
            matchR[ri] = li;
        }
    }

    // mismatchedPairs 补齐：数值未匹配的最近对详情（missing×extra 数值距离和
    // 最近——§5.4.2"最近对详情"；无相容边的 missing/extra 不配对详情）。
    for (const auto ei : r.missingExpected) {
        double best = -1.0;
        std::size_t bestIdx = 0;
        bool found = false;
        for (const auto ai : r.extraActual) {
            const auto ef = traits.numerics(expected[ei]);
            const auto af = traits.numerics(actual[ai]);
            if (ef.empty() || ef.size() != af.size()) { continue; }
            double sum = 0.0;
            for (std::size_t k = 0; k < ef.size(); ++k) {
                sum += std::fabs(af[k].valueSi - ef[k].valueSi);
            }
            if (!found || sum < best) { found = true; best = sum; bestIdx = ai; }
        }
        if (found) {
            const auto ef = traits.numerics(expected[ei]);
            const auto af = traits.numerics(actual[bestIdx]);
            for (std::size_t k = 0; k < ef.size(); ++k) {
                r.mismatchedPairs.push_back(CompareDetail{
                    std::string{ef[k].fieldPath}, k, true, af[k].valueSi,
                    ef[k].valueSi, std::fabs(af[k].valueSi - ef[k].valueSi),
                    std::string{}, profile.resolve(ef[k].fieldPath).tolerance});
            }
        }
    }

    return r;
}

template <class T>
OrderCheckResult checkStableOrder(const std::vector<T>& expectedOrder,
                                  const std::vector<T>& actual,
                                  const SetMatchTraits<T>& traits,
                                  const ToleranceProfile& profile)
{
    OrderCheckResult r;
    const std::size_t n = std::min(expectedOrder.size(), actual.size());
    if (expectedOrder.size() != actual.size()) {
        r.sameOrder = false;
        r.firstDivergence = n;   // 长度不等＝较短长度处起全部错位
    }
    for (std::size_t i = 0; i < n; ++i) {
        const auto ei = traits.identity(expectedOrder[i]);
        const auto ai = traits.identity(actual[i]);
        bool posOk = true;
        if (ei.has_value() && ai.has_value()) {
            // 身份按位精确相等（附录 D 第 12 项：无容差）。
            if (*ei != *ai) { posOk = false; }
        } else {
            // 数值按位容差：对应位置 numeric 字段逐项（resolve 按 expected 路径）。
            const auto ef = traits.numerics(expectedOrder[i]);
            const auto af = traits.numerics(actual[i]);
            if (ef.size() != af.size()) { posOk = false; }
            for (std::size_t k = 0; posOk && k < ef.size(); ++k) {
                if (ef[k].fieldPath != af[k].fieldPath) { posOk = false; continue; }
                const auto tol = profile.resolve(ef[k].fieldPath).tolerance;
                if (!core::closeWithin(af[k].valueSi, ef[k].valueSi, tol)) {
                    posOk = false;
                }
            }
        }
        if (!posOk) {
            if (r.details.empty() && r.sameOrder) { r.firstDivergence = i; }
            r.sameOrder = false;
            r.details.push_back(CompareDetail{
                std::string{"order["} + std::to_string(i) + "]", i, true,
                0.0, 0.0, 0.0, std::string{}, core::Tolerance{}});
        }
    }
    return r;
}

}  // namespace sdurws::ird::testkit

#endif  // SDURWS_IRD_TESTKIT_SETCHECK_HPP
