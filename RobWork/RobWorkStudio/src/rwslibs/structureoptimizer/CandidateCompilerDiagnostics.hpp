#ifndef RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILERDIAGNOSTICS_HPP
#define RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILERDIAGNOSTICS_HPP

#include "StructureOptimizationContracts.hpp"

#include <string>

namespace rws {

/** Creates a stable adapter diagnostic with its binding identity preserved. */
StructureOptimizationDiagnostic makeAdapterDiagnostic(const std::string& adapterId,
                                                       const std::string& bindingId,
                                                       const std::string& objectId,
                                                       const std::string& fieldPath,
                                                       const std::string& code,
                                                       const std::string& message);

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_CANDIDATECOMPILERDIAGNOSTICS_HPP
