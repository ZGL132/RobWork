/**
 * @file   Export.hpp
 * @brief  报告导出服务——IReportExportService（§9.5：导出编排＝渲染→二次
 *         渲染确定性→一致性→发布/写出）及其注入缝。
 *
 * 设计依据：
 *   - units/reporting.md §7.4（幂等导出与冲突拒绝——外部目标同字节幂等
 *     无操作；NeverOverwrite 默认拒绝/OverwriteAtomic 原子替换；失败保留
 *     选择与路径可重试）、§8.5（导出编排时序图步①~⑤——取报告→单次矩阵
 *     →逐格式渲染→二次渲染字节比对→一致性检查→不一致不出工件）、§9.5
 *     （ExportDestination/ReportExportRequest/ReportExportResult/
 *     IReportExportService/IReportIoFactory 契约原文）、§9.9（io 行——
 *     "IO 失败→ExportFailed/DiskFull；取消→清理"）
 *   - 需求 RPT-01（幂等/冲突）、RPT-02（HTML 首版主格式——formats 必含）、
 *     PM-07（只读项目两路径）、PM-08（未完成工件不入清单）、UX-03（取消
 *     非错误）、§16 验收要点（导出失败保留选择与路径可重试）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 1~4
 *
 * 背景说明（编排职责的分界）：
 *   导出服务是 §8.5 时序图的编排者：渲染器（RPT-T06）产内存字节、一致性
 *   检查器（RPT-T07）做逐字段回读比对、归档协调器（本任务 Archive.hpp）
 *   编排项目内汇集座——本服务把四者按序串接，并承载**外部路径**（项目外
 *   用户选择目录）的写出与幂等/冲突判定。任何一步失败/取消＝不出工件：
 *   项目内不 finalize（未完成工件不列入清单——PM-08）、项目外零写出或仅
 *   完整原子替换单元落地（io 原子协议——P-RPT-1）。
 *
 * 落位偏差登记（DTB §5.4 口径——详见单元卡 §14.4 v0.12）：
 *   ①IReportResolver（reportId→ReviewReport 值解析注入缝）：§9.5 前置行
 *     "reportId 可解析（本会话构建或自已发布清单加载）"的实现承载——报告
 *     值的来源（构建会话/已发布清单加载）归调用方装配，服务只见注入缝。
 *   ②ReportProgressCallback 以 std::function 别名承载（§9.5 签名第三参；
 *     进度快照复用 Builder.hpp 的 ReportProgress 与阶段 token——同一状态
 *     词汇，§7.1）。
 *   ③取消结果形态＝五成员全空（§9.5 结果结构无取消位——与构建器 outcome
 *     的 UX-03 取消形态同款：成功必有 published 或 files 在场，全空即取消
 *     信号，无歧义）。
 *   ④withEvidenceBundle==true 的处理＝Usage 拒绝：证据包组装归 RPT-T10
 *     （§11 任务拆分——Bundle.hpp 其产物），本服务按 §9.5 原文携带该请求
 *     字段，能力到位前显式拒绝而非静默忽略（不吞语义——NFR-COR-03 同
 *     精神；RPT-T10 落地后改为组装编排，此处为显式边界非桩）。
 *   ⑤外部幂等判定的存在性/字节探测经只读文件读（std::filesystem/ifstream
 *     ——P-RPT-1 纪律约束的是 io 单元 canonical **写出**设施的编译边，读
 *     探测不属编码/转义行为〔SA-12 不涉〕；全部写入仍经注入的
 *     makeAtomicTarget 原子协议，零写路径旁路）。
 *
 * 工件相对命名（外部目录与项目内归档同名——一致性）：report.html /
 * report-data.json / report.csv；项目内 manifest 另占 report.json（§7.3
 * 原文）——JSON 格式工件让位为 report-data.json（§7.3 与 §8.3 的文件名
 * 在"report.json"上重叠，本命名决策登记 §14.4）。
 *
 * 线程约束：单次 exportReport 调用单线程（§9.5 线程行）；不同导出会话可
 * 并行（不同目标/不同 reportId——同 reportId 同目标并发由汇集座互斥拒绝，
 * §9.5 线程行原文）。注入引用须覆盖服务存续期（借用——L5 装配持有）。
 */

#ifndef SDURWS_IRD_REPORTING_EXPORT_HPP
#define SDURWS_IRD_REPORTING_EXPORT_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>      // core::Digest256（导出文件摘要）
#include <sdurws/ird/core/DiagData.hpp>    // core::DiagnosticRecord（结果诊断）
#include <sdurws/ird/core/Identity.hpp>    // core::ProjectId

#include <sdurws/ird/diagnostics/Redaction.hpp>  // diagnostics::IRedactionService（C-3 消费）

#include <sdurws/ird/reporting/Archive.hpp>       // 汇集座/索引/协调器（项目内发布）
#include <sdurws/ird/reporting/Builder.hpp>       // ReportCancelToken/ReportProgress（§9 复用）
#include <sdurws/ird/reporting/Consistency.hpp>   // 一致性检查器/CSV 回读工厂（§9.4）
#include <sdurws/ird/reporting/Errors.hpp>        // ReportError/ReportErrorCode
#include <sdurws/ird/reporting/Identity.hpp>      // ReportId/PublishedReportRecord
#include <sdurws/ird/reporting/Render.hpp>        // 渲染器/io 注入工厂（§9.3/§9.5）
#include <sdurws/ird/reporting/ReportModel.hpp>   // ReviewReport（解析目标值）

namespace sdurws::ird::reporting {

// =====================================================================
// §9.5 契约值类型（原文逐字；形状补全处逐项注释）
// =====================================================================

/**
 * @brief 导出文件记录（§9.5 ReportExportResult.files 元素：{path, format,
 *        sha256}）。
 *
 * path 为落盘绝对路径（外部导出成功/幂等命中均返回——幂等命中时为既有
 * 文件路径、摘要为既有字节摘要，§7.4"返回成功＋既有摘要"）；sha256 为该
 * 文件字节摘要（core 唯一算法）。
 */
struct ExportedFile {
    std::filesystem::path path;   ///< 落盘路径（外部目录下按工件相对名编址）
    ReportRenderFormat format = ReportRenderFormat::Html;  ///< 工件格式
    core::Digest256 sha256{};     ///< 文件字节 SHA-256（core 唯一算法）
};

/**
 * @brief 导出目标（§9.5 原文三字段）。
 *
 * kind 两值：ProjectArchive＝项目内归档（经 IReportArtifactSink——唯一
 * 项目写路径）；ExternalPath＝项目外用户选择目录（经 io 原子协议注入实现
 * ——P-RPT-1）。externalPath 仅 ExternalPath 必填（语义＝目标**目录**：
 * 多格式导出在其下按工件相对名编址；目录不存在则创建——见类注释的边界）；
 * replace 仅 ExternalPath 生效（项目内归档无覆盖语义——只增/幂等/冲突）。
 */
struct ExportDestination {
    /// 目标类别（§9.5 原文 enum Kind）。
    enum Kind { ProjectArchive, ExternalPath };

    Kind kind = ProjectArchive;             ///< 目标类别
    std::filesystem::path externalPath;     ///< 外部目标目录（ExternalPath 必填——io 口径）
    ReplacePolicy replace = ReplacePolicy::NeverOverwrite;  ///< 外部导出默认不覆盖（§9.5 原文）
};

/**
 * @brief 导出请求（§9.5 原文五字段）。
 *
 * formats 合法性：≥1 且必含 Html（RPT-02——HTML 为首版主格式，§9.5 非法
 * 调用行"formats 不含 Html 即发布（Usage）"）；重复格式＝调用方违约
 * （Usage）。withEvidenceBundle 语义见文件头落位偏差④。
 */
struct ReportExportRequest {
    core::ProjectId project;    ///< 归属项目（汇集座编址/只读检查键）
    ReportId reportId;          ///< 已构建报告（值经 IReportResolver 解析——前置）
    std::vector<ReportRenderFormat> formats;  ///< ≥1，必含 Html（RPT-02）
    ExportDestination destination;            ///< 导出目标
    bool withEvidenceBundle = false;          ///< 同时导出证据包（RPT-03——见偏差④）
};

/**
 * @brief 导出结果（§9.5 原文五成员）。
 *
 * 形态约定：
 *   - 成功（ProjectArchive）＝published 在场（幂等命中时为既有记录——
 *     §7.4"返回既有 PublishedReportRecord"）；files 为空（项目内不经外部
 *     文件返回）；
 *   - 成功（ExternalPath）＝files 在场（逐工件一项）；published 为空；
 *   - 失败＝error 在场＋diagnostics 随交付（NFR-COR-03 不静默）；
 *   - 取消＝五成员全空（落位偏差③——UX-03 正常取消非错误，构建器
 *     outcome 同款形态）。
 */
struct ReportExportResult {
    std::optional<PublishedReportRecord> published;    ///< ProjectArchive 成功时（§9.5 原文）
    std::vector<ExportedFile> files;                   ///< 外部导出逐文件（幂等命中含既有）
    std::optional<core::Digest256> bundleDigest;       ///< 证据包摘要（RPT-T10 能力位——恒空）
    std::vector<core::DiagnosticRecord> diagnostics;   ///< 过程诊断（失败随交付）
    std::optional<ReportError> error;                  ///< 失败错误（成功/取消缺席）
};

/// 导出进度回调（§9.5 签名第三参——落位偏差②；快照复用 §9 章首类型，
/// 回调在导出线程同步执行，实现方不得阻塞或重入导出调用）。
using ReportProgressCallback = std::function<void(const ReportProgress&)>;

// =====================================================================
// IReportResolver——reportId→报告值解析注入缝（落位偏差①）
// =====================================================================

/**
 * @brief 报告值解析器（reporting 定义、调用方装配实现——落位偏差①）。
 *
 * §9.5 前置行"reportId 可解析（本会话构建或自已发布清单加载）"的承载：
 * 报告值来源（本会话构建器缓存/已发布清单反序列化加载）归装配侧，服务
 * 不持有构建器或项目读端口——解析不到（未构建/未发布）＝前置违约 Usage。
 *
 * 返回值拷贝（ReviewReport 值语义——服务内渲染/一致性/发布全链消费只读
 * 值，与 §9.8 预览"只读值"同口径）。线程：实现方承诺并发只读安全。
 */
class IReportResolver {
public:
    virtual ~IReportResolver() = default;

    /**
     * @brief 解析报告值。
     * @param reportId [in] 报告对象身份
     * @return 冻结报告值拷贝；不可解析→nullopt（调用方据以 Usage 拒绝）
     */
    virtual std::optional<ReviewReport> tryResolve(ReportId reportId) const = 0;
};

// =====================================================================
// 工件相对命名（文件头"工件相对命名"节的常量面）
// =====================================================================

/// HTML 工件相对名。
inline constexpr std::string_view kHtmlArtifactRelPath = "report.html";
/// JSON 工件相对名（report.json 让位于 §7.3 manifest——文件头命名决策）。
inline constexpr std::string_view kJsonArtifactRelPath = "report-data.json";
/// CSV 工件相对名（§8.3 主表名）。
inline constexpr std::string_view kCsvArtifactRelPath = "report.csv";

/**
 * @brief 取渲染格式的工件相对名（三格式全表；未知值不可达——枚举全表）。
 * @param format [in] 渲染格式
 * @return 相对名（静态存储期字面量）
 */
std::string_view artifactRelPath(ReportRenderFormat format) noexcept;

// =====================================================================
// IReportExportService——导出服务（§9.5 原文接口）
// =====================================================================

/**
 * @brief 报告导出服务（§9.5——服务级；内部编排渲染/一致性/归档，§7.1/§8.5）。
 *
 * 契约（§9.5 表逐行）：
 *   - 前置/后置：成功＝按 §7.4 幂等/冲突规则发布或写出；失败/取消＝目标
 *     不变（外部）或未完成工件（项目内），选择与路径保留可重试（§16 验收
 *     要点）；一致性不通过不出工件（任何目标——项目内不 finalize、项目外
 *     零写出）；
 *   - 错误：ConsistencyMismatch/ArchiveConflict/ExportFailed/DiskFull/
 *     ReadOnlyStore/Usage；
 *   - 取消：逐格式/逐工件检查点（io 令牌贯通——本单元自有同形令牌，§9
 *     章首块）；
 *   - 确定性：同 (报告, 格式集, 版本)→字节相同；幂等判定仅依赖 manifest
 *     摘要（§7.4）。
 */
class IReportExportService {
public:
    virtual ~IReportExportService() = default;

    /**
     * @brief 导出报告（§8.5 时序：解析→矩阵→逐格式渲染〔含二次渲染确定性
     *        比对〕→一致性→发布/写出）。
     *
     * @param request  [in] 导出请求（身份/格式/目标——合法性见类型注释）
     * @param cancel   [in] 协作取消令牌（可空＝不取消；检查点见类注释）
     * @param progress [in] 进度回调（可空＝不订阅；阶段 token 复用 §7.1
     *                 状态词——rendering/verifying/publishing）
     *
     * @return 导出结果（形态约定见 ReportExportResult 注释）
     */
    virtual ReportExportResult exportReport(const ReportExportRequest& request,
                                            const ReportCancelToken* cancel = nullptr,
                                            ReportProgressCallback progress = {}) = 0;
};

// =====================================================================
// ReportExportService——产品实现
// =====================================================================

/**
 * @brief 导出服务产品实现（§9.5"持渲染器/检查器/汇集座/注入 io 工厂的
 *        引用"的构造面；渲染器与检查器为服务内构成员——HTML/JSON/CSV 三
 *        实现与 FieldConsistencyChecker 按 §9.3/§9.4 注入构造）。
 *
 * exportReport 编排明细（acceptance 1~4 的承载）：
 *   1. 请求校验（Usage）：reportId 有效可解析、formats ≥1 含 Html 无重复、
 *      ExternalPath 时路径非空；withEvidenceBundle==true → Usage（偏差④）；
 *   2. 取消检查点（入轨）→全空结果；
 *   3. 矩阵单次提取＋逐格式渲染（渲染失败即返）；每格式渲染后立即二次
 *      渲染并字节比对（§8.5 步③——不一致＝RenderFailed：渲染层非确定，
 *      非数据问题）；逐格式渲染后取消检查点；
 *   4. 一致性检查（§8.5 步④⑤，≥2 格式才有意义）：不一致→
 *      ConsistencyMismatch＋RPT-CONSISTENCY-MISMATCH 诊断，零写出；
 *   5. ProjectArchive：装配 manifest（身份/生成信息自报告、工件摘要列自
 *      产物）→归档协调器 publish→结果映射（幂等命中 published＝既有记录；
 *      ReadOnlyStore→error＋两路径诊断——PM-07）；
 *   6. ExternalPath：目录就绪后逐工件——目标已存在：读字节比对摘要，一致
 *      ＝幂等成功无操作（不调 makeAtomicTarget——文件 mtime/哈希不变，
 *      acceptance 1）；不一致：NeverOverwrite→ArchiveConflict 拒绝＋
 *      RPT-EXPORT-FAILED 诊断（cause 引 io 族码 IO-PACK-TARGET-EXISTS——
 *      码值权威归 io/diagnostics）；OverwriteAtomic→makeAtomicTarget
 *      原子替换（替换前先前输出完整保留——io 原子协议）；目标不存在→
 *      原子写出。逐工件写前取消检查点——取消→abort 在途目标（清理临时）
 *      →全空结果；commit 失败→ExportFailed（目标不变——可重试）。
 *
 * 线程：单次调用单线程（§9.5 线程行）。
 */
class ReportExportService final : public IReportExportService {
public:
    /**
     * @brief 注入构造（§9.5 生命周期行"持渲染器/检查器/汇集座/注入 io
     *        工厂的引用"）。
     *
     * @param ioFactory  [in] io 注入工厂（borrow；外部写出原子协议/渲染
     *                   canonical 写出载体——P-RPT-1）
     * @param redaction  [in] 脱敏服务（borrow；渲染器双保险消费——C-3）
     * @param sink       [in] 工件汇集座（borrow；项目内发布唯一写路径）
     * @param index      [in] 只读发布清单（borrow；与 sink 同源——Archive 偏差①）
     * @param csvReaders [in] CSV 回读工厂（borrow；一致性检查 CSV 维度）
     * @param resolver   [in] 报告值解析器（borrow；落位偏差①）
     */
    ReportExportService(const IReportIoFactory& ioFactory,
                        const diagnostics::IRedactionService& redaction,
                        IReportArtifactSink& sink, const IReportPublishedIndex& index,
                        const IReportCsvReaderFactory& csvReaders, const IReportResolver& resolver);

    /// 非拷贝（借用语义）。
    ReportExportService(const ReportExportService&) = delete;
    ReportExportService& operator=(const ReportExportService&) = delete;

    ReportExportResult exportReport(const ReportExportRequest& request,
                                    const ReportCancelToken* cancel = nullptr,
                                    ReportProgressCallback progress = {}) override;

private:
    const IReportIoFactory* m_ioFactory;          ///< io 注入工厂（借用；P-RPT-1）
    IReportArtifactSink* m_sink;                  ///< 汇集座（借用）
    const IReportPublishedIndex* m_index;         ///< 只读发布清单（借用）
    const IReportResolver* m_resolver;            ///< 报告值解析器（借用）
    HtmlReportRenderer m_htmlRenderer;            ///< HTML 渲染器（服务内持有——§9.3）
    JsonReportRenderer m_jsonRenderer;            ///< JSON 渲染器（服务内持有）
    CsvReportRenderer m_csvRenderer;              ///< CSV 渲染器（服务内持有）
    FieldConsistencyChecker m_consistencyChecker; ///< 一致性检查器（服务内持有——§9.4）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_EXPORT_HPP
