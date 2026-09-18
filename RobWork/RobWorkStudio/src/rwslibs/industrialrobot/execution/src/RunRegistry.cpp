/**
 * @file   RunRegistry.cpp
 * @brief  运行登记表实现——登记/追加尝试/一次性判定（九步 1~7）/终结
 *         标记与接纳镜像维护（§9.1/§9.2/§4.3）。
 *
 * 设计依据：
 *   - units/execution.md §9.1（登记记录全字段）、§9.2 步 1~7（一次性判定
 *     的七道核对——未知 run／五元组失配／陈旧 attempt／绑定不符／类型
 *     不允许／已终结拒绝 FinalEnvelope）、§4.3（身份分配协议——登记点与
 *     appendAttempt 的取代语义）
 *   - ARCHITECTURE.md §4.5（A8：登记表核对——未知 runId／任一字段不符／
 *     陈旧 attempt 拒绝；ARCH §11.2-7 契约测试清单对应项）
 *   - 任务契约 tasks/foundation/EX-T04.json acceptance 1（EX-REG-1/2/3
 *     的判定本体在本文件——拒绝路径的用例见 test/RunRegistryAdmissionTest）
 *
 * 实现口径：本文件只做"查表＋比对"（§10.5 classifyArrival 注——纯查表
 *   无副作用），诊断记录在判定结论里返回、由 ResultAdmission（Admission.cpp）
 *   统一经 sink 上报并计数——判定与上报分离保证 classifyArrival 可被任意
 *   频率调用而不产生重复诊断。
 *
 * 线程约束：仅调度线程（文件头 RunRegistry.hpp——P-PR-4 单侧冻结域）。
 */

#include <sdurws/ird/execution/RunRegistry.hpp>

#include <utility>

namespace sdurws::ird::execution {
namespace {

/// 判定结论快捷构造（拒绝＋原因＋一条开发级稳定码诊断——§9.3"迟到结果
/// 拒绝是否留诊断：是"行的承载；subject 空＝瞬时开发诊断〔core §4.8：
/// 稳定项必带、瞬时开发诊断可空〕）。
AdmissionDecision makeReject(AdmissionDecision::RejectReason reason,
                             const std::string& stableCode, std::string cause)
{
    AdmissionDecision d;
    d.verdict = AdmissionDecision::Verdict::Rejected;
    d.reason = reason;
    // 开发级诊断：稳定码（StableCodeRegistry 已收编的 EX-\* 码面）＋上下文/
    // 原因/建议——三必填串非空（core DiagnosticRecord::make 的 C-3 校验）。
    d.diagnostics.push_back(core::DiagnosticRecord::make(
        stableCode, std::nullopt, std::nullopt, std::nullopt,
        "execution/run-registry", std::move(cause),
        "核对到达信封来源通道与派发登记的一致性；无匹配登记的到达按丢弃处置"));
    return d;
}

}  // namespace

// =====================================================================
// RunRegistry
// =====================================================================

RunRegistry::RunRegistry(UtcClockFn clock)
    : m_clock(std::move(clock))
{
}

RunRegistration* RunRegistry::find(core::RunId run) noexcept
{
    // 哈希查表；未登记＝nullptr（调用方按各自语义处置——判定出 UnknownRun、
    // 镜像推进抛 InvalidState）。
    const auto it = m_runs.find(run);
    return it == m_runs.end() ? nullptr : &it->second;
}

const RunRegistration* RunRegistry::find(core::RunId run) const noexcept
{
    return const_cast<RunRegistry*>(this)->find(run);
}

RunRegistration& RunRegistry::registerRun(TaskId task, const RegistrationInput& input)
{
    // ---- 调用方契约校验（fail-fast——§10.8 接口共性：调用方违约抛出）----
    // 任务/身份无效＝派发编排拼装错误；带病登记会让九步核对失真（核对
    // 基准本身不合法），必须在入口拦截而不是等接纳时暴露。
    if (!task.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: registerRun 的 taskId 无效（保留零值）");
    }
    if (!input.identity.isValid()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: registerRun 的五元组不完整（任一字段为保留值）");
    }
    if (m_runs.count(input.identity.run) != 0) {
        // 同 runId 重复登记＝派发去重失效（同一运行两份登记会让归档位置
        // 出现两个"权威"——A8 语义被破坏），调用方契约违约。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: runId 重复登记（同一运行只能有一份登记事实）");
    }
    if (input.evaluatorKey.empty() || input.runKind.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: evaluatorKey/runKind 为空（RunManifest 透传字段必须非空）");
    }
    if (input.runDir.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: runDir 为空（归档位置取登记记录——空值使 A8 语义无落点）");
    }
    if (input.allowedResultKinds.empty()) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: allowedResultKinds 为空（步 6 核对面不允许空集）");
    }
    if (!input.resources.evaluation || !input.resources.archive) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: 资源上下文缺接纳事实面或归档网关（正式运行的必备协作件）");
    }

    // ---- 入表（节点式容器：记录地址此后稳定——registerRun 返回引用的
    //      有效性由 unordered_map 的节点不搬移性质保证）----
    RunRegistration rec;
    rec.identity = input.identity;
    rec.task = task;
    rec.evaluatorKey = input.evaluatorKey;
    rec.contractVersion = input.contractVersion;
    rec.snapshotId = input.snapshotId;
    rec.sliceId = input.sliceId;
    rec.inputBaselineId = input.inputBaselineId;
    rec.policyIdentity = input.policyIdentity;
    rec.nameMapIdentity = input.nameMapIdentity;
    rec.mode = input.mode;
    rec.runDir = input.runDir;
    rec.runKind = input.runKind;
    rec.allowedResultKinds = input.allowedResultKinds;
    // 登记时刻：注入时钟（测试确定性）；缺省系统 UTC 墙钟（观测面——
    // 不参与任何判定）。
    rec.registeredAtUtc = m_clock ? m_clock() : std::chrono::system_clock::now();
    rec.currentState = input.currentState;
    rec.archivePhase = ArchivePhase::NotApplicable;  // 归档阶段随归档事务推进
    rec.registryVersion = kRegistryVersion;
    // terminationCause/supersededAttempts/resourceContext 按成员默认/移动
    // 语义承接（首次登记无终结、无被取代尝试）。
    rec.resourceContext = input.resources;

    const core::RunId key = input.identity.run;
    return m_runs.emplace(key, std::move(rec)).first->second;
}

void RunRegistry::appendAttempt(core::RunId run, core::AttemptId nextAttempt)
{
    RunRegistration* rec = find(run);
    if (rec == nullptr) {
        // 未登记运行不能追加尝试（尝试号演进只发生在有登记事实的运行上
        // ——§4.3 分配协议）。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: appendAttempt 的 runId 未登记");
    }
    // 新尝试号必须严格前进且从未出现过（回退/复用旧号会让"陈旧 attempt"
    // 判定失去单调依据——§4.3 AttemptId 单调语义）。
    if (!nextAttempt.isValid() || !(rec->identity.attempt < nextAttempt)) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: appendAttempt 的新尝试号必须严格大于当前值");
    }
    for (const core::AttemptId& s : rec->supersededAttempts) {
        if (s == nextAttempt) {
            throw ExecutionError(ExecutionErrorCode::InvalidState,
                                 "execution/run-registry: appendAttempt 的新尝试号曾被取代使用过");
        }
    }
    // 当前 attempt 移入被取代集合（步 4 的迟到判定数据源），登记身份的
    // attempt 字段前移（登记记录从此以新尝试为核对基准）。
    rec->supersededAttempts.push_back(rec->identity.attempt);
    rec->identity.attempt = nextAttempt;
}

AdmissionDecision RunRegistry::classifyArrival(const ArrivalEnvelope& arrival) const
{
    // ---- 九步步 1：以 arrival.identity.run 查登记表 ----
    // 未命中＝未知运行（含跨项目迟到事件）→ UnknownRun（AT-10：绝不写入
    // 任何项目 results/——丢弃处置在接纳侧执行）。
    const RunRegistration* rec = find(arrival.identity.run);
    if (rec == nullptr) {
        return makeReject(AdmissionDecision::RejectReason::UnknownRun,
                          "EX-REGISTRY-UNKNOWN-RUN",
                          "到达五元组的 runId 无登记记录（含跨项目迟到事件）——按未知运行丢弃");
    }

    // ---- 九步步 2：完整身份核对（五字段逐一）----
    // project/branch/revision/run 四字段严格比对（core 精确等值——附录 D
    // 第 12 项：身份无容差）。attemptId 三分支（见 RunRegistry.hpp 接口注）：
    // 等于登记值→通过；属被取代→放行到步 4 出 StaleAttempt；其余→Mismatch。
    if (rec->identity.project != arrival.identity.project) {
        return makeReject(AdmissionDecision::RejectReason::IdentityMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "五元组 project 字段与登记记录不符（通道串扰或伪造到达）");
    }
    if (rec->identity.branch != arrival.identity.branch) {
        return makeReject(AdmissionDecision::RejectReason::IdentityMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "五元组 branch 字段与登记记录不符（通道串扰或伪造到达）");
    }
    if (rec->identity.revision != arrival.identity.revision) {
        return makeReject(AdmissionDecision::RejectReason::IdentityMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "五元组 revision 字段与登记记录不符（结果只归属被评估的原修订）");
    }
    if (rec->identity.attempt != arrival.identity.attempt) {
        // 尝试号不一致：先分辨"陈旧"与"陌生"——被取代的尝试出步 4 的
        // StaleAttempt（更精确的语义），其余出本步 Mismatch。
        bool superseded = false;
        for (const core::AttemptId& s : rec->supersededAttempts) {
            if (s == arrival.identity.attempt) {
                superseded = true;
                break;
            }
        }
        if (!superseded) {
            return makeReject(AdmissionDecision::RejectReason::IdentityMismatch,
                              "EX-REGISTRY-MISMATCH",
                              "五元组 attempt 字段与登记记录不符（非当前亦非被取代尝试）");
        }
        // ---- 九步步 4：陈旧尝试拒绝（携带被取代 attempt 的到达）----
        return makeReject(AdmissionDecision::RejectReason::StaleAttempt,
                          "EX-STALE-ATTEMPT",
                          "到达携带被取代尝试的 attemptId（重试/继续后旧尝试的结果不被接纳）");
    }

    // ---- 九步步 5：第二道绑定核对（开发级——防通道串扰）----
    // 到达自报的绑定/评估键字段与登记扩展字段比对（§9.2 步 5 原文点名
    // snapshotId/sliceId/evaluatorKey/policyIdentity；契约版本与名称映射
    // 同属登记透传面，一并核对——比 §9.2 列举更严不失语义）。
    if (!(rec->snapshotId == arrival.snapshotId)) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：snapshotId 与登记扩展字段不符（防通道串扰）");
    }
    if (!(rec->sliceId == arrival.sliceId)) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：sliceId 与登记扩展字段不符（防通道串扰）");
    }
    if (rec->evaluatorKey != arrival.evaluatorKey) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：evaluatorKey 与登记扩展字段不符（防通道串扰）");
    }
    if (rec->contractVersion != arrival.contractVersion) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：契约版本与登记扩展字段不符（CON-04 绑定面）");
    }
    if (!(rec->policyIdentity == arrival.policyIdentity)) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：policyIdentity 与登记扩展字段不符（CON-06 绑定）");
    }
    if (!(rec->nameMapIdentity == arrival.nameMapIdentity)) {
        return makeReject(AdmissionDecision::RejectReason::BindingMismatch,
                          "EX-REGISTRY-MISMATCH",
                          "绑定核对失败：nameMapIdentity 与登记扩展字段不符（CON-06 绑定）");
    }

    // ---- 九步步 6：结果类型 ∈ allowedResultKinds ----
    // 登记时按 mode 与阶段声明的允许集；越集到达＝协议错误（§9.2 步 6）。
    bool kindAllowed = false;
    for (const ResultKind k : rec->allowedResultKinds) {
        if (k == arrival.kind) {
            kindAllowed = true;
            break;
        }
    }
    if (!kindAllowed) {
        return makeReject(AdmissionDecision::RejectReason::KindNotAllowed,
                          "EX-CHANNEL-PROTOCOL-ERROR",
                          "到达结果类型不在登记允许集内（协议错误）");
    }

    // ---- 九步步 7：终结语义检查（TASK-02 执行侧落点）----
    // 已 Canceled/Failed/ForceTerminated 的登记再收到 FinalEnvelope＝
    // "取消/失败后不得产生完整成功结果"——拒绝且不构造不归档（EX-ORT-1）。
    // 注意：Interrupted（进程崩溃的恢复期标注）不在拒绝集——§9.2 步 7
    // 原文列举三值；Interrupted 运行在恢复期重建后不可能有到达（通道
    // 已随进程消失），无需在此截获。
    if (arrival.kind == ResultKind::FinalEnvelope && rec->terminationCause.has_value()) {
        const TerminationCause t = *rec->terminationCause;
        if (t == TerminationCause::Canceled || t == TerminationCause::Failed
            || t == TerminationCause::ForceTerminated) {
            return makeReject(AdmissionDecision::RejectReason::AlreadyTerminated,
                              "EX-REGISTRY-MISMATCH",
                              "已终结登记（取消/失败/强杀）收到 FinalEnvelope——取消/失败后不得产生完整成功结果");
        }
    }

    // ---- 步 1~7 全绿（步 3"不存在未登记而接纳路径"由步 1 查表结构性
    //      保证——接纳只可能发生在查表命中之后）----
    AdmissionDecision d;
    d.verdict = AdmissionDecision::Verdict::Admitted;
    return d;
}

std::optional<RunRegistration> RunRegistry::tryRun(core::RunId run) const noexcept
{
    const RunRegistration* rec = find(run);
    if (rec == nullptr) {
        return std::nullopt;
    }
    // 整记录拷贝发布（§4.2 同款：查询经快照拷贝——并发只读安全面）。
    return *rec;
}

void RunRegistry::markTerminated(core::RunId run, TerminationCause cause)
{
    RunRegistration* rec = find(run);
    if (rec == nullptr) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: markTerminated 的 runId 未登记");
    }
    if (rec->terminationCause.has_value()) {
        // 终结一次写入（§4.2 termination 行"终态时一次写入"——重复终结
        // 标记＝编排层状态机与登记表失步，调用方契约违约）。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: 登记已带终结标记，不允许二次写入");
    }
    rec->terminationCause = cause;
    // 状态镜像随终结同步（§9.1 currentState 行"随任务状态机同步"）：
    // 强杀在状态轴上是 Failed（TerminationCause 只携带区分标记——TaskTypes
    // 注），Completed 不经本方法（接纳完成走 noteAdmissionCompleted）。
    switch (cause) {
    case TerminationCause::Canceled:
        rec->currentState = core::TaskState::Canceled;
        break;
    case TerminationCause::Failed:
    case TerminationCause::ForceTerminated:
        rec->currentState = core::TaskState::Failed;
        break;
    case TerminationCause::Interrupted:
        rec->currentState = core::TaskState::Interrupted;
        break;
    case TerminationCause::Completed:
        // Completed 不是"终结标记"入口的语义（完成只由接纳 T8 驱动）——
        // 经此写入属编排违约。
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: Completed 终结须经接纳路径（noteAdmissionCompleted）");
    }
}

void RunRegistry::noteAdmissionCompleted(core::RunId run)
{
    RunRegistration* rec = find(run);
    if (rec == nullptr) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: noteAdmissionCompleted 的 runId 未登记");
    }
    // T8 镜像：登记镜像同步为 Completed（任务状态机真实转移归调度编排
    // ——本镜像保证 tryRun 观测面与状态机一致）。
    rec->currentState = core::TaskState::Completed;
}

void RunRegistry::noteArchivePhase(core::RunId run, ArchivePhase phase)
{
    RunRegistration* rec = find(run);
    if (rec == nullptr) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: noteArchivePhase 的 runId 未登记");
    }
    // 只推进归档轴（§5.6 辅轴独立于任务状态——Completed＋ArchiveFailed
    // 的正交组合由两镜像字段分别承载）。
    rec->archivePhase = phase;
}

void RunRegistry::noteArchiveSession(core::RunId run, project::ArchiveSessionRef session)
{
    RunRegistration* rec = find(run);
    if (rec == nullptr) {
        throw ExecutionError(ExecutionErrorCode::InvalidState,
                             "execution/run-registry: noteArchiveSession 的 runId 未登记");
    }
    // 会话句柄回写/清除（§9.1 resourceContext"归档会话引用"的维护面）：
    // 补建 begin 的会话写回，保证后续到达复用同一会话（同一 runDir 绝无
    // 两个活动会话——project begin 的单写者纪律）；finalize/abandon 终结
    // 后由编排以默认构造空句柄回写清除。
    rec->resourceContext.archiveSession = std::move(session);
}

}  // namespace sdurws::ird::execution
