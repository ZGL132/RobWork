#include "InitialSampler.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace rws {
namespace {

void error(InitialSamplingResult& result, const std::string& code,
           const std::string& message)
{
    StructureOptimizationDiagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = "Error";
    diagnostic.subsystem = "initial-sampler";
    diagnostic.stage = "preflight";
    diagnostic.message = message;
    result.diagnostics.push_back(diagnostic);
}

std::vector<const DesignVariableDefinition*> canonicalVariables(
    const CompiledDesignSpace& space)
{
    std::vector<const DesignVariableDefinition*> result;
    for (const CanonicalVectorSchemaEntry& entry : space.canonicalVectorSchema) {
        const auto found = std::find_if(
            space.independentVariables.begin(), space.independentVariables.end(),
            [&entry](const DesignVariableDefinition& variable) { return variable.id == entry.variableId; });
        if (found == space.independentVariables.end()) return {};
        result.push_back(&*found);
    }
    return result;
}

double quantize(const DesignVariableDefinition& variable, double value)
{
    if (variable.domain == VariableDomain::Discrete) return 0.0;
    if (variable.domain == VariableDomain::Integer || variable.step > 0.0) {
        const double step = variable.step > 0.0 ? variable.step : 1.0;
        value = variable.minimum + std::round((value - variable.minimum) / step) * step;
    }
    return std::max(variable.minimum, std::min(variable.maximum, value));
}

std::string defaultOption(const DesignVariableDefinition& variable)
{
    return variable.discreteOptions.empty() ? std::string() : variable.discreteOptions.front().id;
}

std::vector<EngineeringDesignValue> nominal(const std::vector<const DesignVariableDefinition*>& variables)
{
    std::vector<EngineeringDesignValue> values;
    values.reserve(variables.size());
    for (const auto* variable : variables) {
        const double value = variable->domain == VariableDomain::Discrete
            ? 0.0 : quantize(*variable, variable->nominalValue);
        values.push_back({variable->id, variable->unit, value, defaultOption(*variable)});
    }
    return values;
}

bool appendCandidate(InitialSamplingResult& result, const CompiledDesignSpace& space,
                     const std::vector<const DesignVariableDefinition*>& variables,
                     const std::vector<EngineeringDesignValue>& values,
                     std::set<std::string>& fingerprints, std::size_t index,
                     std::uint64_t seed)
{
    const DesignVectorResult encoded = DesignVectorCodec::fromEngineering(space, values);
    if (!encoded.ok) {
        result.diagnostics.insert(result.diagnostics.end(), encoded.diagnostics.begin(),
                                  encoded.diagnostics.end());
        return false;
    }
    if (!fingerprints.insert(encoded.vector.fingerprint).second) return true;
    result.candidates.push_back({index, seed, encoded.vector});
    return true;
}

} // namespace

InitialSamplingResult InitialSampler::generate(const CompiledDesignSpace& designSpace,
                                               const InitialSamplingSpec& spec)
{
    InitialSamplingResult result;
    const auto variables = canonicalVariables(designSpace);
    if (variables.empty() || variables.size() != designSpace.canonicalVectorSchema.size()) {
        error(result, "INITIAL_SAMPLER_SCHEMA_INVALID", "Canonical design-vector schema is invalid.");
        return result;
    }
    if (spec.method != InitialSamplingMethod::Grid && spec.count == 0) {
        error(result, "INITIAL_SAMPLER_COUNT_INVALID", "Random and LHS sampling require a positive count.");
        return result;
    }
    if (spec.method == InitialSamplingMethod::Grid &&
        (spec.gridStepsPerVariable == 0 || spec.maximumCount == 0)) {
        error(result, "INITIAL_SAMPLER_GRID_INVALID", "Grid sampling requires positive steps and maximumCount.");
        return result;
    }
    if (spec.method == InitialSamplingMethod::Grid) {
        std::size_t combinations = 1;
        for (const auto* variable : variables) {
            if (combinations > spec.maximumCount / spec.gridStepsPerVariable) {
                error(result, "INITIAL_SAMPLER_GRID_EXCEEDS_LIMIT", "Grid combination count exceeds maximumCount.");
                return result;
            }
            combinations *= spec.gridStepsPerVariable;
        }
    }

    // baseline 永远先写入 index 0；后续重复向量只保留第一次出现的样本。
    std::set<std::string> fingerprints;
    if (!appendCandidate(result, designSpace, variables, nominal(variables), fingerprints, 0,
                         DeterministicSeed::candidateSeed(spec.rootSeed, 0)))
        return result;

    std::mt19937_64 rng(spec.rootSeed);
    std::vector<std::vector<EngineeringDesignValue>> generated;
    if (spec.method == InitialSamplingMethod::Random) {
        generated.resize(spec.count, nominal(variables));
        for (auto& row : generated) {
            for (std::size_t index = 0; index < variables.size(); ++index) {
                const auto* variable = variables[index];
                if (variable->domain == VariableDomain::Discrete) {
                    std::uniform_int_distribution<std::size_t> pick(0, variable->discreteOptions.size() - 1);
                    row[index].discreteOptionId = variable->discreteOptions[pick(rng)].id;
                } else {
                    std::uniform_real_distribution<double> sample(variable->minimum, variable->maximum);
                    row[index].engineeringValue = quantize(*variable, sample(rng));
                }
            }
        }
    } else if (spec.method == InitialSamplingMethod::LatinHypercube) {
        generated.resize(spec.count, nominal(variables));
        for (std::size_t index = 0; index < variables.size(); ++index) {
            const auto* variable = variables[index];
            if (variable->domain == VariableDomain::Discrete) {
                for (std::size_t row = 0; row < spec.count; ++row)
                    generated[row][index].discreteOptionId = variable->discreteOptions[row % variable->discreteOptions.size()].id;
            } else {
                std::vector<double> strata(spec.count);
                for (std::size_t row = 0; row < spec.count; ++row) {
                    std::uniform_real_distribution<double> within(0.0, 1.0);
                    strata[row] = quantize(*variable, variable->minimum +
                        (static_cast<double>(row) + within(rng)) *
                        (variable->maximum - variable->minimum) / spec.count);
                }
                std::shuffle(strata.begin(), strata.end(), rng);
                for (std::size_t row = 0; row < spec.count; ++row)
                    generated[row][index].engineeringValue = strata[row];
            }
        }
    } else {
        generated.resize(spec.maximumCount, nominal(variables));
        for (std::size_t row = 0; row < spec.maximumCount; ++row) {
            std::size_t quotient = row;
            for (std::size_t index = variables.size(); index-- > 0;) {
                const auto* variable = variables[index];
                const std::size_t level = quotient % spec.gridStepsPerVariable;
                quotient /= spec.gridStepsPerVariable;
                if (variable->domain == VariableDomain::Discrete) {
                    const std::size_t option = level % variable->discreteOptions.size();
                    generated[row][index].discreteOptionId = variable->discreteOptions[option].id;
                } else {
                    const double fraction = spec.gridStepsPerVariable == 1 ? 0.0 :
                        static_cast<double>(level) / (spec.gridStepsPerVariable - 1);
                    generated[row][index].engineeringValue = quantize(
                        *variable, variable->minimum + fraction * (variable->maximum - variable->minimum));
                }
            }
        }
    }

    for (const auto& row : generated) {
        if (result.candidates.size() >= (spec.method == InitialSamplingMethod::Grid
                ? spec.maximumCount + 1 : spec.count + 1)) break;
        const std::size_t index = result.candidates.size();
        if (!appendCandidate(result, designSpace, variables, row, fingerprints, index,
                             DeterministicSeed::candidateSeed(spec.rootSeed, index)))
            return result;
    }
    result.ok = true;
    return result;
}

} // namespace rws
