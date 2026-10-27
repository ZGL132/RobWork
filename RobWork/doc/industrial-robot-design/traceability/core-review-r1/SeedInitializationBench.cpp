/**
 * @file SeedInitializationBench.cpp
 * @brief CORE-T12隔离微基准：比较原单字播种与实际私有播种入口的初始化成本。
 * 不接入产品构建，不是吞吐预算验收；固定熵源排除操作系统熵源/线程创建噪声。
 */
#include "IdentityRandom.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>

namespace {
/// 仅供计时的确定性32位源；每次初始化使用不同输入，不宣称密码学随机性。
struct BenchEntropy {
    using result_type = std::uint32_t;
    result_type next;
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return UINT32_MAX; }
    result_type operator()() { return next++ * 2654435761u; }
};

volatile std::uint64_t observed = 0;
constexpr int iterations = 20000;

/** @brief 计时初始化和前两个输出，纳秒/引擎；保留可观察结果以防优化消除。 */
template<class Factory>
double measure(Factory factory)
{
    std::uint64_t sum = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        BenchEntropy entropy{static_cast<std::uint32_t>(i * 4)};
        auto engine = factory(entropy);
        sum ^= engine();
        sum ^= engine();
    }
    observed = sum;
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    return std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
}
}

/** @brief 交替先后顺序，报告七轮中位数；不将一次计时写成性能PASS。 */
int main()
{
    const auto old = [](BenchEntropy& entropy) { return std::mt19937_64{entropy()}; };
    const auto current = [](BenchEntropy& entropy) {
        return sdurws::ird::core::detail::makeIdentityEngine(entropy);
    };
    measure(old);
    measure(current);
    std::array<double, 7> before{}, after{};
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (i % 2 == 0) { before[i] = measure(old); after[i] = measure(current); }
        else { after[i] = measure(current); before[i] = measure(old); }
    }
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    std::cout << std::fixed << std::setprecision(1)
              << "iterations_per_round=" << iterations << " rounds=7\n"
              << "old_single_word_ns=" << before[3] << '\n'
              << "current_seed_seq_ns=" << after[3] << '\n'
              << "ratio=" << after[3] / before[3] << '\n'
              << "scope=initialization_and_first_two_outputs; fixed_entropy; "
                 "not_random_device_or_thread_cost; no_budget_verdict\n";
}
