#include "CandidateCompiler.hpp"

#include "CandidateCompilerDiagnostics.hpp"
#include "DependencyGraph.hpp"
#include "KinematicFingerprint.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace rws {
namespace {

void addError(std::vector< StructureOptimizationDiagnostic >& diagnostics,
              const std::string& code, const std::string& fieldPath,
              const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "candidate-compiler";
    diagnostic.stage = "compile";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    diagnostics.push_back(diagnostic);
}

bool containsError(const std::vector< StructureOptimizationDiagnostic >& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const StructureOptimizationDiagnostic& diagnostic) {
                           return diagnostic.severity == "Error";
                       });
}

void appendText(std::ostringstream& stream, const std::string& value)
{
    stream << value.size() << ':' << value << ';';
}

void appendDouble(std::ostringstream& stream, const double value)
{
    stream << std::hexfloat << (value == 0.0 ? 0.0 : value) << ';';
}

std::string fnv1a64(const std::string& value)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream encoded;
    encoded.imbue(std::locale::classic());
    encoded << std::hex << std::setfill('0') << std::setw(16) << hash;
    return encoded.str();
}

std::string candidateFingerprint(const CompiledDesignSpace& space,
                                 const DesignVector& vector,
                                 const std::map< std::string, DerivedValue >& derived,
                                 const CanonicalKinematicModel& model,
                                 const std::vector< std::string >& artifacts)
{
    const KinematicFingerprintResult modelFingerprint = KinematicFingerprint::forModel(model);
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    appendText(stream, "compiled-candidate-v1");
    appendText(stream, space.fingerprint);
    appendText(stream, vector.fingerprint);
    appendText(stream, modelFingerprint.value);
    for (const auto& value : derived) {
        appendText(stream, value.first);
        appendDouble(stream, value.second.value);
        stream << static_cast< int >(value.second.unit) << ';';
    }
    for (const std::string& artifact : artifacts) appendText(stream, artifact);
    return fnv1a64(stream.str());
}

}    // namespace

CandidateCompileResult CandidateCompiler::compile(const CandidateCompileRequest& request)
{
    CandidateCompileResult result;
    result.candidate.status = CandidateCompileStatus::CompileFailed;

    if (request.baseline == nullptr || request.designSpace == nullptr ||
        request.designVector == nullptr || request.adapterRegistry == nullptr ||
        request.capabilities == nullptr) {
        addError(result.diagnostics, "CANDIDATE_COMPILE_INPUT_MISSING", "request",
                 "Baseline, compiled design space, design vector, adapter registry, and capabilities are required.");
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    const CanonicalKinematicModelValidationResult baselineValidation =
        CanonicalKinematicModelValidator::validate(*request.baseline);
    if (!baselineValidation.valid) {
        result.diagnostics.insert(result.diagnostics.end(), baselineValidation.diagnostics.begin(),
                                  baselineValidation.diagnostics.end());
        addError(result.diagnostics, "CANDIDATE_COMPILE_BASELINE_INVALID", "baseline",
                 "Candidate compilation requires a valid canonical baseline model.");
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    const NormalizedDesignVectorResult vectorValidation = DesignVectorCodec::toNormalized(
        *request.designSpace, *request.designVector);
    result.diagnostics.insert(result.diagnostics.end(), vectorValidation.diagnostics.begin(),
                              vectorValidation.diagnostics.end());
    if (!vectorValidation.ok) {
        addError(result.diagnostics, "CANDIDATE_COMPILE_DESIGN_VECTOR_SCHEMA_MISMATCH", "designVector",
                 "The design vector does not match the compiled design-space schema.");
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }
    std::vector< EngineeringDesignValue > engineeringValues;
    engineeringValues.reserve(request.designVector->values.size());
    for (const DesignVectorValue& value : request.designVector->values)
        engineeringValues.push_back({value.variableId, value.unit, value.engineeringValue,
                                     value.discreteOptionId});
    const DesignVectorResult canonicalVector = DesignVectorCodec::fromEngineering(
        *request.designSpace, engineeringValues);
    if (!canonicalVector.ok || request.designVector->fingerprint.empty() ||
        request.designVector->canonicalBytes.empty() ||
        request.designVector->canonicalBytes != canonicalVector.vector.canonicalBytes ||
        request.designVector->fingerprint != canonicalVector.vector.fingerprint) {
        addError(result.diagnostics, "CANDIDATE_COMPILE_DESIGN_VECTOR_FINGERPRINT_MISSING",
                 "designVector",
                 "Candidate compilation requires a canonical design vector whose bytes and fingerprint match its values.");
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    std::map< std::string, DerivedValue > resolvedValues;
    std::map< std::string, std::string > discreteOptions;
    for (std::size_t index = 0; index < request.designVector->values.size(); ++index) {
        const DesignVariableDefinition& variable = request.designSpace->independentVariables[index];
        const DesignVectorValue& value = request.designVector->values[index];
        resolvedValues[variable.id] = {value.engineeringValue, value.unit};
        discreteOptions[variable.id] = value.discreteOptionId;
    }

    std::map< std::string, DerivedValue > derivedByVariableId;
    if (!request.designSpace->derivedVariables.empty()) {
        if (request.designSpace->derivedExpressions.empty()) {
            addError(result.diagnostics, "CANDIDATE_COMPILE_DERIVED_EXPRESSION_MISSING",
                     "designSpace.derivedExpressions",
                     "Derived variables require their compiled expressions.");
        } else {
            const DerivedExpressionEvaluationResult evaluated = DependencyGraph::evaluate(
                request.designSpace->derivedExpressions, resolvedValues);
            result.diagnostics.insert(result.diagnostics.end(), evaluated.diagnostics.begin(),
                                      evaluated.diagnostics.end());
            if (evaluated.ok) {
                for (const DesignVariableDefinition& variable : request.designSpace->derivedVariables) {
                    const auto expression = evaluated.values.find(variable.derivedExpressionId);
                    if (expression == evaluated.values.end()) {
                        addError(result.diagnostics, "CANDIDATE_COMPILE_DERIVED_VALUE_MISSING",
                                 variable.id, "A derived variable did not receive an evaluated value.");
                        continue;
                    }
                    if (expression->second.unit != variable.unit) {
                        addError(result.diagnostics, "CANDIDATE_COMPILE_DERIVED_UNIT_MISMATCH",
                                 variable.id, "A derived value unit does not match its variable declaration.");
                        continue;
                    }
                    resolvedValues[variable.id] = expression->second;
                    derivedByVariableId[variable.id] = expression->second;
                }
            }
        }
    }
    if (containsError(result.diagnostics)) {
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    std::vector< ParameterBinding > bindings = request.designSpace->resolvedBindings;
    std::sort(bindings.begin(), bindings.end(), [](const ParameterBinding& first,
                                                   const ParameterBinding& second) {
        return first.id < second.id;
    });
    std::vector< CandidatePatch > patches;
    for (const ParameterBinding& binding : bindings) {
        std::vector< const DesignVariableDefinition* > owningVariables;
        for (const DesignVariableDefinition& variable : request.designSpace->independentVariables)
            if (variable.bindingId == binding.id) owningVariables.push_back(&variable);
        for (const DesignVariableDefinition& variable : request.designSpace->derivedVariables)
            if (variable.bindingId == binding.id) owningVariables.push_back(&variable);
        if (owningVariables.empty()) {
            addError(result.diagnostics, "CANDIDATE_COMPILE_BINDING_VARIABLE_MISSING", binding.id,
                     "Every resolved binding must be owned by an active variable.");
            continue;
        }

        const std::string group = owningVariables.front()->groupId;
        std::vector< ResolvedAdapterValue > values;
        for (const DesignVariableDefinition& variable : request.designSpace->independentVariables) {
            if (variable.groupId != group || variable.bindingId.empty()) continue;
            const auto value = resolvedValues.find(variable.id);
            if (value != resolvedValues.end())
                values.push_back({variable.id, value->second.unit, value->second.value,
                                  discreteOptions[variable.id],
                                  variable.semanticKind, group, binding.jointLimitScope});
        }
        for (const DesignVariableDefinition& variable : request.designSpace->derivedVariables) {
            if (variable.groupId != group || variable.bindingId.empty()) continue;
            const auto value = resolvedValues.find(variable.id);
            if (value != resolvedValues.end())
                values.push_back({variable.id, value->second.unit, value->second.value,
                                  discreteOptions[variable.id],
                                  variable.semanticKind, group, binding.jointLimitScope});
        }
        if (group.empty()) {
            values.clear();
            for (const DesignVariableDefinition* variable : owningVariables) {
                const auto value = resolvedValues.find(variable->id);
                if (value != resolvedValues.end())
                    values.push_back({variable->id, value->second.unit, value->second.value,
                                      discreteOptions[variable->id],
                                      variable->semanticKind, group, binding.jointLimitScope});
            }
        }
        std::sort(values.begin(), values.end(), [](const ResolvedAdapterValue& first,
                                                   const ResolvedAdapterValue& second) {
            return first.variableId < second.variableId;
        });

        const AdapterPatchCompileResult patch = request.adapterRegistry->compilePatch(
            {request.baseline, &binding, values}, *request.capabilities);
        result.diagnostics.insert(result.diagnostics.end(), patch.diagnostics.begin(),
                                  patch.diagnostics.end());
        if (patch.ok)
            patches.push_back(patch.patch);
        else
            addError(result.diagnostics, "CANDIDATE_COMPILE_ADAPTER_FAILED", binding.id,
                     "The registered adapter did not produce a valid candidate patch.");
    }
    if (containsError(result.diagnostics)) {
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    const CandidatePatchMergeResult merged = CandidatePatchMerger::merge(patches);
    result.diagnostics.insert(result.diagnostics.end(), merged.diagnostics.begin(),
                              merged.diagnostics.end());
    if (!merged.ok) {
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    const CandidatePatchApplyResult applied = CandidatePatchApplier::apply(*request.baseline,
                                                                            merged.patch);
    result.diagnostics.insert(result.diagnostics.end(), applied.diagnostics.begin(),
                              applied.diagnostics.end());
    if (!applied.ok) {
        result.candidate.diagnostics = result.diagnostics;
        return result;
    }

    result.candidate.status = CandidateCompileStatus::Compiled;
    result.candidate.designVector = *request.designVector;
    result.candidate.kinematicModel = applied.model;
    result.candidate.derivedValues = derivedByVariableId;
    result.candidate.artifactFingerprints = applied.generatedArtifacts;
    result.candidate.diagnostics = result.diagnostics;
    result.candidate.fingerprint = candidateFingerprint(
        *request.designSpace, *request.designVector, result.candidate.derivedValues,
        result.candidate.kinematicModel, result.candidate.artifactFingerprints);
    result.candidate.candidateId = result.candidate.fingerprint;
    result.ok = true;
    return result;
}

}    // namespace rws
