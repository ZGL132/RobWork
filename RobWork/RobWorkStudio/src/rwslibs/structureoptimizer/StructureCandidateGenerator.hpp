#ifndef RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEGENERATOR_HPP
#define RWS_STRUCTUREOPTIMIZATION_STRUCTURECANDIDATEGENERATOR_HPP

#include "StructureOptimizationTypes.hpp"

namespace rws {

class StructureCandidateGenerator {
  public:
    static std::vector<std::vector<double>> randomUniform(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);

    static std::vector<std::vector<double>> latinHypercube(
        const std::vector<StructureDesignVariable>& variables,
        int count, unsigned int seed);

    static std::vector<std::vector<double>> grid(
        const std::vector<StructureDesignVariable>& variables,
        int stepsPerVariable, int maximumCount);

    static double quantize(double value, const StructureDesignVariable& variable);

  private:
    static double randomDouble(unsigned int& state);
};

} // namespace rws
#endif
