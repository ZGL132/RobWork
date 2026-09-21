/**
 * @file   IDiagnosticPresentationModel.hpp
 * @brief  诊断呈现模型（§10.8）——诊断表/详情/建议动作/恢复横幅/日志面板
 *         的唯一数据装配面（§9.1 诊断呈现的落位契约）。
 *
 * 设计依据：
 *   - units/ui.md §9.1（诊断呈现全表：过滤/稳定排序/聚合折叠、原因链展开、
 *     actionKind→动作映射表、比较型三要素数值＋单位同显、恢复横幅 PM-15、
 *     Tier-U 日志面板、脱敏仅配置界面、呈现映射分类→去向、UX-03 正常
 *     取消无错误呈现）、§10.8（IDiagnosticPresentationModel 接口原文——
 *     本任务即其"首消费冻结"落位任务，签名按 O-31 值投影机制冻结并登记
 *     §16.7 v1.5）、§3.5（文案键体系——键经 UiText 唯一出口解析）；
 *   - diagnostics.md §9.7（IDiagnosticSink 只读投影——DiagProjectionItem
 *     值拷贝，"ui 不得反写目录、不得以投影文本反查原文"红线）、§4.4
 *     （actionKind 机器锚点词表）、§6.4（去重计数/聚合导航）；
 *   - 需求 UX-03（失败提示字段/取消无错误）、PM-15（恢复横幅）、
 *     NFR-REL-05（Tier-U 无调用栈/内部哈希）、NFR-SEC-07（脱敏）；
 *   - 任务契约 tasks/foundation/UI-T13.json acceptance 1（UI-DIA-1＋
 *     诊断呈现逐项）。
 *
 * 背景说明（为什么呈现模型是"纯装配"而不是第二个诊断源）：诊断事实、
 * 阈值、严重级别的唯一权威在 diagnostics（SA-12/N-7——ui 不改变诊断
 * 事实）；目录的过滤/去重/稳定排序已在 sink.snapshot 内完成（§9.7）。
 * 本模型的职责边界＝**把只读投影翻译为可呈现的值**（动作映射/三要素
 * 文案/横幅装配/面板快照），任何"再过滤、再计数、再排序"的语义新增都
 * 属越权（§6.8 禁令"UI 侧由快照数据计算工程判定"同源红线）。
 *
 * 线程约束：公共方法一律 UI 线程调用（§10.8 线程行）；唯一例外是
 * subscribe 产生的目录通知——底层回调在目录通知线程到达，模型内经
 * QMetaObject 排队投递 Marshal 回 UI 线程后才调用观察者（§3.4 M-1
 * 纪律；实现见 DiagnosticPresentation.cpp）。
 *
 * 头文件零 Qt（QObject/Marshal 半区全在实现 TU）——模型层可无 GUI 测试
 * （§12.1 第一层分工）。
 */

#ifndef SDURWS_IRD_UI_IDIAGNOSTICPRESENTATIONMODEL_HPP
#define SDURWS_IRD_UI_IDIAGNOSTICPRESENTATIONMODEL_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Events.hpp>             // core::IEventSubscription（subscribe 返回的 RAII 句柄——表内登记边）
#include <sdurws/ird/core/Provenance.hpp>         // core::FieldState（比较型四态值的状态判别——表内登记边）
#include <sdurws/ird/diagnostics/Catalog.hpp>     // DiagProjectionItem/DiagQuery/IDiagObserver/IDiagnosticSink/IRedactionService 前向/IDevLogSink（表内登记边）
#include <sdurws/ird/diagnostics/DiagCodes.hpp>   // DiagnosticCategory（呈现映射的判别键——表内登记边）
#include <sdurws/ird/diagnostics/Confirmable.hpp> // diagnostics::FindingRecord（pendingConfirmations 值面——表内登记边）
#include <sdurws/ird/ui/UiProjections.hpp>        // UserLogEntry（Tier-U 面板条目投影——UI-T13 增量）

namespace sdurws::ird {
namespace ui {

class IUiFindingQueryPort;   ///< C-9 确认投影呈现半区端口（UiPorts.hpp——指针成员前置声明）
class IUiUserLogSource;      ///< Tier-U 日志面板数据源端口（UiPorts.hpp——同上）

// =====================================================================
// §9.1 建议动作：actionKind→动作映射表（ui 冻结呈现层映射）
// =====================================================================

/**
 * @brief 诊断建议动作的呈现类别（§9.1"actionKind→动作映射表"行的
 *        ui 侧词表——每个类别对应一种**呈现层去向**）。
 *
 * 红线（§9.1 动作行原文"动作执行全部路由 CommandRegistry 或跳转，
 * 不直调领域服务"）：本词表只描述去向（跳到哪个界面/打开哪个对话），
 * **不承载任何业务执行**——真正的重跑/重链/确认提交都由对应界面经
 * 命令端口或任务端口发起（SA-16 唯一入口）。词表冻结：只允许随
 * diagnostics §4.4 动作族修订演进（表尾追加），不私造类别。
 */
enum class DiagActionKind : std::uint8_t {
    None,                 ///< 无动作（§4.4 "none"——正常取消等 UX-03 无错误呈现）
    NavigateToEdit,       ///< 跳转编辑（fix-input——失败态＋对象定位，左栏定位对象）
    ShowLockHolder,       ///< 显示锁持有者（contact-holder——只读横幅 PID 呈现，PM-07）
    OpenRerunEntry,       ///< 重跑入口（rerun-interrupted——"已中断"可重跑，NFR-REL-03）
    OpenConfirmDialog,    ///< 打开确认对话（confirm-or-fix——SA-15 确认流，§9.2）
    OpenLogPanel,         ///< 打开日志页（inspect-log——Tier-U 面板跳转）
    OpenPolicyEntry,      ///< 打开策略入口（adjust-policy——§6.7 策略摘要卡路径说明）
    OpenUpgradeGuide,     ///< 打开升级指引（open-upgrade-guide——PM-06 旧格式呈现）
    RelinkResource,       ///< 重链资源流程入口（relink——PM-09 外部源 Missing/Changed）
    SupplyEvidence,       ///< 打开证据清单（supply-evidence——data-insufficient 缺项呈现）
    ReviewProof,          ///< 打开评审记录（review-proof——RPT-05 不可行证明呈现）
    ReportBug,            ///< 打开开发日志（report-bug——internal 类 Dev 通道出线）
    InspectResource,      ///< 资源检视（inspect-resource——SafePath/预算/脱敏类）
    OpenTaskPanel,        ///< 打开任务面板（retry-task——失败态重试入口所在面）
};

/**
 * @brief 单条诊断的建议动作值（DiagActionKind＋可选界面定位参数）。
 *
 * targetId：定位参数（如对象身份的规范文本——NavigateToEdit 时左栏
 * 定位用）。**禁止放哈希形态文本**（UX-02——组装方传身份前须经名称
 * 端口解析局部名；本字段由呈现层按需填充，映射表本身不产身份文本）。
 */
struct DiagAction {
    /// 动作类别（None＝无动作——按钮隐藏，不渲染占位控件）。
    DiagActionKind kind = DiagActionKind::None;
    /// 界面定位参数（可空；语义随 kind——见结构体注释；零哈希纪律）。
    std::string targetId;
};

/**
 * @brief actionKind 稳定 token→呈现动作的映射表（§9.1 动作行的唯一
 *        权威实现——ui 冻结呈现层映射，纯函数）。
 *
 * @param actionKind [in] DiagProjectionItem.actionKind（diagnostics
 *                    §4.4 机器锚点 token——actionKindToken(category) 的
 *                    产出词表，小写连字符）
 * @return 呈现动作（未知 token——未来 §4.4 新增动作族——→None：呈现层
 *         不虚构动作，等映射表随单元卡增量修订扩展；这是词表演进的
 *         安全兜底，不是错误）
 *
 * 映射逐行出处（§9.1 表原文）：fix-input→跳转编辑；contact-holder→
 * 显示 PID；rerun-interrupted→重跑入口；confirm-or-fix→确认对话（§9.2）；
 * inspect-log→日志页；其余 §4.4 动作族按"全部路由跳转"纪律落对应去向
 * （见 DiagActionKind 各值注释）。
 */
DiagAction diagActionFor(const std::string& actionKind);

// =====================================================================
// §9.1 呈现映射：分类→去向（diagnostics §4.4 UI 映射列的 ui 落地）
// =====================================================================

/**
 * @brief 诊断分类的呈现去向（§9.1"呈现映射"段的 ui 侧词表——GUI 据
 *        此把诊断放入对应呈现面）。
 */
enum class DiagPresentationRoute : std::uint8_t {
    FailureWithObjectFocus,   ///< 失败态＋对象定位（输入非法/执行失败/资源/证据/超时/安全类）
    ReadOnlyBanner,           ///< 只读横幅（权限或锁——PM-07 显示差异）
    ConfirmDialog,            ///< 确认对话（可确认——SA-15，§9.2）
    NoErrorPresentation,      ///< 无错误呈现（取消——UX-03"正常用户取消无错误呈现"；
                              ///  不可行证明为有效结论，同为非错误呈现面）
    InterruptedRerunnable,    ///< "已中断"可重跑（中断——NFR-REL-03）
    DevLogOrRecoveryBanner,   ///< 开发日志/恢复横幅（内部——不入用户可执行面）
};

/**
 * @brief 分类→呈现去向的映射（§9.1"呈现映射"段唯一权威实现，纯函数）。
 *
 * @param category [in] 诊断分类（§4.3 全 15 值——switch 全枚举）
 * @return 呈现去向（映射逐行注释见实现；ui 不改变分类/严重级别——
 *         只决定"放进哪个呈现面"，N-7 红线）
 */
DiagPresentationRoute diagnosticRouteFor(diagnostics::DiagnosticCategory category);

// =====================================================================
// §9.1 比较型三要素：数值＋单位同显（四态占位不伪造数值）
// =====================================================================

/**
 * @brief 比较型单侧值的呈现文本（数值＋单位同显或四态占位）。
 *
 * state 原样携带 core 四态（GUI 据此着色）；text 为已渲染中文文本：
 * Provided→"数值 单位"（6 位有效数字——UiText 数值格式约定）；其余
 * 三态→占位词（**不伪造数值**——ERR-01/MDL-06"缺失≠零"的呈现半区）。
 */
struct ComparisonValueText {
    /// 原始四态（core::FieldState 直用——不镜像不折叠）。
    core::FieldState state = core::FieldState::NotProvided;
    /// 呈现文本（UTF-8；Provided＝"数值 单位"同显，其余＝占位词）。
    std::string text;
};

/**
 * @brief 比较型三要素的两侧呈现文本。
 */
struct ComparisonText {
    ComparisonValueText actual;    ///< 实际值侧
    ComparisonValueText expected;  ///< 期望值侧
};

/**
 * @brief 比较型单侧值→呈现文本（纯函数；UX-03"数值＋单位同显；
 *        NotApplicable/Invalid 显示'不适用/无效'，不伪造数值"）。
 *
 * 四态处置：Provided→数值（6 位有效数字）＋空格＋单位 token（单位与
 * 数值同串显示——KIN-12 同显纪律）；NotApplicable→UiText::notApplicable
 * Text()「不适用」；Invalid→「无效（原串）」（保留原文——NFR-COR-03，
 * 原串经 resolveText 参数守卫防哈希泄漏）；NotProvided→「未提供」。
 *
 * @param value [in] 比较型单侧值（core::ComparativeValue——数值四态＋
 *              单位 token；单位仅在 Provided 态参与显示）
 * @return 呈现文本（四态必居其一，无空串输出）
 */
ComparisonValueText formatComparisonValue(const core::ComparativeValue& value);

/**
 * @brief 比较型三要素→两侧呈现文本（确认对话/诊断详情共用，纯函数）。
 */
ComparisonText formatComparison(const core::ComparativeFields& comparison);

// =====================================================================
// §9.1 恢复横幅（PM-15）——一句话汇总＋三动作
// =====================================================================

/**
 * @brief 会话恢复横幅投影（PM-15"一句话汇总＋查看详情/恢复草稿/放弃"
 *        的数据装配值——纯装配，零 Qt）。
 *
 * 事实源（§9.1 恢复横幅行）：未完成保存已忽略（project RecoveryReport
 * 的会话级事实，经 L5 折叠为布尔位）、中断任务（execution TaskState=
 * Interrupted 的清单非空位）、未保存草稿（DraftController §8.3 孤儿
 * 计数同源）。三事实独立可选，横幅在任一事实为真时呈现。
 */
struct SessionRecoveryBannerProjection {
    /// 是否呈现横幅（false＝三事实皆空——不渲染，不虚构恢复叙事）。
    bool any = false;
    /// 一句话主文案键（择一：忽略保存＞中断任务＞未保存草稿——
    /// priority 即用户的行动紧迫度；orphan-drafts 键含 {0} 计数占位）。
    TextKey summaryKey;
    /// 主文案参数（仅 summaryKey==…orphan-drafts 时非空——{0}=份数）。
    std::vector<std::string> summaryArgs;
    /// 其余事实的追加行文案键（与 summary 互补——汇总只一句话，剩余
    /// 事实降级为详情行，避免横幅堆叠）。
    std::vector<TextKey> detailKeys;
    /// 未保存草稿份数（恢复动作可用性依据；0＝无草稿可恢复）。
    std::size_t orphanDraftCount = 0;
    /// [查看详情] 文案键（固定三动作之一）。
    TextKey actionDetailsKey;
    /// [恢复草稿] 文案键（orphanDraftCount==0 时调用方应禁用该动作——
    /// 无草稿可恢复，动作不隐藏只禁用，保持三动作位形稳定）。
    TextKey actionRestoreKey;
    /// [放弃] 文案键（放弃＝显式 discard——执行归 DraftController §8.3-4，
    /// 横幅只呈现决议入口）。
    TextKey actionDiscardKey;
};

/**
 * @brief 装配恢复横幅（PM-15 唯一实现点，纯函数）。
 *
 * @param ignoredSaves     [in] 上次会话有未完成保存且已忽略（RecoveryReport 事实）
 * @param interruptedTasks [in] 存在上次会话被中断的任务
 * @param orphanDraftCount [in] 检测到的未保存（孤儿）草稿份数
 * @return 横幅投影（三事实皆空→any=false 的空值——调用方不渲染）
 */
SessionRecoveryBannerProjection
assembleRecoveryBanner(bool ignoredSaves, bool interruptedTasks,
                       std::size_t orphanDraftCount);

// =====================================================================
// §9.1 Tier-U 日志面板（flush 仅关闭路径——模型无 flush 面）
// =====================================================================

/**
 * @brief 用户日志面板快照（§9.1 日志面板行的呈现值——纯装配）。
 *
 * flush 纪律（§3.4"禁止等待 flush（ILogger flush 仅关闭路径）"的结构性
 * 承载）：本快照与数据源端口（IUiUserLogSource）都不提供 flush/写面——
 * 日志面板是纯只读回看；flush 只发生在 L5 关闭路径直接调 diagnostics
 * 设施。Dev 级内容**不内嵌显示**（NFR-REL-05）：面板只呈现 Tier-U 条目；
 * Dev 通道的唯一出线是 devFileJumpAvailable 为真时的"打开开发日志文件"
 * 跳转动作（文件定位归 L5，ui 不解析路径内容）。
 */
struct UserLogPanelSnapshot {
    /// Tier-U 条目快照（最新在后——数据源序；空＝无日志）。
    std::vector<UserLogEntry> entries;
    /// 空面板占位文案键（entries 空时呈现，不虚构条目）。
    TextKey emptyPanelKey;
    /// Dev 日志文件可跳转位（false＝动作隐藏，不虚构路径）。
    bool devFileJumpAvailable = false;
    /// "打开开发日志文件"动作文案键（devFileJumpAvailable 时呈现）。
    TextKey devJumpLabelKey;
};

/**
 * @brief 装配用户日志面板快照（纯函数；source 为空＝无日志数据源——
 *        空面板＋无跳转，不虚构）。
 *
 * @param source [in] Tier-U 数据源端口（可空——无装配场景）
 * @return 面板快照
 */
UserLogPanelSnapshot assembleUserLogPanel(const IUiUserLogSource* source);

// =====================================================================
// §10.8 IDiagnosticPresentationModel（接口原文——首消费冻结签名）
// =====================================================================

/**
 * @brief 诊断呈现模型（§10.8 接口——诊断表/详情链/确认投影/目录订阅/
 *        路径脱敏转发的唯一数据装配面）。
 *
 * 契约要点（§10.8 契约表逐行，详见各方法注释）：
 *   - 查询返回值拷贝（含去重计数/聚合导航字段——目录已去重，同键折叠
 *     由 occurrences 承载）；无自有目录数据（目录归 diagnostics）；
 *   - sink 快照失败→空集＋Dev 日志，无异常穿透（§10.8 错误类型行）；
 *   - 订阅回调全部 UI 线程（目录通知线程→模型内 Marshal——§3.4 M-1）；
 *   - 非法：反写/删除目录条目（无接口）；以投影文本反查原文；在 ui
 *     修改诊断事实/严重级别/阈值；直接调 IConfirmableFindingService
 *     提交确认（确认只经 §9.2 Bridge——pendingConfirmations 是只读投影）。
 */
class IDiagnosticPresentationModel {
public:
    virtual ~IDiagnosticPresentationModel() = default;

    /**
     * @brief 按查询过滤取诊断投影（§10.8 query——诊断表数据源）。
     *
     * 过滤/去重/稳定排序全部由 sink.snapshot(DiagQuery) 承担（§9.7）；
     * 本方法只透传查询并返回值拷贝——同 dedupKey 的重复命中已折叠为
     * 单条目（occurrences 计数呈现"重复 N 次"；aggregatedUnder 为聚合
     * 导航锚，阶段 A 恒空——diagnostics §6.4/§9.7）。
     *
     * @param query [in] 过滤器（任务/修订/类别/严重——全空＝全量）
     * @return 投影值拷贝清单（orderKey 稳定序；空＝无命中或快照失败
     *         ——失败另走 Dev 日志，无异常穿透）
     */
    virtual std::vector<diagnostics::DiagProjectionItem>
    query(const diagnostics::DiagQuery& query) const = 0;

    /**
     * @brief 展开原因链（§10.8 expandChain——causedBy 单父 DAG 逐级
     *        上溯，诊断详情页数据源）。
     *
     * @param entryId [in] 起始条目（详情页当前条目）
     * @return 链上条目值拷贝（**首元素＝起始条目本身**，其后按 causedBy
     *         逐级上溯至根；单父 DAG 保证链唯一；自环/环路由构造期
     *         DAG 性质排除，实现另带防御式截断）；条目不存在→空集
     *         （不虚构链）
     */
    virtual std::vector<diagnostics::DiagProjectionItem>
    expandChain(std::uint64_t entryId) const = 0;

    /**
     * @brief 取非命令期待确认项投影（§10.8 pendingConfirmations——
     *        §9.2"pendingFor(revision) 投影只读显示（ui 经 project 间接
     *        读——不直接调 diagnostics 服务端接口）"）。
     *
     * @return 服务端记录值拷贝（Pending 态；findingQuery 端口未注入
     *         ＝空集——无确认呈现面，不虚构；纯只读，确认提交只经
     *         §9.2 Bridge）
     */
    virtual std::vector<diagnostics::FindingRecord> pendingConfirmations() const = 0;

    /**
     * @brief 订阅目录变更（§10.8 subscribe——回调 Marshal 至 UI 线程）。
     *
     * @param observer [in] 观察者（引用须覆盖订阅期——观察者析构前
     *                 调用方须先退订；回调只保证发生在 UI 线程）
     * @return RAII 订阅句柄（析构＝退订＋丢弃在途排队通知；重复退订幂等）
     */
    virtual std::unique_ptr<core::IEventSubscription>
    subscribe(diagnostics::IDiagObserver& observer) = 0;

    /**
     * @brief 展示路径脱敏转发（§10.8 redactedPath——IRedactionService::
     *        redactPath 的只读转发面；呈现层不自行脱敏——转换权威在
     *        diagnostics 管线，本方法不加工不缓存）。
     *
     * @param raw [in] 路径原文
     * @return 脱敏后路径（服务内部失败时为对端降级 token——单向转换，
     *         严禁以结果反推原文，Redaction.hpp 契约）
     */
    virtual std::string redactedPath(std::string_view raw) const = 0;
};

// =====================================================================
// 装配依赖与工厂（L5 注入面——可空成员的语义见各字段注释）
// =====================================================================

/**
 * @brief 诊断呈现模型的装配依赖（L5 装配期注入；指针非 owning，
 *        生存期须覆盖模型）。
 */
struct DiagnosticPresentationDeps {
    /// 诊断目录只读投影面（**必注入**——无目录则模型无存在意义，
    /// 工厂 fail-fast）。
    diagnostics::IDiagnosticSink* sink = nullptr;
    /// 路径脱敏服务（**必注入**——redactedPath 无服务时呈现原文路径
    /// 即 NFR-SEC-07 违例，宁 fail-fast 不降级泄漏）。
    const diagnostics::IRedactionService* redaction = nullptr;
    /// 确认投影呈现半区（可空——无确认呈现面：pendingConfirmations
    /// 返回空集，须显式声明语义）。
    IUiFindingQueryPort* findingQuery = nullptr;
    /// Tier-U 日志数据源（可空——空面板占位，须显式声明语义）。
    IUiUserLogSource* userLog = nullptr;
    /// 开发日志路由（可空——快照失败的 Dev 记录静默跳过；落地前传
    /// 空与 DIAG-T05 devLog 同案）。
    diagnostics::IDevLogSink* devLog = nullptr;
    /// 原因链查询缝（可空——**单侧冻结**，登记 ui.md §16.7 v1.5）：
    /// §10.8 expandChain 要求沿 causedBy 单父 DAG 上溯，而 diagnostics
    /// §9.7 投影（DiagProjectionItem）不携带链字段（链数据只在条目信封
    /// DiagnosticEntry 上——表内边投影边界），sink 四方法无逐跳查询面。
    /// 链的逐跳数据因此由装配层供给：典型实现＝持有条目视图的 L5 设施
    /// 调 diagnostics CauseLink::chainOf(entries, start)（起点→根因全链
    /// 身份序列——严格契约：断链/环 Usage）。未注入＝expandChain 退化
    /// 为"起点单条目"（自身即根）——不虚构链，缺口登记待 diagnostics
    /// 侧投影扩展（新增字段＝次版本，§9.7 稳定性行）后吸收本缝。
    std::function<std::vector<diagnostics::DiagEntryId>(diagnostics::DiagEntryId)>
        chainOf = nullptr;
};

/**
 * @brief 创建诊断呈现模型（构造线程＝UI 线程——Marshal 上下文 QObject
 *        在构造线程建立，§3.4 M-1）。
 *
 * @param deps [in] 装配依赖（sink/redaction 非空校验——违约抛
 *             std::invalid_argument fail-fast）
 * @return 模型实例
 *
 * @throws std::invalid_argument sink 或 redaction 为空（装配契约违约）
 */
std::unique_ptr<IDiagnosticPresentationModel>
createDiagnosticPresentationModel(DiagnosticPresentationDeps deps);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_IDIAGNOSTICPRESENTATIONMODEL_HPP
