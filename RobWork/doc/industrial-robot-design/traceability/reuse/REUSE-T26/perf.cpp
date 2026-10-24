// REUSE-T26：仅用于同轮比较精确提取的旧/新插值入口；不进入产品目标。
#include <sdurws/ird/trajectory/CartesianLine.hpp>
#include <rw/math/RPY.hpp>
#include <chrono>
#include <cstdio>
#include <cmath>
namespace sdurws::ird::benchold {
rw::math::Transform3D<double> interpolateCartesianLine(
    const rw::math::Transform3D<double>&, const rw::math::Transform3D<double>&, double);
}
namespace sdurws::ird::benchnew {
rw::math::Transform3D<double> interpolateCartesianLine(
    const rw::math::Transform3D<double>&, const rw::math::Transform3D<double>&, double);
}
int main()
{
    using T = rw::math::Transform3D<double>;
    using F = T (*)(const T&, const T&, double);
    const F entries[] = {sdurws::ird::benchold::interpolateCartesianLine,
                         sdurws::ird::benchnew::interpolateCartesianLine};
    const double angles[] = {0, 1e-8, 0.7, 3.13}; // 同姿态、微小角、一般角、接近半周。
    constexpr int count = 200000;
    for (int round = 0; round < 3; ++round) {
        for (int scenario = 0; scenario < 4; ++scenario) {
            const T a({1, 2, 3}, rw::math::RPY<double>(0.1, 0.2, -0.3).toRotation3D());
            const T b({4, -2, 1}, a.R()*rw::math::RPY<double>(angles[scenario], 0, 0).toRotation3D());
            double checks[2]{};
            for (int turn = 0; turn < 2; ++turn) {
                const int variant = (turn + round) % 2;
                volatile double sum = 0;
                const auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < count; ++i) {
                    const T out = entries[variant](a, b, (i % 1024) / 1023.0);
                    sum += out.R()(0, 1) + out.P()[0];
                }
                const double us = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now()-start).count()/count;
                checks[variant] = sum;
                std::printf("round=%d scenario=%d variant=%s us=%.9f checksum=%.9f\n",
                    round, scenario, variant ? "new" : "old", us, double(sum));
            }
            if (!std::isfinite(checks[0]) || std::fabs(checks[0]-checks[1]) > 1e-7) {
                return 1; // 探针观测防御，不代替独立旋转解析 oracle。
            }
        }
    }
}
