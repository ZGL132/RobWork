/**
 * @file   SelPanelModel.hpp
 * @brief  selection 插件面板的模型层流（零 Qt 可测半区）——就绪投影合成、
 *         目录行集归一、候选/原因/缺口行集透传、回填提交流与文案解析流
 *         （units/selection.md §3.1 插件界面模型面；WP-19-T10 界面链路
 *         用例的主被测面——"界面链路用例以模型层流＋契约用例承载"）。
 *
 * 设计依据：
 *   - units/selection.md §3.4（插件零计算红线——本模型只做数据归一、
 *     查表与呈现素材合成，零业务判定；排序唯一在计算库）、§2.3
 *     （NFR-PERF-01——UI 线程不执行目录导入/曲线/批量计算：行集现产
 *     归装配层后台任务，本模型只取现成行）、§16 WP-19-T10 行；
 *   - units/ui.md §6.5（DomainReadinessItem 字段同构——域就绪投影行
 *     SelReadinessRow 与之逐一对应，零增删；真实 ui 类型注册缺口见
 *     单元卡 P-SEL-10）、§6.6（UX-02——零哈希进用户文本；数值带单位
 *     显示归宿主换算，本侧 SI 直显）、§6.3（UX-10 七态素材）；
 *   - units/selection.md wp19-t08 登记（格 note 与组合级缺口的双面
 *     一致——呈现键唯一承载，L-S3c）；
 *   - 先例：dynamics/plugin/DynPanelModel.hpp（模型层行集投影＋具名
 *     数据流——WP-17-T09 的 L-Dx 具名纪律；selection 对应为 L-Sx 流，
 *     测试用例以流编号具名）；
 *   - 需求 UX-02（工程用语、零哈希）、UX-10（七态素材）、SEL-06
 *     （逐项淘汰原因呈现）、AT-30（回填复算提示呈现）；任务契约
 *     tasks/foundation/WP-19-T10.json acceptance 1/2。
 *
 * 流清单（L-Sx——每条流的具名测试见 test/SelPluginPanelTest.cpp）：
 *   - L-S1 就绪投影合成：会话事实→SelReadinessRow（透传，零判定——
 *     判定权威在域就绪校验，插件不复制）。
 *   - L-S2 目录行集归一：缝读取→空缝归一＋目录版本显示文本合成
 *     （零排序——呈现序＝缝给定序，组装侧权威）。
 *   - L-S3 候选/原因/缺口行集透传：缝读取→空缝归一（零重组——行集
 *     翻译在缝组装侧）；范围外呈现键唯一书写点（L-S3c）。
 *   - L-S4 回填提交流：出口缝受理→会话记录追加（空缝/不可用拒绝——
 *     用户可见不受理，非异常）。
 *   - L-S5 文案解析流：键→缝解析（空缝兜底键名原文）；解析结果哈希
 *     形态守卫（UX-02 零哈希进用户文本——64 位十六进制串检测）。
 *
 * 线程约束：仅 UI 线程访问（会话态实参非线程安全——SelPanelTypes.hpp）。
 * 错误语义：回填提交的不可受理＝用户可见不受理（accepted=false——
 *   自持拒绝键词表，非异常）；行集查询的空缝＝空行集＋未装配标记
 *   （Empty 显式语义——不伪造行）；其余空态语义见各函数注。
 */

#ifndef IRD_SELECTION_PLUGIN_SELPANELMODEL_HPP
#define IRD_SELECTION_PLUGIN_SELPANELMODEL_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <sdurws/ird/core/Evaluation.hpp>  // core::EngineeringStatus（投影行
                                           //   verdict 同构字段——core 词表
                                           //   直用）
#include <sdurws/ird/selection/SelectionPluginAssembly.hpp> // kSelDomainKey/
                                                            //   kSel*PageKey
                                                            //   （挂位/域键词表
                                                            //   ——assembly
                                                            //   PUBLIC include
                                                            //   面，同单元消费）
#include "SelPanelTypes.hpp"               // SelModuleSessionState/
                                           //   SelPanelServices/呈现行 DTO
                                           //   （同目录私有头）

namespace sdurws::ird::selection {

// =====================================================================
// 呈现键词表（候选三态与拒绝键的封闭小词表——插件面唯一书写点；
// 领域 token 词面〔原因/缺口维〕由缝组装侧给定，本侧零领域词表）。
// =====================================================================

/// 候选呈现态键——可行（器件域事实——"候选可行"≠整机方案正式通过，
/// §11.2 纪律的呈现面词形）。
inline constexpr const char* kSelVerdictKeyFeasible = "feasible";
/// 候选呈现态键——数据不足（DataInsufficient 语义——含范围外轴）。
inline constexpr const char* kSelVerdictKeyDataInsufficient =
    "data-insufficient";
/// 候选呈现态键——淘汰（逐项原因齐备——明细经原因行集缝取得）。
inline constexpr const char* kSelVerdictKeyRejected = "rejected";

/// 拒绝键——回填提交出口未装配（空缝：诚实反馈，不静默丢弃）。
inline constexpr const char* kSelRejectOutletMissing = "backfill-outlet-missing";
/// 拒绝键——宿主可用性判定不可用（只读模式/前置缺失——宿主权威）。
inline constexpr const char* kSelRejectUnavailable = "backfill-unavailable";

/// 复算提示域文案键——运动学（AT-30 四域词表之一；与计算库复算提示
/// 域词表逐字对账由契约测试钉住）。
inline constexpr const char* kSelRecalcDomainKinematics = "kinematics";
/// 复算提示域文案键——动力学（AT-30 四域词表之二）。
inline constexpr const char* kSelRecalcDomainDynamics = "dynamics";
/// 复算提示域文案键——选型（AT-30 四域词表之三）。
inline constexpr const char* kSelRecalcDomainSelection = "selection";
/// 复算提示域文案键——优化（AT-30 四域词表之四）。
inline constexpr const char* kSelRecalcDomainOptimization = "optimization";

/**
 * @brief 范围外轴呈现键（L-S3c——wp19-t08 登记的格 note 与组合级缺口
 *         维双面一致性的呈现承载：两承载面〔候选行 noteKey／缺口行
 *         dimensionKey〕在呈现层归一到本键，唯一书写点）。
 *
 * @return 呈现键（"axis-out-of-scope"——与域内词表同词形；文案值归宿主）
 */
std::string selOutOfScopePresentationKey();

// =====================================================================
// L-S1 就绪投影行（ui.md §6.5 DomainReadinessItem 字段同构自持值）。
// =====================================================================

/**
 * @brief 域就绪投影行（宿主装配层翻译为 ui 汇聚行的素材——字段与
 *        ui::DomainReadinessItem 一一对应，零增删；真实 ui 类型注册
 *        缺口见单元卡 P-SEL-10）。
 *
 * 语义锚（透传纪律）：verdict/inputComplete/missingItemKeys/hasActiveTask
 * 全部取自会话事实（SelModuleSessionState——权威分属域就绪校验/
 * execution/evidence），本模型零判定、零缓存（防第二真值）；domainKey
 * 恒 "selection"（域注册键——ui.md §6.5 原文词表）。
 * 值语义纯结构；线程安全。
 */
struct SelReadinessRow {
    std::string domainKey;                        ///< 域注册键（恒 "selection"）
    core::EngineeringStatus verdict =
        core::EngineeringStatus::NotApplicable;   ///< 最近正式判定（core 词表
                                                  ///<   直用——无判定＝NotApplicable）
    bool inputComplete = false;                   ///< 就绪校验结论（REQ-06——
                                                  ///<   true＝输入完整）
    std::vector<std::string> missingItemKeys;     ///< 缺项文案键清单（UX-10
                                                  ///<   "未完成附缺项列表"素材）
    bool hasActiveTask = false;                   ///< 在途任务事实（"计算中"
                                                  ///<   呈现素材之一——NFR-PERF-01：
                                                  ///<   计算在后台，UI 只呈现）
};

/**
 * @brief L-S1 就绪投影合成（会话事实→投影行——纯透传，零判定）。
 *
 * @param session [in] 插件会话态（事实由装配层注入/刷新）
 * @return 投影行（domainKey 恒 "selection"；其余字段逐项透传）
 */
SelReadinessRow readinessProjection(const SelModuleSessionState& session);

// =====================================================================
// L-S2 目录行集归一（目录管理页——呈现序＝缝给定序，零排序）。
// =====================================================================

/**
 * @brief L-S2 目录版本清单读取（服务缝→目录行集；本函数只做缝的空态
 *         归一——零排序零过滤）。
 *
 * 空态语义：缝未装配→空行集＋outNotAssembled=true（调用方以"未装配"
 * 空态呈现——与"空清单"不同）；缝已装配→行集透传（空清单＝合法空态，
 * outNotAssembled=false）。
 *
 * @param services        [in] 服务缝聚合
 * @param outNotAssembled [out] 可空回传：true＝缝未装配（空态因）
 * @return 目录行集（呈现序＝缝给定序——组装侧权威；空态→空）
 */
std::vector<SelCatalogRow> catalogRowsPresented(const SelPanelServices& services,
                                                bool* outNotAssembled = nullptr);

/**
 * @brief 目录版本显示文本合成（"目录 ID／版本"的呈现行拼接——UX-02：
 *         身份词面进用户文本，零哈希零内部标识）。
 *
 * @param row [in] 目录行（只读）
 * @return 显示文本（"<catalogId> <version>"——单词面空格分隔；空字段
 *         以"不适用"占位词形承载，不伪造值——ui.md §6.6 纪律）
 */
std::string catalogDisplayText(const SelCatalogRow& row);

// =====================================================================
// L-S3 候选/原因/缺口行集透传（候选表页——零重组零判定）。
// =====================================================================

/**
 * @brief L-S3 候选表行集读取（缝→行集；空缝归一同 L-S2 口径）。
 *
 * @param services        [in] 服务缝聚合
 * @param outNotAssembled [out] 可空回传：true＝缝未装配
 * @return 候选行集（呈现序＝缝给定序——稳定序权威在组装侧；空态→空）
 */
std::vector<SelCandidateRow> candidateRowsPresented(
    const SelPanelServices& services, bool* outNotAssembled = nullptr);

/**
 * @brief L-S3 淘汰原因行集读取（缝→行集；空缝归一同 L-S2 口径）。
 *
 * @param services        [in] 服务缝聚合
 * @param outNotAssembled [out] 可空回传：true＝缝未装配
 * @return 原因行集（呈现序＝缝给定序——词表稳定序在组装侧；空态→空）
 */
std::vector<SelRejectionRow> rejectionRowsPresented(
    const SelPanelServices& services, bool* outNotAssembled = nullptr);

/**
 * @brief L-S3 数据缺口行集读取（缝→行集；空缝归一同 L-S2 口径——
 *         缺口与原因分轨呈现，§10.2 空集语义分类的呈现面纪律）。
 *
 * @param services        [in] 服务缝聚合
 * @param outNotAssembled [out] 可空回传：true＝缝未装配
 * @return 缺口行集（呈现序＝缝给定序；空态→空）
 */
std::vector<SelGapRow> gapRowsPresented(const SelPanelServices& services,
                                        bool* outNotAssembled = nullptr);

/**
 * @brief 指标呈现文本合成（质量/裕量列的"数值＋单位"文本——UX-02
 *         "数值一律带单位显示"的 R1 呈现面）。
 *
 * R1 口径（诚实登记）：SI 单位直显（unitToken 原样拼接）——显示单位
 * 切换与量纲换算唯一归宿主文案系统（core 换算入口），本函数是开发
 * harness 与无宿主装配期的过渡呈现形态；AT-27 纪律：显示形态不改变
 * 计算身份（呈现文本零回流入任何身份/判定路径）。
 *
 * @param value     [in] 数值（SI 单位——量纲由 unitToken 表达）
 * @param unitToken [in] 单位词面（"kg"/""（无量纲）等——零换算）
 * @return 呈现文本（"<value> <unit>"；无单位词面＝纯数值）
 */
std::string formatMetric(double value, const std::string& unitToken);

// =====================================================================
// L-S4 回填提交流（§16 T10 行"回填入口"——出口缝受理＋会话记录）。
// =====================================================================

/// 回填提交记录缓冲上限（呈现缓冲截断——非业务阈值；见
/// SelModuleSessionState::recentBackfills 注）。
inline constexpr std::size_t kSelBackfillRecordCapacity = 8;

/**
 * @brief L-S4 回填提交（域命令 token→出口缝→会话记录）。
 *
 * 执行序：
 *   1. 出口缝未装配（backfillSubmit 为空）→不受理，记录拒绝键
 *      kSelRejectOutletMissing（诚实反馈——不静默丢弃）；
 *   2. 可用性缝已装配且返回 false→不受理，记录拒绝键
 *      kSelRejectUnavailable（宿主权威判定透传——插件零本地判定；
 *      可用性缝未装配＝按可用呈现，不拦截）；
 *   3. 经提交缝发起（token→宿主命令管线——真实修订/事务语义归宿主
 *      命令服务，本插件零事务知识）；记录受理＋复算提示素材
 *      （AT-30 呈现面——素材由本函数以四域词表常量合成，见下）。
 *
 * AT-30 复算提示素材（受理时）：四域文案键全量（运动学/动力学/选型/
 * 优化——"依赖结果需复算"的呈现清单）＋retainPriorConclusion=false
 * （复核完成前不沿用原通过结论——呈现素材位）。四域键词表是本函数
 * 的唯一书写点（kSelRecalcDomain* 常量——与计算库复算提示域词表的
 * 逐字对账由契约测试消费计算库常量钉住；插件面零计算库包含）。
 * 零修订语义（本侧）：本函数对项目/模型零触——唯一写点是会话呈现
 * 缓冲；真实新修订由宿主命令服务产生（插件零事务零修订知识）。
 *
 * @param session      [in,out] 插件会话态（记录缓冲被追加——其余字段
 *                     不触）
 * @param services     [in] 服务缝聚合（backfillSubmit/backfillAvailability）
 * @param commandToken [in] 命令 token（回填提交词表值——目录实现常量）
 * @return 受理记录（与追加进会话缓冲的同值拷贝——调用方呈现）
 */
SelBackfillRecord submitBackfill(SelModuleSessionState& session,
                                 const SelPanelServices& services,
                                 const std::string& commandToken);

// =====================================================================
// L-S5 文案解析流（UX-02——键→工程用语，零哈希泄漏守卫）。
// =====================================================================

/**
 * @brief L-S5 文案解析（titleKey→用户文本；空缝兜底键名原文）。
 *
 * 解析序：缝存在→经缝解析（宿主文案资源唯一出口）；缝空→返回键名
 * 原文（dynamics 同纪律——开发 harness 的可见缺口，产品装配必接宿主
 * 解析器）。哈希形态守卫（UX-02"零哈希进用户文本"）：缝解析结果若
 * 呈 64 位十六进制串形态（内容摘要泄漏——ARC-04 身份不进呈现），按
 * 泄漏处置返回键名原文并回传标记——呈现宁可键名也不可哈希。
 *
 * @param services    [in] 服务缝聚合（textResolver 缝）
 * @param titleKey    [in] 文案键（§3.5 键族——值归宿主文案资源）
 * @param outFellBack [out] 可空回传：true＝兜底（缝空或解析结果哈希
 *                    形态）；false＝缝解析原值
 * @return 用户文本（工程用语——或键名兜底）
 */
std::string resolvePanelText(const SelPanelServices& services,
                             const std::string& titleKey,
                             bool* outFellBack = nullptr);

}  // namespace sdurws::ird::selection

#endif  // IRD_SELECTION_PLUGIN_SELPANELMODEL_HPP
