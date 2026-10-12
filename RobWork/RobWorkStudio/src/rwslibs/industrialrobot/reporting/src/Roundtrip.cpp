/**
 * @file   Roundtrip.cpp
 * @brief  报告往返复算实现——roundtripCheck 编排（解析→抽取→派发复算→
 *         逐字段比对→总判定与伴随诊断）（RPT-T16 产物，§7.7）。
 *
 * 设计依据（对齐 units/reporting.md）：
 *   - §7.7（设计权威，语义逐句承接）：比对入口归 reporting；复算执行归
 *     execution/各域（经注入请求接口——Roundtrip.hpp IRoundtripRecompute-
 *     Source）；"一致→报告维持可用；不一致→报告不可用于正式结论
 *     （RoundtripMismatch……不修改历史报告）"；
 *   - §4.3.1（ResultRefSnapshot＝"报告引用的结果摘要"——比对基准面）；
 *     §7.2 步③（envelope→ref 投影——比对面取"由 envelope 决定"的字段族，
 *     Builder.cpp 同源投影的逆用）；
 *   - §3.5（RoundtripMismatch 码/RPT-ROUNDTRIP-MISMATCH 建议诊断码）；
 *   - 需求 RPT-06（往返复算——不一致时报告不可用于正式结论）、NFR-COR-04
 *     （复现锚定期复验）、NFR-COR-03（不静默——复算不可达必须显式注记）；
 *   - 任务契约 tasks/foundation/RPT-T16.json acceptance 1~2。
 *
 * 落位增量（DTB §5.4 口径，单元卡 §14.4 v0.20 登记——两处裁定）：
 *   ①比对面与不比对面清单（Roundtrip.hpp 文件头"落位增量声明①"）：比对
 *     全部为等值判定（身份/词表/字节），无数值容差参与——"按附录 D 容差"
 *     的容差面在本域为空集（报告引用的结果摘要不含浮点量；附录 D 第 12 项
 *     精神：身份/事实字段无容差）；
 *   ②复算不可达的可用性口径（Roundtrip.hpp 文件头"落位增量声明②"）：
 *     recomputed=false 的条目恒不判一致、整报告 usableForFormalConclusions
 *     取保守 false，但**不**出具 RoundtripMismatch 码面（没有复算事实，
 *     伪造不一致结论同样违背 NFR-COR-03）——以独立 Warning 注记诊断区分。
 *
 * 错误语义归类（AGENTS.md §2.5）：
 *   - reportId 保留值 / 报告不可解析＝**调用方契约违约**——Usage fail-fast
 *     （异常轨，不走诊断收集）；
 *   - 复算源不可达＝**环境错误形态**——结果值内如实登记（recomputed=false
 *     ＋注记诊断），不抛异常、不静默、不伪造判定；
 *   - 复算结果与报告不一致＝**数据事实**（非错误轨）——RoundtripMismatch
 *     判定＋RPT-ROUNDTRIP-MISMATCH 伴随诊断（Error 级），调用方决策。
 *
 * 线程安全：全部函数无共享可变状态（注入引用只读消费）——roundtripCheck
 *   并发安全（Roundtrip.hpp 线程契约）。
 * 确定性：比对序＝ResultRefSnapshot 字段序（§4.3.1 表序）；mismatch 文本
 *   用规范形（toCanonical/词表 token）；无时钟/随机/locale 依赖（NFR-COR-02
 *   ——同输入同结论）。
 */

#include <sdurws/ird/reporting/Roundtrip.hpp>   // 本单元公共头（include 根——目标 include 路径）

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace sdurws::ird::reporting {
namespace {

// =====================================================================
// 内部辅助：诊断工厂与比对原语（匿名命名空间——编译单元私有，不出单元）
// =====================================================================

/**
 * @brief 往回复算伴随诊断工厂（§7.7"标记于伴随提示"的上报载体）。
 *
 * 码值纪律：code 由调用方传入 diagcodes 登记清单常量（kRoundtripMismatch
 * ＝"RPT-ROUNDTRIP-MISMATCH"——diagnostics 收编表登记值的**引用**，码值
 * 权威归 diagnostics StableCodeRegistry——PA-1；Errors.hpp 码清单声明）。
 * core make() 的 C-3 校验（码句法＋必填串非空）在此兜底：常量码面＋调用
 * 点提供的文案，违约即实现缺陷 fail-fast。
 *
 * @param code   [in] 稳定诊断码字面（diagcodes::* 常量）
 * @param run    [in] 结果定位（逐结果诊断携带来源运行；可空＝报告级诊断）
 * @param cause  [in] 原因（非空——C-3 校验）
 * @param action [in] 建议动作（非空——C-3 校验）
 * @param level  [in] 阻断轴级别（Mismatch＝Error；复算不可达注记＝Warning
 *               ——环境形态不阻断报告链，仅提示）
 * @return 诊断记录（context 固定标注往返复算——可定位到 §7.7）
 */
core::DiagnosticRecord roundtripDiagnostic(std::string_view code, std::optional<core::RunId> run,
                                           std::string cause, std::string action,
                                           core::DiagnosticLevel level)
{
    return core::DiagnosticRecord::make(
        std::string(code),
        std::nullopt,   // subject：复算诊断的定位锚是运行（run 参数）而非
                        // 项目对象——伪造 ObjectId 反而误导追溯（Sections.cpp
                        // 范围级诊断同口径）
        std::nullopt,   // localName：无（运行身份即定位）
        std::nullopt,   // runtimeName：⑥名称端口反解归 ui——本检查不越权
        "报告往返复算（RPT-06/§7.7）",
        std::move(cause),
        std::move(action),
        std::nullopt,   // comparison：非比较型诊断
        level);
}

/// 取身份规范文本（ContentIdentity/Id128 族——比较失败时的定位文本）。
std::string canonicalOf(const core::ContentIdentity& id) { return id.toCanonical(); }

/// 取 core 词表值的稳定 token 文本（比对失败定位用——词面权威在 core）。
std::string tokenOf(core::TaskOutcome v) { return std::string(core::toToken(v)); }
std::string tokenOf(core::EngineeringStatus v) { return std::string(core::toToken(v)); }
std::string tokenOf(core::EvaluationMode v) { return std::string(core::toToken(v)); }

/**
 * @brief 覆盖工况集的集合语义等值判定（顺序不敏感——caseScope 是集合，
 *        构建锚定序不构成语义）。
 *
 * 策略：两侧拷贝后按规范文本字典序排序再逐元素比较（O(n log n)，n＝工况
 * 数——复算检查为低频查询路径，非热点；稳定排序保证同集合同序——NFR-COR-02）。
 * 排序键用规范文本而非字节——与 mismatch 定位文本同源，避免"判定用一种
 * 序、报告用另一种序"的双轨。
 */
bool caseScopeEqual(std::vector<core::ObjectId> lhs, std::vector<core::ObjectId> rhs)
{
    const auto key = [](const core::ObjectId& id) { return id.toCanonical(); };
    std::sort(lhs.begin(), lhs.end(),
              [&key](const core::ObjectId& a, const core::ObjectId& b) {
                  return key(a) < key(b);
              });
    std::sort(rhs.begin(), rhs.end(),
              [&key](const core::ObjectId& a, const core::ObjectId& b) {
                  return key(a) < key(b);
              });
    return lhs == rhs;
}

/**
 * @brief 追加一条不等值 mismatch（统一拼装——全量不短路纪律的载体）。
 *
 * @param mismatches [in,out] 明细表（就地追加）
 * @param fieldToken [in] 比对面字段 token（kRoundtripField* 稳定字面）
 * @param expected   [in] 报告侧规范文本
 * @param actual     [in] 复算侧规范文本
 */
void appendMismatch(std::vector<RoundtripFieldMismatch>& mismatches, std::string_view fieldToken,
                    const std::string& expected, const std::string& actual)
{
    mismatches.push_back(RoundtripFieldMismatch{std::string(fieldToken), expected, actual});
}

}  // namespace

// =====================================================================
// ReportRoundtripChecker——构造与主流程
// =====================================================================

ReportRoundtripChecker::ReportRoundtripChecker(const IReportResolver& resolver,
                                               const IRoundtripRecomputeSource& recompute)
    : m_resolver(&resolver), m_recompute(&recompute)
{
}

RoundtripOutcome ReportRoundtripChecker::roundtripCheck(ReportId reportId)
{
    // ---- 前置校验（调用方契约违约＝Usage fail-fast——§9 维度表错误行）----

    // reportId 保留值（全零）＝调用方违约：空身份没有可解析对象，
    // fail-fast 而非返回"不可用"结果值（后者会掩盖调用方错误）。
    if (!reportId.isValid()) {
        throw ReportError(ReportErrorCode::Usage,
                          "roundtripCheck: reportId 为保留值（全零）——无被检对象");
    }

    // ---- 步①：解析报告值（复用 IReportResolver 已冻结注入缝——解析不到
    // ＝调用方前置违约 Usage，Export.hpp 契约同口径；不在此私有重建第二
    // 套报告查找，NFR-MNT-04 单一实现）----
    const std::optional<ReviewReport> resolved = m_resolver->tryResolve(reportId);
    if (!resolved.has_value()) {
        throw ReportError(ReportErrorCode::Usage,
                          "roundtripCheck: reportId 不可解析（未构建/未发布）: "
                              + reportId.toCanonical());
    }
    const ReviewReport& report = *resolved;

    // 结果总壳（身份回执先填——后续条目逐个追加；"不修改历史报告"由纯值
    // 语义＋零写路径结构保证：本类不持有任何写端口，PA-2）。
    RoundtripOutcome outcome;
    outcome.reportId = report.reportId();
    outcome.reportContentIdentity = report.contentIdentity();

    // 逐结果条目与 resultRefs 同序（条目定位键＝runId——调用方按序对齐
    // 报告引用面）。
    outcome.entries.reserve(report.resultRefs().size());

    // ---- 步②~③：逐结果抽取复算请求→派发复算→逐字段比对 ----
    // 比对面取"由 envelope 决定"的字段族（§7.2 步③投影的可逆面）；比对序
    // ＝ResultRefSnapshot 字段序（§4.3.1 表序——NFR-COR-02 稳定序）。逐面
    // 依据（Roundtrip.hpp 文件头落位增量①的展开）：
    //   - 身份/绑定面（run-id/task/evaluation-key/contract-version/mode/
    //     snapshot-id/slice-id/input-baseline-id/case-scope）：报告数据源的
    //     定义面——复算 envelope 在任一面偏离即"报告与当前可复算事实脱节"
    //     （§7.7 不一致的语义本体）；附录 D 第 12 项精神——身份/引用无容差；
    //   - 关键结论面（outcome/engineering-status）：core 词表值（非数值），
    //     严格等值——词表值没有容差概念；
    //   - 不比对：eligibility/currentness（evidence 纯检查与当前性投影——
    //     时点事实，非评估产物，§6.2/§6.3）；producer（复算时刻产品版本
    //     天然为当前版——比对会制造假不一致）；payload digest（报告侧
    //     §4.3.1 字段表无冻结位——作为复算凭据随条目回执输出，不虚增比对）。
    for (const ResultRefSnapshot& ref : report.resultRefs()) {
        RoundtripResultEntry entry;
        entry.runId = ref.runId;

        // ---- 步②：抽取复算请求（§7.2 步③投影的逆用——只取 envelope
        // 决定的字段；eligibility/currentness 是检查/投影结果，不在请求内
        // ——复算方拿到的是"当时的评估输入"，不是"当时的判定上下文"）。
        RoundtripRecomputeRequest request;
        request.runId = ref.runId;
        request.snapshotId = ref.snapshotId;
        request.sliceId = ref.sliceId;
        request.inputBaselineId = ref.inputBaselineId;
        request.evaluationKey = ref.evaluationKey;
        request.evaluatorContractVersion = ref.evaluatorContractVersion;
        request.mode = ref.mode;
        request.caseScope = ref.caseScope;

        // ---- 派发复算（执行归 execution/各域——本处零评估调用实现）。
        // nullopt＝复算不可达（环境形态）：该条目保守不判一致、不出
        // MISMATCH 码面（无复算事实可定位差异），以 Warning 注记诊断
        // 区分（NFR-COR-03 不静默；落位增量②）。
        const std::optional<evidence::ResultEnvelope> recomputed =
            m_recompute->tryRecompute(request);
        if (!recomputed.has_value()) {
            entry.recomputed = false;
            entry.consistent = false;   // 无法证明一致——保守（宁可不可用不可虚可用）
            outcome.diagnostics.push_back(roundtripDiagnostic(
                diagcodes::kRoundtripMismatch,
                ref.runId,
                "结果 " + ref.runId.toCanonical() + " 复算不可达（复算源未产出结果）"
                    "——无法证明报告与当前可复算事实一致",
                "检查复算通道（execution/各域评估适配）装配与输入快照可达性，"
                "恢复后重新执行往返复算（本条目不计为复算不一致）",
                core::DiagnosticLevel::Warning));
            outcome.entries.push_back(std::move(entry));
            continue;
        }

        // ---- 步③：逐字段比对（全量不短路——§8.5 一致性检查器同纪律；
        // expected＝报告侧，actual＝复算侧）。
        const evidence::ResultEnvelope& env = *recomputed;
        entry.recomputed = true;
        entry.recomputedPayloadDigest =
            env.payload.has_value()
                ? std::optional<core::ContentIdentity>{env.payload->digest}
                : std::nullopt;   // 非 Completed 复算结果无载荷（表 3 行 3）——
                                  // 回执如实缺席；载荷面差异已由结论面字段
                                  // （outcome/engineeringStatus）比对覆盖

        // ①运行身份（条目定位键；复算 envelope 的五元组携带运行）。
        if (!(env.task.run == ref.runId)) {
            appendMismatch(entry.mismatches, kRoundtripFieldRunId, ref.runId.toCanonical(),
                           env.task.run.toCanonical());
        }
        // ②任务五元组（project/branch/revision/run/attempt 逐字节——
        // 含运行身份，与①可重复定位：全量不短路纪律接受冗余明细）。
        if (!(env.task == ref.task)) {
            appendMismatch(entry.mismatches, kRoundtripFieldTask,
                           ref.task.project.toCanonical() + "/"
                               + ref.task.branch.toCanonical() + "/"
                               + ref.task.revision.toCanonical() + "/"
                               + ref.task.run.toCanonical() + "/attempt-"
                               + std::to_string(ref.task.attempt.value),
                           env.task.project.toCanonical() + "/"
                               + env.task.branch.toCanonical() + "/"
                               + env.task.revision.toCanonical() + "/"
                               + env.task.run.toCanonical() + "/attempt-"
                               + std::to_string(env.task.attempt.value));
        }
        // ③评估键（词形权威 evidence——本处只做等值与定位）。
        if (env.evaluationKey != ref.evaluationKey) {
            appendMismatch(entry.mismatches, kRoundtripFieldEvaluationKey, ref.evaluationKey,
                           env.evaluationKey);
        }
        // ④评估器契约版本（复算必须同版评估器契约——版本漂移即脱节信号）。
        if (env.evaluatorContractVersion != ref.evaluatorContractVersion) {
            appendMismatch(entry.mismatches, kRoundtripFieldContractVersion,
                           std::to_string(ref.evaluatorContractVersion),
                           std::to_string(env.evaluatorContractVersion));
        }
        // ⑤评估模式（Verified/Quick——模式不同属不同结论形态，§8.1 表 1）。
        if (env.mode != ref.mode) {
            appendMismatch(entry.mismatches, kRoundtripFieldMode, tokenOf(ref.mode),
                           tokenOf(env.mode));
        }
        // ⑥执行轴结论（Completed/Canceled/Failed/Interrupted）。
        if (env.outcome != ref.outcome) {
            appendMismatch(entry.mismatches, kRoundtripFieldOutcome, tokenOf(ref.outcome),
                           tokenOf(env.outcome));
        }
        // ⑦判定轴结论（Feasible/DataInsufficient/EngineeringInfeasible/
        // NotApplicable——关键结论字段的核心面）。
        if (env.engineeringStatus != ref.engineeringStatus) {
            appendMismatch(entry.mismatches, kRoundtripFieldEngineeringStatus,
                           tokenOf(ref.engineeringStatus), tokenOf(env.engineeringStatus));
        }
        // ⑧⑨⑩绑定面三元组（快照/切片/输入基准——报告数据源定位键；
        // 任一偏离＝复算用的不是报告绑定的输入，§7.7"脱节"的直接证据）。
        if (!(env.snapshotId == ref.snapshotId)) {
            appendMismatch(entry.mismatches, kRoundtripFieldSnapshotId,
                           canonicalOf(ref.snapshotId), canonicalOf(env.snapshotId));
        }
        if (!(env.sliceId == ref.sliceId)) {
            appendMismatch(entry.mismatches, kRoundtripFieldSliceId, canonicalOf(ref.sliceId),
                           canonicalOf(env.sliceId));
        }
        if (!(env.inputBaselineId == ref.inputBaselineId)) {
            appendMismatch(entry.mismatches, kRoundtripFieldInputBaselineId,
                           canonicalOf(ref.inputBaselineId), canonicalOf(env.inputBaselineId));
        }
        // ⑪覆盖工况集（集合语义——顺序不敏感；漏算/多算工况都改变结论的
        // 覆盖范围，属结论面差异）。
        if (!caseScopeEqual(env.caseScope.caseIds, ref.caseScope)) {
            std::string expected = "[";
            for (std::size_t i = 0; i < ref.caseScope.size(); ++i) {
                if (i > 0) {
                    expected += ",";
                }
                expected += ref.caseScope[i].toCanonical();
            }
            std::string actual = "[";
            for (std::size_t i = 0; i < env.caseScope.caseIds.size(); ++i) {
                if (i > 0) {
                    actual += ",";
                }
                actual += env.caseScope.caseIds[i].toCanonical();
            }
            appendMismatch(entry.mismatches, kRoundtripFieldCaseScope, expected + "]",
                           actual + "]");
        }

        // 条目判定：无 mismatch＝一致（recomputed 已为 true）。
        entry.consistent = entry.mismatches.empty();
        outcome.entries.push_back(std::move(entry));

        // 逐条目伴随诊断（真不一致才出 RPT-ROUNDTRIP-MISMATCH 码面——
        // Error 级；mismatch 明细全量列于条目，诊断只给定位与建议）。
        if (!outcome.entries.back().consistent) {
            std::string detail = "结果 " + ref.runId.toCanonical() + " 往回复算不一致（";
            const std::vector<RoundtripFieldMismatch>& ms = outcome.entries.back().mismatches;
            for (std::size_t i = 0; i < ms.size(); ++i) {
                if (i > 0) {
                    detail += "；";
                }
                detail += ms[i].fieldToken + ": 期望 " + ms[i].expected + "，复算 "
                          + ms[i].actual;
            }
            detail += ")——报告与当前可复算事实脱节";
            outcome.diagnostics.push_back(roundtripDiagnostic(
                diagcodes::kRoundtripMismatch, ref.runId, std::move(detail),
                "该报告不可用于正式结论（RoundtripMismatch）：复核输入变化或评估器"
                "契约演化后重建报告；历史报告保留不改写（PA-2）",
                core::DiagnosticLevel::Error));
        }
    }

    // ---- 步④：总判定（§7.7 两态语义＋落位增量②——全称量词：任一条目
    // 不一致或复算不可达，整报告即不判一致；usableForFormalConclusions 与
    // consistent 同真值——"报告维持可用"的唯一形态是"全部结果复算一致"）。
    outcome.consistent = true;
    for (const RoundtripResultEntry& e : outcome.entries) {
        if (!e.recomputed || !e.consistent) {
            outcome.consistent = false;
            break;
        }
    }
    outcome.usableForFormalConclusions = outcome.consistent;
    return outcome;
}

}  // namespace sdurws::ird::reporting
