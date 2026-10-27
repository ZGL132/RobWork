// REUSE-T34 性能实测主 TU（G8——T07 前科点名的对照测量）：耦合矩阵奇异值
// 生产基线（单侧 Jacobi，实施前等义副本）vs 候选（rw::math::LinearAlgebra
// ::svd，上游强制 ComputeFullU|ComputeFullV）。判定规则（预声明）：本操作
// 无既有性能预算；T07 同接口迁移实测 4.662× 耗时且接受悬置
// （reuse/coupling-svd-migration.md 运行效果行）为前科——实测 new>old
// （任何幅度退化）即不落地生产，候选/测量交所有者单项裁决；new≤old 才可
// 换核。σ-only vs 全量 U/V 开销份额由 svdSigmaOnlyEigen 单列量化（该径为
// 未批依赖渠道，仅测量证据）。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

#include "perf-old.cpp"
#include "perf-new.cpp"

namespace {

std::vector<double> makeMatrix(std::size_t n, std::uint64_t seed)
{
    // 确定性 LCG（固定种子——同输入可复现）；C=M·Mᵀ+diag(1..n) 保证正定
    // 良态（奇异/病态分支的判定正确性由 DescriptionValidatorTest oracle
    // 承载，本 TU 只测良态域的分解耗时——验证调用的主路径）。
    std::uint64_t state = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    auto next = [&state]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<double>((state >> 11) & 0x1FFFFFFFFFFFFFULL) / 9007199254740992.0;
    };
    std::vector<double> m(n * n, 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t c = 0; c < n; ++c) {
            m[r * n + c] = next() - 0.5;
        }
    }
    std::vector<double> c(m.size(), 0.0);
    for (std::size_t r = 0; r < n; ++r) {
        for (std::size_t cIdx = 0; cIdx < n; ++cIdx) {
            double sum = 0.0;
            for (std::size_t k = 0; k < n; ++k) {
                sum += m[r * n + k] * m[cIdx * n + k];
            }
            c[r * n + cIdx] = sum;
        }
        c[r * n + r] += static_cast<double>(r + 1);
    }
    return c;
}

template <typename Rep>
double measureCycles(const Rep& f, int warmup, int groups, int iters)
{
    for (int i = 0; i < warmup; ++i) { f(); }
    std::vector<double> perCall;
    for (int g = 0; g < groups; ++g) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) { f(); }
        const auto t1 = std::chrono::steady_clock::now();
        perCall.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count()
                          / static_cast<double>(iters));
    }
    std::sort(perCall.begin(), perCall.end());
    return perCall[perCall.size() / 2];  // 组中位数（T07 同口径）
}

}  // namespace

int main()
{
    constexpr int kWarmup = 100;
    constexpr int kGroups = 7;
    constexpr int kIters = 10000;
    bool allMatch = true;
    std::printf("REUSE-T34 耦合矩阵奇异值候选性能（良态域；每版 %d 组×%d 次、%d 次预热，组中位数）\n",
                kGroups, kIters, kWarmup);
    for (const std::size_t n : {std::size_t{3}, std::size_t{6}, std::size_t{12}}) {
        const std::vector<double> a = makeMatrix(n, 20261025ULL + n);
        // 正确性对照（上游直接对照证据）：两路奇异值升序后逐项相对差。
        const std::vector<double> oldS = t34perf::singularValuesOneSidedJacobi(a, n);
        const std::vector<double> newS = t34perf::svdUpstream(a, n);
        double maxRel = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            maxRel = std::max(maxRel, std::fabs(oldS[i] - newS[i]) / std::fabs(oldS[i]));
        }
        if (maxRel > 1e-9) { allMatch = false; }
        const double tOld = measureCycles(
            [&] { t34perf::singularValuesOneSidedJacobi(a, n); }, kWarmup, kGroups, kIters);
        const double tNew = measureCycles(
            [&] { t34perf::svdUpstream(a, n); }, kWarmup, kGroups, kIters);
        const double tSigmaOnly = (n == 6)
            ? measureCycles([&] { t34perf::svdSigmaOnlyEigen(a, n); }, kWarmup, kGroups, kIters)
            : 0.0;
        std::printf("n=%2zu  old=%8.4f μs/次  new(LinearAlgebra::svd)=%8.4f μs/次  "
                    "比值=%.3f  σ相对差=%.2e%s\n",
                    n, tOld, tNew, tNew / tOld, maxRel,
                    n == 6 ? "" : "");
        if (n == 6) {
            std::printf("      σ-only 直连 Eigen JacobiSVD=%8.4f μs/次"
                        "（未批依赖渠道——仅量化上游强制全量 U/V 的开销份额）\n",
                        tSigmaOnly);
        }
    }
    if (!allMatch) {
        std::printf("正确性对照 FAIL——新旧奇异值超 1e-9 相对差\n");
        return 1;
    }
    std::printf("判定规则（预声明）：无既有预算；new>old 任何幅度退化即不落地生产，"
                "候选交所有者单项裁决（T07 同接口 4.662× 悬置前科）\n");
    return 0;
}
