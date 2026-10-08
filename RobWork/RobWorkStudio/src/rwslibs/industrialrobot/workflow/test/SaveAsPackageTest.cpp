/**
 * @file   SaveAsPackageTest.cpp
 * @brief  另存为与包导出/导入编排的模型测试（WP-22-T07——units/workflow.md
 *         §7.4 编排核的直调半区；PM-05 的记忆默认/取消即清理/失败呈现/
 *         校验报告数据面）。
 *
 * 设计依据：
 *   - units/workflow.md §7.4（另存为＝完整目录复制＋勾选记忆默认＋换新
 *     projectId 后按打开协议进入；包导出＝后台进度可取消、取消即清理
 *     临时区；包导入＝预算/路径穿越防护与全量校验〔失败不留目标目录〕
 *     并给出校验报告——io 执行、workflow 呈现）、§10.3（错误二分：调用
 *     方错误 WorkflowError fail-fast；环境/对端错误值轨道；取消非错误）、
 *     §14.1 D-WF-6/D-WF-7（宿主面归 ui；零新增稳定码——对端透传）
 *   - REQUIREMENTS.md §17 PM-05 原文（另存为/包导出/包导入全语义＋
 *     "导出/导入后台进度可取消，取消即清理临时区"）、AT-20（向导取消
 *     不留半成品）、UX-03（取消是状态非错误——取消不产诊断）
 *   - 任务契约 tasks/foundation/WP-22-T07.json acceptance 1/2/3（另存
 *     勾选记忆默认；包导出取消清理；包导入校验失败不留目标目录并给出
 *     校验报告；P-IO-1 注入形态——公共头零 io 类型，端口全桩化）
 *
 * 测试形态（§11.0——模型测试＝直调计算库面）：编排核为静态函数、三个
 * 执行端口全部脚本化桩（勾选/取消/清理观测位/成败/诊断可编程，调用
 * 轨迹可观测）；存储上下文最小桩（编排核只消费 canonicalPath/projectId/
 * query().head()——其余接口调用即测试违约 abort）。真实落盘半区（复制
 * 裁剪、ZIP 产物、越界路径拒绝、失败目标零残留）在契约测试
 * SaveAsPackageContractTest.cpp（跨单元联合）。个别用例的"打开协议"
 * 分支经 ProjectStoreFactory 真实拒绝路径（not-a-project）承载——只读
 * 探测不写盘。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>               // core::ProjectId/RevisionId（toCanonical 词形组装断言）
#include <sdurws/ird/project/QueryPort.hpp>           // project::IProjectQueryPort 完整定义＋RevisionView（查询桩的覆写基面）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::FlowProgressStage;
using workflow::IFlowCancelToken;
using workflow::PackageImportDiagnosticLine;
using workflow::PackageImportEntryLine;
using workflow::PackageImportExecution;
using workflow::PackageImportReportLine;
using workflow::PackageSelectionFlags;

// =====================================================================
// 桩：最小存储上下文（编排核只消费 canonicalPath/projectId/query().head()
// ——其余接口诚实拒绝：调用即测试违约，fail-fast 暴露而非静默返回假数据；
// CloseFlowTest::CloseStoreStub 同款手法）。
// =====================================================================

/// 查询端口最小桩（只供 head()——包导出源元数据组装的读取面）。
class QueryPortStub final : public project::IProjectQueryPort {
public:
    project::RevisionView headView;///< head() 返回值（HEAD 修订词形组装的桩料）
    bool failHead = false;         ///< true＝head() 抛（环境失败注入——编排核值轨道呈现）

    project::RevisionView head() const override
    {
        if (failHead) {
            throw std::runtime_error("stub: HEAD 读取失败（环境注入）");
        }
        return headView;
    }
    std::optional<project::RevisionView> tryRevision(core::RevisionId) const override
    {
        std::abort();  // 不可达：编排核零历史修订查询（见类注）
    }
    project::RevisionView revision(core::RevisionId) const override { std::abort(); }
    project::ProjectMetadataView currentMetadata() const override { std::abort(); }
    std::optional<project::ProjectMetadataView> metadataAt(core::RevisionId) const override
    {
        std::abort();
    }
    std::vector<project::BranchTip> branchTips() const override { std::abort(); }
    std::vector<project::RevisionView> branchHistory(core::BranchId,
                                                     std::uint32_t) const override
    {
        std::abort();
    }
    std::optional<std::vector<std::uint8_t>> tryObject(core::ObjectId,
                                                       core::ContentVersion) const noexcept override
    {
        std::abort();
    }
    std::vector<std::uint8_t> object(core::ObjectId, core::ContentVersion) const override
    {
        std::abort();
    }
    std::vector<project::DraftInfo> listDrafts(core::BranchId) const override
    {
        std::abort();
    }
    std::vector<project::RunInfo> listRuns(core::RevisionId) const override
    {
        std::abort();
    }
    std::filesystem::path runDir(core::RunId) const override { std::abort(); }
};

/// 存储上下文最小桩（另存/包导出编排核的只读消费面）。
class SaveAsStoreStub final : public project::ProjectStore {
public:
    std::filesystem::path path;///< canonicalPath() 返回值（同径判定的桩料）
    core::ProjectId id = core::ProjectId::generate();///< projectId() 返回值（源身份组装的桩料）
    QueryPortStub queryPort;///< query() 返回的查询桩（head 读取面）

    bool writable() const noexcept override { return true; }
    project::LockInfo lockInfo() const override { return project::LockInfo{}; }
    core::ProjectId projectId() const noexcept override { return id; }
    project::SchemaInfo schema() const override { return project::SchemaInfo{}; }
    std::filesystem::path canonicalPath() const override { return path; }

    project::IProjectQueryPort& query() const noexcept override
    {
        // query() 返回成员桩——const 下去除 volatile 性质安全（成员可变
        // 语义由桩自身保证；测试单线程纪律下无竞态）。
        return const_cast<QueryPortStub&>(queryPort);
    }
    project::ProjectCommandService& commands() const noexcept override { std::abort(); }
    project::DraftService& drafts() const noexcept override { std::abort(); }
    project::UndoRedoService& undoRedo() const noexcept override { std::abort(); }
    project::IResultArchivePort& archive() const noexcept override { std::abort(); }

    std::uint32_t requestClose() override { std::abort(); }
    bool closed() const noexcept override { return false; }
    void subscribeClose(project::ICloseObserver&) override {}
};

// =====================================================================
// 桩：取消令牌（内存置位——IFlowCancelToken 接口消费钉扎面）。
// =====================================================================

class MemoryCancelToken final : public IFlowCancelToken {
public:
    bool cancelRequested() const override { return m_flag; }
    void requestCancel() override { m_flag = true; }

private:
    bool m_flag = false;///< 取消标志（置位后恒真——实现契约）
};

// =====================================================================
// 桩：另存执行端口（成败/取消/清理可编程＋调用轨迹观测）。
// =====================================================================

class ScriptedSaveAsPort final : public workflow::ISaveAsPort {
public:
    bool copiedAnswer = true;   ///< copied 回传脚本
    bool cancelledAnswer = false;///< cancelled 回传脚本
    bool cleanAnswer = true;    ///< targetLeftClean 回传脚本
    std::string causeAnswer;    ///< cause 回传脚本
    std::string actionAnswer;   ///< action 回传脚本

    // ---- 调用轨迹观测（编排核对端口的消费契约断言面）----
    int executeCalls = 0;                     ///< executeCopy 调用计数
    workflow::SaveAsRequest seenRequest;      ///< 收到的请求（勾选透传断言）
    IFlowCancelToken* seenCancel = nullptr;   ///< 收到的取消令牌（透传断言）
    int progressCalls = 0;                    ///< 进度回调被编排核贯通的次数

    Execution executeCopy(project::ProjectStore& /*source*/,
                          const workflow::SaveAsRequest& request,
                          IFlowCancelToken* cancel,
                          const workflow::FlowProgressCallback& progress) override
    {
        ++executeCalls;
        seenRequest = request;
        seenCancel = cancel;
        if (progress) {
            ++progressCalls;  // 贯通观测：回调非空即拍发一帧进度
            progress(FlowProgressStage{"stub-copy", 1, 2});
        }
        Execution e;
        e.copied = copiedAnswer;
        e.cancelled = cancelledAnswer;
        e.targetLeftClean = cleanAnswer;
        e.cause = causeAnswer;
        e.action = actionAnswer;
        return e;
    }
};

// =====================================================================
// 桩：包导出执行端口（成败/取消/清理可编程＋请求元数据捕获）。
// =====================================================================

class ScriptedExportPort final : public workflow::IPackageExportPort {
public:
    bool exportedAnswer = true; ///< exported 回传脚本
    bool cancelledAnswer = false;///< cancelled 回传脚本
    bool cleanAnswer = true;    ///< temporaryAreaCleaned 回传脚本
    std::uint64_t entryAnswer = 7;///< entryCount 回传脚本
    std::uint64_t bytesAnswer = 2048;///< totalBytes 回传脚本（单位＝字节）
    std::string causeAnswer;    ///< cause 回传脚本
    std::string actionAnswer;   ///< action 回传脚本

    int exportCalls = 0;                          ///< exportPackage 调用计数
    workflow::PackageExportRequest seenRequest;   ///< 收到的请求（元数据组装断言）
    IFlowCancelToken* seenCancel = nullptr;       ///< 收到的取消令牌
    int progressCalls = 0;                        ///< 进度贯通计数

    Execution exportPackage(project::ProjectStore& /*source*/,
                            const workflow::PackageExportRequest& request,
                            IFlowCancelToken* cancel,
                            const workflow::FlowProgressCallback& progress) override
    {
        ++exportCalls;
        seenRequest = request;
        seenCancel = cancel;
        if (progress) {
            ++progressCalls;
            progress(FlowProgressStage{"stub-export", 1, 1});
        }
        Execution e;
        e.exported = exportedAnswer;
        e.cancelled = cancelledAnswer;
        e.temporaryAreaCleaned = cleanAnswer;
        e.entryCount = entryAnswer;
        e.totalBytes = bytesAnswer;
        e.cause = causeAnswer;
        e.action = actionAnswer;
        return e;
    }
};

// =====================================================================
// 桩：包导入执行端口（校验结论/发布/清理/诊断可编程）。
// =====================================================================

class ScriptedImportPort final : public workflow::IPackageImportPort {
public:
    PackageImportExecution answer;///< importPackage 回传脚本（全字段可编程）

    int importCalls = 0;                          ///< importPackage 调用计数
    workflow::PackageImportRequest seenRequest;   ///< 收到的请求
    IFlowCancelToken* seenCancel = nullptr;       ///< 收到的取消令牌
    int progressCalls = 0;                        ///< 进度贯通计数

    PackageImportExecution importPackage(const workflow::PackageImportRequest& request,
                                         IFlowCancelToken* cancel,
                                         const workflow::FlowProgressCallback& progress) override
    {
        ++importCalls;
        seenRequest = request;
        seenCancel = cancel;
        if (progress) {
            ++progressCalls;
            progress(FlowProgressStage{"stub-import", 3, 4});
        }
        return answer;
    }
};

// =====================================================================
// 夹具：源项目桩（路径成员——同径判定用例需要不同路径实例）。
// =====================================================================

class WfSaveAsPackage : public ::testing::Test {
protected:
    SaveAsStoreStub m_source;///< 源项目桩（每个用例独立实例）

    void SetUp() override
    {
        // 源路径桩料（词面即可——编排核不触该路径）；HEAD 修订桩料
        // （rev- 词形由 core 生成——词形组装断言经 toCanonical 往返）。
        m_source.path = std::filesystem::path("stub-root") / "source-design";  // 相对词面桩料（无驱动器冒号——避开 MSVC 对 "X:stub" 无根分隔符形态的 root-name 解析怪癖；本面只做词法比较不触盘）
        m_source.queryPort.headView.id = core::RevisionId::generate();
    }
};

// =====================================================================
// 记忆默认与输入校验（纯函数面——PM-05"勾选、记忆默认"）
// =====================================================================

/// PM-05"记忆默认"：无记忆→全选缺省（完整复制语义）；有记忆→记忆值
/// 逐字段原样（含部分取消勾选的记忆——不回归全选）。
TEST_F(WfSaveAsPackage, DefaultSelection_MemoryOrDefault_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    // 无记忆（首次使用/设置被清）→ 全选缺省。
    const PackageSelectionFlags defaults =
        workflow::defaultSelectionOf(std::nullopt);
    EXPECT_TRUE(defaults.includeResults);
    EXPECT_TRUE(defaults.includeReports);
    EXPECT_TRUE(defaults.includeDrafts);

    // 有记忆→记忆值原样（部分取消勾选的记忆不被缺省覆盖——记忆默认
    // 语义的本义）。
    PackageSelectionFlags remembered;
    remembered.includeResults = false;   // 上次用户取消了 results 树
    remembered.includeReports = true;
    remembered.includeDrafts = false;
    const PackageSelectionFlags resolved =
        workflow::defaultSelectionOf(remembered);
    EXPECT_FALSE(resolved.includeResults);
    EXPECT_TRUE(resolved.includeReports);
    EXPECT_FALSE(resolved.includeDrafts);
}

/// PM-05 校验键序：空目标 → 同径 → 已存在非空；合法输入放行（空集）。
TEST_F(WfSaveAsPackage, SaveAsValidate_KeySequence_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});

    const std::filesystem::path source = m_source.path;

    // 空目标。
    const auto emptyKeys = workflow::validateSaveAsInputs(source, {});
    ASSERT_EQ(emptyKeys.size(), 1u);
    EXPECT_EQ(emptyKeys[0], "wizard.save-as.error.target-empty");

    // 与源同径（lexically_normal 收敛＋尾分隔去尾——"a/b/.." 形态的
    // 标准规范化形带尾分隔符〔"a/"〕，编排核同径判定按去尾形收敛）。
    std::filesystem::path same = source / "." / "sub" / "..";
    const auto sameKeys = workflow::validateSaveAsInputs(source, same);
    ASSERT_EQ(sameKeys.size(), 1u);
    EXPECT_EQ(sameKeys[0], "wizard.save-as.error.target-same-as-source");

    // 目标已存在且非空（temp 根——环境事实夹具）。
    const auto nonempty = std::filesystem::temp_directory_path();
    const auto existsKeys = workflow::validateSaveAsInputs(source, nonempty);
    ASSERT_EQ(existsKeys.size(), 1u);
    EXPECT_EQ(existsKeys[0], "wizard.save-as.error.target-exists-nonempty");

    // 合法输入（不存在的新路径词面）→ 放行。
    const std::filesystem::path fresh =
        std::filesystem::temp_directory_path() / "ird_wf_saveas_validate_fresh_dir";
    std::error_code ec;
    std::filesystem::remove_all(fresh, ec);  // 前次残留防御
    const auto okKeys = workflow::validateSaveAsInputs(source, fresh);
    EXPECT_TRUE(okKeys.empty());
}

/// 包导出校验：空目标/扩展名词面错（大小写不敏感识别——.RWPACK 放行）。
TEST_F(WfSaveAsPackage, ExportValidate_KeySequence_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});

    const auto emptyKeys = workflow::validatePackageExportInput({});
    ASSERT_EQ(emptyKeys.size(), 1u);
    EXPECT_EQ(emptyKeys[0], "wizard.package-export.error.target-empty");

    const auto extKeys = workflow::validatePackageExportInput(
        std::filesystem::path("D:/out/project.zip"));
    ASSERT_EQ(extKeys.size(), 1u);
    EXPECT_EQ(extKeys[0], "wizard.package-export.error.target-extension");

    // 大写词面放行（大小写不敏感——Windows 惯例，与 classifyOpenTarget 同口径）。
    const auto upperKeys = workflow::validatePackageExportInput(
        std::filesystem::path("D:/out/PROJECT.RWPACK"));
    EXPECT_TRUE(upperKeys.empty());
}

/// 包导入校验：四键序（pack-empty → target-empty → pack-extension →
/// target-exists）＋合法输入放行。
TEST_F(WfSaveAsPackage, ImportValidate_KeySequence_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});

    // 双空：前两键按序齐出。
    workflow::PackageImportRequest bothEmpty;
    const auto bothKeys = workflow::validatePackageImportInput(bothEmpty);
    ASSERT_EQ(bothKeys.size(), 2u);
    EXPECT_EQ(bothKeys[0], "wizard.package-import.error.pack-empty");
    EXPECT_EQ(bothKeys[1], "wizard.package-import.error.target-empty");

    // 扩展名错＋目标已存在（temp 根实测存在）。
    workflow::PackageImportRequest bad;
    bad.packFile = std::filesystem::path("D:/pkg/archive.zip");
    bad.targetDir = std::filesystem::temp_directory_path();
    const auto badKeys = workflow::validatePackageImportInput(bad);
    ASSERT_EQ(badKeys.size(), 2u);
    EXPECT_EQ(badKeys[0], "wizard.package-import.error.pack-extension");
    EXPECT_EQ(badKeys[1], "wizard.package-import.error.target-exists");

    // 合法输入（包词面正确＋目标不存在）→ 放行。
    workflow::PackageImportRequest ok;
    ok.packFile = std::filesystem::path("D:/pkg/demo.rwpack");
    ok.targetDir = std::filesystem::temp_directory_path()
        / "ird_wf_import_validate_fresh_target";
    std::error_code ec;
    std::filesystem::remove_all(ok.targetDir, ec);  // 前次残留防御
    const auto okKeys = workflow::validatePackageImportInput(ok);
    EXPECT_TRUE(okKeys.empty());
}

// =====================================================================
// 另存为编排核（PM-05/AT-20——取消非错误、失败呈现、前置 fail-fast）
// =====================================================================

/// 前置契约：空目标/与源同径 → WorkflowError（调用方装配违约 fail-fast；
/// UX-03——取消与输入错误应在宿主呈现面拦截）。
TEST_F(WfSaveAsPackage, SaveAsFlow_Preconditions_FailFast_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedSaveAsPort port;

    workflow::SaveAsRequest emptyTarget;
    emptyTarget.targetDir = std::filesystem::path{};  // 空路径（fs::path 的 {} 重载歧义——显式类型）
    EXPECT_THROW(workflow::SaveAsFlow::run(m_source, emptyTarget, port),
                 workflow::WorkflowError);

    workflow::SaveAsRequest sameTarget;
    sameTarget.targetDir = m_source.path;  // 与源同径词面
    EXPECT_THROW(workflow::SaveAsFlow::run(m_source, sameTarget, port),
                 workflow::WorkflowError);

    // 前置拒绝路径零端口调用（复制执行未启动）。
    EXPECT_EQ(port.executeCalls, 0);
}

/// PM-05/UX-03/AT-20：取消且目标干净 → Canceled（failure 空、store 空、
/// 零诊断——取消不是错误）＋确认勾选随 Outcome 回传（记忆登记面）。
TEST_F(WfSaveAsPackage, SaveAsFlow_Cancelled_CleanTarget_NonError_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20", "UX-03"});
    ScriptedSaveAsPort port;
    port.copiedAnswer = false;
    port.cancelledAnswer = true;
    port.cleanAnswer = true;

    workflow::SaveAsRequest request;
    request.targetDir = std::filesystem::path("X:/stub/target-design");
    request.selection.includeResults = false;  // 用户取消了 results 勾选

    const workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.failure.has_value());  // 取消非错误——零失败呈现
    EXPECT_FALSE(outcome.store);                // 零存储上下文产出
    // 记忆登记面：确认勾选恒回传（取消路径也带——用户勾选事实已发生）。
    EXPECT_FALSE(outcome.selection.includeResults);
    EXPECT_TRUE(outcome.selection.includeReports);
    // 编排核对端口的消费契约：请求/取消令牌贯通。
    ASSERT_EQ(port.executeCalls, 1);
    EXPECT_EQ(port.seenRequest, request);
    EXPECT_EQ(port.seenCancel, nullptr);  // 未注入令牌＝null 透传
}

/// PM-05/AT-20"取消即清理"：取消但清理观测位为假 → Failed（残留事实
/// 如实呈现，不带病报取消成功）。
TEST_F(WfSaveAsPackage, SaveAsFlow_Cancelled_Leftover_Failed_AT20)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});
    ScriptedSaveAsPort port;
    port.copiedAnswer = false;
    port.cancelledAnswer = true;
    port.cleanAnswer = false;  // 清理承诺破坏——残留

    workflow::SaveAsRequest request;
    request.targetDir = std::filesystem::path("X:/stub/target-design");
    const workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("清理"), std::string::npos)
        << "失败呈现须点名清理未完成（残留事实）";
    EXPECT_EQ(outcome.failure->file, request.targetDir.u8string());
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
}

/// UX-03 三字段：复制失败 → Failed＋端口 cause/action 透传（空 action
/// 兜底默认指引）＋store 恒空。
TEST_F(WfSaveAsPackage, SaveAsFlow_CopyFailed_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    ScriptedSaveAsPort port;
    port.copiedAnswer = false;
    port.cancelledAnswer = false;
    port.cleanAnswer = true;
    port.causeAnswer = "目标卷空间不足";
    port.actionAnswer = "请清理磁盘后重试";

    workflow::SaveAsRequest request;
    request.targetDir = std::filesystem::path("X:/stub/target-design");
    const workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->context, request.targetDir.u8string());
    EXPECT_EQ(outcome.failure->file, request.targetDir.u8string());
    EXPECT_EQ(outcome.failure->cause, "目标卷空间不足");
    EXPECT_EQ(outcome.failure->recommendedAction, "请清理磁盘后重试");
    EXPECT_FALSE(outcome.store);
}

/// PM-05"按打开协议进入"：复制成功但产物不可打开（目标不存在——打开
/// 协议 not-a-project 真实拒绝）→ Failed＋file 定位非空（不以复制成功
/// 伪装进入成功）。
TEST_F(WfSaveAsPackage, SaveAsFlow_CopyOk_OpenFails_Failed_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});
    ScriptedSaveAsPort port;
    port.copiedAnswer = true;  // 桩报复制成功，但不产生真实项目目录

    workflow::SaveAsRequest request;
    // 不存在的目标词面（temp 下未创建——open 探测不写盘）。
    request.targetDir = std::filesystem::temp_directory_path()
        / "ird_wf_saveas_open_fail_target";
    std::error_code ec;
    std::filesystem::remove_all(request.targetDir, ec);  // 前次残留防御

    const workflow::SaveAsOutcome outcome =
        workflow::SaveAsFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->file.empty());  // 具体定位恒非空（AT-20）
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_FALSE(outcome.store);
}

/// 编排核→端口消费契约：取消令牌与进度回调贯通、勾选透传（桩观测）。
TEST_F(WfSaveAsPackage, SaveAsFlow_PortReceivesCancelAndProgress_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedSaveAsPort port;
    port.copiedAnswer = false;
    port.cancelledAnswer = true;  // 短路返回（不触打开协议）

    MemoryCancelToken cancel;
    int frames = 0;
    workflow::SaveAsRequest request;
    request.targetDir = std::filesystem::path("X:/stub/target-design");

    const workflow::SaveAsOutcome outcome = workflow::SaveAsFlow::run(
        m_source, request, port, &cancel,
        [&frames](const FlowProgressStage&) { ++frames; });

    EXPECT_EQ(outcome.result, workflow::SaveAsOutcome::Result::Canceled);
    EXPECT_EQ(port.seenCancel, &cancel);  // 令牌指针贯通（同一实例）
    EXPECT_EQ(frames, 1);                 // 进度回调贯通（桩拍发一帧）
    EXPECT_EQ(port.seenRequest.selection, request.selection);
}

// =====================================================================
// 包导出编排核（PM-05——取消即清理、元数据组装、失败呈现）
// =====================================================================

/// 前置契约：空目标/扩展名词面错 → WorkflowError；前置拒绝零端口调用。
TEST_F(WfSaveAsPackage, ExportFlow_Preconditions_FailFast_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedExportPort port;

    workflow::PackageExportRequest emptyTarget;
    EXPECT_THROW(workflow::PackageExportFlow::run(m_source, emptyTarget, port),
                 workflow::WorkflowError);

    workflow::PackageExportRequest badExt;
    badExt.targetFile = std::filesystem::path("D:/out/project.zip");
    EXPECT_THROW(workflow::PackageExportFlow::run(m_source, badExt, port),
                 workflow::WorkflowError);

    EXPECT_EQ(port.exportCalls, 0);
}

/// PM-05/§7.1：完成路径——统计登记＋源元数据由编排核组装（源项目 id 与
/// HEAD 修订的规范词形——调用方不填，编排核覆盖）＋勾选透传。
TEST_F(WfSaveAsPackage, ExportFlow_Completed_MetaAssembled_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedExportPort port;

    workflow::PackageExportRequest request;
    request.targetFile = std::filesystem::path("X:/stub/transfer.rwpack");
    request.selection.includeReports = false;  // 用户取消 reports 勾选
    // 调用方未填元数据（留空——由编排核组装覆盖）。
    request.sourceProjectId.clear();
    request.headRevisionId.clear();

    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Completed);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_EQ(outcome.entryCount, 7u);
    EXPECT_EQ(outcome.totalBytes, 2048u);
    // 元数据组装断言：桩收到的请求携带源身份与 HEAD 的规范词形。
    ASSERT_EQ(port.exportCalls, 1);
    EXPECT_EQ(port.seenRequest.sourceProjectId, m_source.id.toCanonical());
    EXPECT_EQ(port.seenRequest.headRevisionId,
              m_source.queryPort.headView.id.toCanonical());
    EXPECT_FALSE(port.seenRequest.selection.includeReports);
}

/// PM-05"取消即清理临时区"：取消且临时区已清理 → Canceled（failure 空
/// ——取消非错误）＋令牌贯通。
TEST_F(WfSaveAsPackage, ExportFlow_Cancelled_Cleaned_Canceled_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    ScriptedExportPort port;
    port.exportedAnswer = false;
    port.cancelledAnswer = true;
    port.cleanAnswer = true;

    MemoryCancelToken cancel;
    workflow::PackageExportRequest request;
    request.targetFile = std::filesystem::path("X:/stub/transfer.rwpack");

    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(m_source, request, port, &cancel);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_EQ(port.seenCancel, &cancel);
}

/// PM-05"取消即清理临时区"：取消但清理观测位为假 → Failed（残留事实
/// 如实呈现——不带病报取消成功）。
TEST_F(WfSaveAsPackage, ExportFlow_Cancelled_Leftover_Failed_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedExportPort port;
    port.exportedAnswer = false;
    port.cancelledAnswer = true;
    port.cleanAnswer = false;  // 临时区残留

    workflow::PackageExportRequest request;
    request.targetFile = std::filesystem::path("X:/stub/transfer.rwpack");

    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("清理"), std::string::npos);
}

/// UX-03：导出失败 → Failed＋端口 cause/action 透传＋统计不登记。
TEST_F(WfSaveAsPackage, ExportFlow_ExportFailed_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    ScriptedExportPort port;
    port.exportedAnswer = false;
    port.causeAnswer = "目标文件被占用";
    port.actionAnswer = "请关闭占用程序后重试";

    workflow::PackageExportRequest request;
    request.targetFile = std::filesystem::path("X:/stub/transfer.rwpack");

    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->cause, "目标文件被占用");
    EXPECT_EQ(outcome.failure->recommendedAction, "请关闭占用程序后重试");
    EXPECT_EQ(outcome.entryCount, 0u);  // 失败路径统计不登记
}

/// §7.4"导出失败保证项目状态不变"的编排侧：源元数据读取失败（环境）→
/// Failed（cause 透传）＋零端口调用（复制/压缩未启动）。
TEST_F(WfSaveAsPackage, ExportFlow_MetaReadFails_Failed_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    m_source.queryPort.failHead = true;  // HEAD 读取环境失败注入
    ScriptedExportPort port;

    workflow::PackageExportRequest request;
    request.targetFile = std::filesystem::path("X:/stub/transfer.rwpack");

    const workflow::PackageExportOutcome outcome =
        workflow::PackageExportFlow::run(m_source, request, port);

    EXPECT_EQ(outcome.result, workflow::PackageExportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("HEAD 读取失败"), std::string::npos);
    EXPECT_EQ(port.exportCalls, 0);  // 执行未启动（前置段短路）
}

// =====================================================================
// 包导入编排核（PM-05——校验报告材料透传、取消不落诊断、失败呈现）
// =====================================================================

/// 前置契约：包/目标空、扩展名词面错 → WorkflowError；前置拒绝零端口调用。
TEST_F(WfSaveAsPackage, ImportFlow_Preconditions_FailFast_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedImportPort port;

    workflow::PackageImportRequest emptyReq;
    EXPECT_THROW(workflow::PackageImportFlow::run(emptyReq, port),
                 workflow::WorkflowError);

    workflow::PackageImportRequest badExt;
    badExt.packFile = std::filesystem::path("D:/pkg/archive.zip");
    badExt.targetDir = std::filesystem::path("D:/pkg/imported");
    EXPECT_THROW(workflow::PackageImportFlow::run(badExt, port),
                 workflow::WorkflowError);

    EXPECT_EQ(port.importCalls, 0);
}

/// PM-05"给出校验报告"：完成路径——执行结果（报告材料）零加工透传
/// （逐字段相等——计数/条目/清理观测位）。
TEST_F(WfSaveAsPackage, ImportFlow_Verified_ExecPassedThrough_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"AT-20"});
    ScriptedImportPort port;
    port.answer.verified = true;
    port.answer.published = true;
    port.answer.targetLeftClean = true;
    port.answer.manifestEntries = 4;
    port.answer.verifiedEntries = 4;
    port.answer.totalBytes = 4096;
    port.answer.entryLines = {{"payload/HEAD", true}, {"payload/project.json", true}};

    workflow::PackageImportRequest request;
    request.packFile = std::filesystem::path("X:/pkg/transfer.rwpack");
    request.targetDir = std::filesystem::path("X:/pkg/imported-design");

    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Completed);
    EXPECT_FALSE(outcome.failure.has_value());
    // 报告材料透传：编排核零加工（同值比较——全字段）。
    EXPECT_EQ(outcome.execution, port.answer);
    EXPECT_EQ(port.seenRequest, request);
}

/// PM-05/NFR-SEC-01/02"失败不留目标目录"的呈现半区：校验失败 → Failed
/// ＋结构化诊断逐条透传（校验报告材料恒可读）＋UX-03 半区。
TEST_F(WfSaveAsPackage, ImportFlow_VerifyFailed_ReportsDiagnostics_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    ScriptedImportPort port;
    port.answer.verified = false;
    port.answer.published = false;
    port.answer.targetLeftClean = true;  // 目标零残留（执行面承诺）
    port.answer.manifestEntries = 4;
    port.answer.verifiedEntries = 2;
    port.answer.diagnostics = {
        PackageImportDiagnosticLine{"IO-SEC-PACK-PATH", "条目越界: payload/../evil.txt"},
        PackageImportDiagnosticLine{"IO-FORMAT-PACK-ENTRY", "条目不符 manifest"},
    };
    port.answer.cause = "路径穿越防护拒绝";
    port.answer.action = "请核对包来源";

    workflow::PackageImportRequest request;
    request.packFile = std::filesystem::path("X:/pkg/evil.rwpack");
    request.targetDir = std::filesystem::path("X:/pkg/imported-design");

    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->cause, "路径穿越防护拒绝");
    EXPECT_EQ(outcome.failure->recommendedAction, "请核对包来源");
    // 校验报告材料恒携带（呈现面取数源）——逐条诊断零丢失。
    ASSERT_EQ(outcome.execution.diagnostics.size(), 2u);
    EXPECT_EQ(outcome.execution.diagnostics[0].code, "IO-SEC-PACK-PATH");
    EXPECT_EQ(outcome.execution.diagnostics[1].message, "条目不符 manifest");
    EXPECT_EQ(outcome.execution.verifiedEntries, 2u);
}

/// UX-03：取消 → Canceled 且诊断恒空（取消不落诊断——V19 同型观测面）。
TEST_F(WfSaveAsPackage, ImportFlow_Cancelled_NoDiagnostics_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{"UX-03"});
    ScriptedImportPort port;
    port.answer.verified = false;
    port.answer.cancelled = true;
    port.answer.targetLeftClean = true;
    port.answer.diagnostics = {};  // 取消路径恒空（执行面承诺）

    workflow::PackageImportRequest request;
    request.packFile = std::filesystem::path("X:/pkg/transfer.rwpack");
    request.targetDir = std::filesystem::path("X:/pkg/imported-design");

    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Canceled);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_TRUE(outcome.execution.diagnostics.empty());
}

/// PM-05"失败不留目标目录"：取消但清理观测位为假 → Failed（残留事实
/// 如实呈现）。
TEST_F(WfSaveAsPackage, ImportFlow_Cancelled_Leftover_Failed_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedImportPort port;
    port.answer.cancelled = true;
    port.answer.targetLeftClean = false;  // 目标/临时区残留

    workflow::PackageImportRequest request;
    request.packFile = std::filesystem::path("X:/pkg/transfer.rwpack");
    request.targetDir = std::filesystem::path("X:/pkg/imported-design");

    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("清理"), std::string::npos);
}

/// 端口契约一致性：校验通过但发布未完成 → Failed（校验与发布必须同真
/// ——编排核不伪造完成态）。
TEST_F(WfSaveAsPackage, ImportFlow_VerifiedNotPublished_Failed_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    ScriptedImportPort port;
    port.answer.verified = true;
    port.answer.published = false;  // 发布未完成（端口契约破坏事实）

    workflow::PackageImportRequest request;
    request.packFile = std::filesystem::path("X:/pkg/transfer.rwpack");
    request.targetDir = std::filesystem::path("X:/pkg/imported-design");

    const workflow::PackageImportOutcome outcome =
        workflow::PackageImportFlow::run(request, port);

    EXPECT_EQ(outcome.result, workflow::PackageImportOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("发布"), std::string::npos);
}

/// PM-05"给出校验报告"：报告行集固定序＋值语义（verified 无诊断态 5 行
/// ——diagnostics-count 条件行不出现〔零占位行纪律〕；failed 带诊断态
/// 6 行）＋确定性双跑。
TEST_F(WfSaveAsPackage, ImportReportView_LinesFixedSequence_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});

    // —— verified 无诊断态：5 行固定序（diagnostics-count 条件行缺席）。
    PackageImportExecution ok;
    ok.verified = true;
    ok.published = true;
    ok.targetLeftClean = true;
    ok.manifestEntries = 4;
    ok.verifiedEntries = 4;
    ok.totalBytes = 4096;
    const auto okLines = workflow::buildPackageImportReportView(ok);
    ASSERT_EQ(okLines.size(), 5u);
    EXPECT_EQ(okLines[0].labelKey, "wizard.package-import.report.final-state");
    EXPECT_EQ(okLines[0].valueText, "verified");
    EXPECT_EQ(okLines[1].labelKey, "wizard.package-import.report.manifest-entries");
    EXPECT_EQ(okLines[1].valueText, "4");
    EXPECT_EQ(okLines[2].labelKey, "wizard.package-import.report.verified-entries");
    EXPECT_EQ(okLines[2].valueText, "4");
    EXPECT_EQ(okLines[3].labelKey, "wizard.package-import.report.total-bytes");
    EXPECT_EQ(okLines[3].valueText, "4096");
    EXPECT_EQ(okLines[4].labelKey, "wizard.package-import.report.target-clean");
    EXPECT_EQ(okLines[4].valueText, "yes");

    // —— failed 带诊断态：6 行（diagnostics-count 条件行出现于位 5）。
    PackageImportExecution bad;
    bad.verified = false;
    bad.targetLeftClean = true;
    bad.manifestEntries = 4;
    bad.verifiedEntries = 2;
    bad.totalBytes = 1024;
    bad.diagnostics = {PackageImportDiagnosticLine{"IO-SEC-PACK-PATH", "越界"}};
    const auto badLines = workflow::buildPackageImportReportView(bad);
    ASSERT_EQ(badLines.size(), 6u);
    EXPECT_EQ(badLines[0].valueText, "failed");
    EXPECT_EQ(badLines[4].labelKey,
              "wizard.package-import.report.diagnostics-count");
    EXPECT_EQ(badLines[4].valueText, "1");
    EXPECT_EQ(badLines[5].valueText, "yes");

    // —— canceled 态终态词形。
    PackageImportExecution cancelledState;
    cancelledState.cancelled = true;
    cancelledState.targetLeftClean = true;
    const auto cancelledLines =
        workflow::buildPackageImportReportView(cancelledState);
    EXPECT_EQ(cancelledLines[0].valueText, "canceled");

    // —— 确定性（NFR-COR-02 同型）：同输入双跑同输出。
    EXPECT_EQ(workflow::buildPackageImportReportView(bad), badLines);
}

/// IFlowCancelToken 接口消费钉扎：置位幂等、一经真值恒真（执行面轮询
/// 依赖的复位契约）。
TEST_F(WfSaveAsPackage, CancelToken_IdempotentLatching_P05)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-05"}, std::vector<std::string>{});
    MemoryCancelToken token;
    EXPECT_FALSE(token.cancelRequested());
    token.requestCancel();
    token.requestCancel();  // 幂等置位
    EXPECT_TRUE(token.cancelRequested());
    EXPECT_TRUE(token.cancelRequested());  // 一经真值恒真
}

}  // namespace
