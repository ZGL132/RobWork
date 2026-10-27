/**
 * @file CoreIdentitySuitabilityTest.cpp
 * @brief REUSE-T01：身份哈希契约反例与上游事件原语适用性探测。
 *
 * 设计依据：ARC-04、units/core.md §4.1 的 FNV-1a 128 约定及 §5.8 事件端口。
 * 直接链接既有 core/testkit/上游目标；仅使用固定独立向量，不补写生产哈希或事件总线。
 * 本文件单线程执行，无共享模型、Qt 或新的产品接口；已知反例应真实失败并保留报告。
 */
#include <gtest/gtest.h>
#include <rw/core/Event.hpp>
#include <sdurws/ird/core/Identity.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

// ARC-04：空输入应返回固定偏移基；这是对照，不能以此证明非空输入乘法正确。
TEST(ReuseCoreAudit, FnvEmptyMatchesOffset_CORE_T02) {
    std::uint64_t hi = 0, lo = 0;
    sdurws::ird::core::detail::fnv1a128(nullptr, 0, &hi, &lo);
    EXPECT_EQ(0x6c62272e07bb0142ULL, hi);
    EXPECT_EQ(0x62b821756295c58dULL, lo);
}

// CORE-T02：向量由独立整数算术生成并冻结；不与产品内部另一份实现互证。
TEST(ReuseCoreAudit, FnvNonemptyMatchesFixedVectors_CORE_T02) {
    const std::array<std::uint8_t, 1> one{{0x61}};
    const std::array<std::uint8_t, 16> zero{};
    const std::array<std::uint8_t, 16> sequence{{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}};
    std::uint64_t hi = 0, lo = 0;
    sdurws::ird::core::detail::fnv1a128(one.data(), one.size(), &hi, &lo);
    EXPECT_EQ(0xd228cb696f1a8cafULL, hi);
    EXPECT_EQ(0x78912b704e4a8964ULL, lo);
    sdurws::ird::core::detail::fnv1a128(zero.data(), zero.size(), &hi, &lo);
    EXPECT_EQ(0xf1f90b7bf705b5efULL, hi);
    EXPECT_EQ(0x1517163834145e4dULL, lo);
    sdurws::ird::core::detail::fnv1a128(sequence.data(), sequence.size(), &hi, &lo);
    EXPECT_EQ(0x2f57eaee93cff23fULL, hi);
    EXPECT_EQ(0x8177f0c93cb989fdULL, lo);
}

// ARC-04：确认偏差确实进入公开 std::hash，不将内部helper异常夸大为内容SHA失效。
TEST(ReuseCoreAudit, PublicHashConsumesFnvResult_CORE_T02) {
    using sdurws::ird::core::ObjectId;
    const auto id = ObjectId::fromCanonical("obj-000102030405060708090a0b0c0d0e0f");
    EXPECT_EQ(static_cast<std::size_t>(0xae201a27af767bc2ULL), std::hash<ObjectId>{}(id));
}

// CORE-T08：实际使用上游Event验证订阅序、按对象/编号退订及幂等；不搭建新总线。
TEST(ReuseCoreAudit, UpstreamEventFifoAndRemoval_CORE_T08) {
    rw::core::Event<std::function<void(int)>, int> event;
    int firstOwner = 0, secondOwner = 0;
    std::vector<int> observed;
    event.add([&](int value) { observed.push_back(value * 10 + 1); }, &firstOwner, 1);
    event.add([&](int value) { observed.push_back(value * 10 + 2); }, &secondOwner, 2);
    event.add([&](int value) { observed.push_back(value * 10 + 3); }, &firstOwner, 2);
    event.fire(7);
    EXPECT_EQ((std::vector<int>{71,72,73}), observed);
    event.remove(&firstOwner, 1);
    observed.clear();
    event.fire(8);
    EXPECT_EQ((std::vector<int>{82,83}), observed);
    event.remove(&firstOwner);
    event.remove(&firstOwner); // 重复退订应为no-op；生命周期仍由未来宿主适配负责。
    EXPECT_EQ(1U, event.getListenerList().size());
}
