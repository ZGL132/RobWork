/**
 * @file   CrashCaptureTest.cpp
 * @brief  壳侧崩溃捕获发射核心的模型面用例组（WP-24-T02/PM-17/AT-11）——
 *         CrashReportInput 六段组装、防重入闸门、降级路径的进程内直测。
 *
 * 设计依据：
 *   - units/diagnostics.md §7.6（崩溃诊断文件六段内容/写出位置/失败降级
 *     ——捕获点组装契约）、§12.1 协作行（WP-24-T02 承接捕获与应用壳
 *     装配）；任务契约 tasks/foundation/WP-24-T02.json acceptance 4
 *     （"捕获装配逻辑单测：输入组装六段、降级路径"）；
 *   - 需求 PM-17（异常退出写出诊断文件）、NFR-SEC-07（脱敏——Writer 侧
 *     统一执行，本组验证捕获路径把原文交给 Writer 后产物脱敏）。
 *
 * 落点归属说明（契约 acceptance 4 的"按落点归属选择"）：发射核心
 * emitCrashReportTo 是壳侧（ui/studio）设施——落 sdurws_ird_ui_test
 * （无界面模型测试层，QCoreApplication 级；设施零 Qt，双模式可跑）。
 * 进程级半边（子进程真崩→文件真落盘）归 sdurws_ird_ui_contract_test
 * （StudioCrashContractTest——与 project 单元 ProcessCrashContractTest
 * 同款分层：进程级契约属跨进程契约面）。
 *
 * 测试形态说明：本组不调用 installCrashCapture（进程级全局态＋OS 处置
 * 链注册不属模型面可测范围——OS 回调链半边由进程级契约用例在真子进程
 * 内验证）；经 makeCrashCaptureContext 构造独立上下文直测发射核心，
 * 上下文间互不污染（防重入闸门在上下文内——每用例新鲜闸门）。
 */

#include "../studio/CrashCapture.hpp"

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>          // ProjectId/TaskIdentity 生成
#include <sdurws/ird/diagnostics/Confirmable.hpp> // FindingId 生成（快照段构数）

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::AttemptId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::TaskIdentity;
using sdurws::ird::diagnostics::CrashReportActiveFinding;
using sdurws::ird::diagnostics::FindingId;
using sdurws::ird::diagnostics::FindingState;
using sdurws::ird::ui::studio::CrashContextSegments;
using sdurws::ird::ui::studio::CrashCaptureContext;
using sdurws::ird::ui::studio::CrashEmitStatus;
using sdurws::ird::ui::studio::CrashReportSnapshotSource;
using sdurws::ird::ui::studio::emitCrashReportTo;
using sdurws::ird::ui::studio::makeCrashCaptureContext;

namespace {

// ---------------------------------------------------------------------
// 辅助（每个用例独立目录——互不污染；发射核心对目录无前提假设）
// ---------------------------------------------------------------------

/// 唯一临时目录（进程 PID＋进程内递增——跨运行不复用同名目录：文件计数
/// 断言以目录内容为准，目录复用会让上一运行的产物污染本运行的计数）。
fs::path uniqueCaptureDir(const char* tag)
{
    static int counter = 0;
    const fs::path base = fs::temp_directory_path()
                          / ("ird-t24-crashcap-" + std::string(tag)
                             + "-" + std::to_string(::GetCurrentProcessId())
                             + "-" + std::to_string(++counter));
    fs::create_directories(base);
    return base;
}

/// 读文本文件全量（不存在＝空串——调用方以断言裁决）。
std::string readFileOrEmpty(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 目录下 crash-*.log 文件清单（文件数断言用——防重入的观测面）。
std::vector<fs::path> listCrashFiles(const fs::path& dir)
{
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir), end; it != end; ++it) {
        const std::string name = it->path().filename().string();
        if (name.rfind("crash-", 0) == 0 && name.size() > 4
            && name.substr(name.size() - 4) == ".log") {
            files.push_back(it->path());
        }
    }
    return files;
}

/// 合法任务五元组（TASK-03——isValid 五字段全真；工厂即时生成保证唯一）。
TaskIdentity makeValidTask()
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = sdurws::ird::core::BranchId::generate();
    task.revision = sdurws::ird::core::RevisionId::generate();
    task.run = sdurws::ird::core::RunId::generate();
    task.attempt = AttemptId{1};
    return task;
}

/// 固定段快照源（测试自持替身——返回构造注入的段；可注入发射重入行为）。
class StubSnapshotSource final : public CrashReportSnapshotSource {
public:
    CrashContextSegments snapshot() override
    {
        // 重入探针：命中时在快照采集内部再次发射（模拟"报告路径内二次
        // 崩溃"——闸门必须拒绝该次发射，外层照常完成）。
        ++snapshotCalls;
        if (reenter) {
            reenterStatus = emitCrashReportTo(*ctx, "nested-crash", "nested");
        }
        return segments;
    }

    CrashContextSegments segments;   ///< 注入段（六段中运行期四段）
    CrashCaptureContext* ctx = nullptr;  ///< 重入发射目标（测试接线）
    bool reenter = false;            ///< true＝snapshot() 内再发射
    CrashEmitStatus reenterStatus = CrashEmitStatus::NotInstalled;  ///< 重入发射结果
    int snapshotCalls = 0;           ///< 采集调用计数（重入观测面）
};

}  // namespace

// =====================================================================
// 用例组：捕获发射核心（六段组装/降级/防重入）
// =====================================================================

/**
 * 锚定：WP-24-T02 acceptance 4"捕获装配逻辑单测——输入组装六段"／§7.6
 * 六段内容／P-DIAG-1（五元组原样内嵌——canonical 文本进文件）。
 *
 * 前置：注入满四段的快照源（dev 尾两行/活动任务一/活动 finding 一/打开
 * 项目一）。操作：构造上下文→emitCrashReportTo。预期：Written；目录恰
 * 一个 crash-*.log；六段逐段在案——format 首行、process、exceptionType/
 * exceptionMessage 原文形态、task[0]＝五段 canonical '/' 连接、finding[0]
 * ＝fnd- 规范串＋state、dev[0..1] 逐行在案、project＝prj- 规范串。
 */
TEST (CrashCapture, EmitAssemblesSixSegmentsIntoKeyValueFile_PM17_S76)
{
    // ---- 前置：满四段快照源＋独立上下文 ----
    StubSnapshotSource source;
    source.segments.devLogTail = {"第一行 dev 日志", "second dev line"};
    source.segments.activeTasks = {makeValidTask()};
    source.segments.activeFindings = {{FindingId::generate(), FindingState::Pending}};
    const ProjectId openProject = ProjectId::generate();
    source.segments.openProject = openProject;

    const fs::path dir = uniqueCaptureDir("six");
    std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
        dir, "ird-studio-test x64", &source);

    // ---- 操作：发射（类型＋消息——Writer 侧脱敏面由诊断单元 RedactionTest
    //      钉住，此处验证组装链原文可达 Writer）----
    const CrashEmitStatus status
        = emitCrashReportTo(*ctx, "seh-access-violation", "code=0xC0000005");
    ASSERT_EQ(status, CrashEmitStatus::Written);

    // ---- 文件存在性与六段断言 ----
    const std::vector<fs::path> files = listCrashFiles(dir);
    ASSERT_EQ(files.size(), std::size_t{1}) << "应恰产出一份崩溃诊断文件";
    const std::string text = readFileOrEmpty(files[0]);
    ASSERT_FALSE(text.empty());

    // 键值文本逐段核对（§7.6 顺序：format/进程/异常/项目/任务/finding/dev 尾）。
    EXPECT_NE(text.find("format=ird-crash-report/1"), std::string::npos)
        << "首行自描述标记（§1.4 键值格式约定）";
    EXPECT_NE(text.find("process=ird-studio-test x64"), std::string::npos)
        << "第①段进程/版本信息";
    EXPECT_NE(text.find("exceptionType=seh-access-violation"), std::string::npos)
        << "第②段异常类型";
    EXPECT_NE(text.find("exceptionMessage=code=[ADDR]"), std::string::npos)
        << "第②段异常消息（safeSummary＝User 档——消息内 0x 十六进制地址"
           "按内部模式替换 [ADDR]，报告面不带地址——NFR-REL-05）";
    EXPECT_EQ(text.find("0xC0000005"), std::string::npos)
        << "消息内十六进制地址原文不得出现在报告面";
    EXPECT_NE(text.find("project=" + openProject.toCanonical()), std::string::npos)
        << "第⑥段项目身份（canonical 规范串——P-DIAG-1 原样内嵌）";
    EXPECT_NE(text.find("activeTasks=1"), std::string::npos) << "第④段计数行";
    EXPECT_NE(text.find("task[0]=" + source.segments.activeTasks[0].project.toCanonical()
                        + "/"), std::string::npos)
        << "第④段任务五元组（'/' 连接 canonical——与日志 ids=task: 同形）";
    EXPECT_NE(text.find("activeFindings=1"), std::string::npos) << "第⑤段计数行";
    EXPECT_NE(text.find("finding[0]=" + source.segments.activeFindings[0].id.toCanonical()
                        + " state=pending"),
              std::string::npos)
        << "第⑤段 finding 身份＋状态 token（不含载荷——§7.6）";
    EXPECT_NE(text.find("devLogTail=2"), std::string::npos) << "第③段计数行";
    EXPECT_NE(text.find("dev[0]=第一行 dev 日志"), std::string::npos)
        << "第③段快照行（写入序）";
    EXPECT_NE(text.find("dev[1]=second dev line"), std::string::npos) << "第③段快照行";
}

/**
 * 锚定：WP-24-T02 acceptance 4"输入组装六段"的空段面／§7.6"壳侧能取到
 * 的段如实填，取不到的段留空"（不伪造数据）。
 *
 * 前置：无快照源（产品 StudioMain 装捕获的初始形态——会话栈未接线）。
 * 预期：Written；运行期四段计数为零、身份段为 "-"（§7.6 既有 Writer 的
 * 空项目形态）；进程/异常两段照常在案。
 */
TEST (CrashCapture, EmitWithoutSourceLeavesRuntimeSegmentsEmpty_HonestEmpty)
{
    const fs::path dir = uniqueCaptureDir("empty");
    std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
        dir, "ird-studio-test x64", nullptr);  // 无快照源——四段如实空

    ASSERT_EQ(emitCrashReportTo(*ctx, "crt-abort", "crt-termination-signal"),
              CrashEmitStatus::Written);

    const std::vector<fs::path> files = listCrashFiles(dir);
    ASSERT_EQ(files.size(), std::size_t{1});
    const std::string text = readFileOrEmpty(files[0]);
    EXPECT_NE(text.find("process=ird-studio-test x64"), std::string::npos);
    EXPECT_NE(text.find("exceptionType=crt-abort"), std::string::npos);
    EXPECT_NE(text.find("project=-"), std::string::npos) << "无打开项目＝'-'占位";
    EXPECT_NE(text.find("activeTasks=0"), std::string::npos) << "空段＝计数 0";
    EXPECT_NE(text.find("activeFindings=0"), std::string::npos) << "空段＝计数 0";
    EXPECT_NE(text.find("devLogTail=0"), std::string::npos) << "空段＝计数 0";
    EXPECT_EQ(text.find("task[0]="), std::string::npos) << "不伪造任务行";
}

/**
 * 锚定：WP-24-T02 acceptance 1/4——"捕获路径防重入"（一次性闸门）。
 *
 * 前置：快照源注入重入行为（snapshot() 内对同一上下文再次发射）。操作：
 * 外层发射。预期：重入次发射被闸门拒绝（GateClosed——不产生第二份文件），
 * 外层照常 Written；目录恰一个 crash-*.log（闸门一次性语义的观测面）。
 */
TEST (CrashCapture, ReentrantEmitIsRejectedByOneShotGate_Acceptance1)
{
    const fs::path dir = uniqueCaptureDir("gate");
    std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
        dir, "ird-studio-test x64", nullptr);

    StubSnapshotSource source;
    source.ctx = ctx.get();
    source.reenter = true;  // snapshot() 内再发射——报告路径内二次崩溃形态
    ctx->source.store(&source);

    const CrashEmitStatus outer = emitCrashReportTo(*ctx, "outer-crash", "outer");
    EXPECT_EQ(outer, CrashEmitStatus::Written) << "外层发射不受重入影响";
    EXPECT_EQ(source.reenterStatus, CrashEmitStatus::GateClosed)
        << "重入次发射被一次性闸门拒绝";
    EXPECT_EQ(source.snapshotCalls, 1) << "快照采集恰一次（闸门在采集之前）";
    EXPECT_EQ(listCrashFiles(dir).size(), std::size_t{1}) << "只产出一份诊断文件";

    // 闸门一次性：后续任何发射（模拟崩溃后异常窗口的再次进入）均拒绝。
    EXPECT_EQ(emitCrashReportTo(*ctx, "late-crash", "late"),
              CrashEmitStatus::GateClosed);
    EXPECT_EQ(listCrashFiles(dir).size(), std::size_t{1});
}

/**
 * 锚定：WP-24-T02 acceptance 1/4——"降级路径"（写失败→状态可观测、
 * 不抛、进程存活）／§7.6"写出失败→stderr 摘要＋放弃"。
 *
 * 前置：目录取一个**已存在的文件**路径（create_directories/appendLine
 * 必败的确定性坏目录——不依赖注入设施，磁盘事实即注入）。预期：返回
 * WriteFailed（非 Written）；全程无异常逃逸；目录内无 crash-*.log
 * （Writer 逐行失败即停——部分文件也不会以合法名出现）。
 */
TEST (CrashCapture, WriteFailureDegradesWithoutThrow_DT_LIFE4)
{
    // 坏目录：以"文件"占位目录位置——std::filesystem 写路径确定失败。
    const fs::path blocker = uniqueCaptureDir("blocker") / "occupied";
    { std::ofstream placeholder(blocker); placeholder << "not-a-dir"; }

    std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
        blocker, "ird-studio-test x64", nullptr);
    StubSnapshotSource source;  // 快照源正常供给——失败点在磁盘写
    ctx->source.store(&source);

    // noexcept 契约的进程内实证：调用返回而非抛出（抛出即用例崩——
    // gtest 进程级失败，观测面充分）。
    const CrashEmitStatus status
        = emitCrashReportTo(*ctx, "seh-access-violation", "code=0xC0000005");
    EXPECT_EQ(status, CrashEmitStatus::WriteFailed) << "写失败显性可观测";
    EXPECT_FALSE(fs::exists(blocker / "crash-anything.log"));
}

/**
 * 锚定：WP-24-T02 acceptance 4——降级路径的"快照源内部失败"半区。
 *
 * 前置：快照源 snapshot() 抛异常（源实现契约允许——设施必须隔离）。
 * 预期：发射返回 Written（段缺失不击穿报告路径），文件在案且运行期
 * 四段为空——"尽力而为、不伪造"的降级语义。
 */
TEST (CrashCapture, SnapshotSourceFailureDoesNotBreakReportPath)
{
    class ThrowingSource final : public CrashReportSnapshotSource {
    public:
        CrashContextSegments snapshot() override { throw std::runtime_error("source boom"); }
    };

    const fs::path dir = uniqueCaptureDir("srcfail");
    std::unique_ptr<CrashCaptureContext> ctx = makeCrashCaptureContext(
        dir, "ird-studio-test x64", nullptr);
    ThrowingSource source;
    ctx->source.store(&source);

    EXPECT_EQ(emitCrashReportTo(*ctx, "cpp-terminate", "uncaught"),
              CrashEmitStatus::Written)
        << "快照源失败按段缺失处理——报告照常写出";
    const std::string text = readFileOrEmpty(listCrashFiles(dir).at(0));
    EXPECT_NE(text.find("devLogTail=0"), std::string::npos) << "段缺失＝如实留空";
    EXPECT_NE(text.find("exceptionType=cpp-terminate"), std::string::npos);
}
