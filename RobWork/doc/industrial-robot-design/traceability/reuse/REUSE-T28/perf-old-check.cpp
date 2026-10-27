#define synthesisSelfCheck oldSynthesisSelfCheck
#include "perf-old-inertia.hpp"
#undef synthesisSelfCheck
extern "C" int oldCheck(int k) {
    const std::array<std::array<double,3>,3> m={{{208.0+(k&1),23.04,0},{23.04,160,0},{0,0,80}}};
    return sdurws::ird::modeling::inertiamath::oldSynthesisSelfCheck(m).empty();
}
