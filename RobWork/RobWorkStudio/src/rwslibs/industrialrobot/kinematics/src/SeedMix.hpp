/**
 * @file   SeedMix.hpp
 * @brief  kinematics 单元内共享的确定性种子派生（splitmix64 流种子混合
 *         ——审查 20261021 G12 收拢：此前 Ik/Sampling/Envelope/Workspace
 *         四个 TU 各持逐字相同的自持副本，算法换代需四处同步且耦合身份
 *         codec 版本，存在漂移风险）。
 *
 * 设计依据：
 *   - KIN-P5/审查 A06 修复（splitmix64 双射 avalanche——异种子必异序列
 *     在代数上严格成立）；
 *   - 审查 20261021 G12（四副本漂移风险——收拢为单元内共享私有头）；
 *   - §3.4 确定性总约定（随机性唯一来源＝seed 派生的确定性序列；无时钟
 *     /环境熵；算法公开定版、跨平台位级一致）。
 *
 * 私有头纪律：本头位于 src/（不入 include/ 公共面）——仅 kinematics 单
 * 元内部消费，R-2 跨单元私有头禁令不涉及。
 *
 * ★ 换代纪律：算法再换代时必须同步评估 IkRequest 身份/sampleset 身份
 * codec 版本（同种子不同算法不得假借同一身份——§8.4 版本核对的本意），
 * 并同步四处消费面回归钉（IkTest/EnvelopeTest/RegionCoverageTest/
 * WorkspaceTest 的种子用例）。
 */
#ifndef IRD_KINEMATICS_SRC_SEEDMIX_HPP
#define IRD_KINEMATICS_SRC_SEEDMIX_HPP

#include <cstdint>

namespace sdurws {
namespace ird {
namespace kinematics {
namespace kseed {

/// splitmix64（确定性伪随机序列源；算法公开定版、跨平台位级一致）。
inline std::uint64_t splitmix64(std::uint64_t& state)
{
    state += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

/// (seed, 序号) 二元组的完整 64 位流种子派生（KIN-P5/审查 A06 修复——
/// 唯一混合点：随机初值流/采样流/包络探针流的每项状态由此产出）。
///
/// 为什么不用旧式 "seed·2³²＋k"：uint64 乘法按模 2⁶⁴ 回卷，seed 的
/// 高 32 位与 k 写进同一加法域——seed=1 与 seed=2³²＋1 的第 k 项状态
/// 完全相同（1·2³²＋k ≡ (2³²＋1)·2³²＋k mod 2⁶⁴），整个序列对 seed
/// 高 32 位不敏感（审查 A06 反例：不同 64 位种子生成相同序列，多样性
/// 来源被静默丢弃）。
///
/// 派生规则（确定性——同 (seed, index) 同输出；跨平台位级一致）：
///   a ＝ splitmix64(seed)——seed 全 64 位 avalanche（高位翻转雪崩到
///       全字宽，任何 1 位 seed 差异扩散为约半数位差异）；
///   b ＝ splitmix64(index ⊕ 黄金常量)——序号独立 avalanche（⊕常量
///       把 index=0 拉离 a 的恒等点，防 a⊕0=a 的平凡回归）；
///   输出 ＝ splitmix64(a ⊕ b)——再 avalanche 消除异或的线性结构。
/// 三步后 (seed, index) 任一输入位的改变都以约 1/2 概率翻转输出的任
/// 一位——两个不同 64 位 seed 生成不同序列（除非发生 SHA 级碰撞，对
/// splitmix64 双射 avalanche 实际不可构造）。
inline std::uint64_t streamSeedFor(std::uint64_t seed, std::uint64_t index)
{
    std::uint64_t s = seed;
    const std::uint64_t a = splitmix64(s);
    std::uint64_t t = index ^ 0x9E3779B97F4A7C15ull;
    const std::uint64_t b = splitmix64(t);
    std::uint64_t u = a ^ b;
    return splitmix64(u);
}

}  // namespace kseed
}  // namespace kinematics
}  // namespace ird
}  // namespace sdurws

#endif  // IRD_KINEMATICS_SRC_SEEDMIX_HPP
