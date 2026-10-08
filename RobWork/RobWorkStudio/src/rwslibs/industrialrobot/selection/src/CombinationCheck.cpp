/**
 * @file   CombinationCheck.cpp
 * @brief  组合校核的实现（Combination.hpp 契约的唯一实现翻译单元）——
 *         组合键规范序列化、候选组合构造（兼容过滤/去重/批切分）、候选
 *         传动参数构造、canonical 字节编解码、组合校核核心（§9.3 清单
 *         十步）与 sel-combination-check 评估器（③端口适配层）。
 *
 * 设计依据：
 *   - Combination.hpp 文件头（SEL-05 红线执行形态/错误轨两分法/线程模型）
 *   - units/selection.md §9（两段管线/校核清单/资格矩阵/组合身份）、
 *     §10.2（分层与短路边界——候选能力筛选不短路）、§11.3（惯量比未
 *     裁决期保守行为）、§14.5（组合构造接口）、§14.0（通用约定）
 *   - 需求 SEL-05/DYN-04（消费）/SEL-06/EVI-02/AT-38/NFR-COR-02/03
 *   - 任务契约 tasks/foundation/WP-19-T05.json（acceptance 1～3）
 *
 * ★ 红线自查（本翻译单元逐条对照，review 可按行核对）：
 *   1. 本文件【零】传动映射公式——无传动比映射、虚功对偶、反射惯量折算、功率计算的任何
 *      计算语句（映射公式记法不书写——契约测试词表扫描面）；出现的全部数值均为调用方供给的映射事实
 *      直通（SEL-05"不自建映射实现"；契约测试以映射公式词表零命中
 *      扫描钉住，contract_test/CombinationCheckContractTest.cpp）；
 *   2. 唯一的传动比换算＝c＝1/n（候选减速器速比 n:1 → drivetrain 卡
 *      §5.3 c 口径——候选传动参数构造，卡 §9.1"selection 只能"行明文
 *      允许；单点书写于 makeCombinationDriveInputs，禁第二处）；
 *   3. 零 Qt、零他单元 include（仅 core/evidence/本单元公共头——卡
 *      §3.2 两条登记边）。
 *
 * 线程安全：全部无状态纯函数（可重入）；评估器实例单线程（descriptor
 *   声明），create() 仅构造——线程安全（drivetrain 适配层同款口径）。
 */

#include <sdurws/ird/selection/Combination.hpp>

#include <sdurws/ird/selection/DiagCodes.hpp>
// T06：Profile 项 itemId 词表常量（kSelProfileItem*——唯一书写点引用；
// 评估器证据项产出与 sel 域 Profile 必需项对齐——WP-19-T06 acceptance 3。
// 单元内头依赖：FeasibleSet.hpp → Combination.hpp 单向，无环）。
#include <sdurws/ird/selection/FeasibleSet.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::selection {
namespace {

// =====================================================================
// 内部工具（确定性序与 fail-fast 校验的共享面）
// =====================================================================

/// 非有限判定（NFR-COR-03：NaN/±Inf 一律拒绝——不静默进入计算/编码）。
bool isFiniteNumber(double v) noexcept
{
    return std::isfinite(v);
}

/// 可选数值的有限性校验（present 时必须有限——调用方契约违约 fail-fast）。
void requireFiniteIfPresent(const std::optional<double>& v, const char* what)
{
    if (v.has_value() && !isFiniteNumber(*v)) {
        throw std::invalid_argument(std::string("组合校核：") + what
                                    + " 含非有限数值（NaN/±Inf 拒绝——NFR-COR-03）");
    }
}

/// 在切片条目中按键定位依赖条目（(kind,key) 字典序存储——evidence §4.2.2；
/// 条目数为个位数，线性扫描足够）。
const evidence::DependencyEntry* findEntry(const evidence::InputSlice& slice,
                                           std::string_view key)
{
    for (const evidence::DependencyEntry& e : slice.entries) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

/// 摘要快捷计算（SHA-256——core ContentDigester 唯一算法面，CR-02）。
core::ContentIdentity digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();
    return digest;
}

/// 摘要快捷计算（文本输入重载——组合键规范序列化用）。
core::ContentIdentity digestOf(const std::string& text)
{
    core::ContentDigester digester;
    digester.update(text.data(), text.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();
    return digest;
}

/// 32 字节摘要 → 小写 hex（64 字符——组合键文本形态；确定性无 locale）。
std::string toHex(const core::Digest256& bytes)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const std::uint8_t b : bytes) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

/// CatalogIdentity 的规范文本（键序列化与编码共用——目录身份三要素＋
/// 内容摘要 hex；source 不进键〔呈现面字段——不参与身份，卡 §4.1〕，
/// 编码面携带以保往返完整）。
std::string catalogKeyText(const CatalogIdentity& c)
{
    // 长度前缀拼接（无歧义序列化——避免分隔符与内容冲突）。
    std::string out;
    const auto appendField = [&out](const std::string& s) {
        const std::uint32_t n = static_cast<std::uint32_t>(s.size());
        out.append(reinterpret_cast<const char*>(&n), sizeof(n));
        out.append(s);
    };
    appendField(c.catalogId);
    appendField(c.version);
    out.append(reinterpret_cast<const char*>(c.contentIdentity.bytes.data()),
               c.contentIdentity.bytes.size());
    return out;
}

/// 快照内按型号查找电机条目（modelId 升序排列——T03 装配保证；二分查找）。
const MotorCatalogEntry* findMotor(const CatalogPackageSnapshot& snapshot,
                                   const ModelId& id)
{
    const auto it = std::lower_bound(
        snapshot.motors.begin(), snapshot.motors.end(), id,
        [](const MotorCatalogEntry& e, const ModelId& key) { return e.modelId < key; });
    if (it != snapshot.motors.end() && it->modelId == id) {
        return &*it;
    }
    return nullptr;
}

/// 快照内按型号查找减速器条目（同上）。
const GearboxCatalogEntry* findGearbox(const CatalogPackageSnapshot& snapshot,
                                       const ModelId& id)
{
    const auto it = std::lower_bound(
        snapshot.gearboxes.begin(), snapshot.gearboxes.end(), id,
        [](const GearboxCatalogEntry& e, const ModelId& key) { return e.modelId < key; });
    if (it != snapshot.gearboxes.end() && it->modelId == id) {
        return &*it;
    }
    return nullptr;
}

// =====================================================================
// canonical 字节编解码原语（小端定宽——drivetrain Codec 同款协议风格）
// =====================================================================

/// 字节写出器（编码侧——追加式；容量自管理）。
class ByteWriter {
public:
    /// 追加原始字节。
    void raw(const void* data, std::size_t n)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        m_bytes.insert(m_bytes.end(), p, p + n);
    }
    /// u32 定宽（小端——协议字序）。
    void u32(std::uint32_t v)
    {
        raw(&v, sizeof(v));
    }
    /// u8（枚举底值/有无标志）。
    void u8(std::uint8_t v)
    {
        raw(&v, sizeof(v));
    }
    /// f64 定宽（小端；调用方保证有限——编码入口已校验）。
    void f64(double v)
    {
        raw(&v, sizeof(v));
    }
    /// string＝u32 字节长＋UTF-8 字节。
    void str(const std::string& s)
    {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    /// 16 字节 ID 原始字节。
    void id128(const core::ObjectId& id)
    {
        raw(id.bytes.data(), id.bytes.size());
    }
    /// 32 字节摘要原始字节。
    void digest32(const core::ContentIdentity& c)
    {
        raw(c.bytes.data(), c.bytes.size());
    }
    /// optional<double>＝u8 有无标志＋有值时 f64。
    void optF64(const std::optional<double>& v)
    {
        u8(v.has_value() ? 1 : 0);
        if (v.has_value()) {
            f64(*v);
        }
    }
    /// optional<std::string>。
    void optStr(const std::optional<std::string>& v)
    {
        u8(v.has_value() ? 1 : 0);
        if (v.has_value()) {
            str(*v);
        }
    }
    /// bool＝u8。
    void boolean(bool v)
    {
        u8(v ? 1 : 0);
    }

    std::vector<std::uint8_t> m_bytes; ///< 累积字节（编码产物）
};

/// 字节读出器（解码侧——严格校验：越界/残余即调用方协议违约 fail-fast）。
class ByteReader {
public:
    ByteReader(const std::vector<std::uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

    /// 头部校验：8 字节 magic＋u32 codec 版本（协议冻结面——违约即协议
    /// 版本错配，调用方须按登记版本重编码）。
    void expectHeader(std::string_view magic, std::uint32_t version)
    {
        if (m_bytes.size() < 8 + sizeof(std::uint32_t)
            || std::memcmp(m_bytes.data(), magic.data(), 8) != 0) {
            throw std::invalid_argument("组合校核解码：magic 头不符（期望 "
                                        + std::string(magic) + "——字节形态/协议版本违约）");
        }
        const std::uint32_t v = u32At(8);
        if (v != version) {
            throw std::invalid_argument("组合校核解码：codec 版本不符（实际 "
                                        + std::to_string(v) + "，期望 "
                                        + std::to_string(version) + "）");
        }
        m_pos = 8 + sizeof(std::uint32_t);
    }

    void raw(void* out, std::size_t n)
    {
        require(n);
        std::memcpy(out, m_bytes.data() + m_pos, n);
        m_pos += n;
    }
    std::uint32_t u32()
    {
        std::uint32_t v = 0;
        raw(&v, sizeof(v));
        return v;
    }
    std::uint8_t u8()
    {
        std::uint8_t v = 0;
        raw(&v, sizeof(v));
        return v;
    }
    double f64()
    {
        double v = 0;
        raw(&v, sizeof(v));
        // 解码面同样拒绝非有限（对称于编码入口——防旁路字节注入）。
        if (!isFiniteNumber(v)) {
            throw std::invalid_argument("组合校核解码：非有限 double（NFR-COR-03）");
        }
        return v;
    }
    std::string str()
    {
        const std::uint32_t n = u32();
        require(n);
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_pos), n);
        m_pos += n;
        return s;
    }
    core::ObjectId id128()
    {
        core::ObjectId id;
        raw(id.bytes.data(), id.bytes.size());
        return id;
    }
    core::ContentIdentity digest32()
    {
        core::ContentIdentity c;
        raw(c.bytes.data(), c.bytes.size());
        return c;
    }
    std::optional<double> optF64()
    {
        const bool has = u8() != 0;
        if (!has) {
            return std::nullopt;
        }
        return f64();
    }
    std::optional<std::string> optStr()
    {
        const bool has = u8() != 0;
        if (!has) {
            return std::nullopt;
        }
        return str();
    }
    bool boolean()
    {
        return u8() != 0;
    }

    /// 尾部残余检查（残余字节＝编码/解码面不对齐——协议违约）。
    void expectEnd() const
    {
        if (m_pos != m_bytes.size()) {
            throw std::invalid_argument("组合校核解码：字节残余 "
                                        + std::to_string(m_bytes.size() - m_pos)
                                        + "（编码形态不对齐——协议违约）");
        }
    }

private:
    /// 越界保护（读取前调用——协议违约即 fail-fast，不静默截断）。
    void require(std::size_t n) const
    {
        if (m_pos + n > m_bytes.size()) {
            throw std::invalid_argument("组合校核解码：字节越界（需 "
                                        + std::to_string(n) + "，剩余 "
                                        + std::to_string(m_bytes.size() - m_pos) + "）");
        }
    }
    std::uint32_t u32At(std::size_t pos) const
    {
        std::uint32_t v = 0;
        std::memcpy(&v, m_bytes.data() + pos, sizeof(v));
        return v;
    }

    const std::vector<std::uint8_t>& m_bytes; ///< 源字节（调用方持有）
    std::size_t m_pos = 0;                    ///< 读游标
};

// ---- 各编码面的 codec 版本（随字段面演进递增并登记——CON-04 同源）----
constexpr std::uint32_t kCatalogLockCodecVersion = 1;   ///< IRDSLKV1
constexpr std::uint32_t kScreeningCodecVersion = 1;     ///< IRDSLBV1
constexpr std::uint32_t kAxisFactsCodecVersion = 1;     ///< IRDSAFV1
constexpr std::uint32_t kMappingBatchCodecVersion = 1;  ///< IRDSMBV1
constexpr std::uint32_t kCheckResultCodecVersion = 1;   ///< IRDSCCV1
constexpr std::uint32_t kDtComboSetCodecVersion = 1;    ///< IRDSDCV1

// ---- 逐结构编码/解码（私有面——公共包装在匿名空间外）----

void writeCatalogIdentity(ByteWriter& w, const CatalogIdentity& c)
{
    w.str(c.catalogId);
    w.str(c.version);
    w.digest32(c.contentIdentity);
    w.str(c.source);
}

CatalogIdentity readCatalogIdentity(ByteReader& r)
{
    CatalogIdentity c;
    c.catalogId = r.str();
    c.version = r.str();
    c.contentIdentity = r.digest32();
    c.source = r.str();
    return c;
}

void writeCatalogLockPayload(ByteWriter& w, const CatalogLockPayload& p)
{
    writeCatalogIdentity(w, p.identity);
    w.id128(p.lockObjectId);
}

CatalogLockPayload readCatalogLockPayload(ByteReader& r)
{
    CatalogLockPayload p;
    p.identity = readCatalogIdentity(r);
    p.lockObjectId = r.id128();
    return p;
}

void writeScreeningCriteria(ByteWriter& w, const ScreeningCriteria& c)
{
    // 逐字段按声明序（字段面冻结——新增字段只能表尾追加并递增 codec 版本）。
    w.f64(c.safetyFactor);
    w.str(c.requiredDutyClass);
    w.optF64(c.requiredVoltage);
    w.f64(c.voltageRelativeTolerance);
    w.optF64(c.ambientTemp);
    w.optF64(c.maxBacklash);
    w.optF64(c.requiredLife);
    w.optF64(c.minEfficiency);
    // ratioRange＝optional<结构>（两 double 展开）。
    w.u8(c.ratioRange.has_value() ? 1 : 0);
    if (c.ratioRange.has_value()) {
        w.f64(c.ratioRange->minRatio);
        w.f64(c.ratioRange->maxRatio);
    }
}

ScreeningCriteria readScreeningCriteria(ByteReader& r)
{
    ScreeningCriteria c;
    c.safetyFactor = r.f64();
    c.requiredDutyClass = r.str();
    c.requiredVoltage = r.optF64();
    c.voltageRelativeTolerance = r.f64();
    c.ambientTemp = r.optF64();
    c.maxBacklash = r.optF64();
    c.requiredLife = r.optF64();
    c.minEfficiency = r.optF64();
    if (r.u8() != 0) {
        RatioRange range;
        range.minRatio = r.f64();
        range.maxRatio = r.f64();
        c.ratioRange = range;
    }
    return c;
}

void writeAxisWorkpointFacts(ByteWriter& w, const AxisWorkpointFacts& f)
{
    w.id128(f.jointId);
    w.str(f.caseId);
    // 关节侧三量（DYN-03 口径——dynamics 上游值传递）。
    w.optF64(f.jointTorqueRms);
    w.optF64(f.jointTorquePeak);
    w.optF64(f.jointSpeedPeak);
    // 电机侧六量（dt.mapping 批工作点事实——组合校核段供给）。
    w.optF64(f.motorTorqueRms);
    w.optF64(f.motorTorquePeak);
    w.optF64(f.motorSpeedPeak);
    w.optF64(f.motorSpeedRms);
    w.optF64(f.motorPowerPeak);
    w.optF64(f.motorPowerRms);
    w.optF64(f.peakDuration);
    // 工况需求与轴侧事实。
    w.optF64(f.requiredHoldingTorque);
    w.u8(f.externalLoad.has_value() ? 1 : 0);
    if (f.externalLoad.has_value()) {
        w.f64(f.externalLoad->radial);
        w.f64(f.externalLoad->axial);
        w.f64(f.externalLoad->distance);
    }
    w.u8(f.mountRequirement.has_value() ? 1 : 0);
    if (f.mountRequirement.has_value()) {
        w.str(f.mountRequirement->flangeKind);
        w.str(f.mountRequirement->shaftKind);
        w.str(f.mountRequirement->orientation);
    }
    w.f64(f.atTime);
    w.str(f.segmentId);
}

AxisWorkpointFacts readAxisWorkpointFacts(ByteReader& r)
{
    AxisWorkpointFacts f;
    f.jointId = r.id128();
    f.caseId = r.str();
    f.jointTorqueRms = r.optF64();
    f.jointTorquePeak = r.optF64();
    f.jointSpeedPeak = r.optF64();
    f.motorTorqueRms = r.optF64();
    f.motorTorquePeak = r.optF64();
    f.motorSpeedPeak = r.optF64();
    f.motorSpeedRms = r.optF64();
    f.motorPowerPeak = r.optF64();
    f.motorPowerRms = r.optF64();
    f.peakDuration = r.optF64();
    f.requiredHoldingTorque = r.optF64();
    if (r.u8() != 0) {
        ExternalLoadFacts load;
        load.radial = r.f64();
        load.axial = r.f64();
        load.distance = r.f64();
        f.externalLoad = load;
    }
    if (r.u8() != 0) {
        JointMountRequirement mount;
        mount.flangeKind = r.str();
        mount.shaftKind = r.str();
        mount.orientation = r.str();
        f.mountRequirement = mount;
    }
    f.atTime = r.f64();
    f.segmentId = r.str();
    return f;
}

void writeDataGap(ByteWriter& w, const DataGap& g)
{
    w.str(g.dimension);
    w.str(g.detail);
    w.id128(g.axisId);
    w.str(g.caseId);
    w.str(g.diagCode);
}

DataGap readDataGap(ByteReader& r)
{
    DataGap g;
    g.dimension = r.str();
    g.detail = r.str();
    g.axisId = r.id128();
    g.caseId = r.str();
    g.diagCode = r.str();
    return g;
}

void writeRejectionReason(ByteWriter& w, const RejectionReason& reason)
{
    w.u8(static_cast<std::uint8_t>(reason.token));
    w.str(reason.candidateModelId);
    w.id128(reason.axisId);
    w.str(reason.caseId);
    w.f64(reason.atTime);
    w.str(reason.segmentId);
    w.f64(reason.actual);
    w.f64(reason.required);
    w.str(reason.unit);
    w.str(reason.thresholdSource);
    w.str(reason.actualText);
    w.str(reason.requiredText);
    writeCatalogIdentity(w, reason.catalog);
    w.digest32(reason.inputSliceId);
    w.digest32(reason.mappingId);
    w.str(reason.suggestion);
    w.optStr(reason.diagRef);
}

RejectionReason readRejectionReason(ByteReader& r)
{
    RejectionReason reason;
    reason.token = static_cast<ReasonToken>(r.u8());
    reason.candidateModelId = r.str();
    reason.axisId = r.id128();
    reason.caseId = r.str();
    reason.atTime = r.f64();
    reason.segmentId = r.str();
    reason.actual = r.f64();
    reason.required = r.f64();
    reason.unit = r.str();
    reason.thresholdSource = r.str();
    reason.actualText = r.str();
    reason.requiredText = r.str();
    reason.catalog = readCatalogIdentity(r);
    reason.inputSliceId = r.digest32();
    reason.mappingId = r.digest32();
    reason.suggestion = r.str();
    reason.diagRef = r.optStr();
    return reason;
}

void writeFeasibilityRecord(ByteWriter& w, const FeasibilityRecord& rec)
{
    w.str(rec.id);
    w.u8(static_cast<std::uint8_t>(rec.deviceKind));
    w.str(rec.candidateModelId);
    w.id128(rec.axisId);
    w.u8(static_cast<std::uint8_t>(rec.verdict));
    writeCatalogIdentity(w, rec.catalog);
    w.digest32(rec.inputSliceId);
    w.digest32(rec.mappingId);
    w.u32(static_cast<std::uint32_t>(rec.reasons.size()));
    for (const RejectionReason& reason : rec.reasons) {
        writeRejectionReason(w, reason);
    }
    w.u32(static_cast<std::uint32_t>(rec.gaps.size()));
    for (const DataGap& g : rec.gaps) {
        writeDataGap(w, g);
    }
}

FeasibilityRecord readFeasibilityRecord(ByteReader& r)
{
    FeasibilityRecord rec;
    rec.id = r.str();
    rec.deviceKind = static_cast<DeviceKind>(r.u8());
    rec.candidateModelId = r.str();
    rec.axisId = r.id128();
    rec.verdict = static_cast<VerdictKind>(r.u8());
    rec.catalog = readCatalogIdentity(r);
    rec.inputSliceId = r.digest32();
    rec.mappingId = r.digest32();
    const std::uint32_t reasonCount = r.u32();
    rec.reasons.reserve(reasonCount);
    for (std::uint32_t i = 0; i < reasonCount; ++i) {
        rec.reasons.push_back(readRejectionReason(r));
    }
    const std::uint32_t gapCount = r.u32();
    rec.gaps.reserve(gapCount);
    for (std::uint32_t i = 0; i < gapCount; ++i) {
        rec.gaps.push_back(readDataGap(r));
    }
    return rec;
}

void writeCaseCoverageEntry(ByteWriter& w, const CaseCoverageEntry& e)
{
    w.str(e.combinationId);
    w.str(e.caseId);
    w.u8(static_cast<std::uint8_t>(e.verdict));
    w.id128(e.axisId);
    w.f64(e.atTime);
    w.str(e.segmentId);
    w.str(e.note);
}

CaseCoverageEntry readCaseCoverageEntry(ByteReader& r)
{
    CaseCoverageEntry e;
    e.combinationId = r.str();
    e.caseId = r.str();
    e.verdict = static_cast<VerdictKind>(r.u8());
    e.axisId = r.id128();
    e.atTime = r.f64();
    e.segmentId = r.str();
    e.note = r.str();
    return e;
}

}  // namespace

// =====================================================================
// 组合身份与候选组合构造
// =====================================================================

DeviceCombinationId makeDeviceCombinationId(const CatalogIdentity& catalog,
                                            const std::vector<AxisDeviceAssignment>& axes)
{
    // 规范文本：magic＋目录键文本＋逐轴（轴 ID 规范文本＋电机＋减速器，
    // 长度前缀字段——无歧义序列化，NFR-COR-02 确定性）。
    std::string text = "IRDSCID1";
    text += catalogKeyText(catalog);
    const std::uint32_t axisCount = static_cast<std::uint32_t>(axes.size());
    text.append(reinterpret_cast<const char*>(&axisCount), sizeof(axisCount));
    for (const AxisDeviceAssignment& axis : axes) {
        const auto appendField = [&text](const std::string& s) {
            const std::uint32_t n = static_cast<std::uint32_t>(s.size());
            text.append(reinterpret_cast<const char*>(&n), sizeof(n));
            text.append(s);
        };
        appendField(axis.jointId.toCanonical());
        appendField(axis.motorModelId);
        appendField(axis.gearboxModelId);
    }
    return toHex(digestOf(text).bytes);
}

std::vector<CombinationDriveInput> makeCombinationDriveInputs(
    const CatalogPackageSnapshot& snapshot,
    const std::vector<DeviceCombination>& combinations,
    const std::vector<AxisDriveInput>& loadInertiaByJoint)
{
    if (snapshot.manifest.identity.contentIdentity.bytes
        != snapshot.contentIdentity.bytes) {
        // 防御性核对：清单身份与包身份不一致＝快照内部一致性破坏（T03
        // 装配契约前提）——构造语义破坏，fail-fast（NFR-COR-03 不静默）。
        throw std::invalid_argument(
            "候选传动参数构造：快照清单身份与包内容身份不一致（快照完整性破坏）");
    }

    // 负载惯量按轴键控索引（只消费 jointId＋loadInertiaJointSide 两字段
    // ——本参数是"逐轴参考值"的键控载体，非完整传动输入）。
    std::map<std::string, double> loadByJoint;
    for (const AxisDriveInput& load : loadInertiaByJoint) {
        if (load.loadInertiaJointSide.has_value()) {
            loadByJoint[load.jointId.toCanonical()] = *load.loadInertiaJointSide;
        }
    }

    std::vector<CombinationDriveInput> inputs;
    inputs.reserve(combinations.size());
    for (const DeviceCombination& combo : combinations) {
        if (!(combo.catalog == snapshot.manifest.identity)) {
            throw std::invalid_argument(
                "候选传动参数构造：组合 «" + combo.id + "» 的目录身份与快照不一致"
                "（候选参数来源版本错配——构造语义破坏，fail-fast）");
        }
        CombinationDriveInput in;
        in.combinationId = combo.id;
        in.axes.reserve(combo.axes.size());
        for (const AxisDeviceAssignment& axis : combo.axes) {
            const MotorCatalogEntry* motor = findMotor(snapshot, axis.motorModelId);
            const GearboxCatalogEntry* gearbox = findGearbox(snapshot, axis.gearboxModelId);
            if (motor == nullptr || gearbox == nullptr) {
                throw std::invalid_argument(
                    "候选传动参数构造：组合 «" + combo.id + "» 的候选型号在快照内缺失（"
                    + axis.motorModelId + " / " + axis.gearboxModelId + "）");
            }
            AxisDriveInput a;
            a.jointId = axis.jointId;
            // ★ 唯一的传动比换算（SEL-05 允许的候选传动参数构造，单点）：
            //   减速器速比 n:1 → c＝Δq_joint/Δθ_motor＝1/n（drivetrain 卡
            //   §5.3 c 口径——禁第二种未声明约定）。
            if (!(gearbox->ratio > 0.0) || !isFiniteNumber(gearbox->ratio)) {
                throw std::invalid_argument(
                    "候选传动参数构造：减速器 «" + gearbox->modelId + "» 速比非法（须>0"
                    " 且有限——目录导入期 RANGE 校验前提）");
            }
            a.ratioC = 1.0 / gearbox->ratio;
            // 目录 v1 单一 efficiency 列 → η⁺＝η⁻＝目录值（分方向扩展随
            // 目录 schema 演进——卡 §4.1）。
            if (!(gearbox->efficiency > 0.0) || gearbox->efficiency > 1.0
                || !isFiniteNumber(gearbox->efficiency)) {
                throw std::invalid_argument(
                    "候选传动参数构造：减速器 «" + gearbox->modelId + "» 效率非法"
                    "（须∈(0,1]）");
            }
            a.etaForward = gearbox->efficiency;
            a.etaBackward = gearbox->efficiency;
            if (!(motor->rotorInertia > 0.0) || !isFiniteNumber(motor->rotorInertia)) {
                throw std::invalid_argument(
                    "候选传动参数构造：电机 «" + motor->modelId + "» 转子惯量非法"
                    "（须>0 且有限——kg·m²）");
            }
            a.rotorInertia = motor->rotorInertia;
            // 负载惯量参考值：键控查找；缺键＝nullopt 显式缺失（不默认零
            // ——§11.2"不将缺少证据默认当作零负载"）。
            const auto it = loadByJoint.find(axis.jointId.toCanonical());
            if (it != loadByJoint.end()) {
                a.loadInertiaJointSide = it->second;
            }
            in.axes.push_back(std::move(a));
        }
        inputs.push_back(std::move(in));
    }
    return inputs;
}

CombinationSet DeviceCombinationBuilder::build(
    const std::vector<AxisCandidateList>& perAxis,
    const CompatibilityTable& compat,
    const BatchBudget& budget,
    const CatalogIdentity& catalog) const
{
    // ---- 调用方契约校验（fail-fast——卡 §14.5 @throws 注）。
    if (perAxis.empty()) {
        throw std::invalid_argument("组合构造：perAxis 为空（至少一轴）");
    }
    if (budget.maxCombinationsPerBatch == 0) {
        throw std::invalid_argument("组合构造：批预算须 ≥1（0 为保留值）");
    }
    {
        std::set<std::string> seen;
        for (const AxisCandidateList& axis : perAxis) {
            if (!seen.insert(axis.jointId.toCanonical()).second) {
                throw std::invalid_argument(
                    "组合构造：同一轴重复出现（«" + axis.jointId.toCanonical()
                    + "»——组合轴表必须逐轴恰一指派，卡 §9.3 行 2 构造侧防线）");
            }
        }
    }

    // ---- 兼容对索引（型号对 → 存在；构造过滤 O(1) 查询——兼容表可含
    // 同一对多 mountKind 行，对存在性判定不受影响）。
    std::set<std::pair<std::string, std::string>> compatPairs;
    for (const CompatibilityRecord& r : compat) {
        compatPairs.emplace(r.motorId, r.gearboxId);
    }

    // ---- 候选去重（保持首现序——"电机候选首现序×减速器候选首现序"的
    // 展开序前提；重复候选不重复展开组合）。
    std::vector<std::pair<core::ObjectId, std::vector<ModelId>>> motorsPerAxis;
    std::vector<std::pair<core::ObjectId, std::vector<ModelId>>> gearboxesPerAxis;
    motorsPerAxis.reserve(perAxis.size());
    gearboxesPerAxis.reserve(perAxis.size());
    for (const AxisCandidateList& axis : perAxis) {
        std::vector<ModelId> motors;
        std::vector<ModelId> gearboxes;
        for (const ModelId& m : axis.motorIds) {
            if (std::find(motors.begin(), motors.end(), m) == motors.end()) {
                motors.push_back(m);
            }
        }
        for (const ModelId& g : axis.gearboxIds) {
            if (std::find(gearboxes.begin(), gearboxes.end(), g) == gearboxes.end()) {
                gearboxes.push_back(g);
            }
        }
        motorsPerAxis.emplace_back(axis.jointId, std::move(motors));
        gearboxesPerAxis.emplace_back(axis.jointId, std::move(gearboxes));
    }

    // ---- 里程表展开（每轴选择＝电机数×减速数；轴 0 为字典序主序——
    // 最慢变化。溢出保护：总组合数以 size_t 逐轴累乘，上限保护防溢出）。
    std::vector<std::size_t> radix(perAxis.size(), 0);
    std::size_t total = 1;
    bool overflow = false;
    for (std::size_t a = 0; a < perAxis.size(); ++a) {
        radix[a] = motorsPerAxis[a].second.size() * gearboxesPerAxis[a].second.size();
        if (radix[a] == 0 || total > std::numeric_limits<std::size_t>::max() / radix[a]) {
            // 某轴零候选（空组合面——合法：该轴无任何兼容对，组合集为空）
            // 或累乘溢出——零候选直接得空集；溢出按规模红线拒绝（R-SEL-2
            // 规模爆炸防线——NFR-PERF-03 分批的上游是构造本身不可爆炸）。
            if (radix[a] == 0) {
                CombinationSet emptySet;
                return emptySet;
            }
            overflow = true;
            break;
        }
        total *= radix[a];
    }
    if (overflow) {
        throw std::invalid_argument("组合构造：候选组合规模溢出（轴候选笛卡尔积"
                                    "超过地址空间——R-SEL-2 规模防线，须缩小候选集）");
    }

    CombinationSet out;
    std::set<DeviceCombinationId> seenIds;
    std::vector<std::size_t> choice(perAxis.size(), 0);
    for (std::size_t n = 0; n < total; ++n) {
        // 由里程表索引还原本组合的逐轴 (motor, gearbox) 选择。
        std::vector<AxisDeviceAssignment> axes;
        axes.reserve(perAxis.size());
        bool allCompatible = true;
        for (std::size_t a = 0; a < perAxis.size(); ++a) {
            const std::size_t gCount = gearboxesPerAxis[a].second.size();
            const std::size_t motorIdx = choice[a] / gCount;
            const std::size_t gearboxIdx = choice[a] % gCount;
            const ModelId& motorId = motorsPerAxis[a].second[motorIdx];
            const ModelId& gearboxId = gearboxesPerAxis[a].second[gearboxIdx];
            // 兼容过滤（卡 §14.5"兼容性过滤（compatibility 表）"——无记录
            // 的对不生成组合；零行语义＝无预声明兼容对，卡 §5.2）。
            if (compatPairs.count({motorId, gearboxId}) == 0) {
                allCompatible = false;
                break;
            }
            AxisDeviceAssignment assign;
            assign.jointId = perAxis[a].jointId;
            assign.motorModelId = motorId;
            assign.gearboxModelId = gearboxId;
            axes.push_back(std::move(assign));
        }
        if (allCompatible) {
            DeviceCombination combo;
            combo.catalog = catalog;
            combo.axes = std::move(axes);
            combo.id = makeDeviceCombinationId(combo.catalog, combo.axes);
            // 去重（卡 §9.5"重复组合去重（同组合键只算一次）"——不同候选
            // 路径展开出同键组合时只保留首个，计数观测面）。
            if (seenIds.insert(combo.id).second) {
                out.combinations.push_back(std::move(combo));
            } else {
                ++out.duplicateDroppedCount;
            }
        }
        // 里程表进位（末轴最快——轴 0 主序；全 0 起步，进位溢出由循环
        // 上界 total 保证不发生）。
        for (std::size_t a = perAxis.size(); a-- > 0;) {
            ++choice[a];
            if (choice[a] < radix[a]) {
                break;
            }
            choice[a] = 0;
        }
    }

    // ---- 批切分（全局序切片——批边界不改变组合内容与顺序，§10.4）。
    const std::size_t count = out.combinations.size();
    for (std::size_t begin = 0, batchIndex = 0; begin < count
         || (count == 0 && batchIndex == 0); begin += budget.maxCombinationsPerBatch, ++batchIndex) {
        CombinationBatch batch;
        batch.batchIndex = batchIndex;
        const std::size_t end = std::min(begin + budget.maxCombinationsPerBatch, count);
        batch.combinations.assign(out.combinations.begin() + static_cast<std::ptrdiff_t>(begin),
                                  out.combinations.begin() + static_cast<std::ptrdiff_t>(end));
        out.batches.push_back(std::move(batch));
        if (count == 0) {
            break;  // 空组合集保留一个空批（批视图与全量的对应关系恒成立）。
        }
    }
    return out;
}

// =====================================================================
// canonical 编解码（公共面——协议见 Combination.hpp §codec 注）
// =====================================================================

core::ObjectId selUpstreamAnchor(const core::ContentIdentity& upstreamSliceId)
{
    if (!upstreamSliceId.isValid()) {
        throw std::invalid_argument(
            "selUpstreamAnchor：上游切片身份为全零保留值（物化锚必须有身份前提）");
    }
    // 派生规则：ObjectId.bytes＝切片身份前 16 字节（确定性、无随机——
    // 规则单点在本函数，drivetrain jointSeriesAnchor 的域内对位实现；
    // 两域锚空间按域隔离，禁跨域混用——Combination.hpp AxisFactsBundle 注）。
    core::ObjectId anchor;
    std::copy(upstreamSliceId.bytes.begin(),
              upstreamSliceId.bytes.begin() + static_cast<std::ptrdiff_t>(anchor.bytes.size()),
              anchor.bytes.begin());
    return anchor;
}

std::vector<std::uint8_t> encodeCatalogLockPayload(const CatalogLockPayload& payload)
{
    ByteWriter w;
    w.raw("IRDSLKV1", 8);
    w.u32(kCatalogLockCodecVersion);
    writeCatalogLockPayload(w, payload);
    return std::move(w.m_bytes);
}

CatalogLockPayload decodeCatalogLockPayload(const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSLKV1", kCatalogLockCodecVersion);
    CatalogLockPayload p = readCatalogLockPayload(r);
    r.expectEnd();
    return p;
}

std::vector<std::uint8_t> encodeScreeningCriteria(const ScreeningCriteria& criteria)
{
    // 编码入口有限性校验（NFR-COR-03：非有限不进入身份与持久化链）。
    requireFiniteIfPresent(criteria.requiredVoltage, "requiredVoltage");
    requireFiniteIfPresent(criteria.ambientTemp, "ambientTemp");
    requireFiniteIfPresent(criteria.maxBacklash, "maxBacklash");
    requireFiniteIfPresent(criteria.requiredLife, "requiredLife");
    requireFiniteIfPresent(criteria.minEfficiency, "minEfficiency");
    ByteWriter w;
    w.raw("IRDSLBV1", 8);
    w.u32(kScreeningCodecVersion);
    writeScreeningCriteria(w, criteria);
    return std::move(w.m_bytes);
}

ScreeningCriteria decodeScreeningCriteria(const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSLBV1", kScreeningCodecVersion);
    ScreeningCriteria c = readScreeningCriteria(r);
    r.expectEnd();
    return c;
}

std::vector<std::uint8_t> encodeAxisFactsBundle(const AxisFactsBundle& bundle)
{
    ByteWriter w;
    w.raw("IRDSAFV1", 8);
    w.u32(kAxisFactsCodecVersion);
    w.u32(static_cast<std::uint32_t>(bundle.size()));
    for (const AxisWorkpointFacts& f : bundle) {
        writeAxisWorkpointFacts(w, f);
    }
    return std::move(w.m_bytes);
}

AxisFactsBundle decodeAxisFactsBundle(const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSAFV1", kAxisFactsCodecVersion);
    const std::uint32_t n = r.u32();
    AxisFactsBundle bundle;
    bundle.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        bundle.push_back(readAxisWorkpointFacts(r));
    }
    r.expectEnd();
    return bundle;
}

std::vector<std::uint8_t> encodeMappingBatchFacts(const MappingBatchFacts& facts)
{
    ByteWriter w;
    w.raw("IRDSMBV1", 8);
    w.u32(kMappingBatchCodecVersion);
    w.u32(facts.mappingContractVersion);
    w.u32(facts.mappingAlgorithmVersion);
    w.digest32(facts.upstreamSliceId);
    w.u8(static_cast<std::uint8_t>(facts.completeness));
    w.boolean(facts.mappingFailed);
    w.u32(static_cast<std::uint32_t>(facts.missingItems.size()));
    for (const std::string& s : facts.missingItems) {
        w.str(s);
    }
    w.u32(static_cast<std::uint32_t>(facts.diagnosticCodes.size()));
    for (const std::string& s : facts.diagnosticCodes) {
        w.str(s);
    }
    // 组合指派表（管线②自足性——组合校核的遍历序与轴×候选定位来源；
    // 在逐轴事实之前编码，解码序对称）。
    w.u32(static_cast<std::uint32_t>(facts.combinations.size()));
    for (const MappingCombinationFact& c : facts.combinations) {
        w.str(c.combinationId);
        writeCatalogIdentity(w, c.catalog);
        w.u32(static_cast<std::uint32_t>(c.axes.size()));
        for (const AxisDeviceAssignment& a : c.axes) {
            w.id128(a.jointId);
            w.str(a.motorModelId);
            w.str(a.gearboxModelId);
        }
    }
    w.u32(static_cast<std::uint32_t>(facts.axes.size()));
    for (const MappingAxisFact& a : facts.axes) {
        w.str(a.combinationId);
        w.id128(a.jointId);
        w.str(a.caseId);
        // 电机侧工作点六量＋峰值段时长（组合口径映射事实——§11.1 消费口径）。
        w.optF64(a.motorTorqueRms);
        w.optF64(a.motorTorquePeak);
        w.optF64(a.motorSpeedPeak);
        w.optF64(a.motorSpeedRms);
        w.optF64(a.motorPowerPeak);
        w.optF64(a.motorPowerRms);
        w.optF64(a.peakDuration);
        w.f64(a.peakAtTime);
        w.str(a.peakSegmentId);
        // 惯量与效率事实。
        w.optF64(a.inertiaRatio);
        w.optF64(a.reflectedInertia);
        w.boolean(a.efficiencyApplied);
    }
    return std::move(w.m_bytes);
}

MappingBatchFacts decodeMappingBatchFacts(const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSMBV1", kMappingBatchCodecVersion);
    MappingBatchFacts facts;
    facts.mappingContractVersion = r.u32();
    facts.mappingAlgorithmVersion = r.u32();
    facts.upstreamSliceId = r.digest32();
    facts.completeness = static_cast<CompletenessKind>(r.u8());
    facts.mappingFailed = r.boolean();
    const std::uint32_t missingCount = r.u32();
    facts.missingItems.reserve(missingCount);
    for (std::uint32_t i = 0; i < missingCount; ++i) {
        facts.missingItems.push_back(r.str());
    }
    const std::uint32_t diagCount = r.u32();
    facts.diagnosticCodes.reserve(diagCount);
    for (std::uint32_t i = 0; i < diagCount; ++i) {
        facts.diagnosticCodes.push_back(r.str());
    }
    const std::uint32_t comboCount = r.u32();
    facts.combinations.reserve(comboCount);
    for (std::uint32_t i = 0; i < comboCount; ++i) {
        MappingCombinationFact c;
        c.combinationId = r.str();
        c.catalog = readCatalogIdentity(r);
        const std::uint32_t assignCount = r.u32();
        c.axes.reserve(assignCount);
        for (std::uint32_t j = 0; j < assignCount; ++j) {
            AxisDeviceAssignment a;
            a.jointId = r.id128();
            a.motorModelId = r.str();
            a.gearboxModelId = r.str();
            c.axes.push_back(std::move(a));
        }
        facts.combinations.push_back(std::move(c));
    }
    const std::uint32_t axisCount = r.u32();
    facts.axes.reserve(axisCount);
    for (std::uint32_t i = 0; i < axisCount; ++i) {
        MappingAxisFact a;
        a.combinationId = r.str();
        a.jointId = r.id128();
        a.caseId = r.str();
        a.motorTorqueRms = r.optF64();
        a.motorTorquePeak = r.optF64();
        a.motorSpeedPeak = r.optF64();
        a.motorSpeedRms = r.optF64();
        a.motorPowerPeak = r.optF64();
        a.motorPowerRms = r.optF64();
        a.peakDuration = r.optF64();
        a.peakAtTime = r.f64();
        a.peakSegmentId = r.str();
        a.inertiaRatio = r.optF64();
        a.reflectedInertia = r.optF64();
        a.efficiencyApplied = r.boolean();
        facts.axes.push_back(std::move(a));
    }
    r.expectEnd();
    return facts;
}

std::vector<std::uint8_t> encodeSelectionCheckResult(const SelectionCheckResult& result)
{
    ByteWriter w;
    w.raw("IRDSCCV1", 8);
    w.u32(kCheckResultCodecVersion);
    w.u32(static_cast<std::uint32_t>(result.records.size()));
    for (const FeasibilityRecord& rec : result.records) {
        writeFeasibilityRecord(w, rec);
    }
    w.u32(static_cast<std::uint32_t>(result.coverage.size()));
    for (const CaseCoverageEntry& e : result.coverage) {
        writeCaseCoverageEntry(w, e);
    }
    writeCatalogIdentity(w, result.catalog);
    w.digest32(result.mappingSliceId);
    w.digest32(result.inputSliceId);
    w.u8(static_cast<std::uint8_t>(result.completeness));
    w.u32(static_cast<std::uint32_t>(result.missingItems.size()));
    for (const std::string& s : result.missingItems) {
        w.str(s);
    }
    w.u32(static_cast<std::uint32_t>(result.diagnosticCodes.size()));
    for (const std::string& s : result.diagnosticCodes) {
        w.str(s);
    }
    return std::move(w.m_bytes);
}

SelectionCheckResult decodeSelectionCheckResult(const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSCCV1", kCheckResultCodecVersion);
    SelectionCheckResult result;
    const std::uint32_t recordCount = r.u32();
    result.records.reserve(recordCount);
    for (std::uint32_t i = 0; i < recordCount; ++i) {
        result.records.push_back(readFeasibilityRecord(r));
    }
    const std::uint32_t coverageCount = r.u32();
    result.coverage.reserve(coverageCount);
    for (std::uint32_t i = 0; i < coverageCount; ++i) {
        result.coverage.push_back(readCaseCoverageEntry(r));
    }
    result.catalog = readCatalogIdentity(r);
    result.mappingSliceId = r.digest32();
    result.inputSliceId = r.digest32();
    result.completeness = static_cast<CompletenessKind>(r.u8());
    const std::uint32_t missingCount = r.u32();
    result.missingItems.reserve(missingCount);
    for (std::uint32_t i = 0; i < missingCount; ++i) {
        result.missingItems.push_back(r.str());
    }
    const std::uint32_t diagCount = r.u32();
    result.diagnosticCodes.reserve(diagCount);
    for (std::uint32_t i = 0; i < diagCount; ++i) {
        result.diagnosticCodes.push_back(r.str());
    }
    r.expectEnd();
    return result;
}

std::vector<std::uint8_t> encodeDtComboSetPayload(
    const std::vector<CombinationDriveInput>& inputs)
{
    ByteWriter w;
    w.raw("IRDSDCV1", 8);
    w.u32(kDtComboSetCodecVersion);
    w.u32(static_cast<std::uint32_t>(inputs.size()));
    for (const CombinationDriveInput& in : inputs) {
        w.str(in.combinationId);
        w.u32(static_cast<std::uint32_t>(in.axes.size()));
        for (const AxisDriveInput& a : in.axes) {
            w.id128(a.jointId);
            // 编码入口有限性校验（NFR-COR-03；负载惯量 optional 缺省合法）。
            if (!isFiniteNumber(a.ratioC) || !isFiniteNumber(a.etaForward)
                || !isFiniteNumber(a.etaBackward) || !isFiniteNumber(a.rotorInertia)) {
                throw std::invalid_argument(
                    "组合集载荷编码：传动输入含非有限数值（NFR-COR-03 拒绝）");
            }
            w.f64(a.ratioC);
            w.f64(a.etaForward);
            w.f64(a.etaBackward);
            w.f64(a.rotorInertia);
            w.optF64(a.loadInertiaJointSide);
        }
    }
    return std::move(w.m_bytes);
}

std::vector<CombinationDriveInput> decodeDtComboSetPayload(
    const std::vector<std::uint8_t>& bytes)
{
    ByteReader r(bytes);
    r.expectHeader("IRDSDCV1", kDtComboSetCodecVersion);
    const std::uint32_t comboCount = r.u32();
    std::vector<CombinationDriveInput> inputs;
    inputs.reserve(comboCount);
    for (std::uint32_t i = 0; i < comboCount; ++i) {
        CombinationDriveInput in;
        in.combinationId = r.str();
        const std::uint32_t axisCount = r.u32();
        in.axes.reserve(axisCount);
        for (std::uint32_t a = 0; a < axisCount; ++a) {
            AxisDriveInput axis;
            axis.jointId = r.id128();
            axis.ratioC = r.f64();
            axis.etaForward = r.f64();
            axis.etaBackward = r.f64();
            axis.rotorInertia = r.f64();
            axis.loadInertiaJointSide = r.optF64();
            in.axes.push_back(std::move(axis));
        }
        inputs.push_back(std::move(in));
    }
    r.expectEnd();
    return inputs;
}

// =====================================================================
// 组合校核核心
// =====================================================================

namespace {

/// 筛选条件合法性的核心入口校验（与 T04 HardConstraintSelector 同语义
/// ——调用方契约违约 fail-fast；轴数为 0 时筛选器不触发，故在此显式）。
void requireValidCriteria(const ScreeningCriteria& c)
{
    if (!isFiniteNumber(c.safetyFactor) || c.safetyFactor < 1.0) {
        throw std::invalid_argument("组合校核：安全系数须 ≥1 且有限");
    }
    if (!isFiniteNumber(c.voltageRelativeTolerance) || c.voltageRelativeTolerance < 0.0) {
        throw std::invalid_argument("组合校核：电压容差须 ≥0 且有限");
    }
    if (c.minEfficiency.has_value()
        && (!isFiniteNumber(*c.minEfficiency) || *c.minEfficiency <= 0.0
            || *c.minEfficiency > 1.0)) {
        throw std::invalid_argument("组合校核：最低效率须∈(0,1]");
    }
    requireFiniteIfPresent(c.requiredVoltage, "requiredVoltage");
    requireFiniteIfPresent(c.ambientTemp, "ambientTemp");
    requireFiniteIfPresent(c.maxBacklash, "maxBacklash");
    requireFiniteIfPresent(c.requiredLife, "requiredLife");
    if (c.ratioRange.has_value()) {
        const RatioRange& r = *c.ratioRange;
        if (!isFiniteNumber(r.minRatio) || !isFiniteNumber(r.maxRatio)
            || r.minRatio <= 0.0 || r.minRatio > r.maxRatio) {
            throw std::invalid_argument("组合校核：速比范围须 0<min≤max 且有限");
        }
    }
}

/// 单条组合级原因的快捷构造（组合级与工况无关——caseId 空/atTime=0；
/// ERR-01 字段齐备：阈值来源与建议动作必填）。
RejectionReason makeComboReason(ReasonToken token, const CatalogIdentity& catalog,
                                const core::ContentIdentity& inputSliceId,
                                const core::ContentIdentity& mappingId,
                                ModelId candidateModelId, core::ObjectId axisId,
                                std::string actualText, std::string requiredText,
                                std::string thresholdSource, std::string suggestion)
{
    RejectionReason r;
    r.token = token;
    r.candidateModelId = std::move(candidateModelId);
    r.axisId = std::move(axisId);
    r.actualText = std::move(actualText);
    r.requiredText = std::move(requiredText);
    r.thresholdSource = std::move(thresholdSource);
    r.catalog = catalog;
    r.inputSliceId = inputSliceId;
    r.mappingId = mappingId;
    r.suggestion = std::move(suggestion);
    // diagRef 恒 nullopt（T04 口径——逐 token 稳定码映射随 WP-19-T06 注册；
    // 本任务不预建，DiagCodes.hpp 头注登记口径）。
    return r;
}

/// 原因合并后的稳定排序（卡 §10.4"淘汰原因稳定排序：reasonToken 词表序
/// →工况 ID→时刻"；stable_sort 保留同键子项执行序——T04 细化 ⑦ 同款）。
void sortReasons(std::vector<RejectionReason>& reasons)
{
    std::stable_sort(reasons.begin(), reasons.end(),
                     [](const RejectionReason& a, const RejectionReason& b) {
                         if (a.token != b.token) {
                             return a.token < b.token;
                         }
                         if (a.caseId != b.caseId) {
                             return a.caseId < b.caseId;
                         }
                         return a.atTime < b.atTime;
                     });
}

/// 单格判定聚合（该格全部轴级筛选记录 → 格 verdict 与定位面；覆盖全部
/// 轴——任一轴失败即格失败，卡 §9.4 矩阵语义）。
void aggregateCaseCell(const std::vector<FeasibilityRecord>& records,
                       CaseCoverageEntry& cell)
{
    // 原因优先：任一记录存在淘汰原因 → 格 Fail（定位＝首条原因的工作点
    // ——卡 §9.4"每格判定绑定工作点时间/轨迹段/工况 ID"）。
    for (const FeasibilityRecord& axisRec : records) {
        if (axisRec.verdict == VerdictKind::Rejected && !axisRec.reasons.empty()) {
            cell.verdict = VerdictKind::Rejected;
            const RejectionReason& first = axisRec.reasons.front();
            cell.axisId = first.axisId;
            cell.atTime = first.atTime;
            cell.segmentId = first.segmentId;
            cell.note = std::string(reasonTokenText(first.token));
            return;
        }
    }
    // 缺口次之：任一记录存在数据缺口 → 格 DataInsufficient（不默认通过
    // ——卡 §7.2；缺口明细已在记录 gaps 全量列出）。
    for (const FeasibilityRecord& axisRec : records) {
        if (!axisRec.gaps.empty()) {
            cell.verdict = VerdictKind::DataInsufficient;
            cell.note = "data-gap";
            return;
        }
    }
    cell.verdict = VerdictKind::Feasible;
    cell.note = "pass";
}

}  // namespace

std::vector<CombinationCheckOutcome> checkCombinations(
    const CombinationCheckCoreInput& input,
    const evidence::IEvaluationContext* ctx)
{
    // ---- 调用方契约校验（fail-fast——校验边界快速拒绝，卡 §10.2）。
    if (input.snapshot == nullptr) {
        throw std::invalid_argument("组合校核：快照指针为空（调用方契约违约）");
    }
    const CatalogPackageSnapshot& snapshot = *input.snapshot;
    requireValidCriteria(input.criteria);
    if (input.inertiaRule.referenceMaxRatio.has_value()
        && (!isFiniteNumber(*input.inertiaRule.referenceMaxRatio)
            || *input.inertiaRule.referenceMaxRatio <= 0.0)) {
        throw std::invalid_argument(
            "组合校核：惯量比参考阈值须>0 且有限（无量纲——供给即调用方契约）");
    }
    // 映射批版本字段：有供给时必须非零（0＝未登记非法；映射批整体未供给
    // 的"缺失"不是非法——逐组合按上游缺口处理，见主循环 ⑥）。
    const bool mappingBatchSupplied = input.mappingBatch.mappingContractVersion != 0
                                   || input.mappingBatch.mappingAlgorithmVersion != 0;
    if (mappingBatchSupplied
        && (input.mappingBatch.mappingContractVersion == 0
            || input.mappingBatch.mappingAlgorithmVersion == 0)) {
        throw std::invalid_argument(
            "组合校核：映射批契约/算法版本为 0（未登记非法——调用方契约违约）");
    }
    // 映射批 Partial 纪律：缺失清单必非空（不伪造完整——§10.2 空集语义表）。
    if (input.mappingBatch.completeness == CompletenessKind::Partial
        && input.mappingBatch.missingItems.empty()) {
        throw std::invalid_argument(
            "组合校核：映射批声明 Partial 但缺失清单为空（完整性状态与素材矛盾"
            "——调用方契约违约）");
    }

    // ---- 事实轴集与工况集（首现序——确定性；必验工况集＝事实已覆盖的
    // 工况集，覆盖缺口核对归 evidence 汇总〔EVI-02 跨批次〕——登记边界）。
    std::vector<core::ObjectId> factAxes;
    std::vector<CaseId> caseOrder;
    {
        std::set<std::string> axisSeen;
        std::set<std::string> caseSeen;
        for (const AxisWorkpointFacts& f : input.axisFacts) {
            if (axisSeen.insert(f.jointId.toCanonical()).second) {
                factAxes.push_back(f.jointId);
            }
            if (caseSeen.insert(f.caseId).second) {
                caseOrder.push_back(f.caseId);
            }
        }
    }

    // ---- 兼容对索引（型号对存在性 O(log N) 查询——兼容表可含同一对多
    // mountKind 行，对存在性判定不受影响；快照兼容表为只读输入）。
    std::set<std::pair<std::string, std::string>> compatPairs;
    for (const CompatibilityRecord& r : snapshot.compatibility) {
        compatPairs.emplace(r.motorId, r.gearboxId);
    }

    // ---- 轴级筛选的执行器（T04 HardConstraintSelector 复用——同一实现
    // 不分叉，§7/§8 全维度的黄金表已由 T04 测试钉住）。★ 组合口径纪律：
    // 电机侧工作点依赖组合（不同 c ⇒ 不同电机侧值），筛选调用按
    // (组合×轴×工况) 执行——每次供给该组合口径的单条 facts（T04 筛选器
    // 纯函数语义：调用方供给什么就判什么）；关节侧＋需求侧取自
    // axisFactsBundle（与组合无关），电机侧取自映射批（组合口径权威）。
    const HardConstraintSelector selector;

    // ---- 主循环：逐组合校核（维度序＝卡 §9.3 清单行序；候选能力维度
    // 不短路——全部独立维度执行完毕才汇总）。遍历序＝映射批 combinations
    // 表序（组合指派表的唯一来源——P-SEL-1 提议契约 v1；直调路径由调用
    // 方组装同一表，语义不分叉）。
    std::vector<CombinationCheckOutcome> outcomes;
    outcomes.reserve(input.mappingBatch.combinations.size());
    for (const MappingCombinationFact& combo : input.mappingBatch.combinations) {
        // 组合边界取消查询（观测到取消即停止——已完成前缀返回，§13.4
        // "取消不发布完整可行集"；selection 域未登记取消诊断码，截断由
        // 产出数感知〔调用方对比组合数〕，单元卡登记边界）。
        if (ctx != nullptr && ctx->cancellationRequested()) {
            break;
        }

        CombinationCheckOutcome outcome;
        FeasibilityRecord& rec = outcome.record;
        rec.id = combo.combinationId;
        rec.deviceKind = DeviceKind::Combination;  // T05 表尾追加值（T04 细化）。
        // 组合级身份承载约定（T05 落位细化）：无单候选/单轴——空串＋全零；
        // 逐候选定位在各 RejectionReason.candidateModelId/axisId。
        rec.candidateModelId = ModelId{};
        rec.catalog = snapshot.manifest.identity;  // 判定所用快照版本（追溯面）。
        rec.inputSliceId = input.inputSliceId;     // 评估器路径回填（T04 头注边界 4）。
        rec.mappingId = input.mappingBatch.upstreamSliceId;  // 映射身份（直调无映射＝全零）。

        // ①目录版本一致性（卡 §9.3 行 10）：组合所用目录 ≠ 判定快照 →
        // CatalogVersionIncompatible（逐项定位；不整批短路——其余维度
        // 仍在同一快照上继续执行并如实产出）。
        if (!(combo.catalog == snapshot.manifest.identity)) {
            rec.reasons.push_back(makeComboReason(
                ReasonToken::CatalogVersionIncompatible, rec.catalog,
                rec.inputSliceId, rec.mappingId, ModelId{}, core::ObjectId{},
                combo.catalog.catalogId + "|" + combo.catalog.version,
                snapshot.manifest.identity.catalogId + "|"
                    + snapshot.manifest.identity.version,
                "组合目录身份 vs 目录锁定快照",
                "以锁定快照版本重新构造候选组合后重算（SEL-IDENTITY-MISMATCH 语义面）"));
        }

        // ②组合兼容（卡 §9.3 行 1）：任一轴 (motor, gearbox) 在兼容表无
        // 记录 → ComboIncompatible（构造面已过滤的双保险核对——零行语义）。
        for (const AxisDeviceAssignment& axis : combo.axes) {
            if (compatPairs.count({axis.motorModelId, axis.gearboxModelId}) == 0) {
                rec.reasons.push_back(makeComboReason(
                    ReasonToken::ComboIncompatible, rec.catalog, rec.inputSliceId,
                    rec.mappingId, axis.motorModelId, axis.jointId,
                    axis.motorModelId + "+" + axis.gearboxModelId,
                    "compatibility 表存在该型号对记录",
                    "compatibility.csv",
                    "更换兼容表中登记的电机/减速器配对（无记录即不兼容——SEL-COMBO-INCOMPATIBLE 语义面）"));
            }
        }

        // ③轴映射完整性（卡 §9.3 行 2）：事实轴集 ⊄ 组合轴集（缺轴）→
        // AxisMappingIncomplete（漏轴即本语义——每轴恰一组合的前提）。
        {
            std::set<std::string> comboAxisKeys;
            for (const AxisDeviceAssignment& a : combo.axes) {
                comboAxisKeys.insert(a.jointId.toCanonical());
            }
            std::size_t missing = 0;
            std::string missingText;
            for (const core::ObjectId& axis : factAxes) {
                if (comboAxisKeys.count(axis.toCanonical()) == 0) {
                    ++missing;
                    if (!missingText.empty()) {
                        missingText += ", ";
                    }
                    missingText += axis.toCanonical();
                }
            }
            if (missing > 0) {
                rec.reasons.push_back(makeComboReason(
                    ReasonToken::AxisMappingIncomplete, rec.catalog, rec.inputSliceId,
                    rec.mappingId, ModelId{}, core::ObjectId{},
                    "缺轴 " + std::to_string(missing) + " 根（" + missingText + "）",
                    "每轴恰一指派",
                    "组合轴表 vs 工作点事实轴集",
                    "补全组合轴表覆盖全部工作点轴后重算（SEL-COMBO-AXIS-MAPPING-INCOMPLETE 语义面）"));
            }
        }

        // ④~⑤ 轴级能力判定装配＋惯量比维度（逐工况分格——资格矩阵）。
        outcome.coverage.reserve(caseOrder.size());
        std::size_t unsettledAxes = 0;   // 惯量比未判定轴计数（O-11 显式标记面）。
        for (const CaseId& caseId : caseOrder) {
            CaseCoverageEntry cell;
            cell.combinationId = combo.combinationId;
            cell.caseId = caseId;

            // 惯量比维度（§11.3）：逐轴三态——未判定（规则未配置）/数据
            // 缺口（映射事实缺失）/参考判定（配置且有值；不产生淘汰原因）。
            bool cellHasInertiaGap = false;
            for (const AxisDeviceAssignment& axis : combo.axes) {
                if (input.inertiaRule.referenceMaxRatio.has_value()) {
                    // 查映射事实（组合×轴对位；缺失＝映射批素材不全）。
                    const MappingAxisFact* fact = nullptr;
                    for (const MappingAxisFact& f : input.mappingBatch.axes) {
                        if (f.combinationId == combo.combinationId
                            && f.jointId == axis.jointId) {
                            fact = &f;
                            break;
                        }
                    }
                    if (fact == nullptr || !fact->inertiaRatio.has_value()) {
                        // 规则已配置而数值缺失＝数据不足（不默认通过——§7.2）。
                        DataGap gap;
                        gap.dimension = "inertia-ratio";
                        gap.detail = fact == nullptr
                            ? "映射批无该组合×轴的惯量比事实（dt.mapping 素材不全）"
                            : "映射侧负载折算惯量缺失（惯量比不适用——映射侧降级）";
                        gap.axisId = axis.jointId;
                        gap.caseId = caseId;
                        rec.gaps.push_back(std::move(gap));
                        cellHasInertiaGap = true;
                    }
                    // 配置且有值 → 参考判定（呈现素材——不淘汰，§11.3；
                    // 比较容差＝core C7 dimensionless 1e-12；词表无惯量比
                    // 超限 token——P-SEL-4 裁决后随增量任务启用正式约束形态，
                    // 本头不预建，参考值仅呈现）。
                    if (fact != nullptr && fact->inertiaRatio.has_value()) {
                        const double tol = core::runtimeAbsoluteTolerance(
                                               core::QuantityKind::Dimensionless)
                                               .value_or(0.0);
                        const double ratio = *fact->inertiaRatio;
                        const double ref = *input.inertiaRule.referenceMaxRatio;
                        const bool within = ratio <= ref
                                         || core::closeWithin(ratio, ref,
                                                              core::Tolerance{0.0, tol});
                        if (!within && cell.note.empty()) {
                            // 超参考值的呈现标注（不改变 verdict——格判定
                            // 只由轴级能力维度与缺口聚合决定）。
                            cell.note = "inertia-ratio-over-reference";
                        }
                    }
                } else {
                    // 规则未配置（R1 默认态）→ 显式"未判定"（不产生原因/
                    // 缺口、不影响 verdict——该维度不是表 4 必需项）。
                    ++unsettledAxes;
                }
            }

            // 轴级筛选记录装配（逐轴：组合口径 facts 组装→T04 筛选→取
            // 本组合候选的记录）；格聚合覆盖【全部轴】（任一轴失败即格
            // 失败，§9.4 矩阵语义）。
            std::vector<FeasibilityRecord> cellAxisRecords;
            for (const AxisDeviceAssignment& axis : combo.axes) {
                // 关节侧＋需求侧（与组合无关——DYN-03 口径，按 轴×工况 查）。
                const AxisWorkpointFacts* joint = nullptr;
                for (const AxisWorkpointFacts& f : input.axisFacts) {
                    if (f.jointId == axis.jointId && f.caseId == caseId) {
                        joint = &f;
                        break;
                    }
                }
                // 电机侧＋惯量比＋效率（组合口径——映射批按 组合×轴×工况 查）。
                const MappingAxisFact* mfact = nullptr;
                for (const MappingAxisFact& f : input.mappingBatch.axes) {
                    if (f.combinationId == combo.combinationId
                        && f.jointId == axis.jointId && f.caseId == caseId) {
                        mfact = &f;
                        break;
                    }
                }
                if (joint == nullptr && mfact == nullptr) {
                    // 该组合轴在该工况无任何事实＝覆盖缺口（CaseCoverageGap
                    // 语义素材——不伪造零负载，§11.2）。
                    DataGap gap;
                    gap.dimension = "workpoint-missing";
                    gap.detail = "该轴该工况无工作点事实（覆盖缺口——不伪造零负载）";
                    gap.axisId = axis.jointId;
                    gap.caseId = caseId;
                    rec.gaps.push_back(std::move(gap));
                    continue;
                }
                // 组装该组合口径的单条 facts（关节侧来自 bundle、电机侧以
                // 映射批为权威——AxisFactsBundle 注的 P-SEL-1 消费面边界）。
                AxisWorkpointFacts facts;
                if (joint != nullptr) {
                    facts = *joint;
                } else {
                    facts.jointId = axis.jointId;
                    facts.caseId = caseId;
                }
                facts.jointId = axis.jointId;   // 轴身份以组合轴表为权威。
                facts.caseId = caseId;          // 工况以格为权威。
                if (mfact != nullptr) {
                    facts.motorTorqueRms = mfact->motorTorqueRms;
                    facts.motorTorquePeak = mfact->motorTorquePeak;
                    facts.motorSpeedPeak = mfact->motorSpeedPeak;
                    facts.motorSpeedRms = mfact->motorSpeedRms;
                    facts.motorPowerPeak = mfact->motorPowerPeak;
                    facts.motorPowerRms = mfact->motorPowerRms;
                    facts.peakDuration = mfact->peakDuration;
                    facts.atTime = mfact->peakAtTime;
                    facts.segmentId = mfact->peakSegmentId;
                }
                // 候选在快照内才可筛选（快照外候选＝组装错配——数据缺口，
                // 不猜测能力；质量维度同样按缺口处理）。
                if (findMotor(snapshot, axis.motorModelId) == nullptr
                    || findGearbox(snapshot, axis.gearboxModelId) == nullptr) {
                    DataGap gap;
                    gap.dimension = "candidate-missing";
                    gap.detail = "组合候选在快照内缺失（" + axis.motorModelId + " / "
                                 + axis.gearboxModelId + "——无法按目录能力筛选）";
                    gap.axisId = axis.jointId;
                    gap.caseId = caseId;
                    rec.gaps.push_back(std::move(gap));
                    continue;
                }
                // T04 筛选（该组合口径工作点——全部候选遍历后取本组合候选
                // 的记录；原因/缺口回填切片/映射/快照身份后并入组合级）。
                for (const FeasibilityRecord& src :
                     selector.screenMotors(snapshot, {facts}, input.criteria, ctx)) {
                    if (src.candidateModelId != axis.motorModelId) {
                        continue;  // 只取本组合候选（其余候选记录由其各自组合消费）。
                    }
                    FeasibilityRecord merged = src;
                    for (RejectionReason& reason : merged.reasons) {
                        reason.inputSliceId = rec.inputSliceId;
                        reason.mappingId = rec.mappingId;
                        reason.catalog = rec.catalog;
                    }
                    cellAxisRecords.push_back(std::move(merged));
                }
                for (const FeasibilityRecord& src :
                     selector.screenGearboxes(snapshot, {facts}, input.criteria, ctx)) {
                    if (src.candidateModelId != axis.gearboxModelId) {
                        continue;
                    }
                    FeasibilityRecord merged = src;
                    for (RejectionReason& reason : merged.reasons) {
                        reason.inputSliceId = rec.inputSliceId;
                        reason.mappingId = rec.mappingId;
                        reason.catalog = rec.catalog;
                    }
                    cellAxisRecords.push_back(std::move(merged));
                }
            }
            // 轴级记录的原因/缺口并入组合级（合并后统一稳定排序——⑩）。
            for (const FeasibilityRecord& axisRec : cellAxisRecords) {
                rec.reasons.insert(rec.reasons.end(), axisRec.reasons.begin(),
                                   axisRec.reasons.end());
                rec.gaps.insert(rec.gaps.end(), axisRec.gaps.begin(),
                                axisRec.gaps.end());
            }

            // 效率降级维度（§9.3 行 7）：映射事实 efficiencyApplied=false
            // 的轴 → 数据缺口（不以 η＝1 静默替代——卡 §10.1）。
            for (const AxisDeviceAssignment& axis : combo.axes) {
                for (const MappingAxisFact& f : input.mappingBatch.axes) {
                    if (f.combinationId == combo.combinationId
                        && f.jointId == axis.jointId && !f.efficiencyApplied) {
                        DataGap gap;
                        gap.dimension = "efficiency";
                        gap.detail = "映射侧效率缺失/降级（该轴功率与能量素材不可判）";
                        gap.axisId = axis.jointId;
                        gap.caseId = caseId;
                        rec.gaps.push_back(std::move(gap));
                        break;
                    }
                }
            }

            // 格聚合（原因优先→缺口次之→通过；覆盖全部轴记录——任一轴
            // 失败即格失败，定位＝首条原因的工作点，卡 §9.4）。
            aggregateCaseCell(cellAxisRecords, cell);
            // 该格相关的组合级缺口（惯量比）回落为格 DataInsufficient 的
            // 呈现面（记录 gaps 已全量列出——格 note 摘要标注）。
            if (cell.verdict == VerdictKind::Feasible && cellHasInertiaGap) {
                cell.verdict = VerdictKind::DataInsufficient;
                cell.note = "inertia-ratio-gap";
            }
            // 惯量比未判定标注（呈现面——不改变 verdict，§11.3）。
            if (cell.verdict == VerdictKind::Feasible
                && input.inertiaRule.referenceMaxRatio == std::nullopt) {
                cell.note += cell.note.empty() ? "" : ";";
                cell.note += reasonTokenText(ReasonToken::InertiaRatioPolicyUnsettled);
            }
            outcome.coverage.push_back(std::move(cell));
        }

        // ⑥映射批完整性（卡 §9.3 行 12）：整体失败＝上游失败透传（不伪装
        // 成候选淘汰、不自动判整机不可行——§10.2 空集语义表）；Partial＝
        // 缺失清单逐条转数据缺口；映射批整体未供给（版本全零）＝上游缺口。
        if (input.mappingBatch.mappingFailed) {
            DataGap gap;
            gap.dimension = "drivetrain-mapping";
            gap.detail = "映射批整体失败（上游诊断透传——映射失败≠器件能力不足）";
            rec.gaps.push_back(std::move(gap));
        } else if (!mappingBatchSupplied) {
            DataGap gap;
            gap.dimension = "drivetrain-mapping";
            gap.detail = "映射批未供给（dt.mapping 上游结果缺失——数据不足分轨）";
            rec.gaps.push_back(std::move(gap));
        } else if (input.mappingBatch.completeness == CompletenessKind::Partial) {
            for (const std::string& item : input.mappingBatch.missingItems) {
                DataGap gap;
                gap.dimension = "drivetrain-mapping";
                gap.detail = "映射批缺失项：" + item;
                rec.gaps.push_back(std::move(gap));
            }
        }

        // ⑨质量核算（§16 行"质量成本"）：Σ 各轴电机＋减速器质量（kg——
        // 目录必填字段；快照缺失候选＝质量缺口，不猜测）。
        {
            bool massMissing = false;
            double totalMass = 0.0;
            for (const AxisDeviceAssignment& axis : combo.axes) {
                const MotorCatalogEntry* motor = findMotor(snapshot, axis.motorModelId);
                const GearboxCatalogEntry* gearbox =
                    findGearbox(snapshot, axis.gearboxModelId);
                if (motor == nullptr || gearbox == nullptr) {
                    massMissing = true;
                    continue;
                }
                totalMass += motor->mass + gearbox->mass;
            }
            if (massMissing) {
                DataGap gap;
                gap.dimension = "mass-missing";
                gap.detail = "组合含快照外候选（质量不可核算——不猜测）";
                rec.gaps.push_back(std::move(gap));
            }
            outcome.totalMass = totalMass;
        }

        // 未判定标记（轴级计数——呈现/汇总面）。
        outcome.inertiaRatioUnsettled = unsettledAxes > 0;

        // ⑩verdict 汇总（T04 细化 ⑦ 同规则：原因优先→缺口→可行）。
        sortReasons(rec.reasons);
        if (!rec.reasons.empty()) {
            rec.verdict = VerdictKind::Rejected;
        } else if (!rec.gaps.empty()) {
            rec.verdict = VerdictKind::DataInsufficient;
        } else {
            rec.verdict = VerdictKind::Feasible;
        }
        outcomes.push_back(std::move(outcome));
    }
    return outcomes;
}

// =====================================================================
// 组合校核评估器（③端口适配层——drivetrain 评估器同款五步流程）
// =====================================================================

evidence::EvaluatorDescriptor makeCombinationCheckDescriptor()
{
    evidence::EvaluatorDescriptor d;
    // 评估键/契约版本：唯一书写点常量（kebab 词形偏差——文件头注）。
    d.key = std::string(kCombinationCheckEvaluationKey);
    d.contractVersion = kCombinationCheckContractVersion;

    // 依赖声明（卡 §9.2 管线②四条目——全部 Required；resolutionNote 为
    // 人工评审的解析说明，非身份载体）。P-SEL-4 裁决后按卡 §11.3 增登
    // Policy 依赖声明——本版不预声明（切片面变更走单元卡增量修订）。
    const auto declare = [](std::string key, evidence::DependencyKind kind,
                            std::string note) {
        evidence::DependencyDeclaration dep;
        dep.key = std::move(key);
        dep.kind = kind;
        dep.requiredness = evidence::DependencyRequiredness::Required;
        dep.resolutionNote = std::move(note);
        return dep;
    };
    d.inputs.push_back(declare(std::string(kCatalogLockKey),
                               evidence::DependencyKind::Object,
                               "目录锁定版本对象（CatalogLockPayload canonical 字节；"
                               "经注入 ICatalogProvider 解析快照）"));
    d.inputs.push_back(declare(std::string(kJointSeriesSelKey),
                               evidence::DependencyKind::UpstreamResult,
                               "关节侧＋电机侧轴工作点事实包（P-SEL-1 提议契约 v1"
                               "——selUpstreamAnchor 物化）"));
    d.inputs.push_back(declare(std::string(kMappingBatchKey),
                               evidence::DependencyKind::UpstreamResult,
                               "dt.mapping 组合批映射事实（③端口唯一映射口径产出"
                               "的提取面——SEL-05 红线声明）"));
    d.inputs.push_back(declare(std::string(kSelScreeningConfigKey),
                               evidence::DependencyKind::Configuration,
                               "筛选条件（进入切片身份——条件变更即重算）"));

    // Profile 绑定（sel 域 v1——drivetrain 评估器同款；contentIdentity
    // 保留值＝域不可申报，注册表权威计算——evidence §9.5）。
    d.profile.profileId = std::string(kSelProfileId);
    d.profile.version = std::string(kSelProfileVersion);
    d.profile.contentIdentity = core::ContentIdentity{};

    // 模式与实例语义（卡 §11.2——Preview 暂不含，保守同 drivetrain）。
    d.supportedModes = {core::EvaluationMode::Quick, core::EvaluationMode::Verified};
    d.stateless = true;
    d.threadSafety = evidence::ThreadSafety::SingleThread;
    return d;
}

CombinationCheckEvaluator::CombinationCheckEvaluator(const ICatalogProvider& catalogProvider)
    : m_descriptor(makeCombinationCheckDescriptor())
    , m_catalogProvider(&catalogProvider)
{
}

const evidence::EvaluatorDescriptor& CombinationCheckEvaluator::descriptor() const
{
    return m_descriptor;
}

evidence::EvaluationOutput CombinationCheckEvaluator::evaluate(
    const evidence::EvaluationRequest& request, evidence::IEvaluationContext& context)
{
    // ---- ①切片核对（派发契约前提——execution 按键派发；不符＝调用方
    // 契约违约 fail-fast）。
    if (request.slice.evaluationKey != kCombinationCheckEvaluationKey
        || request.slice.evaluatorContractVersion != kCombinationCheckContractVersion) {
        throw std::invalid_argument(
            "组合校核评估器：切片绑定评估键/契约版本与登记值不符（切片 «"
            + request.slice.evaluationKey + "» v"
            + std::to_string(request.slice.evaluatorContractVersion) + "，期望 «"
            + std::string(kCombinationCheckEvaluationKey) + "» v"
            + std::to_string(kCombinationCheckContractVersion) + "）");
    }

    // ---- ②必需条目提取（注册闭包的第二道防线——缺失即 fail-fast）。
    const evidence::DependencyEntry* catalogEntry = findEntry(request.slice, kCatalogLockKey);
    const evidence::DependencyEntry* seriesEntry = findEntry(request.slice, kJointSeriesSelKey);
    const evidence::DependencyEntry* mappingEntry = findEntry(request.slice, kMappingBatchKey);
    const evidence::DependencyEntry* criteriaEntry
        = findEntry(request.slice, kSelScreeningConfigKey);
    if (catalogEntry == nullptr || seriesEntry == nullptr || mappingEntry == nullptr
        || criteriaEntry == nullptr) {
        throw std::invalid_argument(
            "组合校核评估器：切片缺少必需依赖条目（catalog.lock/dyn.joint-series/"
            "dt.mapping/config.sel-screening 均为 Required）");
    }

    // ---- ③a 目录锁定对象：条目载荷核对→字节读取→解码→快照解析。
    const auto* catalogPayload
        = std::get_if<evidence::ObjectDependencyPayload>(&catalogEntry->payload);
    if (catalogPayload == nullptr) {
        throw std::invalid_argument(
            "组合校核评估器：catalog.lock 条目载荷形态与 Object 类不符（切片"
            "冻结协议破坏——fail-fast）");
    }
    const auto lockBytes = context.tryObjectBytes(catalogPayload->objectId,
                                                  catalogPayload->contentVersion);
    if (!lockBytes.has_value()) {
        // 宿主物化缺口＝数据类：空产出返回（selection 未登记"输入不可得"
        // 稳定码——码面扩展随 WP-19-T06；不产诊断记录即不伪造码值，
        // NFR-MNT-03；消费方以无 payload 判定不完整）。
        return {};
    }
    const CatalogLockPayload lock = decodeCatalogLockPayload(*lockBytes);
    // 第二道核对：字节内锁定对象 ID 与条目载荷一致（错配＝组装协议破坏）。
    if (!(lock.lockObjectId == catalogPayload->objectId)) {
        throw std::invalid_argument(
            "组合校核评估器：catalog.lock 字节内锁定对象 ID 与条目载荷不符"
            "（组装协议破坏——fail-fast）");
    }
    // 快照解析：load 抛出＝锁定对象不存在/内容摘要不符（卡 §14.1 明文
    // fail-fast——引用完整性破坏属装配违约，重抛不吞）。
    const CatalogVersion lockRef{lock.identity, lock.lockObjectId};
    const CatalogPackageSnapshot snapshotLoaded = m_catalogProvider->load(lockRef);

    // ---- ③b 两条 UpstreamResult 条目：锚物化字节读取与解码。
    const auto* seriesRef
        = std::get_if<evidence::UpstreamResultDependencyPayload>(&seriesEntry->payload);
    const auto* mappingRef
        = std::get_if<evidence::UpstreamResultDependencyPayload>(&mappingEntry->payload);
    if (seriesRef == nullptr || mappingRef == nullptr) {
        throw std::invalid_argument(
            "组合校核评估器：UpstreamResult 条目载荷形态不符（切片冻结协议破坏）");
    }
    const auto seriesBytes = context.tryObjectBytes(selUpstreamAnchor(seriesRef->upstreamSliceId),
                                                    core::ContentVersion{});
    const auto mappingBytes = context.tryObjectBytes(selUpstreamAnchor(mappingRef->upstreamSliceId),
                                                     core::ContentVersion{});
    if (!seriesBytes.has_value() || !mappingBytes.has_value()) {
        // 物化锚缺口＝数据类（同 ③a 口径——空产出，不伪造码值）。
        return {};
    }
    const AxisFactsBundle axisFacts = decodeAxisFactsBundle(*seriesBytes);
    const MappingBatchFacts mappingBatch = decodeMappingBatchFacts(*mappingBytes);

    // ---- ③c 筛选条件：Configuration 载荷直接携带 canonicalBytes（无锚）。
    const auto* criteriaPayload
        = std::get_if<evidence::ConfigurationDependencyPayload>(&criteriaEntry->payload);
    if (criteriaPayload == nullptr) {
        throw std::invalid_argument(
            "组合校核评估器：config.sel-screening 条目载荷形态与 Configuration 类不符");
    }
    const ScreeningCriteria criteria = decodeScreeningCriteria(criteriaPayload->canonicalBytes);

    // ---- ④批级身份预检（§9.3 行 11——§10.2 校验边界快速拒绝）：
    // 映射批版本非零＋批切片身份与切片条目声明逐字节一致；不一致→
    // SEL-IDENTITY-MISMATCH 诊断＋空 payload（拒绝评估，不以版本不符的
    // 数据继续计算——AT-38 三方同口径纪律）。
    {
        evidence::EvaluationOutput out;
        const bool versionInvalid = mappingBatch.mappingContractVersion == 0
                                 || mappingBatch.mappingAlgorithmVersion == 0;
        const bool sliceMismatch
            = mappingBatch.upstreamSliceId.bytes != mappingRef->upstreamSliceId.bytes;
        if (versionInvalid || sliceMismatch) {
            core::DiagnosticRecord rec = core::DiagnosticRecord::make(
                std::string(kSelIdentityMismatch), std::nullopt, std::nullopt,
                std::nullopt, "组合校核批级身份预检",
                versionInvalid
                    ? "映射批契约/算法版本为 0（未登记非法）"
                    : "映射批切片身份与 dt.mapping 条目声明不一致（AT-38 三方同口径破坏）",
                "以同一 dt.mapping 批结果与切片声明重派发（版本不符数据不进入校核）");
            out.diagnostics.push_back(std::move(rec));
            return out;
        }
    }

    // ---- ⑤核心调用＋输出装配（取消经 context 适配——组合边界查询）。
    class ContextCancellation final : public evidence::IEvaluationContext {
    public:
        explicit ContextCancellation(evidence::IEvaluationContext& inner)
            : m_inner(inner)
        {
        }
        bool cancellationRequested() const override
        {
            return m_inner.cancellationRequested();
        }
        void reportProgress(std::uint8_t percent, std::string_view phase) override
        {
            m_inner.reportProgress(percent, phase);
        }
        std::optional<std::vector<std::uint8_t>> tryObjectBytes(
            core::ObjectId objectId, core::ContentVersion contentVersion) const override
        {
            return m_inner.tryObjectBytes(objectId, contentVersion);
        }

    private:
        evidence::IEvaluationContext& m_inner;
    } cancellationAdapter(context);

    CombinationCheckCoreInput coreInput;
    coreInput.snapshot = &snapshotLoaded;
    coreInput.axisFacts = axisFacts;
    coreInput.mappingBatch = mappingBatch;
    coreInput.criteria = criteria;
    // 惯量比规则：评估器路径 R1 恒为未配置态（默认构造——nullopt）。理由：
    // P-SEL-4 未裁决期间不写死任何阈值（卡 §11.3"不写死默认阈值"）；裁决
    // 后阈值经 Policy 条目进入切片（descriptor 增加 Policy 依赖声明——
    // 单元卡增量修订），呈现层参考值排序属 Quick 研究态（不入结果身份），
    // 经核心直调路径行使。
    coreInput.inertiaRule = InertiaRatioRule{};
    coreInput.inputSliceId = request.slice.sliceId;

    const std::vector<CombinationCheckOutcome> outcomes
        = checkCombinations(coreInput, &cancellationAdapter);

    // 总产出装配（身份块＋映射批透传面——SelectionCheckResult 契约）。
    SelectionCheckResult result;
    result.records.reserve(outcomes.size());
    for (const CombinationCheckOutcome& o : outcomes) {
        result.records.push_back(o.record);
    }
    for (const CombinationCheckOutcome& o : outcomes) {
        result.coverage.insert(result.coverage.end(), o.coverage.begin(),
                               o.coverage.end());
    }
    result.catalog = snapshotLoaded.manifest.identity;
    result.mappingSliceId = mappingBatch.upstreamSliceId;
    result.inputSliceId = request.slice.sliceId;
    result.completeness = mappingBatch.completeness;
    result.missingItems = mappingBatch.missingItems;
    result.diagnosticCodes = mappingBatch.diagnosticCodes;

    evidence::EvaluationOutput out;
    // 映射批诊断码透传（§10.2"上游诊断透传"——selection 不吞上游诊断；
    // 码文本为 DT-* 稳定码值，记录主体在此登记为透传事实）。
    for (const std::string& code : mappingBatch.diagnosticCodes) {
        out.diagnostics.push_back(core::DiagnosticRecord::make(
            code, std::nullopt, std::nullopt, std::nullopt,
            "组合校核——上游映射批诊断透传",
            "dt.mapping 批结果携带的上游诊断（逐码透传——不吞错）",
            "按上游诊断处置建议核对映射输入后重算"));
    }

    // payload（canonical 字节＋SHA-256 摘要——CR-02 唯一算法面）。
    const std::vector<std::uint8_t> payloadBytes = encodeSelectionCheckResult(result);
    out.payload = evidence::DomainPayload{std::string(kCombinationCheckPayloadToken),
                                          payloadBytes, digestOf(payloadBytes)};

    // EvidenceItem（"域.项"词形——有完整产出〔未取消截断〕即 Satisfied；
    // 截断/空产出由 presence 纪律由汇总层处置——本层不对不完整素材出
    // Satisfied 项）。
    // T06 扩展（WP-19-T06 acceptance 3）：证据项与 sel 域 RequiredEvidence
    // Profile（makeSelRequiredEvidenceProfile——表 4 选型行）必需项对齐
    // ——目录版本锁定/逐项淘汰原因/组合兼容记录三项随本 payload 产出登
    // 记（同 payload 摘要；"sel.combination-check" 为既有总证据项保留
    // ——T05 验收面）；必需项②"每组合电机侧工作点"（sel.motor-op-point）
    // 由 dt.mapping 评估器（drivetrain）产出——同一 sel Profile 的项级
    // 分工，本评估器不重复登记（不伪造上游证据）。
    const bool truncated = outcomes.size() < mappingBatch.combinations.size();
    if (!truncated && !result.records.empty()) {
        evidence::EvidenceItem item;
        item.status = evidence::EvidenceItemStatus::Satisfied;
        item.artifactDigest = digestOf(payloadBytes).bytes;
        // 总证据项（T05 既有——payload 完整性证据）。
        item.itemId = std::string(kSelProfileId) + ".combination-check";
        out.evidence.push_back(item);
        // 必需项①：目录版本锁定标识（payload 内 SelectionCheckResult.catalog）。
        item.itemId = std::string(kSelProfileItemCatalogLock);
        out.evidence.push_back(item);
        // 必需项③：逐项淘汰原因（payload 内 records——实际值/阈值/来源）。
        item.itemId = std::string(kSelProfileItemRejectionReasons);
        out.evidence.push_back(item);
        // 必需项④：组合兼容记录（payload 内兼容核对原因记录）。
        item.itemId = std::string(kSelProfileItemComboCompatibility);
        out.evidence.push_back(item);
    }
    return out;
}

CombinationCheckEvaluatorFactory::CombinationCheckEvaluatorFactory(
    const ICatalogProvider& catalogProvider)
    : m_descriptor(makeCombinationCheckDescriptor())
    , m_catalogProvider(&catalogProvider)
{
}

const evidence::EvaluatorDescriptor& CombinationCheckEvaluatorFactory::descriptor() const
{
    return m_descriptor;
}

std::unique_ptr<evidence::IEngineeringEvaluator> CombinationCheckEvaluatorFactory::create()
    const
{
    return std::make_unique<CombinationCheckEvaluator>(*m_catalogProvider);
}

}  // namespace sdurws::ird::selection
