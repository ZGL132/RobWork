/**
 * @file   PolicyInput.cpp
 * @brief  PolicyCodec 实现——对象字节编解码（full 形态）、语义闭包投影
 *         编码与内容身份计算（CR-02：摘要唯一经 core ContentDigester）。
 *
 * 设计依据：
 *   - units/policy.md §5.3（canonical 编码与内容身份——编码规则的唯一
 *     权威章节：magic/长度前缀/大端/presence 字节/位模式/规范序/版本化）、
 *     §5.1（RawPolicyInput 管线入口语义）、§4.2（语义闭包字段集合）、
 *     §12 POL-T03 行（本任务产物）
 *   - 任务契约 tasks/foundation/POL-T03.json acceptance 1～3（POL-ID-1~5
 *     往返/位模式/排序无关；近似相等禁入身份；CR-02 摘要单点）
 *
 * 实现纪律（与 PolicyInput.hpp 契约注释一一对应，实现不引入第二口径）：
 *   1. 字节布局唯一事实源＝PolicyCodec 类注释的"编码规范序"与两形态
 *      载荷定义；本文件按字段规范序逐段编码/解码，任何布局调整须先改
 *      契约注释并升 schema 代（§5.3 版本化）。
 *   2. 全部 double 进出编码都经 canonicalF64/parseCanonicalF64（本文件
 *      内**唯一**的浮点进出通道——位模式纪律无双路径）。
 *   3. 全单元唯一的摘要调用点在 contentIdentity（见文件尾）——core
 *      ContentDigester 之外不存在任何哈希实现或哈希常量（CR-02）。
 *   4. 解码绝不按字节内 count 预分配内存（损坏字节的巨型 count 不得
 *      转化为分配压力）——逐条读取、每步剩余长度校验，越界即抛。
 *
 * 错误语义（调用方视角）：
 *   - encode/encodeSemanticProjection/contentIdentity 失败＝调用方契约
 *     违约（fail-fast，抛 PolicyError）——不存在"编码出非法字节"的路径；
 *   - decode 失败＝字节损坏（抛 PolicyError(EncodingInvalid)）或版本代
 *     违约（SchemaVersionFuture/Unknown）——不存在静默截断/跳过。
 *
 * 线程安全：全部函数无共享可变状态（ContentDigester 为栈上局部实例，
 * 每线程各持——core §4.2 原文）；可重入（NFR-COR-02）。
 * 确定性：字段规范序＋大端＋无 locale 比较（字节字典序）＋无环境依赖。
 */

#include <sdurws/ird/policy/PolicyInput.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace sdurws::ird::policy {

namespace {

// =====================================================================
// 常量：帧头几何（两形态共用）。
// =====================================================================

/// 帧头长度＝magic(7)＋形态字节(1)＋schemaVersion(4)＋载荷长度(4)。
constexpr std::size_t kHeaderSize = 16;
/// 载荷长度字段的帧内偏移（回填/校验用）。
constexpr std::size_t kPayloadLenOffset = 12;
/// u32 长度前缀的理论上限防御值（size_t 64 位平台上的实际字符串不可能
/// 达到；损坏调用方的超大字符串在此显式拒绝而非静默截断）。
constexpr std::size_t kMaxPrefixedLength = 0xFFFFFFFFull;

// =====================================================================
// ByteWriter——确定性大端写入器（编码侧唯一出口）。
// =====================================================================

/**
 * @brief 大端序字节写入器（内部实现件——非公共契约）。
 *
 * 所有编码输出都经本类产出，保证大端/长度前缀/直写三种原语只此一套
 * 实现（NFR-MNT-03）。无防御性错误路径——长度上限类违约由调用侧
 * （lengthPrefixedText）显式抛码。
 */
class ByteWriter {
public:
    /// 直写 n 字节原始数据（id128 等已定形字节块用）。
    void raw(const void* data, std::size_t n)
    {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        m_buf.insert(m_buf.end(), bytes, bytes + n);
    }

    /// 写单字节（布尔/枚举/形态等定长字段）。
    void u8(std::uint8_t v) { m_buf.push_back(v); }

    /// 写 u32 大端（schemaVersion/长度前缀/计数）。
    void u32(std::uint32_t v)
    {
        m_buf.push_back(static_cast<std::uint8_t>(v >> 24));
        m_buf.push_back(static_cast<std::uint8_t>(v >> 16));
        m_buf.push_back(static_cast<std::uint8_t>(v >> 8));
        m_buf.push_back(static_cast<std::uint8_t>(v));
    }

    /// 写 16 字节身份块（Id128 bytes 直出——生成与解析同一字节序）。
    void id128(const std::array<std::uint8_t, 16>& bytes) { raw(bytes.data(), bytes.size()); }

    /// presence 字节（0＝缺失/1＝存在——缺失≠空值≠零，CR-02 纪律）。
    void presence(bool has) { u8(has ? 1 : 0); }

    /// u32 长度前缀文本（UTF-8 字节直载，无转码——往返无损）。
    void lengthPrefixedText(std::string_view text)
    {
        // 长度上限防御：超过 u32 表示域即拒绝（实际不可达，纵深防御——
        // 静默截断会破坏往返，违背 NFR-COR-03）。
        if (text.size() > kMaxPrefixedLength) {
            throw PolicyError(PolicyErrorCode::EncodingInvalid,
                              "长度前缀文本超出 u32 表示域（理论防御路径）");
        }
        u32(static_cast<std::uint32_t>(text.size()));
        raw(text.data(), text.size());
    }

    /// IEEE754 位模式 8 字节（唯一浮点出口——canonicalF64 内含非有限拒绝）。
    void f64(double value)
    {
        const auto bits = PolicyCodec::canonicalF64(value);
        raw(bits.data(), bits.size());
    }

    /// 取出全部字节（移动语义——写入器一次性消费）。
    std::vector<std::uint8_t> take() { return std::move(m_buf); }

private:
    std::vector<std::uint8_t> m_buf;   ///< 累积输出缓冲
};

// =====================================================================
// ByteReader——解码侧唯一入口（每步越界校验，损坏字节即拒绝）。
// =====================================================================

/**
 * @brief 大端序字节读取器（内部实现件——非公共契约）。
 *
 * 逐字段读取；**每次读取前**校验剩余长度，不足即抛 EncodingInvalid
 * （字节损坏——绝不越界读、绝不静默补零）。不提供按 count 预分配——
 * 损坏字节的巨型计数只能转化为逐条读取失败，而非内存压力（实现纪律 4）。
 */
class ByteReader {
public:
    /// 绑定待读字节区间（调用方保证 [data, data+size) 有效）。
    ByteReader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

    /// 剩余未读字节数。
    std::size_t remaining() const { return m_size - m_pos; }

    /// 读 n 字节原始数据（不足即抛——EncodingInvalid）。
    void raw(void* out, std::size_t n)
    {
        if (remaining() < n) {
            fail("载荷越界（剩余字节不足）");
        }
        std::memcpy(out, m_data + m_pos, n);
        m_pos += n;
    }

    /// 读单字节。
    std::uint8_t u8()
    {
        if (remaining() < 1) {
            fail("载荷越界（读 u8 时剩余不足）");
        }
        return m_data[m_pos++];
    }

    /// 读 u32 大端。
    std::uint32_t u32()
    {
        if (remaining() < 4) {
            fail("载荷越界（读 u32 时剩余不足）");
        }
        const std::uint32_t v = (static_cast<std::uint32_t>(m_data[m_pos]) << 24)
            | (static_cast<std::uint32_t>(m_data[m_pos + 1]) << 16)
            | (static_cast<std::uint32_t>(m_data[m_pos + 2]) << 8)
            | static_cast<std::uint32_t>(m_data[m_pos + 3]);
        m_pos += 4;
        return v;
    }

    /// 读 16 字节身份块。
    std::array<std::uint8_t, 16> id128()
    {
        std::array<std::uint8_t, 16> bytes{};
        raw(bytes.data(), bytes.size());
        return bytes;
    }

    /// 读 presence 字节（非 0/1 即损坏——presence 是单比特语义的显式编码）。
    bool presence()
    {
        const std::uint8_t v = u8();
        if (v > 1) {
            fail("presence 字节非法（仅允许 0/1）");
        }
        return v == 1;
    }

    /// 读 u32 长度前缀文本（长度越界即抛；字节直载无转码）。
    std::string text()
    {
        const std::uint32_t n = u32();
        if (remaining() < n) {
            fail("文本载荷越界（声明长度超过剩余字节）");
        }
        std::string s(reinterpret_cast<const char*>(m_data + m_pos), n);
        m_pos += n;
        return s;
    }

    /// 读 IEEE754 位模式（唯一浮点入口——parseCanonicalF64 内含非有限拒绝）。
    double f64()
    {
        if (remaining() < 8) {
            fail("载荷越界（读 f64 位模式时剩余不足）");
        }
        const double value = PolicyCodec::parseCanonicalF64(m_data + m_pos);
        m_pos += 8;   // parseCanonicalF64 只读不推进游标——消费量在此登记
        return value;
    }

    /// 终检：载荷须被精确耗尽（尾部剩余＝帧与载荷声明不一致——损坏）。
    void requireExactlyConsumed()
    {
        if (remaining() != 0) {
            fail("载荷未精确耗尽（尾部存在未声明字节）");
        }
    }

private:
    /// 统一失败出口：所有违约走同一稳定码 EncodingInvalid（§9.2"字节
    /// 损坏在解码期报错"），detail 携带定位信息供开发诊断。
    [[noreturn]] static void fail(const char* what)
    {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, std::string{"解码契约违约: "} + what);
    }

    const std::uint8_t* m_data;   ///< 只读字节区间起点（调用方持有）
    std::size_t m_size;           ///< 区间总长
    std::size_t m_pos = 0;        ///< 已消费偏移
};

// =====================================================================
// 枚举 ↔ 字节映射与范围核对（两形态共用；映射值一经交付即冻结——
// 枚举字节进入二进制契约面，runtime/evidence 同款稳定第一纪律）。
// =====================================================================

/// 域枚举合法上界（Self=0/Environment=1/Tool=2/Scene=3）。
constexpr std::uint8_t kMaxDomainValue = 3;
/// 评估模式合法上界（Preview=0/Quick=1/Verified=2——core 枚举序）。
constexpr std::uint8_t kMaxModeValue = 2;
/// ScopeTarget 类别合法上界（Object=0/Role=1/Group=2）。
constexpr std::uint8_t kMaxScopeKindValue = 2;
/// 规则级别合法上界（Must=0/Should=1）。
constexpr std::uint8_t kMaxRuleLevelValue = 1;
/// 来源类别合法上界（Template=0/Imported=1/UserEdited=2/SystemDefault=3）。
constexpr std::uint8_t kMaxOriginKindValue = 3;

/// 枚举字节 → 枚举（范围外即 EncodingInvalid——未知值＝损坏或未来格式）。
CollisionDomain domainFromByte(std::uint8_t v)
{
    if (v > kMaxDomainValue) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "未知碰撞域枚举值");
    }
    return static_cast<CollisionDomain>(v);
}

core::EvaluationMode modeFromByte(std::uint8_t v)
{
    if (v > kMaxModeValue) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "未知评估模式枚举值");
    }
    return static_cast<core::EvaluationMode>(v);
}

ScopeTargetKind scopeKindFromByte(std::uint8_t v)
{
    if (v > kMaxScopeKindValue) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "未知作用域目标类别枚举值");
    }
    return static_cast<ScopeTargetKind>(v);
}

PolicyRuleLevel ruleLevelFromByte(std::uint8_t v)
{
    if (v > kMaxRuleLevelValue) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "未知规则级别枚举值");
    }
    return static_cast<PolicyRuleLevel>(v);
}

PolicyOriginKind originKindFromByte(std::uint8_t v)
{
    if (v > kMaxOriginKindValue) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "未知策略来源类别枚举值");
    }
    return static_cast<PolicyOriginKind>(v);
}

// =====================================================================
// 编码规范序（§5.3"字段按规范序"的机制定义；POL-ID-2 的实现落点）。
// =====================================================================

/**
 * @brief ScopeTarget 规范键比较（严格弱序）。
 *
 * 键＝(kind, kind 内载荷)：kind 枚举序（Object<Role<Group）；kind 内
 * Object 按 object.bytes 字节字典序、Role 按 roleToken、Group 按
 * groupName（均为无 locale 的字节字典序——std::string operator< 即
 * 字节比较，确定性 NFR-COR-02）。
 */
bool scopeTargetLess(const ScopeTarget& a, const ScopeTarget& b)
{
    if (a.kind != b.kind) {
        return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
    }
    switch (a.kind) {
    case ScopeTargetKind::Object:
        return a.object.bytes < b.object.bytes;   // Id128 字节字典序
    case ScopeTargetKind::Role:
        return a.roleToken < b.roleToken;         // 字节字典序（无 locale）
    case ScopeTargetKind::Group:
        return a.groupName < b.groupName;
    }
    return false;   // 不可达（全枚举已覆盖——满足编译器出口要求）
}

/**
 * @brief 规范化一条 PairRule 的无序对承载序（§4.3"{first,second} 与
 *        {second,first} 同一对"）。
 *
 * 返回 {较小端, 较大端}——{a,b} 与 {b,a} 规范化后同一承载序（POL-ID-2
 * 无序对无关性的机制）。两端相同时（自对——解析④的语义检查对象）承载
 * 序不变。
 */
std::pair<ScopeTarget, ScopeTarget> orderedPairEnds(const ScopeTarget& first,
                                                    const ScopeTarget& second)
{
    if (scopeTargetLess(second, first)) {
        return {second, first};
    }
    return {first, second};
}

/**
 * @brief PairRule 规范化列表：无序对端排序＋列表升序（拷贝入参，不改写
 *        调用方数据）。
 *
 * 列表键＝规范化后的 (first, second, level, reason) 字典序——同集合不
 * 同承载序的规则列表编码为同字节（POL-ID-2"规则清单输入顺序打乱→身份
 * 相等"的机制）。拷贝而非原地排序：RawPolicyInput 按原样承载是公共契
 * 约（encode 不修改入参——只读引用）。
 */
std::vector<PairRule> normalizedPairRules(const std::vector<PairRule>& rules)
{
    std::vector<PairRule> out;
    out.reserve(rules.size());
    for (const auto& r : rules) {
        PairRule copy = r;
        // 无序对规范化：两端按 ScopeTarget 规范键升序摆放。
        const auto ends = orderedPairEnds(copy.first, copy.second);
        copy.first = ends.first;
        copy.second = ends.second;
        out.push_back(std::move(copy));
    }
    std::sort(out.begin(), out.end(), [](const PairRule& a, const PairRule& b) {
        if (scopeTargetLess(a.first, b.first) || scopeTargetLess(b.first, a.first)) {
            return scopeTargetLess(a.first, b.first);
        }
        if (scopeTargetLess(a.second, b.second) || scopeTargetLess(b.second, a.second)) {
            return scopeTargetLess(a.second, b.second);
        }
        if (a.level != b.level) {
            return static_cast<std::uint8_t>(a.level) < static_cast<std::uint8_t>(b.level);
        }
        return a.reason < b.reason;   // 字节字典序（可追溯性文本——确定性）
    });
    return out;
}

/**
 * @brief 集合规范化：升序排序＋去重（集合语义——成员相同即同语义，
 *        重复条目与顺序不进入身份）。
 *
 * 模板参数 Compare 供 Id128（bytes 字节序）与枚举（枚举值序）共用同一
 * 规范化通道（单一实现，NFR-MNT-03）。
 */
template <typename T, typename Compare>
std::vector<T> normalizedSet(std::vector<T> items, Compare comp)
{
    std::sort(items.begin(), items.end(), comp);
    items.erase(std::unique(items.begin(), items.end(),
                            [&comp](const T& a, const T& b) { return !comp(a, b) && !comp(b, a); }),
                items.end());
    return items;
}

// =====================================================================
// 逐字段编码/解码原语（两形态复用——单一实现）。
// =====================================================================

/// 编码 ScopeTarget（kind 字节＋kind 内载荷；载荷选择由 kind 唯一决定）。
void writeScopeTarget(ByteWriter& w, const ScopeTarget& t)
{
    w.u8(static_cast<std::uint8_t>(t.kind));
    switch (t.kind) {
    case ScopeTargetKind::Object:
        w.id128(t.object.bytes);
        break;
    case ScopeTargetKind::Role:
        w.lengthPrefixedText(t.roleToken);
        break;
    case ScopeTargetKind::Group:
        w.lengthPrefixedText(t.groupName);
        break;
    }
}

/// 解码 ScopeTarget（kind↔载荷由字节决定——kind 非法在枚举核对处拒绝）。
ScopeTarget readScopeTarget(ByteReader& r)
{
    ScopeTarget t;
    t.kind = scopeKindFromByte(r.u8());
    switch (t.kind) {
    case ScopeTargetKind::Object:
        t.object.bytes = r.id128();
        break;
    case ScopeTargetKind::Role:
        t.roleToken = r.text();
        break;
    case ScopeTargetKind::Group:
        t.groupName = r.text();
        break;
    }
    return t;
}

/// 编码 PairRule（两端＋级别＋理由——两形态同布局：reason 进入身份，
/// §4.3"进入策略身份与评估诊断的可追溯性"）。
void writePairRule(ByteWriter& w, const PairRule& r)
{
    writeScopeTarget(w, r.first);
    writeScopeTarget(w, r.second);
    w.u8(static_cast<std::uint8_t>(r.level));
    w.lengthPrefixedText(r.reason);
}

/// 解码 PairRule（结构契约：reason 非空必填——损坏字节在此拒绝）。
PairRule readPairRule(ByteReader& r)
{
    PairRule out;
    out.first = readScopeTarget(r);
    out.second = readScopeTarget(r);
    out.level = ruleLevelFromByte(r.u8());
    out.reason = r.text();
    if (out.reason.empty()) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid,
                          "PairRule 理由为空（§4.3 非空必填——字节契约违约）");
    }
    return out;
}

/// 编码规则列表（count 前缀＋规范化后的逐条编码）。
void writePairRuleList(ByteWriter& w, const std::vector<PairRule>& rules)
{
    const std::vector<PairRule> normalized = normalizedPairRules(rules);
    w.u32(static_cast<std::uint32_t>(normalized.size()));
    for (const auto& r : normalized) {
        writePairRule(w, r);
    }
}

/// 解码规则列表（逐条读取——绝不按 count 预分配，实现纪律 4）。
std::vector<PairRule> readPairRuleList(ByteReader& r)
{
    const std::uint32_t count = r.u32();
    std::vector<PairRule> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(readPairRule(r));
    }
    return out;
}

/// 编码 RawThresholdInput（原始值位模式＋显示单位 token——full 形态专用；
/// 非有限值在 f64 出口被 canonicalF64 拒绝）。
void writeRawThreshold(ByteWriter& w, const RawThresholdInput& t)
{
    w.f64(t.value);
    w.lengthPrefixedText(t.unitToken);
}

/// 解码 RawThresholdInput（与 writeRawThreshold 严格互逆）。
RawThresholdInput readRawThreshold(ByteReader& r)
{
    RawThresholdInput t;
    t.value = r.f64();
    t.unitToken = r.text();
    return t;
}

/// 编码可选阈值槽位（presence 字节＋存在时载荷——缺失≠空值≠零）。
void writeOptionalRawThreshold(ByteWriter& w, const std::optional<RawThresholdInput>& t)
{
    w.presence(t.has_value());
    if (t.has_value()) {
        writeRawThreshold(w, *t);
    }
}

/// 解码可选阈值槽位（与 writeOptionalRawThreshold 严格互逆）。
std::optional<RawThresholdInput> readOptionalRawThreshold(ByteReader& r)
{
    if (!r.presence()) {
        return std::nullopt;
    }
    return readRawThreshold(r);
}

/// 编码语义闭包投影中的可选阈值（presence＋SI 真值位模式——无来源/域/
/// 显示单位槽位：来源标注不影响身份（§4.1）、域由字段位置隐含、显示
/// 单位不入身份（POL-ID-3）；详见 PolicyCodec::encodeSemanticProjection
/// 契约注释）。
void writeSemanticThreshold(ByteWriter& w, const std::optional<PolicyThreshold>& t)
{
    w.presence(t.has_value());
    if (t.has_value()) {
        w.f64(t->siValue());
    }
}

/// 编码语义闭包投影中的值阈值（行程上限——发布形态必有值）。
void writeSemanticThresholdValue(ByteWriter& w, const PolicyThreshold& t)
{
    w.f64(t.siValue());
}

/// 编码域集合（升序去重——集合语义规范化；POL-ID-2）。
void writeDomainSet(ByteWriter& w, const std::vector<CollisionDomain>& domains)
{
    const auto normalized = normalizedSet(
        domains, [](CollisionDomain a, CollisionDomain b) { return a < b; });
    w.u32(static_cast<std::uint32_t>(normalized.size()));
    for (const auto d : normalized) {
        w.u8(static_cast<std::uint8_t>(d));
    }
}

/// 解码域集合（逐条枚举核对；承载原样——重复/乱序不影响身份，重编码即规范化）。
std::vector<CollisionDomain> readDomainSet(ByteReader& r)
{
    const std::uint32_t count = r.u32();
    std::vector<CollisionDomain> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(domainFromByte(r.u8()));
    }
    return out;
}

/// 编码模式集合（升序去重——同 writeDomainSet 口径）。
void writeModeSet(ByteWriter& w, const std::vector<core::EvaluationMode>& modes)
{
    const auto normalized
        = normalizedSet(modes, [](core::EvaluationMode a, core::EvaluationMode b) { return a < b; });
    w.u32(static_cast<std::uint32_t>(normalized.size()));
    for (const auto m : normalized) {
        w.u8(static_cast<std::uint8_t>(m));
    }
}

/// 解码模式集合。
std::vector<core::EvaluationMode> readModeSet(ByteReader& r)
{
    const std::uint32_t count = r.u32();
    std::vector<core::EvaluationMode> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        out.push_back(modeFromByte(r.u8()));
    }
    return out;
}

/// 编码对象身份集合（bytes 字节字典序升序去重——同 writeDomainSet 口径）。
void writeObjectIdSet(ByteWriter& w, const std::vector<core::ObjectId>& objects)
{
    const auto normalized = normalizedSet(
        objects, [](const core::ObjectId& a, const core::ObjectId& b) { return a.bytes < b.bytes; });
    w.u32(static_cast<std::uint32_t>(normalized.size()));
    for (const auto& o : normalized) {
        w.id128(o.bytes);
    }
}

/// 解码对象身份集合。
std::vector<core::ObjectId> readObjectIdSet(ByteReader& r)
{
    const std::uint32_t count = r.u32();
    std::vector<core::ObjectId> out;
    for (std::uint32_t i = 0; i < count; ++i) {
        core::ObjectId o;
        o.bytes = r.id128();
        out.push_back(std::move(o));
    }
    return out;
}

// =====================================================================
// 结构预检（encode/contentIdentity 的 fail-fast 前置——异常先于任何
// 字节产出；谓词复用 PolicySet.hpp detail——单一事实源，NFR-MNT-03）。
// =====================================================================

/**
 * @brief 发布前置结构核对：PairRule 逐条 detail 谓词（kind↔字段一致、
 *        reason 非空）。
 * @throws PolicyError(PolicyObjectInvalid) 任一条结构非法
 */
void requirePairRulesWellFormed(const RawCollisionInput& collision)
{
    constexpr std::string_view where = "PolicyCodec::encode";
    for (const auto& r : collision.mandatoryPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }
    for (const auto& r : collision.excludedPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }
}

void requirePairRulesWellFormed(const CollisionRules& collision)
{
    constexpr std::string_view where = "PolicyCodec::encodeSemanticProjection";
    for (const auto& r : collision.mandatoryPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }
    for (const auto& r : collision.excludedPairs) {
        detail::requirePairRuleWellFormed(r, where);
    }
}

/**
 * @brief schema 代核对（编码/身份共用——当前代码只对当前代编码与计算
 *        身份；未来/未知代＝调用方契约违约 fail-fast，与发布门同款码面）。
 * @throws PolicyError(SchemaVersionFuture/Unknown)
 */
void requireCurrentSchemaVersion(std::uint32_t schemaVersion)
{
    if (schemaVersion > PolicySchema::currentVersion) {
        throw PolicyError(PolicyErrorCode::SchemaVersionFuture,
                          "schemaVersion 高于当前代（未来版本不可编码，PM-06）");
    }
    if (schemaVersion < PolicySchema::currentVersion) {
        throw PolicyError(PolicyErrorCode::SchemaVersionUnknown,
                          "schemaVersion 低于当前已知最低代");
    }
}

/// 帧头组装（两形态共用：magic＋形态＋schema 代＋载荷长度回填占位）。
std::vector<std::uint8_t> buildFrame(std::uint8_t form, std::uint32_t schemaVersion,
                                     std::vector<std::uint8_t>&& payload)
{
    ByteWriter w;
    // magic 逐字节直写（PolicySchema::magic 为 7 字符 ASCII 字面量——
    // CR-02 登记原文，禁止运行期改动）。
    w.raw(PolicySchema::magic.data(), PolicySchema::magic.size());
    w.u8(form);
    w.u32(schemaVersion);
    w.u32(static_cast<std::uint32_t>(payload.size()));   // 长度前缀（CR-02 纪律）
    // 载荷拼接——帧与载荷一次性成形（无需回填：载荷长度在拼接前已知）。
    std::vector<std::uint8_t> frame = w.take();
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

}  // namespace

// =====================================================================
// PolicyCodec——full 形态（对象字节编解码）。
// =====================================================================

std::vector<std::uint8_t> PolicyCodec::encode(const RawPolicyInput& input)
{
    // ---- fail-fast 预检（校验序固定——确定性；异常先于任何字节产出） ----
    // ① schema 代（未来/未知代不可编码——发布门同款核对，PM-06 同源）。
    requireCurrentSchemaVersion(input.schemaVersion);
    // ② 对象身份有效性（对象字节必须挂在已分配的策略对象上——全零
    // 保留值＝未分配身份，非法）。
    if (!input.policyObject.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "policyObject 身份无效（全零保留值——对象字节须挂已分配对象）");
    }
    // ③ 数值契约锚非空（§4.2 必填列——锚参与内容身份）。
    if (input.numericContractAnchor.empty()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "numericContractAnchor 为空（必填）");
    }
    // ④ 规则结构（kind↔字段一致、reason 非空——否则 decode 产物为非法
    // 实例；谓词复用 PolicySet.hpp detail，单一事实源）。
    requirePairRulesWellFormed(input.collision);

    // ---- 载荷组装（字段规范序；先建载荷再组帧——长度前缀一次成形） ----
    ByteWriter w;
    // 身份块：policyObject（入字节、不入身份——CR-02 排除字段；解码产物
    // 携带之，解析期对象存在性核对与记忆化键需要）。
    w.id128(input.policyObject.bytes);
    // 管理与审计块（同不入身份）：origin 三件＋兼容注记。
    w.u8(static_cast<std::uint8_t>(input.origin.kind));
    w.presence(input.origin.sourceObject.has_value());
    if (input.origin.sourceObject.has_value()) {
        w.id128(input.origin.sourceObject->bytes);
    }
    w.presence(input.origin.note.has_value());
    if (input.origin.note.has_value()) {
        w.lengthPrefixedText(*input.origin.note);
    }
    w.presence(input.compatibilityNotes.has_value());
    if (input.compatibilityNotes.has_value()) {
        w.lengthPrefixedText(*input.compatibilityNotes);
    }
    // 语义闭包锚（full 形态也承载——往返保真；SI 真值语义由解析保证）。
    w.lengthPrefixedText(input.numericContractAnchor);

    // 碰撞规则原始输入。
    w.u8(input.collision.enabled ? 1 : 0);
    writeDomainSet(w, input.collision.enabledDomains);
    writeOptionalRawThreshold(w, input.collision.safetyClearance);   // 原始值＋显示单位
    w.u8(input.collision.excludeAdjacentLinksByDefault ? 1 : 0);
    writePairRuleList(w, input.collision.mandatoryPairs);
    writePairRuleList(w, input.collision.excludedPairs);

    // 关节限位/行程阈值原始输入（全部槽位 optional——未设置显式编码）。
    writeOptionalRawThreshold(w, input.jointThresholds.nearLimitRatio);
    writeOptionalRawThreshold(w, input.jointThresholds.conditionNumberWarning);
    writeOptionalRawThreshold(w, input.jointThresholds.finiteRotationTravelLimit);
    w.u8(input.jointThresholds.travelLimitCheckEnabled ? 1 : 0);

    // 适用范围（集合规范化——POL-ID-2）。
    writeModeSet(w, input.applicability.modes);
    writeObjectIdSet(w, input.applicability.modelObjects);
    writeObjectIdSet(w, input.applicability.taskObjects);
    writeObjectIdSet(w, input.applicability.caseObjects);

    // ---- 组帧（full 形态字节 0x00） ----
    return buildFrame(PolicySchema::formFull, input.schemaVersion, w.take());
}

RawPolicyInput PolicyCodec::decode(const std::vector<std::uint8_t>& encoding)
{
    // ---- 帧级校验（几何→magic→形态→版本→长度前缀） ----
    if (encoding.size() < kHeaderSize) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid,
                          "字节长度不足（小于帧头 16 字节）");
    }
    // magic 逐字节核对（互异登记——CR-02；错 magic＝异种编码混入）。
    if (std::memcmp(encoding.data(), PolicySchema::magic.data(), PolicySchema::magic.size())
        != 0) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid, "magic 不符（非 IRDPOL1 编码）");
    }
    // 形态字节：仅 full 可往返（语义闭包投影非往返载体——它不携带管理
    // 字段，decode 不可恢复 RawPolicyInput；evidence baseline-projection
    // 先例同款拒绝）。
    const std::uint8_t form = encoding[7];
    if (form != PolicySchema::formFull) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid,
                          "形态字节非 full（语义闭包投影编码非往返载体）");
    }
    // schema 代：版本违约例外走 PM-06 码面（未来不前向猜测解析）。
    const std::uint32_t schemaVersion
        = (static_cast<std::uint32_t>(encoding[8]) << 24)
          | (static_cast<std::uint32_t>(encoding[9]) << 16)
          | (static_cast<std::uint32_t>(encoding[10]) << 8)
          | static_cast<std::uint32_t>(encoding[11]);
    requireCurrentSchemaVersion(schemaVersion);
    // 长度前缀：声明值须与实际剩余精确一致（尾部垃圾/截断都＝损坏）。
    const std::uint32_t declaredLen
        = (static_cast<std::uint32_t>(encoding[kPayloadLenOffset]) << 24)
          | (static_cast<std::uint32_t>(encoding[kPayloadLenOffset + 1]) << 16)
          | (static_cast<std::uint32_t>(encoding[kPayloadLenOffset + 2]) << 8)
          | static_cast<std::uint32_t>(encoding[kPayloadLenOffset + 3]);
    if (declaredLen != encoding.size() - kHeaderSize) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid,
                          "载荷长度与实际字节不符（截断或尾部垃圾）");
    }

    // ---- 载荷逐字段解码（每步越界校验——见 ByteReader） ----
    ByteReader r(encoding.data() + kHeaderSize, declaredLen);

    RawPolicyInput out;
    out.schemaVersion = schemaVersion;
    // 身份块：policyObject（全零保留值＝非法实例——保留值纪律）。
    out.policyObject.bytes = r.id128();
    if (!out.policyObject.isValid()) {
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "解码产物 policyObject 为全零保留值（无效实例）");
    }
    // 管理与审计块。
    out.origin.kind = originKindFromByte(r.u8());
    if (r.presence()) {
        core::ObjectId src;
        src.bytes = r.id128();
        out.origin.sourceObject = std::move(src);
    }
    if (r.presence()) {
        out.origin.note = r.text();
    }
    if (r.presence()) {
        out.compatibilityNotes = r.text();
    }
    out.numericContractAnchor = r.text();

    // 碰撞规则原始输入。
    out.collision.enabled = r.u8() != 0;
    out.collision.enabledDomains = readDomainSet(r);
    out.collision.safetyClearance = readOptionalRawThreshold(r);
    out.collision.excludeAdjacentLinksByDefault = r.u8() != 0;
    out.collision.mandatoryPairs = readPairRuleList(r);
    out.collision.excludedPairs = readPairRuleList(r);

    // 关节限位/行程阈值原始输入。
    out.jointThresholds.nearLimitRatio = readOptionalRawThreshold(r);
    out.jointThresholds.conditionNumberWarning = readOptionalRawThreshold(r);
    out.jointThresholds.finiteRotationTravelLimit = readOptionalRawThreshold(r);
    out.jointThresholds.travelLimitCheckEnabled = r.u8() != 0;

    // 适用范围。
    out.applicability.modes = readModeSet(r);
    out.applicability.modelObjects = readObjectIdSet(r);
    out.applicability.taskObjects = readObjectIdSet(r);
    out.applicability.caseObjects = readObjectIdSet(r);

    // 终检：载荷精确耗尽（多一字节＝帧声明与内容不一致——损坏）。
    r.requireExactlyConsumed();
    return out;
}

// =====================================================================
// PolicyCodec——语义闭包投影与内容身份（CR-02 边界）。
// =====================================================================

std::vector<std::uint8_t>
PolicyCodec::encodeSemanticProjection(std::uint32_t schemaVersion,
                                      const CollisionRules& collision,
                                      const JointThresholds& jointThresholds,
                                      const PolicyApplicability& applicability,
                                      std::string_view numericContractAnchor)
{
    // ---- fail-fast 预检（与 encode 同款校验序；入参为已归一 SI 形态） ----
    requireCurrentSchemaVersion(schemaVersion);                      // ① schema 代
    if (numericContractAnchor.empty()) {                             // ② 锚非空
        throw PolicyError(PolicyErrorCode::PolicyObjectInvalid,
                          "numericContractAnchor 为空（必填）");
    }
    requirePairRulesWellFormed(collision);                           // ③ 规则结构

    // ---- 载荷组装（仅语义闭包字段——CR-02 排除字段结构性不存在） ----
    ByteWriter w;
    // 碰撞规则（SI 真值——PolicyThreshold 持归一后数值）。
    w.u8(collision.enabled ? 1 : 0);
    writeDomainSet(w, collision.enabledDomains);
    writeSemanticThreshold(w, collision.safetyClearance);            // presence＋SI m 位模式
    w.u8(collision.excludeAdjacentLinksByDefault ? 1 : 0);
    writePairRuleList(w, collision.mandatoryPairs);                  // level/reason 入身份（§4.3）
    writePairRuleList(w, collision.excludedPairs);

    // 关节限位/行程阈值（只编 SI 数值——来源/域标注不入身份，契约注释
    // "来源标注不影响身份"口径）。
    writeSemanticThreshold(w, jointThresholds.nearLimitRatio);       // presence＋无量纲
    writeSemanticThreshold(w, jointThresholds.conditionNumberWarning);
    writeSemanticThresholdValue(w, jointThresholds.finiteRotationTravelLimit);   // 必有值（SI rad）
    w.u8(jointThresholds.travelLimitCheckEnabled ? 1 : 0);

    // 适用范围（集合规范化同 full——同语义同字节）。
    writeModeSet(w, applicability.modes);
    writeObjectIdSet(w, applicability.modelObjects);
    writeObjectIdSet(w, applicability.taskObjects);
    writeObjectIdSet(w, applicability.caseObjects);

    // 数值契约锚（参与身份——§4.2 语义闭包集合）。
    w.lengthPrefixedText(numericContractAnchor);

    // ---- 组帧（语义投影形态字节 0x01） ----
    return buildFrame(PolicySchema::formSemanticProjection, schemaVersion, w.take());
}

core::ContentIdentity
PolicyCodec::contentIdentity(std::uint32_t schemaVersion,
                             const CollisionRules& collision,
                             const JointThresholds& jointThresholds,
                             const PolicyApplicability& applicability,
                             std::string_view numericContractAnchor)
{
    // 第一步：产出语义闭包投影编码（校验在其内——身份与投影字节不可
    // 独立漂移：同一次调用、同一份字节）。
    const std::vector<std::uint8_t> projection = encodeSemanticProjection(
        schemaVersion, collision, jointThresholds, applicability, numericContractAnchor);

    // 第二步：SHA-256 摘要——**全单元唯一的摘要调用点**（CR-02 处置约束：
    // 摘要算法唯一经 core ContentDigester，core.md §4.2 责任边界的 policy
    // 侧承接；不复制摘要算法、不私设第二哈希路径）。栈上局部实例＝每
    // 线程各持（core §4.2 线程安全原文），本函数可重入。
    core::ContentDigester digester;
    digester.update(projection.data(), projection.size());

    // 第三步：封装为内容身份（finalize 自动完成 FIPS 填充；SHA-256 对
    // 固定 magic 前缀的编码不可能产出全零——isValid() 恒 true）。
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

// =====================================================================
// canonical 浮点原语（§5.3 浮点行在 policy 侧的唯一实现点；evidence
// Slice.cpp 同款双端纪律——位模式承载＋两端非有限拒绝）。
// =====================================================================

std::array<std::uint8_t, 8> PolicyCodec::canonicalF64(double value)
{
    // 非有限拒绝（§5.3"NaN/±Inf 在编码入口拒绝"——非有限值无稳定位模式
    // 语义：NaN 有多种位形态（quiet/signaling、payload 位差异），直通会
    // 让"同语义"产生不同字节，破坏 NFR-COR-02；POL-ID-5 的前置纪律）。
    if (!std::isfinite(value)) {
        throw PolicyError(PolicyErrorCode::ThresholdNonFinite,
                          "遇到非有限 double（NaN/±Inf 编码入口拒绝——§5.3）");
    }
    // 位模式提取：memcpy 按宿主表示取 IEEE754 位型（double 二进制 64 是
    // 全部支持平台的既定表示——evidence/runtime 同款），随后按大端序列
    // 化——字节布局与宿主端序无关。
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double 须为 64 位（IEEE754 二进制 64）");
    std::memcpy(&bits, &value, sizeof(bits));
    std::array<std::uint8_t, 8> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    }
    return out;
}

double PolicyCodec::parseCanonicalF64(const std::uint8_t* bytes)
{
    // 大端重组位型→宿主 double（canonicalF64 的严格逆）。
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits = (bits << 8) | bytes[static_cast<std::size_t>(i)];
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    // 接收端复核（双端纪律另一半——runtime RT-Codec 读取同款）：手工构造
    // /传输损坏的位型在此暴露为异常，而非静默进入身份计算。码面走
    // EncodingInvalid（解码期字节契约违约）——与编码入口的
    // ThresholdNonFinite（调用方契约违约）区分。
    if (!std::isfinite(value)) {
        throw PolicyError(PolicyErrorCode::EncodingInvalid,
                          "非有限 double 位型（NaN/±Inf 接收端拒绝）");
    }
    return value;
}

}  // namespace sdurws::ird::policy
