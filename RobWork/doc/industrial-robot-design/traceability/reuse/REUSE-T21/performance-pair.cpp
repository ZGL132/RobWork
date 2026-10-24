/**
 * @file performance-pair.cpp
 * @brief 同驱动、同轮交替测量旧/新生产C4；独立翻译单元且不启用LTO。
 *
 * 旧函数来自起点Git对象，仅重命名符号以同进程对照；不进入产品或增加回退。
 */
#include <sdurws/ird/core/Compare.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>

namespace sdurws::ird::core {
bool oldCloseWithin(double, double, Tolerance) noexcept;
}

int main()
{
    using namespace sdurws::ird::core;
    volatile double normal = 1.0001;
    volatile double maximum = std::numeric_limits<double>::max();
    volatile unsigned checksum = 0;
    // 旧溢出行为是错误对照，计时不能代表同等正确算法之间的性能比较。
    for (int round = 0; round < 3; ++round) {
        for (int overflow = 0; overflow < 2; ++overflow) {
            for (int current = 0; current < 2; ++current) {
                const auto call = current ? closeWithin : oldCloseWithin;
                const int loops = overflow ? 20000 : 5000000;
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < loops; ++i) {
                    checksum += overflow
                        ? call(maximum, -maximum, Tolerance{1.5, 0.0})
                        : call(normal, 1.0, Tolerance{0.01, 0.001});
                }
                std::cout << "round=" << round << " current=" << current
                          << " overflow=" << overflow << " loops=" << loops
                          << " us/call=" << std::setprecision(9)
                          << std::chrono::duration<double, std::micro>(
                                 std::chrono::steady_clock::now()-start).count()/loops
                          << '\n';
            }
        }
    }
    std::cerr << "checksum=" << checksum << '\n';
    return checksum == 30060000 ? 0 : 1;
}
