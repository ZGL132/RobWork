#include <rw/math/EAA.hpp>
#include <rw/math/Constants.hpp>
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <cmath>
using namespace rw::math;
int main() {
 char module[MAX_PATH]={0}; GetModuleFileNameA(GetModuleHandleA("sdurw_math.dll"),module,MAX_PATH);
 std::cout << std::setprecision(17) << "module=" << module << "\n";
 auto a=normalize(Vector3D<>(.3,.4,.5)); auto p=normalize(cross(a,Vector3D<>(1,0,0)));
 for(double eps : {0.,1e-12,1e-8,1e-5}) {
  auto b=normalize(-std::cos(eps)*a+std::sin(eps)*p);
  auto x=cross(a,b); EAA<> e(a,b); auto actual=e.toRotation3D()*a;
  std::cout << "eps="<<eps<<" a="<<a<<" b="<<b<<" norms="<<a.norm2()<<","<<b.norm2()<<" dot="<<dot(a,b)<<" cross="<<x<<" eaa="<<e.toVector3D()<<" actual="<<actual<<" error="<<(actual-b).normInf()<<"\n";
 }
}
