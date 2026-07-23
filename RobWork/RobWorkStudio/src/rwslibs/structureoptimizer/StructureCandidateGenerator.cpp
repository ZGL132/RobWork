#include "StructureCandidateGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace rws {

double StructureCandidateGenerator::randomDouble(unsigned int& state)
{
    // Simple LCG (Numerical Recipes)
    state = 1664525u * state + 1013904223u;
    return static_cast<double>(state) / 4294967296.0;
}

double StructureCandidateGenerator::quantize(double value,
                                              const StructureDesignVariable& variable)
{
    if (variable.step <= 0.0)
        return value;

    double q = variable.minimum +
               std::round((value - variable.minimum) / variable.step) * variable.step;

    if (q < variable.minimum) q = variable.minimum;
    if (q > variable.maximum) q = variable.maximum;
    return q;
}

std::vector<std::vector<double>> StructureCandidateGenerator::randomUniform(
    const std::vector<StructureDesignVariable>& variables,
    int count, unsigned int seed)
{
    std::vector<std::vector<double>> candidates;
    candidates.reserve(static_cast<std::size_t>(count));
    unsigned int state = seed;

    for (int i = 0; i < count; ++i)
    {
        std::vector<double> values(variables.size());
        for (std::size_t j = 0; j < variables.size(); ++j)
        {
            const auto& v = variables[j];
            if (v.enabled)
            {
                double t = randomDouble(state);
                double val = v.minimum + t * (v.maximum - v.minimum);
                values[j] = quantize(val, v);
            }
            else
            {
                values[j] = v.currentValue;
            }
        }
        candidates.push_back(std::move(values));
    }

    return candidates;
}

std::vector<std::vector<double>> StructureCandidateGenerator::latinHypercube(
    const std::vector<StructureDesignVariable>& variables,
    int count, unsigned int seed)
{
    std::mt19937 rng(seed);

    // samples[j] holds the stratum samples for variable j (before permutation)
    std::vector<std::vector<double>> samples(variables.size());

    for (std::size_t j = 0; j < variables.size(); ++j)
    {
        const auto& v = variables[j];
        if (!v.enabled)
        {
            samples[j].resize(static_cast<std::size_t>(count), v.currentValue);
            continue;
        }

        samples[j].resize(static_cast<std::size_t>(count));
        double range       = v.maximum - v.minimum;
        double stratumWidth = range / count;
        std::uniform_real_distribution<double> dist(0.0, stratumWidth);

        for (int i = 0; i < count; ++i)
        {
            double val = v.minimum + static_cast<double>(i) * stratumWidth + dist(rng);
            samples[j][i] = quantize(val, v);
        }

        // Permute the within-stratum order to break the correlation
        std::shuffle(samples[j].begin(), samples[j].end(), rng);
    }

    // Assemble candidates: candidate i gets the i-th (permuted) sample from each variable
    std::vector<std::vector<double>> candidates(
        static_cast<std::size_t>(count),
        std::vector<double>(variables.size()));

    for (int i = 0; i < count; ++i)
    {
        for (std::size_t j = 0; j < variables.size(); ++j)
        {
            candidates[i][j] = samples[j][i];
        }
    }

    return candidates;
}

std::vector<std::vector<double>> StructureCandidateGenerator::grid(
    const std::vector<StructureDesignVariable>& variables,
    int stepsPerVariable, int maximumCount)
{
    if (stepsPerVariable <= 0 || maximumCount <= 0)
        return {};

    // Collect enabled variable indices
    std::vector<std::size_t> enabledIndices;
    for (std::size_t j = 0; j < variables.size(); ++j)
    {
        if (variables[j].enabled)
            enabledIndices.push_back(j);
    }

    int totalEnabled = static_cast<int>(enabledIndices.size());
    if (totalEnabled == 0)
        return {};

    // Odometer-style counter (0 .. stepsPerVariable-1 for each enabled variable)
    std::vector<int> counter(static_cast<std::size_t>(totalEnabled), 0);

    std::vector<std::vector<double>> candidates;
    candidates.reserve(static_cast<std::size_t>(maximumCount));

    while (candidates.size() < static_cast<std::size_t>(maximumCount))
    {
        std::vector<double> values(variables.size());

        // Set disabled variables to currentValue
        for (std::size_t j = 0; j < variables.size(); ++j)
        {
            if (!variables[j].enabled)
                values[j] = variables[j].currentValue;
        }

        // Set enabled variables from the current counter
        for (int k = 0; k < totalEnabled; ++k)
        {
            std::size_t idx = enabledIndices[static_cast<std::size_t>(k)];
            const auto& v   = variables[idx];
            double stepSize = (v.maximum - v.minimum) / stepsPerVariable;
            double val      = v.minimum + counter[static_cast<std::size_t>(k)] * stepSize;
            values[idx]     = quantize(val, v);
        }

        candidates.push_back(std::move(values));

        // Advance the odometer
        int k = totalEnabled - 1;
        counter[static_cast<std::size_t>(k)]++;
        while (k >= 0 && counter[static_cast<std::size_t>(k)] >= stepsPerVariable)
        {
            counter[static_cast<std::size_t>(k)] = 0;
            --k;
            if (k >= 0)
                counter[static_cast<std::size_t>(k)]++;
        }
        if (k < 0)
            break; // All combinations exhausted
    }

    return candidates;
}

} // namespace rws
