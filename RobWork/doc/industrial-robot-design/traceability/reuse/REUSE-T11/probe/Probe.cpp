/**
 * @file Probe.cpp
 * @brief REUSE-T11/MDL-22：独立ZYX/Rodrigues参考验证参数顺序、角度主值及边界。
 * 仅测试消费者，不接入生产；上游12例和EAA原库网格沿用REUSE-T10。
 * 无共享模型或State，测试单线程，单位rad，参考系同输入。
 */
#include "RpyMath.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace rw::math;
namespace subject = sdurws::ird::modeling::rpymath;
constexpr double pi = 3.14159265358979323846;

/** @brief 独立ZYX参考；输入roll/pitch/yaw（rad），返回无量纲旋转；纯值无异常。 */
Rotation3D<> reference(const Vector3D<>& rpy)
{
    const double cr=std::cos(rpy[0]),sr=std::sin(rpy[0]);
    const double cp=std::cos(rpy[1]),sp=std::sin(rpy[1]);
    const double cy=std::cos(rpy[2]),sy=std::sin(rpy[2]);
    return {cy*cp,cy*sp*sr-sy*cr,cy*sp*cr+sy*sr,
            sy*cp,sy*sp*sr+cy*cr,sy*sp*cr-cy*sr,-sp,cp*sr,cp*cr};
}

/** @brief 用独立Rodrigues回构轴角矢量（rad）；精确零返回I，不复用被测EAA转换。 */
Rotation3D<> reconstruct(const Vector3D<>& v)
{
    const double angle=std::hypot(v[0],v[1],v[2]);
    if(angle==0) return Rotation3D<>::identity();
    const double x=v[0]/angle,y=v[1]/angle,z=v[2]/angle;
    const double c=std::cos(angle),s=std::sin(angle),d=1-c;
    return {c+x*x*d,x*y*d-z*s,x*z*d+y*s,
            y*x*d+z*s,c+y*y*d,y*z*d-x*s,
            z*x*d-y*s,z*y*d+x*s,c+z*z*d};
}

/** @brief MDL-22/AT-37：检查零、微小角、pi邻域、万向锁与超一周输入的矩阵语义。 */
TEST(RpyReuse, IndependentBoundaryOracle_MDL22_AT37)
{
    std::vector<Vector3D<>> inputs;
    for(double a: {0.,1e-14,1e-9,0.999999e-6,1e-6,1.000001e-6,3.7e-6,
                   0.3,pi/2,pi-1e-7,pi,pi+1e-7,2*pi,10*pi}) {
        for(double sign: {-1.,1.}) {
            inputs.push_back({sign*a,0,0});inputs.push_back({0,sign*a,0});
            inputs.push_back({0,0,sign*a});inputs.push_back({sign*a,-a/2,a/3});
            inputs.push_back({0.2,sign*pi/2,a});
        }
    }
    // 固定有界网格补一般轴；不使用全局随机状态，便于复现确定性。
    for(int i=0;i<1000;++i) inputs.push_back({i*.037-18.,i*.019-9.,i*.053-26.});
    double maxForward=0,maxRoundTrip=0;
    for(const auto& rpy:inputs) {
        const auto expected=reference(rpy);
        const auto direct=subject::rpyToRotation(rpy[0],rpy[1],rpy[2]);
        const auto v=subject::rpyToEaa(rpy);
        // 明确对照原上游入口，同时保留下方独立公式，避免只有同源自证。
        const auto upstream=RPY<>(rpy[2],rpy[1],rpy[0]).toRotation3D();
        const auto upstreamRoundTrip=reconstruct(EAA<>(upstream).toVector3D());
        ASSERT_TRUE(std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]));
        EXPECT_LE(std::hypot(v[0],v[1],v[2]),pi+1e-12);
        const auto roundTrip=reconstruct(v);
        for(int r=0;r<3;++r) for(int c=0;c<3;++c) {
            maxForward=std::max(maxForward,std::abs(direct(r,c)-expected(r,c)));
            maxRoundTrip=std::max(maxRoundTrip,std::abs(roundTrip(r,c)-expected(r,c)));
            EXPECT_NEAR(direct(r,c),upstream(r,c),1e-12);
            EXPECT_NEAR(roundTrip(r,c),upstreamRoundTrip(r,c),1e-6);
        }
        const auto repeated=subject::rpyToEaa(rpy);
        for(int k=0;k<3;++k) EXPECT_EQ(v[k],repeated[k]);
    }
    std::cout<<"samples="<<inputs.size()<<" maxForward="<<maxForward
             <<" maxEaaRoundTrip="<<maxRoundTrip<<'\n';
    EXPECT_LE(maxForward,1e-12); // 普通RPY正向不放宽。
    EXPECT_LE(maxRoundTrip,1e-6); // 仅已批准的EAA表示转换元素误差。
}

/** @brief 同编译/输入的微基准；输出真实耗时，无既定预算时不伪造性能断言。 */
TEST(RpyReuse, ConversionTiming_REUSE_T11)
{
    constexpr int count=200000;
    volatile double checksum=0;
    const auto start=std::chrono::steady_clock::now();
    for(int i=0;i<count;++i) {
        const auto v=subject::rpyToEaa({i*.0001,-.4,.7});
        checksum=checksum+v[0]+v[1]+v[2];
    }
    const auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"microsecondsPerConversion="<<elapsed/count<<" checksum="<<checksum<<'\n';
    EXPECT_TRUE(std::isfinite(checksum));
}
} // namespace
