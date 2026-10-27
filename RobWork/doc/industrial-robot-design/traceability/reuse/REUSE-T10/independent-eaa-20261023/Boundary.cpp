// 独立验收：检查既有axis阈值、负小角三轴解析oracle及反平行方向；不修改产品。
#include <rw/math/EAA.hpp>
#include <rw/math/Constants.hpp>
#include <gtest/gtest.h>
#include <cmath>
using namespace rw::math;
TEST(IndependentEAA, LegacyAxisBoundary) {
 for(double t : {0.,1e-12,1e-7,0.999999e-6,1e-6,1.000001e-6}) {
  EAA<> e(0,0,t); auto a=e.axis();
  EXPECT_DOUBLE_EQ(a[0],0); EXPECT_DOUBLE_EQ(a[1],0);
  EXPECT_DOUBLE_EQ(a[2],t<1e-6 ? 0. : 1.);
 }
}
TEST(IndependentEAA, SignedSmallAngleAnalyticAxes) {
 for(int k=0;k<3;++k) for(double t : {-1e-3,-1e-6,-1e-7,-1e-12,0.,1e-12,1e-7,1e-6,1e-3}) {
  double v[3]={0,0,0}; v[k]=t; auto r=EAA<>(v[0],v[1],v[2]).toRotation3D();
  int p=(k+1)%3,q=(k+2)%3;
  EXPECT_NEAR(r(p,q),-std::sin(t),1e-15); EXPECT_NEAR(r(q,p),std::sin(t),1e-15);
  EXPECT_NEAR(r(p,p),std::cos(t),1e-15); EXPECT_NEAR(r(q,q),std::cos(t),1e-15);
  auto e=EAA<>(r); EXPECT_NEAR(e[k],t,1e-15);
 }
}
TEST(IndependentEAA, AntiparallelAndNearlyAntiparallel) {
 auto a=normalize(Vector3D<>(.3,.4,.5)); auto p=normalize(cross(a,Vector3D<>(1,0,0)));
 for(double eps : {0.,1e-12,1e-8,1e-5}) {
  auto b=normalize(-std::cos(eps)*a+std::sin(eps)*p);
  EAA<> e(a,b); auto actual=e.toRotation3D()*a;
  EXPECT_NEAR((actual-b).normInf(),0.,1e-12);
  EXPECT_NEAR(e.angle(),Pi-eps,1e-12);
  EXPECT_EQ(e,EAA<>(a,b));
 }
}
