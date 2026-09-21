/**
 * @file   CommandInteractionBridge.cpp
 * @brief  命令确认交互桥实现（UI-T13——§9.2/§9.3 的落位承载）。
 *
 * 设计依据：
 *   - units/ui.md §9.2（确认流：命令执行线程同步回调→Marshal 至 UI 线程
 *     →阻塞等待；isAlive 探针；未确认不提交；无超时——P-DIAG-6）、
 *     §9.3（对话数据契约映射）、§3.4（M-1 Marshal 纪律）、§2.1.2 C-6
 *     （O-31 方向外翻——本类实现 ui 自有 IUiCommandInteraction）；
 *   - project.md §5.3.3 冻结点（P-PR-7：线程模型按对端冻结点实现——
 *     占命令槽、零事务资源、拆除可打断、无永久等待）；
 *   - 需求 SA-15/MDL-06④/UX-03；P-UI-4（principal 会话启动采集缓存）。
 *
 * 实现口径登记（ui.md §16.7 v1.5 同步）：
 *   - 阻塞等待用 std::condition_variable（§9.2 允许"QEventLoop/QFuture"
 *     等价形态——命令执行线程未必是 Qt 线程，条件变量不要求调用方线程
 *     有事件分发器，是更稳的等价承载；等待谓词＝"决议就绪 ∨ 探针
 *     false"，两者同时就绪时决议优先——"用户意图已表达→照常放行"）；
 *   - Marshal 用 QMetaObject::invokeMethod 函子重载（零 Q_OBJECT，零
 *     AUTOMOC 维持——CMakeLists 既有登记口径）；
 *   - principal＝Windows 用户名（GetUserNameA；非 Windows 退化环境变量，
 *     采集失败降级 unknown-user——P-UI-4 建议口径，构造时采集一次）。
 */

#include <sdurws/ird/ui/CommandInteractionBridge.hpp>

#include <QMetaObject>
#include <QObject>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>   // GetUserNameA（principal 采集——P-UI-4 Windows 用户名口径）
#endif

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 内部承载类型
// =====================================================================

/**
 * @brief UI 线程投递上下文（构造线程建立——Marshal 纪律 M-1 锚点）。
 *
 * 函子重载不需要元对象声明（零 Q_OBJECT）——上下文只决定排队调用的
 * 目标线程（构造线程＝UI 线程，构造契约见头文件）。
 */
class CommandInteractionBridge::MarshalContext final : public QObject
{
public:
    MarshalContext() = default;
    ~MarshalContext() override = default;
};

/**
 * @brief 单次确认等待的共享状态（命令线程与 UI 线程的会合点）。
 *
 * 一次性语义：done 置位后不再改写（首个完成者胜出——拆除唤醒与用户
 * 决议竞争时的次序即语义：决议优先，见 requestConfirmations 收口）。
 * shared_ptr 承载：UI 线程 lambda 与命令线程各自持有副本——拆除唤醒
 * 后命令线程已返回、对话仍可能晚到完成的场景不悬垂（晚到结果被安全
 * 丢弃，对端已按 nullopt 处置）。
 */
struct CommandInteractionBridge::PendingWait {
    std::mutex mutex;                          ///< 结果与谓词互斥
    std::condition_variable condition;         ///< 命令线程等待面
    bool done = false;                         ///< 决议是否就绪（一次性）
    ConfirmDialogResolution resolution;        ///< 用户决议（done 时有效）
};

// =====================================================================
// principal 采集（P-UI-4——会话启动采集缓存，不逐次弹问）
// =====================================================================

namespace {

/**
 * @brief 采集 Windows 用户名（构造时一次——会话启动口径）。
 *
 * @return 用户名（UTF-8 近似——GetUserNameA 返回系统 ANSI；用户名属
 *         受控字符集，不做转换）；采集失败→"unknown-user"（呈现占位，
 *         不虚构身份——凭据的权威性由对端复核链保证，principal 只是
 *         审计显示面）。
 */
std::string collectWindowsPrincipal()
{
#ifdef _WIN32
    // GetUserNameA：缓冲区不足时 pcchSize 收到所需长度——一次重试足够
    // （用户名长度有系统上限；两轮都不成功按失败降级）。
    char buffer[256] = {};
    DWORD size = static_cast<DWORD>(sizeof(buffer));
    if (GetUserNameA(buffer, &size) != 0 && size > 1) {
        return std::string(buffer, size - 1);   // size 含终止符
    }
    DWORD needed = 0;
    if (GetUserNameA(nullptr, &needed) == 0 && needed > 1 && needed <= 4096) {
        std::string wide(static_cast<std::size_t>(needed), '\0');
        DWORD actual = needed;
        if (GetUserNameA(wide.data(), &actual) != 0 && actual > 1) {
            return wide.substr(0, actual - 1);
        }
    }
    return "unknown-user";
#else
    // 非 Windows 平台（本产品交付面为 Windows——分支仅测试覆盖用）：
    // 环境变量退化采集，失败同降级。
    if (const char* user = std::getenv("USERNAME")) {
        if (user[0] != '\0') { return std::string(user); }
    }
    if (const char* user = std::getenv("USER")) {
        if (user[0] != '\0') { return std::string(user); }
    }
    return "unknown-user";
#endif
}

}  // namespace

// =====================================================================
// 构造与探针
// =====================================================================

CommandInteractionBridge::CommandInteractionBridge(Deps deps)
    : m_deps(std::move(deps))
    , m_principal(m_deps.principalProvider ? m_deps.principalProvider()
                                           : collectWindowsPrincipal())
{
    // 装配契约 fail-fast（§2.3 错误语义——无呈现器的桥无法履行确认流，
    // 属调用方装配违约，宁构造失败不半成品运行）。
    if (!m_deps.presenter) {
        throw std::invalid_argument(
            "ui/interaction-bridge/deps: presenter 为空——确认交互桥必注入对话呈现器");
    }
    // 上下文在构造线程建立（构造契约＝UI 线程——Marshal 目标线程锚点）。
    m_context = std::make_unique<MarshalContext>();
}

bool CommandInteractionBridge::isAlive() const noexcept
{
    // 任意线程原子读（§9.2 类块注释"isAlive()……任意线程（原子读）"）。
    return m_alive.load(std::memory_order_acquire);
}

CommandInteractionBridge::~CommandInteractionBridge() = default;
// 析构在实现文件定义：MarshalContext 完整类型仅本 TU 可见——unique_ptr
// 成员的删除器在此实例化（头文件不完整类型不可删除）。拆除在途等待的
// 语义由 markSessionDismantled 显式驱动（析构前 L5 必先拆除——对端强
// 引用装配契约，§5.3.3）。

// =====================================================================
// requestConfirmations（§9.2 时序图的落位——四步线程模型）
// =====================================================================

std::optional<std::vector<core::ConfirmationCredential>>
CommandInteractionBridge::requestConfirmations(
    const std::vector<core::ConfirmableFinding>& findings)
{
    // 第 0 步：空集/已拆除的快速路径。空集＝无事可确认（防御性放行
    // 空凭据向量——与"全部确认"同形，不产生对话）；已拆除＝会话不在，
    // 直接 nullopt（对端 Aborted(interaction-lost)）。
    if (findings.empty()) {
        return assembleCredentials(0);
    }
    if (!isAlive()) {
        return std::nullopt;
    }

    // 第 1 步（命令执行线程）：装配对话值（§9.3 映射——纯函数，含
    // 输入变化标注判别）。
    const ConfirmDialogData data = assembleConfirmDialogData(
        findings, m_principal, m_revisionEpoch > 0, m_deps.enrich);

    // 第 2 步：建立会合点并注册（拆除唤醒的遍历面——m_pendingMutex
    // 只保护注册表本身，等待谓词用各等待自带的互斥）。
    auto wait = std::make_shared<PendingWait>();
    {
        std::lock_guard<std::mutex> registryLock(m_pendingMutex);
        m_pending.push_back(wait);
    }

    // 第 3 步：Marshal 到 UI 线程打开对话（Qt::QueuedConnection——
    // §9.2 类块第 1 步原文）。UI 线程任务分三段：①加锁做拆除竞争检查
    // （done 已置＝等待方已被唤醒，对话不再打开；探针 false＝拆除中，
    // 置 Abandoned 收口）→②**解锁后**打开对话（模态至决议——长操作
    // 绝不持锁，否则命令线程的等待谓词拿不到互斥量＝死锁）→③加锁写
    // 结果（一次性：done 已置则丢弃晚到结果）。会话拆除期间对话框的
    // 及时收口是呈现器契约（拆除路径返回 Abandoned）。
    {
        auto* context = m_context.get();
        const auto presenter = m_deps.presenter;   // shared 持有跨线程副本
        const auto* alive = &m_alive;              // 探针观测（桥生存期覆盖等待——L5 装配契约）
        QMetaObject::invokeMethod(
            context,
            [wait, data, presenter, alive]() {
                // ①入口检查（短临界区）：拆除先到→置 Abandoned 收口（对话
                // 不打开——排队窗口内的拆除竞争检查）。
                {
                    std::lock_guard<std::mutex> lock(wait->mutex);
                    if (wait->done) {
                        return;   // 等待方已被唤醒——晚到 Marshal 丢弃
                    }
                    if (!alive->load(std::memory_order_acquire)) {
                        wait->resolution.outcome = ConfirmDialogOutcome::Abandoned;
                        wait->done = true;
                        wait->condition.notify_all();
                        return;
                    }
                }
                if (!presenter) {
                    // 防御：呈现器失效按放弃收口（装配已校验，不可达分支）。
                    std::lock_guard<std::mutex> lock(wait->mutex);
                    wait->resolution.outcome = ConfirmDialogOutcome::Abandoned;
                    wait->done = true;
                    wait->condition.notify_all();
                    return;
                }
                // ②模态打开（锁外）：此调用阻塞 UI 线程至用户决议或对话
                // 被拆除路径关闭（呈现器契约）。
                const ConfirmDialogResolution resolution =
                    presenter->showConfirmDialog(data);
                // ③结果收口（短临界区；一次性——先写者胜出：对话已打开
                // 后的用户决议即"用户意图已表达"（§9.2 关闭窗口行），即使
                // 拆除同时在途——等待方收口以 done 优先判读，决议不被吞）。
                std::lock_guard<std::mutex> lock(wait->mutex);
                if (!wait->done) {
                    wait->resolution = resolution;
                    wait->done = true;
                    wait->condition.notify_all();
                }
            },
            Qt::QueuedConnection);
    }

    // 第 4 步（命令执行线程）：阻塞等待（占命令槽、零事务资源、无超时
    // ——P-DIAG-6；谓词＝决议就绪 ∨ 探针 false——拆除必然唤醒，无永久
    // 等待）。决议与拆除同时就绪时 done 优先检查＝"用户意图已表达→
    // 照常放行"（§9.2 关闭窗口行）。
    {
        std::unique_lock<std::mutex> lock(wait->mutex);
        wait->condition.wait(lock, [&] {
            return wait->done || !m_alive.load(std::memory_order_acquire);
        });
        const bool resolved = wait->done;

        // 注销会合点（先出注册表再释锁——拆除唤醒遍历不再触达本等待）。
        lock.unlock();
        {
            std::lock_guard<std::mutex> registryLock(m_pendingMutex);
            m_pending.erase(std::remove(m_pending.begin(), m_pending.end(), wait),
                            m_pending.end());
        }
        lock.lock();

        if (!resolved) {
            // 拆除唤醒且无决议→nullopt（对端 Aborted(interaction-lost)
            // ——不产生修订、不产生用户诊断，§9.2 规则表"关闭窗口未完成
            // 确认"行）。晚到的对话结果由 shared 状态安全丢弃。
            return std::nullopt;
        }
        if (wait->resolution.outcome != ConfirmDialogOutcome::Confirmed) {
            // 拒绝/放弃→nullopt（未确认不提交——对端按
            // Rejected(confirmations-rejected) 处置，无修订，SA-15）。
            return std::nullopt;
        }
    }

    // 全部确认→凭据向量（principal＝会话缓存；confirmedAtUtc＝组装
    // 时刻——§9.2 principal 行"confirmedAtUtc 由组装时刻填充"）。
    return assembleCredentials(findings.size());
}

std::vector<core::ConfirmationCredential>
CommandInteractionBridge::assembleCredentials(std::size_t count) const
{
    // confirmedAtUtc 时钟注入（测试可替换；空＝system_clock 直通——
    // 组装时刻的语义：决议已定、凭据成形的一刻）。
    const auto now = m_deps.clock != nullptr ? m_deps.clock->nowUtc()
                                             : std::chrono::system_clock::now();
    std::vector<core::ConfirmationCredential> credentials;
    credentials.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        // 批量确认共用同一组装时刻（一次决议一组凭据——逐条时间戳差异
        // 无审计意义，同刻即"同一批用户决议"）。
        credentials.push_back(core::ConfirmationCredential{m_principal, now});
    }
    return credentials;
}

// =====================================================================
// 会话拆除与失效标注（ui 侧驱动面）
// =====================================================================

void CommandInteractionBridge::markSessionDismantled() noexcept
{
    // 探针置 false（acquire/release 对——isAlive 的读侧见同序）＋唤醒
    // 全部在等命令线程（§9.2 类块第 4 步"isAlive 探针触发循环退出→
    // 返回 nullopt"；调用方＝会话控制器拆除路径）。
    m_alive.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> registryLock(m_pendingMutex);
    for (const auto& wait : m_pending) {
        // 逐等待唤醒：谓词侧 m_alive 已翻转为 false——被唤醒的等待方
        // 走"!resolved"收口。持注册表锁逐个 notify 无死锁风险（等待方
        // 不反过来取注册表锁后再等待——注册与注销都在等待开始/结束后）。
        wait->condition.notify_all();
    }
}

void CommandInteractionBridge::noteRevisionCommitted()
{
    // 代次递增：此后新打开的对话带"输入已变化"标注（§9.2 时序图——
    // 标注提示不代替判定，权威失效由 project 编译前复核）。已打开的
    // 对话经呈现器实时标注（UI 线程排队——Marshal 纪律同主流程）。
    ++m_revisionEpoch;
    auto* context = m_context.get();
    const auto presenter = m_deps.presenter;
    if (presenter) {
        QMetaObject::invokeMethod(
            context,
            [presenter]() { presenter->noteInputChanged(); },
            Qt::QueuedConnection);
    }
}

// =====================================================================
// §9.3 对话装配纯函数（映射唯一权威实现）
// =====================================================================

ConfirmDialogData assembleConfirmDialogData(
    const std::vector<core::ConfirmableFinding>& findings,
    const std::string& principal,
    bool revisionCommittedSince,
    const CommandInteractionBridge::FindingEnricher& enrich)
{
    ConfirmDialogData data;
    data.titleKey = "ui.dlg.confirm.title";
    data.instructionKey = "ui.dlg.confirm.instruction";
    data.noSkipHintKey = "ui.dlg.confirm.no-skip";
    data.policyBoundHintKey = "ui.dlg.confirm.policy-bound";
    data.confirmAllKey = "ui.dlg.confirm.option.confirm-all";
    data.staleInputKey = "ui.dlg.confirm.stale-input";
    data.principal = principal;

    data.items.reserve(findings.size());
    for (const auto& finding : findings) {
        ConfirmDialogItem item;
        item.finding = finding;

        // 第 1 层（core 级必有数据）：标题/详情键按码表键约定推导
        // （diag.<code-lower>.<段>——P-DIAG-9；小写化＝键词表约定，
        // 码本身不变）。确认键缺省＝冻结通用确认键。
        std::string codeLower;
        codeLower.reserve(finding.record.code.size());
        for (const char c : finding.record.code) {
            codeLower += static_cast<char>((c >= 'A' && c <= 'Z')
                                               ? static_cast<char>(c - 'A' + 'a')
                                               : c);
        }
        item.titleKey = "diag." + codeLower + ".title";
        item.detailKey = "diag." + codeLower + ".detail";
        item.confirmTextKey = "ui.dlg.confirm.option.confirm";
        item.optionKeys = {"ui.dlg.confirm.option.confirm",
                           "ui.dlg.confirm.option.reject"};

        // 第 2 层（FindingRecord 补全——"经 project 间接读"同源面；
        // 未命中/未注入＝缺行不虚构，零虚构纪律见头文件注释）。
        if (enrich) {
            if (auto record = enrich(finding)) {
                item.subjectScope = record->subjectScope;
                item.baseRevisionCanonical = record->baseRevisionId.toCanonical();
                if (!record->confirmTextKey.empty()) {
                    item.confirmTextKey = record->confirmTextKey;
                }
                if (!record->optionKeys.empty()) {
                    item.optionKeys = record->optionKeys;
                }
                // Dev 折叠区：只呈现来源命令类型 token——载荷摘要是哈希
                // 形态，永不进入用户可见面（UX-02 呈现边界）。
                if (!record->sourceCommandType.empty()) {
                    data.devFoldLines.push_back("command-type="
                                                + record->sourceCommandType);
                }
            }
        }

        // 缺省作用对象＝record.subject 单对象（无补全时的最小对象行）。
        if (item.subjectScope.empty() && finding.record.subject.has_value()) {
            item.subjectScope.push_back(*finding.record.subject);
        }

        // 输入变化标注（对话打开前发生过修订提交——批级横幅＋条目位）。
        item.staleInputHint = revisionCommittedSince;
        data.items.push_back(std::move(item));
    }
    return data;
}

}  // namespace ui
}  // namespace ird
