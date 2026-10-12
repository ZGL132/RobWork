/**
 * @file   PackageServiceImpl.cpp
 * @brief  项目包服务实现——§8.10 导出/导入协议的编排落地与结果装配
 *         （PRJ-T18）。
 *
 * 设计依据：见 PackageServiceImpl.hpp 文件头与 PackageService.hpp 公共
 *   契约（执行序、错误轨口径、结果字段纪律）；共享原语见 StoreClone
 *   （快照/发布/临时位，实现口径①～⑦登记处）。
 *
 * 线程模型：见 PackageServiceImpl.hpp 文件头（m_opMutex 服务级互斥）。
 */

#include "PackageServiceImpl.hpp"

#include <sdurws/ird/project/ProjectStore.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::project {

// =====================================================================
// PackageImportResult 特殊成员函数（out-of-line——unique_ptr<ProjectStore>
// 的删除器实例化需要完整类型，OpenStoreResult 先例同款；本文件 include
// 了 ProjectStore.hpp，此处即完整类型可见处）。
// =====================================================================

PackageImportResult::~PackageImportResult() = default;
PackageImportResult::PackageImportResult() = default;
PackageImportResult::PackageImportResult(PackageImportResult&& other) = default;
PackageImportResult& PackageImportResult::operator=(PackageImportResult&& other) = default;

// =====================================================================
// 服务实体
// =====================================================================

PackageServiceImpl::PackageServiceImpl(ProjectStore* source,
                                       IPackageCodec* codec,
                                       win32::IFileOps* fileOps,
                                       core::IDomainEventBus* eventBus,
                                       IDiagnosticsSink* diagnostics)
    : m_source(source)
    , m_codec(codec)
    , m_ops(fileOps != nullptr ? fileOps : clone::defaultFileOps())
    , m_eventBus(eventBus)
    , m_sink(diagnostics)
{
    if (m_codec == nullptr) {
        // 装配违约 fail-fast（编解码实现是两向操作的机制前提——缺注入
        // 的服务不可能完成任何操作，尽早暴露不过夜）。
        throw std::invalid_argument("project/package: 编解码实现未注入（装配违约）");
    }
}

PackageExportResult PackageServiceImpl::exportPackage(
    const PackageExportOptions& options)
{
    // ---- 服务级操作互斥（文件头线程模型）。
    std::lock_guard<std::mutex> opGuard(m_opMutex);

    PackageExportResult result;

    // ---- 前置校验（调用方错误 fail-fast）：codec 消费面与源绑定是
    // 导出的机制前提；目标文件形态契约（§5.8 冻结注释".rwpack"——
    // 扩展名小写比较，Windows 文件系统大小写不敏感的口径一致）。
    if (m_source == nullptr) {
        throw std::invalid_argument(
            "project/package: 源上下文未绑定（仅导入实例不可导出）");
    }
    if (m_source->closed()) {
        result.error = StoreError(StoreErrorCode::ContextClosed,
                                  "project/package: 源存储上下文已关闭");
        return result;
    }
    if (options.targetFile.empty()) {
        throw std::invalid_argument("project/package: targetFile 为空");
    }
    if (options.targetFile.extension() != ".rwpack") {
        throw std::invalid_argument(
            "project/package: targetFile 扩展名非 .rwpack（§5.8 形态契约）");
    }
    if (options.targetFile.parent_path().empty()
        || !std::filesystem::exists(options.targetFile.parent_path())) {
        throw std::invalid_argument(
            "project/package: 目标父目录不存在（编解码执行的前提）");
    }

    // ---- 快照捕获（D-17——与另存为同源机制：HEAD 锚定闭包清单＋勾选
    // 树清单；§8.9 引用完整性过滤同源。失败＝源存储损坏，零磁盘副作用）。
    clone::CaptureResult capture;
    try {
        capture = clone::captureSnapshot(m_source->query(), m_source->canonicalPath(),
                                         options.includeResults, options.includeReports,
                                         options.includeDrafts, m_sink);
    } catch (const StoreError& e) {
        result.error = e;
        return result;
    }
    result.fileCount = capture.snapshot.files.size();

    // ---- 编解码导出（§8.10"后台进度可取消"——进度/取消逐文件透传；
    // 快照视图为 D-17 取数通道。失败/取消的清理语义在编解码实现侧：
    // 取消即清理临时输出、目标不变；本服务零写源项目——结构性）。
    clone::SnapshotView view(capture.snapshot, m_source->canonicalPath());
    const PackageCodecOutcome outcome = m_codec->exportPack(
        view, options.targetFile, options.onCancel, options.onProgress);
    if (outcome.canceled) {
        // 取消＝正常路径非错误（PM-05/io.md §7.4 同口径）——error 必空、
        // 诊断必空（实现约定：取消诊断不透传）。
        result.canceled = true;
        return result;
    }
    if (!outcome.ok) {
        // 编解码侧失败——io 域码透传不解释（CR-08 不私造 PRJ 码；不
        // 伪造 StoreError 映射——SolidifyServiceImpl"不借用 §6.3 拒绝
        // 词表表达 io 环境事实"先例同款）。
        result.diagnostics = outcome.diagnostics;
        return result;
    }
    result.ok = true;
    return result;
}

PackageImportResult PackageServiceImpl::importPackage(
    const std::filesystem::path& packFile,
    const std::filesystem::path& targetDir,
    const ProgressCallback& onProgress,
    const CancelCallback& onCancel)
{
    // ---- 服务级操作互斥（文件头线程模型）。
    std::lock_guard<std::mutex> opGuard(m_opMutex);

    PackageImportResult result;

    // ---- 前置校验（调用方错误 fail-fast）：包文件存在性/形态与目标
    // 目录契约（PM-05/导入威胁矩阵"不覆盖既有目录"——entry 期即拒绝，
    // 发布期二次收口）。
    if (packFile.empty() || !std::filesystem::is_regular_file(packFile)) {
        throw std::invalid_argument(
            "project/package: packFile 为空/不存在/非普通文件");
    }
    if (targetDir.empty()) {
        throw std::invalid_argument("project/package: targetDir 为空");
    }
    if (std::filesystem::exists(std::filesystem::path(targetDir))) {
        throw std::invalid_argument(
            "project/package: 目标目录已存在（导入不覆盖——PM-05 口径）");
    }
    if (targetDir.parent_path().empty()
        || !std::filesystem::exists(targetDir.parent_path())) {
        throw std::invalid_argument(
            "project/package: 目标父目录不存在（同卷临时位的落点前提）");
    }

    // ---- 建同卷临时位（io.md §7.3 步②命名同形：".rwpack-import-<8hex>/"；
    // 与发布目标同父目录＝同卷——rename 发布的机制前提；点前缀隐藏位，
    // 实现口径⑤）。
    const std::filesystem::path staging
        = clone::makeTempSibling(targetDir, "rwpack-import");
    {
        const win32::FileResult created = m_ops->createDirectories(staging.wstring());
        if (!created.ok) {
            result.error = clone::osToStoreError(created.osError,
                                                 "package-import-staging-create",
                                                 staging.string());
            return result;
        }
    }

    // 清理护栏（PM-05"失败或取消不留目标目录、清理临时区"）：成功发布
    // 后解除（临时位已改名）；任何失败/取消路径整树删除（清理责任单
    // 一归本服务——实现口径登记，见 IPackageCodec::unpackInto 注释）。
    struct StagingGuard {
        win32::IFileOps* ops;
        const std::filesystem::path* dir;
        IDiagnosticsSink* sink;
        bool disarmed = false;
        ~StagingGuard()
        {
            if (!disarmed) {
                clone::removeTreeBestEffort(*dir, *ops, sink, "project/package");
            }
        }
    } guard{m_ops, &staging, m_sink, false};

    try {
        // ---- 编解码解包＋全量校验（NFR-SEC-01/02——预算/路径穿越/
        // 逐字节哈希还原由实现承担；失败/取消不留目标目录的结构前提
        // ＝发布只发生在全量校验之后）。
        const PackageCodecOutcome unpacked
            = m_codec->unpackInto(packFile, staging, onCancel, onProgress);
        if (unpacked.canceled) {
            // 取消＝正常路径非错误（PM-05）——清理由护栏承担。
            result.canceled = true;
            return result;
        }
        if (!unpacked.ok) {
            // 编解码侧失败——io 域码透传（同 exportPackage 口径）。
            result.diagnostics = unpacked.diagnostics;
            return result;
        }

        // ---- 打开协议全量校验（§8.10"于临时目录执行打开协议②③⑤"；
        // 两层版本各自判定：包版本已由编解码判定，项目 schemaVersion
        // 在此独立判定——FormatLegacy/SchemaFuture 稳定拒绝原样透传，
        // io.md §7.9 同口径。ReadOnly 不取锁；校验打开随即释放）。
        {
            OpenStoreRequest verifyRequest;
            verifyRequest.path = staging;
            verifyRequest.mode = OpenMode::ReadOnly;
            verifyRequest.eventBus = m_eventBus;
            verifyRequest.diagnostics = m_sink;
            OpenStoreResult verify = ProjectStoreFactory::open(verifyRequest);
            result.diagnostics.insert(result.diagnostics.end(),
                                      verify.recovery.diagnostics.begin(),
                                      verify.recovery.diagnostics.end());
            (void)verify.store->requestClose();  // 校验完成即释放（同步收尾）
        }

        // ---- 整体发布（rename——同卷原子；目标占用二次收口＝TOCTOU
        // 竞态的机制终点，io.md §7.4"发布时二次校验〔project〕"承载；
        // 失败按实现口径⑥映射，护栏清理临时位）。
        const win32::FileResult published
            = clone::publishDirectory(staging, targetDir, *m_ops, "package-import-publish");
        if (!published.ok) {
            result.error = clone::osToStoreError(published.osError,
                                                 "package-import-publish（目标占用/写路径失败）",
                                                 targetDir.string());
            return result;
        }
        guard.disarmed = true;

        // ---- 按打开协议进入目标目录（发布后打开失败的特例同
        // SaveAsResult：目标已是完整项目，不删除——错误如实返回）。
        OpenStoreRequest entryRequest;
        entryRequest.path = targetDir;
        entryRequest.mode = OpenMode::Writable;
        entryRequest.eventBus = m_eventBus;
        entryRequest.diagnostics = m_sink;
        OpenStoreResult entry = ProjectStoreFactory::open(entryRequest);
        result.diagnostics.insert(result.diagnostics.end(),
                                  entry.recovery.diagnostics.begin(),
                                  entry.recovery.diagnostics.end());
        if (entry.store == nullptr) {
            // 防御性收口（open 失败以异常表达——OpenStoreResult 契约；
            // 本分支不可达）。
            result.error = StoreError(StoreErrorCode::StoreCorrupt,
                                      "project/package: 发布后打开未产出上下文");
            return result;
        }
        result.ok = true;
        result.importedProjectId = entry.store->projectId();
        result.store = std::move(entry.store);
        return result;
    } catch (const StoreError& e) {
        // 打开协议校验失败（StoreCorrupt/FormatLegacy/SchemaFuture 等
        // ——稳定码语义原样）与发布环境失败——清理已由护栏承担，第一
        // 手原因如实入结果轨。
        result.error = e;
        return result;
    }
}

// =====================================================================
// 工厂（公共头 createPackageService 声明的定义点）
// =====================================================================

std::unique_ptr<IPackageService> createPackageService(
    ProjectStore* source,
    IPackageCodec* codec,
    core::IDomainEventBus* eventBus,
    IDiagnosticsSink* diagnostics)
{
    // 实现类型不出公共头（R-2）——装配方只面向 IPackageService 抽象；
    // codec 为空在构造期 fail-fast（装配违约不过夜）；fileOps 形参不
    // 在公共工厂面（生产默认真实实现，故障注入经实现类型构造——测试
    // 消费 src/ 私有头的既定形态）。
    return std::make_unique<PackageServiceImpl>(source, codec, nullptr,
                                                eventBus, diagnostics);
}

}  // namespace sdurws::ird::project
