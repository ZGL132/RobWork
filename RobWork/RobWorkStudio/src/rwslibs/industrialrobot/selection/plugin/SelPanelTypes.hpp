/**
 * @file   SelPanelTypes.hpp
 * @brief  selection 插件界面的零 Qt 值类型半区——服务缝聚合、会话态与
 *         呈现私有 DTO（units/selection.md §3.1 插件界面的模型层承载、
 *         §16 WP-19-T10 行的界面交付面；WP-19-T10 落位）。
 *
 * 设计依据：
 *   - units/selection.md §3.1（插件组成——目录管理页/候选表/淘汰原因
 *     视图/回填入口/进度与取消；"UI 插件只负责目录管理、筛选条件、
 *     候选表、淘汰原因和结果投影，不执行筛选计算"——二分结构红线）、
 *     §3.4（插件零计算红线——本头与整个插件面不消费任何筛选/插值/
 *     校核/排序符号；不直接读文件、不直接写项目）、§2.3（不在 UI
 *     线程执行目录导入/曲线计算/批量筛选/组合校核——NFR-PERF-01：
 *     数据全部由装配层后台任务现产，本侧只呈现）；
 *   - units/selection.md §16 WP-19-T10 行（目录选择/版本/筛选条件/
 *     候选表/工作点摘要/淘汰原因/可行集/回填入口/进度/取消/诊断的
 *     呈现面清单——本头以呈现 DTO 与服务缝承载其数据半区）；
 *   - 先例：dynamics/plugin/DynPanelTypes.hpp（服务缝聚合＋会话态的
 *     零 Qt 模型半区形态——WP-17-T09 落位；selection 按其"缝由装配层
 *     注入、面板零环境依赖"的同款纪律收缩为六缝）；
 *   - 需求 UX-02（工程用语——文案全经键解析，零哈希/内部标识进用户
 *     文本）、UX-10（七种公共状态统一呈现素材——未完成附缺项、计算
 *     中附进度、数据不足、失败附定位）；SEL-06（逐项淘汰原因的呈现）、
 *     AT-30（回填后复算提示的呈现素材）；SEL-09（范围外轴的双面一致
 *     呈现——wp19-t08 登记）；
 *   - 任务契约 tasks/foundation/WP-19-T10.json（acceptance 1/2）。
 *
 * ★ 落地面口径（诚实登记，DTB §5.4 精神——单元卡 §1.2 同步登记）：
 *   1. **零 ui 编译边**：本单元依赖白名单（卡 §3.2 两条登记边）不含
 *      ui，ird_gates 机器面亦无 selection->ui 登记边——ui 单元公共
 *      类型（PluginUiDescriptor/DomainReadinessItem 等）本插件面**零
 *      消费**；就绪投影行 SelReadinessRow 与面板/命令登记记录以**字段
 *      同构的自持值**承载，真实 ui 类型注册归宿主装配批次收口（缺口
 *      登记见单元卡 P-SEL-10——WP-17-T09 P-DYN-8 同款 B 方案）。
 *   2. **呈现 DTO 全自持**：候选表行/淘汰原因行/目录版本行是本头私有
 *      值类型（不引用计算库结果类型——插件面与计算库结果模型解耦，
 *      结果→呈现行的翻译归缝的组装侧〔宿主装配层或开发 harness〕单点
 *      完成；插件面零翻译零重组之外的业务语义）。
 *   3. **缝纪律（dynamics 先例同款）**：目录版本清单读取、候选表行集
 *      读取、淘汰原因行集读取与回填提交/可用性/文案解析全部为
 *      std::function 缝，由装配层（宿主或开发 harness）注入；缝为空
 *      ＝该面未装配——呈现侧如实降级（"数据未装配"），绝不虚构数值
 *      （NFR-COR-03）。行集的组装（计算库结果→呈现行）全部在缝的
 *      组装侧完成——本插件面零计算符号（契约测试词表扫描钉住）。
 *   4. **零计算红线（卡 §3.4）**：本头不消费任何计算库公共头（结果
 *      模型不进插件面）；词表 token/诊断码文本是缝组装侧翻译产物，
 *      本侧仅作文案键透传（UX-02：呈现前必经文案解析缝，token 本身
 *      不直出用户文本）。
 *
 * 线程约束：全部值类型无同步原语——仅 UI 线程访问（卡 §2.3/§3.4——
 *   面板/会话态生命周期随装配层；数据缝的线程安全语义由缝的提供方
 *   声明，本侧约定全部缝在 UI 线程调用）。
 */

#ifndef IRD_SELECTION_PLUGIN_SELPANELTYPES_HPP
#define IRD_SELECTION_PLUGIN_SELPANELTYPES_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>  // core::EngineeringStatus（最近
                                           //   正式判定词表——ui.md §6.5
                                           //   投影行 verdict 字段同构；
                                           //   core 词表直用，装配层翻译
                                           //   零语义）

namespace sdurws::ird::selection {

// =====================================================================
// 目录管理页呈现行（目录版本清单——§16 T10 行"目录选择/版本"的行素材）。
// =====================================================================

/**
 * @brief 已锁定目录版本呈现行（目录管理页清单一行——§4.2 锁定版本
 *        的呈现投影）。
 *
 * 语义边界（诚实登记）：catalogId/version 是目录身份的用户可读词面
 * （企业分配的目录 ID 与版本号——§4.1 CatalogIdentity 的身份字段，
 * 非哈希非内部标识，UX-02 允许进用户文本）；内容摘要（包字节 SHA-256）
 * **不进本结构**——ARC-04 身份不进呈现，缺列即结构承载（翻译侧无从
 * 泄漏）。sourceLabelKey 是来源描述的文案键（§4.1 来源信息——值归宿
 * 主文案资源）。行序由缝的组装侧给定（呈现序权威——本侧零排序）。
 * 值语义纯结构；线程安全。
 */
struct SelCatalogRow {
    std::string catalogId;      ///< 目录 ID（企业分配词面——显示用）
    std::string version;        ///< 目录版本号（同 ID 多版本并存的区分词面）
    std::string sourceLabelKey; ///< 来源描述文案键（值归宿主文案资源）
    bool selected = false;      ///< 是否当前锁定引用版本（呈现高亮素材——
                                ///<   事实由装配层注入，本侧零判定）
};

// =====================================================================
// 候选表呈现行（候选表页——§16 T10 行"候选表/工作点摘要"的行素材）。
// =====================================================================

/**
 * @brief 候选组合呈现行（候选表一行——器件组合资格的呈现投影）。
 *
 * 语义边界（诚实登记）：行字段全部是缝组装侧从选型结果翻译的呈现值
 * ——combinationKeyLabel 是组合键的呈现标签（文案键形态，非哈希——
 * 组合身份的 SHA-256 词面不进用户文本，UX-02）；verdictKey 取三态封
 * 闭词表（kSelVerdictKey* 常量——可行/数据不足/淘汰）；totalMassKg
 * 是组合质量（kg——SEL-06 素材面的透传呈现值）；minMargin 是最小裕
 * 量（无量纲——SEL-06 裕量指标透传；可空＝该组合无裕量素材可算）；
 * reasonCount 是淘汰原因条数（呈现"原因数"列——明细经淘汰原因行集
 * 缝另行取得）。零业务判定：三态归一在缝组装侧完成，本侧只透传。
 * 值语义纯结构；线程安全。
 */
struct SelCandidateRow {
    std::string combinationKeyLabel; ///< 组合呈现标签（文案键——零哈希）
    std::string verdictKey;          ///< 呈现态键（三态封闭词表——见
                                     ///<   kSelVerdictKey* 常量）
    double totalMassKg = 0.0;        ///< 组合质量，单位 kg（SEL-06 素材
                                     ///<   透传——零业务判定）
    bool hasMass = false;            ///< 质量素材是否存在（false＝无素材
                                     ///<   ——呈现"不适用"占位，不伪造 0，
                                     ///<   ui.md §6.6 纪律）
    double minMargin = 0.0;          ///< 最小裕量（无量纲——可为负＝超限
                                     ///<   指标事实如实保留）
    bool hasMargin = false;          ///< 裕量素材是否存在（同 hasMass 纪律）
    int reasonCount = 0;             ///< 淘汰原因条数（明细行经原因行集缝）
    std::string noteKey;             ///< 组合级标注键（可空串——范围外轴
                                     ///<   的格级 note 直拷键，与缺口维
                                     ///<   呈现键双面一致——wp19-t08）
};

// =====================================================================
// 淘汰原因呈现行（候选表页明细区——§16 T10 行"淘汰原因/诊断"的行素材；
// SEL-06"逐项淘汰原因（含实际值与阈值）"的呈现承载）。
// =====================================================================

/**
 * @brief 淘汰原因呈现行（逐项原因的比较型呈现——ERR-01 字段面）。
 *
 * 语义边界（诚实登记）：reasonKey 是原因 token 的文案键（缝组装侧从
 * 结果词表翻译——封闭词表值，本侧零词表知识）；actual/required 是比
 * 较型字段（SI 单位数值透传）；unitToken 是单位词面（SI 域——显示
 * 单位切换归宿主文案系统，AT-27 纪律：显示不改变计算身份）；
 * thresholdSourceKey 是阈值来源文案键（目录字段/筛选条件条目——
 * SEL-06"阈值来源"呈现面）；axisLabel/caseLabel 是轴与工况的呈现标
 * 签（文案键——对象定位经宿主名称解析，零哈希直出）；diagCodeText
 * 是稳定诊断码文本（SEL-* 词面——诊断定位素材，呈现于明细区定位列，
 * UX-02 允许的诊断呈现形态）。行序由缝组装侧给定（词表稳定序——本
 * 侧零排序）。值语义纯结构；线程安全。
 */
struct SelRejectionRow {
    std::string reasonKey;        ///< 原因 token 文案键（封闭词表值透传）
    std::string axisLabel;        ///< 轴呈现标签（文案键——零哈希）
    std::string caseLabel;        ///< 工况呈现标签（文案键——空＝与工况无关）
    double actual = 0.0;          ///< 实际值（SI 单位——比较型字段透传）
    double required = 0.0;        ///< 要求值（SI 单位）
    std::string unitToken;        ///< 单位词面（SI 域 token——显示换算归
                                  ///<   宿主，AT-27）
    std::string thresholdSourceKey; ///< 阈值来源文案键（SEL-06 定位面）
    std::string diagCodeText;     ///< 稳定诊断码文本（SEL-* 词面——诊断
                                  ///<   定位列素材；空＝无稳定码引用）
};

// =====================================================================
// 数据缺口呈现行（候选表页明细区第二分区——数据不足类事实的呈现；
// 与淘汰原因分轨呈现——§10.2 空集语义分类的呈现面纪律）。
// =====================================================================

/**
 * @brief 数据缺口呈现行（DataGap 类事实的呈现行——数据不足与能力
 *        不足分轨呈现，不混同）。
 *
 * dimensionKey 是缺口维键（封闭词表值透传——范围外轴的维键与格级
 * note 键双面一致，wp19-t08）；diagCodeText 同 SelRejectionRow 口径。
 * 值语义纯结构；线程安全。
 */
struct SelGapRow {
    std::string dimensionKey;     ///< 缺口维键（封闭词表值透传）
    std::string axisLabel;        ///< 轴呈现标签（可空＝域级缺口）
    std::string caseLabel;        ///< 工况呈现标签（空＝与工况无关）
    std::string diagCodeText;     ///< 稳定诊断码文本（定位素材）
};

// =====================================================================
// 回填复算提示呈现素材（AT-30——回填受理后的"依赖结果需复算"呈现）。
// =====================================================================

/**
 * @brief 复算提示呈现素材（AT-30"应用产生新修订并提示依赖结果复算；
 *         复核完成前不沿用原通过结论"的呈现半区）。
 *
 * 四域文案键的词表（kinematics/dynamics/selection/optimization）由缝
 * 组装侧从计算库复算提示事实翻译——本侧零域词表知识（四域 token 是
 * 缝给的键，经文案解析呈现）；retainPriorConclusion 恒 false（不沿用
 * 旧结论——呈现"复核完成前不显示正式通过"的素材位，事实权威在
 * evidence，本侧零判定）。值语义纯结构；线程安全。
 */
struct SelRecalcNotice {
    std::vector<std::string> domainLabelKeys; ///< 需复算域的文案键清单
                                              ///<   （四域全量——AT-30）
    bool retainPriorConclusion = false;       ///< 是否沿用旧结论（恒
                                              ///<   false——AT-30 纪律的
                                              ///<   呈现素材位）
};

// =====================================================================
// 会话命令受理记录（工作流页呈现缓冲——回填提交的会话留痕）。
// =====================================================================

/**
 * @brief 单条回填提交记录（工作流页"最近提交"只读清单的行素材）。
 *
 * accepted=false 时 rejectionKey 携带拒绝键（自持小词表——kSelReject*
 * 常量）；accepted=true 时 notice 携带复算提示素材（AT-30 呈现面）。
 * 值语义纯结构；线程安全。
 */
struct SelBackfillRecord {
    std::string commandToken;   ///< 命令 token（回填提交词表值）
    bool accepted = false;      ///< 是否受理（false＝用户可见的不受理
                                ///<   ——拒绝键见 rejectionKey）
    std::string rejectionKey;   ///< 拒绝键（受理时空串——自持小词表）
    SelRecalcNotice notice;     ///< 复算提示素材（受理时携带——AT-30）
};

// =====================================================================
// 插件会话态（面板呈现事实的唯一载体——装配层注入与刷新）。
// =====================================================================

/**
 * @brief selection 插件会话态（dynamics 先例 DynModuleSessionState
 *        同款收缩形态——只承载呈现所需事实，零权威语义）。
 *
 * 权威边界（PA-1 纪律）：本结构的全部字段都是**装配层注入的呈现事实
 * 投影**——就绪真值归域就绪校验、任务事实归 execution、当前性归
 * evidence；本插件不判定、不缓存权威结论（防第二真值）。插件唯一
 * "写"的字段是回填提交记录缓冲（会话级呈现史——零持久化，不入任何
 * 持久层；回填的真实修订史归 project）。
 *
 * 线程约束：仅 UI 线程访问（装配层注入与面板读取同线程——卡 §3.4）。
 */
struct SelModuleSessionState {
    /// 会话纪元（ui.md §6.2——装配层刷新时递增；面板据此丢弃迟到刷新）。
    std::uint64_t epoch = 0;
    /// 会话是否可写（只读模式——回填入口降级；可用性判定权威在宿主
    /// 可用性缝，本位仅作呈现素材——插件本地零二次判定）。
    bool writable = true;
    /// 就绪校验结论透传（true＝输入完整；结论权威在域就绪校验——
    /// UX-10"未完成附缺项列表"的素材面）。
    bool inputComplete = false;
    /// 缺项文案键清单（UX-02/UX-10——键经宿主文案解析呈现；本插件
    /// 零文案值、零哈希/内部标识进用户文本）。
    std::vector<std::string> missingItemKeys;
    /// 是否存在在途任务（true＝"计算中"呈现素材之一——进度/取消的
    /// 呈现素材；事实归 execution，装配层注入；取消动作归宿主任务
    /// 通道，本插件零本地取消语义）。
    bool hasActiveTask = false;
    /// 最近正式判定（core 词表直用——默认 NotApplicable＝无判定，不伪造
    /// 可行性；UX-10"数据不足"等七态素材之一）。
    core::EngineeringStatus verdict = core::EngineeringStatus::NotApplicable;
    /// 最近回填提交记录（呈现缓冲——新记录追加于尾；超过容量上限时
    /// 从头丢弃最旧记录。容量 8 条是呈现缓冲截断，非业务阈值——完整
    /// 修订史归 project，本缓冲只服务工作流页清单）。
    std::vector<SelBackfillRecord> recentBackfills;
};

// =====================================================================
// 服务缝聚合（面板数据源——装配层注入；空缝＝未装配，呈现如实降级）。
// =====================================================================

/**
 * @brief selection 插件服务缝聚合（dynamics 先例 DynPanelServices
 *        同款形态——全部 std::function 缝，装配层在面板创建前注入）。
 *
 * 缝清单与空缝语义（逐缝注明降级呈现——绝不虚构数值，NFR-COR-03）：
 *   - catalogRows：已锁定目录版本清单读取（目录管理页行集——组装侧
 *     消费目录供给设施后翻译为呈现行）。空缝→目录管理页呈现"目录
 *     数据未装配"空态。
 *   - candidateRows：候选表行集读取（候选表页行集——组装侧从选型
 *     结果翻译）。空缝→候选表呈现"候选数据未装配"空态。
 *   - rejectionRows：淘汰原因行集读取（明细区行集——含数据缺口行）。
 *     空缝→明细区呈现"原因数据未装配"空态。
 *   - gapRows：数据缺口行集读取（明细区第二分区——与原因分轨）。
 *     空缝→缺口区呈现"缺口数据未装配"空态。
 *   - backfillSubmit：回填提交出口（域命令 token——真实注册权威＝
 *     宿主 CommandRegistry 与①命令端口，本缝是插件侧唯一出口；
 *     载荷组装归宿主装配批次）。空缝→按钮点击呈现"出口未装配"
 *     （不静默丢弃——诚实反馈）。
 *   - backfillAvailability：回填可用性查询（统一按钮门控——只读模式/
 *     未装配态的可用性判定权威在宿主；空缝→按可用呈现，点击时经
 *     提交缝/空缝语义如实反馈）。
 *   - textResolver：文案解析（titleKey→工程用语——宿主文案资源唯一
 *     出口 UX-02；空缝→按钮/标题呈现键名原文——dynamics 同纪律，
 *     开发 harness 的可见缺口，不是产品装配形态）。
 *
 * 值语义：可拷贝（缝闭包共享装配层捕获物——shared 语义由闭包自然
 * 承载）；仅 UI 线程调用。
 */
struct SelPanelServices {
    /// 已锁定目录版本清单读取缝（空＝未装配——空态呈现）。
    std::function<std::vector<SelCatalogRow>()> catalogRows;
    /// 候选表行集读取缝（空＝未装配——空态呈现）。
    std::function<std::vector<SelCandidateRow>()> candidateRows;
    /// 淘汰原因行集读取缝（空＝未装配——空态呈现）。
    std::function<std::vector<SelRejectionRow>()> rejectionRows;
    /// 数据缺口行集读取缝（空＝未装配——空态呈现）。
    std::function<std::vector<SelGapRow>()> gapRows;
    /// 回填提交出口缝（空＝未装配——点击反馈"出口未装配"）。
    std::function<void(const std::string& commandToken)> backfillSubmit;
    /// 回填可用性查询缝（空＝按可用呈现）。
    std::function<bool(const std::string& commandToken)> backfillAvailability;
    /// 文案解析缝（空＝键名原文呈现——开发态可见缺口）。
    std::function<std::string(const std::string& titleKey)> textResolver;
};

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELPANELTYPES_HPP
