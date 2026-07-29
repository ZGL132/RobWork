#ifndef RWS_ENGINEERINGREQUIREMENTS_GEOMETRYFEATURERESOLVER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_GEOMETRYFEATURERESOLVER_HPP

#include "EngineeringRequirementTypes.hpp"

#include <array>
#include <string>

namespace rw { namespace kinematics { class State; } }
namespace rw { namespace models { class WorkCell; } }

namespace rws {

struct GeometryFeatureResolution {
    std::array<double, 3> position = {{0.0, 0.0, 0.0}};
    std::array<double, 3> rpyDeg = {{0.0, 0.0, 0.0}};
};

class GeometryFeatureResolver {
public:
    static bool resolve(const GeometryFeatureReference& feature, const std::string& referenceFrame,
                        const rw::models::WorkCell& workcell, const rw::kinematics::State& state,
                        GeometryFeatureResolution& resolution, std::string* error = nullptr);
    static bool applyToStation(const GeometryFeatureReference& feature, const rw::models::WorkCell& workcell,
                               const rw::kinematics::State& state, KeyStation& station,
                               std::string* error = nullptr);
};

} // namespace rws

#endif
