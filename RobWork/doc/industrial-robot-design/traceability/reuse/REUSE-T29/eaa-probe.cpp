#include <rw/math/EAA.hpp>
#include <rw/math/Rotation3D.hpp>
#include <Eigen/Geometry>
#include <cmath>
#include <cstdio>
int main(){
 const double a=1e-8;
 const auto rwR=rw::math::EAA<>(a,0,0).toRotation3D();
 const rw::math::Rotation3D<> candidate(Eigen::AngleAxisd(a,Eigen::Vector3d::UnitX()).toRotationMatrix());
 const double expected=std::sin(a);
 std::printf("tiny_x_rad=%.17g oracle_R21=%.17g upstream_R21=%.17g candidate_R21=%.17g\n",a,expected,rwR(2,1),candidate(2,1));
 std::printf("upstream_abs_error=%.17g candidate_abs_error=%.17g tolerance=1e-12\n",std::abs(rwR(2,1)-expected),std::abs(candidate(2,1)-expected));
 return std::abs(rwR(2,1)-expected)>1e-9 && std::abs(candidate(2,1)-expected)<1e-12?0:1;
}
