#ifndef RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTCOMPILER_HPP
#define RWS_ENGINEERINGREQUIREMENTS_REQUIREMENTCOMPILER_HPP

#include "EngineeringRequirementTypes.hpp"

#include <string>
#include <vector>

namespace rws {

class RequirementCompiler {
public:
    static std::vector<RequirementDiagnostic> validateDetailed(const RequirementSet& requirements);
    static std::vector<std::string> validate(const RequirementSet& requirements);
    static bool compile(const RequirementSet& requirements, CompiledRequirementSet& compiled,
                        std::string* error = nullptr);
    static std::string fingerprint(const RequirementSet& requirements);
};

} // namespace rws

#endif
