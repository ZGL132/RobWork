/**
 * @file   IDraftController.hpp
 * @brief  草稿控制器（IDraftController）——未保存标记/草稿投影/会话状态机
 *         界面侧的草稿编排器（§8 DraftController 与草稿交互；§10.5 接口
 *         详设的实现冻结落点）。
 *
 * 设计依据：
 *   - units/ui.md §8 全节：§8.1（分工红线：草稿数据模型/落盘/恢复数据归
 *     project；定时器/手动保存入口/恢复编排/跨模块汇总/未保存标记归 ui；
 *     "保存草稿产生修订？否"）、§8.2（组件与数据流：ModuleDraftTable＋
 *     定时器 UI 线程触发＋ui 后台落盘线程；汇总视图＝会话脏 OR 磁盘
 *     present）、§8.3（打开恢复：list→tryLoad→损坏 .bak 回退→恢复横幅→
 *     孤儿处置）、§8.4（定时器与线程纪律：UI 线程零磁盘 IO/落盘线程串行/
 *     60 s 默认周期/手动与定时共用同一入口/关闭对话上下文同步完成）、
 *     §8.5（应用与 StaleRevisionRejected：冲突呈现/草稿保留/可继续编辑/
 *     不自动重试提交）、§8.6（关闭/切换/分支切换/只读的草稿处置）、
 *     §8.7（草稿局部撤销与项目命令撤销两栈互不越界）；
 *   - §10.5（IDraftController 接口详设——本头按 O-31 裁决"§10.5 IDraft
 *     Controller 签名随本任务实现冻结并增量修订登记"机制落位：restoreOnOpen
 *     的对端形态改为会话绑定注入，登记 ui.md §16.7 v1.4）；
 *   - 需求 PM-04（保存/应用分离：save 只落 drafts/ 不产生修订；应用＝恰好
 *     一个新修订）、PM-08/PM-15（草稿恢复数据面/恢复横幅）、RV-10（基线
 *     过期拒绝——草稿保留、可继续编辑）、PM-11（标题 `*` 未保存标记）、
 *     PM-18/N-10（两栈边界）、PM-12（分支切换不跨分支迁移草稿）；
 *   - 任务契约 tasks/foundation/UI-T12.json acceptance 1~3（逐条对应见各
 *     方法注释的追溯标注）；knownPitfalls O-31（已裁决 2026-09-19——对端
 *     类型 DraftService/ProjectCommandService/IProjectQueryPort 经 ui 自有
 *     草稿/命令/查询端口＋值投影承载，产品面零对端链接/include，
 *     NoCrossUnitInclude_O31_UI_BUILD 守卫常驻）。
 *
 * 背景说明（控制器在装配图中的位置——为什么它不提交命令）：
 *   "保存"与"应用"是两条红线分离的路径（PM-04）：保存＝落 drafts/ 目录
 *   的单文件原子替换（零修订）；应用＝域编辑器组装命令信封经命令端口
 *   （IUiCommandGateway）提交、恰好产生一个新修订。本控制器只承载**保存
 *   半边**与**应用回执的消费半边**（onCommandResult）——它不持有命令网关、
 *   不组装域负载、不判定冲突（§10.5"非法"行原文：在 DraftController 内
 *   组装领域命令载荷/应用草稿均禁止）。"不得自动重试提交"（§8.5）因此
 *   由结构保证：控制器没有任何可提交的通道。
 *
 * 线程模型（§3.4 M-1＋§8.4）：
 *   - 公共方法一律 UI 线程调用（模型状态只在 UI 线程变更——无锁）；
 *   - 磁盘写（save/discard）只经 postToDiskThread 串行执行器进入落盘
 *     线程——UI 线程零磁盘 IO 红线的执行面（端口调用在产品代码内被
 *     封闭在执行器任务里，测试以线程标记替身自证）；
 *   - autosave 完成回执经 postToUiThread 转回 UI 线程消费（Marshal
 *     纪律）；Manual/CloseDialog 在调用线程阻塞等待落盘线程完成
 *     （§8.4"关闭流程中的 saveAll(Manual) 在确认对话框上下文同步完成"
 *     原文——等待≠执行 IO，零磁盘 IO 红线不动）。
 *
 * 生命周期/所有权（§10.5 所有权行）：控制器由 L5 应用壳持有；模块源
 * （IModuleDraftSource）归域编辑器，控制器只存裸引用（弱引用语义），
 * 域编辑器销毁前必须显式 detachModule；会话端口绑定（DraftSessionBinding）
 * 的 shared_ptr 所有权在 L5/装配层——控制器捕获进落盘任务的端口拷贝
 * 恰好覆盖在途保存的存活期（关闭上下文的对端拒绝语义照常经返回值轨
 * 表达，context-closed token）。
 */

#ifndef SDURWS_IRD_UI_IDRAFTCONTROLLER_HPP
#define SDURWS_IRD_UI_IDRAFTCONTROLLER_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Identity.hpp>         // core::ProjectId/BranchId（归属三元组——表内登记边直用）
#include <sdurws/ird/diagnostics/Catalog.hpp>   // diagnostics::IDiagnosticSink/IDevLogSink（用户级/Dev 级出线——表内登记边）
#include <sdurws/ird/diagnostics/Factory.hpp>   // diagnostics::IDiagnosticFactory（create 唯一入口——§9.2）
#include <sdurws/ird/ui/UiPorts.hpp>            // IUiDraftQueryPort/IUiDraftStorePort（C-5 读/写半区——O-31 注入面）
#include <sdurws/ird/ui/UiProjections.hpp>      // DraftDocumentProjection/DraftSaveOutcome/CommandResultProjection（值投影——O-31 载体）
#include <sdurws/ird/ui/UiTypes.hpp>            // TextKey（§3.5 文案键——横幅/对话框键半区）

namespace sdurws::ird {
namespace ui {

// =====================================================================
// 机制文案键（§3.5 键体系——键冻结于本头，值归 ui 文案资源/呈现层；
// P-DIAG-9 键值分离：模型层只携带键，GUI 层经文案资源解析呈现）
// =====================================================================

/// 恢复横幅主文案键（§8.3 恢复编排——"发现未应用的草稿修改"汇总语义）。
constexpr const char* kDraftRecoveryBannerSummaryKey = "ui.draft.recovery.banner.summary";
/// 恢复横幅 .bak 回退提示键（PM-08——"草稿已从备份恢复"）。
constexpr const char* kDraftRecoveryBannerBackupKey = "ui.draft.recovery.banner.backup-recovered";
/// 恢复横幅孤儿草稿键（PM-15——孤儿计数 {0} 呈现，不列内部名，UX-02）。
constexpr const char* kDraftRecoveryBannerOrphanKey = "ui.draft.recovery.banner.orphan";
/// 恢复横幅 .new 残留提示键（§8.3-3——保存崩溃现场已被对端处置的提示）。
constexpr const char* kDraftRecoveryBannerResidueKey = "ui.draft.recovery.banner.new-residue";
/// 恢复横幅动作键：查看详情（PM-15 三动作之一）。
constexpr const char* kDraftRecoveryActionViewDetailKey = "ui.draft.recovery.action.view-detail";
/// 恢复横幅动作键：恢复草稿（恢复编排主路径）。
constexpr const char* kDraftRecoveryActionRestoreKey = "ui.draft.recovery.action.restore";
/// 恢复横幅动作键：放弃（§8.3-4——显式 discard，写操作，只读不可用）。
constexpr const char* kDraftRecoveryActionDiscardKey = "ui.draft.recovery.action.discard";
/// 基线已前进提示键（§8.2——磁盘草稿 stale 位"基线已前进"的呈现面）。
constexpr const char* kDraftStaleHintKey = "ui.draft.stale-hint";
/// 基线冲突对话框主文案键（§8.5——"草稿基线冲突"）。
constexpr const char* kDraftStaleConflictTitleKey = "ui.draft.stale-conflict.title";
/// 基线冲突动作键：基于当前版本重新编辑（推荐——§8.5 对话框选项 1）。
constexpr const char* kDraftStaleConflictReEditActionKey = "ui.draft.stale-conflict.action.re-edit";
/// 基线冲突动作键：查看差异对象（§8.5 对话框选项 2——involvedObjectNames）。
constexpr const char* kDraftStaleConflictViewDiffActionKey = "ui.draft.stale-conflict.action.view-diff";
/// 基线冲突动作键：取消（§8.5 对话框选项 3——继续编辑，草稿原样保留）。
constexpr const char* kDraftStaleConflictCancelActionKey = "ui.draft.stale-conflict.action.cancel";

// =====================================================================
// 保存触发源（§8.4——手动与定时共用同一落盘入口，origin 区分）
// =====================================================================

/**
 * @brief 保存触发源（§10.5 冻结词表；§4.4.5 origin 三值中的 ui 可产出
 *        两值与关闭对话框场景）。
 *
 * 语义锚点：Autosave＝定时落盘（origin=autosave）；Manual＝手动保存
 * （Ctrl+S/命令面板，origin=manual）；CloseDialog＝关闭对话框内的
 * saveAll(Manual)（§8.4"关闭流程中的 saveAll(Manual)"——落盘语义与
 * Manual 相同，触发语境单独成词便于呈现区分进度来源）。ui 永不产出
 * apply-retained（应用被拒后的草稿保留由对端回写——§8.5）。
 */
enum class SaveTrigger : std::uint8_t {
    Autosave,   ///< 定时落盘（origin=autosave）
    Manual,     ///< 手动保存（origin=manual）
    CloseDialog,///< 关闭对话框保存（落盘语义＝Manual——§8.4 同一入口）
};

// =====================================================================
// 模块草稿视图与汇总（§8.2 ModuleDraftTable 的只读投影——未保存标记
// 与跨模块汇总的数据面）
// =====================================================================

/**
 * @brief 单模块草稿视图（§8.2 ModuleDraftTable 条目＋磁盘行的合并形态）。
 *
 * 两源合并纪律（§8.2"汇总视图＝两者合并（会话脏标记 OR 磁盘 present）"）：
 *   - sessionDirty＝会话脏位（本会话编辑后未落盘——ui 自有会话态，
 *     notifySessionDirty 置位、保存成功且期间无新编辑时清除）；
 *   - diskPresent＝磁盘存在该模块草稿（IUiDraftQueryPort 行投影直读——
 *     project 权威）；二者任一为真即计入标题 `*`（PM-11）；
 *   - stale/baseRevisionCanonical＝磁盘行的基线过期位与基线修订（对端
 *     判定直读——"ui 呈现'基线已前进'提示，不在 ui 侧判定冲突细节"）；
 *   - lastSaveOriginToken/lastSavedAtUtc＝本会话最近一次保存的 origin
 *     token（autosave/manual）与时刻（§8.2 表条目"最近保存 origin/时间"；
 *     磁盘行的落盘时刻不经 ui 记录——对端权威）。
 *
 * 值语义；呈现层只读消费（ARC-02 控件互读红线同口径）。
 */
struct ModuleDraftView {
    /// 模块 token（ModuleDraftHandle 同词表——注册表关联键）。
    std::string moduleId;
    /// 域模块显示名（UX-02 工程用语；孤儿行无源可查时回退模块 token——
    /// token 为注册词表标识，非哈希形态，UX-02 允许）。
    std::string displayName;
    /// 会话脏标记（编辑后未落盘——ui 会话态）。
    bool sessionDirty = false;
    /// 磁盘存在草稿（project 权威——present 位直读）。
    bool diskPresent = false;
    /// 草稿基线已过期（对端 stale 位直读——"基线已前进"提示素材）。
    bool stale = false;
    /// 草稿基线修订（canonical 文本——磁盘行直读；无磁盘草稿为空串）。
    std::string baseRevisionCanonical;
    /// 本会话最近保存 origin token（autosave/manual；空＝本会话未保存过）。
    std::string lastSaveOriginToken;
    /// 本会话最近保存时刻（ISO-8601 UTC；空＝本会话未保存过）。
    std::string lastSavedAtUtc;
};

/**
 * @brief 跨模块草稿汇总（§8.2"跨模块草稿汇总视图"——标题 `*` 与关闭
 *        对话框数据装配的数据面）。
 *
 * 排序纪律（NFR-COR-02 稳定呈现）：挂接模块按 attachModule 注册序，
 * 磁盘孤儿行（无挂接模块的磁盘草稿）按端口行序（对端 moduleId 字典序）
 * 追加其后——同状态同输出，不引入字典序重排（会话表序即用户挂接序，
 * 重排会让两次查询的呈现顺序漂移）。
 */
struct DraftingSummary {
    /// 是否存在未应用修改（任一模块 sessionDirty OR 磁盘 present——
    /// PM-11 标题 `*` 判定位；§8.2 合并规则的唯一实现点在控制器）。
    bool anyDirty = false;
    /// 模块视图（挂接模块注册序＋孤儿行端口序——见类型注释）。
    std::vector<ModuleDraftView> modules;
};

// =====================================================================
// 保存结果（§10.5 SaveOutcome 的 O-31 承载形态）
// =====================================================================

/**
 * @brief 单模块保存失败明细（对端 StoreError 的投影承载——token 直用
 *        不镜像枚举，NFR-MNT-03）。
 */
struct SaveModuleError {
    /// 失败模块 token。
    std::string moduleId;
    /// 失败稳定 token（对端 StoreError 词表——context-closed/
    /// lock-held-by-other/media-read-only 等）。
    std::string errorToken;
    /// 开发诊断明细（对端产出——透传不加工；呈现层不显示，UX-03）。
    std::string detail;
};

/**
 * @brief 一次 saveAll 的汇总结果（§10.5 SaveOutcome——保存/应用分离的
 *        可观测面：savedCount 只统计落盘成功，零修订语义由"控制器无命令
 *        通道"结构保证）。
 *
 * Async（Autosave 触发的定时链）语义：本类型描述**同步入口**的结局；
 * 定时链的分派计数经 tickAutosave 返回值表达，落盘成败经完成回执落进
 * 脏标记/UI-DRAFT-AUTOSAVE-FAILED（§8.2 失败行）——不在返回值轨。
 */
struct SaveOutcome {
    /// 落盘成功模块数（savedOnly——§8.2"成功→清脏标记"的统计面）。
    std::size_t savedCount = 0;
    /// 落盘失败模块数（失败项保留脏标记——§8.2 失败行原文）。
    std::size_t failedCount = 0;
    /// 失败模块 token 清单（failedCount 一致）。
    std::vector<std::string> failedModules;
    /// 首个失败明细（按收集序——调用方取一个代表性错误呈现/留痕）。
    std::optional<SaveModuleError> firstError;
};

// =====================================================================
// 打开恢复（§8.3——RestoreOutcome 携带损坏/孤儿明细）
// =====================================================================

/**
 * @brief 打开期草稿恢复报告（§8.3 恢复编排的结局值——恢复横幅装配的
 *        直接输入，PM-08/PM-15）。
 *
 * 字段语义（§8.3 逐行）：
 *   - restoredModules＝成功恢复进域编辑器的模块（Loaded＋RecoveredFrom
 *     Backup 合集——adoptRestoredDocument 已达成的清单）；
 *   - backupRecoveredModules＝其中经 .bak 回退恢复者（"草稿已从备份
 *     恢复"提示素材；**旧损坏文件由对端保留**供人工核查，不删除）；
 *   - corruptModules＝损坏且 .bak 亦不可用（恢复横幅呈现；不影响其余
 *     模块恢复——逐模块独立处置）；
 *   - orphanModuleIds＝磁盘存在但无挂接模块的草稿（恢复横幅一句话
 *     汇总＋"放弃＝显式 discard"（discardDraft）；可写会话才可用——
 *     §8.3-4 只读拒绝）；
 *   - newResidueDropped＝对端触达丢弃的 .new 崩溃残留计数（保存崩溃
 *     现场——横幅提示素材，§8.3-3）。
 */
struct RestoreOutcome {
    /// 已恢复进域编辑器的模块 token（Loaded＋RecoveredFromBackup）。
    std::vector<std::string> restoredModules;
    /// 经 .bak 回退恢复的模块 token（restoredModules 子集）。
    std::vector<std::string> backupRecoveredModules;
    /// 损坏不可恢复的模块 token（含 .bak 亦不可用）。
    std::vector<std::string> corruptModules;
    /// 孤儿草稿模块 token（磁盘有、会话无挂接源）。
    std::vector<std::string> orphanModuleIds;
    /// 对端触达丢弃的 .new 残留数（§8.3-3）。
    std::size_t newResidueDropped = 0;

    /// 是否发生过 .bak 回退（§10.5 示例 `if (ro.corruptRecovered) 恢复
    /// 横幅…` 的判定位——backupRecoveredModules 非空即真）。
    bool corruptRecovered() const noexcept { return !backupRecoveredModules.empty(); }
};

/**
 * @brief 草稿恢复横幅投影（PM-08/PM-15——§8.3 恢复横幅的呈现值装配）。
 *
 * 文案键/动作键为 §3.5 冻结键（本头常量族）；呈现层经文案资源解析。
 * 孤儿以计数呈现（orphanCount）——孤儿无挂接源即无显示名，列模块
 * token 会把内部标识推进用户文本（UX-02 红线），计数是唯一安全呈现。
 *
 * 值语义；present=false＝本次打开无任何恢复事件（不渲染横幅）。
 */
struct DraftRecoveryBannerProjection {
    /// 是否需要渲染横幅（有恢复/损坏/孤儿/残留任一事件即真）。
    bool present = false;
    /// 主文案键（kDraftRecoveryBannerSummaryKey——"发现未应用的草稿修改"）。
    TextKey messageKey;
    /// 已恢复模块显示名清单（UX-02——有源模块的 displayName）。
    std::vector<std::string> restoredModuleNames;
    /// 是否发生 .bak 回退（追加 kDraftRecoveryBannerBackupKey 提示）。
    bool backupRecovered = false;
    /// 孤儿草稿计数（kDraftRecoveryBannerOrphanKey 的 {0} 参数素材）。
    std::size_t orphanCount = 0;
    /// .new 残留计数（kDraftRecoveryBannerResidueKey 的 {0} 参数素材）。
    std::size_t newResidueDropped = 0;
    /// 动作键集（查看详情/恢复草稿/放弃——PM-15 三动作；固定序）。
    std::vector<TextKey> actionKeys;
    /// [放弃]是否可用（写操作——只读会话不可用，§8.3-4/§5.5）。
    bool discardAvailable = false;
};

// =====================================================================
// StaleRevisionRejected 冲突呈现（§8.5——RV-10）
// =====================================================================

/**
 * @brief 草稿基线冲突对话框数据（§8.5——"显示：当前 tip vs 草稿
 *        baseRevisionId"的呈现值装配；RV-10/AT-29）。
 *
 * 数据来源：StaleRevisionDetailProjection（CommandResultProjection.
 * staleDetail——冲突定位数据由命令结果附带，project.md §6.2）；ui 只
 * 装配不判定（冲突判定权威在对端，§7.5 零业务判定红线）。
 *
 * 三选项固定序（§8.5 原文）：[基于当前版本重新编辑]（推荐——reEditOn
 * CurrentRevision）/ [查看差异对象]（involvedObjectNames 数据面）/
 * [取消]（草稿原样保留，编辑不中断）。对照值（currentTipRevision/
 * draftBaseRevision）为 canonical 文本，呈现层原样对比显示，零二次
 * 加工（UiText 哈希守卫作用于参数面——对照值不进位置参数通道）。
 */
struct StaleConflictDialogData {
    /// 是否存在未决冲突（false＝该模块无冲突——呈现层不渲染）。
    bool present = false;
    /// 模块显示名（UX-02——挂接源 displayName）。
    std::string moduleDisplayName;
    /// 分支当前 tip 修订（canonical 文本——对照值 1）。
    std::string currentTipRevision;
    /// 草稿基线修订（canonical 文本——对照值 2）。
    std::string draftBaseRevision;
    /// 前进修订摘要（人读——对端 RevisionView 摘要原样）。
    std::string advancedSummary;
    /// 涉事对象呈现名（[查看差异对象] 数据面——UX-02 局部名）。
    std::vector<std::string> involvedObjectNames;
    /// 主文案键（kDraftStaleConflictTitleKey）。
    TextKey messageKey;
    /// 动作键：基于当前版本重新编辑（推荐）。
    TextKey reEditActionKey;
    /// 动作键：查看差异对象。
    TextKey viewDiffActionKey;
    /// 动作键：取消。
    TextKey cancelActionKey;
};

// =====================================================================
// 模块草稿源（域编辑器侧接口——§8.2"向域编辑器要文档"的 ui 侧承载；
// 阶段 A 由测试桩模块承载协议验证，§8.3-5 原文）
// =====================================================================

/**
 * @brief 单模块草稿源（域编辑器实现的接入面——§10.5 attachModule 的
 *        第二参数；阶段 B 域插件交付，阶段 A 桩模块验证协议）。
 *
 * 分工纪律（§8.1 红线）：域负载的组装与解析都在域侧——本接口把
 * DraftDocumentProjection 当不透明值搬运（payload 零解析，CR-02/D-10）；
 * 控制器只在落盘分派时刻补齐会话一致性字段（归属三元组/时刻/origin）。
 *
 * 所有权：实现归域编辑器（弱引用语义）——attachModule 后域编辑器必须
 * 保证源存活至 detachModule；析构前未 detach＝调用方契约违约（悬挂引用，
 * 不设第二道防线——§10.5"显式 detach"原文）。
 *
 * 线程：四个方法都只在 UI 线程被控制器调用（落盘任务搬运的是已取出的
 * 文档值拷贝——域源不跨线程）。
 */
class IModuleDraftSource {
public:
    virtual ~IModuleDraftSource() = default;

    /**
     * @brief 模块显示名（UX-02 工程用语——汇总视图/横幅/冲突对话框的
     *        呈现值源）。
     * @return 显示名（UTF-8；禁哈希/内部名）
     */
    virtual std::string displayName() const = 0;

    /**
     * @brief 组装当前草稿文档（§8.2"buildDraftDocument() 触发"——定时/
     *        手动落盘时控制器向域编辑器要文档）。
     * @return 草稿文档投影（moduleId 必须与挂接句柄一致；payload 为域
     *         canonical 字节——ui 不解析；schemaVersion 由域侧填写）
     */
    virtual DraftDocumentProjection buildDraftDocument() const = 0;

    /**
     * @brief 承接恢复的草稿文档（§8.3-5"恢复的草稿进入域编辑器"——
     *        打开期 restoreOnOpen 把磁盘内容交回域侧；阶段 A 登记机制，
     *        桩模块以值拷贝承载协议验证）。
     * @param document [in] 恢复的文档值（Loaded＝current 内容；
     *                 RecoveredFromBackup＝.bak 内容——二者对域侧无区别，
     *                 恢复来源差异由控制器横幅面呈现）
     */
    virtual void adoptRestoredDocument(const DraftDocumentProjection& document) = 0;

    /**
     * @brief 以指定修订重建编辑基线（§8.5[基于当前版本重新编辑]——用户
     *        迁移未应用修改的域侧半区；控制器同时重置会话脏标记）。
     * @param tipRevisionCanonical [in] 分支当前 tip（canonical 文本——
     *                             域侧重建基线的锚）
     */
    virtual void rebuildOnRevision(const std::string& tipRevisionCanonical) = 0;
};

/**
 * @brief 模块草稿句柄（§10.5 attachModule 第一参数——注册表键）。
 *
 * 词表＝注册模块 token（与 DraftDocumentProjection.moduleId、磁盘
 * drafts/&lt;branch&gt;/&lt;module&gt;.draft.json 的 module 段同词表——§4.1 命名
 * 规则：1~64 个 ASCII 字母/数字/下划线/连字符）。别名而非强类型：
 * token 的合法性由挂接入口与落盘分派两处 fail-fast 校验（对端白名单
 * 同口径预检——磁盘路径拼装面，提前拦截），不参与身份计算。
 */
using ModuleDraftHandle = std::string;

// =====================================================================
// 会话绑定与装配依赖（DraftSessionBinding/DraftControllerDeps——
// O-31 注入面与 L5 装配期接线面）
// =====================================================================

/**
 * @brief 草稿控制器的会话绑定（一次成功打开的草稿协作面——与
 *        SessionPortBundle 同款"实例随打开流程注入"形态，O-31）。
 *
 * 为什么不用 SessionPortBundle 直接复用：bundle 的 drafts 成员只有读
 * 半区（IUiDraftQueryPort——UI-T11 关闭对话框呈现面）；草稿控制器还需
 * 写半区（IUiDraftStorePort——本任务冻结）与归属三元组（落盘一致性
 * 校验锚）。绑定值由 L5 在打开成功后装配（§5.2 时序"打开成功→恢复
 * 编排"），bindSession 是唯一注入点。
 *
 * 所有权：两端口 shared_ptr 所有权在 L5/装配层；控制器持有共享引用，
 * 并在分派落盘任务时按值捕获端口拷贝（覆盖在途保存存活期——关闭
 * 上下文后的对端拒绝照常经返回值轨表达）。
 */
struct DraftSessionBinding {
    /// 归属项目（落盘一致性锚——文档 projectId 失配即调用方违约）。
    core::ProjectId projectId{};
    /// 归属分支（drafts/&lt;branch&gt;/ 目录锚；分支切换＝换绑定，草稿
    /// 不跨分支迁移——PM-12）。
    core::BranchId branchId{};
    /// 会话可写位（INV-SES-1 数据源直读——只读会话拒绝挂接写源与
    /// saveAll/discard，§5.5/§10.5 前置行）。
    bool writable = false;
    /// 草稿清单端口（C-5 读半区——汇总合并/恢复编排的磁盘面；必填）。
    std::shared_ptr<IUiDraftQueryPort> drafts;
    /// 草稿写半区端口（save/tryLoad/discard——可写会话必填；只读会话
    /// 允许为空＝无写语义场景）。
    std::shared_ptr<IUiDraftStorePort> store;
};

/**
 * @brief 草稿控制器的装配依赖（createDraftController 注入面——L5
 *        应用壳装配期一次性给出）。
 *
 * 注入纪律（与 UiSessionControllerDeps 同款）：
 *   - postToDiskThread/postToUiThread 必填非空（构造期 fail-fast）：
 *     落盘执行器（§3.4"ui 后台落盘线程（1 条）——串行队列"的宿主；
 *     生产形态＝单工作线程依次执行任务；测试替身可内联或手动步进）与
 *     UI 线程回投（Marshal 纪律——autosave 完成回执经它转回）；
 *   - diagSink/diagFactory/devLog 允许为空＝无目录/无日志测试场景
 *     （须显式声明）；UI-DRAFT-AUTOSAVE-FAILED 在空场景下以返回值/
 *     脏标记承载，不虚构目录条目；
 *   - onSessionDirtyChanged 允许为空＝无壳接线场景：anyDirty 状态
 *     翻转时回调（true/false）——L5 把它接到会话控制器（未保存标记
 *     的生产者接线，UI-T11 登记的"生产者接线随 UI-T12 落地"承诺）；
 *   - steadyClock/nowUtcIso 缺省系统钟；测试注入假时钟确定性驱动
 *     （不使用固定 sleep 判据——§12.3 通用判据）。
 */
struct DraftControllerDeps {
    /// 串行落盘执行器（必填——同一时刻至多一个 save 在途由单线程串行
    /// 保证；§8.4 落盘线程纪律的宿主）。
    std::function<void(std::function<void()>)> postToDiskThread;
    /// UI 线程回投（必填——autosave 完成回执的 Marshal 通道）。
    std::function<void(std::function<void()>)> postToUiThread;
    /// anyDirty 翻转回调（允许为空——L5 接到会话控制器的生产者接线）。
    std::function<void(bool)> onSessionDirtyChanged;
    /// 用户级诊断 sink（UI-DRAFT-AUTOSAVE-FAILED 目录通道；允许为空）。
    std::shared_ptr<diagnostics::IDiagnosticSink> diagSink;
    /// 诊断工厂（create 唯一入口；允许为空＝无目录测试场景）。
    std::shared_ptr<diagnostics::IDiagnosticFactory> diagFactory;
    /// 开发日志通道（Dev 级事实唯一出线；允许为空＝无日志场景）。
    std::shared_ptr<diagnostics::IDevLogSink> devLog;
    /// 单调钟（autosave 周期判定——缺省 steady_clock::now；时长语义
    /// 不用墙钟，墙钟跳变不得影响周期）。
    std::function<std::chrono::steady_clock::time_point()> steadyClock;
    /// 落盘时刻供应（ISO-8601 UTC 文本——文档 savedAtUtc 值源；缺省
    /// 系统钟，测试注入固定值保证确定性留痕）。
    std::function<std::string()> nowUtcIso;
};

// =====================================================================
// IDraftController——§10.5 接口（本任务实现冻结）
// =====================================================================

/**
 * @brief 草稿控制器（§8/§10.5）——未保存标记/草稿投影/会话状态机界面
 *        侧的草稿编排唯一执行者。
 *
 * 生命周期与所有权：由 L5 应用壳持有（unique_ptr）；生命周期覆盖会话
 * （bindSession→unbindSession 可多次——项目切换/关闭后再开）。在途
 * 异步保存（autosave）的完成回执以裸 this 捕获——L5 必须保证控制器
 * 存活至落盘执行器排空（关闭路径 §5.7 的"在途草稿落盘完成后上下文
 * 才释放"由对端在途票据保证，控制器对象由应用壳同步持有）。
 *
 * 错误语义（AGENTS §3 错误二分）：
 *   - 调用方契约违约 → std::logic_error/std::invalid_argument
 *     fail-fast（未绑定会话 saveAll/只读会话 saveAll/重复挂接/未知
 *     模块寻址/moduleId 词法违约/非正周期等——静默降级会制造"以为
 *     保存了/以为没脏"的假象，禁吞错）；
 *   - 环境错误（磁盘/门卫）→ 返回值轨（SaveOutcome/DraftSaveOutcome，
 *     稳定 token 透传）＋autosave 失败的 UI-DRAFT-AUTOSAVE-FAILED
 *     目录条目（§3.5 码表）。
 */
class IDraftController {
public:
    virtual ~IDraftController() = default;

    // ---- 会话绑定（§5.2 打开/关闭时序的草稿侧——L5 编排入口）----

    /**
     * @brief 绑定打开成功的会话（§5.2"打开协议⑤步后"的草稿侧编排起点；
     *        restoreOnOpen 的前置）。
     *
     * 绑定重置模块表（新会话零会话脏——上一会话的表状态不跨会话存活；
     * §8.6 切换处置"会话脏数据丢弃"的表半区）。重复绑定须先 unbind
     * （否则 logic_error——状态机界面侧不接纳叠绑）。
     *
     * @param binding [in] 会话绑定（writable=true 时 store 必填非空；
     *                drafts 必填非空——缺清单端口的草稿控制器无从汇总，
     *                装配错误 fail-fast）
     *
     * @throws std::logic_error 已有绑定未解除；或 writable=true 而 store
     *         为空；或 drafts 为空（装配违约）
     */
    virtual void bindSession(const DraftSessionBinding& binding) = 0;

    /**
     * @brief 解除会话绑定（§8.6 关闭/切换的表处置——模块表/局部撤销栈/
     *        冲突态整体清空；磁盘草稿零触碰——"磁盘草稿不删除，下次
     *        打开仍可恢复"原文）。
     *
     * 未绑定调用＝幂等空操作（关闭后再解除的编排容错）。
     */
    virtual void unbindSession() = 0;

    /**
     * @brief 是否已绑定会话（restoreOnOpen/saveAll 的前置观测面）。
     */
    virtual bool hasSession() const noexcept = 0;

    // ---- 模块挂接（§10.5 attach/detach——域编辑器接入，阶段 B 交付；
    //      阶段 A 桩模块验证协议）----

    /**
     * @brief 挂接模块草稿源（§10.5 attachModule——ModuleDraftTable 注册）。
     *
     * 前置（§10.5 前置行原文）：模块 id 未占用且会话可写（只读会话拒绝
     * 挂接写源；未绑定会话无从谈可写——一并拒绝）。displayName 取自源
     * （注册时刻定格——域显示名稳定，汇总视图不因时刻漂移）。
     *
     * @param handle [in] 模块句柄（ModuleDraftHandle——注册 token 词法
     *               校验：1~64 个 ASCII 字母/数字/下划线/连字符，磁盘
     *               路径拼装面提前拦截，对端白名单同口径）
     * @param source [in] 域草稿源（弱引用——调用方保证存活至 detach）
     *
     * @throws std::logic_error 未绑定会话/只读会话/句柄已占用（§5.5
     *         只读禁用与 §10.5 前置行的执行点）
     * @throws std::invalid_argument 句柄空/词法违约
     */
    virtual void attachModule(const ModuleDraftHandle& handle,
                              IModuleDraftSource& source) = 0;

    /**
     * @brief 摘除模块草稿源（§10.5 detachModule——域编辑器销毁前的
     *        显式解除；表条目/撤销栈/冲突态一并清除）。
     *
     * @param handle [in] 模块句柄（未知句柄＝幂等空操作——域侧重复
     *               摘除的编排容错）
     */
    virtual void detachModule(const ModuleDraftHandle& handle) = 0;

    /**
     * @brief 通知会话脏（§8.2"会话编辑态 dirty 通知"的入口——域编辑器
     *        编辑发生后调用；脏代次递增保证"save 期间的新脏数据进入
     *        下一周期"的判定锚）。
     *
     * @param handle [in] 模块句柄（未知句柄＝logic_error——路由违约）
     *
     * @throws std::logic_error 句柄未挂接
     */
    virtual void notifySessionDirty(const ModuleDraftHandle& handle) = 0;

    // ---- 汇总（§8.2——未保存标记与跨模块汇总）----

    /**
     * @brief 跨模块草稿汇总（§10.5 summary——标题 `*` 判定位 anyDirty
     *        ＝会话脏 OR 磁盘 present 的唯一实现点；模块视图合并规则见
     *        DraftingSummary）。磁盘行现取现合（IUiDraftQueryPort 短
     *        查询——UI 线程，§10.5 线程行）。
     */
    virtual DraftingSummary summary() const = 0;

    /**
     * @brief 是否存在未应用修改（summary().anyDirty 的直捷读出——
     *        anyDirtyChanged 回调同源，单一事实不二算）。
     */
    virtual bool anyUnappliedChanges() const = 0;

    // ---- 保存（§8.2/§8.4——保存/应用分离红线：零修订）----

    /**
     * @brief 全量保存挂接的脏模块（§10.5 saveAll——手动/关闭对话框的
     *        同步入口；单模块＝单元素路径）。
     *
     * 执行序（§8.2 数据流逐框）：收集脏模块→逐模块 buildDraftDocument
     * （UI 线程，纯内存）→控制器补齐会话一致性字段（归属三元组/时刻/
     * origin）→经落盘执行器调用 save（**磁盘段全在落盘线程**）→阻塞
     * 等待结局→成功且期间无新编辑清脏标记（脏代次守卫——§8.4"save
     * 期间的新脏数据进入下一周期"）→失败保留脏标记＋firstError。
     *
     * 保存/应用分离（PM-04 红线）：本方法只落 drafts/、**零修订**——
     * 控制器不持有命令网关，结构上不存在"保存顺手提交"的通道（应用＝
     * 域编辑器经 IUiCommandGateway 提交，§8.1 分工红线）。
     *
     * @param trigger [in] 触发源（Manual/CloseDialog＝origin=manual
     *                落盘语义；Autosave＝origin=autosave——同一落盘
     *                入口，§8.4"共用 DraftService 同一入口"）
     * @return 保存结局（savedCount/failedCount/firstError——呈现层
     *         即时反馈与关闭对话框失败中止的数据面）
     *
     * @throws std::logic_error 未绑定会话/只读会话（§5.5 draft.save
     *         禁用——只读调用属契约违约，fail-fast 而非返回全败）
     */
    virtual SaveOutcome saveAll(SaveTrigger trigger) = 0;

    // ---- 打开恢复（§8.3——打开协议⑤步后）----

    /**
     * @brief 打开期草稿恢复编排（§10.5 restoreOnOpen；§8.3 五步逐行：
     *        list→逐模块 tryLoad→损坏 .bak 回退→恢复进域编辑器→孤儿
     *        汇总）。§10.5 原文形态的对端参数按 O-31 裁决改为会话绑定
     *        注入（bindSession——登记 ui.md §16.7 v1.4）。
     *
     * @return 恢复报告（restored/backupRecovered/corrupt/orphan/newResidue
     *         五面明细——recoveryBanner 的直接输入；呈现层据 corrupt
     *         Recovered() 渲染"已从备份恢复"提示）
     *
     * @throws std::logic_error 未绑定会话（前置：store 处于 Open* 态——
     *         绑定即打开成功的草稿侧表达）
     */
    virtual RestoreOutcome restoreOnOpen() = 0;

    /**
     * @brief 装配恢复横幅数据（PM-08/PM-15——restoreOnOpen 结局的呈现
     *        值；纯装配，不触端口）。
     *
     * @param outcome [in] restoreOnOpen 的结局值
     * @return 横幅投影（present=false＝无恢复事件，不渲染；[放弃]可用
     *         位＝当前会话可写——§8.3-4 写操作门控）
     */
    virtual DraftRecoveryBannerProjection
    recoveryBanner(const RestoreOutcome& outcome) const = 0;

    /**
     * @brief 放弃一份草稿（§8.3-4 横幅[放弃]——显式 discard，写操作；
     *        同步等待落盘线程完成——与 saveAll 同一线程纪律）。
     *
     * @param handle [in] 模块句柄（孤儿草稿的 token 亦可寻址——横幅
     *               放弃的对象通常无挂接源）
     * @return 放弃结局（ok/errorToken——失败时横幅就地反馈，草稿原样）
     *
     * @throws std::logic_error 未绑定会话/只读会话（§8.3-4"只读模式
     *         不可用"——调用面契约违约 fail-fast）
     */
    virtual DraftDiscardOutcome discardDraft(const ModuleDraftHandle& handle) = 0;

    // ---- 定时保存（§8.4——定时器与线程纪律）----

    /**
     * @brief 定时保存心跳（§8.4 定时器的控制器半区——L5 的 UI 线程
     *        定时器按 autosaveInterval() 周期调用；触发时只做"收集脏
     *        模块→要文档→投落盘线程"，分派即返回——UI 线程零磁盘 IO）。
     *
     * 纪律：距上次分派不足一个周期＝忽略（双触发防御）；有在途落盘
     * ＝本轮跳过（落盘线程串行——§8.4"save 期间的新脏数据进入下一
     * 周期"）；只读/未绑定会话＝零分派（只读不落盘，§5.5）。
     * 落盘成败经完成回执消费（成功且无新编辑清脏/失败保留脏标记＋
     * UI-DRAFT-AUTOSAVE-FAILED——§8.2 成败两行）。
     *
     * @param now [in] 当前单调时刻（steadyClock 同源——测试注入假时钟
     *            确定性驱动）
     * @return 本次分派的保存任务数（0＝周期未到/有在途/无脏模块）
     */
    virtual std::size_t tickAutosave(std::chrono::steady_clock::time_point now) = 0;

    /**
     * @brief 设置定时保存周期（§10.5 setAutosaveInterval——60 s 默认；
     *        PM-04-S1 用户可调 60–600 s 归 WP-04-T20，本任务不提前实现
     *        调节界面——编程面接受任意正周期，装配/用户面的范围钳制
     *        归该任务）。
     *
     * @param interval [in] 周期（正；语义单位秒——AGENTS §2.5 单位显式）
     *
     * @throws std::invalid_argument 非正值（周期无"立即"语义——双触发
     *         防御要求正周期）
     */
    virtual void setAutosaveInterval(std::chrono::seconds interval) = 0;

    /**
     * @brief 当前定时保存周期（§10.5 autosaveInterval——默认 60 s，
     *        PM-04-S1 原文；装配/测试可改，单位秒）。
     */
    virtual std::chrono::seconds autosaveInterval() const noexcept = 0;

    // ---- 应用回执（§8.5——StaleRevisionRejected 冲突处理入口）----

    /**
     * @brief 消费草稿应用回执（§10.5 onCommandResult——域编辑器经命令
     *        端口提交后的结果路由点，按模块寻址）。
     *
     * 分支表（§8.5/§8.6 逐行）：
     *   - Committed：应用成功＝草稿被消费（§8.6"应用成功后磁盘草稿由
     *     project 侧处置；ui 清会话脏标记"）——清脏＋清局部撤销栈
     *     （§8.7"局部撤销不能撤销已应用修订"）＋记录已应用修订；
     *   - Rejected(stale-revision)：冲突未决——**草稿保留**（脏标记
     *     保持；§8.5"拒绝永远不销毁草稿"）＋暂存冲突定位数据（对话
     *     框数据面）；不自动重试（结构保证：控制器无命令通道）；
     *   - 其余结局（其他拒绝/中止/失败）：草稿原样保留（编辑不中断），
     *     Dev 日志留痕——呈现反馈归调用方。
     *
     * @param handle [in] 模块句柄（应用回执的寻址键——路由方保证与
     *               提交的草稿模块一致）
     * @param result [in] 命令结果投影（staleDetail 仅
     *               Rejected("stale-revision") 时非空——presence 纪律）
     *
     * @throws std::logic_error 句柄未挂接（路由违约）
     */
    virtual void onCommandResult(const ModuleDraftHandle& handle,
                                 const CommandResultProjection& result) = 0;

    /**
     * @brief 装配基线冲突对话框数据（§8.5——"当前 tip vs 草稿
     *        baseRevisionId"＋三选项键；onCommandResult 暂存的定位
     *        数据的呈现值）。
     *
     * @param handle [in] 模块句柄
     * @return 对话框数据（present=false＝该模块无未决冲突——呈现层
     *         只应在冲突未决时渲染；不抛，呈现安全）
     */
    virtual StaleConflictDialogData
    staleConflictData(const ModuleDraftHandle& handle) const = 0;

    /**
     * @brief 执行[基于当前版本重新编辑]（§8.5 冲突决议——域源以当前
     *        tip 重建编辑基线、控制器重置该模块会话脏标记与冲突态；
     *        **不自动重试提交**——用户迁移后的再提交是新的显式动作）。
     *
     * @param handle [in] 模块句柄（须有未决冲突——无冲突调用＝路由
     *               违约 fail-fast）
     *
     * @throws std::logic_error 句柄未挂接/无未决冲突
     */
    virtual void reEditOnCurrentRevision(const ModuleDraftHandle& handle) = 0;

    // ---- 草稿局部撤销栈（§8.7——两栈边界的会话半区）----

    /**
     * @brief 压入局部撤销检查点（§8.7"草稿内编辑级局部撤销……不经
     *        命令服务、不产生修订"——域编辑器完成一次编辑确认后调用，
     *        快照当前文档作为回退点；压栈使该模块重做栈失效——标准
     *        撤销栈纪律）。
     *
     * @param handle [in] 模块句柄（未知句柄＝logic_error）
     *
     * @throws std::logic_error 句柄未挂接
     */
    virtual void pushLocalCheckpoint(const ModuleDraftHandle& handle) = 0;

    /**
     * @brief 草稿局部撤销（§8.7 会话栈半区——回退到上一检查点，经
     *        adoptRestoredDocument 交回域侧；零命令提交零修订，结构
     *        保证与项目命令撤销栈互不越界）。
     *
     * @param handle [in] 模块句柄
     * @return true＝已回退；false＝栈空（无可撤销——应用成功后栈已
     *         清空，"局部撤销不能撤销已应用修订"的执行面）
     *
     * @throws std::logic_error 句柄未挂接
     */
    virtual bool undoLocalEdit(const ModuleDraftHandle& handle) = 0;

    /**
     * @brief 草稿局部重做（§8.7 会话栈半区——恢复被撤销的检查点；
     *        新检查点压入时重做栈失效）。
     *
     * @param handle [in] 模块句柄
     * @return true＝已重做；false＝重做栈空
     *
     * @throws std::logic_error 句柄未挂接
     */
    virtual bool redoLocalEdit(const ModuleDraftHandle& handle) = 0;
};

/**
 * @brief 创建草稿控制器（L5 应用壳装配入口——实现类型不进公共头，
 *        R-2 私有头纪律同款）。
 *
 * @param deps [in] 装配依赖（见 DraftControllerDeps——postToDiskThread/
 *             postToUiThread 必填，构造期 fail-fast）
 * @return 控制器实例（调用方持有）
 *
 * @throws std::invalid_argument 必填执行器为空（没有落盘执行器的
 *         控制器违反 UI 线程零磁盘 IO 红线——装配期拦截）
 */
std::unique_ptr<IDraftController> createDraftController(DraftControllerDeps deps);

}  // namespace ui
}  // namespace ird

#endif  // SDURWS_IRD_UI_IDRAFTCONTROLLER_HPP
