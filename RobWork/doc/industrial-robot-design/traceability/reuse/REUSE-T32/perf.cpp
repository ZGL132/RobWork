// REUSE-T32 性能微基准（历史对照，不进任何产品/测试目标）：Planner 三处
// 度量换核的旧手写循环 vs 新上游 MetricFactory/PathAnalyzer 消费，同输入
// 同轮交替计时。仅摘录算式本身（不含 policy/RRT 装配），不代表整机规划性能。
#include <rw/math/Q.hpp>
#include <rw/math/MetricFactory.hpp>
#include <rw/pathplanning/PathAnalyzer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
using rw::math::Metric;
using rw::math::Q;
using QMetric = rw::math::Metric<rw::math::Q>;
using Path = std::vector<Q>;

Q makeQ(std::size_t dof, unsigned seed)
{
    Q q(dof);
    for (std::size_t i = 0; i < dof; ++i) {
        const double t = static_cast<double>((seed * 2654435761u + i * 40503u) % 1000);
        q[i] = (t / 500.0 - 1.0) * 3.0;
    }
    return q;
}

Path makePath(std::size_t dof, std::size_t nodes, unsigned seed)
{
    Path path;
    path.reserve(nodes);
    for (std::size_t n = 0; n < nodes; ++n) {
        path.push_back(makeQ(dof, static_cast<unsigned>(seed + n * 7u)));
    }
    return path;
}

double sink = 0.0;

// ---- 旧实现（逐字摘自基线 6921197dd/c65ffc4fc 的手写循环）----
double oldMaxAdjacentStep(const Path& path)
{
    double maxStep = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        for (std::size_t j = 0; j < path[i].size(); ++j) {
            maxStep = std::max(maxStep, std::abs(path[i][j] - path[i - 1][j]));
        }
    }
    return maxStep;
}
double oldPathJointLength(const Path& path)
{
    double length = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        for (std::size_t j = 0; j < path[i].size(); ++j) {
            length += std::abs(path[i][j] - path[i - 1][j]);
        }
    }
    return length;
}
double oldSampleL2(const Q& a, const Q& b)
{
    double length = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double delta = b[i] - a[i];
        length += delta * delta;
    }
    return std::sqrt(length);
}

// ---- 新实现（与生产 Planner.cpp 逐字同构）----
double newMaxAdjacentStep(const Path& path, const QMetric::Ptr& infinity)
{
    double maxStep = 0.0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        maxStep = std::max(maxStep, infinity->distance(path[i], path[i - 1]));
    }
    return maxStep;
}
double newPathJointLength(const Path& path, const QMetric::Ptr& manhattan)
{
    return rw::pathplanning::PathAnalyzer::pathLength(path.begin(), path.end(), *manhattan);
}
double newSampleL2(const Q& a, const Q& b, const QMetric::Ptr& euclidean)
{
    return euclidean->distance(a, b);
}

template <typename Fn>
double bench(Fn&& fn, int iterations)
{
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        sink += fn();
    }
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::micro>(stop - start).count()
           / static_cast<double>(iterations);
}
}  // namespace

int main()
{
    const QMetric::Ptr infinity = rw::math::MetricFactory::makeInfinity<Q>();
    const QMetric::Ptr manhattan = rw::math::MetricFactory::makeManhattan<Q>();
    const QMetric::Ptr euclidean = rw::math::MetricFactory::makeEuclidean<Q>();
    const Path p6 = makePath(6, 50, 1);       // 50 节点 6 轴（典型候选路径）
    const Path p6s = makePath(6, 4, 2);       // 4 节点 6 轴（短候选）
    const Q qa = makeQ(6, 3);
    const Q qb = makeQ(6, 4);
    const int iters = 200000;
    const char* unit = "us/op";

    // 一致性自检：maxStep（max 折叠）与 L2（单对）逐位一致；L1 为登记在案
    // 的求和序变化（逐对先归约 vs 单累加器交错）——打印实测相对差并断言
    // 在末位舍入量级（≤1e-12 相对）。
    if (oldMaxAdjacentStep(p6) != newMaxAdjacentStep(p6, infinity)
        || oldSampleL2(qa, qb) != newSampleL2(qa, qb, euclidean)) {
        std::printf("MISMATCH: maxStep/L2 数值不一致——基准无效\n");
        return 2;
    }
    {
        const double o = oldPathJointLength(p6);
        const double n = newPathJointLength(p6, manhattan);
        const double rel = std::fabs(o - n) / std::fabs(o);
        std::printf("L1 fold-order relative delta (50n path): %.3g\n", rel);
        if (rel > 1e-12) {
            std::printf("MISMATCH: L1 相对差超 1e-12——求和序变化超末位舍入量级，须重审\n");
            return 2;
        }
    }

    for (int round = 1; round <= 3; ++round) {
        const double oStep = bench([&] { return oldMaxAdjacentStep(p6); }, iters);
        const double nStep = bench([&] { return newMaxAdjacentStep(p6, infinity); }, iters);
        const double oLen = bench([&] { return oldPathJointLength(p6); }, iters);
        const double nLen = bench([&] { return newPathJointLength(p6, manhattan); }, iters);
        const double oL2 = bench([&] { return oldSampleL2(qa, qb); }, iters);
        const double nL2 = bench([&] { return newSampleL2(qa, qb, euclidean); }, iters);
        const double oStepS = bench([&] { return oldMaxAdjacentStep(p6s); }, iters);
        const double nStepS = bench([&] { return newMaxAdjacentStep(p6s, infinity); }, iters);
        std::printf(
            "round%d maxStep(50n) old=%.4f new=%.4f (%+.1f%%) | pathL1(50n) old=%.4f new=%.4f (%+.1f%%) | L2(1pair) old=%.4f new=%.4f (%+.1f%%) | maxStep(4n) old=%.4f new=%.4f (%+.1f%%) %s\n",
            round, oStep, nStep, (nStep - oStep) / oStep * 100.0,
            oLen, nLen, (nLen - oLen) / oLen * 100.0,
            oL2, nL2, (nL2 - oL2) / oL2 * 100.0,
            oStepS, nStepS, (nStepS - oStepS) / oStepS * 100.0, unit);
    }
    std::printf("sink=%g\n", sink);
    return 0;
}
