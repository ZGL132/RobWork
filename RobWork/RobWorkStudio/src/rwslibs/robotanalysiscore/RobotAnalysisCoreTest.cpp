#include "RobotAnalysisTypes.hpp"

#include <iostream>
#include <string>

int main ()
{
    const std::string name = rws::RobotAnalysisCoreMarker::name ();
    if (name != "RobotAnalysisCore") {
        std::cerr << "RobotAnalysisCore marker returned unexpected name: " << name << std::endl;
        return 1;
    }

    std::cout << "RobotAnalysisCore skeleton test passed." << std::endl;
    return 0;
}
