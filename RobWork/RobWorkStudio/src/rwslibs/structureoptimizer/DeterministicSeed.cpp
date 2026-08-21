#include "DeterministicSeed.hpp"

namespace rws {

std::uint64_t DeterministicSeed::candidateSeed(std::uint64_t rootSeed,
                                               std::size_t stableIndex)
{
    std::uint64_t value = rootSeed + 0x9e3779b97f4a7c15ULL * (stableIndex + 1);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

} // namespace rws
