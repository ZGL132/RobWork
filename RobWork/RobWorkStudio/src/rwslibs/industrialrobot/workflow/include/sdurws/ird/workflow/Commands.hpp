/**
 * @file   Commands.hpp
 * @brief  命令集编排核（WP-22-T12）——工业命令的 id 词表、执行分流与
 *         五条端口接缝（修订/会话/向导/碰撞检查/报告导出）。
 *
 * 设计依据：
 *   - units/workflow.md §8.3（最小命令集注册与工业高频命令——本单元是命令
 *     的"贡献者"，注册设施与全局快捷键唯一注册点归 ui——SA-16；执行回调
 *     分流：产生修订的命令经①命令端口、会话态命令不产生修订〔KIN-06〕、
 *     报告导出编排 reporting 导出服务〔RPT-02〕）、§8.4（"运行碰撞检查"
 *     保持会话级最小语义——P-WF-3 三方契约裁决前不得扩大为正式评估入口）
 *   - 需求 UX-13（命令面板首版 ≥10 条常用命令＋工业高频 M-13；复位关节至
 *     Home/Zero 为会话姿态、KIN-06 语义不产生修订；插件不得私占全局快捷
 *     键——未绑定命令经面板模糊搜索可达）、KIN-06（会话姿态不修改设计模
 *     型、不触发结果失效）、RPT-02（报告导出）、PM-18/AT-29（撤销/重做
 *     产生新修订、历史不改写）
 *   - ARCHITECTURE.md §7.11（CommandRegistry/HotkeyBindingTable 收归 ui；
 *     命令面板中的会话态命令不产生修订；产生修订的命令一律经①命令端口）
 *   - 任务契约 tasks/foundation/WP-22-T12.json（acceptance 1~3）
 *
 * 背景说明（本头与 ui ICommandRegistry 的分工）：命令的登记/呈现/路由/
 * 快捷键设施全部在 ui（UI-T06 已落位 CommandRegistry——唯一注册点
 * SA-16）；本头承载的是 workflow 作为命令**贡献者**的另一半义务——每条
 * 命令被提交（submit）后的**执行编排**。编排按 §8.3 分流五类：
 *   ①修订类（draft.apply/project.undo/project.redo）——经 IRevisionCommand-
 *     Port 端口触达 project ①命令端口（唯一写路径），产生恰好一个新修订
 *     （PA-2 历史只增不改）；
 *   ②生命周期流程类（project.new/project.open/project.saveAs/package.-
 *     export）——经 ILifecycleFlowLauncher 端口触发向导（对话框宿主面归
 *     ui——D-WF-6；流程编排核见 Lifecycle.hpp 既有各 Flow）；
 *   ③会话动作类（draft.save/scheme.switch/view.displayMode/view.resetHome/
 *     view.resetZero）——经 IWorkflowSessionPort 端口触发会话动作，**零
 *     修订**（KIN-06/ARCH §7.7"复位关节至 Home/Zero 同属会话姿态"）；
 *   ④碰撞检查（analysis.collisionCheck）——经 ICollisionCheckPort 端口提
 *     交会话级检查任务（P-WF-3 最小语义：结果仅会话呈现，不产生正式证据、
 *     不归档 envelope、不进入报告正式章节——本文件以"该分支只触达碰撞
 *     端口"的结构性分流保证，测试钉扎）；
 *   ⑤报告导出（report.export）——编排 reporting::IReportExportService 公
 *     共契约（白名单八边之 reporting 边产品面首次 include 消费；导出编排
 *     实现归 reporting 零重实现——PA-1），导出参数经对话框端口收集（宿主
 *     面 D-WF-6），预览宿主（P-UI-9）缺位如实呈现（previewHostAvailable
 *     恒 false，不虚构预览通道）。
 *
 * 为什么编排核返回自有值 WorkflowCommandOutcome 而不是 ui::CommandOutcome：
 * ui::CommandOutcome 定义于 ICommandRegistry.hpp，该头携带 QKeySequence
 * （Qt Gui 类型）；本单元计算库零 Qt（R-3/NFR-MNT-01——include/**＋src/**
 * 零 Qt 包含，ird_gates 红线）。WorkflowCommandOutcome 与 ui::CommandOutcome
 * 同构（accepted＋messageKey），由插件侧（plugin/ 目录——Qt 允许面）的
 * 适配器折叠为 ui::CommandOutcome（R-3 红线的文件域隔离，modeling/
 * requirements 命令目录同款先例）。同因，§10.2 Draft 中
 * IWorkflowCommandContributor（返回 vector<ui::CommandDescriptor>）落位于
 * 插件目录而非本头——偏差登记见单元卡 §10.2 v1.3（DTB §5.4）。
 *
 * 错误语义（§10.3 接口属性表"错误语义"行）：
 *   - 调用方错误 fail-fast：未知命令 id（词表外——静默返回会把用户操作
 *     丢进黑洞）、端口成员空指针（装配违约——L5 必须在装配期接齐端口），
 *     抛 WorkflowError；
 *   - 环境/对端错误：端口结果值透传（stableCode 对端状态词零加工——
 *     D-WF-7 零归码；R1 零新增 WF- 稳定码），accepted=false 由宿主呈现；
 *   - 取消非错误：报告导出对话框取消/导出取消＝Canceled 态（UX-03）。
 *
 * 线程安全：runWorkflowCommand 本身无共享可变状态（并发安全）；端口实现
 * 的线程约束由其自身契约声明（宿主面端口按 ui 线程纪律——命令处理器在
 * UI 线程启动）。
 */

#ifndef SDURWS_IRD_WORKFLOW_COMMANDS_HPP
#define SDURWS_IRD_WORKFLOW_COMMANDS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>       // core::ProjectId——报告导出请求的项目身份（强类型）
#include <sdurws/ird/reporting/Export.hpp>    // reporting::ReportExportRequest/ExportDestination/
                                              //   IReportExportService——报告导出编排的对端公共契约
                                              //   （白名单八边之 reporting 边产品面首次 include 消费）
#include <sdurws/ird/reporting/Identity.hpp>  // reporting::ReportId——报告身份（RPT-3 tag 类型）
#include <sdurws/ird/workflow/Types.hpp>      // WorkflowError——调用方契约违约异常（fail-fast）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 命令 id 词表（ui.md §7.1 最小命令集冻结表同串——SA-12 词表唯一权威归
// ui，本组常量为 workflow 侧登记位；两卡增量同步义务：ui 冻结表修订时
// 本组常量与描述符行同批修订。命令 id 是全局唯一身份（点分段＋段字符
// [A-Za-z0-9-]——ui §7.2 第 1 步句法），与 project commandType（无点
// 词形）是两套命名空间，不混用）
// =====================================================================

/// 新建项目（最小清单——PM-01 三步向导入口）。
inline constexpr const char* kCmdProjectNew = "project.new";
/// 打开项目（最小清单——PM-02 五步协议入口）。
inline constexpr const char* kCmdProjectOpen = "project.open";
/// 保存草稿（最小清单——草稿写轨，非修订；归 ui DraftController 的
/// 存储语义，workflow 命令编排经会话端口触发）。
inline constexpr const char* kCmdDraftSave = "draft.save";
/// 应用修改（最小清单——域信封组装→①命令端口提交，产生修订）。
inline constexpr const char* kCmdDraftApply = "draft.apply";
/// 撤销（最小清单——逆命令经①端口提交，产生新修订〔PA-2/AT-29〕）。
inline constexpr const char* kCmdProjectUndo = "project.undo";
/// 重做（最小清单——重放被撤销命令原始载荷，产生新修订）。
inline constexpr const char* kCmdProjectRedo = "project.redo";
/// 切换方案（最小清单——PM-12 会话选择，零写入零修订〔WF-VER-212〕）。
inline constexpr const char* kCmdSchemeSwitch = "scheme.switch";
/// 另存为（最小清单——PM-05 复制＋换新 projectId＋按打开协议进入）。
inline constexpr const char* kCmdProjectSaveAs = "project.saveAs";
/// 包导出（最小清单——PM-05 后台进度可取消、取消清理临时区）。
inline constexpr const char* kCmdPackageExport = "package.export";
/// 报告导出（最小清单——RPT-02 编排 reporting 导出服务；只读会话可用）。
inline constexpr const char* kCmdReportExport = "report.export";
/// 运行碰撞检查（工业高频 M-13——P-WF-3 会话级最小语义，O-26 裁决前）。
inline constexpr const char* kCmdCollisionCheck = "analysis.collisionCheck";
/// 切换显示模式（工业高频——渲染分组/线框/透明循环；视图会话态）。
inline constexpr const char* kCmdViewDisplayMode = "view.displayMode";
/// 复位关节至 Home（工业高频——KIN-06 会话姿态，不产生修订）。
inline constexpr const char* kCmdViewResetHome = "view.resetHome";
/// 复位关节至 Zero（工业高频——KIN-06 会话姿态，不产生修订）。
inline constexpr const char* kCmdViewResetZero = "view.resetZero";

/**
 * @brief workflow 贡献的命令 id 全集（14 条——§8.3 最小集 10＋工业高频
 *        4 个 id：碰撞检查/显示模式/Home/Zero 各一条）。
 *
 * 用途：贡献清单（插件侧 workflowCommandDescriptors）与编排核分流表的
 * 共同数据源（NFR-MNT-03 单点）；词表封闭性由测试逐值钉住——新增命令
 * 必须同步扩充本函数与 kindOfCommand 分流表（编译期无强制，运行期测试
 * 守护——两处不一致即用例失败）。
 *
 * @return 14 条 id（行序＝ui.md §7.1 冻结表行序——描述符登记序与面板
 *         registrationOrder 的稳定排序锚，NFR-COR-02；静态词表，调用方
 *         不取得所有权，并发只读安全）
 */
const std::vector<std::string>& workflowCommandIds();

// =====================================================================
// 执行分流词表（§8.3"执行回调分流"行的编排面语义身份——会话态枚举，
// 不持久化，§10.3"零新增持久化枚举"边界内）
// =====================================================================

/**
 * @brief 命令执行分流五类（§8.3 分流行的词化）。
 *
 * 类别与端口一一对应（见文件头注释①~⑤）；WF-VER-222/223 契约的分流
 * 断言轴（修订类必须经修订端口、会话类必须零修订端口触达）。
 */
enum class WorkflowCommandKind : std::uint8_t {
    Revision,      ///< 修订类（draft.apply/project.undo/project.redo——经①端口）
    LifecycleFlow, ///< 生命周期流程类（新建/打开/另存为/包导出——触发向导）
    SessionAction, ///< 会话动作类（保存草稿/切换方案/显示模式/复位 Home·Zero——零修订）
    CollisionCheck,///< 碰撞检查（analysis.collisionCheck——P-WF-3 会话级最小语义）
    ReportExport,  ///< 报告导出（report.export——编排 reporting 导出服务）
};

/**
 * @brief 命令 id → 分流类别（§8.3 分流表的唯一判定点——NFR-MNT-03）。
 *
 * @param commandId [in] 命令 id（workflowCommandIds() 词表内值）
 * @return 分流类别
 * @throws WorkflowError 词表外 id（调用方/对端契约违约——fail-fast：
 *         未登记命令的提交不应到达本编排核；静默返回会把用户操作丢进
 *         黑洞，ui 注册表侧的 UI-CMD-UNKNOWN 是第一道防线，本判是第二道）
 */
WorkflowCommandKind kindOfCommand(const std::string& commandId);

// =====================================================================
// 端口接缝（全部为纯接口——实现归 L5 装配层；v0.5 IDomainInitSubmitter
// 先例的编排依赖倒置形态：编排核零宿主面知识，L5 桥接 ui 对话框/project
// 命令面/execution 任务面。测试以脚本化桩直调编排核）
// =====================================================================

/**
 * @brief 会话动作结果值（会话端口三态折叠的载体——成功/失败/取消不区分
 *        枚举，accepted＋文案键二字段已足：取消＝accepted=false＋取消键，
 *        失败＝accepted=false＋原因键——呈现归宿主，UX-02 键值分工）。
 */
struct SessionActionResult {
    /// 动作是否已受理并执行（false＝用户取消或环境拒绝——reasonKey 承载原因）。
    bool accepted = false;
    /// 呈现文案键（ui::TextKey 词形——空串＝成功且无特别文案；值归 ui
    /// 文案资源——本单元只产键不产值，UX-02 工程用语键半区）。
    std::string reasonKey;
};

/**
 * @brief 生命周期流程启动端口（②类命令的触发面——L5 桥接宿主向导对话
 *        框与既有各 Flow 编排核〔Lifecycle.hpp〕；D-WF-6：对话框控件归
 *        ui、流程状态机与用户决策回传归本单元）。
 *
 * 方法语义＝"触发即返回"：向导内部的多步交互（输入收集/取消/失败呈现）
 * 由 L5 适配器驱动的对话框与 Flow 编排核承担，本端口不携带流程结果——
 * 流程完成态经会话事件（⑤端口）回到投影面，命令处理器不阻塞等待
 * （UI 线程禁止长计算——ICommandRegistry 处理器纪律）。
 */
class ILifecycleFlowLauncher {
public:
    virtual ~ILifecycleFlowLauncher() = default;

    /// 触发新建项目三步向导（PM-01——取消/失败不留半成品的流程核见
    /// NewProjectWizardFlow::commit）。
    virtual void launchNewProjectWizard() = 0;
    /// 触发打开项目对话框入口（PM-02——三来源之 Dialog；真实打开协议
    /// 编排核见 OpenProjectFlow::run）。
    virtual void launchOpenProjectDialog() = 0;
    /// 触发另存为向导（PM-05——流程核见 SaveAsFlow::run）。
    virtual void launchSaveAsWizard() = 0;
    /// 触发包导出向导（PM-05——后台进度可取消；流程核见
    /// PackageExportFlow::run）。
    virtual void launchPackageExportWizard() = 0;
};

/**
 * @brief 会话动作端口（③类命令的执行面——L5 桥接 ui 会话态：分支切换
 *        选择/显示模式/会话姿态〔Home·Zero〕/草稿保存落盘链路）。
 *
 * ★ KIN-06 结构性边界（acceptance 2）：本端口五动作**全部不产生修订**
 * ——复位关节至 Home/Zero 只改会话姿态（ARCH §7.7"同属会话姿态"）、
 * 切换方案只改会话选择（PM-12 零写入）、保存草稿走草稿写轨（修订只在
 * 应用时经①端口产生）、显示模式是纯呈现偏好。修订的唯一入口是
 * IRevisionCommandPort（①类命令专用）——两端口分离即"复位关节零修订"
 * 的结构性保证（不依赖实现方自觉，测试 WF-VER-223 双面钉扎）。
 */
class IWorkflowSessionPort {
public:
    virtual ~IWorkflowSessionPort() = default;

    /// 触发方案分支切换（PM-12——分支清单与候选验证的编排核见
    /// SchemeBranchSwitchFlow::run；零写入）。
    virtual SessionActionResult switchSchemeBranch() = 0;
    /// 循环切换显示模式（渲染分组→线框→透明→渲染分组——M-13 工业高频；
    /// 纯呈现偏好，不入项目）。
    virtual SessionActionResult cycleDisplayMode() = 0;
    /// 复位关节至 Home 姿态（KIN-06——会话姿态，零修订零失效）。
    virtual SessionActionResult resetJointsToHome() = 0;
    /// 复位关节至 Zero 姿态（KIN-06——会话姿态，零修订零失效）。
    virtual SessionActionResult resetJointsToZero() = 0;
    /// 保存全部未落盘草稿（草稿写轨——非修订；真实落盘链路归 ui
    /// DraftController〔PM-14 语义面〕，本端口是命令编排的触发面）。
    virtual SessionActionResult saveAllDrafts() = 0;
};

/**
 * @brief 修订命令结果值（①端口四态的 workflow 侧折叠承载——对端状态
 *        零加工透传，D-WF-7 零归码：R1 零新增 WF- 稳定码）。
 */
struct RevisionOutcome {
    /// 是否产生新修订（Committed——恰好一个；全部失败路径 false 且零修订，
    /// project §5.3.1 后置保证）。
    bool committed = false;
    /// 新修订身份（committed 时非空——rev- 规范词形透传；PA-2：撤销/重做
    /// 产生**新**修订，不复现/不改写历史旧修订）。
    std::optional<std::string> newRevisionId;
    /// 对端状态词透传（零加工：committed 时空串；否则为对端 Rejection/
    /// Aborted/Failed 的状态词或建议码词形——UX-03 字段齐备的机器半区，
    /// 呈现键由 reasonKey 承载）。
    std::string stableCode;
    /// 用户呈现文案键（空串＝成功无特别文案；失败时为可操作建议键——
    /// UX-02 键半区，值归 ui 文案资源）。
    std::string reasonKey;
};

/**
 * @brief 修订命令端口（①类命令的提交面——"产生修订的命令经①命令端口"
 *        〔§8.3 行三；ARCH §7.11〕的 workflow 侧视图；L5 桥接 project
 *        ProjectCommandService::submit／UndoRedoService::undo/redo——
 *        唯一写路径，编排核零直写）。
 *
 * 方法到对端的映射（L5 实现义务，契约测试以真实 store 桥验证）：
 *   - applyDraft＝域草稿信封组装→①端口 submit（应用修改——域信封组装
 *     归宿主/域编辑器链路，L5 组合后提交）；
 *   - undo＝UndoRedoService.undo（逆命令提交——产生恰好一个新修订）；
 *   - redo＝UndoRedoService.redo（重放原始载荷——产生新修订，非复现旧
 *     修订身份）。
 */
class IRevisionCommandPort {
public:
    virtual ~IRevisionCommandPort() = default;

    /// 应用修改（draft.apply——草稿→修订；无草稿/只读/拒绝＝committed=false）。
    virtual RevisionOutcome applyDraft() = 0;
    /// 撤销（project.undo——PM-18/AT-29；无可撤销修订＝committed=false
    /// ＋稳定提示，不崩溃）。
    virtual RevisionOutcome undo() = 0;
    /// 重做（project.redo——同上；redo 栈空/被新命令清空＝committed=false）。
    virtual RevisionOutcome redo() = 0;
};

/**
 * @brief 碰撞检查结果值（P-WF-3 最小语义的结果承载——受理/未受理＋呈现键）。
 */
struct CollisionCheckOutcome {
    /// 任务是否已受理提交（false＝无在审模型/只读会话等会话前置不满足
    /// ——呈现键承载原因；提交后的进度/取消/结果呈现归宿主任务面）。
    bool accepted = false;
    /// 呈现文案键（空串＝已受理且无特别文案）。
    std::string reasonKey;
};

/**
 * @brief 碰撞检查端口（analysis.collisionCheck 的执行面——P-WF-3 三方
 *        契约裁决前的**会话级最小语义**接缝）。
 *
 * ★ 最小语义边界（单元卡 §8.4，acceptance 3）：本端口的实现义务＝提交
 *   mode=Preview/Quick 的会话级检查任务（L5 桥接③④端口评估面/execution
 *   任务面），结果仅会话呈现——**不产生正式证据、不归档 envelope、不进
 *   入报告正式章节、不写项目**。编排核侧的结构性保证：碰撞分支只触达
 *   本端口（修订/报告/会话端口零调用——测试以触达计数钉扎）；O-26 三方
 *   契约冻结后若语义扩大，经本端口与单元卡 §8.4 增量同步（P-WF-3 裁决
 *   后需同步面），编排核分流词表不变。
 */
class ICollisionCheckPort {
public:
    virtual ~ICollisionCheckPort() = default;

    /// 提交会话级碰撞检查任务（Preview/Quick 模式选择归 L5 实现编排核
    /// 零模式数值——P-WF-3 裁决前不发明正式评估语义）。
    virtual CollisionCheckOutcome startSessionCheck() = 0;
};

/**
 * @brief 报告导出参数（用户在导出对话框收集的请求半区——reporting::
 *        ReportExportRequest 五字段的用户可决子集；项目/报告身份由 L5
 *        从会话上下文取数，格式/目标/证据包勾选由用户决定）。
 *
 * 等价比较（==/!=）供测试黄金断言与编排核请求组装复核。
 */
struct ReportExportInputs {
    core::ProjectId project;   ///< 归属项目（汇集座编址/只读检查键——会话当前项目）
    reporting::ReportId reportId;///< 已构建报告身份（值经 IReportResolver 解析——前置）
    /// 渲染格式集（≥1 且必含 Html——RPT-02 首版主格式约束的入口面；
    /// 重复格式＝调用方违约，由 reporting 服务请求校验拒绝）。
    std::vector<reporting::ReportRenderFormat> formats;
    reporting::ExportDestination destination;///< 导出目标（项目内归档/项目外目录）
    bool withEvidenceBundle = false;         ///< 同时导出证据包（RPT-03——T12 阶段默认 false）

    bool operator==(const ReportExportInputs& o) const
    {
        return project == o.project && reportId == o.reportId
            && formats == o.formats && destination.kind == o.destination.kind
            && destination.externalPath == o.destination.externalPath
            && destination.replace == o.destination.replace
            && withEvidenceBundle == o.withEvidenceBundle;
    }
    bool operator!=(const ReportExportInputs& o) const { return !(*this == o); }
};

/**
 * @brief 导出参数收集结果（对话框关闭的两态——确认带参数/取消不带）。
 */
struct ReportExportDialogResult {
    /// 用户是否确认导出（false＝取消——UX-03 正常取消非错误，编排核
    /// 转 Canceled 且**零触达**导出服务）。
    bool confirmed = false;
    /// 用户确认的导出参数（confirmed=false 时无意义——默认构造值）。
    ReportExportInputs inputs;
};

/**
 * @brief 导出参数收集端口（⑤类命令的宿主对话框接缝——D-WF-6：导出目
 *        标/格式/勾选的对话框控件归 ui 宿主面，编排核只消费收集结果）。
 *
 * 项目/报告身份的取数也在此折叠（L5 从会话上下文现取当前项目与报告
 * ——编排核零 store/会话知识，与 ITitleFactPort"五路来源收集"同款纪律）。
 */
class IReportExportDialogPort {
public:
    virtual ~IReportExportDialogPort() = default;

    /**
     * @brief 弹出导出参数收集对话框（模态交互——UI 线程调用）。
     * @return 收集结果（confirmed=false＝用户取消——非错误）
     */
    virtual ReportExportDialogResult collectExportInputs() = 0;
};

/**
 * @brief 报告导出命令结果值（⑤类命令编排的终态——P-WF-4 预览缺位的
 *        如实呈现位在此）。
 */
struct ReportExportCommandOutcome {
    /// 编排终态（Completed=导出成功；Canceled=用户取消〔对话框或导出
    /// 检查点〕——非错误；Failed=服务报错——stableCode 透传）。
    enum class Status : std::uint8_t { Completed = 0, Canceled = 1, Failed = 2 };

    Status status = Status::Canceled;///< 编排终态（见枚举注）
    /// 预览宿主可用位——**恒 false（P-WF-4/P-UI-9）**：报告预览宿主接口
    /// 详设未产出，导出命令后的预览呈现通道缺位；本位即"缺位如实呈现"
    /// 的机器观测面（宿主据此展示"预览不可用"而非虚构预览），裁决后随
    /// reporting/ui 侧产出接线翻转。
    bool previewHostAvailable = false;
    /// 对端稳定错误词透传（Failed 时为 reporting::token(ReportErrorCode)
    /// 词形——"reporting/..." 零加工；其余态空串。D-WF-7 零归码）。
    std::string stableCode;
    /// 用户呈现文案键（成功/取消/失败三态各有键——空串＝宿主按默认成功
    /// 文案呈现；UX-02 键半区，值归 ui 文案资源）。
    std::string reasonKey;
};

/**
 * @brief 报告导出命令编排核（⑤类命令的执行编排——"报告导出编排
 *        reporting 服务"〔§8.3 行三；RPT-02〕的实现点）。
 *
 * 执行序（三步，任何一步取消/失败即短路返回）：
 *   1. 参数收集：dialog.collectExportInputs()——用户取消→Canceled
 *      （导出服务**零触达**——测试钉扎）；
 *   2. 请求组装：ReportExportInputs→reporting::ReportExportRequest 逐
 *      字段搬运（零加工零补造——请求合法性校验归 reporting 服务请求
 *      步〔Usage 轨〕，本核不重复实现）；
 *   3. 服务编排：service.exportReport(request)——成功→Completed；
 *      失败→Failed＋token(code) 透传；取消（全空结果）→Canceled。
 *
 * @param dialog  [in] 导出参数收集端口（非 owning——调用方保证存活期）
 * @param service [in] reporting 导出服务（非 owning——白名单边公共契约）
 * @return 编排终态（previewHostAvailable 恒 false——P-WF-4 如实呈现）
 *
 * @throws WorkflowError service 对 ReportExportInputs::withEvidenceBundle
 *         等请求面的 Usage 违约以 ReportError(Usage) 异常抛出时**原样透
 *         传**（调用方错误 fail-fast——不折叠为 Failed：装配期契约违约
 *         必须修复调用方，不属于环境错误）；环境错误（ExportFailed/
 *         DiskFull/ArchiveConflict 等）折叠为 Failed＋stableCode。
 */
ReportExportCommandOutcome runReportExportCommand(IReportExportDialogPort& dialog,
                                                  reporting::IReportExportService& service);

// =====================================================================
// 命令编排核（分流表＋端口组合——插件侧处理器与测试的共同执行面）
// =====================================================================

/**
 * @brief 命令编排核的端口组合（装配期一次性接齐——L5 装配层给出；指针
 *        非 owning，存活期由装配层保证覆盖命令处理期）。
 *
 * 全成员在 runWorkflowCommand 入口统一判空（按命令实际分流到的端口判
 * ——未分流到的端口允许空：如纯会话场景可不给 reportExport）。判空失
 * 败＝装配违约 WorkflowError fail-fast（§10.3 错误语义行）。
 */
struct WorkflowCommandPorts {
    ILifecycleFlowLauncher* lifecycle = nullptr;   ///< 生命周期流程启动（②类需要）
    IWorkflowSessionPort* session = nullptr;       ///< 会话动作（③类需要）
    IRevisionCommandPort* revisions = nullptr;     ///< 修订命令（①类需要）
    ICollisionCheckPort* collision = nullptr;      ///< 碰撞检查（④类需要）
    IReportExportDialogPort* reportDialog = nullptr;///< 导出参数收集（⑤类需要）
    reporting::IReportExportService* reportExport = nullptr;///< 导出服务（⑤类需要）
};

/**
 * @brief 命令编排结果值（ui::CommandOutcome 的零 Qt 同构承载——插件侧
 *        适配器逐字段折叠为 ui::CommandOutcome 后交注册表；本单元计算
 *        库零 Qt 红线下的替代形状，R-3 文件域隔离先例见文件头注）。
 */
struct WorkflowCommandOutcome {
    /// 命令是否已执行（修订类＝是否产生新修订；会话/流程类＝动作是否
    /// 受理；取消＝false——与 ui::CommandOutcome.accepted 语义对齐）。
    bool accepted = false;
    /// 呈现文案键（ui::TextKey 词形——折叠至 ui::CommandOutcome.messageKey；
    /// 空串＝成功无特别文案；UX-02 键半区，值归 ui 文案资源）。
    std::string messageKey;
};

/**
 * @brief 命令执行编排核（§8.3"执行回调分流"的唯一实现点——插件侧命令
 *        处理器统一调本函数，分流逻辑单点可测）。
 *
 * 分流规则（kindOfCommand 判类后逐类执行——文件头①~⑤）：
 *   - Revision：按 id 分派 applyDraft/undo/redo，RevisionOutcome 折叠为
 *     WorkflowCommandOutcome（committed→accepted；reasonKey→messageKey）；
 *   - LifecycleFlow：按 id 分派 launcher 四方法（触发即返回——流程完成
 *     态经会话事件回投影面，accepted=true）；
 *   - SessionAction：按 id 分派会话端口五动作，结果折叠（accepted＋
 *     reasonKey）；**零修订端口触达**（KIN-06 结构性保证）；
 *   - CollisionCheck：startSessionCheck——**只触达碰撞端口**（P-WF-3
 *     最小语义：修订/报告端口零调用）；
 *   - ReportExport：runReportExportCommand（Completed→accepted）。
 *
 * @param commandId [in] 命令 id（workflowCommandIds() 词表内值）
 * @param ports     [in] 端口组合（按命令分流实际所需成员判空——装配违约
 *                  fail-fast）
 * @return 编排结果（命令参数面：阶段 A 最小集全部无参——ui §7.1
 *         ParameterSchema 恒空，故本核不接收参数表；带参命令随首个带参
 *         命令的装配任务增量冻结）
 *
 * @throws WorkflowError 词表外 id；所需端口成员为空（装配违约）
 */
WorkflowCommandOutcome runWorkflowCommand(const std::string& commandId,
                                          const WorkflowCommandPorts& ports);

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_COMMANDS_HPP
