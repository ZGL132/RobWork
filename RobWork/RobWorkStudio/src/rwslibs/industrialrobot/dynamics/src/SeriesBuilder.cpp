/**
 * @file   SeriesBuilder.cpp
 * @brief  动力学序列构建器实现（units/dynamics.md §10.3/§4.4/§4.6）——
 *         行收集与时间单调防御、身份块冻结、排序与完整性计数派生、
 *         canonical 内容身份（SHA-256）计算。
 *
 * 设计依据（契约面见同名公共头 SeriesBuilder.hpp 文件头）：
 *   - units/dynamics.md §10.3（IDynamicsSeriesBuilder 契约表——前置/
 *     职责/合法与非法示例）、§4.4（DynamicsSeries 字段序＝canonical
 *     编码字段序的权威）、§4.6（序列纪律——排序/缺口/非有限/Empty）、
 *     §10.0（通用约定——缺身份拒绝、确定性、线程约束）
 *   - 需求 DYN-03（序列输出）、CON-05（内容寻址——contentIdentity）、
 *     NFR-COR-02（确定性：同输入字节→同摘要）、NFR-COR-03（非有限不
 *     静默——非 Ok 行不抹平，计数如实）
 *   - 任务契约 tasks/foundation/WP-17-T04.json（acceptance 1 序列输出）
 *
 * ★ canonical 编码规则（contentIdentity 的唯一实现点——本域内容寻址
 *   的确定性来源；消费方不得另写第二套编码）：
 *   1. 域分隔 magic "IRDDYNS1"（8 字节 ASCII）起头——防与其它域/其它
 *      序列版本的摘要混同（同 core Digest.hpp"对什么做摘要归各所有者
 *      单元"的登记精神）；
 *   2. 字段序＝§4.4 DynamicsSeries 原文字段序（身份块→内容块），样本行
 *      内＝DynamicsSample 原文字段序——与卡面逐字段对应，review 可
 *      逐项对账；
 *   3. 标量编码全部**显式小端**（整型逐字节移位、double 经 IEEE-754 位
 *      模式 reinterpret 后小端拆字节）——不依赖平台字节序（MSVC/x64
 *      本身小端，显式编码使意图可见、防移植期静默漂移）；NaN 位模式在
 *      同一二进制内确定（评估器恒产 quiet_NaN——同输入同摘要，
 *      NFR-COR-02 的"同二进制内逐位可复现"口径）；
 *   4. 字符串（algorithmVersion/dynConfigDigest）＝u32 长度前缀＋UTF-8
 *      字节（无终结符——长度定界防拼接歧义）；
 *   5. 枚举（jointType/numericState/completeness/forwardCheck）＝底层
 *      值 u8；
 *   6. 诊断引用 diagRefs＝u32 数量＋逐个 16 字节 id（排序后编码——
 *      diagRefs 自身无业务序，编码序取 ObjectId 字典序使清单等价于
 *      集合：同集合同摘要）。
 *
 * 复杂度：O(n)——n 为样本行数（每行固定 176 字节进摘要）；10⁵ 行级
 *   （R-DYN-1 规模）SHA-256 吞吐下为几十毫秒量级，评估链可接受。
 */

#include <sdurws/ird/dynamics/SeriesBuilder.hpp>

#include <sdurws/ird/dynamics/DiagCodes.hpp> // DYN-* 码值常量（唯一书写点——产码共用）

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace sdurws::ird::dynamics {

namespace {

// =====================================================================
// 摘要流写入助手（canonical 小端编码——见文件头编码规则）。
// =====================================================================

/// 摘要流包装：update 转发＋各标量类型的显式小端编码（唯一编码实现点）。
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

/// DynamicsSample 单行的 canonical 编码（字段序＝DynTypes.hpp 原文序——
/// 文件头编码规则 2；单位/语义见 DynTypes.hpp 逐字段注释）。
void writeSampleRow(DigestWriter& w, const DynamicsSample& r)
{
    w.f64(r.t);                       // s
    w.u32(r.segmentIndex);
    w.id16(r.conditionId);
    w.u32(r.jointIndex);
    w.id16(r.jointObjectId);
    w.u8(static_cast<unsigned>(r.jointType));
    w.f64(r.q);                       // rad 或 m
    w.f64(r.qd);                      // rad/s 或 m/s
    w.f64(r.qdd);                     // rad/s² 或 m/s²
    w.f64(r.tauGravity);              // N·m 或 N
    w.f64(r.tauInertia);
    w.f64(r.tauCoriolisCentrifugal);
    w.f64(r.tauFriction);
    w.f64(r.tauExternal);
    w.f64(r.tauTotal);
    w.f64(r.mechanicalPower);         // W
    w.f64(r.energyIntegralJ);         // J
    w.u32(r.payloadVariantIndex);
    w.id16(r.toolObjectId);
    w.u8(static_cast<unsigned>(r.numericState));
}

}  // namespace

// =====================================================================
// addSample（公共头契约的实现——时间单调非降防御）。
// =====================================================================

void DynamicsSeriesBuilder::addSample(const DynamicsSample& sample)
{
    // 防御性二次校验（§10.3 前置行——正式校验在评估入口 §4.3，此处为
    // 组装面防线）：时间倒退＝DYN-SERIES-NON-MONOTONIC 语义，fail-fast
    // 不排序修复（调用方错误轨——异常 message 携带实测时间比较数据）。
    if (mHaveLastT && !(sample.t >= mLastT)) {
        throw DynamicsError("series-non-monotonic",
                            "序列样本时间倒退（新行 t=" + std::to_string(sample.t)
                                + " < 前行 t=" + std::to_string(mLastT)
                                + "）——DYN-SERIES-NON-MONOTONIC 语义，"
                                "不排序修复（§4.6）");
    }
    mLastT = sample.t;
    mHaveLastT = true;
    mSamples.push_back(sample);
}

// =====================================================================
// finalize（公共头契约的实现——身份校验→排序→计数→冻结→内容身份）。
// =====================================================================

DynamicsSeries DynamicsSeriesBuilder::finalize(SeriesIdentity identity)
{
    // ---- 第 1 步：身份块完整性校验（§10.0"身份要求"行——缺身份拒绝，
    //      调用方错误 fail-fast；逐项 detail 中文定位）----
    if (!identity.snapshotId.isValid()) {
        throw DynamicsError("identity-missing", "序列身份缺失：snapshotId 全零——拒绝冻结");
    }
    if (!identity.sliceId.isValid()) {
        throw DynamicsError("identity-missing", "序列身份缺失：sliceId 全零——拒绝冻结");
    }
    if (!identity.trajectoryPayloadId.isValid()) {
        throw DynamicsError("identity-missing",
                            "序列身份缺失：trajectoryPayloadId 全零——拒绝冻结");
    }
    if (!identity.conditionId.isValid()) {
        throw DynamicsError("identity-missing", "序列身份缺失：工况对象 ID 为空——拒绝冻结");
    }
    if (!identity.task.isValid()) {
        throw DynamicsError("identity-missing",
                            "序列身份缺失：运行身份五元组不完整（task.isValid()==false）——拒绝冻结");
    }
    if (identity.algorithmVersion.empty()) {
        throw DynamicsError("identity-missing",
                            "序列身份缺失：algorithmVersion 空串——结果不可追溯，拒绝冻结");
    }
    if (identity.dynConfigDigest.empty()) {
        throw DynamicsError("identity-missing",
                            "序列身份缺失：dynConfigDigest 空串（config.dyn 摘要未入身份）——拒绝冻结");
    }

    // ---- 第 2 步：行序稳定排序（§4.6/NFR-COR-02——(t 升序, jointIndex
    //      升序)；单工况内 conditionId 恒同不参与键。时间倒退已在
    //      addSample fail-fast，此处只修复"同刻度内关节行乱序"）----
    std::stable_sort(mSamples.begin(), mSamples.end(),
                     [](const DynamicsSample& a, const DynamicsSample& b) {
                         if (a.t != b.t) { return a.t < b.t; }
                         return a.jointIndex < b.jointIndex;
                     });

    // ---- 第 3 步：重复 (t, jointIndex) 行对检出（§10.3 非法示例"重复
    //      时间戳 addSample 不报错但 finalize 标记"——防御性二次校验：
    //      正常评估器输出无此形态，检出即标记素材＋压 Partial，不静默
    //      放行为 Complete）----
    std::vector<core::ObjectId> diagRefs;  // 标记清单从空起步（validitySeed 无诊断
                                           //   通道——素材仅在本步产生）
    bool hasDuplicateRows = false;
    for (std::size_t k = 1; k < mSamples.size(); ++k) {
        if (mSamples[k].t == mSamples[k - 1].t
            && mSamples[k].jointIndex == mSamples[k - 1].jointIndex) {
            hasDuplicateRows = true;
            // 标记素材（ERR-01 字段齐备——code 取自码值常量唯一书写点；
            // subject＝重复行所属关节可定位；cause 携带实测 (t, jointIndex)）。
            diagRefs.push_back(mSamples[k].jointObjectId);
        }
    }

    // ---- 第 4 步：完整性计数派生（§4.6——计数以**样本时刻**为单位：
    //      actual＝不同 t 刻度数；nonFinite＝含非 Ok 行的刻度数；行级
    //      计数会重复计同刻度的 n 个关节行——与评估器口径一致）----
    std::size_t actualSamples = 0;
    std::size_t nonFiniteSamples = 0;
    bool prevRowValid = false;          ///< 前行是否已按刻度计数（同刻度多行只计一次）
    double prevCountedT = 0.0;
    bool prevCountedFinite = false;
    for (const DynamicsSample& r : mSamples) {
        const bool finiteRow = r.numericState == SampleNumericState::Ok;
        if (!prevRowValid || r.t != prevCountedT) {
            // 新刻度（或首行）——按刻度计数一次。
            ++actualSamples;
            if (!finiteRow) { ++nonFiniteSamples; }
            prevCountedT = r.t;
            prevCountedFinite = finiteRow;
        } else if (prevCountedFinite && !finiteRow) {
            // 同刻度内发现非 Ok 行（评估器按整样本统一标记——理论不达，
            // 防御面：该刻度补记为非有限，不静默放行）。
            ++nonFiniteSamples;
            prevCountedFinite = false;
        }
        prevRowValid = true;
    }

    // ---- 第 5 步：validity 组装（Seed 事实透传＋派生字段覆写——见
    //      SeriesIdentity 注释；Empty/Complete/Partial 判定＝§4.6）----
    DynamicsSeries out;
    out.validity = identity.validitySeed;  // 事实字段（摩擦缺失/估算计数/forwardCheck）
    if (mSamples.empty()) {
        // 空序列＝Empty 语义（§7.6：不产出统计，绝不 0 值伪装——序列
        // 本身仍可冻结〔身份块＋Empty〕，统计器据 Empty 不产出）。
        out.validity.completeness = DynamicsValidity::Completeness::Empty;
    } else if (actualSamples == identity.plannedSampleCount && nonFiniteSamples == 0
               && !hasDuplicateRows) {
        out.validity.completeness = DynamicsValidity::Completeness::Complete;
    } else {
        // 缺口（actual<planned）、非有限刻度、重复行对任一存在→Partial
        // （§4.6"缺样本/采样间隙"行——不插值补齐，统计可继续）。
        out.validity.completeness = DynamicsValidity::Completeness::Partial;
    }
    out.validity.plannedSampleCount = identity.plannedSampleCount;
    out.validity.actualSampleCount = actualSamples;
    out.validity.nonFiniteCount = nonFiniteSamples;
    out.diagRefs = std::move(diagRefs);

    // ---- 第 6 步：身份块冻结（identity → 序列逐字段拷入）----
    out.snapshotId = identity.snapshotId;
    out.sliceId = identity.sliceId;
    out.trajectoryPayloadId = identity.trajectoryPayloadId;
    out.conditionId = identity.conditionId;
    out.toolObjectId = identity.toolObjectId;
    out.evaluatorContractVersion = identity.evaluatorContractVersion;
    out.algorithmVersion = identity.algorithmVersion;
    out.dynConfigDigest = identity.dynConfigDigest;
    out.task = identity.task;

    // ---- 第 7 步：canonical 内容身份（SHA-256——编码规则见文件头；
    //      字段序＝§4.4 原文序，同输入字节必得同摘要，NFR-COR-02）----
    {
        core::ContentDigester d;
        DigestWriter w(d);
        w.magic("IRDDYNS1", 8);           // 域分隔 magic（版本 1）
        // —— 身份块 ——
        w.cid32(out.snapshotId);
        w.cid32(out.sliceId);
        w.cid32(out.trajectoryPayloadId);
        w.id16(out.conditionId);
        w.id16(out.toolObjectId);
        w.u32(out.evaluatorContractVersion);
        w.str(out.algorithmVersion);
        w.str(out.dynConfigDigest);
        w.idBytes(out.task.project.bytes);
        w.idBytes(out.task.branch.bytes);
        w.idBytes(out.task.revision.bytes);
        w.idBytes(out.task.run.bytes);
        w.u64(out.task.attempt.value);   // AttemptId＝u64 尝试序号（非 Id128）
        // —— 内容块：validity ——
        w.u8(static_cast<unsigned>(out.validity.completeness));
        w.u64(out.validity.plannedSampleCount);
        w.u64(out.validity.actualSampleCount);
        w.u64(out.validity.nonFiniteCount);
        w.u64(out.validity.estimatedLinkCount);
        w.u64(out.validity.estimatedPayloadCount);
        w.u8(out.validity.frictionMissing ? 1u : 0u);
        w.u8(out.validity.externalValidationPending ? 1u : 0u);
        w.u8(static_cast<unsigned>(out.validity.forwardCheck));
        // —— 内容块：diagRefs（ObjectId 字典序后编码——清单等价集合）——
        std::vector<core::ObjectId> sortedRefs = out.diagRefs;
        std::sort(sortedRefs.begin(), sortedRefs.end());
        w.u32(static_cast<std::uint32_t>(sortedRefs.size()));
        for (const core::ObjectId& oid : sortedRefs) { w.id16(oid); }
        // —— 内容块：样本行（已排序——第 2 步）——
        w.u64(mSamples.size());
        for (const DynamicsSample& r : mSamples) { writeSampleRow(w, r); }
        out.contentIdentity.bytes = d.finalize();
    }

    // ---- 第 8 步：交付并清空缓冲（finalize 后实例可复用于下一工况——
    //      每工况一构建器为使用约定，复用不跨工况串数据）----
    out.samples = std::move(mSamples);
    mSamples.clear();
    mHaveLastT = false;
    mLastT = 0.0;
    return out;
}

}  // namespace sdurws::ird::dynamics
