/**
 * @file   ReportCodec.cpp
 * @brief  ReportCodec 实现——确定性二进制编码（§4.4 规则逐条）与 SHA-256
 *         摘要（core ContentDigester 唯一算法）。
 *
 * 实现要点（与 units/reporting.md §4.4 编码规则逐条对应）：
 *   1. magic：Full＝"IRDRPT1"（7 字节）、Data＝"IRDRPTD1"（8 字节）——
 *      原文字面；随后编码器版本号 u32 大端（kCodecVersion）。
 *   2. 字段按名序：每个结构体的字段按字段名升序写入，canonical 顺序在
 *      各 encode 函数头注释逐字列出（"字段按名序"的实施口径——升级字段
 *      时必须重排 canonical 序并升编码器版本）。
 *   3. 章节按 order：sections 以输入存储序写入，输入必须已按 order 严格
 *      递增（§4.7 关系约束由字段校验原语保证；编码入口二次把关——不做
 *      静默重排，违者 DataInvalid）。
 *   4. 长度前缀＋大端：字符串/向量以 u32 长度前缀；全部多字节整数大端。
 *   5. presence 字节：optional 显式编码（0＝缺席/1＝在场；其余值拒绝）。
 *   6. 字符串 UTF-8 禁 NUL：编码与解码两侧拒绝（解码侧拒绝防外来字节）。
 *   7. 浮点 IEEE754 位模式（大端）；NaN/±Inf 编码入口拒绝（evidence
 *      D-06 同源）；解码侧同样拒绝（本编码器永不产出非有限位模式）。
 *   8. 编码器版本号入编码：kCodecVersion 进两种编码头部。
 *   9. 纯函数：无时钟/随机/locale/环境依赖——同输入同字节（NFR-COR-02）。
 *
 * 摘要：SHA-256 经 core::ContentDigester（§4.1"摘要一律 SHA-256 经 core
 * ContentDigester 唯一算法"）——对 canonical 字节整体摘要，结果以
 * core::ContentIdentity 承载（字节等值比较——§4.1）。
 */

#include "ReportCodec.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>

#include <sdurws/ird/core/Digest.hpp>

namespace sdurws::ird::reporting {
namespace {

// =====================================================================
// 编码辅助——Writer（大端字节流写出）
// =====================================================================

/// presence 字节的两个合法值（其余值在解码侧拒绝——严格编码）。
constexpr std::uint8_t kPresenceAbsent = 0;
constexpr std::uint8_t kPresencePresent = 1;

/// 大端字节流写出器（确定性：无缓冲策略差异——顺序 append）。
class Writer {
public:
    /// 无符号整数大端写出（多字节字段统一大端——§4.4 规则 4）。
    void u8(std::uint8_t v) { m_out.push_back(v); }
    void u16(std::uint16_t v)
    {
        m_out.push_back(static_cast<std::uint8_t>(v >> 8));
        m_out.push_back(static_cast<std::uint8_t>(v));
    }
    void u32(std::uint32_t v)
    {
        m_out.push_back(static_cast<std::uint8_t>(v >> 24));
        m_out.push_back(static_cast<std::uint8_t>(v >> 16));
        m_out.push_back(static_cast<std::uint8_t>(v >> 8));
        m_out.push_back(static_cast<std::uint8_t>(v));
    }
    void u64(std::uint64_t v)
    {
        for (int shift = 56; shift >= 0; shift -= 8) {
            m_out.push_back(static_cast<std::uint8_t>(v >> shift));
        }
    }

    /// 有符号 64 位（时间戳纳秒）——按二进制补码位模式大端写出。
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }

    /// double 以 IEEE754 位模式大端写出；NaN/±Inf 编码入口拒绝（§4.4 规则
    /// 7——evidence D-06 同源：身份编码不承载非有限值）。
    void f64(double v)
    {
        if (!std::isfinite(v)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec: 非有限浮点值（NaN/±Inf）拒绝入编码");
        }
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 必须为 64 位 IEEE754");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    /// 布尔以单字节 0/1 写出。
    void boolean(bool v) { u8(v ? 1 : 0); }

    /// optional presence 字节（§4.4 规则 5——显式编码，无隐式缺席）。
    void presence(bool present) { u8(present ? kPresencePresent : kPresenceAbsent); }

    /// 原始字节（Id128/Digest256 等定长数组——原样写入）。
    void raw(const std::uint8_t* data, std::size_t n)
    {
        m_out.insert(m_out.end(), data, data + n);
    }

    /// 字符串：u32 长度前缀＋UTF-8 字节；NUL 拒绝（§4.4 规则 6）。
    void string(const std::string& s)
    {
        if (s.find('\0') != std::string::npos) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec: 字符串含 NUL 字节（UTF-8 禁 NUL）");
        }
        u32(static_cast<std::uint32_t>(s.size()));
        raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    /// 已注册单位 token：以冻结 symbol 字符串承载（解码侧经 find 还原——
    /// 持久化 token 不用注册表下标，下标非稳定契约）。
    void unitToken(core::UnitToken unit)
    {
        if (!unit.isValid()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec: 未注册 UnitToken 拒绝入编码");
        }
        string(std::string(unit.symbol()));
    }

    /// 取出完整字节流。
    std::vector<std::uint8_t> take() { return std::move(m_out); }

private:
    std::vector<std::uint8_t> m_out;   ///< 累积输出字节
};

// =====================================================================
// 编码辅助——Reader（大端字节流读入；全部读取带边界检查）
// =====================================================================

/// 大端字节流读入器（任何越界＝编码截断——DataInvalid fail-fast）。
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

    explicit Reader(const std::vector<std::uint8_t>& bytes)
        : Reader(bytes.data(), bytes.size())
    {
    }

    std::uint8_t u8()
    {
        need(1);
        return m_data[m_pos++];
    }
    std::uint16_t u16()
    {
        need(2);
        const std::uint16_t v = (static_cast<std::uint16_t>(m_data[m_pos]) << 8)
                                | static_cast<std::uint16_t>(m_data[m_pos + 1]);
        m_pos += 2;
        return v;
    }
    std::uint32_t u32()
    {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | static_cast<std::uint32_t>(m_data[m_pos + i]);
        }
        m_pos += 4;
        return v;
    }
    std::uint64_t u64()
    {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<std::uint64_t>(m_data[m_pos + i]);
        }
        m_pos += 8;
        return v;
    }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

    /// double 位模式读入；非有限位模式拒绝（编码器永不产出——外来字节）。
    double f64()
    {
        const std::uint64_t bits = u64();
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        if (!std::isfinite(v)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 非有限浮点位模式（NaN/±Inf）拒绝");
        }
        return v;
    }

    bool boolean()
    {
        const std::uint8_t v = u8();
        if (v > 1) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 布尔字节非法（仅允许 0/1）");
        }
        return v == 1;
    }

    /// presence 字节读入：仅接受 0/1（其余＝编码被篡改/版本错配）。
    bool presence()
    {
        const std::uint8_t v = u8();
        if (v != kPresenceAbsent && v != kPresencePresent) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: presence 字节非法（仅允许 0/1）");
        }
        return v == kPresencePresent;
    }

    /// 定长原始字节读入。
    void raw(std::uint8_t* out, std::size_t n)
    {
        need(n);
        std::memcpy(out, m_data + m_pos, n);
        m_pos += n;
    }

    template <std::size_t N> std::array<std::uint8_t, N> rawArray()
    {
        std::array<std::uint8_t, N> a{};
        raw(a.data(), N);
        return a;
    }

    /// 字符串读入（长度前缀＋字节；NUL 拒绝——解码侧同样执行）。
    std::string string()
    {
        const std::uint32_t len = u32();
        need(len);
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), len);
        m_pos += len;
        if (s.find('\0') != std::string::npos) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 字符串含 NUL 字节（UTF-8 禁 NUL）");
        }
        return s;
    }

    /// 单位 token 还原：经 core 冻结注册表 find（未知符号拒绝——下限闸门）。
    core::UnitToken unitToken()
    {
        const std::string symbol = string();
        const auto unit = core::UnitToken::find(symbol);
        if (!unit.has_value()) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 未知单位 token '" + symbol + "'");
        }
        return *unit;
    }

    /// 尾随字节拒绝（解码必须恰好消耗全部输入——防截断/追加歧义）。
    void expectEnd()
    {
        if (m_pos != m_size) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 存在尾随字节（长度 " + std::to_string(m_size)
                                  + "，已消耗 " + std::to_string(m_pos) + "）");
        }
    }

private:
    /// 边界检查：剩余字节不足即抛（DataInvalid——编码截断）。
    void need(std::size_t n) const
    {
        if (m_size - m_pos < n) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: 字节流截断（需要 " + std::to_string(n)
                                  + " 字节，剩余 " + std::to_string(m_size - m_pos) + "）");
        }
    }

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_pos = 0;
};

// =====================================================================
// 头部（magic＋版本）与摘要共用件
// =====================================================================

/// 写头部：magic 原文字节＋编码器版本 u32 大端（§4.4 规则 1/8）。
void writeHeader(Writer& w, std::string_view magic)
{
    w.raw(reinterpret_cast<const std::uint8_t*>(magic.data()), magic.size());
    w.u32(ReportCodec::kCodecVersion);
}

/// 读头部：magic 逐字节核对＋版本核对（不符＝外来字节/版本错配——拒绝）。
void readHeader(Reader& r, std::string_view magic)
{
    for (const char c : magic) {
        if (r.u8() != static_cast<std::uint8_t>(c)) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: magic 不符（期望 " + std::string(magic) + "）");
        }
    }
    const std::uint32_t version = r.u32();
    if (version != ReportCodec::kCodecVersion) {
        throw ReportError(ReportErrorCode::DataInvalid,
                          "ReportCodec 解码: 编码器版本不符（期望 "
                              + std::to_string(ReportCodec::kCodecVersion) + "，实际 "
                              + std::to_string(version) + "）");
    }
}

/// 对 canonical 字节整体做 SHA-256（core ContentDigester 唯一算法——§4.1）
/// 并以 ContentIdentity 承载。
core::ContentIdentity digestBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

/// 时间点编码：UTC 纳秒计数（system_clock::duration 归一到纳秒——定点
/// 整数，跨平台位稳定；§4.4 浮点位模式规则不适用于时间——时间是整数面）。
void writeTime(Writer& w, const std::chrono::system_clock::time_point& tp)
{
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch());
    w.i64(ns.count());
}

std::chrono::system_clock::time_point readTime(Reader& r)
{
    const std::int64_t ns = r.i64();
    // system_clock::time_point 只接受其自身 duration 类型——纳秒计数先
    // 归一到 system_clock::duration（MSVC 下同为纳秒精度，无损）。
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(ns)));
}

// =====================================================================
// 枚举编解码（uint8 声明序——见 ReportCodec.hpp 枚举编码口径）
// =====================================================================

/// 枚举写出（uint8 声明序；T 为 uint8 底层枚举）。
template <typename E> void writeEnum(Writer& w, E value)
{
    static_assert(std::is_enum_v<E>, "E 必须为枚举");
    w.u8(static_cast<std::uint8_t>(value));
}

/// 枚举读入并做值域校验（maxExclusive＝合法值上界开区间——越界即外来
/// 字节/版本错配，拒绝而非截断到合法值）。
template <typename E>
E readEnum(Reader& r, std::uint8_t maxExclusive, const char* name)
{
    const std::uint8_t v = r.u8();
    if (v >= maxExclusive) {
        throw ReportError(ReportErrorCode::DataInvalid,
                          std::string("ReportCodec 解码: 枚举值越界（") + name + "＝"
                              + std::to_string(v) + "）");
    }
    return static_cast<E>(v);
}

// 各枚举的合法值个数（与各单元卡词表一致；上游词表扩展随编码器升版同步）。
constexpr std::uint8_t kReportLevelCount = 2;             // B/C（§4.2）
constexpr std::uint8_t kSectionStatusCount = 4;           // §5.3 四态
constexpr std::uint8_t kRenderHintCount = 4;              // §9.2 四值
constexpr std::uint8_t kFieldStateCount = 4;              // core §4.3 四态
constexpr std::uint8_t kProvenanceKindCount = 5;          // core §4.3 五类
constexpr std::uint8_t kEvaluationModeCount = 3;          // core §4.7（Preview/Quick/Verified）
constexpr std::uint8_t kProducerProcessCount = 2;         // evidence §7.1（Main/Worker）
constexpr std::uint8_t kInvalidationKindCount = 10;       // evidence §8.1 步骤 4 词表
constexpr std::uint8_t kBaselineDimensionCount = 5;       // evidence §6.5 差异维度
constexpr std::uint8_t kSignOffStateCount = 2;            // Unsigned/Signed
constexpr std::uint8_t kExternalResourceStateCount = 2;   // Recorded/Solidified
constexpr std::uint8_t kQuantityKindCount = 14;           // core §4.4 冻结 14 类
constexpr std::uint8_t kDiagCategoryCount = 15;           // diagnostics §4.3 分类词表 15 值
constexpr std::uint8_t kDiagSeverityCount = 4;            // diagnostics §4.3 严重级别 4 值

/// 可选 core::ContentIdentity 编解码（presence＋32 字节）。
void writeOptionalIdentity(Writer& w, const std::optional<core::ContentIdentity>& id)
{
    w.presence(id.has_value());
    if (id.has_value()) {
        w.raw(id->bytes.data(), id->bytes.size());
    }
}

std::optional<core::ContentIdentity> readOptionalIdentity(Reader& r)
{
    if (!r.presence()) {
        return std::nullopt;
    }
    core::ContentIdentity id;
    id.bytes = r.rawArray<32>();
    return id;
}

/// core::TaskIdentity 编解码（canonical 名序：attempt, branch, project,
/// revision, run——五元组 TASK-03）。
void writeTaskIdentity(Writer& w, const core::TaskIdentity& t)
{
    w.u64(t.attempt.value);
    w.raw(t.branch.bytes.data(), t.branch.bytes.size());
    w.raw(t.project.bytes.data(), t.project.bytes.size());
    w.raw(t.revision.bytes.data(), t.revision.bytes.size());
    w.raw(t.run.bytes.data(), t.run.bytes.size());
}

core::TaskIdentity readTaskIdentity(Reader& r)
{
    core::TaskIdentity t;
    t.attempt.value = r.u64();
    t.branch.bytes = r.rawArray<16>();
    t.project.bytes = r.rawArray<16>();
    t.revision.bytes = r.rawArray<16>();
    t.run.bytes = r.rawArray<16>();
    return t;
}

// =====================================================================
// 叶子值类型编解码
// =====================================================================

/// core::ValueProvenance 编码（canonical 名序：kind, methodTag,
/// sourceObject, sourceVersion——core §4.3 四字段）。
void writeProvenance(Writer& w, const core::ValueProvenance& p)
{
    writeEnum(w, p.kind);   // 声明序五值——kProvenanceKindCount 校验于读侧
    w.presence(p.methodTag.has_value());
    if (p.methodTag.has_value()) {
        w.string(*p.methodTag);
    }
    w.presence(p.sourceObject.has_value());
    if (p.sourceObject.has_value()) {
        w.raw(p.sourceObject->bytes.data(), p.sourceObject->bytes.size());
    }
    w.presence(p.sourceVersion.has_value());
    if (p.sourceVersion.has_value()) {
        w.raw(p.sourceVersion->bytes.data(), p.sourceVersion->bytes.size());
    }
}

core::ValueProvenance readProvenance(Reader& r)
{
    core::ValueProvenance p;
    p.kind = readEnum<core::ProvenanceKind>(r, kProvenanceKindCount, "ProvenanceKind");
    if (r.presence()) {
        p.methodTag = r.string();
    }
    if (r.presence()) {
        core::ObjectId obj;
        obj.bytes = r.rawArray<16>();
        p.sourceObject = obj;
    }
    if (r.presence()) {
        core::ContentVersion cv;
        cv.bytes = r.rawArray<32>();
        p.sourceVersion = cv;
    }
    return p;
}

/// core::SourcedValue<double> 编码（四态显式——§4.4"章节完整内容"包含
/// 字段四态事实；Provided 态写 f64＋provenance、Invalid 态写原串）。
void writeSourcedDouble(Writer& w, const core::SourcedValue<double>& v)
{
    writeEnum(w, v.state());
    switch (v.state()) {
    case core::FieldState::Provided:
        w.f64(v.tryValue().value_or(0.0));   // Provided 必有值——value_or 仅为静型
        writeProvenance(w, v.provenance());
        break;
    case core::FieldState::Invalid:
        w.string(v.invalidRawInput());
        break;
    case core::FieldState::NotProvided:
    case core::FieldState::NotApplicable:
        break;   // 无载荷（四态语义——core §4.3）
    }
}

core::SourcedValue<double> readSourcedDouble(Reader& r)
{
    const auto state = readEnum<core::FieldState>(r, kFieldStateCount, "FieldState");
    switch (state) {
    case core::FieldState::Provided: {
        const double value = r.f64();
        const core::ValueProvenance provenance = readProvenance(r);
        return core::SourcedValue<double>::provided(value, provenance);
    }
    case core::FieldState::Invalid:
        return core::SourcedValue<double>::invalid(r.string());
    case core::FieldState::NotProvided:
        return core::SourcedValue<double>::notProvided();
    case core::FieldState::NotApplicable:
    default:
        return core::SourcedValue<double>::notApplicable();
    }
}

/// core::ComparativeValue 编码（canonical 名序：quantity, unit——UX-03）。
void writeComparativeValue(Writer& w, const core::ComparativeValue& v)
{
    writeSourcedDouble(w, v.quantity);
    w.unitToken(v.unit);
}

core::ComparativeValue readComparativeValue(Reader& r)
{
    core::ComparativeValue v;
    v.quantity = readSourcedDouble(r);
    v.unit = r.unitToken();
    return v;
}

/// core::ComparativeFields 编码（canonical 名序：actual, expected）。
void writeComparativeFields(Writer& w, const core::ComparativeFields& f)
{
    writeComparativeValue(w, f.actual);
    writeComparativeValue(w, f.expected);
}

core::ComparativeFields readComparativeFields(Reader& r)
{
    core::ComparativeFields f;
    f.actual = readComparativeValue(r);
    f.expected = readComparativeValue(r);
    return f;
}

/// evidence::InvalidationReason 编码（canonical 名序：dependencyKey,
/// detail, kind——evidence §8.1 步骤 4 呈现投影）。
void writeInvalidationReason(Writer& w, const evidence::InvalidationReason& reason)
{
    w.string(reason.dependencyKey);
    w.string(reason.detail);
    writeEnum(w, reason.kind);
}

evidence::InvalidationReason readInvalidationReason(Reader& r)
{
    evidence::InvalidationReason reason;
    reason.dependencyKey = r.string();
    reason.detail = r.string();
    reason.kind = readEnum<evidence::InvalidationKind>(r, kInvalidationKindCount,
                                                       "InvalidationKind");
    return reason;
}

/// evidence::BaselineConsistencyResult 编码（canonical 名序：consistent,
/// differingDimensions——evidence §6.5）。
void writeBaselineResult(Writer& w, const evidence::BaselineConsistencyResult& b)
{
    w.boolean(b.consistent);
    w.u32(static_cast<std::uint32_t>(b.differingDimensions.size()));
    for (const auto dim : b.differingDimensions) {
        writeEnum(w, dim);
    }
}

evidence::BaselineConsistencyResult readBaselineResult(Reader& r)
{
    evidence::BaselineConsistencyResult b;
    b.consistent = r.boolean();
    const std::uint32_t n = r.u32();
    b.differingDimensions.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        b.differingDimensions.push_back(
            readEnum<evidence::BaselineDifferenceDimension>(r, kBaselineDimensionCount,
                                                            "BaselineDifferenceDimension"));
    }
    return b;
}

// =====================================================================
// 报告引用类型编解码（canonical 名序逐结构注释——"字段按名序"）
// =====================================================================

/// FieldValue 编码（canonical 名序：key, quantity, text, unit——§9.2）。
void writeFieldValue(Writer& w, const FieldValue& f)
{
    w.string(f.key);
    writeSourcedDouble(w, f.quantity);
    w.presence(f.text.has_value());
    if (f.text.has_value()) {
        w.string(*f.text);
    }
    w.presence(f.unit.has_value());
    if (f.unit.has_value()) {
        w.unitToken(*f.unit);
    }
}

FieldValue readFieldValue(Reader& r)
{
    FieldValue f;
    f.key = r.string();
    f.quantity = readSourcedDouble(r);
    if (r.presence()) {
        f.text = r.string();
    }
    if (r.presence()) {
        f.unit = r.unitToken();
    }
    return f;
}

/// ResultBinding 编码（canonical 名序：fieldPath, runId）。
void writeResultBinding(Writer& w, const ResultBinding& b)
{
    w.string(b.fieldPath);
    w.raw(b.runId.bytes.data(), b.runId.bytes.size());
}

ResultBinding readResultBinding(Reader& r)
{
    ResultBinding b;
    b.fieldPath = r.string();
    b.runId.bytes = r.rawArray<16>();
    return b;
}

/// EvidenceBinding 编码（canonical 名序：caseScope, digest, itemId, status）。
void writeEvidenceBinding(Writer& w, const EvidenceBinding& b)
{
    w.u32(static_cast<std::uint32_t>(b.caseScope.size()));
    for (const auto& id : b.caseScope) {
        w.raw(id.bytes.data(), id.bytes.size());
    }
    w.presence(b.digest.has_value());
    if (b.digest.has_value()) {
        w.raw(b.digest->data(), b.digest->size());
    }
    w.string(b.itemId);
    writeEnum(w, b.status);   // EvidenceItemStatus 五值——P-EV-8 词表声明序
}

EvidenceBinding readEvidenceBinding(Reader& r)
{
    EvidenceBinding b;
    const std::uint32_t n = r.u32();
    b.caseScope.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        b.caseScope.push_back(id);
    }
    if (r.presence()) {
        b.digest = r.rawArray<32>();
    }
    b.itemId = r.string();
    // EvidenceItemStatus 五值（O-13：五值各有其位——读侧值域校验按五值）。
    b.status = readEnum<evidence::EvidenceItemStatus>(r, 5, "EvidenceItemStatus");
    return b;
}

/// JumpTarget 编码（canonical 名序：caseId, objectId, runId）。
void writeJumpTarget(Writer& w, const JumpTarget& j)
{
    w.presence(j.caseId.has_value());
    if (j.caseId.has_value()) {
        w.raw(j.caseId->bytes.data(), j.caseId->bytes.size());
    }
    w.presence(j.objectId.has_value());
    if (j.objectId.has_value()) {
        w.raw(j.objectId->bytes.data(), j.objectId->bytes.size());
    }
    w.presence(j.runId.has_value());
    if (j.runId.has_value()) {
        w.raw(j.runId->bytes.data(), j.runId->bytes.size());
    }
}

JumpTarget readJumpTarget(Reader& r)
{
    JumpTarget j;
    if (r.presence()) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        j.caseId = id;
    }
    if (r.presence()) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        j.objectId = id;
    }
    if (r.presence()) {
        core::RunId id;
        id.bytes = r.rawArray<16>();
        j.runId = id;
    }
    return j;
}

/// SectionEntryView 编码（canonical 名序：caseScope, entryKey, evidence,
/// fields, jump, result——§4.3.4 entries 冻结视图）。
void writeSectionEntry(Writer& w, const SectionEntryView& e)
{
    w.u32(static_cast<std::uint32_t>(e.caseScope.size()));
    for (const auto& id : e.caseScope) {
        w.raw(id.bytes.data(), id.bytes.size());
    }
    w.string(e.entryKey);
    w.u32(static_cast<std::uint32_t>(e.evidence.size()));
    for (const auto& b : e.evidence) {
        writeEvidenceBinding(w, b);
    }
    w.u32(static_cast<std::uint32_t>(e.fields.size()));
    for (const auto& f : e.fields) {
        writeFieldValue(w, f);
    }
    writeJumpTarget(w, e.jump);
    writeResultBinding(w, e.result);
}

SectionEntryView readSectionEntry(Reader& r)
{
    SectionEntryView e;
    const std::uint32_t nScope = r.u32();
    e.caseScope.reserve(nScope);
    for (std::uint32_t i = 0; i < nScope; ++i) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        e.caseScope.push_back(id);
    }
    e.entryKey = r.string();
    const std::uint32_t nEv = r.u32();
    e.evidence.reserve(nEv);
    for (std::uint32_t i = 0; i < nEv; ++i) {
        e.evidence.push_back(readEvidenceBinding(r));
    }
    const std::uint32_t nField = r.u32();
    e.fields.reserve(nField);
    for (std::uint32_t i = 0; i < nField; ++i) {
        e.fields.push_back(readFieldValue(r));
    }
    e.jump = readJumpTarget(r);
    e.result = readResultBinding(r);
    return e;
}

/// MissingItemView 编码（canonical 名序：itemId, reason）。
void writeMissingItem(Writer& w, const MissingItemView& m)
{
    w.string(m.itemId);
    w.string(m.reason);
}

MissingItemView readMissingItem(Reader& r)
{
    MissingItemView m;
    m.itemId = r.string();
    m.reason = r.string();
    return m;
}

/// EligibilityNote 编码（canonical 名序：formalPassAllowed, note,
/// reviewRecordAllowed）。
void writeEligibilityNote(Writer& w, const EligibilityNote& n)
{
    w.boolean(n.formalPassAllowed);
    w.string(n.note);
    w.boolean(n.reviewRecordAllowed);
}

EligibilityNote readEligibilityNote(Reader& r)
{
    EligibilityNote n;
    n.formalPassAllowed = r.boolean();
    n.note = r.string();
    n.reviewRecordAllowed = r.boolean();
    return n;
}

/// DiagRefEntry 编码（canonical 名序：category, code, comparison,
/// localName, occurrences, runtimeName, severity, sourceRun, sourceSection,
/// subject——§4.3.3）。
void writeDiagRefEntry(Writer& w, const DiagRefEntry& d)
{
    writeEnum(w, d.category);
    w.string(d.code);
    w.presence(d.comparison.has_value());
    if (d.comparison.has_value()) {
        writeComparativeFields(w, *d.comparison);
    }
    w.presence(d.localName.has_value());
    if (d.localName.has_value()) {
        w.string(*d.localName);
    }
    w.u64(d.occurrences);
    w.presence(d.runtimeName.has_value());
    if (d.runtimeName.has_value()) {
        w.string(*d.runtimeName);
    }
    writeEnum(w, d.severity);
    w.presence(d.sourceRun.has_value());
    if (d.sourceRun.has_value()) {
        w.raw(d.sourceRun->bytes.data(), d.sourceRun->bytes.size());
    }
    w.string(d.sourceSection);
    w.presence(d.subject.has_value());
    if (d.subject.has_value()) {
        w.raw(d.subject->bytes.data(), d.subject->bytes.size());
    }
}

DiagRefEntry readDiagRefEntry(Reader& r)
{
    DiagRefEntry d;
    d.category = readEnum<diagnostics::DiagnosticCategory>(r, kDiagCategoryCount,
                                                           "DiagnosticCategory");
    d.code = r.string();
    if (r.presence()) {
        d.comparison = readComparativeFields(r);
    }
    if (r.presence()) {
        d.localName = r.string();
    }
    d.occurrences = r.u64();
    if (r.presence()) {
        d.runtimeName = r.string();
    }
    d.severity = readEnum<diagnostics::DiagnosticSeverity>(r, kDiagSeverityCount,
                                                           "DiagnosticSeverity");
    if (r.presence()) {
        core::RunId id;
        id.bytes = r.rawArray<16>();
        d.sourceRun = id;
    }
    d.sourceSection = r.string();
    if (r.presence()) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        d.subject = id;
    }
    return d;
}

/// CurrentnessSnapshot 编码（canonical 名序：computedAtUtc, evaluatedAgainst,
/// reasons, status, unevaluableNote——§6.3；evidence::CurrentnessStatus 两
/// 持久态＋nullopt 计算形态按 presence 承载）。
void writeCurrentnessSnapshot(Writer& w, const CurrentnessSnapshot& c)
{
    writeTime(w, c.computedAtUtc);
    // evaluatedAgainst（canonical 名序：contextSummary, headRevision）。
    w.string(c.evaluatedAgainst.contextSummary);
    w.raw(c.evaluatedAgainst.headRevision.bytes.data(),
          c.evaluatedAgainst.headRevision.bytes.size());
    w.u32(static_cast<std::uint32_t>(c.reasons.size()));
    for (const auto& reason : c.reasons) {
        writeInvalidationReason(w, reason);
    }
    w.presence(c.status.has_value());
    if (c.status.has_value()) {
        // CurrentnessStatus 两持久态（P-EV-4——nullopt 不是第三态而是
        // presence 缺席，"不可判定"以 unevaluableNote 表达）。
        writeEnum(w, *c.status);
    }
    w.presence(c.unevaluableNote.has_value());
    if (c.unevaluableNote.has_value()) {
        writeDiagRefEntry(w, *c.unevaluableNote);
    }
}

CurrentnessSnapshot readCurrentnessSnapshot(Reader& r)
{
    CurrentnessSnapshot c;
    c.computedAtUtc = readTime(r);
    c.evaluatedAgainst.contextSummary = r.string();
    c.evaluatedAgainst.headRevision.bytes = r.rawArray<16>();
    const std::uint32_t n = r.u32();
    c.reasons.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        c.reasons.push_back(readInvalidationReason(r));
    }
    if (r.presence()) {
        c.status = readEnum<evidence::CurrentnessStatus>(r, 2, "CurrentnessStatus");
    }
    if (r.presence()) {
        c.unevaluableNote = readDiagRefEntry(r);
    }
    return c;
}

/// ReviewReportSection 编码（canonical 名序：currentness, diagnostics,
/// eligibilityNote, entries, missingItems, order, renderHint, sectionId,
/// sectionVersion, selected, sourceObjects, sourceResults, status——§4.3.4）。
void writeSection(Writer& w, const ReviewReportSection& s)
{
    w.presence(s.currentness.has_value());
    if (s.currentness.has_value()) {
        writeCurrentnessSnapshot(w, *s.currentness);
    }
    w.u32(static_cast<std::uint32_t>(s.diagnostics.size()));
    for (const auto& d : s.diagnostics) {
        writeDiagRefEntry(w, d);
    }
    w.presence(s.eligibilityNote.has_value());
    if (s.eligibilityNote.has_value()) {
        writeEligibilityNote(w, *s.eligibilityNote);
    }
    w.u32(static_cast<std::uint32_t>(s.entries.size()));
    for (const auto& e : s.entries) {
        writeSectionEntry(w, e);
    }
    w.u32(static_cast<std::uint32_t>(s.missingItems.size()));
    for (const auto& m : s.missingItems) {
        writeMissingItem(w, m);
    }
    w.u16(s.order);
    writeEnum(w, s.renderHint);
    w.string(s.sectionId);
    w.u32(s.sectionVersion);
    w.boolean(s.selected);
    w.u32(static_cast<std::uint32_t>(s.sourceObjects.size()));
    for (const auto& id : s.sourceObjects) {
        w.raw(id.bytes.data(), id.bytes.size());
    }
    w.u32(static_cast<std::uint32_t>(s.sourceResults.size()));
    for (const auto& id : s.sourceResults) {
        w.raw(id.bytes.data(), id.bytes.size());
    }
    writeEnum(w, s.status);
}

ReviewReportSection readSection(Reader& r)
{
    ReviewReportSection s;
    if (r.presence()) {
        s.currentness = readCurrentnessSnapshot(r);
    }
    const std::uint32_t nDiag = r.u32();
    s.diagnostics.reserve(nDiag);
    for (std::uint32_t i = 0; i < nDiag; ++i) {
        s.diagnostics.push_back(readDiagRefEntry(r));
    }
    if (r.presence()) {
        s.eligibilityNote = readEligibilityNote(r);
    }
    const std::uint32_t nEntry = r.u32();
    s.entries.reserve(nEntry);
    for (std::uint32_t i = 0; i < nEntry; ++i) {
        s.entries.push_back(readSectionEntry(r));
    }
    const std::uint32_t nMissing = r.u32();
    s.missingItems.reserve(nMissing);
    for (std::uint32_t i = 0; i < nMissing; ++i) {
        s.missingItems.push_back(readMissingItem(r));
    }
    s.order = r.u16();
    s.renderHint = readEnum<RenderHint>(r, kRenderHintCount, "RenderHint");
    s.sectionId = r.string();
    s.sectionVersion = r.u32();
    s.selected = r.boolean();
    const std::uint32_t nObj = r.u32();
    s.sourceObjects.reserve(nObj);
    for (std::uint32_t i = 0; i < nObj; ++i) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        s.sourceObjects.push_back(id);
    }
    const std::uint32_t nRun = r.u32();
    s.sourceResults.reserve(nRun);
    for (std::uint32_t i = 0; i < nRun; ++i) {
        core::RunId id;
        id.bytes = r.rawArray<16>();
        s.sourceResults.push_back(id);
    }
    s.status = readEnum<SectionStatus>(r, kSectionStatusCount, "SectionStatus");
    return s;
}

// =====================================================================
// ReviewMetadata 编解码（§4.5——入 contentIdentity 的评审元数据全体）
// =====================================================================

/// ReviewComment 编码（canonical 名序：atUtc, author, sectionId, text）。
void writeReviewComment(Writer& w, const ReviewComment& c)
{
    writeTime(w, c.atUtc);
    w.string(c.author);
    w.presence(c.sectionId.has_value());
    if (c.sectionId.has_value()) {
        w.string(*c.sectionId);
    }
    w.string(c.text);
}

ReviewComment readReviewComment(Reader& r)
{
    ReviewComment c;
    c.atUtc = readTime(r);
    c.author = r.string();
    if (r.presence()) {
        c.sectionId = r.string();
    }
    c.text = r.string();
    return c;
}

/// SignOffState 编码（canonical 名序：signedAtUtc, signer, statementDigest,
/// state——§4.5；presence 语义由字段校验原语保证，编码只承载值）。
void writeSignOff(Writer& w, const SignOffState& s)
{
    writeTime(w, s.signedAtUtc);
    w.string(s.signer);
    w.raw(s.statementDigest.data(), s.statementDigest.size());
    writeEnum(w, s.state);
}

SignOffState readSignOff(Reader& r)
{
    SignOffState s;
    s.signedAtUtc = readTime(r);
    s.signer = r.string();
    s.statementDigest = r.rawArray<32>();
    s.state = readEnum<SignOffState::State>(r, kSignOffStateCount, "SignOffState");
    return s;
}

/// TradeOff 编码（canonical 名序：rationale, sectionId, topic）。
void writeTradeOff(Writer& w, const TradeOff& t)
{
    w.string(t.rationale);
    w.presence(t.sectionId.has_value());
    if (t.sectionId.has_value()) {
        w.string(*t.sectionId);
    }
    w.string(t.topic);
}

TradeOff readTradeOff(Reader& r)
{
    TradeOff t;
    t.rationale = r.string();
    if (r.presence()) {
        t.sectionId = r.string();
    }
    t.topic = r.string();
    return t;
}

/// VariantDiffBlock 编码（canonical 名序：baselineCheckResult,
/// baselineRevision, candidateRevision, modelDiffRef, tradeOffs——§4.5）。
void writeVariantDiff(Writer& w, const VariantDiffBlock& v)
{
    writeBaselineResult(w, v.baselineCheckResult);
    w.raw(v.baselineRevision.bytes.data(), v.baselineRevision.bytes.size());
    w.raw(v.candidateRevision.bytes.data(), v.candidateRevision.bytes.size());
    w.raw(v.modelDiffRef.bytes.data(), v.modelDiffRef.bytes.size());
    w.u32(static_cast<std::uint32_t>(v.tradeOffs.size()));
    for (const auto& t : v.tradeOffs) {
        writeTradeOff(w, t);
    }
}

VariantDiffBlock readVariantDiff(Reader& r)
{
    VariantDiffBlock v;
    v.baselineCheckResult = readBaselineResult(r);
    v.baselineRevision.bytes = r.rawArray<16>();
    v.candidateRevision.bytes = r.rawArray<16>();
    v.modelDiffRef.bytes = r.rawArray<16>();
    const std::uint32_t n = r.u32();
    v.tradeOffs.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        v.tradeOffs.push_back(readTradeOff(r));
    }
    return v;
}

/// ReportVersionEntry 编码（canonical 名序：actor, atUtc, reason, reportId,
/// reportVersion——§4.5 changeLog 条目）。
void writeVersionEntry(Writer& w, const ReportVersionEntry& e)
{
    w.string(e.actor);
    writeTime(w, e.atUtc);
    w.string(e.reason);
    w.raw(e.reportId.bytes.data(), e.reportId.bytes.size());
    w.u32(e.reportVersion);
}

ReportVersionEntry readVersionEntry(Reader& r)
{
    ReportVersionEntry e;
    e.actor = r.string();
    e.atUtc = readTime(r);
    e.reason = r.string();
    e.reportId.bytes = r.rawArray<16>();
    e.reportVersion = r.u32();
    return e;
}

/// ReviewMetadata 编码（canonical 名序：basisRevision, basisSnapshot,
/// changeLog, comments, reviewedAtUtc, reviewer, signOff, variantDiff——
/// §4.5 字段表）。
void writeReviewMetadata(Writer& w, const ReviewMetadata& m)
{
    w.raw(m.basisRevision.bytes.data(), m.basisRevision.bytes.size());
    writeOptionalIdentity(w, m.basisSnapshot);
    w.u32(static_cast<std::uint32_t>(m.changeLog.size()));
    for (const auto& e : m.changeLog) {
        writeVersionEntry(w, e);
    }
    w.u32(static_cast<std::uint32_t>(m.comments.size()));
    for (const auto& c : m.comments) {
        writeReviewComment(w, c);
    }
    w.presence(m.reviewedAtUtc.has_value());
    if (m.reviewedAtUtc.has_value()) {
        writeTime(w, *m.reviewedAtUtc);
    }
    w.presence(m.reviewer.has_value());
    if (m.reviewer.has_value()) {
        w.string(*m.reviewer);
    }
    writeSignOff(w, m.signOff);
    w.presence(m.variantDiff.has_value());
    if (m.variantDiff.has_value()) {
        writeVariantDiff(w, *m.variantDiff);
    }
}

ReviewMetadata readReviewMetadata(Reader& r)
{
    ReviewMetadata m;
    m.basisRevision.bytes = r.rawArray<16>();
    m.basisSnapshot = readOptionalIdentity(r);
    const std::uint32_t nLog = r.u32();
    m.changeLog.reserve(nLog);
    for (std::uint32_t i = 0; i < nLog; ++i) {
        m.changeLog.push_back(readVersionEntry(r));
    }
    const std::uint32_t nComment = r.u32();
    m.comments.reserve(nComment);
    for (std::uint32_t i = 0; i < nComment; ++i) {
        m.comments.push_back(readReviewComment(r));
    }
    if (r.presence()) {
        m.reviewedAtUtc = readTime(r);
    }
    if (r.presence()) {
        m.reviewer = r.string();
    }
    m.signOff = readSignOff(r);
    if (r.presence()) {
        m.variantDiff = readVariantDiff(r);
    }
    return m;
}

// =====================================================================
// ReportSourceSpec 编解码（§4.4 ReportCodec-Data 主体——canonical 名序：
// branch, inputSliceId, level, project, resultRefs, revision, revisionSeq,
// selectedSections, snapshotId, unitPreference）
// =====================================================================

/// ResultDataSourceRef 编码（canonical 名序：caseScope, inputBaselineId,
/// runId, sliceId, snapshotId——§4.4 结果引用子集）。
void writeResultDataSourceRef(Writer& w, const ResultDataSourceRef& ref)
{
    w.u32(static_cast<std::uint32_t>(ref.caseScope.size()));
    for (const auto& id : ref.caseScope) {
        w.raw(id.bytes.data(), id.bytes.size());
    }
    w.raw(ref.inputBaselineId.bytes.data(), ref.inputBaselineId.bytes.size());
    w.raw(ref.runId.bytes.data(), ref.runId.bytes.size());
    w.raw(ref.sliceId.bytes.data(), ref.sliceId.bytes.size());
    w.raw(ref.snapshotId.bytes.data(), ref.snapshotId.bytes.size());
}

ResultDataSourceRef readResultDataSourceRef(Reader& r)
{
    ResultDataSourceRef ref;
    const std::uint32_t n = r.u32();
    ref.caseScope.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        core::ObjectId id;
        id.bytes = r.rawArray<16>();
        ref.caseScope.push_back(id);
    }
    ref.inputBaselineId.bytes = r.rawArray<32>();
    ref.runId.bytes = r.rawArray<16>();
    ref.sliceId.bytes = r.rawArray<32>();
    ref.snapshotId.bytes = r.rawArray<32>();
    return ref;
}

/// UnitPreference 编码（map 迭代序＝QuantityKind 枚举序——确定性；
/// 键以 u8 枚举、值以冻结 symbol 字符串承载）。
void writeUnitPreference(Writer& w, const UnitPreference& u)
{
    w.u32(static_cast<std::uint32_t>(u.displayUnits.size()));
    for (const auto& [kind, unit] : u.displayUnits) {
        writeEnum(w, kind);
        w.unitToken(unit);
    }
}

UnitPreference readUnitPreference(Reader& r)
{
    UnitPreference u;
    const std::uint32_t n = r.u32();
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto kind =
            readEnum<core::QuantityKind>(r, kQuantityKindCount, "QuantityKind");
        const core::UnitToken unit = r.unitToken();
        // map emplace：重复键＝编码违约（canonical 序内键唯一）。
        if (!u.displayUnits.emplace(kind, unit).second) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec 解码: UnitPreference 量纲键重复");
        }
    }
    return u;
}

}  // namespace

// =====================================================================
// ReportCodec 公共面——Data 编码
// =====================================================================

std::vector<std::uint8_t> ReportCodec::encodeData(const ReportSourceSpec& spec)
{
    // canonical 名序：branch, inputSliceId, level, project, resultRefs,
    // revision, revisionSeq, selectedSections, snapshotId, unitPreference
    // （§4.4"字段按名序"——字段名升序，复核可对照本注释逐项核对）。
    Writer w;
    writeHeader(w, kMagicData);
    w.raw(spec.branch.bytes.data(), spec.branch.bytes.size());
    writeOptionalIdentity(w, spec.inputSliceId);
    writeEnum(w, spec.level);
    w.raw(spec.project.bytes.data(), spec.project.bytes.size());
    w.u32(static_cast<std::uint32_t>(spec.resultRefs.size()));
    for (const auto& ref : spec.resultRefs) {
        writeResultDataSourceRef(w, ref);
    }
    w.raw(spec.revision.bytes.data(), spec.revision.bytes.size());
    w.u64(spec.revisionSeq);
    w.u32(static_cast<std::uint32_t>(spec.selectedSections.size()));
    for (const auto& sel : spec.selectedSections) {
        // SelectedSectionEntry canonical 名序：sectionId, selected。
        w.string(sel.sectionId);
        w.boolean(sel.selected);
    }
    writeOptionalIdentity(w, spec.snapshotId);
    writeUnitPreference(w, spec.unitPreference);
    return w.take();
}

ReportSourceSpec ReportCodec::parseData(const std::vector<std::uint8_t>& bytes)
{
    Reader r(bytes);
    readHeader(r, kMagicData);
    ReportSourceSpec spec;
    spec.branch.bytes = r.rawArray<16>();
    spec.inputSliceId = readOptionalIdentity(r);
    spec.level = readEnum<ReportLevel>(r, kReportLevelCount, "ReportLevel");
    spec.project.bytes = r.rawArray<16>();
    const std::uint32_t nRefs = r.u32();
    spec.resultRefs.reserve(nRefs);
    for (std::uint32_t i = 0; i < nRefs; ++i) {
        spec.resultRefs.push_back(readResultDataSourceRef(r));
    }
    spec.revision.bytes = r.rawArray<16>();
    spec.revisionSeq = r.u64();
    const std::uint32_t nSel = r.u32();
    spec.selectedSections.reserve(nSel);
    for (std::uint32_t i = 0; i < nSel; ++i) {
        SelectedSectionEntry sel;
        sel.sectionId = r.string();
        sel.selected = r.boolean();
        spec.selectedSections.push_back(sel);
    }
    spec.snapshotId = readOptionalIdentity(r);
    spec.unitPreference = readUnitPreference(r);
    r.expectEnd();
    return spec;
}

core::ContentIdentity ReportCodec::digestData(const ReportSourceSpec& spec)
{
    // dataIdentity＝Data canonical 字节的 SHA-256（§4.4——计算对象
    // ReportCodec-Data；摘要算法经 core 唯一入口，§4.1）。
    return digestBytes(encodeData(spec));
}

// =====================================================================
// ReportCodec 公共面——Full 编码
// =====================================================================

std::vector<std::uint8_t> ReportCodec::encodeFull(const ReportFullFields& full)
{
    // canonical 名序：data, review, sectionModelVersion, sections
    // （"sectionModelVersion" < "sections"——'M'(0x4D) < 's'(0x73)）。
    //
    // 章节按 order（§4.4 规则 3）：输入必须已按 order 严格递增（§4.7 由
    // 字段校验原语保证；编码入口二次把关——静默重排会掩盖构建器违约，
    // 违者 DataInvalid fail-fast）。
    for (std::size_t i = 1; i < full.sections.size(); ++i) {
        if (full.sections[i - 1].order >= full.sections[i].order) {
            throw ReportError(ReportErrorCode::DataInvalid,
                              "ReportCodec: 章节未按 order 严格递增（index "
                                  + std::to_string(i) + "，sectionId '"
                                  + full.sections[i].sectionId + "'）");
        }
    }
    Writer w;
    writeHeader(w, kMagicFull);
    // data＝dataIdentity 全部字段（复用 Data 编码——§4.4"ReportCodec-Full
    // ＝dataIdentity 全部字段＋…"，同一 canonical 字节序列嵌入）。
    const std::vector<std::uint8_t> dataBytes = encodeData(full.data);
    w.raw(dataBytes.data(), dataBytes.size());
    writeReviewMetadata(w, full.review);
    w.string(full.sectionModelVersion);
    w.u32(static_cast<std::uint32_t>(full.sections.size()));
    for (const auto& s : full.sections) {
        writeSection(w, s);
    }
    return w.take();
}

ReportFullFields ReportCodec::parseFull(const std::vector<std::uint8_t>& bytes)
{
    Reader r(bytes);
    readHeader(r, kMagicFull);
    ReportFullFields full;
    // data 段＝encodeData 的完整输出（含其自身 magic＋版本头——复用而非
    // 复制编码逻辑，canonical 序单点）；此处先消费嵌入的 Data 头再读字段。
    readHeader(r, kMagicData);
    full.data.branch.bytes = r.rawArray<16>();
    full.data.inputSliceId = readOptionalIdentity(r);
    full.data.level = readEnum<ReportLevel>(r, kReportLevelCount, "ReportLevel");
    full.data.project.bytes = r.rawArray<16>();
    const std::uint32_t nRefs = r.u32();
    full.data.resultRefs.reserve(nRefs);
    for (std::uint32_t i = 0; i < nRefs; ++i) {
        full.data.resultRefs.push_back(readResultDataSourceRef(r));
    }
    full.data.revision.bytes = r.rawArray<16>();
    full.data.revisionSeq = r.u64();
    const std::uint32_t nSel = r.u32();
    full.data.selectedSections.reserve(nSel);
    for (std::uint32_t i = 0; i < nSel; ++i) {
        SelectedSectionEntry sel;
        sel.sectionId = r.string();
        sel.selected = r.boolean();
        full.data.selectedSections.push_back(sel);
    }
    full.data.snapshotId = readOptionalIdentity(r);
    full.data.unitPreference = readUnitPreference(r);
    full.review = readReviewMetadata(r);
    full.sectionModelVersion = r.string();
    const std::uint32_t nSections = r.u32();
    full.sections.reserve(nSections);
    for (std::uint32_t i = 0; i < nSections; ++i) {
        full.sections.push_back(readSection(r));
    }
    r.expectEnd();
    return full;
}

core::ContentIdentity ReportCodec::digestFull(const ReportFullFields& full)
{
    // contentIdentity＝Full canonical 字节的 SHA-256（§4.4——幂等导出与
    // 冲突判定的唯一依据，§4.1）。
    return digestBytes(encodeFull(full));
}

}  // namespace sdurws::ird::reporting
