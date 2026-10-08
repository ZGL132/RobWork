/**
 * @file   Projection.hpp
 * @brief  标题栏/状态栏与恢复横幅的状态投影数据面（PM-11/PM-15）——
 *         IStatusProjectionProvider（O9）＋两取数端口接缝＋纯函数组装核。
 *
 * 设计依据：
 *   - units/workflow.md §7.6（标题栏 `<显示名>[*][（只读）]`；状态栏＝
 *     项目名、当前方案、结果状态（是否过期——当前性投影）、未保存标记
 *     与只读后缀；恢复横幅三场景〔①未完成保存已忽略（.staging 残留）
 *     ②任务已中断（execution Interrupted 条目）③检测到未保存草稿
 *     （孤儿草稿）〕一句话汇总＋查看详情/恢复草稿/放弃三动作；横幅是
 *     **呈现编排**——状态数据编排归本单元 O9，横幅控件归 ui）、§10.1/
 *     §10.2（接口总表 IStatusProjectionProvider 行＋Draft 签名——
 *     titleStatus()/recoveryBanner() 本头逐字兑现）、§10.3（接口属性表：
 *     titleStatus/recoveryBanner const 并发安全；错误语义＝调用方错误
 *     fail-fast；词表零新增持久化枚举）、§9（对端协作行：ui 横幅/标题栏
 *     宿主、project 恢复诊断数据、execution Interrupted 条目、diagnostics
 *     统一诊断目录条目＝横幅的问题数据源）
 *   - REQUIREMENTS.md §17 PM-11 原文（"标题栏与状态栏显示项目名、当前
 *     方案、结果状态（是否过期）、未保存标记与只读后缀，格式
 *     `<显示名>[*][（只读）]`"；属性面板/改名/体积为 R2 子项 PM-11-S1~
 *     S3——本批不落位）、PM-15 原文（"项目管理界面诊断经统一诊断目录
 *     集成（恢复横幅、诊断表、日志）；恢复横幅一句话汇总（未完成保存
 *     已忽略/任务已中断/检测到未保存草稿）＋查看详情/恢复草稿/放弃"）、
 *     AT-21（残留草稿恢复、中断任务提示）
 *   - 任务契约 tasks/foundation/WP-22-T09.json acceptance 1/2/3（格式与
 *     状态来源用例；横幅三场景用例＋统一诊断目录集成；P-DIAG-7 工程默认
 *     未校准期间按保守默认＋留痕）
 *
 * 背景说明（本头为什么是"投影数据面"而不是"控件/判定面"）：workflow 是
 * 编排单元——标题栏与横幅呈现的**控件**归 ui 宿主面（D-WF-6），本头只
 * 承载呈现所需的**状态数据**：把五路来源事实（项目显示名/写权限/活动
 * 方案/草稿脏标记/当前性投影）与三场景恢复事实编排为键＋值半区齐备的
 * 投影值，ui 宿主面拿到即可渲染（键经 ui UiText 文案表解析——UX-02
 * 键/值半区分工，本头零中文文案）。三场景的**事实判定**也归对端：
 * .staging 残留与孤儿草稿归 project 打开⑤步恢复扫描（RecoveryReport）、
 * 中断任务归 execution 恢复期重建（Interrupted 终态条目）、统一诊断目录
 * 归 diagnostics——本单元经取数端口折叠（PA-1 不复制对端判定）。
 */

/*
 * 实现口径说明（WP-22-T09，DTB §5.4 已随单元卡 §10.2 v1.0 登记段同步）：
 *   - 取数走**注入端口**而非直连对端服务：标题栏五路事实分属 project
 *     （显示名/writable）、会话层（活动方案分支）、ui DraftController
 *     （anyDirty——PM-11 明文"数据归 ui/project"）与当前性投影（evidence
 *     权威经 ui C-7 值投影搬运），恢复横幅三场景事实经统一诊断目录折叠
 *     （PM-15"经统一诊断目录集成"）——L5 装配层持有对端实例并桥接端口，
 *     编排核零对端服务依赖（IExternalRelinkPort/ICloseDrainPort 同款
 *     "端口即接缝"先例）；测试以脚本化桩/真实落盘联合双轨承载。
 *   - 文案键**复用 ui UiText 冻结词表**（SA-12 单一权威）：恢复横幅六键
 *     与 ui 恢复横幅装配面（assembleRecoveryBanner——UI-T13 落位）同串
 *     同值（"ui.recovery.banner.*"族）；本头常量是键串的 workflow 侧
 *     登记位，值权威与改动权归 ui 文案表——两卡增量同步义务（改键须
 *     workflow.md/ui.md 双登记）。标题栏/状态栏其余键（只读后缀/结果
 *     状态两态）为本头新增键半区（值归 ui 文案表，PM-11 字段的键化
 *     承载——键形遵 ui.md §3.5 体系）。
 *   - 三场景择一序＝**行动紧迫度**：未完成保存已忽略＞任务已中断＞
 *     未保存草稿（与 ui 恢复横幅装配面同序——同一句 PM-15 的两面对齐，
 *     主文案一句话，其余事实降级详情行不堆叠）。
 *   - 线程约束：titleStatus() 为 const——要求 ITitleFactPort::collectFacts()
 *     const 可并发；recoveryBanner() 非 const——诊断目录折叠含消费登记
 *     语义（会话内单线程使用，§10.3 流程编排行同款纪律）。
 */

#ifndef SDURWS_IRD_WORKFLOW_PROJECTION_HPP
#define SDURWS_IRD_WORKFLOW_PROJECTION_HPP

#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/ui/UiProjections.hpp>  // ui::CurrentnessProjection——当前性投影值载体（C-7 O-31 投影搬运，零重算）
#include <sdurws/ird/ui/UiTypes.hpp>        // ui::TextKey——文案键类型（ui 词表唯一权威，D-WF-4 零新增类型）

namespace sdurws {
namespace ird {
namespace workflow {

// =====================================================================
// 文案键常量（键半区——值权威归 ui UiText 文案表，UX-02 键/值半区分工）
// =====================================================================

/**
 * @brief 标题栏"（只读）"后缀文案键（PM-11 冻结格式的后缀半区）。
 *
 * 键串为本头新增键半区（值归 ui 文案表）：PM-11 格式串
 * `<显示名>[*][（只读）]` 中 `（只读）` 是用户可见文案——经本键由 ui
 * 文案表解析（与 ui 只读会话提示 kReadOnlyExplicitKey 族同域不同键：
 * 那是横幅提示语，本键是标题后缀短词）。buildTitleText() 的后缀文本
 * 参数由调用方自该键解析后传入——本单元零硬编码中文（UX-02）。
 */
inline constexpr const char* kTitleReadonlySuffixKey = "title.status.readonly-suffix";

/**
 * @brief 状态栏"结果状态"字段——当前文案键（结果切片与目标上下文身份
 *        相等——evidence CurrentnessStatus::Current 的呈现半区）。
 */
inline constexpr const char* kResultsStatusLabelCurrentKey = "status.bar.results-current";

/**
 * @brief 状态栏"结果状态"字段——已过期文案键（Superseded 与"不可判定"
 *        共用——P-UI-2：不可判定归入 results-stale 呈现，不显示为当前、
 *        不显示为通过；evidence"无默认 Current"纪律的呈现面兑现）。
 */
inline constexpr const char* kResultsStatusLabelStaleKey = "status.bar.results-stale";

/**
 * @brief 状态栏"当前方案"字段标签文案键（PM-11 状态栏"当前方案"字段——
 *        字段名键；字段值＝活动分支 label 原文透传，不经键承载）。
 */
inline constexpr const char* kStatusBarSchemeLabelKey = "status.bar.scheme-label";

/// 恢复横幅场景①汇总键（"未完成保存已忽略"——ui.recovery.banner.summary.
/// ignored-saves 同串同值；值权威归 ui UiText，SA-12 两卡同步）。
inline constexpr const char* kBannerSummaryIgnoredSaveKey = "ui.recovery.banner.summary.ignored-saves";

/// 恢复横幅场景②汇总键（"任务已中断"——ui 同串同值；作详情行时复用
/// 本键——横幅只一句话主文案，其余事实降级详情行不堆叠）。
inline constexpr const char* kBannerSummaryInterruptedKey = "ui.recovery.banner.summary.interrupted";

/// 恢复横幅场景③汇总键（"检测到 {0} 份未保存草稿"——{0}＝孤儿草稿
/// 计数占位；ui 同串同值）。
inline constexpr const char* kBannerSummaryOrphanDraftKey = "ui.recovery.banner.summary.orphan-drafts";

/// 恢复横幅动作一文案键（"查看详情"——跳诊断表详情；ui 同串同值）。
inline constexpr const char* kBannerActionDetailsKey = "ui.recovery.banner.action.details";

/// 恢复横幅动作二文案键（"恢复草稿"——载入孤儿草稿；ui 同串同值）。
inline constexpr const char* kBannerActionRestoreKey = "ui.recovery.banner.action.restore";

/// 恢复横幅动作三文案键（"放弃"——显式 discard 孤儿草稿；ui 同串同值）。
inline constexpr const char* kBannerActionDiscardKey = "ui.recovery.banner.action.discard";

// =====================================================================
// 恢复场景词表（PM-15 三场景——会话态枚举，§10.3"零新增持久化枚举"
// 边界内：scenario 只存在于一次横幅编排的生命周期内，不写入任何
// 持久化 schema；与 ui 恢复横幅装配面的三事实位一一对应）
// =====================================================================

/**
 * @brief 恢复横幅场景词表（PM-15 三场景的语义身份——机器可判读、测试
 *        可断言；用户文案经 recoverySummaryKey() 得键、再经 ui UiText
 *        解析——UX-02 键/值半区分工）。
 */
enum class RecoveryScenario : std::uint8_t {
    /// 场景①：未完成保存已忽略（.staging 残留——project 打开⑤步恢复
    /// 扫描 RecoveryReport.ignoredStagingTxs 非空；现场保留不删）。
    IgnoredUnfinishedSave = 0,
    /// 场景②：任务已中断（execution 恢复期重建的 Interrupted 终态条目
    /// 非空——EX-TASK-INTERRUPTED 状态标注诊断随条目累积，"已中断可重跑"）。
    InterruptedTask = 1,
    /// 场景③：检测到未保存草稿（孤儿/损坏草稿——RecoveryReport.
    /// orphanDraftFiles 非空；PRJ-RECOVERY-ORPHAN-DRAFT 随报告）。
    OrphanDraft = 2,
};

/**
 * @brief 场景 → 一句话汇总文案键（唯一映射点，NFR-MNT-03——禁止调用方
 *        自拼键串）。
 *
 * @param scenario [in] 场景值（三值封闭词表）
 * @return 汇总文案键（kBannerSummary* 三常量之一；词表外值防御性返回
 *         空串——不伪造键，与 adviceTitleKey 同款未知 token 口径）
 */
std::string recoverySummaryKey(RecoveryScenario scenario);

// =====================================================================
// 标题栏/状态栏事实端口（取数接缝——实现归 L5 装配层，本单元不实现）
// =====================================================================

/**
 * @brief 标题栏/状态栏的五路来源事实（titleStatus() 的取数值——L5 端口
 *        实现从各权威对端收集后折叠，编排核零加工透传）。
 *
 * 字段级来源锚（PM-11"来源"的逐路对应——acceptance 1 用例的断言面）：
 *   - displayName＝项目显示名，权威 project（ProjectMetadataRecord.
 *     projectDisplayName——经 store->query().currentMetadata() 取数）；
 *   - writable＝写权限事实，唯一判定源 store->writable()（INV-SES-1；
 *     false＝标题"（只读）"后缀与状态栏只读标记的事实依据——PM-07
 *     降级/显式只读两种形态都经此位表达，编排面不区分成因）；
 *   - schemeLabel＝当前方案，权威会话层活动分支登记（分支 label 经
 *     store->query().branchTips() 取数——活动分支身份由宿主会话层
 *     持有，PM-12"切换＝纯会话选择"）；
 *   - anyDirty＝未保存标记（标题 `*`），权威 ui DraftController 汇总位
 *     （DraftingSummary.anyDirty＝会话脏∨磁盘草稿在——PM-11 明文
 *     "数据归 ui/project"；本单元不重算两源合并）；
 *   - resultsCurrentness＝当前性投影（结果状态"是否过期"的事实载体），
 *     权威 evidence 判定、经 ui C-7 值投影搬运（CurrentnessProjection
 *     ——status==nullopt＝不可判定计算形态，不得当作 Current 使用；
 *     本单元零当前性计算——evidence 权威，PA-1）。
 *
 * 值语义；线程安全：纯值（端口每次调用返回完整快照——一致性归组装方，
 * ui C-7 端口"快照一致性归组装方"同款纪律）。
 */
struct TitleFacts {
    /// 项目显示名（project 权威透传；UTF-8——UX-02 工程用语呈现值）。
    std::string displayName;
    /// 写权限事实（true＝可写会话；false＝只读会话——INV-SES-1 唯一判定源）。
    bool writable = true;
    /// 当前方案显示名（活动分支 label 透传；无活动分支＝空串——无项目
    /// 会话的标题状态数据不渲染，见 buildTitleStatus 前置）。
    std::string schemeLabel;
    /// 未保存标记汇总位（任一模块会话脏∨磁盘草稿在——PM-11 标题 `*`）。
    bool anyDirty = false;
    /// 当前性投影（搬运不计算——status/reasons 字段语义锚 evidence §8.1）。
    ui::CurrentnessProjection resultsCurrentness;

    /// 逐字段相等（含当前性投影全量——status/原因/逐条失效原因三元组；
    /// ui::CurrentnessProjection 未定义 operator==，此处逐字段比较，
    /// 字段集与对端投影一致——投影演进时本比较须同步）。
    bool operator==(const TitleFacts& o) const
    {
        if (!(displayName == o.displayName && writable == o.writable
              && schemeLabel == o.schemeLabel && anyDirty == o.anyDirty
              && resultsCurrentness.status == o.resultsCurrentness.status
              && resultsCurrentness.unevaluableCause
                     == o.resultsCurrentness.unevaluableCause
              && resultsCurrentness.reasons.size()
                     == o.resultsCurrentness.reasons.size())) {
            return false;
        }
        // 逐条失效原因比较（ui::CurrentnessProjection::Reason 三字段——
        // dependencyKey/kindToken/detail；CurrentnessResult.reasons 的
        // (kind,key) 字典序由 evidence 侧保证，此处按序逐条比对）。
        for (std::size_t i = 0; i < resultsCurrentness.reasons.size(); ++i) {
            const auto& a = resultsCurrentness.reasons[i];
            const auto& b = o.resultsCurrentness.reasons[i];
            if (a.dependencyKey != b.dependencyKey || a.kindToken != b.kindToken
                || a.detail != b.detail) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const TitleFacts& o) const { return !(*this == o); }
};

/**
 * @brief 标题栏/状态栏事实收集端口（PM-11 五路来源的取数接缝）。
 *
 * 谁实现：L5 装配层——从 project store（显示名/writable/分支 label）、
 * 宿主会话层（活动分支身份）、ui DraftController（anyDirty）与 ui C-7
 * 当前性源（currentness()）收集折叠。本单元是消费方，不实现端口
 * （编排单元不拥有投影设施——D-WF-1 四段职责链对端面）。
 *
 * 错误语义：实现方对环境失败（上下文已关闭等）自行折叠为安全缺省
 * （如空串/false）——标题栏是常驻呈现面，不允许单次取数失败炸掉宿主；
 * 调用方错误（本单元误用）不在端口面（无参无返回轨违约空间）。
 */
class ITitleFactPort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~ITitleFactPort() = default;

    /**
     * @brief 收集标题栏/状态栏五路事实（每次调用返回完整快照）。
     *
     * @return 事实快照（值拷贝——快照投影纪律；无项目会话返回全缺省值，
     *         调用方以 displayName 为空判断"无标题可渲染"）
     *
     * @threadSafe const 只读——titleStatus() 的并发安全由本方法保证
     *            （§10.3 titleStatus 行 const 并发安全的对端承诺）。
     */
    virtual TitleFacts collectFacts() const = 0;
};

/**
 * @brief 单场景恢复事实（统一诊断目录折叠后的编排面值——场景＋计数）。
 *
 * itemCount 语义：该场景的涉事条目计数（.staging 残留事务数/中断任务
 * 条目数/孤儿草稿文件数；单位＝条，无物理量纲）——一句话汇总的 {0}
 * 参数素材（场景③键含计数占位）与"查看详情"的量级暗示。诊断目录的
 * 去重折叠计数（DiagProjectionItem.occurrences）不等于本计数：目录
 * 折叠的是"同码重复上报"，本计数是"涉事条目数"——两口径不得混用，
 * L5 折叠实现以对端清单事实（RecoveryReport 清单长度/中断条目数）为准。
 *
 * 值语义。
 */
struct RecoveryScenarioFact {
    /// 场景值（三值封闭词表）。
    RecoveryScenario scenario{};
    /// 涉事条目计数（单位＝条；恒 ≥1——0 计数场景不入清单，见
    /// RecoveryFacts.scenarios 契约）。
    std::size_t itemCount = 0;

    bool operator==(const RecoveryScenarioFact& o) const noexcept
    {
        return scenario == o.scenario && itemCount == o.itemCount;
    }
    bool operator!=(const RecoveryScenarioFact& o) const noexcept
    {
        return !(*this == o);
    }
};

/**
 * @brief 恢复横幅场景事实集（recoveryBanner() 的取数值——统一诊断目录
 *        折叠产物）。
 *
 * scenarios 契约：**仅含 itemCount ≥ 1 的场景**（无事实的场景不入清单
 * ——横幅不虚构恢复叙事，与 ui"三事实皆空→不渲染"同口径）；同一场景
 * 不得重复出现（折叠端口的一次性输出，重复＝调用方拼装违约）。空清单
 * ＝无任何恢复事实（buildRecoveryBanner 返回 nullopt）。
 *
 * writable 语义：会话写权限事实（与 TitleFacts.writable 同源同值——
 * store->writable()），"放弃"动作可用性的判定输入（放弃＝显式 discard
 * ＝写操作，只读会话禁用——ui §8.3-4 口径；恢复草稿＝载入查看，只读
 * 会话不禁——可查看可恢复，编辑禁令归对端写入口）。
 *
 * 值语义。
 */
struct RecoveryFacts {
    /// 会话写权限（放弃动作可用性输入——true＝可写会话）。
    bool writable = true;
    /// 非空场景清单（itemCount ≥ 1；无重复场景；字典外次序无语义——
    /// 择一序由 buildRecoveryBanner 的冻结紧迫度序决定，不依赖清单序）。
    std::vector<RecoveryScenarioFact> scenarios;
};

/**
 * @brief 恢复场景事实收集端口（PM-15 三场景的取数接缝——统一诊断目录
 *        的 workflow 侧视图）。
 *
 * 谁实现：L5 装配层——从统一诊断目录（DiagCatalog::snapshot 查询恢复
 * 类条目：PRJ-RECOVERY-IGNORED-UNCOMMITTED→场景①、PRJ-RECOVERY-ORPHAN-
 * DRAFT→场景③、EX-TASK-INTERRUPTED→场景②）与会话写权限事实折叠。
 * "经统一诊断目录集成"（PM-15 原文）的结构兑现点：横幅事实**只**经
 * 目录进入本单元（不直连 project RecoveryReport/execution 记录——
 * 打开期对端已把恢复诊断上报目录，横幅取数与诊断表/日志同源，PA-1
 * 不绕开统一通道）。
 *
 * 错误语义：目录不可达等环境失败由实现方折叠为空清单（横幅缺省＝不
 * 渲染——呈现面不允许单次取数失败炸宿主，ITitleFactPort 同款纪律）。
 *
 * 线程约束：会话内单线程（诊断目录快照含消费登记语义——非并发承诺面，
 * §10.3 recoveryBanner 行主线程会话内纪律）。
 */
class IRecoveryFactPort {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~IRecoveryFactPort() = default;

    /**
     * @brief 收集恢复场景事实（统一诊断目录折叠快照）。
     *
     * @return 事实集（空 scenarios＝无恢复事实——横幅不渲染）
     */
    virtual RecoveryFacts collectFacts() = 0;
};

// =====================================================================
// 投影值类型（§10.2 Draft 名逐字兑现——TitleStatusData/RecoveryBannerData）
// =====================================================================

/**
 * @brief 标题栏/状态栏投影数据（PM-11 的呈现值装配——ui 宿主面按需渲染）。
 *
 * 三段式字段组织（与 §7.6 语义逐项对应）：
 *   - 事实半区：displayName/dirty/readOnly/schemeLabel/resultsCurrentness
 *     ——呈现值的素材（buildTitleText() 的输入与状态栏字段的取值源）；
 *   - 键半区：readonlySuffixKey/schemeLabelKey/resultsStatusLabelKey
 *     ——ui 文案表解析入口（UX-02：键/值半区分工，零硬编码文案）；
 *   - 组装函数：buildTitleText()（标题串唯一组装点——PM-11 冻结格式）。
 *
 * 值语义；线程安全：纯值。
 */
struct TitleStatusData {
    // ---- 事实半区 ----
    /// 项目显示名（PM-11 格式 `<显示名>` 位——project 权威透传；空串＝
    /// 无项目会话，宿主面不渲染标题状态）。
    std::string displayName;
    /// 未保存标记（PM-11 格式 `*` 位——true＝有未应用修改）。
    bool dirty = false;
    /// 只读后缀位（PM-11 格式 `（只读）` 位——true＝只读会话）。
    bool readOnly = false;
    /// 当前方案显示名（状态栏"当前方案"字段值——活动分支 label 透传）。
    std::string schemeLabel;
    /// 当前性投影（状态栏"结果状态（是否过期）"的事实载体——搬运不
    /// 计算；reasons 逐条透传供"过期附原因"的呈现扩展，UX-10）。
    ui::CurrentnessProjection resultsCurrentness;

    // ---- 键半区（ui 文案表解析入口——值权威归 ui）----
    /// 只读后缀文案键（kTitleReadonlySuffixKey——恒填充）。
    std::string readonlySuffixKey;
    /// "当前方案"字段标签键（kStatusBarSchemeLabelKey——恒填充）。
    std::string schemeLabelKey;
    /// 结果状态文案键（kResultsStatusLabelCurrentKey/StaleKey 二选一——
    /// 按 resultsCurrentness.status 选键，不可判定归入 stale——P-UI-2）。
    std::string resultsStatusLabelKey;

    bool operator==(const TitleStatusData& o) const
    {
        return displayName == o.displayName && dirty == o.dirty
            && readOnly == o.readOnly && schemeLabel == o.schemeLabel
            && readonlySuffixKey == o.readonlySuffixKey
            && schemeLabelKey == o.schemeLabelKey
            && resultsStatusLabelKey == o.resultsStatusLabelKey;
    }
    bool operator!=(const TitleStatusData& o) const { return !(*this == o); }
};

/**
 * @brief 恢复横幅投影数据（PM-15 的呈现值装配——一句话汇总＋三动作；
 *        横幅控件归 ui 宿主面，本值只携带决策与渲染所需事实）。
 *
 * 择一语义：三场景并存时 scenario/summaryKey/detailKeys 按**行动紧迫度
 * 序**（①忽略保存＞②中断任务＞③未保存草稿——与 ui 恢复横幅装配面
 * 同序）择一为主文案，其余命中场景降级 detailKeys 详情行（横幅只一句
 * 话汇总，PM-15 原文——不堆叠）。
 *
 * 三动作位形稳定（PM-15 冻结三动作恒在场）：动作键三件套恒填充；
 * restoreAvailable/discardAvailable 是**可用位**不是隐藏位（无草稿可
 * 恢复/只读会话禁放弃时动作置灰不消失——ui 恢复横幅装配面"动作不隐藏
 * 只禁用，保持三动作位形稳定"同口径）。
 *
 * 值语义；线程安全：纯值。
 */
struct RecoveryBannerData {
    /// 主文案命中场景（择一序冻结——见类型注释）。
    RecoveryScenario scenario{};
    /// 一句话主文案键（recoverySummaryKey(scenario)——ui 文案表解析）。
    std::string summaryKey;
    /// 主文案位置参数（场景③命中时恰一元素＝孤儿草稿计数文本（{0} 占位
    /// 参数）；其余场景恒空——UX-02：计数是数值参数不是文案，经参数
    /// 通道由 ui 呈现）。
    std::vector<std::string> summaryArgs;
    /// 命中场景涉事条目计数（单位＝条——主文案的量级事实；
    /// 场景③时与 summaryArgs[0] 同源）。
    std::size_t itemCount = 0;
    /// 其余命中场景的详情行键（主文案互补——降级呈现不堆叠；固定序＝
    /// 紧迫度序，确定性 NFR-COR-02）。
    std::vector<std::string> detailKeys;
    /// [查看详情] 动作键（恒填充——三动作位形稳定）。
    std::string actionDetailsKey;
    /// [恢复草稿] 动作键（恒填充）。
    std::string actionRestoreKey;
    /// [放弃] 动作键（恒填充）。
    std::string actionDiscardKey;
    /// [恢复草稿] 可用位（true＝场景③命中——有草稿可恢复；只读会话
    /// 不禁恢复＝载入查看不是写操作）。
    bool restoreAvailable = false;
    /// [放弃] 可用位（true＝场景③命中**且**会话可写——放弃＝显式
    /// discard＝写操作，只读会话禁用〔ui §8.3-4 口径〕；场景③未命中
    /// 时恒 false——无草稿可放弃）。
    bool discardAvailable = false;

    bool operator==(const RecoveryBannerData& o) const
    {
        return scenario == o.scenario && summaryKey == o.summaryKey
            && summaryArgs == o.summaryArgs && itemCount == o.itemCount
            && detailKeys == o.detailKeys
            && actionDetailsKey == o.actionDetailsKey
            && actionRestoreKey == o.actionRestoreKey
            && actionDiscardKey == o.actionDiscardKey
            && restoreAvailable == o.restoreAvailable
            && discardAvailable == o.discardAvailable;
    }
    bool operator!=(const RecoveryBannerData& o) const { return !(*this == o); }
};

// =====================================================================
// 纯函数组装核（D-WF-3 同型——同输入同输出，NFR-COR-02；模型测试直调面）
// =====================================================================

/**
 * @brief 组装标题栏文本（PM-11 冻结格式 `<显示名>[*][（只读）]` 的唯一
 *        组装点——NFR-MNT-03）。
 *
 * 组装规则（格式逐位兑现，无其他成分）：
 *   `<显示名>` ＋（dirty ? "*" : ""）＋（readOnly ? readonlySuffixText : ""）
 * 后缀文本参数化：readonlySuffixText 由调用方自 kTitleReadonlySuffixKey
 * 经 ui 文案表解析后传入（UX-02——本单元零硬编码中文文案；测试以
 * "（只读）"黄金词形钉扎格式）。
 *
 * @param data              [in] 标题状态数据（事实半区为组装输入；空
 *                          displayName＝无项目会话，返回空串——宿主面
 *                          不渲染，不虚构占位标题）
 * @param readonlySuffixText [in] 只读后缀已解析文本（ui 文案表产出；
 *                          readOnly==false 时不参与组装——传空串合法）
 * @return 标题栏文本（UTF-8；确定性——同输入同输出）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同（data, readonlySuffixText）同输出（NFR-COR-02 同型）。
 */
std::string buildTitleText(const TitleStatusData& data,
                           const std::string& readonlySuffixText);

/**
 * @brief 结果状态文案键选择（状态栏"结果状态（是否过期）"的键半区——
 *        唯一映射点，NFR-MNT-03）。
 *
 * 选键规则（P-UI-2 冻结口径）：
 *   - status==Current → kResultsStatusLabelCurrentKey（"当前"）；
 *   - status==Superseded → kResultsStatusLabelStaleKey（"已过期"）；
 *   - status==nullopt（不可判定）→ kResultsStatusLabelStaleKey——
 *     不可判定**归入过期呈现**（evidence"无默认 Current"的呈现面兑现：
 *     不显示为当前、不显示为通过——绝不能把判不了的结果当通过亮绿灯）。
 *
 * @param projection [in] 当前性投影（搬运值——本函数零当前性计算）
 * @return 结果状态文案键（ui 文案表解析入口）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
std::string resultsStatusLabelKey(const ui::CurrentnessProjection& projection);

/**
 * @brief 组装标题栏/状态栏投影数据（TitleFacts → TitleStatusData 的唯一
 *        组装点——事实透传＋键半区填充）。
 *
 * 组装规则：事实半区五字段逐一透传（零加工——来源事实的权威在对端，
 * PA-1）；resultsStatusLabelKey 按 resultsStatusLabelKey() 规则选键；
 * readonlySuffixKey/schemeLabelKey 恒填充常量键。
 *
 * @param facts [in] 五路来源事实（L5 端口折叠产物）
 * @return 标题状态数据（displayName 空＝无项目会话——宿主面不渲染）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出。
 */
TitleStatusData buildTitleStatus(const TitleFacts& facts);

/**
 * @brief 组装恢复横幅投影数据（RecoveryFacts → 横幅值；无事实返回
 *        nullopt——横幅不渲染，不虚构恢复叙事）。
 *
 * 编排规则（逐步）：
 *   1 前置校验：scenarios 中出现 itemCount==0 或重复场景 → WorkflowError
 *     （调用方拼装违约——fail-fast：零计数场景入清单会让"一句话汇总"
 *     说出不存在的事实）。
 *   2 空集早退：scenarios 为空 → nullopt（无恢复事实——PM-15 横幅只在
 *     有事实时呈现）。
 *   3 择一主文案：按行动紧迫度序 IgnoredUnfinishedSave ＞ InterruptedTask
 *     ＞ OrphanDraft 取首个命中场景为主文案（scenario/summaryKey/
 *     itemCount/summaryArgs——场景③时 summaryArgs 恰一元素＝计数文本）。
 *   4 详情行降级：其余命中场景按同序追加 detailKeys（recoverySummaryKey
 *     同键复用作详情行——与 ui 横幅装配面同构）。
 *   5 动作与可用位：三动作键恒填充（位形稳定）；restoreAvailable＝
 *     场景③命中（有草稿可恢复——只读不禁）；discardAvailable＝场景③
 *     命中且 facts.writable（放弃＝写操作——只读会话禁用，ui §8.3-4）。
 *
 * @param facts [in] 恢复场景事实集（统一诊断目录折叠产物）
 * @return 横幅数据（nullopt＝无恢复事实——不渲染）
 *
 * @throws WorkflowError scenarios 含零计数或重复场景（调用方拼装违约
 *         ——fail-fast）
 *
 * @threadSafe const 纯函数，可并发。
 * @determinism 同输入同输出（清单次序无关——择一由冻结紧迫度序决定，
 *              NFR-COR-02）。
 */
std::optional<RecoveryBannerData> buildRecoveryBanner(const RecoveryFacts& facts);

// =====================================================================
// O9 状态投影服务（§10.2 Draft 接口 IStatusProjectionProvider 逐字兑现
// ＋端口组合实现——编排面唯一公共入口）
// =====================================================================

/**
 * @brief 标题栏/恢复横幅状态投影服务接口（§10.2 Draft 逐字——PM-11/15
 *        的 workflow 侧编排产出面；呈现归 ui 宿主面）。
 *
 * 消费方式：ui 宿主面/L5 装配持有本接口引用，按刷新时序拉取（§5.3——
 * 门控/投影刷新由事件触发、宿主主动拉值；本服务不推送）。
 */
class IStatusProjectionProvider {
public:
    /// 虚析构：经接口引用多态消费的常规保障。
    virtual ~IStatusProjectionProvider() = default;

    /**
     * @brief 取标题栏/状态栏投影数据（每次调用现取现组装——无缓存，
     *        事实新鲜度由端口快照保证）。
     *
     * @return 标题状态数据（displayName 空＝无项目会话）
     *
     * @threadSafe const 并发安全（§10.3 行——ITitleFactPort::collectFacts
     *            const 的对端承诺传导）。
     */
    virtual TitleStatusData titleStatus() const = 0;

    /**
     * @brief 取恢复横幅投影数据（打开后拉取——三场景事实经统一诊断
     *        目录折叠；横幅处置完成〔恢复/放弃〕后由宿主决定停用拉取，
     *        本服务不记忆处置状态）。
     *
     * @return nullopt＝无恢复事实（不渲染横幅）
     *
     * @threadSafe 主线程会话内（诊断目录折叠端口非并发承诺面——§10.3）。
     */
    virtual std::optional<RecoveryBannerData> recoveryBanner() const = 0;
};

/**
 * @brief 状态投影服务实现（IStatusProjectionProvider 的端口组合形态——
 *        两取数端口的编排装配点）。
 *
 * 生命周期与所有权：两端口以引用注入（非 owning——调用方保证端口存活
 * 期覆盖本服务）；本服务无会话状态（每次方法调用现取现组装），析构
 * 零动作。
 *
 * 线程约束：titleStatus() const 并发安全（前置＝title 端口 const 并发
 * 安全）；recoveryBanner() 会话内单线程（recovery 端口折叠含目录消费
 * 语义）。构造前置：两引用天然非空（引用语义）——无空指针违约空间。
 */
class StatusProjectionProvider final : public IStatusProjectionProvider {
public:
    /**
     * @brief 构造（装配点——L5/测试装配两端口引用）。
     *
     * @param titlePort    [in] 标题事实端口（非 owning——存活期覆盖本服务）
     * @param recoveryPort [in] 恢复事实端口（非 owning——同上）
     */
    StatusProjectionProvider(const ITitleFactPort& titlePort,
                             IRecoveryFactPort& recoveryPort) noexcept;

    /// @brief 取标题栏/状态栏投影（titlePort 现取 → buildTitleStatus）。
    TitleStatusData titleStatus() const override;

    /// @brief 取恢复横幅投影（recoveryPort 现取 → buildRecoveryBanner；
    ///        nullopt 直通——无事实不渲染）。
    std::optional<RecoveryBannerData> recoveryBanner() const override;

private:
    const ITitleFactPort* m_titlePort;    ///< 标题事实端口（非 owning——见构造注）
    IRecoveryFactPort* m_recoveryPort;    ///< 恢复事实端口（非 owning——同上）
};

}  // namespace workflow
}  // namespace ird
}  // namespace sdurws

#endif  // SDURWS_IRD_WORKFLOW_PROJECTION_HPP
