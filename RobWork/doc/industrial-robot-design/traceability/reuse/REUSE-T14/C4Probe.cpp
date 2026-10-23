/** @file C4Probe.cpp @brief GAP-CORE-C4-01 真实生产入口的位模式探测与计时，不作oracle。 */
#include <sdurws/ird/core/Compare.hpp>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <cstdint>
#include <chrono>
int main(int argc, char**) {
    using namespace sdurws::ird::core;
    if (argc>1) {
        volatile double v=1.0001, r=1.0; volatile unsigned count=0;
        const auto start=std::chrono::steady_clock::now();
        for (int i=0;i<2000000;++i) count+=closeWithin(v,r,Tolerance{0.01,0.001});
        std::cout << std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count() << ' ' << count << '\n';
        return 0;
    }
    std::uint64_t bits[4];
    while (std::cin >> std::hex >> bits[0] >> bits[1] >> bits[2] >> bits[3]) {
        double x[4]; std::memcpy(x,bits,sizeof(x));
        std::cout << closeWithin(x[0],x[1],Tolerance{x[2],x[3]}) << '\n';
    }
}
