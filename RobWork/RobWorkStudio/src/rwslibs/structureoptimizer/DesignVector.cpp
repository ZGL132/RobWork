#include "DesignVector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>

namespace rws {
namespace {

template< class Result >
void addError(Result& result, const std::string& code, const std::string& fieldPath,
              const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "design-vector";
    diagnostic.stage = "codec";
    diagnostic.fieldPath = fieldPath;
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

bool finite(const double value)
{
    return std::isfinite(value);
}

bool isAlignedIntegerValue(const DesignVariableDefinition& variable, const double value)
{
    const double stepCount = (value - variable.minimum) / variable.step;
    const double rounded = std::round(stepCount);
    const double tolerance = 1e-12 * std::max(1.0, std::fabs(stepCount));
    return std::fabs(stepCount - rounded) <= tolerance;
}

bool containsOption(const DesignVariableDefinition& variable, const std::string& optionId)
{
    return std::any_of(variable.discreteOptions.begin(), variable.discreteOptions.end(),
                       [&optionId](const DiscreteOption& option) { return option.id == optionId; });
}

template< class Result >
bool validateSchema(const CompiledDesignSpace& space, Result& result)
{
    if (space.fingerprint.empty()) {
        addError(result, "DESIGN_VECTOR_SPACE_FINGERPRINT_MISSING", "designSpace.fingerprint",
                 "A compiled design space fingerprint is required.");
        return false;
    }
    if (space.canonicalVectorSchema.size() != space.independentVariables.size()) {
        addError(result, "DESIGN_VECTOR_SCHEMA_MISMATCH", "designSpace.canonicalVectorSchema",
                 "The canonical schema must contain exactly the independent variables.");
        return false;
    }
    std::set< std::string > ids;
    for (std::size_t index = 0; index < space.canonicalVectorSchema.size(); ++index) {
        const CanonicalVectorSchemaEntry& entry = space.canonicalVectorSchema[index];
        const DesignVariableDefinition& variable = space.independentVariables[index];
        if (entry.index != index || entry.variableId != variable.id || entry.unit != variable.unit ||
            entry.variableId.empty() || !ids.insert(entry.variableId).second) {
            addError(result, "DESIGN_VECTOR_SCHEMA_MISMATCH",
                     "designSpace.canonicalVectorSchema[" + std::to_string(index) + "]",
                     "Schema positions must be unique and exactly match independent variables.");
            return false;
        }
        if (variable.domain == VariableDomain::Discrete) {
            std::set< std::string > optionIds;
            for (const DiscreteOption& option : variable.discreteOptions)
                if (option.id.empty() || !optionIds.insert(option.id).second) {
                    addError(result, "DESIGN_VECTOR_DISCRETE_OPTIONS_INVALID", variable.id,
                             "A discrete vector variable requires non-empty, unique stable option IDs.");
                    return false;
                }
            if (!variable.discreteOptions.empty()) continue;
            addError(result, "DESIGN_VECTOR_DISCRETE_OPTIONS_INVALID", variable.id,
                     "A discrete vector variable requires stable option IDs.");
            return false;
        }
        if (!finite(variable.minimum) || !finite(variable.maximum) || !finite(variable.step) ||
            variable.minimum >= variable.maximum || variable.step <= 0.0) {
            addError(result, "DESIGN_VECTOR_VARIABLE_DOMAIN_INVALID", variable.id,
                     "Numeric vector variables require finite, non-degenerate bounds and a positive step.");
            return false;
        }
    }
    return true;
}

void appendText(std::string& bytes, const std::string& value)
{
    bytes.append(std::to_string(value.size()));
    bytes.push_back(':');
    bytes.append(value);
    bytes.push_back(';');
}

void appendDoubleBits(std::string& bytes, const double value)
{
    double canonical = value == 0.0 ? 0.0 : value;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &canonical, sizeof(bits));
    static const char hex[] = "0123456789abcdef";
    for (int shift = 60; shift >= 0; shift -= 4)
        bytes.push_back(hex[(bits >> shift) & 0x0f]);
    bytes.push_back(';');
}

std::string fnv1a64(const std::string& bytes)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

void finalize(DesignVector& vector)
{
    std::string bytes;
    appendText(bytes, "design-vector-v1");
    appendText(bytes, vector.designSpaceFingerprint);
    bytes.append(std::to_string(vector.schemaVersion));
    bytes.push_back(';');
    for (const DesignVectorValue& value : vector.values) {
        appendText(bytes, value.variableId);
        bytes.append(std::to_string(static_cast< int >(value.unit)));
        bytes.push_back(';');
        if (value.discreteOptionId.empty())
            appendDoubleBits(bytes, value.engineeringValue);
        else {
            appendText(bytes, "discrete");
            appendText(bytes, value.discreteOptionId);
        }
    }
    vector.canonicalBytes = bytes;
    vector.fingerprint = fnv1a64(bytes);
}

bool validateEngineeringValue(const DesignVariableDefinition& variable, const double value,
                              DesignVectorResult& result)
{
    if (!finite(value)) {
        addError(result, "DESIGN_VECTOR_NONFINITE", variable.id,
                 "Design-vector numeric values must be finite.");
        return false;
    }
    if (value < variable.minimum || value > variable.maximum) {
        addError(result, "DESIGN_VECTOR_OUT_OF_RANGE", variable.id,
                 "Engineering value is outside the declared variable bounds.");
        return false;
    }
    if (variable.domain == VariableDomain::Integer && !isAlignedIntegerValue(variable, value)) {
        addError(result, "DESIGN_VECTOR_INTEGER_STEP_MISALIGNED", variable.id,
                 "Integer variable values must align exactly to the declared step.");
        return false;
    }
    return true;
}

}    // namespace

DesignVectorResult DesignVectorCodec::fromNormalized(
    const CompiledDesignSpace& designSpace,
    const std::vector< NormalizedDesignValue >& normalizedValues)
{
    DesignVectorResult result;
    if (!validateSchema(designSpace, result)) return result;
    if (normalizedValues.size() != designSpace.canonicalVectorSchema.size()) {
        addError(result, "DESIGN_VECTOR_LENGTH_MISMATCH", "normalizedValues",
                 "The normalized vector length must match the canonical schema.");
        return result;
    }
    result.vector.schemaVersion = designSpace.schemaVersion;
    result.vector.designSpaceFingerprint = designSpace.fingerprint;
    for (std::size_t index = 0; index < normalizedValues.size(); ++index) {
        const DesignVariableDefinition& variable = designSpace.independentVariables[index];
        const NormalizedDesignValue& input = normalizedValues[index];
        if (variable.domain == VariableDomain::Discrete) {
            if (!finite(input.normalizedValue) || input.normalizedValue != 0.0 ||
                input.discreteOptionId.empty() ||
                !containsOption(variable, input.discreteOptionId)) {
                addError(result, "DESIGN_VECTOR_DISCRETE_OPTION_INVALID", variable.id,
                         "Discrete values use one declared stable option ID and a zero numeric placeholder.");
                return result;
            }
            result.vector.values.push_back({variable.id, variable.unit, 0.0, input.discreteOptionId});
            continue;
        }
        if (!finite(input.normalizedValue)) {
            addError(result, "DESIGN_VECTOR_NONFINITE", variable.id,
                     "Normalized vector values must be finite.");
            return result;
        }
        if (!input.discreteOptionId.empty()) {
            addError(result, "DESIGN_VECTOR_DISCRETE_ID_UNEXPECTED", variable.id,
                     "Only discrete variables may carry a discrete option ID.");
            return result;
        }
        if (input.normalizedValue < 0.0 || input.normalizedValue > 1.0) {
            addError(result, "DESIGN_VECTOR_OUT_OF_RANGE", variable.id,
                     "Normalized values must be within the closed interval [0, 1].");
            return result;
        }
        const double engineering = variable.minimum + input.normalizedValue *
            (variable.maximum - variable.minimum);
        if (!validateEngineeringValue(variable, engineering, result)) return result;
        result.vector.values.push_back({variable.id, variable.unit, engineering, ""});
    }
    finalize(result.vector);
    result.ok = true;
    return result;
}

DesignVectorResult DesignVectorCodec::fromEngineering(
    const CompiledDesignSpace& designSpace,
    const std::vector< EngineeringDesignValue >& engineeringValues)
{
    DesignVectorResult result;
    if (!validateSchema(designSpace, result)) return result;
    if (engineeringValues.size() != designSpace.canonicalVectorSchema.size()) {
        addError(result, "DESIGN_VECTOR_LENGTH_MISMATCH", "engineeringValues",
                 "The engineering vector length must match the canonical schema.");
        return result;
    }
    result.vector.schemaVersion = designSpace.schemaVersion;
    result.vector.designSpaceFingerprint = designSpace.fingerprint;
    for (std::size_t index = 0; index < engineeringValues.size(); ++index) {
        const DesignVariableDefinition& variable = designSpace.independentVariables[index];
        const EngineeringDesignValue& input = engineeringValues[index];
        if (input.variableId != variable.id || input.unit != variable.unit) {
            addError(result, "DESIGN_VECTOR_SCHEMA_MISMATCH", "engineeringValues[" +
                     std::to_string(index) + "]", "Engineering values must follow the canonical schema.");
            return result;
        }
        if (!finite(input.engineeringValue)) {
            addError(result, "DESIGN_VECTOR_NONFINITE", variable.id,
                     "Design-vector numeric values must be finite.");
            return result;
        }
        if (variable.domain == VariableDomain::Discrete) {
            if (input.engineeringValue != 0.0 || input.discreteOptionId.empty() ||
                !containsOption(variable, input.discreteOptionId)) {
                addError(result, "DESIGN_VECTOR_DISCRETE_OPTION_INVALID", variable.id,
                         "Discrete values use one declared stable option ID and a zero numeric placeholder.");
                return result;
            }
            result.vector.values.push_back({variable.id, variable.unit, 0.0, input.discreteOptionId});
            continue;
        }
        if (!input.discreteOptionId.empty()) {
            addError(result, "DESIGN_VECTOR_DISCRETE_ID_UNEXPECTED", variable.id,
                     "Only discrete variables may carry a discrete option ID.");
            return result;
        }
        if (!validateEngineeringValue(variable, input.engineeringValue, result)) return result;
        result.vector.values.push_back({variable.id, variable.unit, input.engineeringValue, ""});
    }
    finalize(result.vector);
    result.ok = true;
    return result;
}

NormalizedDesignVectorResult DesignVectorCodec::toNormalized(
    const CompiledDesignSpace& designSpace, const DesignVector& vector)
{
    NormalizedDesignVectorResult result;
    if (!validateSchema(designSpace, result)) return result;
    if (vector.designSpaceFingerprint != designSpace.fingerprint ||
        vector.values.size() != designSpace.canonicalVectorSchema.size()) {
        addError(result, "DESIGN_VECTOR_SCHEMA_MISMATCH", "vector",
                 "Design vector fingerprint and length must match the compiled schema.");
        return result;
    }
    result.designSpaceFingerprint = designSpace.fingerprint;
    for (std::size_t index = 0; index < vector.values.size(); ++index) {
        const DesignVariableDefinition& variable = designSpace.independentVariables[index];
        const DesignVectorValue& value = vector.values[index];
        if (value.variableId != variable.id || value.unit != variable.unit) {
            addError(result, "DESIGN_VECTOR_SCHEMA_MISMATCH", "vector.values[" +
                     std::to_string(index) + "]", "Design vector values must follow the canonical schema.");
            return result;
        }
        if (variable.domain == VariableDomain::Discrete) {
            if (!finite(value.engineeringValue) || value.engineeringValue != 0.0 ||
                value.discreteOptionId.empty() ||
                !containsOption(variable, value.discreteOptionId)) {
                addError(result, "DESIGN_VECTOR_DISCRETE_OPTION_INVALID", variable.id,
                         "Discrete values use one declared stable option ID and a zero numeric placeholder.");
                return result;
            }
            result.values.push_back({0.0, value.discreteOptionId});
            continue;
        }
        if (!value.discreteOptionId.empty()) {
            addError(result, "DESIGN_VECTOR_DISCRETE_ID_UNEXPECTED", variable.id,
                     "Only discrete variables may carry a discrete option ID.");
            return result;
        }
        DesignVectorResult validation;
        if (!validateEngineeringValue(variable, value.engineeringValue, validation)) {
            result.diagnostics = validation.diagnostics;
            return result;
        }
        const double normalized = (value.engineeringValue - variable.minimum) /
            (variable.maximum - variable.minimum);
        if (!finite(normalized) || normalized < 0.0 || normalized > 1.0) {
            addError(result, "DESIGN_VECTOR_OUT_OF_RANGE", variable.id,
                     "Engineering values must map to the closed normalized interval [0, 1].");
            return result;
        }
        result.values.push_back({normalized == 0.0 ? 0.0 : normalized, ""});
    }
    result.ok = true;
    return result;
}

}    // namespace rws
