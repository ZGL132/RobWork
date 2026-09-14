/**
 * @file   Confirmable.cpp
 * @brief  可确认诊断服务实现——FindingId 解析/生成、findingDigest canonical
 *         编码、ConfirmableService 状态机与绑定复核（§5.3/§5.4/§9.3）。
 *
 * 设计依据：
 *   - units/diagnostics.md §5.2（FindingRecord 字段表）、§5.3（绑定四元组与
 *     失效条件）、§5.4（服务端状态机与转移约束）、§5.5/§5.6（时序/UI 关闭）、
 *     §9.3（接口契约——五方法签名与前置/后置/错误面）
 *   - 任务契约 tasks/foundation/DIAG-T05.json；DT-CFM-1~10
 *
 * ◆ findingDigest 的 canonical 编码（findingRecordDigest——CR-02：SHA-256
 *   只经 core::ContentDigester；编码表登记，同 DiagCodes.cpp manifest 编码
 *   模式——u32le 长度前缀＋原始字节，小端为登记约定）：
 *     "IRDDREC1"（8 字节 magic——域隔离，防跨用途摘要混淆）
 *     逐字段（序＝core::DiagnosticRecord 成员序，§4.8 表行序）：
 *       code                    ＝ u32le(len)＋原始字节
 *       subject                 ＝ u8 hasSubject；若 1：ObjectId 16 字节
 *       localName               ＝ u8 hasLocalName；若 1：u32le(len)＋字节
 *       runtimeName             ＝ u8 hasRuntimeName；若 1：u32le(len)＋字节
 *       context/cause/recommendedAction ＝ 各 u32le(len)＋字节（空串合法）
 *       comparison              ＝ u8 hasComparison；若 1：actual/expected 两侧
 *         每侧＝ u8 state（core::FieldState 0..3——枚举值即编码，编译期固定）
 *               ＋ u8 hasValue；若 1：double 的 IEEE-754 位模式 u64le（8 字节
 *                 ——memcpy 位复制的字节序归一为小端，跨平台一致）
 *               ＋ u8 hasRaw；若 1：u32le(len)＋字节（Invalid 保留原串）
 *               ＋ u8 hasProv；若 1：u8 kind（core::ProvenanceKind 枚举值）
 *                 ＋ u8 hasSrcObj；若 1：16 字节
 *                 ＋ u8 hasSrcVer；若 1：32 字节
 *                 ＋ u8 hasMethod；若 1：u32le(len)＋字节
 *         unit                    ＝ u32le(len)＋symbol 字节（冻结 token 原文）
 *   确定性（NFR-COR-02）：全部编码无环境依赖——同记录同摘要；枚举值/位模式
 *   的稳定性由 P-DIAG-1 基线（core.md v0.1）钉住，core 冻结 diff 后复核。
 *
 * 线程安全：ConfirmableService 各方法经 m_mutex 串行化（§9.3"短临界区"）；
 * FindingId::generate 用 thread_local 引擎（无锁并发）。
 */

#include <sdurws/ird/diagnostics/Confirmable.hpp>

#include <cstring>
#include <random>
#include <utility>

namespace sdurws::ird::diagnostics {

namespace {

// ---------------------------------------------------------------------
// canonical 编码原语（编码表见文件头——与 DiagCodes.cpp 同模式、小端同约定）
// ---------------------------------------------------------------------

/// 追加 u32 小端 4 字节（编码表"u32le"项）。
void feedU32le(core::ContentDigester& digester, std::uint32_t value)
{
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFu),
        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
        static_cast<std::uint8_t>((value >> 16) & 0xFFu),
        static_cast<std::uint8_t>((value >> 24) & 0xFFu)};
    digester.update(bytes.data(), bytes.size());
}

/// 追加 u64 小端 8 字节（double 位模式编码项——memcpy 位复制后小端归一）。
void feedU64le(core::ContentDigester& digester, std::uint64_t value)
{
    const std::array<std::uint8_t, 8> bytes{
        static_cast<std::uint8_t>(value & 0xFFu),
        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
        static_cast<std::uint8_t>((value >> 16) & 0xFFu),
        static_cast<std::uint8_t>((value >> 24) & 0xFFu),
        static_cast<std::uint8_t>((value >> 32) & 0xFFu),
        static_cast<std::uint8_t>((value >> 40) & 0xFFu),
        static_cast<std::uint8_t>((value >> 48) & 0xFFu),
        static_cast<std::uint8_t>((value >> 56) & 0xFFu)};
    digester.update(bytes.data(), bytes.size());
}

/// 追加长度前缀字符串（编码表"u32le(len)＋原始字节"项）。
void feedString(core::ContentDigester& digester, std::string_view text)
{
    feedU32le(digester, static_cast<std::uint32_t>(text.size()));
    digester.update(text.data(), text.size());
}

/// 追加 u8 布尔/枚举位（编码表"u8"项——0/1 或枚举序数值）。
void feedU8(core::ContentDigester& digester, std::uint8_t value)
{
    digester.update(&value, 1);
}

/// 追加定长字节块（身份/摘要原始字节——16/32 字节）。
void feedBytes(core::ContentDigester& digester, const std::uint8_t* data, std::size_t n)
{
    digester.update(data, n);
}

/// 编码一个比较值侧（§4.8 ComparativeValue——四态数值＋单位；编码表"每侧"项）。
void feedComparativeValue(core::ContentDigester& digester,
                          const core::ComparativeValue& value)
{
    // 四态标记（core::FieldState 枚举值即编码——编译期固定，P-DIAG-1 基线）。
    feedU8(digester, static_cast<std::uint8_t>(value.quantity.state()));
    // 数值载荷：仅 Provided 态编码位模式（其余态无值可编码——"缺失≠零"纪律，
    // NFR-COR-03：不把缺失编码为 0）。
    if (const auto number = value.quantity.tryValue()) {
        feedU8(digester, 1);
        std::uint64_t bits = 0;
        static_assert(sizeof(double) == sizeof(bits), "digest 编码要求 double 为 64 位（IEEE-754）");
        std::memcpy(&bits, &*number, sizeof(bits));
        feedU64le(digester, bits);
    } else {
        feedU8(digester, 0);
    }
    // Invalid 态保留原串（NFR-COR-03：非法输入不静默转 0——原串是身份的一部分）。
    if (value.quantity.state() == core::FieldState::Invalid) {
        feedU8(digester, 1);
        feedString(digester, value.quantity.invalidRawInput());
    } else {
        feedU8(digester, 0);
    }
    // 来源记录（仅 Provided 态语义——其余态编码零标志，保证同态同编码）。
    if (value.quantity.state() == core::FieldState::Provided) {
        feedU8(digester, 1);
        const auto& provenance = value.quantity.provenance();
        feedU8(digester, static_cast<std::uint8_t>(provenance.kind));
        feedU8(digester, provenance.sourceObject.has_value() ? 1 : 0);
        if (provenance.sourceObject) {
            feedBytes(digester, provenance.sourceObject->bytes.data(),
                      provenance.sourceObject->bytes.size());
        }
        feedU8(digester, provenance.sourceVersion.has_value() ? 1 : 0);
        if (provenance.sourceVersion) {
            feedBytes(digester, provenance.sourceVersion->bytes.data(),
                      provenance.sourceVersion->bytes.size());
        }
        feedU8(digester, provenance.methodTag.has_value() ? 1 : 0);
        if (provenance.methodTag) {
            feedString(digester, *provenance.methodTag);
        }
    } else {
        feedU8(digester, 0);
    }
    // 单位：冻结 token 原文（持久化契约——UnitToken::symbol 不改名）。
    feedString(digester, value.unit.symbol());
}

/// 码小写形（P-DIAG-9 键体系派生规则——titleKey/detailKey 的既定约定为
/// "diag.<code-lower>.<槽位>"；confirmTextKey 沿用同一 <code-lower> 派生，
/// 槽位名 ".confirm"（实现口径登记：单元卡 §5.2 confirmTextKey 行"键随码表
/// 登记"的派生面——与文案键同一命名体系，不引入第二套键规则）。
std::string codeLowerOf(std::string_view code)
{
    std::string lower;
    lower.reserve(code.size());
    for (const char ch : code) {
        lower.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    }
    return lower;
}

/// 状态名 token（转移日志用——Dev 行机器判读面；非持久化契约，拼写随日志
/// 约定小写。值域＝FindingState 全四值，switch 全枚举无 default——新增枚举
/// 值时编译器告警暴露遗漏）。
std::string_view findingStateToken(FindingState state) noexcept
{
    switch (state) {
    case FindingState::Pending:     return "Pending";
    case FindingState::Confirmed:   return "Confirmed";
    case FindingState::Invalidated: return "Invalidated";
    case FindingState::Expired:     return "Expired";
    }
    return "Unknown";
}

}  // namespace

// =====================================================================
// findingRecordDigest（绑定四元组首成员的唯一定义点——头文件契约）
// =====================================================================

core::Digest256 findingRecordDigest(const core::DiagnosticRecord& record)
{
    // SHA-256 唯一路径（CR-02/acceptance 4）：摘要算法只经 core::ContentDigester，
    // 本单元不私设第二哈希实现。
    core::ContentDigester digester;
    digester.update("IRDDREC1", 8);   // 域隔离 magic（编码表首行——防跨用途摘要混淆）

    // 逐字段编码（序＝core::DiagnosticRecord 成员序——编码表登记，不得重排：
    // 重排＝摘要契约破坏，须走单元卡增量修订并评估留痕兼容性）。
    feedString(digester, record.code);
    feedU8(digester, record.subject.has_value() ? 1 : 0);
    if (record.subject) {
        feedBytes(digester, record.subject->bytes.data(), record.subject->bytes.size());
    }
    feedU8(digester, record.localName.has_value() ? 1 : 0);
    if (record.localName) {
        feedString(digester, *record.localName);
    }
    feedU8(digester, record.runtimeName.has_value() ? 1 : 0);
    if (record.runtimeName) {
        feedString(digester, *record.runtimeName);
    }
    feedString(digester, record.context);
    feedString(digester, record.cause);
    feedString(digester, record.recommendedAction);
    feedU8(digester, record.comparison.has_value() ? 1 : 0);
    if (record.comparison) {
        feedComparativeValue(digester, record.comparison->actual);
        feedComparativeValue(digester, record.comparison->expected);
    }
    return digester.finalize();
}

// =====================================================================
// FindingId（P-DIAG-4：自持解析——句法与 core Id128 严格一致）
// =====================================================================

namespace {

/// FNV-1a 128 位哈希（仅 generate 的非零混入用？——不：与 core 同源纪律，
/// generate 走 mt19937_64 直接填充，此函数未用则不引入。保留说明：本单元
/// 不自建哈希路径，见 findingRecordDigest）。

/// thread_local 随机引擎（与 core Id128 generate 同源纪律：random_device
/// 播种的 mt19937_64——非密码学承诺，身份唯一性足够；进程内每线程独立无锁）。
std::mt19937_64& findingIdEngine()
{
    thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

/// 严格解析 "fnd-<32 个小写十六进制>"→16 字节（P-DIAG-4：tag 逐字符匹配、
/// 长度恰 32、字符集仅 [0-9a-f]、大写拒绝、无前后缀/空白——规则与 core
/// detail::tryParseId128 逐字一致，实现自持不 include core detail（R-2：
/// detail 层非公共契约，跨单元禁 include）。
bool tryParseFindingId(std::string_view text, std::array<std::uint8_t, 16>* out) noexcept
{
    // 长度前置：4（tag "fnd-"）＋32（hex）＝36，多一字节即拒（无前后缀/空白）。
    if (text.size() != 36) { return false; }
    // tag 逐字符匹配 "fnd-"（tag 不符＝不属于本身份体系——同 core 强类型纪律：
    // 把 rev-… 文本喂给本解析必须在边界失败）。
    if (text[0] != 'f' || text[1] != 'n' || text[2] != 'd' || text[3] != '-') { return false; }
    // 逐字符解十六进制（大写 [A-F] 拒绝——规范文本小写冻结；非法字符拒绝）。
    for (std::size_t i = 0; i < 16; ++i) {
        const auto hi = static_cast<std::uint8_t>(text[4 + i * 2]);
        const auto lo = static_cast<std::uint8_t>(text[5 + i * 2]);
        const auto hexToNibble = [](std::uint8_t c) -> int {
            if (c >= '0' && c <= '9') { return c - '0'; }
            if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
            return -1;   // 大写/符号/空白一律非法
        };
        const int h = hexToNibble(hi);
        const int l = hexToNibble(lo);
        if (h < 0 || l < 0) { return false; }
        (*out)[i] = static_cast<std::uint8_t>((h << 4) | l);
    }
    return true;
}

}  // namespace

FindingId FindingId::generate()
{
    FindingId id;
    // thread_local 引擎一次填满 128 位（两个 64 位随机字——与 core 同源纪律）。
    auto& engine = findingIdEngine();
    const std::uint64_t lo = engine();
    const std::uint64_t hi = engine();
    std::memcpy(id.bytes.data(), &lo, sizeof(lo));
    std::memcpy(id.bytes.data() + sizeof(lo), &hi, sizeof(hi));
    // 保留值纪律：全零重取（generate 保证非零——零是"未设置"保留值）。
    if (id.bytes == std::array<std::uint8_t, 16>{}) {
        return FindingId::generate();
    }
    return id;
}

FindingId FindingId::fromCanonical(std::string_view text)
{
    FindingId id;
    // 严格解析失败＝调用方契约违约 fail-fast（§9.0——DiagnosticsError，错误轨
    // 归本单元而非 core::CoreError；P-DIAG-4 句法一致但异常类型各归其单元）。
    if (!tryParseFindingId(text, &id.bytes)) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "findingId 规范文本非法（要求 fnd-<32 小写 hex>，"
                               "句法与 core Id128 严格一致——P-DIAG-4）: " + std::string(text));
    }
    return id;
}

std::optional<FindingId> FindingId::tryFromCanonical(std::string_view text) noexcept
{
    FindingId id;
    if (!tryParseFindingId(text, &id.bytes)) {
        return std::nullopt;   // try 轨：容错收集场景不抛（日志回读等）
    }
    return id;
}

std::string FindingId::toCanonical() const
{
    // "fnd-"＋32 小写 hex（字节序＝规范文本序——与 parse 构成往返）。
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    out.append("fnd-");
    for (const std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

// =====================================================================
// FindingRecord（等值与 core 投影）
// =====================================================================

bool FindingRecord::operator==(const FindingRecord& o) const
{
    return findingId == o.findingId && finding == o.finding
        && sourceCommandType == o.sourceCommandType
        && commandPayloadDigest == o.commandPayloadDigest && project == o.project
        && branch == o.branch && baseRevisionId == o.baseRevisionId
        && snapshotId == o.snapshotId && inputSliceId == o.inputSliceId
        && policyContentId == o.policyContentId && subjectScope == o.subjectScope
        && evidenceRefs == o.evidenceRefs && confirmTextKey == o.confirmTextKey
        && optionKeys == o.optionKeys && createdAtUtc == o.createdAtUtc
        && expiresAtUtc == o.expiresAtUtc && state == o.state
        && callbackToken == o.callbackToken && rejectionReason == o.rejectionReason
        && confirmation == o.confirmation && binding == o.binding;
}

core::ConfirmableFinding FindingRecord::coreProjection() const
{
    // §5.4 投影规则（P-DIAG-6：Invalidated/Expired 仅存在于服务端记录）：
    // - Confirmed：内嵌 finding 已由服务经 core confirm() 推进（Confirmed＋凭据
    //   ——core C-2 自持），直返拷贝；
    // - Invalidated(user-rejected)：服务已走 core reject()（core Rejected），直返；
    // - Invalidated(其他)/Expired：core 契约无失效承载——投影保持 Pending
    //   （"未确认"事实；服务端转移不伪造 core 终局）。
    if (state == FindingState::Invalidated && rejectionReason == std::string(finding_reason::kUserRejected)) {
        return finding;   // core 态已为 Rejected（服务转移时同步推进）
    }
    if (state == FindingState::Invalidated || state == FindingState::Expired) {
        // 服务端设施态不落 core：返回 Pending 投影（保持 core C-2：无凭据）。
        core::ConfirmableFinding projected = finding;
        projected.state = core::ConfirmationState::Pending;
        projected.credential.reset();
        return projected;
    }
    return finding;   // Pending/Confirmed：内嵌 core 态即投影
}

// =====================================================================
// ConfirmableService（§5.4 状态机＋§5.3 复核＋§9.3 五方法）
// =====================================================================

ConfirmableService::ConfirmableService(const IDiagnosticRegistry& registry,
                                       const IConfirmationEnvironment& environment,
                                       const IClock& clock, IDevLogSink* devLog)
    : m_registry(&registry)
    , m_environment(&environment)
    , m_clock(&clock)
    , m_devLog(devLog)   // 可空：两级日志管线随 DIAG-T07 落地，接线前转移日志静默跳过
{
}

FindingRecord ConfirmableService::create(
    const core::ConfirmableFinding& finding, std::string_view commandType,
    core::ContentIdentity commandDigest, core::ProjectId project, core::BranchId branch,
    core::RevisionId baseRevisionId, std::optional<core::ContentIdentity> policyContentId,
    std::vector<core::ObjectId> subjectScope)
{
    // ---- 前置校验（§9.3 create 注释原文逐条——错误面 ComparisonMissing /
    // ContextMissing / Usage；违约即抛，fail-fast 不产生半构造记录）。----

    // ①core C-1/C-2 形态：可确认诊断必为比较型（core::ConfirmableFinding::make
    // 已强制，但服务可被直接传入手工构造对象——服务端复验，不信任上游类型状态）；
    // 初始态必须 Pending 且不携带凭据（确认凭据只能由本服务经 submitConfirmation
    // 写入——创建即带凭据＝绕过状态机）。
    if (!finding.record.comparison.has_value()) {
        throw DiagnosticsError(DiagnosticsErrorCode::ComparisonMissing,
                               "create：finding.record 必为比较型（core C-1——"
                               "不可比较的发现不可确认）");
    }
    if (finding.state != core::ConfirmationState::Pending || finding.credential.has_value()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "create：finding 初始态必须 Pending 且不带凭据"
                               "（core C-2——凭据只经 submitConfirmation 写入）");
    }

    // ②码前置：code 已注册且 confirmable=true（§9.3"code 的 confirmable=true"）。
    // 未注册/废弃码查询归注册表（find）；未注册→Usage（detail 指明——§9.3 错误
    // 面未列 CodeUnknown，确认服务的码前置语义归 Usage 调用方违约）。
    const core::DiagCode& code = finding.record.code;
    const CodeDescriptor* descriptor = m_registry->find(code);
    if (descriptor == nullptr) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "create：码未注册（码值权威＝StableCodeRegistry）: " + code);
    }
    if (descriptor->deprecated) {
        throw DiagnosticsError(DiagnosticsErrorCode::CodeDeprecated,
                               "create：废弃码不可构造新 finding（§4.5.1）: " + code);
    }
    if (!descriptor->confirmable) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "create：码非可确认类（confirmable=false——确认前置"
                               "违约）: " + code);
    }

    // ③命令上下文必填（§9.3"commandDigest/baseRevisionId 必填"；§9.0
    // ContextMissing 行"命令路径→command/revision"——四身份缺一即上下文缺失）。
    if (commandDigest.bytes == core::Digest256{}
        || !project.isValid() || !branch.isValid() || !baseRevisionId.isValid()) {
        throw DiagnosticsError(DiagnosticsErrorCode::ContextMissing,
                               "create：命令路径上下文缺失（commandDigest/project/"
                               "branch/baseRevisionId 必填且有效——§9.3 前置）");
    }
    // 来源命令类型 token 非空（§5.2 sourceCommandType 必填）。
    if (commandType.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "create：sourceCommandType 为空（必填 token）");
    }

    // ④作用对象集必填且逐个有效（§5.2 subjectScope 必填——确认只覆盖登记的
    // 作用对象；空集＝无对象的确认没有语义）。
    if (subjectScope.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "create：subjectScope 为空（必填——确认只覆盖登记的"
                               "作用对象，§5.2）");
    }
    for (const auto& object : subjectScope) {
        if (!object.isValid()) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "create：subjectScope 含非法（全零）ObjectId");
        }
    }

    // ---- 构造与冻结（§9.3 后置：FindingRecord(Pending)；findingId 分配；
    // binding 四元组冻结；callbackToken 分配）。----

    FindingRecord record;
    record.findingId = FindingId::generate();
    record.finding = finding;                       // core 契约拷贝（Pending，无凭据）
    record.sourceCommandType = std::string(commandType);
    record.commandPayloadDigest = commandDigest;
    record.project = project;
    record.branch = branch;
    record.baseRevisionId = baseRevisionId;
    record.policyContentId = policyContentId;
    record.subjectScope = std::move(subjectScope);
    // confirmTextKey：P-DIAG-9 键体系派生（"diag.<code-lower>.confirm"——与
    // titleKey/detailKey 同一命名规则；值归 ui/文案资源，服务只持键）。实现
    // 口径登记：§5.2 该行"键随码表登记"的派生面随码名机械确定。
    record.confirmTextKey = "diag." + codeLowerOf(code) + ".confirm";
    // optionKeys/evidenceRefs/snapshotId/inputSliceId：create 契约签名（§9.3
    // 逐字承载）无对应参数——保持默认空/无值；这些字段不参与状态机与绑定
    // 复核，丰富的调用方由 create 返回的记录拷贝按需补填（服务端不感知、
    // 不改写——服务端原件仅状态字段可变，§5.2 表头）。
    record.createdAtUtc = m_clock->nowUtc();        // 时钟注入（测试可替换——DT-CFM-3）
    // expiresAtUtc 保持 nullopt（P-DIAG-6：默认无限期——显式设置只经 armExpiration）。
    record.state = FindingState::Pending;

    // 绑定四元组冻结（§5.3：创建时计算并随 FindingRecord 冻结）：
    // findingDigest＝SHA-256 over record canonical（唯一定义点 findingRecordDigest）。
    record.binding.findingDigest = findingRecordDigest(record.finding.record);
    record.binding.policyContentId = policyContentId;
    record.binding.commandDigest = commandDigest;
    record.binding.baseRevisionId = baseRevisionId;

    {
        // callbackToken 分配（进程内单调计数；0＝保留值"无效令牌"——分配从 1 起）。
        // 临界区内赋值：并发 create 的分配互斥（§9.3 状态转移内部互斥的同一把锁
        // 保护分配器与记录表——短临界区）。
        std::lock_guard<std::mutex> lock(m_mutex);
        record.callbackToken = m_nextToken++;
        // 记录表插入：findingId 碰撞概率忽略（128 位随机），仍以注册语义防御
        // （插入失败＝Usage——不静默覆盖既有记录）。
        const auto inserted = m_records.emplace(record.findingId, record);
        if (!inserted.second) {
            throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                                   "create：findingId 碰撞（统计上不可期望——"
                                   "防御性拒绝，不覆盖既有记录）");
        }
    }

    // 转移日志：create 入口（Pending 初态）同样可追溯（§5.4"每次状态转移写入
    // 开发级日志"——初态分配记一行，后续转移各有其行）。
    if (m_devLog != nullptr) {
        m_devLog->logDev("diag.confirmable",
                         "finding created: " + record.findingId.toCanonical()
                             + " state=Pending code=" + code);
    }
    return record;
}

ConfirmOutcome ConfirmableService::submitConfirmation(FindingId findingId,
                                                      std::uint64_t callbackToken,
                                                      core::ConfirmationCredential credential)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // ①存在性（§9.3 结果枚举：UnknownFinding——查询类前置走返回值不抛）。
    const auto it = m_records.find(findingId);
    if (it == m_records.end()) {
        return ConfirmOutcome::UnknownFinding;
    }
    FindingRecord& record = it->second;

    // ②惰性过期（§5.4：expiresAtUtc 显式设置且到达——Expired 为终态，提交随
    // 之无效；默认 nullopt 恒不触发，P-DIAG-6）。返回值取 InvalidState（当前
    // 状态已不允许确认——契约结果枚举无 Expired 值，口径登记于单元卡）。
    expireIfDue(record);
    if (record.state != FindingState::Pending) {
        // §9.3 非法调用行："Confirmed 后 submitConfirmation（InvalidState）"；
        // Invalidated/Expired 同理——终态一律 InvalidState。
        return ConfirmOutcome::InvalidState;
    }

    // ③回调令牌一次性校验（§5.2 callbackToken 行"同一令牌仅一次提交有效"；
    // §9.3 非法调用行"跨命令重用 callbackToken"）。不匹配＝调用方违约面，
    // 但结果枚举优先（§9.3 注释"返回值与异常双轨：结果枚举优先"）——返回
    // InvalidState 不抛。
    if (record.callbackToken == 0 || callbackToken != record.callbackToken) {
        return ConfirmOutcome::InvalidState;
    }

    // ④绑定四元组复核（§5.3：确认提交时——ui 回调返回后、放行前；§9.3 后置
    // "复核 binding 四元组（当前策略/命令/修订）"）。
    std::string_view reason;
    if (!verifyBinding(record, reason)) {
        // 不符 ⇒ 确认凭据失效（§5.3）——Invalidated(reason)＋返回失败，不静默
        // 沿用旧确认（§9.3 后置：不一致→Invalidated(binding-mismatch)——按不符
        // 成员归类 reason：revision-changed/policy-changed/binding-mismatch，
        // DT-CFM-4/5 观测点）。
        finishTransition(record, FindingState::Invalidated, reason);
        return ConfirmOutcome::BindingMismatch;
    }

    // ⑤一致：Confirmed＋confirmation 写入（§9.3 后置）。core 投影同步推进
    // （core C-2：Confirmed ⇔ 凭据在场——core confirm 自持不变量校验）。
    record.finding.confirm(credential);
    record.confirmation = credential;
    finishTransition(record, FindingState::Confirmed, {});
    return ConfirmOutcome::Confirmed;
}

void ConfirmableService::submitRejection(FindingId findingId, std::uint64_t callbackToken,
                                         std::string_view reasonToken)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 存在性：void 方法的失败只能经异常轨表达（§9.0——调用方契约违约 fail-fast）。
    const auto it = m_records.find(findingId);
    if (it == m_records.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "submitRejection：findingId 未登记");
    }
    FindingRecord& record = it->second;

    // 原因 token 非空（§9.0 Usage"空参数等"——拒绝必须可追溯原因）。
    if (reasonToken.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "submitRejection：reasonToken 为空（必填）");
    }

    // 惰性过期：已过期 finding 不可再走拒绝路径（Expired 已是终态）。
    expireIfDue(record);

    // 状态/令牌前置（Pending＋token 匹配——同 submitConfirmation 的校验序；
    // §9.3 错误面 InvalidState）。§5.3"用户拒绝"行：回调返回 rejected →
    // Invalidated(user-rejected)——入参 token 即调用方给定（典型为
    // finding_reason::kUserRejected；词表校验不做强制白名单——§9.3 未定义
    // 校验错误，私扩词表校验＝接口偏差；token 透传登记）。
    if (record.state != FindingState::Pending) {
        throw DiagnosticsError(DiagnosticsErrorCode::InvalidState,
                               "submitRejection：仅 Pending 可拒绝（当前状态机"
                               "终态不可逆——§5.4）");
    }
    if (record.callbackToken == 0 || callbackToken != record.callbackToken) {
        throw DiagnosticsError(DiagnosticsErrorCode::InvalidState,
                               "submitRejection：callbackToken 不匹配（一次性令牌"
                               "——§5.2/§9.3 非法调用行）");
    }

    // core 投影同步：用户拒绝→core reject()（Pending→Rejected——阻止应用，
    // MDL-06④；core C-2：Rejected 不携带凭据，由 core reject 自持清理）。
    record.finding.reject();
    finishTransition(record, FindingState::Invalidated, reasonToken);
}

void ConfirmableService::invalidate(FindingId findingId, std::string_view reasonToken)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // 存在性（Usage fail-fast——失效通知方持 stale id 属调用方契约违约）。
    const auto it = m_records.find(findingId);
    if (it == m_records.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "invalidate：findingId 未登记");
    }
    FindingRecord& record = it->second;

    // 原因 token 非空（同 submitRejection——§5.3 失效必须登记原因，可追溯）。
    if (reasonToken.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "invalidate：reasonToken 为空（必填）");
    }

    // 惰性过期先行（已到期的 finding 直接 Expired——后续 invalidate 命中终态）。
    expireIfDue(record);

    // §5.4 转移约束：Invalidated/Expired 为不可逆终态——再次失效拒绝。
    // Pending 与 Confirmed 均可失效：Pending 是 §5.3 失效条件表的主路径；
    // Confirmed 的失效＝编译前复核失配的登记面（DT-CFM-4 前置"已确认
    // finding→BindingMismatch→Invalidated(revision-changed)"——凭据失效即按
    // 未确认处置，§5.3；确认事实不可"逆转回 Pending"，但复核结论须登记）。
    // 注意：已确认凭据的失效登记后，core 投影按 coreProjection 规则落回
    // Pending（"未确认"事实——失效凭据不得伪造放行）。
    if (record.state == FindingState::Invalidated || record.state == FindingState::Expired) {
        throw DiagnosticsError(DiagnosticsErrorCode::InvalidState,
                               "invalidate：已失效/已过期（不可逆终态）不可再失效"
                               "——§5.4 转移约束");
    }

    // Pending→Invalidated(reasonToken)（§5.3 失效条件的服务端入口——write-lost/
    // interaction-lost/canceled 等由 project 信号驱动；core 投影保持 Pending，
    // 见 coreProjection——非用户拒绝的失效不伪造 core 终局）。
    finishTransition(record, FindingState::Invalidated, reasonToken);
}

std::optional<FindingRecord> ConfirmableService::tryFind(FindingId findingId) const noexcept
{
    // noexcept 契约（§9.3 签名原文）：查询＋惰性过期都可能发生——加锁。
    // 值返回的拷贝在极端内存压力下可能抛 bad_alloc：契约 noexcept 意味着该
    // 极端场景 terminate 而非异常传播（§9.3 签名原文优先——实现照签）。
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_records.find(findingId);
    if (it == m_records.end()) {
        return std::nullopt;
    }
    // 惰性过期（查询面同样驱动 Expired 转移——DT-CFM-3 观测点）。
    // const 语境下执行转移：记录内容的状态字段演进属于"状态字段按状态机转移"
    // （§5.2 表头），不违反查询的只读语义（不新增/删除记录）。
    auto& record = const_cast<FindingRecord&>(it->second);
    expireIfDue(record);
    return record;
}

std::vector<FindingRecord> ConfirmableService::pendingFor(core::RevisionId baseRevisionId) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<FindingRecord> pending;
    // 惰性过期会执行状态转移（expireIfDue 演进记录的状态字段）——const 方法
    // 中经 const_cast 取得底层非 const 视图（对象本体非 const；§5.2 表头：
    // 状态字段按状态机转移不属于"查询改写"）。底层 const_cast 的合法性：
    // m_records 持有的 FindingRecord 对象本身始终非 const。
    auto& records = const_cast<std::map<FindingId, FindingRecord>&>(m_records);
    // 遍历序＝findingId 字典序（std::map 序——同集合同序，输出确定性）。
    for (auto& [id, record] : records) {
        // 惰性过期（逐记录——到期者先转移 Expired，不再出现在 Pending 结果中）。
        expireIfDue(record);
        if (record.state == FindingState::Pending && record.baseRevisionId == baseRevisionId) {
            pending.push_back(record);
        }
    }
    return pending;
}

void ConfirmableService::armExpiration(FindingId findingId,
                                       std::chrono::system_clock::time_point expiresAt)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_records.find(findingId);
    if (it == m_records.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "armExpiration：findingId 未登记");
    }
    FindingRecord& record = it->second;
    // 仅 Pending 可加时（终态不可逆——§5.4；对终态加时无意义且破坏不可变性）。
    if (record.state != FindingState::Pending) {
        throw DiagnosticsError(DiagnosticsErrorCode::InvalidState,
                               "armExpiration：仅 Pending 可设置过期（终态不可逆）");
    }
    // P-DIAG-6：expiresAtUtc 字段保留但默认 nullopt——本方法是其"显式设置"的
    // 唯一起点（当前装配不调用；启用超时＝需求变更）。
    record.expiresAtUtc = expiresAt;
}

// ---------------------------------------------------------------------
// 私有辅助（须在互斥锁内调用——见头文件契约）
// ---------------------------------------------------------------------

void ConfirmableService::expireIfDue(FindingRecord& record) const
{
    // 触发条件三要素：仍处 Pending（终态不可逆）；expiresAtUtc 显式设置
    // （默认 nullopt 恒不触发——P-DIAG-6，DT-CFM-3"默认实例恒 Pending"）；
    // 时钟已到（≥ 过期时刻——到期即失效，不含宽限）。
    if (record.state != FindingState::Pending || !record.expiresAtUtc.has_value()) {
        return;
    }
    if (m_clock->nowUtc() < *record.expiresAtUtc) {
        return;
    }
    // Pending→Expired（§5.4 状态图："expiresAtUtc 到期（默认不启用）→Expired"）。
    // Expired 无 rejectionReason token（§5.4 词表未定义过期原因——状态本身表意；
    // rejectionReason 保持 nullopt，不私造词表值）。
    finishTransition(record, FindingState::Expired, {});
}

bool ConfirmableService::verifyBinding(const FindingRecord& record,
                                       std::string_view& reason) const
{
    // 复核序＝§5.3 四元组序；首个不符成员即定性（reason 归类锚点——DT-CFM-4/5
    // 观测点 revision-changed/policy-changed）。

    // ①findingDigest：重算当前 finding 内容摘要与冻结值比对（服务端自检——
    // 记录内容不可变（§5.2 表头），内存未损则恒一致；不一致＝记录损坏，
    // 归 binding-mismatch）。
    if (findingRecordDigest(record.finding.record) != record.binding.findingDigest) {
        reason = finding_reason::kBindingMismatch;
        return false;
    }

    // ②commandDigest：命令槽内当前命令载荷摘要比对。nullopt＝无活动命令
    // （跨命令重用场景：原命令已终结——旧凭据不得沿用，§5.3"输入修订变化"
    // 行"防御跨命令重用旧确认"语义的命令侧半区）。
    const auto currentCommand = m_environment->currentCommandDigest(record.project, record.branch);
    if (!currentCommand.has_value() || !(*currentCommand == record.binding.commandDigest)) {
        reason = finding_reason::kBindingMismatch;
        return false;
    }

    // ③policyContentId：仅当绑定有值时复核（绑定 nullopt＝非策略来源类 finding
    // ——该成员不在绑定约束内，跳过）；环境 nullopt＝当前会话无已解析策略
    // （策略已不可达——与"有值绑定"必不符）。不符→policy-changed（CON-06：
    // 策略变更→阈值可能变化→须重新判定）。
    if (record.binding.policyContentId.has_value()) {
        const auto currentPolicy = m_environment->currentPolicyContentId(record.project, record.branch);
        if (!currentPolicy.has_value() || !(*currentPolicy == *record.binding.policyContentId)) {
            reason = finding_reason::kPolicyChanged;
            return false;
        }
    }

    // ④baseRevisionId：当前分支 tip 比对（§5.3"输入修订变化"行——旧确认不跨
    // 修订有效；命令槽串行保证检测无竞争，本条件防御跨命令重用旧确认）。
    if (!(m_environment->currentTipRevision(record.project, record.branch)
          == record.binding.baseRevisionId)) {
        reason = finding_reason::kRevisionChanged;
        return false;
    }
    return true;
}

void ConfirmableService::finishTransition(FindingRecord& record, FindingState to,
                                          std::optional<std::string_view> reason) const
{
    // 状态字段演进（§5.2 表头——仅状态字段按状态机转移）＋原因登记。
    record.state = to;
    if (reason.has_value()) {
        record.rejectionReason = std::string(*reason);
    }
    // 回调令牌一次性（§5.2：同一令牌仅一次提交有效；§5.6"回调令牌作废"）——
    // 任何终态转移后清零（0＝保留值"无效令牌"）。
    record.callbackToken = 0;

    // 转移日志（§5.4 转移约束："每次状态转移写入开发级日志（findingId/转移/
    // 原因 token——可追溯）"；devLog 未接线时静默跳过——DIAG-T07 接线登记）。
    // 转移不产生用户诊断（§5.4——用户拒绝路径的"未确认则阻止"呈现归 project）。
    if (m_devLog != nullptr) {
        // string_view 不与 string 直拼（无 operator+）——显式构造 std::string。
        std::string line = "finding transition: " + record.findingId.toCanonical()
                         + " -> " + std::string(findingStateToken(to));
        if (reason.has_value()) {
            line += " reason=" + std::string(*reason);
        }
        m_devLog->logDev("diag.confirmable", std::move(line));
    }
}

}  // namespace sdurws::ird::diagnostics
