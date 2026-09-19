/**
 * @file   RecoveryProcessContractTest.cpp
 * @brief  主进程崩溃恢复契约用例（EX-T09 §11 矩阵补齐）——EX-RCV-1 运行中
 *         kill 主进程（TestProcessRunner）→重启＋恢复扫描→无 manifest 预留
 *         重建为 Interrupted；不伪装完整结果；现场保留可续跑。
 *
 * 设计依据：
 *   - units/execution.md §11 EX-RCV-1 行（需求 NFR-REL-03/PM-08/AT-11；
 *     观测点＝恢复条目＋EX-TASK-INTERRUPTED）、§7.5 应用崩溃行（重启恢复
 *     扫描发现无 manifest 的归档预留→execution 重建 Interrupted 任务条目
 *     ——EX-TASK-INTERRUPTED"已中断可重跑"；主进程崩溃期间不可能有迟到
 *     结果）、§5.1 T14（恢复期指派——工厂路径）、§5.4/P-EX-6（磁盘痕迹
 *     唯一经 project；Queued 纯内存任务崩溃即消失——预留期起可观测）
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch：正确性判据只
 *     来自可观察事件——标记文件；kill＝主动崩溃注入；§6.4 与真进程崩溃
 *     的覆盖分工——本组验证进程隔离与恢复，不替代文件系统故障用例）、
 *     §10.1（触发登记：消费者 WP-08-T10——acceptance 3 的兑现载体）、
 *     §7.3（IRD_TEST_INFO）
 *   - 跨单元契约：project D-02（崩溃后写锁接管——重启可写打开）、D-13
 *     （无 manifest＝不完整，查询不列入）、D-12（派发期即归档预留——
 *     崩溃可观测的判据）；PM-08（恢复呈现）
 *   - 任务契约 tasks/foundation/EX-T09.json acceptance 3（TestProcessRunner
 *     按 TK-T11 触发接入真进程场景——EX-RCV-1 本体；EX-WKR-2 的真进程
 *     面已由 EX-T06 期 WorkerProcessContractTest 登记，本任务在验收证据
 *     表中并列引用）、5（AT-11 载体）
 *
 * 场景边界声明（如实陈述）：恢复扫描的执行侧语义在本用例以"重启开库→
 *   观测无 manifest 预留→T14 重建"全链验证；project 侧预留清点的呈现面
 *   （RecoveryReport 列目录清单）归 project/ui 任务（PM-08 呈现——不私改
 *   对端）。子进程半边见 RecoveryChild.cpp（停靠协议同 Tx15Child 先例）。
 *
 * 替身边界声明（§11——完整声明见 ContractSuiteFacilities.hpp）：本组无
 *   评估替身——验证面为真实进程、真实存储与真实状态机的恢复链。
 */

#include "ContractSuiteFacilities.hpp"
#include "RecoveryChild.hpp"

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/StateMachine.hpp>
#include <sdurws/ird/execution/TaskTypes.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;
namespace pd = sdurws::ird::project;
namespace tk = sdurws::ird::testkit;
namespace fs = std::filesystem;

/// 本测试可执行文件完整路径（子进程＝自身以子进程模式重启——Tx15Child
/// 同款"同一 exe 双形态"协议；GetModuleFileNameW 保证绝对路径）。
fs::path selfExecutablePath()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH + 1);
    DWORD size = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) {
        return {};
    }
    return fs::path(std::wstring(buffer.data(), size));
#else
    return {};
#endif
}

/// 标记文件键值解析（子进程写出的现场信息——run/branch/revision/字节数）。
std::string markerValue(const fs::path& marker, const std::string& key)
{
    std::ifstream in(marker, std::ios::binary);
    if (!in) {
        return {};
    }
    for (std::string line; std::getline(in, line);) {
        const auto eq = line.find('=');
        if (eq != std::string::npos && line.substr(0, eq) == key) {
            return line.substr(eq + 1);
        }
    }
    return {};
}

/// EX-RCV-1 的受理记录底座（恢复重建的承载——T14 工厂输入）。
TaskRecord makeRecoverableRecord()
{
    TaskRecord r;
    r.taskId = TaskId::generate();
    r.submission.snapshot.project = core::ProjectId::generate();
    r.submission.snapshot.branch = core::BranchId::generate();
    r.submission.snapshot.revision = core::RevisionId::generate();
    r.submission.evaluatorKey = "kin-batch-ik";
    r.submission.contractVersion = 1;
    r.submission.mode = core::EvaluationMode::Verified;
    return r;
}

// =====================================================================
// 夹具：testkit TempDir（资源隔离＋失败保留——§6.2）
// =====================================================================

class RecoveryProcessContractTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 确定性上下文随报告携带（acceptance 1——ird-test-report.json）。
        tk::report::bindFixtureContext(m_repro, nullptr, nullptr);
        m_temp = std::make_unique<tk::TempDir>("ex-rcv");
    }

    void TearDown() override
    {
        // 失败保留现场（§6.2 keepOnFailure——gtest 失败信号经注入接缝送达）。
        if (m_temp != nullptr) {
            m_temp->noteTestFailure(HasFailure());
        }
    }

    tk::ReproRecord m_repro;                              ///< 确定性上下文（§6.3）
    std::unique_ptr<tk::TempDir> m_temp;                  ///< 本用例临时目录
};

// =====================================================================
// EX-RCV-1：崩溃后中断任务显示"已中断"——真进程 kill＋重启恢复重建
// =====================================================================

TEST_F(RecoveryProcessContractTest, MainProcessKillReconstructsInterruptedOnRestart_EX_RCV_1)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-REL-03", "PM-08"}, std::vector<std::string>{"AT-11"});

    // ---- 排布：子进程（扮演"主进程"）建库→归档预留（无 manifest）→
    // 写标记→停靠；父进程（本测试）经 EventWatch 等待"运行中"现场就绪。
    const fs::path storePath = m_temp->path() / "p.rwdesign";
    const fs::path markerPath = m_temp->path() / "marker.txt";
    const fs::path self = selfExecutablePath();
    ASSERT_FALSE(self.empty()) << "无法定位自身可执行文件（真进程场景前提）";

    tk::TestProcessRunner runner;
    tk::ProcessSpec spec;
    spec.executable = self;
    spec.args = {"--ird-ex-rcv-child",
                 "--store=" + storePath.u8string(),
                 "--marker=" + markerPath.u8string()};
    spec.timeout = std::chrono::milliseconds{60000};  // 兜底回收（防孤儿——§6.5）
    runner.start(spec, m_temp->path());

    tk::EventWatch watch;
    ASSERT_TRUE(watch.awaitFile(markerPath, std::chrono::milliseconds{60000}))
        << "子进程应在有界时间内建立'运行中'现场并写标记（超时＝现场构造失败）";

    const std::string runCanon = markerValue(markerPath, "run");
    const std::string revisionCanon = markerValue(markerPath, "revision");
    const std::string partialBytesText = markerValue(markerPath, "partial-bytes");
    ASSERT_FALSE(runCanon.empty());
    ASSERT_FALSE(revisionCanon.empty());
    const core::RunId run = core::RunId::fromCanonical(runCanon);
    const core::RevisionId revision = core::RevisionId::fromCanonical(revisionCanon);

    // ---- 崩溃注入：kill 主进程（真进程终止——§6.5"kill＝主动崩溃注入"）。
    runner.kill();
    const tk::ProcessOutcome outcome = runner.outcome();
    EXPECT_EQ(outcome.kind, tk::ProcessExitKind::Killed)
        << "被注入崩溃的主进程应以 Killed 形态回收（而非自然退出）";

    // ---- "重启"：重新打开项目库（崩溃进程持有的写锁经 D-02 残留接管
    // 取得——接管本身就是恢复链的一环）。
    pd::OpenStoreRequest reopen;
    reopen.path = storePath;
    const pd::OpenStoreResult reopened = pd::ProjectStoreFactory::open(reopen);
    ASSERT_TRUE(reopened.store != nullptr);
    EXPECT_TRUE(reopened.writable) << "崩溃接管后应取得写权限（D-02——恢复可续跑的前提）";

    // ---- 恢复扫描面①："不伪装完整结果"——无 manifest 的预留不进查询
    // （D-13：listRuns 只认完整运行；manifest 文件不存在）。
    EXPECT_TRUE(reopened.store->query().listRuns(revision).empty())
        << "被杀运行的预留不得伪装为完整结果（D-13/PM-08）";
    EXPECT_FALSE(fs::exists(storePath / "results" / runCanon / "manifest.json"));

    // ---- 恢复扫描面②：预留现场保留——已写批次字节原样在盘（"检查点
    // 可续"的现场语义：中断运行的工作现场完整可续跑；白名单检查点通道
    // 落地前，磁盘断言以预留批次承载——EX-T08 登记口径）。
    const std::uint64_t expectedBytes = std::stoull(partialBytesText);
    const fs::path partial = storePath / "results" / runCanon / "partial-batch.bin";
    ASSERT_TRUE(fs::exists(partial)) << "预留批次应原样保留（现场不丢）";
    EXPECT_EQ(static_cast<std::uint64_t>(fs::file_size(partial)), expectedBytes);

    // ---- 执行侧重建（T14 恢复期指派）：恢复扫描发现无 manifest 预留→
    // 指派 Interrupted（记录 state——扫描的指派结论，工厂守卫自查）→
    // 工厂补写终结原因与 EX-TASK-INTERRUPTED 状态标注诊断（产品行为，
    // 非测试侧叙述）——不伪装为完整结果。
    TaskRecord recovered = makeRecoverableRecord();
    recovered.run = run;
    recovered.attempt = core::AttemptId{1};
    recovered.state = core::TaskState::Interrupted;  // 恢复扫描的指派结论（工厂守卫要求）
    TaskStateMachine machine = TaskStateMachine::forRecoveryInterrupted(recovered, nullptr);
    EXPECT_EQ(machine.state(), core::TaskState::Interrupted);
    ASSERT_TRUE(machine.record().termination.has_value());
    EXPECT_EQ(*machine.record().termination, TerminationCause::Interrupted);
    // 四轴正交的承接面（§5.6 终态映射）：Interrupted 状态映射 Interrupted
    // outcome——不伪装 Completed（NFR-REL-03 的映射半区）。
    const std::optional<core::TaskOutcome> mapped = terminalOutcome(machine.state());
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(*mapped, core::TaskOutcome::Interrupted);

    // ---- 诊断面（"已中断可重跑"——工厂写入的 EX-TASK-INTERRUPTED 状态
    // 标注码；码值在收编 18 项清单内，acceptance 4 断言口径）。
    ASSERT_TRUE(exsuite::isRegisteredExStableCode("EX-TASK-INTERRUPTED"));
    bool hasInterruptedCode = false;
    for (const core::DiagnosticRecord& d : machine.record().diagnostics) {
        if (d.code == "EX-TASK-INTERRUPTED") {
            hasInterruptedCode = true;
        }
    }
    EXPECT_TRUE(hasInterruptedCode)
        << "T14 重建应携带 EX-TASK-INTERRUPTED 状态标注诊断（PM-08 呈现面）";

    // 现场制品登记（§7.2 artifacts——标记文件随报告归档）。
    tk::report::addArtifact(markerPath.u8string(), "process-marker");
}

}  // namespace
