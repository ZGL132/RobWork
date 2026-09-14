/**
 * @file   Factory.cpp
 * @brief  诊断工厂实现——create 校验链与条目组装、转译注册表与派发查找序、
 *         阶段 A 锚定的转译清单。
 *
 * 设计依据（与 Factory.hpp 头注同源，此处只登记实现口径）：
 *   - §9.2 create 检查序（固定、首错即停并指明字段——顺序登记见 create 函数
 *     内逐段注释；"码已注册"先于"未废弃"，来源 token 合法性先于语义校验，
 *     与 DIAG-T03 注册期验证链"结构校验先、权威校验后、键唯一最后"同风格）；
 *   - §4.5 占位一致性：params 键集与 paramSchema 参数名集必须完全一致
 *     （多键/少键/错名皆拒绝——"按模式填充"的机器语义）；
 *   - §8.10 上下文必填规则（阶段 A 机械执行面）：execution 域码
 *     （ownerUnit=="execution"）⇒ context.task 必填且五字段合法——评估/命令
 *     路径的逐码清单未由卡给规范性枚举，不预判（单元卡 v0.5 登记）；
 *   - §8.1 规则 2/§9.2 错误行：未登记类型→DIAG-REGISTRY-UNKNOWN-CODE 兜底
 *     条目，不抛；消息截断 512 字节（与 §7.6 异常消息摘要口径一致）；
 *   - §8.3 阶段 A 转译清单：std::exception → RT-ROBWORK-ERROR（目标码为
 *     用户级 Error——rootless 转译按 subject 边界 SubjectMissing 拒绝，
 *     调用方须提供根因条目）。
 *
 * 线程安全：entryId 原子分配；转译规则表 seal 后只读（std::map 并发读安全）；
 * registry 按"运行期只读"消费（seal 后无写入——StableCodeRegistry 契约）。
 */

#include <sdurws/ird/diagnostics/Factory.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "EntryDetail.hpp"    // 去重键派生（私有单点——工厂为计算点 §4.2）
#include "ParamSchema.hpp"    // paramSchema 解析单点（占位一致性校验用）

namespace sdurws::ird::diagnostics {
namespace {

/// 兜底条目的异常消息截断上限（§7.6 异常消息摘要口径 512 字节——"安全摘要"
/// 的脱敏级整备随 DIAG-T08 接线，本任务先做确定性截断）。
constexpr std::size_t kMaxTranslateMessageBytes = 512;

/// 截断超限消息并附标注（§7.2"超出截断并标注"同风格——不静默丢弃尾部的
/// 事实告知阅读者）。
std::string truncateMessage(const std::string& message)
{
    if (message.size() <= kMaxTranslateMessageBytes) {
        return message;
    }
    return message.substr(0, kMaxTranslateMessageBytes) + "…（已截断，≤"
        + std::to_string(kMaxTranslateMessageBytes) + " 字节）";
}

}  // namespace

// =====================================================================
// DiagnosticsFactory——构造与 create 校验链
// =====================================================================

DiagnosticsFactory::DiagnosticsFactory(const IDiagnosticRegistry& registry,
                                       const IClock& clock)
    : m_registry(&registry)
    , m_clock(&clock)
{
}

void DiagnosticsFactory::seal() noexcept
{
    m_sealed = true;
}

DiagnosticEntry DiagnosticsFactory::create(const core::DiagnosticRecord& record,
                                           const DiagContext& context)
{
    // ---- 校验链（§9.2 前置；检查序固定——见 Factory.hpp 类注释，首错即停
    // 并在 detail 中指明字段/码值）----

    // ①码已注册（§4.5"工厂以未注册码构造 → code-unknown 拒绝"——诊断码不能
    // 经异常文本临时生成的机制承载：未注册字符串在此全部被拦，DT-REG-3/4）。
    const CodeDescriptor* descriptor = m_registry->find(record.code);
    if (descriptor == nullptr) {
        throw DiagnosticsError(DiagnosticsErrorCode::CodeUnknown,
                               "码未注册（" + record.code + "）——工厂只接受已注册码");
    }

    // ②码未废弃（§4.5.1"工厂拒绝 deprecated 码 code-deprecated"——tombstone
    // 只保留只读解析，不承载新实例构造；新语义＝新码）。
    if (descriptor->deprecated) {
        throw DiagnosticsError(DiagnosticsErrorCode::CodeDeprecated,
                               "码已废弃（" + record.code + "）——废弃码只读映射不改写历史");
    }

    // ③context 来源 token 合法（§4.2 字段边界：sourceUnit ≤32、sourceInterface
    // ≤64 且非空——调用方契约违约 fail-fast）。
    if (context.sourceUnit.empty() || context.sourceUnit.size() > 32) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段 context.sourceUnit 须为非空且 ≤32 字符 token");
    }
    if (context.sourceInterface.empty() || context.sourceInterface.size() > 64) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段 context.sourceInterface 须为非空且 ≤64 字符 token");
    }

    // ④比较型三要素（core C-1 的注册表侧强化——码表 requiresComparison=true
    // 的实例必须携带 comparison；ERR-01/UX-03 比较型校验含实际值/期望值/单位）。
    if (descriptor->requiresComparison && !record.comparison.has_value()) {
        throw DiagnosticsError(DiagnosticsErrorCode::ComparisonMissing,
                               "比较型码缺三要素（" + record.code + "）——core C-1 强化");
    }

    // ⑤subject 边界（core §4.8"稳定诊断项必须携带合法 subjectObjectId；瞬时
    // 开发诊断可空"的边界强制——完整性强制归 diagnostics 边界，DT-DIAG-2）：
    // 用户级码（severity != Dev）缺 subject → 拒绝；任一级携带非法（全零）
    // subject 同样拒绝（"合法 subjectObjectId"——非法值不是绑定）。
    const bool userLevel = descriptor->severity != DiagnosticSeverity::Dev;
    if (record.subject.has_value() && !record.subject->isValid()) {
        throw DiagnosticsError(DiagnosticsErrorCode::SubjectMissing,
                               "subject 为非法 ObjectId（全零保留值）——码 " + record.code);
    }
    if (userLevel && !record.subject.has_value()) {
        throw DiagnosticsError(DiagnosticsErrorCode::SubjectMissing,
                               "用户级码缺 subject（" + record.code
                                   + "）——ERR-01 稳定诊断项绑定对象（core §4.8 交接）");
    }

    // ⑥paramSchema 占位一致（§4.5"实例的 context/cause/comparison 须按模式
    // 填充（工厂校验占位一致性）"——DiagContext.params 键集与 schema 参数名
    // 集完全一致：多键/少键/错名皆拒绝）。
    std::vector<std::string> schemaNames;
    std::string schemaError;
    if (!detail::tryParseParamSchema(descriptor->paramSchema, &schemaNames, &schemaError)) {
        // 注册期已验证过 schema 文法——此处失败说明注册表被绕过装配，防御性
        // 拒绝（不静默放行）。
        throw DiagnosticsError(DiagnosticsErrorCode::ParamSchemaMismatch,
                               "码 " + record.code + " 的 paramSchema 解析失败: " + schemaError);
    }
    if (schemaNames.size() != context.params.size()) {
        throw DiagnosticsError(DiagnosticsErrorCode::ParamSchemaMismatch,
                               "参数占位数量与模式不符（码 " + record.code + "，模式 "
                                   + std::to_string(schemaNames.size()) + " 项，实例 "
                                   + std::to_string(context.params.size()) + " 项）");
    }
    for (const std::string& name : schemaNames) {
        if (context.params.find(name) == context.params.end()) {
            throw DiagnosticsError(DiagnosticsErrorCode::ParamSchemaMismatch,
                                   "参数占位缺失 \"" + name + "\"（码 " + record.code + "）");
        }
    }

    // ⑦来源码必填上下文（§8.10 锚定规则：execution 全路径产码必须携带五元组
    // ——ownerUnit=="execution" 的码缺 context.task 即 ContextMissing；五元组
    // 有效性校验（isValid）一并执行——残缺五元组等于没有锚定，TASK-03）。
    if (descriptor->ownerUnit == "execution"
        && !(context.task.has_value() && context.task->isValid())) {
        throw DiagnosticsError(DiagnosticsErrorCode::ContextMissing,
                               "execution 域码缺合法 task 五元组（" + record.code
                                   + "）——§8.10 产码必带五元组");
    }

    // ---- 组装（§9.2 后置：不可变条目，分类/严重自码表，身份字段已赋）----

    DiagnosticEntry entry;
    entry.entryId = m_nextEntryId.fetch_add(1, std::memory_order_relaxed) + 1;  // 单调 ≥1
    entry.record = record;                       // P-DIAG-1：core 契约原样内嵌（不扩充）
    entry.category = descriptor->category;       // 分类来自码表——调用方不可覆盖（NFR-MNT-03）
    entry.severity = descriptor->severity;       // 同上；不随实例变化（§4.3 传播规则）
    entry.context = context;
    // 码表版本标注（§4.5 registryVersion"进入诊断实例的 contractVersions 标注，
    // 供报告侧检测文案/语义演进"——追加在调用方标注之后，不覆盖）。
    entry.context.contractVersions.emplace_back("diag-code", descriptor->registryVersion);
    entry.emittedAtUtc = m_clock->nowUtc();      // 注入时钟——测试可替换（确定性）
    entry.dedupKey = detail::deriveDedupKey(record, context);   // §6.4（口径见 detail 头注）
    entry.orderKey = std::make_tuple(
        static_cast<std::uint64_t>(entry.emittedAtUtc.time_since_epoch().count()),
        entry.entryId,
        entry.record.code);                      // §6.4 稳定排序键 {时间戳计数, entryId, code}
    // threadTag/workerId/redactedContextSnapshot 阶段 A 置空：采集点随各单元
    // 产码路径登记；worker 标注随回传路径（§7.5，DIAG-T07）；脱敏快照经
    // IRedactionService 产出（§4.2——DIAG-T08 接线）。
    return entry;
}

// =====================================================================
// DiagnosticsFactory——转译注册表与派发
// =====================================================================

void DiagnosticsFactory::registerTranslation(std::type_index errType,
                                             std::string_view targetCode)
{
    // 运行期调用＝装配期契约违约（§9.2"转换规则登记（装配期）"；"运行期"
    // 判据＝seal() 装配原语——同 StableCodeRegistry 实现口径）。
    if (m_sealed) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "运行期 registerTranslation——登记仅限装配期");
    }

    // 目标码必须已注册（未注册码的转译规则＝装配错误——转译产物经 create
    // 产出，码面必须先在案；CodeUnknown 拒绝）。
    if (m_registry->find(targetCode) == nullptr) {
        throw DiagnosticsError(DiagnosticsErrorCode::CodeUnknown,
                               "转译目标码未注册（" + std::string(targetCode) + "）");
    }

    // 同错误类型重复登记拒绝（§9.2"同错误类型重复登记→DuplicateCode"——
    // 不覆盖不静默；一型一码的注册面语义）。
    if (m_rules.find(errType) != m_rules.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::DuplicateCode,
                               "错误类型重复登记（" + std::string(errType.name())
                                   + "）——一型一码");
    }

    m_rules.emplace(errType,
                    std::make_pair(std::string(errType.name()), std::string(targetCode)));
}

std::vector<std::pair<std::string, std::string>>
DiagnosticsFactory::registeredTranslations() const
{
    // 审计清单（按类型名升序＝map 迭代序——确定性观测面，"映射就位"的证据）。
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(m_rules.size());
    for (const auto& [type, rule] : m_rules) {
        result.emplace_back(rule);
    }
    return result;
}

DiagnosticEntry DiagnosticsFactory::translateDispatch(const TranslationInput& input,
                                                      const DiagContext& context,
                                                      const DiagnosticEntry* root)
{
    // ---- 查找序（Factory.hpp 类注释：精确 → runtime_error → exception →
    // 兜底；全静态类型判据——禁字符串匹配的机制面）----
    std::string targetCode;
    bool matched = false;
    if (const auto it = m_rules.find(input.type); it != m_rules.end()) {
        targetCode = it->second.second;          // ①精确命中
        matched = true;
    } else if (input.isRuntimeError) {
        if (const auto base = m_rules.find(typeid(std::runtime_error));
            base != m_rules.end()) {
            targetCode = base->second.second;    // ②runtime_error 体系兜底
            matched = true;
        }
    }
    if (!matched && input.isException) {
        if (const auto base = m_rules.find(typeid(std::exception)); base != m_rules.end()) {
            targetCode = base->second.second;    // ③std::exception 体系兜底（§8.3）
            matched = true;
        }
    }

    if (!matched) {
        // ④未登记类型→DIAG-REGISTRY-UNKNOWN-CODE 兜底条目（§8.1 规则 2/§9.2
        // "不抛"）：保留原始来源（类型名）与安全摘要（消息截断）；该码为
        // 内置 Dev 级——subject 边界天然满足，本路径恒不抛。
        core::DiagnosticRecord fallback = core::DiagnosticRecord::make(
            "DIAG-REGISTRY-UNKNOWN-CODE",
            /*subject=*/{}, /*localName=*/{}, /*runtimeName=*/{},
            /*context=*/"未登记错误类型转译（类型 " + input.typeName + "）——保留原始来源",
            /*cause=*/input.message.empty() ? std::string("（消息不可得——非 std::exception 体系）")
                                            : truncateMessage(input.message),
            /*recommendedAction=*/"核对 ErrorCodeTranslator 装配清单（类型映射禁字符串匹配——§8.1）");
        DiagnosticEntry entry = create(fallback, context);
        if (root != nullptr) {
            entry.causedBy = root->entryId;      // 兜底条目同样不丢根因（§6.3）
        }
        return entry;
    }

    // ---- 已登记路径：经 create 全链产出（分类/严重/校验与直创条目同规则
    // ——§8.1 规则 3"同一错误只有一个权威分类"）----
    // 用户级目标码的 subject 来源：继承根因条目（root 非空——作用对象域
    // 一致，§6.3"转换器不丢作用对象"）；root 空时由 create 按 subject 边界
    // 以 SubjectMissing 拒绝（调用方须提供根因条目或换用 Dev 级目标码）。
    std::optional<core::ObjectId> subject;
    std::optional<std::string> localName;
    std::optional<std::string> runtimeName;
    if (root != nullptr) {
        subject = root->record.subject;
        localName = root->record.localName;
        runtimeName = root->record.runtimeName;
    }
    core::DiagnosticRecord mapped = core::DiagnosticRecord::make(
        targetCode, subject, localName, runtimeName,
        /*context=*/"跨单元错误转译（类型 " + input.typeName + "，目标码 " + targetCode
            + "）——原始来源已保留",
        /*cause=*/input.message.empty() ? std::string("（消息不可得——非 std::exception 体系）")
                                        : truncateMessage(input.message),
        /*recommendedAction=*/"按目标码处理动作族处置；开发级日志保留异常链（§8.1 规则 1）");
    DiagnosticEntry entry = create(mapped, context);
    if (root != nullptr) {
        entry.causedBy = root->entryId;          // §6.3 不丢根因：causedBy 链接
    }
    return entry;
}

// =====================================================================
// 阶段 A 转译清单（§8.3 锚定——RT 族映射）
// =====================================================================

void registerStageATranslations(IDiagnosticFactory& translator)
{
    // §8.3 原文："本文转换表登记 std::exception 体系→RT-ROBWORK-ERROR 的
    // 兜底规则（§8.1 规则 2 的合法应用：类型匹配而非文本匹配）"。重复登记
    // 由 registerTranslation 的 DuplicateCode 拒绝承载（清单与既有装配冲突
    // 即抛，不静默跳过——Factory.hpp 注释）。
    translator.registerTranslation(typeid(std::exception), "RT-ROBWORK-ERROR");
}

}  // namespace sdurws::ird::diagnostics
