/**
 * @file   DiagCodes.cpp
 * @brief  稳定码注册表实现——词表 token、注册期验证、manifest 摘要与内置
 *         码表全量收编（§4.6 87 码）。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.3/§4.5/§4.5.1/§4.6/§9.1（见头注释）；
 *   - 任务契约 tasks/foundation/DIAG-T03.json（≙WP-09-T03）；
 *   - 实现先例：evidence/src/Evaluator.cpp（RegistrationManifest 的
 *     canonical 编码摘要——"magic＋codec 版本＋逐条目规范编码"同模式）。
 *
 * ◆ §4.6 压缩串的机械展开规则（登记——单元卡 §4.6 同步注记，DTB §5.4）：
 *   §4.6 各行的分类/严重为压缩记法（"×n"计数、"/"分段、括注与省略号），
 *   本实现按以下规则逐码展开，全部消歧点逐码注释可复核：
 *   R1 行内分类/严重串按码序逐位对齐（"×n"＝连续 n 码同值）；
 *   R2 段长合计与码数不符（PRJ 行 9 对 10、POLICY 行 22 对 23、EX 行 14
 *      对 18、EVI 严重行 6 对 7）＝压缩丢位：丢位码以 §4.3"典型来源码"
 *      列点名锚点／§4.4 分类默认严重矩阵／收编卡语义登记为交叉依据补齐；
 *   R3 严重列省略号（"Error…"）＝延续首值，行内显式标注（如"(Warning)"、
 *      "(Info)"、备注"四个开发级标 Dev"）为逐码覆盖值；分类默认严重
 *      （§4.4 矩阵）仅用于无任何显式登记的码；
 *   R4 分类串中的非词表 token（POLICY/RPT 行尾"信息"、DIAG 行"可确认类
 *      辅助"）落位：词表内唯一承载"非错误的有效结论"的
 *      infeasibility-proof（§4.3 Info 行"有效结论/告知：默认值已填入"举例
 *      ＋§4.4"结论呈现（RPT-05 评审记录）"锚点）与 confirmable（SA-15
 *      确认流自省码）——不新增词表值（P-DIAG-3"不私裁"约束）。
 *   上述展开只决定码元数据的 category/severity 登记值；**码值集合本身
 *   （87 码、拼写、前缀）与 §4.6 逐字一致，无任何重排**（dtb WP-09-T03
 *   约束）。若所有者对个别消歧点另有裁决，返工面＝对应表行两列值＋
 *   registryVersion 递增，码值不动。
 *
 * ◆ manifest 摘要 canonical 编码（CodeTableManifest.digest——非往返身份
 *   投影，唯一消费方式＝摘要比对）：
 *     "IRDDCM1"（7 字节 magic＋codec 版本位）
 *     之后逐条目（entries 已按 code 字典序）：
 *       逐字符串字段（code/ownerUnit/categoryToken/severityToken/titleKey/
 *         detailKey/paramSchema/retryKindToken/supersededBy?）＝
 *         u32le(长度)＋原始字节；
 *       u8 × 7＝confirmable/requiresComparison/userVisible/reportable/
 *         historical/deprecated/hasSupersededBy（0/1）；
 *       u32le＝registryVersion。
 *   确定性：全部字段为编译期/注册期定值，编码无环境依赖——同表同摘要
 *   （NFR-COR-02；DT-REG-1"两次计算相等"观测点）。
 *
 * 线程安全：见头文件各实体注释（注册期单线程；seal 后运行期只读无锁）。
 */

#include <sdurws/ird/diagnostics/DiagCodes.hpp>

#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace sdurws::ird::diagnostics {
namespace {

// ---------------------------------------------------------------------
// 内部辅助：字符串/整型喂入摘要器（canonical 编码原语——见文件头编码表）。
// ---------------------------------------------------------------------

/// 追加 u32 小端 4 字节（编码表"u32le"项；小端为登记约定——跨平台一致）。
void feedU32le(core::ContentDigester& digester, std::uint32_t value)
{
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(value & 0xFFu),
        static_cast<std::uint8_t>((value >> 8) & 0xFFu),
        static_cast<std::uint8_t>((value >> 16) & 0xFFu),
        static_cast<std::uint8_t>((value >> 24) & 0xFFu)};
    digester.update(bytes.data(), bytes.size());
}

/// 追加长度前缀字符串（编码表"u32le(len)＋原始字节"项；nullptr 安全）。
void feedString(core::ContentDigester& digester, std::string_view text)
{
    feedU32le(digester, static_cast<std::uint32_t>(text.size()));
    digester.update(text.data(), text.size());
}

/// 追加单字节布尔（编码表"u8"项；仅取 0/1——非 0 布尔一律归一为 1）。
void feedBool(core::ContentDigester& digester, bool value)
{
    const std::uint8_t byte = value ? 1u : 0u;
    digester.update(&byte, 1);
}

// ---------------------------------------------------------------------
// 内部辅助：§4.5 前缀-所有权表（"首段＝单元短前缀……前缀即所有权声明"）。
// ---------------------------------------------------------------------

struct PrefixOwner {
    std::string_view prefix;    ///< 码首段（§4.5 命名空间约定）
    std::string_view ownerUnit; ///< 所有者单元 token
};

/// §4.5 原文 17 前缀（9 平台＋8 业务域；POLICY 按原文收编——policy 卡建议
/// 用 POLICY-*，前缀归一为 POLICY；业务域码阶段 B 起随域卡注册，前缀表
/// 阶段 A 即全量登记——注册机制在案，不预建任何域码条目）。
constexpr std::array<PrefixOwner, 17> kPrefixOwners{{
    {"PRJ", "project"},       {"RT", "runtime"},        {"POLICY", "policy"},
    {"EVI", "evidence"},      {"EX", "execution"},      {"IO", "io"},
    {"UI", "ui"},             {"RPT", "reporting"},     {"DIAG", "diagnostics"},
    {"MDL", "modeling"},      {"REQ", "requirements"},  {"KIN", "kinematics"},
    {"TRJ", "trajectory"},    {"DYN", "dynamics"},      {"SEL", "selection"},
    {"OPT", "optimization"},  {"WF", "workflow"},
}};

/// 取码首段（句法合法前提下的第一段；找不到 '-' 即全串）。
std::string_view firstSegment(std::string_view code)
{
    const auto dash = code.find('-');
    return dash == std::string_view::npos ? code : code.substr(0, dash);
}

/// 查前缀表：命中返回期望 ownerUnit；未命中返回 nullptr（跨前缀/未知前缀
/// 注册拒绝——CodeUnknown 的判定面，§9.1"CodeUnknown 前缀冲突"）。
const std::string_view* expectedOwnerFor(std::string_view code)
{
    const std::string_view prefix = firstSegment(code);
    for (const auto& entry : kPrefixOwners) {
        if (entry.prefix == prefix) {
            return &entry.ownerUnit;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------
// 内部辅助：paramSchema 受限校验（§4.5"参数模式：受限 JSON 形 schema
// token 列表"——不引第三方 JSON 库（新增依赖须先登记，dtb 约束）；
// 手写线性扫描，确定性且零依赖）。
// ---------------------------------------------------------------------

/// 参数名词形：^[a-z0-9]+(-[a-z0-9]+)*$（§4.5 示例 ["pid","host"]——小写
/// kebab，与诊断码同族词形的小写变体）。
bool isValidParamName(std::string_view name)
{
    if (name.empty() || !std::isalnum(static_cast<unsigned char>(name.front()))) {
        return false;
    }
    bool prevDash = false;
    for (const char ch : name) {
        if (ch == '-') {
            if (prevDash) {
                return false;   // 连续连字符非法
            }
            prevDash = true;
            continue;
        }
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            return false;       // 仅允许小写字母/数字（isalnum 对大写也为真——补查）
        }
        if (std::isupper(static_cast<unsigned char>(ch))) {
            return false;
        }
        prevDash = false;
    }
    return !prevDash;           // 尾连字符非法
}

/**
 * @brief 校验 paramSchema 为受限 JSON 数组形。
 *
 * 接受且仅接受：`[]`（无参数——合法声明）或 `["name","name",...]`
 * （空白允许于括号/逗号外侧；字符串内不允许转义/空白——参数名词形本身
 * 不含这些字符，存在即非法）。返回失败原因文本（detail 用），成功返回空。
 */
std::string validateParamSchema(const std::string& schema)
{
    if (schema.empty()) {
        return "paramSchema 为空（必填字段——无参数也须显式声明 \"[]\"）";
    }
    std::size_t i = 0;
    const auto skipWs = [&i, &schema] {
        while (i < schema.size() && (schema[i] == ' ' || schema[i] == '\t')) {
            ++i;
        }
    };
    skipWs();
    if (i >= schema.size() || schema[i] != '[') {
        return "paramSchema 须以 '[' 起始（受限 JSON 数组形）";
    }
    ++i;
    skipWs();
    std::vector<std::string_view> names;
    if (i < schema.size() && schema[i] == ']') {
        ++i;   // 空数组分支
    } else {
        while (true) {
            skipWs();
            if (i >= schema.size() || schema[i] != '"') {
                return "paramSchema 参数项须为带引号的参数名";
            }
            ++i;
            const std::size_t start = i;
            while (i < schema.size() && schema[i] != '"') {
                ++i;
            }
            if (i >= schema.size()) {
                return "paramSchema 参数名字符串未闭合";
            }
            const std::string_view name{schema.data() + start, i - start};
            if (!isValidParamName(name)) {
                return "paramSchema 参数名词形非法（须 ^[a-z0-9]+(-[a-z0-9]+)*$）: "
                       + std::string{name};
            }
            for (const auto& prior : names) {
                if (prior == name) {
                    return "paramSchema 参数名重复: " + std::string{name};
                }
            }
            names.push_back(name);
            ++i;   // 跳过闭引号
            skipWs();
            if (i < schema.size() && schema[i] == ',') {
                ++i;
                continue;   // 下一参数项
            }
            if (i < schema.size() && schema[i] == ']') {
                ++i;
                break;      // 数组结束
            }
            return "paramSchema 参数项后须为 ',' 或 ']'";
        }
    }
    skipWs();
    if (i != schema.size()) {
        return "paramSchema 数组结束后存在多余字符";
    }
    return {};   // 合法
}

// ---------------------------------------------------------------------
// 内部辅助：分类→可重试性机械映射（§4.4 动作族的机器锚点——规则登记于
// RetryKind 注释与单元卡 §4.6 展开注记；内置表逐码按此映射）。
// ---------------------------------------------------------------------

RetryKind categoryDefaultRetry(DiagnosticCategory category)
{
    switch (category) {
    case DiagnosticCategory::InputInvalid:          return RetryKind::UserRetry;  // fix-input
    case DiagnosticCategory::FormatOrVersion:       return RetryKind::UserRetry;  // open-upgrade-guide / choose-compatible
    case DiagnosticCategory::PermissionOrLock:      return RetryKind::UserRetry;  // retry-readonly / contact-holder
    case DiagnosticCategory::ResourceMissing:       return RetryKind::UserRetry;  // relink / reimport
    case DiagnosticCategory::PolicyDenied:          return RetryKind::UserRetry;  // adjust-policy
    case DiagnosticCategory::Confirmable:           return RetryKind::UserRetry;  // confirm-or-fix
    case DiagnosticCategory::ExecutionFailed:       return RetryKind::UserRetry;  // retry-task / inspect-log
    case DiagnosticCategory::Canceled:              return RetryKind::Never;      // none（无动作）
    case DiagnosticCategory::Interrupted:           return RetryKind::UserRetry;  // rerun-interrupted
    case DiagnosticCategory::Timeout:               return RetryKind::UserRetry;  // retry-task / inspect-worker
    case DiagnosticCategory::DataInsufficient:      return RetryKind::UserRetry;  // supply-evidence
    case DiagnosticCategory::EvidenceMissing:       return RetryKind::UserRetry;  // supply-evidence
    case DiagnosticCategory::InfeasibilityProof:    return RetryKind::Never;      // review-proof（结论呈现）
    case DiagnosticCategory::Internal:              return RetryKind::Never;      // report-bug
    case DiagnosticCategory::SecurityOrRedaction:   return RetryKind::Never;      // inspect-resource / report-bug
    }
    return RetryKind::Never;   // 全枚举不可达；保守取 Never
}

// ---------------------------------------------------------------------
// 内部辅助：内置表行规格与文案键派生（§4.5 命名约定 diag.<code-lower>.*）。
// ---------------------------------------------------------------------

/// 内置表行规格（§4.6 逐码登记值；ownerUnit/文案键/flags/retryable 经
/// 机械规则派生——见 makeBuiltin）。
struct BuiltinSpec {
    const char* code;             ///< 码值原文（§4.6 收编清单——不重排）
    DiagnosticCategory category;  ///< 分类登记值（逐码注释见下表）
    DiagnosticSeverity severity;  ///< 严重登记值（逐码注释见下表）
    const char* paramSchema;      ///< 参数模式（默认 "[]"；仅 §9.2 示例明示的
                                  ///  PRJ-LOCK-HELD 登记 ["pid","host"]）
};

/// 文案键派生（P-DIAG-9 键体系冻结约定："diag."＋码小写＋".title"/".detail"；
/// 键值分离——值归 ui/文案资源，本设施只登记键）。
std::string derivedTextKey(std::string_view code, std::string_view suffix)
{
    std::string key{"diag."};
    key.reserve(key.size() + code.size() + 1 + suffix.size());
    for (const char ch : code) {
        // 码句法保证仅 A-Z/0-9/'-'——tolower 无 locale 依赖面
        key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    key.push_back('.');
    key.append(suffix);
    return key;
}

/// 由表行构造完整描述符（flags/registryVersion/retryable/文案键按 §4.5
/// 机械规则派生——单点派生保证 87 码一致性；Dev 码 flags 强制 false）。
CodeDescriptor makeBuiltin(const BuiltinSpec& spec)
{
    CodeDescriptor d;
    d.code = spec.code;
    // 前缀→ownerUnit：内置表全部为平台前缀（§4.6 备注"IO-*/UI-* 码表随
    // io/ui 卡注册"——阶段 A 不含；查表必命中，失配即装配表自身缺陷）。
    const std::string_view* owner = expectedOwnerFor(spec.code);
    d.ownerUnit = owner != nullptr ? std::string{*owner} : std::string{};
    d.category = spec.category;
    d.severity = spec.severity;
    d.titleKey = derivedTextKey(spec.code, "title");
    d.detailKey = derivedTextKey(spec.code, "detail");
    d.paramSchema = spec.paramSchema;
    d.confirmable = false;          // 阶段 A 内置表无可确认域码（§4.6 表尾行：
    d.requiresComparison = false;   // "阶段 B 起注册……不预建"——dtb 原则）
    d.retryable = categoryDefaultRetry(spec.category);
    const bool dev = spec.severity == DiagnosticSeverity::Dev;
    d.userVisible = !dev;           // §4.5"Dev 码=false"（注册期验证同规则）
    d.reportable = !dev;
    d.historical = !dev;
    d.registryVersion = 1;          // 首次登记
    d.deprecated = false;           // tombstone 只经 deprecate()／清单显式修订产生
    return d;
}

/// §4.6 内置表数据（行序＝§4.6 表行序，行内＝收编清单原文序；逐码注释为
/// 分类/严重的登记依据——展开规则 R1~R4 见文件头）。
const std::array<BuiltinSpec, 87>& builtinSpecs()
{
    static const std::array<BuiltinSpec, 87> kSpecs{{
        // ---- PRJ（project §5.0 收编，10 项；分类串"权限/内部/权限/权限/
        //      内部/版本/输入/执行/权限"9 项对 10 码——R2 补第 7 位）----
        {"PRJ-LOCK-HELD", DiagnosticCategory::PermissionOrLock, DiagnosticSeverity::Warning,
         "[\"pid\",\"host\"]"},   // ①权限/Warning；§4.3 权限类典型码；参数＝§9.2
                                  // 调用示例明示（pid＋host）
        {"PRJ-STORE-CORRUPT", DiagnosticCategory::Internal, DiagnosticSeverity::Error,
         "[]"},                   // ②内部；严重 Error＝§4.4"个别如 store-corrupt
                                  // 为 Error"明示；备注"定位文件"
        {"PRJ-RECOVERY-IGNORED-UNCOMMITTED", DiagnosticCategory::PermissionOrLock,
         DiagnosticSeverity::Info, "[]"},  // ③串逐位"权限"/Info；随 RecoveryReport（PM-08/15）
        {"PRJ-RECOVERY-ORPHAN-DRAFT", DiagnosticCategory::PermissionOrLock,
         DiagnosticSeverity::Info, "[]"},  // ④串逐位"权限"/Info；孤儿草稿报告（可恢复旧版）
        {"PRJ-RECOVERY-DANGLING-OBJECTS", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},   // ⑤内部/Dev
        {"PRJ-SCHEMA-FUTURE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"}, // ⑥版本；§4.3 格式/版本类典型码
        {"PRJ-FORMAT-LEGACY", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"}, // ⑦串缺位补正（R2）：§4.3 格式/版本类
                                           // 典型码点名——分类串压缩时漏一"版本"
        {"PRJ-STALE-REVISION-REJECTED", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"}, // ⑧输入（过期修订拒绝——project §5.0 同义）
        {"PRJ-ARCHIVE-CONFLICT", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"}, // ⑨执行（归档冲突）
        {"PRJ-WRITE-AUTHORITY-LOST", DiagnosticCategory::PermissionOrLock,
         DiagnosticSeverity::Error, "[]"}, // ⑩权限；§4.3 权限类典型码点名

        // ---- RT（runtime §10.11 收编，14 项；分类串 14 项逐位对齐（R1）；
        //      严重"Error…"＝延续 Error，显式标注两处）----
        {"RT-INPUT-INVALID", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // ①输入；§4.3 输入非法典型码
        {"RT-STRUCTURE-INVALID", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // ②输入
        {"RT-UNIT-MISMATCH", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // ③输入
        {"RT-RESOURCE-MISSING", DiagnosticCategory::ResourceMissing,
         DiagnosticSeverity::Error, "[]"},        // ④资源；§4.3 资源缺失典型码
        {"RT-RESOURCE-CHANGED", DiagnosticCategory::ResourceMissing,
         DiagnosticSeverity::Error, "[]"},        // ⑤资源
        {"RT-RESOURCE-BUDGET", DiagnosticCategory::SecurityOrRedaction,
         DiagnosticSeverity::Error, "[]"},        // ⑥串"安全"（R1）；预算超限归安全类
                                                  // （§4.3 安全类语义"预算超限"）——§4.3
                                                  // 资源缺失行亦点名 BUDGET，矛盾按登记处
                                                  // （§4.6 逐位）优先，登记注记
        {"RT-WC-COMPILE-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // ⑦执行；§4.3 执行失败典型码
        {"RT-DWC-COMPILE-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // ⑧执行
        {"RT-NAME-CONFLICT", DiagnosticCategory::Internal,
         DiagnosticSeverity::Error, "[]"},        // ⑨内部；严重 Error＝串"Error…"
                                                  // 延续显式登记（R3）优先于 internal
                                                  // 默认 Dev——矛盾登记注记
        {"RT-BASE-WORLD-INCONSISTENT", DiagnosticCategory::Internal,
         DiagnosticSeverity::Error, "[]"},        // ⑩内部/同上（串 Error 延续）
        {"RT-CAPABILITY-MISSING", DiagnosticCategory::ResourceMissing,
         DiagnosticSeverity::Warning, "[]"},      // ⑪资源(Warning)——串显式标注
        {"RT-ROBWORK-ERROR", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // ⑫执行
        {"RT-CACHE-INCOMPATIBLE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // ⑬版本；§4.3 格式/版本典型码
        {"RT-CANCELLED", DiagnosticCategory::Canceled,
         DiagnosticSeverity::Info, "[]"},         // ⑭取消(Info)——UX-03 正常取消非错误

        // ---- POLICY（policy §9.6 收编，23 项；串"版本×3/输入×8/版本×2/
        //      数据不足/策略拒绝×4（…归执行失败）/资源/输入/策略拒绝/信息"
        //      段合计 22 对 23——R2 补第 12 位）----
        {"POLICY-SCHEMA-UNKNOWN-FIELD", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×3①（schema 层拒绝发布）
        {"POLICY-SCHEMA-VERSION-FUTURE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×3②（PM-06 升级指引）
        {"POLICY-SCHEMA-VERSION-UNKNOWN", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×3③
        {"POLICY-THRESHOLD-NON-FINITE", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8①；§4.3 输入非法典型码
        {"POLICY-THRESHOLD-NON-POSITIVE", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8②
        {"POLICY-THRESHOLD-OUT-OF-RANGE", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8③
        {"POLICY-UNIT-MISMATCH", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8④
        {"POLICY-RULE-DUPLICATE", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8⑤
        {"POLICY-RULE-CONFLICT", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8⑥
        {"POLICY-RULE-CYCLE", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8⑦
        {"POLICY-SCOPE-OBJECT-MISSING", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // 输入×8⑧（作用对象缺失＝输入闭包非法）
        {"POLICY-APPLICABILITY-INVALID", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // ⑫串缺位补正（R2）：policy §5 校验表
                                                  // "适用范围无效→拒绝发布"与输入/schema
                                                  // 同段——语义归输入非法
        {"POLICY-VERSION-INCOMPATIBLE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×2①（语义：版本不兼容）
        {"POLICY-CONTENT-IDENTITY-MISMATCH", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×2②（CON-04 契约/身份不兼容）
        {"POLICY-CLL-DETECTOR-UNAVAILABLE", DiagnosticCategory::DataInsufficient,
         DiagnosticSeverity::Warning, "[]"},      // "数据不足"段；§4.3 点名（KIN-05 口径）
        {"POLICY-CLL-SCENE-INVALID", DiagnosticCategory::PolicyDenied,
         DiagnosticSeverity::Warning, "[]"},      // 策略拒绝×4①（§4.4 默认 Warning）
        {"POLICY-CLL-NAME-UNRESOLVED", DiagnosticCategory::PolicyDenied,
         DiagnosticSeverity::Warning, "[]"},      // 策略拒绝×4②
        {"POLICY-CLL-CONTEXT-EXPIRED", DiagnosticCategory::PolicyDenied,
         DiagnosticSeverity::Warning, "[]"},      // 策略拒绝×4③
        {"POLICY-CLL-EVALUATION-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 串括注"CLL-EVALUATION-FAILED 归执行失败"
        {"POLICY-CLL-GEOMETRY-MISSING", DiagnosticCategory::ResourceMissing,
         DiagnosticSeverity::Error, "[]"},        // "资源"段；§4.3 资源缺失行点名
        {"POLICY-JNT-TABLE-INVALID", DiagnosticCategory::PolicyDenied,
         DiagnosticSeverity::Warning, "[]"},      // 策略拒绝×4④（跨段——§4.3 策略拒绝
                                                  // 行点名；段计数吻合）
        {"POLICY-ENGINEERING-RANGE-INVALID", DiagnosticCategory::InputInvalid,
         DiagnosticSeverity::Error, "[]"},        // "输入"段（阈值越域同族——policy §5
                                                  // 校验表同段）
        {"POLICY-INFO-DEFAULT-APPLIED", DiagnosticCategory::InfeasibilityProof,
         DiagnosticSeverity::Info, "[]"},         // 串尾"信息"（R4 落位）：§4.3 Info 行
                                                  // 语义举例"默认值已填入"明示本码语义，
                                                  // 词表内唯一"非错误的有效结论"类＝
                                                  // infeasibility-proof（§4.4 结论呈现）；
                                                  // "信息"非 15 值 token——登记注记

        // ---- EVI（evidence §13 收编，7 项；分类串 7 项逐位对齐（R1）；
        //      严重串 6 项对 7 码——R2/R3：末三码按标注＋默认矩阵补齐）----
        {"EVI-SNAPSHOT-INCOMPLETE", DiagnosticCategory::DataInsufficient,
         DiagnosticSeverity::Warning, "[]"},      // ①数据不足；§4.3 数据不足典型码
        {"EVI-CASE-COVERAGE-MISSING", DiagnosticCategory::EvidenceMissing,
         DiagnosticSeverity::Warning, "[]"},      // ②证据缺失；§4.3 证据缺失典型码
        {"EVI-EVIDENCE-MISSING", DiagnosticCategory::EvidenceMissing,
         DiagnosticSeverity::Warning, "[]"},      // ③证据缺失
        {"EVI-PROOF-INVALID", DiagnosticCategory::EvidenceMissing,
         DiagnosticSeverity::Warning, "[]"},      // ④证据缺失
        {"EVI-ENVELOPE-ILLEGAL-COMBINATION", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ⑤内部(Dev)——不变量违反开发级
        {"EVI-CACHE-INCOMPATIBLE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // ⑥版本；严重取 §4.4 format-or-version
                                                  // 默认 Error（串"Dev/Dev"若覆盖此位则跨
                                                  // 用户级/开发级分界——§4.5 禁令排除）；
                                                  // §4.3 格式/版本典型码
        {"EVI-EVALUATOR-DUPLICATE", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ⑦内部(Dev)——备注"装配期错误"（开发级）

        // ---- EX（execution §3.4 收编，18 项；串"执行×5（含超时×2）/Dev×4
        //      （协议/登记表）/版本×2/执行×2/中断"段合计 14 对 18——R2/R3
        //      补位；备注"四个'开发级'标 Dev"与 execution 卡逐码点名吻合）----
        {"EX-TASK-REJECTED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行；§4.3 执行失败典型码
        {"EX-SNAPSHOT-STALE", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行（快照身份失效）
        {"EX-STORE-READ-ONLY", DiagnosticCategory::PermissionOrLock,
         DiagnosticSeverity::Warning, "[]"},      // §4.3 权限类典型码点名（PM-07 只读
                                                  // 横幅）——压缩段丢位补正（R2），登记注记
        {"EX-RESOURCE-INSUFFICIENT", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行（先节流后诊断——NFR-PERF-04）
        {"EX-CAPABILITY-UNSUPPORTED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行（能力不支持显式反馈）
        {"EX-WORKER-LAUNCH-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行（worker 启动失败）
        {"EX-WORKER-CRASHED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行；§4.3 执行失败典型码
        {"EX-WORKER-HUNG", DiagnosticCategory::Timeout,
         DiagnosticSeverity::Error, "[]"},        // 超时×2①；§4.3 超时典型码（心跳失联）
        {"EX-FORCE-TERMINATED", DiagnosticCategory::Timeout,
         DiagnosticSeverity::Error, "[]"},        // 超时×2②；§4.3 超时典型码（强杀）
        {"EX-CHANNEL-PROTOCOL-ERROR", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // Dev×4①（协议——execution 卡开发级点名）
        {"EX-REGISTRY-UNKNOWN-RUN", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // Dev×4②（登记表）
        {"EX-REGISTRY-MISMATCH", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // Dev×4③（登记表；§4.3 内部典型码）
        {"EX-STALE-ATTEMPT", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // Dev×4④（陈旧尝试——execution 卡开发级）
        {"EX-CHECKPOINT-CORRUPT", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×2①（"版本×2"段位＝CHECKPOINT 相邻对；
                                                  // 完整性校验失败按登记段归格式/版本）
        {"EX-CHECKPOINT-INCOMPATIBLE", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // 版本×2②（execution 卡"版本/身份/判定拒绝"）
        {"EX-ARCHIVE-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行×2①（归档失败——透传 StoreError 细节）
        {"EX-ARCHIVE-AUTHORITY-LOST", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // 执行×2②（迟到归档防御 A7）
        {"EX-TASK-INTERRUPTED", DiagnosticCategory::Interrupted,
         DiagnosticSeverity::Info, "[]"},         // "中断"段；§4.4 中断默认 Info（PM-08/15
                                                  // 恢复呈现数据源——execution 卡点名）

        // ---- RPT（reporting §3.5 收编，8 项；分类/严重串各 8 项逐位对齐
        //      （R1）；reporting 卡逐码严重点名全部吻合）----
        {"RPT-SOURCE-MISSING", DiagnosticCategory::EvidenceMissing,
         DiagnosticSeverity::Error, "[]"},        // ①证据缺失/Error（章节缺正式结果）
        {"RPT-SCOPE-INSUFFICIENT", DiagnosticCategory::DataInsufficient,
         DiagnosticSeverity::Warning, "[]"},      // ②数据不足/Warning（C 级降级建议）
        {"RPT-CONSISTENCY-MISMATCH", DiagnosticCategory::EvidenceMissing,
         DiagnosticSeverity::Error, "[]"},        // ③证据缺失/Error（AT-22 反例）
        {"RPT-ARCHIVE-CONFLICT", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // ④执行/Error（幂等判定失败）
        {"RPT-EXPORT-FAILED", DiagnosticCategory::ExecutionFailed,
         DiagnosticSeverity::Error, "[]"},        // ⑤执行/Error；reporting 卡"附可重试
                                                  // 动作"与分类动作族 retry-task 映射一致
        {"RPT-ROUNDTRIP-MISMATCH", DiagnosticCategory::FormatOrVersion,
         DiagnosticSeverity::Error, "[]"},        // ⑥版本/Error（RPT-06 往返复算不一致）
        {"RPT-CURRENTNESS-UNEVALUABLE", DiagnosticCategory::DataInsufficient,
         DiagnosticSeverity::Warning, "[]"},      // ⑦数据不足/Warning（P-EV-4 呈现）
        {"RPT-SECTION-NOT-APPLICABLE", DiagnosticCategory::InfeasibilityProof,
         DiagnosticSeverity::Info, "[]"},         // ⑧串"信息"（R4 落位）；reporting 卡
                                                  // "Info——不适用章节显式标记"；词表落位
                                                  // 同 POLICY-INFO-DEFAULT-APPLIED——登记注记

        // ---- DIAG（diagnostics 自用，7 项；分类串"安全/内部×3/内部/可确认
        //      类辅助×2"7 项、严重串"Error/Dev×3/Dev/Error/Info"7 项——
        //      逐位完美对齐（R1）；备注"红线的红线"）----
        {"DIAG-REDACTION-FAILED", DiagnosticCategory::SecurityOrRedaction,
         DiagnosticSeverity::Error, "[]"},        // ①安全/Error；§4.3 安全类典型码
        {"DIAG-REGISTRY-DUPLICATE", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ②内部×3①/Dev（注册边界自省）
        {"DIAG-REGISTRY-UNKNOWN-CODE", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ③内部×3②/Dev（DT-REG-4 兜底条目码）
        {"DIAG-CATALOG-OVERFLOW", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ④内部×3③/Dev（目录容量自省）
        {"DIAG-LOG-WRITE-FAILED", DiagnosticCategory::Internal,
         DiagnosticSeverity::Dev, "[]"},          // ⑤"内部"段/Dev（日志写失败自省）
        {"DIAG-FINDING-BINDING-INVALID", DiagnosticCategory::Confirmable,
         DiagnosticSeverity::Error, "[]"},        // ⑥"可确认类辅助×2"①（R4：confirmable
                                                  // ——SA-15 绑定复核流自省）/Error
        {"DIAG-FINDING-EXPIRED", DiagnosticCategory::Confirmable,
         DiagnosticSeverity::Info, "[]"},         // ⑦可确认类辅助②/Info（确认失效告知）
    }};
    return kSpecs;
}

}  // namespace

// =====================================================================
// 词表 token（§4.3 原文——switch 全枚举，同 token() 纪律）
// =====================================================================

std::string_view categoryToken(DiagnosticCategory category) noexcept
{
    switch (category) {
    case DiagnosticCategory::InputInvalid:        return "input-invalid";
    case DiagnosticCategory::FormatOrVersion:     return "format-or-version";
    case DiagnosticCategory::PermissionOrLock:    return "permission-or-lock";
    case DiagnosticCategory::ResourceMissing:     return "resource-missing";
    case DiagnosticCategory::PolicyDenied:        return "policy-denied";
    case DiagnosticCategory::Confirmable:         return "confirmable";
    case DiagnosticCategory::ExecutionFailed:     return "execution-failed";
    case DiagnosticCategory::Canceled:            return "canceled";
    case DiagnosticCategory::Interrupted:         return "interrupted";
    case DiagnosticCategory::Timeout:             return "timeout";
    case DiagnosticCategory::DataInsufficient:    return "data-insufficient";
    case DiagnosticCategory::EvidenceMissing:     return "evidence-missing";
    case DiagnosticCategory::InfeasibilityProof:  return "infeasibility-proof";
    case DiagnosticCategory::Internal:            return "internal";
    case DiagnosticCategory::SecurityOrRedaction: return "security-or-redaction";
    }
    return "input-invalid";   // 全枚举不可达；保守取首值（编译器告警已拦截扩表遗漏）
}

std::string_view severityToken(DiagnosticSeverity severity) noexcept
{
    switch (severity) {
    case DiagnosticSeverity::Error:   return "error";
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Info:    return "info";
    case DiagnosticSeverity::Dev:     return "dev";
    }
    return "error";
}

std::string_view retryKindToken(RetryKind kind) noexcept
{
    switch (kind) {
    case RetryKind::Never:     return "never";
    case RetryKind::UserRetry: return "user-retry";
    case RetryKind::AutoRetry: return "auto-retry";
    }
    return "never";
}

// =====================================================================
// CodeDescriptor 成员
// =====================================================================

bool CodeDescriptor::operator==(const CodeDescriptor& o) const
{
    return code == o.code
        && ownerUnit == o.ownerUnit
        && category == o.category
        && severity == o.severity
        && titleKey == o.titleKey
        && detailKey == o.detailKey
        && paramSchema == o.paramSchema
        && confirmable == o.confirmable
        && requiresComparison == o.requiresComparison
        && retryable == o.retryable
        && userVisible == o.userVisible
        && reportable == o.reportable
        && historical == o.historical
        && registryVersion == o.registryVersion
        && deprecated == o.deprecated
        && supersededBy == o.supersededBy;
}

bool isValidDiagCodeSyntax(std::string_view code) noexcept
{
    // §4.5 句法原文：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64。逐字符实现（不用
    // std::regex——异常/分配路径与本判定的高频调用面不匹配，且 MSVC regex
    // 行为差异是确定性风险）。
    if (code.empty() || code.size() > 64) {
        return false;
    }
    bool prevDash = true;   // 起始视同"刚出现分隔符"——首字符不许是 '-'
    for (const char ch : code) {
        const bool alnum = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (ch == '-') {
            if (prevDash) {
                return false;   // 首字符/连续连字符
            }
            prevDash = true;
            continue;
        }
        if (!alnum) {
            return false;       // 小写/空白/标点（异常文本）一律非法——DT-REG-4 拦截面
        }
        prevDash = false;
    }
    return !prevDash;           // 尾连字符非法
}

// =====================================================================
// StableCodeRegistry——注册期验证链与查询（§4.5 行为表/§9.1 契约）
// =====================================================================

void StableCodeRegistry::seal() noexcept
{
    m_sealed = true;
}

void StableCodeRegistry::registerCode(const CodeDescriptor& descriptor)
{
    // 运行期（seal 后）调用＝装配期契约违约（§9.1"注册（装配期；运行期
    // 调用抛 DiagnosticsError(Usage)）"——判据实现口径见类注释）。
    if (m_sealed) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "运行期 registerCode（码 " + descriptor.code + "）——注册仅限装配期");
    }

    // ---- 注册期验证（§4.5"注册期验证"行；检查序固定如下，首错即停并
    // 指明字段——detail 携带字段名，满足"任一失败即拒绝并指明字段"）----

    // ①句法：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64（异常文本/小写串在此拦截——
    // DT-REG-4 的注册侧原语：字符串不经登记不可能成为码）。
    if (!isValidDiagCodeSyntax(descriptor.code)) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段 code 句法非法（^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）: "
                               + descriptor.code);
    }

    // ②前缀-所有权一致：首段必须在 §4.5 前缀表内且与 ownerUnit 声明域一致
    // （"前缀即所有权声明，跨前缀注册拒绝"——§9.1 将此场景归 CodeUnknown
    // "前缀冲突"：该码不在其声称的所有权域内）。
    const std::string_view* expectedOwner = expectedOwnerFor(descriptor.code);
    if (expectedOwner == nullptr || *expectedOwner != descriptor.ownerUnit) {
        throw DiagnosticsError(
            DiagnosticsErrorCode::CodeUnknown,
            "字段 ownerUnit 与码前缀所有权不符（码 " + descriptor.code + " 声明 ownerUnit="
                + descriptor.ownerUnit + "）");
    }

    // ③paramSchema：必填＋受限 JSON 形＋参数名词形＋无重复（"非空且参数名
    // 合法"——空数组 "[]" 是合法的显式"无参数"声明；完全缺省/空串＝违约，
    // 因实例占位一致性校验（§9.2）需要明确的模式边界）。
    if (const std::string schemaProblem = validateParamSchema(descriptor.paramSchema);
        !schemaProblem.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::ParamSchemaMismatch,
                               "字段 paramSchema: " + schemaProblem);
    }

    // ④文案键（三步，检查序固定）：a) 非空（键体系为持久化契约——
    // P-DIAG-9）；b) 跨码唯一（NFR-MNT-03 文案单一权威：一键多码使呈现端
    // 无法分辨语义——重复定义＝DuplicateCode，同款注册边界拒绝）；c) 键形
    // 命名约定（diag.<code-lower>.title/detail——P-DIAG-9 键体系冻结约定，
    // 内置表经 derivedTextKey 单点派生全表合规；注册 API 的硬约束＝a/b，
    // 键形偏离＝Usage 拒绝）。
    if (descriptor.titleKey.empty() || descriptor.detailKey.empty()) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段 titleKey/detailKey 为空（键体系为持久化契约——P-DIAG-9）");
    }
    for (const std::string* key : {&descriptor.titleKey, &descriptor.detailKey}) {
        if (m_textKeys.find(*key) != m_textKeys.end()) {
            throw DiagnosticsError(DiagnosticsErrorCode::DuplicateCode,
                                   "文案键重复（" + *key + "）——键随码表登记须注册期唯一");
        }
    }
    if (descriptor.titleKey != derivedTextKey(descriptor.code, "title")
        || descriptor.detailKey != derivedTextKey(descriptor.code, "detail")) {
        throw DiagnosticsError(
            DiagnosticsErrorCode::Usage,
            "字段 titleKey/detailKey 偏离命名约定 diag.<code-lower>.title/detail（码 "
                + descriptor.code + "）");
    }

    // ⑤确认不变量：confirmable ⇒ requiresComparison（SA-15"可确认必为
    // 比较型"——core C-1 的注册表侧强化，§4.5 字段约束原文）。
    if (descriptor.confirmable && !descriptor.requiresComparison) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段不变量违约：confirmable=true 要求 requiresComparison=true（SA-15）");
    }

    // ⑥Dev 码边界：userVisible/reportable/historical 强制 false（"瞬时开发
    // 诊断不污染历史"——§4.5 字段约束原文；用户级/开发级分界）。
    if (descriptor.severity == DiagnosticSeverity::Dev
        && (descriptor.userVisible || descriptor.reportable || descriptor.historical)) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段不变量违约：Dev 码须 userVisible=reportable=historical=false（码 "
                                   + descriptor.code + "）");
    }

    // ⑦迁移目标自引用（supersededBy==code＝无意义迁移——tombstone 语义面
    // 自洽性校验；supersededBy 指向尚未注册的码允许：装配清单顺序自由，
    // 迁移目标随清单其余码到位）。
    if (descriptor.supersededBy.has_value() && *descriptor.supersededBy == descriptor.code) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "字段 supersededBy 自引用（码 " + descriptor.code + "）");
    }

    // ⑧重复注册拒绝（NFR-MNT-03：重复定义注册边界拒绝——不覆盖、不静默；
    // §4.5 版本冲突行：同 code 不同 registryVersion 的并发注册＝同一拒绝
    // 面。码是唯一键，元数据演进只能走装配清单修订，不走覆盖写）。
    if (m_codes.find(descriptor.code) != m_codes.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::DuplicateCode,
                               "重复注册（码 " + descriptor.code + "）——不覆盖不静默（NFR-MNT-03）");
    }

    // 全部验证通过：登记（值拷贝入表——调用方此后修改 descriptor 与表无关）。
    m_codes.emplace(descriptor.code, descriptor);
    m_textKeys.emplace(descriptor.titleKey, descriptor.code);
    m_textKeys.emplace(descriptor.detailKey, descriptor.code);
}

const CodeDescriptor* StableCodeRegistry::find(std::string_view code) const noexcept
{
    // 查询非抛（§9.1"未知码 find 返回 nullptr"）。透明比较器异构查找：
    // 不构造临时 string——零分配即无 bad_alloc 抛出面，noexcept 真实成立。
    // 非法句法串必然未注册（注册入口已拦截），查表未命中即 nullptr——
    // DT-REG-4"异常文本作码"在此得到与句法非法一致的拒识结果。
    const auto it = m_codes.find(code);
    return it == m_codes.end() ? nullptr : &it->second;
}

std::vector<std::string> StableCodeRegistry::registeredCodes(std::string_view ownerUnit) const
{
    // std::map 迭代即字典序（确定性观测面——NFR-COR-02）；tombstone 保留
    // （§4.5.1"删除禁止"——注册边界只增不删）。
    std::vector<std::string> codes;
    for (const auto& [code, descriptor] : m_codes) {
        if (descriptor.ownerUnit == ownerUnit) {
            codes.push_back(code);
        }
    }
    return codes;
}

CodeTableManifest StableCodeRegistry::manifest() const
{
    // 纯函数（§9.1 契约表"副作用：无 I/O；manifest 摘要计算纯函数"）——
    // 同表两次调用必得逐字节相等结果（DT-REG-1 观测点）。tombstone 参与
    // 清单与摘要：废弃状态是跨进程握手的一致性面（两侧 tombstone 不一致
    // ＝码表漂移，握手应拒绝）。
    CodeTableManifest result;
    result.entries.reserve(m_codes.size());
    for (const auto& [code, descriptor] : m_codes) {
        result.entries.push_back(descriptor);   // map 迭代序＝code 字典序（§9.1 后置）
    }

    // canonical 编码（规则见文件头"manifest 摘要 canonical 编码"）：
    // magic 后逐条目按字段表序编码——字段序即 CodeDescriptor 声明序。
    core::ContentDigester digester;
    digester.update("IRDDCM1", 7);
    for (const CodeDescriptor& d : result.entries) {
        feedString(digester, d.code);
        feedString(digester, d.ownerUnit);
        feedString(digester, categoryToken(d.category));
        feedString(digester, severityToken(d.severity));
        feedString(digester, d.titleKey);
        feedString(digester, d.detailKey);
        feedString(digester, d.paramSchema);
        feedBool(digester, d.confirmable);
        feedBool(digester, d.requiresComparison);
        feedString(digester, retryKindToken(d.retryable));
        feedBool(digester, d.userVisible);
        feedBool(digester, d.reportable);
        feedBool(digester, d.historical);
        feedU32le(digester, d.registryVersion);
        feedBool(digester, d.deprecated);
        if (d.supersededBy.has_value()) {
            feedBool(digester, true);
            feedString(digester, *d.supersededBy);
        } else {
            feedBool(digester, false);
        }
    }
    result.digest.bytes = digester.finalize();
    return result;
}

void StableCodeRegistry::deprecate(std::string_view code,
                                   std::optional<std::string> supersededBy)
{
    // 运行期调用＝装配期契约违约（同 registerCode——§4.5.1"废弃：码元数据
    // 置 deprecated=true（装配清单修订）"；tombstone 永存不删除）。
    if (m_sealed) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "运行期 deprecate（码 " + std::string{code} + "）——废弃仅限装配期修订");
    }

    // 未注册码→CodeUnknown（无码可废；"未注册"自然覆盖句法非法串）。
    const auto it = m_codes.find(std::string{code});
    if (it == m_codes.end()) {
        throw DiagnosticsError(DiagnosticsErrorCode::CodeUnknown,
                               "deprecate 未注册码（" + std::string{code} + "）");
    }

    // 自迁移拒绝（同注册期⑦——tombstone 语义面自洽）。
    if (supersededBy.has_value() && *supersededBy == it->second.code) {
        throw DiagnosticsError(DiagnosticsErrorCode::Usage,
                               "supersededBy 自引用（码 " + it->second.code + "）");
    }

    // 置位 tombstone：以"整值替换"承载（CodeDescriptor 构造后不可变——
    // 不原地改写；装配期单线程约定下替换对后续 find 立即可见）。重复
    // deprecate 合法（装配清单可再修订——以最后一次修订为准；已发布的
    // tombstone 在真实流程中随清单版本固化，不回退）。
    CodeDescriptor tombstone = it->second;
    tombstone.deprecated = true;
    if (supersededBy.has_value()) {
        tombstone.supersededBy = *supersededBy;
    }
    it->second = std::move(tombstone);
}

// =====================================================================
// 内置码表装配（§4.6——87 码全量收编）
// =====================================================================

std::vector<CodeDescriptor> builtinCodeDescriptors()
{
    std::vector<CodeDescriptor> table;
    table.reserve(builtinSpecs().size());
    for (const BuiltinSpec& spec : builtinSpecs()) {
        table.push_back(makeBuiltin(spec));
    }
    return table;
}

void registerBuiltinCodes(IDiagnosticRegistry& registry)
{
    // 逐条注册：任何一条的注册期验证失败（含与既有登记的 DuplicateCode
    // 冲突）即异常中止——内置表是装配清单权威，混入同码他义描述符属装配
    // 错误，fail-fast 不静默跳过（NFR-MNT-03 边界拒绝；AGENTS.md"禁止吞错"）。
    for (const CodeDescriptor& descriptor : builtinCodeDescriptors()) {
        registry.registerCode(descriptor);
    }
}

}  // namespace sdurws::ird::diagnostics
