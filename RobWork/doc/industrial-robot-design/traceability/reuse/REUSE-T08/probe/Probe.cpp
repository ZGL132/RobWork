/** @file Probe.cpp
 * @brief REUSE-T08：只读候选API适用性探针，按原MDL-22矩阵容差1e-12检查。
 * 不进入产品；无新转换算法。分别调用既有RPY、EAA、Quaternion API并记录真实误差。
 */
#include <rw/math/RPY.hpp>
#include <rw/math/EAA.hpp>
#include <rw/math/Quaternion.hpp>
#include <rw/math/Constants.hpp>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <algorithm>
/** @brief 直接执行候选并输出误差；任一EAA候选超原容差返回1，绝不扩大容差。 */
int main() {
 using namespace rw::math;
 const std::vector<Vector3D<>> inputs{{0.3,-0.4,0.7},{1e-6,-2e-6,3e-6},{1e-12,-2e-12,3e-12},{0,0,0},{0,0,Pi},{0.2,Pi/2,-0.6}};
 bool failed=false;
 std::cout << std::setprecision(17);
 for(const auto& rpy:inputs) {
  const auto r=RPY<>(rpy[2],rpy[1],rpy[0]).toRotation3D();
  const auto rebuilt=EAA<>(r).toRotation3D();double error=0;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j)error=std::max(error,std::fabs(rebuilt(i,j)-r(i,j)));
  const Quaternion<> q(RPY<>(rpy[2],rpy[1],rpy[0]));const auto l=q.ln();
  std::cout<<" quaternion="<<q[0]<<","<<q[1]<<","<<q[2]<<","<<q[3]<<" log="<<l[0]<<","<<l[1]<<","<<l[2]<<","<<l[3]<<" ";
  const bool logFinite=std::isfinite(l[0])&&std::isfinite(l[1])&&std::isfinite(l[2]);
  std::cout << "rpy="<<rpy[0]<<","<<rpy[1]<<","<<rpy[2]<<" eaaMatrixMaxAbsError="<<error<<" within1e-12="<<(error<=1e-12)<<" quaternionLogFinite="<<logFinite<<"\n";
  failed|=error>1e-12 || !logFinite;
 }
 return failed?1:0;
}
