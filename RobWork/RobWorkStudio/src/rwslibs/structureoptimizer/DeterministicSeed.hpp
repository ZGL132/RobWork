#ifndef RWS_STRUCTUREOPTIMIZATION_DETERMINISTICSEED_HPP
#define RWS_STRUCTUREOPTIMIZATION_DETERMINISTICSEED_HPP

#include <cstdint>
#include <cstddef>

namespace rws {

class DeterministicSeed
{
  public:
    // 由运行 seed 和稳定候选索引派生，不依赖进程全局随机状态。
    static std::uint64_t candidateSeed(std::uint64_t rootSeed, std::size_t stableIndex);
};

} // namespace rws

#endif
