/**
 * @file IdentityRandom.hpp
 * @brief 身份生成的单元私有播种边界，供固定熵回归验证独立流。
 * 依据：units/core.md §4.1/§5.1；CORE-T12。无共享状态，不作为公共随机服务。
 */
#ifndef SDURWS_IRD_CORE_IDENTITY_RANDOM_HPP
#define SDURWS_IRD_CORE_IDENTITY_RANDOM_HPP
#include <array>
#include <cstdint>
#include <random>
namespace sdurws::ird::core::detail {
/**
 * @brief 以调用方私有的均匀熵源建立身份引擎。
 * @param entropy [in,out] 满足 UniformRandomBitGenerator 的局部熵源，调用同步消费。
 * @return 线程私有 MT 引擎；熵源失败原样抛出，不伪造身份。
 */
template<class EntropySource>
std::mt19937_64 makeIdentityEngine(EntropySource& entropy)
{
    // CORE-T12/U-1：取满128位熵再交标准seed_seq混合，输出位宽不能弥补单字种子。
    std::array<std::uint32_t,4> words{};
    std::uniform_int_distribution<std::uint32_t> word(0,UINT32_MAX);
    for (auto& value : words) { value=word(entropy); }
    std::seed_seq seed(words.begin(),words.end());
    return std::mt19937_64{seed};
}
} // namespace sdurws::ird::core::detail
#endif // SDURWS_IRD_CORE_IDENTITY_RANDOM_HPP
