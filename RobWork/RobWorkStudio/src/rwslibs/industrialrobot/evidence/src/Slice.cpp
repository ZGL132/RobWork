/**
 * @file   Slice.cpp
 * @brief  切片组装/校验/规范编码实现——SliceBuilder 冻结协议（§4.2.3②：
 *         语法对账＋Object 快照子集校验＋CR-05 必填与值形态闸门）＋
 *         SliceCodec 双形态编解码（full/baseline-projection）＋双层身份
 *         计算（§5.1 D-04）＋canonical 浮点位模式原语（§5.2 数值行）。
 *
 * 设计依据：
 *   - units/evidence.md §4.2.2（字段表与存储不变量）、§4.2.3（冻结期协议
 *     ——"请求方提供解析结果；Conditional 条目解析 applied/notAppliedReason；
 *     Object 条目做快照子集校验"）、§4.2.4（改求解预算复评——双层身份的
 *     消费语义）、§5.1（双层身份表＋身份纪律：四类身份互不替代；比较用
 *     字节等值）、§5.2（编码规则表：magic IRDSLCE1/大端/长度前缀/presence
 *     字节/浮点位模式＋非有限拒绝/版本化/纯函数）
 *   - 跨单元红线 CR-02（摘要算法唯一＝core ContentDigester；排除字段以
 *     codec 单测钉住——本文件 D-2 口径＋SliceTest 排除字段用例）、CR-05
 *     （保留 token 必填＋值形态＋进入 inputBaselineId；值由组装方值传递，
 *     evidence 不重算 CanonicalModel——本文件只做形态闸门，无任何模型
 *     重编码入口）
 *   - 需求 CON-04/05/06、KIN-13、NFR-COR-02/03；任务契约
 *     tasks/foundation/EV-T04.json acceptance 1～3
 *
 * == 实现口径登记（偏差均属"实现细节精确化"，语义与设计一致——单元卡
 *    v0.5 变更记录同步登记，DTB §5.4）==
 *   D-1 inputBaselineId 投影的参与集机械口径：进入＝全部 Object＋全部
 *       SampleSet＋全部 NameMap＋Environment 中基准类保留 token 条目
 *       （CR-05 二条目，isBaselineEnvironmentToken）＋快照身份块；排除＝
 *       Configuration（求解类——KIN-13/D-04）、Policy（§4.2.2"仅模型/
 *       需求/工况集/冻结样本集条目参与"的"仅"字＋§5.1 参与清单未列——
 *       策略属消费机制，经 sliceId 失效但不构成输入基准差异）、
 *       UpstreamResult（同上未列；上游可比性经其自身身份追溯，§8.1）、
 *       Environment 非基准版本（§5.1 排除语"Environment 中的非基准版本"）。
 *       Object 的"task/region/case-set/model 类角色"（§5.1 正向括注）机械
 *       化为全量进入——evidence 无域词表（N-5/N-9），按 key 前缀猜类即把
 *       域知识搬进 evidence；且 §5.1 排除语仅点名"求解类 Configuration 与
 *       Environment 非基准版本"，Object 不在排除面；全量进入使碰撞几何/
 *       负载/目录等模型侧对象变化同样被 EVI-02 比较基准拦截——保守方向
 *       （宁可多算不可错复用，§5.1 语义等价行同源精神）。
 *   D-2 sliceId/inputBaselineId 是编码的导出值，不入编码载荷（EV-T03
 *       I-4"身份是导出值"同款）——parse 恒重算，解析结果不可能携带与
 *       内容不符的申报身份；摘要唯一经 core::ContentDigester（CR-02，
 *       computeSliceDigest 单点）。
 *   D-3 CR-05 值形态闸门：凡以保留 token 命名的 Environment 条目恒久
 *       校验（不论必填开关）——runtime.model-identity 值必须可被
 *       core::ContentIdentity::fromCanonical 解析（"cid-<64hex>" 规范文本，
 *       CR-05 裁决第 1 点值形态）；runtime.robwork-baseline 值必须非空且
 *       无空白字符（RobWorkBaselineVersion 契约——RT-T09 测试断言 4 同款，
 *       空白判定用显式六字符表，无 locale 依赖）。必填校验（双条目在在
 *       且各恰一条）仅在 setConsumesCanonicalModel(true) 时执行。
 *   D-4 builder 冻结时对 entries 规范化排序（(kind,key) 字典序——EV-T03
 *       I-3 规范化同款）："未排序输入"不是拒绝面（组装便利与确定性兼得
 *       ——同内容任意添加序同身份）；(kind,key) 重复、载荷非法、applied/
 *       notAppliedReason 矛盾仍拒绝（validateDependencyEntries 在排序后
 *       副本上执行——EntrySetNotSorted 因先排序而不可触达，其余码面全部
 *       保留为冻结拒绝面）。
 *   D-5 InputSlice::evaluationKey 暂以 std::string 承载（isValidEvaluationKey
 *       词形闸门＝§8.2 EvaluationKey 原文语法）——EvaluationKey 类型化
 *       归 Evaluator.hpp（EV-T10），落地后按需替换，公共契约不变。
 *
 * == SliceCodec 字节布局（版本 1；定宽整型一律大端）==
 *   [0..7]  magic "IRDSLCE1"（§5.2 原文）
 *   [8]     codec 版本（=1）
 *   [9]     形态字节：0=full、1=baseline-projection（D-2：两形态均为身份
 *           摘要对象；parse 仅接受 full——投影非往返载体）
 *   ---- full 体（sliceId 的摘要对象）----
 *   [2 len] evaluationKey
 *   [4]     evaluatorContractVersion
 *   [32]    snapshotId（来源快照身份——§4.2.2）
 *   [4]     条目数 N；每条（(kind,key) 字典序）：见下方"条目编码"
 *   ---- baseline-projection 体（inputBaselineId 的摘要对象）----
 *   [32]    snapshotId（"快照身份块"——§5.1 参与清单）
 *   [4]     基准条目数 M；每条：同"条目编码"（字节级同构——"SliceCodec
 *           的子集"字面成立，D-1 排除面在条目选择层实现）
 *   ---- 条目编码（两形态共用——单点实现不漂移）----
 *   [1] kind（枚举声明序值——Dependency.hpp 权威序）
 *   [2 len] key
 *   [1] applied（0x01/0x00——presence 语义）
 *   [1] notAppliedReason presence（0x01 跟 [2 len]——缺失与空值不等价，
 *       NFR-COR-03；builder 已保证配对一致，编码入口复核是再入防线）
 *   载荷（按 kind）：
 *     Object:         [16 objectId][32 contentVersion][2 len objectTypeToken]
 *     Configuration:  [2 len configKindToken][4 len canonicalBytes][32 cid]
 *     Policy:         [32 policyContentIdentity]
 *     NameMap:        [32 nameMapContentIdentity]
 *     SampleSet:      [16 regionObjectId][32 sampleSetIdentity]
 *     UpstreamResult: [2 len upstreamKey][32 upstreamSliceId][1 presence
 *                     upstreamRunId（0x01 跟 [16]）]
 *     Environment:    [2 len token][2 len valueToken]
 *
 * builder 校验顺序（固定——同一坏组装必报同一首错，NFR-COR-02）：
 *   评估键语法 → 快照冻结性（snapshotId 非零）→ 条目集排序 → 条目语法
 *   （validateDependencyEntries 全码面）→ Object 快照子集 → CR-05 值形态
 *   （保留 token 恒久）→ CR-05 必填（开关置位时）。
 *
 * 确定性：编码为纯字节拼装（无隐藏状态）；解析为界限检查的顺序读取；
 * 同输入恒同字节/同结构（NFR-COR-02）。线程安全：无共享可变状态（可重入）。
 */

#include <sdurws/ird/evidence/Slice.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace sdurws::ird::evidence {

namespace {

// =====================================================================
// 字节拼装/读取原语（§5.2：定宽整型大端、长度前缀、无填充——跨进程一致；
// 与 Snapshot.cpp 同款纪律，单元内各 TU 自持——匿名命名空间不跨 TU 复用）
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

/// 追加 64 位整型（大端——浮点位模式承载的整型通道）。
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

/// 追加定长 16 字节块（Id128 家族原始字节）。
void appendRaw16(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 16>& bytes)
{
    appendRaw(out, bytes.data(), bytes.size());
}

/// 追加定长 32 字节块（Digest256 家族原始字节）。
void appendRaw32(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 32>& bytes)
{
    appendRaw(out, bytes.data(), bytes.size());
}

/// 追加"2 字节长度＋字符串"（§5.2 字符串规则：UTF-8、禁 NUL——编码入口
/// 复核是 builder 校验的再入防线：encode 是公共 API，可能遭遇未过 builder
/// 的手改切片，属调用方契约违约即拒绝）。
/// @throws EvidenceError(SliceIncomplete) 含 NUL 或超出 65535 字节
void appendLengthString(std::vector<std::uint8_t>& out, const std::string& s)
{
    if (s.find('\0') != std::string::npos) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "字符串含 NUL（§5.2 编码安全）");
    }
    if (s.size() > 0xFFFFu) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "字符串超出 65535 字节编码上限（长度 "
                                + std::to_string(s.size()) + "）");
    }
    appendU16(out, static_cast<std::uint16_t>(s.size()));
    appendRaw(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
}

/// 追加"4 字节长度＋字节块"（32 位长度上限——越界显式拒绝而非静默回绕，
/// NFR-COR-03；EV-T03 同款纪律）。
/// @throws EvidenceError(SliceIncomplete) 超出 2^32-1 字节
void appendLengthBytes(std::vector<std::uint8_t>& out,
                       const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() > 0xFFFFFFFFu) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "字节块超出 2^32-1 编码上限（长度 "
                                + std::to_string(bytes.size()) + "）");
    }
    appendU32(out, static_cast<std::uint32_t>(bytes.size()));
    appendRaw(out, bytes.data(), bytes.size());
}

/**
 * @brief 界限检查的顺序读取器（解析侧唯一取字节通道——任何越界即抛，
 *        不可能读出未定义字节；EV-T03 Reader 同款）。
 *
 * @throws EvidenceError(SliceIncomplete) 剩余字节不足/内容非法
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
                EvidenceErrorCode::SliceIncomplete,
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
        std::memcpy(out, data.data() + pos, n);
        pos += n;
    }

    /// 读取 16 字节块入 Id128 家族 bytes 成员。
    std::array<std::uint8_t, 16> readRaw16()
    {
        std::array<std::uint8_t, 16> b{};
        readBytes(b.data(), b.size());
        return b;
    }

    /// 读取 32 字节块入 Digest256 家族 bytes 成员。
    std::array<std::uint8_t, 32> readRaw32()
    {
        std::array<std::uint8_t, 32> b{};
        readBytes(b.data(), b.size());
        return b;
    }

    /// 读取"2 字节长度＋字符串"（NUL 复核——与写入端同一编码安全契约）。
    std::string readLengthString()
    {
        const std::uint16_t len = readU16();
        need(len);
        std::string s(reinterpret_cast<const char*>(data.data() + pos), len);
        pos += len;
        if (s.find('\0') != std::string::npos) {
            throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                "字符串含 NUL（偏移 " + std::to_string(pos) + "）");
        }
        return s;
    }

    /// 读取"4 字节长度＋字节块"。
    std::vector<std::uint8_t> readLengthBytes()
    {
        const std::uint32_t len = readU32();
        need(len);
        std::vector<std::uint8_t> b(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                    data.begin() + static_cast<std::ptrdiff_t>(pos) + len);
        pos += len;
        return b;
    }

    /// 确认已消费至末尾（尾随字节＝形态混淆/拼接损坏，显式拒绝）。
    void expectEnd()
    {
        if (pos != data.size()) {
            throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                "编码存在尾随字节（已消费 " + std::to_string(pos)
                                    + "，共 " + std::to_string(data.size()) + "）");
        }
    }
};

// =====================================================================
// 摘要单点（CR-02：摘要算法唯一＝core ContentDigester——本文件全部身份
// 计算的唯一通道，不存在第二摘要实现）
// =====================================================================

/// 对字节序列做 SHA-256（core ContentDigester 单点——CR-02）。
core::Digest256 computeSliceDigest(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

// =====================================================================
// 条目编码/解析（两形态共用——布局单点实现，full/投影不漂移）
// =====================================================================

/// (kind,key) 字典序比较器（§4.2.2 稳定存储序——kind 序＝枚举声明序，
/// Dependency.hpp 权威序；同 kind 按 key 字典序）。
bool entryLess(const DependencyEntry& a, const DependencyEntry& b)
{
    if (a.kind != b.kind) {
        return a.kind < b.kind;
    }
    return a.key < b.key;
}

/// 编码单条依赖条目（载荷按 kind 分派——布局见实现头注释"条目编码"）。
/// @throws EvidenceError(SliceIncomplete) 字符串含 NUL/超长
void writeEntry(std::vector<std::uint8_t>& out, const DependencyEntry& e)
{
    // kind 与键：条目的寻址头（(kind,key) 唯一——稳定存储序的编码承载）。
    appendU8(out, static_cast<std::uint8_t>(e.kind));
    appendLengthString(out, e.key);
    // 解析结果：applied 用 0x01/0x00 显式编码（bool 不直接落码——字节面
    // 只允许 presence 语义的两个合法值，防手改切片带入 0x02 等噪声）。
    appendU8(out, e.applied ? 0x01u : 0x00u);
    // 未适用原因：presence 字节（缺失与空值不等价——NFR-COR-03；builder
    // 已保证 applied==true 时无值、false 时非空，此处复核 presence 配对）。
    if (e.notAppliedReason.has_value() == e.applied) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "applied/notAppliedReason 配对矛盾（key=" + e.key + "）");
    }
    if (e.notAppliedReason.has_value()) {
        appendU8(out, 0x01u);
        appendLengthString(out, *e.notAppliedReason);
    } else {
        appendU8(out, 0x00u);
    }
    // 载荷：variant 下标必须与 kind 对应（builder/validate 已挡——编码端
    // 再入防线：错配载荷不落码，否则产生无法解析的字节序列）。
    switch (e.kind) {
    case DependencyKind::Object: {
        const auto& p = std::get<ObjectDependencyPayload>(e.payload);
        appendRaw16(out, p.objectId.bytes);
        appendRaw32(out, p.contentVersion.bytes);
        appendLengthString(out, p.objectTypeToken);
        break;
    }
    case DependencyKind::Configuration: {
        const auto& p = std::get<ConfigurationDependencyPayload>(e.payload);
        appendLengthString(out, p.configKindToken);
        appendLengthBytes(out, p.canonicalBytes);
        appendRaw32(out, p.contentIdentity.bytes);
        break;
    }
    case DependencyKind::Policy: {
        const auto& p = std::get<PolicyDependencyPayload>(e.payload);
        appendRaw32(out, p.policyContentIdentity.bytes);
        break;
    }
    case DependencyKind::NameMap: {
        const auto& p = std::get<NameMapDependencyPayload>(e.payload);
        appendRaw32(out, p.nameMapContentIdentity.bytes);
        break;
    }
    case DependencyKind::SampleSet: {
        const auto& p = std::get<SampleSetDependencyPayload>(e.payload);
        appendRaw16(out, p.regionObjectId.bytes);
        appendRaw32(out, p.sampleSetIdentity.bytes);
        break;
    }
    case DependencyKind::UpstreamResult: {
        const auto& p = std::get<UpstreamResultDependencyPayload>(e.payload);
        appendLengthString(out, p.upstreamKey);
        appendRaw32(out, p.upstreamSliceId.bytes);
        // 上游运行身份：presence 字节（不限定运行＝缺失，非空值——
        // §4.2.1 UpstreamResultRef.upstreamRunId 可选语义）。
        if (p.upstreamRunId.has_value()) {
            appendU8(out, 0x01u);
            appendRaw16(out, p.upstreamRunId->bytes);
        } else {
            appendU8(out, 0x00u);
        }
        break;
    }
    case DependencyKind::Environment: {
        const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
        appendLengthString(out, p.token);
        appendLengthString(out, p.valueToken);
        break;
    }
    }
}

/// 解析单条依赖条目（writeEntry 的逆；载荷-kind 严格对应）。
/// @throws EvidenceError(SliceIncomplete) 截断/字符串含 NUL/载荷与 kind 不符
DependencyEntry readEntry(Reader& r)
{
    DependencyEntry e;
    // kind：先读枚举字节并校验词表（超出七值即结构性非法——防枚举值被
    // 篡改后误入下游 variant 访问）。
    const std::uint8_t kindByte = r.readU8();
    if (kindByte > static_cast<std::uint8_t>(DependencyKind::Environment)) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "依赖 Kind 词表外值 " + std::to_string(kindByte)
                                + "（偏移 " + std::to_string(r.pos - 1) + "）");
    }
    e.kind = static_cast<DependencyKind>(kindByte);
    e.key = r.readLengthString();
    // applied：只接受 0x00/0x01（其余＝字节面损坏——bool 噪声显式拒绝）。
    const std::uint8_t appliedByte = r.readU8();
    if (appliedByte > 0x01u) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "applied 字节非法（" + std::to_string(appliedByte)
                                + "，key=" + e.key + "）");
    }
    e.applied = appliedByte == 0x01u;
    // 未适用原因：presence 字节（与写入端同款配对复核）。
    const std::uint8_t reasonPresence = r.readU8();
    if (reasonPresence > 0x01u) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "notAppliedReason presence 字节非法（key=" + e.key + "）");
    }
    if (reasonPresence == 0x01u) {
        e.notAppliedReason = r.readLengthString();
    }
    // 载荷：按 kind 严格读取对应 alternative（错配即结构性非法——下同
    // builder 语法闸门，编码面再复核一次防手改字节）。
    switch (e.kind) {
    case DependencyKind::Object: {
        ObjectDependencyPayload p;
        std::memcpy(p.objectId.bytes.data(), r.readRaw16().data(), 16);
        std::memcpy(p.contentVersion.bytes.data(), r.readRaw32().data(), 32);
        p.objectTypeToken = r.readLengthString();
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::Configuration: {
        ConfigurationDependencyPayload p;
        p.configKindToken = r.readLengthString();
        p.canonicalBytes = r.readLengthBytes();
        std::memcpy(p.contentIdentity.bytes.data(), r.readRaw32().data(), 32);
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::Policy: {
        PolicyDependencyPayload p;
        std::memcpy(p.policyContentIdentity.bytes.data(), r.readRaw32().data(), 32);
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::NameMap: {
        NameMapDependencyPayload p;
        std::memcpy(p.nameMapContentIdentity.bytes.data(), r.readRaw32().data(), 32);
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::SampleSet: {
        SampleSetDependencyPayload p;
        std::memcpy(p.regionObjectId.bytes.data(), r.readRaw16().data(), 16);
        std::memcpy(p.sampleSetIdentity.bytes.data(), r.readRaw32().data(), 32);
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::UpstreamResult: {
        UpstreamResultDependencyPayload p;
        p.upstreamKey = r.readLengthString();
        std::memcpy(p.upstreamSliceId.bytes.data(), r.readRaw32().data(), 32);
        const std::uint8_t runPresence = r.readU8();
        if (runPresence > 0x01u) {
            throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                "upstreamRunId presence 字节非法（key=" + e.key + "）");
        }
        if (runPresence == 0x01u) {
            core::RunId run;
            std::memcpy(run.bytes.data(), r.readRaw16().data(), 16);
            p.upstreamRunId = std::move(run);
        }
        e.payload = std::move(p);
        break;
    }
    case DependencyKind::Environment: {
        EnvironmentDependencyPayload p;
        p.token = r.readLengthString();
        p.valueToken = r.readLengthString();
        e.payload = std::move(p);
        break;
    }
    }
    return e;
}

// =====================================================================
// 编码体（full/投影共用头部与条目段写法——形态差异仅条目选择与评估面）
// =====================================================================

/// full 形态体编码（不含 10 字节头——头由 encodeFull 统一写）。
void writeFullBody(std::vector<std::uint8_t>& out, const InputSlice& slice)
{
    // 评估面：评估键＋契约版本（进 sliceId——同输入不同契约版本＝不同
    // 切片，CON-04）。
    appendLengthString(out, slice.evaluationKey);
    appendU32(out, slice.evaluatorContractVersion);
    // 来源快照身份（§4.2.2 snapshotId 行——切片溯源锚）。
    appendRaw32(out, slice.snapshotId.bytes);
    // 条目段：条数前缀＋冻结序逐条（builder 已规范化排序——同内容同字节）。
    appendU32(out, static_cast<std::uint32_t>(slice.entries.size()));
    for (const DependencyEntry& e : slice.entries) {
        writeEntry(out, e);
    }
}

/// baseline-projection 形态体编码（D-1 参与集——快照身份块＋基准类条目）。
void writeBaselineProjectionBody(std::vector<std::uint8_t>& out, const InputSlice& slice)
{
    // 快照身份块（§5.1 inputBaselineId 参与清单原文）。
    appendRaw32(out, slice.snapshotId.bytes);
    // 基准类条目子集：Object/SampleSet/NameMap 全量＋Environment 基准类
    // 保留 token 条目；Configuration/Policy/UpstreamResult/非基准 Environment
    // 排除（口径推导见实现头注释 D-1）。冻结序保持——投影条目相对顺序
    // 与 full 一致（(kind,key) 字典序的子序列）。
    std::vector<const DependencyEntry*> baseline;
    for (const DependencyEntry& e : slice.entries) {
        switch (e.kind) {
        case DependencyKind::Object:
        case DependencyKind::SampleSet:
        case DependencyKind::NameMap:
            baseline.push_back(&e);
            break;
        case DependencyKind::Environment: {
            const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
            if (isBaselineEnvironmentToken(p.token)) {
                baseline.push_back(&e);
            }
            break;
        }
        case DependencyKind::Configuration:
        case DependencyKind::Policy:
        case DependencyKind::UpstreamResult:
            break;  // 排除面（D-1）——不进投影。
        }
    }
    appendU32(out, static_cast<std::uint32_t>(baseline.size()));
    for (const DependencyEntry* e : baseline) {
        writeEntry(out, *e);
    }
}

/// 显式空白判定（无 locale 依赖——基线串契约"无空白"的确定性承载；
/// 字符表＝C 空白集合六字符，与 std::isspace 默认 C locale 语义一致）。
bool isAsciiWhitespace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

}  // namespace

// =====================================================================
// SliceBuilder
// =====================================================================

SliceBuilder& SliceBuilder::setEvaluation(std::string evaluationKey,
                                          std::uint32_t evaluatorContractVersion)
{
    m_evaluationKey = std::move(evaluationKey);
    m_contractVersion = evaluatorContractVersion;
    return *this;
}

SliceBuilder& SliceBuilder::addEntry(DependencyEntry entry)
{
    m_entries.push_back(std::move(entry));
    return *this;
}

SliceBuilder& SliceBuilder::setConsumesCanonicalModel(bool required)
{
    m_consumesCanonicalModel = required;
    return *this;
}

InputSlice SliceBuilder::build(const AnalysisSnapshot& snapshot) const
{
    // ---- 第 1 步：评估面语法（§4.2.2 evaluationKey 行——进身份的键必须
    // 词形合法，否则缓存键/诊断承载出现歧义字节）。
    if (!isValidEvaluationKey(m_evaluationKey)) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "evaluationKey 语法非法（应为 [a-z][a-z0-9-]{1,63}）");
    }

    // ---- 第 2 步：来源快照冻结性（snapshotId 非零＝SnapshotBuilder 产出；
    // 手工拼装的未冻结快照没有身份承诺，切片溯源锚失效）。
    if (!snapshot.snapshotId.isValid()) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "来源快照未冻结（snapshotId 为零保留值——切片只能从"
                            " SnapshotBuilder 产出的冻结快照构建）");
    }

    // ---- 第 3 步：条目集规范化排序＋全量语法校验（D-4：先排序——同内容
    // 任意添加序同身份；排序后 validateDependencyEntries 的其余码面全部
    // 保留：载荷-kind 匹配、(kind,key) 唯一、applied/notAppliedReason
    // 配对、各载荷字段非空——任何一码即冻结拒绝）。
    std::vector<DependencyEntry> entries = m_entries;
    std::sort(entries.begin(), entries.end(), entryLess);
    if (!entries.empty()) {
        for (std::size_t i = 1; i < entries.size(); ++i) {
            if (!entryLess(entries[i - 1], entries[i])) {
                throw EvidenceError(
                    EvidenceErrorCode::SliceIncomplete,
                    "(kind,key) 重复（稳定存储要求唯一）：key=" + entries[i].key);
            }
        }
    }
    // 条目语法闸门（Dependency.hpp 校验器——空集/载荷/配对问题逐条检出，
    // 聚合为一条拒绝消息：冻结失败可一次看全全部问题）。
    {
        const std::vector<DependencyIssue> issues = validateDependencyEntries(entries);
        if (!issues.empty()) {
            std::string detail;
            for (const auto& issue : issues) {
                if (!detail.empty()) {
                    detail += "; ";
                }
                detail += "[" + issue.key + "#" + std::to_string(issue.index) + "] "
                          + issue.message;
            }
            throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                "依赖条目校验失败：" + detail);
        }
    }

    // ---- 第 4 步：Object 条目快照子集校验（§4.2.2 entries 行："每条
    // Object 条目必须能在快照 objectClosure 中找到（子集校验，防漏声明
    // 错配）"——快照外对象进切片＝消费了闭包外输入，CON-01 闭包完整性
    // 的破坏；(oid,cv) 精确匹配＝对象内容也必须与快照承诺一致）。
    for (const DependencyEntry& e : entries) {
        if (e.kind != DependencyKind::Object) {
            continue;
        }
        const auto& p = std::get<ObjectDependencyPayload>(e.payload);
        bool found = false;
        for (const ObjectRefEntry& ref : snapshot.objectClosure) {
            if (ref.objectId == p.objectId && ref.contentVersion == p.contentVersion) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw EvidenceError(
                EvidenceErrorCode::SliceIncomplete,
                "Object 条目不在来源快照闭包内（子集校验失败，防漏声明错配）：key="
                    + e.key + " objectId=" + p.objectId.toCanonical());
        }
    }

    // ---- 第 5 步：CR-05 值形态闸门（D-3：凡以保留 token 命名的
    // Environment 条目恒久校验，不论必填开关——值形态是跨单元身份可比的
    // 前提，与"谁必填"无关）。同时为第 6 步收集保留条目的在在性。
    bool hasModelIdentity = false;
    bool hasRobworkBaseline = false;
    for (const DependencyEntry& e : entries) {
        if (e.kind != DependencyKind::Environment) {
            continue;
        }
        const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
        if (p.token == kEnvRuntimeModelIdentity) {
            // 模型身份值＝RuntimeSnapshot.modelIdentity 规范文本
            // （"cid-<64hex>"，CR-05 裁决第 1 点）——可解析性即形态闸门；
            // evidence 只验证形态，从不重算模型身份（无重编码入口）。
            if (!core::ContentIdentity::tryFromCanonical(p.valueToken).has_value()) {
                throw EvidenceError(
                    EvidenceErrorCode::SliceIncomplete,
                    "runtime.model-identity 值形态非法（须为 cid-<64hex> 规范文本）："
                        + p.valueToken);
            }
            // token 级唯一：同保留 token 出现两次＝身份贡献歧义（条目级
            // (kind,key) 唯一挡不住"不同 key、同 token"的组合）。
            if (hasModelIdentity) {
                throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                    "runtime.model-identity 条目重复（身份贡献歧义）");
            }
            hasModelIdentity = true;
        } else if (p.token == kEnvRuntimeRobworkBaseline) {
            // 基线版本值＝commit/tag＋选项摘要（非空、无空白——
            // RobWorkBaselineVersion 契约；空白会使编码域承载歧义）。
            if (p.valueToken.empty()) {
                throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                    "runtime.robwork-baseline 值为空（基线版本必填）");
            }
            for (const char c : p.valueToken) {
                if (isAsciiWhitespace(c)) {
                    throw EvidenceError(
                        EvidenceErrorCode::SliceIncomplete,
                        "runtime.robwork-baseline 值含空白字符（基线串契约禁止）");
                }
            }
            if (hasRobworkBaseline) {
                throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                                    "runtime.robwork-baseline 条目重复（身份贡献歧义）");
            }
            hasRobworkBaseline = true;
        }
    }

    // ---- 第 6 步：CR-05 必填校验（消费 CanonicalModel 的评估必填双条目
    // ——与 compilerContractVersion 必填口径并列；开关由组装方按评估器
    // 声明置位，evidence 不域判）。缺失即拒绝：跨基线/跨编译器错误复用
    // 的防线（CR-05 冲突描述——基线升级不改变 sliceId 的后果）。
    if (m_consumesCanonicalModel && (!hasModelIdentity || !hasRobworkBaseline)) {
        throw EvidenceError(
            EvidenceErrorCode::SliceIncomplete,
            "消费 CanonicalModel 的评估缺少必填 Environment 条目（CR-05）："
                + std::string(!hasModelIdentity ? "runtime.model-identity " : "")
                + std::string(!hasRobworkBaseline ? "runtime.robwork-baseline" : ""));
    }

    // ---- 第 7 步：组装冻结切片并计算双层身份（D-2：身份＝编码的导出值，
    // 摘要唯一经 core ContentDigester——CR-02）。
    InputSlice slice;
    slice.evaluationKey = m_evaluationKey;
    slice.evaluatorContractVersion = m_contractVersion;
    slice.entries = std::move(entries);
    slice.snapshotId = snapshot.snapshotId;
    slice.sliceId.bytes = computeSliceDigest(SliceCodec::encodeFull(slice));
    slice.inputBaselineId.bytes
        = computeSliceDigest(SliceCodec::encodeBaselineProjection(slice));
    return slice;
}

// =====================================================================
// SliceCodec
// =====================================================================

std::vector<std::uint8_t> SliceCodec::encodeFull(const InputSlice& slice)
{
    std::vector<std::uint8_t> out;
    // 头：magic＋版本＋形态字节（§5.2 版本化行——升版＝全体切片身份变化）。
    appendRaw(out, reinterpret_cast<const std::uint8_t*>(kMagic.data()), kMagic.size());
    appendU8(out, kCodecVersion);
    appendU8(out, 0x00u);  // 形态：full。
    writeFullBody(out, slice);
    return out;
}

std::vector<std::uint8_t> SliceCodec::encodeBaselineProjection(const InputSlice& slice)
{
    std::vector<std::uint8_t> out;
    appendRaw(out, reinterpret_cast<const std::uint8_t*>(kMagic.data()), kMagic.size());
    appendU8(out, kCodecVersion);
    appendU8(out, 0x01u);  // 形态：baseline-projection（非往返载体）。
    writeBaselineProjectionBody(out, slice);
    return out;
}

InputSlice SliceCodec::parse(const std::vector<std::uint8_t>& encoding)
{
    Reader r{encoding};
    // 头：magic 逐字节核对（形态混淆/异单元编码在此拦截——magic 互异是
    // CR-02"无跨单元同构编码"纪律的落点）。
    r.need(8);
    if (std::memcmp(encoding.data(), kMagic.data(), kMagic.size()) != 0) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "magic 不符（非 IRDSLCE1 编码）");
    }
    r.pos = kMagic.size();
    const std::uint8_t version = r.readU8();
    if (version != kCodecVersion) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "codec 版本不符（期望 " + std::to_string(kCodecVersion)
                                + "，实际 " + std::to_string(version) + "）");
    }
    const std::uint8_t form = r.readU8();
    if (form != 0x00u) {
        // baseline-projection 是身份摘要对象而非数据交换格式——不接受
        // 解析（runtime RT-Codec 身份域"非往返载体"同款纪律）。
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "形态字节非法（parse 仅接受 full 形态编码）");
    }

    InputSlice slice;
    // full 体：评估面＋快照身份＋条目段（与 writeFullBody 严格互逆）。
    slice.evaluationKey = r.readLengthString();
    slice.evaluatorContractVersion = r.readU32();
    std::memcpy(slice.snapshotId.bytes.data(), r.readRaw32().data(), 32);
    const std::uint32_t entryCount = r.readU32();
    // 条数上限：长度前缀字段最大 65535 字节/条目，4 字节条数在 32 位长度
    // 约束下不可能合法超出此界——防御性上限防恶意条数导致预分配放大。
    if (entryCount > 0xFFFFFFFFu / 8u) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "条目数超出防御上限（" + std::to_string(entryCount) + "）");
    }
    slice.entries.reserve(entryCount);
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        slice.entries.push_back(readEntry(r));
    }
    r.expectEnd();

    // 双身份重算（D-2：身份是编码的函数——解析结果必与内容自洽）。
    slice.sliceId.bytes = computeSliceDigest(encodeFull(slice));
    slice.inputBaselineId.bytes = computeSliceDigest(encodeBaselineProjection(slice));
    return slice;
}

std::array<std::uint8_t, 8> SliceCodec::canonicalF64(double value)
{
    // 非有限拒绝（§5.2："NaN/±Inf 在编码入口拒绝"——非有限值无稳定位
    // 模式语义，进身份会破坏 NFR-COR-02；EvidenceError 码面同单元拒绝轨）。
    if (!std::isfinite(value)) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "遇到非有限 double（NaN/±Inf 编码入口拒绝——§5.2）");
    }
    // 位模式提取：memcpy 按宿主表示取 IEEE754 位型（double 二进制 64 是
    // 全部支持平台的既定表示——runtime RT-Codec f64 同款），随后按大端
    // 序列化——字节布局与宿主端序无关。
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double 须为 64 位（IEEE754 二进制 64）");
    std::memcpy(&bits, &value, sizeof(bits));
    std::array<std::uint8_t, 8> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    }
    return out;
}

double SliceCodec::parseCanonicalF64(const std::uint8_t* bytes)
{
    // 大端重组位型→宿主 double（canonicalF64 的严格逆）。
    std::uint64_t bits = 0;
    for (int i = 0; i < 8; ++i) {
        bits = (bits << 8) | bytes[static_cast<std::size_t>(i)];
    }
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    // 接收端复核（runtime RT-Codec f64 读取同款双端纪律）：手工构造/
    // 传输损坏的位型在此暴露为异常，而非静默进入身份计算。
    if (!std::isfinite(value)) {
        throw EvidenceError(EvidenceErrorCode::SliceIncomplete,
                            "非有限 double 位型（NaN/±Inf 接收端拒绝）");
    }
    return value;
}

}  // namespace sdurws::ird::evidence
