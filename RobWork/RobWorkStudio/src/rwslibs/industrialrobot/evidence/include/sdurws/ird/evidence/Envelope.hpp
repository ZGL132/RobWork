/**
 * @file   Envelope.hpp
 * @brief  结果包络（ResultEnvelope）——运行终结时唯一正式结果对象的构造
 *         边界（SA-13）：Draft 组装＋make() 非法组合拒绝＋validateCombination
 *         纯函数校验器（表 3 全组合矩阵的逐格强制）。
 *
 * 设计依据：
 *   - units/evidence.md §7.1（数据结构与合法组合矩阵〔表 3〕、构造边界
 *     "唯一入口 make()、非法组合抛 EvidenceError、无 setter 构造后不可变"、
 *     "分批数据与最终结果对象的区别"）、§7.2（构造校验＝make/validateCombination
 *     纯函数；接纳侧 execution 复用同一校验器）
 *   - 需求 TASK-02（表 3 payload 约束：取消/失败/中断不得含正式结论字段、
 *     不得进入正式报告或可行集）、ERR-01（NotApplicable 只配非 Completed、
 *     显式标记不伪造判定）、CON-04（评估键/契约版本进身份面——缓存兼容）、
 *     EVI-01（表 1：Preview 不产生结果对象）、CON-05/06（身份绑定面）、
 *     RPT-05（正式结论资格的承载对象——资格判定本体在 Verdict.hpp §7.2）
 *   - 跨单元红线 CR-01（traceability/foundation-api-diff.md）：ResultEnvelope
 *     的 outcome/engineeringStatus/mode 三轴**仅消费 core 词表**（core/
 *     Evaluation.hpp——无本地枚举定义；联合契约测试在
 *     sdurws_ird_evidence_contract_test 钉住 token 字面量一致）；
 *     CR-02（摘要算法唯一＝core::ContentDigester——payload 摘要由 make()
 *     计算，本单元不引入第二摘要实现）
 *   - 任务契约 tasks/foundation/EV-T07.json（≙WP-05-T07）acceptance 1～2：
 *     ①表 3 全组合矩阵用例通过（EV-ENV-1/2：非法组合逐一构造拒绝、
 *     Preview 构造边界拒绝）；②CR-01：仅消费 core Evaluation 词表＋
 *     _contract_test 断言 token 字面量与 core::toToken 输出一致
 *
 * 背景说明（为什么"构造边界"是结果可信性的总闸）：
 *   本软件中一切"正式结论"（进入正式报告、可行集、正式缓存）的载体只有
 *   一个——经 make() 构造的 ResultEnvelope。评估运行中途的流式/分批数据
 *   （进度、部分样本）是 execution 通道 DTO，**不是**结果对象；只有运行
 *   终结时经 make() 校验成功的包络才有资格成为正式结果。表 3 合法组合
 *   矩阵是这一闸门的逐格规则：取消/失败/中断的运行只允许以 NotApplicable
 *   显式标记（不伪造工程判定）、只允许携带诊断性数据（partialData 恒不可
 *   复用）；Preview 模式根本不产生结果对象。构造边界＝make() 调用（§7.1：
 *   worker 内产出后、回传接纳前各校验一次；接纳侧 execution 复用同一
 *   校验器——纯函数）。
 *
 * 实现形态说明：值类型为聚合结构（公共成员＋成对 ==/!=，无 setter——
 * EV-T03 AnalysisSnapshot/EV-T04 InputSlice 同款纪律；"构造后不可变"由
 * make() 唯一生产者保证，不使用 const 成员以保全值语义/可赋值性）；
 * 校验器为非抛出纯函数（返回 issue 清单——§2.1 try* 分轨先例），实现集中
 * 在 src/Envelope.cpp。issue 顺序＝检查序（固定），同一坏组装必得同一
 * issue 序列（NFR-COR-02，测试逐条断言可用下标）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   TaskIdentity（core Identity.hpp——五元组绑定）、ContentIdentity/
 *   ContentDigester（core Digest.hpp——payload 摘要唯一实现点，CR-02）、
 *   EvaluationMode/TaskOutcome/EngineeringStatus（core Evaluation.hpp——
 *   CR-01 词表唯一来源，本头零本地枚举定义）、DiagnosticRecord
 *   （core DiagData.hpp——诊断承载）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全；
 * 包络构造后按只读值对待（EV-CUR-3"历史不可改写"的载体面——当前性/
 * 资格都是派生投影，永不回写，§7.2）。
 */

#ifndef SDURWS_IRD_EVIDENCE_ENVELOPE_HPP
#define SDURWS_IRD_EVIDENCE_ENVELOPE_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Slice.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>
#include <sdurws/ird/evidence/Verdict.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 包络伴生值类型（§7.1 结构注释的逐一承载；未定名类型按设计注释原文
// 最小成形——不私加语义，登记单元卡 v0.8）
// =====================================================================

/**
 * @brief 本结果覆盖的工况范围（§7.1 caseScope 字段注释："本结果覆盖的
 *        工况 id 集（⊆ 快照 caseSet；make 校验）"）。
 *
 * "⊆ 快照 caseSet" 的核对需要快照事实面——上下文无关的 make() 只查
 * 结构合法性（Completed 必非空），与快照必验工况集的子集核对在
 * validateCombination 的快照绑定重载执行（§7.2"接纳侧 execution 复用
 * 同一校验器"——接纳时快照在手）。不解释工况内容（域事实，PA-1）。
 *
 * 值语义；线程安全：纯值。
 */
struct CaseScope {
    /// 覆盖的工况 id 集（每 id 必须在来源快照冻结必验工况集内——快照绑定
    /// 重载核对；空集仅在非 Completed 行合法——表 3 行 1"必含工况标识"）。
    std::vector<CaseId> caseIds;

    bool operator==(const CaseScope& o) const { return caseIds == o.caseIds; }
    bool operator!=(const CaseScope& o) const { return !(*this == o); }
};

/**
 * @brief 域结果载荷的组装形态（Draft 侧——摘要尚不可信）。
 *
 * 摘要纪律（§7.1 DomainPayload.digest 注释"evidence 计算摘要"）：摘要
 * 由 make() 对 canonicalBytes 重算（CR-02：唯一经 core::ContentDigester），
 * **不接受调用方申报**——申报值可被伪造，重算值才构成完整性凭据。因此
 * Draft 携带的是无摘要的组装形态，make() 产出带摘要的 DomainPayload。
 *
 * 值语义；线程安全：纯值。
 */
struct DomainPayloadDraft {
    /// 域登记 token（§7.1 注释："如 \"kin.batch-ik.v1\""——词表归域，
    /// evidence 只施加"非空＋无 NUL"编码安全下限，O-13/N-5 同口径）。
    std::string kindToken;
    /// 域 canonical 编码（编码契约归域——evidence 视为不透明字节；
    /// 摘要对它计算）。
    std::vector<std::uint8_t> canonicalBytes;

    bool operator==(const DomainPayloadDraft& o) const
    {
        return kindToken == o.kindToken && canonicalBytes == o.canonicalBytes;
    }
    bool operator!=(const DomainPayloadDraft& o) const { return !(*this == o); }
};

/**
 * @brief 域结果载荷（正式结论载荷，§7.1 DomainPayload——evidence 视为
 *        不透明，只承载与计摘要）。
 *
 * 仅 Completed 合法组合允许携带（表 3 行 3：取消/失败/中断 payload 必须为
 * 空）；digest 由 make() 计算（SHA-256 over canonicalBytes——CR-02），
 * 消费方可重算比对作为完整性凭据（追溯/完整性——§7.1 注释原文）。
 *
 * 值语义；线程安全：纯值（构造后按不可变对待——envelope 不可变纪律）。
 */
struct DomainPayload {
    /// 域登记 token（同 DomainPayloadDraft.kindToken 词形）。
    std::string kindToken;
    /// 域 canonical 编码（不透明字节）。
    std::vector<std::uint8_t> canonicalBytes;
    /// 载荷摘要（SHA-256 over canonicalBytes；make() 计算——非申报值，
    /// CR-02 摘要算法唯一）。
    core::ContentIdentity digest;

    bool operator==(const DomainPayload& o) const
    {
        return kindToken == o.kindToken && canonicalBytes == o.canonicalBytes
            && digest == o.digest;
    }
    bool operator!=(const DomainPayload& o) const { return !(*this == o); }
};

/**
 * @brief 诊断性部分数据引用（§7.1 PartialDataRef——取消/失败/中断时的
 *        部分产物归档提示）。
 *
 * reusable 恒 false（§7.1 注释原文："诊断价值保留，不得正式复用"——
 * EV-ENV-2 的包络侧观测点：部分数据可读诊断，不构成正式缓存候选；
 * 缓存判定面 judgeCacheHit 的 DiagnosticOnly 结论归 §8.2/EV-T09，本头
 * 只保证数据面不可复用标记不被篡改——make() 对 reusable==true 拒绝）。
 * 归档的实际写入归 execution/project（§7.1 注释——PA-1 权威唯一）。
 *
 * 值语义；线程安全：纯值。
 */
struct PartialDataRef {
    /// 归档位置提示（提示性路径——存在性/可达性不归 evidence 核对，
    /// 实际写入与读取归 execution/project）。
    std::filesystem::path archiveHint;
    /// 恒 false：诊断价值保留，不得正式复用（§7.1 原文；构造边界强制）。
    bool reusable = false;

    bool operator==(const PartialDataRef& o) const
    {
        return archiveHint == o.archiveHint && reusable == o.reusable;
    }
    bool operator!=(const PartialDataRef& o) const { return !(*this == o); }
};

/**
 * @brief 证据 Profile 绑定引用（§7.1 profile 字段注释："profileId+version+
 *        contentIdentity"三元组——与 RequiredEvidenceProfile 的注册身份
 *        同一面，§6.1/§8.2 缓存兼容按它比对）。
 *
 * Completed 合法组合须绑定有效三元组（表 3 行 1"必含证据清单与工况
 * 标识"的绑定面）；非 Completed 行不做要求（取消/失败/中断的包络只
 * 保留诊断——绑定面属"正式结论字段"家族，不强制）。词表：profileId
 * 为五域词表（isDomainProfileId），不私裁域外 id（N-5）。
 *
 * 值语义；线程安全：纯值。
 */
struct EvidenceProfileRef {
    /// Profile 域 id（"kin"|"trj"|"dyn"|"sel"|"opt"——isDomainProfileId 词表）。
    std::string profileId;
    /// Profile 版本（非空＋无 NUL）。
    std::string version;
    /// Profile 内容身份（computeProfileContentIdentity 的注册值——非零）。
    core::ContentIdentity contentIdentity;

    bool operator==(const EvidenceProfileRef& o) const
    {
        return profileId == o.profileId && version == o.version
            && contentIdentity == o.contentIdentity;
    }
    bool operator!=(const EvidenceProfileRef& o) const { return !(*this == o); }
};

/// 包络产出位置（§7.1 producer 注释"{producedIn: MainProcess|Worker, …}"
/// 的二值词表——worker 产出与主进程产出在接纳侧同受构造边界校验，§7.1
/// "worker 内产出后、回传接纳前各校验一次"）。
enum class ProducerProcess : std::uint8_t { MainProcess, Worker };

/**
 * @brief 产出者信息（§7.1 ProducerInfo——"{producedIn: MainProcess|Worker,
 *        productVersion, …}"的承载；省略号字段待后续任务按需增补，本头
 *        只承载设计注释点名的两字段，不私加）。
 *
 * 值语义；线程安全：纯值。
 */
struct ProducerInfo {
    /// 产出位置（worker 内产出在回传接纳前同样过构造边界——§7.1 原文）。
    ProducerProcess producedIn = ProducerProcess::MainProcess;
    /// 产品版本串（复现要素家族——非空＋无 NUL 编码安全下限）。
    std::string productVersion;

    bool operator==(const ProducerInfo& o) const
    {
        return producedIn == o.producedIn && productVersion == o.productVersion;
    }
    bool operator!=(const ProducerInfo& o) const { return !(*this == o); }
};

// =====================================================================
// 组合校验（表 3 的纯函数承载——§7.2"make/validateCombination 纯函数"）
// =====================================================================

/// 组装草稿前置声明（校验器签名在其定义之前——完整性约束见下文定义处）。
struct ResultEnvelopeDraft;

/**
 * @brief 包络组合校验问题码（表 3 逐格区分——EV-ENV-1 观测点"错误码逐条
 *        对应表 3"的承载；Errors.hpp EnvelopeIllegalCombination 注释登记的
 *        "逐条区分在 EV-T07 落地"即本枚举）。
 *
 * 异常轨仍用单一稳定错误码 EvidenceErrorCode::EnvelopeIllegalCombination
 * （Errors.hpp 全表设计——异常不细分子码），逐格区分由本枚举＋消息承载：
 * validateCombination 返回的 issue 序列机器可判别，make() 抛出的 detail
 * 文本含字段名（EV-ENV-1 观测点"Draft 校验失败消息含字段"）。
 *
 * 枚举顺序＝检查序（固定）：同一坏组装必报同一首错、必得同一 issue 序列
 * （NFR-COR-02）。值一经交付不得改动/插入，只能表尾追加并留痕（DTB §5.4，
 * EvidenceErrorCode 同款纪律）。
 */
enum class EnvelopeIssueCode : std::uint8_t {
    // ---- 无条件边界（先于组合行核对——表 3 行 4"任意×Preview"）----
    PreviewModeForbidden,       ///< mode==Preview（表 1：Preview 不产生结果对象——EV-ENV-1）
    TaskIdentityInvalid,        ///< task 五元组存在保留值（§7.1 task 注释：make 校验 isValid）
    EvaluationKeyInvalid,       ///< evaluationKey 词形非法（isValidEvaluationKey——D-5 同款词形闸门）
    InputIdentityInvalid,       ///< snapshotId/sliceId/inputBaselineId 存在保留值（绑定面非空）
    ProducerInfoInvalid,        ///< productVersion 空串/含 NUL（编码安全下限）
    PayloadKindTokenInvalid,    ///< payload.kindToken 空串/含 NUL（域 token 编码安全下限——O-13）
    PartialDataReusableFlag,    ///< partialData.reusable==true（§7.1"恒 false"——不得正式复用）
    // ---- Completed 组合行（表 3 行 1＋行 2）----
    CompletedStatusNotApplicable,       ///< Completed×NotApplicable（表 3 行 2——ERR-01）
    CompletedEvidenceOrCaseMissing,     ///< Completed 必含证据清单与工况标识（表 3 行 1：caseScope 空/profile 三元组/清单绑定面非法）
    CompletedManifestNotCoherent,       ///< 清单与包络绑定不一致（manifest.snapshotId/sliceId ≠ 包络同名绑定——§6.2 记录面）
    CompletedEvidenceItemDiscipline,    ///< 清单项 presence 纪律违例（validateEvidenceItems 非空——§6.2）
    DataInsufficientNoMissingItems,     ///< Completed+DataInsufficient 而缺失清单为空（表 3 行 1：空清单→拒绝）
    FeasibleWithMissingItems,           ///< Completed+Feasible 带未解决缺失项（表 3 行 1）
    EngineeringInfeasibleNoBasis,       ///< Completed+EngineeringInfeasible 无有效证明且无 Must 违例记录（表 3 行 1）
    // ---- 非 Completed 组合行（表 3 行 3 及其反面）----
    NonCompletedStatusNotNotApplicable, ///< 取消/失败/中断 配非 NotApplicable（如 Canceled×Feasible——不伪造判定）
    NonCompletedPayloadPresent,         ///< 取消/失败/中断 携带 payload（表 3 行 3：payload 必须为空）
    NonCompletedSatisfiedEvidence,      ///< 取消/失败/中断 的清单含 Satisfied 判定声明（表 3 行 3）
    NonCompletedMissingItemsPresent,    ///< 取消/失败/中断 携带 missingItems（表 3 行 3：不适用）
    // ---- 快照绑定重载专属（需快照/注册表事实面——接纳侧校验）----
    CaseScopeNotInSnapshot,             ///< caseScope 含快照冻结必验工况集外的工况（§7.1 caseScope 注释）
    CompletedManifestBindingInvalid,    ///< 清单对快照绑定校验失败（validateEvidenceManifestBinding——§6.2）
    ProofInvalidAgainstSnapshot,        ///< "有效证明"面不成立：validateProof 对快照/切片核对未通过（表 3 行 1；D-09 存在性≠有效性）
};

/**
 * @brief 单条组合校验问题（可定位：问题码＋含字段名的中文说明）。
 *
 * 值语义；线程安全：纯值。
 */
struct EnvelopeIssue {
    EnvelopeIssueCode code; ///< 机器可判别问题码（表 3 逐格——枚举注释）
    std::string message;    ///< 中文开发诊断（含涉事字段名——EV-ENV-1 观测面）
};

/**
 * @brief 判断诊断记录是否为 Must 违例判定记录（表 3 行 1"或 Must 违例
 *        记录"的识别面）。
 *
 * 词表纪律（CR-01 同源：不私造 token）：Must 违例记录的唯一识别面＝
 * EV-T06 汇总层出具的 kDiagMustViolation 建议码（Verdict.hpp——⑤级判定
 * 记录，码值权威归 diagnostics StableCodeRegistry）；包络构造边界只
 * **识别**该码面，不生产新的违例词表（PA-1）。
 *
 * @param record [in] 待检诊断记录
 * @return 是 Must 违例判定记录 true
 *
 * 线程安全：可重入纯函数。
 */
bool isMustViolationRecord(const core::DiagnosticRecord& record);

/**
 * @brief 上下文无关组合校验（表 3 全部不依赖快照/注册表的格子——make()
 *        的拒绝依据；纯函数、确定性）。
 *
 * 检查序（＝EnvelopeIssueCode 声明序，同坏组装必报同一首错）：
 *   ①无条件边界：Preview 拒绝（表 3 行 4/表 1）→task 五元组→评估键
 *     词形→三身份非零→产出者版本→payload token→partialData 标记；
 *   ②Completed 行：NotApplicable 拒绝（行 2/ERR-01）→证据清单与工况
 *     标识在在（行 1）→清单与包络绑定一致→清单项 presence 纪律→
 *     DataInsufficient 缺失全量清单（空→拒绝）→Feasible 无未解决缺失→
 *     EngineeringInfeasible 有凭据（证明在场或 Must 违例记录——上下文
 *     无关面只查**在场**；"有效"面的 validateProof 核对在快照绑定重载）；
 *   ③非 Completed 行：状态必须 NotApplicable→payload 为空→无 Satisfied
 *     判定声明→无 missingItems（行 3 及其反面——诊断与 partialData 保留）。
 *
 * @param draft [in] 待检组装草稿（调用方持有，本函数不修改）
 * @return 问题清单（空＝上下文无关面全部通过；顺序＝检查序，确定性）
 *
 * 复杂度：O(E)（清单项遍历＋诊断码比对——构造期一次性，非热点）。
 *
 * 线程安全：可重入纯函数。
 */
std::vector<EnvelopeIssue> validateCombination(const ResultEnvelopeDraft& draft);

/**
 * @brief 快照绑定组合校验（表 3 全量面——接纳侧 execution 复用的同一
 *        校验器，§7.1/§7.2；纯函数、确定性）。
 *
 * 在上下文无关面之上追加两个需要事实面的格子：
 *   - caseScope ⊆ 快照冻结必验工况集（§7.1 caseScope 注释——逐条点名
 *     越界工况 id）；
 *   - Completed+EngineeringInfeasible 且凭据为证明时：validateProof 对
 *     （快照, expectedSliceId=draft.sliceId, 产生者注册表）字段级核对
 *     必须通过（表 3 行 1"有效证明（validateProof 通过）"——D-09：
 *     存在性≠有效性；凭据为 Must 违例记录时不需证明）。
 *     上下文无关面已发现问题时本函数直接返回该清单（结构非法的草稿
 *     无绑定核对意义——首错优先，检查序确定）。
 *
 * @param draft     [in] 待检组装草稿（同上）
 * @param snapshot  [in] 来源冻结快照（caseSet 分母与清单绑定的事实面；
 *                  调用方持有，仅调用期使用）
 * @param producers [in] 产生者注册表只读投影（validateProof 的注册查询面
 *                  ——EV-T05 IProducerRegistryView；调用方持有，仅调用期
 *                  使用）
 * @return 问题清单（空＝表 3 全量面通过；顺序＝检查序，确定性）
 *
 * 复杂度：O(E＋C·S＋P)（上下文无关面＋工况集核对〔S＝快照必验集〕＋
 * validateProof——接纳期一次性，非热点）。
 *
 * 线程安全：可重入纯函数（注册表只读查询的并发安全由实现方保证）。
 */
std::vector<EnvelopeIssue> validateCombination(const ResultEnvelopeDraft& draft,
                                               const AnalysisSnapshot& snapshot,
                                               const IProducerRegistryView& producers);

// =====================================================================
// ResultEnvelopeDraft——组装草稿（make() 的唯一入参；可变中间态）
// =====================================================================

/**
 * @brief 结果包络组装草稿（§7.1 make(ResultEnvelopeDraft&&) 的入参面）。
 *
 * 字段与 ResultEnvelope 一一对应（payload 除外：草稿携带 DomainPayloadDraft，
 * make() 重算摘要后产出 DomainPayload——见其结构注释）。草稿是**可变
 * 中间态**（评估器/汇总器逐字段填充），合法性与"不可变"都从 make() 起
 * 算：make() 校验失败抛异常，草稿不产出任何包络。
 *
 * EvaluationKey 承载口径（D-5 同款，登记单元卡 v0.8）：§7.1 原文类型名
 * EvaluationKey 暂以 std::string 承载（isValidEvaluationKey 词形闸门），
 * 类型化归 Evaluator.hpp（EV-T10）——与 InputSlice::evaluationKey、
 * DeterministicInfeasibilityProof::producer 同口径。
 *
 * 值语义（可移动——make() 按 rvalue 消耗）；线程约束：非线程安全的组装
 * 中间态，仅组装线程持有（SnapshotBuilder/SliceBuilder 同源约束）。
 */
struct ResultEnvelopeDraft {
    // —— 身份与绑定（§7.1 注释逐字）——
    /// 任务五元组（execution 分配；make 校验 isValid——§7.1 注释原文）。
    core::TaskIdentity task;
    /// 评估键（isValidEvaluationKey 词形；D-5 承载口径——进缓存身份面，
    /// CON-04）。
    std::string evaluationKey;
    /// 评估器契约版本（无量纲；同 sliceId、不同契约版本＝不同算法契约，
    /// CON-04/EV-CPA-2）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 评估模式（core 词表——CR-01；Preview 被构造边界拒绝，表 1/表 3 行 4）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 来源快照身份（非零——绑定面）。
    core::ContentIdentity snapshotId;
    /// 来源切片身份（非零——缓存键面，CON-05）。
    core::ContentIdentity sliceId;
    /// 输入基准身份（非零——比较基准面，D-04/EVI-02）。
    core::ContentIdentity inputBaselineId;
    /// 本结果覆盖的工况范围（⊆ 快照 caseSet——子集核对在快照绑定重载）。
    CaseScope caseScope;
    /// 证据 Profile 绑定三元组（Completed 行须有效——表 3 行 1）。
    EvidenceProfileRef profile;
    // —— 三轴之执行轴与判定轴（core 词表——CR-01 零本地枚举）——
    /// 任务结果（core 词表：completed/canceled/failed/interrupted）。
    core::TaskOutcome outcome = core::TaskOutcome::Completed;
    /// 工程判定（core 词表：feasible/engineering-infeasible/data-insufficient/
    /// not-applicable；非 Completed 必须为 NotApplicable——ERR-01）。
    core::EngineeringStatus engineeringStatus = core::EngineeringStatus::Feasible;
    // —— 证据（表 3：Completed 必含）——
    /// 证据清单（§6.2——逐项状态＋绑定；Completed 行须绑定一致且项纪律
    /// 合格；非 Completed 行不得含 Satisfied 判定声明——表 3 行 3）。
    EvidenceManifest evidence;
    /// 确定性不可行证明（EngineeringInfeasible 的凭据之一——"有效"面经
    /// validateProof，快照绑定重载核对）。
    std::optional<DeterministicInfeasibilityProof> infeasibilityProof;
    /// 搜索未果记录（DataInsufficient 的搜索未果口径凭据——§6.3 末）。
    std::optional<SearchExhaustedRecord> searchRecord;
    /// 缺失项清单（DataInsufficient 必含**全量**清单——空清单＋
    /// DataInsufficient→拒绝，表 3 行 1；Feasible 必为空）。
    std::vector<MissingItem> missingItems;
    // —— 载荷与诊断 ——
    /// 正式结论载荷（仅 Completed 组合；表 3 行 3：取消/失败/中断必须为空）。
    std::optional<DomainPayloadDraft> payload;
    /// 诊断性部分数据（取消/失败/中断场景——reusable 恒 false）。
    std::optional<PartialDataRef> partialData;
    /// 诊断记录（各组合行均保留——§7.1 行 3"保留诊断"）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 产出者信息（worker/主进程＋产品版本）。
    ProducerInfo producer;
};

// =====================================================================
// ResultEnvelope——正式结果对象（构造后不可变；make() 唯一入口，SA-13）
// =====================================================================

/**
 * @brief 结果包络（§7.1 ResultEnvelope——运行终结时唯一正式结果对象）。
 *
 * 生命周期与不可变纪律：由 make() 一次性校验、计算载荷摘要并返回；此后
 * 无任何修改途径——本结构不提供 setter，调用方按只读值对待（"值语义＋
 * 无 setter；当前性/资格都是派生投影，永不回写归档结果"——§7.2，EV-CUR-3
 * 的载体承诺）。成员为公共字段（值语义、可赋值拷贝——"不可变"由 make()
 * 唯一生产者纪律保证，InputSlice 同款实现口径）。
 *
 * 消费面（§7.2 生命周期表）：五元组核对与归档触发归 execution（RunRegistry）；
 * 磁盘保存归 project（透传字段）；当前性投影归 evidence §8.1（纯计算，
 * EV-T08）；报告资格检查归 reporting（判定函数在 Verdict.hpp §7.2）。
 *
 * 值语义（拷贝即深拷贝）；冻结后只读，可跨线程共享。确定性：同草稿必得
 * 同包络（载荷摘要经 ContentDigester——CR-02；NFR-COR-02）。
 */
struct ResultEnvelope {
    // —— 身份与绑定 ——
    core::TaskIdentity task;                  ///< 任务五元组（make 已校验 isValid）
    std::string evaluationKey;                ///< 评估键（isValidEvaluationKey 词形）
    std::uint32_t evaluatorContractVersion = 0; ///< 评估器契约版本（无量纲）
    core::EvaluationMode mode = core::EvaluationMode::Verified; ///< 评估模式（非 Preview——构造边界）
    core::ContentIdentity snapshotId;         ///< 来源快照身份（非零）
    core::ContentIdentity sliceId;            ///< 来源切片身份（非零——缓存键）
    core::ContentIdentity inputBaselineId;    ///< 输入基准身份（非零——比较基准）
    CaseScope caseScope;                      ///< 覆盖工况范围（Completed 行非空）
    EvidenceProfileRef profile;               ///< Profile 绑定三元组（Completed 行有效）
    // —— 三轴之执行轴与判定轴 ——
    core::TaskOutcome outcome = core::TaskOutcome::Completed;       ///< 任务结果（core 词表）
    core::EngineeringStatus engineeringStatus =
        core::EngineeringStatus::Feasible;    ///< 工程判定（core 词表）
    // —— 证据 ——
    EvidenceManifest evidence;                ///< 证据清单（Completed 必含——表 3）
    std::optional<DeterministicInfeasibilityProof> infeasibilityProof; ///< 不可行证明（可选凭据）
    std::optional<SearchExhaustedRecord> searchRecord; ///< 搜索未果记录（可选凭据）
    std::vector<MissingItem> missingItems;    ///< 缺失项全量清单（DataInsufficient 必含）
    // —— 载荷与诊断 ——
    std::optional<DomainPayload> payload;     ///< 正式结论载荷（仅 Completed；摘要 make 计算）
    std::optional<PartialDataRef> partialData; ///< 诊断性部分数据（reusable 恒 false）
    std::vector<core::DiagnosticRecord> diagnostics; ///< 诊断记录（各组合行保留）
    ProducerInfo producer;                    ///< 产出者信息

    /**
     * @brief 构造边界（SA-13）：唯一入口，非法组合抛 EvidenceError。
     *
     * 校验＝validateCombination(draft) 上下文无关全量面；任一问题即抛
     * EvidenceError(EnvelopeIllegalCombination)，detail 为逐条问题消息
     * 拼接（含字段名——EV-ENV-1"Draft 校验失败消息含字段"观测面；逐格
     * 区分经 EnvelopeIssueCode，可先用 validateCombination 取结构化清单）。
     * 需要快照事实面的格子（工况子集/证明有效性）由接纳侧在 make() 之前
     * 调 validateCombination 快照绑定重载核对（§7.1"接纳侧复用同一校验
     * 器"——构造边界两段式，纯函数）。
     *
     * @param draft [in,out] 组装草稿（rvalue——字段被移动消耗；校验失败时
     *              草稿内容不承诺保留，调用方需要重试应另备副本）
     * @return 正式结果包络（载荷摘要已计算——CR-02；不可变纪律自此生效）
     *
     * @throws EvidenceError（EvidenceErrorCode::EnvelopeIllegalCombination）
     *         任一表 3 上下文无关格子违例（detail 含逐条字段名消息）
     *
     * 复杂度：O(E)（validateCombination）＋O(B)（载荷摘要，B＝字节量）。
     *
     * 线程安全：可重入纯函数（输入消耗性——移动语义，无共享状态）。
     */
    static ResultEnvelope make(ResultEnvelopeDraft&& draft);

    /// 全字段成员精确等值（测试/归档核对用；历史包络按 §7.2 永不回写）。
    bool operator==(const ResultEnvelope& o) const
    {
        return task == o.task && evaluationKey == o.evaluationKey
            && evaluatorContractVersion == o.evaluatorContractVersion
            && mode == o.mode && snapshotId == o.snapshotId
            && sliceId == o.sliceId && inputBaselineId == o.inputBaselineId
            && caseScope == o.caseScope && profile == o.profile
            && outcome == o.outcome && engineeringStatus == o.engineeringStatus
            && evidence == o.evidence && infeasibilityProof == o.infeasibilityProof
            && searchRecord == o.searchRecord && missingItems == o.missingItems
            && payload == o.payload && partialData == o.partialData
            && diagnostics == o.diagnostics && producer == o.producer;
    }
    bool operator!=(const ResultEnvelope& o) const { return !(*this == o); }
};

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_ENVELOPE_HPP
