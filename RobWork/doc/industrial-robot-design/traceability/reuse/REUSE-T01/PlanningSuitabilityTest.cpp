/**
 * @file PlanningSuitabilityTest.cpp
 * @brief REUSE-T01：规划原语与积分候选适用性探测；不替换产品入口。
 * 设计依据：TRJ-03、DYN-03、NFR-COR-02；只消费原公开API和现有审计目标。
 * 线程约束：含进程全局随机源探测，仅本可执行文件单线程运行。
 */
#include <gtest/gtest.h>
#include <rw/math/Math.hpp>
#include <rw/math/MetricFactory.hpp>
#include <rw/pathplanning/PathAnalyzer.hpp>
#include <rw/pathplanning/QSampler.hpp>
#include <rw/pathplanning/StopCriteria.hpp>
#include <rw/trajectory/Path.hpp>
#include <rwlibs/softbody/numerics/TrapMethod.hpp>
#include <cstdint>

// TRJ-03：路径长度为逐轴绝对差之和；不能因规划器用L2就把选择键改成L2。
TEST(ReusePlanningAudit, ManhattanPathLengthPreservesSelectionMeaning) {
    using rw::math::Q;
    const auto l1 = rw::math::MetricFactory::makeManhattan<Q>();
    const auto linf = rw::math::MetricFactory::makeInfinity<Q>();
    const auto l2 = rw::math::MetricFactory::makeEuclidean<Q>();
    rw::trajectory::QPath path;
    path.push_back(Q(2, 0.0, 0.0));
    path.push_back(Q(2, 3.0, 4.0));
    path.push_back(Q(2, 2.0, 6.0));
    EXPECT_DOUBLE_EQ(10.0, rw::pathplanning::PathAnalyzer::pathLength(path.begin(), path.end(), *l1));
    EXPECT_DOUBLE_EQ(4.0, linf->distance(path[0], path[1]));
    EXPECT_DOUBLE_EQ(5.0, l2->distance(path[0], path[1]));
    EXPECT_DOUBLE_EQ(0.0, rw::pathplanning::PathAnalyzer::pathLength(path.begin(), path.begin(), *l1));
    EXPECT_DOUBLE_EQ(0.0, rw::pathplanning::PathAnalyzer::pathLength(path.begin(), path.begin()+1, *l1));
}

// TRJ-03：现有停止接口可组合取消与预算，无须发明另一套规划器循环。
TEST(ReusePlanningAudit, StopCriteriaDelegatesCancellation) {
    bool cancel = false;
    int observations = 0;
    const auto callback = rw::pathplanning::StopCriteria::stopByFun([&] { ++observations; return cancel; });
    const auto combined = rw::pathplanning::StopCriteria::stopEither(
        rw::pathplanning::StopCriteria::stopNever(), callback);
    EXPECT_FALSE(combined->stop());
    cancel = true;
    EXPECT_TRUE(combined->stop());
    EXPECT_EQ(2, observations);
}

// NFR-COR-02：两个sampler共享全局源，重播种会影响已构造sampler；不是任务私有流。
TEST(ReusePlanningAudit, UniformSamplerSharesSeededProcessSource) {
    const rw::models::Device::QBox box{rw::math::Q(2, -1.0, -1.0), rw::math::Q(2, 1.0, 1.0)};
    const auto first = rw::pathplanning::QSampler::makeUniform(box);
    const auto second = rw::pathplanning::QSampler::makeUniform(box);
    rw::math::Math::seed(17U);
    const auto initial = first->sample();
    const auto next = first->sample();
    rw::math::Math::seed(17U);
    const auto replay = second->sample();
    const auto sharedNext = first->sample();
    for (std::size_t i=0; i<2; ++i) {
        EXPECT_DOUBLE_EQ(initial[i], replay[i]);
        EXPECT_DOUBLE_EQ(next[i], sharedNext[i]);
    }
    const std::uint64_t highSeed = (std::uint64_t{1} << 32U) + 17U;
    rw::math::Math::seed(static_cast<unsigned>(highSeed));
    const auto truncated = first->sample();
    for (std::size_t i=0; i<2; ++i) EXPECT_DOUBLE_EQ(initial[i], truncated[i]);
}

// DYN-03：已存在梯形原语；M=2可逐区间消费，整序列却只能输入一个固定h。
// 这里只核两个正功率区间；业务缺口/正负分项/身份/峰值规则仍由产品契约决定。
TEST(ReusePlanningAudit, TrapezoidPrimitiveAllowsPerIntervalSteps) {
    double firstValues[2]{2.0, 4.0};
    double secondValues[2]{4.0, 8.0};
    auto first = [&](int i) { return firstValues[i]; };
    auto second = [&](int i) { return secondValues[i]; };
    EXPECT_DOUBLE_EQ(3.0, rwlibs::softbody::TrapMethod::trapezMethod(first, 2, 1.0));
    EXPECT_DOUBLE_EQ(12.0, rwlibs::softbody::TrapMethod::trapezMethod(second, 2, 2.0));
    EXPECT_DOUBLE_EQ(15.0, rwlibs::softbody::TrapMethod::trapezMethod(first, 2, 1.0)
                            + rwlibs::softbody::TrapMethod::trapezMethod(second, 2, 2.0));
}
