#include <rw/math/Rotation3D.hpp>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>
using R=rw::math::Rotation3D<double>;
extern "C" void oldRotate(const R&,const double*,double*);
extern "C" void newRotate(const R&,const double*,double*);
extern "C" void oldCuboid(double,double,double,double,double*);
extern "C" void newCuboid(double,double,double,double,double*);
extern "C" int oldCheck(int);
extern "C" int newCheck(int);
volatile double sink=0;
template<class F> double measure(F fn) {
    constexpr int n=1000000;double sum=0;
    for(int k=0;k<10000;++k) sum+=fn(k);
    const auto start=std::chrono::steady_clock::now();
    for(int k=0;k<n;++k) sum+=fn(k);
    const auto end=std::chrono::steady_clock::now();sink=sum;
    return std::chrono::duration<double,std::micro>(end-start).count()/n;
}
int main() {
    const R rotations[]={R::identity(),R(.6,-.8,0,.8,.6,0,0,0,1),
        R(1./3,2./3,2./3,2./3,1./3,-2./3,-2./3,2./3,-1./3)};
    const double d[]={208,160,80};
    double old[9]{},next[9]{};
    for(const auto& r:rotations) {
        oldRotate(r,d,old);newRotate(r,d,next);
        for(int k=0;k<9;++k) if(std::abs(old[k]-next[k])>1e-12*208) return 2;
    }
    oldCuboid(48,2,4,6,old);newCuboid(48,2,4,6,next);
    for(int k=0;k<3;++k) if(std::abs(old[k]-next[k])>1e-12*208) return 3;
    if(!oldCheck(0)||!newCheck(0)) return 4;
    std::puts("MSVC /O2 /MD /DNDEBUG; separate translation units; 1000000 iterations + 10000 warmup; us/call; alternating old/new order");
    for(int round=0;round<3;++round) {
        for(int scene=0;scene<5;++scene) {
            auto a=[&](int k) {if(scene<3) oldRotate(rotations[scene],d,old);else if(scene==3) oldCuboid(48+(k&1),2,4,6,old);else return double(oldCheck(k));return old[k%3];};
            auto b=[&](int k) {if(scene<3) newRotate(rotations[scene],d,next);else if(scene==3) newCuboid(48+(k&1),2,4,6,next);else return double(newCheck(k));return next[k%3];};
            double ta,tb;if(round%2){tb=measure(b);ta=measure(a);}else{ta=measure(a);tb=measure(b);}
            std::printf("round=%d scene=%d old=%.8f new=%.8f ratio=%.4f checksum=%.8f\n",round+1,scene,ta,tb,tb/ta,(double)sink);
        }
    }
    return 0;
}
