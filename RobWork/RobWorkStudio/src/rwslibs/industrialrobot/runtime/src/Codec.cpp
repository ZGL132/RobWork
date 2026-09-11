/**
 * @file   Codec.cpp
 * @brief  RT-Codec 实现——确定性二进制写/读原语、SourcedValue/变换/资源/诊断
 *         的编解码、三形态入口（encode/parse/内容身份）。
 *
 * 设计依据：units/runtime.md §4.5（规则总表逐行）、§4.3.5（contentIdentity）、
 * CR-02（摘要只经 core::ContentDigester——见 Codec.hpp 排除字段声明）；
 * 任务契约 tasks/foundation/RT-T04.json（产物 2）。
 *
 * 实现纪律：
 *   - 字节序：全部多字节量手工按大端写出/读入（§4.5"大端"）——不依赖宿主
 *     字节序，跨平台跨进程逐字节一致（NFR-COR-02）；
 *   - double：IEEE754 位模式 8 字节（§4.5"数值"行）——写入口拒绝 NaN/±Inf；
 *   - 可选值：presence 字节 0/1 显式编码（nullopt ≠ 零值——NFR-COR-03）；
 *   - 枚举：以声明序整数值单字节编码，读入口校验值域（未知值拒绝——不静默
 *     映射，防编码面漂移）；
 *   - 集合：count(4)＋逐条；排序面（objectRefs/tools/scene/manifest）依赖
 *     builder 的规范化序——编码器不重排（单一规范化执行点）；
 *   - 纯函数、无 I/O、无隐藏状态（§4.5 末行——ARC-03）。
 *
 * 线程安全：无共享可变状态，可重入。
 */

#include <sdurws/ird/runtime/Codec.hpp>

#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include <sdurws/ird/core/Errors.hpp>  // CoreError（诊断记录工厂校验失败转译）
#include <sdurws/ird/core/Units.hpp>   // UnitToken（诊断比较型三要素的单位句柄）

namespace sdurws::ird::runtime::rtcodec {
namespace {

// =====================================================================
// 写原语（大端；f64 写入口拒绝非有限——§4.5"数值"行）。
// =====================================================================

class Writer {
public:
    /// 取出编码字节（移动语义——Writer 为一次性构建器）。
    std::vector<std::uint8_t> take() { return std::move(m_buf); }

    /// 追加原始字节。
    void raw(const std::uint8_t* data, std::size_t n)
    {
        m_buf.insert(m_buf.end(), data, data + n);
    }

    /// 16 字节身份（Id128 强类型的原始字节）。
    void raw16(const std::array<std::uint8_t, 16>& a) { raw(a.data(), a.size()); }

    /// 32 字节摘要（Digest256 的原始字节）。
    void raw32(const core::Digest256& a) { raw(a.data(), a.size()); }

    /// 单字节。
    void u8(std::uint8_t v) { m_buf.push_back(v); }

    /// 16 位无符号，大端（版本号/域标志）。
    void u16(std::uint16_t v)
    {
        m_buf.push_back(static_cast<std::uint8_t>(v >> 8));
        m_buf.push_back(static_cast<std::uint8_t>(v));
    }

    /// 32 位无符号，大端（计数/长度/契约版本）。
    void u32(std::uint32_t v)
    {
        for (int i = 3; i >= 0; --i) {
            m_buf.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
        }
    }

    /// 64 位无符号，大端（revisionSeq——全字段域专用）。
    void u64(std::uint64_t v)
    {
        for (int i = 7; i >= 0; --i) {
            m_buf.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu));
        }
    }

    /**
     * @brief IEEE754 双精度位模式，大端（§4.5"数值"行——round-trip 精确）。
     * @throws RuntimeError InputInvalid 非有限值（NaN/±Inf 编码入口拒绝）
     */
    void f64(double v)
    {
        if (!std::isfinite(v)) {
            throw RuntimeError{RuntimeErrorCode::InputInvalid,
                               "codec/encode：遇到非有限 double（NaN/±Inf 编码入口拒绝——§4.5）"};
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        u64(bits);
    }

    /// 长度前缀 UTF-8 字符串（u32 长度＋原始字节）。
    void str(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    /// presence 字节（0＝缺省、1＝存在——nullopt ≠ 零值，NFR-COR-03）。
    void presence(bool has) { u8(has ? 1u : 0u); }

private:
    std::vector<std::uint8_t> m_buf;
};

// =====================================================================
// 读原语（全部越界检查；失败置错误消息并记字节偏移——parse 定位用）。
// =====================================================================

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t n) noexcept : m_data(data), m_size(n) {}

    /// 最近一次失败的错误消息（空＝无失败）。
    const std::string& error() const noexcept { return m_err; }
    /// 当前字节偏移（错误定位）。
    std::size_t offset() const noexcept { return m_off; }
    /// 是否已进入失败态（失败后不再前进）。
    bool bad() const noexcept { return !m_err.empty(); }

    bool raw(std::uint8_t* out, std::size_t n)
    {
        if (m_err.empty() && m_off + n > m_size) {
            return fail("字节越界（需要 " + std::to_string(n) + " 字节）");
        }
        if (!m_err.empty()) { return false; }
        std::memcpy(out, m_data + m_off, n);
        m_off += n;
        return true;
    }

    bool raw16(std::array<std::uint8_t, 16>& out) { return raw(out.data(), out.size()); }
    bool raw32(core::Digest256& out) { return raw(out.data(), out.size()); }

    bool u8(std::uint8_t& v)
    {
        return raw(&v, 1);
    }

    bool u16(std::uint16_t& v)
    {
        std::uint8_t b[2] = {0, 0};
        if (!raw(b, 2)) { return false; }
        v = static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[0]) << 8) | b[1]);
        return true;
    }

    bool u32(std::uint32_t& v)
    {
        std::uint8_t b[4] = {0, 0, 0, 0};
        if (!raw(b, 4)) { return false; }
        v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | b[i];
        }
        return true;
    }

    bool u64(std::uint64_t& v)
    {
        std::uint8_t b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        if (!raw(b, 8)) { return false; }
        v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | b[i];
        }
        return true;
    }

    bool f64(double& v)
    {
        std::uint64_t bits = 0;
        if (!u64(bits)) { return false; }
        std::memcpy(&v, &bits, sizeof(v));
        // 位模式往返不产生非有限（写入口已拒绝）——防御性复核仍保留：
        // 手工构造的字节流可能携带 NaN 位型。
        if (!std::isfinite(v)) {
            return fail("非有限 double 位型（NaN/±Inf）");
        }
        return true;
    }

    bool str(std::string& s)
    {
        std::uint32_t n = 0;
        if (!u32(n)) { return false; }
        if (m_off + n > m_size) { return fail("字符串长度越界"); }
        s.assign(reinterpret_cast<const char*>(m_data + m_off), n);
        m_off += n;
        return true;
    }

    bool presence(bool& has)
    {
        std::uint8_t b = 0;
        if (!u8(b)) { return false; }
        if (b > 1) { return fail("非法 presence 字节（须 0/1）"); }
        has = (b == 1);
        return true;
    }

    /// 布尔字节读取（0/1；越界值＝非法编码——显式报错定位）。
    bool flag(bool& b)
    {
        std::uint8_t v = 0;
        if (!u8(v)) { return false; }
        if (v > 1) { return fail("非法布尔字节（须 0/1）"); }
        b = (v == 1);
        return true;
    }

    /// 枚举字节读取＋值域校验（未知值拒绝——防编码面漂移）。
    template <typename E>
    bool enumByte(E& v, std::uint8_t maxValid)
    {
        std::uint8_t b = 0;
        if (!u8(b)) { return false; }
        if (b > maxValid) { return fail("枚举字节越界（值 " + std::to_string(b) + "）"); }
        v = static_cast<E>(b);
        return true;
    }

private:
    bool fail(std::string what)
    {
        if (m_err.empty()) {  // 保留首个失败（定位最深处）
            m_err = "字节偏移 " + std::to_string(m_off) + "：" + std::move(what);
        }
        return false;
    }

    const std::uint8_t* m_data;
    std::size_t m_size;
    std::size_t m_off = 0;
    std::string m_err;
};

// =====================================================================
// 复合值编码（声明序——与 CanonicalModel.hpp 成员声明序一致）。
// =====================================================================

/// ValueProvenance（Provided 态 SourcedValue 的来源记录——来源标记入身份，
/// §4.3.6）。
void writeProvenance(Writer& w, const core::ValueProvenance& p)
{
    w.u8(static_cast<std::uint8_t>(p.kind));
    w.presence(p.sourceObject.has_value());
    if (p.sourceObject) { w.raw16(p.sourceObject->bytes); }
    w.presence(p.sourceVersion.has_value());
    if (p.sourceVersion) { w.raw32(p.sourceVersion->bytes); }
    w.presence(p.methodTag.has_value());
    if (p.methodTag) { w.str(*p.methodTag); }
}

/// SourcedValue<double>：状态字节＋（Provided：值＋来源｜Invalid：原串）。
void writeSourcedDouble(Writer& w, const core::SourcedValue<double>& v)
{
    w.u8(static_cast<std::uint8_t>(v.state()));
    switch (v.state()) {
    case core::FieldState::Provided:
        w.f64(*v.tryValue());
        writeProvenance(w, v.provenance());
        break;
    case core::FieldState::Invalid:
        w.str(v.invalidRawInput());  // 非法态保留原串（NFR-COR-03）
        break;
    default:  // NotProvided/NotApplicable——无载荷
        break;
    }
}

/// SourcedValue<Vector3D>（质心——单位 m，系随字段语境）。Invalid 态保留
/// 原串（§4.4 四态保留到 CanonicalModel——全字段往返不失真）。
void writeSourcedVec3(Writer& w, const core::SourcedValue<rw::math::Vector3D<double>>& v)
{
    w.u8(static_cast<std::uint8_t>(v.state()));
    switch (v.state()) {
    case core::FieldState::Provided: {
        const auto x = *v.tryValue();  // 立即拷贝（防临时 optional 悬垂引用）
        for (int i = 0; i < 3; ++i) { w.f64(x(i)); }
        writeProvenance(w, v.provenance());
        break;
    }
    case core::FieldState::Invalid:
        w.str(v.invalidRawInput());
        break;
    default:
        break;
    }
}

/// SourcedValue<InertiaMatrix>（9 分量行主序——与 §4.5 旋转矩阵同序）。
void writeSourcedInertia(Writer& w, const core::SourcedValue<rw::math::InertiaMatrix<double>>& v)
{
    w.u8(static_cast<std::uint8_t>(v.state()));
    switch (v.state()) {
    case core::FieldState::Provided: {
        const auto m = *v.tryValue();  // 立即拷贝（防临时 optional 悬垂引用）
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) { w.f64(m(i, j)); }
        }
        writeProvenance(w, v.provenance());
        break;
    }
    case core::FieldState::Invalid:
        w.str(v.invalidRawInput());
        break;
    default:
        break;
    }
}

/// SourcedValue<InstallationPresetToken>（全字段域专用——身份域排除）。
void writeSourcedPreset(Writer& w, const core::SourcedValue<InstallationPresetToken>& v)
{
    w.u8(static_cast<std::uint8_t>(v.state()));
    switch (v.state()) {
    case core::FieldState::Provided:
        w.u8(static_cast<std::uint8_t>(*v.tryValue()));
        writeProvenance(w, v.provenance());
        break;
    case core::FieldState::Invalid:
        w.str(v.invalidRawInput());
        break;
    default:
        break;
    }
}

/// 变换＝旋转 9 分量行主序＋平移 3 分量（§4.5"旋转矩阵按行主序编码"——
/// 表示无关：EAA/欧拉只在编辑侧，规范侧只存矩阵）。
void writeTransform(Writer& w, const rw::math::Transform3D<double>& t)
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) { w.f64(t.R()(i, j)); }
    }
    for (int i = 0; i < 3; ++i) { w.f64(t.P()(i)); }
}

void writeVec3(Writer& w, const rw::math::Vector3D<double>& v)
{
    for (int i = 0; i < 3; ++i) { w.f64(v(i)); }
}

/// ResourceRef（§8.6——路径 hint 只是提示、仍全字段编码供往返）。
void writeResourceRef(Writer& w, const ResourceRef& r)
{
    w.raw16(r.resourceId.bytes);
    w.raw32(r.contentDigest);
    w.presence(r.sourcePathHint.has_value());
    if (r.sourcePathHint) { w.str(*r.sourcePathHint); }
    w.u8(static_cast<std::uint8_t>(r.state));
    w.u32(r.accessVersion);
}

/// DiagnosticRecord（§4.3.5 诊断块——全字段域专用）。
void writeDiagnostic(Writer& w, const core::DiagnosticRecord& d)
{
    w.str(d.code);
    w.presence(d.subject.has_value());
    if (d.subject) { w.raw16(d.subject->bytes); }
    w.presence(d.localName.has_value());
    if (d.localName) { w.str(*d.localName); }
    w.presence(d.runtimeName.has_value());
    if (d.runtimeName) { w.str(*d.runtimeName); }
    w.str(d.context);
    w.str(d.cause);
    w.str(d.recommendedAction);
    w.presence(d.comparison.has_value());
    if (d.comparison) {
        // 比较型三要素两侧（UX-03）——单位以冻结 token 原文编码。
        for (const core::ComparativeValue* side : {&d.comparison->actual, &d.comparison->expected}) {
            writeSourcedDouble(w, side->quantity);
            if (!side->unit.isValid()) {
                // 无效单位句柄（未注册）没有可编码的稳定 token——拒绝而非
                // 写占位串（防 unitSymbolAt 越界＋防往返失真，NFR-COR-03）。
                throw RuntimeError{RuntimeErrorCode::InputInvalid,
                                   "codec/encode：诊断比较值携带未注册单位 token——编码入口拒绝"};
            }
            w.str(std::string{side->unit.symbol()});
        }
    }
}

/// RuntimeCapability（§9.6——全字段域专用；派生投影冗余存储）。
void writeCapabilities(Writer& w, const RuntimeCapability& c)
{
    w.u8(c.hasWorkCell ? 1 : 0);
    w.u8(c.hasDynamicWorkCell ? 1 : 0);
    w.u8(c.hasFullMassInertia ? 1 : 0);
    w.u8(c.hasJointVelocityLimits ? 1 : 0);
    w.u8(c.hasCollisionGeometry ? 1 : 0);
    w.u8(c.hasTools ? 1 : 0);
    w.u8(c.hasScene ? 1 : 0);
    w.u8(c.hasFrictionModel ? 1 : 0);
    w.u8(c.hasCouplingMatrix ? 1 : 0);
    w.u32(static_cast<std::uint32_t>(c.jointTypesPresent.size()));
    for (const JointType t : c.jointTypesPresent) { w.u8(static_cast<std::uint8_t>(t)); }
    w.u8(c.hasBidirectionalNameMap ? 1 : 0);
}

// =====================================================================
// 复合值解码（与编码一一对应；失败经 Reader 记错误并返回 false）。
// =====================================================================

bool readProvenance(Reader& r, core::ValueProvenance& p)
{
    if (!r.enumByte(p.kind, static_cast<std::uint8_t>(core::ProvenanceKind::DerivedReadOnly))) {
        return false;
    }
    bool has = false;
    if (!r.presence(has)) { return false; }
    if (has) {
        core::ObjectId id;
        if (!r.raw16(id.bytes)) { return false; }
        p.sourceObject = id;
    }
    if (!r.presence(has)) { return false; }
    if (has) {
        core::ContentVersion cv;
        if (!r.raw32(cv.bytes)) { return false; }
        p.sourceVersion = cv;
    }
    if (!r.presence(has)) { return false; }
    if (has) {
        std::string tag;
        if (!r.str(tag)) { return false; }
        p.methodTag = std::move(tag);
    }
    return true;
}

bool readSourcedDouble(Reader& r, core::SourcedValue<double>& v)
{
    core::FieldState state = core::FieldState::NotProvided;
    if (!r.enumByte(state, static_cast<std::uint8_t>(core::FieldState::Invalid))) { return false; }
    switch (state) {
    case core::FieldState::Provided: {
        double x = 0.0;
        if (!r.f64(x)) { return false; }
        core::ValueProvenance p;
        if (!readProvenance(r, p)) { return false; }
        v = core::SourcedValue<double>::provided(x, std::move(p));
        return true;
    }
    case core::FieldState::Invalid: {
        std::string rawIn;
        if (!r.str(rawIn)) { return false; }
        if (rawIn.empty()) {
            // 非法态的存在意义是保留原串——空原串进不了 core 工厂
            // （会抛 CoreError），在读取侧前置拒绝。
            return false;
        }
        v = core::SourcedValue<double>::invalid(std::move(rawIn));
        return true;
    }
    case core::FieldState::NotProvided:
        v = core::SourcedValue<double>::notProvided();
        return true;
    case core::FieldState::NotApplicable:
        v = core::SourcedValue<double>::notApplicable();
        return true;
    }
    return false;  // 不可达（enumByte 已校验值域）
}

bool readSourcedVec3(Reader& r, core::SourcedValue<rw::math::Vector3D<double>>& v)
{
    core::FieldState state = core::FieldState::NotProvided;
    if (!r.enumByte(state, static_cast<std::uint8_t>(core::FieldState::Invalid))) { return false; }
    switch (state) {
    case core::FieldState::Provided: {
        double x[3] = {0.0, 0.0, 0.0};
        for (double& xi : x) {
            if (!r.f64(xi)) { return false; }
        }
        core::ValueProvenance p;
        if (!readProvenance(r, p)) { return false; }
        v = core::SourcedValue<rw::math::Vector3D<double>>::provided(
            rw::math::Vector3D<double>(x[0], x[1], x[2]), std::move(p));
        return true;
    }
    case core::FieldState::Invalid: {
        std::string rawIn;
        if (!r.str(rawIn) || rawIn.empty()) { return false; }
        v = core::SourcedValue<rw::math::Vector3D<double>>::invalid(std::move(rawIn));
        return true;
    }
    case core::FieldState::NotProvided:
        v = core::SourcedValue<rw::math::Vector3D<double>>::notProvided();
        return true;
    case core::FieldState::NotApplicable:
        v = core::SourcedValue<rw::math::Vector3D<double>>::notApplicable();
        return true;
    }
    return false;  // 不可达（enumByte 已校验值域）
}

bool readSourcedInertia(Reader& r, core::SourcedValue<rw::math::InertiaMatrix<double>>& v)
{
    core::FieldState state = core::FieldState::NotProvided;
    if (!r.enumByte(state, static_cast<std::uint8_t>(core::FieldState::Invalid))) { return false; }
    switch (state) {
    case core::FieldState::Provided: {
        double a[9] = {};
        for (double& e : a) {
            if (!r.f64(e)) { return false; }
        }
        core::ValueProvenance p;
        if (!readProvenance(r, p)) { return false; }
        // InertiaMatrix 无 9 参构造——经逐元素赋值（行主序读入）。
        rw::math::InertiaMatrix<double> m;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) { m(i, j) = a[i * 3 + j]; }
        }
        v = core::SourcedValue<rw::math::InertiaMatrix<double>>::provided(m, std::move(p));
        return true;
    }
    case core::FieldState::Invalid: {
        std::string rawIn;
        if (!r.str(rawIn) || rawIn.empty()) { return false; }
        v = core::SourcedValue<rw::math::InertiaMatrix<double>>::invalid(std::move(rawIn));
        return true;
    }
    case core::FieldState::NotProvided:
        v = core::SourcedValue<rw::math::InertiaMatrix<double>>::notProvided();
        return true;
    case core::FieldState::NotApplicable:
        v = core::SourcedValue<rw::math::InertiaMatrix<double>>::notApplicable();
        return true;
    }
    return false;  // 不可达
}

bool readSourcedPreset(Reader& r, core::SourcedValue<InstallationPresetToken>& v)
{
    core::FieldState state = core::FieldState::NotProvided;
    if (!r.enumByte(state, static_cast<std::uint8_t>(core::FieldState::Invalid))) { return false; }
    switch (state) {
    case core::FieldState::Provided: {
        InstallationPresetToken token = InstallationPresetToken::Ground;
        if (!r.enumByte(token, static_cast<std::uint8_t>(InstallationPresetToken::Custom))) {
            return false;
        }
        core::ValueProvenance p;
        if (!readProvenance(r, p)) { return false; }
        v = core::SourcedValue<InstallationPresetToken>::provided(token, std::move(p));
        return true;
    }
    case core::FieldState::Invalid: {
        std::string rawIn;
        if (!r.str(rawIn) || rawIn.empty()) { return false; }
        v = core::SourcedValue<InstallationPresetToken>::invalid(std::move(rawIn));
        return true;
    }
    case core::FieldState::NotProvided:
        v = core::SourcedValue<InstallationPresetToken>::notProvided();
        return true;
    case core::FieldState::NotApplicable:
        v = core::SourcedValue<InstallationPresetToken>::notApplicable();
        return true;
    }
    return false;  // 不可达
}

bool readTransform(Reader& r, rw::math::Transform3D<double>& t)
{
    double rr[9] = {};
    double pp[3] = {};
    for (double& e : rr) {
        if (!r.f64(e)) { return false; }
    }
    for (double& e : pp) {
        if (!r.f64(e)) { return false; }
    }
    t = rw::math::Transform3D<double>(
        rw::math::Vector3D<double>(pp[0], pp[1], pp[2]),
        rw::math::Rotation3D<double>(rr[0], rr[1], rr[2], rr[3], rr[4], rr[5], rr[6], rr[7],
                                     rr[8]));
    return true;
}

bool readVec3(Reader& r, rw::math::Vector3D<double>& v)
{
    double x[3] = {};
    for (double& e : x) {
        if (!r.f64(e)) { return false; }
    }
    v = rw::math::Vector3D<double>(x[0], x[1], x[2]);
    return true;
}

bool readResourceRef(Reader& r, ResourceRef& ref)
{
    if (!r.raw16(ref.resourceId.bytes)) { return false; }
    if (!r.raw32(ref.contentDigest)) { return false; }
    bool has = false;
    if (!r.presence(has)) { return false; }
    if (has) {
        std::string hint;
        if (!r.str(hint)) { return false; }
        ref.sourcePathHint = std::move(hint);
    }
    if (!r.enumByte(ref.state, static_cast<std::uint8_t>(ResourceState::Solidified))) {
        return false;
    }
    return r.u32(ref.accessVersion);
}

bool readDiagnostic(Reader& r, core::DiagnosticRecord& d)
{
    std::string code;
    if (!r.str(code)) { return false; }
    bool has = false;
    if (!r.presence(has)) { return false; }
    std::optional<core::ObjectId> subject;
    if (has) {
        core::ObjectId id;
        if (!r.raw16(id.bytes)) { return false; }
        subject = id;
    }
    if (!r.presence(has)) { return false; }
    std::optional<std::string> localName;
    if (has) {
        std::string s;
        if (!r.str(s)) { return false; }
        localName = std::move(s);
    }
    if (!r.presence(has)) { return false; }
    std::optional<std::string> runtimeName;
    if (has) {
        std::string s;
        if (!r.str(s)) { return false; }
        runtimeName = std::move(s);
    }
    std::string context;
    std::string cause;
    std::string action;
    if (!r.str(context) || !r.str(cause) || !r.str(action)) { return false; }
    if (!r.presence(has)) { return false; }
    std::optional<core::ComparativeFields> comparison;
    if (has) {
        core::ComparativeFields cmp;
        for (core::ComparativeValue* side : {&cmp.actual, &cmp.expected}) {
            if (!readSourcedDouble(r, side->quantity)) { return false; }
            std::string unitSym;
            if (!r.str(unitSym)) { return false; }
            const auto unit = core::UnitToken::find(unitSym);
            if (!unit.has_value()) { return false; }  // 未注册 token＝非法编码
            side->unit = *unit;
        }
        comparison = std::move(cmp);
    }
    // 经 core 工厂重建（C-3 校验——code 句法/必填串非法的字节在此被拒；
    // CoreError 由 parse 外层转译为 err，不外逃）。
    d = core::DiagnosticRecord::make(std::move(code), std::move(subject), std::move(localName),
                                     std::move(runtimeName), std::move(context), std::move(cause),
                                     std::move(action), std::move(comparison));
    return true;
}

bool readCapabilities(Reader& r, RuntimeCapability& c)
{
    // 读序＝写序（writeCapabilities）：9 个能力位 → jointTypesPresent
    // （count＋逐字节）→ hasBidirectionalNameMap。读序错位会使后续全部
    // 字段错读——往返用例（RT-ID-1/parse(encode)==x）钉住该序。
    if (!r.flag(c.hasWorkCell) || !r.flag(c.hasDynamicWorkCell)
        || !r.flag(c.hasFullMassInertia) || !r.flag(c.hasJointVelocityLimits)
        || !r.flag(c.hasCollisionGeometry) || !r.flag(c.hasTools) || !r.flag(c.hasScene)
        || !r.flag(c.hasFrictionModel) || !r.flag(c.hasCouplingMatrix)) {
        return false;
    }
    std::uint32_t n = 0;
    if (!r.u32(n)) { return false; }
    c.jointTypesPresent.resize(n);
    for (JointType& t : c.jointTypesPresent) {
        if (!r.enumByte(t, static_cast<std::uint8_t>(JointType::Fixed))) { return false; }
    }
    return r.flag(c.hasBidirectionalNameMap);
}

/// 能力块等值（RuntimeCapability 无 operator==——逐字段）。
bool capabilityEqual(const RuntimeCapability& a, const RuntimeCapability& b) noexcept
{
    return a.hasWorkCell == b.hasWorkCell
           && a.hasDynamicWorkCell == b.hasDynamicWorkCell
           && a.hasFullMassInertia == b.hasFullMassInertia
           && a.hasJointVelocityLimits == b.hasJointVelocityLimits
           && a.hasCollisionGeometry == b.hasCollisionGeometry
           && a.hasTools == b.hasTools && a.hasScene == b.hasScene
           && a.hasFrictionModel == b.hasFrictionModel
           && a.hasCouplingMatrix == b.hasCouplingMatrix
           && a.hasBidirectionalNameMap == b.hasBidirectionalNameMap
           && a.jointTypesPresent == b.jointTypesPresent;
}

// =====================================================================
// 编码主体（domain 决定身份域排除项是否落码——CR-02 声明的执行点）。
// =====================================================================

std::vector<std::uint8_t> encodeImpl(const CanonicalModel& model, std::uint16_t domain)
{
    const bool identityDomain = (domain == kDomainIdentity);
    Writer w;
    w.raw(kMagic.data(), kMagic.size());
    w.u16(kVersionMajor);
    w.u16(kVersionMinor);
    w.u16(domain);

    const CanonicalModelHeader& h = model.header();

    // ---- 身份与来源块（§4.3.1；revisionSeq 仅全字段域）----
    w.raw16(h.project.bytes);
    w.raw16(h.branch.bytes);
    w.raw16(h.revision.bytes);
    if (!identityDomain) { w.u64(h.revisionSeq); }
    w.u32(static_cast<std::uint32_t>(h.objectRefs.size()));
    for (const ObjectRefEntry& e : h.objectRefs) {
        w.raw16(e.objectId.bytes);
        w.raw32(e.contentVersion.bytes);
        w.str(e.objectTypeToken);
        w.raw32(e.digest);
    }
    w.u32(h.descriptionContractVersion);
    w.u32(h.compilerContractVersion);
    w.raw32(h.builtFrom);

    // ---- 世界与基座块（§4.3.2；installPreset 仅全字段域）----
    writeTransform(w, model.world().T_world_base);
    if (!identityDomain) { writeSourcedPreset(w, model.world().installPreset); }
    writeVec3(w, model.world().gravityWorld);

    // ---- 机器人链块（§4.3.3；关节/连杆保持链序）----
    const RobotChain& chain = model.chain();
    w.raw16(chain.robotObjectId.bytes);
    w.str(chain.robotLocalName);
    w.str(chain.deviceName);
    w.u32(static_cast<std::uint32_t>(chain.joints.size()));
    for (const CanonicalJoint& j : chain.joints) {
        w.raw16(j.objectId.bytes);
        w.str(j.localName);
        w.u8(static_cast<std::uint8_t>(j.type));
        writeVec3(w, j.axis);
        writeTransform(w, j.origin);
        w.f64(j.zeroOffset);
        w.presence(j.bounds.has_value());
        if (j.bounds) {
            w.f64(j.bounds->lower);
            w.f64(j.bounds->upper);
        }
        w.presence(j.workingRange.has_value());
        if (j.workingRange) {
            w.f64(j.workingRange->lower);
            w.f64(j.workingRange->upper);
        }
        writeSourcedDouble(w, j.maxVelocity);
        writeSourcedDouble(w, j.maxAcceleration);
        writeSourcedDouble(w, j.friction.viscous);
        writeSourcedDouble(w, j.friction.coulomb);
        writeSourcedDouble(w, j.friction.bias);
    }
    w.u32(static_cast<std::uint32_t>(chain.links.size()));
    for (const CanonicalLink& l : chain.links) {
        w.raw16(l.objectId.bytes);
        w.str(l.localName);
        w.presence(l.visual.has_value());
        if (l.visual) { writeResourceRef(w, *l.visual); }
        w.presence(l.collision.has_value());
        if (l.collision) { writeResourceRef(w, *l.collision); }
        writeSourcedDouble(w, l.mass);
        writeSourcedVec3(w, l.centerOfMass);
        writeSourcedInertia(w, l.inertia);
    }

    // ---- 工具/默认 TCP/场景（§4.3.4）----
    const auto& tools = model.tools();
    w.u32(static_cast<std::uint32_t>(tools.size()));
    for (const CanonicalTool& t : tools) {
        w.raw16(t.objectId.bytes);
        w.str(t.localName);
        w.presence(t.geometry.has_value());
        if (t.geometry) { writeResourceRef(w, *t.geometry); }
        writeSourcedDouble(w, t.mass);
        writeSourcedVec3(w, t.centerOfMass);
        writeSourcedInertia(w, t.inertia);
        writeTransform(w, t.tcpOffset);
    }
    w.presence(model.defaultTcpIndex().has_value());
    if (model.defaultTcpIndex()) { w.u32(*model.defaultTcpIndex()); }
    const auto& scene = model.scene();
    w.u32(static_cast<std::uint32_t>(scene.size()));
    for (const CanonicalSceneObject& s : scene) {
        w.raw16(s.objectId.bytes);
        w.str(s.localName);
        writeTransform(w, s.worldPose);
        writeResourceRef(w, s.geometry);
    }

    // ---- 传动块（§4.3.4）----
    const CanonicalDrivetrain& dt = model.drivetrain();
    w.u32(static_cast<std::uint32_t>(dt.ratioPerJoint.size()));
    for (const core::SourcedValue<double>& ratio : dt.ratioPerJoint) {
        writeSourcedDouble(w, ratio);
    }
    w.presence(dt.coupling.has_value());
    if (dt.coupling) {
        const CouplingMatrix& c = *dt.coupling;
        w.u32(c.rows);
        w.u32(c.cols);
        w.u32(static_cast<std::uint32_t>(c.c.size()));
        for (const double e : c.c) { w.f64(e); }
        w.u32(c.jointRange.firstIndex);
        w.u32(c.jointRange.count);
        w.presence(c.conditionNumber.has_value());
        if (c.conditionNumber) { w.f64(*c.conditionNumber); }
    }

    // ---- 资源清单（§4.3.5）----
    const auto& manifest = model.resourceManifest();
    w.u32(static_cast<std::uint32_t>(manifest.size()));
    for (const ResourceRef& r : manifest) { writeResourceRef(w, r); }

    // ---- 身份域排除块（CR-02 清单 3/4/5——仅全字段域）----
    if (!identityDomain) {
        const auto& diags = model.diagnostics();
        w.u32(static_cast<std::uint32_t>(diags.size()));
        for (const core::DiagnosticRecord& d : diags) { writeDiagnostic(w, d); }
        writeCapabilities(w, model.capabilities());
        w.raw32(model.contentIdentity().bytes);
    }
    return w.take();
}

}  // namespace

// =====================================================================
// 三形态入口（公共契约——Codec.hpp 注释为准）。
// =====================================================================

std::vector<std::uint8_t> encode(const CanonicalModel& model)
{
    return encodeImpl(model, kDomainFull);
}

std::vector<std::uint8_t> encodeIdentityDomain(const CanonicalModel& model)
{
    return encodeImpl(model, kDomainIdentity);
}

core::ContentIdentity computeContentIdentity(const CanonicalModel& model)
{
    // CR-02 摘要边界：只调 core::ContentDigester——runtime 无第二套 SHA-256。
    const std::vector<std::uint8_t> bytes = encodeIdentityDomain(model);
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity identity;
    identity.bytes = digester.finalize();
    return identity;
}

Expected<CanonicalModel, RuntimeError> parse(const std::vector<std::uint8_t>& bytes)
{
    try {
        // ---- ①头校验（magic/域/版本——不符即拒绝，不尽力猜测）----
        Reader r(bytes.data(), bytes.size());
        std::array<std::uint8_t, 7> magic{};
        if (!r.raw(magic.data(), magic.size()) || magic != kMagic) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：magic 不符（偏移 0）——非 IRDCANO 编码"});
        }
        std::uint16_t major = 0;
        std::uint16_t minor = 0;
        std::uint16_t domain = 0;
        if (!r.u16(major) || !r.u16(minor) || !r.u16(domain)) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid, "codec/parse：" + r.error()});
        }
        if (major != kVersionMajor || minor != kVersionMinor) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：编码版本 " + std::to_string(major) + "." + std::to_string(minor)
                    + " 不受支持（本编码器＝1.0）——编码升版为破坏性变更，拒绝而非猜测"});
        }
        if (domain != kDomainFull) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：身份域编码不是往返载体（仅接受全字段编码）"});
        }

        // ---- ②逐字段解码到 builder 输入（越界/非法值在 Reader 记错误）----
        CanonicalModelHeader h;
        WorldPlacement world;
        RobotChain chain;
        std::vector<CanonicalTool> tools;
        std::optional<std::uint32_t> tcp;
        std::vector<CanonicalSceneObject> scene;
        CanonicalDrivetrain drivetrain;
        std::vector<ResourceRef> manifest;
        std::vector<core::DiagnosticRecord> diags;
        RuntimeCapability caps;
        core::ContentIdentity storedIdentity;

        auto failed = [&r]() {
            return Expected<CanonicalModel, RuntimeError>::err(
                RuntimeError{RuntimeErrorCode::InputInvalid, "codec/parse：" + r.error()});
        };

        if (!r.raw16(h.project.bytes) || !r.raw16(h.branch.bytes) || !r.raw16(h.revision.bytes)) {
            return failed();
        }
        if (!r.u64(h.revisionSeq)) { return failed(); }
        std::uint32_t n = 0;
        if (!r.u32(n)) { return failed(); }
        h.objectRefs.resize(n);
        for (ObjectRefEntry& e : h.objectRefs) {
            if (!r.raw16(e.objectId.bytes) || !r.raw32(e.contentVersion.bytes) || !r.str(e.objectTypeToken)
                || !r.raw32(e.digest)) {
                return failed();
            }
        }
        if (!r.u32(h.descriptionContractVersion) || !r.u32(h.compilerContractVersion)
            || !r.raw32(h.builtFrom)) {
            return failed();
        }

        if (!readTransform(r, world.T_world_base)) { return failed(); }
        if (!readSourcedPreset(r, world.installPreset)) { return failed(); }
        if (!readVec3(r, world.gravityWorld)) { return failed(); }

        if (!r.raw16(chain.robotObjectId.bytes) || !r.str(chain.robotLocalName)
            || !r.str(chain.deviceName)) {
            return failed();
        }
        if (!r.u32(n)) { return failed(); }
        chain.joints.resize(n);
        for (CanonicalJoint& j : chain.joints) {
            if (!r.raw16(j.objectId.bytes) || !r.str(j.localName)) { return failed(); }
            if (!r.enumByte(j.type, static_cast<std::uint8_t>(JointType::Fixed))) {
                return failed();
            }
            if (!readVec3(r, j.axis) || !readTransform(r, j.origin) || !r.f64(j.zeroOffset)) {
                return failed();
            }
            bool has = false;
            if (!r.presence(has)) { return failed(); }
            if (has) {
                JointBounds b;
                if (!r.f64(b.lower) || !r.f64(b.upper)) { return failed(); }
                j.bounds = b;
            }
            if (!r.presence(has)) { return failed(); }
            if (has) {
                WorkingRange wr;
                if (!r.f64(wr.lower) || !r.f64(wr.upper)) { return failed(); }
                j.workingRange = wr;
            }
            if (!readSourcedDouble(r, j.maxVelocity) || !readSourcedDouble(r, j.maxAcceleration)
                || !readSourcedDouble(r, j.friction.viscous)
                || !readSourcedDouble(r, j.friction.coulomb)
                || !readSourcedDouble(r, j.friction.bias)) {
                return failed();
            }
        }
        if (!r.u32(n)) { return failed(); }
        chain.links.resize(n);
        for (CanonicalLink& l : chain.links) {
            if (!r.raw16(l.objectId.bytes) || !r.str(l.localName)) { return failed(); }
            bool has = false;
            if (!r.presence(has)) { return failed(); }
            if (has) {
                ResourceRef ref;
                if (!readResourceRef(r, ref)) { return failed(); }
                l.visual = std::move(ref);
            }
            if (!r.presence(has)) { return failed(); }
            if (has) {
                ResourceRef ref;
                if (!readResourceRef(r, ref)) { return failed(); }
                l.collision = std::move(ref);
            }
            if (!readSourcedDouble(r, l.mass) || !readSourcedVec3(r, l.centerOfMass)
                || !readSourcedInertia(r, l.inertia)) {
                return failed();
            }
        }

        if (!r.u32(n)) { return failed(); }
        tools.resize(n);
        for (CanonicalTool& t : tools) {
            if (!r.raw16(t.objectId.bytes) || !r.str(t.localName)) { return failed(); }
            bool has = false;
            if (!r.presence(has)) { return failed(); }
            if (has) {
                ResourceRef ref;
                if (!readResourceRef(r, ref)) { return failed(); }
                t.geometry = std::move(ref);
            }
            if (!readSourcedDouble(r, t.mass) || !readSourcedVec3(r, t.centerOfMass)
                || !readSourcedInertia(r, t.inertia) || !readTransform(r, t.tcpOffset)) {
                return failed();
            }
        }
        bool hasTcp = false;
        if (!r.presence(hasTcp)) { return failed(); }
        if (hasTcp) {
            std::uint32_t idx = 0;
            if (!r.u32(idx)) { return failed(); }
            tcp = idx;
        }
        if (!r.u32(n)) { return failed(); }
        scene.resize(n);
        for (CanonicalSceneObject& s : scene) {
            if (!r.raw16(s.objectId.bytes) || !r.str(s.localName)
                || !readTransform(r, s.worldPose) || !readResourceRef(r, s.geometry)) {
                return failed();
            }
        }

        if (!r.u32(n)) { return failed(); }
        drivetrain.ratioPerJoint.resize(n);
        for (core::SourcedValue<double>& ratio : drivetrain.ratioPerJoint) {
            if (!readSourcedDouble(r, ratio)) { return failed(); }
        }
        bool has = false;
        if (!r.presence(has)) { return failed(); }
        if (has) {
            CouplingMatrix c;
            std::uint32_t nElem = 0;
            if (!r.u32(c.rows) || !r.u32(c.cols) || !r.u32(nElem)) { return failed(); }
            c.c.resize(nElem);
            for (double& e : c.c) {
                if (!r.f64(e)) { return failed(); }
            }
            if (!r.u32(c.jointRange.firstIndex) || !r.u32(c.jointRange.count)) {
                return failed();
            }
            bool hasCond = false;
            if (!r.presence(hasCond)) { return failed(); }
            if (hasCond) {
                double cond = 0.0;
                if (!r.f64(cond)) { return failed(); }
                c.conditionNumber = cond;
            }
            drivetrain.coupling = std::move(c);
        }

        if (!r.u32(n)) { return failed(); }
        manifest.resize(n);
        for (ResourceRef& ref : manifest) {
            if (!readResourceRef(r, ref)) { return failed(); }
        }

        if (!r.u32(n)) { return failed(); }
        diags.resize(n);
        for (core::DiagnosticRecord& d : diags) {
            if (!readDiagnostic(r, d)) { return failed(); }
        }
        if (!readCapabilities(r, caps)) { return failed(); }
        if (!r.raw32(storedIdentity.bytes)) { return failed(); }
        if (r.offset() != bytes.size()) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：存在尾随字节（解码止于偏移 " + std::to_string(r.offset())
                    + "，实得 " + std::to_string(bytes.size()) + "）"});
        }

        // ---- ③builder 全量不变量复核（防篡改字节绕过构造约束）----
        CanonicalModel model = CanonicalModelBuilder()
                                   .setHeader(h)
                                   .setWorld(world)
                                   .setChain(chain)
                                   .setTools(std::move(tools))
                                   .setDefaultTcpIndex(tcp)
                                   .setScene(std::move(scene))
                                   .setDrivetrain(drivetrain)
                                   .setResourceManifest(std::move(manifest))
                                   .setDiagnostics(std::move(diags))
                                   .build();

        // ---- ④身份/能力复核（篡改与半传输的确定性检测——D-13 前置）----
        const core::ContentIdentity computed = computeContentIdentity(model);
        if (!(computed == storedIdentity)) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：内容身份不符（重算 != 编码携带值）——身份域字节被篡改"
                "或来自不同编码器版本"});
        }
        if (!capabilityEqual(caps, model.capabilities())) {
            return Expected<CanonicalModel, RuntimeError>::err(RuntimeError{
                RuntimeErrorCode::InputInvalid,
                "codec/parse：能力块与内容派生不一致——capabilities 字节被篡改"});
        }
        return Expected<CanonicalModel, RuntimeError>::ok(std::move(model));
    } catch (const RuntimeError& e) {
        // builder/编码入口的违约转译为查询轨错误（可恢复数据错误——不外抛）。
        return Expected<CanonicalModel, RuntimeError>::err(e);
    } catch (const core::CoreError& e) {
        // core 工厂（DiagnosticRecord::make 等）对畸形字节的校验失败——数据错误。
        return Expected<CanonicalModel, RuntimeError>::err(
            RuntimeError{RuntimeErrorCode::InputInvalid,
                         std::string{"codec/parse：核心契约校验失败——"} + e.what()});
    }
    // 其余异常（如 std::bad_alloc）不吞——照常传播（资源类错误不伪装成数据错误）。
}

}  // namespace sdurws::ird::runtime::rtcodec
