/**
 * @file   Export.cpp
 * @brief  Export.hpp 的实现——§8.5 导出编排（解析→矩阵→渲染〔含二次渲染
 *         确定性比对〕→一致性→发布/写出）与 §7.4 幂等/冲突/取消/只读四轨。
 *
 * 设计依据：
 *   - units/reporting.md §7.4（幂等导出与冲突拒绝表＋流程图——外部目标同
 *     字节幂等无操作；NeverOverwrite 默认拒绝/OverwriteAtomic 原子替换；
 *     失败保留选择与路径可重试）、§8.5（时序图步①~⑤——步③二次渲染字节
 *     比对、步⑤"不一致→ConsistencyMismatch 失败〔不出工件〕"）、§9.5
 *     （契约与维度表——非法调用行/formats 必含 Html/只读项目两路径）、
 *     §9.9（io 行——"IO 失败→ExportFailed/DiskFull；取消→清理"）
 *   - 需求 RPT-01/RPT-02、PM-07/PM-08、UX-03、NFR-COR-02（同输入同字节）、
 *     NFR-COR-03（不静默）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 1~4
 *
 * 诊断码口径（与 Archive.cpp 文件头同则）：RPT-CONSISTENCY-MISMATCH/
 * RPT-EXPORT-FAILED 为 diagnostics 收编登记值（diagcodes 引用）；外部冲突
 * 诊断的 cause 引 io 族码 IO-PACK-TARGET-EXISTS（io.md §7.4 错误矩阵登记
 * ——码值权威归 io/diagnostics 侧，本处为 cause 文本引用非注册）。
 */

#include "sdurws/ird/reporting/Export.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <utility>

#include <sdurws/ird/core/Identity.hpp>   // core::RunId（sourceRuns 装配）

namespace sdurws::ird::reporting {

// =====================================================================
// 工件相对命名
// =====================================================================

std::string_view artifactRelPath(ReportRenderFormat format) noexcept
{
    switch (format) {
    case ReportRenderFormat::Html: return kHtmlArtifactRelPath;
    case ReportRenderFormat::Json: return kJsonArtifactRelPath;
    case ReportRenderFormat::Csv: return kCsvArtifactRelPath;
    }
    return "unknown";   // 不可达（全枚举 switch 已尽；防御性兜底）
}

// =====================================================================
// 内部工具（匿名命名空间——翻译单元局部）
// =====================================================================

namespace {

/// 字节集 SHA-256（core ContentDigester 唯一算法——§4.1 摘要纪律）。
core::Digest256 digestOf(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 只读探测：读取既有目标文件的完整字节（外部幂等判定的读半区——文件头
/// 落位偏差⑤：读探测不属 io 单元 canonical 写出设施纪律面，全部写入仍经
/// 注入原子协议）。
bool tryReadFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>* out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;   // 打不开（不存在/无读权）——调用方按存在性前置区分
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size < 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(out->data()), size);
        if (file.gcount() != size) {
            return false;   // 读半截——按探测失败处理（幂等判定宁拒绝不误判）
        }
    }
    return true;
}

/// 诊断构造薄封装（core make() C-3 校验；常量文本违约即实现缺陷——fail-fast
/// 兜底，Sections.cpp/Archive.cpp 同款先例）。
core::DiagnosticRecord makeDiag(std::string code, std::string context, std::string cause,
                                std::string action)
{
    return core::DiagnosticRecord::make(std::move(code), std::nullopt, std::nullopt,
                                        std::nullopt, std::move(context), std::move(cause),
                                        std::move(action));
}

/// 失败结果快捷构造（error 在场；诊断由调用方追加）。
ReportExportResult failedResult(ReportErrorCode code, std::string detail)
{
    ReportExportResult result;
    result.error = ReportError(code, std::move(detail));
    return result;
}

/// 取消结果快捷构造（五成员全空——UX-03 正常取消非错误，落位偏差③）。
ReportExportResult canceledResult()
{
    return ReportExportResult{};
}

}  // namespace

// =====================================================================
// ReportExportService
// =====================================================================

ReportExportService::ReportExportService(const IReportIoFactory& ioFactory,
                                         const diagnostics::IRedactionService& redaction,
                                         IReportArtifactSink& sink,
                                         const IReportPublishedIndex& index,
                                         const IReportCsvReaderFactory& csvReaders,
                                         const IReportResolver& resolver)
    : m_ioFactory(&ioFactory),
      m_sink(&sink),
      m_index(&index),
      m_resolver(&resolver),
      m_htmlRenderer(redaction),
      m_jsonRenderer(ioFactory, redaction),
      m_csvRenderer(ioFactory, redaction),
      m_consistencyChecker(csvReaders)
{
}

ReportExportResult ReportExportService::exportReport(const ReportExportRequest& request,
                                                     const ReportCancelToken* cancel,
                                                     ReportProgressCallback progress)
{
    // ---- 第 1 步：请求校验（§9.5 前置/非法调用行——Usage 走结果错误轨：
    // §9.5 错误行列明 Usage，导出为用户命令链路，结果错误与"保留选择与
    // 路径可重试"同通道）。 ----
    if (!request.reportId.isValid()) {
        return failedResult(ReportErrorCode::Usage, "exportReport: reportId 为零（保留值）");
    }
    if (request.formats.empty()) {
        return failedResult(ReportErrorCode::Usage, "exportReport: formats 为空（≥1——§9.5）");
    }
    const bool hasHtml = std::any_of(request.formats.begin(), request.formats.end(),
                                     [](ReportRenderFormat f) {
                                         return f == ReportRenderFormat::Html;
                                     });
    if (!hasHtml) {
        // RPT-02：HTML 为首版主格式——不含 Html 的发布/导出＝非法调用（§9.5）。
        return failedResult(ReportErrorCode::Usage,
                            "exportReport: formats 不含 Html（RPT-02 首版主格式——§9.5 非法调用行）");
    }
    for (std::size_t i = 0; i < request.formats.size(); ++i) {
        for (std::size_t j = i + 1; j < request.formats.size(); ++j) {
            if (request.formats[i] == request.formats[j]) {
                return failedResult(ReportErrorCode::Usage,
                                    "exportReport: formats 含重复格式（逐格式导出语义二义）");
            }
        }
    }
    if (request.destination.kind == ExportDestination::ExternalPath
        && request.destination.externalPath.empty()) {
        return failedResult(ReportErrorCode::Usage,
                            "exportReport: ExternalPath 目标路径为空（§9.5 必填）");
    }
    if (request.withEvidenceBundle) {
        // 落位偏差④：证据包组装归 RPT-T10（§11 任务拆分）——能力到位前
        // 显式拒绝而非静默忽略（不吞语义）；RPT-T10 落地后此处改为组装编排。
        return failedResult(ReportErrorCode::Usage,
                            "exportReport: withEvidenceBundle 证据包组装随 RPT-T10 落地"
                            "（本服务显式拒绝——非静默忽略）");
    }

    // ---- 第 2 步：入轨取消检查点（§9.5"检查点密集"之首）。 ----
    if (cancel != nullptr && cancel->isCancelled()) {
        return canceledResult();
    }

    // ---- 第 3 步：解析报告值（§9.5 前置"reportId 可解析"；落位偏差①
    // ——来源归装配侧，解析不到＝前置违约）。 ----
    const std::optional<ReviewReport> resolved = m_resolver->tryResolve(request.reportId);
    if (!resolved.has_value()) {
        return failedResult(ReportErrorCode::Usage,
                            "exportReport: reportId 不可解析（未构建且未发布——§9.5 前置）");
    }
    const ReviewReport& report = *resolved;

    // ---- 第 4 步：字段矩阵单次提取（§8.4——三格式值单源，D-07）。 ----
    const FieldMatrix matrix = extractFieldMatrix(report);

    // ---- 第 5 步：逐格式渲染＋二次渲染确定性比对（§8.5 步②③——同输入
    // 同字节是幂等导出与 AT-22 一致性的字节前提；二次不一致＝渲染层非确定
    // （RenderFailed——非数据问题，§8.1 错误分立）。 ----
    std::vector<RenderArtifact> artifacts;
    artifacts.reserve(request.formats.size());
    for (std::size_t i = 0; i < request.formats.size(); ++i) {
        const ReportRenderFormat format = request.formats[i];
        if (progress) {
            // 进度：逐格式渲染粒度（done/total 无单位计数——§9 章首块；
            // 阶段 token 复用 §7.1 状态词）。
            progress(ReportProgress{i, request.formats.size(),
                                    token(ReportBuildStage::Rendering).data()});
        }
        const RenderOutcome first = [&]() {
            switch (format) {
            case ReportRenderFormat::Html:
                return m_htmlRenderer.render(report, matrix, format);
            case ReportRenderFormat::Json:
                return m_jsonRenderer.render(report, matrix, format);
            case ReportRenderFormat::Csv:
                return m_csvRenderer.render(report, matrix, format);
            }
            // 不可达（枚举全表）——编译器全枚举告警保护下的兜底。
            RenderOutcome fallback;
            fallback.error = ReportError(ReportErrorCode::Usage, "未知渲染格式");
            return fallback;
        }();
        if (first.error.has_value()) {
            ReportExportResult result = failedResult(first.error->code(),
                                                     std::string("exportReport: 渲染失败——")
                                                         + first.error->what());
            return result;
        }
        // 二次渲染（同一渲染器、同一输入）——字节必须逐位一致（NFR-COR-02
        // 确定性在导出链的执行侧自证；§8.5 步③原文）。
        const RenderOutcome second = [&]() {
            switch (format) {
            case ReportRenderFormat::Html:
                return m_htmlRenderer.render(report, matrix, format);
            case ReportRenderFormat::Json:
                return m_jsonRenderer.render(report, matrix, format);
            case ReportRenderFormat::Csv:
                return m_csvRenderer.render(report, matrix, format);
            }
            RenderOutcome fallback;
            fallback.error = ReportError(ReportErrorCode::Usage, "未知渲染格式");
            return fallback;
        }();
        if (second.error.has_value() || second.artifact->bytes != first.artifact->bytes) {
            ReportExportResult result = failedResult(
                ReportErrorCode::RenderFailed,
                std::string("exportReport: 二次渲染字节不一致（渲染层非确定——§8.5 步③）"));
            return result;
        }
        artifacts.push_back(std::move(*first.artifact));

        // 逐格式渲染后取消检查点（§9.5 取消行"逐格式渲染后"）。
        if (cancel != nullptr && cancel->isCancelled()) {
            return canceledResult();
        }
    }

    // ---- 第 6 步：一致性检查（§8.5 步④⑤——≥2 格式才有比对意义；不一致
    // ＝ConsistencyMismatch＋诊断，零写出：项目内不 finalize、项目外零写出
    // ——§9.5 前置/后置行"一致性不通过不出工件"）。 ----
    if (progress) {
        progress(ReportProgress{request.formats.size(), request.formats.size() + 1,
                                token(ReportBuildStage::Verifying).data()});
    }
    if (artifacts.size() >= 2) {
        ConsistencyInput input;
        input.report = &report;
        input.matrix = &matrix;
        input.artifacts.reserve(artifacts.size());
        for (const RenderArtifact& a : artifacts) {
            input.artifacts.push_back(&a);
        }
        const ConsistencyResult verdict = m_consistencyChecker.check(input);
        if (!verdict.consistent) {
            // mismatch 明细入诊断 cause（首条定位＋总数——ERR-01 可定位；
            // 全量明细在 ConsistencyResult 由调用方可复核，诊断不吞细节）。
            std::string firstMismatch = "（无明细）";
            if (!verdict.mismatches.empty()) {
                const FieldMismatch& m = verdict.mismatches.front();
                firstMismatch = std::string(token(m.format)) + "/" + m.fieldKey + "/"
                                + m.dimension;
            }
            ReportExportResult result = failedResult(
                ReportErrorCode::ConsistencyMismatch,
                "exportReport: 多格式逐字段不一致（§8.5 步⑤——不出工件）；mismatch 共 "
                    + std::to_string(verdict.mismatches.size()) + " 项，首条 " + firstMismatch);
            result.diagnostics.push_back(makeDiag(
                std::string(diagcodes::kConsistencyMismatch),
                "报告导出一致性检查（AT-22/§8.5）",
                "多格式逐字段回读比对不一致：共 " + std::to_string(verdict.mismatches.size())
                    + " 项，首条 " + firstMismatch,
                "修正渲染器或数据后重试（本次导出零写出——不出工件）"));
            return result;
        }
    }

    // ---- 第 7 步：按目标分派（§7.4 流程图两主干）。 ----
    if (progress) {
        progress(ReportProgress{request.formats.size() + 1, request.formats.size() + 2,
                                token(ReportBuildStage::Publishing).data()});
    }
    if (request.destination.kind == ExportDestination::ProjectArchive) {
        // ===== 项目内归档（汇集座唯一写路径——§9.5 副作用行）=====
        // 装配 manifest（§7.3 字段表：身份/生成信息自报告——报告值唯一来
        // 源；工件摘要列自产物；finalizedAtUtc/manifestDigest 由协调器盖章）。
        ReportArtifactManifest manifest;
        manifest.reportId = report.reportId();
        manifest.reportVersion = report.reportVersion();
        manifest.supersedes = report.supersedes();
        manifest.level = report.level();
        manifest.project = report.project();
        manifest.branch = report.branch();
        manifest.revision = report.revision();
        manifest.dataIdentity = report.dataIdentity();
        manifest.contentIdentity = report.contentIdentity();
        manifest.generatedAtUtc = report.generatedAtUtc();
        manifest.generatedBy = report.generatedBy();
        manifest.generatorVersion = report.generatorVersion();
        manifest.artifacts.reserve(artifacts.size());
        for (const RenderArtifact& a : artifacts) {
            ArtifactManifestEntry entry;
            entry.relPath = std::string(artifactRelPath(a.format));
            entry.format = a.format;
            entry.sha256 = a.digest;
            entry.sizeBytes = a.bytes.size();   // 单位：字节
            entry.rendererVersion = a.rendererVersion;
            entry.templateVersion = a.templateVersion;
            manifest.artifacts.push_back(std::move(entry));
        }
        // sourceRuns＝结果引用的 runId 集（去重保序——§7.3"引用的结果集"，
        // sources.json 数据源，project §8.9 勾选规则消费）。
        for (const ResultRefSnapshot& r : report.resultRefs()) {
            const bool seen = std::any_of(manifest.sourceRuns.begin(),
                                          manifest.sourceRuns.end(),
                                          [&r](const core::RunId& x) { return x == r.runId; });
            if (!seen) {
                manifest.sourceRuns.push_back(r.runId);
            }
        }

        // 编排发布（会话级协调器——取消令牌随会话注入，Archive 落位偏差②）。
        ReportArchiveCoordinator coordinator(*m_sink, *m_index, cancel);
        ArchiveOutcome outcome = coordinator.publish(report, artifacts, manifest);
        switch (outcome.status) {
        case ArchiveStatus::Published:
        case ArchiveStatus::IdempotentHit: {
            // 成功两态（幂等命中时 published＝既有记录原文——§7.4"返回既有
            // PublishedReportRecord"，零重写由协调器零汇集座写调用保证）。
            ReportExportResult result;
            result.published = std::move(outcome.record);
            result.diagnostics = std::move(outcome.diagnostics);
            return result;
        }
        case ArchiveStatus::Canceled:
            // UX-03 全空形态（协调器已 abandon(Canceled)——目标不变）。
            return canceledResult();
        case ArchiveStatus::Failed:
        default:
            // 失败（冲突/磁盘/只读/环境——error＋诊断随交付，选择与路径
            // 保留可重试）。防御面：协调器 Failed 必带 error（其结果形态
            // 约定），缺位＝实现缺陷——合成 ExportFailed 如实上报，不静默
            // 返回"无错误的失败"（NFR-COR-03）。
            ReportExportResult result;
            if (outcome.error.has_value()) {
                result.error = std::move(outcome.error);
            } else {
                result.error = ReportError(ReportErrorCode::ExportFailed,
                                           "exportReport: 归档失败（协调器错误缺位——防御合成）");
            }
            result.diagnostics = std::move(outcome.diagnostics);
            return result;
        }
    }

    // ===== 外部路径导出（§7.4 流程图"目标＝项目外"主干）=====
    const std::filesystem::path& dir = request.destination.externalPath;

    // 目标目录就绪：已存在且非目录→拒绝；不存在→创建（目录创建属导出会话
    // 准备动作，失败→ExportFailed 保留路径可重试——§16 验收要点）。
    std::error_code fsError;
    if (std::filesystem::exists(dir)) {
        if (!std::filesystem::is_directory(dir)) {
            return failedResult(ReportErrorCode::ExportFailed,
                                "exportReport: 外部目标已存在且不是目录（" + dir.string()
                                    + "）——保留路径可重试");
        }
    } else {
        std::filesystem::create_directories(dir, fsError);
        if (fsError) {
            return failedResult(ReportErrorCode::ExportFailed,
                                "exportReport: 外部目标目录创建失败（" + dir.string() + "："
                                    + fsError.message() + "）——保留路径可重试");
        }
    }

    ReportExportResult result;
    for (const RenderArtifact& a : artifacts) {
        // 写前取消检查点（§9.5 取消行"逐工件写出前"；在途目标未开——无临时
        // 可清；此前已提交的完整工件文件保留〔完整原子单元，非半成品〕）。
        if (cancel != nullptr && cancel->isCancelled()) {
            return canceledResult();
        }
        const std::filesystem::path target = dir / std::filesystem::u8path(artifactRelPath(a.format));
        const core::Digest256 wantDigest = a.digest;

        if (std::filesystem::exists(target)) {
            // 目标已存在：读字节比对（§7.4 行 5/6——判定以字节内容为准，
            // 不以存在性/时间戳推测）。
            std::vector<std::uint8_t> existingBytes;
            const bool readable = tryReadFileBytes(target, &existingBytes);
            if (readable && digestOf(existingBytes) == wantDigest) {
                // 幂等成功（无操作）：不调 makeAtomicTarget——既有文件
                // mtime/哈希不变（acceptance 1 外部半区）；返回成功＋
                // 既有摘要（§7.4 行 5）。
                ExportedFile file;
                file.path = target;
                file.format = a.format;
                file.sha256 = wantDigest;
                result.files.push_back(std::move(file));
                continue;
            }
            if (request.destination.replace == ReplacePolicy::NeverOverwrite) {
                if (readable) {
                    // 默认拒绝（§7.4 行 6）：不触碰目标（零写调用——先前
                    // 输出天然完整保留）；保留选择与路径可重试（§16）。
                    core::ContentIdentity existingDigest = core::ContentIdentity{};
                    existingDigest.bytes = digestOf(existingBytes);
                    core::ContentIdentity wantIdentity = core::ContentIdentity{};
                    wantIdentity.bytes = wantDigest;
                    ReportExportResult conflict = failedResult(
                        ReportErrorCode::ExportFailed,
                        "exportReport: 外部目标已存在且内容不同（" + target.string()
                            + "）——NeverOverwrite 默认拒绝，保留选择与路径可重试");
                    conflict.diagnostics.push_back(makeDiag(
                        std::string(diagcodes::kExportFailed),
                        "报告外部导出冲突（RPT-01/§7.4）",
                        "目标已存在且字节不同（io 族码 IO-PACK-TARGET-EXISTS——io.md §7.4）："
                            "路径 " + target.string() + "；既有摘要 "
                            + existingDigest.toCanonical() + "，本次摘要 "
                            + wantIdentity.toCanonical(),
                        "确认覆盖后以 OverwriteAtomic 重试（原子替换、先前输出替换前完整保留），"
                        "或更换目标目录"));
                    return conflict;
                }
                // 既有目标存在但不可读且策略为不覆盖——宁拒绝不覆盖（保守
                // 方向：无法证明同内容，就不动既有文件）。
                return failedResult(ReportErrorCode::ExportFailed,
                                    "exportReport: 外部目标存在但不可读（" + target.string()
                                        + "）——NeverOverwrite 保守拒绝，保留路径可重试");
            }
            // OverwriteAtomic（用户显式确认——§7.4 行 6）：落入下方统一
            // 原子替换路径（不可读目标同轨——替换语义不依赖读探测结果）。
        }

        // 原子写出（io 原子协议 <target>.<8hex>.tmp 由注入实现承载——P-RPT-1；
        // NeverOverwrite 命中"目标不存在"分支，OverwriteAtomic 走替换分支）。
        std::unique_ptr<IReportOutputTarget> out =
            m_ioFactory->makeAtomicTarget(target, request.destination.replace);
        if (!out) {
            return failedResult(ReportErrorCode::ExportFailed,
                                "exportReport: io 适配器未提供原子目标（装配缺陷）");
        }
        const bool writeOk =
            out->write(a.bytes.data(), a.bytes.size());
        if (!writeOk || !out->commit()) {
            // 写入/提交失败：abort 清理临时（幂等放弃路径）——目标不变，
            // 先前输出完整保留（§7.4 行 6 原子替换语义的失败半区）；ExportFailed
            // 保留选择与路径可重试。磁盘不足在 io 适配器侧映射 DiskFull 时，
            // 以 DiskFull 面上报（此处按投影契约 bool 无法细分——环境细节
            // 由适配器诊断通道补充，登记 §14.4）。
            out->abort();
            return failedResult(ReportErrorCode::ExportFailed,
                                "exportReport: 原子写出失败（" + target.string()
                                    + "）——目标不变，保留选择与路径可重试");
        }
        ExportedFile file;
        file.path = target;
        file.format = a.format;
        file.sha256 = wantDigest;
        result.files.push_back(std::move(file));
    }
    return result;
}

}  // namespace sdurws::ird::reporting
