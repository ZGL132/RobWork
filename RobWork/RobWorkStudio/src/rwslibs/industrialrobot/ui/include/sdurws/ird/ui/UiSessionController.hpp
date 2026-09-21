/**
 * @file   UiSessionController.hpp
 * @brief  UI 会话控制器——§5 会话状态机的唯一执行者与关闭/切换统一确认
 *         对话框机制（对 L5/workflow 的编程入口）。
 *
 * 设计依据：
 *   - units/ui.md §5 全节：§5.1（五种生命周期概念与 SA-17/A7 分离原则：
 *     UI 会话结束≠存储上下文结束）、§5.2（七态状态机＋INV-SES-1~4 不变
 *     量）、§5.3（打开期错误与显示差异——PM-07 横幅含 PID/actionKind
 *     区分）、§5.4（关闭/切换时序 S1/S2——统一确认对话框：草稿三选＋任务
 *     二选，PM-03）、§5.5（只读模式写入口禁用——判定源唯一＝writable）、
 *     §5.6（关闭流程防永久阻塞——四级防线：有界等待/协作取消 2 s·10 s/
 *     T_force 强制终止确认/T_force2 兜底轮询）、§5.7（在途运行、归档与
 *     资源释放——requestClose 不催促不跳过归档）；
 *   - §10.1（ShellWiring 注释行："project/execution 实例随打开流程经 ui
 *     自有会话/命令/草稿/查询端口注入 UiSessionController"——O-31 裁决
 *     注入形态；本控制器即该注入的承接点）、§3.3（UiSessionController.hpp
 *     ＝"§5 会话状态机（对 L5/workflow 的编程入口）"）、§3.4（线程模型：
 *     全部方法 UI 线程——M-1 Marshal 纪律的消费点）、§6.2（会话纪元
 *     epoch——打开成功/关闭完成/切换绑定新项目时递增）、§9.5（关闭对话
 *     框任务区＝tasksByProject 过滤非终态；应用退出时 scheduler.shutdown
 *     (DrainPolicy) 归 L5/workflow 调用）、§11.5（分工表：ui＝T_force 计
 *     时与确认对话，L5＝abandonAll 调用）；
 *   - 需求 PM-03（关闭/切换统一确认：草稿三选＋任务二选）、PM-07（只读
 *     打开提示与显示差异）、TASK-03/AT-10（切换迟到事件 epoch 过滤——
 *     本控制器维护 epoch）、INV-SES-4（关闭不永久阻塞）；
 *   - 任务契约 tasks/foundation/UI-T11.json acceptance 1~3（机制用例/
 *     状态机不变量/O-31＋P-UI-8 处置）。
 *
 * 背景说明（控制器在装配图中的位置）：
 *   L5 应用壳/workflow 持有本控制器（经 IWorkbenchShell::session() 或直接
 *   构造），把"打开/关闭/切换"的**触发时机编排**留在自己一侧（§11.5 分工
 *   表 PM-03 行），把"状态机推进＋对话框数据装配＋处置执行＋Draining 防
 *   线"交给本控制器。对端（project/execution）实例从不以对端类型进入本
 *   单元——它们的协作面在打开成功时由 L5 适配器包装成 ui 自有端口
 *   （SessionPortBundle）注入（O-31 裁决，产品面零对端链接/include，
 *   NoCrossUnitInclude_O31_UI_BUILD 守卫常驻自证）。
 *
 * 对话框机制说明（为什么"机制"是数据＋决议而不是 Widget）：
 *   统一确认对话框的**呈现**是 GUI 层（关闭对话框widget/命令面板等，阶段
 *   B 交付面），**机制**＝数据装配（草稿区/任务区行集＋按钮可用位）、决议
 *   解析（取消/保存/放弃×等待/协作取消的分支执行）、状态迁移（§5.2 时序）
 *   与 Draining 防线（§5.6）——本控制器承载全部机制且零 Qt（模型层可测，
 *   §12.1 第一层分工）；GUI 测试（§12.1 第三层）只验证呈现绑定。
 *
 * P-UI-8 处置（契约 acceptance 3）：T_force/T_force2 按 §5.6 默认保守值
 *   （120 s/300 s）实现，经 UiSessionControllerDeps 阈值字段**装配期可
 *   配**；P-UI-8 阈值裁决未闭合，本实现不私定终值——默认值只是 §5.6 原文
 *   的保守值承载，裁决产出后随装配参数更新，不改语义。
 *
 * 线程约束：非线程安全——全部方法只允许 UI 线程调用（§3.4 M-1；状态机
 *   推进、对话框装配、Draining 轮询均在 UI 线程；存储上下文关闭回调由
 *   L5 端口适配器负责 Marshal 到 UI 线程后投递——IUiStoreCloseObserver
 *   契约注释，防线 4 的 closed() 轮询是回调丢失时的 UI 线程兜底）。
 */

#ifndef SDURWS_IRD_UI_UISESSIONCONTROLLER_HPP
#define SDURWS_IRD_UI_UISESSIONCONTROLLER_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>           // core::IEventSubscription（subscribeClose RAII 句柄——表内登记边）
#include <sdurws/ird/core/Identity.hpp>         // core::ProjectId（表内登记边——后台持有点按项目身份释放）
#include <sdurws/ird/diagnostics/Catalog.hpp>   // diagnostics::IDiagnosticSink（用户级诊断出线——表内登记边）
#include <sdurws/ird/diagnostics/DiagCodes.hpp> // diagnostics::CodeDescriptor（描述符词表同族）
#include <sdurws/ird/diagnostics/Factory.hpp>   // diagnostics::IDiagnosticFactory（create 唯一入口——§9.2）
#include <sdurws/ird/ui/UiPorts.hpp>            // C-3/C-5/C-8 会话端口（O-31 注入面——IUiStoreCloseObserver 基类）
#include <sdurws/ird/ui/UiProjections.hpp>      // 会话投影族（OpenStoreOutcome/CloseDialogData 等——UI-T11 增量）
#include <sdurws/ird/ui/UiTypes.hpp>            // UiSessionState/UiOpenMode/UiCloseIntent（§5.2 词表）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 关闭对话框数据装配输入（纯函数 assembleCloseDialogData 的入参聚合）
// =====================================================================

/**
 * @brief 关闭对话框数据装配的输入快照（§5.4 S1"数据装配"框的两个来源行
 *        ＋会话事实位的聚合——一次一致快照）。
 *
 * 为什么是单一聚合值而不是逐端口现取：对话框三按钮的可用位必须取**同一
 * 时刻**的会话事实（writable 决定"保存"可用、行集决定按钮影响面）——
 * 分次取数会出现"行集是新查询、可用位是旧会话"的拼接态（§6.1 快照一致
 * 性纪律同案）。调用方（控制器/测试）现取现传，装配函数纯消费。
 */
struct CloseDialogInputs {
    /// 项目显示名（对话框标题区——UX-02 工程用语）。
    std::string projectDisplayName;
    /// 会话可写位（INV-SES-1 数据源直通——决定 saveDraftsAvailable）。
    bool writable = false;
    /// 会话脏标记（§8.5 会话级"未应用修改"——present ∨ sessionDirty 的
    /// 会话半区，与磁盘草稿行集并列呈现）。
    bool sessionDirty = false;
    /// 磁盘草稿行集（IUiDraftQueryPort::listDrafts 原样——装配不加工）。
    std::vector<DraftRowProjection> diskDraftRows;
    /// 非终态任务行集（IUiSessionTaskPort::nonTerminalTasks 原样）。
    std::vector<TaskRowProjection> nonTerminalTaskRows;
};

/**
 * @brief 装配统一确认对话框数据（§5.4 S1/S2"数据装配"的唯一实现点——
 *        纯函数，UI-SES 系列用例的观测面）。
 *
 * 装配规则（§5.4/§9.5/§5.5 逐条）：
 *   - 草稿区＝磁盘草稿行集原样（不排序不加工——NFR-COR-02 稳定呈现）；
 *   - 任务区＝非终态任务行集原样（§9.5"过滤非终态"由端口侧完成）；
 *   - saveDraftsAvailable＝writable（§5.4"[保存草稿]（可写会话）"原文；
 *     只读会话保存按钮不可选——§5.5 draft.save 为写操作禁用）；
 *   - noActiveTasks＝任务行集为空（"等待"此时直接进入 Draining）。
 *
 * @param inputs [in] 装配输入快照（见类型注释——一次一致快照）
 * @return 对话框数据（呈现层只渲染本数据——ARC-02 控件互读红线同口径）
 */
CloseDialogData assembleCloseDialogData(const CloseDialogInputs& inputs);

// =====================================================================
// UiSessionControllerDeps——装配期注入面（O-31 会话端口＋防线配置）
// =====================================================================

/**
 * @brief 会话控制器的运行时协作面与配置（L5 应用壳装配期一次性给出）。
 *
 * 注入纪律（与 ShellWiring 同款）：
 *   - storeFactory 必填非空（打开五步协议入口——没有它控制器无法进入
 *     Opening，构造期 fail-fast 校验，AGENTS §3 错误语义"调用方错误
 *     fail-fast"）；
 *   - diagSink/diagFactory/devLog 允许为空＝无目录/无日志测试场景（须
 *     显式声明——§10.1 前置条件行同款纪律）；UI-SESSION-CONTEXT-INVALID
 *     等用户级码在空场景下以返回值/横幅承载，不虚构目录条目；
 *   - 三个 std::function 接线点的接线目标：presentContext →
 *     IWorkbenchShell::presentProjectContext（§10.1 v0.5 facets——上下文
 *     原子快照注入口）；saveAllDraftsManual → IDraftController::saveAll(
 *     Manual)（§8/UI-T12 落位后接线；仅在草稿处置＝保存时调用）； 
 *     forceAbandonAll → L5 关闭控制器 abandonAll(ForceTerminated)
 *     （§11.5 分工表原文——调用权在 L5，ui 只在 T_force 确认后触发）。
 *
 * P-UI-8（契约 acceptance 3）：forceWaitThreshold/forceWaitGiveUpThreshold
 *   为**装配期可配**阈值，默认取 §5.6 原文保守值 120 s/300 s——P-UI-8
 *   阈值裁决未闭合，装配方不得把默认值当作裁决终值登记。
 */
struct UiSessionControllerDeps {
    /// 打开五步协议端口（C-3 工厂面——必填非空，构造期校验）。
    std::shared_ptr<IUiStoreFactoryPort> storeFactory;
    /// 诊断统一 sink（用户级码入目录通道；允许为空＝无目录测试场景）。
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    /// 诊断工厂（create 唯一入口——§9.2；允许为空＝无目录测试场景）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
    /// 开发日志通道（Dev 级事实唯一出线——§6.2；允许为空＝无日志场景）。
    std::shared_ptr<diagnostics::IDevLogSink> devLog;
    /// 工作台上下文注入口（→ IWorkbenchShell::presentProjectContext；
    /// 允许为空＝无壳测试场景——控制器状态仍可经 context() 观察）。
    std::function<void(const ProjectContextProjection&)> presentContext;
    /// 草稿全量保存接线点（→ IDraftController::saveAll(Manual)，§8.6；
    /// 允许为空＝无草稿处置测试场景——但草稿处置选"保存"时必须已接线，
    /// 否则 resolveCloseDialog fail-fast，见其注释）。
    std::function<bool()> saveAllDraftsManual;
    /// 强制放弃兜底接线点（→ L5 关闭控制器 abandonAll(ForceTerminated)，
    /// §11.5；允许为空＝无强杀测试场景——但 T_force 确认时必须已接线，
    /// 否则 resolveForceCloseDialog fail-fast）。
    std::function<void()> forceAbandonAll;
    /// T_force：强制终止确认阈值（§5.6 防线 3——默认 120 s 保守值；
    /// P-UI-8 裁决未闭合，装配期可配不私定终值）。
    std::chrono::milliseconds forceWaitThreshold{120 * 1000};
    /// T_force2：兜底放弃等待阈值（§5.6 防线 4——默认 300 s 保守值；
    /// 自 Draining 起点的总预算，"绝不无限等待"的最终界）。
    std::chrono::milliseconds forceWaitGiveUpThreshold{300 * 1000};
    /// 单调时钟供应（Draining 计时——缺省 steady_clock::now；测试注入
    /// 假时钟以确定性地驱动 T_force/T_force2，不用 sleep 判据——§12.3
    /// 通用判据"不使用固定 sleep 判据"）。
    std::function<std::chrono::steady_clock::time_point()> steadyClock;
};

// =====================================================================
// 打开/轮询的结果报告值（方法返回值轨——控制器不抛环境错误）
// =====================================================================

/**
 * @brief 打开请求的完整报告（openProject/beginSwitch 候选验证共用）。
 *
 * ok==true 时 opened/readonlyBanner 有效（降级只读时 banner 非空——
 * §5.3 横幅随打开立即呈现）；ok==false 时 failure 有效（错误页数据——
 * "错误页定位具体文件；当前项目不动"，§5.3 行 6/PM-03）。
 */
struct SessionOpenReport {
    /// 是否成功（含降级只读——PM-07 降级不是失败，见 OpenStoreOutcome）。
    bool ok = false;
    /// 成功事实（INV-SES-1 数据源——ok 时有效）。
    OpenedProjectFacts opened;
    /// 只读横幅（ok 且 writable=false 时非空——assembleReadOnlyBanner
    /// 产出，呈现层原样渲染）。
    std::optional<ReadOnlyBannerProjection> readOnlyBanner;
    /// 失败事实（错误页数据——!ok 时有效）。
    OpenStoreFailureFacts failure;
};

/**
 * @brief Draining 轮询报告（pollDrain 的返回值——§5.6 四级防线的观测面）。
 *
 * Draining：仍在等待（含防线 1 的有界面反馈义务——在途引用数/任务进度
 * 由呈现层经持有点端口读取）；ForceConfirmDue：T_force 到点——强制结束
 * 确认对话框数据就绪（forceCloseDialogData() 取数呈现，§5.6 防线 3）；
 * GivenUp：T_force2 到点——放弃等待完成关闭（UI-SESSION-CONTEXT-INVALID
 * 已出线，数据损失限于未归档结果——检查点保留）；ClosedNow：本次轮询
 * 观察到 closed()==true，关闭流程已完成。
 */
struct DrainPollReport {
    /// 轮询结论（四值词表见类型注释）。
    enum class Status : std::uint8_t {
        Draining,       ///< 等待中（未达任何阈值/未关闭）
        ForceConfirmDue,///< T_force 到点——强制结束确认待呈现（防线 3）
        GivenUp,        ///< T_force2 到点——放弃等待并完成关闭（防线 4）
        ClosedNow,      ///< 本次轮询完成关闭（回调或轮询观察到 closed）
    };
    /// 轮询结论（默认 Draining＝继续等待的安全值）。
    Status status = Status::Draining;
    /// 本次轮询释放的后台持有点数（切换场景旧项目排空完成数——INV-SES-3
    /// 持有点只在 closed 后释放，§5.1 分离原则）。
    std::size_t holdsReleased = 0;
    /// 进入 Draining 时 requestClose() 返回的在途引用数（§5.6 防线 1
    /// "有界面反馈"的初始值——归档会话/在途事务/草稿落盘的计数快照；
    /// 当前进度以任务行与 closed()==true 为准，本值不随轮询刷新——端口
    /// 只在 requestClose 时返回该计数，§5.2 Draining 框原文）。
    std::uint32_t inFlightReferences = 0;
};

// =====================================================================
// UiSessionController——会话状态机与关闭/切换对话框机制
// =====================================================================

/**
 * @brief UI 会话状态机执行者（§5.2 七态）＋统一确认对话框机制（§5.4）＋
 *        Draining 四级防线（§5.6）。
 *
 * 生命周期/所有权：由 L5/workflow 独占持有（unique_ptr 或壳成员）；注入
 * 端口所有权在 L5/装配层，控制器只持共享引用；旧项目存储上下文的保活
 * 语义＝shared_ptr 持有点（INV-SES-3——保持到 subscribeClose 回调后释放，
 * §5.1 分离原则原文）。
 *
 * 状态迁移表（§5.2 状态机图 + 状态表，实现逐行对齐）：
 *   - NoProject --openProject 成功--> OpenWritable/OpenReadOnly（epoch++）；
 *   - Open* --beginClose/beginSwitch--> Open*（对话框呈现不改状态；
 *     [取消]→原状态）；
 *   - Open* --resolveCloseDialog(确认)--> CloseConfirmed --> Draining
 *     （S1）或 Open*（S2：A 入后台持有点，B 绑定 epoch++）；
 *   - Draining --closed()==true--> Closed --> NoProject（瞬态，epoch++）；
 *   - Draining --cancelDrainWait()--> Open*（仅当尚未 abandon——状态表
 *     "取消等待→（仅当尚未 abandon 时）恢复显示"）；
 *   - Draining --T_force--> 强制结束确认；--T_force2--> 放弃等待（防线 4，
 *     INV-SES-4：绝不无限等待）。
 *
 * 不变量实现位置（契约 acceptance 2——逐条自证见模型测试用例）：
 *   - INV-SES-1：m_session.writable 是唯一只读判定源——isReadOnlySession()
 *     只读该字段，不存在心跳/控件/上次结果的第二来源（编译面保证）；
 *   - INV-SES-2：不存在双 Open* 界面会话——B 只能经"切换绑定"路径进入
 *     Open*，且该路径前置＝A 已转入 Draining 持有点（结构性排除双 Open*）；
 *   - INV-SES-3：Draining 后台持有点＝DrainingHold 的 shared_ptr 保活，
 *     subscribeClose 回调/轮询观察到 closed 后释放；
 *   - INV-SES-4：Draining 全程有界——T_force 强制终止确认＋T_force2 兜底
 *     放弃，两阈值均自 Draining 起点计时（防线 3→4 串联总预算有界）。
 *
 * 异常语义（AGENTS §3——调用方错误 fail-fast，环境错误走返回值/诊断）：
 *   - 构造期：deps.storeFactory 为空 → std::invalid_argument（装配错误）；
 *   - beginClose/beginSwitch/resolve 系列/pollDrain/cancelDrainWait 等
 *     在非法状态调用 → std::logic_error（调用次序契约违约）；
 *   - 决议选"保存"但会话只读 / 接线点缺失 → std::logic_error（装配或
 *     呈现层契约违约——不静默降级，避免"以为保存了"的假象）。
 */
class UiSessionController final : public IUiStoreCloseObserver {
public:
    /**
     * @brief 构造并校验注入面（storeFactory 必填——装配错误 fail-fast）。
     *
     * @param deps [in] 注入包（见类型注释——所有权在装配层，控制器只持
     *             引用；steadyClock 未提供时缺省 steady_clock::now）
     *
     * @throws std::invalid_argument 若 deps.storeFactory 为空（没有打开
     *         端口的控制器没有存在意义——装配期拦截，不留给运行期）
     */
    explicit UiSessionController(UiSessionControllerDeps deps);

    ~UiSessionController() override;
    UiSessionController(const UiSessionController&) = delete;
    UiSessionController& operator=(const UiSessionController&) = delete;

    // ---- 会话状态观测（UI 线程）----

    /**
     * @brief 当前会话状态（§5.2 七态——呈现门控的唯一会话轴）。
     *
     * Closed 为瞬态：控制器在完成关闭的同一调用内把状态推进到 NoProject
     * （状态表"立即转 NoProject/Opening"原文）；外部观察不到停驻的 Closed。
     */
    UiSessionState state() const noexcept { return m_state; }

    /**
     * @brief 会话纪元（§6.2——打开成功/关闭完成/切换绑定新项目时 ++；
     *        单调 uint64，迟到事件过滤的匹配键）。
     *
     * 后台持有点排空完成**不**递增：epoch 守护的是"当前 UI 会话"的迟到
     * 事件路由（§6.2 三分支），持有点释放不改变当前会话的身份轴——切换
     * 场景的递增发生在"切换绑定新项目"时刻（一次性，见 §6.2 原文三触发
     * 的互斥解释）。
     */
    std::uint64_t epoch() const noexcept { return m_epoch; }

    /**
     * @brief 是否存在已打开的项目会话（状态 ∈ {OpenWritable, OpenReadOnly}）。
     */
    bool hasOpenSession() const noexcept
    {
        return m_state == UiSessionState::OpenWritable
               || m_state == UiSessionState::OpenReadOnly;
    }

    /**
     * @brief 当前会话是否只读（INV-SES-1 的唯一读出点）。
     *
     * 只读 m_session.writable（打开结果直读——OS 排他锁持有状态的投影）；
     * 不以心跳、控件状态或"上次结果"推断（§5.2 INV-SES-1 原文）。无会话
     * 时返回 false（无项目态没有"只读会话"语义——写命令禁用由无项目门控
     * 承担，PM-10/§7.5）。
     */
    bool isReadOnlySession() const noexcept;

    /**
     * @brief 当前工作台上下文投影（与 presentContext 注入的同一快照——
     *        无壳测试场景的观测面；无项目时 project=nullopt）。
     */
    const ProjectContextProjection& context() const noexcept { return m_context; }

    /**
     * @brief 会话脏标记的生产者接线点（UI-T12 增量——§16.7 v1.4）。
     *
     * 生产者＝DraftController（§10.5/UI-T12：编辑会话的 anyDirty 事实
     * 源，经 DraftControllerDeps.onSessionDirtyChanged 翻转回调到达）；
     * 本方法是该事实的**唯一注入面**——m_sessionDirty 的其余写点仍是
     * 关闭对话框处置（[放弃]/[保存]清除）与绑定重置，语义零变化。
     *
     * 为什么需要它：标题 `*` 判定位＝DraftProjection.present ∨ 会话脏
     * 标记（PM-11/§4.2 状态栏行），上下文投影在 presentContext 组装时
     * 读 m_sessionDirty——没有生产者接线，"编辑后未首次落盘"的脏态无
     * 从进入投影（UI-T11 落位登记的既定缺口："生产者接线随 UI-T12
     * 契约头落地复核"）。
     *
     * @param dirty [in] true＝存在未应用修改（任一挂接模块会话脏或磁盘
     *              草稿 present）；false＝全部消费/清除
     */
    void reportSessionDirty(bool dirty);

    // ---- 打开（§5.2 NoProject→Opening→Open*）----

    /**
     * @brief 打开项目（workflow 编排入口→控制器，§5.2 状态机图首迁移）。
     *
     * 时序：state=Opening → IUiStoreFactoryPort::open（五步协议）→ 成功：
     * 绑定 SessionPortBundle、epoch++、订阅关闭回调、注入上下文快照、
     * 降级只读时装配横幅；失败：回原状态（NoProject——"失败→错误页，
     * 停留/回退 NoProject，不动当前项目"，§5.2 图）。
     *
     * @param canonicalPath [in] 项目规范路径（错误页定位数据源）
     * @param mode          [in] 请求模式（Writable 降级只读不阻塞——PM-07）
     * @return 打开报告（横幅/错误页数据随附——见 SessionOpenReport）
     *
     * @throws std::logic_error 若当前状态已是 OpenWritable/OpenReadOnly/
     *         Opening/Draining（打开请求须从无项目/已关闭态发起——切换
     *         走 beginSwitch；INV-SES-2 的调用面防线）
     */
    SessionOpenReport openProject(const std::string& canonicalPath, UiOpenMode mode);

    // ---- 关闭/切换：统一确认对话框机制（§5.4 S1/S2）----

    /**
     * @brief 发起关闭/退出请求（§5.4 S1 首步——装配并请求呈现对话框）。
     *
     * 状态不变（对话框呈现期间会话仍在 Open*——"取消→回到原状态"要求
     * 原状态可恢复；§5.2 状态表 Open* 行"closeRequest/switchRequest/
     * exitRequest（统一确认对话框……取消→回到原状态）"原文）。挂起的
     * 对话框请求是单槽：未决议前再次 begin* 是调用次序违约。
     *
     * @param intent [in] 关闭意图（CloseProject/ExitApplication——决定
     *               关闭完成后的去向，见 UiCloseIntent）
     * @return 对话框数据（呈现层渲染后把用户决议交回 resolveCloseDialog）
     *
     * @throws std::logic_error 若当前状态不是 Open*，或已有挂起的对话框
     *         请求未决议
     */
    CloseDialogData beginClose(UiCloseIntent intent);

    /**
     * @brief 发起项目切换请求 A→B（§5.4 S2 首步——先对 A 弹统一确认）。
     *
     * 对话框针对 A 装配（与 beginClose 同一机制——"对话框（同上，针对
     * A）"原文）；候选 B 的验证在**决议确认后**执行（"候选验证成功才切"，
     * PM-03）——失败则 A 界面会话不变（CandidateRejected）。
     *
     * @param candidatePath [in] 候选项目 B 的规范路径
     * @param mode          [in] 候选打开模式
     * @return 对话框数据（针对 A——草稿区/任务区均为 A 的行集）
     *
     * @throws std::logic_error 同 beginClose（须 Open* 态且无挂起请求）
     */
    CloseDialogData beginSwitch(const std::string& candidatePath, UiOpenMode mode);

    /**
     * @brief 解析统一确认对话框的用户决议（§5.4 S1/S2 的分支执行者）。
     *
     * 分支表（逐行对齐 §5.4 时序）：
     *   - [取消] → 回到原状态（清除挂起请求——不执行任何处置）；
     *   - 保存草稿 → saveAllDraftsManual()（须可写会话＋已接线）；失败→
     *     关闭中止、回到原状态（SaveFailed——不允许"没保存成还关了"）；
     *   - 放弃 → 会话脏数据丢弃（磁盘草稿保留策略归 §8.6 DraftController）；
     *   - 协作取消 → 对**重查**的非终态任务逐个 requestCancel（NFR-PERF-02
     *     收敛由 execution 保证——重查避免决议期间任务集变化的陈旧行）；
     *   - 等待 → 无额外动作（Draining 中保持进度可见——§5.6 防线 1）；
     *   - 切换流：上述处置完成后做候选验证——成功则 A 转入 Draining 背景
     *     持有点（INV-SES-3）＋B 绑定（epoch++，INV-SES-2 结构性保序）；
     *     失败则 A 会话不变（CandidateRejected——"候选验证成功才切"）。
     *
     * @param decision [in] 用户决议（confirmed=false 时其余字段被忽略）
     * @return 决议执行结果（四值——见 CloseDialogResolution 词表）
     *
     * @throws std::logic_error 若无挂起的对话框请求；或决议选"保存"但
     *         会话只读（§5.5 违约）或 saveAllDraftsManual 未接线（装配
     *         违约）；或切换流确认但 forceAbandonAll 之外的接线缺失不
     *         在此处校验（候选验证走工厂端口，无函数接线需求）
     */
    CloseDialogResolution resolveCloseDialog(const CloseDecision& decision);

    // ---- Draining 防线（§5.6——INV-SES-4）----

    /**
     * @brief Draining 周期轮询（§5.6 四级防线的 UI 线程驱动点）。
     *
     * 调用方（L5 壳的定时器/测试的显式驱动）在 Draining 期间周期调用；
     * 单次调用按序执行防线检查：
     *   1. 兜底轮询 closed()（防线 4 的检测半区——回调丢失时仍能观察到
     *      关闭完成）→ 已关闭则完成关闭（ClosedNow）；
     *   2. 后台持有点逐个轮询 closed() → 已排空的释放（holdsReleased——
     *      INV-SES-3 的轮询兜底；持有点无强制终止路径——§5.7"迟到结果
     *      照常归档"，不催促）；
     *   3. T_force 计时（防线 3）→ 到点且未呈现过确认框 → ForceConfirmDue
     *      （数据经 forceCloseDialogData() 取用；呈现一次后不重复触发）；
     *   4. T_force2 计时（防线 4）→ 到点则放弃等待：先补行强制终止序列
     *      （若防线 3 未执行过——见实现注释），再出线
     *      UI-SESSION-CONTEXT-INVALID（目录 Warning＋Dev 日志双通道）并
     *      完成关闭（GivenUp——数据损失限于未归档结果，检查点保留；
     *      INV-SES-4"绝不无限等待"）。
     *
     * @return 轮询报告（等待中/确认待呈现/放弃/本次完成——见类型注释）
     *
     * @throws std::logic_error 若当前不在 Draining 且无后台持有点（无
     *         轮询对象——调用次序违约）
     */
    DrainPollReport pollDrain();

    /**
     * @brief 取强制结束确认对话框数据（T_force 到点后的呈现输入）。
     *
     * @return 确认框数据（waitedText 由本方法按内部计时渲染为文本——
     *         呈现层原样显示，不二次计时，保证防线反馈与实际等待一致）
     *
     * @throws std::logic_error 若 T_force 未到点（无数据可取——呈现层
     *         只应在 pollDrain 报告 ForceConfirmDue 后调用）
     */
    ForceCloseDialogData forceCloseDialogData() const;

    /**
     * @brief 解析强制结束确认对话框的用户决议（§5.6 防线 3 的执行半区）。
     *
     * 确认 → 对重查的非终态任务逐个 requestForceTerminate（任务记 Failed
     * ＋EX-FORCE-TERMINATED，最近检查点保留可续——§9.4）＋触发
     * forceAbandonAll（L5 abandonAll 兜底，§11.5）＋标记 abandon（此后
     * cancelDrainWait 拒绝——"仅当尚未 abandon 时"恢复）＋置防线 4 的
     * T_force2 观察窗；拒绝 → 继续等待（不重复弹确认；T_force2 兜底
     * 仍然生效——INV-SES-4 不依赖用户在场）。
     *
     * @param confirmed [in] true＝强制结束并关闭；false＝继续等待
     *
     * @throws std::logic_error 若不在 Draining/T_force 未到点；或确认但
     *         forceAbandonAll 未接线（装配违约——强杀序列缺兜底半区，
     *         不允许半执行）
     */
    void resolveForceCloseDialog(bool confirmed);

    /**
     * @brief 取消 Draining 等待、恢复旧项目显示（状态表 Draining 行
     *        "取消等待→（仅当尚未 abandon 时）恢复显示"）。
     *
     * 恢复语义：状态回到关闭前的 Open*（显示恢复）；存储上下文仍处于
     * 关闭信号已发出的状态（requestClose 幂等——再次关闭时重发即可，
     * Draining 计时重启）。已 abandon（强制终止已执行/T_force2 已放弃）
     * 时拒绝恢复并返回 false。
     *
     * @return true＝已恢复显示；false＝已 abandon，不可恢复（调用方据
     *         此保持 Draining 呈现）
     *
     * @throws std::logic_error 若当前不在 Draining
     */
    bool cancelDrainWait();

    // ---- 已释放上下文的迟到写提示（§5.3 行 4/§5.7——UI-SES-7/UI-LCY-1）----

    /**
     * @brief 装配"项目上下文已释放"提示并出线诊断（UI-SESSION-CONTEXT-
     *        INVALID 的唯一呈现面）。
     *
     * 调用时机：命令/草稿写路径对已释放上下文提交被对端以 context-closed
     * 拒绝后，由呈现层（UI-T13 桥）调用本方法取提示数据。"不重试写"由
     * 结构保证：ContextInvalidNotice 不携带任何可执行动作（唯一建议是
     * 重新打开，编排归 workflow——§5.3 行 4/§5.7"不重试、不缓存待写"
     * 原文），本方法也不接受任何待写载荷。
     *
     * 诊断出线（双通道）：目录 Warning（码表严重级别——§3.5 码表行为
     * 权威；无目录场景跳过，不虚构条目）＋Dev 日志明文（§5.6"（Dev 级）"
     * 的排障半区——回调丢失/迟到提交是开发侧可观测事实）。
     *
     * @return 提示数据（主文案＋建议文案键——呈现层经 UiText 解析）
     */
    ContextInvalidNotice makeContextInvalidNotice();

    // ---- 后台持有点观测（INV-SES-3 的测试/呈现面）----

    /**
     * @brief 当前后台持有点数（切换后未排空的旧项目存储上下文数）。
     */
    std::size_t drainingHoldCount() const noexcept { return m_holds.size(); }

    /**
     * @brief 取并清除"退出就绪"位（关闭意图＝ExitApplication 的关闭完成
     *        后为 true——L5/workflow 据此走 scheduler.shutdown(DrainPolicy)
     *        ＋应用退出，§9.5/§11.5 分工：调度器排空调用权在 L5）。
     *
     * @return 上一值（true＝有一次未消费的退出就绪；取走即清除——幂等
     *         消费由调用方保证只走一次退出序列）
     */
    bool takeExitPending() noexcept
    {
        const bool pending = m_exitPending;
        m_exitPending = false;
        return pending;
    }

    // ---- IUiStoreCloseObserver（存储上下文关闭回调——端口订阅面）----

    /**
     * @brief 存储上下文关闭完成回调（§5.2 Draining→Closed 的触发面）。
     *
     * 线程契约见类注释（L5 适配器负责 Marshal 到 UI 线程——本实现不设
     * 锁，依赖投递纪律；防线 4 的 closed() 轮询是投递纪律被破坏时的
     * UI 线程兜底）。语义：当前会话在 Draining 且身份匹配 → 完成关闭；
     * 否则按项目身份匹配后台持有点 → 释放该持有点（INV-SES-3）。
     * 无匹配（重复回调/未知项目）→ 忽略＋Dev 日志（幂等防御）。
     *
     * @param project [in] 已释放的项目身份
     */
    void onStoreClosed(const core::ProjectId& project) override;

private:
    /// 当前会话绑定（§5.1"项目会话"的承载——无会话时 store 为空）。
    struct SessionBinding {
        ProjectMetadataProjection metadata;
        std::shared_ptr<IUiProjectStorePort> store;      ///< C-3 关闭协议面（shared 持有＝存储上下文保活）
        std::shared_ptr<IUiDraftQueryPort> drafts;       ///< C-5 草稿清单面
        std::shared_ptr<IUiSessionTaskPort> tasks;       ///< C-8 任务查询/控制面
        bool writable = false;                            ///< INV-SES-1 唯一数据源（打开结果直读）
    };

    /// 后台排空持有点（INV-SES-3——切换后旧项目的保活引用，closed 后释放）。
    struct DrainingHold {
        ProjectMetadataProjection metadata;               ///< 旧项目元数据（后台清单呈现/回调匹配键）
        std::shared_ptr<IUiProjectStorePort> store;       ///< 保活引用（§5.1"保持 shared 持有直到 subscribeClose 回调"）
        std::shared_ptr<IUiSessionTaskPort> tasks;        ///< 后台任务只读清单数据面（§5.4 迟到任务呈现）
        std::unique_ptr<core::IEventSubscription> subscription; ///< 关闭回调订阅（析构即退订）
    };

    // ---- 内部迁移助手（单一写点——状态字段只在私有助手内变更）----

    void resetToNoProject();
    void bindSession(const OpenedProjectFacts& facts, SessionPortBundle&& bindings);
    void presentContextLocked();
    CloseDialogData assembleDialogForCurrentSession() const;
    void executeForceSequence();
    void finishCurrentClose();
    void emitDevLine(const std::string& message) const;
    void emitContextInvalidDiagnostic();

    /// 注入包（装配期给定，运行期只读）。
    UiSessionControllerDeps m_deps;
    /// 会话状态（§5.2 七态——唯一写点在私有助手，UI 线程串行）。
    UiSessionState m_state = UiSessionState::NoProject;
    /// 会话纪元（§6.2——递增时机见 epoch() 注释）。
    std::uint64_t m_epoch = 0;
    /// 当前会话绑定（无会话时 store 为空——hasOpenSession 与状态字段互证）。
    SessionBinding m_session;
    /// 工作台上下文投影（与 presentContext 注入同源——观测面）。
    ProjectContextProjection m_context;
    /// 最后上下文项目锚（bindSession 时更新、关闭后保留——迟到写提示的
    /// DiagContext.project 锚定面：UI-LCY-1 场景在 Closed 后出线，会话
    /// 绑定已清空但诊断仍须可追溯到来源项目）。
    core::ProjectId m_lastAnchorProjectId{};
    /// 会话脏标记（§8.5 会话半区——放弃处置清除，保存/落盘由草稿侧维护）。
    bool m_sessionDirty = false;

    // ---- 挂起的统一确认对话框（单槽——begin/resolve 必须配对）----
    /// 是否有挂起的对话框请求（begin* 置位、resolve* 清除）。
    bool m_dialogPending = false;
    /// 挂起请求的关闭意图（CloseProject/ExitApplication——切换时复用
    /// CloseProject 语义处置 A）。
    UiCloseIntent m_pendingIntent = UiCloseIntent::CloseProject;
    /// 挂起请求是否切换流（true 时 resolve 后段做候选验证＋绑定 B）。
    bool m_pendingSwitch = false;
    /// 候选项目路径（切换流——候选验证输入）。
    std::string m_pendingCandidatePath;
    /// 候选打开模式（切换流）。
    UiOpenMode m_pendingCandidateMode = UiOpenMode::Writable;

    // ---- Draining 防线状态（§5.6——全部计时自 Draining 起点）----
    /// Draining 起点单调时刻（cancelDrainWait 后再次关闭时重启）。
    std::chrono::steady_clock::time_point m_drainStartedAt{};
    /// 进入 Draining 时 requestClose() 返回的在途引用数（防线 1 初始反馈
    /// 值——DrainPollReport.inFlightReferences 的数据源）。
    std::uint32_t m_drainInFlightReferences = 0;
    /// 关闭订阅句柄（subscribeClose 返回——成员析构即退订，RAII）。
    std::unique_ptr<core::IEventSubscription> m_closeSubscription;
    /// T_force 确认框是否已呈现（呈现一次后不重复触发——防线 3 单次）。
    bool m_forcePromptPresented = false;
    /// 强制终止序列是否已执行（防线 3 确认或防线 4 补行——abandon 标记）。
    bool m_forceExecuted = false;
    /// Draining 是否已放弃/废弃（true 后 cancelDrainWait 拒绝恢复）。
    bool m_drainAbandoned = false;
    /// 关闭意图（Draining 期间保留——决定完成后的退出就绪位）。
    UiCloseIntent m_drainIntent = UiCloseIntent::CloseProject;

    /// 后台排空持有点集（INV-SES-3——切换后旧项目保活，closed 后释放）。
    std::vector<DrainingHold> m_holds;
    /// 退出就绪位（ExitApplication 关闭完成置位——takeExitPending 消费）。
    bool m_exitPending = false;
    /// 关闭完成次数（观测面——Closed 瞬态不可停驻，计数即审计轨迹）。
    std::size_t m_closeCompletedCount = 0;
};

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_UISESSIONCONTROLLER_HPP