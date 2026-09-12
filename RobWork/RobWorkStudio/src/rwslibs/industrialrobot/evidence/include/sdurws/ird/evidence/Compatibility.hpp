/**
 * @file   Compatibility.hpp
 * @brief  缓存/检查点兼容判定（Compatibility）——正式缓存命中、诊断性读取
 *         与检查点恢复三类复用的纯判定契约（judgeCacheHit／
 *         judgeCheckpointCompatibility；存储/淘汰/恢复归 execution/project）。
 *
 * 设计依据：
 *   - units/evidence.md §8.2（四类复用严格区分——任务约束§五.9；
 *     CachedResultSummary/CacheHitQuery/CacheHitResult/CheckpointSummary/
 *     CheckpointCompatibilityResult 五个结构体与两个判定函数签名的**原文
 *     冻结代码**；FullHit/DiagnosticOnly/Incompatible 三档判定式；
 *     Resumeable 五条件判定式）、§12 EV-T09 行（验证方式＝EV-CPA-1~3）、
 *     §14 追踪矩阵 CON-04 行（设计落点＝§8.2＋§5.1 身份含契约版本）、
 *     §11 反例矩阵（EV-CPA-1~3/EV-ENV-2 行）
 *   - 需求 CON-04（缓存和检查点按契约判断命中与兼容，**部分/失败结果不得
 *     作为正式缓存命中**）、OPT-06（Quick 筛选/Verified 复核/缓存/检查点
 *     ——缓存判定是其正确性的契约面）、EVI-01 表 1（模式效力分层——Quick
 *     不得单独支撑正式通过，模式不升降级）
 *   - 任务契约 tasks/foundation/EV-T09.json（≙WP-05-T09）acceptance 1～2：
 *     ①EV-CPA-1~3：Quick≠Verified/契约/检查点独立用例通过；②部分/失败
 *     结果不得作正式缓存命中（§8.2 纯判定反例）用例通过
 *
 * 背景说明（为什么"复用判定"必须是纯函数而不是缓存的附属逻辑）：
 *   评估结果与检查点是两类可复用资产，但它们的"可复用"语义完全不同——
 *   结果复用回答"这份已完成的计算能否直接当作本次请求的答案"（效力面：
 *   模式/身份/完整性全部对齐才可正式命中；部分/失败结果只能读作诊断）；
 *   检查点复用回答"这份中断的运行进度能否继续跑"（进度面：身份对齐＋
 *   格式可读＋完整性校验通过即可恢复，与上次任务成败无关——上次
 *   Canceled/Failed 的检查点仍可 Resumeable，CON-04/TASK-01）。两类判定
 *   若混在存储层实现，极易出现"取出来就能用"的隐式复用——部分结果冒充
 *   正式命中、跨契约旧结果冒充新算法输出。本头把判定收敛为**两个无状态
 *   纯函数**：输入（请求＋摘要）完全决定输出，不读磁盘、不写缓存、不抛
 *   异常——存储/淘汰/恢复的副作用全部归 execution/project（§8.2 标题
 *   括注），evidence 只冻结判定语义（PA-1 权威唯一）。
 *
 * 四类复用的严格区分（§8.2 首行"四类复用严格区分"的承载）：
 *   ①正式缓存命中（FullHit）——六条件全对齐，可当作本次请求的正式答案；
 *   ②诊断性读取（DiagnosticOnly）——身份面对齐但结果未完成（部分/失败/
 *     取消/清单未封账），只可读作诊断参考，**不得正式命中**（CON-04）；
 *   ③不兼容拒绝（Incompatible）——身份面任一失配，一律拒绝（含 Quick↔
 *     Verified 互查的双向拒绝，D-13 不升降级）；
 *   ④检查点恢复（Resumeable）——独立判定轨，与①②③互不冒充：检查点
 *     不是结果、结果不是检查点（§15.5 自审行"是否对缓存与检查点采用了
 *     同一套错误门禁"→"两套独立判定接口"）。
 *
 * 实现口径（单元卡 v1.0 变更记录登记，实现细节精确化、语义与设计一致）：
 *   - C-1 未满足条件**全量收集**不首错短路：judgeCacheHit 的 reasons 按
 *     CacheMissReason 声明序逐项列出全部失败检查（模式→切片→契约→
 *     Profile→结果→清单），同失配必得同一清单（NFR-COR-02）；verdict
 *     规则＝身份面（模式/切片/契约/Profile 四项）任一失配→Incompatible；
 *     身份面全对齐且有结果面失配（outcome≠Completed 或清单未封账）→
 *     DiagnosticOnly；全对齐→FullHit——与 §8.2 三档判定式逐字对应。
 *   - C-2 CheckpointSummary.evaluatorKey 暂以 std::string 承载（D-5 同款
 *     词形闸门 isValidEvaluationKey——InputSlice/Envelope 先例；类型化归
 *     Evaluator.hpp〔EV-T10〕落地后按需替换）。"evaluatorKey 相等"条件
 *     的承载：评估键进入切片身份（Slice.hpp——"进 sliceId——CON-04"），
 *     sliceId 相等 ⇒ 评估键相等，故请求侧无需另带键字段；显式字段做
 *     **词形校验**——键词形非法的检查点记录视为损坏/伪造（evaluator-
 *     key-invalid → Incompatible，保守拒绝）。
 *   - C-3 "checkpoint 格式版本兼容"＝summary.checkpointFormatVersion 落在
 *     本构建支持的**闭区间**〔kCheckpointFormatVersionMin,
 *     kCheckpointFormatVersionMax〕内（当前 [1,1]）。格式版本随
 *     execution/project 侧检查点存储格式演进推进（§8.2 标题括注——存储
 *     归该两单元）；区间外一律 Incompatible（读不懂的格式不得恢复——
 *     保守方向，宁重跑不可带病续跑）。
 *   - C-4 CacheHitQuery 的 requestedMode 与 requestProfileIdentity 不参与
 *     检查点判定（Resumeable 五条件无此二项——§8.2 原文）：检查点恢复的
 *     是**运行进度**而非正式证据，续跑完成后产生的结果仍要过包络构造
 *     边界（§7.1 make）与缓存判定（本头 judgeCacheHit），模式效力与
 *     Profile 绑定在结果面把关、不在恢复面拦截。
 *   - C-5 判定函数为**非抛出纯函数**（§2.1 try 轨分轨——可恢复查询路径；
 *     Errors.hpp CacheIncompatible 注释"判定输出供调用方转译"）：结构化
 *     返回而非异常；调用方对 Incompatible 按需转译为
 *     EvidenceErrorCode::CacheIncompatible／建议码 EVI-CACHE-INCOMPATIBLE
 *     （码值权威归 diagnostics StableCodeRegistry——PA-1，本单元不注册）。
 *
 * 消费的 core 契约（core.md v0.1 基线——P-EV-1 状态锚点，冻结时按 diff
 * 增量同步）：
 *   EvaluationMode/TaskOutcome（core Evaluation.hpp——§8.1 表 1/表 3 词表，
 *   CR-01 零本地枚举）、ContentIdentity（core Digest.hpp——切片/Profile
 *   身份承载，字节精确等值比较——附录 D 第 12 项）。
 *
 * 线程安全：两个判定函数均为可重入纯函数（全部入参 const 引用，无共享
 * 可变状态、无副作用——同一输入并发调用必得同一输出）。
 * 确定性：检查序固定（C-1/C-2 的逐项顺序）、reasons 输出顺序＝检查序、
 * 身份比较为字节精确等值（无浮点、无容差）——同输入必得同输出、跨平台
 * 跨进程一致（NFR-COR-02）。
 */

#ifndef SDURWS_IRD_EVIDENCE_COMPATIBILITY_HPP
#define SDURWS_IRD_EVIDENCE_COMPATIBILITY_HPP

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/evidence/Slice.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sdurws::ird::evidence {

// =====================================================================
// 缓存命中判定（§8.2 上半——judgeCacheHit 三结构）
// =====================================================================

/**
 * @brief 被复用结果的摘要（§8.2 CachedResultSummary——"从缓存条目/envelope
 *        提取（不载入全载荷）"）。
 *
 * 为什么只带摘要不带载荷：命中判定只消费**身份面与完成面**七个字段，读
 * 全载荷（域数据/证据清单/诊断）既浪费带宽又扩大判定面的出错可能——
 * "不载入全载荷"是设计原文的性能与职责双重要求。七个字段全部来自
 * make() 构造边界校验过的正式包络（§7.1），或来自缓存条目的登记头
 * （execution/project 侧存储格式——§8.2 标题括注）；字段值语义与
 * ResultEnvelope 同名字段一致。
 *
 * inputBaselineId 为什么是 optional：§8.2 原文如此（缓存条目可能早于
 * 基准身份登记引入）。它**不参与** FullHit 判定（§8.2 判定式六条件无
 * 此项——切片身份已含求解配置/契约/环境版本，D-04 双层身份的分工：
 * sliceId 管复用、inputBaselineId 管比较基准一致性〔EVI-02/RPT-04，
 * checkComparisonBaselinesConsistent 消费〕），仅随摘要携带供调用方
 * 做记录面核对。
 *
 * 值语义；线程安全：纯值（判定全程只读）。
 */
struct CachedResultSummary {
    /// 产生结果时的评估模式（core 词表——表 1 效力分层；**不会是 Preview**
    /// ：Preview 不产生结果对象〔表 1/表 3 行 4〕，make() 构造边界拒绝，
    /// 故真实缓存条目中不存在 Preview 摘要）。
    core::EvaluationMode mode = core::EvaluationMode::Quick;
    /// 任务结果（core 词表：completed/canceled/failed/interrupted——表 3
    /// outcome 轴；≠Completed 的结果只可诊断性读取，CON-04）。
    core::TaskOutcome outcome = core::TaskOutcome::Completed;
    /// 证据清单是否已封账（manifest finalized——运行终结时清单定稿标记；
    /// false＝清单还可能变化，结果不可作正式命中）。
    bool manifestFinalized = false;
    /// 结果切片内容身份（CON-05 缓存键级身份——策略/种子/线程/配置/样本/
    /// 环境版本已含于其中，D-01/D-11：身份要素完备性是 FullHit 的前提）。
    core::ContentIdentity sliceId;
    /// 产生结果时的评估器契约版本（无量纲；模拟算法升级即升版——缓存
    /// 兼容判定的契约面，CON-04/EV-CPA-2）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 证据 Profile 内容身份（§6.1 computeProfileContentIdentity 的注册值
    /// ——Profile 是证据效力语义的冻结载体，身份不一致＝证据口径不同，
    /// 不得互为复用）。
    core::ContentIdentity profileContentIdentity;
    /// 输入基准身份（D-04 基准层；可选——见结构注释；不参与 FullHit 判定）。
    std::optional<core::ContentIdentity> inputBaselineId;

    bool operator==(const CachedResultSummary& o) const
    {
        return mode == o.mode && outcome == o.outcome
            && manifestFinalized == o.manifestFinalized && sliceId == o.sliceId
            && evaluatorContractVersion == o.evaluatorContractVersion
            && profileContentIdentity == o.profileContentIdentity
            && inputBaselineId == o.inputBaselineId;
    }
    bool operator!=(const CachedResultSummary& o) const { return !(*this == o); }
};

/**
 * @brief 缓存命中查询（§8.2 CacheHitQuery——请求方的身份面四要素）。
 *
 * 同时是两个判定的请求载体：judgeCacheHit 消费全部四字段；judgeCheckpoint
 * Compatibility 只消费 requestSliceId/requestContractVersion（C-4——模式与
 * Profile 不参与检查点判定）。四身份字段的值域与 CachedResultSummary 对应
 * 字段相同。
 *
 * 值语义；线程安全：纯值。
 */
struct CacheHitQuery {
    /// 请求的评估模式（表 1 效力属性——模式是请求的属性，不做隐式升降级
    /// ：Quick 缓存对 Verified 请求与 Verified 缓存对 Quick 请求**双向均为
    /// Incompatible**，D-13 保守方向防"高级别证据降级冒充"或"低级别证据
    /// 升格冒充"）。
    core::EvaluationMode requestedMode = core::EvaluationMode::Quick;
    /// 请求的切片身份（本次评估输入冻结切片的 CON-05 缓存键——与摘要
    /// sliceId 的相等是 FullHit 的第二条件）。
    core::ContentIdentity requestSliceId;
    /// 请求的评估器契约版本（与摘要 evaluatorContractVersion 的相等是
    /// FullHit 的第三条件——跨契约旧结果一律拒绝，EV-CPA-2）。
    std::uint32_t requestContractVersion = 0;
    /// 请求的 Profile 内容身份（与摘要 profileContentIdentity 的相等是
    /// FullHit 的第四条件——证据效力口径必须一致）。
    core::ContentIdentity requestProfileIdentity;

    bool operator==(const CacheHitQuery& o) const
    {
        return requestedMode == o.requestedMode && requestSliceId == o.requestSliceId
            && requestContractVersion == o.requestContractVersion
            && requestProfileIdentity == o.requestProfileIdentity;
    }
    bool operator!=(const CacheHitQuery& o) const { return !(*this == o); }
};

/// 缓存未命中原因（§8.2 reasons 注释行原文六项——kebab token 即注释中的
/// "mode-mismatch / slice-mismatch / contract-mismatch / profile-mismatch /
/// outcome-not-completed / manifest-incomplete"）。枚举序＝检查序（C-1）：
/// 身份面四项在前（决定 Incompatible/DiagnosticOnly 分界）、结果面两项
/// 在后；一经交付不得改动/插入，只允许表尾追加并留痕（DTB §5.4——
/// EvidenceErrorCode 同款纪律）。
enum class CacheMissReason : std::uint8_t {
    ModeMismatch,        ///< mode-mismatch——评估模式失配（双向同判，D-13 不升降级）
    SliceMismatch,       ///< slice-mismatch——切片内容身份失配（输入已变，结果过期）
    ContractMismatch,    ///< contract-mismatch——评估器契约版本失配（算法契约已升级，EV-CPA-2）
    ProfileMismatch,     ///< profile-mismatch——Profile 内容身份失配（证据效力口径不同）
    OutcomeNotCompleted, ///< outcome-not-completed——结果未完成（部分/失败/取消——只可诊断性读取，CON-04）
    ManifestIncomplete,  ///< manifest-incomplete——证据清单未封账（结果不完整，不得正式命中）
};

/**
 * @brief 缓存命中判定结果（§8.2 CacheHitResult——verdict＋reasons）。
 *
 * 消费纪律（§8.2 判定式注释的机械化）：
 *   - FullHit 当且仅当 reasons 为空（模式相等 ∧ sliceId 相等 ∧ 契约版本
 *     相等 ∧ Profile 身份相等 ∧ outcome==Completed ∧ manifestFinalized）；
 *   - DiagnosticOnly 当且仅当身份面四项全对齐且 reasons 仅含结果面项
 *     （OutcomeNotCompleted/ManifestIncomplete 的任意组合）——部分/失败/
 *     取消结果**可读作诊断，不得正式命中**（CON-04/EV-ENV-2）；
 *   - Incompatible 为其余一切——reasons 至少含一个身份面项。
 *
 * reasons 全量收集（C-1）：一次判定列出**全部**未满足条件（含身份面与
 * 结果面的组合），调用方可据此区分"差一项就命中"与"身份完全不同"——
 * 与"缺失全量列出"的汇总纪律（§6.4）同源，避免逐项重试的调用方负担。
 *
 * 值语义；线程安全：纯值。
 */
struct CacheHitResult {
    /// 判定结论（§8.2 三档——FullHit/DiagnosticOnly/Incompatible；
    /// 嵌套枚举沿设计原文作用域）。
    enum Verdict : std::uint8_t { FullHit, DiagnosticOnly, Incompatible };

    Verdict verdict = Incompatible;      ///< 判定结论（默认最保守值——未判定即拒绝）
    /// 全部未满足条件（C-1 检查序；FullHit 恒空；诊断性/不兼容至少一项）。
    std::vector<CacheMissReason> reasons;

    bool operator==(const CacheHitResult& o) const
    {
        return verdict == o.verdict && reasons == o.reasons;
    }
    bool operator!=(const CacheHitResult& o) const { return !(*this == o); }
};

/**
 * @brief 判定缓存条目能否命中本次请求（§8.2 judgeCacheHit——纯函数）。
 *
 * 判定式（§8.2 原文三条，实现为 C-1 的全量收集）：
 *   - FullHit ⇔ 模式相等 ∧ sliceId 相等 ∧ 契约版本相等 ∧ Profile 身份相等
 *     ∧ outcome==Completed ∧ manifestFinalized——策略/种子/线程/配置/样本/
 *     环境版本已含于 sliceId（D-01/D-11：身份要素完备性是 FullHit 的前提，
 *     故无需逐项再查）；
 *   - DiagnosticOnly ⇔ 身份面（sliceId/契约/模式/Profile）相等 ∧
 *     (outcome≠Completed ∨ 未 finalize)——部分/失败/取消结果可读作诊断，
 *     不得正式命中（CON-04；EV-ENV-2/EV-CPA-2 反例的承载面）；
 *   - Incompatible：其余一切（身份面任一失配——含 Quick↔Verified 双向
 *     互查，D-13）。
 *
 * @param request [in] 请求身份面四要素（调用方持有，仅调用期使用）
 * @param summary [in] 被复用结果的七字段摘要（调用方持有，仅调用期使用）
 * @return 判定结果（verdict＋全量 reasons——不抛异常、不产生副作用，
 *         调用方对 Incompatible 自行决定转译与诊断登记，C-5）
 *
 * 复杂度：O(1)（六次定长比较＋至多六次入表）。
 *
 * 线程安全：可重入纯函数（无共享状态）。
 */
CacheHitResult judgeCacheHit(const CacheHitQuery& request,
                             const CachedResultSummary& summary);

// =====================================================================
// 检查点兼容判定（§8.2 下半——judgeCheckpointCompatibility 两结构）
// =====================================================================

/// 检查点格式版本支持区间下界（C-3——本构建可读的最老存储格式；无量纲
/// 单调递增版本号，由 execution/project 侧存储格式演进推进；当前仅格式 1
/// ——阶段 A 无真实存储设施，§13 交接后随其落位）。**修改须同步单元卡
/// 增量修订留痕**（DTB §5.4——兼容边界是可复现性承诺的一部分）。
inline constexpr std::uint32_t kCheckpointFormatVersionMin = 1;

/// 检查点格式版本支持区间上界（C-3——本构建可读的最新存储格式；与下界
/// 构成闭区间，区间外一律 Incompatible——读不懂的格式不得恢复，保守方向）。
inline constexpr std::uint32_t kCheckpointFormatVersionMax = 1;

/**
 * @brief 被复用检查点的摘要（§8.2 CheckpointSummary——从检查点存储条目
 *        提取的登记头五字段）。
 *
 * 与 CachedResultSummary 的本质差异：检查点没有 outcome/模式/Profile 面
 * ——它是**运行进度**（已完成的求解步、中间态），不是工程结论；其可
 * 恢复性五条件（§8.2 Resumeable 判定式）全部是"身份对齐＋格式可读＋
 * 完整性校验"维度。上次任务 Canceled/Failed 的检查点仍可 Resumeable
 * （CON-04/TASK-01：强制终止保留最近检查点——正是为续跑而生）。
 *
 * evaluatorKey 承载口径（C-2，D-5 同款）：std::string＋isValidEvaluationKey
 * 词形闸门（类型化归 EV-T10）。
 *
 * 值语义；线程安全：纯值。
 */
struct CheckpointSummary {
    /// 写入检查点的评估器键（isValidEvaluationKey 词形；词形非法＝记录
    /// 损坏/伪造——C-2 保守拒绝；键相等性由 sliceId 承载：评估键进入
    /// 切片身份〔Slice.hpp——CON-04〕，sliceId 相等 ⇒ 评估键相等）。
    std::string evaluatorKey;
    /// 检查点所属运行的切片身份（与请求 requestSliceId 相等是恢复第二条件
    /// ——输入变了就得从头跑，续跑旧输入毫无意义）。
    core::ContentIdentity sliceId;
    /// 写入检查点时的评估器契约版本（与请求 requestContractVersion 相等是
    /// 恢复第三条件——契约升级后旧进度的求解状态不可信，EV-CPA-2）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 检查点存储格式版本（无量纲；落在本构建支持闭区间内是恢复第四条件
    /// ——C-3；区间常量见 kCheckpointFormatVersionMin/Max）。
    std::uint32_t checkpointFormatVersion = 0;
    /// 完整性校验标记（存储侧载入时对检查点本体做的校验〔如摘要核对〕
    /// 的结论；false＝本体可能损坏——恢复第五条件要求 true，**未通过
    /// 完整性校验的检查点一律拒绝恢复**，NFR-COR-03 不静默通过）。
    bool integrityVerified = false;

    bool operator==(const CheckpointSummary& o) const
    {
        return evaluatorKey == o.evaluatorKey && sliceId == o.sliceId
            && evaluatorContractVersion == o.evaluatorContractVersion
            && checkpointFormatVersion == o.checkpointFormatVersion
            && integrityVerified == o.integrityVerified;
    }
    bool operator!=(const CheckpointSummary& o) const { return !(*this == o); }
};

/**
 * @brief 检查点兼容判定结果（§8.2 CheckpointCompatibilityResult）。
 *
 * 与 CacheHitResult 的分档差异：检查点只有 Resumeable/Incompatible 两档
 * ——没有"诊断性恢复"：进度要么能接着跑、要么不能（结果面才有"可读
 * 诊断不可正式命中"的第三档，CON-04 的"部分结果"语义只属结果复用）。
 *
 * reasons 为稳定 token 串（§8.2 原文 std::vector<std::string>——检查点
 * 拒绝原因需跨进程透出给 execution 通道层〔D-15 错误码化的上游输入〕，
 * 故以自描述 token 承载而非枚举）：词表冻结为五值——
 *   "evaluator-key-invalid"（评估键词形非法——C-2）/ "slice-mismatch"
 *   （切片身份失配）/ "contract-mismatch"（契约版本失配——EV-CPA-2
 *   观测 token）/ "checkpoint-format-incompatible"（格式版本出支持区间
 *   ——C-3）/ "integrity-not-verified"（完整性未通过）。按检查序排列
 * （同失配必得同一清单，NFR-COR-02）；token 词表一经交付不得改名（跨
 * 进程契约面，EvidenceErrorCode token 同款纪律）。
 *
 * 值语义；线程安全：纯值。
 */
struct CheckpointCompatibilityResult {
    /// 判定结论（§8.2 两档——嵌套枚举沿设计原文作用域）。
    enum Verdict : std::uint8_t { Resumeable, Incompatible };

    Verdict verdict = Incompatible;   ///< 判定结论（默认最保守值——未判定即拒绝）
    /// 全部拒绝原因（稳定 token——见结构注释词表；Resumeable 恒空；
    /// 检查序排列）。
    std::vector<std::string> reasons;

    bool operator==(const CheckpointCompatibilityResult& o) const
    {
        return verdict == o.verdict && reasons == o.reasons;
    }
    bool operator!=(const CheckpointCompatibilityResult& o) const { return !(*this == o); }
};

/**
 * @brief 判定检查点能否恢复续跑（§8.2 judgeCheckpointCompatibility——纯
 *        函数；恢复执行/续跑调度/检查点写入归 execution，ARCH §4.3/§6.5）。
 *
 * 判定式（§8.2 原文）：Resumeable ⇔ evaluatorKey 相等 ∧ sliceId 相等 ∧
 * 契约版本相等 ∧ checkpoint 格式版本兼容 ∧ integrityVerified。五条件逐项
 * 核对、全量收集拒绝原因（实现口径见各结构注释）：
 *   1 评估键：词形校验（键相等性由 sliceId 承载——C-2）；
 *   2 切片身份：summary.sliceId == request.requestSliceId（字节精确等值）；
 *   3 契约版本：summary.evaluatorContractVersion == request.requestContractVersion；
 *   4 格式版本：summary.checkpointFormatVersion ∈ 支持闭区间（C-3）；
 *   5 完整性：summary.integrityVerified == true。
 *
 * **可恢复性与任务成败独立**（§8.2 原文/EV-CPA-3）：检查点摘要没有
 * outcome 字段——上次任务 Canceled/Failed 不影响 Resumeable 判定（强制
 * 终止保留最近检查点正是为续跑而生，TASK-01）。requestedMode 与
 * requestProfileIdentity 不参与本判定（C-4——模式效力与 Profile 绑定在
 * 结果面把关）。
 *
 * @param request [in] 请求身份面（本判定只消费 requestSliceId/
 *                requestContractVersion 两字段——C-4；调用方持有）
 * @param summary [in] 被复用检查点五字段摘要（调用方持有，仅调用期使用）
 * @return 判定结果（verdict＋全量 reasons token——不抛异常、不产生副作用，
 *         恢复动作由调用方执行，C-5）
 *
 * 复杂度：O(1)（一次词形校验＋四次定长比较）。
 *
 * 线程安全：可重入纯函数（无共享状态）。
 */
CheckpointCompatibilityResult judgeCheckpointCompatibility(
    const CacheHitQuery& request, const CheckpointSummary& summary);

}  // namespace sdurws::ird::evidence

#endif  // SDURWS_IRD_EVIDENCE_COMPATIBILITY_HPP
