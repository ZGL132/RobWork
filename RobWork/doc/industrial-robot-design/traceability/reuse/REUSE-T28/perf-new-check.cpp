#define synthesisSelfCheck newSynthesisSelfCheck
#include "D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/src/InertiaMath.hpp"
#undef synthesisSelfCheck
extern "C" int newCheck(int k) {
    const std::array<std::array<double,3>,3> m={{{208.0+(k&1),23.04,0},{23.04,160,0},{0,0,80}}};
    return sdurws::ird::modeling::inertiamath::newSynthesisSelfCheck(m).empty();
}
