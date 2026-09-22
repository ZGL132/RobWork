/**
 * @file   Codec.cpp
 * @brief  IRobotDesignCodec 的产品实现——五对象 canonical 字节布局的
 *         写入（encode）与校验性读回（decode）。
 *
 * 设计依据：units/modeling.md §4.8（canonical 序列化五要素：字段定序、
 * 小端、UTF-8、无填充、集合按 ObjectId 规范文本字典序）、§7.1/§14.2
 * D-MDL-5（派生字段不入编码身份）、§4.3-B（selfCollisionHints 不编码）、
 * §9.4.9（接口契约）；core.md §6.3（"对什么做摘要归各所有者"——本文件
 * 即 modeling 的字节布局声明点，ContentVersion 由 project 对本文件产出的
 * 字节计算）；任务契约 tasks/foundation/WP-13-T03.json acceptance 1/3/4。
 *
 * 布局总表（全部整数/长度/计数小端、无填充、无对齐；字符串 UTF-8 无 NUL）：
 *   头：magic "IRDMDLO"(7) | major(4) | minor(4) | objectTypeToken{len(4)+字节}
 *   SourcedValue<T>：state(1) 〔Provided：载荷＋provenance{kind(1)；
 *     sourceObject presence(1)[+16]；sourceVersion presence(1)[+32]；
 *     methodTag presence(1)[+串]}；Invalid：原串；其余态：无载荷〕
 *   double：IEEE754 位模式 8 字节小端（非文本——位级确定，见 putF64 注）
 *   Transform3D：R 9×f64 行主序＋P 3×f64
 *
 * ★ 派生字段排除的落点（D-MDL-5——验收对照点，CodecTest 钉住）：
 *   - 关节条目字节随根对象 authority 模式分叉：Explicit 态写 axis/origin、
 *     不写 dhDerived；StandardDH 态写 dhDerived、不写 axis/origin——任何
 *     模式下派生侧都绝不出现在字节里；
 *   - LinkEntry.selfCollisionHints 不编码（§4.3-B"不入根对象编码权威
 *     语义"）。
 *
 * 线程安全：本 TU 全部函数/类无共享可变状态（Sink/Reader 为栈上局部），
 * 可重入；确定性：无环境/时钟/locale 依赖——同对象同字节（NFR-COR-02）。
 */

#include <sdurws/ird/modeling/Codec.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::modeling {

namespace {

// =====================================================================
// 编码头常量（家族魔数——runtime RT-Codec 家族〔IRDCANO/IRDNAME〕的同构
// 形态；modeling 家族独立魔数：两单元字节流互不识别是特性——各域序列化
// 登记彼此独立，core.md §6.3 分工）
// =====================================================================

/// 魔数 "IRDMDLO"（modeling object canonical 编码家族标识；7 字节与家族
/// 惯例同宽）。改动即破坏全部存量字节——只允许整体升版替换。
constexpr std::array<std::uint8_t, 7> kMagic{'I', 'R', 'D', 'M', 'D', 'L', 'O'};

// =====================================================================
// 错误构造（两族——SchemaVersionUnsupported 与 MalformedPayload；params
// 键名与 DiagCodes.hpp 同码 paramSchema 对齐〔object-type/schema-version/
// supported-major〕，Errors.hpp 注）
// =====================================================================

/// 版本不可支持（encode 不可产出／decode 不识别——NFR-DEP-04 拒绝猜测）。
ModelingError unsupportedVersion(const std::string& objectType,
                                 std::uint32_t schemaVersion,
                                 std::uint32_t supportedMajor)
{
    ModelingError e;
    e.code = ModelingErrorCode::SchemaVersionUnsupported;
    e.params.emplace_back("object-type", objectType);
    e.params.emplace_back("schema-version", std::to_string(schemaVersion));
    e.params.emplace_back("supported-major", std::to_string(supportedMajor));
    e.detail = "modeling/codec: 对象 " + objectType + " 的 schema 版本 "
             + std::to_string(schemaVersion) + " 超出本编解码器支持主版本 "
             + std::to_string(supportedMajor) + "（NFR-DEP-04：拒绝而非猜测）";
    return e;
}

/// 字节破损（decode 结构校验失败——detail 携字节偏移定位）。
ModelingError malformedAt(std::size_t offset, const std::string& what)
{
    ModelingError e;
    e.code = ModelingErrorCode::MalformedPayload;
    e.params.emplace_back("byte-offset", std::to_string(offset));
    e.detail = "modeling/codec: " + what + "（字节偏移 " + std::to_string(offset) + "）";
    return e;
}

/// 解码产物不变量违例（校验链第④步——防绕过构造边界的字节）。
ModelingError malformedInvariant(std::string_view invariantToken,
                                 const std::string& subject)
{
    ModelingError e;
    e.code = ModelingErrorCode::MalformedPayload;
    e.params.emplace_back("invariant", std::string(invariantToken));
    e.params.emplace_back("subject", subject);
    e.detail = "modeling/codec: 解码产物违反不变量 " + std::string(invariantToken)
             + "（subject=" + subject + "）——字节绕过构造边界被拒（NFR-COR-03）";
    return e;
}

// =====================================================================
// UTF-8 校验（decode 侧——字符串须成形 UTF-8 且无 NUL；写入侧输入是
// std::string，本就按字节承载，UTF-8 语义在 decode 边界强制）
// =====================================================================

/**
 * @brief 校验字节串为成形 UTF-8 且不含 NUL（0x00）。
 *
 * 规则（RFC 3629 子集）：合法序列 C0/C1 续字节、超长编码、代理区
 * U+D800–DFFF、>U+10FFFF 一律拒绝；NUL 拒绝（字符串定界语义——IRDNAME
 * "UTF-8 无 NUL"同口径）。
 *
 * 纯函数；确定性。
 */
bool validUtf8NoNul(const std::string& s) noexcept
{
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char b0 = p[i];
        if (b0 == 0x00) { return false; }              // NUL 拒绝
        if (b0 < 0x80) { ++i; continue; }              // ASCII 单字节
        std::size_t len = 0;                           // 序列总长
        std::uint32_t cp = 0;                          // 累积码点
        if ((b0 & 0xE0) == 0xC0) { len = 2; cp = b0 & 0x1Fu; }
        else if ((b0 & 0xF0) == 0xE0) { len = 3; cp = b0 & 0x0Fu; }
        else if ((b0 & 0xF8) == 0xF0) { len = 4; cp = b0 & 0x07u; }
        else { return false; }                         // C0/C1/F5..FF 头字节
        if (i + len > n) { return false; }             // 截断
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char bk = p[i + k];
            if ((bk & 0xC0) != 0x80) { return false; } // 续字节形态
            cp = (cp << 6) | (bk & 0x3Fu);
        }
        if (cp < 0x80u || (len == 3 && cp < 0x800u) || (len == 4 && cp < 0x10000u)) {
            return false;                              // 超长编码
        }
        if (cp >= 0xD800u && cp <= 0xDFFFu) { return false; }  // 代理区
        if (cp > 0x10FFFFu) { return false; }          // Unicode 上界
        i += len;
    }
    return true;
}

// =====================================================================
// Sink——编码写字节流（小端原语；栈上持有 buffer，纯局部状态）
// =====================================================================

class Sink {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }

    void u32(std::uint32_t v)
    {
        // 小端逐字节（§4.8"小端"——固定字节序，与主机序无关）
        buf_.push_back(static_cast<std::uint8_t>(v & 0xFFu));
        buf_.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
        buf_.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
        buf_.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
    }

    void u64(std::uint64_t v)
    {
        for (int k = 0; k < 8; ++k) {
            buf_.push_back(static_cast<std::uint8_t>((v >> (8 * k)) & 0xFFu));
        }
    }

    /// double→IEEE754 位模式（memcpy 保证无解释变换）→小端 8 字节。
    /// 前置：值有限（NaN/Inf 由编码入口逐字段拒绝——位模式仍可写，但
    /// canonical 语义拒绝非有限值，调用方已查）。
    void f64(double v)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "IEEE754 double 假定");
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    void raw(const std::uint8_t* data, std::size_t n)
    {
        buf_.insert(buf_.end(), data, data + n);
    }

    void u128Bytes(const std::array<std::uint8_t, 16>& bytes) { raw(bytes.data(), 16); }
    void digestBytes(const core::Digest256& d) { raw(d.data(), d.size()); }

    /// 字符串＝长度前缀（u32 字节数）＋UTF-8 字节本体（无 NUL——写入侧
    /// 不强制校验〔输入是内部 std::string〕，decode 侧强制）。
    void string(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    void presence(bool has) { u8(has ? 1 : 0); }

    Bytes take() { return std::move(buf_); }

private:
    Bytes buf_;
};

// =====================================================================
// Reader——解码读字节流（全部读取带回越界报告；失败即整体失败）
// =====================================================================

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t n) : data_(data), n_(n) {}

    std::size_t offset() const { return pos_; }
    bool atEnd() const { return pos_ == n_; }

    bool u8(std::uint8_t* out)
    {
        if (n_ - pos_ < 1) { return false; }
        *out = data_[pos_++];
        return true;
    }

    bool u32(std::uint32_t* out)
    {
        if (n_ - pos_ < 4) { return false; }
        *out = static_cast<std::uint32_t>(data_[pos_])
             | (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8)
             | (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16)
             | (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return true;
    }

    bool u64(std::uint64_t* out)
    {
        if (n_ - pos_ < 8) { return false; }
        std::uint64_t v = 0;
        for (int k = 7; k >= 0; --k) {
            v = (v << 8) | data_[pos_ + static_cast<std::size_t>(k)];
        }
        *out = v;
        pos_ += 8;
        return true;
    }

    /// 位模式小端 8 字节→double（有限性由调用方逐字段核查——canonical
    /// 字节里不允许出现非有限值，出现即 MalformedPayload）。
    bool f64(double* out)
    {
        std::uint64_t bits = 0;
        if (!u64(&bits)) { return false; }
        std::memcpy(out, &bits, sizeof(*out));
        return true;
    }

    bool bytes(std::uint8_t* out, std::size_t n)
    {
        if (n_ - pos_ < n) { return false; }
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    bool u128Bytes(std::array<std::uint8_t, 16>* out) { return bytes(out->data(), 16); }
    bool digestBytes(core::Digest256* out) { return bytes(out->data(), out->size()); }

    /// 长度前缀字符串（长度上限＝剩余字节——防伪长度整爆内存；UTF-8/NUL
    /// 由调用方 validUtf8NoNul 核查）。
    bool lengthPrefixed(std::string* out)
    {
        std::uint32_t len = 0;
        if (!u32(&len)) { return false; }
        if (n_ - pos_ < len) { return false; }
        out->assign(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return true;
    }

    bool presence(bool* out)
    {
        std::uint8_t b = 0;
        if (!u8(&b)) { return false; }
        if (b > 1) { return false; }   // presence 字节只有 0/1 两种合法形态
        *out = (b == 1);
        return true;
    }

private:
    const std::uint8_t* data_;
    std::size_t n_;
    std::size_t pos_ = 0;
};

// =====================================================================
// 基础值类型的字节形态（写/读一一对应；读侧逐项做 canonical 校验）
// =====================================================================

/// Vector3D（3×f64，m 或无量纲——随字段）。
void putVector3D(Sink& s, const rw::math::Vector3D<double>& v)
{
    s.f64(v[0]);
    s.f64(v[1]);
    s.f64(v[2]);
}

bool getVector3D(Reader& r, rw::math::Vector3D<double>* out)
{
    double x = 0.0, y = 0.0, z = 0.0;
    if (!r.f64(&x) || !r.f64(&y) || !r.f64(&z)) { return false; }
    *out = rw::math::Vector3D<double>(x, y, z);
    return true;
}

/// Transform3D（R 9×f64 行主序＋P 3×f64——T_ab 的 12 个 double，m/rad）。
void putTransform3D(Sink& s, const rw::math::Transform3D<double>& t)
{
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t col = 0; col < 3; ++col) {
            s.f64(t.R()(row, col));
        }
    }
    s.f64(t.P()[0]);
    s.f64(t.P()[1]);
    s.f64(t.P()[2]);
}

/// 位姿分量读取（12×f64：R 行主序 9＋平移 3）＋有限性核查——分量先入
/// 定长数组再逐元素构造（★ 全程不默认构造 Transform3D：其默认构造引用
/// 框架库外联符号 Rotation3D::identity()，冒烟 header-only 纪律禁止）。
bool readTransformComponents(Reader& r, double (&rMat)[9], double (&p)[3])
{
    for (double& e : rMat) {
        if (!r.f64(&e)) { return false; }
    }
    if (!r.f64(&p[0]) || !r.f64(&p[1]) || !r.f64(&p[2])) { return false; }
    for (double e : rMat) {
        if (!std::isfinite(e)) { return false; }
    }
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) { return false; }
    return true;
}

bool getTransform3D(Reader& r, rw::math::Transform3D<double>* out)
{
    double rMat[9] = {};
    double p[3] = {};
    if (!readTransformComponents(r, rMat, p)) { return false; }
    // 逐元素构造（冒烟纪律：不调用 Rotation3D::identity()/外联符号——
    // 与 runtime Description.hpp detail::identityTransform3D 同款）
    const rw::math::Rotation3D<double> rot(
        rMat[0], rMat[1], rMat[2],
        rMat[3], rMat[4], rMat[5],
        rMat[6], rMat[7], rMat[8]);
    *out = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(p[0], p[1], p[2]), rot);
    return true;
}

/// ObjectId（16 原始字节——规范文本与字节序在 core 定义下等价，存字节省文本化）。
void putObjectId(Sink& s, const core::ObjectId& id) { s.u128Bytes(id.bytes); }

bool getObjectId(Reader& r, core::ObjectId* out) { return r.u128Bytes(&out->bytes); }

// ---- SourcedValue 载荷的写/读分派（overload 集——模板经 ADL 外的普通
// ---- 重载决议逐类型落到对应实现）----

void putValueBytes(Sink& s, double v) { s.f64(v); }
bool getValueBytes(Reader& r, double* out)
{
    if (!r.f64(out)) { return false; }
    return std::isfinite(*out);  // 非有限＝破损（canonical 拒绝 NaN/Inf）
}

void putValueBytes(Sink& s, const JointLimits& lim)
{
    s.f64(lim.first);   // qmin（rad 或 m——随关节类型）
    s.f64(lim.second);  // qmax
}
bool getValueBytes(Reader& r, JointLimits* out)
{
    JointLimits lim{};
    if (!r.f64(&lim.first) || !r.f64(&lim.second)) { return false; }
    if (!std::isfinite(lim.first) || !std::isfinite(lim.second)) { return false; }
    *out = lim;
    return true;
}

void putValueBytes(Sink& s, const rw::math::Vector3D<double>& v) { putVector3D(s, v); }
bool getValueBytes(Reader& r, rw::math::Vector3D<double>* out)
{
    rw::math::Vector3D<double> v(0.0, 0.0, 0.0);
    if (!getVector3D(r, &v)) { return false; }
    if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) { return false; }
    *out = v;
    return true;
}

void putValueBytes(Sink& s, const JointPose& p) { putTransform3D(s, p); }
bool getValueBytes(Reader& r, JointPose* out)
{
    // 分量先读后构造——不默认构造 JointPose/Transform3D（外联符号纪律，
    // 见 readTransformComponents 注）
    double rMat[9] = {};
    double p[3] = {};
    if (!readTransformComponents(r, rMat, p)) { return false; }
    const rw::math::Rotation3D<double> rot(
        rMat[0], rMat[1], rMat[2],
        rMat[3], rMat[4], rMat[5],
        rMat[6], rMat[7], rMat[8]);
    *out = JointPose(rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(p[0], p[1], p[2]), rot));
    return true;
}

void putValueBytes(Sink& s, const InertiaTensor& it)
{
    s.f64(it.ixx);  // kg·m²
    s.f64(it.iyy);
    s.f64(it.izz);
    s.f64(it.ixy);
    s.f64(it.ixz);
    s.f64(it.iyz);
}
bool getValueBytes(Reader& r, InertiaTensor* out)
{
    InertiaTensor it{};
    if (!r.f64(&it.ixx) || !r.f64(&it.iyy) || !r.f64(&it.izz)
        || !r.f64(&it.ixy) || !r.f64(&it.ixz) || !r.f64(&it.iyz)) {
        return false;
    }
    const bool ok = std::isfinite(it.ixx) && std::isfinite(it.iyy) && std::isfinite(it.izz)
                 && std::isfinite(it.ixy) && std::isfinite(it.ixz) && std::isfinite(it.iyz);
    if (!ok) { return false; }
    *out = it;
    return true;
}

/// SourcedValue<T> 写入（state 字节＋按态载荷；provenance 四字段定序）。
template <class T>
void putSourcedValue(Sink& s, const core::SourcedValue<T>& v)
{
    s.u8(static_cast<std::uint8_t>(v.state()));  // 四态序＝core FieldState 声明序
    switch (v.state()) {
    case core::FieldState::Provided: {
        putValueBytes(s, v.value());
        const core::ValueProvenance& p = v.provenance();
        s.u8(static_cast<std::uint8_t>(p.kind));  // 五类序＝ProvenanceKind 声明序
        s.presence(p.sourceObject.has_value());
        if (p.sourceObject.has_value()) { putObjectId(s, *p.sourceObject); }
        s.presence(p.sourceVersion.has_value());
        if (p.sourceVersion.has_value()) { s.digestBytes(p.sourceVersion->bytes); }
        s.presence(p.methodTag.has_value());
        if (p.methodTag.has_value()) { s.string(*p.methodTag); }
        break;
    }
    case core::FieldState::Invalid:
        s.string(v.invalidRawInput());  // 保留原串——NFR-COR-03 不静默丢弃
        break;
    case core::FieldState::NotProvided:
    case core::FieldState::NotApplicable:
        break;  // 无载荷态
    }
}

/// SourcedValue<T> 读回（ValueProvenance::make 工厂校验 P-1 违约视为破损）。
template <class T>
bool getSourcedValue(Reader& r, core::SourcedValue<T>* out, std::string* err)
{
    std::uint8_t stateRaw = 0;
    if (!r.u8(&stateRaw)) { return false; }
    if (stateRaw > 3) {
        *err = "SourcedValue 非法状态字节";
        return false;
    }
    const auto state = static_cast<core::FieldState>(stateRaw);
    switch (state) {
    case core::FieldState::Provided: {
        T value{};
        if (!getValueBytes(r, &value)) {
            *err = "Provided 载荷缺失或含非有限值";
            return false;
        }
        std::uint8_t kindRaw = 0;
        if (!r.u8(&kindRaw) || kindRaw > 4) {
            *err = "ProvenanceKind 非法字节";
            return false;
        }
        core::ValueProvenance p;
        bool hasSourceObject = false;
        bool hasSourceVersion = false;
        bool hasMethodTag = false;
        std::optional<core::ObjectId> srcObj;
        std::optional<core::ContentVersion> srcVer;
        std::optional<std::string> tag;
        if (!r.presence(&hasSourceObject)) { *err = "presence 越界"; return false; }
        if (hasSourceObject) {
            core::ObjectId id;
            if (!getObjectId(r, &id)) { *err = "sourceObject 截断"; return false; }
            srcObj = id;
        }
        if (!r.presence(&hasSourceVersion)) { *err = "presence 越界"; return false; }
        if (hasSourceVersion) {
            core::ContentVersion cv;
            if (!r.digestBytes(&cv.bytes)) { *err = "sourceVersion 截断"; return false; }
            srcVer = cv;
        }
        if (!r.presence(&hasMethodTag)) { *err = "presence 越界"; return false; }
        if (hasMethodTag) {
            std::string m;
            if (!r.lengthPrefixed(&m)) { *err = "methodTag 截断"; return false; }
            tag = m;
        }
        // 工厂校验（P-1：sourceVersion 有值⇒sourceObject 有值；methodTag
        // 语法）——字节层面的违约在此转破损（try/catch 仅此一处：工厂的
        // 异常轨是 core 公共契约，跨单元调用须承接转译为值面错误）
        try {
            p = core::ValueProvenance::make(static_cast<core::ProvenanceKind>(kindRaw),
                                            srcObj, srcVer, tag);
        } catch (const std::exception& ex) {
            *err = std::string("provenance 工厂校验拒绝：") + ex.what();
            return false;
        }
        *out = core::SourcedValue<T>::provided(std::move(value), std::move(p));
        return true;
    }
    case core::FieldState::Invalid: {
        std::string rawInput;
        if (!r.lengthPrefixed(&rawInput)) { *err = "Invalid 原串截断"; return false; }
        if (rawInput.empty()) { *err = "Invalid 原串为空（core 契约禁止）"; return false; }
        try {
            *out = core::SourcedValue<T>::invalid(std::move(rawInput));
        } catch (const std::exception& ex) {
            *err = std::string("invalid 工厂拒绝：") + ex.what();
            return false;
        }
        return true;
    }
    case core::FieldState::NotProvided:
        *out = core::SourcedValue<T>::notProvided();
        return true;
    case core::FieldState::NotApplicable:
        *out = core::SourcedValue<T>::notApplicable();
        return true;
    }
    *err = "SourcedValue 状态不可达";
    return false;
}

// =====================================================================
// 枚举的 wire 映射（u8＝声明序索引；表尾追加纪律保证既有编号稳定——
// 头文件各枚举注。decode 对越界编号一律破损拒绝，不猜测映射）
// =====================================================================

bool getEnum(Reader& r, std::uint8_t maxValue, std::uint8_t* out, std::string* err)
{
    std::uint8_t b = 0;
    if (!r.u8(&b)) { *err = "枚举字节截断"; return false; }
    if (b > maxValue) {
        *err = "枚举值越界";
        return false;
    }
    *out = b;
    return true;
}

/// 枚举→u8（static_cast 依赖声明序即 wire 序——各枚举头注释的表尾纪律）。
template <class E>
std::uint8_t enumWire(E e) noexcept { return static_cast<std::uint8_t>(e); }

// =====================================================================
// 共用子结构：GeometryRef／BodyData（写/读两向；字段定序＝头文件声明序）
// =====================================================================

void putGeometryRef(Sink& s, const GeometryRef& g)
{
    s.string(g.resourceRefId);
    putTransform3D(s, g.localTransform);
    s.u8(enumWire(g.kind));
}

bool getGeometryRef(Reader& r, GeometryRef* out, std::string* err)
{
    GeometryRef g;
    if (!r.lengthPrefixed(&g.resourceRefId)) { *err = "resourceRefId 截断"; return false; }
    if (!validUtf8NoNul(g.resourceRefId)) { *err = "resourceRefId 非 UTF-8/含 NUL"; return false; }
    if (!getTransform3D(r, &g.localTransform)) { *err = "localTransform 截断/非有限"; return false; }
    std::uint8_t kind = 0;
    if (!getEnum(r, 1, &kind, err)) { return false; }  // GeometryKind 两值
    g.kind = static_cast<GeometryKind>(kind);
    *out = std::move(g);
    return true;
}

void putBodyData(Sink& s, const BodyData& b)
{
    putSourcedValue(s, b.mass);           // kg
    putSourcedValue(s, b.centerOfMass);   // m，连杆系下
    putSourcedValue(s, b.inertia);        // kg·m²（六分量）
    s.presence(b.material.has_value());
    if (b.material.has_value()) {
        s.string(b.material->materialId);
        putSourcedValue(s, b.material->density);  // kg/m³
    }
}

bool getBodyData(Reader& r, BodyData* out, std::string* err)
{
    BodyData b;
    if (!getSourcedValue(r, &b.mass, err)) { return false; }
    if (!getSourcedValue(r, &b.centerOfMass, err)) { return false; }
    if (!getSourcedValue(r, &b.inertia, err)) { return false; }
    bool hasMaterial = false;
    if (!r.presence(&hasMaterial)) { *err = "presence 越界"; return false; }
    if (hasMaterial) {
        MaterialRef m;
        if (!r.lengthPrefixed(&m.materialId)) { *err = "materialId 截断"; return false; }
        if (!validUtf8NoNul(m.materialId)) { *err = "materialId 非 UTF-8/含 NUL"; return false; }
        if (!getSourcedValue(r, &m.density, err)) { return false; }
        b.material = std::move(m);
    }
    *out = std::move(b);
    return true;
}

// =====================================================================
// 集合规范化：排序比较器（§4.8"集合按 ObjectId 规范文本字典序"——引用
// 表语义上是集合（§4.3"无重复"），编码前排序写入、解码后校验有序——
// 读写两侧同一规范化，防漂移）
// =====================================================================

/// ObjectId 集合的规范化写入（拷贝排序——encode 纯函数不改入参）。
void putObjectIdSet(Sink& s, const std::vector<core::ObjectId>& refs)
{
    std::vector<std::string> keys;
    keys.reserve(refs.size());
    for (const core::ObjectId& id : refs) {
        keys.push_back(id.toCanonical());
    }
    std::sort(keys.begin(), keys.end());  // 规范文本字典序（§4.8 原文）
    s.u32(static_cast<std::uint32_t>(keys.size()));
    for (const std::string& key : keys) {
        // 从规范文本还原 16 字节写入（文本→字节的解析在 core 已验证为
        // 全等往返；此处再失败属内部错误——canonical 键来自本进程对象）
        auto parsed = core::ObjectId::tryFromCanonical(key);
        // tryFromCanonical 失败不可能（键刚由 toCanonical 产出）；防御：跳过
        if (parsed.has_value()) { putObjectId(s, *parsed); }
    }
}

/// ObjectId 集合的读回＋canonical 校验（严格升序＋无重复）。
bool getObjectIdSet(Reader& r, std::vector<core::ObjectId>* out,
                    std::string* err)
{
    std::uint32_t count = 0;
    if (!r.u32(&count)) { *err = "集合计数截断"; return false; }
    std::vector<core::ObjectId> refs;
    refs.reserve(count);
    std::string prev;
    for (std::uint32_t i = 0; i < count; ++i) {
        core::ObjectId id;
        if (!getObjectId(r, &id)) {
            *err = "引用表条目截断";
            return false;
        }
        const std::string key = id.toCanonical();
        if (i > 0 && !(prev < key)) {
            // 非严格升序＝未规范化或重复——canonical 形态是身份前提（§4.8）
            *err = "引用表未按规范文本字典序/含重复（canonical 违约）";
            return false;
        }
        prev = key;
        refs.push_back(id);
    }
    *out = std::move(refs);
    return true;
}

}  // namespace

// =====================================================================
// 五对象的字段级写/读（每个对象一对函数；字段定序＝对应头文件声明序＝
// 单元卡 §4.3/§4.4/§4.5/§4.6/§4.7 表行序——调整即破坏性变更）
// =====================================================================

namespace {

// ---- RobotDesign（§4.3）----

void putRobotDesign(Sink& s, const RobotDesign& d)
{
    s.u32(d.schemaVersion);
    s.string(d.displayName);
    s.u8(enumWire(d.authority));
    // basePlacement（preset 词表四值——runtime::InstallationPresetToken 声明序）
    s.u8(enumWire(d.basePlacement.preset));
    putSourcedValue(s, d.basePlacement.customEaa);    // rad（EAA）
    putSourcedValue(s, d.basePlacement.basePosition); // m
    // joints（链序保持——串联有序，绝不排序；条目随 authority 分叉，D-MDL-5）
    s.u32(static_cast<std::uint32_t>(d.joints.size()));
    for (const JointEntry& j : d.joints) {
        putObjectId(s, j.objectId);
        s.string(j.localName);
        s.u8(enumWire(j.type));
        s.f64(j.zeroOffset);  // rad/m——两模式均权威
        if (d.authority == AuthorityMode::Explicit) {
            // 显式权威：axis/origin 是权威字段（编码）；dhDerived 是派生
            // 展示缓存（不编码——D-MDL-5）
            putSourcedValue(s, j.axis);
            putSourcedValue(s, j.origin);
        } else {
            // DH 权威：dhDerived 是权威（编码）；axis/origin 派生只读（不编码）
            s.presence(j.dhDerived.has_value());
            if (j.dhDerived.has_value()) {
                s.f64(j.dhDerived->thetaOffset);  // rad
                s.f64(j.dhDerived->d);            // m
                s.f64(j.dhDerived->a);            // m
                s.f64(j.dhDerived->alpha);        // rad
            }
        }
        putSourcedValue(s, j.bounds);        // rad/m——两模式均权威
        putSourcedValue(s, j.workingRange);  // rad——仅 continuous 有语义
    }
    // links（链序保持；selfCollisionHints 不编码——§4.3-B）
    s.u32(static_cast<std::uint32_t>(d.links.size()));
    for (const LinkEntry& l : d.links) {
        putObjectId(s, l.objectId);
        s.string(l.localName);
        putBodyData(s, l.body);
        s.presence(l.visual.has_value());
        if (l.visual.has_value()) { putGeometryRef(s, *l.visual); }
        s.presence(l.collision.has_value());
        if (l.collision.has_value()) { putGeometryRef(s, *l.collision); }
    }
    // defaultTcp / 引用表 / 资源清单 / notes
    s.presence(d.defaultTcp.has_value());
    if (d.defaultTcp.has_value()) {
        putObjectId(s, d.defaultTcp->toolOid);
        s.string(d.defaultTcp->tcpKey);
    }
    putObjectIdSet(s, d.toolRefs);   // 按 ObjectId 规范文本字典序
    putObjectIdSet(s, d.sceneRefs);  // 同上
    s.presence(d.poseSetRef.has_value());
    if (d.poseSetRef.has_value()) { putObjectId(s, *d.poseSetRef); }
    s.presence(d.drivetrainRef.has_value());
    if (d.drivetrainRef.has_value()) { putObjectId(s, *d.drivetrainRef); }
    // resourceManifest 按 resourceId 字典序（清单键唯一——I-MDL-10 载体；
    // 字符串键集合与 ObjectId 集合同一字典序纪律）
    {
        std::vector<const ResourceRef*> items;
        items.reserve(d.resourceManifest.size());
        for (const ResourceRef& res : d.resourceManifest) { items.push_back(&res); }
        std::sort(items.begin(), items.end(),
                  [](const ResourceRef* a, const ResourceRef* b) {
                      return a->resourceId < b->resourceId;
                  });
        s.u32(static_cast<std::uint32_t>(items.size()));
        for (const ResourceRef* res : items) {
            s.string(res->resourceId);
            s.digestBytes(res->contentDigest);
            s.u8(enumWire(res->state));
            s.presence(res->externalRecord.has_value());
            if (res->externalRecord.has_value()) {
                s.string(res->externalRecord->absPath);       // 路径≠身份——仅记录层
                s.digestBytes(res->externalRecord->recordedDigest);
            }
            s.presence(res->solidifiedObject.has_value());
            if (res->solidifiedObject.has_value()) {
                putObjectId(s, res->solidifiedObject->objectId);
                s.digestBytes(res->solidifiedObject->contentVersion.bytes);
            }
        }
    }
    s.string(d.notes);
}

bool getRobotDesign(Reader& r, RobotDesign* out, std::string* err)
{
    RobotDesign d;
    if (!r.u32(&d.schemaVersion)) { *err = "schemaVersion 截断"; return false; }
    if (!r.lengthPrefixed(&d.displayName)) { *err = "displayName 截断"; return false; }
    if (!validUtf8NoNul(d.displayName)) { *err = "displayName 非 UTF-8/含 NUL"; return false; }
    std::uint8_t authorityRaw = 0;
    if (!getEnum(r, 1, &authorityRaw, err)) { return false; }  // AuthorityMode 两值
    d.authority = static_cast<AuthorityMode>(authorityRaw);
    std::uint8_t presetRaw = 0;
    if (!getEnum(r, 3, &presetRaw, err)) { return false; }     // InstallationPresetToken 四值
    d.basePlacement.preset = static_cast<runtime::InstallationPresetToken>(presetRaw);
    if (!getSourcedValue(r, &d.basePlacement.customEaa, err)) { return false; }
    if (!getSourcedValue(r, &d.basePlacement.basePosition, err)) { return false; }

    std::uint32_t jointCount = 0;
    if (!r.u32(&jointCount)) { *err = "joints 计数截断"; return false; }
    d.joints.reserve(jointCount);
    for (std::uint32_t i = 0; i < jointCount; ++i) {
        JointEntry j;
        if (!getObjectId(r, &j.objectId)) { *err = "joints[i].objectId 截断"; return false; }
        if (!r.lengthPrefixed(&j.localName)) { *err = "joints[i].localName 截断"; return false; }
        if (!validUtf8NoNul(j.localName)) { *err = "joints[i].localName 非 UTF-8/含 NUL"; return false; }
        std::uint8_t typeRaw = 0;
        if (!getEnum(r, 3, &typeRaw, err)) { return false; }   // JointType 四值
        j.type = static_cast<JointType>(typeRaw);
        if (!r.f64(&j.zeroOffset)) { *err = "zeroOffset 截断"; return false; }
        if (!std::isfinite(j.zeroOffset)) { *err = "zeroOffset 非有限"; return false; }
        if (d.authority == AuthorityMode::Explicit) {
            // 与 put 对称：显式态读 axis/origin；dhDerived 恒空（不在字节里）
            if (!getSourcedValue(r, &j.axis, err)) { return false; }
            if (!getSourcedValue(r, &j.origin, err)) { return false; }
        } else {
            bool hasDh = false;
            if (!r.presence(&hasDh)) { *err = "dhDerived presence 越界"; return false; }
            if (hasDh) {
                DhParameters dh;
                if (!r.f64(&dh.thetaOffset) || !r.f64(&dh.d)
                    || !r.f64(&dh.a) || !r.f64(&dh.alpha)) {
                    *err = "dhDerived 截断";
                    return false;
                }
                if (!std::isfinite(dh.thetaOffset) || !std::isfinite(dh.d)
                    || !std::isfinite(dh.a) || !std::isfinite(dh.alpha)) {
                    *err = "dhDerived 非有限";
                    return false;
                }
                j.dhDerived = dh;
            }
            // axis/origin＝派生待重算（D-MDL-5：不在字节里——消费方经 T09
            // 转换器按需确定性重算；NotProvided 是解码后的定义载态）
            j.axis = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
            j.origin = core::SourcedValue<JointPose>::notProvided();
        }
        if (!getSourcedValue(r, &j.bounds, err)) { return false; }
        if (!getSourcedValue(r, &j.workingRange, err)) { return false; }
        d.joints.push_back(std::move(j));
    }

    std::uint32_t linkCount = 0;
    if (!r.u32(&linkCount)) { *err = "links 计数截断"; return false; }
    d.links.reserve(linkCount);
    for (std::uint32_t i = 0; i < linkCount; ++i) {
        LinkEntry l;
        if (!getObjectId(r, &l.objectId)) { *err = "links[i].objectId 截断"; return false; }
        if (!r.lengthPrefixed(&l.localName)) { *err = "links[i].localName 截断"; return false; }
        if (!validUtf8NoNul(l.localName)) { *err = "links[i].localName 非 UTF-8/含 NUL"; return false; }
        if (!getBodyData(r, &l.body, err)) { return false; }
        bool hasVisual = false;
        if (!r.presence(&hasVisual)) { *err = "presence 越界"; return false; }
        if (hasVisual && !getGeometryRef(r, &l.visual.emplace(), err)) { return false; }
        bool hasCollision = false;
        if (!r.presence(&hasCollision)) { *err = "presence 越界"; return false; }
        if (hasCollision && !getGeometryRef(r, &l.collision.emplace(), err)) { return false; }
        // selfCollisionHints 不在字节里（设计使然——解码后恒空）
        d.links.push_back(std::move(l));
    }

    bool hasTcp = false;
    if (!r.presence(&hasTcp)) { *err = "presence 越界"; return false; }
    if (hasTcp) {
        TcpRef tcp;
        if (!getObjectId(r, &tcp.toolOid)) { *err = "defaultTcp.toolOid 截断"; return false; }
        if (!r.lengthPrefixed(&tcp.tcpKey)) { *err = "defaultTcp.tcpKey 截断"; return false; }
        if (!validUtf8NoNul(tcp.tcpKey)) { *err = "tcpKey 非 UTF-8/含 NUL"; return false; }
        d.defaultTcp = std::move(tcp);
    }
    if (!getObjectIdSet(r, &d.toolRefs, err)) { return false; }
    if (!getObjectIdSet(r, &d.sceneRefs, err)) { return false; }
    bool hasPoseSet = false;
    if (!r.presence(&hasPoseSet)) { *err = "presence 越界"; return false; }
    if (hasPoseSet) {
        core::ObjectId id;
        if (!getObjectId(r, &id)) { *err = "poseSetRef 截断"; return false; }
        d.poseSetRef = id;
    }
    bool hasDrivetrain = false;
    if (!r.presence(&hasDrivetrain)) { *err = "presence 越界"; return false; }
    if (hasDrivetrain) {
        core::ObjectId id;
        if (!getObjectId(r, &id)) { *err = "drivetrainRef 截断"; return false; }
        d.drivetrainRef = id;
    }
    std::uint32_t resCount = 0;
    if (!r.u32(&resCount)) { *err = "resourceManifest 计数截断"; return false; }
    d.resourceManifest.reserve(resCount);
    std::string prevKey;
    for (std::uint32_t i = 0; i < resCount; ++i) {
        ResourceRef res;
        if (!r.lengthPrefixed(&res.resourceId)) { *err = "resourceId 截断"; return false; }
        if (!validUtf8NoNul(res.resourceId)) { *err = "resourceId 非 UTF-8/含 NUL"; return false; }
        if (i > 0 && !(prevKey < res.resourceId)) {
            *err = "resourceManifest 未按 resourceId 字典序/含重复";
            return false;
        }
        prevKey = res.resourceId;
        if (!r.digestBytes(&res.contentDigest)) { *err = "contentDigest 截断"; return false; }
        std::uint8_t stateRaw = 0;
        if (!getEnum(r, 1, &stateRaw, err)) { return false; }  // ResourceState 两值
        res.state = static_cast<ResourceState>(stateRaw);
        bool hasExternal = false;
        if (!r.presence(&hasExternal)) { *err = "presence 越界"; return false; }
        if (hasExternal) {
            ExternalResourceRecord ext;
            if (!r.lengthPrefixed(&ext.absPath)) { *err = "absPath 截断"; return false; }
            if (!r.digestBytes(&ext.recordedDigest)) { *err = "recordedDigest 截断"; return false; }
            res.externalRecord = std::move(ext);
        }
        bool hasSolidified = false;
        if (!r.presence(&hasSolidified)) { *err = "presence 越界"; return false; }
        if (hasSolidified) {
            SolidifiedRef sol;
            if (!getObjectId(r, &sol.objectId)) { *err = "solidifiedObject 截断"; return false; }
            if (!r.digestBytes(&sol.contentVersion.bytes)) { *err = "solidified cv 截断"; return false; }
            res.solidifiedObject = std::move(sol);
        }
        d.resourceManifest.push_back(std::move(res));
    }
    if (!r.lengthPrefixed(&d.notes)) { *err = "notes 截断"; return false; }
    if (!validUtf8NoNul(d.notes)) { *err = "notes 非 UTF-8/含 NUL"; return false; }
    *out = std::move(d);
    return true;
}

// ---- ToolDefinition（§4.4）----

void putToolDefinition(Sink& s, const ToolDefinition& t)
{
    s.u32(t.schemaVersion);
    putObjectId(s, t.objectId);
    s.string(t.localName);
    s.string(t.displayName);
    putTransform3D(s, t.mountInterface);  // T_flange_tool，m/rad
    // tcpList 按 key 字典序（键为 defaultTcp 引用锚——顺序无语义，规范化）
    {
        std::vector<const TcpEntry*> items;
        items.reserve(t.tcpList.size());
        for (const TcpEntry& e : t.tcpList) { items.push_back(&e); }
        std::sort(items.begin(), items.end(),
                  [](const TcpEntry* a, const TcpEntry* b) { return a->key < b->key; });
        s.u32(static_cast<std::uint32_t>(items.size()));
        for (const TcpEntry* e : items) {
            s.string(e->key);
            putTransform3D(s, e->offset);  // T_tool_tcp，m/rad
            s.string(e->displayName);
        }
    }
    s.presence(t.geometry.has_value());
    if (t.geometry.has_value()) { putGeometryRef(s, *t.geometry); }
    putBodyData(s, t.body);
    s.presence(t.payloadAttributes.has_value());
    if (t.payloadAttributes.has_value()) {
        s.f64(t.payloadAttributes->ratedLoad);  // kg
    }
}

bool getToolDefinition(Reader& r, ToolDefinition* out, std::string* err)
{
    ToolDefinition t;
    if (!r.u32(&t.schemaVersion)) { *err = "schemaVersion 截断"; return false; }
    if (!getObjectId(r, &t.objectId)) { *err = "objectId 截断"; return false; }
    if (!r.lengthPrefixed(&t.localName)) { *err = "localName 截断"; return false; }
    if (!validUtf8NoNul(t.localName)) { *err = "localName 非 UTF-8/含 NUL"; return false; }
    if (!r.lengthPrefixed(&t.displayName)) { *err = "displayName 截断"; return false; }
    if (!validUtf8NoNul(t.displayName)) { *err = "displayName 非 UTF-8/含 NUL"; return false; }
    if (!getTransform3D(r, &t.mountInterface)) { *err = "mountInterface 截断/非有限"; return false; }
    std::uint32_t tcpCount = 0;
    if (!r.u32(&tcpCount)) { *err = "tcpList 计数截断"; return false; }
    t.tcpList.reserve(tcpCount);
    std::string prevKey;
    for (std::uint32_t i = 0; i < tcpCount; ++i) {
        TcpEntry e;
        if (!r.lengthPrefixed(&e.key)) { *err = "tcpList[i].key 截断"; return false; }
        if (!validUtf8NoNul(e.key)) { *err = "tcpKey 非 UTF-8/含 NUL"; return false; }
        if (i > 0 && !(prevKey < e.key)) {
            *err = "tcpList 未按 key 字典序/含重复";
            return false;
        }
        prevKey = e.key;
        if (!getTransform3D(r, &e.offset)) { *err = "tcpList[i].offset 截断/非有限"; return false; }
        if (!r.lengthPrefixed(&e.displayName)) { *err = "tcpList[i].displayName 截断"; return false; }
        if (!validUtf8NoNul(e.displayName)) { *err = "tcp displayName 非 UTF-8/含 NUL"; return false; }
        t.tcpList.push_back(std::move(e));
    }
    bool hasGeometry = false;
    if (!r.presence(&hasGeometry)) { *err = "presence 越界"; return false; }
    if (hasGeometry && !getGeometryRef(r, &t.geometry.emplace(), err)) { return false; }
    if (!getBodyData(r, &t.body, err)) { return false; }
    bool hasPayload = false;
    if (!r.presence(&hasPayload)) { *err = "presence 越界"; return false; }
    if (hasPayload) {
        PayloadAttributes pa;
        if (!r.f64(&pa.ratedLoad)) { *err = "ratedLoad 截断"; return false; }
        if (!std::isfinite(pa.ratedLoad)) { *err = "ratedLoad 非有限"; return false; }
        t.payloadAttributes = pa;
    }
    *out = std::move(t);
    return true;
}

// ---- SceneObject（§4.5）----

void putSceneObject(Sink& s, const SceneObject& o)
{
    s.u32(o.schemaVersion);
    putObjectId(s, o.objectId);
    s.string(o.localName);
    putTransform3D(s, o.worldPose);  // 世界系固连，m/rad（M-11——不预乘安装旋转）
    s.presence(o.geometry.has_value());
    if (o.geometry.has_value()) { putGeometryRef(s, *o.geometry); }
    s.u8(enumWire(o.role));  // 五值词表——ObjectTypes.hpp 声明序
    s.presence(o.collisionProfileHint.has_value());
    if (o.collisionProfileHint.has_value()) { s.string(*o.collisionProfileHint); }
}

bool getSceneObject(Reader& r, SceneObject* out, std::string* err)
{
    SceneObject o;
    if (!r.u32(&o.schemaVersion)) { *err = "schemaVersion 截断"; return false; }
    if (!getObjectId(r, &o.objectId)) { *err = "objectId 截断"; return false; }
    if (!r.lengthPrefixed(&o.localName)) { *err = "localName 截断"; return false; }
    if (!validUtf8NoNul(o.localName)) { *err = "localName 非 UTF-8/含 NUL"; return false; }
    if (!getTransform3D(r, &o.worldPose)) { *err = "worldPose 截断/非有限"; return false; }
    bool hasGeometry = false;
    if (!r.presence(&hasGeometry)) { *err = "presence 越界"; return false; }
    if (hasGeometry && !getGeometryRef(r, &o.geometry.emplace(), err)) { return false; }
    std::uint8_t roleRaw = 0;
    if (!getEnum(r, 4, &roleRaw, err)) { return false; }  // SceneObjectRole 五值
    o.role = static_cast<SceneObjectRole>(roleRaw);
    bool hasHint = false;
    if (!r.presence(&hasHint)) { *err = "presence 越界"; return false; }
    if (hasHint) {
        std::string hint;
        if (!r.lengthPrefixed(&hint)) { *err = "collisionProfileHint 截断"; return false; }
        if (!validUtf8NoNul(hint)) { *err = "collisionProfileHint 非 UTF-8/含 NUL"; return false; }
        o.collisionProfileHint = std::move(hint);
    }
    *out = std::move(o);
    return true;
}

// ---- PoseSet（§4.6）----

void putPoseSet(Sink& s, const PoseSet& p)
{
    s.u32(p.schemaVersion);
    putObjectId(s, p.objectId);
    // entries 按 key 字典序（保留键 home/zero 与普通键同一排序域）
    {
        std::vector<const PoseSetEntry*> items;
        items.reserve(p.entries.size());
        for (const PoseSetEntry& e : p.entries) { items.push_back(&e); }
        std::sort(items.begin(), items.end(),
                  [](const PoseSetEntry* a, const PoseSetEntry* b) { return a->key < b->key; });
        s.u32(static_cast<std::uint32_t>(items.size()));
        for (const PoseSetEntry* e : items) {
            s.string(e->key);
            // jointConfiguration 与关节序一一对应（rad/m）——有序，绝不排序
            s.u32(static_cast<std::uint32_t>(e->jointConfiguration.size()));
            for (double q : e->jointConfiguration) { s.f64(q); }
            s.string(e->note);
        }
    }
}

bool getPoseSet(Reader& r, PoseSet* out, std::string* err)
{
    PoseSet p;
    if (!r.u32(&p.schemaVersion)) { *err = "schemaVersion 截断"; return false; }
    if (!getObjectId(r, &p.objectId)) { *err = "objectId 截断"; return false; }
    std::uint32_t count = 0;
    if (!r.u32(&count)) { *err = "entries 计数截断"; return false; }
    p.entries.reserve(count);
    std::string prevKey;
    for (std::uint32_t i = 0; i < count; ++i) {
        PoseSetEntry e;
        if (!r.lengthPrefixed(&e.key)) { *err = "entries[i].key 截断"; return false; }
        if (!validUtf8NoNul(e.key)) { *err = "pose key 非 UTF-8/含 NUL"; return false; }
        if (i > 0 && !(prevKey < e.key)) {
            *err = "entries 未按 key 字典序/含重复";
            return false;
        }
        prevKey = e.key;
        std::uint32_t qCount = 0;
        if (!r.u32(&qCount)) { *err = "jointConfiguration 计数截断"; return false; }
        e.jointConfiguration.resize(qCount);
        for (double& q : e.jointConfiguration) {
            if (!r.f64(&q)) { *err = "jointConfiguration 截断"; return false; }
            if (!std::isfinite(q)) { *err = "jointConfiguration 非有限"; return false; }
        }
        if (!r.lengthPrefixed(&e.note)) { *err = "note 截断"; return false; }
        if (!validUtf8NoNul(e.note)) { *err = "note 非 UTF-8/含 NUL"; return false; }
        p.entries.push_back(std::move(e));
    }
    *out = std::move(p);
    return true;
}

// ---- DrivetrainDesign（§4.7）----

void putDrivetrainDesign(Sink& s, const DrivetrainDesign& dt)
{
    s.u32(dt.schemaVersion);
    putObjectId(s, dt.objectId);
    // ratioPerJoint/frictionPerJoint/torqueLimitsPerJoint 与关节序一一对应
    // ——有序不排序（排序即篡改关节对应关系）
    s.u32(static_cast<std::uint32_t>(dt.ratioPerJoint.size()));
    for (const auto& ratio : dt.ratioPerJoint) { putSourcedValue(s, ratio); }  // 无量纲
    s.presence(dt.coupling.has_value());
    if (dt.coupling.has_value()) {
        const CouplingDesign& c = *dt.coupling;
        s.u32(c.rows);
        s.u32(c.cols);
        s.u32(static_cast<std::uint32_t>(c.c.size()));
        for (double v : c.c) { s.f64(v); }  // 行主序
        s.u32(c.jointRangeFirst);
        s.u32(c.jointRangeLast);
        s.f64(c.conditionNumber);  // 无量纲
    }
    s.u32(static_cast<std::uint32_t>(dt.frictionPerJoint.size()));
    for (const FrictionEntry& f : dt.frictionPerJoint) {
        putSourcedValue(s, f.viscous);  // N·m·s/rad 或 N·s/m
        putSourcedValue(s, f.coulomb);  // N·m 或 N
        putSourcedValue(s, f.bias);     // N·m 或 N
    }
    s.u32(static_cast<std::uint32_t>(dt.torqueLimitsPerJoint.size()));
    for (const TorqueLimitEntry& t : dt.torqueLimitsPerJoint) {
        putSourcedValue(s, t.rated);  // N·m 或 N
        putSourcedValue(s, t.peak);   // N·m 或 N
    }
    s.presence(dt.catalogBackfill.has_value());
    if (dt.catalogBackfill.has_value()) {
        s.string(dt.catalogBackfill->catalogVersion);
        s.string(dt.catalogBackfill->motorKey);
        s.string(dt.catalogBackfill->reducerKey);
        s.string(dt.catalogBackfill->mounting);
    }
}

bool getDrivetrainDesign(Reader& r, DrivetrainDesign* out, std::string* err)
{
    DrivetrainDesign dt;
    if (!r.u32(&dt.schemaVersion)) { *err = "schemaVersion 截断"; return false; }
    if (!getObjectId(r, &dt.objectId)) { *err = "objectId 截断"; return false; }
    std::uint32_t ratioCount = 0;
    if (!r.u32(&ratioCount)) { *err = "ratioPerJoint 计数截断"; return false; }
    dt.ratioPerJoint.resize(ratioCount);
    for (auto& ratio : dt.ratioPerJoint) {
        if (!getSourcedValue(r, &ratio, err)) { return false; }
    }
    bool hasCoupling = false;
    if (!r.presence(&hasCoupling)) { *err = "presence 越界"; return false; }
    if (hasCoupling) {
        CouplingDesign c;
        if (!r.u32(&c.rows) || !r.u32(&c.cols)) { *err = "coupling 维度截断"; return false; }
        std::uint32_t elemCount = 0;
        if (!r.u32(&elemCount)) { *err = "coupling 计数截断"; return false; }
        c.c.resize(elemCount);
        for (double& v : c.c) {
            if (!r.f64(&v)) { *err = "coupling 元素截断"; return false; }
            if (!std::isfinite(v)) { *err = "coupling 元素非有限"; return false; }
        }
        if (!r.u32(&c.jointRangeFirst) || !r.u32(&c.jointRangeLast)) {
            *err = "jointRange 截断";
            return false;
        }
        if (!r.f64(&c.conditionNumber)) { *err = "conditionNumber 截断"; return false; }
        if (!std::isfinite(c.conditionNumber)) { *err = "conditionNumber 非有限"; return false; }
        dt.coupling = std::move(c);
    }
    std::uint32_t frictionCount = 0;
    if (!r.u32(&frictionCount)) { *err = "frictionPerJoint 计数截断"; return false; }
    dt.frictionPerJoint.resize(frictionCount);
    for (FrictionEntry& f : dt.frictionPerJoint) {
        if (!getSourcedValue(r, &f.viscous, err)) { return false; }
        if (!getSourcedValue(r, &f.coulomb, err)) { return false; }
        if (!getSourcedValue(r, &f.bias, err)) { return false; }
    }
    std::uint32_t torqueCount = 0;
    if (!r.u32(&torqueCount)) { *err = "torqueLimitsPerJoint 计数截断"; return false; }
    dt.torqueLimitsPerJoint.resize(torqueCount);
    for (TorqueLimitEntry& t : dt.torqueLimitsPerJoint) {
        if (!getSourcedValue(r, &t.rated, err)) { return false; }
        if (!getSourcedValue(r, &t.peak, err)) { return false; }
    }
    bool hasCatalog = false;
    if (!r.presence(&hasCatalog)) { *err = "presence 越界"; return false; }
    if (hasCatalog) {
        CatalogBackfill cb;
        if (!r.lengthPrefixed(&cb.catalogVersion)) { *err = "catalogVersion 截断"; return false; }
        if (!validUtf8NoNul(cb.catalogVersion)) { *err = "catalogVersion 非 UTF-8/含 NUL"; return false; }
        if (!r.lengthPrefixed(&cb.motorKey)) { *err = "motorKey 截断"; return false; }
        if (!validUtf8NoNul(cb.motorKey)) { *err = "motorKey 非 UTF-8/含 NUL"; return false; }
        if (!r.lengthPrefixed(&cb.reducerKey)) { *err = "reducerKey 截断"; return false; }
        if (!validUtf8NoNul(cb.reducerKey)) { *err = "reducerKey 非 UTF-8/含 NUL"; return false; }
        if (!r.lengthPrefixed(&cb.mounting)) { *err = "mounting 截断"; return false; }
        if (!validUtf8NoNul(cb.mounting)) { *err = "mounting 非 UTF-8/含 NUL"; return false; }
        dt.catalogBackfill = std::move(cb);
    }
    *out = std::move(dt);
    return true;
}

// =====================================================================
// 对象路由：变体→token/schemaVersion/写入器；字节→读回器
// =====================================================================

/// 五对象的 token（variant 备择序＝§4.2 表行序；robot-design 复用 runtime
/// 字面常量——契约 acceptance 1"不另设第二常量"）。
std::string_view objectTypeTokenOf(const ObjectVariant& object)
{
    switch (object.index()) {
    case 0: return kRobotDesignObjectType;
    case 1: return kToolDefinitionObjectType;
    case 2: return kSceneObjectObjectType;
    case 3: return kNamedPoseSetObjectType;
    case 4: return kRobotDrivetrainObjectType;
    default: return {};  // valueless-by-variant（仅移动异常后可能出现）——调用方处理
    }
}

/// 五对象的 schema 主版本（单一权威＝ObjectTypes.hpp 常量/对象字段——两者
/// 同源：字段缺省即常量）。
std::uint32_t objectSchemaMajor(const ObjectVariant& object)
{
    switch (object.index()) {
    case 0: return std::get<0>(object).schemaVersion;
    case 1: return std::get<1>(object).schemaVersion;
    case 2: return std::get<2>(object).schemaVersion;
    case 3: return std::get<3>(object).schemaVersion;
    case 4: return std::get<4>(object).schemaVersion;
    default: return 0;
    }
}

}  // namespace

// =====================================================================
// IRobotDesignCodec 实现（RobotDesignCodec——无状态，方法 const）
// =====================================================================

Expected<Bytes> RobotDesignCodec::encode(const ObjectVariant& object,
                                         FormatVersion version) const
{
    // 变体态防御：valueless_by_exception（仅移动类型异常后）不可编码
    if (object.valueless_by_exception() || object.index() > 4) {
        ModelingError e;
        e.code = ModelingErrorCode::MalformedPayload;
        e.detail = "modeling/codec: ObjectVariant 处于无值态（调用方契约违约）";
        return Expected<Bytes>::err(std::move(e));
    }

    // 版本闸（NFR-DEP-04：无降级/升级产出能力——请求版本不可产出即拒绝）。
    // 两条件：①请求＝当前格式（minor 追加未落位前只认 {1,0}）；②请求主
    // 版本＝对象 schema 字段（编解码头与值模型交叉一致的编码侧检查）。
    const std::string token(objectTypeTokenOf(object));
    const std::uint32_t schemaMajor = objectSchemaMajor(object);
    if (version != kCurrentFormatVersion || version.major != schemaMajor) {
        return Expected<Bytes>::err(
            unsupportedVersion(token, version.major, kCurrentFormatVersion.major));
    }

    Sink s;
    s.raw(kMagic.data(), kMagic.size());
    s.u32(version.major);
    s.u32(version.minor);
    s.string(token);
    switch (object.index()) {
    case 0: putRobotDesign(s, std::get<0>(object)); break;
    case 1: putToolDefinition(s, std::get<1>(object)); break;
    case 2: putSceneObject(s, std::get<2>(object)); break;
    case 3: putPoseSet(s, std::get<3>(object)); break;
    case 4: putDrivetrainDesign(s, std::get<4>(object)); break;
    default: break;  // 不可达（上方已防御）
    }
    return Expected<Bytes>::ok(s.take());
}

Expected<ObjectVariant> RobotDesignCodec::decode(const Bytes& bytes,
                                                 FormatVersion supported) const
{
    // ---- 校验链①：头（magic/版本/对象 token）----
    if (bytes.size() < kMagic.size() + 4 + 4 + 4) {
        return Expected<ObjectVariant>::err(malformedAt(0, "头域截断"));
    }
    Reader r(bytes.data(), bytes.size());
    std::array<std::uint8_t, 7> magic{};
    r.bytes(magic.data(), magic.size());
    if (magic != kMagic) {
        return Expected<ObjectVariant>::err(malformedAt(0, "magic 不符（非 IRDMDLO 家族字节）"));
    }
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    if (!r.u32(&major) || !r.u32(&minor)) {
        return Expected<ObjectVariant>::err(malformedAt(r.offset(), "版本域截断"));
    }
    // 版本闸：未知主版本（无论高于还是低于支持主版本——无升级器）或超出
    // 支持次版本 → SchemaVersionUnsupported（§9.4.9/NFR-DEP-04）
    if (major != supported.major || minor > supported.minor) {
        return Expected<ObjectVariant>::err(
            unsupportedVersion("(header)", major, supported.major));
    }
    std::string token;
    if (!r.lengthPrefixed(&token)) {
        return Expected<ObjectVariant>::err(malformedAt(r.offset(), "objectTypeToken 截断"));
    }

    // ---- 校验链②③：按 token 路由的载荷解码＋canonical 校验 ----
    std::string err;
    ObjectVariant object;
    if (token == std::string(kRobotDesignObjectType)) {
        RobotDesign d;
        if (!getRobotDesign(r, &d, &err)) {
            return Expected<ObjectVariant>::err(malformedAt(r.offset(), err));
        }
        if (d.schemaVersion != major) {
            return Expected<ObjectVariant>::err(
                malformedAt(r.offset(), "robot-design schemaVersion 字段与编解码头不一致"));
        }
        // 校验链④：不变量复核（I-MDL-1～10——根对象可判面；T08 就绪校验
        // 的闭包半段不在字节可判范围）
        for (const InvariantViolation& v : checkInvariants(d)) {
            return Expected<ObjectVariant>::err(malformedInvariant(invariantIdToken(v.id), v.subject));
        }
        object = std::move(d);
    } else if (token == std::string(kToolDefinitionObjectType)) {
        ToolDefinition t;
        if (!getToolDefinition(r, &t, &err)) {
            return Expected<ObjectVariant>::err(malformedAt(r.offset(), err));
        }
        if (t.schemaVersion != major) {
            return Expected<ObjectVariant>::err(
                malformedAt(r.offset(), "tool-definition schemaVersion 与编解码头不一致"));
        }
        for (const InvariantViolation& v : checkInvariants(t)) {
            return Expected<ObjectVariant>::err(malformedInvariant(invariantIdToken(v.id), v.subject));
        }
        object = std::move(t);
    } else if (token == std::string(kSceneObjectObjectType)) {
        SceneObject o;
        if (!getSceneObject(r, &o, &err)) {
            return Expected<ObjectVariant>::err(malformedAt(r.offset(), err));
        }
        if (o.schemaVersion != major) {
            return Expected<ObjectVariant>::err(
                malformedAt(r.offset(), "scene-object schemaVersion 与编解码头不一致"));
        }
        object = std::move(o);
    } else if (token == std::string(kNamedPoseSetObjectType)) {
        PoseSet p;
        if (!getPoseSet(r, &p, &err)) {
            return Expected<ObjectVariant>::err(malformedAt(r.offset(), err));
        }
        if (p.schemaVersion != major) {
            return Expected<ObjectVariant>::err(
                malformedAt(r.offset(), "named-pose-set schemaVersion 与编解码头不一致"));
        }
        object = std::move(p);
    } else if (token == std::string(kRobotDrivetrainObjectType)) {
        DrivetrainDesign dt;
        if (!getDrivetrainDesign(r, &dt, &err)) {
            return Expected<ObjectVariant>::err(malformedAt(r.offset(), err));
        }
        if (dt.schemaVersion != major) {
            return Expected<ObjectVariant>::err(
                malformedAt(r.offset(), "robot-drivetrain schemaVersion 与编解码头不一致"));
        }
        // 传动不变量按 R1 锁定口径复核（当前程序阶段——R1 下含 coupling
        // 的字节＝数据错误，就地拒绝；MDL-21-COUPLING-STAGE-LOCKED 语义）
        for (const InvariantViolation& v : checkInvariants(dt, CouplingStage::R1Locked)) {
            return Expected<ObjectVariant>::err(malformedInvariant(invariantIdToken(v.id), v.subject));
        }
        object = std::move(dt);
    } else {
        return Expected<ObjectVariant>::err(
            malformedAt(r.offset(), "未知对象类型 token（" + token + "）——非本单元登记"));
    }

    // 尾随字节＝破损（canonical 字节自含长度语义——余量即篡改/截写证据）
    if (!r.atEnd()) {
        return Expected<ObjectVariant>::err(
            malformedAt(r.offset(), "尾随字节（解码完成后仍有剩余）"));
    }
    return Expected<ObjectVariant>::ok(std::move(object));
}

}  // namespace sdurws::ird::modeling
