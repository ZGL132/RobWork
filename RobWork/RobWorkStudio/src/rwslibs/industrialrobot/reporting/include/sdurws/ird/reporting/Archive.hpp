/**
 * @file   Archive.hpp
 * @brief  报告归档协调——IReportArtifactSink 工件汇集座契约（project 实现）、
 *         ReportArtifactManifest（§7.3）与 IReportArchiveCoordinator（reporting
 *         实现的编排器，§9.6）。
 *
 * 设计依据：
 *   - units/reporting.md §7.3（ReportArtifactManifest 字段表＋报告工件状态图
 *     ——begin 冲突预检/Staged→Partial→Finalized/abandon 未完成工件不入清单
 *     ——"文件存在≠已发布"）、§7.4（幂等导出与冲突拒绝——判定键＝manifest
 *     内容摘要 D-02，不以 ReportId、不以目标路径；冲突差异定位；磁盘不足
 *     abandon＋清理＋可重试）、§9.6（IReportArtifactSink/IReportArchiveCoordinator
 *     契约原文——D-13/D-14 模式的报告侧）、§9.9（project 行——"StoreError
 *     透传→ReportError"；副作用行——项目内 reports/ 写入唯一路径经汇集座）
 *   - 需求 RPT-01（报告目录追加幂等、冲突拒绝）、PM-07（只读项目两路径）、
 *     PM-08（未完成工件不入清单）、PM-03（归档失败＝责任终结）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 1~5（幂等/冲突/磁盘/
 *     只读/取消/sink 契约与编排）
 *
 * 背景说明（为什么写路径只有一个汇集座）：
 *   项目内 reports/ 的持久化一致性（manifest 原子发布、目录编址、恢复扫描、
 *   随包导出树镜像）全部归 project（§7.4 末段"project 负责提交指针和持久化
 *   一致性"）；reporting 只经 IReportArtifactSink **请求**写入——reporting
 *   侧不存在第二条项目写路径，"绕过汇集座直写 reports/"是 §9.6 非法调用行。
 *   幂等（同内容零重写）与冲突（同 reportId 不同内容拒绝）两判定由 project
 *   实现的汇集座承载（它见得到磁盘事实），reporting 侧协调器只做**编排**：
 *   begin→writeArtifact×N→finalize 顺序、任一非 Ok→abandon（§9.6 前置/后置）。
 *
 * 落位偏差登记（DTB §5.4 口径——详见单元卡 §14.4 v0.12）：
 *   ①IReportPublishedIndex（只读发布清单接口）为本头增补的注入缝：§9.6 的
 *     sink 仅 begin/writeArtifact/finalize/abandon 四方法（acceptance 5"逐字
 *     一致"面——本头不增删其签名），但 §7.4 幂等行要求"返回既有
 *     PublishedReportRecord"、§9.6 协调器要求 listPublished 仅 Finalized
 *     ——既有记录的读取必须另有通道，否则协调器要么伪造 publishedAtUtc
 *     要么无法列出已发布报告。该接口与 sink 同源于 project 侧实现（fake
 *     同步交付），属"写入端口＋只读清单"的成对注入缝，非第二写路径。
 *   ②publish() 的取消检查点经构造注入的 ReportCancelToken 承载（§9.6 取消
 *     行"逐工件检查点"的实现面；§9.6 publish 签名无令牌参数——协调器随
 *     导出会话构造〔§9.6 生命周期行"协调器随导出会话"〕，令牌随会话注入，
 *     签名零改动）。
 *   ③ReportEndReason/ArtifactSessionRef/ArchiveOutcome/ReportListingEntry
 *     为 §9.6 引用但未定义形状的值类型——本头按最小值语义冻结（§9.5
 *     "签名为实现建议"同口径）。
 *   ④manifest 内容摘要（幂等判定键）的规范化编码不含 finalizedAtUtc——
 *     D-04 同源（发布时刻是发布事实非内容事实；同输入重建摘要一致是幂等
 *     的前提），与 §7.4 判定键公式一致。
 *
 * P-RPT-4 处置（acceptance 5）：IReportArtifactSink 为 reporting 单侧冻结
 *   契约（project.md 未定义对应公共接口）——以单元卡 §9.6 为起点在本头
 *   落位；project 详设修订时按 D-13/D-14 语义交叉核对；project 侧实现随其
 *   阶段 B 任务对齐，本任务不私改 project 公共头（本头即交接凭据——§12.2
 *   project 行"reports/ 写入端口实现"的输入）。
 *
 * P-RPT-9 处置（acceptance 5）：core 摘要/身份类型消费（ContentIdentity/
 * Digest256/RunId/ProjectId）以 core.md v0.1（Draft）签名为基线；冻结出
 * diff 后按影响面增量同步留痕，不私改 core。
 *
 * 线程约束：IReportArtifactSink 的会话方法（begin/writeArtifact/finalize/
 * abandon）非线程安全——同一会话引用仅归属线程使用（§9.6 线程行"汇集座由
 * project 实现承诺互斥"指跨会话并发；会话内单线程）。IReportArchiveCoordinator
 * 会话单线程（§9.6 线程行）。IReportPublishedIndex 查询并发只读安全（由
 * 实现方承诺——与 sink 同源实现）。
 */

#ifndef SDURWS_IRD_REPORTING_ARCHIVE_HPP
#define SDURWS_IRD_REPORTING_ARCHIVE_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sdurws/ird/core/Digest.hpp>     // core::ContentIdentity/Digest256（P-RPT-9 基线）
#include <sdurws/ird/core/Identity.hpp>   // core::ProjectId/BranchId/RevisionId/RunId

#include <sdurws/ird/reporting/Builder.hpp>     // ReportCancelToken（取消检查点注入）
#include <sdurws/ird/reporting/Errors.hpp>      // ReportError/ReportErrorCode/diagcodes
#include <sdurws/ird/reporting/Identity.hpp>    // ReportId/ReportLevel/PublishedReportRecord
#include <sdurws/ird/reporting/Render.hpp>      // ReportRenderFormat（工件清单 format 列）
#include <sdurws/ird/reporting/ReportModel.hpp> // ReviewReport（publish 编排输入）

namespace sdurws::ird::reporting {

// =====================================================================
// 会话引用与结束原因（§9.6 签名中的值类型——落位偏差③的最小形状）
// =====================================================================

/**
 * @brief 工件会话引用（begin 返回、writeArtifact/finalize/abandon 回传的
 *        会话句柄——一次归档会话一个）。
 *
 * 值句柄而非指针：汇集座（project 实现）内部以编号索引会话表，reporting
 * 侧不持有任何 project 内部对象指针——跨单元值边界（§3.3 注入模式同则）。
 * 保留值纪律：value==0＝无效引用（begin 失败不产生会话——失败经异常轨，
 * 不返回无效引用让调用方猜测）；非零值由汇集座分配并保证进程内唯一。
 *
 * 生命周期：自 begin() 起至 abandon()/finalize 成功止；终态后再经本引用
 * 调用＝调用方契约违约（汇集座以 ContextClosed/Usage 拒绝——§9.6 前置行
 * "存储上下文 Active"的会话侧对偶）。拷贝/比较为纯值操作。
 */
struct ArtifactSessionRef {
    /// 会话编号（0＝无效保留值；非零由汇集座分配——见类注释）。
    std::uint64_t value = 0;

    /// 非零即有效（保留值纪律——begin 失败走异常轨，不产生无效引用）。
    bool isValid() const noexcept { return value != 0; }

    /// 纯值相等（句柄无业务排序语义）。
    bool operator==(const ArtifactSessionRef& o) const noexcept { return value == o.value; }
    bool operator!=(const ArtifactSessionRef& o) const noexcept { return value != o.value; }
};

/**
 * @brief 归档会话结束原因（§9.6 abandon 注释原文三值："Completed/Canceled/
 *        Failed——责任终结"）。
 *
 * abandon＝归档责任终结（§7.4"归档失败"行：不永久等待、已 Finalized 的
 * 其他报告不受影响、清理失败仅残留诊断）——本枚举把"会话为何终结"告知
 * 汇集座，供其决定清理深度与恢复诊断登记（PM-08 口径：未完成工件不列入
 * 已发布清单、恢复诊断列出）。
 */
enum class ReportEndReason : std::uint8_t {
    Completed,  ///< completed——正常完成（finalize 成功后的收尾路径；正常流程
                ///< 不经 abandon 终结，此值供汇集座幂等收尾）
    Canceled,   ///< canceled——协作取消（UX-03：正常取消非错误——清理临时、
                ///< 不产生错误诊断）
    Failed,     ///< failed——任一步失败（写入/冲突/环境错误——未完成工件按
                ///< PM-08 处置）
};

/// 取结束原因稳定 token（kebab 形；静态存储期——NFR-COR-02 同口径）。
std::string_view token(ReportEndReason reason) noexcept;

// =====================================================================
// IReportArtifactSink 契约值类型（§9.6 原文逐字）
// =====================================================================

/**
 * @brief begin 请求（§9.6 原文四字段）。
 *
 * contentIdentity 为冲突预检键（§9.6 注释行："冲突预检键〔幂等判定用
 * manifest 摘要，finalize 时复核〕"）——begin 只登记待比对事实，真正的
 * 幂等/冲突判定在 finalize（摘要比对，D-14 同构）；idempotencyCheckRequired
 * 缺省 true——false 语义＝调用方自证无幂等诉求（本单元协调器恒用缺省值，
 * 形参面保留 §9.6 原文）。
 */
struct ReportArtifactBeginRequest {
    ReportId reportId;                       ///< 报告对象身份（编址 reports/<report-id>/）
    core::ProjectId project;                 ///< 归属项目（writable 检查归 project——§6.8）
    core::ContentIdentity contentIdentity;   ///< 冲突预检键（报告内容身份——非零）
    bool idempotencyCheckRequired = true;    ///< 幂等预检开关（§9.6 原文缺省 true）
};

/**
 * @brief 单个工件写入载荷（§9.6 原文两字段）。
 *
 * relPath 为**纯相对路径**（report.json 相对的正斜杠路径——SP-2 口径：
 * 禁绝对路径/上溯段，汇集座拒绝违约）；bytes 为完整工件字节（渲染产物
 * canonical 字节——摘要已记入 manifest 工件清单）。
 */
struct ArtifactWrite {
    std::string relPath;                    ///< 相对路径（正斜杠；禁上溯——SP-2）
    std::vector<std::uint8_t> bytes;        ///< 完整字节（canonical）
};

/**
 * @brief 工件操作状态（§9.6 原文六值——writeArtifact/finalize 共用）。
 *
 * 判定语义（§7.4 表＋§9.6 finalize 注释）：
 *   - Ok：本次操作成功落盘；
 *   - IdempotentHit：幂等命中（同内容已存在——零重写；write 只增幂等的
 *     "目标存在且摘要一致跳过"与 finalize 的"manifest 摘要一致"同源）；
 *   - Conflict：冲突拒绝（同 reportId 已 Finalized 且摘要不一致——不覆盖，
 *     RPT-ARCHIVE-CONFLICT 面）；
 *   - DiskFull：磁盘不足（比较型诊断所需/可用随协调器诊断交付——§7.4 表）；
 *   - WriteRejected：存储侧拒绝写入（只读/越权/校验失败等环境类拒绝）；
 *   - ContextClosed：存储上下文已关闭（会话/上下文生命周期越界）。
 */
enum class ArtifactStatus : std::uint8_t {
    Ok,
    IdempotentHit,
    Conflict,
    DiskFull,
    WriteRejected,
    ContextClosed,
};

/// 取操作状态稳定 token（kebab 形；静态存储期——诊断定位用）。
std::string_view token(ArtifactStatus status) noexcept;

// =====================================================================
// ReportArtifactManifest（§7.3 字段表——持久化为 reports/<report-id>/
// report.json；幂等/冲突判定的事实载体）
// =====================================================================

/**
 * @brief 工件清单条目（§7.3 artifacts[] 元素五字段原文）。
 *
 * relPath 为纯相对路径（正斜杠、禁上溯）；sha256 为工件字节摘要（core
 * ContentDigester 唯一算法——§4.1）；rendererVersion/templateVersion 入
 * 清单（§14.2 R-6：复验按清单内版本）。
 */
struct ArtifactManifestEntry {
    std::string relPath;                    ///< 相对路径（report.json 相对；正斜杠）
    ReportRenderFormat format = ReportRenderFormat::Html;  ///< 工件格式
    core::Digest256 sha256{};               ///< 工件字节 SHA-256（core 唯一算法）
    std::uint64_t sizeBytes = 0;            ///< 工件字节数（单位：字节）
    std::uint32_t rendererVersion = 0;      ///< 渲染器版本（入清单——R-6 复验凭据）
    std::uint32_t templateVersion = 0;      ///< 模板版本（入清单——按格式取常量）

    /// 全字段精确相等（摘要字节等值）。
    bool operator==(const ArtifactManifestEntry& o) const
    {
        return relPath == o.relPath && format == o.format && sha256 == o.sha256
               && sizeBytes == o.sizeBytes && rendererVersion == o.rendererVersion
               && templateVersion == o.templateVersion;
    }
    bool operator!=(const ArtifactManifestEntry& o) const { return !(*this == o); }
};

/**
 * @brief 报告工件清单（§7.3 字段表全字段——report.json 的内存形态）。
 *
 * 字段与 §7.3 表逐行对应：formatId/schemaVersion（格式标识）、报告身份链
 * （reportId/reportVersion/supersedes）、level、数据源锚（project/branch/
 * revision 三身份）、双层内容身份（dataIdentity/contentIdentity——幂等/
 * 冲突判定依据）、生成信息（generatedAtUtc/generatedBy/generatorVersion
 * ——身份排除项在此持久化，D-04）、工件清单 artifacts[]、来源运行
 * sourceRuns[]（另存 sources.json 供 project §8.9 勾选规则消费）、finalize
 * 时间与自身规范化摘要（finalizedAtUtc/manifestDigest——D-14 幂等重投递
 * 判据）。
 *
 * 生命周期约定：artifacts/finalizedAtUtc/manifestDigest 三者由归档协调器
 * 在 publish 内装配/盖章（调用方不填——填了也会被协调器覆写）；其余字段
 * 由导出服务自报告值装配。manifestDigest 的计算域见 computeManifestDigest。
 *
 * 值语义；线程安全：纯值（发布后作为持久化事实不再变更——PA-2 精神）。
 */
struct ReportArtifactManifest {
    /// 格式标识（§7.3 原文 "ird-report"）。
    std::string formatId = "ird-report";
    /// 格式版本（§7.3 uint32；升级走版本拒绝＋指引——PM-06 执行侧口径）。
    std::uint32_t schemaVersion = 1;

    ReportId reportId;                        ///< 报告对象身份（编址键）
    std::uint32_t reportVersion = 1;          ///< 评审演化版本（≥1）
    std::optional<ReportId> supersedes;       ///< 被本版取代的报告（演化链）
    ReportLevel level = ReportLevel::B;       ///< 报告级别
    core::ProjectId project;                  ///< 数据源锚三身份
    core::BranchId branch;
    core::RevisionId revision;
    core::ContentIdentity dataIdentity;       ///< 数据源身份（D-03 双层身份之外层）
    core::ContentIdentity contentIdentity;    ///< 内容身份（幂等/冲突判定依据——唯一）

    std::chrono::system_clock::time_point generatedAtUtc{};  ///< 生成时刻（UTC；不入身份）
    std::string generatedBy;                  ///< 生成者（principal/batch；不入身份）
    std::string generatorVersion;             ///< 生成器版本（不入身份）

    std::vector<ArtifactManifestEntry> artifacts;  ///< 工件清单（逐工件摘要＋版本）
    std::vector<core::RunId> sourceRuns;           ///< 引用的结果集（sources.json 数据源）

    /// finalize 时刻（UTC——协调器盖章；**不入** manifestDigest，见其注释）。
    std::chrono::system_clock::time_point finalizedAtUtc{};
    /// 自身规范化摘要（D-14 幂等重投递判据——协调器在 finalize 前计算盖章）。
    core::ContentIdentity manifestDigest{};

    /// 全字段精确相等（身份/摘要字节等值、时间点精确相等——发布事实无容差）。
    bool operator==(const ReportArtifactManifest& o) const
    {
        return formatId == o.formatId && schemaVersion == o.schemaVersion
               && reportId == o.reportId && reportVersion == o.reportVersion
               && supersedes == o.supersedes && level == o.level && project == o.project
               && branch == o.branch && revision == o.revision
               && dataIdentity == o.dataIdentity && contentIdentity == o.contentIdentity
               && generatedAtUtc == o.generatedAtUtc && generatedBy == o.generatedBy
               && generatorVersion == o.generatorVersion && artifacts == o.artifacts
               && sourceRuns == o.sourceRuns && finalizedAtUtc == o.finalizedAtUtc
               && manifestDigest == o.manifestDigest;
    }
    bool operator!=(const ReportArtifactManifest& o) const { return !(*this == o); }
};

/**
 * @brief 计算 manifest 内容摘要——幂等判定键（§7.4/D-02 原文："判定键＝
 *        (contentIdentity, format 集, rendererVersion, templateVersion) 的
 *        manifest 内容摘要"；**不以 ReportId、不以目标路径**）。
 *
 * 规范化编码域（canonical——同工件集同摘要，NFR-COR-02）：
 *   magic "IRDRPTM1"（8 字节 ASCII）＋编码器版本 u32＋contentIdentity 32
 *   字节＋工件数 u32＋逐工件（按 relPath 字节序稳定排序）：relPath 长度
 *   u32＋relPath 字节＋format u8＋sha256 32 字节＋sizeBytes u64＋
 *   rendererVersion u32＋templateVersion u32。整数一律大端（ReportCodec
 *   §4.4 同口径）。
 *
 * 排除域（与判定键公式逐项对应）：
 *   - reportId/supersedes/project/branch/revision/reportVersion——D-02：
 *     按 Id 判幂等会误放行内容变化（评审演化链上同 dataIdentity 不同
 *     contentIdentity 属不同内容）；
 *   - generatedAtUtc/generatedBy/generatorVersion/finalizedAtUtc——D-04：
 *     生成/发布事实不入内容判定（同输入重建摘要一致是幂等前提）；
 *   - formatId/schemaVersion——格式标识常量（进判定会使版本升级后的重导出
 *     恒判冲突，与 §7.4"幂等成功"语义相悖）。
 *   relPath 保留在编码域：它是"format 集"的构成表达（relPath↔format 映射
 *   属工件集事实，非目标路径——D-02 排除的是**目标路径**〔ExternalPath/
 *   目录编址〕，不是工件相对名）。
 *
 * @param manifest [in] 工件清单（artifacts 至少一项；重复 relPath＝调用方
 *                违约——fail-fast）
 * @return 32 字节摘要（core ContentDigester 唯一算法——SHA-256）
 *
 * @throws ReportError(Usage) artifacts 为空或存在重复 relPath（无工件集
 *         即无幂等判定对象；重复键＝清单装配违约）
 * @throws ReportError(DataInvalid) relPath 含 NUL 字节（编码层禁止——§4.4 同则）
 *
 * 复杂度：O(n log n)（排序；n＝工件数）。纯函数：同输入同摘要、无副作用。
 */
core::ContentIdentity computeManifestDigest(const ReportArtifactManifest& manifest);

/// manifest 自身的持久化相对路径（§7.3 原文：reports/<report-id>/report.json
/// ——project §4.1 行为契约的承载；"唯一完整标志"D-13 的文件名）。
inline constexpr std::string_view kManifestRelPath = "report.json";

// =====================================================================
// IReportArtifactSink——工件汇集座（§9.6 原文四方法逐字；project 实现）
// =====================================================================

/**
 * @brief 报告工件汇集座（§9.6——reporting 定义、project 实现；writable
 *        检查归 project §6.8）。
 *
 * 四方法语义（§9.6 注释逐条——acceptance 5 的"逐字一致"面）：
 *   - begin：前置＝存储上下文 Active ∧ writable、reportId 目录未被其他会话
 *     占用；后置＝目录已建（Staged）；同 reportId 已 Finalized→记录待比对
 *     （finalize 判幂等）。前置违约/环境错误（只读存储、上下文非 Active、
 *     目录被占用）经异常轨 ReportError 上抛（§9.9 project 行"StoreError
 *     透传→ReportError"；ReadOnlyStore 即只读项目拒绝面）。
 *   - writeArtifact：只增（批次文件只增——§7.3 状态图；重试续写时目标存在
 *     且摘要一致→IdempotentHit 跳过——§9.6 合法调用行）。
 *   - finalize：report.json 原子发布＝完整（D-13——唯一"完整"标志）；幂等：
 *     manifest 摘要一致→IdempotentHit（零重写）；不一致→Conflict
 *     （RPT-ARCHIVE-CONFLICT）；不覆盖已 Finalized 内容。
 *   - abandon：责任终结（§7.4"归档失败"行——不永久等待；已 Finalized 的
 *     其他报告不受影响；清理失败仅残留诊断、不抛出）。
 *
 * 错误轨约定：begin 的环境错误抛 ReportError；writeArtifact/finalize 的
 * 操作结果经 ArtifactStatus 返回（操作级失败不是异常——可重试语义由状态
 * 表达）；abandon 不抛（责任终结语义——清理失败仅残留诊断）。
 *
 * 线程约束：同一会话引用的各方法仅归属线程调用（会话内单线程）；跨会话
 * 并发互斥由 project 实现承诺（对齐其 writer 互斥——project §9.8）。
 */
class IReportArtifactSink {
public:
    virtual ~IReportArtifactSink() = default;

    /**
     * @brief 开启归档会话（目录已建＝Staged——§7.3 状态图起点）。
     * @param request [in] 会话请求（身份/预检键——见类型注释）
     * @return 会话引用（非零——无效引用不产生；失败走异常轨）
     *
     * @throws ReportError 存储上下文非 Active/只读（ReadOnlyStore）/目录被
     *         其他会话占用（ArchiveConflict）等环境与状态错误（§9.9 透传）
     */
    virtual ArtifactSessionRef begin(const ReportArtifactBeginRequest& request) = 0;

    /**
     * @brief 写入单个工件（只增——批次文件只增）。
     * @param session [in] begin 返回的会话引用（无效/终态＝调用方违约）
     * @param artifact [in] 工件载荷（relPath 禁上溯——SP-2）
     * @return Ok/IdempotentHit（续写同内容跳过）/Conflict/DiskFull/
     *         WriteRejected/ContextClosed（语义见 ArtifactStatus 注释）
     */
    virtual ArtifactStatus writeArtifact(ArtifactSessionRef session,
                                         const ArtifactWrite& artifact) = 0;

    /**
     * @brief finalize：manifest 原子发布＝唯一完整标志（D-13）；幂等/冲突
     *        判定承载（摘要比对——见类注释）。
     * @param session [in] 会话引用
     * @param manifest [in] 工件清单（manifestDigest 已由协调器盖章）
     * @return Ok/IdempotentHit/Conflict/DiskFull/WriteRejected/ContextClosed
     */
    virtual ArtifactStatus finalize(ArtifactSessionRef session,
                                    const ReportArtifactManifest& manifest) = 0;

    /**
     * @brief abandon：归档责任终结（不抛出——清理失败仅残留诊断，§7.4）。
     * @param session [in] 会话引用（终态后重复 abandon＝汇集座幂等收尾）
     * @param reason  [in] 结束原因（Completed/Canceled/Failed——清理与恢复
     *                诊断登记口径）
     */
    virtual void abandon(ArtifactSessionRef session, ReportEndReason reason) = 0;
};

// =====================================================================
// IReportPublishedIndex——只读发布清单（落位偏差①：幂等"返回既有记录"与
// listPublished 的读取通道；与 sink 成对注入，project 侧实现）
// =====================================================================

/**
 * @brief 已发布报告只读清单（reporting 定义、project 侧实现——落位偏差①）。
 *
 * 为什么需要独立只读缝：§9.6 sink 四方法（acceptance 5 逐字面）无读取
 * 通道，而 §7.4 幂等行要求"返回既有 PublishedReportRecord"、§9.6 协调器
 * 行要求 listPublished 仅 Finalized——协调器必须能读到磁盘发布事实（含
 * 原 publishedAtUtc），否则只能伪造时间戳或无法列清单。本接口为**只读**
 * 投影：与 sink 同源实现（同一存储上下文），数据权威仍归 project（§7.4
 * "project 负责提交指针和持久化一致性"）。
 *
 * 线程：并发只读安全（实现方承诺——§9.9 project 行查询并发只读同口径）。
 */
class IReportPublishedIndex {
public:
    virtual ~IReportPublishedIndex() = default;

    /**
     * @brief 查单个报告的发布记录（幂等命中的"既有记录"来源）。
     * @param project  [in] 归属项目
     * @param reportId [in] 报告对象身份
     * @return 已 Finalized 的发布记录；未发布/未完成工件（manifest 缺失
     *         ——PM-08："文件存在≠已发布"）→ nullopt
     */
    virtual std::optional<PublishedReportRecord>
    tryPublished(core::ProjectId project, ReportId reportId) const = 0;

    /**
     * @brief 列出项目内已发布报告（仅 Finalized——未完成工件不列入，PM-08）。
     * @param project [in] 归属项目
     * @return 发布记录清单（稳定性由实现方按 reportId 字典序承诺——列表
     *         呈现确定性，NFR-COR-02 同精神）
     */
    virtual std::vector<PublishedReportRecord> listPublished(core::ProjectId project) const = 0;
};

// =====================================================================
// 协调器结果形状（§9.6 引用值类型——落位偏差③）
// =====================================================================

/**
 * @brief 归档结果状态（ArchiveOutcome 的状态轴）。
 *
 * Published/IdempotentHit 为成功两态（§9.6 后置行"成功＝PublishedReport
 * Record（Finalized/幂等命中）"）；Canceled＝协作取消（UX-03 正常取消非
 * 错误——error 缺席）；Failed＝失败（error 在场＋诊断随交付）。
 */
enum class ArchiveStatus : std::uint8_t {
    Published,      ///< published——新发布完成（finalize Ok）
    IdempotentHit,  ///< idempotent-hit——幂等命中（零重写；返回既有记录）
    Canceled,       ///< canceled——协作取消（正常取消非错误——UX-03）
    Failed,         ///< failed——失败（冲突/磁盘/只读/环境——error 在场）
};

/// 取归档状态稳定 token（kebab 形；静态存储期）。
std::string_view token(ArchiveStatus status) noexcept;

/**
 * @brief 归档结果（§9.6 publish 返回值 ArchiveOutcome 的形状冻结）。
 *
 * 形态约定：成功（Published/IdempotentHit）＝record 在场、error 缺席；
 * 取消＝四成员全空（UX-03——与构建器 outcome 取消形态同款：全空即取消
 * 信号）；失败＝error 在场＋diagnostics 随交付（NFR-COR-03 不静默）。
 */
struct ArchiveOutcome {
    ArchiveStatus status = ArchiveStatus::Failed;      ///< 结果状态
    /// 发布记录（Published/IdempotentHit 在场——幂等命中时为既有记录原文）。
    std::optional<PublishedReportRecord> record;
    /// 失败错误（Failed 在场；Canceled/成功恒缺席）。
    std::optional<ReportError> error;
    /// 过程诊断（失败随交付——冲突差异定位/DiskFull 比较型/只读两路径说明；
    /// 成功与取消为空——UX-03）。
    std::vector<core::DiagnosticRecord> diagnostics;
};

/**
 * @brief 已发布报告清单条目（§9.6 listPublished 返回元素）。
 *
 * 发布事实的最小投影（身份＋摘要＋时刻）——供消费方（ui 报告清单/随包
 * 导出勾选）呈现；完整工件清单经 manifest 读取（project 职责）。
 */
struct ReportListingEntry {
    ReportId reportId;                                 ///< 报告对象身份
    core::ContentIdentity contentIdentity;             ///< 内容身份（幂等判定依据）
    core::ContentIdentity manifestDigest;              ///< manifest 规范化摘要
    std::chrono::system_clock::time_point finalizedAtUtc{};  ///< 发布完成时刻（UTC）

    /// 全字段精确相等。
    bool operator==(const ReportListingEntry& o) const noexcept
    {
        return reportId == o.reportId && contentIdentity == o.contentIdentity
               && manifestDigest == o.manifestDigest && finalizedAtUtc == o.finalizedAtUtc;
    }
    bool operator!=(const ReportListingEntry& o) const noexcept { return !(*this == o); }
};

// =====================================================================
// IReportArchiveCoordinator——归档协调器（§9.6 原文接口；reporting 实现）
// =====================================================================

/**
 * @brief 归档协调器接口（§9.6——编排汇集座调用序；会话级）。
 *
 * 契约（§9.6 注释逐条）：
 *   - 前置：报告已冻结（contentIdentity 非零）、一致性已通过（导出服务
 *     步⑤之后才到本层——§8.5）、工件与报告同源；
 *   - 后置：成功＝PublishedReportRecord（Finalized/幂等命中）；失败/取消
 *     ＝abandon＋清理；
 *   - 编排顺序＝begin→write×N→finalize，任一非 Ok→abandon；
 *   - listPublished 仅 Finalized（未完成工件不列入——"文件存在≠已发布"）。
 */
class IReportArchiveCoordinator {
public:
    virtual ~IReportArchiveCoordinator() = default;

    /**
     * @brief 发布归档（编排：幂等预检→begin→writeArtifact×N→finalize；
     *        任一非 Ok→abandon——§9.6 前置/后置行）。
     *
     * @param report    [in] 冻结报告（只读；contentIdentity 非零——前置）
     * @param artifacts [in] 渲染产物集（非空；每项 sourceReportIdentity 与
     *                  报告 contentIdentity 一致——同源前置）
     * @param manifest  [in] 工件清单（身份/生成信息/工件摘要列由调用方装配；
     *                  finalizedAtUtc/manifestDigest 由本方法盖章——见类型注释）
     *
     * @return 归档结果（形态约定见 ArchiveOutcome 注释）
     *
     * @throws ReportError(Usage) 前置违约（报告未冻结/工件空/同源不符/
     *         manifest 身份与报告不一致/artifacts 重复 relPath）
     */
    virtual ArchiveOutcome publish(const ReviewReport& report,
                                   const std::vector<RenderArtifact>& artifacts,
                                   const ReportArtifactManifest& manifest) = 0;

    /**
     * @brief 列出项目内已发布报告（仅 Finalized——§9.6 原文注释）。
     * @param project [in] 归属项目
     * @return 清单条目（只读投影——经 IReportPublishedIndex）
     */
    virtual std::vector<ReportListingEntry> listPublished(core::ProjectId project) const = 0;
};

// =====================================================================
// ReportArchiveCoordinator——产品实现（§9.6"reporting 实现"）
// =====================================================================

/**
 * @brief 归档协调器产品实现（编排幂等预检/冲突拒绝/取消/失败四轨）。
 *
 * publish 编排明细（§7.4 流程图逐分支——acceptance 1~4 的承载）：
 *   1. 前置校验（Usage fail-fast）：报告冻结、工件非空且与报告同源、
 *      manifest 身份列与报告一致、工件 relPath 无重复；
 *   2. 幂等预检（**零汇集座调用**——零重写的最强形态）：查
 *      IReportPublishedIndex，同 reportId 已 Finalized：摘要一致→
 *      IdempotentHit（返回既有记录原文，含原 publishedAtUtc）；不一致→
 *      ArchiveConflict（差异定位诊断：身份/摘要维度）；见 §7.4 流程图
 *      "sink.begin：同 reportId 已 Finalized？"分支——磁盘事实经索引读出，
 *      begin 待比对语义（§9.6）由 finalize 兜底复核承载（步骤 5）；
 *   3. 取消检查点→Canceled（尚无会话——无需 abandon）；
 *   4. sink.begin→逐工件 writeArtifact（逐工件前后取消检查点；写返回
 *      IdempotentHit＝续写同内容跳过，按 Ok 继续——§9.6 合法调用行）；
 *   5. 盖章（finalizedAtUtc=now＋manifestDigest=computeManifestDigest）→
 *      sink.finalize：Ok→Published；IdempotentHit→回查索引取既有记录
 *      （并发幂等——索引缺记录＝不一致，Failed 不伪造）；Conflict→
 *      abandon(Failed)＋ArchiveConflict；DiskFull→abandon(Failed)＋
 *      DiskFull＋比较型诊断（§7.4 表）；
 *   6. 任一步取消→abandon(Canceled)→Canceled；任一步失败→abandon(Failed)
 *      →Failed（abandon 本身不抛——责任终结）。
 *
 * 线程约束：会话单线程（§9.6 线程行）——一次 publish 一个逻辑会话；注入
 * 引用须覆盖本协调器存续期（借用——L5 装配/测试持有）。
 */
class ReportArchiveCoordinator final : public IReportArchiveCoordinator {
public:
    /**
     * @brief 注入构造（汇集座＋只读索引＋会话取消令牌）。
     * @param sink   [in] 工件汇集座（borrow、**可写引用**——汇集座是项目
     *               内唯一写路径，协调器对其调用 begin/write/finalize/
     *               abandon 写方法；project 实现/测试 fake）
     * @param index  [in] 只读发布清单（borrow、const 只读——与 sink 同源
     *               实现的另一投影面；落位偏差①）
     * @param cancel [in] 会话取消令牌（borrow；可空＝不取消——§9.5 同缺省
     *               口径；逐工件检查点消费）
     */
    ReportArchiveCoordinator(IReportArtifactSink& sink, const IReportPublishedIndex& index,
                             const ReportCancelToken* cancel = nullptr) noexcept;

    /// 非拷贝（借用语义）。
    ReportArchiveCoordinator(const ReportArchiveCoordinator&) = delete;
    ReportArchiveCoordinator& operator=(const ReportArchiveCoordinator&) = delete;

    ArchiveOutcome publish(const ReviewReport& report,
                           const std::vector<RenderArtifact>& artifacts,
                           const ReportArtifactManifest& manifest) override;

    std::vector<ReportListingEntry> listPublished(core::ProjectId project) const override;

private:
    IReportArtifactSink* m_sink;              ///< 汇集座（借用、可写——唯一项目写路径）
    const IReportPublishedIndex* m_index;     ///< 只读发布清单（借用）
    const ReportCancelToken* m_cancel;        ///< 会话取消令牌（借用；可空）
};

}  // namespace sdurws::ird::reporting

#endif  // SDURWS_IRD_REPORTING_ARCHIVE_HPP
