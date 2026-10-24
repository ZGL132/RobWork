#include <chrono>
#include <cstdio>
extern "C" double old_eval(int,double); extern "C" double new_eval(int,double);
volatile double checksum;
double run(double(*f)(int,double),int s,int n){double out=0;auto a=std::chrono::steady_clock::now();for(int i=0;i<n;++i)out+=f(s,(i%101)*1e-7);auto b=std::chrono::steady_clock::now();checksum=out;return std::chrono::duration<double,std::micro>(b-a).count()/n;}
int main(){constexpr int n=200000;std::puts("MSVC /O2 /MD /DNDEBUG EIGEN_DONT_ALIGN_STATICALLY=1; extracted exact functions, separate TUs; 200000/case,10000warmup; us/call; alternating order");for(int round=1;round<=3;++round)for(int s=0;s<6;++s){run(old_eval,s,10000);run(new_eval,s,10000);double a,b;if(round%2){a=run(old_eval,s,n);b=run(new_eval,s,n);}else{b=run(new_eval,s,n);a=run(old_eval,s,n);}std::printf("round=%d scene=%d old=%.8f new=%.8f ratio=%.4f checksum=%.10f\n",round,s,a,b,b/a,checksum);} }
