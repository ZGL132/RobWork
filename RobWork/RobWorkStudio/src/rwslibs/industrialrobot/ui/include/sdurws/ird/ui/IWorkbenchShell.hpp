/**
 * @file   IWorkbenchShell.hpp
 * @brief  工作台壳门面（WorkbenchShell）——L5 应用壳装配 ui 的唯一入口：
 *         五区布局、布局记忆、无项目首页、状态栏格式与壳层命令门控。
 *
 * 设计依据：
 *   - units/ui.md §10.1（IWorkbenchShell 装配门面：ShellWiring 注入、
 *     initialize 恰好一次、mainWindow 唯一 Widget 出口、有界 shutdown、
 *     壳层命令集与默认布局装载的后置条件）、§4.1~§4.6（五区布局总图/区域
 *     交互边界/最小可用布局/布局状态归属/用户级设置项）、§7.1（最小命令集
 *     的壳层子集）、§7.5（可用性谓词口径）、§3.5（UI-* 稳定诊断码建议值）；
 *   - O-31 裁决（DTB §4，2026-09-19）：ShellWiring 持有 ui 自有端口
 *     （IPolicySummarySource/IUiNameResolver）——产品面零对
 *     project/evidence/execution/policy/runtime 的链接或 include；本头同时
 *     是该裁决"接口签名随首个消费任务实现冻结并做单元卡增量修订登记"
 *     机制的第一个落点（UI-T03 增量：devLog 成员与五区/上下文/门控 facets，
 *     登记 ui.md §10.1 v0.5）；
 *   - 需求 UX-09（五区布局与跨会话记忆）、PM-10（无项目首页）、PM-11
 *     （状态栏格式）、PM-14（用户级设置持久化，绝不写入 .rwdesign）。
 *
 * 背景说明（门面的装配方向）：壳由 L5 独占持有（unique_ptr），L5 经
 * createWorkbenchShell() 创建、initialize(ShellWiring) 注入运行时协作面，
 * 之后全部交互收口在 mainWindow()（唯一 QWidget 出口——L5 只拿到这一个
 * Widget，其余五区/面板均为壳私有，ARC-02 控件互读红线由此收口）。
 * 具体实现类不进公共头（R-2：WorkbenchShell_p.hpp 留在 src/）。
 *
 * 线程模型（ui.md §3.4）：initialize/shutdown 允许在 L5 装配线程调用
 * （内部完成 UI 线程移交）；其余方法只允许 UI 线程调用（违规＝未定义行为
 * ＋DT 断言——§10.1 通用约定原文）。
 */

#ifndef SDURWS_IRD_UI_IWORKBENCHSHELL_HPP
#define SDURWS_IRD_UI_IWORKBENCHSHELL_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <sdurws/ird/core/Events.hpp>             // core::IDomainEventBus（§10.1 wiring——表内登记边）
#include <sdurws/ird/diagnostics/Catalog.hpp>     // diagnostics::IDiagnosticSink/IDevLogSink（表内登记边；Dev 码走 IDevLogSink——§6.2）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>   // diagnostics::CodeDescriptor（ui 码表描述符——§3.5）
#include <sdurws/ird/diagnostics/Redaction.hpp>   // diagnostics::IRedactionService（表内登记边）
#include <sdurws/ird/ui/UiPorts.hpp>              // ui::IPolicySummarySource/IUiNameResolver（O-31 ui 自有端口）
#include <sdurws/ird/ui/UiProjections.hpp>        // ui::ProjectContextProjection/RecentProjectEntry（值投影）

class QWidget;  // 前置声明：mainWindow() 的返回类型；头文件不拖入 Widgets（消费者按需自含）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 五区词表（§4.1 布局总图）
// =====================================================================

/**
 * @brief 工作台五区标识（§4.1：顶栏/左栏/中央工作区/右栏/底部任务和状态区）。
 *
 * 五区均为 QDockWidget 族容器（中央区为主视图区）——枚举序即 §4.1 总图
 * 从上到下、从左到右的空间序，一经交付只允许表尾追加并升单元卡修订
 * （持久化布局记忆按此值序列化，重排会破坏跨会话恢复）。
 */
enum class WorkbenchRegion : std::uint8_t {
    Top,      ///< 顶部工具栏（项目入口/阶段导航/保存/撤销/任务状态/只读徽标）
    Left,     ///< 左栏（项目对象树＋阶段任务列表——阶段就绪投影驱动）
    Central,  ///< 中央工作区（阶段面板栈/无项目首页——主视图区）
    Right,    ///< 右栏（属性编辑区＋诊断与设置区）
    Bottom,   ///< 底部任务和状态区（结果/任务进度/诊断/日志/下一步建议页签）
};

// =====================================================================
// 壳层命令词表（§7.1 CommandScope 的 UI-T03 子集——完整注册表随 UI-T06）
// =====================================================================

/**
 * @brief 壳层命令作用域（§7.1 CommandScope 词表：会话态/项目作用域/视图
 *        作用域）。
 *
 * UI-T03 冻结说明：完整命令注册表（ICommandRegistry，§10.3）随 UI-T06
 * 落地；本枚举是壳层命令门控（§7.5 口径的最小子集）所需的作用域轴，词表
 * 值与 §7.1 一致，UI-T06 直接复用不重定义（NFR-MNT-03）。
 */
enum class ShellCommandScope : std::uint8_t {
    Session,  ///< 会话态（无项目也可用——如 project.new/open、命令面板）
    Project,  ///< 项目作用域（须有项目；readonlyAllowed=false 者还须可写会话）
    View,     ///< 视图作用域（纯视图操作——如 view.resetLayout，恒可用）
};

/**
 * @brief 壳层命令可用性查询结果（§7.5"谓词结果是界面使能态，不是业务判定"）。
 *
 * registered=false 表示该 id 未在壳层登记（完整注册表的未知命令拒绝＋
 * UI-CMD-UNKNOWN 诊断随 UI-T06 落地；UI-T03 壳面只登记 §7.1 壳层子集）。
 * visible/enabled 是菜单/面板/工具栏三处一致使能态的单一来源（§7.6）；
 * reasonKey 为禁用原因文案键（UX-02 文案键体系；值解析随 UI-T09 UiText
 * 落地——UI-T03 阶段键本身即稳定标识）。
 */
struct ShellCommandAvailability {
    /// 该 id 是否已在壳层登记（§7.1 壳层子集）。
    bool registered = false;
    /// 是否可见（PM-10 无项目态：项目作用域命令按"禁用＋说明"保留发现性
    /// ——§7.4，故 visible 恒为登记命令的真值）。
    bool visible = false;
    /// 是否可执行（界面使能态——真实校验仍在目标服务，§7.5 红线）。
    bool enabled = false;
    /// 禁用原因文案键（enabled=false 时非空；如 no-project/readonly）。
    std::string reasonKey;
};

// =====================================================================
// ShellWiring——L5 装配期注入面（§10.1 v0.4 裁决签名＋UI-T03 首消费增量）
// =====================================================================

/**
 * @brief 工作台壳的运行时协作面注入包（L5 应用壳装配期一次性给出）。
 *
 * 成员语义（§10.1 v0.4 裁决签名逐字承载＋登记增量）：
 *   - 全部为共享引用（所有权仍在 L5/装配层，壳只持引用——§10.1 所有权行）；
 *   - eventBus 允许为空＝无事件测试场景（须显式声明——§10.1 前置条件行
 *     原文；投影管线的事件订阅随 UI-T04+ 接入）；
 *   - devLog 为 UI-T03 首消费增量（登记 ui.md §10.1 v0.5）：UI-LAYOUT-
 *     RESTORE-FAILED 等 Dev 级码**不入目录**（diagnostics §6.2——IDiagnostic
 *     Sink::append 拒绝 Dev），走开发日志通道 IDevLogSink（diagnostics 公共
 *     类型，ui→diagnostics 表内边）；允许为空＝无日志测试场景（与 eventBus
 *     同款"须显式声明"），为空时回退默认布局的行为不受影响（恢复不得依赖
 *     日志可用性——§4.5"不得阻塞启动"）；
 *   - diagFactory 为 UI-T06 首消费增量（登记 ui.md §10.1 v0.8）：命令/快捷
 *     键注册表的用户级拒绝码（UI-CMD-UNKNOWN/UI-CMD-NOT-EXECUTABLE/
 *     UI-HOTKEY-CONFLICT）经 IDiagnosticFactory::create 唯一入口产条目
 *     （§9.2）——工厂绑定的 StableCodeRegistry 句柄不入 wiring（v0.5 裁决
 *     签名纪律），由 L5 装配序列完成码注册后给出工厂。允许为空＝无目录
 *     测试场景（与 eventBus 同款"须显式声明"）；为空时拒绝语义由返回值轨
 *     与状态栏说明承载（不虚构目录条目）；
 *   - policySource/nameResolver 为 O-31 裁决的 ui 自有端口（UiPorts.hpp），
 *     L5 适配对端（policy::IPolicyProvider/runtime::IRuntimeNameResolver）；
 *   - aboutSource 为 UI-T10 首消费增量（登记 ui.md §10.1 v1.2）：关于框
 *     数据源端口 IUiAboutDataSource（UiPorts.hpp——§11.4 装配报告＋冻结
 *     版本基线的注入面），help.about 处理器打开关于框时现取。允许为空＝
 *     无关于数据测试场景（须显式声明——与 eventBus 同款纪律）；为空时
 *     关于框照常打开：清单＝白名单占位行、版本区「未装载」占位（零虚构）。
 */
struct ShellWiring {
    /// 领域事件总线（core 表内边；允许为空＝无事件测试场景，须显式声明）。
    std::shared_ptr<core::IDomainEventBus> eventBus;
    /// 诊断统一 sink（用户级诊断入目录的通道；Dev 码不可经此——append 拒绝）。
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    /// 脱敏服务（诊断/日志呈现前的强制脱敏——NFR-SEC-07；UI-T13 呈现消费）。
    std::shared_ptr<diagnostics::IRedactionService> redaction;
    /// 开发日志通道（Dev 级码唯一出线——diagnostics §6.2；UI-T03 增量成员，
    /// 允许为空＝无日志测试场景，须显式声明）。
    std::shared_ptr<diagnostics::IDevLogSink> devLog;
    /// 诊断工厂（用户级码 create 唯一入口——§9.2；UI-T06 增量成员，允许为
    /// 空＝无目录测试场景，须显式声明）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
    /// 策略摘要端口（O-31：ui 自有端口，L5 适配 policy::IPolicyProvider——
    /// §6.7 策略摘要只读面语义不变；UI-T07 消费）。
    std::shared_ptr<ui::IPolicySummarySource> policySource;
    /// 名称解析端口（O-31：ui 自有端口，L5 适配 runtime::IRuntimeNameResolver
    /// ——resolveObjectId 取 localName 语义不变；左栏对象树消费）。
    std::shared_ptr<ui::IUiNameResolver> nameResolver;
    /// 关于框数据源端口（UI-T10 增量：ui 自有端口 IUiAboutDataSource——
    /// §11.4 装配报告＋冻结版本基线；允许为空＝无关于数据测试场景，须
    /// 显式声明，为空时关于框以占位呈现不虚构数据）。
    std::shared_ptr<ui::IUiAboutDataSource> aboutSource;
};

// =====================================================================
// 拆卸报告与生命周期结果值
// =====================================================================

/**
 * @brief 有界 shutdown 的结果报告（§10.1 shutdown 返回值；§5.6 有界拆除）。
 *
 * "有界"在 UI-T03 的口径：壳自身拆卸（布局落盘＋Widget 销毁）同步有界
 * 完成，无任何无上限等待；项目关闭的 drain 协议（在途任务等待/强杀确认，
 * §5.6 主体）归 UiSessionController（UI-T11），不在本报告语义内。
 */
struct ShellTeardownReport {
    /// shutdown 前壳处于已 initialize 状态（false＝未初始化即拆卸——调用
    /// 方次序违约，壳按幂等空操作处理）。
    bool wasInitialized = false;
    /// 布局/设置是否成功持久化到用户级存储（PM-14；false＝写队列为空或
    /// 落盘失败——下次启动回到上次成功落盘的位形）。
    bool layoutPersisted = false;
    /// 拆卸序列是否完整走完（true 后 mainWindow() 返回 nullptr 且不可再用
    /// ——§10.1 后置条件；shutdown 幂等，重复调用返回末次报告）。
    bool completed = false;
};

// =====================================================================
// IWorkbenchShell——装配门面（§10.1）
// =====================================================================

/**
 * @brief 工作台壳门面（L5/workflow 消费的 ui 唯一装配入口之一——§3.3）。
 *
 * 生命周期/所有权（§10.1 契约表）：壳由 L5 独占持有（unique_ptr，经
 * createWorkbenchShell() 取得）；注入实例所有权在 L5/装配层，壳只持共享
 * 引用。前置条件：initialize 恰好一次、先于其余一切调用。非法使用：
 * 二次 initialize；从非 UI 线程触碰；shutdown 后调用除 shutdown 外任何
 * 方法（§10.1"非法"行——违约按调用方错误处置：DT 断言＋安全空返回）。
 */
class IWorkbenchShell {
public:
    virtual ~IWorkbenchShell() = default;

    /**
     * @brief 装配：注入协作面、注册壳层命令集与默认布局、装载布局记忆。
     *
     * 后置（§10.1）：壳层命令集（§7.1 子集）可用性门控就位；五区按出厂
     * 位形或用户级布局记忆（PM-14）就位；mainWindow() 可显示；无项目首页
     * 呈现（PM-10）。布局记忆损坏/版本不识别时：回退出厂默认＋
     * UI-LAYOUT-RESTORE-FAILED（Dev，经 wiring.devLog）＋损坏段整段丢弃，
     * **不阻塞启动**（§4.5）。
     *
     * @param wiring [in] 注入包（diagSink/redaction/policySource/nameResolver
     *               必须非空；eventBus/devLog 允许为空但须显式声明——§10.1）
     * @return true＝装配成功；false＝已 initialize 过（恰好一次违约）或
     *         wiring 必填指针为空（调用方错误——fail-fast 语义的返回值轨，
     *         §10.1"错误类型：返回 bool/report，不抛"）
     *
     * @note 可在 L5 装配线程调用（内部完成 UI 线程移交——§10.1 线程行）。
     */
    virtual bool initialize(const ShellWiring& wiring) = 0;

    // ---- 五区布局面（UI-T03；§4.1/§4.4/§4.5）----

    /**
     * @brief 查询五区当前可见性。
     *
     * @param region [in] 五区标识
     * @return true＝可见；Top/Central 恒 true（最小可用布局三要素恒在——
     *         §4.4）；低于最小尺寸的自动折叠期间 Left/Right/Bottom 返回
     *         false（折叠即隐藏——§4.4"左/右栏自动折叠为图标条"的阶段 A
     *         实现口径）
     */
    virtual bool regionVisible(WorkbenchRegion region) const = 0;

    /**
     * @brief 设置五区可见性（视图菜单开关的壳内实现——§4.1"支持隐藏"）。
     *
     * @param region  [in] 五区标识（Top/Central 不接受隐藏——三要素恒在，
     *                本方法对其为无操作）
     * @param visible [in] true＝显示；false＝隐藏（用户隐藏在尺寸折叠恢复
     *                后仍生效——折叠不覆盖用户意愿，§4.4 折叠语义）
     */
    virtual void setRegionVisible(WorkbenchRegion region, bool visible) = 0;

    /**
     * @brief 恢复出厂默认布局（壳层命令 view.resetLayout 的实现——§4.5）。
     *
     * 把五区恢复出厂位形（停靠位形/可见性/几何）并清除该会话的浮动窗口
     * 记忆（§4.4 原文）；不影响用户级设置中的最近项目等非布局项。
     */
    virtual void resetLayout() = 0;

    // ---- 工作台上下文投影输入（PM-10/PM-11/§7.5 门控的注入口）----

    /**
     * @brief 注入工作台上下文投影（状态栏/首页/命令门控的原子快照）。
     *
     * project=nullopt 切入无项目首页态（PM-10：三入口呈现、项目作用域命令
     * 禁用、命令面板可达）；有值切入项目态（状态栏进入 PM-11 格式、中央区
     * 切阶段面板栈、只读徽标随 writable）。完整会话状态机（打开/关闭/切换
     * 时序）归 UiSessionController（UI-T11），本方法是该控制器落地前壳的
     * 唯一上下文注入口（投影值语义见 UiProjections.hpp）。
     *
     * @param context [in] 上下文投影（一次一致快照——值语义拷贝）
     */
    virtual void presentProjectContext(const ProjectContextProjection& context) = 0;

    // ---- 壳层命令可用性（§7.5 门控子集；完整注册表随 UI-T06）----

    /**
     * @brief 查询壳层命令可用性（§7.1 壳层子集；UI-SES-1 观测面）。
     *
     * @param commandId [in] 命令 id（点分小写——§7.1 语法；如 "draft.apply"）
     * @return 可用性快照（registered=false＝未登记；enabled=false 时
     *         reasonKey 给出禁用原因键）
     */
    virtual ShellCommandAvailability commandAvailability(const std::string& commandId) const = 0;

    // ---- 最近项目（PM-10/PM-14——用户级设置承载）----

    /**
     * @brief 取最近项目列表（首页"最近项目"入口数据源）。
     *
     * @return 按最近使用序（最新在前）；上限 10、按规范路径去重、失效项
     *         保留（available=false——PM-10）
     */
    virtual std::vector<RecentProjectEntry> recentProjects() const = 0;

    /**
     * @brief 登记一次成功打开的项目路径（最近使用置顶；PM-10 去重/上限）。
     *
     * @param canonicalPath [in] 项目路径（规范化形态——weakly_canonical；
     *              非规范输入会被规范化后去重）
     */
    virtual void noteRecentProject(const std::string& canonicalPath) = 0;

    /**
     * @brief 从最近项目列表移除一项（PM-10"移除"动作；用户级设置同步）。
     *
     * @param canonicalPath [in] 要移除的规范路径（未在列则为无操作）
     */
    virtual void removeRecentProject(const std::string& canonicalPath) = 0;

    // ---- §10.1 固有三面 ----

    /**
     * @brief 主窗口（唯一 Widget 出口——L5 壳挂接点；§10.1）。
     *
     * @return 主窗口指针；shutdown 完成后返回 nullptr 且不可再用（§10.1
     *         后置条件）。除本指针外壳不交出任何内部 Widget（ARC-02）。
     */
    virtual QWidget* mainWindow() = 0;

    /**
     * @brief 有界拆卸：布局落盘、窗口销毁（§10.1/§5.6）。
     *
     * @return 拆卸报告（见 ShellTeardownReport）。幂等：重复调用返回末次
     *         报告，不重复拆卸。
     *
     * @note 可在 L5 装配线程调用；完成后壳实例可安全析构。
     */
    virtual ShellTeardownReport shutdown() = 0;
};

// =====================================================================
// 装配入口与 ui 诊断码表（UI-T03 冻结的壳层自由函数面）
// =====================================================================

/**
 * @brief 创建工作台壳实例（L5 独占持有——unique_ptr 所有权即刻移交）。
 *
 * 为什么是工厂函数而不是公共具体类：具体实现类属壳私有（R-2——
 * WorkbenchShell_p.hpp 只在 src/），门面模式经本函数把实现类型封闭在库内；
 * L5 侧只见 IWorkbenchShell（§3.3"面向 workflow/L5 暴露的装配入口集中在
 * IWorkbenchShell＋UiSessionController＋IPluginUiRegistrar"）。
 *
 * @return 未初始化的壳实例（initialize 前 mainWindow() 为 nullptr）
 */
std::unique_ptr<IWorkbenchShell> createWorkbenchShell();

/**
 * @brief ui 单元稳定诊断码描述符全表（ui.md §3.5 登记的九码建议值）。
 *
 * 码值权威归 diagnostics 码表（§3.5 原文）；本函数是 ui 侧的**描述符供体**
 * ——L5 装配期与 diagnostics::builtinCodeDescriptors() 一并向
 * StableCodeRegistry::registerCode 提交（§10.1 副作用行"注册 UI-* 诊断码
 * （经 diagnostics 码表）"的落地面：壳不在 initialize 内自行注册——wiring
 * 不携带注册表句柄（§10.1 裁决签名不扩），注册动作归 L5 装配序列）。
 * 分类/严重/可重试性按 §3.5 表逐行落位；paramSchema 统一 "[]"（无参——
 * §4.5 必填字段的显式空；占位参数随 UI-T13 呈现任务按需增量登记）。
 *
 * @return 九项描述符（§3.5 表行序；纯函数——每次新值，登记值编译期固定）
 */
std::vector<diagnostics::CodeDescriptor> uiDiagnosticCodeDescriptors();

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_IWORKBENCHSHELL_HPP
