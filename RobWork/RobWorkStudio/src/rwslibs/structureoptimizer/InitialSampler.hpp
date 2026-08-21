#ifndef RWS_STRUCTUREOPTIMIZATION_INITIALSAMPLER_HPP
#define RWS_STRUCTUREOPTIMIZATION_INITIALSAMPLER_HPP

#include "CompiledDesignSpace.hpp"
#include "DesignVector.hpp"
#include "DeterministicSeed.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace rws {

enum class InitialSamplingMethod { Random, LatinHypercube, Grid };

/** 初始候选池的冻结采样配置。 */
struct InitialSamplingSpec
{
    InitialSamplingMethod method = InitialSamplingMethod::Random;
    std::size_t count = 0;
    std::size_t gridStepsPerVariable = 0;
    std::size_t maximumCount = 0;
    std::uint64_t rootSeed = 0;
};

/** 带稳定索引和设计向量指纹的候选样本。 */
struct InitialSampleCandidate
{
    std::size_t index = 0;
    std::uint64_t seed = 0;
    DesignVector vector;
};

struct InitialSamplingResult
{
    bool ok = false;
    std::vector<InitialSampleCandidate> candidates;
    std::vector<StructureOptimizationDiagnostic> diagnostics;
};

class InitialSampler
{
  public:
    static InitialSamplingResult generate(const CompiledDesignSpace& designSpace,
                                          const InitialSamplingSpec& spec);
};

} // namespace rws

#endif
