/**
 * @file   ConfirmationFlow.cpp
 * @brief  确认放行流绑定设施的实现体——摘要计算、留痕冻结、绑定复核与
 *         生产探针（私有实现，消费者仅 CommandServiceImpl 与同单元测试）。
 *
 * 设计依据：见私有头 ConfirmationFlow.hpp（文件头全量登记设计锚点与实现
 *   口径①～⑤）；本文件注释聚焦编码表与每段计算的业务含义。
 *
 * 实现口径登记（DTB §5.4——单元卡 §6.7 未定义判据的落值；与头文件口径
 *   清单对应）：
 *
 *   ①findingDigest 的 canonical 编码表（"对什么字节做摘要"——core.md §4.2
 *     明文该归属归所有者单元；diagnostics 侧设施对其 FindingRecord 另有
 *     编码（其 src/Confirmable.cpp 登记），两单元不直链（P-PR-6），编码
 *     各自登记、算法同源（core::ContentDigester）。本表为 project 侧冻结
 *     编码——一经有修订落盘即不可更改（留痕只增不改，PA-2；演进＝新
 *     magic 版本号，不覆写旧摘要语义）：
 *
 *       magic   = "IRD-PRJ-FINDING-DIGEST-V1"（ASCII，逐字节）
 *       逐字段  = tag(ASCII) + u64le 长度 + 字节载荷；可选成员＝1 字节
 *                 出席旗标（0x00 缺席 / 0x01 出席）再跟载荷
 *
 *       字段序（DiagnosticRecord 字段序——core DiagData.hpp 声明序）：
 *         code                              ASCII
 *         subject                           旗标 + 32 字节（Id128 原序字节）
 *         localName / runtimeName           旗标 + UTF-8
 *         context / cause / recommendedAction  UTF-8
 *         comparison                        旗标 + 出席时：
 *           actual / expected（各）：
 *             state                         1 字节（FieldState 序：0=provided,
 *                                           1=not-provided, 2=not-applicable,
 *                                           3=invalid）
 *             provided 载荷                  8 字节 IEEE-754 位模式（大端——
 *                                           与 SHA-256/FIPS 字节序一致）+
 *                                           provenance 编码（见下）
 *             invalid 载荷                  原始输入串 UTF-8
 *             unit                          ASCII token
 *           provenance 编码（provided 态）：
 *             kind                          1 字节（ProvenanceKind 声明序）
 *             sourceObject                  旗标 + 32 字节
 *             sourceVersion                 旗标 + 32 字节
 *             methodTag                     旗标 + UTF-8
 *
 *       编码性质：确定性（同值必同字节——枚举取声明序定值、无环境依赖）、
 *       无歧义（tag+长度前缀框架，无拼接粘连）、无浮点文本化（位模式直
 *       编——§4.8"无浮点"同精神，规避 locale/最短表示差异）。
 *
 *   ②policyContentId 阶段 A 来源：对"策略语境投影"（code＋comparison 三
 *     要素〔双侧四态值＋单位〕＋cause）以同框架编码（magic=
 *     "IRD-PRJ-POLICY-CONTEXT-V1"）后取 ContentIdentity——投影不含
 *     context/localName 等呈现面字段（那些属"事实描述"而非"策略语境"）。
 *     project 无④策略端口通道（P-PR-3：判定与阈值读取全在域处理器），
 *     finding 内嵌比较语境即 project 可见面的已解析策略内容；真实域处理
 *     器落位后经计划声明面接入④端口身份（增量切换，不改形状）。
 *
 *   ③复核时机落点：确认提交时＝S4 冻结后立即（回调返回后、放行前——
 *     diagnostics.md §5.5 要点⑦前半）；编译前＝S5 IModelCompilePort 调用
 *     前（要点⑦后半"确认不豁免编译"的顺序面：复核不过不进编译）。
 *
 *   ④失配处置归类：Rejected(confirmations-unresolved)——§6.7"四者任一
 *     不符…命令按未确认处置"＋diagnostics.md §5.3 同语（confirmations-
 *     unresolved）；失配维度经 reportDev 开发诊断定位（用户级稳定码无
 *     对应收编项——diagnostics.md §4.6 PRJ 清单无确认流码，不私造——
 *     CR-08）。
 *
 *   ⑤IConfirmationProbe 接缝：D-10 形态（生产窄接口/测试侧 fake）——
 *     失配分支生产不可达（槽内无并发写，§6.7 明文），注入面仅测试消费
 *     （PRJ-TX-2 第四路）；生产探针与冻结同源同函数（无第二计算路径）。
 */

#include "ConfirmationFlow.hpp"

#include <cstring>

#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Provenance.hpp>

#include "win32/StoreLock.hpp"  // win32::formatIsoMilli——凭据时刻的
                                // time_point→ISO-8601 文本（单元内唯一
                                // 时间格式来源，与心跳/提交时间同钟面）

namespace sdurws::ird::project::confirm {

namespace {

// =====================================================================
// canonical 编码器（实现口径①②的框架——两个 magic 共用同一字节框架）
// =====================================================================

/// 追加 u64 小端长度前缀（框架的定长字段——字节序显式，杜绝平台差异）。
void appendU64Le(std::string& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFFu));
    }
}

/// 追加带 tag 的字节载荷：tag(ASCII) + u64le 长度 + 原始字节。
void appendField(std::string& out, const char* tag, const void* data, std::size_t n)
{
    out += tag;
    appendU64Le(out, n);
    out.append(static_cast<const char*>(data), n);
}

/// 追加带 tag 的字符串载荷（string_view 单一入口——std::string 与字面量
/// 同经此重载，免重载歧义〔const char[N] 对两形态均可转换〕；UTF-8 透传
/// ——摘要输入只要求确定性，不做磁盘 ASCII 约束；core C-3 已保证必填串
/// 非空）。
void appendField(std::string& out, const char* tag, std::string_view text)
{
    appendField(out, tag, text.data(), text.size());
}

/// 可选成员的出席旗标（0x00 缺席 / 0x01 出席——单字节，先于载荷）。
void appendFlag(std::string& out, bool present)
{
    out.push_back(present ? static_cast<char>(0x01) : static_cast<char>(0x00));
}

/// 可选字符串成员（旗标 + tag + u64le 长度 + 字节）。
void appendOptionalString(std::string& out, const char* tag,
                          const std::optional<std::string>& text)
{
    appendFlag(out, text.has_value());
    if (text.has_value()) {
        appendField(out, tag, *text);
    }
}

/// 可选 Id128 型成员（旗标 + tag + 32 字节原序字节——core Id128 的字节序
/// 即规范文本序，直接取原始字节，不经文本往返）。
template <class Id>
void appendOptionalId(std::string& out, const char* tag, const std::optional<Id>& id)
{
    appendFlag(out, id.has_value());
    if (id.has_value()) {
        appendField(out, tag, id->bytes.data(), id->bytes.size());
    }
}

/// 32 字节摘要 → 64 字符小写 hex（§4.4.4 摘要字段形态；查表实现——
/// 确定性输出，无 iostream/locale 依赖）。
void appendHex64(std::string& out, const core::Digest256& digest)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    out.reserve(out.size() + 64);
    for (std::uint8_t byte : digest) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
}

/// core::DiagnosticRecord → findingDigest 编码（magic V1——实现口径①）。
std::string encodeFindingRecord(const core::DiagnosticRecord& record)
{
    std::string out;
    // magic 先行：编码版本演进＝换 magic（旧摘要语义不覆写——PA-2 精神；
    // 留痕里存的是摘要值，旧修订的验证按旧 magic 重算的义务归读取侧版本
    // 判定——阶段 A 单版本，无分叉）。
    appendField(out, "magic", "IRD-PRJ-FINDING-DIGEST-V1");

    // 码（稳定 token——core C-3 已保证句法合法）。
    appendField(out, "code", record.code);

    // 主体对象（可选——Id128 原序字节）。
    appendOptionalId(out, "subject", record.subject);

    // 局部名/运行时名（可选——缺失≠空串，旗标区分）。
    appendOptionalString(out, "localName", record.localName);
    appendOptionalString(out, "runtimeName", record.runtimeName);

    // 三个必填描述串（ERR-01 字段全集——context/cause/recommendedAction；
    // core C-3 保证非空，此处按原样入码）。
    appendField(out, "context", record.context);
    appendField(out, "cause", record.cause);
    appendField(out, "recommendedAction", record.recommendedAction);

    // 比较型三要素（可确认诊断必为比较型——core C-1；缺席属防御分支，
    // 旗标编码保持总序无歧义）。
    appendFlag(out, record.comparison.has_value());
    if (record.comparison.has_value()) {
        // 逐侧编码（actual 先于 expected——UX-03 三要素的声明序）。
        const core::ComparativeValue* sides[2] = {&record.comparison->actual,
                                                  &record.comparison->expected};
        for (const core::ComparativeValue* side : sides) {
            // 四态（1 字节——FieldState 声明序定值；同值必同码）。
            const auto state = side->quantity.state();
            const char stateByte = static_cast<char>(state);
            appendField(out, "state", &stateByte, 1);
            if (state == core::FieldState::Provided) {
                // 数值：IEEE-754 位模式大端直编（无浮点文本化——实现口径
                // ①；bit_cast 语义经 memcpy，避开 C++20 特性〔D-01〕）。
                const double value = side->quantity.tryValue().value_or(0.0);
                std::uint64_t bits = 0;
                static_assert(sizeof(bits) == sizeof(value), "double 须为 64 位");
                std::memcpy(&bits, &value, sizeof(bits));
                const std::uint64_t be = [] (std::uint64_t v) {
                    // 主机序→大端（字节反转——摘要输入的平台无关性）。
                    std::uint64_t r = 0;
                    for (int i = 0; i < 8; ++i) {
                        r = (r << 8) | ((v >> (8 * i)) & 0xFFu);
                    }
                    return r;
                }(bits);
                appendField(out, "value", &be, sizeof(be));
                // 来源记录（ValueProvenance 四字段——kind 定值 + 三可选）。
                const core::ValueProvenance& provenance = side->quantity.provenance();
                const char kindByte = static_cast<char>(provenance.kind);
                appendField(out, "provKind", &kindByte, 1);
                appendOptionalId(out, "provSourceObject", provenance.sourceObject);
                appendOptionalId(out, "provSourceVersion", provenance.sourceVersion);
                appendOptionalString(out, "provMethodTag", provenance.methodTag);
            } else if (state == core::FieldState::Invalid) {
                // 非法态：保留原始输入串（NFR-COR-03——不得静默转 0；
                // 原串是内容的一部分，必须进摘要）。
                appendField(out, "invalidRaw", side->quantity.invalidRawInput());
            }
            // 单位（比较值的第二要素——已注册 token）。
            appendField(out, "unit", side->unit.symbol());
        }
    }
    return out;
}

/// 策略语境投影编码（实现口径②——code＋比较三要素＋cause；不含呈现面
/// 字段〔context/localName/runtimeName/recommendedAction〕——那些描述
/// "怎么说"，不描述"依据什么阈值"）。
std::string encodePolicyContext(const core::DiagnosticRecord& record)
{
    // 复用 finding 编码框架，裁剪出策略语境子集：直接以子集字段重建
    // （而非全文编码后裁剪——后者会把缺席字段的旗标也带进摘要边界）。
    std::string out;
    appendField(out, "magic", "IRD-PRJ-POLICY-CONTEXT-V1");
    appendField(out, "code", record.code);
    appendField(out, "cause", record.cause);
    appendFlag(out, record.comparison.has_value());
    if (record.comparison.has_value()) {
        const core::ComparativeValue* sides[2] = {&record.comparison->actual,
                                                  &record.comparison->expected};
        for (const core::ComparativeValue* side : sides) {
            const auto state = side->quantity.state();
            const char stateByte = static_cast<char>(state);
            appendField(out, "state", &stateByte, 1);
            if (state == core::FieldState::Provided) {
                const double value = side->quantity.tryValue().value_or(0.0);
                std::uint64_t bits = 0;
                std::memcpy(&bits, &value, sizeof(bits));
                std::uint64_t be = 0;
                for (int i = 0; i < 8; ++i) {
                    be = (be << 8) | ((bits >> (8 * i)) & 0xFFu);
                }
                appendField(out, "value", &be, sizeof(be));
                const char kindByte
                    = static_cast<char>(side->quantity.provenance().kind);
                appendField(out, "provKind", &kindByte, 1);
            } else if (state == core::FieldState::Invalid) {
                appendField(out, "invalidRaw", side->quantity.invalidRawInput());
            }
            appendField(out, "unit", side->unit.symbol());
        }
    }
    return out;
}

/// SHA-256 单一入口（CR-02：算法唯一路径＝core::ContentDigester——本文件
/// 全部摘要计算收拢于此，无第二哈希实现）。
core::Digest256 sha256Of(const std::string& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 载荷字节（vector 形态）的 SHA-256（commandDigest 用——空负载合法）。
core::Digest256 sha256OfBytes(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

}  // namespace

// =====================================================================
// 摘要计算（自由函数——头文件契约的实现体）
// =====================================================================

std::string findingDigestHex(const core::DiagnosticRecord& record)
{
    std::string hex;
    appendHex64(hex, sha256Of(encodeFindingRecord(record)));
    return hex;
}

core::ContentIdentity policyContentIdentity(const core::DiagnosticRecord& record)
{
    return core::ContentIdentity{sha256Of(encodePolicyContext(record))};
}

std::string commandDigestHex(const std::vector<std::uint8_t>& payloadCanonical)
{
    std::string hex;
    appendHex64(hex, sha256OfBytes(payloadCanonical));
    return hex;
}

// =====================================================================
// 冻结与复核
// =====================================================================

ConfirmationRecord freezeConfirmation(const CommandEnvelope& envelope,
                                      const core::ConfirmableFinding& finding,
                                      const core::ConfirmationCredential& credential,
                                      const core::RevisionId& baseRevision)
{
    // 确认留痕装配（§4.4.4 五字段——四元组＋凭据；全部取规范文本形态，
    // 与 Codec 落盘/回读往返后的形态逐字节一致）。
    ConfirmationRecord record;
    record.findingDigest = findingDigestHex(finding.record);
    record.policyContentId = policyContentIdentity(finding.record).toCanonical();
    record.commandDigest = commandDigestHex(envelope.payloadCanonical);
    record.baseRevisionId = baseRevision.toCanonical();
    // 凭据：主体透传；时刻经单元内唯一时间格式（win32::formatIsoMilli——
    // 毫秒截断语义与其注释一致；该字段是留痕展示面，不参与内容寻址）。
    record.credential.principal = credential.principal;
    record.credential.confirmedAtUtc = win32::formatIsoMilli(credential.confirmedAtUtc);
    return record;
}

BindingVerdict verifyBinding(const ConfirmationRecord& frozen,
                             const ConfirmationActuals& actuals)
{
    // 复核序＝§6.7 四元组序（finding → policy → command → base）；首个
    // 不符成员即定性返回（失配维度可定位——开发诊断的输入）。字符串
    // 比对即字节比对：两侧都是同一计算点的规范文本（冻结侧产出自
    // freezeConfirmation、当前侧产出自同源自由函数——无表示形态分叉）。
    if (frozen.findingDigest != actuals.findingDigest) {
        return BindingVerdict::FindingDigestMismatch;
    }
    if (frozen.policyContentId != actuals.policyContentId) {
        return BindingVerdict::PolicyContentMismatch;
    }
    if (frozen.commandDigest != actuals.commandDigest) {
        return BindingVerdict::CommandDigestMismatch;
    }
    if (frozen.baseRevisionId != actuals.baseRevisionId) {
        return BindingVerdict::BaseRevisionMismatch;
    }
    return BindingVerdict::Bound;
}

// =====================================================================
// 生产探针（真值采集——与冻结同源同函数）
// =====================================================================

ConfirmationActuals ProductionConfirmationProbe::actuals(
    const CommandEnvelope& envelope,
    const core::ConfirmableFinding& finding,
    const core::RevisionId& currentBaseRevision)
{
    // 无状态采集：三成员自命令上下文重算、一成员取调用方重查的当前基线
    // （CommandServiceImpl 在复核时点重查权威元数据 tip 后传入——"当前"
    // 面由调用方供数，本类保持纯函数）。全部规范文本形态——与冻结记录
    // 逐字段直接可比（verifyBinding 的比对面）。
    ConfirmationActuals actuals;
    actuals.findingDigest = findingDigestHex(finding.record);
    actuals.policyContentId = policyContentIdentity(finding.record).toCanonical();
    actuals.commandDigest = commandDigestHex(envelope.payloadCanonical);
    actuals.baseRevisionId = currentBaseRevision.toCanonical();
    return actuals;
}

}  // namespace sdurws::ird::project::confirm
