#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP

#include "KinematicAnalysisVisualizationTypes.hpp"

#include <vector>
#include <string>

namespace rw { namespace models { class Device; } }

namespace rws {

bool ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked);
bool visualEnvelopeModeAvailable (int sourceKind, int renderMode);
std::vector< int > taskPointCompactTableColumns ();
std::vector< int > taskPointDetailColumns ();
std::string defaultTcpFrameName (const rw::models::Device* device);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
