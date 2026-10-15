/**
 * @file   SolidifyServiceImpl.cpp
 * @brief  外部资源固化服务实现——§5.7 流程编排的落地：读取（io 源，
 *         注入面）→ 前后一致性裁决 → 副本字节经命令事务发布入对象库
 *         （CON-03）→ materializedVersion 回填；附带固化/重关联命令
 *         处理器与载荷 v1 编解码（PRJ-T17）。
 *
 * 设计依据：
 *   - units/project.md §5.7（流程四段与责任划分——本文件按实现口径①
 *     的编排分解落位：读取/比对在命令外、发布在命令内）、§6.1（命令
 *     生命周期——提交失败的零修订面）、§6.3（invalid-payload 拒绝）、
 *     §6.4（载荷为域所有 canonical 字节——本域〔project 自有命令族〕
 *     的编码见文末载荷格式块）、§6.5（注册协议）、§4.6（对象库——
 *     oid 复用＋新 cv＝替换版本；"project 分配"语义）、§4.8（project
 *     自有格式 canonical 纪律——ASCII/定序/round-trip）；
 *   - 需求 CON-03（副本随命令入对象库并受引用保护）、PM-09/AT-21
 *     （重关联＝显式提交新修订）、NFR-REL-04（复制期间变化检测——
 *     io 检测结论透传，固化中止）、EVI-01（判据数据面在公共头）；
 *   - 任务契约 tasks/foundation/PRJ-T17.json acceptance 1～2。
 *
 * 实现口径登记（DTB §5.4，与 SolidifyServiceImpl.hpp 文件头一致）：
 *   ①读取在命令外/发布在命令内；②pending 槽交接＋载荷绑定校验；
 *   ③对象类型 token＝"ExternalResource"；④载荷 v1 行式 canonical
 *   编码（本文件"载荷 v1 格式"块）；⑤域内拒绝零用户级诊断码
 *   （CR-08 不私造——机器可读面＝status.rejection）。
 *
 * 线程模型：见 SolidifyServiceImpl.hpp 文件头（m_opMutex →
 *   m_pendingMutex 锁序；处理器只取 m_pendingMutex）。
 *
 * 载荷 v1 格式（域所有 canonical——§6.4；行序固定、'\n' 分隔、无尾
 *   随换行、全 ASCII）：
 *   行 1  magic：IRD-PRJ-SOLIDIFY-PAYLOAD-V1
 *   行 2  ref=<hex>        externalRefId（hex——任意字节安全 round-trip）
 *   行 3  path=<hex>       absolutePath（hex——含分隔符/非 ASCII 均安全）
 *   行 4  hash=<64hex>     登记内容哈希 record.contentHash256（透传，
 *                          CR-02 不重算）
 *   行 5  copy=<cv-...>    副本内容版本（服务实算——pending 绑定键）
 *   行 6  size=<dec>       record.sizeBytes（登记字节数）
 *   行 7  recorded=<hex>   record.recordedAtUtc（ISO 文本 hex 化）
 *   行 8  state=<token>    生命周期 token（处理器复核 recorded）
 *   行 9  target=<obj-|空> 固化副本对象身份（服务已解析——恒有效）
 *   行 10 budget=<dec>     读取预算（>0）
 * 摘要纪律（CR-02）：本编码器对 hash/copy 字段零计算只透传/实算经
 *   codec::contentVersionOf（core ContentDigester）——无第二哈希路径。
 */

#include "SolidifyServiceImpl.hpp"

#include <sdurws/ird/project/ProjectStore.hpp>

#include "Codec.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::project {

namespace {

// =====================================================================
// 载荷 v1 编解码辅助（行式 canonical——格式块见文件头）
// =====================================================================

/// 载荷魔数（v1——格式演进按 §6.4 版本三元组递增 payloadFormatVersion）。
constexpr const char* kPayloadMagic = "IRD-PRJ-SOLIDIFY-PAYLOAD-V1";

/// 字节串→小写十六进制（hex 化自由文本——ASCII 载荷承载任意字节，
/// round-trip 无歧义；§4.8 ASCII 磁盘纪律的域内实例）。
std::string toHexText(std::string_view text)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

/// 小写十六进制→字节串（严格：偶数长＋全 hex 字符；违约 nullopt）。
std::optional<std::string> tryFromHexText(std::string_view hex)
{
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    // 半字节解析（非法字符即整体拒绝——不跳过不容忍，canonical 严格性）。
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

/// 无符号十进制严格解析（全数字、无符号、无空白；溢出拒绝）。
std::optional<std::uint64_t> tryParseDecimal(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        // 溢出前置检查（value*10+d 不越过 uint64 上界——防回绕静默接受）。
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return std::nullopt;
        }
        value = value * 10 + digit;
    }
    return value;
}

/// 64 个小写十六进制字符校验（摘要字段的载体形态——§4.4 约定）。
bool isHex64(std::string_view text)
{
    if (text.size() != 64) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

/**
 * @brief 载荷 v1 解析结果（域内值载体——record 字段按载荷回填，
 *        materializedVersion 恒零：载荷只携带 recorded 语义，固化结果
 *        随命令提交后的修订引用集承载）。
 */
struct SolidifyPayload {
    ExternalResourceRecord record;   ///< 记录字段（state 经 token 解析）
    std::string copy;                ///< 副本内容版本（cv- 规范文本——绑定键）
    core::ObjectId target;           ///< 固化副本对象身份（v1 恒有效）
    std::uint64_t budgetBytes = 0;   ///< 读取预算（>0）
};

/**
 * @brief 载荷 v1 编码（格式块见文件头；调用方前置校验后调用——本函数
 *        只做形态装配，域校验在 validateRequest/处理器复核）。
 *
 * @param record     [in] 待固化记录（state==Recorded——validateRequest 保证）
 * @param target     [in] 已解析的副本对象身份（恒有效——submitSolidify 保证）
 * @param budgetBytes [in] 读取预算（>0——validateRequest 保证）
 * @param copyHash   [in] 副本内容版本规范文本（cv-——服务实算的绑定键）
 * @return canonical 载荷字节（同输入必同字节——NFR-COR-02）
 */
std::vector<std::uint8_t> encodeSolidifyPayload(const ExternalResourceRecord& record,
                                                const core::ObjectId& target,
                                                std::uint64_t budgetBytes,
                                                std::string_view copyHash)
{
    // 逐行装配（定序即 canonical 序——行序＝格式块行号，防字段漂移）。
    std::string text;
    text.reserve(256);
    text += kPayloadMagic;
    text += "\nref=" + toHexText(record.externalRefId);
    text += "\npath=" + toHexText(record.absolutePath);
    text += "\nhash=" + record.contentHash256;
    text += "\ncopy=" + std::string(copyHash);
    text += "\nsize=" + std::to_string(record.sizeBytes);
    text += "\nrecorded=" + toHexText(record.recordedAtUtc);
    // toToken 返回 const char*——不能与字面量指针直接相加（C2110），
    // 以 std::string 装配（语义不变，canonical 序不受影响）。
    text += std::string("\nstate=") + toToken(record.state);
    text += "\ntarget=" + target.toCanonical();
    text += "\nbudget=" + std::to_string(budgetBytes);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

/**
 * @brief 载荷 v1 严格解析（10 行定序、逐键精确匹配、逐值形态校验；
 *        任一违约＝nullopt——处理器据此 invalid-payload 拒绝，实现口径⑤
 *        不产用户诊断码）。
 *
 * @param text [in] 载荷文本（信封 payloadCanonical 的字符视图）
 * @return 解析结果；违约 nullopt
 */
std::optional<SolidifyPayload> tryParseSolidifyPayload(std::string_view text)
{
    // 行切分（'\n' 分隔；严格 10 行——不收尾随换行/空行，canonical
    // 编码的输出即唯一合法输入形态）。
    const auto splitLines = [](std::string_view t) {
        std::vector<std::string_view> lines;
        std::size_t begin = 0;
        while (true) {
            const std::size_t pos = t.find('\n', begin);
            if (pos == std::string_view::npos) {
                lines.push_back(t.substr(begin));
                break;
            }
            lines.push_back(t.substr(begin, pos - begin));
            begin = pos + 1;
        }
        return lines;
    };
    const std::vector<std::string_view> lines = splitLines(text);
    if (lines.size() != 10 || lines[0] != kPayloadMagic) {
        return std::nullopt;
    }
    // "key=value" 拆解（键精确匹配——未知键＝结构违约，当前版本不预埋
    // 兼容面：载荷演进走版本号，不改行语义）。
    const auto valueOf = [](std::string_view line, std::string_view key) -> std::optional<std::string_view> {
        if (line.rfind(key, 0) != 0 || line.size() <= key.size()
            || line[key.size()] != '=') {
            return std::nullopt;
        }
        return line.substr(key.size() + 1);
    };

    SolidifyPayload payload;

    // 第 2 行 ref=：hex 解码后非空（记录 id 必填——ExternalResourceRecord 注释）。
    // R3 审计 B5 修复（2026-10-15）：tryFromHexText 对奇数长度/非 hex 文本
    // 返回 nullopt，原条件 `!decoded->empty()` 对空 optional 解引用是 UB
    // （MSVC 下读未构造 string）——本应按载荷违约拒绝的路径变成未定义行
    // 为。补 has_value() 前置：解码失败与解码为空同走拒绝（载荷形态违约）。
    if (const auto v = valueOf(lines[1], "ref")) {
        if (const auto decoded = tryFromHexText(*v);
            decoded.has_value() && !decoded->empty()) {
            payload.record.externalRefId = std::move(*decoded);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 3 行 path=：hex 解码后非空（登记路径必填——§4.4.5 同约束；
    // has_value() 前置同上——R3 审计 B5）。
    if (const auto v = valueOf(lines[2], "path")) {
        if (const auto decoded = tryFromHexText(*v);
            decoded.has_value() && !decoded->empty()) {
            payload.record.absolutePath = std::move(*decoded);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 4 行 hash=：64 小写 hex（登记哈希形态校验——透传不重算，CR-02）。
    if (const auto v = valueOf(lines[3], "hash")) {
        if (!isHex64(*v)) {
            return std::nullopt;
        }
        payload.record.contentHash256.assign(*v);
    } else {
        return std::nullopt;
    }
    // 第 5 行 copy=：cv- 规范文本（副本绑定键——core 严格解析）。
    if (const auto v = valueOf(lines[4], "copy")) {
        if (const auto cv = core::ContentVersion::tryFromCanonical(*v)) {
            payload.copy.assign(*v);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 6 行 size=：无符号十进制（0 合法——空内容资源不预拒）。
    if (const auto v = valueOf(lines[5], "size")) {
        if (const auto n = tryParseDecimal(*v)) {
            payload.record.sizeBytes = *n;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 7 行 recorded=：hex 解码后非空（登记时刻必填——§4.4.5；
    // has_value() 前置同 ref=/path= ——R3 审计 B5）。
    if (const auto v = valueOf(lines[6], "recorded")) {
        if (const auto decoded = tryFromHexText(*v);
            decoded.has_value() && !decoded->empty()) {
            payload.record.recordedAtUtc = std::move(*decoded);
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 8 行 state=：冻结两态 token 严格解析（recorded 之外由处理器
    // 复核拒绝——本函数只负责形态）。
    if (const auto v = valueOf(lines[7], "state")) {
        if (const auto state = externalResourceStateFromToken(*v)) {
            payload.record.state = *state;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 9 行 target=：obj- 规范文本（v1 契约恒有效——空/无效即违约）。
    if (const auto v = valueOf(lines[8], "target")) {
        if (const auto oid = core::ObjectId::tryFromCanonical(*v)) {
            payload.target = *oid;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // 第 10 行 budget=：无符号十进制且 >0（零预算无意义——validateRequest
    // 同口径的载荷侧复核）。
    if (const auto v = valueOf(lines[9], "budget")) {
        if (const auto n = tryParseDecimal(*v); n && *n > 0) {
            payload.budgetBytes = *n;
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    return payload;
}

}  // namespace

// =====================================================================
// ResourceRefs.hpp 值类型自由函数（公共头声明的唯一定义点）
// =====================================================================

const char* toToken(ExternalResourceState state) noexcept
{
    switch (state) {
    case ExternalResourceState::Recorded:
        return "recorded";
    case ExternalResourceState::Materialized:
        return "materialized";
    }
    return "unknown";  // 防御（switch 全覆盖——不可达）
}

std::optional<ExternalResourceState> externalResourceStateFromToken(
    std::string_view token) noexcept
{
    if (token == "recorded") {
        return ExternalResourceState::Recorded;
    }
    if (token == "materialized") {
        return ExternalResourceState::Materialized;
    }
    return std::nullopt;  // 未知 token——调用方按载体损坏处置（不猜测）
}

ExternalRefRecord toDraftExternalRef(const ExternalResourceRecord& record)
{
    // 投影只保留 §4.4.5 冻结字段集合（固化结果三字段不进草稿文件——
    // 公共头 ExternalResourceRecord 注释的实现口径）。
    ExternalRefRecord draft;
    draft.externalRefId = record.externalRefId;
    draft.absolutePath = record.absolutePath;
    draft.contentHash256 = record.contentHash256;
    draft.sizeBytes = record.sizeBytes;
    draft.recordedAtUtc = record.recordedAtUtc;
    draft.state = toToken(record.state);
    return draft;
}

std::optional<ExternalResourceRecord> externalResourceFromDraft(
    const ExternalRefRecord& draft)
{
    // state token 严格解析（未知 token＝记录不可信——nullopt 不猜测）；
    // 固化结果三字段置 recorded 态缺省（草稿文件不携带——见公共头注释）。
    const auto state = externalResourceStateFromToken(draft.state);
    if (!state.has_value()) {
        return std::nullopt;
    }
    ExternalResourceRecord record;
    record.externalRefId = draft.externalRefId;
    record.absolutePath = draft.absolutePath;
    record.contentHash256 = draft.contentHash256;
    record.sizeBytes = draft.sizeBytes;
    record.recordedAtUtc = draft.recordedAtUtc;
    record.state = *state;
    return record;
}

ExternalResourceGateData externalResourceGateData(const ExternalResourceRecord& record,
                                                  const RevisionView& revision)
{
    // 数据面纯比对（N-9：判定归 evidence——本函数只聚合两个机械事实）。
    ExternalResourceGateData data;
    data.state = record.state;
    // 引用可达性：materialized 态才有编址键可查（recorded 的全零保留值
    // 不可能命中引用集——合取自然 false，无需特判）。
    for (const ObjectRef& ref : revision.objectRefs) {
        if (ref.objectId == record.resourceObject
            && ref.contentVersion == record.materializedVersion) {
            data.copyReferencedByRevision = true;
            break;
        }
    }
    data.gateSatisfied = data.state == ExternalResourceState::Materialized
        && data.copyReferencedByRevision;
    return data;
}

// =====================================================================
// 固化/重关联命令处理器（§5.3.2 ICommandHandler——发布面）
// =====================================================================

SolidifyResourceHandler::SolidifyResourceHandler(SolidifyServiceImpl* owner,
                                                 std::string commandType,
                                                 std::string summaryPrefix)
    : m_owner(owner)
    , m_commandType(std::move(commandType))
    , m_summaryPrefix(std::move(summaryPrefix))
{
}

PrepareOutcome SolidifyResourceHandler::prepare(HandlerContext& /*ctx*/,
                                                const CommandEnvelope& envelope,
                                                const RevisionView& /*baseSnapshot*/,
                                                CommandPlan& out,
                                                std::vector<core::DiagnosticRecord>& /*diags*/)
{
    // ---- 第 1 步：载荷 v1 严格解析（结构性违约 → invalid-payload）。
    // 实现口径⑤：零用户级诊断码（§4.6 收编清单无对应码——CR-08 不私造；
    // 机器可读面＝status.rejection，开发观察面＝命令服务 S3 既有 dev 留痕）。
    const std::string payloadText(envelope.payloadCanonical.begin(),
                                  envelope.payloadCanonical.end());
    const std::optional<SolidifyPayload> parsed = tryParseSolidifyPayload(payloadText);
    if (!parsed.has_value()) {
        return PrepareOutcome::RejectedInvalidInput;
    }

    // ---- 第 2 步：状态复核。固化/重关联只受理 recorded——materialized
    // 记录重复固化＝域校验拒绝（状态事实随修订持久化，翻新走 relink 新
    // 记录，不走状态改写）。同口径⑤零诊断码。
    if (parsed->record.state != ExternalResourceState::Recorded) {
        return PrepareOutcome::RejectedInvalidInput;
    }

    // ---- 第 3 步：取走交接副本（实现口径②）。绑定键＝refId＋副本内容
    // 版本（摘要命中即字节命中——SHA-256 抗碰撞使摘要是完备的字节身份）。
    // 服务级互斥（m_opMutex 持有中）保证槽内必是本命令的字节，校验拦截
    // 的是实现缺陷而非用户输入——失配同样按 invalid-payload 收口。
    std::optional<std::vector<std::uint8_t>> bytes;
    {
        std::lock_guard<std::mutex> guard(m_owner->m_pendingMutex);
        if (m_owner->m_pending.has_value()
            && m_owner->m_pending->refId == parsed->record.externalRefId
            && m_owner->m_pending->hash256 == parsed->copy) {
            bytes = std::move(m_owner->m_pending->bytes);
            m_owner->m_pending.reset();
        }
    }
    if (!bytes.has_value()) {
        return PrepareOutcome::RejectedInvalidInput;
    }

    // ---- 第 4 步：发布计划装配（CON-03——副本随命令事务入对象库）。
    // target 由服务解析后恒有效入载荷（v1 契约），无效形态在第 1 步已被
    // 拒绝——此处直接采用。objectTypeToken＝"ExternalResource"（实现
    // 口径③：域登记 token，随修订清单持久化）。不声明逆命令：对象库只增
    // （PA-2），固化无撤销表达，内容更正走 relink 新修订（§6.9 声明制）。
    ObjectWrite write;
    write.objectId = parsed->target;
    write.objectTypeToken = "ExternalResource";
    write.payloadCanonical = std::move(*bytes);
    out.objectWrites.push_back(std::move(write));
    // 命令摘要：前缀＋记录 id（随修订持久化——PM-12-S1 历史浏览展示面）。
    out.summary = m_summaryPrefix + " " + parsed->record.externalRefId;
    return PrepareOutcome::Planned;
}

// =====================================================================
// 固化服务实现（§5.7 服务实体）
// =====================================================================

SolidifyServiceImpl::SolidifyServiceImpl(ProjectStore& store,
                                         IExternalResourceSource* source)
    : m_store(store)
    , m_source(source)
{
    // §6.5 装配协议：两类命令共用本处理器实现（token/摘要前缀区分），
    // 经存储上下文公共装配通道注册（UI-T39 落位的 handlerRegistry()）。
    // token 重复＝注册表边界拒绝（std::invalid_argument——装配错误
    // 尽早暴露，不静默覆盖）。
    HandlerRegistry& registry = store.handlerRegistry();
    registry.registerHandler(std::make_unique<SolidifyResourceHandler>(
        this, std::string{kSolidifyCommandType}, std::string{"固化外部资源"}));
    registry.registerHandler(std::make_unique<SolidifyResourceHandler>(
        this, std::string{kRelinkCommandType}, std::string{"重关联外部资源"}));
}

void SolidifyServiceImpl::validateRequest(const SolidifyRequest& request,
                                          const ExternalResourceRecord& record) const
{
    // 调用方错误 fail-fast（AGENTS §3 错误二分——请求/记录不一致属契约
    // 违约，不走返回值轨不产诊断码；逐项语义见公共头 @throws）。
    if (request.externalRefId.empty()) {
        throw std::invalid_argument("project/solidify: externalRefId 为空");
    }
    if (record.externalRefId.empty()) {
        throw std::invalid_argument("project/solidify: record.externalRefId 为空");
    }
    if (request.externalRefId != record.externalRefId) {
        throw std::invalid_argument(
            "project/solidify: request.externalRefId 与 record.externalRefId 失配");
    }
    if (record.state != ExternalResourceState::Recorded) {
        throw std::invalid_argument(
            "project/solidify: 记录已固化（state≠recorded）——更正走 relink");
    }
    if (request.budgetBytes == 0) {
        // 零预算＝无意义的读取上限（io BudgetGuard 语义下任何读取即超限
        // ——与其让 io 失败不如调用方显式给值；实现口径：不代默认值）。
        throw std::invalid_argument("project/solidify: budgetBytes 必须大于 0");
    }
    if (m_source == nullptr) {
        // 装配违约（源未注入）——与 attachCompilePort"端口未注入即
        // fail-fast"先例同口径。
        throw std::invalid_argument("project/solidify: 外部资源源未注入（装配违约）");
    }
}

SolidifyResult SolidifyServiceImpl::solidify(const SolidifyRequest& request,
                                             core::BranchId branch,
                                             const ExternalResourceRecord& record)
{
    return submitSolidify(request, branch, record, kSolidifyCommandType,
                          "固化外部资源");
}

SolidifyResult SolidifyServiceImpl::relink(const SolidifyRequest& request,
                                           core::BranchId branch,
                                           const ExternalResourceRecord& newRecord)
{
    return submitSolidify(request, branch, newRecord, kRelinkCommandType,
                          "重关联外部资源");
}

ExternalRefProbe SolidifyServiceImpl::probe(const ExternalResourceRecord& record)
{
    // 装配违约 fail-fast（与 solidify 同口径；probe 不涉 pending 槽，
    // 无需服务级互斥——直通源，并发安全由源实现保证）。
    if (m_source == nullptr) {
        throw std::invalid_argument("project/solidify: 外部资源源未注入（装配违约）");
    }
    return m_source->probe(record);
}

SolidifyResult SolidifyServiceImpl::submitSolidify(const SolidifyRequest& request,
                                                   core::BranchId branch,
                                                   const ExternalResourceRecord& record,
                                                   const char* commandType,
                                                   const char* summaryPrefix)
{
    // ---- 服务级操作互斥（文件头线程模型）：pending 槽为单份交接面，
    // 并发固化/重关联必须串行（命令槽串行性不受影响——本锁在 submit
    // 之外多包住了读取段，读取期阻塞并发固化是可接受的语义代价）。
    std::lock_guard<std::mutex> opGuard(m_opMutex);

    // ---- 前置校验（调用方错误 fail-fast）。
    validateRequest(request, record);

    // ---- 第 1 段：io 源安全读取（§5.7 流程 1～2 段——SafePath/预算/
    // 复制/前后一致性，全部在注入源内）。失败/变化即中止：命令未提交、
    // **零修订**（实现口径①——不借用 §6.3 拒绝词表表达 io 环境事实，
    // 诊断按 §5.7 原样透传，不伪造数值）。
    const ExternalSourceReadResult read = m_source->readForSolidify(record, request.budgetBytes);
    if (!read.ok) {
        SolidifyResult result;
        result.ok = false;
        // 变化检出 → 冻结签名字段置位（值＝复制后快照哈希——新内容的
        // 对照面；nullopt＝未检出，语义见 SolidifyResult 注释）。
        if (read.sourceChangedDuringCopy) {
            result.sourceChangedDuringCopy = read.after.contentDigest;
        }
        result.diagnostics = read.diagnostics;
        return result;
    }

    // ---- 第 2 段（准备）：目标对象身份解析（§5.7"新分配或替换版本"——
    // 全零＝project 分配（core generate，§4.6 创建语义）；有效＝同 oid
    // 新 cv 替换版本——对象库内容编址天然支持，重关联谱系连续面）。
    const core::ObjectId target = request.targetResourceObject.isValid()
        ? request.targetResourceObject
        : core::ObjectId::generate();

    // 副本内容版本（CR-02 唯一哈希入口——对象库编址与修订引用将复用
    // 同一摘要事实；pending 绑定键即此值）。
    const std::string copyBytesView(reinterpret_cast<const char*>(read.canonicalBytes.data()),
                                    read.canonicalBytes.size());
    const std::string copyHash = codec::contentVersionOf(copyBytesView).toCanonical();

    // ---- 第 3 段：命令装配与提交（CON-03——固化经命令进入事务）。
    // 载荷 v1＝记录字段＋目标＋预算＋副本摘要（历史留痕的追溯面：修订
    // 命令留痕记录"固化的是哪条引用、声称什么内容、落到哪个对象"）。
    CommandEnvelope envelope;
    envelope.branch = branch;
    envelope.commandType = commandType;
    envelope.payloadFormatVersion = 1;
    envelope.payloadCanonical = encodeSolidifyPayload(record, target,
                                                      request.budgetBytes, copyHash);

    // pending 交接槽生命周期与提交严格同界（异常安全：非 StoreError 异常
    // 透传面也清残留，防下一次固化撞上陈旧字节——实现口径②）。
    {
        std::lock_guard<std::mutex> guard(m_pendingMutex);
        PendingRead pending;
        pending.refId = record.externalRefId;
        pending.hash256 = copyHash;
        pending.sizeBytes = read.canonicalBytes.size();
        pending.bytes = read.canonicalBytes;
        m_pending = std::move(pending);
    }
    CommandResult commandResult;
    try {
        // S1～S7 全流程（§6.1）——只读/Closed 上下文在 S1 拒绝
        // （Rejected(not-writable)＋PRJ-LOCK-HELD/PRJ-WRITE-AUTHORITY-
        // LOST 用户诊断经宿主 sink 产出）；失败零修订。
        commandResult = m_store.commands().submit(envelope);
    } catch (...) {
        std::lock_guard<std::mutex> guard(m_pendingMutex);
        m_pending.reset();
        throw;
    }
    {
        std::lock_guard<std::mutex> guard(m_pendingMutex);
        m_pending.reset();
    }

    // ---- 第 4 段：结果映射。未提交（拒绝/中止/失败）→ ok=false＋
    // 命令端口用户级诊断透传（sourceChangedDuringCopy 恒 nullopt——
    // 变化中止在第 1 段已返回，此路径不伪造检测事实）。
    if (!commandResult.committed()) {
        SolidifyResult result;
        result.ok = false;
        result.diagnostics = commandResult.diagnostics;
        return result;
    }

    // committed → materializedVersion＝新修订引用集中目标 oid 的内容
    // 版本（§5.7 冻结签名语义——引用保护已随该修订生效：对象不可变＋
    // 引用可达）。
    if (commandResult.newHeadState.has_value()) {
        for (const ObjectRef& ref : commandResult.newHeadState->objectRefs) {
            if (ref.objectId == target) {
                SolidifyResult result;
                result.ok = true;
                result.materializedVersion = ref.contentVersion;
                return result;
            }
        }
    }
    // 防御性不可达：提交计划含该对象（处理器产出），修订清单必含其引用
    // （TxEngine 第 4 步发布＋第 3 步验证链）——触发即实现缺陷，fail-fast
    // 不伪造成功。
    throw std::logic_error(
        "project/solidify: 提交修订缺少固化副本引用（实现缺陷——不可达）");
}

// =====================================================================
// 工厂（公共头 createSolidifyService 声明的定义点）
// =====================================================================

std::unique_ptr<SolidifyService> createSolidifyService(ProjectStore& store,
                                                       IExternalResourceSource* source)
{
    // 实现类型不出公共头（R-2）——装配方只面向 SolidifyService 抽象。
    return std::make_unique<SolidifyServiceImpl>(store, source);
}

}  // namespace sdurws::ird::project
