// REUSE-T27：仅隔离比较旧/新精确函数；旧核心不进入生产目标。
#include <sdurws/ird/modeling/RobotDesign.hpp>
#include <array>
#include <chrono>
#include <cstdio>
#include <cmath>
using sdurws::ird::modeling::InertiaTensor;
namespace t27old { std::array<double,3> symmetricEigenvalues3x3(const InertiaTensor&) noexcept; }
namespace t27new { std::array<double,3> symmetricEigenvalues3x3(const InertiaTensor&); }
int main()
{
    using F=std::array<double,3>(*)(const InertiaTensor&);
    const F calls[]={t27old::symmetricEigenvalues3x3,t27new::symmetricEigenvalues3x3};
    constexpr int count=100000;
    for(int round=0;round<3;++round) {
        for(int scenario=0;scenario<4;++scenario) {
            const double scale=scenario==2?1e-300:scenario==3?1e300:1.0;
            InertiaTensor t;t.ixx=4*scale;t.iyy=4*scale;t.izz=4*scale;
            t.ixy=scenario==0?0:scale; // 各向同性、一般非对角、极小、极大。
            for(int turn=0;turn<2;++turn) {
                const int variant=(round+turn)%2;
                volatile double sum=0;
                const auto start=std::chrono::steady_clock::now();
                for(int i=0;i<count;++i) {
                    // 对角加微扰防止重复输入退化为编译期常量；已知谱始终有裕量。
                    t.izz=(4.0+(i%17)*0.0001)*scale;
                    const auto result=calls[variant](t);
                    sum+=result[0]/scale;
                }
                const double us=std::chrono::duration<double,std::micro>(
                    std::chrono::steady_clock::now()-start).count()/count;
                std::printf("round=%d scenario=%d variant=%s us=%.9f checksum=%.9f\n",
                    round,scenario,variant?"new":"old",us,double(sum));
                if(variant && !std::isfinite(double(sum)))return 1;
            }
        }
    }
}
