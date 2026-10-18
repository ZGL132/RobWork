/**
 * @file   Roundtrip.hpp
 * @brief  报告往返复算契约——roundtripCheck 比对入口＋复算请求/结果值类型＋
 *         IRoundtripRecomputeSource 注入源＋RoundtripMismatch 不一致标记
 *         （RPT-T16 产物，§7.7）。
 *
 * 设计依据：
 *   - units/reporting.md §7.7（报告往返复算——本头的**设计权威**，语义逐句
 *     承接："从报告数据源（报告绑定的快照/切片与评估键）经③端口重新派发
 *     评估→比对复算结果与报告引用的结果摘要……一致→报告维持可用；不一致
 *     →报告不可用于正式结论（RoundtripMismatch……标记于伴随提示与后续
 *     构建的资格核对，不修改历史报告）。比对入口归 reporting
 *     （roundtripCheck(reportId)——阶段 C 随 WP-12-T08 落地）；复算执行归
 *     execution/各域（reporting 不自建评估调用——经注入的请求接口发起，
 *     登记 §12 交接）"）、§3.5（RoundtripMismatch 错误码与
 *     RPT-ROUNDTRIP-MISMATCH 建议诊断码）、§4.3.1（ResultRefSnapshot——
 *     "报告引用的结果摘要"的字面承载）、§10.1 RP-ROUND-1（用例矩阵行）
 *   - 需求 RPT-06（支持报告往返复算：从报告数据源可复算并比对一致
 *     （roundtrip），不一致时报告不可用于正式结论）、NFR-COR-04（正式报告
 *     中的每项结论必须能定位到自动输入快照和对应证据——roundtrip 是复现
 *     锚的定期复验面）、AT-22（roundtrip 复算一致——报告往返与格式观测点）
 *   - 任务契约 tasks/foundation/RPT-T16.json acceptance 1~2（RP-ROUND-1
 *     一致/不一致双路用例过；不一致时报告不可用于正式结论）
 *
 * 背景说明（roundtrip 在报告链中的位置——为什么需要它）：
 *   报告是历史快照（§4.2 不可变值对象），而"当前可复算事实"会随输入数据
 *   演化（CON-02 当前性三轴）。roundtripCheck 回答的问题是："这份报告引用
 *   的结果，今天用同样的报告数据源（快照/切片/评估键）重新评估一遍，还会
 *   得到同样的结论吗？"——它是 NFR-COR-04 复现锚的**定期复验面**：证据包
 *   （RPT-03）保证"当时怎么算的可以重放"，roundtrip 保证"重放结果与报告
 *   记载一致"。不一致不代表评估器有错——只说明报告与当前可复算事实脱节
 *   （输入变了/评估器版本变了），此时报告不得再支撑正式结论，必须复核后
 *   重建；历史报告本身不改写（PA-2 不可变历史——检查是纯查询）。
 *
 *   职责边界（§7.7 原文＋§2.1 O-14 行）：reporting 拥有**比对入口与不一致
 *   标记**；复算执行归 execution/各域（③端口评估器）——本头只定义注入
 *   请求接口（IRoundtripRecomputeSource，§3.3 注入模式——零 execution/
 *   io/runtime 编译边，P-RPT-1/P-RPT-2 同款纪律），L5 装配把 execution/
 *   各域的评估通道适配到该接口。判定阈值归 evidence/附录 D（§2.1 不拥有
 *   表）——本头全部比对面为**等值比对**（身份/词表/摘要字节），无数值
 *   容差参与（见 compareFields 常量表注释的逐面依据）。
 *
 * 落位增量声明（DTB §5.4 口径，单元卡 §14.4 v0.19 登记——F-826 勘正）：
 *   §7.7 原文未给出 roundtripCheck 的返回值形状与"复算源不可达"的可用性
 *   口径，本头按以下裁定落地（语义为 §7.7 两态的保守细化，不改变原文
 *   一致/不一致双路语义）：
 *   ①比对面＝ResultRefSnapshot 中"由 envelope 决定"的字段族（§7.2 步③
 *     投影的可逆面）；eligibility/currentness（evidence 纯检查与当前性
 *     投影——时点事实，非评估产物）与 producer（复算时刻的产品版本天然
 *     为当前版）不参与——比对它们会制造假不一致；
 *   ②"复算源不可达"（注入源 nullopt）＝环境形态而非数据事实：该条目
 *     不得判"一致"（无法证明），也不得判"RoundtripMismatch"（没有复算
 *     事实，伪造不一致结论同样违背 NFR-COR-03）；整份报告的
 *     usableForFormalConclusions 取保守 false（宁可不可用不可虚可用——
 *     P-RPT-5 同精神）。两类原因的区分承载：判定级＝RoundtripResultEntry
 *     .recomputed 位（机器可核对）；提示级＝同码面 RPT-ROUNDTRIP-MISMATCH
 *     诊断以**级别与原因文案区分**（不可达＝Warning＋"复算不可达"文案，
 *     真不一致＝Error＋逐字段明细）——reporting 不私设新码面（码值权威归
 *     diagnostics StableCodeRegistry 收编表，P-RPT-8 同纪律）。
 *   ③不一致标记的承载＝RoundtripOutcome 三处：usableForFormalConclusions
 *     位（后续构建的资格核对消费点——true 才可沿用原正式结论）、逐字段
 *     mismatch 明细（定位）、RPT-ROUNDTRIP-MISMATCH 伴随诊断（提示面）。
 *
 * 线程契约：IReportRoundtripChecker::roundtripCheck 并发安全（无共享可变
 *   状态；每次调用独立执行）——前提是注入的 IReportResolver 与
 *   IRoundtripRecomputeSource 各自并发只读安全（实现方承诺，§9.1 同款）。
 * 确定性：同（报告值，复算 envelope 集）→ 同比对结论与 mismatch 明细
 *   （逐字段比对序＝ResultRefSnapshot 字段序——NFR-COR-02 稳定序）。
 */

#ifndef SDURWS_IRD_REPORTING_ROUNDTRIP_HPP
#define SDURWS_IRD_REPORTING_ROUNDTRIP_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/DiagData.hpp>    // core::DiagnosticRecord（伴随提示载体）
#include <sdurws/ird/core/Evaluation.hpp>  // core::EvaluationMode（复算请求模式面）
#include <sdurws/ird/core/Identity.hpp>    // core::RunId/ObjectId（复算请求键与工况）
#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity（快照/切片/摘要）

#include <sdurws/ird/evidence/Envelope.hpp>  // evidence::ResultEnvelope（复算返回值——只读值）

#include <sdurws/ird/reporting/Errors.hpp>      // ReportError/ReportErrorCode（Usage 轨）
#include <sdurws/ird/reporting/Export.hpp>      // IReportResolver（reportId→报告值——复用已冻结注入缝）
#include <sdurws/ird/reporting/Identity.hpp>    // ReportId（被检报告身份）
#include <sdurws/ird/reporting/ReportModel.hpp> // ReviewReport/ResultRefSnapshot（比对基准面）

namespace sdurws::ird::reporting {

// =====================================================================
// 复算请求与注入源（§7.7"复算执行归 execution/各域……经注入的请求接口
// 发起"——reporting 定义最小契约，L5 装配适配）
// =====================================================================

/**
 * @brief 复算请求（§7.7"报告数据源（报告绑定的快照/切片与评估键）"的
 *        结构化承载——从报告 ResultRefSnapshot 抽取的最小重评估描述）。
 *
 * 字段与 §4.3.1 ResultRefSnapshot 的数据源子集一一对应（抽取由实现按
 * §7.2 步③同源规则执行——Builder.cpp envelope→ref 投影的逆用）：复算方
 * （execution/各域适配器）以此定位输入快照（snapshotId）、输入切片
 * （sliceId）、输入基准（inputBaselineId）与评估器（evaluationKey/
 * evaluatorContractVersion/mode），在覆盖工况集（caseScope）上重新派发
 * 评估。runId 也随请求携带——适配器可据此关联原运行归档（诊断定位用；
 * 复算产物是**新** envelope，不写回原运行——PA-2）。
 *
 * 值语义；线程安全：纯值。
 */
struct RoundtripRecomputeRequest {
    /// 报告引用的运行身份（原运行——关联与诊断定位锚；复算不写回）。
    core::RunId runId;
    /// 报告绑定快照身份（非零——复算输入定位键之一）。
    core::ContentIdentity snapshotId;
    /// 报告绑定切片身份（非零——复算输入定位键之一）。
    core::ContentIdentity sliceId;
    /// 输入基准身份（非零——比较基准定位键）。
    core::ContentIdentity inputBaselineId;
    /// 评估键（evidence::EvaluationKey 词形——评估器定位键）。
    std::string evaluationKey;
    /// 评估器契约版本（复算须用同版评估器契约——版本漂移即脱节信号之一，
    /// 由复算方在 envelope 中如实回填、报告侧比对）。
    std::uint32_t evaluatorContractVersion = 0;
    /// 评估模式（Verified/Quick——模式不同的复算结果属不同结论形态）。
    core::EvaluationMode mode = core::EvaluationMode::Verified;
    /// 覆盖工况集（CaseId ≙ core::ObjectId 强类型别名——集合语义，
    /// 顺序不敏感）。
    std::vector<core::ObjectId> caseScope;
};

/**
 * @brief 复算执行注入源（§7.7 原文"复算执行归 execution/各域（reporting
 *        不自建评估调用——经注入的请求接口发起）"的注入承载）。
 *
 * 实现方（L5 装配的 execution/各域适配器）持有评估通道事实面，按请求在
 * 指定快照/切片/评估键上重新派发评估，返回复算产物 envelope（只读值）。
 * reporting 侧**零评估调用实现**（§2.1 O-14 行职责边界）——本接口是
 * reporting 与评估通道之间唯一的类型缝。
 *
 * 失败语义（try 轨约定——§1.4 可恢复查询路径）：复算不可达（评估通道
 * 未装配/输入不可解析/评估执行失败）返回 nullopt，不抛异常；check() 据此
 * 把该条目标记为"复算不可达"（保守不判可用，独立注记诊断——见文件头
 * 落位增量②）。**不返回伪造的 envelope**——复算失败而编造"成功"样本
 * 是最严重的契约违约（NFR-COR-03 不静默的对偶面）。
 *
 * 线程契约：并发只读安全（roundtripCheck 并发安全的前提——§9.1 同款）。
 */
class IRoundtripRecomputeSource {
public:
    virtual ~IRoundtripRecomputeSource() = default;

    /**
     * @brief 按报告数据源重新派发评估（③端口——复算执行的唯一入口）。
     * @param request [in] 复算请求（从被检报告抽取——调用方持有）
     * @return 复算产物 envelope（经 evidence 构造边界 make() 产出的只读
     *         值——reporting 信任其字段即评估事实）；复算不可达＝nullopt
     *         （不抛——可恢复查询路径）
     *
     * 线程安全：并发只读安全。
     */
    virtual std::optional<evidence::ResultEnvelope>
    tryRecompute(const RoundtripRecomputeRequest& request) const = 0;
};

// =====================================================================
// 比对结果值类型（RoundtripMismatch 不一致标记的承载——§7.7"标记于伴随
// 提示与后续构建的资格核对"）
// =====================================================================

/**
 * @brief 逐字段不一致定位（比对面四元组：字段 token＋报告侧期望值＋复算
 *        侧实际值）。
 *
 * fieldToken 为稳定字面（kRoundtripField* 常量族——持久化契约，比对序与
 * 定位锚）；expected/actual 为两侧值的规范文本（身份类型 toCanonical()/
 * core 词表 token——人读定位用，不参与判定本身）。mismatch 不短路：
 * 同一条目全部比对面逐项列出（§8.5 一致性检查器同纪律——全量定位）。
 *
 * 值语义；线程安全：纯值。
 */
struct RoundtripFieldMismatch {
    /// 比对面字段 token（kRoundtripField* 稳定字面——非空）。
    std::string fieldToken;
    /// 报告侧期望值（规范文本——ResultRefSnapshot 对应字段）。
    std::string expected;
    /// 复算侧实际值（规范文本——复算 envelope 对应字段）。
    std::string actual;

    bool operator==(const RoundtripFieldMismatch& o) const
    {
        return fieldToken == o.fieldToken && expected == o.expected && actual == o.actual;
    }
    bool operator!=(const RoundtripFieldMismatch& o) const { return !(*this == o); }
};

/// 比对面字段 token（稳定字面族——持久化契约；比对序＝ResultRefSnapshot
/// 字段序，NFR-COR-02。逐常量注释标注比对面归属，见 compareFields）。
/// @{
inline constexpr std::string_view kRoundtripFieldRunId = "run-id";                       ///< 运行身份
inline constexpr std::string_view kRoundtripFieldTask = "task";                          ///< 任务五元组
inline constexpr std::string_view kRoundtripFieldEvaluationKey = "evaluation-key";       ///< 评估键
inline constexpr std::string_view kRoundtripFieldContractVersion = "contract-version";   ///< 评估器契约版本
inline constexpr std::string_view kRoundtripFieldMode = "mode";                          ///< 评估模式
inline constexpr std::string_view kRoundtripFieldOutcome = "outcome";                    ///< 执行轴结论
inline constexpr std::string_view kRoundtripFieldEngineeringStatus = "engineering-status"; ///< 判定轴结论
inline constexpr std::string_view kRoundtripFieldSnapshotId = "snapshot-id";             ///< 快照绑定
inline constexpr std::string_view kRoundtripFieldSliceId = "slice-id";                   ///< 切片绑定
inline constexpr std::string_view kRoundtripFieldInputBaselineId = "input-baseline-id";  ///< 输入基准绑定
inline constexpr std::string_view kRoundtripFieldCaseScope = "case-scope";               ///< 覆盖工况集
/// @}

/**
 * @brief 逐结果复算条目（RoundtripOutcome.entries 的元素——一份报告的
 *        每个引用结果一行）。
 *
 * 三态正交：recomputed=false（复算不可达——环境形态）时 consistent 恒
 * false（无法证明一致）且 mismatches 恒空（没有复算事实可定位差异）；
 * recomputed=true 时 consistent＝mismatches.empty()。条目不携带复算
 * envelope 本体（避免大对象随结果值扩散；需要人工排查时经 mismatch 明细
 * 与诊断定位）。
 *
 * 值语义；线程安全：纯值。
 */
struct RoundtripResultEntry {
    /// 报告引用的运行身份（与报告 resultRefs 对齐的定位键）。
    core::RunId runId;
    /// 复算是否可达（true＝注入源返回了复算 envelope；false＝nullopt）。
    bool recomputed = false;
    /// 该结果复算是否一致（不可达恒 false——保守；不一致恒 false）。
    bool consistent = false;
    /// 逐字段不一致明细（全量不短路；复算不可达恒空）。
    std::vector<RoundtripFieldMismatch> mismatches;
    /// 复算产物载荷摘要回执（DomainPayload.digest——evidence make() 对
    /// canonicalBytes 重算的 SHA-256；复算不可达＝nullopt）。用途＝复算
    /// 凭据输出（§7.7"envelope digest"的可观测面）：报告侧 §4.3.1 字段表
    /// 不冻结载荷摘要（ResultRefSnapshot 无该列——不虚增报告身份面），
    /// 故摘要不参与一致判定、只随结果输出供调用方/诊断引用。
    std::optional<core::ContentIdentity> recomputedPayloadDigest;

    bool operator==(const RoundtripResultEntry& o) const
    {
        return runId == o.runId && recomputed == o.recomputed && consistent == o.consistent
               && mismatches == o.mismatches && recomputedPayloadDigest == o.recomputedPayloadDigest;
    }
    bool operator!=(const RoundtripResultEntry& o) const { return !(*this == o); }
};

/**
 * @brief 往回复算总结果（roundtripCheck 返回值——§7.7 两态语义的承载＋
 *        不一致标记的三处落点）。
 *
 * 判定语义（§7.7 原文＋落位增量②）：
 *   - consistent＝entries 非空且全部条目 recomputed 且全部条目 consistent
 *     （§7.7"一致→报告维持可用"）；
 *   - usableForFormalConclusions＝consistent（同上真值面）：false 即"报告
 *     不可用于正式结论"的**机器可核对标记**——"后续构建的资格核对"消费点
 *     （后续构建器对新报告做资格核对时可查询历史报告的该标记；本头只
 *     承载值，消费挂接归构建器侧任务）；false 的两种原因由条目
 *     recomputed 位与诊断级别/文案区分（数据事实＝Error＋逐字段明细；
 *     复算不可达＝Warning＋"复算不可达"文案——见文件头落位增量②）；
 *   - "不修改历史报告"（§7.7 原文）：本结果是纯值快照，检查全程零写
 *     路径（结构保证——实现不持有任何写端口；PA-2）。
 *
 * 值语义；线程安全：纯值。
 */
struct RoundtripOutcome {
    /// 被检报告身份（入参回执——结果自定位）。
    ReportId reportId;
    /// 被检报告内容身份（检查基准的凭据锚——调用方可用它核对检查对象
    /// 未被并发替换；同身份同报告值，§4.1 内容寻址）。
    core::ContentIdentity reportContentIdentity;
    /// 全部结果复算可达且一致（§7.7"一致"判定——entries 全称量词）。
    bool consistent = false;
    /// 报告可否用于正式结论（＝consistent——§7.7"不一致→报告不可用于
    /// 正式结论"的标记位；复算不可达保守 false，见 RoundtripResultEntry）。
    bool usableForFormalConclusions = false;
    /// 逐结果条目（与报告 resultRefs 同序同数量——构建器已保证 resultRefs
    /// ≥1，故恒非空）。
    std::vector<RoundtripResultEntry> entries;
    /// 伴随提示（§7.7"标记于伴随提示"——不一致时含 RPT-ROUNDTRIP-MISMATCH
    /// 码面诊断（Error 级＋逐字段明细）；复算不可达条目附同码面 Warning
    /// 注记（"复算不可达"文案——两类原因的区分见文件头落位增量②）；一致
    /// 时为空——零诊断零噪音）。
    std::vector<core::DiagnosticRecord> diagnostics;

    bool operator==(const RoundtripOutcome& o) const
    {
        return reportId == o.reportId && reportContentIdentity == o.reportContentIdentity
               && consistent == o.consistent
               && usableForFormalConclusions == o.usableForFormalConclusions
               && entries == o.entries && diagnostics == o.diagnostics;
    }
    bool operator!=(const RoundtripOutcome& o) const { return !(*this == o); }
};

// =====================================================================
// 比对入口（§7.7 原文——roundtripCheck(reportId)，阶段 C 随 WP-12-T08）
// =====================================================================

/**
 * @brief 报告往返复算检查器接口（§7.7"比对入口归 reporting"的服务级承载）。
 *
 * 契约（§9 章首维度表口径）：
 *   - 前置：reportId 为合法（非保留值）报告身份；reportId 可经注入解析器
 *     解析为冻结报告值（解析不到＝调用方前置违约——IReportResolver 契约
 *     同口径）；
 *   - 后置：返回逐结果比对条目（与报告 resultRefs 同序同数量）＋总判定
 *     ＋伴随诊断；被检报告值与全部项目/归档数据零修改（纯查询——PA-2）；
 *   - 错误：Usage（reportId 保留值/不可解析——调用方违约 fail-fast）；
 *     复算不可达**不是**异常（环境形态走 RoundtripResultEntry.recomputed
 *     ＝false＋注记诊断——NFR-COR-03 的非静默表达）；
 *   - 取消：本检查为同步纯查询（复算耗时由注入源实现方自担——需要取消
 *     的长耗时评估通道应在适配器内桥接 io 令牌，reporting 侧无磁盘/长循
 *     环检查点）。
 *
 * 线程：roundtripCheck 并发安全（无共享可变状态——每次调用独立执行）。
 */
class IReportRoundtripChecker {
public:
    virtual ~IReportRoundtripChecker() = default;

    /**
     * @brief 往回复算检查（§7.7——对 reportId 引用的全部结果逐个复算比对）。
     * @param reportId [in] 被检报告对象身份（"rpt-<32hex>"）
     * @return 比对总结果（值快照——随返回值交付，不驻留本对象）
     *
     * @throws ReportError(ReportErrorCode::Usage) reportId 为保留值，或
     *         注入解析器无法解析该报告（未构建/未发布——调用方前置违约）
     */
    virtual RoundtripOutcome roundtripCheck(ReportId reportId) = 0;
};

/**
 * @brief 往回复算检查器唯一实现（RPT-T16——§7.7 语义的编排体）。
 *
 * 注入面（§3.3 注入模式；构造引用为借用——L5 装配持有实现并保证全部
 * roundtripCheck() 调用期间存活）：
 *   - IReportResolver（Export.hpp 已冻结注入缝——**复用不新建**，NFR-MNT-04
 *     单一实现纪律）：reportId→冻结报告值；报告值来源（本会话构建缓存/
 *     已发布清单加载）归装配侧，与本检查器无耦；
 *   - IRoundtripRecomputeSource（本头定义）：复算执行通道。
 *
 * 执行流程（每报告一次）：
 *   步① 解析报告（resolver.tryResolve——nullopt 即 Usage）；
 *   步② 逐 ResultRefSnapshot 抽取复算请求（§7.2 步③投影的逆用——只取
 *        envelope 决定的字段族）并派发复算（recompute.tryRecompute）；
 *   步③ 逐条目比对（比对面＝身份/绑定面＋关键结论面——compareFields
 *        常量表，全部等值比对无容差）并汇总总判定；
 *   步④ 不一致/不可达条目生成伴随诊断（RPT-ROUNDTRIP-MISMATCH 码面仅在
 *        真不一致时出现——码值权威归 diagnostics StableCodeRegistry，
 *        diagcodes 登记清单引用）。
 *
 * 线程契约：并发安全——全部注入引用只读消费、无共享可变状态。
 */
class ReportRoundtripChecker final : public IReportRoundtripChecker {
public:
    /**
     * @brief 注入构造（借用语义——两注入引用须覆盖全部 roundtripCheck()
     *        调用期间；不接管所有权）。
     *
     * @param resolver  [in] 报告值解析注入缝（borrow——只读消费）
     * @param recompute [in] 复算执行注入源（borrow——只读消费）
     */
    ReportRoundtripChecker(const IReportResolver& resolver,
                           const IRoundtripRecomputeSource& recompute);

    /// 非拷贝（借用语义——拷贝即双属主）。
    ReportRoundtripChecker(const ReportRoundtripChecker&) = delete;
    ReportRoundtripChecker& operator=(const ReportRoundtripChecker&) = delete;

    RoundtripOutcome roundtripCheck(ReportId reportId) override;

private:
    const IReportResolver* m_resolver;        ///< 报告值解析注入缝（借用；只读）
    const IRoundtripRecomputeSource* m_recompute; ///< 复算执行注入源（借用；只读）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_ROUNDTRIP_HPP
