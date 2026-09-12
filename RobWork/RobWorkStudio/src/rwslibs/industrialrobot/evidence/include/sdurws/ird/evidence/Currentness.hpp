/**
 * @file   Currentness.hpp
 * @brief  当前性纯投影（ResultCurrentness）——历史结果相对目标上下文是否
 *         仍然有效的派生计算（computeCurrentness 五步算法）＋会话态投影
 *         缓存（CurrentnessIndex，键＝(runId, targetContextId)，永不写回
 *         归档结果）。
 *
 * 设计依据：
 *   - units/evidence.md §8.1（ResultCurrentness 纯投影：输入三要素——结果
 *     引用〔envelope 或其身份摘要〕/目标上下文 CurrentnessTarget/依赖重建
 *     源；计算步骤 1~5；规则冻结表——两持久态＋不可判定为计算形态〔P-EV-4〕/
 *     派生投影不可写回/无默认 Current）、§4.2.4（修改求解预算后的复评——
 *     新 sliceId 旧 inputBaseline、复评不冒充原运行，AT-05 载体场景）、
 *     §10.1 步骤⑥（修订事件→computeCurrentness→CurrentnessIndex→ui，
 *     不写回归档）、§10.4（S2/S7 切面：内容身份判定、上下文匹配拦截）
 *   - 需求 CON-02（三轴正交——当前性是派生投影、历史证据不可变）、
 *     CON-05（切片内容身份＝失效判据；显示名/展示形态不进身份——AT-05
 *     失效矩阵精准性）、TASK-03（跨项目结果不成为另一项目的当前结果）、
 *     任务约束§五.8（不得默认 Current；NotEvaluable 不是第三持久态）
 *   - 任务契约 tasks/foundation/EV-T08.json（≙WP-05-T08）acceptance 1～3：
 *     ①EV-CUR-1~4 内容身份/不可解析/跨上下文/不写回用例；②当前性纯投影、
 *     不写回（断言无持久化副作用）；③AT-05 载体——依赖提示驱动的重算
 *     判定（§4.2.4 修改求解预算后的复评不冒充原运行）用例
 *
 * 背景说明（为什么"当前性"必须是纯投影而不是持久状态）：
 *   一份已归档的评估结果（ResultEnvelope）回答的是"当时的输入下算出了
 *   什么"——它是历史证据，永不修改（CON-02/PA-2）。而"这份结果对现在还
 *   有效吗"随上下文演进而变：HEAD 前进、策略修改、配置调整都会让答案
 *   翻转，但归档结果本身一个字节都不该动。因此当前性被设计为**每次相对
 *   目标上下文计算的派生投影**：输入（结果引用＋目标上下文＋依赖重建源）
 *   完全决定输出，无副作用、无默认值——未计算/不可判定的结果消费者不得
 *   当作 Current 使用（任务约束§五.8）。判定依据是内容身份（sliceId，
 *   CON-05）而非修订号：HEAD 前进本身不改变判定，只有被消费条目的内容
 *   变化才触发 Superseded（S7 正例：仅电机成本变更→运动学切片不变→
 *   Current）。
 *
 * P-EV-4 处置（knownPitfalls——呈现口径归 ui，本单元冻结计算形态）：
 *   当前性只有 Current/Superseded **两个持久语义状态**；"无法判定"
 *   （NotEvaluable）是**计算结果形态**（status=空 optional＋诊断记录），
 *   不是第三个持久状态——上游（ARCH §7.6）只定义两态，本头不私增词表。
 *   "不可判定"的呈现（归入结果过期＋原因展示）由 ui 详设定稿（governance
 *   P-EV-4 / ui P-UI-2，open——不阻断本单元：本头交付的是计算面而非呈现面）。
 *
 * 实现口径（单元卡 §12 EV-T08 行登记，语义与设计一致）：
 *   - I-1 结果引用以身份摘要形态承载（ResultRef，§8.1 输入第 1 项"envelope
 *     或其身份摘要"的后者；envelope 经 fromEnvelope 投影取得）。
 *   - I-2 §8.1 输入第 3 项"注册表当前 descriptor"在注册表（EV-T10）落地
 *     前以值传递注入（CurrentnessSources.currentContractVersion＋
 *     declarations＋consumesCanonicalModel——Dependency.hpp snapshotFactKeys
 *     注入同款先例；evidence 不持有注册表实例）。
 *   - I-3 目标切片重建的目标快照（目标上下文的冻结事实面）由请求方按
 *     §10.1 ①~③同流程组装后经 CurrentnessSources.targetSnapshot 注入；
 *     每声明键的当前条目经 ICurrentnessFactSource 解析（§3.3"值传递＋最小
 *     注入接口"同款模式，适配器归 L5 装配或请求方）。目标条目集的校验与
 *     身份计算唯一经 SliceBuilder/SliceCodec（CR-02——本单元不复制任何
 *     身份计算）。**失效判定按条目级对比执行**（§5.1 身份纪律原文"失效
 *     判定用条目级 ContentVersion/内容身份对比，从不用修订号代替"）：
 *     sliceId 全值编码含快照锚（SliceCodec full 的 [32] snapshotId——
 *     EV-T04 布局），若以"结果 sliceId vs 目标重建 sliceId"全值判定，
 *     HEAD 前进（新修订＝新快照锚）会直接出现在判定里，违反 §8.1 步骤 4
 *     "HEAD 前进本身不出现在比较里"的括号注记——故 sliceId 全值相等仅作
 *     Current 捷径，不等时以条目集逐条对比为主判据（锚差异不触发失效）。
 *   - I-4 条目级失效原因需要结果侧切片条目参与比对（§8.1 步骤 4"逐条目
 *     失效原因清单"）；身份摘要形态不携带条目，故 resultSlice 为可选注入
 *     ——在场时输出条目级原因＋条目级 Current 判据（设计主形态，KIN-13
 *     重算提示数据源）；缺席时降级为 sliceId 全值比较（跨修订恒 Superseded
 *     ——保守方向，绝不误 Current），输出单条 SliceContentChanged。
 *   - I-5 InvalidationKind 词表：§8.1 步骤 4 列举值＋EvaluatorContractChanged
 *     （步骤 2 的 evaluator-contract-changed 承载）＋EnvironmentChanged/
 *     SliceContentChanged（原文"…"的两处保守扩展——复现版本要素变化与
 *     条目级差异不可得；登记单元卡 v0.9）。
 *   - I-7 评估键并入上下文匹配（§8.1 规则表行 1：当前性相对 (project,
 *     branch, evaluationKey, 当前配置内容身份) 计算——评估键是上下文四
 *     要素之一）：结果评估键与目标评估键不一致 ⇒ 不可判定{cross-context}
 *     （跨评估键查询无语义——步骤 1 原文"分支/方案匹配"是 task 五元组
 *     携带面的执行注记，四要素定义以规则表为权威）。
 *   - I-8 判据分层：sliceId 捷径（全值相等 ⇒ Current）→ 条目级对比
 *     （主判据——Current ⇔ 条目集逐条相等）→ 身份摘要降级（条目不可得时
 *     全值比较，保守 Superseded）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   TaskIdentity（core Identity.hpp——上下文匹配取 project/branch 两字段）、
 *   DiagnosticRecord（core DiagData.hpp——不可判定诊断承载，make() 工厂
 *   构造）、RunId（CurrentnessIndex 缓存键半边）。
 *
 * 线程安全：computeCurrentness 为可重入纯函数（全部入参 const 引用，无
 * 共享可变状态）；CurrentnessIndex 为会话态可变缓存——内部以互斥量保护
 * （修订事件线程 invalidate 与查询线程 find/store 并发），单次调用原子。
 */

#ifndef SDURWS_IRD_EVIDENCE_CURRENTNESS_HPP
#define SDURWS_IRD_EVIDENCE_CURRENTNESS_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Dependency.hpp>
#include <sdurws/ird/evidence/Envelope.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 建议诊断码（P-PR-6 同模式——**建议值**，码值权威归 diagnostics
// StableCodeRegistry；本单元不承担注册职责，PA-1）
// =====================================================================

/// 不可判定·跨上下文（§8.1 步骤 1 的 cross-context 诊断建议码——
/// TASK-03 会话隔离的观测面；reporting 侧呈现码 RPT-CURRENTNESS-UNEVALUABLE
/// 归 reporting 消费，本码是其 evidence 侧来源之一）。
inline constexpr std::string_view kDiagCurrentnessCrossContext{
    "EVI-CURRENTNESS-CROSS-CONTEXT"};

/// 不可判定·依赖无法解析（§8.1 步骤 3 的 unresolved-dependency 诊断建议码
/// ——EV-CUR-2"不得默认 Current"的观测面）。
inline constexpr std::string_view kDiagCurrentnessUnresolvedDependency{
    "EVI-CURRENTNESS-UNRESOLVED-DEPENDENCY"};

// =====================================================================
// ResultRef——结果引用（§8.1 输入第 1 项：envelope 或其身份摘要）
// =====================================================================

/**
 * @brief 被判定结果的身份摘要（§8.1 输入原文"task/evaluationKey/
 *        contractVersion/sliceId/inputBaselineId"五要素的值承载）。
 *
 * 为什么是"摘要"而不是 envelope 本体：当前性判定只消费身份面（上下文
 * 匹配取 task 五元组的 project/branch；契约匹配取 evaluatorContractVersion；
 * 内容比较取 sliceId），不读载荷/证据/诊断——以摘要承载可让调用方在只有
 * 归档索引（无全量包络）时也能发起判定。ResultRef::fromEnvelope 提供
 * envelope→摘要的投影（§8.1"envelope 或其身份摘要"的承接点）。
 *
 * 值语义；线程安全：纯值（判定全程只读）。
 */
struct ResultRef {
    /// 结果的任务五元组（execution 分配；上下文匹配取 project/branch——
    /// §8.1 步骤 1；revision/run/attempt 仅随五元组完整性携带，不参与判定
    /// ——HEAD 修订号不参与当前性判定，EV-CUR-1 观测点）。
    core::TaskIdentity task;
    /// 产生结果的评估器键（isValidEvaluationKey 词形；随结果切片编码进
    /// sliceId——目标重建按 CurrentnessTarget.evaluationKey 的 descriptor
    /// 展开，键不一致的差异经 sliceId 比较自然体现，§8.1 步骤 4）。
    std::string evaluationKey;
    /// 产生结果时的评估器契约版本（无量纲；§8.1 步骤 2 契约匹配的左操作数）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 结果切片内容身份（CON-05 缓存键级身份；§8.1 步骤 4 比较的主键）。
    core::ContentIdentity sliceId;
    /// 结果的输入基准身份（D-04 双层身份的基准层；§4.2.4 复评场景中
    /// "新旧结果同属一次冻结输入研究"的凭据——AT-05 载体用例观测点）。
    core::ContentIdentity inputBaselineId;

    /**
     * @brief envelope→身份摘要投影（§8.1 输入第 1 项"envelope 或其身份
     *        摘要"的形态转换——只拷贝五个身份字段，不触碰载荷/证据）。
     *
     * @param envelope [in] 归档结果包络（构造边界 make() 产出；调用方持有，
     *                 本函数仅调用期使用）
     * @return 身份摘要（纯投影——与 envelope 无任何后续关联）
     *
     * 线程安全：可重入纯函数。
     */
    static ResultRef fromEnvelope(const ResultEnvelope& envelope);

    bool operator==(const ResultRef& o) const
    {
        return task == o.task && evaluationKey == o.evaluationKey
            && evaluatorContractVersion == o.evaluatorContractVersion
            && sliceId == o.sliceId && inputBaselineId == o.inputBaselineId;
    }
    bool operator!=(const ResultRef& o) const { return !(*this == o); }
};

// =====================================================================
// CurrentnessTarget——目标上下文（§8.1 输入第 2 项）
// =====================================================================

/**
 * @brief 当前性判定的目标上下文（§8.1 CurrentnessTarget 原文：{projectId,
 *        branchId, scheme?, evaluationKey, 当前 AnalysisConfiguration 内容
 *        身份}）。
 *
 * "当前性相对什么计算"的规则冻结（§8.1 规则表行 1）：(project, branch,
 * evaluationKey, 当前配置内容身份)——方案/配置是上下文的一部分；切换界面/
 * 方案视图**不改变**历史 payload，只改变"哪个上下文的当前性被查询"。
 *
 * 实现口径 I-6（登记单元卡 v0.9）：scheme 在 core 契约中无独立 Id 类型
 * （core Identity.hpp 六 Id128 无 SchemeId；分支即方案分支——ARCH §3.5
 * A2 处置），以可选字符串承载、仅随 evaluatedAgainst 记录面输出（供消费
 * 方核对查询语境）；上下文匹配谓词按 §8.1 步骤 1 原文取 project ∧ branch
 * （结果侧 ResultRef.task 无 scheme 承载面——设计未定义其来源，不私加
 * 匹配语义）。
 *
 * 值语义；线程安全：纯值。
 */
struct CurrentnessTarget {
    core::ProjectId projectId;   ///< 目标项目（须 isValid；步骤 1 匹配面）
    core::BranchId branchId;     ///< 目标方案分支（须 isValid；步骤 1 匹配面）
    /// 方案标识（可选记录面——实现口径 I-6；core 无 SchemeId 类型，不私造）
    std::optional<std::string> scheme;
    /// 目标查询的评估器键（isValidEvaluationKey 词形；决定目标重建使用
    /// 哪个 descriptor 的依赖声明——与 sources.declarations 的所属关系
    /// 由调用方保证，evidence 不持有注册表无从核对〔I-2 值传递边界〕）。
    std::string evaluationKey;
    /// 目标上下文的当前 AnalysisConfiguration 内容身份（§8.1 原文第五要素
    /// ——配置是上下文一部分的承载；目标切片的 Configuration 条目身份
    /// 由事实源按当前配置解析，本字段进入 evaluatedAgainst 记录面）。
    core::ContentIdentity currentConfigIdentity;

    bool operator==(const CurrentnessTarget& o) const
    {
        return projectId == o.projectId && branchId == o.branchId
            && scheme == o.scheme && evaluationKey == o.evaluationKey
            && currentConfigIdentity == o.currentConfigIdentity;
    }
    bool operator!=(const CurrentnessTarget& o) const { return !(*this == o); }
};

// =====================================================================
// 依赖重建源（§8.1 输入第 3 项——§3.3"值传递＋最小注入接口"同款模式）
// =====================================================================

/**
 * @brief 目标上下文依赖事实源（§8.1 步骤 3"目标切片重建"的解析面——
 *        按声明逐键提供目标上下文的当前条目）。
 *
 * 语义：对每个依赖声明回答"该依赖在目标上下文的当前值"——对象键回答
 * 当前 (objectId, contentVersion, typeToken)、配置键回答当前配置子集、
 * 策略/名称映射键回答当前内容身份……解析语义归实现方（适配 project/
 * policy/runtime 的读取能力，适配器归 L5 装配或请求方——§3.3 注入边界
 * 同源）；evidence 不解释键与载荷的域含义（N-5）。
 *
 * @return 解析成功返回冻结形态的 DependencyEntry（applied/notAppliedReason
 *         的条件语义由实现方按目标上下文重判定——条件翻转 ConditionFlipped
 *         因此在重建中自然出现）；返回 nullopt＝该依赖在目标上下文**无法
 *         解析**（对象缺失/资源缺失/工况对象不存在——§8.1 步骤 3 的
 *         NotEvaluable{unresolved-dependency} 触发面，EV-CUR-2；不抛异常
 *         ——可恢复查询路径，§2.1）。
 *
 * 一致性契约：实现方返回的 Object 条目 (oid,cv) 必须落在 targetSnapshot
 * 的 objectClosure 内（同一目标上下文事实面——否则 SliceBuilder 子集校验
 * 拒绝，computeCurrentness 转译为不可判定＋诊断，见其注释）。
 *
 * 生命周期：调用方持有并保证 computeCurrentness 调用期间存活；本单元不
 * 接管所有权。线程约束：实现方自行保证（当前性查询按调用方并发模型串行
 * 或实现方内部同步）。
 */
class ICurrentnessFactSource {
public:
    virtual ~ICurrentnessFactSource() = default;
    virtual std::optional<DependencyEntry>
    tryCurrentEntry(const DependencyDeclaration& declaration) const = 0;
};

/**
 * @brief 依赖重建源集合（§8.1 输入第 3 项的值传递承载＋I-2/I-3/I-4 实现
 *        口径的注入面）。
 *
 * 指针成员为借用引用（调用方持有并保证 computeCurrentness 调用期间存活，
 * 本单元不接管所有权——§3.3 注入边界同款约定）；其余成员按值注入。
 * 值语义（指针浅拷贝——生命周期由调用方保证）；线程安全：纯值（指向的
 * 事实面对象的并发安全性由其实现方保证）。
 */
struct CurrentnessSources {
    /// 目标上下文当前快照（§10.1 ①~③同流程组装的冻结事实面——目标切片
    /// 重建经 SliceBuilder::build(*targetSnapshot) 完成，Object 条目对它的
    /// objectClosure 做子集校验；null＝调用方契约违约，fail-fast）。
    const AnalysisSnapshot* targetSnapshot = nullptr;
    /// 声明→当前条目解析源（null＝调用方契约违约，fail-fast）。
    const ICurrentnessFactSource* facts = nullptr;
    /// 注册表当前 descriptor 的契约版本（I-2 值注入——§8.1 步骤 2 契约
    /// 匹配的右操作数；EV-T10 注册表落地前的承载形态）。
    std::uint32_t currentContractVersion = 0;
    /// 注册表当前 descriptor 的依赖声明集（I-2 值注入——§8.1 步骤 3"以
    /// 同一 descriptor 的依赖声明"重建；空集＝无效 descriptor 输入，
    /// fail-fast——无依赖的评估没有失效语义，§4.2.2 条目 ≥1 同源口径）。
    std::vector<DependencyDeclaration> declarations;
    /// 当前 descriptor 是否消费 CanonicalModel（CR-05 必填开关的值传递
    /// ——透传给目标重建的 SliceBuilder::setConsumesCanonicalModel）。
    bool consumesCanonicalModel = false;
    /// 产生结果的冻结切片（可选——I-4：在场时输出条目级失效原因〔§8.1
    /// 步骤 4 设计主形态〕；null＝仅身份级判定，Superseded 时输出单条
    /// SliceContentChanged 整体原因）。切片条目与 ResultRef.sliceId 的
    /// 对应关系由调用方保证（evidence 不重算核对——切片不可变，重算
    /// sliceId 属冗余成本）。
    const InputSlice* resultSlice = nullptr;
};

// =====================================================================
// 判定输出（§8.1 步骤 5：CurrentnessResult 四字段）
// =====================================================================

/// 当前性持久语义状态（§8.1 规则表行 2：**仅此两值**——ARCH §7.6 词表；
/// "无法判定"不是第三态而是 status=空＋诊断的计算形态，P-EV-4）。
enum class CurrentnessStatus : std::uint8_t {
    Current,   ///< 结果切片与目标重建切片内容身份相等（HEAD 前进本身不改变判定）
    Superseded, ///< 结果已相对目标上下文过期（附逐条目失效原因清单）
};

/// 不可判定原因（§8.1 步骤 1/3 大括号标注的两种 NotEvaluable 形态——
/// status==nullopt 时必有值；呈现口径 P-EV-4 归 ui）。
enum class UnevaluableCause : std::uint8_t {
    CrossContext,        ///< 跨上下文（步骤 1——原项目结果不成为另一项目/会话的当前结果，TASK-03）
    UnresolvedDependency, ///< 依赖无法解析（步骤 3——对象缺失/资源缺失/目标事实冻结失败；不得默认 Current）
};

/// 失效原因类别（§8.1 步骤 4 词表——KIN-13 重算提示的数据源；枚举序＝
/// 设计步骤出现序：步骤 2 契约变化先于步骤 4 条目级差异，兜底值殿后）。
enum class InvalidationKind : std::uint8_t {
    EvaluatorContractChanged,  ///< 评估器契约版本变化（步骤 2 的 evaluator-contract-changed）
    ObjectContentChanged,      ///< 对象内容变化（旧 cv→新 cv——detail 携带两版规范文本）
    ConfigurationChanged,      ///< 求解配置子集变化（KIN-13：改预算→新 sliceId→重算——§4.2.4）
    PolicyChanged,             ///< 策略内容身份变化（CON-06）
    NameMapChanged,            ///< 名称映射内容身份变化（CON-06）
    SampleBaselineChanged,     ///< 冻结采样基准变化（采样预算/种子变＝新一轮研究基准）
    ConditionFlipped,          ///< 条件依赖适用性翻转（D-10：applied/notAppliedReason 变化）
    UpstreamResultChanged,     ///< 上游结果切片身份变化（跨运行依赖，S3 切面）
    EnvironmentChanged,        ///< 环境版本/供给要素变化（原文"…"扩展 I-5——复现版本要素，D-11）
    SliceContentChanged,       ///< 切片整体内容变化且条目级差异不可得（原文"…"扩展 I-5——身份摘要形态兜底）
};

/**
 * @brief 单条失效原因（§8.1 步骤 4 原文 {dependencyKey, kind}＋detail）。
 *
 * detail 携带旧→新差异的可读摘要（如 Object 条目的两版 contentVersion
 * 规范文本）——KIN-13"新旧结果不可直接比较"提示的数据源；确定性输出
 * （同差异必同文本，NFR-COR-02）。
 *
 * 值语义；线程安全：纯值。
 */
struct InvalidationReason {
    /// 涉事依赖键（条目级差异＝该条目的语义角色键；契约版本变化与身份级
    /// 兜底为空串——无具体涉事条目）。
    std::string dependencyKey;
    /// 失效类别（词表见 InvalidationKind 注释）。
    InvalidationKind kind = InvalidationKind::SliceContentChanged;
    /// 旧→新差异摘要（人读中文＋规范身份文本；确定性——同差异同文本）。
    std::string detail;

    bool operator==(const InvalidationReason& o) const
    {
        return dependencyKey == o.dependencyKey && kind == o.kind
            && detail == o.detail;
    }
    bool operator!=(const InvalidationReason& o) const { return !(*this == o); }
};

/**
 * @brief 当前性判定结果（§8.1 步骤 5 原文四字段）。
 *
 * 消费纪律（§8.1 规则表行 4"默认值"）：status==nullopt（不可判定）时
 * 消费者**不得当作 Current 使用**——无默认 Current（任务约束§五.8）。
 * reasons 仅在 status==Superseded 时非空（Current 恒空）；diagnostics
 * 仅在 status==nullopt 时非空（两持久态不产诊断——§8.1 步骤 1 原文
 * "不产生任何持久状态，仅诊断"指不可判定形态本身不落持久层，本结构
 * 全体都是会话内计算值）。
 *
 * 本结构是**派生投影**（CON-02 三轴正交表"当前性"行）：存入
 * CurrentnessIndex 即会话态缓存，永不写回归档结果（EV-CUR-3）。
 *
 * 值语义；线程安全：纯值（冻结后按只读对待）。
 */
struct CurrentnessResult {
    /// 判定状态：nullopt＝不可判定（NotEvaluable——P-EV-4 计算形态，
    /// 非第三持久态）；否则 Current/Superseded 两持久语义态之一。
    std::optional<CurrentnessStatus> status;
    /// 不可判定原因（status==nullopt 时必有值；两持久态时必无值——
    /// presence 纪律与 DependencyEntry::notAppliedReason 同源，§5.2）。
    std::optional<UnevaluableCause> unevaluableCause;
    /// 逐条目失效原因清单（Superseded 时按 (kind,key) 字典序输出——
    /// 确定性，NFR-COR-02；Current/不可判定恒空）。
    std::vector<InvalidationReason> reasons;
    /// 判定所相对的目标上下文（§8.1"当前性相对什么计算"的记录面——
    /// 输入 CurrentnessTarget 的原样快照；消费者据此核对查询语境）。
    CurrentnessTarget evaluatedAgainst;
    /// 不可判定诊断（status==nullopt 时非空——逐条说明无法解析的依赖键
    /// 或跨上下文事实；建议码见 kDiagCurrentness* 常量；make() 工厂构造，
    /// C-3 校验通过）。
    std::vector<core::DiagnosticRecord> diagnostics;

    bool operator==(const CurrentnessResult& o) const
    {
        return status == o.status && unevaluableCause == o.unevaluableCause
            && reasons == o.reasons && evaluatedAgainst == o.evaluatedAgainst
            && diagnostics == o.diagnostics;
    }
    bool operator!=(const CurrentnessResult& o) const { return !(*this == o); }
};

// =====================================================================
// computeCurrentness——五步算法（§8.1 计算步骤的逐条承载；纯函数）
// =====================================================================

/**
 * @brief 计算历史结果相对目标上下文的当前性（§8.1 computeCurrentness）。
 *
 * 算法五步（每步的失败去向见 .cpp 实现头注释——步骤序与设计原文一致）：
 *   1 上下文匹配：result.task 的 project/branch 与 target 不符（或评估键
 *     不一致——I-7）→ 不可判定{cross-context}＋诊断（TASK-03——原项目
 *     结果不能成为另一项目/会话的当前结果；EV-CUR-4）；
 *   2 契约匹配：result.evaluatorContractVersion ≠ sources.currentContractVersion
 *     → Superseded{EvaluatorContractChanged}（算法在此短路——契约升级后
 *     旧结果的依赖声明语义已不可靠，条目级比较无意义）；
 *   3 目标切片重建：按 sources.declarations 逐声明经 facts 解析当前条目，
 *     任一无法解析 → 不可判定{unresolved-dependency}＋诊断（**不得默认
 *     Current**，EV-CUR-2）；解析齐全经 SliceBuilder 在 targetSnapshot 上
 *     校验并冻结出目标切片（条目校验与身份计算唯一经 SliceBuilder/
 *     SliceCodec——CR-02）；
 *   4 比较（I-8 判据分层）：result.sliceId == 目标 sliceId（全值捷径）
 *     或条目集逐条相等（主判据——§5.1 身份纪律"从不用修订号"）→
 *     Current（reasons 空）；否则 Superseded＋逐条目失效原因（resultSlice
 *     在场时按 (kind,key) 对齐比对；缺席时单条 SliceContentChanged）；
 *   5 输出 CurrentnessResult（evaluatedAgainst＝输入 target 原样快照）。
 *
 * 纯投影承诺（acceptance 2）：全部入参 const 引用、无任何持久化副作用
 * ——归档包络/快照/切片在调用前后逐字节不变（EV-CUR-3/EV-CUR-4 的
 * "不写回"观测面）；写缓存是调用方的显式动作（CurrentnessIndex::store）。
 *
 * @param result  [in] 结果引用（身份摘要形态——ResultRef::fromEnvelope
 *                投影或手工组装）
 * @param target  [in] 目标上下文（projectId/branchId 须 isValid——不满足
 *                走步骤 1 的跨上下文形态？否：非法目标上下文属调用方组装
 *                错误，见下异常）
 * @param sources [in] 依赖重建源（借用指针与声明集——见结构注释）
 *
 * @return 判定结果（永不返回"默认 Current"——不可判定以 status=空＋
 *         诊断表达，任务约束§五.8）
 *
 * @throws EvidenceError（DeclarationInvalid）sources.declarations 为空集
 *         （无失效语义的 descriptor 输入）或 facts 返回条目的键/Kind 与
 *         声明不符（事实源实现违约）——调用方契约违约，fail-fast；
 *         （SnapshotIncomplete）sources.targetSnapshot/facts 为空指针
 *         （重建事实面未注入）——同上
 *
 * 复杂度：O(D) 解析＋O(E log E) 目标切片冻结排序＋O(E) 条目比对
 * （E＝条目数——修订事件后按需调用，非热点路径）。
 *
 * 线程安全：可重入纯函数（事实面/快照的并发只读安全由其实现方保证）。
 */
CurrentnessResult computeCurrentness(const ResultRef& result,
                                     const CurrentnessTarget& target,
                                     const CurrentnessSources& sources);

// =====================================================================
// CurrentnessIndex——会话态投影缓存（§8.1 规则表行 3）
// =====================================================================

/**
 * @brief 当前性会话缓存（§8.1 规则表行 3 原文：CurrentnessIndex〔会话态
 *        缓存，键＝(runId, targetContextId)〕缓存计算结果；修订事件触发
 *        按需重算；归档结果永不修改）。
 *
 * 职责边界（PA-1/PA-2）：本类只是**内存中的投影缓存**——
 *   - 不持久化：全部状态在进程内存（会话态），无磁盘写入；
 *   - 不写回：缓存值是 CurrentnessResult 拷贝，与归档包络无任何关联
 *     （EV-CUR-3"currentness 仅存在于投影索引"的承载面）；
 *   - 不主动重算：修订事件（RevisionCommitted/DependencyInvalidated——
 *     事件不携带数据，core D-09）到达后由调用方调 invalidateRun()（事件
 *     无数据，无法精确定位受影响条目，故按运行粒度整体失效——保守正确）；
 *     下次 find 未命中即"按需重算"的触发点（调用方重新 computeCurrentness
 *     后 store）。
 *
 * targetContextId：调用方定义的会话内目标上下文标识（如
 * "brn-<32hex>#cid-<64hex>"）——同一运行可对多个目标上下文各有投影；
 * evidence 不解释其内部结构（词表归调用方，O-13/N-5 同口径；空串拒绝
 * ——空串无法与"未指定"区分，presence 噪声）。
 *
 * 线程安全：内部互斥量保护（修订事件线程 invalidate 与查询线程 find/
 * store 可并发）；单次调用原子，无跨调用不变量。
 */
class CurrentnessIndex {
public:
    CurrentnessIndex() = default;

    /**
     * @brief 查询缓存投影。
     *
     * @param runId           [in] 结果运行身份（缓存键半边）
     * @param targetContextId [in] 目标上下文标识（缓存键半边；可空串查询
     *                        ——空串永不命中，因 store 拒绝空串）
     * @return 命中返回缓存结果的拷贝（调用方对副本的修改不影响缓存）；
     *         未命中（含已被 invalidate）返回 nullopt——重算触发点
     *
     * 线程安全：内部加锁（与 store/invalidateRun/clear 互斥）。
     */
    std::optional<CurrentnessResult> find(const core::RunId& runId,
                                          std::string_view targetContextId) const;

    /**
     * @brief 存储判定结果（调用方在 computeCurrentness 返回后显式调用
     *        ——本类不替调用方计算）。
     *
     * @param runId           [in] 结果运行身份（须 isValid——保留值拒绝）
     * @param targetContextId [in] 目标上下文标识（**禁止空串**——presence
     *                        语义，空串＝调用方契约违约）
     * @param result          [in] 判定结果（按值拷贝存储——后续对调用方
     *                        副本/入参的修改不影响缓存内容）
     *
     * @throws EvidenceError（CacheIncompatible）runId 非法或
     *         targetContextId 为空串（缓存键契约违约——fail-fast；码面
     *         复用：缓存键入参非法属缓存面的调用方违约，不新增错误码）
     *
     * 线程安全：内部加锁。
     */
    void store(const core::RunId& runId, std::string_view targetContextId,
               const CurrentnessResult& result);

    /**
     * @brief 按运行失效缓存（修订事件到达后的失效原语——事件不携带数据，
     *        core D-09，无法精确定位，按运行粒度整体失效）。
     *
     * @param runId [in] 收到修订事件的运行身份（该运行的全部目标上下文
     *              投影一并移除；无命中时静默——失效是幂等动作）
     *
     * 线程安全：内部加锁。
     */
    void invalidateRun(const core::RunId& runId);

    /// 全清（上下文关闭/会话重置——§10.1 会话态投影随会话生命周期消亡）。
    /// 线程安全：内部加锁。
    void clear();

private:
    /// 缓存键（(runId, targetContextId)——§8.1 原文键形；string 承接
    /// targetContextId 以获得稳定的字典序与所有权）。
    using Key = std::pair<core::RunId, std::string>;

    mutable std::mutex m_mutex;                 ///< 会话态并发保护（事件线程×查询线程）
    std::map<Key, CurrentnessResult> m_entries; ///< 缓存本体（map 序＝键字典序，遍历确定性）
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_CURRENTNESS_HPP
