/**
 * @file   Verdict.hpp
 * @brief  工程判定汇总（Verdict）——五级优先级决策表的可执行实现（§6.4）、
 *         VerdictTrace 追溯承载、§7.2 两类声明资格检查与 §6.5 比较基准
 *         一致性检查。
 *
 * 设计依据：
 *   - units/evidence.md §6.4（§6.4.1 汇总决策表——C2/C5/C6/C8 修订后口径、
 *     §6.4.2 必须区分的判定对、§6.4.3 汇总层级）、§6.5（比较基准一致性
 *     检查——EVI-02/RPT-04）、§6.6（覆盖矩阵/区域证据的汇总消费——
 *     P-EV-7 保守处置）、§7.2（FormalPass/ReviewRecord 两类声明资格——
 *     RPT-05）、§12 EV-T06 行（产物＝Verdict.hpp/.cpp：决策表/VerdictTrace/
 *     资格/基准检查）
 *   - 需求 EVI-01（§8.1 表 2 五级优先级）、EVI-02（必验工况全覆盖）、
 *     REQ-06（Must/Should 两级判定语义）、KIN-04（区域覆盖降级/零样本）、
 *     C5/C8（搜索未果≠不可行、碰撞作用域）、D-09（汇总为纯函数＋trace
 *     留痕；证明走字段级 validateProof——不凭状态字段采信证明）、
 *     NFR-COR-02（确定性）、NFR-COR-04（追溯）、TASK-02（⓪ 取消/失败/
 *     中断无工程判定）、DYN-07（包络不替代覆盖）
 *   - 任务契约 tasks/foundation/EV-T06.json（≙WP-05-T06）acceptance 1～4：
 *     ①EV-VER-1~8、EV-COV-1/4 五级优先级正反例全部通过；②缺失项全量
 *     列出（不抽样）；③汇总不得凭状态字段采信证明（D-9）；④O-14 未决
 *     保守字面——必验工况覆盖矩阵按 §6.6 字面实现（分母＝快照冻结态
 *     RequiredCaseSet，工况对象 schema 归 requirements 卡 WP-14-T01）
 *
 * 背景说明（本头在证据链上的位置——为什么"汇总"值得一个独立契约头）：
 *   域评估器产出证据/证明/搜索未果后，必须有一个**唯一、确定、保守**的
 *   裁定入口把这些材料折算成一个工程判定（Feasible/EngineeringInfeasible/
 *   DataInsufficient）——它就是 §6.4.1 五级决策表：⓪执行轴前置→①输入
 *   非法→②通用证据门禁→③确定性不可行证明→④必需证据缺失→⑤工程判定，
 *   自上而下首个命中生效。aggregateVerdict 是纯函数：同输入必得同输出
 *   （NFR-COR-02），且全程留 VerdictTrace（命中级次＋逐级判定记录，
 *   NFR-COR-04——ui 的"未完成"缺项列表数据源，§13 交接）。
 *   红线（D-09，acceptance 3）：汇总器**绝不凭单一状态字段（如 bool
 *   provenInfeasible）采信证明**——证明必须经 validateProof 逐字段校验
 *   通过方可进入③；校验失败的证明按"无效"落入④（数据不足），绝不输出
 *   不可行结论。
 *
 * knownPitfalls 处置（契约 knownPitfalls＝P-EV-3/P-EV-7——均已有明确
 * 保守字面处置方向，按处置实现、最终语义待需求侧裁决）：
 *   - P-EV-3（跨域汇总顺序）：任务级不可行证明（③）与他域证据缺失（④）
 *     并存时按表 2 字面顺序——③ 先命中先输出，B 类无关缺失不阻断③
 *     （证明为任务级作用域，§6.4.3 整体方案级行同口径）。本实现即字面
 *     顺序：③命中即返回 EngineeringInfeasible，不再评估④。
 *   - P-EV-7（空必验工况集合/全不适用）：覆盖矩阵平凡完备但快照必验集
 *     无 enabled∧mandatory 工况时，按 ④ 级判 DataInsufficient（附
 *     "无启用必验工况"诊断），不得输出正式通过——保守方向（宁可数据
 *     不足不可虚通过）。"全部工况显式 NotApplicable"场景由 §6.6 覆盖
 *     判据字面（只有 Executed 构成覆盖）在②级自然拦截，不另设路径。
 *   - O-14（acceptance 4）：覆盖矩阵分母＝快照冻结态 RequiredCaseSet
 *     （enabled∧mandatory 为冻结标记字面消费，evidence 不解释标记）；
 *     工况对象 schema 权威归 requirements 卡 WP-14-T01。
 *
 * 实现口径（相对 §6.4.1 伪代码骨架的落位登记，登记单元卡 v0.7）：
 *   - I-1 aggregateVerdict 签名：伪代码 (VerdictInput, EvaluatorRegistry,
 *     EvidenceProfileRegistry) 中的两注册表类型归 §9（EV-T10 未落地），
 *     以最小只读注入端口承载（EV-T05 IProducerRegistryView 先例＋本头
 *     新增 IProfileRegistryView——§3.3 注入边界同款，EV-T10 注册表直接
 *     适配）；快照以显式参数传入（覆盖矩阵分母/清单绑定/证明绑定/模式
 *     前置校验的事实面——validateCaseCoverageMatrix 等 EV-T05 校验器
 *     均以快照为事实来源）。
 *   - I-2 VerdictInput 增补三字段：mode（②级"模式与证据等级标识"门禁
 *     组成＋§7.2 资格条件消费——Verified 前置固化校验由汇总器内部执行
 *     validateSnapshotForMode）；regionCoverages（§6.4.3 工作区域级
 *     证据的汇总裁定输入——EV-COV-3 零样本/降级判 DataInsufficient）；
 *     searchRecord（评估产出 SearchExhaustedRecord 的透传入参——
 *     §6.3 末"汇总层判 DataInsufficient（附该记录）"，VerdictResult.
 *     searchRecord 的"域产出时透传"需要输入承载）。伪代码其余字段逐字保留。
 *   - I-3 缺失项 MissingItem {itemId, reason}：itemId 为可定位稳定指称
 *     （工况规范文本/快照字段名/Profile itemId/证明 claimToken 等），
 *     reason 为人读中文原因；机器判别走 trace.hitLevel＋diagnostics 码面。
 *   - I-4 决策表＝§6.4.1 字面顺序（P-EV-3）；②级门禁内部按固定检查序
 *     全量收集（快照身份门禁→模式前置固化→Profile 注册核对→清单绑定→
 *     覆盖矩阵→区域证据形状），不短路——缺失全量列出（acceptance 2）。
 *   - I-5 区域证据分流：形状非法（validateRegionCoverageEvidence 有
 *     issue）＝②级门禁失败；形状自洽但 zeroSample（覆盖率不定义）或
 *     downgradedRequired（dataInsufficient>0）＝④级数据不足判
 *     DataInsufficient（EV-COV-3"整体 DataInsufficient"；⑤"证据齐备"
 *     不含数据不足的区域证据——KIN-04 R8 保守方向）。
 *   - I-6 证明无效（validateProof 有 issue）＝④级命中条件（该证明按
 *     EvidenceItemStatus::Invalid 语义不满足——"缺 mandatoryState 的
 *     同款证明→Invalid→④ DataInsufficient"，EV-VER-6 反例半边）；
 *     有效证明（③命中）时 substitutable 项按"因不可行而不适用"出具
 *     说明性诊断（§6.4.1 ③附加义务；清单本身的 NotApplicable 标记
 *     义务在评估器侧，汇总器不重写清单）。
 *   - I-7 资格检查（§7.2 表两行逐条件）：checkFormalPassEligibility/
 *     checkReviewRecordEligibility 为纯函数，unmetConditions 输出稳定
 *     token（结构注释冻结词表）；二者均重算所需事实，不信任 VerdictTrace
 *     以外的任何"已通过"声明（与 D-09 同向）。
 *   - I-8 基准检查（§6.5）：差异维度五值枚举；sampleSetIds 按序比较
 *     （快照冻结采样计划序——builder 规范化序，NFR-COR-02）；空集/单条
 *     平凡通过（无可比差异）。
 *   - I-9 诊断建议码：复用 §13 清单四值（EVI-SNAPSHOT-INCOMPLETE/
 *     EVI-CASE-COVERAGE-MISSING/EVI-EVIDENCE-MISSING/EVI-PROOF-INVALID）＋
 *     补登八值（P-PR-6/F-056 同模式——**建议值**，码值权威归 diagnostics
 *     StableCodeRegistry，见 kDiag* 常量注释）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点）：
 *   TaskOutcome/EvaluationMode/EngineeringStatus（Evaluation.hpp 词表）、
 *   DiagnosticRecord（DiagData.hpp——诊断承载；码值权威归 diagnostics）。
 *
 * 线程安全：本头全部实体为纯值/纯函数（无共享可变状态），并发只读安全；
 * aggregateVerdict 可重入（EV-T05 校验器同款纪律），注册表端口的并发
 * 安全由实现方保证（两次只读查询）。
 */

#ifndef SDURWS_IRD_EVIDENCE_VERDICT_HPP
#define SDURWS_IRD_EVIDENCE_VERDICT_HPP

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/evidence/Errors.hpp>
#include <sdurws/ird/evidence/Evidence.hpp>
#include <sdurws/ird/evidence/Snapshot.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 决策表输入组件（§6.4.1 VerdictInput 各成员的类型承载）
// =====================================================================

/**
 * @brief 输入就绪摘要（§6.4.1 ①级：ReadinessSummary——REQ-06 口径）。
 *
 * 数据源＝请求方/域的就绪校验（REQ-06"任一启用 Must 条目非法→输入未
 * 完成，不运行正式评估"）——evidence 是**消费者**不是生产者（权威唯一
 * PA-1：就绪判定语义归 requirements 侧），本结构只承载其结论供①级判定。
 *
 * 值语义；线程安全：纯值。
 */
struct ReadinessSummary {
    /// 就绪结论（false＝存在非法的启用 Must 条目——①级命中）。
    bool valid = true;
    /// 未满足的启用 Must 条目 id 清单（REQ-06 词表归 requirements 侧；
    /// valid==true 时应为空——两字段一致性由提供方负责，本结构不校验）。
    std::vector<std::string> invalidMustItems;

    bool operator==(const ReadinessSummary& o) const
    {
        return valid == o.valid && invalidMustItems == o.invalidMustItems;
    }
    bool operator!=(const ReadinessSummary& o) const { return !(*this == o); }
};

/**
 * @brief 快照身份门禁结果（§6.4.1 ②级：SnapshotGateResult）。
 *
 * 语义：对"快照身份（含策略/名称映射内容身份、复现块）缺失/无效"的
 * 门禁预校验结论，由调用方装配（快照 builder 已挡的 CON-06 非空等在此
 * 是对手工构造/传输破损的复核面）；Verified 前置固化校验（CON-03）由
 * 汇总器内部对快照值直接执行 validateSnapshotForMode（I-2），不经本结构。
 * "模式与证据等级标识缺失/无效"（表 4 通用必需项第三行）同属调用方
 * 装配面——其缺失以 missingFields 条目表达。
 *
 * 值语义；线程安全：纯值。
 */
struct SnapshotGateResult {
    /// 门禁结论（false＝存在缺失/无效的快照身份字段——②级命中）。
    bool complete = true;
    /// 缺失/无效字段清单（稳定字段指称，如 "policyContentIdentity"——
    /// EV-VER-1 场景"无策略内容身份"；complete==false 时应非空）。
    std::vector<std::string> missingFields;

    bool operator==(const SnapshotGateResult& o) const
    {
        return complete == o.complete && missingFields == o.missingFields;
    }
    bool operator!=(const SnapshotGateResult& o) const { return !(*this == o); }
};

/**
 * @brief 域判定违例（§6.4.1 ⑤级 DomainVerdictInputs 的清单元素）。
 *
 * Must/Should 违例的**判定**由域评估产出（域知识——哪条 Must/Should
 * 被违例是域语义，REQ-06），evidence 只做汇总定级与透传（PA-1）。
 *
 * 值语义；线程安全：纯值。
 */
struct DomainVerdictViolation {
    /// 违例项 id（域登记的 Must/Should 条目指称——词表归域）。
    std::string itemId;
    /// 违例详情（域承载文案——人读；透传进诊断 cause，EV-VER-8"verdictInputs
    /// 透传"观测面）。
    std::string detail;

    bool operator==(const DomainVerdictViolation& o) const
    {
        return itemId == o.itemId && detail == o.detail;
    }
    bool operator!=(const DomainVerdictViolation& o) const { return !(*this == o); }
};

/**
 * @brief 域判定输入（§6.4.1 ⑤级：DomainVerdictInputs）。
 *
 * 判定规则（REQ-06/表 2 ⑤）：有效 Must 未满足→EngineeringInfeasible
 * （判定记录）；Should 未满足→Feasible＋警告诊断（不阻断）。
 *
 * 值语义；线程安全：纯值。
 */
struct DomainVerdictInputs {
    std::vector<DomainVerdictViolation> mustViolations;   ///< 有效 Must 违例清单（非空→⑤级不可行）
    std::vector<DomainVerdictViolation> shouldViolations; ///< Should 违例清单（非空→Feasible＋警告）

    bool operator==(const DomainVerdictInputs& o) const
    {
        return mustViolations == o.mustViolations
            && shouldViolations == o.shouldViolations;
    }
    bool operator!=(const DomainVerdictInputs& o) const { return !(*this == o); }
};

/**
 * @brief 汇总输入（§6.4.1 VerdictInput——一次 aggregateVerdict 的全部
 *        声明性输入；实现口径 I-2 增补三字段见文件头）。
 *
 * 生命周期：全部成员为值承载；聚合期间只读（纯函数承诺）。
 * 值语义；线程安全：纯值。
 */
struct VerdictInput {
    /// ⓪ 被汇总运行的任务结果（≠Completed→不汇总，NotApplicable——
    /// TASK-02"取消/失败/中断无工程判定"）。
    core::TaskOutcome outcome = core::TaskOutcome::Completed;
    /// 评估模式（I-2 增补：②级门禁"模式与证据等级标识"组成——Verified
    /// 时汇总器内部执行 validateSnapshotForMode 固化校验；§7.2 资格条件
    /// "mode==Verified"消费）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// ① 输入就绪摘要（REQ-06——请求方/域提供，evidence 只消费）。
    ReadinessSummary readiness;
    /// ② 快照身份门禁结果（调用方装配——含策略/名称映射内容身份、复现块、
    /// 模式与证据等级标识的缺失/无效清单）。
    SnapshotGateResult snapshotGate;
    /// ② 必验工况覆盖矩阵（分母＝快照冻结态 RequiredCaseSet——汇总器
    /// 内部执行 validateCaseCoverageMatrix；O-14 保守字面）。
    CaseCoverageMatrix coverage;
    /// ②/④ 区域采样证据（I-2 增补：§6.4.3 工作区域级——形状非法＝②级
    /// 门禁失败；零样本/降级＝④级 DataInsufficient，EV-COV-3）。
    std::vector<RegionCoverageEvidence> regionCoverages;
    /// ③ 确定性不可行证明（有值时必须经 validateProof 逐字段校验——
    /// D-09：不凭存在性采信；校验失败＝无效证明→④级数据不足）。
    std::optional<DeterministicInfeasibilityProof> proof;
    /// ③④⑤ 证据清单（Profile 绑定三元组＋逐项状态——②级绑定校验、
    /// ④级完备性核对的事实面）。
    EvidenceManifest evidence;
    /// ⑤ 域判定输入（Must/Should 违例——域产出，汇总器定级与透传）。
    DomainVerdictInputs domain;
    /// 搜索未果记录（I-2 增补：评估产出透传——§6.3 末硬规则"汇总层判
    /// DataInsufficient（附该记录），不得输出不可行结论"；④级数据不足
    /// 凭据）。
    std::optional<SearchExhaustedRecord> searchRecord;

    bool operator==(const VerdictInput& o) const
    {
        return outcome == o.outcome && mode == o.mode && readiness == o.readiness
            && snapshotGate == o.snapshotGate && coverage == o.coverage
            && regionCoverages == o.regionCoverages && proof == o.proof
            && evidence == o.evidence && domain == o.domain
            && searchRecord == o.searchRecord;
    }
    bool operator!=(const VerdictInput& o) const { return !(*this == o); }
};

// =====================================================================
// 决策表输出（§6.4.1 VerdictResult——status/missingItems/diagnostics/
// trace/searchRecord 五成员逐字承载）
// =====================================================================

/**
 * @brief 决策表级次（§6.4.1 表行号——⓪前置～⑤工程判定；trace 与测试
 *        断言的稳定词表；值即表行号，一经交付不得改动/插入）。
 */
enum class VerdictLevel : std::uint8_t {
    OutcomePrecheck = 0,      ///< ⓪ 前置：outcome ≠ Completed→不汇总（NotApplicable）
    InputReadiness = 1,       ///< ① 输入非法：readiness.valid==false（REQ-06）
    CommonEvidenceGate = 2,   ///< ② 通用证据门禁（快照身份/固化/覆盖矩阵/清单绑定——C6 证明不豁免）
    InfeasibilityProof = 3,   ///< ③ 确定性不可行证明（validateProof 通过方计入——D-09）
    MissingEvidence = 4,      ///< ④ 必需证据缺失/数据不足（gaps/无效证明/搜索未果/区域降级/P-EV-7）
    EngineeringJudgement = 5, ///< ⑤ 工程判定（Must/Should——REQ-06）
};

/**
 * @brief 单级判定记录（VerdictTrace 元素——NFR-COR-04 追溯承载）。
 *
 * 逐级记录固定六槽（⓪~⑤），evaluated==false 的级保留槽位但 hit==false
 * ——trace 永远是完整决策路径（ui"未完成"缺项列表与验收解释的对照面）。
 *
 * 值语义；线程安全：纯值。
 */
struct VerdictLevelRecord {
    VerdictLevel level = VerdictLevel::OutcomePrecheck; ///< 级次（＝槽位号）
    bool evaluated = false; ///< 本级是否被评估（前级命中后本级短路——字面顺序，P-EV-3）
    bool hit = false;       ///< 本级是否命中（命中即决策输出级；evaluated==false 时恒 false）
    /// 判定说明（人读中文：命中条件摘要/短路原因——同输入恒同文本，
    /// NFR-COR-02；无时间戳/无随机）。
    std::string note;

    bool operator==(const VerdictLevelRecord& o) const
    {
        return level == o.level && evaluated == o.evaluated && hit == o.hit
            && note == o.note;
    }
    bool operator!=(const VerdictLevelRecord& o) const { return !(*this == o); }
};

/**
 * @brief 汇总追溯（§6.4.1 VerdictTrace——"命中级次与逐级判定记录"）。
 *
 * 值语义；线程安全：纯值。
 */
struct VerdictTrace {
    /// 命中级次（＝records 中 hit==true 槽的 level——恰一个）。
    VerdictLevel hitLevel = VerdictLevel::OutcomePrecheck;
    /// 逐级判定记录（固定六槽，按 level 升序——决策表字面顺序）。
    std::vector<VerdictLevelRecord> records;

    bool operator==(const VerdictTrace& o) const
    {
        return hitLevel == o.hitLevel && records == o.records;
    }
    bool operator!=(const VerdictTrace& o) const { return !(*this == o); }
};

/**
 * @brief 缺失项（§6.4.1 MissingItem——"itemId+原因"；实现口径 I-3）。
 *
 * acceptance 2 的承载：①②④级命中时 missingItems **全量列出**（不抽样、
 * 不因首个缺失短路——表 2 ④"缺失项全量列出"原文）。
 *
 * 值语义；线程安全：纯值。
 */
struct MissingItem {
    /// 缺失项可定位稳定指称：漏验工况＝caseId 规范文本（"obj-<32hex>"）、
    /// 快照身份字段＝字段名、Profile 项＝itemId、证明＝claimToken 等。
    std::string itemId;
    /// 人读中文原因（区分缺失类别——机器判别另走 trace.hitLevel 与
    /// diagnostics 码面）。
    std::string reason;

    bool operator==(const MissingItem& o) const
    {
        return itemId == o.itemId && reason == o.reason;
    }
    bool operator!=(const MissingItem& o) const { return !(*this == o); }
};

/**
 * @brief 汇总结果（§6.4.1 VerdictResult 五成员逐字承载）。
 *
 * status 语义（§6.4.1 表）：⓪→NotApplicable；①②④→DataInsufficient；
 * ③→EngineeringInfeasible；⑤→Feasible|EngineeringInfeasible。
 *
 * 值语义；线程安全：纯值。
 */
struct VerdictResult {
    /// 工程判定（四值词表——core Evaluation.hpp）。
    core::EngineeringStatus status = core::EngineeringStatus::NotApplicable;
    /// 缺失项全量清单（①②④级填充；③⑤级为空）。
    std::vector<MissingItem> missingItems;
    /// 诊断记录（各命中级的补充语义——码面见 kDiag* 建议码常量）。
    std::vector<core::DiagnosticRecord> diagnostics;
    /// 追溯（命中级次＋逐级记录——NFR-COR-04）。
    VerdictTrace trace;
    /// 搜索未果记录透传（④级场景的数据不足凭据——§6.3 末；其余级为空）。
    std::optional<SearchExhaustedRecord> searchRecord;

    bool operator==(const VerdictResult& o) const
    {
        return status == o.status && missingItems == o.missingItems
            && diagnostics == o.diagnostics && trace == o.trace
            && searchRecord == o.searchRecord;
    }
    bool operator!=(const VerdictResult& o) const { return !(*this == o); }
};

// =====================================================================
// 注入端口（I-1：§6.4.1 伪代码两注册表参数的最小只读投影——EV-T10 落地
// 后适配；§3.3 注入边界同款，本单元零跨单元编译依赖，R-1/R-2）
// =====================================================================

/**
 * @brief 证据 Profile 注册表只读投影（aggregateVerdict 的 Profile 查询面
 *        ——§6.4.1 伪代码 EvidenceProfileRegistry 参数的承载）。
 *
 * 语义：按 (profileId, version) 精确查找已注册 Profile（§9.5 注册键为
 * 二元组——同域多版本可共存）；未注册返回 nullptr。找到后由汇总器核对
 * manifest.profileContentIdentity 与注册内容身份一致（三元组绑定面，
 * §6.1 表）——错配即②级门禁失败（证据清单绑定到未注册的 Profile 形态，
 * 保守拒绝）。
 *
 * 生命周期：调用方持有并保证 aggregateVerdict/资格检查调用期间存活；
 * 返回指针指向注册表内对象，调用期有效，本单元不接管所有权。
 * 线程约束：实现方自行保证（汇总器只做只读查询）。
 */
class IProfileRegistryView {
public:
    virtual ~IProfileRegistryView() = default;

    /// 按 (profileId, version) 精确查找；未注册返回 nullptr（只读）。
    virtual const RequiredEvidenceProfile*
    findProfile(std::string_view profileId, std::string_view version) const = 0;
};

// =====================================================================
// 汇总决策表（§6.4.1——五级优先级，自上而下首个命中生效）
// =====================================================================

/// 汇总诊断建议码：⓪ 级说明（取消/失败/中断无工程判定——TASK-02）。
/// 补登建议值（P-PR-6/F-056 同模式——非 §13 清单原文；码值权威归
/// diagnostics StableCodeRegistry，登记单元卡 v0.7）。
inline constexpr std::string_view kDiagOutcomeNotCompleted{"EVI-OUTCOME-NOT-COMPLETED"};
/// 汇总诊断建议码：① 级输入未完成（REQ-06）。补登建议值（同上）。
inline constexpr std::string_view kDiagInputNotReady{"EVI-INPUT-NOT-READY"};
/// 汇总诊断建议码：② 级快照身份门禁缺失（§13 清单原文——EVI-SNAPSHOT-INCOMPLETE）。
inline constexpr std::string_view kDiagSnapshotIncomplete{"EVI-SNAPSHOT-INCOMPLETE"};
/// 汇总诊断建议码：② 级覆盖矩阵缺失/漏验（§13 清单原文——EVI-CASE-COVERAGE-MISSING；
/// 空必验集 P-EV-7 诊断亦复用本码面——覆盖门禁语义家族，cause 区分）。
inline constexpr std::string_view kDiagCaseCoverageMissing{"EVI-CASE-COVERAGE-MISSING"};
/// 汇总诊断建议码：② 级证据清单绑定失败（§6.2 汇总器绑定校验）。
/// 补登建议值（P-PR-6/F-056 同模式——码值权威归 diagnostics）。
inline constexpr std::string_view kDiagManifestBindingInvalid{"EVI-MANIFEST-BINDING-INVALID"};
/// 汇总诊断建议码：② 级 Profile 未注册/内容身份错配（三元组绑定面）。
/// 补登建议值（同上）。
inline constexpr std::string_view kDiagProfileUnresolved{"EVI-PROFILE-UNRESOLVED"};
/// 汇总诊断建议码：③ 级证明字段级校验失败（§13 清单原文——EVI-PROOF-INVALID；
/// D-09：无效证明不被采信为不可行）。
inline constexpr std::string_view kDiagProofInvalid{"EVI-PROOF-INVALID"};
/// 汇总诊断建议码：③ 命中时的附加义务说明（substitutable 项按因不可行
/// 不适用豁免）。复用 §13 清单 EVI-EVIDENCE-MISSING 码面不贴切，此场景
/// 非缺失而是豁免记录——补登建议值（同上）。
inline constexpr std::string_view kDiagInfeasibilitySubstitution{"EVI-INFEASIBILITY-SUBSTITUTION"};
/// 汇总诊断建议码：④ 级必需证据缺失（§13 清单原文——EVI-EVIDENCE-MISSING）。
inline constexpr std::string_view kDiagEvidenceMissing{"EVI-EVIDENCE-MISSING"};
/// 汇总诊断建议码：④ 级搜索未果数据不足（§6.3 末——DataInsufficient 凭据）。
/// 补登建议值（同上）。
inline constexpr std::string_view kDiagSearchExhausted{"EVI-SEARCH-EXHAUSTED"};
/// 汇总诊断建议码：④ 级区域覆盖降级/零样本（KIN-04 R8——整体降级
/// DataInsufficient/覆盖率不定义）。补登建议值（同上）。
inline constexpr std::string_view kDiagRegionCoverageDowngraded{"EVI-REGION-COVERAGE-DOWNGRADED"};
/// 汇总诊断建议码：⑤ 级 Must 违例判定记录（REQ-06）。补登建议值（同上）。
inline constexpr std::string_view kDiagMustViolation{"EVI-MUST-VIOLATION"};
/// 汇总诊断建议码：⑤ 级 Should 违例警告（REQ-06——不阻断）。补登建议值（同上）。
inline constexpr std::string_view kDiagShouldViolation{"EVI-SHOULD-VIOLATION"};

/**
 * @brief 五级优先级汇总决策表（§6.4.1 的可执行实现——纯函数、确定性、
 *        无副作用、线程安全可重入）。
 *
 * 决策顺序（自上而下首个命中生效——字面顺序，P-EV-3）：
 *   ⓪ outcome ≠ Completed → NotApplicable（不汇总——TASK-02）；
 *   ① readiness.valid==false → DataInsufficient（missingItems＝
 *     invalidMustItems 全量；"不运行正式评估"由调用侧遵循——本函数不派发
 *     任何评估）；
 *   ② 通用证据门禁（固定检查序全量收集，任一命中即本级命中——缺失全量
 *     列出）：snapshotGate.complete==false ∨ Verified 前置固化校验存在
 *     问题（validateSnapshotForMode——CON-03）∨ 清单绑定的 Profile 未
 *     注册/内容身份错配 ∨ 清单对快照绑定校验失败（caseScope/subject——
 *     §6.2 汇总器校验）∨ 覆盖矩阵非法/覆盖不完备（validateCaseCoverageMatrix
 *     ——漏验/错误引用/重复；包络不替代，DYN-07）∨ 区域覆盖证据形状非法
 *     （validateRegionCoverageEvidence 有 issue）。**证明不豁免门禁**（C6）：
 *     本级命中即返回，不进入③；
 *   ③ proof 有值且 validateProof 通过（绑定一致性以快照与 manifest.
 *     sliceId 为事实面——D-09 字段级校验）→ EngineeringInfeasible；
 *     substitutable 项出具"因不可行而不适用"说明诊断（§6.4.1 ③附加义务）；
 *     proof 校验失败→不输出不可行，落④（无效证明＝Invalid 语义）；
 *   ④ 数据不足判定（任一命中→DataInsufficient）：必需项 gaps 非空
 *     （checkEvidenceCompleteness——Missing/Invalid/Unverified 全量列出）
 *     ∨ 证明无效（见③）∨ searchRecord 有值（§6.3 末硬规则）∨ 区域证据
 *     零样本（覆盖率不定义）∨ 区域证据降级必要（dataInsufficient>0——
 *     EV-COV-3 整体降级）∨ 快照必验集无 enabled∧mandatory 工况（P-EV-7
 *     保守处置——附"无启用必验工况"诊断，不得输出正式通过）；
 *   ⑤ 证据齐备：mustViolations 非空→EngineeringInfeasible（逐条判定
 *     记录诊断）；shouldViolations 非空→Feasible＋逐条警告诊断（不阻断，
 *     REQ-06）；否则→Feasible。
 *
 * @param input     [in] 汇总输入（声明性材料——调用方持有，本函数不修改）
 * @param snapshot  [in] 被汇总结果的来源冻结快照（覆盖矩阵分母/清单与证明
 *                  绑定/模式前置校验的事实面；builder 产出——I-1）
 * @param producers [in] 产生者注册表只读投影（validateProof 的"已注册且
 *                  契约版本相符"查询面；EV-T05 IProducerRegistryView——
 *                  调用方持有，仅调用期使用）
 * @param profiles  [in] Profile 注册表只读投影（清单绑定 Profile 的查询面
 *                  ——I-1；调用方持有，仅调用期使用）
 *
 * @return 汇总结果（status/missingItems/diagnostics/trace/searchRecord；
 *         同输入必得同输出——确定性 NFR-COR-02；missingItems 顺序＝检查序，
 *         可用下标断言）
 *
 * 复杂度：O(G·C＋E＋P)（覆盖矩阵校验的工况×条目、清单完备性、证明碰撞对
 * ——单次汇总，非热点路径）。
 *
 * 线程安全：可重入纯函数（两注册表只读查询的并发安全由实现方保证）。
 */
VerdictResult aggregateVerdict(const VerdictInput& input,
                               const AnalysisSnapshot& snapshot,
                               const IProducerRegistryView& producers,
                               const IProfileRegistryView& profiles);

// =====================================================================
// 两类声明资格检查（§7.2 表——RPT-05 冻结措辞的判定承载；EV-T06 产物
 // "资格"面；实现口径 I-7）
// =====================================================================

/**
 * @brief 资格检查结果（§7.2 两类资格共用的结构承载）。
 *
 * 值语义；线程安全：纯值。
 */
struct EligibilityCheck {
    /// 五（或五）条件是否全部满足（§7.2 表条件列——同时满足方可 true）。
    bool eligible = false;
    /// 未满足条件的稳定 token 清单（词表冻结于两检查函数注释——reporting/
    /// ui 消费面；顺序＝§7.2 表条件列顺序，确定性）。
    std::vector<std::string> unmetConditions;

    bool operator==(const EligibilityCheck& o) const
    {
        return eligible == o.eligible && unmetConditions == o.unmetConditions;
    }
    bool operator!=(const EligibilityCheck& o) const { return !(*this == o); }
};

/**
 * @brief FormalPassEligibility（§7.2：可渲染"正式通过结论"的资格）。
 *
 * 五条件（§7.2 表原文，同时满足）：mode==Verified；outcome==Completed；
 * 覆盖矩阵完备（矩阵合法且全部 enabled∧mandatory 工况 Executed——EVI-02）；
 * 必需证据齐备（checkEvidenceCompleteness requiredGaps 空）；verdict.
 * status==Feasible。
 *
 * unmetConditions 稳定 token 词表（按上序）："mode-not-verified" /
 * "outcome-not-completed" / "coverage-incomplete" / "evidence-incomplete" /
 * "status-not-feasible"。
 *
 * @param input    [in] 汇总输入（mode/outcome/coverage/evidence 事实面）
 * @param snapshot [in] 来源冻结快照（覆盖矩阵分母事实面）
 * @param verdict  [in] aggregateVerdict 的结果（status 条件面）
 * @param profiles [in] Profile 注册表只读投影（完备性核对需要 Profile）
 * @return 资格检查（eligible＋未满足条件清单——纯函数即时计算，不写回
 *         任何归档状态，§7.2"永不回写"纪律）
 *
 * 线程安全：可重入纯函数。
 */
EligibilityCheck checkFormalPassEligibility(const VerdictInput& input,
                                            const AnalysisSnapshot& snapshot,
                                            const VerdictResult& verdict,
                                            const IProfileRegistryView& profiles);

/**
 * @brief ReviewRecordEligibility（§7.2：可作"正式评审记录"的资格）。
 *
 * 五条件（§7.2 表原文，同时满足）：mode==Verified；outcome==Completed；
 * ②级门禁通过（快照身份门禁/固化/Profile 绑定/清单绑定/覆盖矩阵合法且
 * 完备/区域证据形状——与 aggregateVerdict ②级同一事实面）；verdict.
 * status==EngineeringInfeasible；携带有效证明（validateProof 通过）或
 * Must 违例记录（mustViolations 非空）。
 *
 * unmetConditions 稳定 token 词表（按上序）："mode-not-verified" /
 * "outcome-not-completed" / "common-gate-failed" / "status-not-infeasible" /
 * "no-valid-proof-or-must-violation"。
 *
 * @param input    [in] 汇总输入（mode/outcome/domain 事实面）
 * @param snapshot [in] 来源冻结快照（门禁/证明绑定事实面）
 * @param verdict  [in] aggregateVerdict 的结果（status 条件面）
 * @param producers [in] 产生者注册表只读投影（证明有效性核对——D-09：
 *                  "携带有效证明"不是"携带证明"，必须字段级校验通过）
 * @param profiles [in] Profile 注册表只读投影（门禁 Profile 核对面）
 * @return 资格检查（同上——纯函数即时计算）
 *
 * 线程安全：可重入纯函数。
 */
EligibilityCheck checkReviewRecordEligibility(const VerdictInput& input,
                                              const AnalysisSnapshot& snapshot,
                                              const VerdictResult& verdict,
                                              const IProducerRegistryView& producers,
                                              const IProfileRegistryView& profiles);

// =====================================================================
// 比较基准一致性检查（§6.5——EVI-02/RPT-04/OPT 纯判定；EV-COV-4）
// =====================================================================

/**
 * @brief 比较基准（§6.5 ComparisonBaseline——从各结果的 envelope 提取）。
 *
 * 值语义；线程安全：纯值。
 */
struct ComparisonBaseline {
    core::ProjectId project;                    ///< 项目身份（跨项目结果不得直接比较）
    core::BranchId branch;                      ///< 方案分支身份（跨分支不得直接比较）
    /// 模型/需求/工况集/冻结样本集基准（D-04 双层身份的基准层——§5.1）。
    core::ContentIdentity inputBaselineId;
    /// 必验工况集身份（§6.5 字段表原文——快照 caseSet 冻结凭据）。
    core::ContentIdentity requiredCaseSetId;
    /// 样本集身份清单（§6.5 字段表原文；按序比较——快照冻结采样计划序
    /// 为规范序，I-8；采样预算改变→身份变→不可直接比较，KIN-04/C5）。
    std::vector<core::ContentIdentity> sampleSetIds;

    bool operator==(const ComparisonBaseline& o) const
    {
        return project == o.project && branch == o.branch
            && inputBaselineId == o.inputBaselineId
            && requiredCaseSetId == o.requiredCaseSetId
            && sampleSetIds == o.sampleSetIds;
    }
    bool operator!=(const ComparisonBaseline& o) const { return !(*this == o); }
};

/**
 * @brief 基准差异维度（§6.5"列出差异维度"的稳定词表——值即检查序，
 *        一经交付不得改动/插入）。
 */
enum class BaselineDifferenceDimension : std::uint8_t {
    Project,          ///< project 不等（跨项目比较——拒绝面）
    Branch,           ///< branch 不等（跨分支比较——拒绝面）
    InputBaseline,    ///< inputBaselineId 不等（模型/需求/工况集/冻结样本集基准——EVI-02）
    RequiredCaseSet,  ///< requiredCaseSetId 不等（必验工况集不同）
    SampleSets,       ///< sampleSetIds 不等（采样预算改变——KIN-04 冻结样本集，EV-COV-4）
};

/**
 * @brief 基准一致性检查结果（§6.5 CheckResult 的结构承载）。
 *
 * 值语义；线程安全：纯值。
 */
struct BaselineConsistencyResult {
    /// 全部基准相等（空集/单条平凡通过——无可比差异，I-8）。
    bool consistent = true;
    /// 差异维度（去重、按枚举声明序——"差异维度逐项输出"，EV-COV-4）。
    std::vector<BaselineDifferenceDimension> differingDimensions;

    bool operator==(const BaselineConsistencyResult& o) const
    {
        return consistent == o.consistent
            && differingDimensions == o.differingDimensions;
    }
    bool operator!=(const BaselineConsistencyResult& o) const { return !(*this == o); }
};

/**
 * @brief 比较基准一致性检查（§6.5 checkComparisonBaselinesConsistent）。
 *
 * 规则（§6.5 原文）：全部相等→通过；任何不等→失败并列出差异维度——
 * 基准不同的方案/候选/报告变体不得直接比较（RPT-04 方案比较、OPT 内部
 * Pareto 比较、报告变体章节共用本检查）；样本集身份不等（如采样预算改变）
 * →旧覆盖率/候选指标不可与新的直接比较（KIN-04 冻结样本集＋C5 复评规则
 * 的联合落地，EV-COV-4）。
 *
 * @param baselines [in] 待比较基准集（≥2 条才有可比性；空集/单条平凡
 *                  通过——I-8；调用方持有，本函数不修改）
 * @return 一致性结果（consistent＋差异维度清单——同输入恒同输出，顺序
 *         ＝枚举声明序，NFR-COR-02）
 *
 * 复杂度：O(n·k)（n＝基准条数、k＝样本集清单长度）。
 *
 * 线程安全：可重入纯函数。
 */
BaselineConsistencyResult
checkComparisonBaselinesConsistent(const std::vector<ComparisonBaseline>& baselines);

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_VERDICT_HPP
