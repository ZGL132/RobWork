/**
 * @file   Admission.cpp
 * @brief  结果接纳编排实现——九步 8~9（名称反解→envelope 组装）、重投递
 *         幂等与内容冲突、归档事务（begin〔A7 防御〕→writeBatch→finalize）
 *         与 ResultArchived 事件发布（§9.2/§9.3/§9.5）。
 *
 * 设计依据：
 *   - units/execution.md §9.2 步 8~9（先反解后接纳〔CON-06〕；aggregateVerdict
 *     →ResultEnvelope::make→validateCombination 单点复用——TASK-02/D-03
 *     接纳唯一权威点）、§9.3（重投递幂等/内容冲突/有界重试 D-11/abandon
 *     责任终结/迟到写 A7 防御）、§9.5（归档协作——P-PR-4 冻结：调度线程
 *     调用＋writer 互斥串行化＋完成事件幂等无上限＋abandon 时机）
 *   - ARCHITECTURE.md §4.5（第二段·按登记记录接纳并归档——归档位置取自
 *     登记记录、不写入当前 HEAD；当前性独立判定归 evidence，HEAD 不参与
 *     接纳判定）、§6.8（A7 存储上下文生命周期的防御路径）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 1~5（EX-REG-4~8、
 *     EX-ORT-1；P-EX-5/P-PR-4/P-EX-3/P-EX-1 处置）
 *
 * 实现口径（登记单元卡 §15.4 的两处细化）：
 *   ①"九步重新执行"的重投递读法：步 1~7 经 classifyArrival 真实重执行；
 *     步 8/9 对相同 canonical 输入是确定性纯计算（NFR-COR-02——同输入必得
 *     同 envelope/同 manifest），指纹一致即短路为 DuplicateIgnored——"不
 *     重写、不报错"由零归档端口调用最强保证，重算无可观测差异。
 *   ②T8 与归档的次序：按 §9.2 文字序"通过→T8→归档触发"执行（envelope
 *     构造成功即 T8）；finalize 的 D-14 幂等比对若在会话内被 bypass（指纹
 *     表为空的跨进程窗口）检出内容冲突，判定面出具 Rejected(ContentConflict)
 *     而任务镜像保持 Completed——冲突语义归属归档内容而非任务状态（两轴
 *     正交，§5.6）。
 *
 * 线程约束：仅调度线程（P-PR-4 单侧冻结——归档端口由调度线程调用，经
 *   project writer 互斥串行化；本文件所有方法无内部锁，单写者纪律由调用
 *   域承担）。
 */

#include <sdurws/ird/execution/RunRegistry.hpp>

#include <sdurws/ird/core/Digest.hpp>      // ContentDigester（CR-02：SHA-256 唯一实现点）
#include <sdurws/ird/evidence/Errors.hpp>  // EvidenceError（make 的非法组合异常——捕获转译）

#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>
#include <utility>

namespace sdurws::ird::execution {
namespace {

// =====================================================================
// 本地帮助（匿名命名空间——实现细节不出单元）
// =====================================================================

/// 字节序列的 SHA-256 十六进制文本（小写 64 字符）。
///
/// CR-02 纪律：摘要算法唯一经 core::ContentDigester（不私设第二哈希）；
/// hex 编码是本单元的本地格式化——core 的 formatDigest 属 detail 命名
/// 空间（R-2 纪律：非公共契约面不跨单元消费），故在此自编码。
std::string sha256Hex(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    // 空字节序列也走同一摘要路径（update(nullptr,0) 契约允许——空追加
    // 不改变摘要状态；统一路径保证"空载荷指纹"确定性）。
    digester.update(bytes.empty() ? nullptr : bytes.data(), bytes.size());
    const core::Digest256 digest = digester.finalize();
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const std::uint8_t b : digest) {
        out.push_back(kHex[b >> 4]);   // 高半字节
        out.push_back(kHex[b & 0x0F]); // 低半字节
    }
    return out;
}

/// system_clock 时刻→ISO-8601 UTC 文本（RunManifest.finalizedAtUtc 字段
/// 契约"ISO-8601 UTC 文本"——project finalize 只透传不重算，格式责任在
/// 调用方）。
std::string iso8601Utc(std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tmValue{};
#ifdef _WIN32
    gmtime_s(&tmValue, &t);   // MSVC 安全版（线程局部分解）
#else
    gmtime_r(&t, &tmValue);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tmValue.tm_year + 1900, tmValue.tm_mon + 1, tmValue.tm_mday,
                  tmValue.tm_hour, tmValue.tm_min, tmValue.tm_sec);
    return buf;
}

/// 接纳侧开发级稳定码诊断（瞬时开发诊断——subject 空，core §4.8 同款；
/// context 固定 "execution/admission" 供日志检索）。
core::DiagnosticRecord admissionDiag(const char* stableCode, std::string cause,
                                     std::string action)
{
    return core::DiagnosticRecord::make(stableCode, std::nullopt, std::nullopt,
                                        std::nullopt, "execution/admission",
                                        std::move(cause), std::move(action));
}

}  // namespace

// =====================================================================
// 构造与注入
// =====================================================================

ResultAdmission::ResultAdmission(RunRegistry& registry, IExecutionDiagnosticsSink& diagnostics,
                                 UtcClockFn clock, Config config, DelayFn delay)
    : m_registry(registry)
    , m_diagnostics(diagnostics)
    , m_clock(std::move(clock))
    , m_config(config)
    , m_delay(std::move(delay))
{
}

void ResultAdmission::setNameResolver(const INameResolverAdapter* resolver) noexcept
{
    m_nameResolver = resolver;
}

void ResultAdmission::setOutputDecoder(const IEvaluationOutputDecoder* decoder) noexcept
{
    m_decoder = decoder;
}

void ResultAdmission::setEventBus(core::IDomainEventBus* bus) noexcept
{
    m_eventBus = bus;
}

ArchivePhase ResultAdmission::archivePhase(core::RunId run) const noexcept
{
    // 只读投影：登记记录的阶段镜像（tryRun 拷贝语义——无共享可变外泄）。
    const std::optional<RunRegistration> rec = m_registry.tryRun(run);
    return rec.has_value() ? rec->archivePhase : ArchivePhase::NotApplicable;
}

std::uint64_t ResultAdmission::rejectedArrivalCount() const noexcept
{
    return m_rejectedArrivals;
}

// =====================================================================
// admitResult——入口分发（步 1~7 后按类型分派）
// =====================================================================

AdmissionOutcome ResultAdmission::admitResult(ArrivalEnvelope&& arrival)
{
    // ---- 调用方契约校验（fail-fast）----
    // 保留值身份不可作到达：九步核对以登记表为唯一基准，一个不合法的
    // 自报身份说明通道拼装错误，属调用方契约违约（AGENTS §3 二分的
    // "调用方错误"侧）——异常拦截而非结构化拒绝。
    if (!arrival.identity.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/admission: 到达五元组不完整（存在保留值字段）");
    }

    // ---- 九步 1~7：登记表一次性判定（纯查表——RunRegistry.cpp）----
    AdmissionDecision decision = m_registry.classifyArrival(arrival);
    const std::optional<RunRegistration> record = m_registry.tryRun(arrival.identity.run);
    if (!record.has_value()) {
        // 防御分支（正常不可达）：classify 判 Admitted 却查无记录＝登记表
        // 在两调用之间被改动——单写者纪律被破坏。按 UnknownRun 丢弃并开
        // 发通道告警，不把不一致状态继续向后传。
        decision.verdict = AdmissionDecision::Verdict::Rejected;
        decision.reason = AdmissionDecision::RejectReason::UnknownRun;
        decision.diagnostics.push_back(admissionDiag(
            "EX-REGISTRY-UNKNOWN-RUN",
            "classify 判定与查表结果不一致（登记表被并发改动？）",
            "检查登记表的单写者纪律（仅调度线程）是否被破坏"));
    }

    // ---- 拒绝路径：上报开发诊断＋丢弃计数；两轴投影不变 ----
    // 拒绝≠任务失败：到达被丢弃，任务状态与归档阶段保持登记镜像（§9.3
    // "迟到结果拒绝是否留诊断：是"＋丢弃计数）。
    if (decision.verdict == AdmissionDecision::Verdict::Rejected) {
        for (const core::DiagnosticRecord& d : decision.diagnostics) {
            m_diagnostics.report(d);
        }
        ++m_rejectedArrivals;
        AdmissionOutcome outcome;
        outcome.decision = std::move(decision);
        outcome.taskStateAfter = record->currentState;
        outcome.archivePhaseAfter = record->archivePhase;
        return outcome;
    }

    // ---- 分类通过（步 1~7 全绿）——按结果类型分派 ----
    switch (arrival.kind) {
    case ResultKind::FinalEnvelope:
        return admitFinalEnvelope(std::move(arrival), *record);
    case ResultKind::ResultBatch:
        return admitResultBatch(std::move(arrival), *record);
    default:
        return admitNonArchivedKind(std::move(arrival), *record);
    }
}

// =====================================================================
// FinalEnvelope——九步 8~9＋T8＋归档＋事件
// =====================================================================

AdmissionOutcome ResultAdmission::admitFinalEnvelope(ArrivalEnvelope&& arrival,
                                                     const RunRegistration& record)
{
    AdmissionOutcome outcome;
    // 分类已过（步 1~7 全绿）——判定结论的缺省面＝Admitted；后续冲突/
    // 组装失败路径显式覆写为对应拒绝值。
    outcome.decision.verdict = AdmissionDecision::Verdict::Admitted;
    const core::RunId run = arrival.identity.run;

    // ---- 重投递幂等/冲突预判（§9.3 重投递行；实现口径①见文件头）----
    // 已接纳过 FinalEnvelope 的运行再收到同类到达：比载荷指纹（canonical
    // 字节相同⇒内容相同）。一致→DuplicateIgnored；不一致→ContentConflict。
    // 两条路径都**零归档端口调用**——"不重写/不覆盖"由不触碰端口最强
    // 保证；完成事件重复投递无次数上限（P-PR-4 冻结义务之二）——每次
    // 重投递都走到这里幂等处置。
    const std::string fingerprint = sha256Hex(arrival.payloadCanon);
    const auto known = m_admittedFinalDigest.find(run);
    if (known != m_admittedFinalDigest.end()) {
        outcome.taskStateAfter = record.currentState;
        outcome.archivePhaseAfter = record.archivePhase;
        if (known->second == fingerprint) {
            outcome.decision.verdict = AdmissionDecision::Verdict::DuplicateIgnored;
            return outcome;  // 幂等成功：不重写、不报错、不发事件
        }
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ContentConflict;
        outcome.decision.diagnostics.push_back(admissionDiag(
            "PRJ-ARCHIVE-CONFLICT",
            "重投递 FinalEnvelope 内容与已归档不一致（D-14 摘要比对的接纳侧映像）",
            "核对回传通道是否存在内容串扰；既有归档保持原样，需变更请重跑产生新运行"));
        m_diagnostics.reportDev("PRJ-ARCHIVE-CONFLICT",
                                "execution/admission: FinalEnvelope 重投递内容冲突（run="
                                    + run.toCanonical() + "）——既有归档不被覆盖");
        ++m_rejectedArrivals;
        return outcome;
    }

    // ---- 步 9 前置：解码到达载荷（通道字节→结构化评估产出）----
    // 解码器未装配或解码失败＝通道数据问题（可预期失败）——结构化拒绝
    // PayloadUndecodable，不走异常。
    if (m_decoder == nullptr) {
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::PayloadUndecodable;
        outcome.decision.diagnostics.push_back(admissionDiag(
            "EX-CHANNEL-PROTOCOL-ERROR",
            "FinalEnvelope 到达但评估产出解码器未装配（IEvaluationOutputDecoder 缺失）",
            "检查 L5 装配清单是否注入通道解码适配器"));
        m_diagnostics.reportDev("EX-CHANNEL-PROTOCOL-ERROR",
                                "execution/admission: 解码器缺失，FinalEnvelope 无法接纳");
        ++m_rejectedArrivals;
        outcome.taskStateAfter = record.currentState;
        outcome.archivePhaseAfter = record.archivePhase;
        return outcome;
    }
    std::optional<evidence::EvaluationOutput> output = m_decoder->tryDecode(arrival.payloadCanon);
    if (!output.has_value()) {
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::PayloadUndecodable;
        outcome.decision.diagnostics.push_back(admissionDiag(
            "EX-CHANNEL-PROTOCOL-ERROR",
            "到达载荷无法解码为 EvaluationOutput（字节非法/版本不符/截断）",
            "核对 worker 通道编码版本与主进程解码器的一致性；损坏载荷按丢弃处置"));
        m_diagnostics.reportDev("EX-CHANNEL-PROTOCOL-ERROR",
                                "execution/admission: 载荷解码失败（run=" + run.toCanonical() + "）");
        ++m_rejectedArrivals;
        outcome.taskStateAfter = record.currentState;
        outcome.archivePhaseAfter = record.archivePhase;
        return outcome;
    }

    // ---- 九步步 8：名称反解（先反解后接纳——CON-06）----
    // 到达材料中显式携带运行时名称的契约面＝诊断记录的 runtimeName 字段
    // （core §4.8"⑥名称端口取得"）；域载荷字节对 execution 不透明（域
    // 契约自解释）。每个 runtimeName 都必须按**登记**的 nameMapIdentity
    // 反解成功（绝不用当前映射——项目切换后映射可能已前进）；任一失败
    // ＝结果不可接纳（NameUnresolved）。反解器未装配而到达含名称：同样
    // 拒绝（fail-closed——无反解能力不放行含名称结果）。
    for (const core::DiagnosticRecord& d : output->diagnostics) {
        if (!d.runtimeName.has_value()) {
            continue;  // 本条诊断不带运行时名称——不在反解面
        }
        const bool resolved = m_nameResolver != nullptr
            && m_nameResolver->tryResolve(record.nameMapIdentity, *d.runtimeName).has_value();
        if (!resolved) {
            outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
            outcome.decision.reason = AdmissionDecision::RejectReason::NameUnresolved;
            outcome.decision.diagnostics.push_back(admissionDiag(
                "EX-REGISTRY-MISMATCH",
                "运行时名称在登记绑定的名称映射下无法反解为对象 ID（CON-06：先反解后接纳）"
                    + (*d.runtimeName),
                "核对运行所绑 RuntimeNameMap 与 worker 使用的映射是否同源；名称漂移请重跑"));
            m_diagnostics.reportDev("EX-REGISTRY-MISMATCH",
                                    "execution/admission: 名称反解失败（run=" + run.toCanonical()
                                        + "，name=" + *d.runtimeName + "）");
            ++m_rejectedArrivals;
            outcome.taskStateAfter = record.currentState;
            outcome.archivePhaseAfter = record.archivePhase;
            return outcome;
        }
    }

    // ---- 九步步 9：envelope 组装（evidence 校验器单点复用——acceptance 3）----
    const RunEvaluationMaterials& materials = *record.resourceContext.evaluation;
    const evidence::AnalysisSnapshot& snapshot = *materials.snapshot;

    // 证据清单组装：EvaluationOutput 只携带**条目集**（§9.3 产出面）——
    // 清单的绑定面（快照/切片/Profile 三元组）属登记事实（绑定三元组自
    // 派发期评估器注册清单查得，见 RunEvaluationMaterials.manifestProfile），
    // 由接纳侧落位成完整 EvidenceManifest；其一致性由汇总②级门禁与包络
    // 构造边界复验（零本地校验逻辑）。
    evidence::EvidenceManifest manifest;
    manifest.snapshotId = record.snapshotId;
    manifest.sliceId = record.sliceId;
    manifest.profileId = materials.manifestProfile.profileId;
    manifest.profileVersion = materials.manifestProfile.version;
    manifest.profileContentIdentity = materials.manifestProfile.contentIdentity;
    manifest.items = output->evidence;

    // 9a. 五级汇总（判定权威在 evidence——execution 只装配输入，不复制
    //     任何判定规则；aggregateVerdict 纯函数、确定性）。
    evidence::VerdictInput verdictInput;
    verdictInput.outcome = core::TaskOutcome::Completed;  // FinalEnvelope 的执行轴结论
    verdictInput.mode = record.mode;
    verdictInput.readiness = materials.readiness;
    verdictInput.snapshotGate = materials.snapshotGate;
    verdictInput.coverage = materials.coverage;
    verdictInput.regionCoverages = materials.regionCoverages;
    verdictInput.proof = output->proof;
    verdictInput.evidence = manifest;
    verdictInput.domain = output->verdictInputs;
    verdictInput.searchRecord = output->searchRecord;
    const evidence::VerdictResult verdict = evidence::aggregateVerdict(
        verdictInput, snapshot, *materials.producers, *materials.profiles);

    // 9b. 组装草稿（字段来源逐项对应：登记记录＝身份/绑定/模式；到达
    //     产出＝证据/凭据/载荷/诊断；汇总＝判定轴/缺失清单/透传凭据）。
    evidence::ResultEnvelopeDraft draft;
    draft.task = record.identity;                    // 接纳绑定登记身份（非自报——步 2 已核对一致）
    draft.evaluationKey = record.evaluatorKey;
    draft.evaluatorContractVersion = record.contractVersion;
    draft.mode = record.mode;
    draft.snapshotId = record.snapshotId;
    draft.sliceId = record.sliceId;
    draft.inputBaselineId = record.inputBaselineId;
    // 工况覆盖范围＝覆盖矩阵中 Executed 的工况集（"本结果覆盖的工况"——
    // §7.1 caseScope 语义；子集核对在 9c 的快照绑定重载执行）。
    for (const evidence::CaseCoverageEntry& entry : materials.coverage.entries) {
        if (entry.status == evidence::CaseExecutionStatus::Executed) {
            draft.caseScope.caseIds.push_back(entry.caseId);
        }
    }
    // Profile 绑定三元组＝登记侧清单绑定面（汇总层已按注册表核对其一致
    // 性——②级门禁；此处只是转写进包络）。
    draft.profile = materials.manifestProfile;
    draft.outcome = core::TaskOutcome::Completed;    // 接纳的 FinalEnvelope 恒 Completed（TASK-02）
    draft.engineeringStatus = verdict.status;        // 判定轴独立——Completed＋DataInsufficient 合法
    draft.evidence = manifest;
    draft.infeasibilityProof = output->proof;
    draft.searchRecord = verdict.searchRecord;       // 汇总层透传（④级凭据）
    draft.missingItems = verdict.missingItems;       // 缺失全量清单（①②④级）
    if (output->payload.has_value()) {
        // 草稿携带无摘要组装形态——载荷摘要由 make() 重算（CR-02：不采信
        // 申报值，重算值才是完整性凭据）。
        draft.payload = evidence::DomainPayloadDraft{output->payload->kindToken,
                                                     output->payload->canonicalBytes};
    }
    draft.diagnostics = output->diagnostics;         // 域诊断保留（步 8 已过反解门）
    draft.diagnostics.insert(draft.diagnostics.end(), verdict.diagnostics.begin(),
                             verdict.diagnostics.end());
    draft.producer.producedIn = materials.producedIn;
    draft.producer.productVersion = snapshot.reproduction.productVersion;  // 复现要素（快照冻结值）

    // 9c. 快照绑定重载校验（make 之前——Envelope.hpp make 注：需要快照
    //     事实面的格子〔工况子集/证明有效性〕由接纳侧先核对；同一校验器
    //     的重载，零复制）。
    const std::vector<evidence::EnvelopeIssue> boundIssues =
        evidence::validateCombination(draft, snapshot, *materials.producers);
    if (!boundIssues.empty()) {
        // 表 3 快照绑定格子违例＝组装校验失败——步 9"make 失败→
        // Running→Failed＋诊断"（T9）；任务转 Failed，不构造不归档。
        // T9 登记镜像同步（终结标记一次写入 Failed——currentState 随之
        // Failed），保证 tryRun 观测面与状态机一致。
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ConstructionFailed;
        for (const evidence::EnvelopeIssue& issue : boundIssues) {
            const core::DiagnosticRecord diag = admissionDiag(
                "EX-SNAPSHOT-STALE",
                "envelope 组装校验拒绝（快照绑定面）：" + issue.message,
                "核对 worker 产出的证据与派发快照的一致性；不一致属评估侧数据问题");
            outcome.decision.diagnostics.push_back(diag);
            m_diagnostics.report(diag);
        }
        m_diagnostics.reportDev("EX-SNAPSHOT-STALE",
                                "execution/admission: 组装校验（快照绑定）失败——任务转 Failed");
        ++m_rejectedArrivals;
        m_registry.markTerminated(run, TerminationCause::Failed);
        outcome.taskStateAfter = core::TaskState::Failed;
        outcome.archivePhaseAfter = ArchivePhase::NotApplicable;
        return outcome;
    }

    // 9d. 构造边界（SA-13——make 是正式结果的唯一入口；上下文无关面在
    //     make 内全量复验）。失败＝T9（同 9c）。
    std::shared_ptr<evidence::ResultEnvelope> envelope;
    try {
        envelope = std::make_shared<evidence::ResultEnvelope>(
            evidence::ResultEnvelope::make(std::move(draft)));
    } catch (const evidence::EvidenceError& e) {
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ConstructionFailed;
        const core::DiagnosticRecord diag = admissionDiag(
            "EX-SNAPSHOT-STALE",
            std::string{"envelope 构造边界拒绝（SA-13）："}.append(e.what()),
            "核对 worker 产出的组合合法性（表 3）；非法组合属评估侧数据问题");
        outcome.decision.diagnostics.push_back(diag);
        m_diagnostics.report(diag);
        m_diagnostics.reportDev("EX-SNAPSHOT-STALE",
                                "execution/admission: make 拒绝——任务转 Failed（T9）");
        ++m_rejectedArrivals;
        m_registry.markTerminated(run, TerminationCause::Failed);
        outcome.taskStateAfter = core::TaskState::Failed;
        outcome.archivePhaseAfter = ArchivePhase::NotApplicable;
        return outcome;
    }

    // ---- T8：接纳通过→Completed（登记镜像同步；任务状态机的真实转移
    //      与 TaskStatusChanged 事件归调度编排——本镜像保证观测面一致）----
    m_registry.noteAdmissionCompleted(run);
    outcome.taskStateAfter = core::TaskState::Completed;

    // ---- 归档触发（§9.5：begin〔预留优先/A7 防御〕→writeBatch→finalize）----
    // 归档产物＝域载荷 canonical 字节（域工件对 project 不透明——D-10
    // 同源）；无载荷的合法组合（如 DataInsufficient）归档空文件工件。
    static const std::vector<std::uint8_t> kEmptyBytes;
    const std::vector<std::uint8_t>& archiveBytes =
        output->payload.has_value() ? output->payload->canonicalBytes : kEmptyBytes;
    const ArchiveResult archive = runArchiveTransaction(record, arrival.identity, archiveBytes,
                                                        "envelope-payload.bin", /*doFinalize=*/true,
                                                        outcome);
    if (archive == ArchiveResult::Finalized) {
        // finalize 成功：指纹入表（后续重投递的幂等判据）→ResultArchived
        // 事件（§9.2"九步之后"段：归档完成后发布；事件不携带路径——core
        // §4.9，消费者经查询端口取数）。总线未装配＝不发布（core D-09：
        // 事件不持久化，装配缺失属 L5 装配面——开发通道留痕一次）。
        m_admittedFinalDigest.emplace(run, fingerprint);
        if (m_eventBus != nullptr) {
            core::ResultArchivedPayload payload;
            payload.task = arrival.identity;
            m_eventBus->publish(core::DomainEvent::make(payload));
        } else if (!m_eventBusAbsenceReported) {
            m_eventBusAbsenceReported = true;
            m_diagnostics.reportDev("execution/admission",
                                    "事件总线未装配：ResultArchived 不发布（core D-09）");
        }
    } else if (archive == ArchiveResult::Conflict) {
        // finalize 的 D-14 比对检出内容冲突（会话内被指纹表 bypass 的跨
        // 进程窗口——实现口径②见文件头）：判定面出具冲突拒绝；任务镜像
        // 保持 Completed（T8 已按 §9.2 次序完成——冲突语义归属归档内容，
        // 两轴正交 §5.6）。
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ContentConflict;
        ++m_rejectedArrivals;
    }
    // Failed/AuthorityLost：接纳成功＋归档失败——任务终态不变（§9.3 末行
    // "Completed 保持；archivePhase=ArchiveFailed"，EX-ARC-5 正交观测）；
    // decision 保持 Admitted，诊断与阶段投影已在事务内写入。
    outcome.envelope = std::move(envelope);
    return outcome;
}

// =====================================================================
// ResultBatch——批次落盘（批次级重投递幂等/冲突——§9.3）
// =====================================================================

AdmissionOutcome ResultAdmission::admitResultBatch(ArrivalEnvelope&& arrival,
                                                   const RunRegistration& record)
{
    AdmissionOutcome outcome;
    // 分类已过——缺省面＝Admitted（冲突路径显式覆写，见下）。
    outcome.decision.verdict = AdmissionDecision::Verdict::Admitted;
    const core::RunId run = arrival.identity.run;

    // ---- 批次级重投递预判（§9.3"批次级重投递：目标已存在且摘要一致→
    //      跳过；不一致→冲突拒绝"的接纳侧映像——同 FinalEnvelope 的
    //      零端口调用口径；project writeBatch 的幂等/冲突机制仍是第二道
    //      防线，contract 测试在端口粒度另验）----
    const std::string fingerprint = sha256Hex(arrival.payloadCanon);
    const auto known = m_batchDigest.find(run);
    if (known != m_batchDigest.end()) {
        outcome.taskStateAfter = record.currentState;
        outcome.archivePhaseAfter = record.archivePhase;
        if (known->second == fingerprint) {
            outcome.decision.verdict = AdmissionDecision::Verdict::DuplicateIgnored;
            return outcome;
        }
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ContentConflict;
        outcome.decision.diagnostics.push_back(admissionDiag(
            "PRJ-ARCHIVE-CONFLICT",
            "重投递批次内容与已落盘批次不一致（D-14）——既有文件不被覆盖",
            "核对回传通道批次序与内容一致性；既有批次保持原样"));
        m_diagnostics.reportDev("PRJ-ARCHIVE-CONFLICT",
                                "execution/admission: 批次重投递内容冲突（run=" + run.toCanonical()
                                    + "）——既有文件不被覆盖");
        ++m_rejectedArrivals;
        return outcome;
    }

    // ---- 首见批次：归档事务（批次只增、不 finalize——"完整"标志只由
    //      FinalEnvelope 的 finalize 发布，D-13）----
    const ArchiveResult archive = runArchiveTransaction(record, arrival.identity,
                                                        arrival.payloadCanon,
                                                        "result-batch.bin", /*doFinalize=*/false,
                                                        outcome);
    if (archive == ArchiveResult::BatchWritten) {
        m_batchDigest.emplace(run, fingerprint);
        outcome.decision.verdict = AdmissionDecision::Verdict::Admitted;
    } else if (archive == ArchiveResult::Conflict) {
        outcome.decision.verdict = AdmissionDecision::Verdict::Rejected;
        outcome.decision.reason = AdmissionDecision::RejectReason::ContentConflict;
        ++m_rejectedArrivals;
    }
    // Failed/AuthorityLost：接纳成功＋归档失败（同 FinalEnvelope 口径——
    // 诊断与阶段投影在事务内写入；任务状态由调度域持有，此处投影不变）。
    outcome.taskStateAfter = record.currentState;
    return outcome;
}

// =====================================================================
// 其余类型——登记核对面处置（本任务边界，见注释）
// =====================================================================

AdmissionOutcome ResultAdmission::admitNonArchivedKind(ArrivalEnvelope&& arrival,
                                                       const RunRegistration& record)
{
    // CheckpointBatch/Progress/PartialDataOnTerminate 三类到达的九步核对
    // （1~7）已由 classifyArrival 完成；其后续处置的权威在各自组件（PA-1
    // ——不在接纳编排里伪造第二持久化路径）：
    //   - CheckpointBatch：检查点持久化归 ICheckpointCoordinator（§10.6，
    //     EX-T08 落位时消费本表的分类结论接线）；
    //   - Progress：进度走 execution 自有通道与查询投影（§6.1——进度不
    //     入领域事件，core D-09）；
    //   - PartialDataOnTerminate：终结时部分数据经 envelope 的
    //     PartialDataRef 随终结路径处置（§7.1——诊断性保留，不复用）。
    // 本编排对它们出具 Admitted（核对面通过）＋归档阶段 NotApplicable＋
    // 开发通道留痕——留痕供通道健康观测（§9.3 丢弃计数同源动机）。
    AdmissionOutcome outcome;
    outcome.decision.verdict = AdmissionDecision::Verdict::Admitted;
    outcome.taskStateAfter = record.currentState;
    outcome.archivePhaseAfter = record.archivePhase;
    m_diagnostics.reportDev("execution/admission",
                            std::string{"到达类型经登记核对面放行（持久化归各自组件）：kind="}
                                .append(std::to_string(static_cast<int>(arrival.kind)))
                                .append("，run=")
                                .append(arrival.identity.run.toCanonical()));
    return outcome;
}

// =====================================================================
// 归档事务（§9.5：begin〔A7 防御〕→writeBatch→finalize；D-11 有界重试）
// =====================================================================

ResultAdmission::ArchiveResult ResultAdmission::runArchiveTransaction(
    const RunRegistration& record, const core::TaskIdentity& identity,
    const std::vector<std::uint8_t>& payloadBytes, const std::string& itemName,
    bool doFinalize, AdmissionOutcome& outcome)
{
    const core::RunId run = record.identity.run;
    IRunArchiveGateway& gateway = *record.resourceContext.archive;
    // 当前写面（指针形态——A7 重取成功后重绑到网关交付的新写面；引用
    // 无法重绑定，见下方 A7 防御分支）。
    project::IResultArchivePort* port = &gateway.port();

    // 退避等待（D-11"退避 1 s/4 s"：第 1 次重试等 1 s、第 2 次等 4 s——
    // 指数底 4、封顶 cap）。DelayFn 缺省 sleep_for（生产形态）；测试注入
    // no-op 虚拟推进——不真实等待（testkit §6.5 同纪律）。
    const auto backoff = [this](std::uint32_t attemptIndex) {
        std::chrono::milliseconds wait = m_config.archiveBackoffInitial;
        for (std::uint32_t i = 0; i < attemptIndex && wait < m_config.archiveBackoffCap; ++i) {
            wait *= 4;
        }
        if (wait > m_config.archiveBackoffCap) {
            wait = m_config.archiveBackoffCap;
        }
        if (m_delay) {
            m_delay(wait);
        } else {
            std::this_thread::sleep_for(wait);
        }
    };

    // ---- ① 会话取得：预留优先，缺失补建（§9.5"派发期预留"行）----
    // Preparing 段的空会话预留由调度派发建立并写入登记记录（EX-T05）；
    // EX-T04 直纳路径（无预留）在此补建。复用预留绝不二次 begin——同一
    // runDir 的并发第二会话被 project 拒绝（单写者纪律）。
    project::ArchiveSessionRef session = record.resourceContext.archiveSession;
    if (!session) {
        project::ArchiveRequest request;
        request.task = identity;
        request.runDir = record.runDir;   // 归档位置取登记记录原值（A8——不重新推导）
        request.runKind = record.runKind;
        request.evaluationKey = record.evaluatorKey;
        try {
            session = port->begin(request);
        } catch (const project::StoreError& e) {
            if (e.code() != project::StoreErrorCode::ContextClosed) {
                // 非"上下文关闭"类 begin 失败（环境/数据错误）：责任尚未
                // 建立（无会话可 abandon）——EX-ARCHIVE-FAILED 透传
                // StoreError 细节＋阶段 ArchiveFailed；任务终态不变。
                const core::DiagnosticRecord diag = admissionDiag(
                    "EX-ARCHIVE-FAILED",
                    std::string{"归档 begin 失败："}.append(e.what()),
                    "检查存储介质与项目写锁状态；结果可重跑");
                outcome.decision.diagnostics.push_back(diag);
                m_diagnostics.report(diag);
                m_diagnostics.reportDev("EX-ARCHIVE-FAILED",
                                        "execution/admission: begin 失败（run=" + run.toCanonical()
                                            + "）：" + e.what());
                m_registry.noteArchivePhase(run, ArchivePhase::ArchiveFailed);
                outcome.archivePhaseAfter = ArchivePhase::ArchiveFailed;
                return ArchiveResult::Failed;
            }
            // ---- A7 防御（§9.3/ARCH §6.8）----
            // 上下文已释放的迟到写：正常不应发生（execution 持引用期间
            // 上下文不 Closed），防御路径＝先尝试重新取得写权限（重新
            // open writable）；成功→照常归档；失败（锁被他持/介质只读）
            // →拒绝归档＋EX-ARCHIVE-AUTHORITY-LOST（结果不落盘，用户可
            // 重跑；开发诊断记录丢失细节）。
            if (!gateway.reacquireWriteAuthority()) {
                const core::DiagnosticRecord diag = admissionDiag(
                    "EX-ARCHIVE-AUTHORITY-LOST",
                    "存储上下文已 Closed 且重新取得写权限失败（A7 防御）——归档被拒绝，结果不落盘",
                    "结果可重跑；检查项目写锁归属与介质只读状态");
                outcome.decision.diagnostics.push_back(diag);
                m_diagnostics.report(diag);
                m_diagnostics.reportDev("EX-ARCHIVE-AUTHORITY-LOST",
                                        "execution/admission: 迟到归档重取权限失败（run="
                                            + run.toCanonical() + "）");
                m_registry.noteArchivePhase(run, ArchivePhase::ArchiveFailed);
                outcome.archivePhaseAfter = ArchivePhase::ArchiveFailed;
                return ArchiveResult::AuthorityLost;
            }
            try {
                // 重取成功后从网关取**新**写面重试 begin（旧端口引用随旧
                // 上下文失效——重绑定是本防御路径的一部分）。
                session = gateway.port().begin(request);
                port = &gateway.port();
            } catch (const project::StoreError& e2) {
                const core::DiagnosticRecord diag = admissionDiag(
                    "EX-ARCHIVE-AUTHORITY-LOST",
                    std::string{"重新取得写权限后 begin 仍失败（A7 防御）："}.append(e2.what()),
                    "结果可重跑；检查存储上下文重建后的白名单与权限");
                outcome.decision.diagnostics.push_back(diag);
                m_diagnostics.report(diag);
                m_diagnostics.reportDev("EX-ARCHIVE-AUTHORITY-LOST",
                                        "execution/admission: 重取后 begin 失败（run="
                                            + run.toCanonical() + "）：" + e2.what());
                m_registry.noteArchivePhase(run, ArchivePhase::ArchiveFailed);
                outcome.archivePhaseAfter = ArchivePhase::ArchiveFailed;
                return ArchiveResult::AuthorityLost;
            }
        }
        // 补建会话回写登记记录（§9.1 resourceContext"归档会话引用"——
        // 后续到达复用同一会话；finalize/abandon 后回写空句柄清除）。
        m_registry.noteArchiveSession(run, session);
        m_registry.noteArchivePhase(run, ArchivePhase::Reserved);
    }

    // ---- ② 批次写入（有界重试 D-11；冲突不重试——数据冲突非瞬态）----
    project::ArchiveBatch batch;
    batch.items.push_back(project::ArchiveItem{itemName, payloadBytes});
    for (std::uint32_t attempt = 0;; ++attempt) {
        const project::ArchiveStatus st = port->writeBatch(session, batch);
        if (st.ok) {
            break;
        }
        const project::StoreErrorCode code = st.error->code();
        if (code == project::StoreErrorCode::ArchiveConflict) {
            // 内容冲突（D-14）：不覆盖既有文件；活动运行的会话保留（运行
            // 可继续、终结路径负责回收），已完成运行的会话立即以 Failed
            // abandon 终结（不会再有后续写入——不存在无 abandon 的终结路径）。
            if (record.currentState == core::TaskState::Completed) {
                port->abandon(session, project::ArchiveEndReason::Failed);
                m_registry.noteArchiveSession(run, {});
            }
            const core::DiagnosticRecord diag = admissionDiag(
                "PRJ-ARCHIVE-CONFLICT",
                "批次写入检出内容冲突（D-14：目标已存在且摘要不一致）——既有文件不被覆盖",
                "核对回传通道批次内容与序号一致性；既有数据保持原样");
            outcome.decision.diagnostics.push_back(diag);
            m_diagnostics.report(diag);
            m_diagnostics.reportDev("PRJ-ARCHIVE-CONFLICT",
                                    "execution/admission: writeBatch 冲突（run=" + run.toCanonical()
                                        + "，item=" + itemName + "）");
            return ArchiveResult::Conflict;
        }
        if (attempt >= m_config.archiveRetries) {
            // 有界重试耗尽：abandon(Failed) 终结归档责任（§9.3 末行——
            // "责任终结＝finalize 成功或 abandon"）＋EX-ARCHIVE-FAILED
            // 透传 StoreError 细节；任务终态不变（调用方保持 Completed）。
            port->abandon(session, project::ArchiveEndReason::Failed);
            m_registry.noteArchiveSession(run, {});
            const core::DiagnosticRecord diag = admissionDiag(
                "EX-ARCHIVE-FAILED",
                std::string{"批次写入在有界重试后仍失败："}.append(st.error->what()),
                "检查磁盘空间与介质状态；任务结果保持（归档可经重跑恢复）");
            outcome.decision.diagnostics.push_back(diag);
            m_diagnostics.report(diag);
            m_diagnostics.reportDev("EX-ARCHIVE-FAILED",
                                    "execution/admission: writeBatch 重试耗尽（run="
                                        + run.toCanonical() + "）：" + st.error->what());
            m_registry.noteArchivePhase(run, ArchivePhase::ArchiveFailed);
            outcome.archivePhaseAfter = ArchivePhase::ArchiveFailed;
            return ArchiveResult::Failed;
        }
        backoff(attempt);
    }
    m_registry.noteArchivePhase(run, ArchivePhase::Archiving);
    outcome.archivePhaseAfter = ArchivePhase::Archiving;

    // ---- 批次路径收尾（无 finalize）----
    if (!doFinalize) {
        // 已完成运行的迟到批次（跨进程恢复窗口）：写完即以 Completed
        // abandon 终结会话——"不经 finalize 而以 Completed abandon 是
        // execution 侧声明'以 abandon 终结且原因记完成'的形态"（project
        // ArchiveEndReason 注）；活动运行保留会话给后续批次/最终 finalize。
        if (record.currentState == core::TaskState::Completed) {
            port->abandon(session, project::ArchiveEndReason::Completed);
            m_registry.noteArchiveSession(run, {});
        }
        return ArchiveResult::BatchWritten;
    }

    // ---- ③ finalize：manifest 原子发布＝"完整"（D-13 唯一完整性判据；
    //      §9.5"分批与最终发布"行）----
    project::RunManifest manifest;
    manifest.taskIdentity = identity;                // 登记五元组（project 仍校验与会话一致）
    manifest.items.push_back(project::RunManifestItem{
        itemName, sha256Hex(payloadBytes), static_cast<std::uint64_t>(payloadBytes.size())});
    manifest.runKind = record.runKind;
    manifest.evaluationKey = record.evaluatorKey;
    manifest.finalizedAtUtc = iso8601Utc(m_clock ? m_clock() : std::chrono::system_clock::now());
    // manifestDigest 留空——由 project 在 finalize 计算回填（"自身规范化
    // 摘要"，调用方该字段不参与——ArchivePort finalize 注）。
    for (std::uint32_t attempt = 0;; ++attempt) {
        const project::ArchiveStatus st = port->finalize(session, manifest);
        if (st.ok) {
            break;
        }
        const project::StoreErrorCode code = st.error->code();
        if (code == project::StoreErrorCode::ArchiveConflict) {
            // D-14 摘要不一致（跨进程窗口的冲突面）：abandon(Failed) 终结
            // 责任——既有 manifest 保持权威，本次内容被拒绝（不覆盖）。
            port->abandon(session, project::ArchiveEndReason::Failed);
            m_registry.noteArchiveSession(run, {});
            const core::DiagnosticRecord diag = admissionDiag(
                "PRJ-ARCHIVE-CONFLICT",
                "finalize 检出 manifest 摘要与既有归档不一致（D-14）——既有发布保持权威",
                "核对重投递内容与既有归档差异；需变更请产生新运行");
            outcome.decision.diagnostics.push_back(diag);
            m_diagnostics.report(diag);
            m_diagnostics.reportDev("PRJ-ARCHIVE-CONFLICT",
                                    "execution/admission: finalize 冲突（run="
                                        + run.toCanonical() + "）");
            return ArchiveResult::Conflict;
        }
        if (attempt >= m_config.archiveRetries) {
            // 重试耗尽：abandon(Failed)＋EX-ARCHIVE-FAILED（透传细节）；
            // 任务终态不变（Completed 保持——§9.3 末行/EX-ARC-5）。
            port->abandon(session, project::ArchiveEndReason::Failed);
            m_registry.noteArchiveSession(run, {});
            const core::DiagnosticRecord diag = admissionDiag(
                "EX-ARCHIVE-FAILED",
                std::string{"finalize 在有界重试后仍失败："}.append(st.error->what()),
                "检查磁盘空间与介质状态；任务已完成（归档残留按未完整处置）");
            outcome.decision.diagnostics.push_back(diag);
            m_diagnostics.report(diag);
            m_diagnostics.reportDev("EX-ARCHIVE-FAILED",
                                    "execution/admission: finalize 重试耗尽（run="
                                        + run.toCanonical() + "）：" + st.error->what());
            m_registry.noteArchivePhase(run, ArchivePhase::ArchiveFailed);
            outcome.archivePhaseAfter = ArchivePhase::ArchiveFailed;
            return ArchiveResult::Failed;
        }
        backoff(attempt);
    }

    // ---- finalize 成功：会话已终结（句柄失效）——回写清除；阶段 Archived ----
    m_registry.noteArchiveSession(run, {});
    m_registry.noteArchivePhase(run, ArchivePhase::Archived);
    outcome.archivePhaseAfter = ArchivePhase::Archived;
    return ArchiveResult::Finalized;
}

}  // namespace sdurws::ird::execution
