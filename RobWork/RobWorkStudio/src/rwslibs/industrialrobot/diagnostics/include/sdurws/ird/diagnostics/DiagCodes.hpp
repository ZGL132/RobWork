/**
 * @file   DiagCodes.hpp
 * @brief  稳定诊断码注册表——分类/严重词表、CodeDescriptor、IDiagnosticRegistry、
 *         StableCodeRegistry 实现与内置码表（§4.6 全量收编）。
 *
 * 设计依据：
 *   - units/diagnostics.md §4.3（DiagnosticCategory 15 值/DiagnosticSeverity
 *     4 值——实现承载词表，P-DIAG-3 登记）、§4.5（CodeDescriptor 字段表＋
 *     注册表行为冻结表）、§4.5.1（废弃码与迁移——tombstone 永存）、
 *     §4.6（内置码表阶段 A 收编清单——码值权威＝本注册表，不重排各卡已
 *     登记建议值）、§9.1（IDiagnosticRegistry 接口签名与行为契约）
 *   - 需求 ERR-01（诊断码稳定可追溯）、NFR-MNT-03（码/文案单一权威：
 *     重复定义注册边界拒绝；titleKey/detailKey 注册期唯一性校验）
 *   - 任务契约 tasks/foundation/DIAG-T03.json（≙WP-09-T03）：`DiagCodes.*`
 *     （CodeDescriptor/StableCodeRegistry/内置码表全量收编——§11 行产物）
 *
 * 背景说明（码值为什么以注册表为唯一权威——PA-1/NFR-MNT-03）：诊断码进入
 * 项目历史与报告后必须永久可解释（"码值一经注册并进入任何持久化产物即不
 * 再改义、不改拼"——§4.5 持久化契约）。各协作单元（project/runtime/policy/
 * evidence/reporting/execution）的卡内码表仅为**建议值**，全量收编进本注册
 * 表的内置装配清单（§4.6，87 码）；任何单元不得以字符串字面量临时拼码构造
 * 诊断（工厂只接受已注册码——§4.5，异常文本仅可经 ErrorCodeTranslator 的
 * 已登记类型映射转译，DT-REG-4）。manifest() 摘要供主进程/worker 握手比对
 * （与 evidence EvaluatorRegistry manifest 同模式——跨进程码表一致是"同一
 * 码同义"的装配侧保障）。
 *
 * 陷阱处置锚点（契约 knownPitfalls 逐项）：
 *   - P-DIAG-3：DiagnosticCategory/DiagnosticSeverity 为**实现承载词表**
 *     （上游无需求级枚举），语义锚点逐项登记于 §4.4 矩阵；本头词表值即
 *     §4.3 原文 15/4 值，**不新增轴值**——分类只驱动呈现分组/动作建议/
 *     日志分流，不改写 outcome/engineeringStatus/当前性任何一轴；需求侧如
 *     冻结正式枚举走需求变更，不私裁。
 *   - P-DIAG-1：DiagCode 等消费契约以 core.md v0.1（Draft 未冻结）为基线
 *     （本头 code/supersededBy 用 core::DiagCode）；core 冻结 diff 后增量
 *     同步，不私改 core。
 *   - P-DIAG-9：titleKey/detailKey 为**文案键**（diag.<code-lower>.title/
 *     detail 命名约定，注册期唯一性校验）；文案**值**归 ui/文案资源——
 *     键/值分离（UX-02），本设施不携带任何用户可见文案。
 *
 * 线程安全：注册期单线程约定（L5 装配）；运行期 find/manifest 并发只读
 * 安全（内部无锁——运行期无写入路径；"运行期不增删"由 seal() 装配原语
 * 承载，见 StableCodeRegistry 注释）。CodeDescriptor/CodeTableManifest 为
 * 纯值类型。
 */

#ifndef SDURWS_IRD_DIAGNOSTICS_DIAGCODES_HPP
#define SDURWS_IRD_DIAGNOSTICS_DIAGCODES_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>       // core::DiagCode（P-DIAG-1 基线——core.md v0.1）
#include <sdurws/ird/core/Digest.hpp>          // core::ContentIdentity/ContentDigester（manifest 摘要——CR-02 唯一算法）
#include <sdurws/ird/diagnostics/Errors.hpp>   // DiagnosticsError/DiagnosticsErrorCode（本单元唯一异常类型）

namespace sdurws::ird::diagnostics {

// =====================================================================
// 实现承载词表（§4.3——P-DIAG-3 登记；枚举顺序＝§4.3 表行序，一经交付
// 只允许表尾追加并走单元卡增量修订——持久化契约面纪律）
// =====================================================================

/**
 * @brief 诊断分类（§4.3 词表 15 值，token 小写连字符）。
 *
 * 实现承载（P-DIAG-3）：上游无需求级枚举定义（ERR-01 只定字段；
 * NFR-REL-05 只分两级日志），本词表为 UX-10/PM-15 分组呈现与 §7 日志
 * 分流提供机器可读分类。语义锚点逐项见 §4.4 矩阵；分类只驱动呈现分组、
 * 处理动作建议与日志分流，**绝不改写** outcome/engineeringStatus/当前性
 * 任何一轴（不构成新工程状态）。
 */
enum class DiagnosticCategory : std::uint8_t {
    InputInvalid,          ///< input-invalid——输入非法（REQ-06/MDL-06 硬断言）
    FormatOrVersion,       ///< format-or-version——旧格式/未来版本/schema/契约不兼容（PM-06/CON-04）
    PermissionOrLock,      ///< permission-or-lock——只读/锁持有/写拒绝（PM-07/SA-17）
    ResourceMissing,       ///< resource-missing——外部源 Missing/Changed/资源越界（NFR-REL-04）
    PolicyDenied,          ///< policy-denied——策略评估否决（域内事实，非用户错误）
    Confirmable,           ///< confirmable——策略校验超限待用户显式确认（SA-15/MDL-06④）
    ExecutionFailed,       ///< execution-failed——outcome=Failed 轴（TASK-02）
    Canceled,              ///< canceled——outcome=Canceled 轴（正常取消非错误——UX-03）
    Interrupted,           ///< interrupted——outcome=Interrupted 轴（NFR-REL-03）
    Timeout,               ///< timeout——心跳失联/取消协议超时/等待超时
    DataInsufficient,      ///< data-insufficient——engineeringStatus=DataInsufficient 轴（搜索未果 C5）
    EvidenceMissing,       ///< evidence-missing——必需证据 Missing/Invalid
    InfeasibilityProof,    ///< infeasibility-proof——有效工程结论而非错误（RPT-05 评审记录）
    Internal,              ///< internal——防御性检查失败/协议错误/不变量违反（开发级为主）
    SecurityOrRedaction,   ///< security-or-redaction——SafePath 违规/预算超限/脱敏失败
};

/**
 * @brief 取分类 token（§4.3 表列原文，小写连字符）。
 * @param category [in] 分类值（全表 15 值均有 token——switch 全枚举）
 * @return 稳定 token（静态存储期；码表 canonical 编码与呈现分组共用——
 *         同值同串、无 locale 依赖，NFR-COR-02）
 */
std::string_view categoryToken(DiagnosticCategory category) noexcept;

/**
 * @brief 诊断严重级别（§4.3 词表 4 值）。
 *
 * severity 归属码表（注册时声明），**不随实例变化**（§4.3 传播规则）；
 * 目录/日志按 severity 分流（§4.3 表：Dev 不进用户目录/用户日志/报告）；
 * 聚合取成员最高 severity（Dev 不参与用户级聚合——§6.4，随 DIAG-T06 消费）。
 */
enum class DiagnosticSeverity : std::uint8_t {
    Error,    ///< error——阻断当前操作/任务失败/数据损坏风险（全渠道）
    Warning,  ///< warning——不阻断但需用户知晓（含可确认类、策略拒绝类）
    Info,     ///< info——有效结论/告知（不可行证明、默认值已填入、中断呈现）
    Dev,      ///< dev——开发诊断（无用户语义；NFR-REL-05 开发级；userVisible/
              ///       reportable/historical 强制 false——§4.5 注册期验证）
};

/**
 * @brief 取严重级别 token（§4.3 表列原文）。
 * @param severity [in] 严重级别值（全表 4 值均有 token——switch 全枚举）
 * @return 稳定 token（静态存储期）
 */
std::string_view severityToken(DiagnosticSeverity severity) noexcept;

/**
 * @brief 可重试性（§4.5 CodeDescriptor.retryable——处理动作族的机器锚点）。
 *
 * 阶段 A 内置码表按 §4.4 分类动作族机械映射（登记于 DiagCodes.cpp 表头
 * 注释）：动作族含重试语义（fix-input/retry-readonly/relink/adjust-policy/
 * confirm-or-fix/retry-task/rerun-interrupted/supply-evidence 等）→
 * UserRetry；动作族为 none/review-proof/report-bug/inspect 类 → Never；
 * AutoRetry 阶段 A 无消费者（§4.4 无对应动作族）——枚举值保留不使用
 * （NFR-MNT-04：无消费者的能力不预建行为）。
 */
enum class RetryKind : std::uint8_t {
    Never,       ///< never——无重试语义（结论呈现/报告缺陷类）
    UserRetry,   ///< user-retry——用户采取措施后可重试（动作族机器锚点）
    AutoRetry,   ///< auto-retry——系统自动重试（阶段 A 未使用，值保留）
};

/**
 * @brief 取可重试性 token（canonical 编码用；词表小写连字符）。
 * @param kind [in] 可重试性值（全表 3 值均有 token——switch 全枚举）
 * @return 稳定 token（静态存储期）
 */
std::string_view retryKindToken(RetryKind kind) noexcept;

// =====================================================================
// CodeDescriptor（§4.5 字段表——注册项，构造后不可变；值语义）
// =====================================================================

/**
 * @brief 稳定诊断码的注册项（§4.5 字段表 16 字段，逐字段约束见成员注释）。
 *
 * 值语义聚合（构造后不可变——注册表内以整值替换承载元数据演进，§4.5
 * "码元数据演进＝装配清单变更"）；无 setter（类型层面杜绝——§9.2"非法
 * 调用"行同款纪律）。字段顺序＝§4.5 表行序（canonical 编码依赖此序，
 * 只允许表尾追加字段并升 registryVersion/codec 版本）。
 *
 * 线程安全：纯值类型（并发只读安全）。
 */
struct CodeDescriptor {
    /// 稳定码（§4.5：唯一键；句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64；首段＝
    /// 单元短前缀＝所有权声明。P-DIAG-1：类型＝core::DiagCode（core.md
    /// v0.1 基线——core 冻结 diff 后增量同步，不私改 core））。
    core::DiagCode code;
    /// 所有者单元 token（如 "project"——前缀-所有权一致性校验的对照面；
    /// 阶段 B 起业务域注册须域卡在案）。
    std::string ownerUnit;
    /// 分类（§4.3 词表——实现承载 P-DIAG-3；默认呈现分组与动作建议依据）。
    DiagnosticCategory category{};
    /// 默认严重级别（归属码表不随实例变化——§4.3；Dev 码强制 userVisible/
    /// reportable/historical=false——§4.5 注册期验证）。
    DiagnosticSeverity severity{};
    /// 用户文案键（P-DIAG-9 键/值分离：键体系冻结——命名约定
    /// "diag.<code-lower>.title"，注册期唯一性校验（NFR-MNT-03 文案单一
    /// 权威）；**值**归 ui/文案资源，本设施不携带）。
    std::string titleKey;
    /// 用户文案键（同上，约定 "diag.<code-lower>.detail"）。
    std::string detailKey;
    /// 参数模式（§4.5：受限 JSON 形命名参数清单，如 ["pid","host"]；空数组
    /// "[]"＝无参数。**必填字段**——调用方必须显式声明（缺省即
    /// ParamSchemaMismatch 拒绝）；实例占位一致性归工厂校验（§9.2，随
    /// DIAG-T04 消费））。
    std::string paramSchema;
    /// 是否为可确认类（§4.5：true ⇒ requiresComparison=true——SA-15"可确认
    /// 必为比较型"的注册表侧强化；阶段 A 内置表全 false，可确认域码阶段 B
    /// 起随域卡注册）。
    bool confirmable = false;
    /// 比较型三要素强制（ERR-01/UX-03——实例 comparison 完整性归工厂校验）。
    bool requiresComparison = false;
    /// 可重试性（§4.4 动作族的机器锚点——映射规则见 RetryKind 注释）。
    RetryKind retryable = RetryKind::Never;
    /// 是否允许对外显示（Dev 码强制 false——§4.5"Dev 码=false"）。
    bool userVisible = true;
    /// 是否允许进入报告（reporting 消费；Dev 码强制 false）。
    bool reportable = true;
    /// 是否允许进入项目历史（随修订/结果持久化；Dev 码强制 false——瞬时
    /// 开发诊断不污染历史，PA-2 精神）。
    bool historical = true;
    /// 注册版本（每次该码元数据变更＋1；进入诊断实例 contractVersions 标注
    /// 供报告侧检测文案/语义演进——§4.5；首次登记＝1）。
    std::uint32_t registryVersion = 1;
    /// 废弃标记（§4.5.1：经 deprecate() 置位＝装配清单修订；tombstone 永存
    /// 不删除——持久化兼容）。
    bool deprecated = false;
    /// 迁移目标（§4.5.1：supersededBy 指向新码；历史产物不重写（PA-2）——
    /// 呈现侧遇 deprecated 码按 tombstone 映射到新码文案键并保留原码原文）。
    std::optional<core::DiagCode> supersededBy;

    /// 成员精确等值（测试/装配核对用；跨进程一致性判定以 manifest 摘要为准）。
    bool operator==(const CodeDescriptor& o) const;
    bool operator!=(const CodeDescriptor& o) const { return !(*this == o); }
};

/**
 * @brief 诊断码句法校验（§4.5：^[A-Z0-9]+(-[A-Z0-9]+)*$ 且 ≤64）。
 *
 * 注册期验证的第一道闸（§4.5 注册期验证表"句法"项）；工厂（DIAG-T04）
 * 以未注册码构造时的句法前置同用此函数。设计为公开函数：句法是持久化
 * 契约（§4.5），测试与后续任务的转译器（ErrorCodeTranslator 登记校验）
 * 均需同一判定单点。
 *
 * @param code [in] 待校验码文本（可为任意串——异常文本不作码即由此拦截，
 *              DT-REG-4 的注册表侧原语）
 * @return true＝句法合法（非空、仅 A-Z/0-9、单段或多段以 '-' 相连、
 *         无首尾连字符、总长 ≤64）
 *
 * 纯函数；线程安全；确定性（NFR-COR-02）。
 */
bool isValidDiagCodeSyntax(std::string_view code) noexcept;

// =====================================================================
// CodeTableManifest（§9.1——跨进程一致性；与 evidence RegistrationManifest
// 同模式：排序清单＋SHA-256 摘要，worker 握手比对）
// =====================================================================

/**
 * @brief 码表清单及其摘要（§9.1 manifest() 后置：按 code 字典序排序的
 *        清单及其 SHA-256 摘要——core ContentDigester，CR-02 唯一算法）。
 *
 * 确定性（NFR-COR-02）：entries 按 code 字典序升序；digest 为清单的
 * canonical 编码摘要（magic "IRDDCM1"＋逐条目规范编码——实现见
 * src/DiagCodes.cpp 头注释；非往返载体，唯一消费方式＝摘要比对）。
 * 同一注册集（任意注册顺序、含 tombstone）必得同一 entries 序与同一
 * digest——manifest() 两次计算稳定（DT-REG-1 观测点）。
 *
 * 值语义；线程安全：纯值。
 */
struct CodeTableManifest {
    /// 全表描述符（按 code 字典序升序——§9.1 排序稳定性；含 deprecated
    /// tombstone：废弃状态是跨进程握手的一致性面之一）。
    std::vector<CodeDescriptor> entries;
    /// 清单摘要（SHA-256 经 core::ContentDigester；空表也有确定摘要——
    /// 两侧空装配一致）。
    core::ContentIdentity digest;

    bool operator==(const CodeTableManifest& o) const
    {
        return entries == o.entries && digest == o.digest;
    }
    bool operator!=(const CodeTableManifest& o) const { return !(*this == o); }
};

// =====================================================================
// IDiagnosticRegistry（§9.1 接口原文——稳定码注册表抽象；实现
// StableCodeRegistry）
// =====================================================================

/**
 * @brief 稳定码注册表接口（§9.1 原文五方法，签名逐字承载）。
 *
 * 行为契约（§9.1 签名注释＋契约表，逐行冻结）：
 *   - registerCode：注册（装配期；**运行期调用抛 DiagnosticsError(Usage)**）。
 *     前置：descriptor 全字段合法（§4.5 注册期验证表）。后置：码可被工厂
 *     使用；manifest 摘要更新。错误：DuplicateCode / CodeUnknown 前缀冲突 /
 *     ParamSchemaMismatch 等（detail 指明字段）。
 *   - find：查询（运行期；并发只读安全；未注册→nullptr——查询非抛）。
 *   - registeredCodes：按 ownerUnit 列码（字典序——确定性）。
 *   - manifest：跨进程一致性（worker 握手比对——CON-06/AT-19 同模式）。
 *   - deprecate：废弃（装配清单修订；tombstone 永存——§4.5.1）。
 *
 * 生命周期/所有权（§9.1 契约表）：进程级单例（L5 创建；主进程与 worker
 * 各一，同一注册清单）；消费方持引用。副作用：无 I/O；manifest 摘要计算
 * 纯函数。
 */
class IDiagnosticRegistry {
public:
    virtual ~IDiagnosticRegistry() = default;

    /**
     * @brief 注册一个稳定码（§9.1 原文签名）。
     *
     * 注册期验证（§4.5 行为表"注册期验证"行，任一失败即拒绝并指明字段）：
     * 句法（isValidDiagCodeSyntax）、前缀-所有权一致、titleKey/detailKey
     * 唯一、confirmable⇒requiresComparison、Dev 码 userVisible/reportable/
     * historical=false、paramSchema 必填且参数名合法。
     *
     * @param descriptor [in] 注册项（值语义拷贝存入；调用方此后对其的修改
     *                   与注册表无关）
     *
     * @throws DiagnosticsError Usage（码句法非法/descriptor 不变量违约/
     *         运行期调用〔实现 seal 后〕）；CodeUnknown（首段前缀不在
     *         §4.5 前缀表或与 ownerUnit 声明域不符）；DuplicateCode（同码
     *         重复注册〔含同码不同 registryVersion——§4.5 版本冲突行〕/
     *         titleKey-detailKey 唯一性冲突）；ParamSchemaMismatch
     *         （paramSchema 缺省/形非法/参数名非法）
     */
    virtual void registerCode(const CodeDescriptor& descriptor) = 0;

    /**
     * @brief 查询码描述符（§9.1 原文签名；查询非抛）。
     *
     * @param code [in] 码文本（任意串——非法句法必然未注册，返回 nullptr）
     * @return 已注册码的描述符指针（tombstone 也命中——§4.5.1 只读解析）；
     *         未注册→nullptr。指针指向注册表内部存储：**注册表存续期内
     *         有效**（进程级单例——消费方持注册表引用，生命周期覆盖使用面）；
     *         装配期后续 deprecate 会替换同码登记值（装配期单线程约定下
     *         调用方自行避免持旧指针跨越修订）。
     */
    virtual const CodeDescriptor* find(std::string_view code) const noexcept = 0;

    /**
     * @brief 列出某所有者单元已注册的码（§9.1 原文签名）。
     *
     * @param ownerUnit [in] 所有者单元 token（如 "project"）；无匹配→空表
     * @return 码清单（按字典序升序——确定性观测面；含 tombstone——注册
     *         边界只增不删）
     */
    virtual std::vector<std::string> registeredCodes(std::string_view ownerUnit) const = 0;

    /**
     * @brief 码表清单＋摘要（§9.1 原文签名；纯函数——同表同摘要，
     *        DT-REG-1"manifest 摘要稳定（两次计算相等）"的承载）。
     *
     * @return 清单（entries 按 code 字典序；digest＝canonical 编码的
     *         SHA-256——worker 握手比对用，主/worker 两侧同清单必同摘要）
     */
    virtual CodeTableManifest manifest() const = 0;

    /**
     * @brief 废弃一个码（§9.1 原文签名；装配清单修订——tombstone 永存）。
     *
     * 后置（§4.5.1）：该码 descriptor.deprecated=true；supersededBy 按入参
     * 置位（nullopt＝纯废弃无迁移目标）；旧持久化产物中的该码仍可只读
     * 解析（find 命中 tombstone）；**删除禁止**（registeredCodes/manifest
     * 仍含该码——持久化兼容，PA-2：历史不重写）。
     *
     * @param code         [in] 已注册码（未注册→CodeUnknown）
     * @param supersededBy [in] 迁移目标码（可为 nullopt；与 code 相同→
     *                     Usage——自迁移无意义）
     *
     * @throws DiagnosticsError CodeUnknown（码未注册）；Usage（运行期调用
     *         〔实现 seal 后〕/supersededBy 自引用）
     */
    virtual void deprecate(std::string_view code,
                           std::optional<std::string> supersededBy) = 0;
};

// =====================================================================
// StableCodeRegistry（§9.1 实现；§11 DIAG-T03 行产物）
// =====================================================================

/**
 * @brief 稳定码注册表实现（std::map 承载——节点地址稳定＋字典序天然有序）。
 *
 * "运行期"判据的实现口径（§9.1"注册（装配期；运行期调用抛 Usage）"——
 * 设计未定义"运行期"判据，按 evidence EV-T09 实现口径登记先例 DTB §5.4
 * 落位）：seal() 为**实现类**装配完成原语（不在 IDiagnosticRegistry 接口
 * 上——五方法签名逐字 §9.1，不扩接口）；L5 装配清单注册完毕后调用 seal()
 * 进入运行期，此后 registerCode/deprecate 一律 Usage。seal 幂等；未 seal
 * 的注册表查询路径（find/registeredCodes/manifest）可用（装配期自检合法）。
 *
 * 线程安全（§4.5"线程安全"行原文）：注册期单线程约定（装配）；运行期
 * find/manifest 并发只读安全——实现内部无锁：seal 后不存在任何写入路径
 * （注册/废弃走 Usage 拒绝），并发只读天然安全。注册与运行期混调（多线程
 * 同时 registerCode）属调用方违约，不在防护范围（§4.5 单线程约定原文）。
 *
 * 生命周期：进程级单例（L5 创建；主进程与 worker 各一）；消费方持引用。
 */
class StableCodeRegistry final : public IDiagnosticRegistry {
public:
    StableCodeRegistry() = default;

    // 禁拷贝/禁移动：进程级单例语义（消费方持引用——§9.1 契约表；引用
    // 稳定性是 find 返回指针有效性的前提）。
    StableCodeRegistry(const StableCodeRegistry&) = delete;
    StableCodeRegistry& operator=(const StableCodeRegistry&) = delete;

    /// @brief 装配完成原语（实现类口径——见类注释）：进入运行期，幂等。
    void seal() noexcept;

    /// @brief 是否已进入运行期（装配自检与测试观测用）。
    bool sealed() const noexcept { return m_sealed; }

    // ---- IDiagnosticRegistry（§9.1 五方法——行为契约见接口注释）----
    void registerCode(const CodeDescriptor& descriptor) override;
    const CodeDescriptor* find(std::string_view code) const noexcept override;
    std::vector<std::string> registeredCodes(std::string_view ownerUnit) const override;
    CodeTableManifest manifest() const override;
    void deprecate(std::string_view code,
                   std::optional<std::string> supersededBy) override;

private:
    /// 全表（code→描述符）。std::map＋透明比较器（std::less<>）：节点地址
    /// 稳定（find 指针有效性——运行期无删除）；迭代即字典序（manifest/
    /// registeredCodes 的确定性序）；find 以 string_view 异构查找——不构造
    /// 临时 string，noexcept 保证真实成立（无分配即无 bad_alloc 抛出面）。
    std::map<std::string, CodeDescriptor, std::less<>> m_codes;
    /// titleKey/detailKey → code 反查（§4.5 注册期"titleKey/detailKey 唯一"
    /// 校验面——NFR-MNT-03 文案单一权威）。运行期不再使用（无写入）。
    std::map<std::string, std::string> m_textKeys;
    /// 运行期标志（seal 后置位——注册/废弃路径的 Usage 拒绝判据）。
    bool m_sealed = false;
};

// =====================================================================
// 内置码表（§4.6 阶段 A 收编清单——87 码全量：PRJ 10/RT 14/POLICY 23/
// EVI 7/EX 18/RPT 8/DIAG 7；码值不重排各卡已登记建议值——dtb WP-09-T03）
// =====================================================================

/**
 * @brief 返回 §4.6 内置码表全量描述符（87 项；行序＝§4.6 表行序，行内＝
 *        收编清单原文序——非字典序，manifest 排序由 manifest() 承担）。
 *
 * 纯函数（每次调用返回新值——表数据编译期固定，登记值逐码注释见
 * src/DiagCodes.cpp：分类/严重按 §4.6 压缩串机械展开＋§4.3/§4.4/收编卡
 * 交叉锚点，全部消歧点已在实现侧登记）。L5 装配经 registerBuiltinCodes()
 * 消费；测试直接断言本表与注册表行为。
 */
std::vector<CodeDescriptor> builtinCodeDescriptors();

/**
 * @brief 将 §4.6 内置码表全量注册进注册表（L5 装配清单的承载）。
 *
 * 前置：registry 未 seal 且为空/不含与内置表冲突的码（任何冲突＝
 * DuplicateCode 中止——内置表是装配清单权威，混入同码他义描述符属装配
 * 错误，fail-fast 不跳过）。后置：87 码全部可 find；manifest 反映全表。
 *
 * @param registry [in,out] 目标注册表（调用方持有——本函数不接管）
 * @throws DiagnosticsError 逐条注册的注册期验证错误（含 DuplicateCode——
 *         与既有登记冲突时即抛，不静默跳过：NFR-MNT-03 边界拒绝）
 */
void registerBuiltinCodes(IDiagnosticRegistry& registry);

}  // namespace sdurws::ird::diagnostics

#endif  // SDURWS_IRD_DIAGNOSTICS_DIAGCODES_HPP
