/**
 * @file   Export.hpp
 * @brief  优化证据一站式导出（OPT-12/O12——§11.4 六工件契约的 WP-20-T09
 *         交付面）：契约三元组校验与正式导出阻断（OPT-EXPORT-CONTRACT-
 *         STALE）、资格位终判与限定语强制（取消/失败/中断/数据不足不得
 *         冒充完整正式导出）、六工件数据面组装（研究结果 JSON/候选 CSV/
 *         任务明细 CSV/审计 CSV/Markdown 证据报告/候选模型包）与 io
 *         canonical 写出编排（P-RPT-1 注入形态）。
 *
 * 设计依据：
 *   - units/optimization.md §11.4（OPT-12 一站式导出六工件契约表——逐工件
 *     的格式/写出通道/内容要点；"导出请求必须绑定输入快照、运行身份、
 *     候选身份和证据（Profile/评估器/导出契约版本三元组校验）；契约过期
 *     时阻断正式导出（OPT-EXPORT-CONTRACT-STALE）——版本三元组与结果记录
 *     不符即阻断（reporting RP-CUR-2 同源）。不得冒充完整正式导出：
 *     Preview、取消、失败、中断、数据不足研究结果导出时必须携带对应限定
 *     语状态，且 allowFormalExport=false 的研究不得产出'正式'标记工件
 *     （导出面 status 字段强制）。CSV/JSON 底层安全解析和文件写入归
 *     io/reporting/project——optimization 只定义字段和导出契约（N10）"）、
 *     §11.2（取消、失败和中断结果不得进入正式可行集〔TASK-02〕；缓存命中
 *     ≠Current；Current/Superseded 是 evidence 投影）、§11.3（报告数据源
 *     过期阻断正式导出——与 §11.4 同一阻断面）、§12.2（IOptimization-
 *     ExportProvider 签名与 @throws——契约过期抛 OPT-EXPORT-CONTRACT-STALE；
 *     调用示例非法 2："对 Canceled 运行调用 buildExportBundle 且请求
 *     formal=true——抛 OPT-EXPORT-CONTRACT-STALE 语义拒绝"）、§12.3（错误
 *     语义行——调用方错误 fail-fast／环境错误稳定诊断码＋结构化返回）、
 *     §16.3 P-OPT-5（口径未冻结项按"—"/待裁决口径导出）、P-OPT-6（研究
 *     配置持久化——"R1 会话态＋研究结果 JSON 副本承载研究定义〔§11.4
 *     工件 1〕；不入 .rwdesign"）、P-OPT-7（误淘汰审计置信上界计算式未
 *     冻结——审计 CSV 该列按"—"导出）、P-OPT-2（候选物化通道未裁决——
 *     工件 6 候选模型包以补丁 canonical＋来源标识承载，写出通道为注入缝）、
 *     §6.6 行 14（OPT-EXPORT-CONTRACT-STALE——error 级域码）
 *   - 需求 OPT-12（一站式导出六工件＋契约过期阻断正式导出）、NFR-COR-04
 *     （报告可追溯——逐候选证据引用与 Profile 项对齐）、TASK-02、CON-02、
 *     EVO/EVI-01（Profile 项证据引用——§11.1 事实清单）
 *   - 任务契约 tasks/foundation/WP-20-T09.json（acceptance 1/2/3；knownPitfalls
 *     P-OPT-5/P-RPT-1）
 *
 * ★ 落位范围口径（诚实登记，防扩大）：§15.3 表"§11 → Export.hpp＋src＋
 *   报告章节提供方"——本任务交付：①契约三元组校验＋正式导出阻断＋资格
 *   位终判（T08 Preflight allowances.allowFormalExport 的"研究定义可预判
 *   部分"在此做运行记录面终判——T08 注预告的执行点）；②六工件数据面组装
 *   （字段契约单点——列名/JSON 键序/MD 章节结构）；③io canonical 写出
 *   编排（writeExportBundle——经注入的 io ICsvWriter/IJsonWriter/
 *   IAtomicFileWriter 会话驱动，零自有文件写路径——N10）。**不落**：
 *   reporting `optimization-candidates` 报告章节提供方（§11.3 的
 *   IReportSectionProvider 适配——其输入面是 evidence ResultEnvelope 报告
 *   构建链，归报告联调批次随 RPT-T14 交接行落位；本批六工件导出与报告
 *   章节是 OPT-12 的两条独立呈现面，任务契约 acceptance 三条均不涉及章节
 *   提供方，NFR-MNT-04 不预建）；归档执行（IResultArchivePort 写盘序列——
 *   归 execution 触发，本面只消费 ArchivePhase 状态）；工件 6 的产品写出
 *   适配器（桥 io IPackageExporter＋project ISnapshotFileSource——经
 *   ICandidatePackageWriter 注入缝承载，随 P-OPT-2/MDL-20 roundtrip 联调
 *   落位，见该接口注）。
 *
 * 背景说明（第一读者须知——四件事）：
 *   ① **导出面只有字段契约，没有第二套写盘实现**（N10/SA-12）：CSV 转义、
 *      JSON canonical（键序字典序＋to_chars 最短往返＋UTF-8 无 BOM）与
 *      原子替换全部唯一归 io 设施；本面组装的是"写什么"（列/键/单元格），
 *      "怎么写"经注入的 io 会话对象完成（P-RPT-1 注入形态——产品装配
 *      makeDefaultExportIoPorts() 供真 io 工厂，测试可注入计数替身）。
 *   ② **三元组校验是记录面 vs 当前面的对账**：Profile（运行时点引用
 *      spec.profile vs 当前装配面权威 currentProfile）、评估器契约版本
 *      （运行记录登记值 vs kOptStaticScreenContractVersion 现值）、导出契约
 *      版本（运行记录登记值 vs kOptExportContractVersion 现值）。任一漂移
 *      ⇒ contractCurrent=false ⇒ formal 请求抛 OPT-EXPORT-CONTRACT-STALE
 *      （阻断正式导出）；限定语导出不阻断但强制携带 "export-contract-stale"
 *      限定语（消费者不得把过期研究当当前证据引用——RP-CUR-2 同源纪律）。
 *   ③ **status 字段强制**（§11.4 尾段字面）：ExportStatus 两值——Formal 仅
 *      在"请求 formal＋run.allowFormalExport＋三元组当前"同时成立时产出；
 *      其余一切导出（取消/失败/中断/数据不足/契约漂移/限定语请求）都是
 *      Qualified 且 qualifierTokens 非空携带限定语清单（确定性序）。正式
 *      请求落到无资格运行上按 §12.2 调用示例非法 2 抛 OPT-EXPORT-CONTRACT-
 *      STALE 语义拒绝——绝不降级成限定语导出"悄悄放行"（冒充面的反面：
 *      调用方要正式就给正式，给不了就明确失败）。
 *   ④ **当前性是消费投影不是第二套判定**（N8/N9——T07 注⑤划归本面的
 *      OPT-VER-148）：Current/Superseded 唯一归 evidence computeCurrentness；
 *      ExportRequest 以投影值携带（std::optional——同 ProjectBaselineState
 *      先例：调用方在组装请求时点自 evidence 纯函数计算后随请求提供）。
 *      未携带＝限定语 "currentness-not-provided"（如实呈现，不默认 Current
 *      ——EV-CUR-2"无默认 Current"的消费侧同源纪律）。
 *
 * 线程约束：buildExportBundle const 只读可并发（§12.3——纯函数面）；
 *   writeExportBundle 驱动注入的 io 会话对象（会话型单线程——io §9.4/§9.5），
 *   一次调用内串行驱动全部工件，不同调用可各自并行（io 目标互异时）。
 * 确定性：同 request ⇒ 同 bundle（逐字节——JSON canonical 键序字典序、CSV
 *   行序＝run.candidates 编排序、MD 章节序固定；无时间戳/无随机源——
 *   createdAtUtc 等审计字段按记录面原样透传）。
 */

#ifndef SDURWS_IRD_OPTIMIZATION_EXPORT_HPP
#define SDURWS_IRD_OPTIMIZATION_EXPORT_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::ContentIdentity——评估器集
                                            //  摘要（EvaluatorSetId 承载）与
                                            //  Profile 身份分量
#include <sdurws/ird/evidence/Currentness.hpp>  // evidence::CurrentnessResult——
                                            //  当前性投影（OPT-VER-148 消费面；
                                            //  判定唯一归 evidence compute-
                                            //  Currentness——本面零判定复制）
#include <sdurws/ird/evidence/Envelope.hpp>  // evidence::EvidenceProfileRef——
                                            //  Profile 三元组（三元组校验①）
#include <sdurws/ird/io/AtomicFile.hpp>    // io::IAtomicFileWriter(IAtomicFile-
                                            //  WriterPtr)/ReplacePolicy——工件 5
                                            //  Markdown 的原子写出通道（完整
                                            //  类型——端口消费面需要成员可见）
#include <sdurws/ird/io/Csv.hpp>           // io::ICsvWriter/CsvCell/CsvWrite-
                                            //  Options/CsvOutputTarget——CSV 三
                                            //  工件的 canonical 写出通道（io
                                            //  唯一转义点——本面零自有转义）
#include <sdurws/ird/io/Json.hpp>          // io::IJsonWriter/JsonDocument——
                                            //  工件 1 的 canonical 写出通道
#include <sdurws/ird/io/IoFwd.hpp>         // io::IoResult——通道缝返回容器
#include <sdurws/ird/optimization/EvaluatorPorts.hpp>  // kOptStaticScreenContract-
                                            //  Version——三元组②当前面权威值
#include <sdurws/ird/optimization/Preflight.hpp>  // OptimizationRunSpec——研究定
                                            //  义（P-OPT-6 工件 1 副本承载源；
                                            //  validateRunSpec 复验消费）
#include <sdurws/ird/optimization/Run.hpp> // OptimizationRunResult——导出源
                                            //  （归档不可变历史；ArchivePhase）
#include <sdurws/ird/optimization/Types.hpp>  // OptimizationError——域异常

namespace sdurws::ird::optimization {

// =====================================================================
// 导出契约版本与状态词表（§11.4/§12.3 版本行——三元组第三元）
// =====================================================================

/**
 * @brief 导出契约版本（§11.4 六工件字段契约的 schema 演进戳——三元组
 *        第三元"导出契约版本"的当前面权威值）。
 *
 * 语义：六工件的字段集/列名/JSON 键集由本头冻结（v1）。任何破坏性字段
 * 变化（改列名/删键/改值语义）必须升此版本——运行记录登记的导出契约版本
 * （exportContractVersionAtRun）与本常量不等即三元组漂移（导出面 schema
 * 与运行时点不一致，正式导出阻断）。新增可选字段（只增不改）同样升版本
 * ——保守口径：消费方按版本解释字节，"只增"不豁免（NFR-DEP-04 同源）。
 */
inline constexpr std::uint32_t kOptExportContractVersion = 1U;

/**
 * @brief 导出状态两值词表（§11.4 尾段"导出面 status 字段强制"的承载——
 *        封闭词表，后续只消费不扩表）。
 *
 * Formal＝正式完整导出（"formal"标记工件可被下游作为当前证据引用——资格
 * 三条件：formal 请求＋run.allowFormalExport＋契约三元组当前）；Qualified
 * ＝限定语导出（"qualified"——消费者必须按限定语解释，不得当作完整正式
 * 结论）。限定语与状态正交：Formal 工件同样携带**事实自述类**限定语
 * （搜索空/数据不足/当前性状态——"formal"认证运行完成＋契约当前，不否
 * 认空集与数据不足事实）；资格缺失类限定语（run-not-completed/contract-
 * stale）只出现在 Qualified（其命中即阻断 formal）。
 */
enum class ExportStatus {
    Formal,     ///< 正式导出（三元组当前＋allowFormalExport＋formal 请求）
    Qualified,  ///< 限定语导出（取消/失败/中断/数据不足/契约漂移/限定语请求）
};

/**
 * @brief 导出状态稳定 token（"formal"/"qualified"——JSON/CSV/MD 与测试的
 *        确定性书写；同 Types.hpp 各词表 toToken 纪律）。
 * @param s [in] 导出状态枚举值
 * @return 稳定 token 视图（编译期字面量，生命周期静态）
 */
std::string_view toToken(ExportStatus s) noexcept;

// =====================================================================
// 限定语 token 词表（§11.4"必须携带对应限定语状态"——确定性序产出；
// 值一经交付不得改动——导出工件的持久化契约面）
// =====================================================================

/// 运行未正常完成（Canceled/Failed/Interrupted——TASK-02 三态：不得冒充
/// 完整正式导出；RunPhase 具体 token 随 JSON status 块携带）。
inline constexpr std::string_view kExportQualifierRunNotCompleted
    = "run-not-completed";
/// 搜索空（OPT-VER-120 数据不足语义——可行集为空＝无有效候选结论，非任务
/// 不可行；正式导出里也如实携带——工件须自我声明"空集"事实）。
inline constexpr std::string_view kExportQualifierSearchEmpty = "search-empty";
/// Verified 批存在 DataInsufficient 候选（§7.5/§11.2——数据不足不得冒充
/// 完整正式导出；逐候选状态在候选 CSV/JSON 原样呈现）。
inline constexpr std::string_view kExportQualifierVerifiedDataInsufficient
    = "verified-data-insufficient";
/// 契约三元组漂移（Profile/评估器/导出契约任一——RP-CUR-2 同源；formal
/// 请求已阻断抛出，本限定语仅出现于 Qualified 工件）。
inline constexpr std::string_view kExportQualifierContractStale
    = "export-contract-stale";
/// 历史结果已过期（evidence 当前性投影 Superseded——CON-02：保留＋原因
/// 清单，不得当作当前结论引用）。
inline constexpr std::string_view kExportQualifierSuperseded = "superseded";
/// 当前性投影不可判定（evidence NotEvaluable——P-EV-4：不得当作 Current）。
inline constexpr std::string_view kExportQualifierCurrentnessUnevaluable
    = "currentness-unevaluable";
/// 请求未携带当前性投影（R1 常态——computeCurrentness 消费归调用方；如实
/// 呈现，不默认 Current——EV-CUR-2"无默认 Current"消费侧纪律）。
inline constexpr std::string_view kExportQualifierCurrentnessNotProvided
    = "currentness-not-provided";

// =====================================================================
// 契约三元组校验结果（§11.4"Profile/评估器/导出契约版本三元组校验"）
// =====================================================================

/**
 * @brief 契约三元组逐维校验结果（记录面 vs 当前面的对账账本——进 JSON
 *        contract 块与 MD 契约节；测试对账锚）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExportContractCheck {
    /// ①Profile：currentProfile 与 spec.profile 逐字段一致（profileId/
    /// version/contentIdentity——Profile 内容身份漂移＝证据契约过期）。
    bool profileCurrent = false;
    /// ②评估器：evaluatorContractVersionAtRun（运行记录登记值）==
    /// kOptStaticScreenContractVersion（现值——评估器契约升级即漂移）。
    bool evaluatorCurrent = false;
    /// ③导出契约：exportContractVersionAtRun（运行记录登记值）==
    /// kOptExportContractVersion（现值——六工件字段契约升级即漂移）。
    bool exportCurrent = false;

    /// 三元组全部一致（formal 导出的必要条件）。
    bool current() const noexcept { return profileCurrent && evaluatorCurrent && exportCurrent; }
    bool operator==(const ExportContractCheck& o) const noexcept
    {
        return profileCurrent == o.profileCurrent && evaluatorCurrent == o.evaluatorCurrent
               && exportCurrent == o.exportCurrent;
    }
    bool operator!=(const ExportContractCheck& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// 导出请求（buildExportBundle 的冻结输入面——值语义拷贝入组装）
// =====================================================================

/**
 * @brief 一站式导出请求（§11.4"导出请求必须绑定输入快照、运行身份、候选
 *        身份和证据"的组装面——run 与 spec 双载体＋三元组双侧面）。
 *
 * 绑定关系（构造后不可变；校验在 buildExportBundle 内单点执行）：
 *   - run＝归档运行结果（导出的结论面——候选/指标/Pareto/审计）；
 *   - spec＝运行启动时点的研究定义（P-OPT-6 工件 1 副本承载源——研究
 *     定义跨会话恢复的裁决前通道；其身份四元组须与 run 逐一相等——绑定
 *     校验）；
 *   - currentProfile＝**当前**装配面权威 Profile 引用（自 evidence 注册表
 *     查询——三元组①当前面；与 spec.profile〔记录面〕对账）；
 *   - evaluatorContractVersionAtRun/exportContractVersionAtRun＝**运行记录
 *     面**登记值（运行启动时 Preflight #7/导出契约版本登记的时点值；
 *     R1 由运行记录供给——与 kOptStaticScreenContractVersion/
 *     kOptExportContractVersion 现值对账）；
 *   - evaluatorSetId＝运行登记的评估器集摘要（EvaluatorSetId——§4.2 辅助
 *     身份；Preflight evaluatorSetDigest 的登记值；保留值＝未登记，工件按
 *     "—"导出）；
 *   - formal＝正式导出请求位（true 且无资格 ⇒ 抛阻断，不降级——见文件头
 *     注③）；
 *   - currentness＝当前性投影（可选——OPT-VER-148 消费面；未携带按限定语
 *     呈现，绝不默认 Current）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExportRequest {
    OptimizationRunResult run{};   ///< 归档运行结果（archivePhase 须 Archived）
    OptimizationRunSpec spec{};    ///< 研究定义（身份四元组须与 run 一致）
    evidence::EvidenceProfileRef currentProfile{}; ///< 当前装配面权威 Profile
                                                    ///  （三元组①当前面）
    std::uint32_t evaluatorContractVersionAtRun = 0; ///< 运行记录面评估器契约
                                                    ///  版本（三元组②记录值；须 ≥1）
    std::uint32_t exportContractVersionAtRun = 0;   ///< 运行记录面导出契约版本
                                                    ///  （三元组③记录值；须 ≥1）
    core::ContentIdentity evaluatorSetId{}; ///< 评估器集摘要（可保留值＝"—"）
    bool formal = false;           ///< 正式导出请求位（无资格即阻断抛出）
    std::optional<evidence::CurrentnessResult> currentness{}; ///< 当前性投影
                                                    ///  （OPT-VER-148；可空）
};

// =====================================================================
// 工件 6：候选模型包数据面（§11.4 行 6——ZIP 传输封装语义的条目集）
// =====================================================================

/**
 * @brief 候选模型包内条目（包内相对路径＋完整字节——§11.4"候选设计工件＋
 *        来源标识"的数据面；写出经 ICandidatePackageWriter 注入缝）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExportPackageEntry {
    std::string packPath = {};               ///< 包内相对路径（正斜杠分隔——
                                             ///  ZIP 惯例；确定性命名见组装实现注）
    std::vector<std::uint8_t> bytes = {};    ///< 条目完整字节（UTF-8 文本或
                                             ///  补丁 canonical 二进制）
};

// =====================================================================
// 六工件数据面（buildExportBundle 产出——§11.4 逐工件字段契约）
// =====================================================================

/**
 * @brief 导出六工件数据面（§11.4 表的逐一承载——"写什么"在此，"怎么写"
 *        在 writeExportBundle 经 io 设施）。
 *
 * 字段分组（与 §11.4 表行序一致）：
 *   - 状态面：status＋qualifierTokens＋contract（三态正交的导出侧投影——
 *     status 字段强制的承载）；
 *   - 工件 1：researchJson（io::JsonDocument 受限 DOM——canonical 写出即
 *     研究结果 JSON；P-OPT-6 研究定义副本在内）；
 *   - 工件 2~4：三张 CSV 的表头＋行集（io::CsvCell 值语义单元格——io 唯一
 *     转义点的输入面；行序确定性）；
 *   - 工件 5：evidenceReportMarkdown（UTF-8 Markdown 全文——经 io 原子写
 *     出；内容组装归本单元，§11.3 分工行）；
 *   - 工件 6：candidatePackageEntries（包内条目集——经注入缝写出）。
 *
 * 线程安全：纯值（拷贝独立；构造后按不可变历史消费——CON-02）。
 */
struct ExportBundleData {
    // ---- 状态面（status 字段强制——§11.4 尾段）----
    ExportStatus status = ExportStatus::Qualified; ///< 导出状态（Formal 资格
                                                    ///  判定见文件头注③）
    /// 限定语清单（无条件推导——资格缺失类＋事实自述类全量携带；确定性
    /// 序＝上方 kExportQualifier* 常量登记序。资格缺失类仅出现在
    /// Qualified；事实自述类在 Formal 工件上同样携带——RPT-05 限定语纪律）。
    std::vector<std::string> qualifierTokens = {};
    ExportContractCheck contract = {};  ///< 三元组逐维校验账本

    // ---- 工件 1：研究结果 JSON（canonical DOM——io IJsonWriter 写出）----
    io::JsonDocument researchJson = {};

    // ---- 工件 2：候选 CSV（逐候选——§11.4 行 2 字段契约）----
    std::vector<io::IoString> candidatesCsvHeader = {};
    std::vector<std::vector<io::CsvCell>> candidatesCsvRows = {};

    // ---- 工件 3：任务明细 CSV（逐 execution 任务——§11.4 行 3）----
    std::vector<io::IoString> tasksCsvHeader = {};
    std::vector<std::vector<io::CsvCell>> tasksCsvRows = {};

    // ---- 工件 4：审计 CSV（与重放统计一致——AT-34；§11.4 行 4）----
    std::vector<io::IoString> auditCsvHeader = {};
    std::vector<std::vector<io::CsvCell>> auditCsvRows = {};

    // ---- 工件 5：Markdown 证据报告（内容组装归本单元——io 原子写出）----
    std::string evidenceReportMarkdown = {};

    // ---- 工件 6：候选模型包（条目集——注入缝写出）----
    std::vector<ExportPackageEntry> candidatePackageEntries = {};
};

// =====================================================================
// 导出提供接口（§12.2 IOptimizationExportProvider——签名逐字）
// =====================================================================

/**
 * @brief 导出事实提供接口（§12.2 原文——"组装六工件的数据面（§11.4 字段
 *        契约）＋契约版本三元组校验"；文件写出归 io/reporting/project——
 *        N10，本接口不触盘）。
 *
 * @pre 运行已归档（request.run.archivePhase==Archived——归档不可变历史是
 *      导出源的结构性资格，CON-02/PA-2）；allowFormalExport 才可出"正式"
 *      标记（formal 请求的资格终判在此执行——T08 allowances 注预告的终判
 *      执行点）。
 * @throws OptimizationError 契约过期/资格缺失（OPT-EXPORT-CONTRACT-STALE，
 *         阻断正式导出——§12.2 调用示例非法 2 语义）；调用方组装违约走
 *         kOptInputInvalid（身份保留值/spec 与 run 身份不一致/记录面版本
 *         未登记等——比较型定位消息）。
 * @threadSafe 是（const 纯函数——§12.3）。
 * @determinism 同请求同产出（逐字节——确定性纪律见文件头）。
 */
class IOptimizationExportProvider {
public:
    virtual ~IOptimizationExportProvider() = default;

    /**
     * @brief 组装六工件数据面＋三元组校验（§12.2 原文签名）。
     *
     * 执行序（固定——确定性首错）：
     *   1. 请求校验（调用方违约 fail-fast——kOptInputInvalid）：run 身份面
     *      非保留值；archivePhase==Archived；validateRunSpec(spec) 复验＋
     *      spec 与 run 的身份四元组逐一相等（导出绑定输入快照与运行身份）；
     *      currentProfile 三分量完整且 profileId=="opt"；记录面两版本 ≥1；
     *   2. 三元组对账（①Profile 逐字段 ②评估器版本 ③导出契约版本）；
     *   3. 资格终判：formal 请求且（!run.allowFormalExport 或三元组漂移）
     *      ⇒ 抛 OptimizationError(kOptExportContractStale)（消息定位到具体
     *      缺资格维度——取消结果不得冒充正式导出）；其余请求继续；
     *   4. 状态与限定语推导（Formal 仅在 formal＋allowFormalExport＋三元组
     *      当前全成立时；Qualified 强制携带限定语——确定性序）；
     *   5. 六工件数据面组装（JSON DOM→CSV 行集→MD 全文→包条目——各工件
     *      组装规则见 .cpp 逐步注释）。
     *
     * @param request [in] 导出请求（值拷贝入组装——调用方持有）
     * @return 六工件数据面（值语义；写盘经 writeExportBundle 另行编排）
     *
     * @throws OptimizationError(kOptExportContractStale) formal 请求遇无资格
     *         运行/三元组漂移（阻断正式导出——不降级不冒充）；
     *         kOptInputInvalid 请求组装违约（见执行序第 1 步）
     */
    virtual ExportBundleData buildExportBundle(const ExportRequest& request) const = 0;
};

/**
 * @brief 导出提供唯一产品实现（无状态纯函数面——实例进程级共享安全）。
 */
class OptimizationExportProvider final : public IOptimizationExportProvider {
public:
    /// @copydoc IOptimizationExportProvider::buildExportBundle
    ExportBundleData buildExportBundle(const ExportRequest& request) const override;
};

// =====================================================================
// 工件 6 写出通道缝（P-OPT-2 裁决前形态——与 Applier 物化缝同款先例）
// =====================================================================

/**
 * @brief 候选模型包写出缝（§11.4 行 6"写出通道＝io IPackageExporter（经
 *        project 快照）"的注入面——本单元零 project 快照读取面与 ZIP 编码
 *        实现（N10/N1）：产品适配器由 L5 装配层提供——输入条目集，内部经
 *        project ISnapshotFileSource 适配视图＋io IPackageExporter 完成
 *        ZIP 传输封装与完整性自检；roundtrip 口径同 MDL-20）。
 *
 * 为什么是缝而不是直连：io IPackageExporter 的一致视图源（ISnapshotFile-
 * Source）是 project 快照语义（§9.9——"io 定义、project 实现并注入"），
 * 而候选模型包的条目来自运行结果数据面（补丁 canonical＋来源标识）——
 * 两侧的桥接（内存条目→一致视图适配）属于装配层组合逻辑；且 P-OPT-2
 * 候选物化通道裁决后，包内"候选设计工件"将从补丁 canonical 升级为完整
 * 候选设计字节——通道语义随裁决演进，缝面稳定（与 Applier.hpp
 * ICandidateDesignMaterializer 同款先例：P-OPT-3 裁决前由调用方/测试
 * 供给）。
 *
 * 实现契约：writePackage 对全部条目原子就位（任一失败＝目标不受影响——
 * 同 io §4.6 失败恢复语义）；返回 io 稳定码（桥接 io 通道时透传其码面）。
 */
class ICandidatePackageWriter {
public:
    virtual ~ICandidatePackageWriter() = default;

    /**
     * @brief 把候选包条目集写出到目标文件（§11.4 行 6 通道执行点）。
     *
     * @param entries    [in] 包内条目集（非空——空包无导出意义，组装面保证）
     * @param targetFile [in] 包文件目标路径（父目录必须已存在——调用方契约）
     * @return ok＝包已原子就位；失败＝通道稳定码（IO-*——桥接 io 时透传）
     */
    virtual io::IoResult<void> writePackage(
        const std::vector<ExportPackageEntry>& entries,
        const std::filesystem::path& targetFile) const = 0;
};

// =====================================================================
// io 写出编排（P-RPT-1 注入形态——"怎么写"的注入面与驱动器）
// =====================================================================

/**
 * @brief 导出 io 端口集（P-RPT-1 注入形态——全部 io 能力经本结构注入，
 *        本单元零 io 具体实现的编译期硬绑定；与 reporting IReportIoFactory
 *        同案：产品装配供真工厂、测试注入替身/计数器）。
 *
 * 字段语义：
 *   - jsonWriterFactory/csvWriterFactory：会话工厂（每工件一个会话实例——
 *     io §9.4/§9.5"一个实例一个目标"；std::function 缝——测试可计数）；
 *   - atomicWriter：原子文件写出器（工件 5 Markdown——prepare→write→
 *     commit 协议；io §9.10）；
 *   - packageWriter：候选包写出缝（工件 6——上方 ICandidatePackageWriter）。
 *
 * 全部字段缺省为空＝未装配（writeExportBundle 构造期校验拒绝——装配违约
 * fail-fast，不静默跳过任何工件：六工件要么全套要么明确失败，不出三件套
 * 冒充全套）。
 *
 * 产品装配入口＝makeDefaultExportIoPorts()（真 io 工厂）。
 */
struct ExportIoPorts {
    /// JSON 写出器工厂（工件 1——canonical 写出会话）。
    std::function<std::unique_ptr<io::IJsonWriter>()> jsonWriterFactory;
    /// CSV 写出器工厂（工件 2~4——每张表一个会话；工厂可被多次调用）。
    std::function<std::unique_ptr<io::ICsvWriter>()> csvWriterFactory;
    /// 原子文件写出器（工件 5——Markdown 的 prepare/write/commit 协议）。
    std::shared_ptr<io::IAtomicFileWriter> atomicWriter;
    /// 候选包写出缝（工件 6——非 owning 只读借用，调用期间存活）。
    const ICandidatePackageWriter* packageWriter = nullptr;
};

/**
 * @brief 产品装配的 io 端口集（真 io 设施——makeJsonWriter/makeCsvWriter/
 *        makeAtomicFileWriter；候选包缝无产品实现——仍须装配层供给，
 *        P-OPT-2 裁决前 R1 交付面为测试替身，诚实登记）。
 * @return 端口集（json/csv/atomic 三端口就绪；packageWriter 恒空——调用
 *         方装配）
 */
ExportIoPorts makeDefaultExportIoPorts();

// ---- 六工件的目标文件名（canonical 固定名——一站式导出的稳定布局）----

/// 工件 1：研究结果 JSON（P-OPT-6 研究定义副本承载）。
inline constexpr std::string_view kExportResearchJsonName = "opt-research.json";
/// 工件 2：候选 CSV。
inline constexpr std::string_view kExportCandidatesCsvName = "opt-candidates.csv";
/// 工件 3：任务明细 CSV。
inline constexpr std::string_view kExportTasksCsvName = "opt-tasks.csv";
/// 工件 4：审计 CSV（AT-34 对账面）。
inline constexpr std::string_view kExportAuditCsvName = "opt-audit.csv";
/// 工件 5：Markdown 证据报告。
inline constexpr std::string_view kExportEvidenceReportName = "opt-evidence-report.md";
/// 工件 6：候选模型包（ZIP 传输封装——通道缝目标名）。
inline constexpr std::string_view kExportCandidatePackageName = "opt-candidate-package.zip";

/**
 * @brief 单工件写出结果（§12.3 错误语义行的结构化承载——环境错误不抛，
 *        逐工件带 io 稳定码返回；调用方错误〔端口未装配/目标目录空〕才
 *        走异常 fail-fast）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExportArtifactOutcome {
    std::filesystem::path target;  ///< 目标路径（未尝试时为空路径）
    bool ok = false;               ///< 写出成功（目标已原子就位）
    std::string errorCode = {};    ///< io 稳定码 token（ok 恒空串；环境错误
                                   ///  登记面——如 "IO-RES-WRITE-FAILED"）
};

/**
 * @brief 六工件写出结果（全部工件逐一给账——部分失败不冒充完整导出：
 *        消费方以 allOk() 判定，失败工件按 errorCode 登记）。
 *
 * 值语义；线程安全：纯值。
 */
struct ExportWriteResult {
    ExportArtifactOutcome researchJson = {};       ///< 工件 1
    ExportArtifactOutcome candidatesCsv = {};      ///< 工件 2
    ExportArtifactOutcome tasksCsv = {};           ///< 工件 3
    ExportArtifactOutcome auditCsv = {};           ///< 工件 4
    ExportArtifactOutcome evidenceReport = {};     ///< 工件 5
    ExportArtifactOutcome candidatePackage = {};   ///< 工件 6

    /// 六工件全部成功（一站式完整导出的判定面——任一失败即 false）。
    bool allOk() const noexcept
    {
        return researchJson.ok && candidatesCsv.ok && tasksCsv.ok && auditCsv.ok
               && evidenceReport.ok && candidatePackage.ok;
    }
};

/**
 * @brief 把六工件数据面写出到目标目录（一站式导出的文件层编排——经注入
 *        的 io canonical 写出通道，零自有文件写路径——N10）。
 *
 * 执行序（固定；逐工件独立给账，单件失败不阻断其余工件——失败清单完整
 * 呈现，"部分失败不冒充完整导出"由 allOk() 与逐工件 ok 位承载）：
 *   1. 编排校验（调用方违约 fail-fast——kOptInputInvalid）：ports 四端口
 *      非空、targetDirectory 非空路径；
 *   2. 工件 1：jsonWriterFactory() 会话 → write(文件目标, researchJson,
 *      {profile=nullptr→字典序 canonical})——io 单点 canonical（键序/数值
 *      最短往返/UTF-8 无 BOM）；
 *   3. 工件 2~4：每张 CSV 一个 csvWriterFactory() 会话 → open（rwDefault
 *      方言＋标识行）→ writeHeader → writeRow×N → finish（原子替换就位；
 *      RAII——中途失败析构即放弃，目标不变，io §9.4）；
 *   4. 工件 5：atomicWriter->prepare（OverwriteAtomic）→ target.write
 *      （UTF-8 全文一次写入）→ commit；失败路径 abort（目标不变）；
 *   5. 工件 6：packageWriter->writePackage(entries, 目标)（通道缝——条目
 *      集原子就位语义归缝实现）。
 *
 * @param bundle          [in] 六工件数据面（buildExportBundle 产出）
 * @param ports           [in] io 端口集（四端口必须就绪——装配违约抛出）
 * @param targetDirectory [in] 目标目录（必须已存在——io 不代建目录）
 * @return 六工件逐一结果（allOk()==true 即一站式完整导出）
 *
 * @throws OptimizationError(kOptInputInvalid) 端口未装配/目标目录为空
 *         （调用方组装违约 fail-fast；io 通道失败不走异常——结构化返回）
 *
 * 线程约束：驱动注入的 io 会话对象（会话型单线程——一次调用内串行）。
 */
ExportWriteResult writeExportBundle(const ExportBundleData& bundle,
                                    const ExportIoPorts& ports,
                                    const std::filesystem::path& targetDirectory);

}  // namespace sdurws::ird::optimization

#endif  // SDURWS_IRD_OPTIMIZATION_EXPORT_HPP
