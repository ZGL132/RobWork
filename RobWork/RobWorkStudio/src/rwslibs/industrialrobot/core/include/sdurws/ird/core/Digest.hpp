/**
 * @file   Digest.hpp
 * @brief  摘要类型——Digest256/ContentVersion/ContentIdentity＋SHA-256 增量摘要器。
 *
 * 设计依据：
 *   - units/core.md §4.2（类型表与规范文本）、§5.2（接口签名：ContentDigester
 *     增量式、finalize 后再用抛 CoreError）、§4.10（CoreError 前缀）、D-05（SHA-256）
 *   - 需求 CON-05/06（内容身份＝缓存键与失效判据）、NFR-COR-02（确定性：
 *     同字节序列→同摘要，与平台/编译器/线程无关）
 *   - 任务契约 tasks/foundation/CORE-T02.json（≙WP-03-T02，UT-ID-D 载体——
 *     FIPS 180-2 已知向量钉住）
 *
 * 背景说明（§4.2 责任边界——本头是"字节→摘要"的唯一实现点）：core 只提供
 * 摘要算法；对"什么"做摘要（canonical 序列化）归各所有者单元——切片归 evidence、
 * 策略归 policy、名称映射归 runtime、对象编码归 project/io。跨单元内容身份
 * 可比的前提＝相同 canonical 序列化（交接项，§10.3）。
 *
 * 确定性来源（NFR-COR-02）：SHA-256 为纯字节变换——无随机、无时间、无环境
 * 依赖；实现按 FIPS 180-2 逐条对照（大端序、消息填充、64 轮压缩），不引入
 * 任何平台差异分支。
 *
 * 线程安全：ContentDigester 实例非线程安全（每线程各持实例，§4.2 原文）；
 * Digest256/ContentVersion/ContentIdentity 为纯值类型。
 */

#ifndef SDURWS_IRD_CORE_DIGEST_HPP
#define SDURWS_IRD_CORE_DIGEST_HPP

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sdurws::ird::core {

/// 256 位摘要值（§4.2）：全零＝空（保留值纪律，与 Id128 同源）。
using Digest256 = std::array<std::uint8_t, 32>;

// detail 层：tag 化 32 字节类型的解析/格式化单点实现（非公共契约，同 Identity.hpp 纪律）。
namespace detail {

/// 严格解析 "<tag><64 个小写十六进制>"：规则与 tryParseId128 同型（长度 64）。
bool tryParseDigest(std::string_view tag, std::string_view text,
                    Digest256* out) noexcept;
/// 格式化 "<tag><64 个小写十六进制>"。
std::string formatDigest(std::string_view tag, const Digest256& bytes);

/// FNV-1a 128 哈希（std::hash 共用算法——声明复述自 Identity.hpp，定义唯一在其 .cpp）。
void fnv1a128(const std::uint8_t* data, std::size_t n,
              std::uint64_t* outHi, std::uint64_t* outLo) noexcept;

}  // namespace detail

/**
 * @brief 对象内容版本（CON-01"内容版本"概念）：单个对象内容的摘要版本戳。
 *
 * 内容变则变、ObjectId 不变——二者构成"身份/版本包络"（§4.1.1，互不替代）。
 */
struct ContentVersion {
    Digest256 bytes{};   ///< 摘要原始字节；全零＝空（保留值）

    /// 严格解析 "cv-<64 小写 hex>"；违约抛 CoreError("core/identity/parse: …")。
    static ContentVersion fromCanonical(std::string_view text);
    /// try 轨：失败返回 nullopt 不抛。
    static std::optional<ContentVersion> tryFromCanonical(std::string_view text) noexcept;
    /// 规范文本 "cv-<64 小写 hex>"（持久化时带 tag——§4.2 规范文本列）。
    std::string toCanonical() const;
    /// 非全零。
    bool isValid() const noexcept;
    /// 字节精确相等。
    bool operator==(const ContentVersion& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const ContentVersion& o) const noexcept { return !(*this == o); }
    /// 字节字典序（容器键用）。
    bool operator<(const ContentVersion& o) const noexcept { return bytes < o.bytes; }
};

/**
 * @brief 内容集合身份（CON-05/06）：一组内容的复合摘要（切片/策略/名称映射）。
 *
 * 由被摘要集合决定；进入缓存键与失效判据（§4.2 evidence 侧示例）。
 * 与 ContentVersion 同形但独立类型——语义不同（单对象版本 vs 集合身份），
 * 不提供互转（强类型纪律同 §4.1）。
 */
struct ContentIdentity {
    Digest256 bytes{};   ///< 摘要原始字节；全零＝空（保留值）

    /// 严格解析 "cid-<64 小写 hex>"。
    static ContentIdentity fromCanonical(std::string_view text);
    /// try 轨：失败返回 nullopt 不抛。
    static std::optional<ContentIdentity> tryFromCanonical(std::string_view text) noexcept;
    /// 规范文本 "cid-<64 小写 hex>"。
    std::string toCanonical() const;
    /// 非全零。
    bool isValid() const noexcept;
    bool operator==(const ContentIdentity& o) const noexcept { return bytes == o.bytes; }
    bool operator!=(const ContentIdentity& o) const noexcept { return !(*this == o); }
    bool operator<(const ContentIdentity& o) const noexcept { return bytes < o.bytes; }
};

/**
 * @brief 增量式字节流摘要器（SHA-256，决策 D-05）。
 *
 * 使用契约（§5.2）：
 *   - update() 接受任意字节（含 nBytes=0 的空追加——空追加不改变摘要状态）；
 *   - finalize() 结束并取摘要；**幂等禁止**——finalize 后再 update/finalize
 *     抛 CoreError（前缀 core/digest/finalized:），防"半截摘要"静默传播；
 *   - 纯函数性：同字节序列→同摘要（分块方式无关——1 次 update 与多次
 *     等价拼接产生同摘要，UT-ID-D 钉住）。
 *
 * 生命周期：栈/成员持有，无资源；非线程安全（每线程各持实例）。
 */
class ContentDigester {
public:
    /// 初始状态（H0 取 FIPS 180-2 §5.3.3 前八个平方根前 32 位）。
    ContentDigester() = default;

    /// 追加 nBytes 字节（data 可为 nullptr 当 nBytes==0）；finalize 后调用抛 CoreError。
    void update(const void* data, std::size_t nBytes);

    /// 结束并取摘要（自动完成 FIPS 填充）；之后实例不可再用（再调用抛 CoreError）。
    Digest256 finalize();

private:
    /// 压缩一个 64 字节分组（FIPS 180-2 §6.2.2）——仅内部使用。
    void compressBlock(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};  ///< 链接变量 H0..H7
    std::array<std::uint8_t, 64> buffer_{};                   ///< 待压缩分组缓冲
    std::uint64_t totalBytes_ = 0;                            ///< 已吸收总字节数（填充长度字段用）
    std::size_t buffered_ = 0;                                ///< buffer_ 内有效字节数（0..63）
    bool finalized_ = false;                                  ///< 幂等禁止标志（§5.2 finalize 后不可再用）
};

}  // namespace sdurws::ird::core

// ---- std::hash 特化（§5.2 "+ hash"：FNV-1a 128 over 32 字节，与 Id128 同算法） ----
namespace std {

template <> struct hash<sdurws::ird::core::ContentVersion> {
    std::size_t operator()(const sdurws::ird::core::ContentVersion& v) const noexcept {
        std::uint64_t hi = 0, lo = 0;
        sdurws::ird::core::detail::fnv1a128(v.bytes.data(), v.bytes.size(), &hi, &lo);
        return static_cast<std::size_t>(hi ^ lo);
    }
};

template <> struct hash<sdurws::ird::core::ContentIdentity> {
    std::size_t operator()(const sdurws::ird::core::ContentIdentity& c) const noexcept {
        std::uint64_t hi = 0, lo = 0;
        sdurws::ird::core::detail::fnv1a128(c.bytes.data(), c.bytes.size(), &hi, &lo);
        return static_cast<std::size_t>(hi ^ lo);
    }
};

}  // namespace std

#endif  // SDURWS_IRD_CORE_DIGEST_HPP
