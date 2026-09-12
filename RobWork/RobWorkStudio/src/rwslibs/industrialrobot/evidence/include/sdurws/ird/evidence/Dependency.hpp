/**
 * @file   Dependency.hpp
 * @brief  依赖表达模型——DependencyKey/Kind、依赖声明、适用条件、切片条目
 *         载荷与声明闭包校验器（evidence 失效精准的基石）。
 *
 * 设计依据：
 *   - units/evidence.md §4.2.1（依赖表达模型——七类 Kind、对象级粒度＋
 *     语义角色键 D-03、DependencyKey 语法 [a-z][a-z0-9.-]{2,63}）、
 *     §4.2.2（InputSlice.entries 的 (kind,key) 字典序稳定存储＋≥1 约束）、
 *     §4.2.3（声明—解析—冻结协议：①注册期闭包校验/②冻结期条目解析；
 *     D-10 条件输入未入切片的声明在注册期即被拒绝）、§3.1 组成表
 *     （Dependency.hpp｜DependencyKey/Kind、DependencyDeclaration、
 *     ApplicabilityCondition、DependencyEntry、UpstreamResultRef）
 *   - 需求 CON-05（切片内容身份＝缓存键与失效判据——本头类型即"实际
 *     消费依赖"的承载，切片只含被声明条目，AT-05 失效矩阵的实现基础）、
 *     CON-06（策略/名称映射内容身份进入切片——Policy/NameMap 载荷）、
 *     NFR-COR-02（校验结果确定性——issue 顺序稳定）
 *   - 任务契约 tasks/foundation/EV-T02.json（≙WP-05-T02）acceptance 1/3：
 *     声明闭包校验用例＋Dependency 条目语法与闭包校验器正反例
 *
 * 背景说明（本头在失效链上的位置，为什么"声明"值得整个校验器）：
 *   缓存与当前性按切片条目级内容身份判定（CON-05），而切片只含被声明的
 *   条目——声明遗漏＝该输入永不失效（错误缓存复用）。evidence 不懂对象
 *   语义（N-5），无法替域找漏声明，只能机械化两条防线：①注册期闭包规则
 *   （条件依赖的 referencedKeys 必须已在声明集∪快照事实键内——条件翻转
 *   必然改变切片身份）；②冻结期条目语法校验（载荷与 Kind 严格匹配）。
 *   本头交付这两条防线的纯函数校验器；抛错时机归消费方（注册拒绝＝
 *   EvaluatorRegistry〔EV-T10〕，冻结拒绝＝SliceBuilder〔EV-T04〕）。
 *
 * 实现形态说明：契约产物列为"Dependency.hpp"（无 .cpp）——本头
 * header-only，校验器为 inline 纯函数（无隐藏状态，线程安全＝可重入；
 * NFR-COR-02）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   ObjectId/RunId（core.md §4.1/§5.1，core Identity.hpp）、
 *   ContentVersion/ContentIdentity（core.md §4.2/§5.2，core Digest.hpp）。
 */

#ifndef SDURWS_IRD_EVIDENCE_DEPENDENCY_HPP
#define SDURWS_IRD_EVIDENCE_DEPENDENCY_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 键语法与 Kind 词表（§4.2.1 表——词表归各域评估器声明，evidence 只管语法）
// =====================================================================

/**
 * @brief 依赖键语法校验（§4.2.1 原文语法：[a-z][a-z0-9.-]{2,63}）。
 *
 * @param key [in] 待检依赖键（语义角色 token，如 "model.robot-design"、
 *            "task-points"、"collision-models"、"catalog.motor-capability"
 *            ——§4.2.1 原文示例）
 * @return 语法合法 true；空串/超长/首字符非小写字母/出现词表外字符 false
 *
 * 语义说明：键＝消费用途的语义角色 token，词表归各域评估器注册时登记
 * （跨域唯一性由 EvaluatorRegistry 校验——EV-T10），本函数只做语法闸门；
 * 同一语法在声明、条目、上游引用三处复用，保证编码安全（§5.2 字符串规则
 * 的构造入口校验——词表字符集天然无 NUL/空白）。
 *
 * 确定性：纯函数、无 locale 依赖（NFR-COR-02）。
 */
inline bool isValidDependencyKey(std::string_view key) noexcept
{
    // 长度边界：首字符 1 个＋后续 2~63 个 ⇒ 总长 3~64（{2,63} 语义）。
    if (key.size() < 3 || key.size() > 64) {
        return false;
    }
    // 首字符：必须小写字母（[a-z]——大写/数字/连点开头一律拒绝）。
    if (key[0] < 'a' || key[0] > 'z') {
        return false;
    }
    // 后续字符：[a-z0-9.-]；逐字符白名单判定——下划线/空格/斜杠/NUL 等
    // 词表外字符即非法（编码安全前置）。
    for (const char c : key.substr(1)) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '.' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 域内字符串 token 的最小合法性（非空＋无 NUL）。
 *
 * @param tokenText [in] 待检 token（conditionToken/objectTypeToken/
 *                  configKindToken/Environment 的 token 与 valueToken——
 *                  词表归各域登记，evidence 不拥有，只施加编码安全下限：
 *                  §5.2"字符串 UTF-8、禁止 NUL"的构造入口前置）
 * @return 非空且不含 '\\0' true；否则 false
 *
 * 说明：这里刻意**不**施加 DependencyKey 的字符集约束——上述 token 的
 * 词表归域（N-5/N-9 边界），强行套用键语法会把域词表知识搬进 evidence。
 */
inline bool isWellFormedToken(std::string_view tokenText) noexcept
{
    if (tokenText.empty()) {
        return false;
    }
    return tokenText.find('\0') == std::string_view::npos;
}

/// 七类依赖（§4.2.1 Kind 行原文）。枚举声明序即 (kind,key) 字典序中 kind
/// 的权威序（＝dependencyKindToken 表序——canonical 编码的单一序来源，
/// EV-T04 SliceCodec 消费；一经交付不得改动/插入，只能表尾追加并留痕）。
enum class DependencyKind : std::uint8_t {
    Object,         ///< 模型/需求对象（ObjectId＋ContentVersion＋objectTypeToken）
    Configuration,  ///< 求解配置子集（KIN-13：种子/预算/线程进入运行与缓存身份）
    Policy,         ///< 已解析 EngineeringPolicySet 内容身份（CON-06）
    NameMap,        ///< RuntimeNameMap 内容身份（CON-06）
    SampleSet,      ///< 冻结采样计划身份（§4.1.4；KIN-04 分母与样本凭据）
    UpstreamResult, ///< 上游结果切片（跨运行依赖；§8.1 当前性传播消费）
    Environment,    ///< 复现版本要素＋runtime 供给身份要素（CON-04/D-11）
};

/**
 * @brief 取依赖 Kind 的稳定名（"object"/"configuration"/…，枚举声明序）。
 *
 * @param kind [in] 依赖 Kind（全 7 值均有名——全函数）
 * @return 稳定名（kebab 形态；静态存储期）
 *
 * 用途：诊断消息与（EV-T04）canonical 编码中 Kind 的稳定承载；命名表与
 * 枚举声明序一一同表钉住（实现侧词表，登记单元卡 v0.3 变更记录——
 * 设计文本未逐一给出 Kind 名，本表按 Kind 行原文 kebab 化，不新增语义）。
 */
inline std::string_view dependencyKindToken(DependencyKind kind) noexcept
{
    switch (kind) {
    case DependencyKind::Object:         return "object";
    case DependencyKind::Configuration:  return "configuration";
    case DependencyKind::Policy:         return "policy";
    case DependencyKind::NameMap:        return "name-map";
    case DependencyKind::SampleSet:      return "sample-set";
    case DependencyKind::UpstreamResult: return "upstream-result";
    case DependencyKind::Environment:    return "environment";
    }
    return {};   // 不可达（switch 全枚举无 default——遗漏新值编译器告警）
}

/// 必需性（§4.2.1 DependencyDeclaration 行原文）：Required＝无条件必消费；
/// Conditional＝按适用条件解析（冻结期记录 applied/notAppliedReason）。
enum class DependencyRequiredness : std::uint8_t {
    Required,    ///< 无条件必消费（声明不得携带适用条件）
    Conditional, ///< 条件依赖（必须携带 ApplicabilityCondition——D-10）
};

// =====================================================================
// 声明侧值类型（评估器 descriptor.inputs 的组成，§4.2.1/§9.2）
// =====================================================================

/**
 * @brief 条件依赖的适用条件（§4.2.1 ApplicabilityCondition 行）。
 *
 * 语义：conditionToken 归域登记（如"策略启用碰撞"）——evidence 不解释
 * 条件内容；referencedKeys＝**决定该条件的依赖键**——闭包校验对象
 * （§4.2.3①：⊆ 同一 descriptor 已声明键 ∪ 快照事实键，防漏声明）。
 * 条件输入变化 ⇒ 其内容身份变化 ⇒ sliceId 变化 ⇒ 条件重新解析（D-10）。
 *
 * 值语义；线程安全：纯值（构造后按不可变对待）。
 */
struct ApplicabilityCondition {
    /// 适用条件 token（归域登记；非空＋无 NUL——isWellFormedToken 下限）。
    std::string conditionToken;
    /// 决定该条件的依赖键（每键须语法合法且在声明闭包内——非空集）。
    std::vector<std::string> referencedKeys;

    /// 成员精确等值（C++17 显式逐成员——测试/构建器核对用；身份判定仍以
    /// 内容身份为准——§5.1）。下同，各值类型成对提供 ==/!=。
    bool operator==(const ApplicabilityCondition& o) const
    {
        return conditionToken == o.conditionToken && referencedKeys == o.referencedKeys;
    }
    bool operator!=(const ApplicabilityCondition& o) const { return !(*this == o); }
};

/**
 * @brief 依赖声明（§4.2.1 DependencyDeclaration 行）——评估器 descriptor
 *        注册时提供"本评估消费什么"，evidence 在注册期验证（§4.2.3①）。
 *
 * 声明不带载荷——载荷在冻结期解析为 DependencyEntry（§4.2.3②）；
 * 声明与条目的分离正是"声明什么"与"实际消费什么"的审计链。
 *
 * 值语义；线程安全：纯值。
 */
struct DependencyDeclaration {
    /// 语义角色键（isValidDependencyKey 语法；词表归域，跨域唯一归注册表）。
    std::string key;
    /// 七类依赖之一（声明"以什么形态消费"——决定冻结期载荷形态）。
    DependencyKind kind = DependencyKind::Object;
    /// 必需性：Conditional 必须携带 applicability、Required 不得携带
    /// （二者与适用条件一一配对——矛盾即声明非法，注册期拒绝）。
    DependencyRequiredness requiredness = DependencyRequiredness::Required;
    /// 适用条件（仅 Conditional 携带；nullopt＝无条件）。
    std::optional<ApplicabilityCondition> applicability;
    /// 解析说明（域注释，供人工评审"该键从哪里解析"；非身份载体——
    /// 不进切片条目，无语法约束）。
    std::string resolutionNote;

    /// 成员精确等值（测试/注册表核对用——逐成员含说明文本）。
    bool operator==(const DependencyDeclaration& o) const
    {
        return key == o.key && kind == o.kind && requiredness == o.requiredness
            && applicability == o.applicability && resolutionNote == o.resolutionNote;
    }
    bool operator!=(const DependencyDeclaration& o) const { return !(*this == o); }
};

/**
 * @brief 上游结果依赖引用（§4.2.1 UpstreamResultRef 行）——跨运行依赖
 *        （如优化消费运动学结果），当前性传播消费（§8.1）。
 *
 * 值语义；线程安全：纯值。
 */
struct UpstreamResultRef {
    /// 上游结果的角色键（isValidDependencyKey 语法；如优化引用的运动学结果键）。
    std::string upstreamKey;
    /// 上游结果切片身份（sliceId——上游冻结切片的缓存键级身份，§4.2.2）。
    core::ContentIdentity upstreamSliceId;
    /// 上游运行身份（可选——不限定具体运行时 nullopt，仅按切片身份追溯）。
    std::optional<core::RunId> upstreamRunId;

    /// 成员精确等值（测试/当前性核对用）。
    bool operator==(const UpstreamResultRef& o) const
    {
        return upstreamKey == o.upstreamKey && upstreamSliceId == o.upstreamSliceId
            && upstreamRunId == o.upstreamRunId;
    }
    bool operator!=(const UpstreamResultRef& o) const { return !(*this == o); }
};

// =====================================================================
// 条目侧载荷值类型（§4.2.1 七类 Kind 的载荷行——冻结进切片的实际数据）
// =====================================================================

/// Object 载荷：对象身份＋内容版本＋对象类型 token（模型/任务点/碰撞几何/
/// 负载/摩擦参数/目录锁定引用——各自独立角色键，§4.2.1 Object 行）。
struct ObjectDependencyPayload {
    core::ObjectId objectId;             ///< 逻辑对象身份（跨修订稳定——ARC-04）
    core::ContentVersion contentVersion; ///< 对象内容版本（变则变——CON-01/05）
    std::string objectTypeToken;         ///< 对象类型 token（归域；非空＋无 NUL）

    bool operator==(const ObjectDependencyPayload& o) const
    {
        return objectId == o.objectId && contentVersion == o.contentVersion
            && objectTypeToken == o.objectTypeToken;
    }
    bool operator!=(const ObjectDependencyPayload& o) const { return !(*this == o); }
};

/// Configuration 载荷：求解配置子集（§4.2.1 Configuration 行——IK 初值
/// 策略/数量、迭代上限、容差、去重阈值、采样预算与线程、随机种子；
/// KIN-13：进入运行身份与缓存身份）。字节不透明承载（域 schema 归域）。
struct ConfigurationDependencyPayload {
    std::string configKindToken;              ///< 配置种类 token（归域；非空＋无 NUL）
    std::vector<std::uint8_t> canonicalBytes; ///< 域 canonical 字节（非空——CON-06 非空纪律）
    core::ContentIdentity contentIdentity;    ///< 配置子集内容身份（进 sliceId）

    bool operator==(const ConfigurationDependencyPayload& o) const
    {
        return configKindToken == o.configKindToken && canonicalBytes == o.canonicalBytes
            && contentIdentity == o.contentIdentity;
    }
    bool operator!=(const ConfigurationDependencyPayload& o) const { return !(*this == o); }
};

/// Policy 载荷：已解析 EngineeringPolicySet 内容身份（§4.2.1 Policy 行/
/// CON-06——碰撞启用/阈值/行程上限等一切策略状态都包含在该身份内）。
struct PolicyDependencyPayload {
    core::ContentIdentity policyContentIdentity; ///< 策略内容身份（须 isValid）

    bool operator==(const PolicyDependencyPayload& o) const
    {
        return policyContentIdentity == o.policyContentIdentity;
    }
    bool operator!=(const PolicyDependencyPayload& o) const { return !(*this == o); }
};

/// NameMap 载荷：RuntimeNameMap 内容身份（§4.2.1 NameMap 行/CON-06；
/// evidence 永不解析名称内容——R-4）。
struct NameMapDependencyPayload {
    core::ContentIdentity nameMapContentIdentity; ///< 名称映射内容身份（须 isValid）

    bool operator==(const NameMapDependencyPayload& o) const
    {
        return nameMapContentIdentity == o.nameMapContentIdentity;
    }
    bool operator!=(const NameMapDependencyPayload& o) const { return !(*this == o); }
};

/// SampleSet 载荷：冻结采样计划身份（§4.1.4/§4.2.1 SampleSet 行——
/// 身份对计划参数计算而非对枚举列表；KIN-04 分母凭据）。
struct SampleSetDependencyPayload {
    core::ObjectId regionObjectId;            ///< 工作区域对象身份（REQ-03）
    core::ContentIdentity sampleSetIdentity;  ///< 样本集身份（SHA-256 over 计划参数）

    bool operator==(const SampleSetDependencyPayload& o) const
    {
        return regionObjectId == o.regionObjectId && sampleSetIdentity == o.sampleSetIdentity;
    }
    bool operator!=(const SampleSetDependencyPayload& o) const { return !(*this == o); }
};

/// Environment 载荷：版本/供给身份要素（§4.2.1 Environment 行——产品版本/
/// 评估契约版本/编码器版本/编译器契约版本/碰撞后端版本＋
/// runtime.model-identity/runtime.robwork-baseline；"消费 CanonicalModel 的
/// 评估必填"清单属快照组装协议〔EV-T03〕，条目级只做非空语法）。
/// CON-04"算法/契约版本兼容"与 D-11 升版失效的载体。
struct EnvironmentDependencyPayload {
    std::string token;       ///< 要素名 token（如 "compiler-contract-version"；非空＋无 NUL）
    std::string valueToken;  ///< 要素值 token（版本/身份规范文本；非空＋无 NUL）

    bool operator==(const EnvironmentDependencyPayload& o) const
    {
        return token == o.token && valueToken == o.valueToken;
    }
    bool operator!=(const EnvironmentDependencyPayload& o) const { return !(*this == o); }
};

/// UpstreamResult 载荷＝UpstreamResultRef 本体（§4.2.1 UpstreamResult 行）。
using UpstreamResultDependencyPayload = UpstreamResultRef;

/**
 * @brief 依赖条目载荷（std::variant 承载七类 Kind 载荷）。
 *
 * std::monostate＝未初始化保留态（默认构造可用的代价）——合法条目不得
 * 处于该态，validateDependencyEntries 以 PayloadKindMismatch 拒绝；
 * variant 下标必须与条目 kind 严格对应（kind 载荷合法——§4.2.3①）。
 */
using DependencyPayload = std::variant<
    std::monostate,
    ObjectDependencyPayload,
    ConfigurationDependencyPayload,
    PolicyDependencyPayload,
    NameMapDependencyPayload,
    SampleSetDependencyPayload,
    UpstreamResultDependencyPayload,
    EnvironmentDependencyPayload>;

/**
 * @brief 依赖条目（§4.2.1 DependencyEntry 行）——冻结进切片的实际条目。
 *
 * 与声明的区别：条目带解析结果载荷＋applied 标记；Conditional 条目未
 * 适用时**仍保留在切片中**（带 notAppliedReason）——其条件输入仍在身份里
 * （D-10：条件翻转 ⇒ 条件输入身份变 ⇒ sliceId 变 ⇒ 重新解析，不错误复用）。
 *
 * 值语义（冻结后不可变——由 SliceBuilder〔EV-T04〕的冻结协议保证）；
 * 线程安全：纯值。
 */
struct DependencyEntry {
    /// 语义角色键（与声明同语法——isValidDependencyKey）。
    std::string key;
    /// 七类依赖之一（决定 payload 的合法 alternative）。
    DependencyKind kind = DependencyKind::Object;
    /// 按 kind 的载荷（variant 下标必须与 kind 对应）。
    DependencyPayload payload;
    /// 解析结果：true＝本条目被消费；false＝条件未适用（Conditional 专属）。
    bool applied = true;
    /// 未适用原因（applied==false 必须有值且非空；applied==true 必须无值
    /// ——presence 语义（§5.2 可选值显式编码）不允许"有值空串"噪声）。
    std::optional<std::string> notAppliedReason;

    /// 成员精确等值（测试/SliceBuilder 核对用；身份判定以 codec 为准——§5.1）。
    bool operator==(const DependencyEntry& o) const
    {
        return key == o.key && kind == o.kind && payload == o.payload
            && applied == o.applied && notAppliedReason == o.notAppliedReason;
    }
    bool operator!=(const DependencyEntry& o) const { return !(*this == o); }
};

// =====================================================================
// 校验结果与校验器（§4.2.3①注册期声明闭包＋②冻结期条目语法的纯函数面）
// =====================================================================

/**
 * @brief 校验问题码（声明侧＋条目侧）。顺序＝校验器检查序（确定性输出，
 * NFR-COR-02）；值一经交付不得改动/插入，只能表尾追加并留痕。
 */
enum class DependencyIssueCode : std::uint8_t {
    // ---- 声明侧（validateDependencyDeclarations）----
    DuplicateDeclarationKey,     ///< 同一声明集内依赖键重复（歧义声明）
    DeclarationKeySyntax,        ///< 声明 key 语法非法（[a-z][a-z0-9.-]{2,63}）
    ConditionalMissingCondition, ///< Conditional 声明未携带适用条件（D-10 违约）
    RequiredWithCondition,       ///< Required 声明携带适用条件（必需性矛盾）
    ConditionTokenInvalid,       ///< conditionToken 空/含 NUL（isWellFormedToken）
    ReferencedKeysEmpty,         ///< referencedKeys 空集（条件不由任何键决定——永不重解析）
    ReferencedKeySyntax,         ///< referencedKey 语法非法
    ReferencedKeyNotInClosure,   ///< referencedKey ∉ 已声明键 ∪ 快照事实键（§4.2.3① 闭包规则/D-10 注册期拒绝）
    // ---- 条目侧（validateDependencyEntries）----
    EmptyEntrySet,               ///< 条目集为空（§4.2.2 entries ≥1）
    EntryKeySyntax,              ///< 条目 key 语法非法
    EntrySetNotSorted,           ///< 未按 (kind,key) 字典序稳定存储（§4.2.2）
    DuplicateEntry,              ///< (kind,key) 重复（稳定存储要求唯一）
    PayloadKindMismatch,         ///< 载荷 variant 下标与 kind 不符（或 monostate）
    ObjectPayloadInvalid,        ///< Object 载荷字段非法（空 id/cv/typeToken）
    ConfigurationPayloadInvalid, ///< Configuration 载荷字段非法（空 token/字节/身份）
    PolicyPayloadInvalid,        ///< Policy 载荷身份非法（CON-06 非空）
    NameMapPayloadInvalid,       ///< NameMap 载荷身份非法（CON-06 非空）
    SampleSetPayloadInvalid,     ///< SampleSet 载荷字段非法（空 region/身份）
    UpstreamResultPayloadInvalid,///< UpstreamResult 载荷字段非法（键语法/空 sliceId）
    EnvironmentPayloadInvalid,   ///< Environment 载荷字段非法（空 token/valueToken）
    AppliedReasonInconsistent,   ///< applied/notAppliedReason 配对矛盾（§4.2.1）
};

/**
 * @brief 单条校验问题（可定位：涉事键＋声明/条目下标＋中文说明）。
 *
 * index 语义：涉事声明/条目在输入集中的下标；集合级问题（空集）取
 * npos。issues 顺序＝输入顺序×检查序（确定性——测试逐条断言可用下标）。
 */
struct DependencyIssue {
    /// 集合级问题的 index 保留值（"无涉事下标"）。
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    DependencyIssueCode code; ///< 机器可判别问题码
    std::string key;          ///< 涉事依赖键（集合级问题为空串）
    std::size_t index = npos; ///< 涉事声明/条目下标（集合级＝npos）
    std::string message;      ///< 中文开发诊断（定位字段与原因；供注册拒绝消息聚合）
};

/**
 * @brief 声明闭包校验器（§4.2.3① 注册期规则的纯函数承载）。
 *
 * 规则集（全部来自设计原文，顺序＝检查序）：
 *   1. 声明语法：key 过 isValidDependencyKey；
 *   2. 必需性配对：Conditional ⇒ 必带 applicability；Required ⇒ 必不带
 *      （二者互斥，矛盾即声明语义不可判定）；
 *   3. 条件合法性：conditionToken 非 NUL 非空；referencedKeys 非空、
 *      每键语法合法；
 *   4. 闭包规则（D-10 防线）：referencedKeys ⊆ 同一声明集已声明键 ∪
 *      快照事实键——"条件输入未入切片的声明在注册期即被拒绝"；
 *   5. 键唯一：同一声明集内 key 不得重复（歧义声明）。
 *
 * @param declarations    [in] 待检声明集（同一 evaluator descriptor 的
 *                        inputs——§9.2；调用方持有，本函数不修改）
 * @param snapshotFactKeys [in] 快照事实键（§4.2.3①：policy/nameMap/caseSet
 *                        三类快照恒备事实的角色键；由注册方按当前快照给出
 *                        ——evidence 不拥有其词表，故作参数注入）
 *
 * @return 问题清单（空＝通过；顺序＝输入顺序×检查序，确定性——NFR-COR-02）
 *
 * 复杂度：O(D×(R＋F))，D＝声明数、R＝平均 referencedKeys 数、F＝事实键数
 * （注册期一次性调用，规模无热点）。
 *
 * 线程安全：可重入纯函数（不修改输入、无共享状态）。
 */
inline std::vector<DependencyIssue> validateDependencyDeclarations(
    const std::vector<DependencyDeclaration>& declarations,
    const std::vector<std::string>& snapshotFactKeys)
{
    std::vector<DependencyIssue> issues;
    // 事实键查找：逐声明×逐引用键线性扫描（注册期规模小，免排序拷贝——
    // 保持入参 const 与确定性输出）。
    const auto isFactKey = [&snapshotFactKeys](std::string_view k) {
        for (const auto& f : snapshotFactKeys) {
            if (f == k) { return true; }
        }
        return false;
    };

    // 逐声明检查（下标随 issue 一起返回——注册拒绝消息可就地定位）。
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        const DependencyDeclaration& d = declarations[i];
        // 规则 1：键语法（编码安全＋词表形态闸门）。
        if (!isValidDependencyKey(d.key)) {
            issues.push_back({DependencyIssueCode::DeclarationKeySyntax, d.key, i,
                              "声明 key 语法非法（应为 [a-z][a-z0-9.-]{2,63}）"});
        }
        // 规则 2：必需性与适用条件一一配对（Conditional 承载条件语义，
        // Required 表无条件必消费——交叉组合均使声明语义不可判定）。
        if (d.requiredness == DependencyRequiredness::Conditional
            && !d.applicability.has_value()) {
            issues.push_back({DependencyIssueCode::ConditionalMissingCondition, d.key, i,
                              "Conditional 声明必须携带适用条件（D-10）"});
        }
        if (d.requiredness == DependencyRequiredness::Required
            && d.applicability.has_value()) {
            issues.push_back({DependencyIssueCode::RequiredWithCondition, d.key, i,
                              "Required 声明不得携带适用条件（必需性矛盾）"});
        }
        // 规则 3＋4：条件内容与闭包（仅对携带条件者检查——与规则 2 的
        // issue 并行输出，一次注册可看全全部问题）。
        if (d.applicability.has_value()) {
            const ApplicabilityCondition& c = *d.applicability;
            // 规则 3a：条件 token 非空＋无 NUL（词表归域，语法不越界）。
            if (!isWellFormedToken(c.conditionToken)) {
                issues.push_back({DependencyIssueCode::ConditionTokenInvalid, d.key, i,
                                  "conditionToken 为空或含 NUL（编码安全下限，§5.2）"});
            }
            // 规则 3b：引用键非空集——空集＝条件不由任何依赖决定，
            // 条件翻转将永不触发重解析（错误复用通道，必拒）。
            if (c.referencedKeys.empty()) {
                issues.push_back({DependencyIssueCode::ReferencedKeysEmpty, d.key, i,
                                  "referencedKeys 为空（条件必须有决定键）"});
            }
            for (const auto& rk : c.referencedKeys) {
                // 规则 3c：引用键自身语法合法。
                if (!isValidDependencyKey(rk)) {
                    issues.push_back({DependencyIssueCode::ReferencedKeySyntax, rk, i,
                                      "referencedKey 语法非法（[a-z][a-z0-9.-]{2,63}）"});
                    continue;
                }
                // 规则 4：闭包——引用键必须是同 descriptor 已声明键或快照
                // 事实键。不满足＝条件输入未入切片（D-10 注册期拒绝的
                // 系统性防线：漏声明的条件输入永不失效）。
                bool declared = false;
                for (const auto& d2 : declarations) {
                    if (d2.key == rk) { declared = true; break; }
                }
                if (!declared && !isFactKey(rk)) {
                    issues.push_back({DependencyIssueCode::ReferencedKeyNotInClosure, rk, i,
                                      "referencedKey 不在声明闭包内（须为同 descriptor"
                                      " 已声明键 ∪ 快照事实键——§4.2.3①）"});
                }
            }
        }
    }
    // 规则 5：键唯一（在重复项的下标处报告；每键只报首处重复）。
    for (std::size_t i = 0; i < declarations.size(); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (declarations[j].key == declarations[i].key) {
                issues.push_back({DependencyIssueCode::DuplicateDeclarationKey,
                                  declarations[i].key, i, "声明 key 重复（歧义声明）"});
                break;
            }
        }
    }
    return issues;
}

/**
 * @brief 条目语法校验器（§4.2.3②冻结期条目检查的语法部分＋§4.2.2 存储
 *        不变量的纯函数承载）。
 *
 * 规则集（全部来自设计原文，顺序＝检查序）：
 *   1. 集合非空（§4.2.2 entries ≥1）；
 *   2. 条目键语法（isValidDependencyKey）；
 *   3. 载荷与 kind 严格匹配且字段合法（§4.2.1 七类载荷行——core 身份
 *      类型经 isValid() 判非零保留值，CON-06 非空纪律）；
 *   4. applied/notAppliedReason 配对（未适用必带原因、适用必不带——
 *      presence 语义，§5.2）；
 *   5. (kind,key) 字典序稳定存储且唯一（§4.2.2——kind 序＝枚举声明序，
 *      即 dependencyKindToken 表序，canonical 编码的单一权威序）。
 *
 * @param entries [in] 待检条目集（冻结切片候选；调用方持有，本函数不修改）
 * @return 问题清单（空＝通过；顺序＝输入顺序×检查序，确定性——NFR-COR-02）
 *
 * 边界说明：条目对快照 objectClosure 的子集校验、Conditional 条目与声明
 * 的逐条对账属冻结协议语义（§4.2.3②），需快照上下文——归 SliceBuilder
 * （EV-T04）；本函数只做无需上下文的语法/结构面。抛错时机同样归
 * SliceBuilder（其任务卡登记冻结拒绝的码面——本头按 §2.1"可恢复查询
 * 路径提供 try* 非抛出变体"只交付非抛出校验器）。
 *
 * 线程安全：可重入纯函数。
 */
inline std::vector<DependencyIssue> validateDependencyEntries(
    const std::vector<DependencyEntry>& entries)
{
    std::vector<DependencyIssue> issues;
    // 规则 1：空集拒绝（§4.2.2"是（≥1）"——无依赖的评估没有失效语义）。
    if (entries.empty()) {
        issues.push_back({DependencyIssueCode::EmptyEntrySet, {}, DependencyIssue::npos,
                          "条目集为空（§4.2.2 entries 至少 1 条）"});
        return issues;
    }

    // 逐条目检查（载荷按 kind 分派——variant 下标必须与 kind 对应）。
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const DependencyEntry& e = entries[i];
        // 规则 2：键语法（与声明同闸门）。
        if (!isValidDependencyKey(e.key)) {
            issues.push_back({DependencyIssueCode::EntryKeySyntax, e.key, i,
                              "条目 key 语法非法（应为 [a-z][a-z0-9.-]{2,63}）"});
        }
        // 规则 3：kind 载荷合法（下标匹配＋字段级非空）；每类一个 case，
        // variant 未持有对应 alternative 即载荷错配（冻结期必须拒绝——
        // 错配载荷进 canonical 编码会产生错误身份）。
        const auto kindMismatch = [&]() {
            issues.push_back({DependencyIssueCode::PayloadKindMismatch, e.key, i,
                              "载荷与 kind 不符（variant 未持有该 kind 的载荷）"});
        };
        switch (e.kind) {
        case DependencyKind::Object: {
            if (!std::holds_alternative<ObjectDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<ObjectDependencyPayload>(e.payload);
            // Object 三元组全非空：身份/版本非零保留值＋类型 token 合法。
            if (!p.objectId.isValid() || !p.contentVersion.isValid()
                || !isWellFormedToken(p.objectTypeToken)) {
                issues.push_back({DependencyIssueCode::ObjectPayloadInvalid, e.key, i,
                                  "Object 载荷非法（objectId/contentVersion 须非零、"
                                  "objectTypeToken 须非空且无 NUL）"});
            }
            break;
        }
        case DependencyKind::Configuration: {
            if (!std::holds_alternative<ConfigurationDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<ConfigurationDependencyPayload>(e.payload);
            // 配置子集：种类 token 合法＋canonical 字节非空（空配置子集
            // 会让"配置进入身份"退化为占位条目——CON-06 非空纪律）＋身份非零。
            if (!isWellFormedToken(p.configKindToken) || p.canonicalBytes.empty()
                || !p.contentIdentity.isValid()) {
                issues.push_back({DependencyIssueCode::ConfigurationPayloadInvalid, e.key, i,
                                  "Configuration 载荷非法（configKindToken 非空、canonicalBytes"
                                  " 非空、contentIdentity 须非零）"});
            }
            break;
        }
        case DependencyKind::Policy: {
            if (!std::holds_alternative<PolicyDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<PolicyDependencyPayload>(e.payload);
            // 策略身份非零（CON-06：空策略身份＝策略未进切片＝错误复用通道）。
            if (!p.policyContentIdentity.isValid()) {
                issues.push_back({DependencyIssueCode::PolicyPayloadInvalid, e.key, i,
                                  "Policy 载荷非法（policyContentIdentity 须非零——CON-06）"});
            }
            break;
        }
        case DependencyKind::NameMap: {
            if (!std::holds_alternative<NameMapDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<NameMapDependencyPayload>(e.payload);
            if (!p.nameMapContentIdentity.isValid()) {
                issues.push_back({DependencyIssueCode::NameMapPayloadInvalid, e.key, i,
                                  "NameMap 载荷非法（nameMapContentIdentity 须非零——CON-06）"});
            }
            break;
        }
        case DependencyKind::SampleSet: {
            if (!std::holds_alternative<SampleSetDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<SampleSetDependencyPayload>(e.payload);
            // 区域对象＋样本集身份双非空（§4.1.4：身份对计划参数计算——
            // 空身份＝无冻结凭据，KIN-04 分母来源失效）。
            if (!p.regionObjectId.isValid() || !p.sampleSetIdentity.isValid()) {
                issues.push_back({DependencyIssueCode::SampleSetPayloadInvalid, e.key, i,
                                  "SampleSet 载荷非法（regionObjectId/sampleSetIdentity 须非零）"});
            }
            break;
        }
        case DependencyKind::UpstreamResult: {
            if (!std::holds_alternative<UpstreamResultDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<UpstreamResultDependencyPayload>(e.payload);
            // 上游键语法＋切片身份非零＋（若限定运行）运行身份非零。
            if (!isValidDependencyKey(p.upstreamKey) || !p.upstreamSliceId.isValid()
                || (p.upstreamRunId.has_value() && !p.upstreamRunId->isValid())) {
                issues.push_back({DependencyIssueCode::UpstreamResultPayloadInvalid, e.key, i,
                                  "UpstreamResult 载荷非法（upstreamKey 语法、upstreamSliceId"
                                  " 须非零、upstreamRunId 若有须非零）"});
            }
            break;
        }
        case DependencyKind::Environment: {
            if (!std::holds_alternative<EnvironmentDependencyPayload>(e.payload)) { kindMismatch(); break; }
            const auto& p = std::get<EnvironmentDependencyPayload>(e.payload);
            // 要素名/值双非空（必填清单归快照组装协议——EV-T03）。
            if (!isWellFormedToken(p.token) || !isWellFormedToken(p.valueToken)) {
                issues.push_back({DependencyIssueCode::EnvironmentPayloadInvalid, e.key, i,
                                  "Environment 载荷非法（token/valueToken 须非空且无 NUL）"});
            }
            break;
        }
        }
        // 规则 4：applied/notAppliedReason 配对（presence 语义——§5.2
        // "optional 缺失与空值不等价"，故按 has_value 判定而非空串）。
        if (!e.applied
            && (!e.notAppliedReason.has_value() || e.notAppliedReason->empty())) {
            issues.push_back({DependencyIssueCode::AppliedReasonInconsistent, e.key, i,
                              "未适用条目必须携带非空 notAppliedReason（§4.2.1）"});
        }
        if (e.applied && e.notAppliedReason.has_value()) {
            issues.push_back({DependencyIssueCode::AppliedReasonInconsistent, e.key, i,
                              "已应用条目不得携带 notAppliedReason（§4.2.1）"});
        }
    }

    // 规则 5：(kind,key) 字典序稳定存储＋唯一（§4.2.2——稳定存储是
    // canonical 编码确定性的前提：同条目集必得同字节序）。kind 序＝枚举
    // 声明序（dependencyKindToken 表序）；相邻比较即可判全序违例。
    for (std::size_t i = 1; i < entries.size(); ++i) {
        const bool kindLess = entries[i].kind < entries[i - 1].kind;
        const bool sameKindKeyLess = entries[i].kind == entries[i - 1].kind
            && entries[i].key < entries[i - 1].key;
        if (kindLess || sameKindKeyLess) {
            issues.push_back({DependencyIssueCode::EntrySetNotSorted, entries[i].key, i,
                              "条目未按 (kind,key) 字典序存储（§4.2.2 稳定存储）"});
        }
        if (entries[i].kind == entries[i - 1].kind && entries[i].key == entries[i - 1].key) {
            issues.push_back({DependencyIssueCode::DuplicateEntry, entries[i].key, i,
                              "(kind,key) 重复（稳定存储要求唯一）"});
        }
    }
    return issues;
}

/**
 * @brief 声明闭包校验的 fail-fast 轨（注册拒绝原语——§4.2.3①/§9.4 边界）。
 *
 * 语义：issues 非空即抛 EvidenceError(DeclarationInvalid)，消息聚合全部
 * 问题（确定性拼装——注册拒绝可一次看全）；通过则静默返回。抛错时机
 * 归 EvaluatorRegistry（EV-T10）；本函数是其可复用的拒绝原语。
 *
 * @param declarations    [in] 同 validateDependencyDeclarations
 * @param snapshotFactKeys [in] 同 validateDependencyDeclarations
 *
 * @throws EvidenceError（EvidenceErrorCode::DeclarationInvalid）任一声明白含
 *         语法/结构/闭包问题——调用方契约违约，fail-fast（AGENTS 错误语义）
 *
 * 线程安全：可重入纯函数（抛错路径同）。
 */
inline void requireValidDependencyDeclarations(
    const std::vector<DependencyDeclaration>& declarations,
    const std::vector<std::string>& snapshotFactKeys)
{
    const std::vector<DependencyIssue> issues
        = validateDependencyDeclarations(declarations, snapshotFactKeys);
    if (issues.empty()) {
        return;
    }
    // 聚合全部问题为一条消息（"; " 连接，顺序＝issues 确定性顺序；
    // 每条带 [key#下标] 前缀——注册拒绝可就地定位）。
    std::string detail;
    for (const auto& issue : issues) {
        if (!detail.empty()) { detail += "; "; }
        detail += "[" + issue.key + "#" + std::to_string(issue.index) + "] " + issue.message;
    }
    throw EvidenceError(EvidenceErrorCode::DeclarationInvalid, std::move(detail));
}

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_DEPENDENCY_HPP
