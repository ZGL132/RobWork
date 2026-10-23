// GAP-EAA-01：同机Release原API的前后耗时；不实现旋转核或设立新预算。
#include <rw/math/EAA.hpp>
#include <rw/math/RPY.hpp>
#include <chrono>
#include <algorithm>
#include <array>
#include <iostream>
#include <iomanip>
int main() {
 using namespace rw::math;
 std::array<Rotation3D<>,4> inputs{{RPY<>(.7,-.4,.3).toRotation3D(), RPY<>(3e-6,-2e-6,1e-6).toRotation3D(), RPY<>(3e-12,-2e-12,1e-12).toRotation3D(),RPY<>(3.141592653589793,0,0).toRotation3D()}};
 volatile double sum=0;std::array<double,7> runs;
 for(int n=0;n<100;++n)sum+=EAA<>(inputs[n%4]).toRotation3D()(0,0);
 for(int r=0;r<7;++r){auto start=std::chrono::steady_clock::now();
  for(int n=0;n<100000;++n)sum+=EAA<>(inputs[n%4]).toRotation3D()(0,0);
  runs[r]=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/100000;}
 auto sorted=runs;std::sort(sorted.begin(),sorted.end());
 std::cout<<std::setprecision(17)<<"{\"iterations\":100000,\"warmup\":100,\"groups\":7,\"medianMicroseconds\":"<<sorted[3]<<",\"checksum\":"<<sum<<",\"runs\":[";
 for(int r=0;r<7;++r){if(r)std::cout<<",";std::cout<<runs[r];}std::cout<<"]}\n";
}
