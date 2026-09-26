/**
 * @file   CanonicalCodec.hpp
 * @brief  kinematics 私有实现头——canonical 编码原语（定宽小端 u32/f64
 *         与小写十六进制）在 T04 各翻译单元间的共享点。
 *
 * 设计依据：
 *   - units/kinematics.md §4.4（canonical 编码纪律——"定宽小端＋字段
 *     定序＋集合字典序"）、§6.1（ConfigurationSignature——全精度定宽）
 *   - R-2 红线：私有实现头置于 src/（不在 include/ 公共面）——仅本单元
 *     实现文件可包含（构建目标以 include 路径约束，跨单元不可达）。
 *
 * 背景（为什么共享而非各 TU 复制）：Fk.cpp 的编码原语为 T03 已交付面
 * （文件局部匿名实现，不回改）；T04 起新增的多个编码点
 * （ConfigurationSignature／解集载荷）共用同一字节序语义——共享一份
 * 实现（inline，头内定义）避免逐字节语义的第二书写点漂移（NFR-COR-02
 * 的实现面保障）。
 *
 * 线程安全：全部为纯函数；确定性：位模式直写（含 +∞——位表示确定），
 * 无格式化/环境依赖（NFR-COR-01）。
 */

#ifndef SDURWS_IRD_KINEMATICS_SRC_CANONICALCODEC_HPP
#define SDURWS_IRD_KINEMATICS_SRC_CANONICALCODEC_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sdurws::ird::kinematics::detail {

/// 追加小端 u32（4 字节，低位在前——定宽小端纪律）。
inline void putU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
    }
}

/// 追加小端 u64（8 字节，低位在前）。
inline void putU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFull));
    }
}

/// 追加小端 f64（IEEE754 位模式经 memcpy 取得——免别名 UB；含 +∞，
/// 位模式确定——确定性承载面）。
inline void putF64(std::vector<std::uint8_t>& out, double v)
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "f64 位模式直写要求 8 字节 double");
    std::memcpy(&bits, &v, sizeof(bits));
    putU64(out, bits);
}

/// 追加字节串（长度不写入——由调用方按布局先写长度或定长）。
inline void putBytes(std::vector<std::uint8_t>& out, const void* data, std::size_t n)
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + n);
}

/// 字节向量转小写十六进制（记录键文本面——ConfigurationSignature 等；
/// 同字节同串——确定性）。
inline std::string toHex(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[(b >> 4) & 0x0Fu]);
        out.push_back(kDigits[b & 0x0Fu]);
    }
    return out;
}

}  // namespace sdurws::ird::kinematics::detail

#endif  // SDURWS_IRD_KINEMATICS_SRC_CANONICALCODEC_HPP
