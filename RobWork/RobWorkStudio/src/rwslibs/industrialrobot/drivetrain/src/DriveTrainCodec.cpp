/**
 * @file   DriveTrainCodec.cpp
 * @brief  传动映射域 canonical 字节编解码的实现（Codec.hpp 协议的落位）。
 *
 * 设计依据：
 *   - Codec.hpp 文件头（协议形态——小端定宽/版本化 magic/严格解码校验/
 *     非有限拒绝；偏差登记两则）
 *   - 需求 NFR-COR-02（确定性：同值对象必得同字节——编码无环境依赖分支，
 *     浮点按 IEEE-754 位型直写）、NFR-COR-03（非有限拒绝）、CON-04/05
 *   - 任务契约 tasks/foundation/WP-18-T03.json（acceptance 1——③端口
 *     评估器的输入/输出字节承载）
 *
 * 确定性实现注记：double 经位型（memcpy 到 u64）小端直写——不经过文本
 * 格式化（printf 类的 locale 依赖是确定性大敌）；-0.0 与 0.0 位型不同
 *（语义上如实区分——组装方规范化责任，不在编码层吞并）。
 *
 * 线程安全：无共享可变状态——可重入纯函数。
 */

#include <sdurws/ird/drivetrain/Codec.hpp>

#include <cstring>
#include <stdexcept>
#include <string>

namespace sdurws::ird::drivetrain {
namespace {

// =====================================================================
// 字节流读写工具（小端定宽；解码端全部带边界校验——越界即调用方契约违约）
// =====================================================================

/// 魔数与 codec 版本（版本演进＝尾部数字递增；解码端严格匹配——版本不符
/// 即拒绝，不做多版本兼容读：CON-04 契约版本化纪律的字节承载侧）。
/// WP-18-T05：输出载荷升级 v2（反射惯量段增补窗口投影标记＋R2 完整关节
/// 轴系反射惯量矩阵——§9.3/§9.5）；模型/序列载荷无字段变化保持 v1
/// （窗口字段自 T03 起已在 v1 形态内——仅值域从"恒缺省"扩展为"R2 合法"，
/// 字节布局不变）。
constexpr char kMagicModel[] = "IRDDTD1";   // DriveTrainModel 载荷（v1——布局不变）
constexpr char kMagicSeries[] = "IRDDTJ1";  // JointSeriesView 载荷（v1——布局不变）
constexpr char kMagicOutput[] = "IRDDTO2";  // DriveTrainMappingOutput 载荷（v2）
constexpr std::uint32_t kModelCodecVersion = 1;   // 模型载荷版本（v1）
constexpr std::uint32_t kSeriesCodecVersion = 1;  // 序列载荷版本（v1）
constexpr std::uint32_t kOutputCodecVersion = 2;  // 输出载荷版本（v2——T05 增补字段）

/// 编码写入器（容量按需增长；无共享状态）。
class ByteWriter {
public:
    /// 预留容量（减少重分配——非正确性需求）。
    void reserve(std::size_t n) { m_bytes.reserve(n); }

    void putU8(std::uint8_t v) { m_bytes.push_back(v); }

    void putU32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void putU64(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    /// double 按位型小端直写（非有限值由调用方先行拒绝——本层不做语义检查）。
    void putF64(double v)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v), "double 必须为 64 位（IEEE-754）");
        std::memcpy(&bits, &v, sizeof(bits));
        putU64(bits);
    }

    void putBytes(const std::uint8_t* p, std::size_t n)
    {
        m_bytes.insert(m_bytes.end(), p, p + n);
    }

    void putString(const std::string& s)
    {
        putU32(static_cast<std::uint32_t>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }

    const std::vector<std::uint8_t>& bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

/// 解码读取器（游标式；任何越界/余量不符即抛——严格校验形态）。
class ByteReader {
public:
    explicit ByteReader(const std::vector<std::uint8_t>& bytes)
        : m_bytes(bytes)
    {
    }

    /// 前置字节量核对（不足即抛——消息含期望/实得，供定位）。
    void require(std::size_t n) const
    {
        if (m_pos + n > m_bytes.size()) {
            throw std::invalid_argument(
                "drivetrain codec：字节流提前结束（需要 " + std::to_string(n)
                + " 字节，位置 " + std::to_string(m_pos) + "，总长 "
                + std::to_string(m_bytes.size()) + "）");
        }
    }

    /// 全部消费核对（尾部余量非零＝形态不符——拒绝静默截断/粘包）。
    void requireEnd() const
    {
        if (m_pos != m_bytes.size()) {
            throw std::invalid_argument(
                "drivetrain codec：解码后余留 " + std::to_string(m_bytes.size() - m_pos)
                + " 字节（形态与协议不符——拒绝静默截断）");
        }
    }

    std::uint8_t getU8()
    {
        require(1);
        return m_bytes[m_pos++];
    }

    std::uint32_t getU32()
    {
        require(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(m_bytes[m_pos++]) << (8 * i);
        }
        return v;
    }

    std::uint64_t getU64()
    {
        require(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(m_bytes[m_pos++]) << (8 * i);
        }
        return v;
    }

    double getF64()
    {
        const std::uint64_t bits = getU64();
        double v = 0.0;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    /// 读取 n 字节原始块（id/摘要承载——n 由调用方按类型给定）。
    void getBytes(std::uint8_t* out, std::size_t n)
    {
        require(n);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = m_bytes[m_pos++];
        }
    }

    std::string getString()
    {
        const std::uint32_t len = getU32();
        require(len);
        std::string s(reinterpret_cast<const char*>(&m_bytes[m_pos]), len);
        m_pos += len;
        return s;
    }

private:
    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_pos = 0;
};

// =====================================================================
// 编码安全校验与域校验工具
// =====================================================================

/// 非有限守卫（NFR-COR-03——非有限值不进入身份与持久化链）。
void requireFinite(double v, const char* field)
{
    if (!std::isfinite(v)) {
        throw std::invalid_argument(std::string("drivetrain codec：字段 ") + field
                                    + " 含非有限值（NaN/±Inf 不得编码——NFR-COR-03）");
    }
}

/// 枚举域守卫（解码端——词表外位型拒绝，防伪造字节流注入非法枚举）。
template <typename E>
E enumFromU8(std::uint8_t raw, E max, const char* field)
{
    if (raw > static_cast<std::uint8_t>(max)) {
        throw std::invalid_argument(std::string("drivetrain codec：字段 ") + field
                                    + " 的枚举值越域（raw=" + std::to_string(raw) + "）");
    }
    return static_cast<E>(raw);
}

/// 原始 id 读写（16 字节强类型通用——ObjectId；ContentVersion/Identity 为 32 字节
/// 由调用方按 Digest256 直写）。
void putObjectId(ByteWriter& w, const core::ObjectId& id)
{
    w.putBytes(id.bytes.data(), id.bytes.size());
}

core::ObjectId getObjectId(ByteReader& r)
{
    core::ObjectId id;
    r.getBytes(id.bytes.data(), id.bytes.size());
    return id;
}

void putDigest(ByteWriter& w, const core::Digest256& d)
{
    w.putBytes(d.data(), d.size());
}

core::Digest256 getDigest(ByteReader& r)
{
    core::Digest256 d;
    r.getBytes(d.data(), d.size());
    return d;
}

/// RowMatrix 读写（维度＋行主序元素——编码形态完整性核对：count==rows*cols）。
void putMatrix(ByteWriter& w, const RowMatrix& m, const char* field)
{
    w.putU32(static_cast<std::uint32_t>(m.rows));
    w.putU32(static_cast<std::uint32_t>(m.cols));
    if (m.data.size() != m.rows * m.cols) {
        throw std::invalid_argument(std::string("drivetrain codec：矩阵 ") + field
                                    + " 元素数与维度不符（形态完整性）");
    }
    w.putU32(static_cast<std::uint32_t>(m.data.size()));
    for (const double v : m.data) {
        requireFinite(v, field);
        w.putF64(v);
    }
}

RowMatrix getMatrix(ByteReader& r, const char* field)
{
    RowMatrix m;
    m.rows = r.getU32();
    m.cols = r.getU32();
    const std::uint32_t count = r.getU32();
    // 维度自洽核对（溢出安全乘法——大维度直接拒绝）。
    if (count != m.rows * m.cols || m.rows > 1U << 20 || m.cols > 1U << 20) {
        throw std::invalid_argument(std::string("drivetrain codec：矩阵 ") + field
                                    + " 维度与元素数不符（解码形态校验失败）");
    }
    m.data.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        m.data[i] = r.getF64();
        requireFinite(m.data[i], field);
    }
    return m;
}

// ---- 复合字段的读写对（模型/序列/输出三面共用）----

void putEfficiency(ByteWriter& w, const EfficiencyModel& e)
{
    requireFinite(e.etaForward, "efficiency.etaForward");
    requireFinite(e.etaBackward, "efficiency.etaBackward");
    w.putF64(e.etaForward);
    w.putF64(e.etaBackward);
    w.putU8(static_cast<std::uint8_t>(e.source));
}

EfficiencyModel getEfficiency(ByteReader& r)
{
    EfficiencyModel e;
    e.etaForward = r.getF64();
    e.etaBackward = r.getF64();
    e.source = enumFromU8(r.getU8(), SourcedValueTag::Estimated, "efficiency.source");
    return e;
}

void putRotor(ByteWriter& w, const RotorInertiaModel& ro)
{
    requireFinite(ro.rotorInertia, "rotor.rotorInertia");
    w.putF64(ro.rotorInertia);
    w.putU8(static_cast<std::uint8_t>(ro.source));
}

RotorInertiaModel getRotor(ByteReader& r)
{
    RotorInertiaModel ro;
    ro.rotorInertia = r.getF64();
    ro.source = enumFromU8(r.getU8(), SourcedValueTag::Estimated, "rotor.source");
    return ro;
}

void putIdentity(ByteWriter& w, const DriveTrainIdentity& id)
{
    putDigest(w, id.drivetrainObjectCv.bytes);
    w.putU32(id.algorithmVersion);
    w.putU32(id.contractVersion);
}

DriveTrainIdentity getIdentity(ByteReader& r)
{
    DriveTrainIdentity id;
    id.drivetrainObjectCv.bytes = getDigest(r);
    id.algorithmVersion = r.getU32();
    id.contractVersion = r.getU32();
    return id;
}

void putPeak(ByteWriter& w, const PeakRecord& p)
{
    w.putU8(p.present ? 1 : 0);
    requireFinite(p.value, "peak.value");
    requireFinite(p.t, "peak.t");
    w.putF64(p.value);
    w.putF64(p.t);
    w.putString(p.segmentId);
    putObjectId(w, p.caseId);
}

PeakRecord getPeak(ByteReader& r)
{
    PeakRecord p;
    p.present = (r.getU8() != 0);
    p.value = r.getF64();
    p.t = r.getF64();
    p.segmentId = r.getString();
    p.caseId = getObjectId(r);
    return p;
}

void putQuadrantStats(ByteWriter& w, const QuadrantStats& q)
{
    requireFinite(q.timeShare, "quadrant.timeShare");
    requireFinite(q.energy, "quadrant.energy");
    w.putF64(q.timeShare);
    w.putF64(q.energy);
    w.putU64(q.sampleCount);
}

QuadrantStats getQuadrantStats(ByteReader& r)
{
    QuadrantStats q;
    q.timeShare = r.getF64();
    q.energy = r.getF64();
    q.sampleCount = r.getU64();
    return q;
}

void putEnergy(ByteWriter& w, const EnergyBreakdown& e)
{
    requireFinite(e.eMotor, "energy.eMotor");
    requireFinite(e.eJoint, "energy.eJoint");
    requireFinite(e.eLoss, "energy.eLoss");
    requireFinite(e.eRegen, "energy.eRegen");
    requireFinite(e.ePos, "energy.ePos");
    requireFinite(e.eRotor, "energy.eRotor");
    w.putF64(e.eMotor);
    w.putF64(e.eJoint);
    w.putF64(e.eLoss);
    w.putF64(e.eRegen);
    w.putF64(e.ePos);
    w.putF64(e.eRotor);
}

EnergyBreakdown getEnergy(ByteReader& r)
{
    EnergyBreakdown e;
    e.eMotor = r.getF64();
    e.eJoint = r.getF64();
    e.eLoss = r.getF64();
    e.eRegen = r.getF64();
    e.ePos = r.getF64();
    e.eRotor = r.getF64();
    return e;
}

}  // namespace

// =====================================================================
// DriveTrainModel 编解码（切片 Object 条目载荷）
// =====================================================================

std::vector<std::uint8_t> encodeDriveTrainModel(const DriveTrainModel& model)
{
    ByteWriter w;
    w.reserve(256 + model.jointAxes.size() * 64);
    w.putBytes(reinterpret_cast<const std::uint8_t*>(kMagicModel), 7);
    w.putU32(kModelCodecVersion);

    // 轴表（串联序；localName 仅诊断呈现——进编码保持往返完整性）。
    w.putU32(static_cast<std::uint32_t>(model.jointAxes.size()));
    for (const JointDriveAxis& a : model.jointAxes) {
        putObjectId(w, a.jointId);
        w.putU8(static_cast<std::uint8_t>(a.kind));
        w.putString(a.localName);
    }
    w.putU32(static_cast<std::uint32_t>(model.motorAxes.size()));
    for (const MotorDriveAxis& a : model.motorAxes) {
        putObjectId(w, a.motorId);
        w.putU64(a.jointIndex);
    }

    // 归一化矩阵与逐轴视图（窗口存在时一并承载——R1 阻断面在评估入口，
    // 编码层如实承载字节）。
    putMatrix(w, model.chat, "chat");
    w.putU32(static_cast<std::uint32_t>(model.ratios.size()));
    for (const TransmissionRatio& rt : model.ratios) {
        requireFinite(rt.c, "ratio.c");
        w.putF64(rt.c);
        w.putU8(static_cast<std::uint8_t>(rt.source));
    }
    w.putU8(model.window.has_value() ? 1 : 0);
    if (model.window.has_value()) {
        putMatrix(w, model.window->C, "window.C");
        w.putU32(static_cast<std::uint32_t>(model.window->jointRange.size()));
        for (const core::ObjectId& id : model.window->jointRange) {
            putObjectId(w, id);
        }
        requireFinite(model.window->conditionNumber, "window.conditionNumber");
        w.putF64(model.window->conditionNumber);
    }

    // 效率/转子/零位偏置/额定力矩（下标配对列表——缺失降级语义按表长承载）。
    w.putU32(static_cast<std::uint32_t>(model.efficiency.size()));
    for (const EfficiencyModel& e : model.efficiency) {
        putEfficiency(w, e);
    }
    w.putU32(static_cast<std::uint32_t>(model.rotor.size()));
    for (const RotorInertiaModel& ro : model.rotor) {
        putRotor(w, ro);
    }
    w.putU32(static_cast<std::uint32_t>(model.zeroOffsetMotor.size()));
    for (const double off : model.zeroOffsetMotor) {
        requireFinite(off, "zeroOffsetMotor");
        w.putF64(off);
    }
    w.putU32(static_cast<std::uint32_t>(model.ratedTorque.size()));
    for (const std::optional<double>& rt : model.ratedTorque) {
        w.putU8(rt.has_value() ? 1 : 0);
        if (rt.has_value()) {
            requireFinite(*rt, "ratedTorque");
            w.putF64(*rt);
        }
    }

    putIdentity(w, model.identity);
    return w.bytes();
}

DriveTrainModel decodeDriveTrainModel(const std::vector<std::uint8_t>& bytes)
{
    ByteReader reader(bytes);
    reader.require(8);
    if (std::memcmp(bytes.data(), kMagicModel, 7) != 0) {
        throw std::invalid_argument("drivetrain codec：DriveTrainModel 载荷 magic 不符");
    }
    // 消费 7 字节 magic（值已由 memcmp 核对）＋4 字节版本（严格匹配——
    // 拒绝多版本兼容读，CON-04 契约版本化纪律的字节承载侧）。
    for (std::size_t i = 0; i < 7; ++i) {
        (void)reader.getU8();
    }
    if (reader.getU32() != kModelCodecVersion) {
        throw std::invalid_argument("drivetrain codec：DriveTrainModel 载荷版本不符"
                                    "（拒绝多版本兼容读——CON-04 契约版本化）");
    }

    DriveTrainModel model;
    const std::uint32_t nJoints = reader.getU32();
    model.jointAxes.reserve(nJoints);
    for (std::uint32_t i = 0; i < nJoints; ++i) {
        JointDriveAxis a;
        a.jointId = getObjectId(reader);
        a.kind = enumFromU8(reader.getU8(), JointKind::Prismatic, "jointAxes.kind");
        a.localName = reader.getString();
        model.jointAxes.push_back(std::move(a));
    }
    const std::uint32_t nMotors = reader.getU32();
    model.motorAxes.reserve(nMotors);
    for (std::uint32_t i = 0; i < nMotors; ++i) {
        MotorDriveAxis a;
        a.motorId = getObjectId(reader);
        a.jointIndex = reader.getU64();
        model.motorAxes.push_back(std::move(a));
    }

    model.chat = getMatrix(reader, "chat");
    const std::uint32_t nRatios = reader.getU32();
    model.ratios.reserve(nRatios);
    for (std::uint32_t i = 0; i < nRatios; ++i) {
        TransmissionRatio rt;
        rt.c = reader.getF64();
        requireFinite(rt.c, "ratio.c");
        rt.source = enumFromU8(reader.getU8(), SourcedValueTag::Estimated, "ratio.source");
        model.ratios.push_back(rt);
    }
    if (reader.getU8() != 0) {
        CouplingWindow win;
        win.C = getMatrix(reader, "window.C");
        const std::uint32_t nRange = reader.getU32();
        win.jointRange.reserve(nRange);
        for (std::uint32_t i = 0; i < nRange; ++i) {
            win.jointRange.push_back(getObjectId(reader));
        }
        win.conditionNumber = reader.getF64();
        requireFinite(win.conditionNumber, "window.conditionNumber");
        model.window = std::move(win);
    }

    const std::uint32_t nEff = reader.getU32();
    model.efficiency.reserve(nEff);
    for (std::uint32_t i = 0; i < nEff; ++i) {
        model.efficiency.push_back(getEfficiency(reader));
    }
    const std::uint32_t nRotor = reader.getU32();
    model.rotor.reserve(nRotor);
    for (std::uint32_t i = 0; i < nRotor; ++i) {
        model.rotor.push_back(getRotor(reader));
    }
    const std::uint32_t nOffset = reader.getU32();
    model.zeroOffsetMotor.reserve(nOffset);
    for (std::uint32_t i = 0; i < nOffset; ++i) {
        model.zeroOffsetMotor.push_back(reader.getF64());
    }
    const std::uint32_t nRated = reader.getU32();
    model.ratedTorque.reserve(nRated);
    for (std::uint32_t i = 0; i < nRated; ++i) {
        if (reader.getU8() != 0) {
            model.ratedTorque.push_back(reader.getF64());
        } else {
            model.ratedTorque.push_back(std::nullopt);
        }
    }

    model.identity = getIdentity(reader);
    reader.requireEnd();
    return model;
}

// =====================================================================
// JointSeriesView 编解码（切片 UpstreamResult 条目载荷）
// =====================================================================

std::vector<std::uint8_t> encodeJointSeries(const JointSeriesView& series)
{
    ByteWriter w;
    w.reserve(64 + series.samples.size() * 48);
    w.putBytes(reinterpret_cast<const std::uint8_t*>(kMagicSeries), 7);
    w.putU32(kSeriesCodecVersion);

    w.putU32(static_cast<std::uint32_t>(series.jointIds.size()));
    for (const core::ObjectId& id : series.jointIds) {
        putObjectId(w, id);
    }
    putObjectId(w, series.caseId);
    putDigest(w, series.upstreamSliceId.bytes);

    w.putU32(static_cast<std::uint32_t>(series.samples.size()));
    for (const JointDriveSample& s : series.samples) {
        requireFinite(s.t, "sample.t");
        requireFinite(s.q, "sample.q");
        requireFinite(s.qd, "sample.qd");
        requireFinite(s.qdd, "sample.qdd");
        requireFinite(s.tauJoint, "sample.tauJoint");
        w.putF64(s.t);
        w.putF64(s.q);
        w.putF64(s.qd);
        w.putF64(s.qdd);
        w.putF64(s.tauJoint);
        w.putString(s.segmentId);
    }
    return w.bytes();
}

JointSeriesView decodeJointSeries(const std::vector<std::uint8_t>& bytes)
{
    ByteReader reader(bytes);
    reader.require(8);
    if (std::memcmp(bytes.data(), kMagicSeries, 7) != 0) {
        throw std::invalid_argument("drivetrain codec：JointSeriesView 载荷 magic 不符");
    }
    for (std::size_t i = 0; i < 7; ++i) {
        (void)reader.getU8();
    }
    if (reader.getU32() != kSeriesCodecVersion) {
        throw std::invalid_argument("drivetrain codec：JointSeriesView 载荷版本不符"
                                    "（拒绝多版本兼容读——CON-04 契约版本化）");
    }

    JointSeriesView series;
    const std::uint32_t nIds = reader.getU32();
    series.jointIds.reserve(nIds);
    for (std::uint32_t i = 0; i < nIds; ++i) {
        series.jointIds.push_back(getObjectId(reader));
    }
    series.caseId = getObjectId(reader);
    series.upstreamSliceId.bytes = getDigest(reader);

    const std::uint32_t nSamples = reader.getU32();
    series.samples.reserve(nSamples);
    for (std::uint32_t i = 0; i < nSamples; ++i) {
        JointDriveSample s;
        s.t = reader.getF64();
        s.q = reader.getF64();
        s.qd = reader.getF64();
        s.qdd = reader.getF64();
        s.tauJoint = reader.getF64();
        s.segmentId = reader.getString();
        series.samples.push_back(std::move(s));
    }
    reader.requireEnd();
    return series;
}

// =====================================================================
// DriveTrainMappingOutput 编解码（payload canonical 字节）
// =====================================================================

std::vector<std::uint8_t> encodeMappingOutput(const DriveTrainMappingOutput& output)
{
    ByteWriter w;
    w.reserve(512);
    w.putBytes(reinterpret_cast<const std::uint8_t*>(kMagicOutput), 7);
    w.putU32(kOutputCodecVersion);

    // 身份块（§6.2 结果身份——消费方核对同一矩阵内容身份）。
    putIdentity(w, output.identity);
    putDigest(w, output.upstreamSliceId.bytes);
    w.putU32(output.algorithmVersion);
    w.putU32(output.contractVersion);
    putObjectId(w, output.caseId);
    w.putU8(static_cast<std::uint8_t>(output.completeness));
    w.putU32(static_cast<std::uint32_t>(output.missingItems.size()));
    for (const std::string& item : output.missingItems) {
        w.putString(item);
    }

    // 反射惯量（§9——逐轴数值事实；v2 增补窗口投影标记——§9.5 限定语）。
    w.putU32(static_cast<std::uint32_t>(output.inertia.axes.size()));
    for (const ReflectedInertiaAxis& ax : output.inertia.axes) {
        w.putU64(ax.jointIndex);
        requireFinite(ax.jReflectedJointSide, "inertia.jReflectedJointSide");
        w.putF64(ax.jReflectedJointSide);
        w.putU8(ax.inertiaRatio.has_value() ? 1 : 0);
        if (ax.inertiaRatio.has_value()) {
            requireFinite(*ax.inertiaRatio, "inertia.inertiaRatio");
            w.putF64(*ax.inertiaRatio);
        }
        w.putU8(ax.windowProjected ? 1 : 0); // v2——窗口投影限定（R1 恒 0）
    }
    // v2 增补：R2 完整关节轴系反射惯量矩阵（§9.3——含交叉惯量项；R1 恒
    // 缺省）。optional<RowMatrix>＝u8 有无标志＋有值时 rows/cols/逐元素。
    w.putU8(output.inertia.jointSideFullMatrix.has_value() ? 1 : 0);
    if (output.inertia.jointSideFullMatrix.has_value()) {
        const RowMatrix& jref = *output.inertia.jointSideFullMatrix;
        if (!jref.wellFormed()) {
            throw std::invalid_argument("drivetrain codec：inertia.jointSideFullMatrix"
                                        " 结构失配（rows*cols != data.size()）");
        }
        w.putU64(jref.rows);
        w.putU64(jref.cols);
        for (double v : jref.data) {
            requireFinite(v, "inertia.jointSideFullMatrix");
            w.putF64(v);
        }
    }

    // 电机侧序列（§6.2 全列——11 个 double＋2 个 u8）。
    w.putU32(static_cast<std::uint32_t>(output.motorSeries.size()));
    for (const MotorSeries& ms : output.motorSeries) {
        putObjectId(w, ms.axisId);
        w.putU64(ms.jointIndex);
        w.putU32(static_cast<std::uint32_t>(ms.samples.size()));
        for (const MotorDriveSample& s : ms.samples) {
            requireFinite(s.t, "motor.t");
            requireFinite(s.theta, "motor.theta");
            requireFinite(s.thetaDot, "motor.thetaDot");
            requireFinite(s.thetaDDot, "motor.thetaDDot");
            requireFinite(s.tauIdeal, "motor.tauIdeal");
            requireFinite(s.tauMotor, "motor.tauMotor");
            requireFinite(s.pJoint, "motor.pJoint");
            requireFinite(s.pTransmission, "motor.pTransmission");
            requireFinite(s.pRotor, "motor.pRotor");
            requireFinite(s.pMotor, "motor.pMotor");
            w.putF64(s.t);
            w.putF64(s.theta);
            w.putF64(s.thetaDot);
            w.putF64(s.thetaDDot);
            w.putF64(s.tauIdeal);
            w.putF64(s.tauMotor);
            w.putF64(s.pJoint);
            w.putF64(s.pTransmission);
            w.putF64(s.pRotor);
            w.putF64(s.pMotor);
            w.putU8(s.efficiencyApplicable ? 1 : 0);
            w.putU8(static_cast<std::uint8_t>(s.quadrant));
        }
    }

    // 工作点（§11.1 字段表——缺失清单在输出级已承载，点内不再重复）。
    w.putU32(static_cast<std::uint32_t>(output.points.size()));
    for (const MotorOperatingPoint& p : output.points) {
        putObjectId(w, p.axisId);
        putObjectId(w, p.jointId);
        w.putU64(p.jointIndex);
        putPeak(w, p.tauPeakPos);
        putPeak(w, p.tauPeakNeg);
        putPeak(w, p.omegaPeak);
        putPeak(w, p.powerPeak);
        requireFinite(p.tauRms, "point.tauRms");
        requireFinite(p.omegaRms, "point.omegaRms");
        w.putF64(p.tauRms);
        w.putF64(p.omegaRms);
        w.putU8(p.loadRatio.has_value() ? 1 : 0);
        if (p.loadRatio.has_value()) {
            requireFinite(*p.loadRatio, "point.loadRatio");
            w.putF64(*p.loadRatio);
        }
        w.putU8(p.etaApplied.has_value() ? 1 : 0);
        if (p.etaApplied.has_value()) {
            putEfficiency(w, *p.etaApplied);
        }
        requireFinite(p.reflectedInertia, "point.reflectedInertia");
        w.putF64(p.reflectedInertia);
        w.putU8(p.inertiaRatio.has_value() ? 1 : 0);
        if (p.inertiaRatio.has_value()) {
            requireFinite(*p.inertiaRatio, "point.inertiaRatio");
            w.putF64(*p.inertiaRatio);
        }
        putQuadrantStats(w, p.q1);
        putQuadrantStats(w, p.q2);
        putQuadrantStats(w, p.q3);
        putQuadrantStats(w, p.q4);
        putQuadrantStats(w, p.zeroDwell);
        putEnergy(w, p.energy);
        putObjectId(w, p.caseId);
        w.putString(p.segmentId);
        w.putU8(static_cast<std::uint8_t>(p.quality));
        w.putU8(p.estimatedSource ? 1 : 0);
    }
    return w.bytes();
}

DriveTrainMappingOutput decodeMappingOutput(const std::vector<std::uint8_t>& bytes)
{
    ByteReader reader(bytes);
    reader.require(8);
    if (std::memcmp(bytes.data(), kMagicOutput, 7) != 0) {
        throw std::invalid_argument("drivetrain codec：MappingOutput 载荷 magic 不符");
    }
    for (std::size_t i = 0; i < 7; ++i) {
        (void)reader.getU8();
    }
    if (reader.getU32() != kOutputCodecVersion) {
        throw std::invalid_argument("drivetrain codec：MappingOutput 载荷版本不符"
                                    "（拒绝多版本兼容读——CON-04 契约版本化）");
    }

    DriveTrainMappingOutput output;
    output.identity = getIdentity(reader);
    output.upstreamSliceId.bytes = getDigest(reader);
    output.algorithmVersion = reader.getU32();
    output.contractVersion = reader.getU32();
    output.caseId = getObjectId(reader);
    output.completeness = enumFromU8(reader.getU8(), CompletenessState::Partial,
                                     "output.completeness");
    const std::uint32_t nMissing = reader.getU32();
    output.missingItems.reserve(nMissing);
    for (std::uint32_t i = 0; i < nMissing; ++i) {
        output.missingItems.push_back(reader.getString());
    }

    const std::uint32_t nAxes = reader.getU32();
    output.inertia.axes.reserve(nAxes);
    for (std::uint32_t i = 0; i < nAxes; ++i) {
        ReflectedInertiaAxis ax;
        ax.jointIndex = reader.getU64();
        ax.jReflectedJointSide = reader.getF64();
        requireFinite(ax.jReflectedJointSide, "inertia.jReflectedJointSide");
        if (reader.getU8() != 0) {
            ax.inertiaRatio = reader.getF64();
        }
        ax.windowProjected = (reader.getU8() != 0); // v2——窗口投影限定
        output.inertia.axes.push_back(std::move(ax));
    }
    // v2 增补：R2 完整关节轴系反射惯量矩阵（optional——u8 有无标志）。
    if (reader.getU8() != 0) {
        RowMatrix jref;
        jref.rows = reader.getU64();
        jref.cols = reader.getU64();
        const std::uint64_t nElems = jref.rows * jref.cols;
        jref.data.reserve(nElems);
        for (std::uint64_t e = 0; e < nElems; ++e) {
            const double v = reader.getF64();
            requireFinite(v, "inertia.jointSideFullMatrix");
            jref.data.push_back(v);
        }
        output.inertia.jointSideFullMatrix = std::move(jref);
    }

    const std::uint32_t nSeries = reader.getU32();
    output.motorSeries.reserve(nSeries);
    for (std::uint32_t k = 0; k < nSeries; ++k) {
        MotorSeries ms;
        ms.axisId = getObjectId(reader);
        ms.jointIndex = reader.getU64();
        const std::uint32_t nSamples = reader.getU32();
        ms.samples.reserve(nSamples);
        for (std::uint32_t i = 0; i < nSamples; ++i) {
            MotorDriveSample s;
            s.t = reader.getF64();
            s.theta = reader.getF64();
            s.thetaDot = reader.getF64();
            s.thetaDDot = reader.getF64();
            s.tauIdeal = reader.getF64();
            s.tauMotor = reader.getF64();
            s.pJoint = reader.getF64();
            s.pTransmission = reader.getF64();
            s.pRotor = reader.getF64();
            s.pMotor = reader.getF64();
            s.efficiencyApplicable = (reader.getU8() != 0);
            s.quadrant = enumFromU8(reader.getU8(), Quadrant::ZeroDwell, "motor.quadrant");
            ms.samples.push_back(std::move(s));
        }
        output.motorSeries.push_back(std::move(ms));
    }

    const std::uint32_t nPoints = reader.getU32();
    output.points.reserve(nPoints);
    for (std::uint32_t k = 0; k < nPoints; ++k) {
        MotorOperatingPoint p;
        p.axisId = getObjectId(reader);
        p.jointId = getObjectId(reader);
        p.jointIndex = reader.getU64();
        p.tauPeakPos = getPeak(reader);
        p.tauPeakNeg = getPeak(reader);
        p.omegaPeak = getPeak(reader);
        p.powerPeak = getPeak(reader);
        p.tauRms = reader.getF64();
        p.omegaRms = reader.getF64();
        requireFinite(p.tauRms, "point.tauRms");
        requireFinite(p.omegaRms, "point.omegaRms");
        if (reader.getU8() != 0) {
            p.loadRatio = reader.getF64();
        }
        if (reader.getU8() != 0) {
            p.etaApplied = getEfficiency(reader);
        }
        p.reflectedInertia = reader.getF64();
        requireFinite(p.reflectedInertia, "point.reflectedInertia");
        if (reader.getU8() != 0) {
            p.inertiaRatio = reader.getF64();
        }
        p.q1 = getQuadrantStats(reader);
        p.q2 = getQuadrantStats(reader);
        p.q3 = getQuadrantStats(reader);
        p.q4 = getQuadrantStats(reader);
        p.zeroDwell = getQuadrantStats(reader);
        p.energy = getEnergy(reader);
        p.caseId = getObjectId(reader);
        p.segmentId = reader.getString();
        p.quality = enumFromU8(reader.getU8(), CompletenessState::Partial, "point.quality");
        p.estimatedSource = (reader.getU8() != 0);
        output.points.push_back(std::move(p));
    }
    reader.requireEnd();
    return output;
}

// =====================================================================
// 上游序列物化锚（P-DT-6 对齐前的本卡提议承载——规则单点，见 Codec.hpp）
// =====================================================================

core::ObjectId jointSeriesAnchor(const core::ContentIdentity& upstreamSliceId)
{
    // 确定性派生：ObjectId.bytes ＝ 上游切片身份摘要的前 16 字节——同上游
    // 切片必得同锚（无随机、无环境依赖——NFR-COR-02）；组装方/评估器/
    // 契约测试共用本函数，禁第二处字面派生。
    core::ObjectId anchor;
    for (std::size_t i = 0; i < 16; ++i) {
        anchor.bytes[i] = upstreamSliceId.bytes[i];
    }
    return anchor;
}

}  // namespace sdurws::ird::drivetrain
