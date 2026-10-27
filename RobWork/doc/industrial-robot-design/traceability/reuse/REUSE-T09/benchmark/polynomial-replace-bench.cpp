/**
 * @file   polynomial-replace-bench.cpp
 * @brief  REUSE-T09 性能/分配/确定性核查探针（代表性采样场景）。
 *
 * 场景：20 样条段 × 每段 100 采样点 × 6 关节（轨迹时间参数化峰值扫描
 * 的典型量级——TimeParam 每缩放轮次的采样面）。
 *
 * 对照三方（同一 TU、同一编译开关、同一算术序结构）：
 *   - old-hand      替换前的手写五次/七次基展开式（git 历史 0032c8e78^
 *                   的 TimeParam.cpp 逐式转录——对照基准）；
 *   - upstream-eval 替换后生产形态：上游 Polynomial 基族表（derivative()
 *                   构造导数链）＋ 16 次 evaluate/采样 ——期望：与旧式
 *                   时间同量级、零分配；
 *   - evalderivs    被否决的替代形态：逐轴装配段多项式 ＋ 每采样每轴
 *                   evaluateDerivatives(u,3)——期望：每采样 6 次 vector
 *                   分配（热路径纪律否决依据的实证）。
 *
 * 输出：每形态的耗时（多次迭代取最优，降低调度噪声）、每迭代堆分配次
 * 数、新旧形态输出的位级一致性（确定性）与最大相对漂移。
 *
 * 编译（NDEBUG 使 RW_ASSERT 为空——头内模板零 rw 库符号依赖）：
 *   cl /O2 /DNDEBUG /std:c++17 /utf-8 /EHsc /I <rw>/src /I <eigen3> ^
 *     polynomial-replace-bench.cpp /Fe:polynomial-replace-bench.exe
 */

#include <rw/math/Polynomial.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

// =====================================================================
// 分配计数（全局 operator new/delete——本探针全部模板代码在本 TU 实例
// 化，无 DLL 边界；CRT 内部分配不经 operator new，不计入）
// =====================================================================

static std::size_t g_allocCount = 0;

void* operator new(std::size_t n)
{
    ++g_allocCount;
    if (void* p = std::malloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    ++g_allocCount;
    if (void* p = std::malloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// =====================================================================
// 场景常量
// =====================================================================

namespace {
constexpr int kSpans = 20;
constexpr int kSamplesPerSpan = 100;
constexpr int kJoints = 6;
constexpr int kIterations = 20;

/// 逐轴固定权重（q0/hm0/q1/hm1——四基加权的合成输入，确定性来源固定）。
struct AxisWeights
{
    double q0, hm0, q1, hm1;
};

std::array<std::array<AxisWeights, kJoints>, kSpans> makeWeights()
{
    std::array<std::array<AxisWeights, kJoints>, kSpans> w{};
    for (int s = 0; s < kSpans; ++s) {
        for (int j = 0; j < kJoints; ++j) {
            const double v = 0.1 * (s + 1) + 0.01 * (j + 1);
            w[s][j] = {v * 0.25, v * 0.1, v, v * 0.3};  // 非零端导数形态
        }
    }
    return w;
}

// =====================================================================
// old-hand——替换前手写基（git 历史逐式转录；五次 4 基＋七次 4 基）
// =====================================================================

struct OldBasis
{
    double p, d1, d2, d3;
};

inline OldBasis oldH0000(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    OldBasis b;
    b.p  = 1.0 - 10.0 * u3 + 15.0 * u2 * u2 - 6.0 * u3 * u2;
    b.d1 = -30.0 * u2 + 60.0 * u3 - 30.0 * u2 * u2;
    b.d2 = -60.0 * u + 180.0 * u2 - 120.0 * u3;
    b.d3 = -60.0 + 360.0 * u - 360.0 * u2;
    return b;
}
inline OldBasis oldH0010(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    OldBasis b;
    b.p  = u - 6.0 * u3 + 8.0 * u2 * u2 - 3.0 * u3 * u2;
    b.d1 = 1.0 - 18.0 * u2 + 32.0 * u3 - 15.0 * u2 * u2;
    b.d2 = -36.0 * u + 96.0 * u2 - 60.0 * u3;
    b.d3 = -36.0 + 192.0 * u - 180.0 * u2;
    return b;
}
inline OldBasis oldH0001(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    OldBasis b;
    b.p  = 10.0 * u3 - 15.0 * u2 * u2 + 6.0 * u3 * u2;
    b.d1 = 30.0 * u2 - 60.0 * u3 + 30.0 * u2 * u2;
    b.d2 = 60.0 * u - 180.0 * u2 + 120.0 * u3;
    b.d3 = 60.0 - 360.0 * u + 360.0 * u2;
    return b;
}
inline OldBasis oldH0011(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    OldBasis b;
    b.p  = -4.0 * u3 + 7.0 * u2 * u2 - 3.0 * u3 * u2;
    b.d1 = -12.0 * u2 + 28.0 * u3 - 15.0 * u2 * u2;
    b.d2 = -24.0 * u + 84.0 * u2 - 60.0 * u3;
    b.d3 = -24.0 + 168.0 * u - 180.0 * u2;
    return b;
}
inline OldBasis oldS0(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    OldBasis b;
    b.p  = 1.0 - 35.0 * u4 + 84.0 * u5 - 70.0 * u6 + 20.0 * u5 * u2;
    b.d1 = -140.0 * u3 + 420.0 * u4 - 420.0 * u5 + 140.0 * u6;
    b.d2 = -420.0 * u2 + 1680.0 * u3 - 2100.0 * u4 + 840.0 * u5;
    b.d3 = -840.0 * u + 5040.0 * u2 - 8400.0 * u3 + 4200.0 * u4;
    return b;
}
inline OldBasis oldS1(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    OldBasis b;
    b.p  = u - 20.0 * u4 + 45.0 * u5 - 36.0 * u6 + 10.0 * u5 * u2;
    b.d1 = 1.0 - 80.0 * u3 + 225.0 * u4 - 216.0 * u5 + 70.0 * u6;
    b.d2 = -240.0 * u2 + 900.0 * u3 - 1080.0 * u4 + 420.0 * u5;
    b.d3 = -480.0 * u + 2700.0 * u2 - 4320.0 * u3 + 2100.0 * u4;
    return b;
}
inline OldBasis oldS2(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    OldBasis b;
    b.p  = 35.0 * u4 - 84.0 * u5 + 70.0 * u6 - 20.0 * u5 * u2;
    b.d1 = 140.0 * u3 - 420.0 * u4 + 420.0 * u5 - 140.0 * u6;
    b.d2 = 420.0 * u2 - 1680.0 * u3 + 2100.0 * u4 - 840.0 * u5;
    b.d3 = 840.0 * u - 5040.0 * u2 + 8400.0 * u3 - 4200.0 * u4;
    return b;
}
inline OldBasis oldS3(double u)
{
    const double u2 = u * u;
    const double u3 = u2 * u;
    const double u4 = u2 * u2;
    const double u5 = u4 * u;
    const double u6 = u5 * u;
    OldBasis b;
    b.p  = -15.0 * u4 + 39.0 * u5 - 34.0 * u6 + 10.0 * u5 * u2;
    b.d1 = -60.0 * u3 + 195.0 * u4 - 204.0 * u5 + 70.0 * u6;
    b.d2 = -180.0 * u2 + 780.0 * u3 - 1020.0 * u4 + 420.0 * u5;
    b.d3 = -360.0 * u + 2340.0 * u2 - 4080.0 * u3 + 2100.0 * u4;
    return b;
}

/// 旧式形态一段采样（septic=false 走五次）——返回校验和兼输出面。
double runOldHand(const std::array<std::array<AxisWeights, kJoints>, kSpans>& w,
                  bool septic, double* maxAbsOut)
{
    double checksum = 0.0;
    double maxAbs = 0.0;
    for (int iter = 0; iter < kIterations; ++iter) {
        for (int s = 0; s < kSpans; ++s) {
            for (int k = 0; k < kSamplesPerSpan; ++k) {
                const double u = static_cast<double>(k) / kSamplesPerSpan;
                const OldBasis b0 = septic ? oldS0(u) : oldH0000(u);
                const OldBasis b1 = septic ? oldS1(u) : oldH0010(u);
                const OldBasis b2 = septic ? oldS2(u) : oldH0001(u);
                const OldBasis b3 = septic ? oldS3(u) : oldH0011(u);
                for (int j = 0; j < kJoints; ++j) {
                    const AxisWeights& a = w[s][j];
                    const double q    = b0.p * a.q0 + b1.p * a.hm0 + b2.p * a.q1 + b3.p * a.hm1;
                    const double qd   = b0.d1 * a.q0 + b1.d1 * a.hm0 + b2.d1 * a.q1 + b3.d1 * a.hm1;
                    const double qdd  = b0.d2 * a.q0 + b1.d2 * a.hm0 + b2.d2 * a.q1 + b3.d2 * a.hm1;
                    const double qddd = b0.d3 * a.q0 + b1.d3 * a.hm0 + b2.d3 * a.q1 + b3.d3 * a.hm1;
                    checksum += q + 0.1 * qd + 0.01 * qdd + 0.001 * qddd;
                    maxAbs = std::max(maxAbs, std::abs(q) + std::abs(qd));
                }
            }
        }
    }
    *maxAbsOut = maxAbs;
    return checksum;
}

// =====================================================================
// upstream-eval——替换后生产形态（基族表＋上游 evaluate）
// =====================================================================

struct BasisFamily
{
    explicit BasisFamily(const rw::math::Polynomial<>& value)
        : p(value),
          d1(value.derivative(1)),
          d2(value.derivative(2)),
          d3(value.derivative(3))
    {
    }
    rw::math::Polynomial<> p, d1, d2, d3;
};

const std::array<BasisFamily, 4>& tableBases(bool septic)
{
    static const std::array<BasisFamily, 4> quintic = {
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{1.0, 0.0, 0.0, -10.0, 15.0, -6.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 1.0, 0.0, -6.0, 8.0, -3.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 0.0, 0.0, 10.0, -15.0, 6.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 0.0, 0.0, -4.0, 7.0, -3.0}))};
    static const std::array<BasisFamily, 4> septicTable = {
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{1.0, 0.0, 0.0, 0.0, -35.0, 84.0, -70.0, 20.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 1.0, 0.0, 0.0, -20.0, 45.0, -36.0, 10.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 0.0, 0.0, 0.0, 35.0, -84.0, 70.0, -20.0})),
        BasisFamily(rw::math::Polynomial<>(std::vector<double>{0.0, 0.0, 0.0, 0.0, -15.0, 39.0, -34.0, 10.0}))};
    return septic ? septicTable : quintic;
}

double runUpstreamEval(const std::array<std::array<AxisWeights, kJoints>, kSpans>& w,
                       bool septic, double* maxAbsOut)
{
    const std::array<BasisFamily, 4>& t = tableBases(septic);
    double checksum = 0.0;
    double maxAbs = 0.0;
    for (int iter = 0; iter < kIterations; ++iter) {
        for (int s = 0; s < kSpans; ++s) {
            for (int k = 0; k < kSamplesPerSpan; ++k) {
                const double u = static_cast<double>(k) / kSamplesPerSpan;
                const double b[4][4] = {
                    {t[0].p.evaluate(u), t[0].d1.evaluate(u), t[0].d2.evaluate(u), t[0].d3.evaluate(u)},
                    {t[1].p.evaluate(u), t[1].d1.evaluate(u), t[1].d2.evaluate(u), t[1].d3.evaluate(u)},
                    {t[2].p.evaluate(u), t[2].d1.evaluate(u), t[2].d2.evaluate(u), t[2].d3.evaluate(u)},
                    {t[3].p.evaluate(u), t[3].d1.evaluate(u), t[3].d2.evaluate(u), t[3].d3.evaluate(u)}};
                for (int j = 0; j < kJoints; ++j) {
                    const AxisWeights& a = w[s][j];
                    const double q    = b[0][0] * a.q0 + b[1][0] * a.hm0 + b[2][0] * a.q1 + b[3][0] * a.hm1;
                    const double qd   = b[0][1] * a.q0 + b[1][1] * a.hm0 + b[2][1] * a.q1 + b[3][1] * a.hm1;
                    const double qdd  = b[0][2] * a.q0 + b[1][2] * a.hm0 + b[2][2] * a.q1 + b[3][2] * a.hm1;
                    const double qddd = b[0][3] * a.q0 + b[1][3] * a.hm0 + b[2][3] * a.q1 + b[3][3] * a.hm1;
                    checksum += q + 0.1 * qd + 0.01 * qdd + 0.001 * qddd;
                    maxAbs = std::max(maxAbs, std::abs(q) + std::abs(qd));
                }
            }
        }
    }
    *maxAbsOut = maxAbs;
    return checksum;
}

// =====================================================================
// evalderivs——被否决形态（逐轴装配段多项式＋每采样每轴 evaluateDerivatives）
// =====================================================================

double runEvalDerivs(const std::array<std::array<AxisWeights, kJoints>, kSpans>& w,
                     bool septic, double* maxAbsOut)
{
    const std::array<BasisFamily, 4>& t = tableBases(septic);
    double checksum = 0.0;
    double maxAbs = 0.0;
    for (int iter = 0; iter < kIterations; ++iter) {
        for (int s = 0; s < kSpans; ++s) {
            // 段构建期装配（每轴）：c(u)=Σ基系数·权重——上游代数逐轴装配。
            std::array<rw::math::Polynomial<>, kJoints> axisPoly{
                {rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0)),
                 rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0)),
                 rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0)),
                 rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0)),
                 rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0)),
                 rw::math::Polynomial<>(std::vector<double>(septic ? 8u : 6u, 0.0))}};
            const std::size_t deg = septic ? 7u : 5u;
            // 系数装配（升幂对齐——短基高位补 0）。
            for (int j = 0; j < kJoints; ++j) {
                const AxisWeights& a = w[s][j];
                for (std::size_t c = 0; c <= deg; ++c) {
                    const double b0 = c <= t[0].p.order() ? t[0].p[c] : 0.0;
                    const double b1 = c <= t[1].p.order() ? t[1].p[c] : 0.0;
                    const double b2 = c <= t[2].p.order() ? t[2].p[c] : 0.0;
                    const double b3 = c <= t[3].p.order() ? t[3].p[c] : 0.0;
                    axisPoly[j][c] = b0 * a.q0 + b1 * a.hm0 + b2 * a.q1 + b3 * a.hm1;
                }
            }
            // 每采样每轴一次 evaluateDerivatives（返回 vector——分配面）。
            for (int k = 0; k < kSamplesPerSpan; ++k) {
                const double u = static_cast<double>(k) / kSamplesPerSpan;
                for (int j = 0; j < kJoints; ++j) {
                    const std::vector<double> d = axisPoly[j].evaluateDerivatives(u, 3);
                    checksum += d[0] + 0.1 * d[1] + 0.01 * d[2] + 0.001 * d[3];
                    maxAbs = std::max(maxAbs, std::abs(d[0]) + std::abs(d[1]));
                }
            }
        }
    }
    *maxAbsOut = maxAbs;
    return checksum;
}

// =====================================================================
// 计时与报告
// =====================================================================

template <typename Fn>
double timeBestOf(Fn&& fn, std::size_t& allocsOut)
{
    double bestMs = 1e300;
    for (int r = 0; r < 7; ++r) {
        const std::size_t before = g_allocCount;
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        const auto t1 = std::chrono::steady_clock::now();
        const double ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        bestMs = std::min(bestMs, ms);
        allocsOut = g_allocCount - before;  // 末轮分配数（各轮应相同）
    }
    return bestMs;
}

}  // namespace

int main()
{
    const auto weights = makeWeights();
    std::printf("scenario: %d spans x %d samples x %d joints x %d iterations\n",
                kSpans, kSamplesPerSpan, kJoints, kIterations);
    std::printf("compiler: MSVC /O2 /DNDEBUG /std:c++17\n\n");

    for (int septicInt = 0; septicInt <= 1; ++septicInt) {
        const bool septic = septicInt != 0;
        std::printf("== %s ==\n", septic ? "septic" : "quintic");

        double oldMax = 0.0;
        double newMax = 0.0;
        double edMax = 0.0;

        std::size_t oldAllocs = 0;
        std::size_t newAllocs = 0;
        std::size_t edAllocs = 0;

        const double oldCk = runOldHand(weights, septic, &oldMax);
        const double newCk = runUpstreamEval(weights, septic, &newMax);
        const double edCk = runEvalDerivs(weights, septic, &edMax);

        // 位级确定性：各形态复跑一次，校验和位级一致。
        const double oldCk2 = runOldHand(weights, septic, &oldMax);
        const double newCk2 = runUpstreamEval(weights, septic, &newMax);
        std::printf("determinism(old rerun bit-equal): %s\n",
                    oldCk == oldCk2 ? "yes" : "NO");
        std::printf("determinism(new rerun bit-equal): %s\n",
                    newCk == newCk2 ? "yes" : "NO");
        // 新旧形态漂移（不同求值式的固有舍入差——非位级目标）。
        std::printf("old-vs-new checksum rel-drift: %.3e\n",
                    std::abs(oldCk - newCk) / std::abs(oldCk));

        const double oldMs =
            timeBestOf([&] { runOldHand(weights, septic, &oldMax); }, oldAllocs);
        const double newMs = timeBestOf(
            [&] { runUpstreamEval(weights, septic, &newMax); }, newAllocs);
        const double edMs = timeBestOf(
            [&] { runEvalDerivs(weights, septic, &edMax); }, edAllocs);

        std::printf("old-hand      : %8.3f ms  allocs/scene=%zu\n", oldMs, oldAllocs);
        std::printf("upstream-eval : %8.3f ms  allocs/scene=%zu  (vs old %+.1f%%)\n",
                    newMs, newAllocs, (newMs / oldMs - 1.0) * 100.0);
        std::printf("evalderivs    : %8.3f ms  allocs/scene=%zu  (vs new %+.1f%%)\n",
                    edMs, edAllocs, (edMs / newMs - 1.0) * 100.0);
        (void) edCk;
        std::printf("\n");
    }
    return 0;
}
