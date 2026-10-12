/**
 * @file   SaveAsServiceImpl.cpp
 * @brief  另存为服务实现——§8.8 协议八步的编排落地与结果装配（PRJ-T18）。
 *
 * 设计依据：见 SaveAsServiceImpl.hpp 文件头与 PackageService.hpp 公共
 *   契约（执行序、错误轨口径、结果字段纪律）；共享原语见 StoreClone
 *   （快照/复制/改写/发布，实现口径①～⑦登记处）。
 *
 * 线程模型：见 SaveAsServiceImpl.hpp 文件头（m_opMutex 服务级互斥）。
 */

#include "SaveAsServiceImpl.hpp"

#include <sdurws/ird/project/ProjectStore.hpp>

#include <stdexcept>
#include <utility>

namespace sdurws::ird::project {

// =====================================================================
// SaveAsResult 特殊成员函数（out-of-line——unique_ptr<ProjectStore> 的
// 删除器实例化需要完整类型，OpenStoreResult 先例同款；本文件 include
// 了 ProjectStore.hpp，此处即完整类型可见处）。
// =====================================================================

SaveAsResult::~SaveAsResult() = default;
SaveAsResult::SaveAsResult() = default;
SaveAsResult::SaveAsResult(SaveAsResult&& other) = default;
SaveAsResult& SaveAsResult::operator=(SaveAsResult&& other) = default;

// =====================================================================
// 服务实体
// =====================================================================

SaveAsServiceImpl::SaveAsServiceImpl(ProjectStore& source,
                                     win32::IFileOps* fileOps,
                                     core::IDomainEventBus* eventBus,
                                     IDiagnosticsSink* diagnostics)
    : m_source(source)
    , m_ops(fileOps != nullptr ? fileOps : clone::defaultFileOps())
    , m_eventBus(eventBus)
    , m_sink(diagnostics)
{
}

SaveAsResult SaveAsServiceImpl::saveAs(const SaveAsOptions& options)
{
    // ---- 服务级操作互斥（文件头线程模型）：快照捕获的读轨一致性语义
    // 收敛点（并发 saveAs 串行；与源上下文的命令提交并存由 D-17 快照
    // 自洽吸收）。
    std::lock_guard<std::mutex> opGuard(m_opMutex);

    SaveAsResult result;

    // ---- 第 1 步：前置校验。目标目录契约（§8.8"目标目录"的用户意图
    // ＋PM-05"不覆盖既有目录"口径）：空路径/已存在/父目录缺失＝调用方
    // 错误 fail-fast（createNew 先例同款——向导应先行校验的场景）。
    if (options.targetDir.empty()) {
        throw std::invalid_argument("project/save-as: targetDir 为空");
    }
    if (std::filesystem::exists(std::filesystem::path(options.targetDir))) {
        throw std::invalid_argument(
            "project/save-as: 目标目录已存在（另存为不覆盖——PM-05 口径）");
    }
    if (options.targetDir.parent_path().empty()
        || !std::filesystem::exists(options.targetDir.parent_path())) {
        throw std::invalid_argument(
            "project/save-as: 目标父目录不存在（同卷临时位的落点前提）");
    }
    // 源上下文状态（服务消费②端口取数——Closed 即不可用；只读上下文
    // 照常可另存为——读轨操作，权限语义见公共头）。
    if (m_source.closed()) {
        result.error = StoreError(StoreErrorCode::ContextClosed,
                                  "project/save-as: 源存储上下文已关闭");
        return result;
    }

    // ---- 第 2 步：快照捕获（D-17——HEAD 锚定闭包清单＋勾选树清单；
    // §8.9 引用完整性过滤在清单装配期完成）。失败＝源存储损坏——
    // 发生在建临时位之前，零磁盘副作用，直接入结果轨。
    clone::CaptureResult capture;
    try {
        capture = clone::captureSnapshot(m_source.query(), m_source.canonicalPath(),
                                         options.copyResults, options.copyReports,
                                         options.copyDrafts, m_sink);
    } catch (const StoreError& e) {
        result.error = e;
        return result;
    }
    result.skippedReports = std::move(capture.skippedReports);

    // ---- 第 3 步：建同卷临时位（§8.8"同卷临时位"——目标父目录旁，
    // rename 发布的机制前提；点前缀隐藏位——实现口径⑤）。
    const std::filesystem::path staging
        = clone::makeTempSibling(options.targetDir, "saveas");
    {
        const win32::FileResult created = m_ops->createDirectories(staging.wstring());
        if (!created.ok) {
            result.error = clone::osToStoreError(created.osError,
                                                 "saveas-staging-create",
                                                 staging.string());
            return result;
        }
    }

    // 清理护栏（PM-05"取消/失败清理临时位，不留半成品"）：成功发布后
    // 解除（临时位已改名消失）；任何失败/异常路径整树删除（删除失败
    // 仅开发诊断——不掩盖第一手失败原因）。
    struct StagingGuard {
        win32::IFileOps* ops;
        const std::filesystem::path* dir;
        IDiagnosticsSink* sink;
        bool disarmed = false;
        ~StagingGuard()
        {
            if (!disarmed) {
                clone::removeTreeBestEffort(*dir, *ops, sink, "project/save-as");
            }
        }
    } guard{m_ops, &staging, m_sink, false};

    try {
        // ---- 第 4 步：完整复制（§8.8"完整复制"——write-through 闸门，
        // 清单为唯一范围；草稿缺失跳过、不可变文件失真即失败的分流在
        // copySnapshotFiles 内）。
        win32::AtomicFile atomic(m_ops);
        clone::copySnapshotFiles(m_source.canonicalPath(), capture.snapshot,
                                 staging, atomic, *m_ops, m_sink);

        // ---- 第 5 步：闭包校验（§8.8"校验闭包"——对副本执行 PM-02
        // 打开协议②③⑤全量：目录形态/版本→闭包完整性〔对象/清单哈希〕
        // →恢复诊断。ReadOnly 不取锁；校验即真实的打开——失败＝源存储
        // 损坏或复制失真，清理后如实返回）。校验打开的上下文随即释放
        // （其唯一职责是完整性裁决）。
        {
            OpenStoreRequest verifyRequest;
            verifyRequest.path = staging;
            verifyRequest.mode = OpenMode::ReadOnly;
            verifyRequest.eventBus = m_eventBus;
            verifyRequest.diagnostics = m_sink;
            OpenStoreResult verify = ProjectStoreFactory::open(verifyRequest);
            (void)verify.store->requestClose();  // 校验完成即释放（同步收尾）
        }

        // ---- 第 6 步：换新 projectId（core 生成——"core 分配"§8.8
        // 原文；对象/修订/分支 ID 全部原样保留）＋三处改写（D-16 冻结
        // 改写面：project.json/HEAD/drafts 的 projectId；元数据对象零
        // 触碰——newDisplayName 的落位途径见公共头背景说明）。
        const core::ProjectId newId = core::ProjectId::generate();
        clone::rewriteProjectIdForCopy(capture.snapshot, newId, staging,
                                       atomic, m_sink);

        // ---- 第 7 步：发布（§7.2 行 2 只增发布原语的目录形态——同卷
        // rename 原子可见；目标占用的竞态按 WriteRejected 如实映射，
        // 实现口径⑥）。成功后临时位已消失——解除清理护栏。
        const win32::FileResult published
            = clone::publishDirectory(staging, options.targetDir, *m_ops, "saveas-publish");
        if (!published.ok) {
            result.error = clone::osToStoreError(published.osError,
                                                 "saveas-publish（目标占用/写路径失败）",
                                                 options.targetDir.string());
            return result;
        }
        guard.disarmed = true;

        // ---- 第 8 步：按 PM-02 打开协议进入新目录（D-16；Writable
        // 请求——被他方持锁时按 PM-07 降级只读，以结果 store 的
        // writable 位为准）。发布后打开失败的特例：目标已是完整项目
        // （发布即交付）——不删除目标，错误如实返回（SaveAsResult
        // 注释的实现口径）。
        OpenStoreRequest entryRequest;
        entryRequest.path = options.targetDir;
        entryRequest.mode = OpenMode::Writable;
        entryRequest.eventBus = m_eventBus;
        entryRequest.diagnostics = m_sink;
        OpenStoreResult entry = ProjectStoreFactory::open(entryRequest);
        result.diagnostics = entry.recovery.diagnostics;
        if (entry.store == nullptr) {
            // 防御性：open 失败以异常表达（OpenStoreResult 注释——能拿
            // 到结构则 store 必非空）；此处按失败构造收口（不可达）。
            result.error = StoreError(StoreErrorCode::StoreCorrupt,
                                      "project/save-as: 发布后打开未产出上下文");
            return result;
        }
        result.ok = true;
        result.newProjectId = entry.store->projectId();
        result.store = std::move(entry.store);
        return result;
    } catch (const StoreError& e) {
        // 复制/改写/校验路径的环境与数据侧错误——清理已由护栏承担，
        // 第一手原因如实入结果轨。
        result.error = e;
        return result;
    }
}

// =====================================================================
// 工厂（公共头 createSaveAsService 声明的定义点）
// =====================================================================

std::unique_ptr<ISaveAsService> createSaveAsService(
    ProjectStore& source,
    core::IDomainEventBus* eventBus,
    IDiagnosticsSink* diagnostics)
{
    // 实现类型不出公共头（R-2）——装配方只面向 ISaveAsService 抽象；
    // fileOps 形参不在公共工厂面（生产默认真实实现，故障注入经实现
    // 类型构造——测试消费 src/ 私有头的既定形态）。
    return std::make_unique<SaveAsServiceImpl>(source, nullptr, eventBus, diagnostics);
}

}  // namespace sdurws::ird::project
