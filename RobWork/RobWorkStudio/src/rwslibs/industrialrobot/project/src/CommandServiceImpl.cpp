/**
 * @file   CommandServiceImpl.cpp
 * @brief  命令服务实现——S1～S7 生命周期编排（§6.1）、命令执行槽串行、
 *         确认放行决策映射（§6.7）与双编译事务编排（§6.6）的实现体。
 *
 * 设计依据：见私有头 CommandServiceImpl.hpp（文件头全量登记设计锚点、
 *   实现口径①～⑥与 P-PR-9 阻断面）；本文件注释聚焦每段编排的"为什么"
 *   与失败路径去向。
 *
 * 错误语义（错误二分——AGENTS §3/单元卡 §5.0；与公共头总表一致）：
 *   - 边界拒绝/中止 → CommandResult（Rejected/Aborted——零修订）；
 *   - 环境与数据侧失败 → Failed＋error（StoreError 透传——executeCommit
 *     门卫与 TxEngine 契约不吞不改）；执行中 ContextClosed 例外映射为
 *     Aborted(context-closing)（§6.7 排空中止——实现口径②）；
 *   - 装配违约 → std::invalid_argument fail-fast（requiresDualCompile
 *     而端口未注入；HandlerRegistry 重复注册）。
 */

#include "CommandServiceImpl.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/core/Identity.hpp>

#include "DiagRecords.hpp"
#include "ProjectStoreImpl.hpp"
#include "win32/StoreLock.hpp"  // win32::utcNowIsoMilli——提交时间戳供给
                                // （与打开/创建路径同一时钟面——单元内
                                // 唯一时间格式来源）

namespace sdurws::ird::project {

// =====================================================================
// HandlerRegistry——pimpl 与注册语义（§5.3.5）
// =====================================================================

/**
 * @brief 注册表本体：token→处理器的有序映射＋互斥。
 *
 * 背景说明（两个实现取舍）：
 *   1. std::map＋std::less<>（透明比较器）——find(string_view) 走异质
 *      查找（C++17），查找路径零临时 string 分配，noexcept 契约下不引入
 *      分配失败终止面；map 的有序性同时使 registeredCommandTypes 天然
 *      字典序（NFR-COR-02 确定性——同注册集必得同输出）。
 *   2. 互斥保护读面：find 被查询端口的 hasUnresolvedPayload 判据在查询
 *      线程消费（§5.2 增量落位说明 3——T10 注入后真判定），与 submit
 *      线程的 S1 查找并发；注册互斥下的写面仅装配期发生（§5.3.5"运行
 *      期只读"——运行期注册是装配纪律违约，本锁不为其背书）。
 */
struct HandlerRegistry::Impl {
    /// token→处理器（键序即输出序；所有权唯一归注册表）。
    std::map<std::string, std::unique_ptr<ICommandHandler>,
             std::less<>>
        handlers;
    /// 读面互斥（find/registeredCommandTypes 并发消费——见上）。
    mutable std::mutex mutex;
};

HandlerRegistry::HandlerRegistry()
    : m_impl(std::make_unique<Impl>())
{
}

HandlerRegistry::~HandlerRegistry() = default;

void HandlerRegistry::registerHandler(std::unique_ptr<ICommandHandler> handler)
{
    // 装配期 fail-fast（实现口径⑥）：空处理器/空 token＝装配错误，尽早
    // 暴露（注册表面无稳定码——§5.0 封闭集不私扩）。
    if (handler == nullptr) {
        throw std::invalid_argument(
            "project/command: registerHandler 非法——handler 为空");
    }
    const std::string token = handler->commandType();
    if (token.empty()) {
        throw std::invalid_argument(
            "project/command: registerHandler 非法——commandType 为空"
            "（处理器注册 token 是提交与历史浏览的唯一解析键——§4.4.4）");
    }
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    // 重复 token＝边界拒绝（§5.3.5 注释原文）：同一 token 两个处理器会
    // 使 S1 解析歧义（提交语义不确定）——装配期拒绝而非静默覆盖。
    if (!m_impl->handlers.emplace(token, std::move(handler)).second) {
        throw std::invalid_argument(
            "project/command: registerHandler 重复 token=" + token
            + "（§5.3.5——重复 commandType 注册边界拒绝）");
    }
}

ICommandHandler* HandlerRegistry::find(std::string_view commandType) const noexcept
{
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    const auto it = m_impl->handlers.find(commandType);  // 透明比较——零分配
    return it == m_impl->handlers.end() ? nullptr : it->second.get();
}

std::vector<std::string> HandlerRegistry::registeredCommandTypes() const
{
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    std::vector<std::string> tokens;
    tokens.reserve(m_impl->handlers.size());
    for (const auto& entry : m_impl->handlers) {
        tokens.push_back(entry.first);  // map 键序＝字典序（NFR-COR-02）
    }
    return tokens;
}

std::size_t HandlerRegistry::size() const noexcept
{
    std::lock_guard<std::mutex> guard(m_impl->mutex);
    return m_impl->handlers.size();
}

// =====================================================================
// HandlerContext——prepare 执行上下文（§5.3.2）
// =====================================================================

HandlerContext::HandlerContext(IProjectQueryPort& queryPort,
                               IModelCompilePort* compilePort,
                               ICommandInteraction* interaction) noexcept
    : m_query(queryPort)
    , m_compilePort(compilePort)
    , m_interaction(interaction)
{
}

core::ObjectId HandlerContext::objectId()
{
    // 对象身份分配（§4.6 创建段：处理器申请新 ObjectId——project 分配，
    // core.md §4.1 分配列）。core generate 使用 thread_local 引擎，多线
    // 程安全；命令槽保证同上下文串行，编号冲突不可能（128 位随机空间）。
    return core::ObjectId::generate();
}

// =====================================================================
// CommandServiceImpl——构造与端口契约
// =====================================================================

CommandServiceImpl::CommandServiceImpl(ProjectStoreImpl& host,
                                       IModelCompilePort* compilePort,
                                       IDiagnosticsSink* sink) noexcept
    : m_host(host)
    , m_compilePort(compilePort)
    , m_sink(sink)
{
}

CommandServiceImpl::~CommandServiceImpl() = default;

std::vector<std::string> CommandServiceImpl::registeredCommandTypes()
    const noexcept
{
    // 冻结签名 noexcept（§5.3.1）：结果小（token 数量级），分配失败即
    // terminate——登记于公共头（实现口径，DTB §5.4）。确定性排序由注册
    // 表投影保证（NFR-COR-02）。
    return m_registry.registeredCommandTypes();
}

// =====================================================================
// submit——S1～S7 线性编排（§6.1 生命周期原文的逐步落地）
// =====================================================================

CommandResult CommandServiceImpl::submit(const CommandEnvelope& envelope,
                                         ICommandInteraction* interaction)
{
    // ---- 命令执行槽（§6.1"每存储上下文一个命令执行槽（互斥）"）：
    //      全程持锁——S3～S6 的处理器代码、确认等待与编译都在槽内；
    //      并发 submit 在此排队（PRJ-TX-1"第二个等待"）。
    std::lock_guard<std::mutex> slotGuard(m_slot);

    // ---- 在途票据（§9.7 引用持有者清单）：本命令在途期间 requestClose
    //      的排空等待不完成——票据析构（本函数任何返回路径）释放引用并
    //      可能触发排空收尾。票据在槽内获取、随栈帧析构——生命周期与
    //      命令严格同界。
    std::shared_ptr<void> inFlight = m_host.acquireInFlight();

    CommandResult result;
    ICommandHandler* handler = nullptr;
    RevisionView baseView;

    // ---- StoreError 的统一映射面（私有头实现口径②）：S1～S6 的查询/
    //      门卫/事务失败在此收敛——ContextClosed＝执行中被关闭（§6.7
    //      排空中止）；其余环境/数据侧码＝Failed 透传。处理器与 core 的
    //      非 StoreError 异常不捕获（处理器缺陷＝调用方错误 fail-fast——
    //      §5.3.2 契约"不吞不改"）。
    try {
        // [S1] 形式校验（type→payload 版本→branch→writable）。
        if (!runFormChecks(envelope, interaction, handler, result)) {
            return result;
        }
        // [S2] 基线解析与并发校验（expectedRevision vs tip——§6.2）。
        if (!resolveBaseline(envelope, baseView, result)) {
            return result;
        }
        // [S3] prepare ／ [S4] 确认放行 ／ [S5] 双编译 ／ [S6] 事务提交
        // （S7 事件发布在事务第 6 步内——TxEngine，D-18 口径）。
        executePlan(envelope, *handler, baseView, interaction, result);
        return result;
    } catch (const StoreError& e) {
        // 失败重建结果（半填充的中间态不外泄——Result 四态互斥）。
        result = CommandResult{};
        if (e.code() == StoreErrorCode::ContextClosed) {
            // 执行中排空（门卫拒绝）＝中止而非拒绝：命令已过 S1 形式
            // 校验、被会话关闭打断（§6.7"context-closing 路径"）。
            // error 保留原码供开发定位（Aborted 面附 error＝实现口径②）。
            result.status.kind = CommandStatus::Kind::Aborted;
            result.status.abort = CommandStatus::Abort::ContextClosing;
            result.error = e;
            return result;
        }
        result.status.kind = CommandStatus::Kind::Failed;
        result.error = e;
        return result;
    }
}

// =====================================================================
// S1 形式校验（§6.1 步 1；§6.3 前两行的拒绝落点）
// =====================================================================

bool CommandServiceImpl::runFormChecks(const CommandEnvelope& envelope,
                                       ICommandInteraction* /*interaction*/,
                                       ICommandHandler*& handler,
                                       CommandResult& result)
{
    // ---- 关闭态先行（writable 检查的第一半）：上下文 Closed 后查询面
    //      进入拒绝态（§4.7），若不先分流，后续 currentMetadata 会以
    //      ContextClosed 异常绕过"边界拒绝"的形态。诊断码＝
    //      PRJ-WRITE-AUTHORITY-LOST（executeCommit 门卫同表——diagrec
    //      共享工厂，P-PR-6 链路）。
    if (m_host.closed()) {
        if (m_sink != nullptr) {
            m_sink->report(diagrec::makeWriteAuthorityLost(
                "project/command: S1 写拒绝（上下文已关闭） branch="
                + envelope.branch.toCanonical()));
        }
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::NotWritable;
        result.error = StoreError(StoreErrorCode::ContextClosed,
                                  "project/command: context closed（S1）");
        return false;
    }

    // ---- token 已注册（§6.3 行 1：未知 commandType → 提交边界拒绝）。
    //      未注册＝调用方拼错/未装配——无对应收编用户码（不私造，CR-08），
    //      机器可读面由 status.rejection 承载。
    handler = m_registry.find(envelope.commandType);
    if (handler == nullptr) {
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::UnknownCommand;
        return false;
    }

    // ---- payload 版本受理（§6.3 行 2/§6.4）：受理集合＝
    //      {currentPayloadVersion()}（文件头"增量落位说明 3"——冻结接口
    //      的可表达形态）。不受理的历史载荷可能仍可读（浏览侧 unresolved
    //      判据在查询端口），但提交面一律拒绝——写路径只收当前格式。
    if (envelope.payloadFormatVersion != handler->currentPayloadVersion()) {
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::InvalidPayload;
        return false;
    }

    // ---- branch 存在于权威元数据（§6.1 S1 第三项；INV-M3 权威纪律——
    //      只读 HEAD 引用版本）。不存在＝实现口径①：Rejected(invalid-
    //      payload)（封闭集无"未知分支"稳定码——§5.2 branchHistory 先例
    //      同口径）；开发诊断保留定位信息（dev 通道不进用户目录）。
    const ProjectMetadataView meta = m_host.query().currentMetadata();
    bool branchKnown = false;
    for (const BranchRecord& b : meta.record.branches) {
        if (b.branchId == envelope.branch) {
            branchKnown = true;
            break;
        }
    }
    if (!branchKnown) {
        if (m_sink != nullptr) {
            m_sink->reportDev(
                "project/command",
                "S1 拒绝：目标分支不在权威分支表 branch="
                    + envelope.branch.toCanonical());
        }
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::InvalidPayload;
        return false;
    }

    // ---- writable（§6.1 S1 第四项/§9.6 门卫的形式校验半边）：只读上下
    //      文（从未持锁）→ PRJ-LOCK-HELD；持锁但失权 →
    //      PRJ-WRITE-AUTHORITY-LOST。error 携带对应门卫稳定码（实现口径
    //      ②）——调用方可按 §5.0 码面细分类。权威裁决仍在 executeCommit
    //      门卫（S1 只做提前拒绝——两道防线串联，§9.6①②）。
    if (!m_host.writable()) {
        const bool readOnlyContext = m_host.readOnlyContext();
        if (m_sink != nullptr) {
            m_sink->report(readOnlyContext
                               ? diagrec::makeLockHeld(
                                   "project/command: S1 写拒绝（只读上下文）"
                                   " branch=" + envelope.branch.toCanonical())
                               : diagrec::makeWriteAuthorityLost(
                                   "project/command: S1 写拒绝（写权限已"
                                   "丢失） branch="
                                       + envelope.branch.toCanonical()));
        }
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::NotWritable;
        result.error = readOnlyContext
            ? StoreError(StoreErrorCode::LockHeldByOther,
                         "project/command: readonly context（S1）")
            : StoreError(StoreErrorCode::WriteRejected,
                         "project/command: write authority lost（S1）");
        return false;
    }
    return true;
}

// =====================================================================
// S2 基线解析与并发校验（§6.2）
// =====================================================================

bool CommandServiceImpl::resolveBaseline(const CommandEnvelope& envelope,
                                         RevisionView& baseView,
                                         CommandResult& result)
{
    // 权威元数据自 HEAD 引用版本读取（INV-M3——禁止经历史修订重建）；
    // 查询端口在命令槽内与提交互斥（QueryPortImpl 一致性窗口），快照即
    // 提交期地面事实——"解析→提交之间无竞争窗口"（§6.2）由此成立。
    const ProjectMetadataView meta = m_host.query().currentMetadata();
    std::optional<core::RevisionId> tip;
    for (const BranchRecord& b : meta.record.branches) {
        if (b.branchId == envelope.branch) {
            tip = b.tipRevisionId;
            break;
        }
    }
    // S1 已验证分支存在——此处 nullopt 属防御面（不可达），按数据侧拒绝。
    if (!tip.has_value()) {
        throw StoreError(StoreErrorCode::StoreCorrupt,
                         "project/command: 权威分支表缺失目标分支 branch="
                             + envelope.branch.toCanonical());
    }

    // ---- 过期基线判定（§6.2）：显式 expectedRevision ≠ tip →
    //      Rejected(stale-revision)＋PRJ-STALE-REVISION-REJECTED（§6.3
    //      表"过期修订"行）。差异定位数据（detail）＝分支级（expected/
    //      actual tip 键值——§6.2"附当前 tip 与差异定位数据"；对象级差异
    //      由消费方经②端口比对两修订引用集）。双通道交付：结果内诊断＋
    //      sink 用户级上报（与打开协议 RecoveryReport 同款形态）。
    if (envelope.expectedRevision.has_value()
        && !(*envelope.expectedRevision == *tip)) {
        const core::DiagnosticRecord stale = diagrec::makeStaleRevision(
            "project/command: expected=" + envelope.expectedRevision->toCanonical()
                + " actual-tip=" + tip->toCanonical() + " branch="
                + envelope.branch.toCanonical());
        if (m_sink != nullptr) {
            m_sink->report(stale);
        }
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::StaleRevision;
        result.diagnostics.push_back(stale);
        return false;
    }

    // 基线修订视图（expected 缺省＝tip——提交期解析后等同显式，§6.2）。
    // tip 取自权威分支表、会话索引只装闭包内修订（§7.3 装载纪律）——
    // nullopt 属数据侧异常面（悬挂 tip），由外层 StoreError 映射承载。
    baseView = m_host.query().revision(*tip);
    return true;
}

// =====================================================================
// S3～S6 计划装配与事务提交（§5.3.2/§5.3.4/§6.6/§6.8）
// =====================================================================

void CommandServiceImpl::executePlan(const CommandEnvelope& envelope,
                                     ICommandHandler& handler,
                                     const RevisionView& baseView,
                                     ICommandInteraction* interaction,
                                     CommandResult& result)
{
    // ---- [S3] 处理器 prepare（业务校验＋计划——§5.3.2）。上下文按次
    //      构造（objectId 分配/查询/编译端口/确认回调四个访问面）；业务
    //      断言全在处理器内（P-PR-3 处置——本类不持有业务数值）。
    HandlerContext ctx(m_host.query(), m_compilePort, interaction);
    CommandPlan plan;
    std::vector<core::DiagnosticRecord> prepareDiags;
    const PrepareOutcome outcome
        = handler.prepare(ctx, envelope, baseView, plan, prepareDiags);

    switch (outcome) {
    case PrepareOutcome::Planned:
        break;
    case PrepareOutcome::RejectedHardAssert:
        // 硬断言失败（§6.3 行 3）：就地阻止＋精确定位诊断随结果回传
        // （MDL-06）——无修订。
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection
            = CommandStatus::Rejection::HardAssertFailed;
        result.diagnostics = std::move(prepareDiags);
        return;
    case PrepareOutcome::RejectedInvalidInput:
        // 非法输入（§6.3 行 2 后半）：处理器域内逐项诊断原样回传。
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection
            = CommandStatus::Rejection::InvalidPayload;
        result.diagnostics = std::move(prepareDiags);
        return;
    }

    // 计划声明面防御（R2 预留字段——文件头"增量落位说明 1"）：改名随
    // R2 命令族与元数据增量扩展落位；当前产出即拒绝，防静默丢弃。
    if (plan.metadataChange.has_value()
        && plan.metadataChange->newDisplayName.has_value()) {
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::InvalidPayload;
        return;
    }
    // 逆命令声明成对性（§6.9——可逆性声明由 type＋载荷成对构成；单边
    // 出现＝畸形计划）。
    if (plan.inverseCommandType.has_value()
        != plan.inversePayloadCanonical.has_value()) {
        result.status.kind = CommandStatus::Kind::Rejected;
        result.status.rejection = CommandStatus::Rejection::InvalidPayload;
        return;
    }

    // ---- [S4] 确认放行（§5.3.3/§5.3.4/§6.7）。待确认集为空＝直接放行
    //      （SA-15——无 finding 无需交互）。等待持槽零事务资源（§5.3.4）：
    //      此时尚未创建 .staging、未写任何文件。
    if (!plan.confirmableFindings.empty()) {
        // 非交互提交（测试/后台通道）＋待确认集 → 阻止应用（§6.7 末行
        // ——未确认不落盘）。
        if (interaction == nullptr) {
            result.status.kind = CommandStatus::Kind::Rejected;
            result.status.rejection
                = CommandStatus::Rejection::ConfirmationsUnresolved;
            result.findings = std::move(plan.confirmableFindings);
            return;
        }
        // 回调失效（§5.3.3：isAlive==false → interaction-lost）。
        if (!interaction->isAlive()) {
            result.status.kind = CommandStatus::Kind::Aborted;
            result.status.abort = CommandStatus::Abort::InteractionLost;
            return;
        }
        // 同步回调（命令执行线程——ui 实现 Marshar 到 UI 线程等结果；
        // 抛出＝回调失效——§5.3.3"调用抛出 → Aborted(interaction-lost)"）。
        std::optional<std::vector<core::ConfirmationCredential>> decisions;
        try {
            decisions = interaction->requestConfirmations(
                plan.confirmableFindings);
        } catch (...) {
            result.status.kind = CommandStatus::Kind::Aborted;
            result.status.abort = CommandStatus::Abort::InteractionLost;
            return;
        }
        // 整体拒绝（nullopt）或任一缺失/不对应（§5.3.3"返回空或任一
        // rejected → confirmations-rejected"）→ 阻止应用＋回传未决集。
        if (!decisions.has_value()
            || decisions->size() != plan.confirmableFindings.size()) {
            result.status.kind = CommandStatus::Kind::Rejected;
            result.status.rejection
                = CommandStatus::Rejection::ConfirmationsRejected;
            result.findings = std::move(plan.confirmableFindings);
            return;
        }
        // 逐项确认（core C-2：Confirmed⇔凭据——状态推进在数据面留痕，
        // 凭据绑定复核与 command.json 留痕随 PRJ-T11——§12 分工）。
        for (std::size_t i = 0; i < plan.confirmableFindings.size(); ++i) {
            plan.confirmableFindings[i].confirm((*decisions)[i]);
        }
    }

    // ---- [S5] 双编译（§6.6——requiresDualCompile 声明面）。编译先于
    //      S6：失败时无任何已发布内容需要回退（"回滚＝丢弃内存计划与
    //      临时目录"，MDL-06 口径）；无修订、暂存区未创建。
    if (plan.requiresDualCompile) {
        if (m_compilePort == nullptr) {
            // 装配违约（实现口径——私有头）：处理器声明编译但 L5 未注入
            // 端口＝装配错误，fail-fast（不是运行期稳定码面）。
            throw std::invalid_argument(
                "project/command: requiresDualCompile 但 IModelCompilePort "
                "未注入（§5.3.6 L5 装配面——装配违约 fail-fast）");
        }
        // 编译输入＝计划闭包（baseSnapshot 引用集＋plannedWrites 合成
        // 视图——§6.6；请求持引用，调用期有效）。runtime 只读快照语义
        // 由端口实现方保证（P-PR-7 单侧冻结——project 仅编排）。
        const CompileRequest request{m_host.query(), plan.objectWrites,
                                     baseView.id};
        const CompileResult compiled = m_compilePort->compileWorkCellAndDwc(request);
        // 临时资源收尾（D-09/实现口径⑤）：无论成败，编译产生的落盘中间
        // 产物只允许在 .staging/tmp——命令结束即清空（§6.6"随事务/命令
        // 结束清理"；失败残留由启动恢复扫描兜底）。清理失败仅开发诊断
        // （不致命——已提交状态不受影响，与 TxEngine 第 7 步同口径）。
        cleanCompileTmp();
        if (!compiled.ok) {
            // 任一半边失败 → 不提交（MDL-06 原子性）。归类＝
            // Failed(compile-failed)（§6.6：非硬断言不适用 Rejected 面）
            // ＋诊断透传。
            result.status.kind = CommandStatus::Kind::Failed;
            result.error = StoreError(
                StoreErrorCode::CompileFailed,
                "project/command: dual compile failed（WorkCell/"
                "DynamicWorkCell 任一半边失败——MDL-06）");
            result.diagnostics = std::move(compiled.diagnostics);
            return;
        }
    }

    // ---- [S6] CommitPlan 装配（修订身份单一分配点——tx::CommitPlan
    //      登记）＋七步事务（经宿主唯一写通道——门卫＋writer 互斥＋
    //      TxEngine::commit）。
    tx::CommitPlan commitPlan;
    commitPlan.revisionId = core::RevisionId::generate();
    commitPlan.branchId = envelope.branch;
    // 提交时间戳：与打开/创建路径同一时钟面（win32::utcNowIsoMilli——
    // ISO-8601 UTC 带毫秒，§4.4 约定；确定性测试注入归 TxEngine 层）。
    commitPlan.committedAtUtc = win32::utcNowIsoMilli();

    // 域对象变更集（§6.8：CommandPlan.objectWrites → 修订引用集的增量）。
    // 空 objectId＝申请新对象（project 分配——S6 装配点生成）；处理器
    // prepare 期已取号的（ctx.objectId()）使用原身份——两条路径同为
    // "project 分配"语义（私有头实现口径）。
    commitPlan.newObjects.reserve(plan.objectWrites.size());
    for (const ObjectWrite& write : plan.objectWrites) {
        tx::PlannedObject obj;
        obj.oid = write.objectId.has_value() ? *write.objectId
                                             : core::ObjectId::generate();
        obj.objectTypeToken = write.objectTypeToken;
        obj.payload = write.payloadCanonical;
        commitPlan.newObjects.push_back(std::move(obj));
    }

    // 元数据增量（实现口径③——两种提交形态，§4.5.1 走查背书）：
    //   A. 建支型（createBranchWithBase）：无 tipUpdate＋addedBranches
    //      条目（base/tip＝源分支 tip——走查步骤 1/3：CreateBranch 修订
    //      parent＝源分支 tip、非任何分支 tip）；parent 亦取源 tip。
    //   B. 常规型：tipUpdate{活动分支→新修订}（§4.5"分支 tip 随该分支
    //      上的每次提交更新"）；parent＝活动分支原 tip（S2 基线）。
    const ProjectMetadataView authoritative = m_host.query().currentMetadata();
    if (plan.metadataChange.has_value()
        && plan.metadataChange->createBranchWithBase.has_value()) {
        const core::BranchId sourceBranch
            = *plan.metadataChange->createBranchWithBase;
        if (plan.metadataChange->label.empty()) {
            // label 一次写入（P-PR-8）——空 label 的建支声明＝畸形计划
            // （处理器契约违约的保守拒绝面，防落盘不可呈现分支）。
            result.status.kind = CommandStatus::Kind::Rejected;
            result.status.rejection
                = CommandStatus::Rejection::InvalidPayload;
            return;
        }
        std::optional<core::RevisionId> sourceTip;
        for (const BranchRecord& b : authoritative.record.branches) {
            if (b.branchId == sourceBranch) {
                sourceTip = b.tipRevisionId;
                break;
            }
        }
        if (!sourceTip.has_value()) {
            // 源分支不存在＝处理器基于过期快照声明（快照在槽内本应一致
            // ——防御面），按数据侧拒绝（INV-M1b 会在发布边界拒绝未注册
            // 修订，此处提前给出更精确归类）。
            throw StoreError(
                StoreErrorCode::StoreCorrupt,
                "project/command: 建支声明引用未知源分支 branch="
                    + sourceBranch.toCanonical());
        }
        // 新增条目（D-5 显式声明制）：branchId 由 project 分配（S6 装配
        // 点）；base/tip 一次写入源分支 tip；label 一次写入（P-PR-8）；
        // createdAt 与修订提交时间同钟面（§4.4.3）。
        revindex::MetadataDelta delta;
        BranchRecord created;
        created.branchId = core::BranchId::generate();
        created.label = plan.metadataChange->label;
        created.baseRevisionId = *sourceTip;
        created.tipRevisionId = *sourceTip;
        created.createdAtUtc = commitPlan.committedAtUtc;
        delta.addedBranches.push_back(std::move(created));
        commitPlan.metadataDelta = std::move(delta);
        commitPlan.parentRevisionId = *sourceTip;
    } else {
        revindex::MetadataDelta delta;
        revindex::TipUpdate update;
        update.branchId = envelope.branch;
        update.newTip = commitPlan.revisionId;
        delta.tipUpdate = std::move(update);
        commitPlan.metadataDelta = std::move(delta);
        commitPlan.parentRevisionId = baseView.id;
    }

    // 命令留痕（§4.4.4/§6.4 版本三元组）：project 原样持久化不解释
    // （D-10）；confirmations[] 留痕随 PRJ-T11（§12 分工——本任务落位
    // 面恒空表，§4.4.4 省略规则下不输出该字段）。
    commitPlan.command.commandType = envelope.commandType;
    commitPlan.command.payloadFormatVersion = envelope.payloadFormatVersion;
    commitPlan.command.payloadCanonical.assign(
        envelope.payloadCanonical.begin(), envelope.payloadCanonical.end());
    if (plan.inverseCommandType.has_value()) {
        // 逆命令表达（§6.9）：类型＋版本＋载荷三元组随修订持久化；undo
        // 提交该载荷产生新修订（PRJ-T13 消费 RevisionView.inverse）。
        commitPlan.command.inverse = InverseCommand{};
        commitPlan.command.inverse->commandType = *plan.inverseCommandType;
        // 逆载荷版本＝处理器当前受理版本（实现口径④——逆载荷由处理器
        // 按其当前格式产出）。
        commitPlan.command.inverse->payloadFormatVersion
            = handler.currentPayloadVersion();
        const auto& inversePayload = *plan.inversePayloadCanonical;
        commitPlan.command.inverse->payloadCanonical.assign(
            inversePayload.begin(), inversePayload.end());
    }
    commitPlan.command.summary = plan.summary;

    // 七步事务（S6 文件事务＋第 6 步内的事件发布——S7；失败重试一次
    // 不回滚＝D-18，TxEngine 承载）。门卫 ContextClosed 在此映射为
    // Aborted(context-closing)（外层 catch——实现口径②）。
    const tx::CommitResult committed = m_host.executeCommit(commitPlan);

    // ---- 终态组装（§5.3.1 CommandResult）：Committed{newRevision}＋
    //      新 HEAD 视图（值快照——调用方免二次查询；修订已注册进会话
    //      索引，tryRevision 必命中——nullopt 属防御面，开发诊断）。
    result.status.kind = CommandStatus::Kind::Committed;
    result.newRevision = committed.revisionId;
    result.newHeadState = m_host.query().tryRevision(committed.revisionId);
    if (!result.newHeadState.has_value() && m_sink != nullptr) {
        m_sink->reportDev("project/command",
                          "提交后视图装配未命中（防御面——数据侧异常）rev="
                              + committed.revisionId.toCanonical());
    }
}

// =====================================================================
// 编译临时目录清理（D-09/§6.6 实现口径⑤）
// =====================================================================

void CommandServiceImpl::cleanCompileTmp()
{
    // 落点＝<项目根>/.staging/tmp（§4.1 tmp 行——非事务操作的临时产物
    // 目录；编译是事务前阶段，不得触碰 .staging/<tx-id> 事务私有区）。
    // 语义＝清空内容、保留目录（§4.1"启动清理（按需重建）"同形态——
    // 目录存在性不是状态）。
    const std::filesystem::path tmpRoot
        = m_host.compileTmpDir();
    std::error_code ec;
    if (!std::filesystem::exists(tmpRoot, ec) || ec) {
        return;  // 目录不存在＝无临时产物（常态）——不重建不报错
    }
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(tmpRoot, ec)) {
        if (ec) {
            break;  // 迭代中断＝权限类残余——开发诊断后放弃（不致命）
        }
        std::error_code removeEc;
        std::filesystem::remove_all(entry.path(), removeEc);
        if (removeEc && m_sink != nullptr) {
            m_sink->reportDev(
                "project/command",
                "编译临时产物清理失败 path=" + entry.path().string()
                    + " osError=" + std::to_string(removeEc.value()));
        }
    }
    if (ec && m_sink != nullptr) {
        m_sink->reportDev("project/command",
                          "编译临时目录迭代失败（清理中断）dir="
                              + tmpRoot.string());
    }
}

}  // namespace sdurws::ird::project
