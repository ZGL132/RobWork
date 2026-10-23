/** @file Envelope.cpp
 * @brief 只读测量原版EAA的两类转换误差；仅独立测试oracle，不接入生产或修改上游。
 * 参数范围：主值旋转[0,pi]，无量纲单位轴；独立sin/cos元素公式作参考。
 */
#include <rw/math/EAA.hpp>
#include <rw/math/Constants.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>
using namespace rw::math;
/** @brief 测试独立Rodrigues参考，仅单位轴/有限rad；无共享状态，不调用EAA。 */
static Rotation3D<> reference(const Vector3D<>& a,double t) {
 const double c=std::cos(t),s=std::sin(t),v=1-c,x=a[0],y=a[1],z=a[2];
 return Rotation3D<>(c+x*x*v,x*y*v-z*s,x*z*v+y*s,
                    x*y*v+z*s,c+y*y*v,y*z*v-x*s,
                    x*z*v-y*s,y*z*v+x*s,c+z*z*v);
}
/** @brief 比较两个旋转的逐元素最大绝对误差，无量纲，不允许正负抵消。 */
static double maxError(const Rotation3D<>& a,const Rotation3D<>& b) {
 double v=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j)v=std::max(v,std::fabs(a(i,j)-b(i,j)));return v;
}
/** @brief 固定输入覆盖正负坐标轴及一般轴、零/小角阈值两侧/一般角/pi两侧；不随机。 */
static std::vector<Vector3D<>> axes() {
 return {{1,0,0},{0,1,0},{0,0,1},normalize(Vector3D<>(.3,.4,.5)),normalize(Vector3D<>(1,1e-8,-2e-8)),normalize(Vector3D<>(-.6,.2,-.3))};
}
/** @brief 固定角度网格（rad），重点覆盖原1e-6截断界及pi邻域；无随机性。 */
static std::vector<double> angles() {
 std::vector<double> a{0,1e-15,1e-12,1e-10,1e-8,1e-7,0.999999e-6,1e-6,1.000001e-6,2e-6,3.741657386773941e-6,1e-4,.1,.7,Pi/2,Pi-1e-4,Pi-1.000001e-6,Pi-1e-6,Pi-.999999e-6,Pi-1e-8,Pi-1e-12,Pi};
 for(int i=-120;i<=120;++i)a.push_back(1e-6*std::pow(10.,i/40.));return a;
}
/** @brief MDL-22/AT-37：矩阵->EAA独立回构限1e-6，隔离toRotation3D的1e-6截断界。 */
TEST(EaaOriginal, MatrixToVectorEnvelope_MDL22_AT37) {
 double worst=0,worstAngle=0;size_t n=0;
 for(const auto& axis:axes())for(const auto theta:angles())for(const double sign:{-1.,1.}) {
  auto expected=reference(axis,sign*theta);EAA<> actual(expected);const auto v=actual.toVector3D();const double t=v.norm2();
  auto rebuilt=t==0?reference(Vector3D<>(1,0,0),0):reference(v/t,t);
  const double e=maxError(expected,rebuilt);if(e>worst){worst=e;worstAngle=theta;}++n;
  ASSERT_TRUE(std::isfinite(e)); EXPECT_LE(e,1e-6)<<"theta="<<theta;
 }
 std::cout<<std::setprecision(17)<<"ENVELOPE matrix_to_vector samples="<<n<<" maxElementError="<<worst<<" worstAngle="<<worstAngle<<"\n";
}
/** @brief 测原版EAA->矩阵与完整往返，1e-6来自原axis()的小角截断界，而非任意放宽。 */
TEST(EaaOriginal, VectorAndRoundTripEnvelope_MDL22_AT37) {
 double worstDirect=0,worstRoundTrip=0,worstAngle=0;size_t n=0;
 for(const auto& axis:axes())for(const auto theta:angles())for(const double sign:{-1.,1.}) {
  auto expected=reference(axis,sign*theta);const auto direct=EAA<>(axis*(sign*theta)).toRotation3D();
  const auto roundTrip=EAA<>(expected).toRotation3D();
  const double d=maxError(expected,direct),r=maxError(expected,roundTrip);
  if(d>worstDirect){worstDirect=d;worstAngle=theta;}worstRoundTrip=std::max(worstRoundTrip,r);++n;
  ASSERT_TRUE(std::isfinite(d)&&std::isfinite(r));EXPECT_LE(d,1e-6)<<"theta="<<theta;EXPECT_LE(r,1e-6)<<"theta="<<theta;
 }
 std::cout<<std::setprecision(17)<<"ENVELOPE vector_to_matrix samples="<<n<<" maxElementError="<<worstDirect<<" roundTripMax="<<worstRoundTrip<<" worstAngle="<<worstAngle<<"\n";
}
