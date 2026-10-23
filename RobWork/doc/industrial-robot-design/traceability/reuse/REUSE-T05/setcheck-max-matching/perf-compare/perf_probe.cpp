/**
 * @file   perf_probe.cpp
 * @brief  SetCheck 阶段 2 匹配操作——基线（Kuhn，691cbd42e 快照）与新实现
 *         （Boost checked Edmonds 经 detail::maximumBipartiteMatching）的
 *         受控规模对照探针（REUSE-T20 补正轮；measurement-only，非产品代码）。
 *
 * 边界与公平性：
 *   - 两侧使用同一生产相容边语义：detail::compatible（SetCheck.hpp 模板，
 *     经 ToleranceProfile::resolve＋core::closeWithin——产品原路径，零复制）；
 *   - "匹配操作"计时＝新实现：相容边物化＋接缝调用（生产路径完整形态）；
 *     基线：谓词驱动 Kuhn 驱动循环（含每左点 visited 分配——生产原形态）。
 *     元素构造/档案装载/伪随机填充均不计入；
 *   - 基线内核为 691cbd42e 的 detail::tryAugmentAt 逐字快照（仅测量用，
 *     产品已删除、无 fallback 语义——.snapshot 标注段）；
 *   - 规模护栏外的病态输入（如 100000 节点完全图）按指令不运行。
 *
 * 内存口径：解析项（edges vector 16B/边＋Boost vecS 邻接 (V+2E)×8B，对比
 * 基线 O(V) 状态）由本探针按边数打印；进程峰值工作集（GetProcessMemoryInfo）
 * 作为粗粒度实测参照（两模式分进程运行，main(mode) 分发）。
 *
 * 确定性：LCG 固定种子；同输入同案例集（无运行时随机源）。
 */

#include <sdurws/ird/core/Compare.hpp>
#include <sdurws/ird/testkit/SetCheck.hpp>
#include <sdurws/ird/testkit/ToleranceProfile.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>
#include <psapi.h>

/// 探针元素：单一数值字段（fieldPath 随用例选择容差档位）。
struct ProbeElem {
    double v = 0.0;               ///< SI 值（单一声明字段）
    std::string fp;               ///< 字段路径（选择 profile 容差条目）
};

/// traits：与 SetCheckTest 同型（单字段视图；全局作用域特化——匿名命名空间
/// 内不得定义外部模板的特化，C2888）。
template <>
struct sdurws::ird::testkit::SetMatchTraits<ProbeElem> {
    static std::optional<std::string> identity(const ProbeElem&)
    {
        return std::nullopt;   // 纯数值形态（阶段 2 全量走匹配内核）
    }
    static std::vector<sdurws::ird::testkit::NumericFieldView> numerics(
        const ProbeElem& e)
    {
        return {{e.fp, e.v}};
    }
};

namespace {

using namespace sdurws::ird::testkit;

/// 确定性 LCG（与 SetCheckTest 同构；固定种子——同输入同案例集）。
struct DetRandom {
    std::uint32_t state;
    explicit DetRandom(std::uint32_t seed) : state(seed) {}
    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 16) & 0x7FFFu;
    }
};

/// 计时器（QueryPerformanceCounter）。
double secondsSince(const LARGE_INTEGER& start, const LARGE_INTEGER& freq)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - start.QuadPart)
           / static_cast<double>(freq.QuadPart);
}

/// 进程峰值工作集（MiB；粗粒度实测参照）。
double peakWorkingSetMiB()
{
    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
}

// =====================================================================
// 基线内核快照（measurement-only）：691cbd42e SetCheck.hpp detail::tryAugmentAt
// 与其驱动循环的逐字复制（Kuhn 增广＋回滚；SIZE_MAX 哨兵）。产品已删除该
// 实现——此处仅为受控对照保留，不含任何 fallback 语义。
// =====================================================================
template <class EdgeFn>
bool tryAugmentAt_baseline(std::size_t li, const EdgeFn& edge, std::size_t nR,
                           std::vector<std::size_t>& matchR,
                           std::vector<std::size_t>& matchL,
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
        if (tryAugmentAt_baseline(prevL, edge, nR, matchR, matchL, visited)) {
            matchL[li] = ri;
            return true;
        }
        matchR[ri] = prevL;
        matchL[prevL] = ri;
        matchL[li] = kNil;
    }
    return false;
}

/// 基线驱动循环（同产品原形态：逐左点 fresh visited）。
std::vector<std::size_t> baselineMatching(
    std::size_t nL, std::size_t nR,
    const std::function<bool(std::size_t, std::size_t)>& edge)
{
    constexpr std::size_t kNil = static_cast<std::size_t>(-1);
    std::vector<std::size_t> matchL(nL, kNil);
    std::vector<std::size_t> matchR(nR, kNil);
    for (std::size_t li = 0; li < nL; ++li) {
        if (matchL[li] != kNil) { continue; }
        std::vector<bool> visited(nR, false);
        (void)tryAugmentAt_baseline(li, edge, nR, matchR, matchL, visited);
    }
    return matchL;
}

/// 新实现生产路径：相容边物化（同 checkSetEquivalent 阶段 2）＋接缝调用。
std::vector<std::size_t> newMatching(std::size_t nL, std::size_t nR,
                                     const std::function<bool(std::size_t, std::size_t)>& edge,
                                     std::size_t& edgeCount)
{
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (std::size_t li = 0; li < nL; ++li) {
        for (std::size_t ri = 0; ri < nR; ++ri) {
            if (edge(li, ri)) { edges.emplace_back(li, ri); }
        }
    }
    edgeCount = edges.size();
    return detail::maximumBipartiteMatching(nL, nR, edges);
}

/// 单案例：生成确定性案例，按模式计时（best-of-3），打印行。
void runCase(std::size_t n, double tolAbs, const char* densityLabel,
             const char* mode, const ToleranceProfile& profile,
             const std::string& fieldPath)
{
    // 案例生成（不计入匹配计时）：U[0,1] 值——P(|U1-U2|≤t)=2t-t²。
    DetRandom rng{20261023u};
    std::vector<ProbeElem> left(n), right(n);
    for (std::size_t i = 0; i < n; ++i) {
        left[i].v = static_cast<double>(rng.next()) / 32768.0;
        left[i].fp = fieldPath;
        right[i].v = static_cast<double>(rng.next()) / 32768.0;
        right[i].fp = fieldPath;
    }
    const auto edge = [&](std::size_t li, std::size_t ri) {
        return detail::compatible(left[li], right[ri], profile,
                                  SetMatchTraits<ProbeElem>{});
    };

    double best = 1e30;
    std::size_t matched = 0;
    std::size_t edgeCount = 0;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    for (int rep = 0; rep < 3; ++rep) {
        LARGE_INTEGER t0;
        QueryPerformanceCounter(&t0);
        if (std::strcmp(mode, "old") == 0) {
            const auto m = baselineMatching(n, n, edge);
            std::size_t c = 0;
            for (auto v : m) { if (v != static_cast<std::size_t>(-1)) { ++c; } }
            matched = c;
        } else {
            const auto m = newMatching(n, n, edge, edgeCount);
            std::size_t c = 0;
            for (auto v : m) { if (v != static_cast<std::size_t>(-1)) { ++c; } }
            matched = c;
        }
        const double dt = secondsSince(t0, freq);
        if (dt < best) { best = dt; }
    }
    // 解析内存（新实现新增面）：edges 16B/边＋邻接 (2n+2E)×8B；基线 O(V) 状态。
    const double eApprox = 2.0 * tolAbs * static_cast<double>(n) * static_cast<double>(n);
    const double newBytes = (mode[0] == 'n')
        ? (eApprox * 16.0 + (2.0 * static_cast<double>(n) + 2.0 * eApprox) * 8.0)
        : (2.0 * static_cast<double>(n) * 8.0 + static_cast<double>(n) / 8.0);
    std::printf("mode=%s n=%zu density=%s tol=%.3g matched=%zu edges~%.0f best_ms=%.2f analyticMB=%.1f peakWS=%.1f\n",
                mode, n, densityLabel, tolAbs, matched, eApprox, best * 1000.0,
                newBytes / (1024.0 * 1024.0), peakWorkingSetMiB());
    std::fflush(stdout);
}

}  // namespace

/**
 * @brief 探针入口：perf_probe old|new <profile.json>。
 * @param argc [in] 参数计数（模式＋档案路径）
 * @param argv [in] 模式（old/new）＋容差档案路径
 * @return 0（任一案例异常即传播失败）
 */
int main(int argc, char** argv)
{
    std::printf("probe start argc=%d\n", argc);
    std::fflush(stdout);
    if (argc < 3) {
        std::fprintf(stderr, "usage: perf_probe old|new <tolerance-profile.json>\n");
        return 2;
    }
    const std::string mode = argv[1];
    std::optional<ToleranceProfile> profile;
    try {
        profile = ToleranceProfile::load(std::filesystem::path{argv[2]});
    } catch (const TestKitError& e) {
        std::fprintf(stderr, "profile load failed: %s\n", e.what());
        std::fflush(stderr);
        return 3;
    }
    std::printf("profile loaded\n");
    std::fflush(stdout);
    // 档位：fine≈2%／medium≈10%／coarse≈36%（P(|U1-U2|≤t)=2t-t²）。
    const struct { const char* label; const char* fp; double tol; } tiers[] = {
        {"sparse", "probe.tolerance.fine", 0.01},
        {"medium", "probe.tolerance.medium", 0.05},
        {"dense", "probe.tolerance.coarse", 0.20},
    };
    const std::size_t sizesDense[] = {64, 256, 1024};
    const std::size_t sizesSparse[] = {64, 256, 1024, 4096};
    for (const auto& tier : tiers) {
        const bool isSparse = std::strcmp(tier.label, "sparse") == 0;
        if (isSparse) {
            // 稀疏档含大尺度点（4096×4096 求值 ≈ 16.8M 次边评估，受控）。
            for (std::size_t n : sizesSparse) {
                runCase(n, tier.tol, tier.label, mode.c_str(), *profile, tier.fp);
            }
        } else {
            // 指令边界：病态完全图/护栏规模不运行——medium/dense 档不含 4096。
            for (std::size_t n : sizesDense) {
                runCase(n, tier.tol, tier.label, mode.c_str(), *profile, tier.fp);
            }
        }
    }
    std::printf("done mode=%s\n", mode.c_str());
    return 0;
}
