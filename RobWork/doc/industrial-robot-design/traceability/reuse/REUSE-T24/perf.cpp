/** REUSE-T24：同轮私有原入口对照；只测成功路径，不以错误结果当性能替代。 */
#include "ParamSchema.hpp"
#include <chrono>
#include <cstdio>
namespace sdurws::ird::diagnostics::detail {
bool baselineTryParseParamSchema(const std::string&, std::vector<std::string>*, std::string*);
}
using Parse = bool (*)(const std::string&, std::vector<std::string>*, std::string*);
volatile std::size_t sink = 0;
// 同样的原文输入、输出和次数；分别覆盖生产新槽及重复调用容量复用。
double measure(Parse parse, bool reuse) {
    constexpr int count = 1000000;
    const std::string schema = "[\"pid\",\"host\"]";
    std::vector<std::string> names;
    std::string error;
    auto start = std::chrono::steady_clock::now();
    for (int i=0; i<count; ++i) {
        if (reuse) { sink += parse(schema, &names, &error) + names.size(); }
        else {
            std::vector<std::string> fresh;
            std::string freshError;
            sink += parse(schema, &fresh, &freshError) + fresh.size();
        }
    }
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-start).count()/count;
}
int main() {
    namespace d = sdurws::ird::diagnostics::detail;
    for (int round=0; round<3; ++round) {
        for (bool reuse : {false,true}) {
            double oldUs, newUs;
            if (round%2) {
                newUs=measure(d::tryParseParamSchema,reuse);
                oldUs=measure(d::baselineTryParseParamSchema,reuse);
            } else {
                oldUs=measure(d::baselineTryParseParamSchema,reuse);
                newUs=measure(d::tryParseParamSchema,reuse);
            }
            std::printf("round=%d reuse=%d old_us=%.6f new_us=%.6f\n",round+1,reuse,oldUs,newUs);
        }
    }
}
