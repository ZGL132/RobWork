/**
 * @file CouplingBenchmark.cpp
 * @brief REUSE-T07 冻结基线/迁移版本的同工具链微基准；不进入产品目标。
 * 每进程预热100次，再测7组各10000次有限3×3分解，报告中位数；无新SVD算法。
 */
#include IRD_COUPLING_HEADER
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <array>
/** @brief 独立进程入口；无参数，输出耗时/校验和；结果不符返回1，分配异常传播。 */
int main()
{
    using namespace sdurws::ird::modeling::couplingmath;
    const std::vector<double> matrix{2.5,1.5,0,1.5,2.5,0,0,0,-2};
    double checksum=0;
    for (int i=0;i<100;++i) { checksum+=couplingSingularValues(matrix,3).front(); }
    std::array<double,7> samples{};
    for (double& sample:samples) {
        const auto start=std::chrono::steady_clock::now();
        for (int i=0;i<10000;++i) { checksum+=couplingSingularValues(matrix,3).front(); }
        sample=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/10000;
    }
    std::sort(samples.begin(),samples.end());
    std::cout<<std::setprecision(12)<<"{\"medianMicroseconds\":"<<samples[3]
             <<",\"minMicroseconds\":"<<samples.front()<<",\"maxMicroseconds\":"<<samples.back()
             <<",\"checksum\":"<<checksum<<",\"calls\":70100}"<<std::endl;
    return std::fabs(checksum-280400)>1e-6 ? 1 : 0;
}
