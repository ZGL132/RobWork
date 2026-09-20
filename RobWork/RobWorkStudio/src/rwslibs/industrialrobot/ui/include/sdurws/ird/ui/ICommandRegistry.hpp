/**
 * @file   ICommandRegistry.hpp
 * @brief  命令注册表（CommandRegistry）——工作台命令的唯一登记/查询/提交
 *         入口（SA-16：无第二套命令入口；命令面板/菜单/快捷键共用）。
 *
 * 设计依据：
 *   - units/ui.md §7.1（命令项模型：CommandDescriptor 逐字段）、§7.2（注册
 *     协议与冲突处理：id 句法/owner 白名单/重复拒绝＋UI-CMD-DUPLICATE/未知
 *     命令拒绝＋UI-CMD-UNKNOWN/注册序＝白名单序）、§7.4（命令面板：只读
 *     投影/模糊搜索/结果上限 50）、§7.5（可见与可执行谓词：UiContextSnapshot/
 *     使能态≠业务判定）、§7.6（只读条件：readOnlyAllowed=false 且不可写→
 *     三处一致禁用＋UI-CMD-NOT-EXECUTABLE 双保险）、§10.3（接口契约表）；
 *   - O-31 裁决（DTB §4，2026-09-19）：§10.3 原文的
 *     CommandOutcome.revisionResult（project::CommandResult，C-4）在本头以
 *     ui 值投影 CommandResultProjection 承载（UiProjections.hpp），提交协作
 *     经 IUiCommandGateway 端口（UiPorts.hpp）——产品面零对 project 的链接
 *     或 include（NoCrossUnitInclude_O31_UI_BUILD 守卫常驻；本头即该处置的
 *     代码落点，登记 ui.md §16.7 v0.8）；
 *   - 需求 UX-13（命令面板/快捷键——机制归 ui）、PM-14（用户级设置——
 *     面板近期使用历史持久化）、NFR-MNT-03（词表唯一权威）、NFR-PERF-01
 *     （谓词 <1 ms——只消费快照）。
 *
 * 背景说明（为什么命令注册表在 ui 而命令执行在 project）：命令的**登记/
 * 呈现/路由**是界面聚合事实（菜单、面板、快捷键三处共用一份注册表——
 * SA-16 唯一入口），而命令的**执行**（修订产生）唯一写路径经 project
 * （§7.7 时序）。注册表把两者分开：submit() 只做前置校验（未知/不可执行/
 * 只读）后派发给注册时绑定的处理器；产生修订的命令由处理器经命令网关端口
 * 提交（§7.7 ②③），注册表不触碰任何领域对象。
 *
 * 线程模型（§10.3 契约表"线程"行）：全部方法 UI 线程调用（§3.4 M-1）；
 * 处理器在 UI 线程启动，耗时工作由处理器转交后台/命令服务——处理器本身
 * 禁止长计算。非线程安全：仅 UI 线程访问。
 */

#ifndef SDURWS_IRD_UI_ICOMMANDREGISTRY_HPP
#define SDURWS_IRD_UI_ICOMMANDREGISTRY_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <QKeySequence>

#include <sdurws/ird/diagnostics/Catalog.hpp>    // diagnostics::IDiagnosticSink（表内登记边——用户级码入目录）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>  // diagnostics::CodeDescriptor/IDiagnosticRegistry（码表查询）
#include <sdurws/ird/diagnostics/Factory.hpp>    // diagnostics::IDiagnosticFactory/IDevLogSink（create 唯一入口/Dev 出线）
#include <sdurws/ird/ui/IWorkbenchShell.hpp>     // ui::ShellCommandScope（CommandScope 别名的被指类型——UI-T03 冻结词表，NFR-MNT-03）
#include <sdurws/ird/ui/UiProjections.hpp>       // ui::CommandResultProjection（C-4 投影——O-31 承载）
#include <sdurws/ird/ui/UiTypes.hpp>             // ui::CommandId/TextKey/MenuPath（§7.1 词表）

namespace sdurws {
namespace ird {
namespace ui {

class ICommandRegistry;  // 前置声明（工厂返回类型）

// =====================================================================
// 命令词表（§7.1 命令项模型）
// =====================================================================

/**
 * @brief 命令作用域（§7.1 CommandScope 词表：会话态/项目作用域/视图作用域）。
 *
 * UI-T03 已冻结同一词表为 ShellCommandScope（IWorkbenchShell.hpp——"UI-T06
 * 直接复用不重定义"，NFR-MNT-03）；本别名是**同一类型的第二名字**（true
 * alias，非新枚举），§7.1 行文的 CommandScope 由此落地。
 */
using CommandScope = ShellCommandScope;

/**
 * @brief 命令分类（§7.1 CommandCategory 词表——面板/菜单的二级分组轴）。
 *
 * 枚举序＝§7.1 原文行序（交付后只允许表尾追加并升单元卡修订——分类值
 * 进 CommandView 投影，重排破坏消费者稳定呈现）。
 */
enum class CommandCategory : std::uint8_t {
    Project,   ///< 项目（新建/打开/另存等）
    Edit,      ///< 编辑（撤销/重做等）
    View,      ///< 视图（显示模式/复位等）
    Stage,     ///< 阶段（方案切换等）
    Analysis,  ///< 分析（碰撞检查等）
    Report,    ///< 报告（导出等）
    Workbench, ///< 工作台（命令面板/关闭项目/恢复布局等）
    Help,      ///< 帮助（关于/手册等）
};

/**
 * @brief 命令参数（§10.3 submit 的可变参承载——无参命令传空表）。
 *
 * 形态说明：§7.1 的 ParameterSchema（参数模式）在阶段 A 的全部最小集命令
 * 上为空 schema（"无参命令为空 schema"原文）——参数的 schema 校验属业务
 * 命令处理器职责（随插件装配的任务增量冻结），注册表只做**不透明搬运**
 * （key/value 直传处理器，不解析不校验——与 CommandEnvelopeProjection.
 * payloadCanonical 同款边界）。
 */
struct CommandParameter {
    /// 参数键（处理器声明的词形；不透明）。
    std::string key;
    /// 参数值（字符串形态；不透明）。
    std::string value;
};

/**
 * @brief 参数模式描述（§7.1："可选参数模式（无参命令为空 schema）"）。
 *
 * 阶段 A 恒为空串（最小命令集全部无参）；非空形态（参数键词表/类型约束
 * 的承载格式）随首个带参命令的装配任务增量冻结（v0.4 §10 引导注机制）。
 */
using ParameterSchema = std::string;

/**
 * @brief 命令描述符（§7.1 原文——注册时一次性给出，构造后不可变）。
 *
 * 注册要素对照（§7.1"注册要素对照"表）：命令 ID＝id；所有者＝ownerUnit；
 * 文案键＝titleKey/keywordKeys；图标＝iconKey；默认快捷键＝defaultShortcut；
 * 可见/可执行条件＝注册时的谓词（registerCommandWithPredicates——§7.5）；
 * 只读条件＝readOnlyAllowed；参数模式＝params；菜单与面板分组＝menuPath＋
 * category；注册顺序＝白名单序（§7.2）。
 */
struct CommandDescriptor {
    /// 命令 id（点分小写，全局唯一——§7.2 第 1 步句法校验）。
    CommandId id;
    /// 注册者（ui 内部设施名或白名单插件 id——§7.2 第 1 步白名单校验）。
    std::string ownerUnit;
    /// 标题文案键（"cmd.<id>.title"——§3.5 键约定；值归 UI-T09 UiText）。
    TextKey titleKey;
    /// 模糊搜索关键字文案键（"cmd.<id>.kw.<n>"——UX-13；可空）。
    std::vector<TextKey> keywordKeys;
    /// 分类（面板/菜单二级分组——§7.1 CommandCategory）。
    CommandCategory category = CommandCategory::Workbench;
    /// 图标资源键（可空——阶段 A 不消费）。
    std::string iconKey;
    /// 作用域（会话态/项目/视图——§7.1 CommandScope）。
    CommandScope scope = CommandScope::Session;
    /// 只读会话是否可用（§7.6：false 且上下文不可写→禁用＋提交拒绝）。
    bool readOnlyAllowed = true;
    /// 是否可绑定快捷键（false＝面板可达但快捷键表拒绝——§7.3）。
    bool bindable = true;
    /// 默认绑定（nullopt＝默认无键——经命令面板可达，UX-13 兜底；§7.3
    /// "未列默认键的命令 bindable=true 但无默认绑定"）。
    std::optional<QKeySequence> defaultShortcut;
    /// 菜单与面板分组路径（"文件/新建"——MenuPath 词表）。
    MenuPath menuPath;
    /// 参数模式（阶段 A 恒空——见 ParameterSchema 注释）。
    ParameterSchema params;
};

// =====================================================================
// 可用性谓词（§7.5——界面使能态，不是业务判定）
// =====================================================================

/**
 * @brief 命令可用性求值的上下文快照（§7.5 UiContextSnapshot 的 UI-T06
 *        阶段 A 冻结子集）。
 *
 * 形态登记（ui.md §16.7 v0.8 增量）：§7.5 原文的 UndoRedoStatus（C-4 对端
 * ——project 类型）/DraftingSummary（§8）/TaskSummary（§9.4）/
 * StageReadinessSnapshot（§6.5）各轴随其首消费任务增量冻结（O-31：对端
 * 类型一律投影承载；撤销轴 UndoRedoStatusProjection 归撤销入口接线任务）。
 * 阶段 A 载荷＝壳门控事实（§7.5/§7.6 求值的最小输入）：
 *   - session/writable/hasActiveProject＝无项目/只读门控主轴（PM-10/PM-07）；
 *   - drafts＝草稿存在性投影（§7.5 示例"draft.apply ⇔ writable ∧
 *     drafts.hasApplicable"的阶段 A 输入——present 位）。
 */
struct UiContextSnapshot {
    /// 会话态（阶段 A 投影：true＝已绑定已打开项目——PM-10 门控主轴）。
    bool hasActiveProject = false;
    /// 可写性（false＝只读会话——§7.6 只读条件输入；PM-07）。
    bool writable = false;
    /// 草稿存在性投影（§7.5 可执行谓词示例的草稿轴输入——UI-T03 投影复用）。
    DraftPresenceProjection drafts{};
};

/**
 * @brief 可见性谓词（§7.5 原文签名——函数指针形态；注册时绑定）。
 *
 * 纪律（§7.5 原文）：求值仅消费 UiContextSnapshot 快照（UI 线程），禁止
 * 谓词内发起端口查询/IO（保证 <1 ms——NFR-PERF-01）。返回 false 的命令
 * 不进菜单/面板（可见性塌缩）；项目命令的 NoProject 态按 PM-10 采用
 * "禁用＋说明"保留发现性（§7.4）——默认谓词恒可见。
 */
using VisibilityPredicate = bool (*)(const UiContextSnapshot&);

/**
 * @brief 禁用原因（§7.5 EnablementPredicate 的返回载荷）。
 *
 * 形态＝禁用原因文案键（TextKey——UX-02 键体系；如 "reason.no-project"/
 * "reason.readonly"）。值解析归 UiText（UI-T09）；键即契约（UI-T03
 * ShellCommandAvailability.reasonKey 同案）。
 */
using DisableReason = TextKey;

/**
 * @brief 可执行（使能）谓词（§7.5 原文签名——返回 nullopt＝可执行，
 *        否则携带禁用原因键）。
 *
 * std::function 形态（§7.5 原文）以支持闭包捕获（如按命令 id 特化的
 * 使能条件）。纪律同 VisibilityPredicate：只消费快照、零 IO。
 */
using EnablementPredicate =
    std::function<std::optional<DisableReason>(const UiContextSnapshot&)>;

// =====================================================================
// 只读投影与结果值（§7.4 面板快照／§10.3 查询/可用性/提交结果）
// =====================================================================

/**
 * @brief 命令只读投影行（§7.4："面板是 CommandRegistry 的只读投影"的行
 *        载体——query()/paletteSnapshot() 的返回元素）。
 *
 * 不携带执行逻辑（§7.4 原文"不复制执行逻辑"）；菜单/面板/快捷键三处
 * 从同一行取呈现与使能态（§7.6"三处一致禁用"的单一来源）。
 */
struct CommandView {
    /// 命令 id。
    CommandId id;
    /// 标题文案键（§3.5 键；值解析归 UiText/UI-T09）。
    TextKey titleKey;
    /// 标题过渡中文（UI-T09 前的呈现承载——commandTransitionalTitle 解析，
    /// 键值分离过渡期与 statusWordTransitionalLabel 同案）。
    std::string title;
    /// 分类（面板分组轴）。
    CommandCategory category = CommandCategory::Workbench;
    /// 菜单分组路径（"文件/新建"）。
    MenuPath menuPath;
    /// 关键字文案键（模糊搜索命中的呈现高亮输入；可空）。
    std::vector<TextKey> keywordKeys;
    /// 当前是否可见（可见谓词求值结果——§7.5）。
    bool visible = true;
    /// 当前是否可执行（使能谓词求值结果——§7.5；真实校验仍在目标服务）。
    bool enabled = true;
    /// 禁用原因文案键（enabled=false 时非空——"禁用＋说明"，§7.4）。
    DisableReason disableReasonKey;
    /// 只读阻断位（§7.6：readOnlyAllowed=false 且上下文不可写时 true——
    /// 呈现层可据此显示只读专属说明）。
    bool readOnlyBlocked = false;
    /// 是否可绑定快捷键（描述符 bindable 位投影——§7.3 快捷键表的前置校验
    /// 数据源；false＝快捷键表 NotBindable、面板仍可达）。
    bool bindable = true;
    /// 注册序（0 起，装配序——同分稳定排序锚，NFR-COR-02 界面延伸）。
    std::size_t registrationOrder = 0;
};

/**
 * @brief 命令查询过滤器（§10.3 query 的入参——只读投影的筛选面）。
 *
 * 阶段 A 最小面：分类过滤＋模糊词＋上限。fuzzy 语义与 paletteSnapshot
 * 一致（空＝不过滤）；limit=0 表示不限。
 */
struct CommandQuery {
    /// 模糊词（空＝全部；匹配规则同 §7.4）。
    std::string fuzzy;
    /// 分类过滤（nullopt＝不限）。
    std::optional<CommandCategory> category;
    /// 结果上限（0＝不限；超出截断——调用方以更细的 fuzzy 收窄）。
    std::size_t limit = 0;
};

/**
 * @brief 命令可用性快照（§10.3 availability 返回——{visible, enabled,
 *        disableReasonKey, readOnlyBlocked}）。
 *
 * UI-T06 增量登记（ui.md §16.7 v0.8）：§10.3 原文四字段之外增补
 * registered 位——未注册命令的显式观测面（GlobalShortcutRegistry 的
 * UnknownCommand 前置校验与 UI-T03 ShellCommandAvailability.registered
 * 同语义；不可见但已注册的命令 registered 仍为 true——可见性与存在性
 * 正交，§7.5 可见谓词塌缩不等于注销）。
 */
struct CommandAvailability {
    /// 是否已注册（存在性——与可见/使能正交；未注册命令其余字段无意义）。
    bool registered = false;
    /// 是否可见（可见谓词求值）。
    bool visible = false;
    /// 是否可执行（使能谓词求值；未注册命令恒 false）。
    bool enabled = false;
    /// 禁用原因文案键（enabled=false 时非空）。
    DisableReason disableReasonKey;
    /// 只读阻断位（§7.6 只读条件的机器观测面）。
    bool readOnlyBlocked = false;
};

/**
 * @brief 命令注册结果（§10.3 原文词表：Ok | DuplicateId | InvalidDescriptor
 *        | OwnerNotWhitelisted）。
 *
 * 拒绝一律**不覆盖不静默**（§7.2：重复注册拒绝＋诊断 UI-CMD-DUPLICATE；
 * 装配期失败进 AssemblyReport——§11.3，报告面归装配任务）。
 */
enum class RegistrationResult : std::uint8_t {
    Ok,                 ///< 注册成功
    DuplicateId,        ///< 命令 id 重复（含不同 owner——§7.2 第 2 步）
    InvalidDescriptor,  ///< 描述符违约（id 句法/必填键空；含 seal 后注册的
                        ///  装配期违约安全轨——见 ICommandRegistry::seal）
    OwnerNotWhitelisted,///< owner 不在白名单（§7.2 第 1 步——SA-01 静态白名单）
};

/**
 * @brief 命令提交结果（§10.3 原文形态——O-31 裁决后的承载说明见
 *        revisionResult 字段注释）。
 */
struct CommandOutcome {
    /// 是否已派发执行（前置校验拒绝时 false——命令恰好执行一次或被拒绝，
    /// §10.3 后置条件行）。
    bool accepted = false;
    /// 结果消息文案键（可空——处理器可选给出；如会话命令的完成提示）。
    std::optional<TextKey> messageKey;
    /**
     * 修订结果投影（C-4——O-31 承载形态）。
     *
     * ui.md §10.3 原文本字段为 project::CommandResult（对端类型）；按 O-31
     * 裁决（2026-09-19，DTB §4；任务契约 UI-T06 acceptance 3）以 ui 值投影
     * CommandResultProjection 承载（UiProjections.hpp——字段语义逐条锚定
     * project.md §5.3.1，投影≠重定义），对端提交经 IUiCommandGateway 端口
     * （UiPorts.hpp，L5 适配）。命令语义零变化：唯一写路径经 project、
     * ui 零业务判定（§7.5/§7.7 原文照旧）。产生修订的命令处理器回填本
     * 字段；会话/视图命令无修订语义，保持 nullopt（§7.7"会话态命令"分支）。
     */
    std::optional<CommandResultProjection> revisionResult;
};

// =====================================================================
// 过渡文案解析（UI-T09 UiText 前的呈现承载——键冻结、值过渡）
// =====================================================================

/**
 * @brief 命令标题的过渡中文（UI-T09 UiText 文案资源落地前的过渡承载）。
 *
 * 与 statusWordTransitionalLabel/taskStateTransitionalLabel 同案（键值
 * 分离——键 "cmd.<id>.title" 冻结于描述符，中文值过渡期随实现表走；
 * UI-T09 后本函数退役，键不变）。未登记的键返回空串（调用方回退显示
 * id——不虚构标题）。
 *
 * @param titleKey [in] 标题文案键（"cmd.<id>.title"）
 * @return 过渡中文标题（如 "新建项目"；无登记→空串）
 */
std::string commandTransitionalTitle(const TextKey& titleKey);

/**
 * @brief 关键字键的过渡中文（模糊搜索的命中词源——同上过渡承载）。
 *
 * @param keywordKey [in] 关键字文案键（"cmd.<id>.kw.<n>"）
 * @return 过渡中文关键字（如 "新建"；无登记→空串）
 */
std::string commandTransitionalKeyword(const TextKey& keywordKey);

// =====================================================================
// 注入依赖与工厂（§10.1 createWorkbenchShell 同款门面惯例）
// =====================================================================

/**
 * @brief 命令注册表的装配依赖（create 注入——所有权在装配层，注册表只持
 *        共享引用）。
 *
 * 诊断语义（§7.2/§3.5/P-UI-10）：
 *   - UI-CMD-DUPLICATE（Dev）→ **不入目录**（IDiagnosticSink::append 拒绝
 *     Dev——§6.2），经 devLog 出线；devLog 为空时装配违约只剩返回值轨
 *     （注册表行为不受影响——日志可用性不参与正确性，§4.5 同纪律）；
 *   - UI-CMD-UNKNOWN/UI-CMD-NOT-EXECUTABLE（Warning/Info，用户级）→
 *     factory.create＋sink.append 入目录（§9.2 唯一创建入口；sourceUnit
 *     ="ui"——§4.2 词表）；factory 或 sink 为空＝无目录测试场景（须显式
 *     声明——ShellWiring 可空成员同款），拒绝行为不受影响（返回值轨恒在，
 *     诊断只是观测面）；
 *   - 码描述符与码值权威：§3.5 九码已随 UI-T03 登记为描述符供体
 *     （uiDiagnosticCodeDescriptors），注册动作归 L5 装配序列（P-UI-10：
 *     码值以 diagnostics 码表收编表现为准，不私定）——本表不携带注册表
 *     句柄（v0.5 裁决签名纪律）。
 */
struct CommandRegistryDeps {
    /// 诊断工厂（用户级码 create 唯一入口；可空＝无目录场景，须显式声明）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
    /// 诊断目录 sink（create 产物的记录面；可空＝同上）。
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    /// 开发日志通道（Dev 码唯一出线；可空＝无日志场景，须显式声明）。
    std::shared_ptr<diagnostics::IDevLogSink> devLog;
    /// owner 白名单（§7.2 第 1 步——SA-01 静态白名单的命令侧：ui 内部设施
    /// 名＋各白名单插件 id；装配层给出，运行期只读）。
    std::vector<std::string> ownerWhitelist;
};

/**
 * @brief 创建命令注册表实例（装配层独占持有——unique_ptr 所有权即刻移交）。
 *
 * 为什么是工厂函数而不是公共具体类：具体实现类属库私有（R-2 同
 * WorkbenchShell 惯例），消费方（L5/插件经 IPluginUiRegistrar、命令面板、
 * 快捷键表）只见 ICommandRegistry 接口。
 *
 * @param deps [in] 装配依赖（白名单/诊断面——见 CommandRegistryDeps 注释）
 * @return 空注册表（装配期；seal 前可注册）
 */
std::unique_ptr<ICommandRegistry> createCommandRegistry(CommandRegistryDeps deps);

// =====================================================================
// ICommandRegistry——接口（§10.3）
// =====================================================================

/**
 * @brief 命令注册表接口（§10.3 契约表原文逐条承载；O-31 增量见
 *        CommandOutcome.revisionResult 与 seal 注释）。
 *
 * 生命周期/所有权（§10.3 契约表）：handler 按值持有（std::function）；注册
 * 项在壳生命周期内不注销（静态白名单——无注销接口，§7.2"不存在运行时
 * 卸载"）。非法使用：运行期 registerCommand（seal 后拒绝）；重复 id（拒绝
 * ＋诊断）；处理器内直接修改领域对象/项目文件（只准走命令端口——§10.3
 * "非法"行）；以 availability 之外的方式隐藏写入口绕过只读（§7.6）。
 */
class ICommandRegistry {
public:
    virtual ~ICommandRegistry() = default;

    // ---- 装配期（L5/插件经 IPluginUiRegistrar 间接使用；§7.2 注册协议）----

    /**
     * @brief 注册命令（无自定义谓词——默认可见/默认使能规则，见下）。
     *
     * 处理器签名说明：§10.3 原文为
     * `std::function<CommandOutcome(std::span<const CommandParameter>)>`；
     * std::span 是 C++20 设施，本项目钉死 C++17（core.md D-01/DTB §5.1），
     * 以 const vector 引用承载同一语义（只读视图——实现层面登记 ui.md
     * §16.7 v0.8，契约语义零变化）。
     *
     * 注册协议（§7.2）：①id 句法校验（点分小写）＋owner 在白名单内；
     * ②重复 id（含不同 owner）→ 拒绝＋诊断 UI-CMD-DUPLICATE（Dev，装配期
     * ——经 deps.devLog 出线）；③写入注册表（registrationOrder＝装配序）。
     * 默认谓词（未走 registerCommandWithPredicates 时）：可见＝恒可见
     * （§7.4"禁用＋说明"保留发现性）；使能＝作用域/只读规则（Session/View
     * 恒可用；Project 须有项目且 readOnlyAllowed=false 者还须可写——§7.5/
     * §7.6，与 UI-T03 壳板同口径）。
     *
     * @param descriptor [in] 命令描述符（构造后注册表持拷贝——调用方可复用）
     * @param handler    [in] 命令处理器（按值持有；UI 线程启动——禁止长计算）
     * @return Ok｜DuplicateId｜InvalidDescriptor｜OwnerNotWhitelisted
     *         （拒绝时不覆盖既有登记——§7.2"不覆盖不静默"）
     *
     * @throws 无（校验失败走返回值轨——§10.3"错误类型：RegistrationResult
     *         枚举＋诊断"；处理器异常在 submit 捕获，注册期不执行处理器）
     */
    using CommandHandler = std::function<CommandOutcome(const std::vector<CommandParameter>&)>;

    virtual RegistrationResult registerCommand(const CommandDescriptor& descriptor,
                                               CommandHandler handler) = 0;

    /**
     * @brief 注册命令并绑定可见/可执行谓词（§10.3 原文签名——§7.5 谓词）。
     *
     * 谓词在每次 query/availability/paletteSnapshot 时按当前快照求值；
     * 传 nullptr 谓词等价于该轴取默认规则（可见恒真／使能按作用域只读）。
     */
    virtual RegistrationResult registerCommandWithPredicates(
        const CommandDescriptor& descriptor,
        CommandHandler handler,
        VisibilityPredicate visible,
        EnablementPredicate enablement) = 0;

    /**
     * @brief 装配收口（进入运行期——§7.2"运行期：注册表只读快照查询"）。
     *
     * §10.3"非法"行"运行期 registerCommand（拒绝）"的执行机制：seal 后
     * 调用 registerCommand* 属调用方契约违约——Q_ASSERT（调试期）＋返回
     * InvalidDescriptor（安全轨，全构型生效；不抛——装配违约与运行期提交
     * 拒绝同走值轨）。幂等：重复 seal 无操作。seal 前 submit/query 合法
     * （装配自检场景——不禁止）。
     */
    virtual void seal() = 0;

    /**
     * @brief 注入可用性求值上下文快照（§7.5 谓词求值的唯一上下文来源）。
     *
     * 装配层/壳在上下文变化点（presentProjectContext 同拍）推送原子快照；
     * 注册表持最近一份（值拷贝——后续谓词求值全部消费该快照，谓词内零
     * 端口查询/IO——§7.5"保证 <1 ms"纪律的实现前提）。未推送前求值消费
     * 默认构造快照（无项目态——PM-10 门控口径）。
     */
    virtual void presentContext(const UiContextSnapshot& snapshot) = 0;

    // ---- 运行期（UI 线程；命令面板/菜单/快捷键共用）----

    /**
     * @brief 只读投影查询（§10.3 原文签名——菜单/面板/快捷键的数据源）。
     *
     * 返回按注册序稳定排序的投影行（同输入同序——NFR-COR-02 界面延伸）；
     * 每行的可见/使能态为当前快照求值结果。
     */
    virtual std::vector<CommandView> query(const CommandQuery& query) const = 0;

    /**
     * @brief 单命令可用性（§10.3 原文签名——{visible, enabled,
     *        disableReasonKey, readOnlyBlocked}）。
     */
    virtual CommandAvailability availability(CommandId id) const = 0;

    /**
     * @brief 提交命令（§10.3 原文签名——统一提交路径，菜单/面板/快捷键
     *        三处共用，无旁路）。
     *
     * 前置校验链（§7.7 时序①）：未知命令→拒绝＋UI-CMD-UNKNOWN（含请求
     * id），不派发；不可执行/只读→拒绝＋UI-CMD-NOT-EXECUTABLE（含只读
     * 阻断——§7.6 双保险的界面侧）；通过→处理器恰好执行一次。
     *
     * 处理器异常→捕获→返回 accepted=false＋UI-CMD-NOT-EXECUTABLE＋Dev
     * 日志（不让异常穿透事件循环——§10.3 错误类型行）。
     *
     * @param id      [in] 命令 id
     * @param params  [in] 命令参数（无参命令传空表——不透明搬运给处理器）
     * @return 提交结果（accepted＋可选消息键＋可选修订结果投影）
     */
    virtual CommandOutcome submit(CommandId id, std::vector<CommandParameter> params = {}) = 0;

    /**
     * @brief 命令面板模糊快照（§10.3 原文签名；§7.4 面板投影）。
     *
     * 匹配规则（§7.4 原文）：子序列匹配（大小写不敏感）＋词边界前缀加权
     * ＋关键字命中加权；得分并列按 registrationOrder 稳定排序；结果上限
     * limit（§7.4 默认 50，超出由调用方提示"继续输入以缩小范围"）。近期
     * 使用命令置顶分组（§7.4"最近使用"行）——submit 接受即自动登记一次
     * 近期使用（三处入口统一计数，避免菜单/快捷键/面板口径漂移）。
     *
     * @param fuzzy [in] 模糊词（空＝全部命令）
     * @param limit [in] 结果上限（§7.4 默认 50）
     * @return 面板投影行（含当前可见/使能态与绑定键显示文本）
     */
    virtual std::vector<CommandView> paletteSnapshot(const std::string& fuzzy,
                                                     std::size_t limit) const = 0;

    /**
     * @brief 近期使用序（§7.4 置顶分组的数据源；最新在前，≤20）。
     *
     * submit 接受（accepted=true）即自动置顶去重；上限 20（§7.4"用户级
     * 持久化最近 20 条"）。
     */
    virtual std::vector<CommandId> recentUsed() const = 0;

    /**
     * @brief 装载持久化的近期使用历史（启动路径——PM-14 用户级设置）。
     *
     * 装配层从用户级设置读出后一次性注入；未注册 id 丢弃（历史可能跨
     * 版本失效）、重复收敛、裁剪到 20。运行期任意时刻调用合法（重放覆盖
     * 当前会话序）。
     */
    virtual void restoreRecentUsed(std::vector<CommandId> ids) = 0;
};

}  // namespace ui
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_UI_ICOMMANDREGISTRY_HPP
