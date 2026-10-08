/**
 * @file   CanonicalDigest.hpp
 * @brief  dynamics 域内私有的 canonical 摘要流写入器（DigestWriter）——
 *         本单元各内容身份（series/envelope/…）共用的显式小端编码工具。
 *
 * 设计依据：
 *   - units/dynamics.md §4.4（contentIdentity＝canonical SHA-256——各
 *     结构的"身份/内容身份"字段义务）、§10.0（确定性 NFR-COR-02：同输入
 *     字节→同输出）、CON-05（内容寻址）
 *   - 编码规则权威＝各所有者实现文件头（SeriesBuilder.cpp 的序列编码
 *     规则 1～6、Envelope.cpp 的包络编码规则）——本头只承载**编码工具**，
 *     不承载"对什么字段按什么序编码"的规则（规则随各结构所有者，review
 *     时对照其文件头逐字段对账）。
 *
 * ★ 为什么抽取为私有头（WP-17-T07）：原 DigestWriter 为 SeriesBuilder.cpp
 *   匿名命名空间内的文件局部实现（WP-17-T04 落位）；T07 包络合并引入第二
 *   个 canonical 摘要消费点（DynamicsEnvelope.contentIdentity），若在
 *   Envelope.cpp 复制第二份写入器会造成同域两套编码工具漂移的风险——
 *   抽取到 src/ 私有实现头（R-2 红线：私有实现头不置于 include/、不跨
 *   单元暴露；仅本单元实现翻译单元可见）后单点维护。
 *
 * 线程安全：DigestWriter 持有调用方摘要器引用（不接管所有权），实例不
 *   跨线程共享——与底层 core::ContentDigester 同约束（每次摘要一实例）。
 * 确定性：全部编码显式小端、无环境依赖——同字段值序列必得同字节流
 *   （NFR-COR-02；NaN 位模式在同一二进制内确定——quiet_NaN 单点产出）。
 */

#ifndef IRD_DYNAMICS_SRC_CANONICAL_DIGEST_HPP
#define IRD_DYNAMICS_SRC_CANONICAL_DIGEST_HPP

#include <sdurws/ird/core/Digest.hpp>   // core::ContentDigester/ContentIdentity
#include <sdurws/ird/core/Identity.hpp> // core::ObjectId（Id128 家族 bytes 直通）

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace sdurws::ird::dynamics::canonical_detail {

/**
 * @brief 摘要流写入器：update 转发＋各标量类型的显式小端编码（本域唯一
 *        编码工具实现点——各内容身份的编码"规则"见其所有者文件头）。
 */
class DigestWriter {
public:
    /// 构造（持有调用方摘要器引用——生命周期覆盖本写入器使用区间）。
    explicit DigestWriter(core::ContentDigester& d) : mDigest(d) {}

    /// 原始字节追加（magic/已编码块用）。
    void raw(const void* data, std::size_t n) { mDigest.update(data, n); }

    /// 固定文本（magic——长度隐含于调用点，不写长度前缀）。
    void magic(const char* text, std::size_t n) { raw(text, n); }

    /// u8（枚举底层值/布尔——bool 以 0/1 编码）。
    void u8(unsigned v) { mDigest.update(&v, 1); }

    /// u32 显式小端（4 字节）。
    void u32(std::uint32_t v)
    {
        const unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu)};
        mDigest.update(b, 4);
    }

    /// u64 显式小端（8 字节——计数类字段用）。
    void u64(std::uint64_t v)
    {
        const unsigned char b[8] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu),
                                    static_cast<unsigned char>((v >> 32) & 0xFFu),
                                    static_cast<unsigned char>((v >> 40) & 0xFFu),
                                    static_cast<unsigned char>((v >> 48) & 0xFFu),
                                    static_cast<unsigned char>((v >> 56) & 0xFFu)};
        mDigest.update(b, 8);
    }

    /// f64＝IEEE-754 位模式的 u64 小端编码（memcpy 取位——无别名违例）。
    void f64(double v)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 必须 64 位（IEEE-754）");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    /// 长度前缀字符串（u32 长度＋UTF-8 字节——无终结符）。
    void str(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }

    /// 16 字节强类型 id（ObjectId——Identity.hpp bytes 直通）。
    void id16(const core::ObjectId& oid) { raw(oid.bytes.data(), oid.bytes.size()); }

    /// 16 字节强类型 id 的字节面通用口（TaskIdentity 五元组等 Id128 家族
    /// ——各 id 为独立强类型、与 ObjectId 无隐式转换，统一走 bytes）。
    void idBytes(const std::array<std::uint8_t, 16>& bytes) { raw(bytes.data(), bytes.size()); }

    /// 32 字节内容身份（ContentIdentity——Digest.hpp bytes 直通）。
    void cid32(const core::ContentIdentity& cid) { raw(cid.bytes.data(), cid.bytes.size()); }

private:
    core::ContentDigester& mDigest; ///< 调用方摘要器（不接管所有权）
};

}  // namespace sdurws::ird::dynamics::canonical_detail

#endif  // IRD_DYNAMICS_SRC_CANONICAL_DIGEST_HPP
