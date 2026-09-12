/**
 * @file   Snapshot.cpp
 * @brief  RuntimeSnapshot/快照身份/工厂/物化编码的实现（Snapshot.hpp 契约；
 *         §9.1～§9.3/§9.5——集成模式编译，见文件尾 CMake 口径说明）。
 *
 * 设计依据：
 *   - units/runtime.md §9.1（字段表——访问器与合法域）、§9.2（并发/共享/
 *     worker 隔离流程）、§9.3（零外部依赖/迟到语义）、§9.5（IRDMAT1 载荷
 *     与 D-13 身份核对）、§5.2 S10（唯一发布点）、§9.6（发布门禁组合表）、
 *     §8.4（RobWorkBaselineVersion——基线记录随快照）、§6（基座—世界读取
 *     经 BaseWorldTransform 规则单点）
 *   - 任务契约 tasks/foundation/RT-T09.json（RuntimeSnapshot/IRuntimeModelView
 *     实现/工厂/materialize；CR-05 值供给）
 *
 * 实现纪律：
 *   - 摘要只经 core::ContentDigester（CR-02——本单元一切 SHA-256 路径的
 *     唯一底层；"对什么字节做摘要"由各 encode* 声明）；
 *   - 编码全大端/长度前缀/无填充（RT-Codec 家族同规则——NFR-COR-02）；
 *   - fail-fast 面：工厂内部不变量破坏（理论不可达）与调用方契约违约
 *     （UnknownObject/ContextReleased——§3.4 总纲）抛 RuntimeError；
 *     环境错误经 CompileOutcome.diagnostics 稳定码登记（PA-1：码值权威＝
 *     diagnostics，本单元经 Errors.hpp registryCode 单点取串）；
 *   - 线程安全：工厂无状态（并发 create 各自局部对象）；快照构造后只读。
 *
 * 两模式口径（§15.4 v0.10）：本文件消费 S6/S7 编译器（WorkCellCompiler.hpp/
 * DynamicWorkCellCompiler.hpp 私有头——真实 rw/rwsim 非模板类），由
 * runtime/CMakeLists.txt 按 TARGET sdurw_kinematics 条件增列（仅集成模式
 * 编译；冒烟模式 Snapshot.hpp 不被任何 TU include——v0.8②/v0.9⑧ 同款）。
 *
 * RT-T11 增量（src/SnapshotAssembler.hpp 提升）：SnapshotAssembler 类声明与
 * 终态工具（cancelRequested/cancelledOutcome/failedOutcome/toDiagnostic）的
 * 声明提升为单元内私有头（产品编译器 compile() 与工厂共用 S8+S10 尾段——
 * 单一发布逻辑），方法/函数定义保留本文件，行为零变化；同批增补
 * translateNameMapNotices（S8 生成期警告→警告级诊断转译——v0.5⑥ 登记的
 * 落位点）与 assemble 的 extraWarnings 合并通道（默认空＝既有调用零变化）。
 */

#include <sdurws/ird/runtime/Snapshot.hpp>

// ---- 单元内私有编译器（R-2：src/ 同权；S6/S7 是工厂编排的执行段）----
#include "WorkCellCompiler.hpp"           // compileWorkCell（S6）
#include "DynamicWorkCellCompiler.hpp"    // compileDynamicWorkCell（S7）
#include "SnapshotAssembler.hpp"          // SnapshotAssembler＋终态工具（RT-T11 提升——
                                          //  工厂与产品编译器 compile() 共用尾段）

#include <rw/kinematics/Frame.hpp>            // Frame::getName（S8 实际名收集）
#include <rw/models/Device.hpp>               // Device::getName
#include <rw/models/WorkCell.hpp>             // getFrames/getDevices
#include <rwsim/dynamics/DynamicWorkCell.hpp> // DWC 完整类型（Ptr 持有/析构）

#include <sdurws/ird/core/Digest.hpp>       // ContentDigester（CR-02 单点）
#include <sdurws/ird/runtime/BaseWorldTransform.hpp>  // invertWorldBase/gravityToBase/
                                              // kBaseWorldRuleVersion（§6 规则单点）
#include <sdurws/ird/runtime/Codec.hpp>      // rtcodec::parse/parseNameMap＋编码头常量

#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::runtime {

// 帧名保留字（§7.2——WORLD 根帧不入映射；与 WorkCellCompiler 口径一致：
// 根帧是基线树锚点不是编译产物，S8 交叉校验集合不含它）。
namespace {
constexpr const char* kWorldFrameName = "WORLD";
}  // namespace

// =====================================================================
// 大端编码工具（RT-Codec 家族同规则：大端/长度前缀/无填充——NFR-COR-02；
// 全部 append* 为纯局部函数，无共享状态）。
// =====================================================================

namespace {

/// 追加 16 位大端整数。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

/// 追加 32 位大端整数。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

/// 追加 64 位大端整数。
void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

/// 追加 1 字节布尔（0/1——非 0 即 1，编码无歧义面）。
void appendBool(std::vector<std::uint8_t>& out, bool v)
{
    out.push_back(v ? 1u : 0u);
}

/// 追加定长字节块（摘要/身份/id 的裸字节）。
void appendBytes(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::size_t n)
{
    out.insert(out.end(), data, data + n);
}

/// 追加 32 字节摘要。
void appendDigest(std::vector<std::uint8_t>& out, const core::Digest256& d)
{
    appendBytes(out, d.data(), d.size());
}

/// 追加 16 字节强类型 id（Id128 家族的裸字节）。
template <typename Id>
void appendId(std::vector<std::uint8_t>& out, const Id& id)
{
    appendBytes(out, id.bytes.data(), id.bytes.size());
}

/// 追加长度前缀 UTF-8 串（len(8)+字节——8 字节长度与 RT-Codec 大块载体一致）。
void appendString(std::vector<std::uint8_t>& out, const std::string& s)
{
    appendU64(out, static_cast<std::uint64_t>(s.size()));
    appendBytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// 追加 32 字节内容身份（§9.1"入"字段的编码形态）。
void appendContentIdentity(std::vector<std::uint8_t>& out, const core::ContentIdentity& cid)
{
    appendDigest(out, cid.bytes);
}

/// SHA-256 摘要收口（CR-02——唯一摘要底层；输入字节→32 字节）。
core::Digest256 sha256(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 资源引用等值（ResourceRef 无 operator==——§9.1"与 model.resourceManifest
/// 一致"的逐字段复核；sourcePathHint 不入身份但属记录一致性，一并比对）。
bool resourceRefEqual(const ResourceRef& a, const ResourceRef& b)
{
    return a.resourceId == b.resourceId && a.contentDigest == b.contentDigest
        && a.sourcePathHint == b.sourcePathHint && a.state == b.state
        && a.accessVersion == b.accessVersion;
}

}  // namespace

// =====================================================================
// snapshotidentity——快照身份计算（§9.1 snapshotIdentity/workCellCompileIdentity）。
// =====================================================================

namespace snapshotidentity {

std::vector<std::uint8_t> encodeIdentityDomain(const Fields& f)
{
    std::vector<std::uint8_t> out;
    // 预留：避免多次扩容（编码约数百字节级——大模型资源清单线性增长）。
    out.reserve(256 + f.resourceManifest.size() * 64 + f.skippedDynamicObjects.size() * 16);

    // ---- 第一段：来源定位四元组（§9.1 表行 1——快照层 revisionSeq 入身份）----
    appendId(out, f.project);
    appendId(out, f.branch);
    appendId(out, f.revision);
    appendU64(out, f.revisionSeq);

    // ---- 第二段：身份块（表行 2～4：modelIdentity/nameMapIdentity/
    //      nameMapRuleVersion/workCellCompileIdentity）----
    appendContentIdentity(out, f.modelIdentity);
    appendContentIdentity(out, f.nameMapIdentity);
    appendU32(out, f.nameMapRuleVersion);
    appendContentIdentity(out, f.workCellCompileIdentity);

    // ---- 第三段：DWC 事实（表行 5：状态＋Skipped 缺失清单——链序原样）----
    switch (f.dynamicWorkCellState) {
    case DwcSnapshotState::Compiled: out.push_back(0u); break;
    case DwcSnapshotState::SkippedNoPhysics: out.push_back(1u); break;
    }
    appendU32(out, static_cast<std::uint32_t>(f.skippedDynamicObjects.size()));
    for (const core::ObjectId& id : f.skippedDynamicObjects) {
        appendId(out, id);
    }

    // ---- 第四段：编译器与基线（表行 6～7）----
    appendU32(out, f.compilerContractVersion);
    appendString(out, f.compilerVersion);
    appendString(out, f.robworkBaselineVersion);

    // ---- 第五段：编码器版本三元组（表行 8——6 个 16 位分量）----
    appendU16(out, f.codecVersions.canonicalModelMajor);
    appendU16(out, f.codecVersions.canonicalModelMinor);
    appendU16(out, f.codecVersions.nameMapMajor);
    appendU16(out, f.codecVersions.nameMapMinor);
    appendU16(out, f.codecVersions.snapshotMajor);
    appendU16(out, f.codecVersions.snapshotMinor);

    // ---- 第六段：编译选项全集（表行 9——快照身份用全集；WC 键只取子集，
    //      见 computeWorkCellCompileIdentity 的分层说明）----
    appendBool(out, f.compileOptions.includeCollisionGeometry);
    appendU32(out, static_cast<std::uint32_t>(f.compileOptions.geometryDetail));
    appendBool(out, f.compileOptions.requestDynamicWorkCell);

    // ---- 第七段：资源清单（表行 10——逐条 ResourceRef）----
    appendU32(out, static_cast<std::uint32_t>(f.resourceManifest.size()));
    for (const ResourceRef& r : f.resourceManifest) {
        appendId(out, r.resourceId);
        appendDigest(out, r.contentDigest);
        // 源路径提示：presence 字节显式编码（nullopt ≠ 空串——§4.5 可选值行）。
        if (r.sourcePathHint.has_value()) {
            out.push_back(1u);
            appendString(out, *r.sourcePathHint);
        } else {
            out.push_back(0u);
        }
        out.push_back(r.state == ResourceState::Recorded ? 0u : 1u);
        appendU32(out, r.accessVersion);
    }

    // ---- 第八段：能力声明（表行 11——与 rtcodec 能力块同构的确定性投影）----
    appendBool(out, f.capabilities.hasWorkCell);
    appendBool(out, f.capabilities.hasDynamicWorkCell);
    appendBool(out, f.capabilities.hasFullMassInertia);
    appendBool(out, f.capabilities.hasJointVelocityLimits);
    appendBool(out, f.capabilities.hasCollisionGeometry);
    appendBool(out, f.capabilities.hasTools);
    appendBool(out, f.capabilities.hasScene);
    appendBool(out, f.capabilities.hasFrictionModel);
    appendBool(out, f.capabilities.hasCouplingMatrix);
    appendU32(out, static_cast<std::uint32_t>(f.capabilities.jointTypesPresent.size()));
    for (const JointType t : f.capabilities.jointTypesPresent) {
        // 关节类型编码＝§4.5 RT-Codec 关节类型同序枚举值（Description.hpp 声明序）。
        out.push_back(static_cast<std::uint8_t>(t));
    }
    appendBool(out, f.capabilities.hasBidirectionalNameMap);

    return out;
}

core::ContentIdentity compute(const Fields& fields)
{
    // 身份＝SHA-256 over 身份域编码（§9.1 派生行）；CR-02 单点摘要。
    core::ContentIdentity id;
    id.bytes = sha256(encodeIdentityDomain(fields));
    return id;
}

core::ContentIdentity computeWorkCellCompileIdentity(
    const core::ContentIdentity& modelIdentity,
    std::uint32_t compilerContractVersion,
    const std::string& compilerVersion,
    const std::string& robworkBaselineVersion,
    std::uint32_t nameMapRuleVersion,
    const SnapshotCodecVersions& codecVersions,
    const CompileOptions& options)
{
    // 键分量合法域（§9.1 合法列"非零"＋版本串非空）——非法输入 fail-fast：
    // 全零身份/空版本串的键无缓存语义，返回占位键会伪装成合法命中面
    // （NFR-COR-03 不静默）。
    if (!modelIdentity.isValid()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"computeWorkCellCompileIdentity: modelIdentity 为全零保留值"});
    }
    if (compilerVersion.empty() || robworkBaselineVersion.empty()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"computeWorkCellCompileIdentity: 编译器版本/基线版本为空串"});
    }

    // 编码＝公式分量的声明序拼接（§9.1 workCellCompileIdentity 行）：
    // (modelIdentity, compilerContractVersion, compilerVersion,
    //  robworkBaselineVersion, nameMapRuleVersion, baseWorldRuleVersion,
    //  codecVersions, compileOptions〔DWC 无关子集〕)。
    std::vector<std::uint8_t> out;
    out.reserve(160);
    appendContentIdentity(out, modelIdentity);
    appendU32(out, compilerContractVersion);
    appendString(out, compilerVersion);
    appendString(out, robworkBaselineVersion);
    appendU32(out, nameMapRuleVersion);
    // baseWorldRuleVersion：§6 规则版本常量单点（不作参数——消费方不得
    // 各持副本，§9.4"随规则修改递增"；kBaseWorldRuleVersion 即权威值）。
    appendU32(out, kBaseWorldRuleVersion);
    appendU16(out, codecVersions.canonicalModelMajor);
    appendU16(out, codecVersions.canonicalModelMinor);
    appendU16(out, codecVersions.nameMapMajor);
    appendU16(out, codecVersions.nameMapMinor);
    appendU16(out, codecVersions.snapshotMajor);
    appendU16(out, codecVersions.snapshotMinor);
    // DWC 无关子集（D-04 分层键）：requestDynamicWorkCell（capabilityLevel）
    // 只入 DWC 层键，不入 WC 键——"WC 可复用而 DWC 不可"的键面前提。
    appendBool(out, options.includeCollisionGeometry);
    appendU32(out, static_cast<std::uint32_t>(options.geometryDetail));

    core::ContentIdentity key;
    key.bytes = sha256(out);
    return key;
}

}  // namespace snapshotidentity

// =====================================================================
// snapshotcodec——worker 物化编码（§9.5，IRDMAT1）。
// =====================================================================

namespace snapshotcodec {

namespace {

/// 载荷解码的光标读取器（越界即失败——长度前缀逐字段解码的防御面；
/// detail 携字节偏移定位，与 rtcodec parse 的错误面同风格）。
class ByteReader {
public:
    ByteReader(const std::uint8_t* data, std::size_t size) : m_data(data), m_size(size) {}

    /// 是否仍有 n 字节可读。
    bool canRead(std::size_t n) const noexcept { return m_offset + n <= m_size; }
    /// 当前偏移（错误定位用）。
    std::size_t offset() const noexcept { return m_offset; }
    /// 剩余可读字节数（长度前缀预算防线用——readString/readRevision）。
    std::size_t remaining() const noexcept { return m_size - m_offset; }

    /// 读 1 字节。
    bool readU8(std::uint8_t* v)
    {
        if (!canRead(1)) { return false; }
        *v = m_data[m_offset++];
        return true;
    }

    /// 读 16 位大端整数。
    bool readU16(std::uint16_t* v)
    {
        if (!canRead(2)) { return false; }
        *v = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(m_data[m_offset]) << 8) | m_data[m_offset + 1]);
        m_offset += 2;
        return true;
    }

    /// 读 32 位大端整数。
    bool readU32(std::uint32_t* v)
    {
        if (!canRead(4)) { return false; }
        *v = (static_cast<std::uint32_t>(m_data[m_offset]) << 24)
           | (static_cast<std::uint32_t>(m_data[m_offset + 1]) << 16)
           | (static_cast<std::uint32_t>(m_data[m_offset + 2]) << 8)
           | static_cast<std::uint32_t>(m_data[m_offset + 3]);
        m_offset += 4;
        return true;
    }

    /// 读 64 位大端整数（长度前缀载体）。
    bool readU64(std::uint64_t* v)
    {
        if (!canRead(8)) { return false; }
        *v = 0;
        for (int i = 0; i < 8; ++i) {
            *v = (*v << 8) | m_data[m_offset + static_cast<std::size_t>(i)];
        }
        m_offset += 8;
        return true;
    }

    /// 读 n 裸字节（n==0 恒成功——空串/空块的合法形态）。
    bool readBytes(std::uint8_t* dst, std::size_t n)
    {
        if (n == 0) { return true; }
        if (!canRead(n)) { return false; }
        std::copy_n(m_data + m_offset, n, dst);
        m_offset += n;
        return true;
    }

    /// 读长度前缀串（防御伪造超大长度——长度预算先于内存分配，NFR-COR-03；
    /// 与 rtcodec parseNameMap 的"剩余字节预算防线"同款）。
    bool readString(std::string* s)
    {
        std::uint64_t len = 0;
        if (!readU64(&len)) { return false; }
        if (len > remaining()) { return false; }
        s->resize(static_cast<std::size_t>(len));
        return readBytes(reinterpret_cast<std::uint8_t*>(s->empty() ? nullptr : &(*s)[0]),
                         static_cast<std::size_t>(len));
    }

    /// 读 16 字节强类型 id（tag 语义由调用方类型承载——字节面不区分）。
    template <typename Id>
    bool readId(Id* id)
    {
        return readBytes(id->bytes.data(), id->bytes.size());
    }

    /// 读 32 字节摘要/身份。
    bool readDigest(core::Digest256* d) { return readBytes(d->data(), d->size()); }

private:
    const std::uint8_t* m_data; ///< 输入字节（借持——只读）
    std::size_t m_size = 0;     ///< 总长
    std::size_t m_offset = 0;   ///< 当前偏移
};

/// 解码失败载荷（InputInvalid＋偏移定位——查询轨错误面）。
RuntimeError decodeError(const char* what, std::size_t offset)
{
    return RuntimeError(RuntimeErrorCode::InputInvalid,
                        std::string{"IRDMAT1 解码失败（偏移 "} + std::to_string(offset)
                            + "）：" + what);
}

/// 读 16 字节 id 的便捷包装（模板成员在失败路径统一转 decodeError）。
template <typename Id>
bool readIdOr(ByteReader& r, Id* id)
{
    return r.readId(id);
}

/// RevisionSummary 解码（布局见 encode 注释；presence 字节显式编码 parent）。
bool readRevision(ByteReader& r, RevisionSummary* rev)
{
    if (!readIdOr(r, &rev->id)) { return false; }
    if (!r.readU64(&rev->seq)) { return false; }
    std::uint8_t parentPresent = 0;
    if (!r.readU8(&parentPresent)) { return false; }
    if (parentPresent == 1) {
        core::RevisionId parent;
        if (!readIdOr(r, &parent)) { return false; }
        rev->parent = parent;
    } else if (parentPresent != 0) {
        return false;  // 非法 presence 值（§4.5——nullopt ≠ 零值的显式编码纪律）
    }
    if (!readIdOr(r, &rev->branch)) { return false; }
    std::uint32_t refCount = 0;
    if (!r.readU32(&refCount)) { return false; }
    if (refCount > 0xFFFFFFu) {
        // 计数预算防线：超过 2^24 条引用视同伪造（真实闭包远小于此——
        // 防御性上限，防超大 count 触发内存预留；语义与 parseNameMap 同）。
        return false;
    }
    rev->objectRefs.resize(refCount);
    for (std::uint32_t i = 0; i < refCount; ++i) {
        ObjectRefEntry& e = rev->objectRefs.at(i);
        if (!readIdOr(r, &e.objectId)) { return false; }
        if (!r.readDigest(&e.contentVersion.bytes)) { return false; }
        if (!r.readString(&e.objectTypeToken)) { return false; }
        if (!r.readDigest(&e.digest)) { return false; }
    }
    return true;
}

/// CompileOptions 解码（geometryDetail 枚举值域校验——阶段 A 仅 Full=0）。
bool readOptions(ByteReader& r, CompileOptions* o)
{
    std::uint8_t b = 0;
    if (!r.readU8(&b)) { return false; }
    if (b > 1) { return false; }
    o->includeCollisionGeometry = (b == 1);
    std::uint32_t detail = 0;
    if (!r.readU32(&detail)) { return false; }
    if (detail != static_cast<std::uint32_t>(GeometryDetail::Full)) {
        return false;  // 未知档位（升版编码才可能出现——拒绝而非尽力猜测）
    }
    o->geometryDetail = GeometryDetail::Full;
    if (!r.readU8(&b)) { return false; }
    if (b > 1) { return false; }
    o->requestDynamicWorkCell = (b == 1);
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode(const MaterializedPayload& payload)
{
    // 载荷合法域（encode 契约的失败面）：模型/映射字节非空、预期身份非零
    // ——空载荷无物化语义，编码占位值会伪装成合法通道数据（NFR-COR-03）。
    if (payload.canonicalModelBytes.empty() || payload.nameMapBytes.empty()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"IRDMAT1 encode: 模型/映射编码字节为空"});
    }
    if (!payload.expectedModelIdentity.isValid() || !payload.expectedNameMapIdentity.isValid()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"IRDMAT1 encode: 预期身份为全零保留值"});
    }

    std::vector<std::uint8_t> out;
    out.reserve(96 + payload.revision.objectRefs.size() * 96
                + payload.canonicalModelBytes.size() + payload.nameMapBytes.size()
                + payload.compilerVersion.size());

    // ---- 编码头（magic＋结构版本——§9.5"RT-Codec 家族"约束）----
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    appendU16(out, kVersionMajor);
    appendU16(out, kVersionMinor);

    // ---- RevisionSummary（修订摘要——execution 通道定位面）----
    appendId(out, payload.revision.id);
    appendU64(out, payload.revision.seq);
    if (payload.revision.parent.has_value()) {
        out.push_back(1u);
        appendId(out, *payload.revision.parent);
    } else {
        out.push_back(0u);
    }
    appendId(out, payload.revision.branch);
    appendU32(out, static_cast<std::uint32_t>(payload.revision.objectRefs.size()));
    for (const ObjectRefEntry& e : payload.revision.objectRefs) {
        appendId(out, e.objectId);
        appendDigest(out, e.contentVersion.bytes);
        appendString(out, e.objectTypeToken);
        appendDigest(out, e.digest);
    }

    // ---- 对象字节/映射字节（长度前缀大块——8 字节长度）----
    appendU64(out, static_cast<std::uint64_t>(payload.canonicalModelBytes.size()));
    appendBytes(out, payload.canonicalModelBytes.data(), payload.canonicalModelBytes.size());
    appendU64(out, static_cast<std::uint64_t>(payload.nameMapBytes.size()));
    appendBytes(out, payload.nameMapBytes.data(), payload.nameMapBytes.size());

    // ---- 编译选项（全集——worker 侧 S7 请求级别一致性输入）----
    appendBool(out, payload.options.includeCollisionGeometry);
    appendU32(out, static_cast<std::uint32_t>(payload.options.geometryDetail));
    appendBool(out, payload.options.requestDynamicWorkCell);

    // ---- 编译器版本面（快照身份分量——重建快照身份与主进程一致的前提）----
    appendU32(out, payload.compilerContractVersion);
    appendString(out, payload.compilerVersion);

    // ---- 编译身份预期值（D-13 核对基准）----
    appendContentIdentity(out, payload.expectedModelIdentity);
    appendContentIdentity(out, payload.expectedNameMapIdentity);

    return out;
}

Expected<MaterializedPayload, RuntimeError> parse(const std::vector<std::uint8_t>& bytes)
{
    // 校验链 ①：magic/版本匹配——版本不符＝拒绝而非尽力猜测（编码升版是
    // 破坏性变更；NFR-COR-03 不静默）。
    if (bytes.size() < kMagic.size() + 4) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("字节长度小于编码头", bytes.size()));
    }
    if (std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        // 命中期望 magic——继续
    } else {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("magic 不符（非 IRDMAT1 家族）", 0));
    }
    // 以读取器逐字段解码（头 7+2+2 字节之后为载荷体；版本核对在尾部独立
    // 复核——保持错误信息明确）。
    ByteReader r(bytes.data() + kMagic.size() + 4,
                 bytes.size() - kMagic.size() - 4);

    MaterializedPayload payload;
    if (!readRevision(r, &payload.revision)) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("RevisionSummary 字段越界/非法 presence", r.offset()));
    }
    std::uint64_t modelLen = 0;
    if (!r.readU64(&modelLen) || modelLen > r.remaining()) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("模型字节长度越界", r.offset()));
    }
    payload.canonicalModelBytes.resize(static_cast<std::size_t>(modelLen));
    if (!r.readBytes(payload.canonicalModelBytes.empty()
                         ? nullptr
                         : payload.canonicalModelBytes.data(),
                     static_cast<std::size_t>(modelLen))) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("模型字节读取越界", r.offset()));
    }
    std::uint64_t mapLen = 0;
    if (!r.readU64(&mapLen) || mapLen > r.remaining()) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("映射字节长度越界", r.offset()));
    }
    payload.nameMapBytes.resize(static_cast<std::size_t>(mapLen));
    if (!r.readBytes(payload.nameMapBytes.empty() ? nullptr : payload.nameMapBytes.data(),
                     static_cast<std::size_t>(mapLen))) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("映射字节读取越界", r.offset()));
    }
    if (!readOptions(r, &payload.options)) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("编译选项字段非法", r.offset()));
    }
    if (!r.readU32(&payload.compilerContractVersion)) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("编译器契约版本读取越界", r.offset()));
    }
    if (!r.readString(&payload.compilerVersion)) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("编译器实现版本读取越界", r.offset()));
    }
    if (!r.readDigest(&payload.expectedModelIdentity.bytes)
        || !r.readDigest(&payload.expectedNameMapIdentity.bytes)) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("预期身份读取越界", r.offset()));
    }
    // 校验链 ②补：无尾随字节（多余数据＝半传输/拼接错位——确定性拒绝）。
    if (r.remaining() != 0) {
        return Expected<MaterializedPayload, RuntimeError>::err(
            decodeError("存在尾随字节", r.offset()));
    }
    // 版本核对（读取器外的独立复核——保持错误信息明确）。
    {
        const std::uint8_t* p = bytes.data() + kMagic.size();
        const std::uint16_t major = static_cast<std::uint16_t>((p[0] << 8) | p[1]);
        const std::uint16_t minor = static_cast<std::uint16_t>((p[2] << 8) | p[3]);
        if (major != kVersionMajor || minor != kVersionMinor) {
            return Expected<MaterializedPayload, RuntimeError>::err(
                decodeError("结构版本不符（拒绝而非尽力猜测）", kMagic.size()));
        }
    }
    return Expected<MaterializedPayload, RuntimeError>::ok(std::move(payload));
}

}  // namespace snapshotcodec

// =====================================================================
// RobWorkBaselineVersion（§8.4——Adapter.hpp 声明；基线记录随快照身份块）。
// =====================================================================

RobWorkBaselineVersion RobWorkBaselineVersion::capture()
{
    // 取值口径（§15.4 v0.10 登记）：vendored 基线锚（框架子树最后实质提交
    // 31b81845——"初始可运行版本"，SA-02 零修改）＋工具链选项摘要。
    // ★ WP-24-T01（ird/share/baseline.md）冻结精确基线串后仅替换此处常量
    // 单点（消费方零改动；O-20 同源待确认项）。
    RobWorkBaselineVersion v;
#if defined(_MSC_VER)
    // 工具链摘要：编译器版本（_MSC_VER）＋目标架构＋语言标准＋字符集选项
    // ——同基线同工具链确定性同串；工具链升级＝串变＝产物不可比（NFR-DEP-05
    // 的正向语义：旧基线产物不得跨工具链复用）。
    v.text = "vendored-31b81845+msvc" + std::to_string(_MSC_VER) + "+x64+cpp17+utf8";
#else
    // 非 MSVC 工具链（防御分支——本文件仅在 MSVC 集成模式编译；保留占位
    // 以保证任何工具链下 text 非空，空串会被快照构造门禁拒绝）。
    v.text = "vendored-31b81845+unknown-toolchain";
#endif
    return v;
}

// =====================================================================
// RuntimeSnapshot（§9.1——装配、门禁、视图实现）。
// =====================================================================

RuntimeSnapshot::RuntimeSnapshot(snapshotidentity::Fields fields, const CanonicalModel& model,
                                 const RuntimeNameMap& nameMap,
                                 rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> dynamicWorkCell,
                                 rw::core::Ptr<const rw::models::WorkCell> workCell,
                                 std::vector<core::DiagnosticRecord> diagnostics,
                                 std::chrono::system_clock::time_point createdAtUtc,
                                 SnapshotOrigin createdFrom)
    // ---- 成员初始化序＝声明序（§8.5：m_dynamicWorkCell 先于 m_workCell；
    //      m_nameMap 先于 m_resolver——解析器绑定其地址）----
    : m_dynamicWorkCell(dynamicWorkCell)
    , m_workCell(workCell)
    , m_workCellView(workCell)  // 空 Ptr 由视图构造 fail-fast（防御）
    , m_fields(std::move(fields))
    , m_model(model)
    , m_nameMap(nameMap)
    , m_resolver(m_nameMap)
    , m_diagnostics(std::move(diagnostics))
    , m_createdAtUtc(createdAtUtc)
    , m_createdFrom(createdFrom)
    , m_snapshotIdentity(snapshotidentity::compute(m_fields))
{
    // ---- 发布门禁（§9.6 组合表＋§9.1"合法与非法实例"列的构造期执行；
    //      全部 fail-fast——快照是"声明完整的发布态"，非法实例不可存在）----

    // 身份块非零（§9.1 合法列：modelIdentity/nameMapIdentity/
    // workCellCompileIdentity 均为"非零"）。
    if (!m_fields.modelIdentity.isValid() || !m_fields.nameMapIdentity.isValid()
        || !m_fields.workCellCompileIdentity.isValid()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeSnapshot 构造拒绝：身份块含全零保留值"});
    }
    // 版本串非空（§9.1 合法列：compilerVersion/robworkBaselineVersion）。
    if (m_fields.compilerVersion.empty() || m_fields.robworkBaselineVersion.empty()) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeSnapshot 构造拒绝：编译器版本/基线版本为空串"});
    }
    // 身份与产物一致（工厂取值口径的防御复核——modelIdentity 恒等于模型
    // 内容身份、nameMapIdentity 恒等于映射内容身份；不一致＝装配错误）。
    if (!(m_fields.modelIdentity == m_model.contentIdentity())) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeSnapshot 构造拒绝：modelIdentity 与模型内容身份不一致"});
    }
    if (!(m_fields.nameMapIdentity == m_nameMap.contentIdentity())) {
        throw RuntimeError(RuntimeErrorCode::InputInvalid,
                           std::string{"RuntimeSnapshot 构造拒绝：nameMapIdentity 与映射内容身份不一致"});
    }
    // §9.1"与 capability 一致（不一致构造拒绝）"的执行规则（见
    // DwcSnapshotState 注释）：Compiled 必因物性齐备；Skipped 且带缺失清单
    // 时能力位必为 false（物性缺失路径——S7 门控保证，此处防御重复）。
    if (m_fields.dynamicWorkCellState == DwcSnapshotState::Compiled
        && !m_model.capabilities().hasDynamicWorkCell) {
        throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                           std::string{"stage=S10 防御：DWC 状态 Compiled 但能力位为 false（§9.1 不一致构造拒绝）"});
    }
    if (m_fields.dynamicWorkCellState == DwcSnapshotState::SkippedNoPhysics
        && !m_fields.skippedDynamicObjects.empty()
        && m_model.capabilities().hasDynamicWorkCell) {
        throw RuntimeError(RuntimeErrorCode::DwcCompileFailed,
                           std::string{"stage=S10 防御：Skipped 缺失清单非空但能力位为 true（§9.1 不一致构造拒绝）"});
    }
    // 资源清单与模型一致（§9.1 合法列"与 model.resourceManifest 一致"——
    // 工厂取值口径的防御复核；逐字段等值）。
    const std::vector<ResourceRef>& manifest = m_model.resourceManifest();
    if (m_fields.resourceManifest.size() != manifest.size()) {
        throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                           std::string{"RuntimeSnapshot 构造拒绝：resourceManifest 与模型不一致"});
    }
    for (std::size_t i = 0; i < manifest.size(); ++i) {
        if (!resourceRefEqual(m_fields.resourceManifest.at(i), manifest.at(i))) {
            throw RuntimeError(RuntimeErrorCode::StructureInvalid,
                               std::string{"RuntimeSnapshot 构造拒绝：resourceManifest 第 "}
                                   + std::to_string(i) + " 条与模型不一致");
        }
    }
}

RuntimeSnapshot::~RuntimeSnapshot() = default;
// 析构序（声明序逆序）：m_snapshotIdentity→…→m_resolver→m_nameMap→
// m_model→m_fields→m_workCellView→m_workCell→m_dynamicWorkCell。
// m_workCell 析构仅减 WC 引用计数——WC 对象因 rwsim DWC 自持 WC 引用
// （§8.5）在 m_dynamicWorkCell 析构（最后）前持续存活，即"WC 实际释放
// 不早于 DWC"（D-14/v0.9⑤——RT-AD-3 同款引用计数观测的类内承载）。

// ---- IRuntimeModelView 实现（§8.3——全部只读直查；规则投影经 §6 单点）----

const CanonicalModel& RuntimeSnapshot::model() const
{
    return m_model;
}

const RuntimeNameMap& RuntimeSnapshot::nameMap() const
{
    return m_nameMap;
}

const RuntimeCapability& RuntimeSnapshot::capabilities() const
{
    return m_model.capabilities();
}

const WorkCellConstView& RuntimeSnapshot::workCell() const
{
    return m_workCellView;
}

Expected<DynamicWorkCellConstView, RuntimeError> RuntimeSnapshot::tryDynamicWorkCell() const
{
    // 无 DWC→错误＋定位能力项（§8.3 属性表"code 指向能力缺失，非崩溃"）：
    // DWC 不在本快照＝解析目标缺失，取 UnknownObject（§7.4 同码语义——
    // "解析目标不在本快照"）；detail 携状态与能力位定位（下游按 §9.6
    // 能力声明处置——dynamics 的 DataInsufficient 域判定归 dynamics）。
    if (m_dynamicWorkCell == nullptr) {
        return Expected<DynamicWorkCellConstView, RuntimeError>::err(RuntimeError(
            RuntimeErrorCode::UnknownObject,
            std::string{"快照无 DynamicWorkCell（dynamicWorkCellState=SkippedNoPhysics"
                        "，hasDynamicWorkCell="}
                + (m_model.capabilities().hasDynamicWorkCell ? "true" : "false")
                + "）——能力缺失未编译，见 §9.6 能力声明"));
    }
    return Expected<DynamicWorkCellConstView, RuntimeError>::ok(
        DynamicWorkCellConstView(m_dynamicWorkCell));
}

rw::math::Transform3D<double> RuntimeSnapshot::worldToBase() const
{
    // §6.3 唯一存储直读（不复制、不重算——T_world_base 权威值在模型内）。
    return m_model.world().T_world_base;
}

rw::math::Transform3D<double> RuntimeSnapshot::baseToWorld() const
{
    // §6.1 反解＝规则函数单点（正交前提由 S5/builder 校验保证——理论不可
    // 达的 InputInvalid 经异常向上传播，不静默）。
    return invertWorldBase(m_model.world().T_world_base);
}

rw::math::Vector3D<double> RuntimeSnapshot::gravityWorld() const
{
    // 世界系重力恒定（MDL-22——不随安装预设变化）。
    return m_model.world().gravityWorld;
}

rw::math::Vector3D<double> RuntimeSnapshot::gravityBase() const
{
    // DYN-01 投影公式单点（禁止"倒挂就地取反"——§6.4 禁止清单 2）。
    return gravityToBase(m_model.world().T_world_base.R(), m_model.world().gravityWorld);
}

rw::kinematics::State RuntimeSnapshot::makeState() const
{
    // 每线程 State 工厂（§8.7/§9.2——值拷贝，调用方线程私有；跨线程绝不
    // 共享 State）。
    return m_workCellView.defaultState();
}

const IRuntimeNameResolver& RuntimeSnapshot::nameResolver() const
{
    // ⑥端口＝绑定本快照映射的解析器（迟到反解的权威路径——§9.3：项目
    // 切换后用结果绑定快照的映射，绝不用当前映射）。
    return m_resolver;
}

// =====================================================================
// RuntimeSnapshotFactory（§10.0——S1–S10 编排＋worker 物化）。
// =====================================================================

namespace {

/// S8 交叉校验的实际名收集（§7.2"编译器写入 WC 的名字必须与映射输出逐一
/// 相等"——工厂对 S6+S7 写入全集的核对；WORLD 根帧不入映射故排除）。
/// 本文件局部（仅 materialize/create 的编排路径使用；产品编译器经
/// SnapshotAssembler::assemble 内部同名逻辑共享——RT-T11 提升登记）。
std::vector<std::string> collectActualRuntimeNames(const WorkCellCompileOutcome& s6)
{
    std::vector<std::string> names;
    // 全部帧（S6 Frame 树＋S7 挂接的 Body 承载 MovableFrame——一个不漏）。
    for (const rw::kinematics::Frame* frame : s6.workCell->getFrames()) {
        const std::string name = frame->getName();
        if (name != kWorldFrameName) {
            names.push_back(name);
        }
    }
    // 全部设备（S6 SerialDevice＋S7 RigidDevice——设备名同在映射 Device 条目）。
    for (const rw::core::Ptr<rw::models::Device>& device : s6.workCell->getDevices()) {
        names.push_back(device->getName());
    }
    return names;
}

}  // namespace

// =====================================================================
// 编译链终态工具（声明在 src/SnapshotAssembler.hpp——RT-T11 提升为工厂与
// 产品编译器共用的单一实现；定义保留本文件，行为零变化）。
// =====================================================================

bool cancelRequested(const ICompileCancelToken* cancel)
{
    return cancel != nullptr && cancel->cancellationRequested();
}

CompileOutcome cancelledOutcome()
{
    CompileOutcome out;
    out.status = CompileStatus::Cancelled;
    // 码面：RT-CANCELLED（registryCode(Cancelled) 单点——PA-1 不私裁码值）。
    out.diagnostics.push_back(core::DiagnosticRecord::make(
        std::string{registryCode(RuntimeErrorCode::Cancelled)},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::string{"runtime 编译链取消（段边界轮询——协作取消为正常控制流，非错误）"},
        std::string{token(RuntimeErrorCode::Cancelled)},
        std::string{"重新发起编译（无半成品残留，可直接重试——§5.5 可重入）"}));
    return out;
}

CompileOutcome failedOutcome(core::DiagnosticRecord record)
{
    CompileOutcome out;
    out.status = CompileStatus::Failed;
    out.diagnostics.push_back(std::move(record));
    return out;
}

core::DiagnosticRecord toDiagnostic(const RuntimeError& e, const char* stage)
{
    std::string code{registryCode(e.code())};
    if (code.empty()) {
        // 事件码路径（RobWorkError——不发注册码的第三值，§10.11 v0.3）。
        code = "RT-ROBWORK-ERROR";
    }
    return core::DiagnosticRecord::make(
        code,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::string{"runtime 编译链失败（段: "} + stage + "）——错误归属见 §5.2 十段表",
        std::string{e.what()},
        std::string{"按码面分类修复输入/环境后重编译（分类：Errors.hpp category）"});
}

/// S8 名称生成期警告→警告级诊断的转译（声明见 src/SnapshotAssembler.hpp；
/// RT-T11 交付——v0.5⑥ 登记的落位点，建议码待 diagnostics 收编，PA-1）。
std::vector<core::DiagnosticRecord> translateNameMapNotices(const RuntimeNameMap& map)
{
    // 建议码（待收编——收编后仅替换本常量单点；PA-1：码值权威归 diagnostics）。
    constexpr const char* kNameDisambiguatedCode = "RT-NAME-DISAMBIGUATED";
    std::vector<core::DiagnosticRecord> out;
    out.reserve(map.notices().size());
    for (const RuntimeNameNotice& n : map.notices()) {
        out.push_back(core::DiagnosticRecord::make(
            std::string{kNameDisambiguatedCode},
            n.objectId,
            n.originalLocalName,
            n.fullName,
            std::string{"S8 名称映射生成警告（不阻断——§7.2 消歧/空名警告）"},
            n.kind == RuntimeNameNotice::Kind::EmptyName
                ? std::string{"空名已合法化为 unnamed（原名=\"\"——builder 拒绝面之外"
                              "的规则面保留分支）"}
                : std::string{"同名消歧：["} + n.originalLocalName + "] → ["
                      + n.runtimeLocalName + "]（按 ObjectId 序加后缀）",
            std::string{"经⑥端口以消歧后运行时名引用该对象（AT-18 往返不变）"}));
    }
    return out;
}

/// D-13 身份核对失败诊断（§10.0 materialize 行——诊断含期望/实得身份）。
namespace {

core::DiagnosticRecord identityMismatchDiagnostic(const char* what,
                                                  const core::ContentIdentity& expected,
                                                  const core::ContentIdentity& actual)
{
    return core::DiagnosticRecord::make(
        std::string{registryCode(RuntimeErrorCode::InputInvalid)},
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::string{"worker 物化身份核对失败（D-13——不相等拒绝执行，§9.5/§9.2）"},
        std::string{what} + "：期望 " + expected.toCanonical() + "，实得 "
            + actual.toCanonical(),
        std::string{"核对派发通道与请求一致性（防通道错配/半传输）后重新物化"});
}

}  // namespace

// =====================================================================
// SnapshotAssembler——快照装配尾段（类声明提升至 src/SnapshotAssembler.hpp——
// RT-T11：产品编译器 compile() 与工厂共用 S8＋S10 尾段；方法定义保留本文件）。
// =====================================================================

CompileOutcome SnapshotAssembler::assemble(const CanonicalModel& model,
                                           const RuntimeNameMap& map,
                                           const WorkCellCompileOutcome& s6,
                                           const DynamicWorkCellCompileOutcome* dwcOutcome,
                                           const CompileOptions& options,
                                           std::uint32_t compilerContractVersion,
                                           const std::string& compilerVersion,
                                           SnapshotOrigin origin,
                                           std::vector<core::DiagnosticRecord> extraWarnings)
{
    // ---- S8：WC 实际对象名 ↔ 映射逐一交叉校验（§7.2 生成时点；双前缀/
    //      旧名残留/写出映射外名字在此拦截——RT-NM-5/6 的产品执行点）----
    crossCheckRuntimeNames(map, collectActualRuntimeNames(s6));

    // ---- 身份块装配（§9.1"入"字段取值口径）----
    const std::string baseline = RobWorkBaselineVersion::capture().text;
    // 编码器版本三元组：与各编码头一致（§9.1 合法列）——常量单点取值，
    // 消费方不得另写字面量（编码升版＝全体身份变化）。
    const SnapshotCodecVersions codecVersions{rtcodec::kVersionMajor,
                                              rtcodec::kVersionMinor,
                                              rtcodec::kNameMapVersionMajor,
                                              rtcodec::kNameMapVersionMinor,
                                              snapshotcodec::kVersionMajor,
                                              snapshotcodec::kVersionMinor};

    snapshotidentity::Fields fields;
    fields.project = model.header().project;
    fields.branch = model.header().branch;
    fields.revision = model.header().revision;
    fields.revisionSeq = model.header().revisionSeq;
    fields.modelIdentity = model.contentIdentity();
    fields.nameMapIdentity = map.contentIdentity();
    fields.nameMapRuleVersion = map.ruleVersion();
    // WC 层缓存键（§9.1 公式——与 RT-T10 CacheKey 复用同一计算单点）。
    fields.workCellCompileIdentity = snapshotidentity::computeWorkCellCompileIdentity(
        fields.modelIdentity, compilerContractVersion, compilerVersion, baseline,
        fields.nameMapRuleVersion, codecVersions, options);

    // ---- DWC 事实状态（§9.1 dynamicWorkCellState——三个成因分支）----
    if (dwcOutcome != nullptr && dwcOutcome->status == DwcCompileStatus::Compiled) {
        fields.dynamicWorkCellState = DwcSnapshotState::Compiled;
        // Compiled 时缺失清单恒空（§5.2 S7——构造成功无缺失面）。
    } else {
        fields.dynamicWorkCellState = DwcSnapshotState::SkippedNoPhysics;
        if (dwcOutcome != nullptr) {
            // 物性缺失路径：缺失清单＝S7 产物（链序）；"未请求 DWC"路径
            // 清单为空（无缺失事实——见 DwcSnapshotState 注释）。
            fields.skippedDynamicObjects = dwcOutcome->skippedObjects;
        }
    }

    fields.compilerContractVersion = compilerContractVersion;
    fields.compilerVersion = compilerVersion;
    fields.robworkBaselineVersion = baseline;
    fields.codecVersions = codecVersions;
    fields.compileOptions = options;
    fields.resourceManifest = model.resourceManifest();
    fields.capabilities = model.capabilities();

    // ---- 诊断合并（模型警告块＋S7 警告＋编排路径额外警告〔S8 消歧转译等
    //      ——extraWarnings〕；不产 error 码记录——§9.6 组合表 Published 行
    //      的"无 error 级"由来源受限保证：builder 已拒 error、S7 警告恰为
    //      RT-CAPABILITY-MISSING 警告级、S8 转译恒警告级）----
    std::vector<core::DiagnosticRecord> diagnostics = model.diagnostics();
    if (dwcOutcome != nullptr) {
        diagnostics.insert(diagnostics.end(), dwcOutcome->warnings.begin(),
                           dwcOutcome->warnings.end());
    }
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(extraWarnings.begin()),
                       std::make_move_iterator(extraWarnings.end()));

    // ---- S10：装配＋原子发布（shared_ptr<const>——此前一切产物对外不可见）。
    //      全部以拷贝传入：私有构造在初始化列表与函数体内都要消费 fields
    //      （身份计算），moved-from 状态会污染身份——确定性优先，值量级
    //      （数百字节～数 KB）拷贝成本可忽略。
    //      DWC 句柄：Ptr<DWC>→Ptr<const DWC> 经基线模板转换构造（共享同一
    //      std::shared_ptr 控制块——所有权随快照交接，§8.2/§8.5）。
    rw::core::Ptr<const rwsim::dynamics::DynamicWorkCell> dwcForSnapshot;
    if (dwcOutcome != nullptr && dwcOutcome->status == DwcCompileStatus::Compiled) {
        dwcForSnapshot = dwcOutcome->dynamicWorkCell;
    }
    // 创建时刻：观测性要素（不入身份——§9.1"不入"列）；UTC 语义由
    // system_clock 承载（core D-02 同款——无包装类型）。
    std::shared_ptr<const RuntimeSnapshot> snapshot(new RuntimeSnapshot(
        fields, model, map, dwcForSnapshot, s6.workCell, diagnostics,
        std::chrono::system_clock::now(), origin));

    CompileOutcome out;
    out.status = CompileStatus::Published;
    out.snapshot = std::move(snapshot);
    out.diagnostics = std::move(diagnostics);
    return out;
}

CompileOutcome RuntimeSnapshotFactory::create(const CompileRequest& request,
                                              ICanonicalModelCompiler& compiler)
{
    try {
        // ---- 段边界取消检查（S1 前——§3.3 令牌可空＝不可取消）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }

        // ---- S1–S5：委托编译器分段入口（§10.0——工厂经分段入口编排；
        //      注入失败的归属转译见 Sources.hpp 文件头表）----
        const Expected<CanonicalModel, RuntimeError> canonical =
            compiler.buildCanonicalModel(request);
        if (!canonical.ok()) {
            const RuntimeError& err = canonical.error();
            // 调用方契约违约（UnknownObject/ContextReleased）＝fail-fast 轨
            // （§3.4 总纲：不走诊断收集——对不存在修订/已释放上下文的编译
            // 请求以异常终止；RT-SNAP-3 的"ContextReleased 拒绝"观测面）。
            if (isContractViolation(err.code())) {
                throw err;
            }
            // 环境错误→Failed＋稳定码诊断（不短路——单条错误即终态，
            // S1–S5 的警告面随 err.detail 携带定位）。
            return failedOutcome(toDiagnostic(err, "S1-S5"));
        }

        // ---- 段边界取消检查（S6 前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }

        // ---- S6：WorkCell 编译（BaseMount 唯一写入＋S9 防御自检内置——
        //      RT-T07；异常经 §8.4 转译后以 RuntimeError 抛出→catch 转 Failed）----
        const WorkCellCompileOutcome s6 = compileWorkCell(canonical.get());

        // ---- 段边界取消检查（S7 前）----
        if (cancelRequested(request.cancel)) {
            return cancelledOutcome();
        }

        // ---- S7：DynamicWorkCell 编译（能力门控；requestDynamicWorkCell=
        //      false 时跳过——§9.4 capabilityLevel"是否要求 DWC"）----
        DynamicWorkCellCompileOutcome s7;
        const bool dwcRequested = request.options.requestDynamicWorkCell;
        if (dwcRequested) {
            s7 = compileDynamicWorkCell(canonical.get(), s6);
        }

        // ---- S8：名称映射构建（工厂执行——S6/S7 写入的名字必须与映射
        //      逐一相等；交叉校验在公共尾段）----
        const RuntimeNameMap map = buildRuntimeNameMap(canonical.get());

        // ---- S8＋S10：交叉校验＋身份装配＋发布（生成期警告随尾段合并——
        //      v0.5⑥ 转译落位，RT-T11）----
        return SnapshotAssembler::assemble(canonical.get(), map, s6, dwcRequested ? &s7 : nullptr,
                                  request.options, compiler.contractVersion(),
                                  compiler.implementationVersion(), SnapshotOrigin::Command,
                                  translateNameMapNotices(map));
    } catch (const RuntimeError& e) {
        // 契约违约保持 fail-fast 轨（§3.4——S6–S10 段内编译器抛出的
        // UnknownObject/ContextReleased 不得转为诊断静默）。
        if (isContractViolation(e.code())) {
            throw;
        }
        // 编译硬失败（WC/DWC/Name/BaseWorld…）→Failed＋稳定码诊断（§8.4：
        // 转译后以 Failed 终止；瞬态 RobWork 对象由局部产物 RAII 析构——
        // MDL-06"失败不发布半成品"）。
        return failedOutcome(toDiagnostic(e, "S6-S10"));
    } catch (const std::bad_alloc&) {
        // 内存不足→ResourceBudget＋对象清理（§5.5——RT-CPX-4 的 bad_alloc
        // 分支；局部 s6/s7 产物析构即清理，无泄漏面）。
        return failedOutcome(core::DiagnosticRecord::make(
            std::string{registryCode(RuntimeErrorCode::ResourceBudget)},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::string{"runtime 编译链失败（内存不足——§5.5 bad_alloc 转译）"},
            std::string{token(RuntimeErrorCode::ResourceBudget)},
            std::string{"缩减模型规模/释放内存后重编译（瞬态对象已由 RAII 清理）"}));
    } catch (const std::exception& e) {
        // 其余异常（RobWork 基线异常等）→事件码转译＋Failed（§8.4 转译表
        // 第三行；stage=S10＝工厂编排段的失败定位面）。
        return failedOutcome(
            translateRobWorkError(e, "S10", std::nullopt));
    }
}

CompileOutcome RuntimeSnapshotFactory::materialize(
    const std::vector<std::uint8_t>& materializedBytes)
{
    try {
        // ---- ① 载荷解码（字节面校验——magic/版本/长度/尾随）----
        const Expected<snapshotcodec::MaterializedPayload, RuntimeError> payload =
            snapshotcodec::parse(materializedBytes);
        if (!payload.ok()) {
            return failedOutcome(toDiagnostic(payload.error(), "M1 载荷解码"));
        }

        // ---- ② 模型重建（rtcodec::parse——builder 全量不变量复核＋重算
        //      身份与携带值比对＝篡改/半传输的模型层防线，D-13 前置）----
        const Expected<CanonicalModel, RuntimeError> model =
            rtcodec::parse(payload.get().canonicalModelBytes);
        if (!model.ok()) {
            return failedOutcome(toDiagnostic(model.error(), "M2 模型重建"));
        }

        // ---- ③ 映射重建（parseNameMap——结构复核＋身份重算）----
        const Expected<RuntimeNameMap, RuntimeError> nameMap =
            rtcodec::parseNameMap(payload.get().nameMapBytes);
        if (!nameMap.ok()) {
            return failedOutcome(toDiagnostic(nameMap.error(), "M3 映射重建"));
        }

        // ---- ④ D-13 身份核对（§9.5"worker 按身份核对，不等即拒绝执行"；
        //      诊断含期望/实得身份——§10.0 materialize 行原文）----
        if (!(model.get().contentIdentity() == payload.get().expectedModelIdentity)) {
            return failedOutcome(identityMismatchDiagnostic(
                "modelIdentity", payload.get().expectedModelIdentity,
                model.get().contentIdentity()));
        }
        if (!(nameMap.get().contentIdentity() == payload.get().expectedNameMapIdentity)) {
            return failedOutcome(identityMismatchDiagnostic(
                "nameMapIdentity", payload.get().expectedNameMapIdentity,
                nameMap.get().contentIdentity()));
        }

        // ---- ⑤ S6/S7/S8＋S10（与 create 同一尾段；createdFrom=Worker——
        //      §9.1 createdFrom 枚举的 worker 路径）----
        const WorkCellCompileOutcome s6 = compileWorkCell(model.get());
        DynamicWorkCellCompileOutcome s7;
        const bool dwcRequested = payload.get().options.requestDynamicWorkCell;
        if (dwcRequested) {
            s7 = compileDynamicWorkCell(model.get(), s6);
        }
        return SnapshotAssembler::assemble(model.get(), nameMap.get(), s6,
                                  dwcRequested ? &s7 : nullptr, payload.get().options,
                                  payload.get().compilerContractVersion,
                                  payload.get().compilerVersion, SnapshotOrigin::Worker,
                                  translateNameMapNotices(nameMap.get()));
    } catch (const RuntimeError& e) {
        // 字节面/重建面不应出现契约违约码；保持与 create 同轨的防御一致
        // （§3.4——违约码不走诊断静默）。
        if (isContractViolation(e.code())) {
            throw;
        }
        return failedOutcome(toDiagnostic(e, "M4 快照重建"));
    } catch (const std::bad_alloc&) {
        // 与 create 同款预算面（§5.5）。
        return failedOutcome(core::DiagnosticRecord::make(
            std::string{registryCode(RuntimeErrorCode::ResourceBudget)},
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::string{"runtime 物化失败（内存不足——§5.5 bad_alloc 转译）"},
            std::string{token(RuntimeErrorCode::ResourceBudget)},
            std::string{"缩减载荷/释放内存后重新物化（瞬态对象已由 RAII 清理）"}));
    } catch (const std::exception& e) {
        return failedOutcome(translateRobWorkError(e, "M4", std::nullopt));
    }
}

}  // namespace sdurws::ird::runtime
