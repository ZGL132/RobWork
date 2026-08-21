#ifndef RWS_STRUCTUREOPTIMIZATION_ADAPTERREGISTRY_HPP
#define RWS_STRUCTUREOPTIMIZATION_ADAPTERREGISTRY_HPP

#include "ModelParameterAdapter.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rws {

struct AdapterRegistryRegistrationResult
{
    bool ok = false;
    std::vector< StructureOptimizationDiagnostic > diagnostics;
};

/** Explicitly owned core registry.  It stores adapters, never live WorkCells. */
class AdapterRegistry
{
  public:
    AdapterRegistryRegistrationResult registerAdapter(
        const std::shared_ptr< const IModelParameterAdapter >& adapter);
    void clear();

    bool supports(SemanticKind semanticKind) const;
    const IModelParameterAdapter* find(const std::string& adapterId) const;
    std::string fingerprintMaterial() const;

    AdapterPatchCompileResult compilePatch(
        const AdapterPatchCompileRequest& request,
        const AdapterCapabilityQuery& capabilities) const;

  private:
    std::map< std::string, std::shared_ptr< const IModelParameterAdapter > > _adapters;
};

}    // namespace rws

#endif    // RWS_STRUCTUREOPTIMIZATION_ADAPTERREGISTRY_HPP
