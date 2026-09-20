/**
 * @file   Bundle.cpp
 * @brief  证据包组装实现——EvidenceBundleAssembler 编排体（§7.6 全流程）＋
 *         格式常量/条目名/状态词面（Bundle.hpp 契约的实现侧）。
 *
 * 设计依据（与 Bundle.hpp 文件头同源，此处只记实现口径）：
 *   - units/reporting.md §7.6（流程原文＋要素边界＋"数值来自 evidence 复现块
 *     与配置 canonical，不自算"）、§9.6（IArchiveWriter 三方法契约）、§3.5
 *     （错误码面——BundleIncomplete/Usage/ExportFailed）、§9 章首（协作取消
 *     检查点）、§13（RPT-03/NFR-COR-04 追溯链交付形态）
 *   - 任务契约 tasks/foundation/RPT-T10.json acceptance 1~5
 *
 * 实现口径登记（DTB §5.4——登记于单元卡 §14.4 v0.13，零契约语义变更）：
 *   ①bundle.json 六字段名与 JSON 形状（§7.6 大括号原文的字段级落位）；
 *   ②条目编址：snapshot/<runId>/snapshot.json、results/<runId>/result.json、
 *     reproduction.json、bundle.json（§7.6 目录形态的 ZIP 条目化——容器以
 *     路径式条目名表达层级）；
 *   ③totalDigest 规范化域＝IRDRPTB1＋codec 版本＋条目数＋逐条目（名＋SHA-256，
 *     条目名字节序）大端编码，排除 bundle.json 自身（D-04——generatedAtUtc
 *     不入内容摘要）；
 *   ④reproduction.json 形状＝逐运行 {versions〔evidence ReproductionBlock
 *     逐字段原样〕＋configurations〔配置 canonical 引用〕}——不聚合不改写
 *     （"不自算"纪律：种子/线程数/容差的值在配置 canonical 字节内，域
 *     schema 归域，reporting 只承载引用）；
 *   ⑤放弃路径清理＝IArchiveWriter 实现的析构义务（RAII——Bundle.hpp 文件头
 *     "IArchiveWriter 的放弃路径"节，§9.6 三方法签名零改动）。
 *
 * 线程约束：assemble 无共享可变状态（并发安全）；本翻译单元全部函数为
 * 纯函数或实例方法（无全局可变态）。确定性：条目序/键序/编码全部 canonical
 * ——同输入同包（totalDigest 逐字节相等）。
 */

#include "sdurws/ird/reporting/Bundle.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <utility>

#include <sdurws/ird/evidence/Envelope.hpp>   // evidence::ResultEnvelope（证据清单/绑定面）
#include <sdurws/ird/evidence/Snapshot.hpp>   // evidence::ReproductionBlock（复现要素值）

#include <sdurws/ird/reporting/ReportModel.hpp>  // ReviewReport（解析所得报告值）

namespace sdurws::ird::reporting {

// =====================================================================
// 公共小函数（Bundle.hpp 契约面）
// =====================================================================

std::string bundleSnapshotEntryName(core::RunId runId)
{
    // "snapshot/<runId>/snapshot.json"——runId 规范文本（"run-<32hex>"）
    // 字符集受限，天然满足 SP-2 纯相对正斜杠（Bundle.hpp 注释）。
    return "snapshot/" + runId.toCanonical() + "/snapshot.json";
}

std::string bundleResultEntryName(core::RunId runId)
{
    // "results/<runId>/result.json"——结果引用条目（§7.6 "results/<run>/"）。
    return "results/" + runId.toCanonical() + "/result.json";
}

std::string_view bundleItemStatusToken(evidence::EvidenceItemStatus status) noexcept
{
    // 序列化词面映射（格式词面非第二词表——Bundle.hpp 函数注释的权威声明）。
    // 全枚举 switch 无 default：evidence 追加枚举值时编译器告警暴露本表遗漏。
    switch (status) {
    case evidence::EvidenceItemStatus::Satisfied: return "satisfied";
    case evidence::EvidenceItemStatus::Missing: return "missing";
    case evidence::EvidenceItemStatus::Invalid: return "invalid";
    case evidence::EvidenceItemStatus::Unverified: return "unverified";
    case evidence::EvidenceItemStatus::NotApplicable: return "not-applicable";
    }
    return "unknown";   // 不可达（全枚举）——防御位，与 token() 家族同款
}

std::string_view token(BundleStatus status) noexcept
{
    switch (status) {
    case BundleStatus::Completed: return "completed";
    case BundleStatus::Canceled: return "canceled";
    case BundleStatus::Failed: return "failed";
    }
    return "unknown";
}

bool operator==(const BundleConfigRef& a, const BundleConfigRef& b) noexcept
{
    return a.configKindToken == b.configKindToken && a.contentIdentity == b.contentIdentity;
}

bool operator!=(const BundleConfigRef& a, const BundleConfigRef& b) noexcept
{
    return !(a == b);
}

// =====================================================================
// 匿名命名空间：编码/格式化/JSON 构造辅助（全部 canonical——同输入同字节）
// =====================================================================

namespace {

/// 小写十六进制编码（字节→文本的展示形；非哈希算法——SHA-256 唯一算法
/// 仍经 core ContentDigester，SA-12 约束的是摘要算法与序列化器，不是
/// 字节展示格式。ReportId 自建 hex 格式化同先例——P-RPT-3）。
std::string hexLower(const std::uint8_t* data, std::size_t n)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kDigits[data[i] >> 4]);       // 高 4 位
        out.push_back(kDigits[data[i] & 0x0Fu]);    // 低 4 位
    }
    return out;
}

/// 32 字节摘要的 64 位小写 hex（bundle 内 digest 列的统一文本形）。
std::string hexOf(const core::Digest256& digest)
{
    return hexLower(digest.data(), digest.size());
}

/// 字节向的 SHA-256（core ContentDigester 唯一算法——CR-02 不私设第二
/// 哈希路径；本函数只是调用形态收口）。
core::Digest256 sha256Of(const std::vector<std::uint8_t>& bytes)
{
    core::ContentDigester digester;
    digester.update(bytes.data(), bytes.size());
    return digester.finalize();
}

/// 大端追加 u32（ReportCodec/Archive.cpp §4.4 整数编码同口径——跨平台
/// 字节稳定；totalDigest 规范化域用）。
void appendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 24));
    out.push_back(static_cast<std::uint8_t>(v >> 16));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

/// UTC 时刻的 ISO-8601 文本（"YYYY-MM-DDTHH:MM:SSZ"＝恰 20 字符＋NUL；
/// 缓冲取 32 防截断。时刻是记录性字段不入摘要——D-04）。
std::string formatUtcIso8601(std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    if (gmtime_s(&tm, &t) != 0) {
        return {};   // 时刻转换失败＝记录性字段留空（不入摘要——可观测的降级）
    }
#else
    gmtime_r(&t, &tm);   // POSIX 线程安全版
#endif
    char buf[32] = {};
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm) == 0) {
        return {};   // 格式化失败＝同上（留空不伪造——NFR-COR-03 同精神）
    }
    return buf;
}

// ---- JSON 文档构造（§7.6 四类条目——键序固定＝装配序，canonical 写出
//      经注入 io IReportJsonWriter〔SA-12〕；DOM 六型见 Render.hpp）----
//
// 摘要/身份列一律以规范文本入 JSON（"cid-<64hex>"/"rpt-<32hex>"/"run-<32hex>"
// ——消费方可直接经 tryFromCanonical 回读；64hex 裸形仅用于无 tag 的
// Digest256〔envelope 摘要/证据产物摘要——归档工件摘要列〕）。

/// ReproductionBlock → JSON 对象（逐字段原样承载——evidence 值拷贝零改写；
/// 可选字段缺席＝键省略〔§5.2"optional 缺失与空值不等价"的序列化对偶：
/// 有值空串与缺席在 JSON 上可区分〕）。
ReportJsonDom reproductionJson(const evidence::ReproductionBlock& block)
{
    std::vector<std::pair<std::string, ReportJsonDom>> members;
    members.emplace_back("productVersion", ReportJsonDom::string(block.productVersion));
    members.emplace_back("evidenceContractVersion",
                         ReportJsonDom::string(block.evidenceContractVersion));
    std::vector<ReportJsonDom> codecs;
    codecs.reserve(block.codecVersions.size());
    for (const std::string& v : block.codecVersions) {
        codecs.push_back(ReportJsonDom::string(v));   // 保序承载——evidence 不排序、此处亦不重排
    }
    members.emplace_back("codecVersions", ReportJsonDom::array(std::move(codecs)));
    if (block.compilerContractVersion.has_value()) {
        members.emplace_back("compilerContractVersion",
                             ReportJsonDom::string(*block.compilerContractVersion));
    }
    if (block.collisionBackendVersion.has_value()) {
        members.emplace_back("collisionBackendVersion",
                             ReportJsonDom::string(*block.collisionBackendVersion));
    }
    return ReportJsonDom::object(std::move(members));
}

/// 证据清单 → JSON 对象（§7.6 "证据清单（itemId/status/digest）"——逐项
/// 三列；digest 仅 Satisfied 在场〔presence 纪律的序列化对偶——非 Satisfied
/// 无产物凭据可载〕）。
ReportJsonDom evidenceListJson(const evidence::EvidenceManifest& manifest)
{
    std::vector<std::pair<std::string, ReportJsonDom>> evMembers;
    evMembers.emplace_back("profileId", ReportJsonDom::string(manifest.profileId));
    evMembers.emplace_back("profileVersion", ReportJsonDom::string(manifest.profileVersion));
    evMembers.emplace_back("profileContentIdentity",
                           ReportJsonDom::string(manifest.profileContentIdentity.toCanonical()));
    std::vector<ReportJsonDom> items;
    items.reserve(manifest.items.size());
    for (const evidence::EvidenceItem& item : manifest.items) {
        std::vector<std::pair<std::string, ReportJsonDom>> itemMembers;
        itemMembers.emplace_back("itemId", ReportJsonDom::string(item.itemId));
        itemMembers.emplace_back("status", ReportJsonDom::string(
            std::string(bundleItemStatusToken(item.status))));
        if (item.artifactDigest.has_value()) {
            itemMembers.emplace_back("digest", ReportJsonDom::string(hexOf(*item.artifactDigest)));
        }
        items.push_back(ReportJsonDom::object(std::move(itemMembers)));
    }
    evMembers.emplace_back("items", ReportJsonDom::array(std::move(items)));
    return ReportJsonDom::object(std::move(evMembers));
}

/// 单运行的收集事实（收集阶段产物——写出阶段的输入；"先全量收集再写"
/// 是"要素缺失→拒绝、不产出半包"且零临时的结构保证——assemble 编排步骤 3）。
struct CollectedRun {
    core::RunId runId;                             ///< 运行身份（编址键）
    evidence::ResultEnvelope envelope;             ///< 包络只读值（证据清单数据源）
    evidence::ReproductionBlock reproduction;      ///< 逐运行复现块（evidence 值拷贝）
    core::Digest256 envelopeDigest;                ///< 归档 envelope 工件摘要（事实投影）
    core::ContentIdentity caseSetDigest;           ///< 必验工况集冻结凭据（事实投影）
    std::vector<BundleConfigRef> configurations;   ///< 配置 canonical 引用集（保序）
};

}  // namespace

// =====================================================================
// EvidenceBundleAssembler——构造与编排体
// =====================================================================

EvidenceBundleAssembler::EvidenceBundleAssembler(const IReportResolver& resolver,
                                                 IReportResultSource& resultSource,
                                                 IEvidenceBundleSource& bundleSource,
                                                 IArchiveWriter& writer,
                                                 const IReportIoFactory& ioFactory) noexcept
    : m_resolver(&resolver)
    , m_resultSource(&resultSource)
    , m_bundleSource(&bundleSource)
    , m_writer(&writer)
    , m_ioFactory(&ioFactory)
{
}

EvidenceBundleOutcome
EvidenceBundleAssembler::assemble(const EvidenceBundleRequest& request,
                                  const ReportCancelToken* cancel)
{
    // ---------------------------------------------------------------
    // 第 1 步：请求校验（Usage fail-fast——调用方契约违约，AGENTS.md 错误
    // 语义；与 ReportArchiveCoordinator::publish 前置校验同款）。
    // ---------------------------------------------------------------
    if (!request.reportId.isValid()) {
        throw ReportError(ReportErrorCode::Usage, "assemble: reportId 为零（保留值）");
    }
    if (request.target.empty()) {
        throw ReportError(ReportErrorCode::Usage, "assemble: target 为空（外部目标必填）");
    }
    if (m_resolver == nullptr || m_resultSource == nullptr || m_bundleSource == nullptr
        || m_writer == nullptr || m_ioFactory == nullptr) {
        // 注入面空指针＝构造期装配违约（noexcept 构造不校验——此处兜底，
        // 语义仍是调用方违约 fail-fast）。
        throw ReportError(ReportErrorCode::Usage, "assemble: 注入引用为空（装配违约）");
    }

    // reportId → 冻结报告值（§9.5 解析缝复用；不可解析＝Usage——包没有
    // 报告值就没有身份锚与 resultRefs 全集）。
    const std::optional<ReviewReport> resolved = m_resolver->tryResolve(request.reportId);
    if (!resolved.has_value()) {
        throw ReportError(ReportErrorCode::Usage,
                          "assemble: reportId 不可解析（未构建/未发布——" + request.reportId.toCanonical()
                              + "）");
    }
    const ReviewReport& report = *resolved;
    if (!(report.reportId() == request.reportId)) {
        // 解析缝违约：按 reportId 解析所得报告与请求身份不一致——调用方
        // 装配错误（fail-fast，不静默按解析值继续）。
        throw ReportError(ReportErrorCode::Usage,
                          "assemble: 解析报告身份与请求不一致（请求 " + request.reportId.toCanonical()
                              + " ≠ 解析 " + report.reportId().toCanonical() + "）");
    }

    // 结果集定版：显式 runs 或缺省（§7.6 原文"默认＝报告 resultRefs"）。
    // 排序按 runId 字节序（＝规范文本序——core Id128 纪律），与报告
    // resultRefs 的 canonical 序一致：条目名/sourceRuns 列的确定性来源。
    std::vector<core::RunId> runs = request.runs;
    if (runs.empty()) {
        runs.reserve(report.resultRefs().size());
        for (const ResultRefSnapshot& ref : report.resultRefs()) {
            runs.push_back(ref.runId);
        }
    }
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (!runs[i].isValid()) {
            throw ReportError(ReportErrorCode::Usage,
                              "assemble: runs[" + std::to_string(i) + "] 为零（保留值）");
        }
        for (std::size_t j = i + 1; j < runs.size(); ++j) {
            if (runs[i] == runs[j]) {
                throw ReportError(ReportErrorCode::Usage,
                                  "assemble: runs 重复（" + runs[i].toCanonical() + "）");
            }
        }
        // 越界核对：包是"这份报告的证据包"——选报告未引用的结果会让包与
        // 身份锚（reportId/contentIdentity）脱钩（Usage，不静默扩集）。
        bool referenced = false;
        for (const ResultRefSnapshot& ref : report.resultRefs()) {
            if (ref.runId == runs[i]) {
                referenced = true;
                break;
            }
        }
        if (!referenced) {
            throw ReportError(ReportErrorCode::Usage,
                              "assemble: runs 越界（" + runs[i].toCanonical()
                                  + " 不在报告 resultRefs——EvidenceBundleRequest 语义）");
        }
    }
    std::sort(runs.begin(), runs.end());   // 字节序＝规范文本序（确定性）

    // ---------------------------------------------------------------
    // 第 2 步：取消检查点（入轨——尚无任何临时物，直接取消返回）。
    // ---------------------------------------------------------------
    if (cancel != nullptr && cancel->isCancelled()) {
        return EvidenceBundleOutcome{BundleStatus::Canceled, std::nullopt, {}, std::nullopt};
    }

    // ---------------------------------------------------------------
    // 第 3 步：收集阶段（零 IO——先全量收集再写。"要素缺失→拒绝、不产出
    // 半包"的结构保证：任何缺失在 writer.open 之前即拒绝，目标路径上零
    // 临时文件）。逐运行五项事实＋复现块完整性核对；缺失全量列出（不短路
    // ——acceptance 2"全量列出缺失项"）。
    // ---------------------------------------------------------------
    std::vector<CollectedRun> collected;
    collected.reserve(runs.size());
    std::vector<std::string> missing;   // 缺失要素清单（"<要素>@<runId>" 形态）
    for (const core::RunId runId : runs) {
        const std::string runText = runId.toCanonical();

        // ①envelope（§7.6 输入①——resultSource.tryEnvelope 同名方法）。
        std::optional<evidence::ResultEnvelope> envelope = m_resultSource->tryEnvelope(runId);
        if (!envelope.has_value()) {
            missing.push_back("envelope@" + runText);
        }

        // ②逐运行复现块（§7.6 results/<run>/ "复现要素"列的数据源——
        // evidence ReproductionBlock 值拷贝）。
        std::optional<evidence::ReproductionBlock> reproduction =
            m_resultSource->tryReproduction(runId);
        if (!reproduction.has_value()) {
            missing.push_back("reproduction@" + runText);
        } else {
            // 复现块完整性核对（acceptance 2"复现块不完整"路径）：两个
            // 必填字段（§4.1.2 原文 productVersion/evidenceContractVersion）
            // 任一为空即"不完整"——逐字段计入缺失清单（全量不短路）。
            if (reproduction->productVersion.empty()) {
                missing.push_back("reproduction.productVersion@" + runText);
            }
            if (reproduction->evidenceContractVersion.empty()) {
                missing.push_back("reproduction.evidenceContractVersion@" + runText);
            }
        }

        // ③归档 envelope 工件摘要（§7.6 输入②投影——reporting 不自算）。
        std::optional<core::Digest256> envelopeDigest = m_bundleSource->tryEnvelopeDigest(runId);
        if (!envelopeDigest.has_value()) {
            missing.push_back("envelope-digest@" + runText);
        }

        // ④必验工况集冻结凭据（snapshot/<run>/ "caseSet 摘要"——evidence
        // requiredCaseSetId 投影）。
        std::optional<core::ContentIdentity> caseSetDigest =
            m_bundleSource->tryCaseSetDigest(runId);
        if (!caseSetDigest.has_value()) {
            missing.push_back("case-set-digest@" + runText);
        }

        // ⑤配置 canonical 引用集（reproduction.json 种子/线程/容差承载——
        // nullopt＝事实不可解析计缺失；空集＝快照合法无配置，不算缺失）。
        std::optional<std::vector<BundleConfigRef>> configurations =
            m_bundleSource->tryConfigurationRefs(runId);
        if (!configurations.has_value()) {
            missing.push_back("configurations@" + runText);
        }

        // 逐运行取消检查点（§10.1 RP-BUN-1 观测点"检查点逐条目"——收集
        // 阶段的条目粒度＝逐运行）。
        if (cancel != nullptr && cancel->isCancelled()) {
            return EvidenceBundleOutcome{BundleStatus::Canceled, std::nullopt, {},
                                         std::nullopt};
        }

        // 五项齐备才入收集集（缺失项已登记——不再消费残缺事实）。
        if (envelope.has_value() && reproduction.has_value() && envelopeDigest.has_value()
            && caseSetDigest.has_value() && configurations.has_value()) {
            CollectedRun row;
            row.runId = runId;
            row.envelope = std::move(*envelope);
            row.reproduction = std::move(*reproduction);
            row.envelopeDigest = *envelopeDigest;
            row.caseSetDigest = *caseSetDigest;
            row.configurations = std::move(*configurations);
            collected.push_back(std::move(row));
        }
    }

    // 要素缺失→BundleIncomplete 拒绝（不产出半包——§7.6 要素边界原文；
    // detail 全量列出：逐项"<要素>@<runId>"，供调用方一次修复全部缺失）。
    if (!missing.empty()) {
        std::string detail = "assemble: 证据包要素缺失 " + std::to_string(missing.size())
                             + " 项（不产出半包）：";
        for (std::size_t i = 0; i < missing.size(); ++i) {
            if (i > 0) {
                detail += "；";
            }
            detail += missing[i];
        }
        EvidenceBundleOutcome out;
        out.status = BundleStatus::Failed;
        out.error = ReportError(ReportErrorCode::BundleIncomplete, std::move(detail));
        return out;   // writer 从未 open——目标路径零临时、零写出
    }

    // ---------------------------------------------------------------
    // 第 4 步：写出阶段。条目集先在内存定版（名称＋字节），再逐条目写入
    // ——名称一律由本实现以 runId 规范文本构造（SP-2 安全）；JSON 字节
    // 经注入 io 写出器序列化（SA-12——reporting 不自建序列化器）。
    // ---------------------------------------------------------------

    // JSON 文档 → canonical 字节（io 写出器＋内存目标——Render.cpp 渲染链
    // 同款调用形态；失败＝写出通道错误 ExportFailed——可重试语义）。
    auto canonicalBytes = [this](const ReportJsonDom& dom) {
        std::vector<std::uint8_t> buffer;
        MemoryReportOutputTarget target(&buffer);
        auto jsonWriter = m_ioFactory->makeJsonWriter();
        if (jsonWriter == nullptr || !jsonWriter->write(std::move(target), dom)) {
            throw ReportError(ReportErrorCode::ExportFailed,
                              "assemble: JSON canonical 写出失败（io 适配）");
        }
        return buffer;
    };

    // 待写条目集（名称→字节）。装配序＝canonical 序（条目名字节序——
    // NFR-COR-02），bundle.json 殿后（totalDigest 依赖其余全部条目）。
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> entries;

    // 逐运行两条目：snapshot/<run>/snapshot.json＋results/<run>/result.json。
    for (const CollectedRun& row : collected) {
        // —— snapshot/<run>/：输入快照引用元数据（refs-only——§7.6 原文
        //    四列；值全部来自包络绑定面与 evidence 冻结凭据，零自算）。
        std::vector<std::pair<std::string, ReportJsonDom>> snapMembers;
        snapMembers.emplace_back("snapshotId",
                                 ReportJsonDom::string(row.envelope.snapshotId.toCanonical()));
        snapMembers.emplace_back("sliceId",
                                 ReportJsonDom::string(row.envelope.sliceId.toCanonical()));
        snapMembers.emplace_back(
            "inputBaselineId", ReportJsonDom::string(row.envelope.inputBaselineId.toCanonical()));
        snapMembers.emplace_back("caseSetDigest",
                                 ReportJsonDom::string(row.caseSetDigest.toCanonical()));
        entries.emplace_back(bundleSnapshotEntryName(row.runId),
                             canonicalBytes(ReportJsonDom::object(std::move(snapMembers))));

        // —— results/<run>/：结果引用（envelope 摘要＋证据清单＋复现要素
        //    ——§7.6 原文三列；摘要/清单/复现块全部为 evidence/归档事实
        //    值的逐字段承载）。
        std::vector<std::pair<std::string, ReportJsonDom>> resultMembers;
        resultMembers.emplace_back("runId", ReportJsonDom::string(row.runId.toCanonical()));
        resultMembers.emplace_back("envelopeDigest",
                                   ReportJsonDom::string(hexOf(row.envelopeDigest)));
        resultMembers.emplace_back("evidence", evidenceListJson(row.envelope.evidence));
        resultMembers.emplace_back("reproduction", reproductionJson(row.reproduction));
        entries.emplace_back(bundleResultEntryName(row.runId),
                             canonicalBytes(ReportJsonDom::object(std::move(resultMembers))));
    }

    // —— reproduction.json：复现要素汇总（§7.6 原文顶层条目——逐运行
    //    versions〔evidence 复现块逐字段〕＋configurations〔配置 canonical
    //    引用——种子/线程数/容差的值在其中，域 schema 归域，reporting 只
    //    承载引用——"不自算"纪律的实现形态〕）。
    std::vector<ReportJsonDom> runNodes;
    runNodes.reserve(collected.size());
    for (const CollectedRun& row : collected) {
        std::vector<std::pair<std::string, ReportJsonDom>> runMembers;
        runMembers.emplace_back("runId", ReportJsonDom::string(row.runId.toCanonical()));
        runMembers.emplace_back("versions", reproductionJson(row.reproduction));
        std::vector<ReportJsonDom> configNodes;
        configNodes.reserve(row.configurations.size());
        for (const BundleConfigRef& cfg : row.configurations) {
            std::vector<std::pair<std::string, ReportJsonDom>> cfgMembers;
            cfgMembers.emplace_back("configKindToken", ReportJsonDom::string(cfg.configKindToken));
            cfgMembers.emplace_back("contentIdentity",
                                    ReportJsonDom::string(cfg.contentIdentity.toCanonical()));
            configNodes.push_back(ReportJsonDom::object(std::move(cfgMembers)));
        }
        runMembers.emplace_back("configurations", ReportJsonDom::array(std::move(configNodes)));
        runNodes.push_back(ReportJsonDom::object(std::move(runMembers)));
    }
    std::vector<std::pair<std::string, ReportJsonDom>> reproMembers;
    reproMembers.emplace_back("runs", ReportJsonDom::array(std::move(runNodes)));
    entries.emplace_back(std::string(kBundleReproductionName),
                         canonicalBytes(ReportJsonDom::object(std::move(reproMembers))));

    // 条目名稳定排序（除 bundle.json——canonical 序；同输入同序——
    // totalDigest 与容器条目序的确定性来源）。
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // totalDigest（acceptance 1 第六字段）：规范化域＝magic＋编码器版本＋
    // 条目数＋逐条目（名长 u32＋名字节＋条目 SHA-256 32 字节），整数大端、
    // SHA-256 经 core 唯一算法。排除 bundle.json 自身——其 generatedAtUtc
    // 是生成事实不入内容摘要（D-04），排除后同输入同摘要（NFR-COR-02）。
    std::vector<std::uint8_t> digestDomain;
    digestDomain.reserve(64 + entries.size() * 48);
    digestDomain.insert(digestDomain.end(), kBundleDigestMagic.begin(),
                        kBundleDigestMagic.end());
    appendU32(digestDomain, kBundleDigestCodecVersion);
    appendU32(digestDomain, static_cast<std::uint32_t>(entries.size()));
    for (const auto& [name, bytes] : entries) {
        appendU32(digestDomain, static_cast<std::uint32_t>(name.size()));
        digestDomain.insert(digestDomain.end(), name.begin(), name.end());
        const core::Digest256 entryDigest = sha256Of(bytes);
        digestDomain.insert(digestDomain.end(), entryDigest.begin(), entryDigest.end());
    }
    const core::Digest256 totalDigest = sha256Of(digestDomain);

    // —— bundle.json（殿后写入）：六字段（§7.6 大括号原文——acceptance 1
    //    的逐字段核对面）。
    std::vector<std::pair<std::string, ReportJsonDom>> bundleMembers;
    bundleMembers.emplace_back("schemaVersion", ReportJsonDom::string(
        std::string(kBundleSchemaVersion)));
    bundleMembers.emplace_back("reportId", ReportJsonDom::string(report.reportId().toCanonical()));
    bundleMembers.emplace_back("contentIdentity",
                               ReportJsonDom::string(report.contentIdentity().toCanonical()));
    std::vector<ReportJsonDom> sourceRunNodes;
    sourceRunNodes.reserve(runs.size());
    for (const core::RunId runId : runs) {
        sourceRunNodes.push_back(ReportJsonDom::string(runId.toCanonical()));
    }
    bundleMembers.emplace_back("sourceRuns", ReportJsonDom::array(std::move(sourceRunNodes)));
    bundleMembers.emplace_back("generatedAtUtc",
                               ReportJsonDom::string(formatUtcIso8601(std::chrono::system_clock::now())));
    bundleMembers.emplace_back("totalDigest", ReportJsonDom::string(hexOf(totalDigest)));
    entries.emplace_back(std::string(kBundleManifestName),
                         canonicalBytes(ReportJsonDom::object(std::move(bundleMembers))));

    // ---------------------------------------------------------------
    // 第 5 步：writer 会话（open→addEntry×N→finish）。本段任一失败/取消：
    //   - 失败→writer 契约自理临时清理（其 finish/析构失败路径——Bundle.hpp
    //     "放弃路径"节）、目标不变；装配器映射 Failed（错误原码保留——
    //     不吞错，NFR-COR-03）；
    //   - 取消→直接返回 Canceled（UX-03 全空形态）；writer 会话由其属主
    //     析构收尾（RAII 清理临时、目标不变——§9.6 三方法签名零改动的
    //     放弃路径契约）。
    // ---------------------------------------------------------------
    try {
        m_writer->open(request.target);
    } catch (const ReportError& e) {
        EvidenceBundleOutcome out;
        out.status = BundleStatus::Failed;
        out.error = ReportError(e.code(), std::string("assemble: open 失败——") + e.what());
        return out;
    } catch (const std::exception& e) {
        EvidenceBundleOutcome out;
        out.status = BundleStatus::Failed;
        out.error = ReportError(ReportErrorCode::ExportFailed,
                                std::string("assemble: open 失败（通道）——") + e.what());
        return out;
    }

    // 逐条目写入（写前取消检查点——"检查点逐条目"的写出阶段粒度）。
    for (const auto& [entryName, entryBytes] : entries) {
        if (cancel != nullptr && cancel->isCancelled()) {
            return EvidenceBundleOutcome{BundleStatus::Canceled, std::nullopt, {},
                                         std::nullopt};
        }
        // addEntry 的 SP-2/重复名违约理论不可达（名称全部由本实现以 runId
        // 规范文本构造——字符集受限；若触发＝实现缺陷，按 Usage 透出，
        // 不静默吞掉——writer 契约的异常轨原样上抛后在此定码）。
        try {
            m_writer->addEntry(entryName, entryBytes);
        } catch (const ReportError& e) {
            EvidenceBundleOutcome out;
            out.status = BundleStatus::Failed;
            out.error = ReportError(e.code(), std::string("assemble: addEntry 失败（")
                                              + entryName + "）——" + e.what());
            return out;
        } catch (const std::exception& e) {
            EvidenceBundleOutcome out;
            out.status = BundleStatus::Failed;
            out.error = ReportError(ReportErrorCode::ExportFailed,
                                    std::string("assemble: addEntry 失败（") + entryName
                                        + "）——" + e.what());
            return out;
        }
    }

    // finish 前取消检查点（逐条目检查点链的最后一环——finish 是原子发布
    // 点，发布完成后取消不再可表达；因此检查点必须落在 finish 之前）。
    if (cancel != nullptr && cancel->isCancelled()) {
        return EvidenceBundleOutcome{BundleStatus::Canceled, std::nullopt, {}, std::nullopt};
    }

    // finish：原子替换到目标（§9.6 原文——失败时 writer 自理清理、目标
    // 不变）。返回值＝容器级字节面摘要（writer 自定义规范化域），与
    // bundle.json 的 totalDigest 互不替代——装配器不消费（显式弃置）。
    try {
        const core::Digest256 containerDigest = m_writer->finish();
        (void)containerDigest;
    } catch (const ReportError& e) {
        EvidenceBundleOutcome out;
        out.status = BundleStatus::Failed;
        out.error = ReportError(e.code(), std::string("assemble: finish 失败——") + e.what());
        return out;
    } catch (const std::exception& e) {
        EvidenceBundleOutcome out;
        out.status = BundleStatus::Failed;
        out.error = ReportError(ReportErrorCode::ExportFailed,
                                std::string("assemble: finish 失败（通道）——") + e.what());
        return out;
    }

    // Completed：目标已是完整包（四类条目齐备——"不产出半包"的后置面）。
    EvidenceBundleOutcome out;
    out.status = BundleStatus::Completed;
    out.totalDigest = totalDigest;
    return out;
}

}  // namespace sdurws::ird::reporting
