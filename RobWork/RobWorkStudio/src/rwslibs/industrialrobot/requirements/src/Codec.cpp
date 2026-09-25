/**
 * @file   Codec.cpp
 * @brief  IRequirementCodec 的产品实现——五对象 canonical 字节布局的
 *         写入（encode）与四步校验链读回（decode）。
 *
 * 设计依据：units/requirements.md §4.7（I-REQ-1 集合字典序＝canonical
 * 形态）、§4.8（RequirementProfile 派生档不入编码）、§9.5（接口契约）、
 * §3.4（确定性总约定）；core.md §6.3（"对什么做摘要归各所有者"——本文件
 * 即 requirements 的字节布局声明点，ContentVersion 由 project 对本文件
 * 产出的字节计算）；任务契约 tasks/foundation/WP-14-T03.json acceptance 2。
 *
 * 布局总表（全部整数/长度/计数小端、无填充、无对齐；字符串 UTF-8 无 NUL）：
 *   头：magic "IRDREQO"(7) | major(4) | minor(4) | wireType(1)
 *   〔注：modeling 布局在头内写 objectTypeToken 串；本单元用 1 字节
 *   wireType（＝§4.1 表行序——RequirementObjectVariant 备择序）承载对象
 *   种类：对象 token 是 requirements 所有权内的固定五值（ObjectTypes.hpp
 *   登记簿），变体序号与 token 一一对应（tokenForWireType 表锁定），
 *   1 字节定长比长度前缀串更省且同样无歧义〕
 *   ObjectId：16 原始字节（bytes 数组序＝规范文本序——core Identity.hpp）
 *   Digest256：32 原始字节
 *   optional<T>：presence(1)〔0/1；其他值 MalformedPayload〕[+T]
 *   string：len(4)+字节（UTF-8 无 NUL——decode 校验）
 *   double：IEEE754 位模式 8 字节小端（非文本——位级确定；decode 拒非有限）
 *   SourcedValue<T>：state(1)〔Provided：载荷＋provenance{kind(1)；
 *     sourceObject presence(1)[+16]；sourceVersion presence(1)[+32]；
 *     methodTag presence(1)[+串]}；Invalid：原串；其余态：无载荷〕
 *   vector<Entry>：count(4)+条目（集合对象写出前规范化排序——I-REQ-1；
 *   decode 校验链第③步核对其已排序且唯一）
 *   OrientationRule/PositionSampling/RequirementReference：kind/method(1)
 *   ＋按 kind 的载荷（其余字段缺省——字节面不写未用字段，与 modeling DH
 *   权威分叉同款"未用面绝不出现在字节里"）
 *
 * 线程安全：本 TU 全部函数/类无共享可变状态（Sink/Reader 为栈上局部），
 * 可重入；确定性：无环境/时钟/locale 依赖——同对象同字节（NFR-COR-02）。
 */

#include <sdurws/ird/requirements/Codec.hpp>

#include <sdurws/ird/core/Provenance.hpp>  // FieldState/ProvenanceKind——SourcedValue 编码的状态词表

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace sdurws::ird::requirements {

namespace {

// =====================================================================
// 编码头常量（家族魔数——modeling "IRDMDLO" 家族的 requirements 同构形态；
// 两单元字节流互不识别是各域序列化登记彼此独立的特性）
// =====================================================================

/// 魔数 "IRDREQO"（requirements object canonical 编码家族标识；7 字节与
/// 家族惯例同宽）。改动即破坏全部存量字节——只允许整体升版替换。
constexpr std::array<std::uint8_t, 7> kMagic{'I', 'R', 'D', 'R', 'E', 'Q', 'O'};

// =====================================================================
// 错误构造（两族——SchemaVersionUnsupported 与 MalformedPayload；params
// 键名与 DiagCodes.hpp 同码 paramSchema 对齐〔object-type/schema-version/
// supported-major〕）
// =====================================================================

/// 版本不可支持（encode 不可产出／decode 不识别——NFR-DEP-04 拒绝猜测；
/// detail 携升级指引——acceptance 2"schema 主版本不识别→RequirementError
/// (REQ-SCHEMA-UNSUPPORTED)＋升级指引"）。
RequirementError unsupportedVersion(const std::string& objectType,
                                    std::uint32_t schemaVersion,
                                    std::uint32_t supportedMajor)
{
    RequirementError e;
    e.code = RequirementErrorCode::SchemaVersionUnsupported;
    e.params.emplace_back("object-type", objectType);
    e.params.emplace_back("schema-version", std::to_string(schemaVersion));
    e.params.emplace_back("supported-major", std::to_string(supportedMajor));
    e.detail = "requirements/codec: 对象 " + objectType + " 的 schema 版本 "
             + std::to_string(schemaVersion) + " 超出本编解码器支持主版本 "
             + std::to_string(supportedMajor)
             + "（NFR-DEP-04：拒绝而非猜测）。处置：升级程序，或以兼容版本"
               "重新编辑（升级器归 project 口径——§4.2 schemaVersion 行）";
    return e;
}

/// 字节破损（decode 结构校验失败——detail 携字节偏移定位）。
RequirementError malformedAt(std::size_t offset, const std::string& what)
{
    RequirementError e;
    e.code = RequirementErrorCode::MalformedPayload;
    e.params.emplace_back("byte-offset", std::to_string(offset));
    e.detail = "requirements/codec: " + what + "（字节偏移 " + std::to_string(offset) + "）";
    return e;
}

/// 解码产物不变量违例（校验链第③/④步——防绕过构造边界的字节）。
RequirementError malformedInvariant(std::string_view invariantToken,
                                    const std::string& subject,
                                    const std::string& detail)
{
    RequirementError e;
    e.code = RequirementErrorCode::MalformedPayload;
    e.params.emplace_back("invariant", std::string(invariantToken));
    e.params.emplace_back("subject", subject);
    e.detail = "requirements/codec: 解码产物违反 " + std::string(invariantToken)
             + "（subject=" + subject + "；" + detail
             + "）——字节绕过构造边界被拒（NFR-COR-03）";
    return e;
}

// =====================================================================
// wireType ↔ 对象 token（1 字节对象种类——§4.1 表行序＝变体备择序；
// 表与 token 登记簿两份副本由测试机械比对钉住）
// =====================================================================

/// wireType→对象 token（表行序＝§4.1 权威表行序；表尾追加纪律）。
std::string_view tokenForWireType(std::uint8_t wireType)
{
    switch (wireType) {
    case 0: return kReqSetObjectType;
    case 1: return kReqPointSetObjectType;
    case 2: return kReqRegionSetObjectType;
    case 3: return kReqConditionSetObjectType;
    case 4: return kReqPlanSetObjectType;
    }
    return {};  // 越界 wireType——decode 头校验先经 validWireType 拒绝
}

/// wireType 是否在登记范围内（decode 头校验用）。
bool validWireType(std::uint8_t wireType)
{
    return wireType <= 4;
}

// =====================================================================
// Sink/Reader——字节写入与校验读回原语（栈上局部，无共享状态）
// =====================================================================

/// 字节写入器（追加式；无格式化依赖——纯位移拼装）。
struct Sink {
    RequirementBytes out;  ///< 输出缓冲（增长式）

    void raw(const void* p, std::size_t n)
    {
        out.insert(out.end(), static_cast<const std::uint8_t*>(p),
                   static_cast<const std::uint8_t*>(p) + n);
    }
    void u8(std::uint8_t v) { out.push_back(v); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void u32(std::uint32_t v)
    {
        // 小端逐字节（跨平台字节序确定——布局总表"全部小端"）。
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    }
    void u64(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }
    void f64(double v)
    {
        // IEEE754 位模式直写（非文本——位级确定；宿主必须 IEEE754，
        // 框架基线已保证）。memcpy 保位型、免 aliasing 问题。
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 须为 64 位 IEEE754");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }
    void bytes16(const core::ObjectId& id) { raw(id.bytes.data(), id.bytes.size()); }
    void bytes32(const core::Digest256& d) { raw(d.data(), d.size()); }
    void str(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void presence(bool has) { boolean(has); }
};

/// 字节读取器（带位置与失败旗标——失败即停，不产出半成品）。
struct Reader {
    const RequirementBytes& in;  ///< 输入字节（只读）
    std::size_t pos = 0;         ///< 读位置（错误定位用）
    bool failed = false;         ///< 失败旗标（一次失败永久置位）

    std::size_t remaining() const { return failed ? 0 : in.size() - pos; }

    bool takeRaw(void* dst, std::size_t n)
    {
        if (failed || remaining() < n) { failed = true; return false; }
        std::memcpy(dst, in.data() + pos, n);
        pos += n;
        return true;
    }
    std::optional<std::uint8_t> u8()
    {
        std::uint8_t v = 0;
        if (!takeRaw(&v, 1)) { return std::nullopt; }
        return v;
    }
    std::optional<bool> boolean()
    {
        const auto b = u8();
        if (!b || (*b != 0 && *b != 1)) { failed = true; return std::nullopt; }
        return *b == 1;
    }
    std::optional<std::uint32_t> u32()
    {
        std::array<std::uint8_t, 4> b{};
        if (!takeRaw(b.data(), b.size())) { return std::nullopt; }
        return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8)
             | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    }
    std::optional<std::uint64_t> u64()
    {
        std::array<std::uint8_t, 8> b{};
        if (!takeRaw(b.data(), b.size())) { return std::nullopt; }
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(b[static_cast<std::size_t>(i)]) << (8 * i);
        }
        return v;
    }
    std::optional<double> f64()
    {
        const auto bits = u64();
        if (!bits) { return std::nullopt; }
        double v = 0.0;
        std::memcpy(&v, &*bits, sizeof(v));
        return v;
    }
    std::optional<core::ObjectId> objectId()
    {
        core::ObjectId id;
        if (!takeRaw(id.bytes.data(), id.bytes.size())) { return std::nullopt; }
        return id;  // 全零（保留值）允许出现——合法性归不变量校验层
    }
    std::optional<core::Digest256> digest32()
    {
        core::Digest256 d{};
        if (!takeRaw(d.data(), d.size())) { return std::nullopt; }
        return d;
    }
    std::optional<std::string> str()
    {
        // 长度前缀串：长度本身不可信——先按剩余字节量约束，防整型欺骗
        // （越界即失败，不做大分配）。
        const auto len = u32();
        if (!len) { return std::nullopt; }
        if (*len > remaining()) { failed = true; return std::nullopt; }
        std::string s(static_cast<std::size_t>(*len), '\0');
        if (!takeRaw(s.data(), s.size())) { return std::nullopt; }
        return s;
    }
};

// =====================================================================
// UTF-8 校验（decode 侧——字符串须成形 UTF-8 且无 NUL；与 modeling 同款
// RFC 3629 子集规则）
// =====================================================================

bool validUtf8NoNul(const std::string& s) noexcept
{
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c == 0x00) { return false; }  // NUL 拒绝（定界语义）
        std::size_t extra = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) { i += 1; continue; }                          // ASCII
        else if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
        else { return false; }                                        // C0/C1/F8+ 非法首字节
        if (n - i - 1 < extra) { return false; }                      // 截断序列
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) { return false; }                // 续字节形态
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((extra == 1 && cp < 0x80u)
            || (extra == 2 && cp < 0x800u)
            || (extra == 3 && cp < 0x10000u)) { return false; }       // 超长编码
        if (cp >= 0xD800u && cp <= 0xDFFFu) { return false; }         // 代理区
        if (cp > 0x10FFFFu) { return false; }                         // 越界码点
        i += extra + 1;
    }
    return true;
}

// =====================================================================
// ProvenanceKind/FieldState 词表字节映射（枚举序＝wire 字节序；全表机械
// 比对归测试——两份词表副本失同步即暴露）
// =====================================================================

std::optional<core::ProvenanceKind> provenanceKindFromByte(std::uint8_t b) noexcept
{
    if (b <= static_cast<std::uint8_t>(core::ProvenanceKind::DerivedReadOnly)) {
        return static_cast<core::ProvenanceKind>(b);
    }
    return std::nullopt;
}

// =====================================================================
// 值编码原语（optional/枚举/Provenance/SourcedValue——各对象编码共用）
// =====================================================================

void putString(Sink& s, const std::string& v) { s.str(v); }
void putBool(Sink& s, bool v) { s.boolean(v); }

template <class F>
void putOptional(Sink& s, bool has, F&& putPayload)
{
    s.presence(has);
    if (has) { putPayload(); }
}

void putRequirementReference(Sink& s, const RequirementReference& r)
{
    // kind(1)＋按 kind 载荷（有载荷种写 16 字节 id；Tool 另写 tcpKey 串；
    // 无载荷种不写——未用字段绝不出现在字节里）。
    s.u8(static_cast<std::uint8_t>(r.kind));
    switch (r.kind) {
    case RequirementRefKind::ModelFrame:
    case RequirementRefKind::SceneObject:
        s.bytes16(*r.objectId);
        break;
    case RequirementRefKind::Tool:
        s.bytes16(*r.objectId);
        putString(s, r.tcpKey);
        break;
    case RequirementRefKind::World:
    case RequirementRefKind::DefaultTcp:
        break;
    }
}

void putValueProvenance(Sink& s, const core::ValueProvenance& v)
{
    s.u8(static_cast<std::uint8_t>(v.kind));
    putOptional(s, v.sourceObject.has_value(), [&] { s.bytes16(*v.sourceObject); });
    putOptional(s, v.sourceVersion.has_value(), [&] { s.bytes32(v.sourceVersion->bytes); });
    putOptional(s, v.methodTag.has_value(), [&] { putString(s, *v.methodTag); });
}

void putGenerationProvenance(Sink& s, const GenerationProvenance& g)
{
    // 一次性参数快照（I-REQ-10——无源条目引用字段可写）。
    putString(s, g.generatorId);
    putString(s, g.instanceId);
    putBool(s, g.linked);
    s.u32(static_cast<std::uint32_t>(g.parameters.size()));
    for (const auto& kv : g.parameters) {
        putString(s, kv.first);
        putString(s, kv.second);
    }
}

void putImportProvenance(Sink& s, const ImportProvenance& im)
{
    // 摘要＋行号（I-REQ-8——无路径字段可写；路径永不进入条目字节）。
    s.bytes32(im.sourceDigest);
    s.u64(im.recordNumber);
}

void putTolerance(Sink& s, const ToleranceSpec& t)
{
    s.f64(t.positionTolerance);
    s.f64(t.orientationTolerance);
}

void putConstrainedDof(Sink& s, const ConstrainedDof& d)
{
    // 六分量掩码按固定序 x,y,z,roll,pitch,yaw 各 1 字节。
    putBool(s, d.x);
    putBool(s, d.y);
    putBool(s, d.z);
    putBool(s, d.roll);
    putBool(s, d.pitch);
    putBool(s, d.yaw);
}

void putRollRange(Sink& s, const RollRange& r)
{
    s.f64(r.min);
    s.f64(r.max);
}

void putOrientationRule(Sink& s, const OrientationRule& rule)
{
    // kind(1)＋该 kind 的载荷（字节面不写未用字段——文件头布局总表注）。
    s.u8(static_cast<std::uint8_t>(rule.kind));
    switch (rule.kind) {
    case OrientationRuleKind::Fixed:
        s.f64(rule.fixedRpy[0]);
        s.f64(rule.fixedRpy[1]);
        s.f64(rule.fixedRpy[2]);
        break;
    case OrientationRuleKind::AlignFrame:
        putRequirementReference(s, rule.targetFrame);
        break;
    case OrientationRuleKind::AlignGeometryNormal:
        s.bytes16(*rule.targetSceneObject);
        s.u8(static_cast<std::uint8_t>(*rule.feature));
        putBool(s, rule.invertNormal);
        break;
    case OrientationRuleKind::PointAtTarget:
        s.f64(rule.targetPoint[0]);
        s.f64(rule.targetPoint[1]);
        s.f64(rule.targetPoint[2]);
        break;
    case OrientationRuleKind::ToolRollFree:
        putRollRange(s, rule.rollRange);
        break;
    }
}

/// SourcedValue<T> 公共编码步：state 字节＋Invalid 态原串（Provided 的
/// 载荷由调用方按 T 形状写——模板不能递归进值布局）。
void putSourcedHeader(Sink& s, core::FieldState st) { s.u8(static_cast<std::uint8_t>(st)); }
void putSourcedInvalidRaw(Sink& s, const std::string& raw)
{
    putString(s, raw);
}
void putSourcedProvenance(Sink& s, const core::ValueProvenance& prov)
{
    putValueProvenance(s, prov);
}

void putPoseConstraint(Sink& s, const PoseConstraint& p)
{
    // position：SourcedValue<Vector3>（state＋载荷＋provenance）。
    putSourcedHeader(s, p.position.state());
    if (p.position.state() == core::FieldState::Provided) {
        const auto& v = p.position.value();
        s.f64(v[0]);
        s.f64(v[1]);
        s.f64(v[2]);
    } else if (p.position.state() == core::FieldState::Invalid) {
        putSourcedInvalidRaw(s, p.position.invalidRawInput());
    }
    if (p.position.state() == core::FieldState::Provided) {
        putSourcedProvenance(s, p.position.provenance());
    }
    putConstrainedDof(s, p.constrainedDof);
    putOrientationRule(s, p.orientation);
}

void putTaskSegment(Sink& s, const TaskSegment& seg)
{
    putBool(s, seg.enabled);
    s.u8(static_cast<std::uint8_t>(seg.axis));
    s.f64(seg.distanceM);
}

void putDemand(Sink& s, const RequirementDemand& d)
{
    putBool(s, d.collisionFreeRequired);
    putOptional(s, d.minimumJointMargin.has_value(), [&] { s.f64(*d.minimumJointMargin); });
}

void putPositionSampling(Sink& s, const PositionSampling& ps)
{
    // method(1)＋按 method 载荷（字节面不写未用形态的载荷）。
    s.u8(static_cast<std::uint8_t>(ps.method));
    switch (ps.method) {
    case PositionSamplingMethod::Grid:
        for (int i = 0; i < 3; ++i) {
            s.u32(ps.counts[static_cast<std::size_t>(i)]);
        }
        break;
    case PositionSamplingMethod::GridBySpacing:
        for (int i = 0; i < 3; ++i) {
            s.f64(ps.spacing[static_cast<std::size_t>(i)]);
        }
        break;
    case PositionSamplingMethod::Random:
        s.u32(ps.count);
        break;
    }
}

void putOrientationSampling(Sink& s, const OrientationSampling& os)
{
    s.u32(os.directionSamples);
    s.u32(os.rollSamples);
    putString(s, os.methodToken);
}

void putCoverageTargets(Sink& s, const CoverageTargets& c)
{
    s.f64(c.minPositionCoverage);
    putOptional(s, c.minOrientationCoverage.has_value(), [&] { s.f64(*c.minOrientationCoverage); });
}

// =====================================================================
// 条目编码（值模型→字节；与 decode 的 readXxx 一一对应）
// =====================================================================

void putTaskPoint(Sink& s, const TaskPoint& p)
{
    s.bytes16(p.objectId);
    putString(s, p.name);
    s.u8(static_cast<std::uint8_t>(p.processTag));
    s.u8(static_cast<std::uint8_t>(p.level));
    putBool(s, p.enabled);
    putValueProvenance(s, p.source);
    putOptional(s, p.generation.has_value(), [&] { putGenerationProvenance(s, *p.generation); });
    putOptional(s, p.importProvenance.has_value(), [&] { putImportProvenance(s, *p.importProvenance); });
    putRequirementReference(s, p.refFrame);
    putOptional(s, p.tcpRef.has_value(), [&] { putRequirementReference(s, *p.tcpRef); });
    putPoseConstraint(s, p.pose);
    putTolerance(s, p.tolerance);
    putTaskSegment(s, p.approach);
    putTaskSegment(s, p.work);
    putTaskSegment(s, p.retract);
    putDemand(s, p.demands);
    putOptional(s, p.sequenceKey.has_value(), [&] { putString(s, *p.sequenceKey); });
    putString(s, p.note);
}

void putWorkRegion(Sink& s, const WorkRegion& r)
{
    s.bytes16(r.objectId);
    putString(s, r.name);
    s.u8(static_cast<std::uint8_t>(r.level));
    putBool(s, r.enabled);
    putValueProvenance(s, r.source);
    putOptional(s, r.generation.has_value(), [&] { putGenerationProvenance(s, *r.generation); });
    putOptional(s, r.importProvenance.has_value(), [&] { putImportProvenance(s, *r.importProvenance); });
    putRequirementReference(s, r.refFrame);
    putOptional(s, r.tcpRef.has_value(), [&] { putRequirementReference(s, *r.tcpRef); });
    // 盒：center(3×f64)＋size(3×f64)，固定序 x,y,z。
    s.f64(r.box.center[0]);
    s.f64(r.box.center[1]);
    s.f64(r.box.center[2]);
    s.f64(r.box.size[0]);
    s.f64(r.box.size[1]);
    s.f64(r.box.size[2]);
    putPositionSampling(s, r.positionSampling);
    putOrientationSampling(s, r.orientationSampling);
    putCoverageTargets(s, r.coverageTargets);
    putDemand(s, r.demands);
    putOptional(s, r.sequenceKey.has_value(), [&] { putString(s, *r.sequenceKey); });
    putString(s, r.note);
}

/// 单个 SourcedValue<double> 的完整编码（state＋载荷＋provenance）。
void putSourcedF64(Sink& s, const core::SourcedValue<double>& v)
{
    putSourcedHeader(s, v.state());
    if (v.state() == core::FieldState::Provided) {
        s.f64(v.value());
    } else if (v.state() == core::FieldState::Invalid) {
        putSourcedInvalidRaw(s, v.invalidRawInput());
    }
    if (v.state() == core::FieldState::Provided) {
        putSourcedProvenance(s, v.provenance());
    }
}

/// 单个 SourcedValue<Vector3> 的完整编码（state＋3×f64＋provenance）。
void putSourcedVec3(Sink& s, const core::SourcedValue<rw::math::Vector3D<double>>& v)
{
    putSourcedHeader(s, v.state());
    if (v.state() == core::FieldState::Provided) {
        s.f64(v.value()[0]);
        s.f64(v.value()[1]);
        s.f64(v.value()[2]);
    } else if (v.state() == core::FieldState::Invalid) {
        putSourcedInvalidRaw(s, v.invalidRawInput());
    }
    if (v.state() == core::FieldState::Provided) {
        putSourcedProvenance(s, v.provenance());
    }
}

void putOperatingCondition(Sink& s, const OperatingCondition& c)
{
    s.bytes16(c.objectId);
    putString(s, c.name);
    s.u8(static_cast<std::uint8_t>(c.level));
    putBool(s, c.enabled);
    s.u32(static_cast<std::uint32_t>(c.environmentRefs.size()));
    for (const auto& id : c.environmentRefs) { s.bytes16(id); }
    s.u32(static_cast<std::uint32_t>(c.toolRefs.size()));
    for (const auto& t : c.toolRefs) { putRequirementReference(s, t); }
    s.u32(static_cast<std::uint32_t>(c.payloads.size()));
    for (const auto& pl : c.payloads) {
        putRequirementReference(s, pl.toolRef);
        putSourcedF64(s, pl.mass);     // 质量（kg）
        putSourcedVec3(s, pl.com);     // 质心（m）
        putSourcedF64(s, pl.inertia);  // 惯量（kg·m²）
    }
    s.u32(static_cast<std::uint32_t>(c.events.size()));
    for (const auto& ev : c.events) {
        s.u8(static_cast<std::uint8_t>(ev.type));
        s.bytes16(ev.stationRef);
        putOptional(s, ev.durationS.has_value(), [&] { s.f64(*ev.durationS); });
    }
    putOptional(s, c.targetCycleTimeS.has_value(), [&] { s.f64(*c.targetCycleTimeS); });
    putDemand(s, c.demands);
    putOptional(s, c.verificationOrderHint.has_value(), [&] { s.u32(*c.verificationOrderHint); });
    s.u8(static_cast<std::uint8_t>(c.appliesTo.scope));
    if (c.appliesTo.scope == AppliesToScope::Stations) {
        s.u32(static_cast<std::uint32_t>(c.appliesTo.stations.size()));
        for (const auto& id : c.appliesTo.stations) { s.bytes16(id); }
    }
    putString(s, c.note);
}

void putSamplingPlan(Sink& s, const SamplingPlan& pl)
{
    s.bytes16(pl.objectId);
    s.bytes16(pl.regionRef);
    putPositionSampling(s, pl.positionSampling);
    putOrientationSampling(s, pl.orientationSampling);
    putString(s, pl.note);
}

// =====================================================================
// 值解码原语（失败经 err 值面返回——调用方短返，不产出半成品）
// =====================================================================

std::optional<std::string> readString(Reader& r, RequirementError& err)
{
    const auto at = r.pos;
    auto v = r.str();
    if (!v) {
        err = malformedAt(at, "字符串长度越界或截断");
        return std::nullopt;
    }
    if (!validUtf8NoNul(*v)) {
        err = malformedAt(at, "字符串非成形 UTF-8 或含 NUL");
        return std::nullopt;
    }
    return v;
}

/// 词表字节→枚举的通用回读（枚举序＝wire 字节序；越界＝词表外——拒绝）。
/// maxByte 由各枚举的登记表行数锁定（表尾追加纪律：新值随行数同步 +1）。
template <class E>
std::optional<E> enumFromByte(std::uint8_t b, std::uint8_t maxByte) noexcept
{
    if (b <= maxByte) { return static_cast<E>(b); }
    return std::nullopt;
}

std::optional<bool> readBool(Reader& r, RequirementError& err)
{
    const auto at = r.pos;
    auto v = r.boolean();
    if (!v) {
        err = malformedAt(at, "布尔字节非法（须 0/1）或截断");
        return std::nullopt;
    }
    return v;
}

std::optional<double> readFiniteF64(Reader& r, RequirementError& err)
{
    const auto at = r.pos;
    auto v = r.f64();
    if (!v) {
        err = malformedAt(at, "double 截断");
        return std::nullopt;
    }
    if (!std::isfinite(*v)) {
        err = malformedAt(at, "double 非有限（NaN/Inf——canonical 字节禁止）");
        return std::nullopt;
    }
    return v;
}

std::optional<core::ObjectId> readObjectId(Reader& r, RequirementError& err)
{
    const auto at = r.pos;
    auto id = r.objectId();
    if (!id) {
        err = malformedAt(at, "ObjectId 截断（须 16 字节）");
        return std::nullopt;
    }
    return id;
}

template <class E, class TryFn>
std::optional<E> readEnumByte(Reader& r, RequirementError& err, TryFn&& tryFn, const char* what)
{
    const auto at = r.pos;
    const auto b = r.u8();
    if (!b) {
        err = malformedAt(at, std::string(what) + " 截断");
        return std::nullopt;
    }
    auto v = tryFn(*b);
    if (!v) {
        err = malformedAt(at, std::string(what) + " 枚举字节越界（词表外）");
        return std::nullopt;
    }
    return v;
}

/// optional 读回公共步：presence 字节＋有值时经 readPayload 装载。
/// 返回 false＝失败（err 已置）。
template <class F>
bool readOptional(Reader& r, RequirementError& err, F&& loadPayload)
{
    const auto at = r.pos;
    const auto has = r.boolean();
    if (!has) {
        err = malformedAt(at, "presence 字节非法（须 0/1）或截断");
        return false;
    }
    if (*has) {
        if (!loadPayload()) { return false; }
    }
    return true;
}

std::optional<RequirementReference> readRequirementReference(Reader& r, RequirementError& err)
{
    RequirementReference ref;
    // 词表字节→枚举：枚举序＝wire 字节序；maxByte=4（五个引用种——表尾
    // 追加纪律下随登记表行数同步）。
    const auto kind = readEnumByte<RequirementRefKind>(
        r, err, [](std::uint8_t b) { return enumFromByte<RequirementRefKind>(b, 4); },
        "RequirementReference.kind");
    if (!kind) { return std::nullopt; }
    ref.kind = *kind;
    switch (ref.kind) {
    case RequirementRefKind::ModelFrame:
    case RequirementRefKind::SceneObject: {
        auto id = readObjectId(r, err);
        if (!id) { return std::nullopt; }
        ref.objectId = *id;
        break;
    }
    case RequirementRefKind::Tool: {
        auto id = readObjectId(r, err);
        if (!id) { return std::nullopt; }
        ref.objectId = *id;
        auto key = readString(r, err);
        if (!key) { return std::nullopt; }
        ref.tcpKey = *key;
        break;
    }
    case RequirementRefKind::World:
    case RequirementRefKind::DefaultTcp:
        break;  // 无载荷种——字节面无载荷
    }
    return ref;
}

std::optional<core::ValueProvenance> readValueProvenance(Reader& r, RequirementError& err)
{
    const auto kind = readEnumByte<core::ProvenanceKind>(
        r, err, [](std::uint8_t b) { return provenanceKindFromByte(b); },
        "ValueProvenance.kind");
    if (!kind) { return std::nullopt; }
    core::ValueProvenance v;
    v.kind = *kind;
    // sourceObject/sourceVersion/methodTag 三个 optional（presence+载荷）。
    if (!readOptional(r, err, [&] {
            auto id = readObjectId(r, err);
            if (!id) { return false; }
            v.sourceObject = *id;
            return true;
        })) {
        return std::nullopt;
    }
    if (!readOptional(r, err, [&] {
            auto d = r.digest32();
            if (!d) { err = malformedAt(r.pos, "provenance.sourceVersion 截断"); return false; }
            v.sourceVersion = core::ContentVersion{*d};
            return true;
        })) {
        return std::nullopt;
    }
    if (!readOptional(r, err, [&] {
            auto tag = readString(r, err);
            if (!tag) { return false; }
            v.methodTag = *tag;
            return true;
        })) {
        return std::nullopt;
    }
    return v;
}

std::optional<GenerationProvenance> readGenerationProvenance(Reader& r, RequirementError& err)
{
    GenerationProvenance g;
    auto gen = readString(r, err);
    if (!gen) { return std::nullopt; }
    g.generatorId = *gen;
    auto inst = readString(r, err);
    if (!inst) { return std::nullopt; }
    g.instanceId = *inst;
    const auto linked = readBool(r, err);
    if (!linked) { return std::nullopt; }
    g.linked = *linked;
    const auto at = r.pos;
    const auto n = r.u32();
    if (!n) { err = malformedAt(at, "生成参数计数截断"); return std::nullopt; }
    g.parameters.reserve(std::min<std::size_t>(*n, 4096u));
    for (std::uint32_t i = 0; i < *n; ++i) {
        auto k = readString(r, err);
        if (!k) { return std::nullopt; }
        auto v = readString(r, err);
        if (!v) { return std::nullopt; }
        g.parameters.emplace_back(*k, *v);
    }
    return g;
}

std::optional<ImportProvenance> readImportProvenance(Reader& r, RequirementError& err)
{
    ImportProvenance im;
    auto d = r.digest32();
    if (!d) { err = malformedAt(r.pos, "导入溯源摘要截断"); return std::nullopt; }
    im.sourceDigest = *d;
    auto rec = r.u64();
    if (!rec) { err = malformedAt(r.pos, "导入溯源记录号截断"); return std::nullopt; }
    im.recordNumber = *rec;
    return im;
}

std::optional<ToleranceSpec> readTolerance(Reader& r, RequirementError& err)
{
    ToleranceSpec t;
    auto pos = readFiniteF64(r, err);
    if (!pos) { return std::nullopt; }
    t.positionTolerance = *pos;
    auto ori = readFiniteF64(r, err);
    if (!ori) { return std::nullopt; }
    t.orientationTolerance = *ori;
    return t;
}

std::optional<ConstrainedDof> readConstrainedDof(Reader& r, RequirementError& err)
{
    ConstrainedDof d;
    // 六分量按固定序 x,y,z,roll,pitch,yaw 逐字节读回（与 putConstrainedDof
    // 写出序一一对应——任一侧失序即 roundtrip 失败，测试钉住）。
    auto one = [&](bool* out) {
        const auto v = readBool(r, err);
        if (!v) { return false; }
        *out = *v;
        return true;
    };
    if (!one(&d.x) || !one(&d.y) || !one(&d.z) || !one(&d.roll) || !one(&d.pitch)
        || !one(&d.yaw)) {
        return std::nullopt;
    }
    return d;
}

std::optional<RollRange> readRollRange(Reader& r, RequirementError& err)
{
    RollRange rr;
    auto mn = readFiniteF64(r, err);
    if (!mn) { return std::nullopt; }
    rr.min = *mn;
    auto mx = readFiniteF64(r, err);
    if (!mx) { return std::nullopt; }
    rr.max = *mx;
    return rr;
}

std::optional<OrientationRule> readOrientationRule(Reader& r, RequirementError& err)
{
    OrientationRule rule;
    const auto kind = readEnumByte<OrientationRuleKind>(
        r, err, [](std::uint8_t b) { return enumFromByte<OrientationRuleKind>(b, 4); },
        "OrientationRule.kind");
    if (!kind) { return std::nullopt; }
    rule.kind = *kind;
    switch (rule.kind) {
    case OrientationRuleKind::Fixed: {
        auto x = readFiniteF64(r, err);
        if (!x) { return std::nullopt; }
        auto y = readFiniteF64(r, err);
        if (!y) { return std::nullopt; }
        auto z = readFiniteF64(r, err);
        if (!z) { return std::nullopt; }
        rule.fixedRpy = rw::math::Vector3D<double>(*x, *y, *z);
        break;
    }
    case OrientationRuleKind::AlignFrame: {
        auto ref = readRequirementReference(r, err);
        if (!ref) { return std::nullopt; }
        rule.targetFrame = *ref;
        break;
    }
    case OrientationRuleKind::AlignGeometryNormal: {
        auto id = readObjectId(r, err);
        if (!id) { return std::nullopt; }
        rule.targetSceneObject = *id;
        const auto feat = readEnumByte<OrientationFeature>(
            r, err, [](std::uint8_t b) { return enumFromByte<OrientationFeature>(b, 1); },
            "OrientationRule.feature");
        if (!feat) { return std::nullopt; }
        rule.feature = *feat;
        const auto inv = readBool(r, err);
        if (!inv) { return std::nullopt; }
        rule.invertNormal = *inv;
        break;
    }
    case OrientationRuleKind::PointAtTarget: {
        auto x = readFiniteF64(r, err);
        if (!x) { return std::nullopt; }
        auto y = readFiniteF64(r, err);
        if (!y) { return std::nullopt; }
        auto z = readFiniteF64(r, err);
        if (!z) { return std::nullopt; }
        rule.targetPoint = rw::math::Vector3D<double>(*x, *y, *z);
        break;
    }
    case OrientationRuleKind::ToolRollFree: {
        auto rr = readRollRange(r, err);
        if (!rr) { return std::nullopt; }
        rule.rollRange = *rr;
        break;
    }
    }
    return rule;
}

/// 单个 SourcedValue<double> 的四态装配（readSourcedState 产出的部件→值）。
core::SourcedValue<double> sourcedF64FromParts(core::FieldState st, double v,
                                               const std::string& raw,
                                               const core::ValueProvenance& prov)
{
    if (st == core::FieldState::Provided) {
        return core::SourcedValue<double>::provided(v, prov);
    }
    if (st == core::FieldState::Invalid) {
        return core::SourcedValue<double>::invalid(raw);
    }
    if (st == core::FieldState::NotApplicable) {
        return core::SourcedValue<double>::notApplicable();
    }
    return core::SourcedValue<double>::notProvided();
}

/// SourcedValue 公共读回步：state 字节＋（Invalid 态原串／Provided 态由
/// loadPayload 装载并取 provenance）。失败返回 false（err 已置）。
bool readSourcedState(Reader& r, RequirementError& err, core::FieldState& st,
                      std::string* invalidRaw, core::ValueProvenance* prov,
                      const std::function<bool()>& loadPayload)
{
    const auto stateByte = r.u8();
    if (!stateByte || *stateByte > 3u) {
        err = malformedAt(r.pos, "SourcedValue.state 非法（须四态词表内）");
        return false;
    }
    st = static_cast<core::FieldState>(*stateByte);
    if (st == core::FieldState::Provided) {
        if (!loadPayload()) { return false; }
        auto p = readValueProvenance(r, err);
        if (!p) { return false; }
        *prov = *p;
    } else if (st == core::FieldState::Invalid) {
        auto raw = readString(r, err);
        if (!raw) { return false; }
        *invalidRaw = *raw;
    }
    return true;
}

std::optional<PoseConstraint> readPoseConstraint(Reader& r, RequirementError& err)
{
    PoseConstraint p;
    core::FieldState st = core::FieldState::NotProvided;
    std::string raw;
    core::ValueProvenance prov{};
    double x = 0.0, y = 0.0, z = 0.0;
    if (!readSourcedState(r, err, st, &raw, &prov, [&] {
            auto vx = readFiniteF64(r, err);
            if (!vx) { return false; }
            auto vy = readFiniteF64(r, err);
            if (!vy) { return false; }
            auto vz = readFiniteF64(r, err);
            if (!vz) { return false; }
            x = *vx; y = *vy; z = *vz;
            return true;
        })) {
        return std::nullopt;
    }
    if (st == core::FieldState::Provided) {
        p.position = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(x, y, z), prov);
    } else if (st == core::FieldState::Invalid) {
        p.position = core::SourcedValue<rw::math::Vector3D<double>>::invalid(raw);
    } else if (st == core::FieldState::NotApplicable) {
        p.position = core::SourcedValue<rw::math::Vector3D<double>>::notApplicable();
    } else {
        p.position = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
    }
    auto dof = readConstrainedDof(r, err);
    if (!dof) { return std::nullopt; }
    p.constrainedDof = *dof;
    auto rule = readOrientationRule(r, err);
    if (!rule) { return std::nullopt; }
    p.orientation = *rule;
    return p;
}

std::optional<TaskSegment> readTaskSegment(Reader& r, RequirementError& err)
{
    TaskSegment seg;
    const auto en = readBool(r, err);
    if (!en) { return std::nullopt; }
    seg.enabled = *en;
    const auto axis = readEnumByte<SegmentAxis>(
        r, err, [](std::uint8_t b) { return enumFromByte<SegmentAxis>(b, 1); },
        "TaskSegment.axis");
    if (!axis) { return std::nullopt; }
    seg.axis = *axis;
    auto dist = readFiniteF64(r, err);
    if (!dist) { return std::nullopt; }
    seg.distanceM = *dist;
    return seg;
}

std::optional<RequirementDemand> readDemand(Reader& r, RequirementError& err)
{
    RequirementDemand d;
    const auto cf = readBool(r, err);
    if (!cf) { return std::nullopt; }
    d.collisionFreeRequired = *cf;
    if (!readOptional(r, err, [&] {
            auto m = readFiniteF64(r, err);
            if (!m) { return false; }
            d.minimumJointMargin = *m;
            return true;
        })) {
        return std::nullopt;
    }
    return d;
}

std::optional<PositionSampling> readPositionSampling(Reader& r, RequirementError& err)
{
    PositionSampling ps;
    const auto method = readEnumByte<PositionSamplingMethod>(
        r, err, [](std::uint8_t b) { return enumFromByte<PositionSamplingMethod>(b, 2); },
        "PositionSampling.method");
    if (!method) { return std::nullopt; }
    ps.method = *method;
    switch (ps.method) {
    case PositionSamplingMethod::Grid:
        for (int i = 0; i < 3; ++i) {
            const auto c = r.u32();
            if (!c) { err = malformedAt(r.pos, "Grid counts 截断"); return std::nullopt; }
            ps.counts[static_cast<std::size_t>(i)] = *c;
        }
        break;
    case PositionSamplingMethod::GridBySpacing:
        for (int i = 0; i < 3; ++i) {
            auto v = readFiniteF64(r, err);
            if (!v) { return std::nullopt; }
            ps.spacing[static_cast<std::size_t>(i)] = *v;
        }
        break;
    case PositionSamplingMethod::Random: {
        const auto c = r.u32();
        if (!c) { err = malformedAt(r.pos, "Random count 截断"); return std::nullopt; }
        ps.count = *c;
        break;
    }
    }
    return ps;
}

std::optional<OrientationSampling> readOrientationSampling(Reader& r, RequirementError& err)
{
    OrientationSampling os;
    const auto dir = r.u32();
    if (!dir) { err = malformedAt(r.pos, "姿态方向采样数截断"); return std::nullopt; }
    os.directionSamples = *dir;
    const auto roll = r.u32();
    if (!roll) { err = malformedAt(r.pos, "姿态滚转采样数截断"); return std::nullopt; }
    os.rollSamples = *roll;
    auto tok = readString(r, err);
    if (!tok) { return std::nullopt; }
    os.methodToken = *tok;
    return os;
}

std::optional<CoverageTargets> readCoverageTargets(Reader& r, RequirementError& err)
{
    CoverageTargets c;
    auto pos = readFiniteF64(r, err);
    if (!pos) { return std::nullopt; }
    c.minPositionCoverage = *pos;
    if (!readOptional(r, err, [&] {
            auto v = readFiniteF64(r, err);
            if (!v) { return false; }
            c.minOrientationCoverage = *v;
            return true;
        })) {
        return std::nullopt;
    }
    return c;
}

std::optional<TaskPoint> readTaskPoint(Reader& r, RequirementError& err)
{
    TaskPoint p;
    auto id = readObjectId(r, err);
    if (!id) { return std::nullopt; }
    p.objectId = *id;
    auto name = readString(r, err);
    if (!name) { return std::nullopt; }
    p.name = *name;
    const auto tag = readEnumByte<ProcessTag>(
        r, err, [](std::uint8_t b) { return enumFromByte<ProcessTag>(b, 10); },
        "TaskPoint.processTag");
    if (!tag) { return std::nullopt; }
    p.processTag = *tag;
    const auto level = readEnumByte<RequirementLevel>(
        r, err, [](std::uint8_t b) { return enumFromByte<RequirementLevel>(b, 1); },
        "TaskPoint.level");
    if (!level) { return std::nullopt; }
    p.level = *level;
    const auto en = readBool(r, err);
    if (!en) { return std::nullopt; }
    p.enabled = *en;
    auto src = readValueProvenance(r, err);
    if (!src) { return std::nullopt; }
    p.source = *src;
    if (!readOptional(r, err, [&] {
            auto g = readGenerationProvenance(r, err);
            if (!g) { return false; }
            p.generation = *g;
            return true;
        })) {
        return std::nullopt;
    }
    if (!readOptional(r, err, [&] {
            auto im = readImportProvenance(r, err);
            if (!im) { return false; }
            p.importProvenance = *im;
            return true;
        })) {
        return std::nullopt;
    }
    auto frame = readRequirementReference(r, err);
    if (!frame) { return std::nullopt; }
    p.refFrame = *frame;
    if (!readOptional(r, err, [&] {
            auto t = readRequirementReference(r, err);
            if (!t) { return false; }
            p.tcpRef = *t;
            return true;
        })) {
        return std::nullopt;
    }
    auto pose = readPoseConstraint(r, err);
    if (!pose) { return std::nullopt; }
    p.pose = *pose;
    auto tol = readTolerance(r, err);
    if (!tol) { return std::nullopt; }
    p.tolerance = *tol;
    auto app = readTaskSegment(r, err);
    if (!app) { return std::nullopt; }
    p.approach = *app;
    auto wrk = readTaskSegment(r, err);
    if (!wrk) { return std::nullopt; }
    p.work = *wrk;
    auto ret = readTaskSegment(r, err);
    if (!ret) { return std::nullopt; }
    p.retract = *ret;
    auto dem = readDemand(r, err);
    if (!dem) { return std::nullopt; }
    p.demands = *dem;
    if (!readOptional(r, err, [&] {
            auto k = readString(r, err);
            if (!k) { return false; }
            p.sequenceKey = *k;
            return true;
        })) {
        return std::nullopt;
    }
    auto note = readString(r, err);
    if (!note) { return std::nullopt; }
    p.note = *note;
    return p;
}

std::optional<WorkRegion> readWorkRegion(Reader& r, RequirementError& err)
{
    WorkRegion w;
    auto id = readObjectId(r, err);
    if (!id) { return std::nullopt; }
    w.objectId = *id;
    auto name = readString(r, err);
    if (!name) { return std::nullopt; }
    w.name = *name;
    const auto level = readEnumByte<RequirementLevel>(
        r, err, [](std::uint8_t b) { return enumFromByte<RequirementLevel>(b, 1); },
        "WorkRegion.level");
    if (!level) { return std::nullopt; }
    w.level = *level;
    const auto en = readBool(r, err);
    if (!en) { return std::nullopt; }
    w.enabled = *en;
    auto src = readValueProvenance(r, err);
    if (!src) { return std::nullopt; }
    w.source = *src;
    if (!readOptional(r, err, [&] {
            auto g = readGenerationProvenance(r, err);
            if (!g) { return false; }
            w.generation = *g;
            return true;
        })) {
        return std::nullopt;
    }
    if (!readOptional(r, err, [&] {
            auto im = readImportProvenance(r, err);
            if (!im) { return false; }
            w.importProvenance = *im;
            return true;
        })) {
        return std::nullopt;
    }
    auto frame = readRequirementReference(r, err);
    if (!frame) { return std::nullopt; }
    w.refFrame = *frame;
    if (!readOptional(r, err, [&] {
            auto t = readRequirementReference(r, err);
            if (!t) { return false; }
            w.tcpRef = *t;
            return true;
        })) {
        return std::nullopt;
    }
    double cc[3] = {0.0, 0.0, 0.0};
    double ss[3] = {0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        auto v = readFiniteF64(r, err);
        if (!v) { return std::nullopt; }
        cc[i] = *v;
    }
    for (int i = 0; i < 3; ++i) {
        auto v = readFiniteF64(r, err);
        if (!v) { return std::nullopt; }
        ss[i] = *v;
    }
    w.box.center = rw::math::Vector3D<double>(cc[0], cc[1], cc[2]);
    w.box.size = rw::math::Vector3D<double>(ss[0], ss[1], ss[2]);
    auto ps = readPositionSampling(r, err);
    if (!ps) { return std::nullopt; }
    w.positionSampling = *ps;
    auto os = readOrientationSampling(r, err);
    if (!os) { return std::nullopt; }
    w.orientationSampling = *os;
    auto ct = readCoverageTargets(r, err);
    if (!ct) { return std::nullopt; }
    w.coverageTargets = *ct;
    auto dem = readDemand(r, err);
    if (!dem) { return std::nullopt; }
    w.demands = *dem;
    if (!readOptional(r, err, [&] {
            auto k = readString(r, err);
            if (!k) { return false; }
            w.sequenceKey = *k;
            return true;
        })) {
        return std::nullopt;
    }
    auto note = readString(r, err);
    if (!note) { return std::nullopt; }
    w.note = *note;
    return w;
}

std::optional<ConditionPayload> readConditionPayload(Reader& r, RequirementError& err)
{
    ConditionPayload pl;
    auto tool = readRequirementReference(r, err);
    if (!tool) { return std::nullopt; }
    pl.toolRef = *tool;
    // mass（kg）：SourcedValue<double> 四态。
    {
        core::FieldState st = core::FieldState::NotProvided;
        std::string raw;
        core::ValueProvenance prov{};
        double v = 0.0;
        if (!readSourcedState(r, err, st, &raw, &prov, [&] {
                auto m = readFiniteF64(r, err);
                if (!m) { return false; }
                v = *m;
                return true;
            })) {
            return std::nullopt;
        }
        pl.mass = sourcedF64FromParts(st, v, raw, prov);
    }
    // com（m）：SourcedValue<Vector3> 四态。
    {
        core::FieldState st = core::FieldState::NotProvided;
        std::string raw;
        core::ValueProvenance prov{};
        double x = 0.0, y = 0.0, z = 0.0;
        if (!readSourcedState(r, err, st, &raw, &prov, [&] {
                auto vx = readFiniteF64(r, err);
                if (!vx) { return false; }
                auto vy = readFiniteF64(r, err);
                if (!vy) { return false; }
                auto vz = readFiniteF64(r, err);
                if (!vz) { return false; }
                x = *vx; y = *vy; z = *vz;
                return true;
            })) {
            return std::nullopt;
        }
        if (st == core::FieldState::Provided) {
            pl.com = core::SourcedValue<rw::math::Vector3D<double>>::provided(
                rw::math::Vector3D<double>(x, y, z), prov);
        } else if (st == core::FieldState::Invalid) {
            pl.com = core::SourcedValue<rw::math::Vector3D<double>>::invalid(raw);
        } else if (st == core::FieldState::NotApplicable) {
            pl.com = core::SourcedValue<rw::math::Vector3D<double>>::notApplicable();
        } else {
            pl.com = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
        }
    }
    // inertia（kg·m²）：SourcedValue<double> 四态。
    {
        core::FieldState st = core::FieldState::NotProvided;
        std::string raw;
        core::ValueProvenance prov{};
        double v = 0.0;
        if (!readSourcedState(r, err, st, &raw, &prov, [&] {
                auto j = readFiniteF64(r, err);
                if (!j) { return false; }
                v = *j;
                return true;
            })) {
            return std::nullopt;
        }
        pl.inertia = sourcedF64FromParts(st, v, raw, prov);
    }
    return pl;
}

std::optional<ConditionEvent> readConditionEvent(Reader& r, RequirementError& err)
{
    ConditionEvent ev;
    const auto type = readEnumByte<ConditionEventType>(
        r, err, [](std::uint8_t b) { return enumFromByte<ConditionEventType>(b, 2); },
        "ConditionEvent.type");
    if (!type) { return std::nullopt; }
    ev.type = *type;
    auto st = readObjectId(r, err);
    if (!st) { return std::nullopt; }
    ev.stationRef = *st;
    if (!readOptional(r, err, [&] {
            auto d = readFiniteF64(r, err);
            if (!d) { return false; }
            ev.durationS = *d;
            return true;
        })) {
        return std::nullopt;
    }
    return ev;
}

std::optional<OperatingCondition> readOperatingCondition(Reader& r, RequirementError& err)
{
    OperatingCondition c;
    auto id = readObjectId(r, err);
    if (!id) { return std::nullopt; }
    c.objectId = *id;
    auto name = readString(r, err);
    if (!name) { return std::nullopt; }
    c.name = *name;
    const auto level = readEnumByte<RequirementLevel>(
        r, err, [](std::uint8_t b) { return enumFromByte<RequirementLevel>(b, 1); },
        "OperatingCondition.level");
    if (!level) { return std::nullopt; }
    c.level = *level;
    const auto en = readBool(r, err);
    if (!en) { return std::nullopt; }
    c.enabled = *en;
    const auto atEnv = r.pos;
    const auto nEnv = r.u32();
    if (!nEnv) { err = malformedAt(atEnv, "环境引用计数截断"); return std::nullopt; }
    c.environmentRefs.reserve(std::min<std::size_t>(*nEnv, 4096u));
    for (std::uint32_t i = 0; i < *nEnv; ++i) {
        auto e = readObjectId(r, err);
        if (!e) { return std::nullopt; }
        c.environmentRefs.push_back(*e);
    }
    const auto atTool = r.pos;
    const auto nTool = r.u32();
    if (!nTool) { err = malformedAt(atTool, "工具引用计数截断"); return std::nullopt; }
    c.toolRefs.reserve(std::min<std::size_t>(*nTool, 4096u));
    for (std::uint32_t i = 0; i < *nTool; ++i) {
        auto t = readRequirementReference(r, err);
        if (!t) { return std::nullopt; }
        c.toolRefs.push_back(*t);
    }
    const auto atPl = r.pos;
    const auto nPl = r.u32();
    if (!nPl) { err = malformedAt(atPl, "负载数截断"); return std::nullopt; }
    c.payloads.reserve(std::min<std::size_t>(*nPl, 4096u));
    for (std::uint32_t i = 0; i < *nPl; ++i) {
        auto p = readConditionPayload(r, err);
        if (!p) { return std::nullopt; }
        c.payloads.push_back(*p);
    }
    const auto atEv = r.pos;
    const auto nEv = r.u32();
    if (!nEv) { err = malformedAt(atEv, "事件数截断"); return std::nullopt; }
    c.events.reserve(std::min<std::size_t>(*nEv, 4096u));
    for (std::uint32_t i = 0; i < *nEv; ++i) {
        auto e = readConditionEvent(r, err);
        if (!e) { return std::nullopt; }
        c.events.push_back(*e);
    }
    if (!readOptional(r, err, [&] {
            auto t = readFiniteF64(r, err);
            if (!t) { return false; }
            c.targetCycleTimeS = *t;
            return true;
        })) {
        return std::nullopt;
    }
    auto dem = readDemand(r, err);
    if (!dem) { return std::nullopt; }
    c.demands = *dem;
    if (!readOptional(r, err, [&] {
            const auto h = r.u32();
            if (!h) { err = malformedAt(r.pos, "必验顺序建议截断"); return false; }
            c.verificationOrderHint = *h;
            return true;
        })) {
        return std::nullopt;
    }
    const auto scope = readEnumByte<AppliesToScope>(
        r, err, [](std::uint8_t b) { return enumFromByte<AppliesToScope>(b, 2); },
        "AppliesTo.scope");
    if (!scope) { return std::nullopt; }
    c.appliesTo.scope = *scope;
    if (c.appliesTo.scope == AppliesToScope::Stations) {
        const auto atSt = r.pos;
        const auto nSt = r.u32();
        if (!nSt) { err = malformedAt(atSt, "工位清单计数截断"); return std::nullopt; }
        c.appliesTo.stations.reserve(std::min<std::size_t>(*nSt, 4096u));
        for (std::uint32_t i = 0; i < *nSt; ++i) {
            auto s = readObjectId(r, err);
            if (!s) { return std::nullopt; }
            c.appliesTo.stations.push_back(*s);
        }
    }
    auto note = readString(r, err);
    if (!note) { return std::nullopt; }
    c.note = *note;
    return c;
}

std::optional<SamplingPlan> readSamplingPlan(Reader& r, RequirementError& err)
{
    SamplingPlan pl;
    auto id = readObjectId(r, err);
    if (!id) { return std::nullopt; }
    pl.objectId = *id;
    auto reg = readObjectId(r, err);
    if (!reg) { return std::nullopt; }
    pl.regionRef = *reg;
    auto ps = readPositionSampling(r, err);
    if (!ps) { return std::nullopt; }
    pl.positionSampling = *ps;
    auto os = readOrientationSampling(r, err);
    if (!os) { return std::nullopt; }
    pl.orientationSampling = *os;
    auto note = readString(r, err);
    if (!note) { return std::nullopt; }
    pl.note = *note;
    return pl;
}

// =====================================================================
// 集合规范化与不变量复核（decode 校验链第③/④步）
// =====================================================================

/// 集合条目数组复核（canonical 序＋集合内 id/名称唯一＋条目级不变量——
/// I-REQ-1/2/3/4/5/6 结构面；复核失败置 err 返回 false）。
template <class Entry, class ValidateFn>
bool checkCollection(const std::vector<Entry>& entries, std::string_view subject,
                     RequirementError& err, ValidateFn&& validateEntry)
{
    // ③规范化：字典序（相邻对严格升——canonical 形态前提，I-REQ-1）＋
    // 集合内 id 唯一（I-REQ-2 集合内半区）。
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const auto prev = entries[i - 1].objectId.toCanonical();
        const auto cur = entries[i].objectId.toCanonical();
        if (prev > cur) {
            err = malformedInvariant("I-REQ-1", std::string(subject),
                                     "条目未按 ObjectId 规范文本字典序存放");
            return false;
        }
        if (prev == cur) {
            err = malformedInvariant("I-REQ-2", std::string(subject),
                                     "集合内 ObjectId 重复");
            return false;
        }
    }
    // 名称唯一（I-REQ-3）＋空名拒绝（§8.1 R0 非空）。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name.empty()) {
            err = malformedInvariant("I-REQ-3", std::string(subject), "条目名称为空");
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (entries[i].name == entries[j].name) {
                err = malformedInvariant("I-REQ-3", entries[i].name, "集合内名称重复");
                return false;
            }
        }
    }
    // ④不变量：条目级全量校验（构造边界语义单源复用——NFR-MNT-04）。
    for (const auto& entry : entries) {
        if (auto e = validateEntry(entry)) {
            err = malformedInvariant("entry-invariant", entry.name,
                                     e->detail.empty() ? "条目校验失败" : e->detail);
            return false;
        }
    }
    return true;
}

}  // namespace

// =====================================================================
// IRequirementCodec 产品实现
// =====================================================================

Expected<RequirementBytes> RequirementCodec::encode(
    const RequirementObjectVariant& object, CodecFormatVersion formatVersion) const
{
    // 对象类型 token（诊断 params 的 object-type——visit 单次取出）。
    const std::string objectType = std::visit([](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, RequirementSet>) { return std::string(kReqSetObjectType); }
        else if constexpr (std::is_same_v<T, PointSet>) { return std::string(kReqPointSetObjectType); }
        else if constexpr (std::is_same_v<T, RegionSet>) { return std::string(kReqRegionSetObjectType); }
        else if constexpr (std::is_same_v<T, ConditionSet>) { return std::string(kReqConditionSetObjectType); }
        else { return std::string(kReqPlanSetObjectType); }
    }, object);

    // ---- ①版本门（唯一拒绝面——NFR-DEP-04 无降级/升级产出）----
    if (!(formatVersion == kCurrentRequirementFormatVersion)) {
        return Expected<RequirementBytes>::err(unsupportedVersion(
            objectType, formatVersion.major, kCurrentRequirementFormatVersion.major));
    }
    // 对象自身 schemaVersion 字段与请求主版本交叉核对（两处版本必须
    // 一致——单一权威常量的双面锚定）。
    const std::uint32_t schemaVersion = std::visit(
        [](const auto& o) { return o.schemaVersion; }, object);
    if (schemaVersion != formatVersion.major) {
        return Expected<RequirementBytes>::err(unsupportedVersion(
            objectType, schemaVersion, formatVersion.major));
    }

    // ---- ②忠实编码（I-REQ-1：集合条目取规范化副本排序写出——同集合
    // 任意输入序必得同字节；副本不回写调用方，"不静默改写"仅指输入值）----
    Sink s;
    s.raw(kMagic.data(), kMagic.size());
    s.u32(kCurrentRequirementFormatVersion.major);
    s.u32(kCurrentRequirementFormatVersion.minor);
    s.u8(static_cast<std::uint8_t>(object.index()));  // wireType＝变体备择序（§4.1 表行序）

    std::visit(
        [&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            s.u32(o.schemaVersion);
            if constexpr (std::is_same_v<T, RequirementSet>) {
                putString(s, o.name);
                putOptional(s, o.pointSetRef.has_value(), [&] { s.bytes16(*o.pointSetRef); });
                putOptional(s, o.regionSetRef.has_value(), [&] { s.bytes16(*o.regionSetRef); });
                putOptional(s, o.conditionSetRef.has_value(), [&] { s.bytes16(*o.conditionSetRef); });
                putOptional(s, o.planSetRef.has_value(), [&] { s.bytes16(*o.planSetRef); });
                putString(s, o.note);
            } else {
                // 四集合对象：条目规范化副本（sortEntriesByObjectId——
                // ObjectId 规范文本字典序）＋计数前缀逐条写出。
                auto entries = o.entries;
                sortEntriesByObjectId(entries);
                s.u32(static_cast<std::uint32_t>(entries.size()));
                for (const auto& e : entries) {
                    if constexpr (std::is_same_v<T, PointSet>) { putTaskPoint(s, e); }
                    else if constexpr (std::is_same_v<T, RegionSet>) { putWorkRegion(s, e); }
                    else if constexpr (std::is_same_v<T, ConditionSet>) { putOperatingCondition(s, e); }
                    else { putSamplingPlan(s, e); }
                }
            }
        },
        object);
    return Expected<RequirementBytes>::ok(std::move(s.out));
}

Expected<RequirementObjectVariant> RequirementCodec::decode(
    const RequirementBytes& bytes, CodecFormatVersion formatVersion) const
{
    // ---- ①magic/版本门（NFR-DEP-04：未知主版本/超支持次版本→稳定拒绝
    // ＋升级指引）----
    if (bytes.size() < kMagic.size() + 4 + 4 + 1) {
        return Expected<RequirementObjectVariant>::err(
            malformedAt(bytes.size(), "头部截断（magic+major+minor+wireType）"));
    }
    if (std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0) {
        return Expected<RequirementObjectVariant>::err(
            malformedAt(0, "magic 不符（非 IRDREQO 家族字节）"));
    }
    Reader r{bytes};
    r.pos = kMagic.size();
    const auto major = r.u32();
    const auto minor = r.u32();
    const auto wt = r.u8();
    if (!major || !minor || !wt) {
        return Expected<RequirementObjectVariant>::err(malformedAt(kMagic.size(), "头部字段截断"));
    }
    if (!validWireType(*wt)) {
        return Expected<RequirementObjectVariant>::err(
            malformedAt(kMagic.size() + 8, "wireType 越界（对象种类词表外）"));
    }
    const std::string objectType{tokenForWireType(*wt)};
    if (*major != formatVersion.major || *major != kCurrentRequirementFormatVersion.major) {
        return Expected<RequirementObjectVariant>::err(unsupportedVersion(
            objectType, *major, formatVersion.major));
    }
    if (*minor > kCurrentRequirementFormatVersion.minor) {
        // 超支持次版本＝本程序过旧（NFR-DEP-04 同款稳定拒绝；schema 主
        // 版本未变、仅格式次版本超前——升级程序即可读）。
        RequirementError e = unsupportedVersion(objectType, *major, formatVersion.major);
        e.params.emplace_back("format-minor", std::to_string(*minor));
        e.params.emplace_back("supported-minor",
                              std::to_string(kCurrentRequirementFormatVersion.minor));
        e.detail = "requirements/codec: 对象 " + objectType + " 的编码格式次版本 "
                 + std::to_string(*minor) + " 超出本程序支持的 "
                 + std::to_string(kCurrentRequirementFormatVersion.minor)
                 + "（主版本一致、次版本超前——升级程序即可读，NFR-DEP-04）";
        return Expected<RequirementObjectVariant>::err(std::move(e));
    }

    // ---- ②结构读回（逐字段；失败即整体失败——NFR-COR-03）----
    RequirementError err;
    const auto schemaVersion = r.u32();
    if (!schemaVersion) {
        return Expected<RequirementObjectVariant>::err(malformedAt(r.pos, "schemaVersion 截断"));
    }
    if (*schemaVersion != *major) {
        // 对象 schemaVersion 字段与头 major 交叉核对（两处版本必须一致
        // ——防"头部新版本＋载荷旧结构"的拼接字节）。
        return Expected<RequirementObjectVariant>::err(unsupportedVersion(
            objectType, *schemaVersion, formatVersion.major));
    }
    std::optional<RequirementObjectVariant> parsed;
    switch (*wt) {
    case 0: {
        // RequirementSet（根对象）。
        RequirementSet o;
        o.schemaVersion = *schemaVersion;
        auto name = readString(r, err);
        if (!name) { break; }
        o.name = *name;
        auto four = [&](std::optional<core::ObjectId>& slot) {
            return readOptional(r, err, [&] {
                auto id = readObjectId(r, err);
                if (!id) { return false; }
                slot = *id;
                return true;
            });
        };
        if (!four(o.pointSetRef) || !four(o.regionSetRef) || !four(o.conditionSetRef)
            || !four(o.planSetRef)) {
            break;
        }
        auto note = readString(r, err);
        if (!note) { break; }
        o.note = *note;
        parsed = RequirementObjectVariant{std::move(o)};
        break;
    }
    case 1: {
        // PointSet：count＋条目（条目内含全部字段读回）。
        const auto n = r.u32();
        if (!n) { err = malformedAt(r.pos, "条目计数截断"); break; }
        PointSet o;
        o.schemaVersion = *schemaVersion;
        o.entries.reserve(std::min<std::size_t>(*n, 65536u));
        bool okAll = true;
        for (std::uint32_t i = 0; i < *n; ++i) {
            auto e = readTaskPoint(r, err);
            if (!e) { okAll = false; break; }
            o.entries.push_back(std::move(*e));
        }
        if (!okAll) { break; }
        // ---- ③④规范化＋不变量复核（集合面）----
        if (!checkCollection(o.entries, kReqPointSetObjectType, err, validateTaskPoint)) {
            break;
        }
        parsed = RequirementObjectVariant{std::move(o)};
        break;
    }
    case 2: {
        const auto n = r.u32();
        if (!n) { err = malformedAt(r.pos, "条目计数截断"); break; }
        RegionSet o;
        o.schemaVersion = *schemaVersion;
        o.entries.reserve(std::min<std::size_t>(*n, 65536u));
        bool okAll = true;
        for (std::uint32_t i = 0; i < *n; ++i) {
            auto e = readWorkRegion(r, err);
            if (!e) { okAll = false; break; }
            o.entries.push_back(std::move(*e));
        }
        if (!okAll) { break; }
        if (!checkCollection(o.entries, kReqRegionSetObjectType, err, validateWorkRegion)) {
            break;
        }
        parsed = RequirementObjectVariant{std::move(o)};
        break;
    }
    case 3: {
        const auto n = r.u32();
        if (!n) { err = malformedAt(r.pos, "条目计数截断"); break; }
        ConditionSet o;
        o.schemaVersion = *schemaVersion;
        o.entries.reserve(std::min<std::size_t>(*n, 65536u));
        bool okAll = true;
        for (std::uint32_t i = 0; i < *n; ++i) {
            auto e = readOperatingCondition(r, err);
            if (!e) { okAll = false; break; }
            o.entries.push_back(std::move(*e));
        }
        if (!okAll) { break; }
        if (!checkCollection(o.entries, kReqConditionSetObjectType, err,
                             validateOperatingCondition)) {
            break;
        }
        parsed = RequirementObjectVariant{std::move(o)};
        break;
    }
    case 4: {
        const auto n = r.u32();
        if (!n) { err = malformedAt(r.pos, "条目计数截断"); break; }
        PlanSet o;
        o.schemaVersion = *schemaVersion;
        o.entries.reserve(std::min<std::size_t>(*n, 65536u));
        bool okAll = true;
        for (std::uint32_t i = 0; i < *n; ++i) {
            auto e = readSamplingPlan(r, err);
            if (!e) { okAll = false; break; }
            o.entries.push_back(std::move(*e));
        }
        if (!okAll) { break; }
        // 计划条目无 name 字段——名称唯一性不适用；canonical 序＋id 唯一
        // 由下方专用复核（validateSamplingPlan 走条目级不变量）。
        for (std::size_t i = 1; i < o.entries.size(); ++i) {
            const auto prev = o.entries[i - 1].objectId.toCanonical();
            const auto cur = o.entries[i].objectId.toCanonical();
            if (prev >= cur) {
                err = malformedInvariant(
                    prev == cur ? "I-REQ-2" : "I-REQ-1",
                    std::string{kReqPlanSetObjectType},
                    prev == cur ? "集合内 ObjectId 重复" : "条目未按字典序存放");
                okAll = false;
                break;
            }
        }
        if (!okAll) { break; }
        for (const auto& e : o.entries) {
            if (auto ve = validateSamplingPlan(e)) {
                err = malformedInvariant("entry-invariant", "sampling-plan", ve->detail);
                okAll = false;
                break;
            }
        }
        if (!okAll) { break; }
        parsed = RequirementObjectVariant{std::move(o)};
        break;
    }
    default:
        break;  // validWireType 已拒——防御面
    }
    if (!parsed) {
        if (err.code == RequirementErrorCode::SchemaVersionUnsupported
            && err.params.empty()) {
            // err 未被置位（r.failed 但无 err 写入）——统一归结构破损。
            err = malformedAt(r.pos, "字节结构破损或截断");
        } else if (err.params.empty() && err.detail.empty()) {
            err = malformedAt(r.pos, "字节结构破损或截断");
        }
        return Expected<RequirementObjectVariant>::err(std::move(err));
    }
    // 尾随字节拒绝（canonical 布局定长自洽——多出来的字节即破损）。
    if (!r.failed && r.pos != bytes.size()) {
        return Expected<RequirementObjectVariant>::err(
            malformedAt(r.pos, "尾随字节（canonical 布局外）"));
    }
    return Expected<RequirementObjectVariant>::ok(std::move(*parsed));
}

}  // namespace sdurws::ird::requirements
