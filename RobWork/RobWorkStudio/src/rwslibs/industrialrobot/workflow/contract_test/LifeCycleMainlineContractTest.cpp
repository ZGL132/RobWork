/**
 * @file   LifeCycleMainlineContractTest.cpp
 * @brief  项目生命周期全主线回归的契约测试（WF-VER-227——units/workflow.md
 *         §11.2"227 生命周期全主线回归"＋DTB §2.23 WP-22-T13 验收原文
 *         "新建→编辑→应用→撤销/重做→包导出导入→另存为主线用例通过并留
 *         痕（AT-20/29）"的执行面）。
 *
 * 设计依据：
 *   - units/workflow.md §7.1（新建三步向导——PM-01）、§7.4（另存/包——
 *     PM-05）、§8.3（命令集——产生修订的命令经①端口、会话命令零修订）、
 *     §11.2（227：全链通过并留痕——观测点"全部"；226 撤销/重做主线半区
 *     已由 CommandsContractTest 承载，本用例承载全链串接）、§11.3（AT-20
 *     项目生命周期＝新建/编辑/应用/包/另存主线；AT-29 撤销/重做＝226/227）
 *   - REQUIREMENTS.md §17 PM-01/PM-05/PM-18/AT-20/AT-29 原文；KIN-06
 *     （会话命令零修订——编辑步 draft.save 的写轨边界）
 *   - ARCHITECTURE.md §3.4（workflow 编排定位——全部写路径经 project）、
 *     §7.11（SA-16 命令统一注册入口）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"；各分段桥为既有契约
 * 文件同款形态的单文件自持复刻——桩不跨文件共享的既有惯例）：
 *   - **真实 project store 全程**：createNew→草稿落盘→①端口提交→
 *     UndoRedoService 撤销/重做→io 真实包导出/导入→另存复制＋打开协议
 *     进入——修订链/草稿/盘面全部真实落盘可复核；
 *   - **真实 ui CommandRegistry**：draft.apply/project.undo/project.redo/
 *     draft.save 四命令经统一注册入口 submit 驱动（SA-16——三处入口同一
 *     路径）；贡献清单经 registerWorkflowCommands 真实注册；
 *   - **领域命令处理器为测试桩**（BaselineSeedHandler/wf-e2e-apply 对——
 *     模拟 L5 装配层注册的建模处理器，R-1 不链 modeling）：修订链机制
 *     真值全在 project 生产代码（NewProjectWizardContract/CommandsContract
 *     同款先例口径）；applyDraft 桥＝"草稿在盘检查→①端口提交"的 L5 域
 *     信封组装测试等价物（诚实登记——WP-24-T03b 真实组装链路落位后替换）；
 *   - **另存/包端口桥＝L5 装配桥测试等价物**（真实 fs::copy 复制＋真实 io
 *     导出器/导入器联合——SaveAsPackageContractTest 同款形态；导入发布半区
 *     以同卷 rename 桩模拟并显式声明，WP-04-T18 落位后替换）。
 *
 * 全链修订链黄金序列（观测点"修订"）：r0 骨架(1)→模板基线(2)→应用(3)
 * →撤销(4)→重做(5)——每步恰好一个新修订（PA-2 只增不改，最早修订全程
 * 可读）；编辑步（draft.save 会话写轨）链长不变（KIN-06 同源边界）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Digest.hpp>   // core::ContentDigester（导入桥摘要复算面——io 桥内部）
#include <sdurws/ird/io/Budget.hpp>     // io::BudgetSpec::packImportHardened
#include <sdurws/ird/io/IoDiagnostics.hpp>  // io::errorCodeToken
#include <sdurws/ird/io/IoError.hpp>    // io::IoError/IoErrorCode
#include <sdurws/ird/io/IoFwd.hpp>      // io::IoCancelToken/IoProgress
#include <sdurws/ird/io/Package.hpp>    // io 包设施（真实导出器/导入器/快照源）
#include <sdurws/ird/project/CommandService.hpp>  // HandlerRegistry/CommandEnvelope（①端口）
#include <sdurws/ird/project/DraftService.hpp>    // DraftService（编辑草稿真实落盘）
#include <sdurws/ird/project/PersistenceFormat.hpp>  // DraftDocument/DraftOrigin
#include <sdurws/ird/project/ProjectStore.hpp>    // ProjectStoreFactory
#include <sdurws/ird/project/QueryPort.hpp>       // branchTips/branchHistory/tryRevision
#include <sdurws/ird/project/UndoRedo.hpp>        // UndoRedoService（真实撤销/重做）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO
#include <sdurws/ird/ui/ICommandRegistry.hpp>     // ui::createCommandRegistry（真实设施）
#include <sdurws/ird/workflow/Commands.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>
#include <sdurws/ird/workflow/Types.hpp>
#include "plugin/WorkflowCommandCatalog.hpp"      // registerWorkflowCommands（贡献面）

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
namespace pd = sdurws::ird::project;
using workflow::DomainInitRequest;
using workflow::DomainInitResult;
using workflow::InitialSourceKind;
using workflow::InstallPreset;
using workflow::NewProjectInputs;
using workflow::IFlowCancelToken;
using workflow::RevisionOutcome;
using workflow::SessionActionResult;
using workflow::TemplateKind;
using workflow::WorkflowCommandPorts;
using pd::CommandEnvelope;
using pd::CommandResult;
using pd::ICommandHandler;
using pd::PrepareOutcome;
using pd::ProjectStoreFactory;

// =====================================================================
// 测试桩：领域命令处理器（模拟 L5 装配层注册的建模处理器——R-1 不链
// modeling；修订链机制真值全在 project 生产代码）
// =====================================================================

/**
 * @brief 新建向导的领域基线种子处理器（NewProjectWizardContract 同款——
 *        模板来源的"模板基线修订"实体面；载荷不透明、project 不解释）。
 */
class BaselineSeedHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "wf-e2e-baseline-seed";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(pd::HandlerContext& ctx, const CommandEnvelope&,
                           const pd::RevisionView&, pd::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>&) override
    {
        pd::ObjectWrite write;
        write.objectId = ctx.objectId();  // project 分配对象身份（PA-1）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'b', 'a', 's', 'e'};
        out.objectWrites.push_back(std::move(write));
        out.summary = "wf 全主线契约：模板基线种子修订（模拟建模模板基线）";
        return PrepareOutcome::Planned;
    }
};

/**
 * @brief "应用修改"的领域命令对（可逆写回形态——CommandsContract
 *        UndoSeedHandler 同款：正向写 seedId='A'＋逆命令声明；逆命令写回
 *        seedId='B'＋对称声明）。撤销/重做主线（AT-29）依赖这对声明：
 *        撤销提交逆命令、重做重放原始载荷——各产生恰好一个新修订。
 */
class ApplySeedHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override { return "wf-e2e-apply"; }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(pd::HandlerContext&, const CommandEnvelope&,
                           const pd::RevisionView&, pd::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>&) override
    {
        pd::ObjectWrite write;
        write.objectId = seedId;  // fixture 固定身份（正逆两半同对象写回）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'A'};
        out.objectWrites.push_back(std::move(write));
        out.inverseCommandType = std::string{"wf-e2e-apply-revert"};  // §6.9 声明面
        out.inversePayloadCanonical = std::vector<std::uint8_t>{};
        out.summary = "wf 全主线契约：应用修改（草稿落为修订——领域载荷测试桩）";
        return PrepareOutcome::Planned;
    }

    core::ObjectId seedId{};  ///< 正逆两半共写的对象身份（fixture 注入）
};

/// "应用"的逆命令（撤销提交——写回 'B'；对称声明 inverse＝正命令）。
class ApplySeedInverseHandler final : public ICommandHandler {
public:
    [[nodiscard]] std::string commandType() const override
    {
        return "wf-e2e-apply-revert";
    }
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    PrepareOutcome prepare(pd::HandlerContext&, const CommandEnvelope&,
                           const pd::RevisionView&, pd::CommandPlan& out,
                           std::vector<core::DiagnosticRecord>&) override
    {
        pd::ObjectWrite write;
        write.objectId = seedId;
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'B'};
        out.objectWrites.push_back(std::move(write));
        // 对称声明面（§6.9）：撤销修订携带的 inverse 记录＝正命令原始表达
        // ——会话 redo 栈据此重放（原始载荷与提交信封逐字一致）。
        out.inverseCommandType = std::string{"wf-e2e-apply"};
        out.inversePayloadCanonical = std::vector<std::uint8_t>{'a', 'p', 'p', 'l', 'y'};
        out.summary = "wf 全主线契约：应用逆命令（撤销半区）";
        return PrepareOutcome::Planned;
    }

    core::ObjectId seedId{};  ///< 与正向同写的对象身份
};

// =====================================================================
// 测试桩：领域初始化提交器（IDomainInitSubmitter——装配层替身，
// NewProjectWizardContract 同款：录制请求＋①端口真实提交基线修订）
// =====================================================================

class RecordingSubmitter final : public workflow::IDomainInitSubmitter {
public:
    int calls = 0;  ///< 调用计数（段 1 前置：恰好一次）

    DomainInitResult submitInitialization(pd::ProjectStore& store,
                                          const DomainInitRequest&) override
    {
        ++calls;
        // 真实提交：注册基线种子处理器（装配期通道——判重防同 store 二次
        // 注册被拒）后经①端口提交（七步事务）。
        {
            pd::HandlerRegistry& registry = store.handlerRegistry();
            const auto tokens = registry.registeredCommandTypes();
            const bool already = std::find(tokens.begin(), tokens.end(),
                                           std::string{"wf-e2e-baseline-seed"})
                != tokens.end();
            if (!already) {
                registry.registerHandler(std::make_unique<BaselineSeedHandler>());
            }
        }
        DomainInitResult ok;
        CommandEnvelope envelope;
        envelope.branch = store.query().branchTips().at(0).id;
        envelope.commandType = "wf-e2e-baseline-seed";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'b', 'a', 's', 'e'};
        const CommandResult result = store.commands().submit(envelope);
        ok.committed = result.committed();
        ok.baselineRevision = result.newRevision;
        ok.diagnostics = result.diagnostics;
        return ok;
    }
};

// =====================================================================
// 端口桩：会话动作（draft.save 触达观测）与修订命令（apply/undo/redo）
// =====================================================================

/// 会话动作桩（draft.save→saveAllDrafts——触达计数；落盘本体由夹具经
/// DraftService 真实完成，桥语义＝"已落盘事实的命令面触达"）。
class SessionStub final : public workflow::IWorkflowSessionPort {
public:
    int saveCalls = 0;  ///< saveAllDrafts 调用计数（编辑步观测点）

    SessionActionResult switchSchemeBranch() override { return {true, ""}; }
    SessionActionResult cycleDisplayMode() override { return {true, ""}; }
    SessionActionResult resetJointsToHome() override { return {true, ""}; }
    SessionActionResult resetJointsToZero() override { return {true, ""}; }
    SessionActionResult saveAllDrafts() override
    {
        ++saveCalls;
        return {true, ""};
    }
};

/**
 * @brief 修订命令端口真实桥（L5 装配桥的测试等价物——CommandsContract
 *        StoreRevisionBridgeV2 同款＋applyDraft 的"草稿在盘"半区）：
 *          - applyDraft＝检查草稿在盘（DraftService tryLoad——"应用修改
 *            以草稿为输入"的测试等价物）→①端口提交 wf-e2e-apply（域信封
 *            组装的桩承载——诚实登记）；
 *          - undo/redo＝前置判定（先 status 后动作——菜单绑定纪律）→
 *            桥接真实 UndoRedoService（逆命令提交/原始载荷重放——各产生
 *            恰好一个新修订）。
 */
class MainlineRevisionBridge final : public workflow::IRevisionCommandPort {
public:
    pd::ProjectStore* store = nullptr;        ///< 目标存储上下文（非 owning）
    pd::BranchId branch{};                    ///< 活动分支（草稿查询键——fixture 注入）
    std::string moduleId = "modeling";        ///< 编辑步草稿模块（fixture 注入）
    core::ObjectId applySeedId{};             ///< 应用命令对共写对象（fixture 注入）
    int applyCalls = 0;                       ///< 触达计数
    int undoCalls = 0;                        ///< 撤销触达计数
    int redoCalls = 0;                        ///< 重做触达计数

    /// 应用修改：草稿在盘检查→①端口提交（链 +1）。
    RevisionOutcome applyDraft() override
    {
        ++applyCalls;
        std::vector<core::DiagnosticRecord> diags;
        if (!store->drafts().tryLoad(branch, moduleId, diags).has_value()) {
            // 无草稿可应用＝调用方前置不满足（诚实不伪造提交——PM-04）。
            RevisionOutcome out;
            out.committed = false;
            out.reasonKey = "cmd.workflow.apply.no-draft";
            return out;
        }
        CommandEnvelope envelope;
        envelope.branch = branch;
        envelope.commandType = "wf-e2e-apply";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'a', 'p', 'p', 'l', 'y'};
        return fold(store->commands().submit(envelope));
    }

    /// 撤销：前置判定→UndoRedoService.undo（逆命令提交——新修订）。
    RevisionOutcome undo() override
    {
        ++undoCalls;
        if (!store->undoRedo().status(mainBranch()).canUndo) {
            RevisionOutcome out;
            out.committed = false;
            out.reasonKey = "cmd.workflow.undo.nothing";
            return out;
        }
        return fold(store->undoRedo().undo(mainBranch()));
    }

    /// 重做：前置判定→UndoRedoService.redo（原始载荷重放——新修订）。
    RevisionOutcome redo() override
    {
        ++redoCalls;
        if (!store->undoRedo().status(mainBranch()).canRedo) {
            RevisionOutcome out;
            out.committed = false;
            out.reasonKey = "cmd.workflow.redo.nothing";
            return out;
        }
        return fold(store->undoRedo().redo(mainBranch()));
    }

private:
    [[nodiscard]] core::BranchId mainBranch() const
    {
        return store->query().currentMetadata().record.primaryBranchId;
    }

    /// 提交结果折叠（CommandResult→RevisionOutcome——对端状态零加工，D-WF-7）。
    static RevisionOutcome fold(const CommandResult& result)
    {
        RevisionOutcome out;
        out.committed = result.committed();
        if (result.committed() && result.newRevision.has_value()) {
            out.newRevisionId = result.newRevision->toCanonical();
        }
        if (!out.committed) {
            out.reasonKey = "cmd.workflow.revision.rejected";
        }
        return out;
    }
};

// =====================================================================
// 端口桥：另存真实复制／包导出真实 io／包导入真实 io＋发布桩
// （SaveAsPackageContractTest 同款形态的单文件自持复刻——L5 装配桥测试
// 等价物；发布半区同卷 rename 桩模拟并显式声明）
// =====================================================================

class MemoryCancelLike final : public workflow::IFlowCancelToken {
public:
    bool cancelRequested() const override { return m_flag; }
    void requestCancel() override { m_flag = true; }

private:
    bool m_flag = false;
};

/// 另存执行端口（真实 fs::copy 完整目录复制按勾选裁剪＋取消检查点清理）。
class MainlineCopySaveAsPort final : public workflow::ISaveAsPort {
public:
    workflow::ISaveAsPort::Execution executeCopy(
        pd::ProjectStore& source, const workflow::SaveAsRequest& request,
        IFlowCancelToken* cancel, const workflow::FlowProgressCallback&) override
    {
        workflow::ISaveAsPort::Execution e;
        const fs::path sourceDir = source.canonicalPath();
        const fs::path targetDir = request.targetDir;
        std::error_code makeDirEc;
        fs::create_directories(targetDir, makeDirEc);
        if (makeDirEc) {
            e.cause = "目标目录创建失败: " + makeDirEc.message();
            return e;
        }
        // 勾选裁剪映射（PM-05）：未勾选的顶层树不进入复制遍历。
        std::map<std::string, bool> topIncluded;
        topIncluded["results"] = request.selection.includeResults;
        topIncluded["reports"] = request.selection.includeReports;
        topIncluded["drafts"] = request.selection.includeDrafts;

        std::error_code ec;
        for (auto it = fs::directory_iterator(sourceDir, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { break; }
            const std::string name = it->path().filename().string();
            if (name == "lock" || it->is_directory(ec)) { continue; }
            fs::copy_file(it->path(), targetDir / name,
                          fs::copy_options::overwrite_existing, ec);
            if (ec) { return failedCopy(targetDir, "复制失败: " + it->path().string()); }
            if (cancel != nullptr && cancel->cancelRequested()) {
                return cancelledCopy(targetDir);
            }
        }
        for (auto it = fs::directory_iterator(sourceDir, ec);
             it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { break; }
            if (!it->is_directory(ec)) { continue; }
            const std::string name = it->path().filename().string();
            const auto known = topIncluded.find(name);
            if (known != topIncluded.end() && !known->second) { continue; }
            fs::copy(it->path(), targetDir / name,
                     fs::copy_options::recursive
                         | fs::copy_options::overwrite_existing, ec);
            if (ec) { return failedCopy(targetDir, "树复制失败: " + it->path().string()); }
            if (cancel != nullptr && cancel->cancelRequested()) {
                return cancelledCopy(targetDir);
            }
        }
        e.copied = true;
        e.targetLeftClean = true;
        return e;
    }

private:
    static workflow::ISaveAsPort::Execution failedCopy(const fs::path& targetDir,
                                                    std::string cause)
    {
        workflow::ISaveAsPort::Execution e;
        e.cause = std::move(cause);
        std::error_code ec;
        fs::remove_all(targetDir, ec);
        e.targetLeftClean = !fs::exists(targetDir);
        return e;
    }
    static workflow::ISaveAsPort::Execution cancelledCopy(const fs::path& targetDir)
    {
        workflow::ISaveAsPort::Execution e;
        e.cancelled = true;
        std::error_code ec;
        fs::remove_all(targetDir, ec);  // 取消即清理（AT-20）
        e.targetLeftClean = !fs::exists(targetDir);
        return e;
    }
};

/// workflow 取消令牌 → io IoCancelToken 适配（L5 桥接面——公共头零 io 类型）。
class IoCancelAdapter final : public io::IoCancelToken {
public:
    explicit IoCancelAdapter(IFlowCancelToken* flow) : m_flow(flow) {}
    bool isCancelled() const override
    {
        return m_flow != nullptr && m_flow->cancelRequested();
    }

private:
    IFlowCancelToken* m_flow;
};

/// 目录一致快照源（io ISnapshotFileSource 的测试等价物——排除 lock 心跳与
/// .staging 组装区；包内名＝payload/ 前缀＋正斜杠相对名——io §7.1 镜像布局）。
class DirectorySnapshotSource final : public io::ISnapshotFileSource {
public:
    explicit DirectorySnapshotSource(fs::path root) : m_root(std::move(root)) {}

    io::IoResult<std::vector<io::PackFileEntry>> enumerate() const override
    {
        io::IoResult<std::vector<io::PackFileEntry>> out;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(m_root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                out.error.code = io::IoErrorCode::ResNotFound;
                out.error.detail = "目录遍历失败";
                return out;
            }
            if (!it->is_regular_file(ec) || ec) { continue; }
            const std::string name = it->path().filename().string();
            if (name == "lock") { continue; }
            std::string rel = it->path().lexically_relative(m_root).u8string();
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (rel.rfind(".staging", 0) == 0 || rel.find("/.staging") != std::string::npos) {
                continue;  // 组装区残留非快照闭包
            }
            io::PackFileEntry entry;
            entry.path = std::string(io::PackFormat::kPayloadPrefix) + rel;
            entry.size = static_cast<std::uint64_t>(it->file_size(ec));
            out.value.push_back(std::move(entry));
        }
        return out;
    }

    io::IoResult<io::IoString> read(const io::IoString& packPath) override
    {
        io::IoResult<io::IoString> out;
        std::string rel = packPath;
        const std::string prefix = io::PackFormat::kPayloadPrefix;
        if (rel.rfind(prefix, 0) == 0) { rel = rel.substr(prefix.size()); }
        std::replace(rel.begin(), rel.end(), '/', '\\');
        const fs::path full = m_root / fs::u8path(rel);
        std::ifstream in(full, std::ios::binary);
        if (!in.is_open()) {
            out.error.code = io::IoErrorCode::ResNotFound;
            out.error.detail = "快照文件读取失败: " + packPath;
            return out;
        }
        out.value.assign(std::istreambuf_iterator<char>(in),
                         std::istreambuf_iterator<char>());
        return out;
    }

private:
    fs::path m_root;
};

/// 包导出端口（真实 io 导出器——六步协议全真）。
class MainlineExportPort final : public workflow::IPackageExportPort {
public:
    workflow::IPackageExportPort::Execution exportPackage(
        pd::ProjectStore& source, const workflow::PackageExportRequest& request,
        IFlowCancelToken* cancel, const workflow::FlowProgressCallback&) override
    {
        workflow::IPackageExportPort::Execution e;
        DirectorySnapshotSource snapshot(source.canonicalPath());
        io::PackageExportOptions options;
        options.targetFile = request.targetFile;
        options.includeResults = request.selection.includeResults;
        options.includeReports = request.selection.includeReports;
        options.includeDrafts = request.selection.includeDrafts;
        options.sourceProjectId = request.sourceProjectId;
        options.headRevisionId = request.headRevisionId;

        IoCancelAdapter cancelAdapter(cancel);
        auto result = io::makePackageExporter()->export_(
            snapshot, options, nullptr,
            (cancel != nullptr) ? &cancelAdapter : nullptr, [](const io::IoProgress&) {});
        if (result) {
            e.exported = true;
            e.entryCount = result.value.entryCount;
            e.totalBytes = result.value.totalBytes;
            e.temporaryAreaCleaned = true;
            return e;
        }
        e.cancelled = (result.error.code == io::IoErrorCode::Cancelled);
        e.temporaryAreaCleaned =
            (result.error.code != io::IoErrorCode::PackCleanupFailed);
        e.cause = std::string(io::errorCodeToken(result.error.code))
            + ": " + result.error.detail;
        return e;
    }
};

/// 包导入端口（真实 io 导入器 begin/verifyThrough/cleanup＋发布半区同卷
/// rename 桩——显式声明：WP-04-T18 落位后由真实发布替换）。
class MainlineImportPort final : public workflow::IPackageImportPort {
public:
    workflow::PackageImportExecution importPackage(
        const workflow::PackageImportRequest& request, IFlowCancelToken* cancel,
        const workflow::FlowProgressCallback&) override
    {
        workflow::PackageImportExecution e;
        io::PackageImportOptions options;
        options.targetDir = request.targetDir;
        options.budget = io::BudgetSpec::packImportHardened();

        IoCancelAdapter cancelAdapter(cancel);
        auto importer = io::makePackageImporter();
        auto session = importer->begin(request.packFile, options,
                                       (cancel != nullptr) ? &cancelAdapter : nullptr,
                                       [](const io::IoProgress&) {});
        if (!session) {
            foldIoFailure(session.error, e);
            return e;
        }
        auto verified = importer->verifyThrough(session.value,
                                                (cancel != nullptr) ? &cancelAdapter : nullptr,
                                                [](const io::IoProgress&) {});
        const io::PackageImportReport& report = session.value.report();
        e.manifestEntries = report.manifestEntries;
        e.verifiedEntries = report.verifiedEntries;
        e.totalBytes = report.totalBytes;
        if (!verified) {
            e.verified = false;
            e.cancelled = e.cancelled
                || (verified.error.code == io::IoErrorCode::Cancelled);
            e.cancelled = e.cancelled
                || (session.value.state() == io::PackageImportState::Canceled);
            auto cleaned = importer->cleanup(session.value);
            e.targetLeftClean = static_cast<bool>(cleaned);
            if (e.cancelled) {
                e.diagnostics.clear();  // UX-03：取消不落诊断
            } else {
                e.cause = std::string(io::errorCodeToken(verified.error.code))
                    + ": " + verified.error.detail;
            }
            return e;
        }
        e.verified = true;
        // 发布半区（project ⑧步桩模拟——同卷 rename payloadRoot→targetDir）。
        std::error_code ec;
        fs::rename(session.value.payloadRoot(), request.targetDir, ec);
        auto cleaned = importer->cleanup(session.value);
        e.targetLeftClean = static_cast<bool>(cleaned);
        if (ec) {
            e.published = false;
            e.cause = "发布（rename）失败: " + ec.message();
            return e;
        }
        e.published = true;
        return e;
    }

private:
    static void foldIoFailure(const io::IoError& error, workflow::PackageImportExecution& e)
    {
        e.verified = false;
        e.published = false;
        e.cancelled = (error.code == io::IoErrorCode::Cancelled);
        e.targetLeftClean = true;  // begin 失败＝临时区未建立
        workflow::PackageImportDiagnosticLine line;
        line.code = std::string(io::errorCodeToken(error.code));
        line.message = error.detail;
        e.diagnostics.push_back(std::move(line));
        e.cause = line.code + ": " + error.detail;
    }
};

// =====================================================================
// 夹具：临时目录＋命令注册表装配（真实 registry＋贡献清单真实注册）
// =====================================================================

class LifeCycleMainlineContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_mainline_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    /// 编辑步：在真实存储上下文的活动分支上落盘一份草稿（编辑器会话产物
    /// 的测试等价物——PM-04 草稿写轨；基线＝当前分支 tip）。
    static fs::path saveDraftViaService(pd::ProjectStore& store,
                                        const std::string& moduleId)
    {
        const auto tips = store.query().branchTips();
        if (tips.empty()) {
            ADD_FAILURE() << "夹具前置失败：分支清单为空";
            return {};
        }
        pd::DraftDocument doc;
        doc.projectId = store.projectId();
        doc.branchId = tips[0].id;
        doc.moduleId = moduleId;
        doc.baseRevisionId = tips[0].tip;
        doc.payload = "{\"note\":\"lifecycle-mainline-contract\"}";
        doc.savedAtUtc = "2026-10-10T00:00:00Z";
        doc.origin = pd::DraftOrigin::Manual;
        const pd::SaveResult saved = store.drafts().save(doc);
        if (!saved.ok) {
            ADD_FAILURE() << "草稿落盘失败（夹具前置）: "
                          << (saved.error ? saved.error->what() : "?");
            return {};
        }
        return saved.file;
    }

    /// 修订链长度（branchHistory——PA-2 观测点；上限给足）。
    static std::size_t historySize(pd::ProjectStore& store)
    {
        return store.query().branchHistory(mainBranchOf(store), 1024).size();
    }

    static core::BranchId mainBranchOf(pd::ProjectStore& store)
    {
        return store.query().currentMetadata().record.primaryBranchId;
    }

    static fs::path s_base;
    fs::path m_dir;
};

fs::path LifeCycleMainlineContract::s_base;

// 前向声明（目录树快照辅助——定义在文件尾私有段；用例先消费）。
std::map<std::string, std::uintmax_t> snapshotTreeOf(const fs::path& root);

// =====================================================================
// WF-VER-227：新建→编辑→应用→撤销/重做→包导出导入→另存（AT-20/29）
// =====================================================================

/**
 * 全主线回归（单用例串接六段——227 场景原文"全链通过并留痕"；各段的
 * 分支/失败语义由既有契约文件独立钉扎，本用例承载全链串接与修订链
 * 黄金序列）：
 *   段 1 新建（PM-01/AT-20）：三步向导确认→r0＋模板基线＝链 2；
 *   段 2 编辑（草稿写轨）：真实草稿落盘＋draft.save 命令触达→链不变（会话
 *       命令零修订——KIN-06 同源边界）；
 *   段 3 应用（draft.apply→①端口）：草稿落为修订→链 3；
 *   段 4 撤销/重做（PM-18/AT-29）：撤销＝逆命令新修订→链 4；重做＝原始
 *       载荷重放新修订→链 5；最早修订（r0）全程可读（历史只增不改）；
 *   段 5 包导出（PM-05）：真实 io 导出器→.rwpack 产物＋源目录树零变化
 *       （导出只读）；
 *   段 6 包导入（PM-05/NFR-SEC-01/02）：真实 io 校验全绿＋目标就位＋真实
 *       open 成功（projectId 同源——解包逐字节还原）；
 *   段 7 另存（PM-05/AT-20）：真实复制→换新 projectId→按打开协议进入
 *       （Entered＋store 可写）；
 *   终局：全链观测点汇总复核（修订链黄金值 5＋草稿面＋包产物＋另存身份）。
 */
TEST_F(LifeCycleMainlineContract, WF_VER_227_FullMainlineNewEditApplyUndoRedoPackageSaveAs)
{
    IRD_TEST_INFO((std::vector<std::string>{"PM-01", "PM-05", "PM-18", "KIN-06"}),
                  (std::vector<std::string>{"AT-20", "AT-29", "WF-VER-227"}));

    // ---- 装配：可逆应用命令对（对象身份一次生成两半共享）＋真实注册表。
    ApplySeedHandler applyHandler;
    ApplySeedInverseHandler applyInverseHandler;
    const core::ObjectId applySeedId = core::ObjectId::generate();
    applyHandler.seedId = applySeedId;
    applyInverseHandler.seedId = applySeedId;

    SessionStub session;
    MainlineRevisionBridge revisionBridge;

    ui::CommandRegistryDeps deps;
    deps.ownerWhitelist = {"ui", "workflow"};
    std::unique_ptr<ui::ICommandRegistry> registry = ui::createCommandRegistry(std::move(deps));
    // 端口集（会话＋修订两端口——本主线消费面；注册期不校验空端口）。
    WorkflowCommandPorts ports;
    ports.session = &session;
    ports.revisions = &revisionBridge;
    const std::vector<ui::RegistrationResult> registration =
        workflow::registerWorkflowCommands(*registry, ports);
    for (std::size_t i = 0; i < registration.size(); ++i) {
        ASSERT_EQ(registration[i], ui::RegistrationResult::Ok)
            << "贡献命令注册被拒（第 " << i << " 条）";
    }

    // ================= 段 1：新建（PM-01——三步向导确认） =================
    RecordingSubmitter submitter;
    NewProjectInputs inputs;
    inputs.displayName = "全主线契约项目";
    inputs.directory = s_base / "mainline" / "proj.rwdesign";
    inputs.source = InitialSourceKind::Template;
    inputs.templateKind = TemplateKind::SixAxis;
    inputs.installPreset = InstallPreset::Ground;
    inputs.templateLocalName = "robot-1";

    workflow::NewProjectOutcome created =
        workflow::NewProjectWizardFlow::commit(inputs, &submitter);
    ASSERT_TRUE(created.created) << "段 1 新建失败";
    ASSERT_TRUE(created.store != nullptr);
    ASSERT_TRUE(created.baselineRevision.has_value());
    pd::ProjectStore& store = *created.store;
    ASSERT_EQ(submitter.calls, 1) << "领域初始化恰好一次（模板来源）";
    // 修订链黄金值：r0 骨架(1)＋模板基线(2)。
    ASSERT_EQ(historySize(store), std::size_t{2}) << "段 1 链长（r0＋基线）";
    EXPECT_EQ(store.query().branchTips().at(0).tip, *created.baselineRevision)
        << "基线修订在 tip";

    // 应用命令对装配期注册（store 就绪后、任何 submit 前——project 注册时序）。
    store.handlerRegistry().registerHandler(
        std::make_unique<ApplySeedHandler>(applyHandler));
    store.handlerRegistry().registerHandler(
        std::make_unique<ApplySeedInverseHandler>(applyInverseHandler));

    // 桥接线＋上下文快照（Project 作用域命令谓词——有项目且可写）。
    revisionBridge.store = &store;
    revisionBridge.branch = mainBranchOf(store);
    revisionBridge.applySeedId = applySeedId;
    registry->presentContext(ui::UiContextSnapshot{true, true, {}});

    // ================= 段 2：编辑（草稿写轨——KIN-06 边界） =================
    const fs::path draftFile = saveDraftViaService(store, "modeling");
    ASSERT_FALSE(draftFile.empty()) << "段 2 前置：编辑草稿落盘";
    const std::size_t historyBeforeEdit = historySize(store);
    const ui::CommandOutcome saved = registry->submit("draft.save");
    EXPECT_TRUE(saved.accepted) << "保存草稿命令受理";
    EXPECT_EQ(session.saveCalls, 1) << "会话端口恰好一次";
    EXPECT_EQ(historySize(store), historyBeforeEdit)
        << "编辑步零修订（草稿写轨不是修订——KIN-06 同源边界）";
    // 草稿在盘（summarize present——应用步的输入事实）。
    {
        const auto summary = store.drafts().summarize(mainBranchOf(store));
        bool present = false;
        for (const auto& item : summary.items) {
            present = present || (item.moduleId == "modeling" && item.present);
        }
        EXPECT_TRUE(present) << "编辑草稿在投影中 present";
    }

    // ================= 段 3：应用（draft.apply→①端口） =================
    const ui::CommandOutcome applied = registry->submit("draft.apply");
    ASSERT_TRUE(applied.accepted) << "应用命令派发成功";
    EXPECT_FALSE(applied.messageKey.has_value()) << "应用成功无失败文案键";
    EXPECT_EQ(revisionBridge.applyCalls, 1) << "修订端口恰一次";
    ASSERT_EQ(historySize(store), std::size_t{3}) << "段 3 链长（＋应用修订）";
    // 应用修订＝分支新 tip（草稿落为修订的链面观测）。
    const auto tipsAfterApply = store.query().branchTips();
    ASSERT_FALSE(tipsAfterApply.empty());
    const core::RevisionId appliedTip = tipsAfterApply[0].tip;
    EXPECT_TRUE(store.query().tryRevision(appliedTip).has_value()) << "应用修订可读";

    // ================= 段 4：撤销/重做（PM-18/AT-29） =================
    // 全程修订身份登记（PA-2"只增不改"的逐修订可读复核素材）。
    const auto historySnapshot = store.query().branchHistory(mainBranchOf(store), 1024);
    ASSERT_EQ(historySnapshot.size(), std::size_t{3});

    const ui::CommandOutcome undo = registry->submit("project.undo");
    ASSERT_TRUE(undo.accepted) << "撤销派发成功";
    EXPECT_FALSE(undo.messageKey.has_value()) << "撤销成功无失败文案键";
    ASSERT_EQ(historySize(store), std::size_t{4})
        << "撤销产生恰好一个新修订（不是抹除——PA-2）";
    EXPECT_TRUE(store.undoRedo().status(mainBranchOf(store)).canRedo)
        << "撤销后会话 redo 栈非空";

    const ui::CommandOutcome redo = registry->submit("project.redo");
    ASSERT_TRUE(redo.accepted) << "重做派发成功";
    ASSERT_EQ(historySize(store), std::size_t{5})
        << "重做产生新修订（原始载荷重放——不是回退）";

    // 历史只增不改：段 4 前的三个修订（r0/基线/应用）逐个仍可读。
    for (const auto& entry : historySnapshot) {
        EXPECT_TRUE(store.query().tryRevision(entry.id).has_value())
            << "早期修订未被改写（PA-2）";
    }

    // ================= 段 5：包导出（真实 io 导出器） =================
    const auto treeBeforeExport = snapshotTreeOf(created.store->canonicalPath());
    workflow::PackageExportRequest exportRequest;
    exportRequest.targetFile = s_base / "mainline" / "mainline.rwpack";
    exportRequest.selection = workflow::PackageSelectionFlags{};  // 全选
    MainlineExportPort exportPort;
    const workflow::PackageExportOutcome exported =
        workflow::PackageExportFlow::run(store, exportRequest, exportPort);
    ASSERT_EQ(exported.result, workflow::PackageExportOutcome::Result::Completed)
        << "段 5 导出失败: "
        << (exported.failure.has_value() ? exported.failure->cause : "?");
    EXPECT_GT(exported.entryCount, 0u) << ".rwpack 条目数>0（观测点：包产物）";
    ASSERT_TRUE(fs::exists(exportRequest.targetFile)) << ".rwpack 落盘";
    EXPECT_EQ(snapshotTreeOf(created.store->canonicalPath()), treeBeforeExport)
        << "导出只读——源目录树零变化（导出失败/成功均不动项目状态）";
    // 导出不产生修订（链面复核——元数据组装是对源只读消费）。
    EXPECT_EQ(historySize(store), std::size_t{5}) << "导出后链长不变";

    // ================= 段 6：包导入（真实 io 校验＋发布桩） =================
    const core::ProjectId sourceProjectId = store.projectId();
    workflow::PackageImportRequest importRequest;
    importRequest.packFile = exportRequest.targetFile;
    importRequest.targetDir = s_base / "mainline" / "imported.rwdesign";
    MainlineImportPort importPort;
    const workflow::PackageImportOutcome imported =
        workflow::PackageImportFlow::run(importRequest, importPort);
    ASSERT_EQ(imported.result, workflow::PackageImportOutcome::Result::Completed)
        << "段 6 导入失败: "
        << (imported.failure.has_value() ? imported.failure->cause : "?");
    EXPECT_TRUE(imported.execution.verified) << "全量校验通过（逐条目哈希复算）";
    EXPECT_TRUE(imported.execution.published) << "发布完成（目标就位）";
    EXPECT_EQ(imported.execution.verifiedEntries, imported.execution.manifestEntries)
        << "校验报告：逐条目全绿";
    ASSERT_TRUE(fs::exists(importRequest.targetDir)) << "导入目标目录就位（失败不留目录的正面）";
    // 复制产物合法性：按打开协议真实打开（projectId 同源——解包逐字节还原）。
    {
        pd::OpenStoreRequest openRequest;
        openRequest.path = importRequest.targetDir;
        openRequest.mode = pd::OpenMode::Writable;
        auto reopened = ProjectStoreFactory::open(openRequest);
        ASSERT_TRUE(reopened.store != nullptr) << "导入目标真实可打开";
        EXPECT_EQ(reopened.store->projectId(), sourceProjectId)
            << "导入项目身份与源一致（还原语义）";
        reopened.store->requestClose();
        reopened.store.reset();
    }

    // ================= 段 7：另存（真实复制→打开协议进入） =================
    // 承载边界（诚实登记——SaveAsPackageContract 同款先例）：复制端口桩
    // 执行真实 fs::copy 但**不代写新 projectId 分配**（project.json 创建期
    // 一次写入归 project 存储侧——WP-04-T18 契约未生成，Identity.hpp
    // ProjectId 行"另存为换新 id；分配者＝project"）；编排半区的可复核面
    // ＝Outcome.projectId 随复制产物回传＋按打开协议真实进入。存储侧真实
    // 分配落位后由"≠源"断言补强（本用例的另存观测点＝进入成功＋身份一致
    // ＋镜像在位）。
    workflow::SaveAsRequest saveAsRequest;
    saveAsRequest.targetDir = s_base / "mainline" / "saveas.rwdesign";
    saveAsRequest.selection = workflow::PackageSelectionFlags{};  // 全选
    MainlineCopySaveAsPort saveAsPort;
    // 非 const：Entered 路径需要释放 outcome.store 的写锁（reset——const
    // 对象的 unique_ptr 成员不可移转；SaveAsPackageContract 同款口径）。
    workflow::SaveAsOutcome savedAs =
        workflow::SaveAsFlow::run(store, saveAsRequest, saveAsPort);
    ASSERT_EQ(savedAs.result, workflow::SaveAsOutcome::Result::Entered)
        << "段 7 另存失败: "
        << (savedAs.failure.has_value() ? savedAs.failure->cause : "?");
    ASSERT_TRUE(savedAs.store != nullptr) << "另存移交新项目存储上下文";
    ASSERT_TRUE(savedAs.projectId.has_value());
    EXPECT_TRUE(savedAs.projectId->isValid()) << "projectId 有效（prj- 非保留值）";
    EXPECT_TRUE(savedAs.store->writable()) << "进入的新项目可写";
    EXPECT_EQ(savedAs.store->projectId(), savedAs.projectId) << "身份一致";
    EXPECT_FALSE(savedAs.canonicalPath.empty()) << "规范路径非空（最近项目记录键）";
    // 镜像必备在目标（复制完整性——open 成功的独立证据）。
    EXPECT_TRUE(fs::exists(saveAsRequest.targetDir / "project.json"));
    EXPECT_TRUE(fs::exists(saveAsRequest.targetDir / "HEAD"));
    EXPECT_EQ(savedAs.selection, saveAsRequest.selection) << "勾选随 Outcome 回传（记忆登记面）";

    // ================= 终局：全链观测点汇总 =================
    // 修订链黄金序列终值：5（r0/基线/应用/撤销/重做）——全链写路径全部
    // 经①端口发生且只增不改。
    EXPECT_EQ(historySize(store), std::size_t{5}) << "全链修订链黄金值";
    EXPECT_TRUE(store.undoRedo().status(mainBranchOf(store)).canUndo)
        << "链尾（重做修订）可撤销态稳定";
    // 全链触达计数：应用/撤销/重做各恰一次（统一入口 submit 路径）。
    EXPECT_EQ(revisionBridge.applyCalls, 1);
    EXPECT_EQ(revisionBridge.undoCalls, 1);
    EXPECT_EQ(revisionBridge.redoCalls, 1);
    EXPECT_EQ(session.saveCalls, 1);

    // 收尾释放三个存储上下文写锁（原项目/导入重启的已关/另存新项目）。
    savedAs.store->requestClose();
    savedAs.store.reset();
    created.store->requestClose();
    created.store.reset();
}

// =====================================================================
// 夹具辅助（目录树字节面快照——CommandsContract/CloseFlowContract 同款
// 口径；定义在外层匿名 namespace——与用例的前向声明同一命名空间）
// =====================================================================

/// 目录树字节面快照（相对路径 UTF-8 → 文件大小；lock 心跳文件排除——
/// 持有期心跳重写是正常事实）。
std::map<std::string, std::uintmax_t> snapshotTreeOf(const fs::path& root)
{
    std::map<std::string, std::uintmax_t> snapshot;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec || !it->is_regular_file(ec)) {
            continue;
        }
        if (it->path().filename().string() == "lock") {
            continue;
        }
        snapshot[it->path().lexically_relative(root).u8string()] = it->file_size(ec);
    }
    return snapshot;
}

}  // namespace
