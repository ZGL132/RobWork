/**
 * @file production-probe.cpp
 * @brief 真实生产C4协议/分配与计时探针，仅作为验证可执行文件。
 *
 * 复用R3的全局分配计数器与原语配置，但测试调用只指向当前core::closeWithin。
 * 不将实验candidate的结果作为产品结果，不将测试代码注册到产品库。
 */
#define main unusedR3ProtocolMain
#include "../REUSE-T14/revision-r3/probe.cpp"
#undef main

int main(int argc, char**)
{
    if (argc > 1) {
        volatile double normal = 1.0001;
        volatile double maximum = std::numeric_limits<double>::max();
        volatile unsigned count = 0;
        for (int round = 0; round < 3; ++round) {
            for (int mode = 0; mode < 2; ++mode) {
                const int loops = mode ? 20000 : 1000000;
                const auto start = std::chrono::steady_clock::now();
                countingAllocations = true;
                for (int i = 0; i < loops; ++i) {
                    count += mode ? closeWithin(maximum, -maximum, Tolerance{1.5, 0.0})
                                  : closeWithin(normal, 1.0, Tolerance{0.01, 0.001});
                }
                countingAllocations = false;
                std::cout << "production-" << (mode ? "overflow" : "normal")
                          << " loops=" << loops << " us/call=" << std::setprecision(8)
                          << std::chrono::duration<double, std::micro>(
                                 std::chrono::steady_clock::now()-start).count()/loops << '\n';
            }
        }
        std::cerr << "checksum=" << count << " production-new-calls=" << measuredAllocations << '\n';
        return count == 3000000 && measuredAllocations == 0 ? 0 : 1;
    }
    std::uint64_t bits[4];
    while (std::cin >> std::hex >> bits[0] >> bits[1] >> bits[2] >> bits[3]) {
        double x[4];
        std::memcpy(x, bits, sizeof(x));
        countingAllocations = true;
        const bool result = closeWithin(x[0], x[1], Tolerance{x[2], x[3]});
        countingAllocations = false;
        std::cout << result << '\n';
    }
    std::cerr << "production-new-calls=" << measuredAllocations << '\n';
    return std::cin.eof() && measuredAllocations == 0 ? 0 : 2;
}
