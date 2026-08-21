#include "AdapterRegistry.hpp"

#include <algorithm>
#include <sstream>

namespace rws {
namespace {

void addRegistryError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
                      const std::string& adapterId, const std::string& bindingId,
                      const std::string& objectId, const std::string& fieldPath,
                      const std::string& code, const std::string& message)
{
    diagnostics.push_back(makeAdapterDiagnostic(adapterId, bindingId, objectId, fieldPath,
                                                code, message));
}

bool supportsSemantic(const IModelParameterAdapter& adapter, SemanticKind semanticKind)
{
    const std::vector< SemanticKind > kinds = adapter.supportedSemanticKinds();
    return std::find(kinds.begin(), kinds.end(), semanticKind) != kinds.end();
}

std::string fallbackBindingFieldPath(const ParameterBinding& binding)
{
    return binding.displayPath.empty() ? "binding:" + binding.id : binding.displayPath;
}

void contextualizeAdapterDiagnostics(
    std::vector< StructureOptimizationDiagnostic >& diagnostics,
    const std::string& adapterId, const ParameterBinding& binding)
{
    for (StructureOptimizationDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.subsystem.empty())
            diagnostic.subsystem = "candidate-compiler";
        if (diagnostic.stage.empty())
            diagnostic.stage = adapterId.empty() ? "adapter" : "adapter:" + adapterId;
        if (diagnostic.objectId.empty())
            diagnostic.objectId = binding.targetObjectId;
        if (diagnostic.fieldPath.empty())
            diagnostic.fieldPath = fallbackBindingFieldPath(binding);
        if (!binding.id.empty() &&
            std::find(diagnostic.evidenceIds.begin(), diagnostic.evidenceIds.end(), binding.id) ==
                diagnostic.evidenceIds.end())
            diagnostic.evidenceIds.push_back(binding.id);
    }
}

bool containsErrorDiagnostic(const std::vector< StructureOptimizationDiagnostic >& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.severity == "Error";
                       });
}

}    // namespace

AdapterRegistryRegistrationResult AdapterRegistry::registerAdapter(
    const std::shared_ptr< const IModelParameterAdapter >& adapter)
{
    AdapterRegistryRegistrationResult result;
    if (!adapter) {
        addRegistryError(result.diagnostics, "", "", "", "adapter",
                         "ADAPTER_REGISTRY_ADAPTER_REQUIRED",
                         "Adapter registry registration requires an adapter instance.");
        return result;
    }
    const std::string id = adapter->adapterId();
    if (id.empty()) {
        addRegistryError(result.diagnostics, "", "", "", "adapterId",
                         "ADAPTER_REGISTRY_ID_REQUIRED",
                         "Adapters require a stable non-empty ID.");
        return result;
    }
    if (adapter->adapterVersion() <= 0) {
        addRegistryError(result.diagnostics, id, "", "", "adapterVersion",
                         "ADAPTER_REGISTRY_VERSION_INVALID",
                         "Adapters require a positive version.");
        return result;
    }
    const std::vector< SemanticKind > semantics = adapter->supportedSemanticKinds();
    if (semantics.empty() || std::find(semantics.begin(), semantics.end(), SemanticKind::Unknown) !=
                                 semantics.end()) {
        addRegistryError(result.diagnostics, id, "", "", "supportedSemanticKinds",
                         "ADAPTER_REGISTRY_SEMANTIC_REQUIRED",
                         "Adapters must declare at least one concrete semantic kind.");
        return result;
    }
    if (_adapters.find(id) != _adapters.end()) {
        addRegistryError(result.diagnostics, id, "", "", "adapterId",
                         "ADAPTER_REGISTRY_DUPLICATE_ID_VERSION",
                         "An adapter ID/version may be registered only once.");
        return result;
    }
    _adapters[id] = adapter;
    result.ok = true;
    return result;
}

void AdapterRegistry::clear()
{
    _adapters.clear();
}

bool AdapterRegistry::supports(SemanticKind semanticKind) const
{
    for (const auto& entry : _adapters)
        if (supportsSemantic(*entry.second, semanticKind))
            return true;
    return false;
}

const IModelParameterAdapter* AdapterRegistry::find(const std::string& adapterId) const
{
    const auto found = _adapters.find(adapterId);
    return found == _adapters.end() ? nullptr : found->second.get();
}

std::string AdapterRegistry::fingerprintMaterial() const
{
    std::ostringstream stream;
    stream << "adapter-registry-v1\\n";
    for (const auto& entry : _adapters) {
        const IModelParameterAdapter& adapter = *entry.second;
        stream << entry.first.size() << ':' << entry.first << '|' << adapter.adapterVersion() << '|';
        std::vector< SemanticKind > semantics = adapter.supportedSemanticKinds();
        std::sort(semantics.begin(), semantics.end());
        for (const SemanticKind semantic : semantics)
            stream << static_cast< int >(semantic) << ',';
        stream << '|';
        std::vector< AdapterCapability > capabilities = adapter.requiredCapabilities();
        std::sort(capabilities.begin(), capabilities.end());
        for (const AdapterCapability capability : capabilities)
            stream << static_cast< int >(capability) << ',';
        stream << '\\n';
    }
    return stream.str();
}

AdapterPatchCompileResult AdapterRegistry::compilePatch(
    const AdapterPatchCompileRequest& request, const AdapterCapabilityQuery& capabilities) const
{
    AdapterPatchCompileResult result;
    if (request.baseline == nullptr) {
        addRegistryError(result.diagnostics, "", "", "", "baseline",
                         "ADAPTER_COMPILE_BASELINE_REQUIRED",
                         "Adapter patch compilation requires an immutable baseline model.");
        return result;
    }
    if (request.binding == nullptr) {
        addRegistryError(result.diagnostics, "", "", "", "binding",
                         "ADAPTER_COMPILE_BINDING_REQUIRED",
                         "Adapter patch compilation requires a parameter binding.");
        return result;
    }

    const ParameterBinding& binding = *request.binding;
    const ParameterBindingValidationResult genericBindingResult =
        ParameterBindingValidator::validate(binding);
    std::vector< StructureOptimizationDiagnostic > genericDiagnostics =
        genericBindingResult.diagnostics;
    contextualizeAdapterDiagnostics(genericDiagnostics, binding.ownerAdapterId, binding);
    result.diagnostics.insert(result.diagnostics.end(), genericDiagnostics.begin(),
                              genericDiagnostics.end());
    if (!genericBindingResult.valid || containsErrorDiagnostic(result.diagnostics))
        return result;

    const IModelParameterAdapter* adapter = find(binding.ownerAdapterId);
    if (adapter == nullptr) {
        addRegistryError(result.diagnostics, binding.ownerAdapterId, binding.id,
                         binding.targetObjectId, "ownerAdapterId",
                         "ADAPTER_REGISTRY_OWNER_NOT_FOUND",
                         "No registered adapter owns this binding.");
        return result;
    }
    if (binding.ownerAdapterVersion != adapter->adapterVersion()) {
        addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                         binding.targetObjectId, "ownerAdapterVersion",
                         "ADAPTER_REGISTRY_BINDING_VERSION_MISMATCH",
                         "The binding owner adapter version does not match the registered adapter.");
        return result;
    }
    if (!supportsSemantic(*adapter, binding.semanticKind)) {
        addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                         binding.targetObjectId, "semanticKind", "ADAPTER_SEMANTIC_UNSUPPORTED",
                         "The binding semantic kind is not supported by its owning adapter.");
        return result;
    }

    const AdapterBindingValidationResult bindingResult =
        adapter->validateBinding(binding, *request.baseline);
    std::vector< StructureOptimizationDiagnostic > adapterBindingDiagnostics =
        bindingResult.diagnostics;
    contextualizeAdapterDiagnostics(adapterBindingDiagnostics, adapter->adapterId(), binding);
    result.diagnostics.insert(result.diagnostics.end(), adapterBindingDiagnostics.begin(),
                              adapterBindingDiagnostics.end());
    if (!bindingResult.valid || containsErrorDiagnostic(result.diagnostics))
        return result;

    const std::vector< ReadWriteTarget > declaredReadSet = adapter->declaredReadSet(binding);
    if (declaredReadSet.empty()) {
        addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                         binding.targetObjectId, "declaredReadSet",
                         "ADAPTER_DECLARED_READ_SET_REQUIRED",
                         "Each adapter binding must declare the canonical targets it reads.");
        return result;
    }
    const std::vector< ReadWriteTarget > declaredWriteSet = adapter->declaredWriteSet(binding);
    if (declaredWriteSet.empty()) {
        addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                         binding.targetObjectId, "declaredWriteSet",
                         "ADAPTER_DECLARED_WRITE_SET_REQUIRED",
                         "Each adapter binding must declare the canonical targets it writes.");
        return result;
    }

    for (const AdapterCapability capability : adapter->requiredCapabilities()) {
        if (!capabilities.supports(binding.targetObjectType, binding.targetObjectId, capability)) {
            addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                             binding.targetObjectId, "requiredCapabilities",
                             "ADAPTER_REGISTRY_CAPABILITY_REQUIRED",
                             "The selected target lacks a capability required by its adapter.");
            return result;
        }
    }

    AdapterPatchCompileResult adapterCompileResult = adapter->compilePatch(request);
    contextualizeAdapterDiagnostics(adapterCompileResult.diagnostics, adapter->adapterId(), binding);
    contextualizeAdapterDiagnostics(adapterCompileResult.patch.diagnostics, adapter->adapterId(),
                                    binding);
    result.ok = adapterCompileResult.ok;
    result.patch = adapterCompileResult.patch;
    result.diagnostics.insert(result.diagnostics.end(), adapterCompileResult.diagnostics.begin(),
                              adapterCompileResult.diagnostics.end());
    result.diagnostics.insert(result.diagnostics.end(), result.patch.diagnostics.begin(),
                              result.patch.diagnostics.end());
    if (!result.ok || containsErrorDiagnostic(result.diagnostics)) {
        result.ok = false;
        return result;
    }
    if (result.patch.adapterId != adapter->adapterId() ||
        result.patch.adapterVersion != adapter->adapterVersion() ||
        result.patch.bindingId != binding.id) {
        addRegistryError(result.diagnostics, adapter->adapterId(), binding.id,
                         binding.targetObjectId, "patch.identity",
                         "ADAPTER_PATCH_IDENTITY_MISMATCH",
                         "Adapter patch identity must match the registered adapter and binding.");
        result.ok = false;
        return result;
    }
    const CandidatePatchValidationResult patchValidation =
        CandidatePatchValidator::validate(result.patch, declaredWriteSet);
    result.diagnostics.insert(result.diagnostics.end(), patchValidation.diagnostics.begin(),
                              patchValidation.diagnostics.end());
    if (!patchValidation.valid)
        result.ok = false;
    return result;
}

}    // namespace rws
