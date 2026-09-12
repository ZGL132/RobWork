/**
 * @file   Snapshot.cpp
 * @brief  快照组装/校验/规范编码实现——SnapshotBuilder 冻结协议（§4.1.5①～
 *         ④a）＋SnapshotCodec 双形态编解码（§5.2 canonical 规则＋§4.1.5⑤
 *         worker 投递＋④b 载荷级校验）＋RequiredCaseSet 冻结凭据计算。
 *
 * 设计依据：
 *   - units/evidence.md §4.1.2（字段表与非法实例清单）、§4.1.3（RequiredCaseSet：
 *     caseId 唯一、requiredCaseSetId＝entries 规范编码摘要）、§4.1.4（采样
 *     计划身份）、§4.1.5（组装协议：①锚定修订/②逐类录入/③冻结/④a builder
 *     即时验证/④b 载荷级校验/⑤双形态同身份）、§5.1（snapshotId＝refs-only
 *     的 SHA-256）、§5.2（编码规则：magic IRDSNAP1/大端/长度前缀/presence
 *     字节/版本化/纯函数）
 *   - 需求 CON-01（不可变输入闭包）、CON-03（外部资源 Recorded→Solidified
 *     单向）、CON-05/06（内容寻址/策略与名称映射身份非空）、NFR-COR-02/03
 *   - 任务契约 tasks/foundation/EV-T03.json（≙WP-05-T03）acceptance 1～3
 *
 * == 实现口径登记（偏差均属"实现细节精确化"，语义与设计一致——单元卡
 *    v0.4 变更记录同步登记，DTB §5.4）==
 *   I-1 revisionSeq 不参与编码与 snapshotId（§4.1.2"不参与内容身份语义
 *       判断"原文的机械落实）；parse 恢复为默认 0——往返等值断言排除该字段。
 *   I-2 ObjectRefEntry.digest＝对象载荷字节 SHA-256 裸字节（与 contentVersion
 *       同源——core §4.2 ContentVersion 即内容摘要版本戳）；builder 校验
 *       digest==contentVersion.bytes（冗余承载的一致性闸门）。
 *   I-3 builder 冻结时对集合类成员规范化排序（closure 按 objectId、
 *       configurationRefs 按 configKindToken、caseSet.entries 按 caseId、
 *       externalResources 按 resourceId、samplingPlans 按 regionObjectId；
 *       codecVersions 保序承载）——同内容任意插入序必得同 snapshotId；
 *       同集重复键拒绝（objectId/configKindToken/resourceId/regionObjectId
 *       ——歧义防错，EV-T02 声明键唯一同源口径）。
 *   I-4 requiredCaseSetId＝SHA-256 over("IRDCASE1"＋条目数＋按 caseId 序的
 *       {caseId,label,enabled,mandatory} 规范串)——"entries 规范编码摘要"
 *       原文的字节级定义；builder/parse 双端同函数计算（身份是导出值，
 *       不入编码载荷）。
 *   I-5 编码布局含形态字节（0=refs-only,1=materialized）；snapshotId 恒对
 *       refs-only 形态（form=0）字节计算——双形态同身份（§4.1.5⑤/D-01）。
 *   I-6 parse 结构性非法（magic/版本/形态/截断/NUL/尾随字节）→
 *       SnapshotIncomplete（detail 带偏移）；载荷摘要不符 → SnapshotIntegrity
 *       （§4.1.5④b 原文码面）。materialized 在 encode（物化）与 parse（接收）
 *       两端都执行④b——接收端复核是传输损坏的检测点。
 *   I-7 caseSet.entries 允许空集（P-EV-7 场景在汇总判定层保守处置，
 *       不在组装层拒绝）；enabled/mandatory 字面承载不解释（O-14/P-EV-9）。
 *
 * builder 校验顺序（固定——同一坏组装必报同一首错，NFR-COR-02）：
 *   身份三元组 → 策略（CON-06）→ 名称映射（CON-06）→ 复现块 → 对象闭包
 *   （逐条字段 → digest 一致 → 重复 → 修订闭包包含性/防混入）→ 配置引用
 *   → 工况集（唯一性）→ 外部资源（状态机/唯一）→ 采样计划（身份/唯一）。
 *
 * == SnapshotCodec 字节布局（版本 1；定宽整型一律大端）==
 *   [0..7]   magic "IRDSNAP1"（§5.2 原文）
 *   [8]      codec 版本（=1）
 *   [9]      形态字节：0=refs-only、1=materialized（I-5）
 *   ---- refs-only 体（snapshotId 的摘要对象＝form=0 的完整编码）----
 *   [16+16+16] project/branch/revision 原始字节（revisionSeq 不编码——I-1）
 *   [4] 闭包条数；每条（objectId 序）：[16 oid][32 cv][2 len token][32 digest]
 *   [4] 配置条数；每条（token 序）：[2 len token][4 len bytes][32 cid]
 *   [32] 策略内容身份；[32] 名称映射内容身份（均必填——builder 已验非零）
 *   [4] 工况条数；每条（caseId 序）：[16 caseId][2 len label][1 enabled]
 *       [1 mandatory]
 *   [4] 外部资源条数；每条（resourceId 序）：[16 rid][1 state][1 presence]
 *       （presence=1 时跟 [32] 固化版本）
 *   [4] 采样计划条数；每条（regionId 序）：[16 rid][32 planId][8 posSamples]
 *       [8 poseSamples][32 sampleSetId]
 *   复现块：[2 len productVersion][2 len evidenceContractVersion][4 条数]
 *       每条（保序）[2 len]；[1 presence]（=1 跟 [2 len] 编译器契约版本）；
 *       [1 presence]（=1 跟 [2 len] 碰撞后端版本）
 *   ---- materialized 附加段（form=1 专属）----
 *   [4] 载荷条数；每条（objectId 序）：[16 oid][4 len bytes]
 *
 * 编解码共用同一套体编码/体解析函数（encodeRefsOnlyBody/parseBody）——
 * 布局单点实现，双形态/双入口不漂移（往返用例另有钉住）。
 *
 * 确定性：编码为纯字节拼装（无隐藏状态）；解析为界限检查的顺序读取；
 * 同输入恒同字节/同结构（NFR-COR-02）。线程安全：无共享可变状态（可重入）。
 */

#include <sdurws/ird/evidence/Snapshot.hpp>

#include <algorithm>
#include <utility>

namespace sdurws::ird::evidence {

namespace {

// =====================================================================
// 字节拼装/读取原语（§5.2：定宽整型大端、长度前缀、无填充——跨进程一致）
// =====================================================================

/// 追加单字节。
void appendU8(std::vector<std::uint8_t>& out, std::uint8_t v)
{
    out.push_back(v);
}

/// 追加 16 位整型（大端——高位在前，跨端序一致）。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

/// 追加 32 位整型（大端）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

/// 追加 64 位整型（大端）。
void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(v >> shift));
    }
}

/// 追加原始字节序列。
void appendRaw(std::vector<std::uint8_t>& out, const std::uint8_t* p, std::size_t n)
{
    out.insert(out.end(), p, p + n);
}

/// 追加"2 字节长度＋字符串"（§5.2 字符串规则：UTF-8、禁 NUL——编码入口
/// 复核是 builder 校验的再入防线：encode 是公共 API，可能遭遇未过 builder
/// 的手改快照，属调用方契约违约即拒绝）。
/// @throws EvidenceError(SnapshotIncomplete) 含 NUL 或超出 65535 字节
void appendLengthString(std::vector<std::uint8_t>& out, const std::string& s)
{
    if (s.find('\0') != std::string::npos) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "字符串含 NUL（§5.2 编码安全）");
    }
    if (s.size() > 0xFFFFu) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "字符串超出 65535 字节编码上限（长度 "
                                + std::to_string(s.size()) + "）");
    }
    appendU16(out, static_cast<std::uint16_t>(s.size()));
    appendRaw(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// 追加"4 字节长度＋字节块"（32 位长度上限——截断会伪造长度一致性，
/// 越界必须显式拒绝而非静默回绕，NFR-COR-03）。
/// @throws EvidenceError(SnapshotIncomplete) 超出 2^32-1 字节
void appendLengthBytes(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() > 0xFFFFFFFFu) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "字节块超出 2^32-1 编码上限（长度 "
                                + std::to_string(bytes.size()) + "）");
    }
    appendU32(out, static_cast<std::uint32_t>(bytes.size()));
    appendRaw(out, bytes.data(), bytes.size());
}

/**
 * @brief 界限检查的顺序读取器（解析侧唯一取字节通道——任何越界即抛，
 *        不可能读出未定义字节）。
 *
 * @throws EvidenceError(SnapshotIncomplete) 剩余字节不足/内容非法
 *         （detail 带偏移——结构性非法＝传输损坏/非法输入的定位线索）
 */
struct Reader {
    const std::vector<std::uint8_t>& data; ///< 被读编码（调用方持有）
    std::size_t pos = 0;                   ///< 当前读取偏移（诊断定位用）

    /// 确认剩余至少 n 字节，不足即抛（每个读取原语的第一步）。
    void need(std::size_t n)
    {
        if (data.size() - pos < n) {
            throw EvidenceError(
                EvidenceErrorCode::SnapshotIncomplete,
                "编码截断（偏移 " + std::to_string(pos) + "：需 " + std::to_string(n)
                    + " 字节，剩余 " + std::to_string(data.size() - pos) + "）");
        }
    }

    std::uint8_t readU8()
    {
        need(1);
        return data[pos++];
    }

    std::uint16_t readU16()
    {
        need(2);
        const std::uint16_t hi = data[pos];
        const std::uint16_t lo = data[pos + 1];
        pos += 2;
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }

    std::uint32_t readU32()
    {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | data[pos + static_cast<std::size_t>(i)];
        }
        pos += 4;
        return v;
    }

    std::uint64_t readU64()
    {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v = (v << 8) | data[pos + static_cast<std::size_t>(i)];
        }
        pos += 8;
        return v;
    }

    /// 读取 n 字节定长块（Id128/摘要原始字节）。
    void readBytes(std::uint8_t* out, std::size_t n)
    {
        need(n);
        std::copy_n(data.data() + pos, n, out);
        pos += n;
    }

    /// 读取"2 字节长度＋字符串"；NUL 在此拒绝（编码侧已挡，再入防线对称）。
    std::string readLengthString()
    {
        const std::uint16_t len = readU16();
        need(len);
        std::string s(reinterpret_cast<const char*>(data.data() + pos), len);
        pos += len;
        if (s.find('\0') != std::string::npos) {
            throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                                "字符串含 NUL（偏移 " + std::to_string(pos) + "，§5.2）");
        }
        return s;
    }

    /// 读取"4 字节长度＋字节块"。
    std::vector<std::uint8_t> readLengthBytes()
    {
        const std::uint32_t len = readU32();
        need(len);
        std::vector<std::uint8_t> bytes(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                        data.begin() + static_cast<std::ptrdiff_t>(pos)
                                            + len);
        pos += len;
        return bytes;
    }
};

// =====================================================================
// 摘要与身份原语（SHA-256 全部经 core ContentDigester——D-05 同源，
// evidence 不出现第二摘要实现）
// =====================================================================

/// 对字节区间做 SHA-256（ContentDigester 的一次性便捷封装）。
core::Digest256 sha256(const std::uint8_t* p, std::size_t n)
{
    core::ContentDigester digester;
    digester.update(p, n);
    return digester.finalize();
}

/// Digest256 → ContentIdentity（同字节强类型提升——摘要即身份，§5.1）。
core::ContentIdentity toIdentity(const core::Digest256& d)
{
    core::ContentIdentity id;
    id.bytes = d;
    return id;
}

// =====================================================================
// RequiredCaseSet 冻结凭据（I-4：requiredCaseSetId 的字节级定义）
// =====================================================================

/// entries 规范编码摘要：magic "IRDCASE1"＋条目数＋按 caseId 序的
/// {caseId, label, enabled, mandatory} 规范串（label 长度前缀；标记单字节
/// 0/1）。输入必须已按 caseId 排序（builder/parse 双端都喂冻结序——同一
/// 工况集必得同凭据，§6.6 覆盖矩阵的核对基准）。
core::Digest256 digestCaseSetEntries(const std::vector<CaseEntry>& sortedEntries)
{
    std::vector<std::uint8_t> buf;
    buf.reserve(16 + sortedEntries.size() * 24);
    appendRaw(buf, reinterpret_cast<const std::uint8_t*>("IRDCASE1"), 8);
    appendU32(buf, static_cast<std::uint32_t>(sortedEntries.size()));
    for (const CaseEntry& e : sortedEntries) {
        appendRaw(buf, e.caseId.bytes.data(), e.caseId.bytes.size());
        appendLengthString(buf, e.label);
        appendU8(buf, e.enabled ? 1u : 0u);
        appendU8(buf, e.mandatory ? 1u : 0u);
    }
    return sha256(buf.data(), buf.size());
}

// =====================================================================
// 组装校验的小工具（确定性失败信息）
// =====================================================================

/// 错误消息内字符串截断（超长 label/token 防日志爆炸——纯诊断用途）。
std::string clipForMessage(const std::string& s)
{
    constexpr std::size_t kMax = 32;   // 消息内最长原文（超过加省略号）
    if (s.size() <= kMax) {
        return s;
    }
    return s.substr(0, kMax) + "…";
}

/// builder 拒绝的统一抛错原语（fail-fast——调用方组装契约违约）。
[[noreturn]] void rejectSnapshot(const std::string& detail)
{
    throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete, detail);
}

/// 版本文本合法性（必填：非空禁 NUL——presence 语义见 §5.2）。
void validateVersionText(const std::string& text, const char* fieldName)
{
    if (text.empty()) {
        rejectSnapshot(std::string("复现块字段为空：") + fieldName);
    }
    if (text.find('\0') != std::string::npos) {
        rejectSnapshot(std::string("复现块字段含 NUL（§5.2 编码安全）：") + fieldName);
    }
}

/// 可选版本文本合法性（若有值必须非空禁 NUL——"有值空串"是噪声）。
void validateOptionalVersionText(const std::optional<std::string>& text,
                                 const char* fieldName)
{
    if (text.has_value()) {
        validateVersionText(*text, fieldName);
    }
}

// =====================================================================
// 编码体/解析体（布局单点实现——布局表见文件头注释，两形态共用）
// =====================================================================

/// refs-only 体的编码（不含 10 字节头——头由两个 encode 入口各自拼装，
/// 便于 materialized 复用同一体）。按布局表逐字段落字节；输入须为冻结序
/// （builder 产出——codec 不排序，见 Snapshot.hpp 契约）。
void encodeRefsOnlyBody(std::vector<std::uint8_t>& out, const AnalysisSnapshot& s)
{
    // 身份三元组（revisionSeq 不编码——I-1）。
    appendRaw(out, s.project.bytes.data(), s.project.bytes.size());
    appendRaw(out, s.branch.bytes.data(), s.branch.bytes.size());
    appendRaw(out, s.revision.bytes.data(), s.revision.bytes.size());

    // 对象引用闭包（冻结序＝objectId 序）。
    appendU32(out, static_cast<std::uint32_t>(s.objectClosure.size()));
    for (const ObjectRefEntry& e : s.objectClosure) {
        appendRaw(out, e.objectId.bytes.data(), e.objectId.bytes.size());
        appendRaw(out, e.contentVersion.bytes.data(), e.contentVersion.bytes.size());
        appendLengthString(out, e.objectTypeToken);
        appendRaw(out, e.digest.data(), e.digest.size());
    }

    // 分析配置引用（冻结序＝configKindToken 序）。
    appendU32(out, static_cast<std::uint32_t>(s.configurationRefs.size()));
    for (const ConfigEntry& e : s.configurationRefs) {
        appendLengthString(out, e.configKindToken);
        appendLengthBytes(out, e.canonicalBytes);
        appendRaw(out, e.contentIdentity.bytes.data(), e.contentIdentity.bytes.size());
    }

    // 策略与名称映射内容身份（必填——builder 已验非零）。
    appendRaw(out, s.policyRef.policyContentIdentity.bytes.data(),
              s.policyRef.policyContentIdentity.bytes.size());
    appendRaw(out, s.nameMapRef.nameMapContentIdentity.bytes.data(),
              s.nameMapRef.nameMapContentIdentity.bytes.size());

    // 必验工况集合（冻结序＝caseId 序；标记单字节 0/1——字面承载，O-14）。
    appendU32(out, static_cast<std::uint32_t>(s.caseSet.entries.size()));
    for (const CaseEntry& e : s.caseSet.entries) {
        appendRaw(out, e.caseId.bytes.data(), e.caseId.bytes.size());
        appendLengthString(out, e.label);
        appendU8(out, e.enabled ? 1u : 0u);
        appendU8(out, e.mandatory ? 1u : 0u);
    }

    // 外部资源固化状态（presence 字节显式编码——§5.2 可选值规则）。
    appendU32(out, static_cast<std::uint32_t>(s.externalResources.size()));
    for (const ExternalResourceState& e : s.externalResources) {
        appendRaw(out, e.resourceId.bytes.data(), e.resourceId.bytes.size());
        appendU8(out, e.state == ExternalResourceStatus::Solidified ? 1u : 0u);
        if (e.solidifiedContentVersion.has_value()) {
            appendU8(out, 1u);
            appendRaw(out, e.solidifiedContentVersion->bytes.data(),
                      e.solidifiedContentVersion->bytes.size());
        } else {
            appendU8(out, 0u);
        }
    }

    // 采样计划（计数 u64 大端；身份原始字节）。
    appendU32(out, static_cast<std::uint32_t>(s.samplingPlans.size()));
    for (const SamplingPlanRef& e : s.samplingPlans) {
        appendRaw(out, e.regionObjectId.bytes.data(), e.regionObjectId.bytes.size());
        appendRaw(out, e.planContentIdentity.bytes.data(),
                  e.planContentIdentity.bytes.size());
        appendU64(out, e.plannedPositionSamples);
        appendU64(out, e.plannedPoseSamples);
        appendRaw(out, e.sampleSetIdentity.bytes.data(), e.sampleSetIdentity.bytes.size());
    }

    // 复现块（必填标量＋保序版本族＋两个可选字段 presence 字节）。
    appendLengthString(out, s.reproduction.productVersion);
    appendLengthString(out, s.reproduction.evidenceContractVersion);
    appendU32(out, static_cast<std::uint32_t>(s.reproduction.codecVersions.size()));
    for (const std::string& v : s.reproduction.codecVersions) {
        appendLengthString(out, v);
    }
    if (s.reproduction.compilerContractVersion.has_value()) {
        appendU8(out, 1u);
        appendLengthString(out, *s.reproduction.compilerContractVersion);
    } else {
        appendU8(out, 0u);
    }
    if (s.reproduction.collisionBackendVersion.has_value()) {
        appendU8(out, 1u);
        appendLengthString(out, *s.reproduction.collisionBackendVersion);
    } else {
        appendU8(out, 0u);
    }
}

/// refs-only 体的解析（与 encodeRefsOnlyBody 严格镜像——布局单点实现的
/// 解析半边；身份重算由调用方在体读完后统一执行）。
AnalysisSnapshot parseRefsOnlyBody(Reader& r)
{
    AnalysisSnapshot s;

    // 身份三元组（revisionSeq 不在编码内——恢复为默认 0，I-1）。
    r.readBytes(s.project.bytes.data(), s.project.bytes.size());
    r.readBytes(s.branch.bytes.data(), s.branch.bytes.size());
    r.readBytes(s.revision.bytes.data(), s.revision.bytes.size());

    // 对象引用闭包（顺序即冻结序——写入序恢复）。
    const std::uint32_t closureCount = r.readU32();
    s.objectClosure.resize(closureCount);
    for (std::uint32_t i = 0; i < closureCount; ++i) {
        ObjectRefEntry& e = s.objectClosure[i];
        r.readBytes(e.objectId.bytes.data(), e.objectId.bytes.size());
        r.readBytes(e.contentVersion.bytes.data(), e.contentVersion.bytes.size());
        e.objectTypeToken = r.readLengthString();
        r.readBytes(e.digest.data(), e.digest.size());
    }

    // 分析配置引用。
    const std::uint32_t configCount = r.readU32();
    s.configurationRefs.resize(configCount);
    for (std::uint32_t i = 0; i < configCount; ++i) {
        ConfigEntry& e = s.configurationRefs[i];
        e.configKindToken = r.readLengthString();
        e.canonicalBytes = r.readLengthBytes();
        r.readBytes(e.contentIdentity.bytes.data(), e.contentIdentity.bytes.size());
    }

    // 策略与名称映射内容身份。
    r.readBytes(s.policyRef.policyContentIdentity.bytes.data(),
                s.policyRef.policyContentIdentity.bytes.size());
    r.readBytes(s.nameMapRef.nameMapContentIdentity.bytes.data(),
                s.nameMapRef.nameMapContentIdentity.bytes.size());

    // 必验工况集合；requiredCaseSetId 双端同函数重算（I-4——身份是导出值，
    // 不是编码载荷：解析结果不可能携带与内容不符的申报凭据）。
    const std::uint32_t caseCount = r.readU32();
    s.caseSet.entries.resize(caseCount);
    for (std::uint32_t i = 0; i < caseCount; ++i) {
        CaseEntry& e = s.caseSet.entries[i];
        r.readBytes(e.caseId.bytes.data(), e.caseId.bytes.size());
        e.label = r.readLengthString();
        e.enabled = r.readU8() != 0;
        e.mandatory = r.readU8() != 0;
    }
    s.caseSet.requiredCaseSetId = toIdentity(digestCaseSetEntries(s.caseSet.entries));

    // 外部资源固化状态（presence 字节→optional——缺失与空值不等价，§5.2）。
    const std::uint32_t resourceCount = r.readU32();
    s.externalResources.resize(resourceCount);
    for (std::uint32_t i = 0; i < resourceCount; ++i) {
        ExternalResourceState& e = s.externalResources[i];
        r.readBytes(e.resourceId.bytes.data(), e.resourceId.bytes.size());
        const std::uint8_t stateByte = r.readU8();
        if (stateByte > 1u) {
            throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                                "外部资源状态字节非法（偏移 "
                                    + std::to_string(r.pos - 1) + "："
                                    + std::to_string(stateByte) + "）");
        }
        e.state = stateByte == 1u ? ExternalResourceStatus::Solidified
                                  : ExternalResourceStatus::Recorded;
        if (r.readU8() == 1u) {
            core::ContentVersion solidified;
            r.readBytes(solidified.bytes.data(), solidified.bytes.size());
            e.solidifiedContentVersion = std::move(solidified);
        } else {
            e.solidifiedContentVersion.reset();
        }
    }

    // 采样计划。
    const std::uint32_t planCount = r.readU32();
    s.samplingPlans.resize(planCount);
    for (std::uint32_t i = 0; i < planCount; ++i) {
        SamplingPlanRef& e = s.samplingPlans[i];
        r.readBytes(e.regionObjectId.bytes.data(), e.regionObjectId.bytes.size());
        r.readBytes(e.planContentIdentity.bytes.data(),
                    e.planContentIdentity.bytes.size());
        e.plannedPositionSamples = r.readU64();
        e.plannedPoseSamples = r.readU64();
        r.readBytes(e.sampleSetIdentity.bytes.data(), e.sampleSetIdentity.bytes.size());
    }

    // 复现块。
    s.reproduction.productVersion = r.readLengthString();
    s.reproduction.evidenceContractVersion = r.readLengthString();
    const std::uint32_t codecVersionCount = r.readU32();
    s.reproduction.codecVersions.resize(codecVersionCount);
    for (std::uint32_t i = 0; i < codecVersionCount; ++i) {
        s.reproduction.codecVersions[i] = r.readLengthString();
    }
    if (r.readU8() == 1u) {
        s.reproduction.compilerContractVersion = r.readLengthString();
    }
    if (r.readU8() == 1u) {
        s.reproduction.collisionBackendVersion = r.readLengthString();
    }
    return s;
}

/// 10 字节编码头拼装（magic＋版本＋形态——两个 encode 入口共用）。
void appendHeader(std::vector<std::uint8_t>& out, std::uint8_t form)
{
    appendRaw(out, reinterpret_cast<const std::uint8_t*>(SnapshotCodec::kMagic.data()),
              SnapshotCodec::kMagic.size());
    appendU8(out, SnapshotCodec::kCodecVersion);
    appendU8(out, form);
}

/// 头校验（magic/版本/形态逐字节——形态不符给出明确指向，防双形态互混）。
void parseHeader(Reader& r, std::uint8_t expectedForm)
{
    r.need(SnapshotCodec::kMagic.size() + 2);
    for (std::size_t i = 0; i < SnapshotCodec::kMagic.size(); ++i) {
        if (r.data[r.pos + i]
            != static_cast<std::uint8_t>(SnapshotCodec::kMagic[i])) {
            throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                                "编码 magic 不符（应为 IRDSNAP1，偏移 0）");
        }
    }
    r.pos += SnapshotCodec::kMagic.size();
    const std::uint8_t version = r.readU8();
    if (version != SnapshotCodec::kCodecVersion) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "codec 版本不符（编码版本=" + std::to_string(version)
                                + "，实现版本="
                                + std::to_string(SnapshotCodec::kCodecVersion)
                                + "——升版走设计变更评审，§5.2 版本化行）");
    }
    const std::uint8_t form = r.readU8();
    if (form != expectedForm) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            std::string("编码形态不符（编码=")
                                + (form == 1u ? "materialized" : "refs-only")
                                + "，请走对应的 parse 入口）");
    }
}

/// 载荷段的④b 载荷级校验核心（encode/parse 双端共用）：
/// 每条载荷必须在闭包内且重算 SHA-256 等于 contentVersion。
/// @throws SnapshotIncomplete（闭包外对象）/SnapshotIntegrity（摘要不符）
void verifyMaterializedPayload(const AnalysisSnapshot& s, const core::ObjectId& oid,
                               const std::vector<std::uint8_t>& bytes)
{
    // 第一步：载荷对象必须在闭包内（物化只允许"被评估切片"的闭包子集，
    // §4.1.5⑤/R-6——闭包外对象＝组装契约违约）。
    const auto it = std::find_if(s.objectClosure.begin(), s.objectClosure.end(),
                                 [&oid](const ObjectRefEntry& e) {
                                     return e.objectId == oid;
                                 });
    if (it == s.objectClosure.end()) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "物化载荷含闭包外对象（objectId=" + oid.toCanonical()
                                + "——物化必须是 objectClosure 子集，§4.1.5⑤）");
    }
    // 第二步：④b 载荷级校验（§4.1.5④b 原文）——重算对象字节 SHA-256 与
    // contentVersion 比对，不符即传输/磁盘损坏或错配，绝不静默（NFR-COR-03）。
    const core::Digest256 actual = sha256(bytes.data(), bytes.size());
    if (actual != it->contentVersion.bytes) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIntegrity,
                            "物化载荷摘要与 contentVersion 不符（objectId="
                                + oid.toCanonical() + "——§4.1.5④b，检测传输/磁盘损坏）");
    }
}

}  // namespace

// =====================================================================
// SnapshotBuilder——②逐类录入（纯累加）＋③冻结（④a 即时验证全部在 build）
// =====================================================================

SnapshotBuilder& SnapshotBuilder::setIdentity(core::ProjectId project, core::BranchId branch,
                                              core::RevisionId revision,
                                              std::uint64_t revisionSeq)
{
    m_project = project;
    m_branch = branch;
    m_revision = revision;
    m_revisionSeq = revisionSeq;   // 序号仅展示/排序——不参与身份（§4.1.2 原文）
    return *this;
}

SnapshotBuilder& SnapshotBuilder::addObjectRef(ObjectRefEntry entry)
{
    m_objectClosure.push_back(std::move(entry));
    return *this;
}

SnapshotBuilder& SnapshotBuilder::addConfiguration(ConfigEntry entry)
{
    m_configurations.push_back(std::move(entry));
    return *this;
}

SnapshotBuilder& SnapshotBuilder::setPolicyRef(PolicyRef ref)
{
    m_policyRef = std::move(ref);
    return *this;
}

SnapshotBuilder& SnapshotBuilder::setNameMapRef(NameMapRef ref)
{
    m_nameMapRef = std::move(ref);
    return *this;
}

SnapshotBuilder& SnapshotBuilder::addCase(CaseEntry entry)
{
    m_cases.push_back(std::move(entry));
    return *this;
}

SnapshotBuilder& SnapshotBuilder::addExternalResource(ExternalResourceState state)
{
    m_externalResources.push_back(std::move(state));
    return *this;
}

SnapshotBuilder& SnapshotBuilder::addSamplingPlan(SamplingPlanRef plan)
{
    m_samplingPlans.push_back(std::move(plan));
    return *this;
}

SnapshotBuilder& SnapshotBuilder::setReproduction(ReproductionBlock block)
{
    m_reproduction = std::move(block);
    return *this;
}

AnalysisSnapshot SnapshotBuilder::build(const IRevisionClosureSource& closureSource) const
{
    // ---------------------------------------------------------------
    // 校验 1：身份三元组（§4.1.2 行 1——revision 是唯一对象解析锚，零值＝
    // 没有锚，后续闭包校验无从谈起，必须最先拒绝）。
    // ---------------------------------------------------------------
    if (!m_project.isValid() || !m_branch.isValid() || !m_revision.isValid()) {
        rejectSnapshot("身份三元组存在零值（project/branch/revision 均须非零——§4.1.2）");
    }

    // ---------------------------------------------------------------
    // 校验 2/3：策略与名称映射内容身份非空（CON-06 违反——§4.1.2 非法实例
    // 原文"policyRef/nameMapRef 内容身份为空"；二者由④/⑥端口解析后值传递
    // 传入，未设定即组装不完整）。
    // ---------------------------------------------------------------
    if (!m_policyRef.has_value() || !m_policyRef->policyContentIdentity.isValid()) {
        rejectSnapshot("policyRef 内容身份为空（CON-06：策略必须以已解析内容身份进入快照）");
    }
    if (!m_nameMapRef.has_value() || !m_nameMapRef->nameMapContentIdentity.isValid()) {
        rejectSnapshot("nameMapRef 内容身份为空（CON-06：名称映射内容身份必须非空）");
    }

    // ---------------------------------------------------------------
    // 校验 4：复现块完整性（NFR-COR-02/RPT-03——合法实例原文"复现块完整"；
    // 可选字段执行 presence 语义）。
    // ---------------------------------------------------------------
    if (!m_reproduction.has_value()) {
        rejectSnapshot("复现块未设定（§4.1.2 reproduction 必填）");
    }
    validateVersionText(m_reproduction->productVersion, "productVersion");
    validateVersionText(m_reproduction->evidenceContractVersion, "evidenceContractVersion");
    for (const std::string& v : m_reproduction->codecVersions) {
        validateVersionText(v, "codecVersions 元素");
    }
    validateOptionalVersionText(m_reproduction->compilerContractVersion,
                                "compilerContractVersion");
    validateOptionalVersionText(m_reproduction->collisionBackendVersion,
                                "collisionBackendVersion");

    // ---------------------------------------------------------------
    // 以下集合类成员先做规范化排序（I-3：冻结序＝同内容同序同身份），
    // 再在冻结序上逐条校验（确定性首错）。
    // ---------------------------------------------------------------
    AnalysisSnapshot snapshot;   // 冻结产物（身份最后统一计算）

    // ---- 对象引用闭包（≥1；字段合法性 → digest 一致 → 唯一 → 混入）----
    snapshot.objectClosure = m_objectClosure;
    std::sort(snapshot.objectClosure.begin(), snapshot.objectClosure.end(),
              [](const ObjectRefEntry& a, const ObjectRefEntry& b) {
                  return a.objectId.bytes < b.objectId.bytes;
              });
    if (snapshot.objectClosure.empty()) {
        rejectSnapshot("对象引用闭包为空（§4.1.2 objectClosure 必填 ≥1）");
    }
    for (std::size_t i = 0; i < snapshot.objectClosure.size(); ++i) {
        const ObjectRefEntry& e = snapshot.objectClosure[i];
        // 字段合法性：身份/版本非零保留值＋类型 token 编码安全（§5.2）。
        if (!e.objectId.isValid() || !e.contentVersion.isValid()
            || !isWellFormedToken(e.objectTypeToken)) {
            rejectSnapshot("对象闭包条目字段非法（下标 " + std::to_string(i)
                           + "：objectId/contentVersion 须非零、objectTypeToken 须"
                             "非空且无 NUL）");
        }
        // digest 一致性闸门（I-2）：digest 与 contentVersion 同源（core §4.2
        // ——内容版本即内容摘要版本戳），不一致＝组装方错填，必须拒绝。
        if (e.digest != e.contentVersion.bytes) {
            rejectSnapshot("对象闭包条目 digest 与 contentVersion 不一致（下标 "
                           + std::to_string(i) + "，objectId=" + e.objectId.toCanonical()
                           + "——两字段同为对象载荷摘要，I-2 一致性闸门）");
        }
        // 唯一性（I-3）：同一对象在闭包中只允许一个引用（修订内对象唯一版本
        // ——重复引用使"闭包"语义不可判定；排序后相邻比较即可）。
        if (i > 0 && snapshot.objectClosure[i - 1].objectId == e.objectId) {
            rejectSnapshot("对象闭包含重复 objectId（objectId="
                           + e.objectId.toCanonical() + "）");
        }
        // 修订闭包包含性（§4.1.5① 防混入——acceptance 2 反例的拒绝点）：
        // 所有 (oid,cv) 必须通过事实来源逐条校验，任一不属于锚定修订即拒绝
        // ——混入其他修订数据是 CON-01 不可变输入闭包的根本破坏。
        if (!closureSource.objectInRevision(m_revision, e.objectId, e.contentVersion)) {
            rejectSnapshot("对象引用不属于锚定修订（防混入，§4.1.5①）：revision="
                           + m_revision.toCanonical()
                           + " objectId=" + e.objectId.toCanonical()
                           + " contentVersion=" + e.contentVersion.toCanonical());
        }
    }

    // ---- 分析配置引用（字段合法性 → 唯一性；schema 归域，evidence 只验
    //      承载纪律——与 Dependency.hpp Configuration 载荷同款口径）----
    snapshot.configurationRefs = m_configurations;
    std::sort(snapshot.configurationRefs.begin(), snapshot.configurationRefs.end(),
              [](const ConfigEntry& a, const ConfigEntry& b) {
                  return a.configKindToken < b.configKindToken;
              });
    for (std::size_t i = 0; i < snapshot.configurationRefs.size(); ++i) {
        const ConfigEntry& e = snapshot.configurationRefs[i];
        if (!isWellFormedToken(e.configKindToken) || e.canonicalBytes.empty()
            || !e.contentIdentity.isValid()) {
            rejectSnapshot("配置引用条目非法（configKindToken=\""
                           + clipForMessage(e.configKindToken)
                           + "\"：token 须非空无 NUL、canonicalBytes 非空、内容身份非零）");
        }
        if (i > 0
            && snapshot.configurationRefs[i - 1].configKindToken == e.configKindToken) {
            rejectSnapshot("配置引用 configKindToken 重复（\""
                           + clipForMessage(e.configKindToken)
                           + "\"——同一种类配置只能有一个引用）");
        }
    }

    // ---- 必验工况集合（caseId 唯一——§4.1.2 非法实例原文"caseSet 含重复
    //      工况 id"；enabled/mandatory 字面承载不解释——O-14/P-EV-9 保守
    //      字面，acceptance 3；空集合法——P-EV-7 场景入口，I-7）----
    snapshot.caseSet.entries = m_cases;
    std::sort(snapshot.caseSet.entries.begin(), snapshot.caseSet.entries.end(),
              [](const CaseEntry& a, const CaseEntry& b) {
                  return a.caseId.bytes < b.caseId.bytes;
              });
    for (std::size_t i = 0; i < snapshot.caseSet.entries.size(); ++i) {
        const CaseEntry& e = snapshot.caseSet.entries[i];
        if (!e.caseId.isValid()) {
            rejectSnapshot("工况条目 caseId 为零值（§4.1.3）");
        }
        if (e.label.find('\0') != std::string::npos) {
            rejectSnapshot("工况 label 含 NUL（§5.2 编码安全）");
        }
        if (i > 0 && snapshot.caseSet.entries[i - 1].caseId == e.caseId) {
            rejectSnapshot("caseSet 含重复工况 id（caseId=" + e.caseId.toCanonical()
                           + "——§4.1.2 非法实例）");
        }
    }
    // 冻结凭据：entries 规范编码摘要（I-4——builder 计算，非调用方申报；
    // §6.6 覆盖矩阵与它核对）。空集同样计算（空集的凭据也是确定值）。
    snapshot.caseSet.requiredCaseSetId
        = toIdentity(digestCaseSetEntries(snapshot.caseSet.entries));

    // ---- 外部资源固化状态（状态机 Recorded→Solidified 单向——CON-03；
    //      presence 语义：Solidified 必带非零固化版本、Recorded 必不带）----
    snapshot.externalResources = m_externalResources;
    std::sort(snapshot.externalResources.begin(), snapshot.externalResources.end(),
              [](const ExternalResourceState& a, const ExternalResourceState& b) {
                  return a.resourceId.bytes < b.resourceId.bytes;
              });
    for (std::size_t i = 0; i < snapshot.externalResources.size(); ++i) {
        const ExternalResourceState& e = snapshot.externalResources[i];
        if (!e.resourceId.isValid()) {
            rejectSnapshot("外部资源 resourceId 为零值（§4.1.2）");
        }
        if (e.state == ExternalResourceStatus::Solidified) {
            // 固化态必须钉住内容版本（无版本承诺的"固化"不是固化——CON-03）。
            if (!e.solidifiedContentVersion.has_value()
                || !e.solidifiedContentVersion->isValid()) {
                rejectSnapshot("外部资源 Solidified 缺少有效 solidifiedContentVersion"
                               "（resourceId=" + e.resourceId.toCanonical() + "）");
            }
        } else {
            // Recorded 态禁止携带固化版本（presence 语义——噪声即矛盾）。
            if (e.solidifiedContentVersion.has_value()) {
                rejectSnapshot("外部资源 Recorded 不得携带 solidifiedContentVersion"
                               "（resourceId=" + e.resourceId.toCanonical() + "）");
            }
        }
        if (i > 0 && snapshot.externalResources[i - 1].resourceId == e.resourceId) {
            rejectSnapshot("外部资源 resourceId 重复（resourceId="
                           + e.resourceId.toCanonical()
                           + "——同一资源的两条状态记录自相矛盾）");
        }
    }

    // ---- 采样计划（身份字段非零；计数为 uint64 定宽无负值——"采样计划
    //      参数非负"（§4.1.5④a）由类型承载，0 合法（零样本场景 §6.6）；
    //      唯一性防同区域双计划）----
    snapshot.samplingPlans = m_samplingPlans;
    std::sort(snapshot.samplingPlans.begin(), snapshot.samplingPlans.end(),
              [](const SamplingPlanRef& a, const SamplingPlanRef& b) {
                  return a.regionObjectId.bytes < b.regionObjectId.bytes;
              });
    for (std::size_t i = 0; i < snapshot.samplingPlans.size(); ++i) {
        const SamplingPlanRef& e = snapshot.samplingPlans[i];
        if (!e.regionObjectId.isValid() || !e.planContentIdentity.isValid()
            || !e.sampleSetIdentity.isValid()) {
            rejectSnapshot("采样计划字段非法（regionObjectId/planContentIdentity/"
                           "sampleSetIdentity 均须非零——§4.1.4）");
        }
        if (i > 0 && snapshot.samplingPlans[i - 1].regionObjectId == e.regionObjectId) {
            rejectSnapshot("采样计划 regionObjectId 重复（region="
                           + e.regionObjectId.toCanonical()
                           + "——同一区域只能有一个冻结采样计划）");
        }
    }

    // ---------------------------------------------------------------
    // 透传其余必填承载字段（上面已逐一校验过合法性）。
    // ---------------------------------------------------------------
    snapshot.project = m_project;
    snapshot.branch = m_branch;
    snapshot.revision = m_revision;
    snapshot.revisionSeq = m_revisionSeq;   // 不参与身份（I-1）——仅实例承载
    snapshot.policyRef = *m_policyRef;
    snapshot.nameMapRef = *m_nameMapRef;
    snapshot.reproduction = *m_reproduction;

    // ---------------------------------------------------------------
    // ③冻结：snapshotId＝SHA-256 over refs-only 编码（§5.1——builder 计算，
    // 非调用方申报）。编码不含 snapshotId 本身（身份是编码的函数，不是
    // 编码的载荷），因此"先编码、后回填"无循环依赖。
    // ---------------------------------------------------------------
    const std::vector<std::uint8_t> refsOnly = SnapshotCodec::encodeRefsOnly(snapshot);
    snapshot.snapshotId = toIdentity(sha256(refsOnly.data(), refsOnly.size()));
    return snapshot;
}

// =====================================================================
// SnapshotCodec——规范编码（布局见文件头注释；全部纯函数）
// =====================================================================

std::vector<std::uint8_t> SnapshotCodec::encodeRefsOnly(const AnalysisSnapshot& snapshot)
{
    std::vector<std::uint8_t> out;
    // 粗估容量减少重分配（闭包条目约 80 字节/条——纯性能项，非正确性）。
    out.reserve(128 + snapshot.objectClosure.size() * 80
                + snapshot.caseSet.entries.size() * 32);
    appendHeader(out, 0);   // form=0：refs-only（身份形态）
    encodeRefsOnlyBody(out, snapshot);
    return out;
}

AnalysisSnapshot SnapshotCodec::parseRefsOnly(const std::vector<std::uint8_t>& encoding)
{
    Reader r{encoding, 0};
    parseHeader(r, 0);   // refs-only 专属入口——materialized 字节在此拒绝

    AnalysisSnapshot s = parseRefsOnlyBody(r);

    // 尾部必须恰好读完——多余字节说明这不是本 codec 的合法产物（错位/
    // 混入他版字节），静默截断会伪造身份一致性，必须拒绝（NFR-COR-03）。
    if (r.pos != encoding.size()) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "编码存在尾随多余字节（已读 " + std::to_string(r.pos)
                                + "，总长 " + std::to_string(encoding.size()) + "）");
    }

    // 身份重算：snapshotId＝refs-only 摘要（对刚解析出的内容重新编码取
    // 摘要——身份是编码的函数，不是编码的载荷；解析结果不可能携带与内容
    // 不符的申报身份，§4.1.2"由 builder 计算，非调用方申报"的解析侧延伸）。
    const std::vector<std::uint8_t> reencoded = encodeRefsOnly(s);
    s.snapshotId = toIdentity(sha256(reencoded.data(), reencoded.size()));
    return s;
}

std::vector<std::uint8_t>
SnapshotCodec::encodeMaterialized(const AnalysisSnapshot& snapshot,
                                  const MaterializedPayloads& payloads)
{
    // 物化前置校验（④b 在物化入口执行——I-6）：逐条闭包含性＋摘要一致性。
    for (const auto& entry : payloads) {
        verifyMaterializedPayload(snapshot, entry.first, entry.second);
    }

    // 复用 refs-only 全编码的字节：保留其体（去 10 字节头），换 materialized
    // 头（身份恒按 refs-only 形态计算——双形态同身份，I-5）。
    const std::vector<std::uint8_t> refsOnly = encodeRefsOnly(snapshot);
    std::vector<std::uint8_t> out;
    out.reserve(refsOnly.size() + 16);
    appendHeader(out, 1);   // form=1：materialized（worker 投递形态）
    const std::size_t bodyBegin = SnapshotCodec::kMagic.size() + 2;
    out.insert(out.end(),
               refsOnly.begin() + static_cast<std::ptrdiff_t>(bodyBegin),
               refsOnly.end());
    // 载荷段（std::map 迭代序＝objectId 序——编码确定性）。
    appendU32(out, static_cast<std::uint32_t>(payloads.size()));
    for (const auto& entry : payloads) {
        appendRaw(out, entry.first.bytes.data(), entry.first.bytes.size());
        appendLengthBytes(out, entry.second);
    }
    return out;
}

SnapshotCodec::MaterializedSnapshot
SnapshotCodec::parseMaterialized(const std::vector<std::uint8_t>& encoding)
{
    Reader r{encoding, 0};
    parseHeader(r, 1);   // materialized 专属入口——refs-only 字节在此拒绝

    // 体与 parseRefsOnly 同布局——直接复用共享解析函数（布局单点实现，
    // 双形态解析不漂移）。
    AnalysisSnapshot s = parseRefsOnlyBody(r);

    // 载荷段：逐条④b 接收端复核（I-6——传输/磁盘损坏在此暴露为
    // SnapshotIntegrity；NFR-COR-03 不静默）。
    MaterializedSnapshot result;
    const std::uint32_t payloadCount = r.readU32();
    for (std::uint32_t i = 0; i < payloadCount; ++i) {
        core::ObjectId oid;
        r.readBytes(oid.bytes.data(), oid.bytes.size());
        std::vector<std::uint8_t> bytes = r.readLengthBytes();
        verifyMaterializedPayload(s, oid, bytes);
        result.payloads.emplace(std::move(oid), std::move(bytes));
    }
    // 尾部恰好读完（同 parseRefsOnly——多余字节即拒绝）。
    if (r.pos != encoding.size()) {
        throw EvidenceError(EvidenceErrorCode::SnapshotIncomplete,
                            "编码存在尾随多余字节（已读 " + std::to_string(r.pos)
                                + "，总长 " + std::to_string(encoding.size()) + "）");
    }

    // 身份重算（refs-only 形态——双形态同身份，§4.1.5⑤/D-01）。
    const std::vector<std::uint8_t> reencoded = encodeRefsOnly(s);
    s.snapshotId = toIdentity(sha256(reencoded.data(), reencoded.size()));
    result.snapshot = std::move(s);
    return result;
}

}  // namespace sdurws::ird::evidence
