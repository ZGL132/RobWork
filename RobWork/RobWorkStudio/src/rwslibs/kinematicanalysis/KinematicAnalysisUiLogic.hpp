#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP

#include <vector>

namespace rws {

bool ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked);
std::vector< int > taskPointCompactTableColumns ();
std::vector< int > taskPointDetailColumns ();

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
