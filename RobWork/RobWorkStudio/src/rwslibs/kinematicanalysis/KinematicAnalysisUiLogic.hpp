#ifndef RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
#define RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP

#include <vector>
#include <string>

namespace rw { namespace models { class Device; } }

namespace rws {

bool ikCollisionCheckRequested (bool checkboxAvailable, bool checkboxChecked);
std::vector< int > taskPointCompactTableColumns ();
std::vector< int > taskPointDetailColumns ();
std::string defaultTcpFrameName (const rw::models::Device* device);

}    // namespace rws

#endif    // RWS_KINEMATICANALYSIS_KINEMATICANALYSISUILOGIC_HPP
