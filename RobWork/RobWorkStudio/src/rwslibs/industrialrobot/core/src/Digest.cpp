/**
 * @file   Digest.cpp
 * @brief  SHA-256（FIPS 180-2）实现＋摘要类型解析/格式化＋ContentDigester。
 *
 * 设计依据：
 *   - units/core.md §4.2/§5.2（类型与 ContentDigester 契约）、D-05（SHA-256 唯一算法）
 *   - FIPS 180-2（§4.1 消息填充、§4.1.2 函数与常量、§6.2.2 压缩）
 *   - 需求 NFR-COR-02（确定性）、CON-05/06（内容身份）
 *   - 任务契约 tasks/foundation/CORE-T02.json（UT-ID-D：FIPS 180-2 已知向量钉住）
 *
 * 实现说明：教科书式按标准逐条实现（无平台内建依赖——跨平台确定性优先于
 * 吞吐，§4.2 纯函数性承诺；性能优化留给有证据的后续修订，不做投机改写）。
 * 大端序注意事项：SHA-256 全程大端——x86 小端机上装入/输出须显式字节序转换。
 */

#include <sdurws/ird/core/Digest.hpp>

#include <sdurws/ird/core/Errors.hpp>

#include <algorithm>
#include <array>
#include <string>

namespace sdurws::ird::core {

namespace detail {

// ---------------------------------------------------------------------
// "<tag><64 个小写十六进制>" 解析/格式化（与 Identity 的 128 位版本同型规则）
// ---------------------------------------------------------------------
bool tryParseDigest(std::string_view tag, std::string_view text,
                    Digest256* out) noexcept
{
    if (text.size() != tag.size() + 64) {            // 总长固定：tag＋64 hex
        return false;
    }
    for (std::size_t i = 0; i < tag.size(); ++i) {   // tag 逐字符（类型误用边界失败）
        if (text[i] != tag[i]) { return false; }
    }
    const std::size_t body = tag.size();
    for (std::size_t i = 0; i < 64; ++i) {
        const char c = text[body + i];
        unsigned nibble;
        if (c >= '0' && c <= '9')      { nibble = static_cast<unsigned>(c - '0'); }
        else if (c >= 'a' && c <= 'f') { nibble = static_cast<unsigned>(c - 'a') + 10u; }
        else { return false; }                       // 大写/非法字符拒绝（规范文本唯一小写）
        if ((i & 1u) == 0u) { (*out)[i / 2] = static_cast<std::uint8_t>(nibble << 4); }
        else { (*out)[i / 2] = static_cast<std::uint8_t>((*out)[i / 2] | nibble); }
    }
    return true;
}

std::string formatDigest(std::string_view tag, const Digest256& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.reserve(tag.size() + 64);
    s.append(tag);
    for (const std::uint8_t b : bytes) {
        s.push_back(kHex[b >> 4]);
        s.push_back(kHex[b & 0x0Fu]);
    }
    return s;
}

}  // namespace detail

// ---------------------------------------------------------------------
// 摘要类型的核外定义（宏＝与头文件声明成对，参数必须一致）
// ---------------------------------------------------------------------
#define IRD_CORE_IMPLEMENT_DIGEST_TYPE(TYPE_NAME, TAG_LIT)                              \
    TYPE_NAME TYPE_NAME::fromCanonical(std::string_view text) {                         \
        TYPE_NAME d;                                                                    \
        if (!detail::tryParseDigest(TAG_LIT, text, &d.bytes)) {                         \
            const std::size_t n = std::min<std::size_t>(text.size(), 80);               \
            throw CoreError(std::string{"core/identity/parse: " TAG_LIT " 期望 <tag><64 小写 hex>，实际: \""} \
                            + std::string(text.substr(0, n)) + "\"（长度 "                           \
                            + std::to_string(text.size()) + "）");                      \
        }                                                                               \
        return d;                                                                       \
    }                                                                                   \
    std::optional<TYPE_NAME> TYPE_NAME::tryFromCanonical(std::string_view text) noexcept { \
        TYPE_NAME d;                                                                    \
        if (!detail::tryParseDigest(TAG_LIT, text, &d.bytes)) { return std::nullopt; }  \
        return d;                                                                       \
    }                                                                                   \
    std::string TYPE_NAME::toCanonical() const {                                        \
        return detail::formatDigest(TAG_LIT, bytes);                                    \
    }                                                                                   \
    bool TYPE_NAME::isValid() const noexcept {                                          \
        return std::any_of(bytes.begin(), bytes.end(),                                  \
                           [](std::uint8_t b) { return b != 0; });                      \
    }

IRD_CORE_IMPLEMENT_DIGEST_TYPE(ContentVersion, "cv-")
IRD_CORE_IMPLEMENT_DIGEST_TYPE(ContentIdentity, "cid-")

// ---------------------------------------------------------------------
// SHA-256 核心（FIPS 180-2）
// ---------------------------------------------------------------------
namespace {

/// 大端读 32 位（消息字装入——x86 小端机上必须显式转换）。
inline std::uint32_t loadBE32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)  |  static_cast<std::uint32_t>(p[3]);
}

/// 大端写 32 位（摘要输出）。
inline void storeBE32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

/// 右旋转（FIPS 记号 ROTR^n(x)）。
inline std::uint32_t rotr(std::uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

/// 压缩轮常量 K[0..63]：前 64 个素数的立方根小数部分前 32 位（FIPS 180-2 §4.2.3）。
constexpr std::array<std::uint32_t, 64> kSha256K{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

}  // namespace

void ContentDigester::compressBlock(const std::uint8_t* block)
{
    // 消息调度：W[0..15] 直接取自分组（大端），W[16..63] 按 σ0/σ1 扩展（§6.2.2）。
    std::uint32_t w[64];
    for (int t = 0; t < 16; ++t) {
        w[t] = loadBE32(block + 4 * t);
    }
    for (int t = 16; t < 64; ++t) {
        const std::uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        const std::uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
    }

    // 64 轮压缩（工作变量 a..h；函数 Ch/Maj/Σ0/Σ1 见 FIPS §4.1.2）。
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int t = 0; t < 64; ++t) {
        const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + kSha256K[static_cast<std::size_t>(t)] + w[t];
        const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g; g = f; f = e;
        e = d + t1;
        d = c; c = b; b = a;
        a = t1 + t2;
    }

    // 链接变量累加（中途值寄存于工作变量，轮毕回加——FIPS §6.2.2 第 14 步）。
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void ContentDigester::update(const void* data, std::size_t nBytes)
{
    // 幂等禁止（§5.2）：finalize 后实例不可再用——防"半截摘要"静默传播。
    if (finalized_) {
        throw CoreError("core/digest/finalized: finalize 之后不得再 update（摘要已终结，另起新实例）");
    }
    if (nBytes == 0) {           // 空追加合法且不改变状态（§5.2"含空"）
        return;
    }
    const auto* p = static_cast<const std::uint8_t*>(data);
    totalBytes_ += nBytes;

    // 先填满残余缓冲（跨 update 边界的分组连续性）。
    if (buffered_ > 0) {
        const std::size_t need = 64 - buffered_;
        const std::size_t take = (nBytes < need) ? nBytes : need;
        std::copy_n(p, take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
        buffered_ += take;
        p += take;
        nBytes -= take;
        if (buffered_ == 64) {
            compressBlock(buffer_.data());
            buffered_ = 0;
        }
    }
    // 整块直压（不进缓冲——避免无谓拷贝）。
    while (nBytes >= 64) {
        compressBlock(p);
        p += 64;
        nBytes -= 64;
    }
    // 尾部残余入缓冲等待下一 update 或 finalize 的填充。
    if (nBytes > 0) {
        std::copy_n(p, nBytes, buffer_.begin());
        buffered_ = nBytes;
    }
}

Digest256 ContentDigester::finalize()
{
    if (finalized_) {
        throw CoreError("core/digest/finalized: finalize 幂等禁止（二次调用，§5.2）");
    }
    finalized_ = true;

    // FIPS 180-2 §4.1 消息填充：先补 0x80，再补 0 至 56 mod 64，末 8 字节＝原消息位长（大端）。
    const std::uint64_t bitLen = totalBytes_ * 8ULL;
    std::uint8_t pad[72];                        // 最多 1 字节 0x80＋63 字节 0＋8 字节长度
    std::size_t padLen = (buffered_ < 56) ? (56 - buffered_) : (120 - buffered_);
    pad[0] = 0x80;
    std::fill_n(pad + 1, padLen - 1 + 8, static_cast<std::uint8_t>(0));
    for (int i = 0; i < 8; ++i) {                // 位长：大端（无长度截断——uint64 足够任意流）
        pad[padLen + static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>(bitLen >> (56 - 8 * i));
    }
    // 填充走标准压缩路径（复用 update 的缓冲逻辑但绕过 finalized_ 检查——手工分块）。
    const auto feed = [&](const std::uint8_t* src, std::size_t n) {
        while (n > 0) {
            const std::size_t take = (n < 64 - buffered_) ? n : (64 - buffered_);
            std::copy_n(src, take, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
            buffered_ += take;
            src += take;
            n -= take;
            if (buffered_ == 64) {
                compressBlock(buffer_.data());
                buffered_ = 0;
            }
        }
    };
    feed(pad, padLen + 8);
    // 此时 buffered_ 必为 0（填充使总长恰为 64 的倍数）——如不满足即为实现错误。

    // 输出：H0..H7 大端拼接为 32 字节（FIPS §6.2.2 最后一步）。
    Digest256 out{};
    for (int i = 0; i < 8; ++i) {
        storeBE32(out.data() + 4 * i, state_[static_cast<std::size_t>(i)]);
    }
    return out;
}

}  // namespace sdurws::ird::core
