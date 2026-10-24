/**
 * @file probe.cpp
 * @brief C4 成熟数值原语的隔离适用性探针；不注册产品目标，不构成生产批准。
 *
 * 正常路径直接调用当前生产 closeWithin，避免复制业务核心；候选只在合法有限
 * tolerance 且差值/限值同时溢出时，用既有 Boost 固定精度浮点表达原公式。
 * 4224 位的范围推导、依赖/异常风险及适用边界见同目录 README。
 */
#include <sdurws/ird/core/Compare.hpp>
#include <boost/multiprecision/cpp_bin_float.hpp>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <string>

using sdurws::ird::core::Tolerance;
using sdurws::ird::core::closeWithin;
// 仅实验计数：检查候选计算期间是否调用 C++ 动态分配；不进入拟议产品适配。
bool countingAllocations = false;
std::size_t measuredAllocations = 0;
void* operator new(std::size_t size)
{
    if (countingAllocations) ++measuredAllocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

using Product = boost::multiprecision::number<
    boost::multiprecision::cpp_bin_float<128,
        boost::multiprecision::digit_base_2, void>,
    boost::multiprecision::et_off>;
using Wide = boost::multiprecision::number<
    boost::multiprecision::cpp_bin_float<4224,
        boost::multiprecision::digit_base_2, void>,
    boost::multiprecision::et_off>;
static_assert(std::numeric_limits<Wide>::digits == 4224, "binary precision");

/** @brief 仅对合法有限输入评价库侧高精度公式；异常保留，由探针主函数报告。 */
bool wideFormula(double v, double r, Tolerance t)
{
    // binary64 乘积至多106位；先用128位库乘法，再无损扩大以精确相加。
    // 避免4224位乘法进入Boost的Karatsuba临时堆存储，不修改库阈值或实现。
    countingAllocations = true;
    const Product product = Product(t.relative) * Product(std::fabs(r));
    const Wide value(v), reference(r), absolute(t.absolute);
    const Wide difference = boost::multiprecision::abs(value - reference);
    const Wide limit = Wide(product) + absolute;
    const bool result = difference <= limit;
    countingAllocations = false;
    return result;
}

/** @brief 分支适配候选；本实验刻意不标 noexcept，不能以其通过证明生产异常契约。 */
bool candidate(double v, double r, Tolerance t)
{
    if (std::isfinite(v) && std::isfinite(r)
        && std::isfinite(t.relative) && std::isfinite(t.absolute)
        && t.relative >= 0.0 && t.absolute >= 0.0
        && std::isinf(v - r)
        && std::isinf(t.relative * std::fabs(r) + t.absolute)) {
        return wideFormula(v, r, t);
    }
    // 未获批准的普通舍入、非法聚合 tolerance 和非有限输入语义不在本实验改动范围。
    return closeWithin(v, r, t);
}

/** @brief 位模式协议输出生产/候选/库公式三路结果；--bench 仅测独立调用成本。 */
int main(int argc, char** argv)
{
    try {
        if (argc > 1 && std::string(argv[1]) == "--bench") {
            volatile double normal = 1.0001;
            volatile double extreme = std::numeric_limits<double>::max();
            volatile unsigned count = 0;
            auto measure = [&](const char* label, int loops, auto call) {
                const auto begin = std::chrono::steady_clock::now();
                for (int i = 0; i < loops; ++i) count += call();
                const double us = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - begin).count() / loops;
                std::cout << label << " loops=" << loops << " us/call="
                          << std::setprecision(8) << us << '\n';
            };
            for (int round = 0; round < 3; ++round) {
                measure("production-normal", 1000000, [&] {
                    return closeWithin(normal, 1.0, Tolerance{0.01, 0.001}); });
                measure("candidate-normal-delegated", 1000000, [&] {
                    return candidate(normal, 1.0, Tolerance{0.01, 0.001}); });
                measure("production-overflow-incorrect", 20000, [&] {
                    return closeWithin(extreme, -extreme, Tolerance{1.5, 0.0}); });
                measure("candidate-overflow", 20000, [&] {
                    return candidate(extreme, -extreme, Tolerance{1.5, 0.0}); });
            }
            std::cerr << "checksum=" << count << " candidate-new-calls="
                      << measuredAllocations << '\n';
            return 0;
        }
        std::uint64_t bits[4];
        while (std::cin >> std::hex >> bits[0] >> bits[1] >> bits[2] >> bits[3]) {
            double x[4];
            std::memcpy(x, bits, sizeof(x));
            const Tolerance t{x[2], x[3]};
            const bool valid = std::isfinite(x[0]) && std::isfinite(x[1])
                && std::isfinite(t.relative) && std::isfinite(t.absolute)
                && t.relative >= 0.0 && t.absolute >= 0.0;
            std::cout << closeWithin(x[0], x[1], t) << ' '
                      << candidate(x[0], x[1], t) << ' '
                      << (valid ? int(wideFormula(x[0], x[1], t)) : -1) << '\n';
        }
        std::cerr << "candidate-new-calls=" << measuredAllocations << '\n';
        return std::cin.eof() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "probe exception: " << error.what() << '\n';
        return 3;
    }
}
