/**
 * @file   RuntimePublishBridge.cpp
 * @brief  宿主运行时发布桥的实现——四步呈现刷新事务（事件过滤→完整构造
 *         →身份对账→原子替换＋提交）与失败路径（稳定诊断＋保留旧呈现＋
 *         零修订回滚）的唯一编排点（契约面见 RuntimePublishBridge.hpp）。
 *
 * 设计依据：units/ui.md §13 UI-T20 行、B1-SPEC §4.3/§3.3（D10/INV-B4）、
 *   ARCHITECTURE §7.12/SA-18、knownPitfalls P-RT-4/O-43——逐条对应见头
 *   文件与各实现段注释；诊断出线纪律＝DraftController emitAutosaveFailed
 *   Diagnostic 同款（目录＋Dev 双通道、空目录显式跳过不虚构）。
 */

#include <sdurws/ird/ui/RuntimePublishBridge.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws {
namespace ird {
namespace ui {

namespace {

/// Dev 日志通道 token（≤48 字符——diagnostics 日志通道词法；与壳层/
/// 草稿控制器同款命名形制，前缀 ird.ui 声明单元内归属）。
constexpr const char* kPublishBridgeDevChannel = "ird.ui.publish-bridge";

/// 稳定诊断码（UI 族——码值登记 units/ui.md §3.5；描述符经
/// uiDiagnosticCodeDescriptors 注册进 StableCodeRegistry，未注册码被
/// 诊断工厂拒绝——产码纪律与 DraftController/UI-T11 同源）。
constexpr const char* kPresentationRefreshFailedCode = "UI-PRESENTATION-REFRESH-FAILED";

/// 失败词表 token（头文件 PresentationRefreshOutcome 注释的封闭词表）。
constexpr const char* kFailNoSession = "no-session";
constexpr const char* kFailStaleEvent = "stale-event";
constexpr const char* kFailConstructFailed = "construct-failed";
constexpr const char* kFailIncompleteView = "incomplete-view";
constexpr const char* kFailIdentityMismatch = "identity-mismatch";
constexpr const char* kFailApplyFailed = "apply-failed";

}  // namespace

// =====================================================================
// INV-B4 选中处置规则（纯函数——B1-SPEC §3.3 原文承载）
// =====================================================================

SelectionDisposition selectionDispositionAfterRefresh(
    bool selectionPresent, bool objectExistsInNewPresentation)
{
    // 规则只有两条（词表双值封闭——"改选其他对象"不可表达，即"不静默
    // 换选"的结构承载）：
    //   - 选中对象在新呈现中存在（ObjectId 存在性通过）→ 保持；
    //   - 其余（对象已消失，或本无选中——置空空集为无操作）→ 置空。
    // "失效呈现按 UX 当前性口径"的呈现半区：置空后的用户提示归
    // SelectionService/当前性徽标（UI-T21+），本规则只产出处置结论。
    if (selectionPresent && objectExistsInNewPresentation) {
        return SelectionDisposition::Keep;
    }
    return SelectionDisposition::Clear;
}

// =====================================================================
// 构造与装配校验
// =====================================================================

RuntimePublishBridge::RuntimePublishBridge(Deps deps)
    : m_deps(std::move(deps))
{
    // 必填端口缺失＝装配缺陷 fail-fast（AGENTS §3 调用方错误轨——无刷新
    // 通道的桥会制造"事件已被消费"的假象，禁吞错）。
    if (!m_deps.source || !m_deps.outlet) {
        throw std::invalid_argument(
            "RuntimePublishBridge: source 与 outlet 端口必填非空（O-31 注入面"
            "——缺失属装配缺陷，禁止构造不可刷新的桥）");
    }
}

// =====================================================================
// 宿主会话生命周期（acceptance 1——呈现视图生命周期的桥半区）
// =====================================================================

void RuntimePublishBridge::attachHostSession(const core::ProjectId& project)
{
    // 事件事实级校验：全零保留值的项目身份无法参与归属比对（迟到事件
    // 过滤键失效）＝调用方契约违约，fail-fast。
    if (!project.isValid()) {
        throw std::invalid_argument(
            "RuntimePublishBridge::attachHostSession: project 为全零保留值"
            "（归属比对键必须可核对面）");
    }
    // 重复挂接拒绝（状态机界面侧不接纳叠绑——切换绑定走 switchHostSession，
    // 语义不同：attach 会清空呈现语义而 switch 保留宿主画面到替换成功）。
    if (m_bound.has_value()) {
        throw std::logic_error(
            "RuntimePublishBridge::attachHostSession: 已绑定会话，重复挂接被拒"
            "（项目切换请走 switchHostSession）");
    }
    m_bound = project;
    emitDevLine("host session attached: project=" + project.toCanonical());
}

void RuntimePublishBridge::switchHostSession(const core::ProjectId& project)
{
    if (!project.isValid()) {
        throw std::invalid_argument(
            "RuntimePublishBridge::switchHostSession: project 为全零保留值");
    }
    // 切换只在会话内有意义（未绑定＝时序违约——先 attach 再 switch）。
    if (!m_bound.has_value()) {
        throw std::logic_error(
            "RuntimePublishBridge::switchHostSession: 未绑定会话，切换被拒"
            "（先 attachHostSession）");
    }
    // 换绑不动宿主呈现：旧项目的宿主画面保留到新呈现替换成功——切换
    // 失败时"保留旧宿主 WorkCell 与旧三维场景"（acceptance 3）的桥半区；
    // 此后旧项目事件按归属比对在事务①被拒（stale-event）。
    m_bound = project;
    emitDevLine("host session switched: project=" + project.toCanonical());
}

void RuntimePublishBridge::detachHostSession()
{
    if (!m_bound.has_value()) {
        throw std::logic_error(
            "RuntimePublishBridge::detachHostSession: 未绑定会话，拆绑被拒"
            "（对称纪律——与 attach 配对使用）");
    }
    // 先释放宿主呈现（尽力而为——outlet 契约；实现缺陷抛出即穿透，
    // 不吞错），再清空桥侧状态。顺序不可换：清空后 release 的失败将
    // 失去"是哪次会话的释放"留痕上下文。
    if (m_deps.outlet) {
        m_deps.outlet->releasePresentation();
    }
    emitDevLine("host session detached: project=" + m_bound->toCanonical());
    m_current.reset();
    m_bound.reset();
}

// =====================================================================
// ⑤事件消费——四步呈现刷新事务（acceptance 1/3）
// =====================================================================

PresentationRefreshOutcome
RuntimePublishBridge::handlePresentationEvent(const PresentationEventFacts& facts)
{
    // ---- 事务①：事件过滤（不进事务——无"刷新失败"语义，Dev 拒绝）----
    // 未绑定会话：事件先于会话建立到达（打开协议未完成/已拆绑后的迟到
    // 转发）——呈现面无宿主会话可承载，Dev 留痕即返。
    if (!m_bound.has_value()) {
        emitDevLine("event rejected (no-session): kind="
                    + std::to_string(static_cast<int>(facts.kind)));
        PresentationRefreshOutcome out;
        out.failureToken = kFailNoSession;
        return out;
    }
    // 归属比对：事件项目与绑定不符＝旧项目迟到事件（§6.2 迟到事件过滤
    // 同型）——B1-SPEC §4.3"修订切换后旧呈现身份失效，不得继续冒充当前
    // 呈现"的事件半区：旧事件永远进不了事务，当前呈现不被旧项目刷新。
    if (!(facts.project == *m_bound)) {
        emitDevLine("event rejected (stale-event): bound="
                    + m_bound->toCanonical() + " event=" + facts.project.toCanonical());
        PresentationRefreshOutcome out;
        out.failureToken = kFailStaleEvent;
        return out;
    }

    // 对账基准有效性（转发方契约）：全零修订/模型身份使对账退化为
    // 伪验证——fail-fast（AGENTS §3 调用方错误轨）。
    if (!facts.appliedRevision.isValid() || !facts.modelIdentity.isValid()) {
        throw std::invalid_argument(
            "RuntimePublishBridge::handlePresentationEvent: 事件事实携带全零"
            "身份（appliedRevision/modelIdentity 必须有效——对账基准残缺）");
    }

    // ---- 事务②：完整构造（acceptance 3 第一步——先完整构造）----
    // source 返回 nullopt＝本修订不可呈现（工厂拒绝/快照缺失——无半成品）；
    // 失败路径：旧宿主画面保留（outlet 未被调用）＋稳定诊断＋零修订回滚。
    std::optional<PresentationViewProjection> fetched =
        m_deps.source->fetchPresentation(facts);
    if (!fetched.has_value()) {
        return failRefresh(facts, kFailConstructFailed,
                           "呈现构造不可达（source 返回空——本修订无完整呈现）");
    }

    // ---- 事务③：身份对账（B1-SPEC §4.3 v1.1——应用之前的最后防线）----
    // ③a 契约面完整位：身份无效/载体与存在性查询缺失＝适配缺陷产物，
    // 不得进宿主。
    if (!fetched->isComplete()) {
        return failRefresh(facts, kFailIncompleteView,
                           "呈现投影契约面不完整（isComplete()==false——身份/载体/存在性查询缺一）");
    }
    // ③b 绑定②：appliedRevisionId 对照本次发布修订（事件事实为基准）。
    if (!(fetched->appliedRevisionId == facts.appliedRevision)) {
        return failRefresh(facts, kFailIdentityMismatch,
                           "身份对账失败（appliedRevisionId 与本次发布修订不符——绑定②）");
    }
    // ③c 绑定①：modelIdentity 对照来源规范模型内容（事件事实为基准）。
    if (!(fetched->modelIdentity == facts.modelIdentity)) {
        return failRefresh(facts, kFailIdentityMismatch,
                           "身份对账失败（modelIdentity 与来源规范模型内容不符——绑定①）");
    }
    // ③d 绑定③＋"重建即新身份"：存在旧呈现时，本次构造实例身份必须与
    // 其不同——适配器复用旧视图对象（未走工厂新构造）即被此处拦截
    // （"旧 presentationIdentity 失效，不得继续冒充当前呈现"）。首次
    // 呈现（无旧实例）只需 isValid（已在 ③a 完整位覆盖）。
    if (m_current.has_value()
        && fetched->presentationIdentity == m_current->presentationIdentity) {
        return failRefresh(facts, kFailIdentityMismatch,
                           "身份对账失败（presentationIdentity 与旧呈现相同——重建未换新实例，"
                           "旧呈现身份不得冒充当前呈现——绑定③）");
    }

    // ---- 事务④：原子替换＋提交（acceptance 3 第二步——后原子替换）----
    // outlet 单入口应用（TreeView＋三维场景同源——INV-B4 一致性的宿主
    // 半区）；失败＝宿主保持原状，桥保留旧当前呈现。**只有 ok 才更新
    // m_current**——"先完整构造、后原子替换"的提交点。
    PresentationApplyReport report = m_deps.outlet->applyPresentation(*fetched);
    if (!report.ok) {
        return failRefresh(facts, kFailApplyFailed,
                           "宿主应用失败（outlet ok=false）：token=" + report.failureToken
                               + (report.failureDetail.empty()
                                      ? std::string{}
                                      : "；detail=" + report.failureDetail));
    }

    // ---- 提交：更新当前呈现＋通知观察者（事务终态——此后无失败路径）----
    m_current = *fetched;
    emitDevLine("presentation replaced: revision="
                + fetched->appliedRevisionId.toCanonical() + " presentation="
                + fetched->presentationIdentity.toCanonical());
    notifyReplaced(*fetched);

    PresentationRefreshOutcome out;
    out.applied = true;
    return out;
}

// =====================================================================
// 当前呈现查询（失效语义——旧呈现身份不可冒充）
// =====================================================================

std::optional<PresentationViewProjection> RuntimePublishBridge::currentPresentation() const
{
    return m_current;
}

std::optional<core::ProjectId> RuntimePublishBridge::boundSession() const
{
    return m_bound;
}

void RuntimePublishBridge::addObserver(
    std::weak_ptr<IUiPresentationRefreshObserver> observer)
{
    // 空观察者＝调用方违约 fail-fast（弱持有由本类管理，空条目无语义）。
    if (observer.lock() == nullptr) {
        throw std::invalid_argument(
            "RuntimePublishBridge::addObserver: observer 为空或已过期");
    }
    m_observers.push_back(std::move(observer));
}

// =====================================================================
// 失败路径与出线（acceptance 3——稳定诊断＋观察者＋Dev 三通道）
// =====================================================================

PresentationRefreshOutcome
RuntimePublishBridge::failRefresh(const PresentationEventFacts& facts,
                                  const std::string& reasonToken,
                                  const std::string& causeDetail)
{
    // 双通道诊断（目录 Error＋Dev 明文；空目录/空日志显式跳过不虚构）。
    emitRefreshFailedDiagnostic(facts, reasonToken, causeDetail);
    // 观察者失败通知（UI-T21+ 联动接缝——旧当前呈现保持不变）。
    notifyFailed(facts, reasonToken);
    // 事务失败出口：**零修订回滚动作**——本方法体（及全类）不存在任何
    // 命令网关/项目存储表面，已合法产生的项目修订不受呈现刷新失败影响
    // （acceptance 3 的结构承载；验收对抗项）。
    emitDevLine("presentation refresh failed: token=" + reasonToken);
    PresentationRefreshOutcome out;
    out.failureToken = reasonToken;
    return out;
}

void RuntimePublishBridge::emitRefreshFailedDiagnostic(
    const PresentationEventFacts& facts, const std::string& reasonToken,
    const std::string& causeDetail) const
{
    emitDevLine(std::string(kPresentationRefreshFailedCode) + ": token=" + reasonToken
                + " revision=" + facts.appliedRevision.toCanonical()
                + (causeDetail.empty() ? std::string{} : " detail=" + causeDetail));
    if (!m_deps.diagFactory || !m_deps.diagSink) {
        return;
    }
    // 产码经工厂唯一入口（码已随 uiDiagnosticCodeDescriptors 注册——未
    // 注册码被工厂拒绝，产码纪律与 UI-T11 实测同源）。
    const core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kPresentationRefreshFailedCode,
        core::ObjectId::generate(),  // 主体＝事件级条目（非域对象——瞬态 id 定位本条诊断）
        std::nullopt,                // localName（呈现级事件——域对象定位不适用）
        std::nullopt,                // runtimeName（同上）
        "项目已应用但呈现刷新失败",   // 用户文案（acceptance 3 指定语义逐字承载）
        causeDetail,
        "项目数据已安全（修订未回滚）；可重新触发该操作刷新呈现，或检查开发日志定位失败原因",
        std::nullopt);  // 比较型三要素（非数值判定场景——缺省不携带）
    diagnostics::DiagContext context;
    context.sourceUnit = "ui";  // §4.2 sourceUnit 词表含 ui
    context.sourceInterface = "publish-bridge.refresh";
    context.project = facts.project.isValid()
        ? std::optional<core::ProjectId>(facts.project)
        : std::nullopt;
    m_deps.diagSink->append(m_deps.diagFactory->create(record, context));
}

void RuntimePublishBridge::emitDevLine(const std::string& message) const
{
    if (m_deps.devLog) {
        m_deps.devLog->logDev(kPublishBridgeDevChannel, message);
    }
}

// =====================================================================
// 观察者通知（弱持有——过期剪除；回调在事务内同步执行）
// =====================================================================

void RuntimePublishBridge::notifyReplaced(const PresentationViewProjection& view)
{
    // 剪除过期项后逐个通知（观察者抛出＝装配缺陷，穿透不吞——禁止
    // 半通知状态的静默降级）。
    std::vector<std::shared_ptr<IUiPresentationRefreshObserver>> alive;
    alive.reserve(m_observers.size());
    for (std::weak_ptr<IUiPresentationRefreshObserver>& weak : m_observers) {
        if (std::shared_ptr<IUiPresentationRefreshObserver> pinned = weak.lock()) {
            alive.push_back(std::move(pinned));
        }
    }
    m_observers.erase(
        std::remove_if(m_observers.begin(), m_observers.end(),
                       [](const std::weak_ptr<IUiPresentationRefreshObserver>& weak) {
                           return weak.expired();
                       }),
        m_observers.end());
    for (const auto& observer : alive) {
        observer->onPresentationReplaced(view);
    }
}

void RuntimePublishBridge::notifyFailed(const PresentationEventFacts& facts,
                                        const std::string& reasonToken)
{
    std::vector<std::shared_ptr<IUiPresentationRefreshObserver>> alive;
    alive.reserve(m_observers.size());
    for (std::weak_ptr<IUiPresentationRefreshObserver>& weak : m_observers) {
        if (std::shared_ptr<IUiPresentationRefreshObserver> pinned = weak.lock()) {
            alive.push_back(std::move(pinned));
        }
    }
    m_observers.erase(
        std::remove_if(m_observers.begin(), m_observers.end(),
                       [](const std::weak_ptr<IUiPresentationRefreshObserver>& weak) {
                           return weak.expired();
                       }),
        m_observers.end());
    for (const auto& observer : alive) {
        observer->onPresentationRefreshFailed(facts, reasonToken);
    }
}

}  // namespace ui
}  // namespace ird
}  // namespace sdurws
