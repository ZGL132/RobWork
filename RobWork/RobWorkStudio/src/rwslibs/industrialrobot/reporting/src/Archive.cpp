/**
 * @file   Archive.cpp
 * @brief  Archive.hpp 的实现——manifest 规范化摘要（幂等判定键）与归档
 *         协调器编排（幂等预检→begin→write×N→finalize，任一非 Ok→abandon）。
 *
 * 设计依据：
 *   - units/reporting.md §7.3（报告工件状态图——begin 冲突预检/finalize＝
 *     唯一完整标志 D-13/abandon 未完成工件不入清单）、§7.4（幂等导出与
 *     冲突拒绝表＋流程图——判定键不以 ReportId/目标路径；冲突差异定位；
 *     磁盘不足 abandon＋清理＋可重试；只读项目两路径）、§9.6（sink 四方法
 *     与协调器契约——编排顺序、listPublished 仅 Finalized）
 *   - 需求 RPT-01（追加幂等/冲突拒绝）、PM-03（归档失败责任终结）、PM-07
 *     （只读项目）、PM-08（未完成工件不入清单）、UX-03（取消非错误）
 *   - 任务契约 tasks/foundation/RPT-T09.json acceptance 1~5
 *
 * 实现要点（对转头文件注释的对应关系）：
 *   - 幂等预检先于 begin（零汇集座调用＝零重写的最强形态——acceptance 1
 *     "sink 调用记录＋摘要比对"）；finalize 处的 IdempotentHit/Conflict 是
 *     汇集座对同一判定的兜底复核（并发/索引滞后防御——§9.6 finalize 注释）。
 *   - 诊断码三类：RPT-ARCHIVE-CONFLICT/RPT-EXPORT-FAILED 为 diagnostics
 *     收编表登记值（引用登记——Errors.hpp diagcodes）；RPT-DISK-FULL 与
 *     RPT-READONLY-STORE 引用自单元卡 §7.4 表原文，**登记状态＝待收编**
 *     （P-RPT-8 关联——码值权威归 diagnostics StableCodeRegistry，收编前
 *     该码在 diagnostics 呈现链路按"占位＋开发诊断"处理，不崩溃——§4.5；
 *     本处为设计表引用非二次定义）。
 */

#include "sdurws/ird/reporting/Archive.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <sdurws/ird/core/DiagData.hpp>   // DiagnosticRecord/ComparativeFields（诊断构造）
#include <sdurws/ird/core/Provenance.hpp> // SourcedValue/ValueProvenance（比较型数值侧）
#include <sdurws/ird/core/Units.hpp>      // UnitToken（比较型单位侧）

namespace sdurws::ird::reporting {

// =====================================================================
// token 表（全枚举 switch——新增枚举值未登记表项时编译告警暴露）
// =====================================================================

std::string_view token(ReportEndReason reason) noexcept
{
    switch (reason) {
    case ReportEndReason::Completed: return "completed";
    case ReportEndReason::Canceled: return "canceled";
    case ReportEndReason::Failed: return "failed";
    }
    return "unknown";   // 不可达（全枚举 switch 已尽；防御性兜底）
}

std::string_view token(ArtifactStatus status) noexcept
{
    switch (status) {
    case ArtifactStatus::Ok: return "ok";
    case ArtifactStatus::IdempotentHit: return "idempotent-hit";
    case ArtifactStatus::Conflict: return "conflict";
    case ArtifactStatus::DiskFull: return "disk-full";
    case ArtifactStatus::WriteRejected: return "write-rejected";
    case ArtifactStatus::ContextClosed: return "context-closed";
    }
    return "unknown";
}

std::string_view token(ArchiveStatus status) noexcept
{
    switch (status) {
    case ArchiveStatus::Published: return "published";
    case ArchiveStatus::IdempotentHit: return "idempotent-hit";
    case ArchiveStatus::Canceled: return "canceled";
    case ArchiveStatus::Failed: return "failed";
    }
    return "unknown";
}

// =====================================================================
// manifest 规范化摘要（幂等判定键——头文件注释列出的编码域/排除域）
// =====================================================================

namespace {

/// 编码器版本（入编码——编码规则升版＝全体 manifest 摘要变化，走设计变更
/// 评审；ReportCodec::kCodecVersion 同纪律）。
constexpr std::uint32_t kManifestCodecVersion = 1;

/// manifest 编码 magic（8 字节 ASCII；"IRDRPTM1"＝M for manifest——与
/// ReportCodec 的 IRDRPT1/IRDRPTD1 同族不同位，防跨编码误读）。
constexpr std::string_view kMagicManifest = "IRDRPTM1";

/// 大端追加 u16/u32/u64（§4.4 整数编码同口径——跨平台字节稳定）。
void appendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void appendU64(std::vector<std::uint8_t>& out, std::uint64_t v)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> shift));
    }
}

/// 追加长度前缀字节串（u32 长度＋原始字节——§4.4 同口径）。
void appendBytes(std::vector<std::uint8_t>& out, const void* data, std::size_t n)
{
    appendU32(out, static_cast<std::uint32_t>(n));
    const auto* p = static_cast<const std::uint8_t*>(data);
    out.insert(out.end(), p, p + n);
}

void appendString(std::vector<std::uint8_t>& out, const std::string& s)
{
    appendBytes(out, s.data(), s.size());
}

/// 待收编诊断码（单元卡 §7.4 表原文引用——登记状态与处置见文件头注释）。
constexpr std::string_view kDiagDiskFull = "RPT-DISK-FULL";
constexpr std::string_view kDiagReadOnlyStore = "RPT-READONLY-STORE";

/// 将 ReportError 转为失败结果（error 在场——诊断由调用方按场景追加）。
ArchiveOutcome failedOutcome(ReportErrorCode code, std::string detail)
{
    ArchiveOutcome out;
    out.status = ArchiveStatus::Failed;
    out.error = ReportError(code, std::move(detail));
    return out;
}

/// 诊断构造的统一薄封装（core make() 的 C-3 校验对常量文本不可能违约；
/// 违约即实现缺陷——异常 fail-fast 兜底，与 Sections.cpp 先例同款）。
core::DiagnosticRecord makeDiag(std::string code, std::string context, std::string cause,
                                std::string action)
{
    return core::DiagnosticRecord::make(std::move(code), std::nullopt, std::nullopt,
                                        std::nullopt, std::move(context), std::move(cause),
                                        std::move(action));
}

}  // namespace

core::ContentIdentity computeManifestDigest(const ReportArtifactManifest& manifest)
{
    // 前置校验：空工件集＝无幂等判定对象（§7.4 判定键的"format 集"非空）；
    // 重复 relPath＝清单装配违约（同路径双字节来源——判定域不稳定）。
    // 调用方违约 fail-fast（Usage）而非静默排序去重——AGENTS.md 错误语义。
    if (manifest.artifacts.empty()) {
        throw ReportError(ReportErrorCode::Usage, "computeManifestDigest: artifacts 为空");
    }
    for (std::size_t i = 0; i < manifest.artifacts.size(); ++i) {
        if (manifest.artifacts[i].relPath.find('\0') != std::string::npos) {
            // 编码层禁止 NUL（§4.4"UTF-8 禁 NUL"同则——长度前缀编码下 NUL
            // 不破坏结构，但持久化文本二义在入口拒绝）。
            throw ReportError(ReportErrorCode::DataInvalid,
                              "computeManifestDigest: relPath 含 NUL 字节");
        }
        for (std::size_t j = i + 1; j < manifest.artifacts.size(); ++j) {
            if (manifest.artifacts[i].relPath == manifest.artifacts[j].relPath) {
                throw ReportError(ReportErrorCode::Usage,
                                  "computeManifestDigest: 重复 relPath: "
                                      + manifest.artifacts[i].relPath);
            }
        }
    }

    // 稳定排序：按 relPath 字节序（std::string 比较＝char_traits::compare
    // ＝无符号字节序——同工件集同序，NFR-COR-02）。不修改调用方输入——
    // 拷贝下标排序而非原地排序 manifest.artifacts。
    std::vector<std::size_t> order(manifest.artifacts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&manifest](std::size_t a, std::size_t b) {
        return manifest.artifacts[a].relPath < manifest.artifacts[b].relPath;
    });

    // 规范化编码（域见头文件 computeManifestDigest 注释——判定键五元组：
    // contentIdentity＋(relPath, format, sha256, sizeBytes, rendererVersion,
    // templateVersion) 集合；身份链/生成发布时刻/格式常量全部排除）。
    std::vector<std::uint8_t> encoded;
    encoded.reserve(64 + manifest.artifacts.size() * 96);
    encoded.insert(encoded.end(), kMagicManifest.begin(), kMagicManifest.end());
    appendU32(encoded, kManifestCodecVersion);
    appendBytes(encoded, manifest.contentIdentity.bytes.data(),
                manifest.contentIdentity.bytes.size());
    appendU32(encoded, static_cast<std::uint32_t>(manifest.artifacts.size()));
    for (const std::size_t idx : order) {
        const ArtifactManifestEntry& e = manifest.artifacts[idx];
        appendString(encoded, e.relPath);
        appendU16(encoded, static_cast<std::uint16_t>(e.format));   // 枚举以声明序数值写入
        appendBytes(encoded, e.sha256.data(), e.sha256.size());
        appendU64(encoded, e.sizeBytes);
        appendU32(encoded, e.rendererVersion);
        appendU32(encoded, e.templateVersion);
    }

    // SHA-256 经 core ContentDigester 唯一算法（§4.1 摘要纪律——不自建摘要器）。
    core::ContentDigester digester;
    digester.update(encoded.data(), encoded.size());
    core::ContentIdentity digest;
    digest.bytes = digester.finalize();
    return digest;
}

// =====================================================================
// ReportArchiveCoordinator——编排实现
// =====================================================================

ReportArchiveCoordinator::ReportArchiveCoordinator(IReportArtifactSink& sink,
                                                   const IReportPublishedIndex& index,
                                                   const ReportCancelToken* cancel) noexcept
    : m_sink(&sink), m_index(&index), m_cancel(cancel)
{
}

ArchiveOutcome ReportArchiveCoordinator::publish(const ReviewReport& report,
                                                 const std::vector<RenderArtifact>& artifacts,
                                                 const ReportArtifactManifest& manifest)
{
    // ---- 第 1 步：前置校验（Usage fail-fast——§9.6 前置行"报告已冻结、
    // 一致性已通过"中可廉价验证的同源面；"一致性已通过"由导出服务编排序
    // 保证——本层不重跑检查器，只校验输入自洽）。 ----
    if (!report.contentIdentity().isValid()) {
        throw ReportError(ReportErrorCode::Usage,
                          "publish: 报告未冻结（contentIdentity 为零——须 ReviewReport::make 产出）");
    }
    if (artifacts.empty()) {
        throw ReportError(ReportErrorCode::Usage, "publish: artifacts 为空（无工件可归档）");
    }
    for (const RenderArtifact& a : artifacts) {
        // 同源前置：工件必须产自本报告（§9.4 前置行"artifacts 与 report/
        // matrix 同源"的汇集座侧复核——防跨报告工件混入归档）。
        if (!(a.sourceReportIdentity == report.contentIdentity())) {
            throw ReportError(ReportErrorCode::Usage,
                              "publish: 工件与报告不同源（sourceReportIdentity 不符）");
        }
    }
    if (manifest.reportId != report.reportId() || !(manifest.project == report.project())
        || !(manifest.contentIdentity == report.contentIdentity())) {
        // manifest 与报告身份不一致＝调用方装配违约（manifest 是报告的持久
        // 化事实——错链的 manifest 会让发布记录指向不存在的报告）。
        throw ReportError(ReportErrorCode::Usage,
                          "publish: manifest 身份列与报告不一致（reportId/project/contentIdentity）");
    }
    if (!manifest.reportId.isValid()) {
        // 保留值纪律（§4.1：全零＝空）——零 reportId 无编址意义（reports/
        // 目录键），fail-fast。
        throw ReportError(ReportErrorCode::Usage, "publish: manifest.reportId 为零（保留值）");
    }
    if (manifest.artifacts.size() != artifacts.size()) {
        // 清单与产物集必须一一对应（清单是发布事实的字节证据——缺项/多项
        // 都会让 manifestDigest 与磁盘事实脱节）。
        throw ReportError(ReportErrorCode::Usage,
                          "publish: manifest 工件清单与产物集数量不一致（"
                              + std::to_string(manifest.artifacts.size()) + " vs "
                              + std::to_string(artifacts.size()) + "）");
    }

    // ---- 第 2 步：幂等预检（§7.4 流程图"sink.begin：同 reportId 已
    // Finalized？"分支——经只读索引读磁盘事实，**零汇集座调用**：命中幂等
    // 时不产生任何 begin/write/finalize 调用，零重写由"根本不写"保证——
    // acceptance 1 的 sink 调用记录观测点）。 ----
    const core::ContentIdentity digest = computeManifestDigest(manifest);
    const std::optional<PublishedReportRecord> existing =
        m_index->tryPublished(manifest.project, manifest.reportId);
    if (existing.has_value()) {
        if (existing->manifestDigest == digest) {
            // 幂等成功（§7.4 行 1）：同 reportId 已 Finalized 且 manifest
            // 摘要一致——零重写、不报错，返回既有记录原文（含原
            // publishedAtUtc——不伪造发布时刻，落位偏差①的读取通道）。
            ArchiveOutcome out;
            out.status = ArchiveStatus::IdempotentHit;
            out.record = existing;
            return out;
        }
        // 冲突拒绝（§7.4 行 2）：摘要不一致——差异维度定位（acceptance 2①
        // "身份/版本维度"）：contentIdentity 已变＝身份维度（报告内容不同
        // ——评审演化/重建）；contentIdentity 相同而 manifestDigest 不同＝
        // 版本/工件集维度（同内容、渲染器/模板版本或格式集不同——D-02
        // 判定键的其余因子）。不覆盖（磁盘零调用——已 Finalized 内容不动）。
        const bool identityDimension = !(existing->contentIdentity == manifest.contentIdentity);
        const std::string dimText =
            identityDimension
                ? "身份维度（contentIdentity 不同——报告内容已变化）"
                : "版本/工件集维度（contentIdentity 相同、manifest 摘要不同——"
                  "渲染器/模板版本或格式集变化）";
        ArchiveOutcome out = failedOutcome(
            ReportErrorCode::ArchiveConflict,
            "publish: 同 reportId 已 Finalized 且 manifest 摘要不一致（差异定位：" + dimText
                + "）；不覆盖既有发布");
        // 差异定位诊断（RPT-ARCHIVE-CONFLICT＝收编登记值——diagcodes 引用；
        // 两摘要 canonical 文本入 cause 供人工比对——ERR-01 可定位）。
        out.diagnostics.push_back(makeDiag(
            std::string(diagcodes::kArchiveConflict), "报告归档冲突预检（RPT-01/§7.4）",
            "报告 " + manifest.reportId.toCanonical() + " 已发布（现存 manifest 摘要 "
                + existing->manifestDigest.toCanonical() + "），本次发布摘要 "
                + digest.toCanonical() + "；差异定位：" + dimText,
            "修改报告内容后以新 ReportId 发布，或核对渲染器/模板版本后重试（不覆盖既有发布）"));
        return out;
    }

    // ---- 第 3 步：取消检查点（逐阶段检查点之一——§9.5"检查点密集"；此时
    // 尚无会话，无需 abandon）。 ----
    if (m_cancel != nullptr && m_cancel->isCancelled()) {
        ArchiveOutcome out;
        out.status = ArchiveStatus::Canceled;   // UX-03：正常取消非错误——全空形态
        return out;
    }

    // ---- 第 4 步：begin（目录已建＝Staged——§7.3 状态图）。只读项目等
    // 环境错误在此以异常轨上抛（sink 契约：begin 的环境错误抛 ReportError
    // ——§9.9"StoreError 透传→ReportError"），协调器转译为 Failed 结果。 ----
    ArtifactSessionRef session;
    try {
        ReportArtifactBeginRequest request;
        request.reportId = manifest.reportId;
        request.project = manifest.project;
        request.contentIdentity = manifest.contentIdentity;
        request.idempotencyCheckRequired = true;   // 幂等预检开关恒开（§9.6 缺省语义）
        session = m_sink->begin(request);
    } catch (const ReportError& e) {
        // 环境错误（ReadOnlyStore/目录占用/上下文非 Active）——abandon 无从
        // 调用（无会话），错误＋诊断直接随结果交付。
        ArchiveOutcome out = failedOutcome(e.code(), std::string("publish: begin 失败——") + e.what());
        if (e.code() == ReportErrorCode::ReadOnlyStore) {
            // 只读项目两路径说明（acceptance 3/PM-07）：项目内发布拒绝≠导出
            // 被禁——项目外用户选择路径允许（ui report.export readOnlyAllowed）。
            // 码面引用单元卡 §7.4 表（RPT-READONLY-STORE——待收编，见文件头）。
            out.diagnostics.push_back(makeDiag(
                std::string(kDiagReadOnlyStore), "报告归档只读检查（PM-07/§7.4）",
                "项目为只读状态，项目内 reports/ 发布被拒绝（writable 检查归 project §6.8）",
                "两条路径：①解锁项目（获得写权威）后重试项目内发布；②将报告导出到"
                "项目外用户选择路径（report.export 只读项目允许）"));
        }
        return out;
    }

    // ---- 第 5 步：逐工件写入（begin→write×N——编排顺序 §9.6；逐工件前后
    // 取消检查点；只增幂等：续写同内容 IdempotentHit 按 Ok 继续——§9.6
    // 合法调用行"重试续写：write 只增幂等"）。 ----
    for (const RenderArtifact& a : artifacts) {
        // 工件清单↔产物集的对位：以工件摘要为键在 manifest 工件清单中查
        // relPath（摘要相等＝同一字节集——按下标假定同序是脆弱耦合；查不到
        // ＝清单与产物集不对应，属调用方装配违约）。前置已校验数量相等与
        // 逐项可对位，此处查找必然命中——防御性兜底仍保留。
        const auto it = std::find_if(manifest.artifacts.begin(), manifest.artifacts.end(),
                                     [&a](const ArtifactManifestEntry& e) {
                                         return e.sha256 == a.digest;
                                     });
        if (it == manifest.artifacts.end()) {
            m_sink->abandon(session, ReportEndReason::Failed);
            throw ReportError(ReportErrorCode::Usage,
                              "publish: 工件清单与产物集不对应（摘要无对位条目）");
        }
        // 写前取消检查点——取消→abandon(Canceled)→清理→目标不变。
        if (m_cancel != nullptr && m_cancel->isCancelled()) {
            m_sink->abandon(session, ReportEndReason::Canceled);
            ArchiveOutcome out;
            out.status = ArchiveStatus::Canceled;   // UX-03 全空形态（无诊断）
            return out;
        }
        ArtifactWrite write;
        write.relPath = it->relPath;
        write.bytes = a.bytes;
        const ArtifactStatus st = m_sink->writeArtifact(session, write);
        if (st == ArtifactStatus::Ok || st == ArtifactStatus::IdempotentHit) {
            continue;   // Ok＝新写成功；IdempotentHit＝目标已存在且摘要一致（跳过重写）
        }
        // 任一非 Ok→abandon（§9.6 编排行）——失败/取消两轨分流。
        m_sink->abandon(session, ReportEndReason::Failed);
        switch (st) {
        case ArtifactStatus::Conflict:
            // 写入期冲突（§9.6 finalize 注释的判定承载在 write 面提前命中
            // ——如并发会话已 Finalized 同目录）。
            return failedOutcome(ReportErrorCode::ArchiveConflict,
                                 std::string("publish: 工件写入冲突（") + token(st).data()
                                     + "：" + write.relPath + "）");
        case ArtifactStatus::DiskFull: {
            // 磁盘不足（§7.4 行 8）：abandon＋清理已完成（上行）＋可重试
            // （失败保留选择与路径——§16 验收要点）。比较型诊断：所需侧＝
            // 失败工件字节数（数值在位，来源＝派生只读——本进程事实）；
            // 可用侧＝存储侧未提供（四态 NotProvided——汇集座四方法契约无
            // 可用空间通道，精确值随 project 实现对齐补充，登记 §14.4）。
            ArchiveOutcome out = failedOutcome(
                ReportErrorCode::DiskFull,
                std::string("publish: 磁盘不足（工件 ") + write.relPath + "，所需 "
                    + std::to_string(write.bytes.size())
                    + " 字节；已 abandon＋清理——保留选择与路径可重试）");
            core::ComparativeFields comparison;
            comparison.expected.quantity =
                core::SourcedValue<double>::provided(
                    static_cast<double>(write.bytes.size()),
                    core::ValueProvenance::make(core::ProvenanceKind::DerivedReadOnly));
            comparison.expected.unit = core::UnitToken{};   // 字节非 SI 量纲——句柄保持无效，
                                                            // 单位语义以 cause 文本承载
            comparison.actual.quantity = core::SourcedValue<double>::notProvided();
            comparison.actual.unit = core::UnitToken{};
            out.diagnostics.push_back(makeDiag(
                std::string(kDiagDiskFull), "报告工件写入（RPT-DISK-FULL/§7.4 表）",
                "磁盘空间不足：所需 " + std::to_string(write.bytes.size())
                    + " 字节（工件 " + write.relPath + "）；可用空间存储侧未提供精确值",
                "清理空间后重试（会话已 abandon＋清理临时——选择与路径保留）"));
            out.diagnostics.back().comparison = comparison;
            return out;
        }
        case ArtifactStatus::WriteRejected:
        case ArtifactStatus::ContextClosed:
        default:
            // 环境类拒绝（只读写入/上下文关闭）——ExportFailed 面（保留
            // 选择与路径可重试）；其余未枚举状态同轨兜底（不吞错——如实
            // 报告状态 token）。
            return failedOutcome(ReportErrorCode::ExportFailed,
                                 std::string("publish: 工件写入被拒绝（") + token(st).data()
                                     + "：" + write.relPath + "）");
        }
    }

    // ---- 第 6 步：finalize 盖章与原子发布（manifest 原子发布＝唯一完整
    // 标志 D-13；finalizedAtUtc 由协调器盖章——发布时刻是发布事实，不回填
    // 报告对象〔PA-2：报告值不因发布回写〕）。 ----
    if (m_cancel != nullptr && m_cancel->isCancelled()) {
        m_sink->abandon(session, ReportEndReason::Canceled);
        ArchiveOutcome out;
        out.status = ArchiveStatus::Canceled;
        return out;
    }
    ReportArtifactManifest stamped = manifest;
    stamped.finalizedAtUtc = std::chrono::system_clock::now();
    stamped.manifestDigest = digest;
    const ArtifactStatus st = m_sink->finalize(session, stamped);
    switch (st) {
    case ArtifactStatus::Ok: {
        // 新发布完成：构造发布事实（§7.3 PublishedReportRecord 六字段）。
        // artifactRelPaths＝工件相对路径＋manifest 自身（report.json——发布
        // 成功至少含清单内工件＋唯一完整标志文件）。
        ArchiveOutcome out;
        out.status = ArchiveStatus::Published;
        PublishedReportRecord record;
        record.reportId = manifest.reportId;
        record.contentIdentity = manifest.contentIdentity;
        record.archiveState = ReportArchiveState::Finalized;
        record.manifestDigest = digest;
        record.artifactRelPaths.reserve(manifest.artifacts.size() + 1);
        for (const ArtifactManifestEntry& e : manifest.artifacts) {
            record.artifactRelPaths.push_back(e.relPath);
        }
        record.artifactRelPaths.emplace_back(kManifestRelPath);
        record.publishedAtUtc = stamped.finalizedAtUtc;
        out.record = std::move(record);
        return out;
    }
    case ArtifactStatus::IdempotentHit: {
        // 并发幂等兜底（§9.6 finalize 注释）：预检后他人已完成同内容发布
        // ——回查索引取既有记录原文；索引缺记录＝存储/索引不一致，如实
        // 失败（不伪造 publishedAtUtc——NFR-COR-03 不静默）。
        const std::optional<PublishedReportRecord> hit =
            m_index->tryPublished(manifest.project, manifest.reportId);
        if (hit.has_value()) {
            ArchiveOutcome out;
            out.status = ArchiveStatus::IdempotentHit;
            out.record = hit;
            return out;
        }
        return failedOutcome(
            ReportErrorCode::ExportFailed,
            "publish: finalize 幂等命中但发布清单缺记录（存储/索引不一致——如实失败）");
    }
    case ArtifactStatus::Conflict:
        // finalize 冲突（预检后内容被他人以不同内容 Finalized）——abandon
        // ＋ArchiveConflict（不覆盖已 Finalized 内容——零重写保证）。
        m_sink->abandon(session, ReportEndReason::Failed);
        return failedOutcome(ReportErrorCode::ArchiveConflict,
                             "publish: finalize 冲突（同 reportId 已被不同内容发布——不覆盖）");
    case ArtifactStatus::DiskFull:
        // manifest 原子发布期磁盘不足——同写入期口径（abandon＋比较型诊断；
        // 所需侧以 manifest 清单规模估算——精确值存储侧通道同前）。
        m_sink->abandon(session, ReportEndReason::Failed);
        {
            ArchiveOutcome out = failedOutcome(
                ReportErrorCode::DiskFull,
                "publish: finalize 磁盘不足（report.json 原子发布失败；已 abandon＋清理"
                "——保留选择与路径可重试）");
            core::ComparativeFields comparison;
            comparison.expected.quantity = core::SourcedValue<double>::notProvided();
            comparison.expected.unit = core::UnitToken{};
            comparison.actual.quantity = core::SourcedValue<double>::notProvided();
            comparison.actual.unit = core::UnitToken{};
            out.diagnostics.push_back(makeDiag(
                std::string(kDiagDiskFull), "报告 manifest 发布（RPT-DISK-FULL/§7.4 表）",
                "磁盘空间不足：report.json 原子发布失败（所需/可用精确值存储侧未提供）",
                "清理空间后重试（会话已 abandon＋清理临时——选择与路径保留）"));
            out.diagnostics.back().comparison = comparison;
            return out;
        }
    case ArtifactStatus::WriteRejected:
    case ArtifactStatus::ContextClosed:
    default:
        m_sink->abandon(session, ReportEndReason::Failed);
        return failedOutcome(ReportErrorCode::ExportFailed,
                             std::string("publish: finalize 被拒绝（") + token(st).data() + "）");
    }
}

std::vector<ReportListingEntry> ReportArchiveCoordinator::listPublished(
    core::ProjectId project) const
{
    // 仅 Finalized（§9.6 原文注释）——未完成工件（manifest 缺失）由索引侧
    // 排除（"文件存在≠已发布"——任务约束§五.6/PM-08）。清单呈现确定性：
    // 索引按 reportId 字典序返回（IReportPublishedIndex 契约），本层保序
    // 投影——同数据同清单（NFR-COR-02 同精神）。
    const std::vector<PublishedReportRecord> records = m_index->listPublished(project);
    std::vector<ReportListingEntry> entries;
    entries.reserve(records.size());
    for (const PublishedReportRecord& r : records) {
        ReportListingEntry e;
        e.reportId = r.reportId;
        e.contentIdentity = r.contentIdentity;
        e.manifestDigest = r.manifestDigest;
        e.finalizedAtUtc = r.publishedAtUtc;
        entries.push_back(std::move(e));
    }
    return entries;
}

}  // namespace sdurws::ird::reporting
