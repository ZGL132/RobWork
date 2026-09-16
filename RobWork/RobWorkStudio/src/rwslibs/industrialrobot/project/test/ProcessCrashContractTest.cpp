/**
 * @file   ProcessCrashContractTest.cpp
 * @brief  真进程崩溃注入契约用例组（跨单元契约面）——PRJ-TX-4/F8 的
 *         进程级半边（TestProcessRunner kill 于事务步骤边界后重启恢复）
 *         与 PRJ-TX-8 的进程级半边（在途归档会话随宿主进程被强杀）。
 *
 * 设计依据：
 *   - units/project.md §12 PRJ-T15 行（产物：PRJ-TX-1～14 用例体——含
 *     TestProcessRunner 接入的 F8/PRJ-TX-7/8；验证方式：§11 表逐项）、
 *     §7.6 F8 行（"TestProcessRunner kill 于 T1～T7 每步后；重启：已提交
 *     字节不变；未提交忽略＋恢复报告字段逐项断言"——AT-11/13）、§7.4
 *     （恢复状态机：①暂存残留→忽略不删＋PRJ-RECOVERY-IGNORED-
 *     UNCOMMITTED；④闭包完整性；⑤悬挂计数）、§7.5（崩溃一致性要点：
 *     提交点＝HEAD 原子切换——提交点后崩溃＝新状态字节不变）、§11
 *     PRJ-TX-8 行（在途归档——强杀后无 manifest＝不完整，D-13）、§9.3
 *     （崩溃后写锁接管——D-02）；
 *   - units/testkit.md §6.5（TestProcessRunner/EventWatch 消费纪律：
 *     正确性判据只来自可观察事件——标记文件；禁止固定 sleep；kill＝
 *     主动崩溃注入；§6.4 与真进程崩溃的覆盖分工——文件系统故障验证
 *     逻辑鲁棒性〔TxEngineTest F1～F7〕，真进程崩溃验证进程隔离与恢复
 *     〔本组〕，两者不互相替代）、§10.1（触发登记：消费者 PRJ-T15）、
 *     §6.2（TempDir 资源隔离与失败保留）；
 *   - 需求 NFR-REL-01（事务边界：任一步中断旧版本完整）、PM-08（崩溃
 *     恢复报告）、AT-11（进程隔离与恢复）、AT-13（保存事务）、TASK-03/
 *     CON-04（归档完整性判据）。
 *
 * 与既有用例的关系（零重复声明）：TxEngineTest.F8_* 已钉住同边界的
 * 进程内语义（本组其复验载体——TxEngineTest 文件头登记"进程级 kill 归
 * PRJ-T15"）；本组唯一断言面＝真实 OS 进程被强杀后的磁盘事实与恢复
 * 报告（进程隔离：锁句柄泄漏、作业对象回收、跨进程接管）。
 *
 * 停靠协作协议：子进程半边见 Tx15Child.cpp（testkit FaultInterceptor 经
 * IFileOps 接缝停靠在 §7.1 步骤边界）；父进程（本文件）＝启动→
 * EventWatch.awaitFile 等标记→TestProcessRunner.kill 强杀→重启→断言。
 */

#include "Tx15Child.hpp"

// Codec.hpp＝src/ 私有实现头（R-2 只允许同单元测试目标消费——contract
// 目标已注入 src/ include 路径）：重启后 HEAD 字节的磁盘直读解析面。
#include "Codec.hpp"

#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include <sdurws/ird/testkit/Fixture.hpp>
#include <sdurws/ird/testkit/ProcessRunner.hpp>

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::project::BranchTip;
using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::OpenStoreResult;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::testkit::EventWatch;
using sdurws::ird::testkit::ProcessExitKind;
using sdurws::ird::testkit::ProcessOutcome;
using sdurws::ird::testkit::ProcessSpec;
using sdurws::ird::testkit::TempDir;
using sdurws::ird::testkit::TestProcessRunner;

namespace {

// ---------------------------------------------------------------------
// 磁盘快照辅助（TxEngineTest 同型——字节不变断言的基准集）
// ---------------------------------------------------------------------

/// 二进制整读（读不到＝显性失败，不留"读不到＝内容不符"的假阳性通道）。
std::string readAll(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取文件: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 递归收集目录下全部普通文件（相对根的路径→字节）——提交内容快照。
void collectFiles(const fs::path& root, const fs::path& base,
                  std::map<std::string, std::string>& out)
{
    std::error_code ec;
    if (!fs::exists(root, ec) || ec) {
        return;
    }
    for (fs::directory_iterator it(root, ec), end; it != end && !ec;
         it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code typeEc;
        if (it->is_directory(typeEc) && !typeEc) {
            collectFiles(it->path(), base, out);
        } else if (it->is_regular_file(typeEc) && !typeEc) {
            std::error_code relEc;
            const auto rel = fs::relative(it->path(), base, relEc);
            out[rel.string()] = readAll(it->path());
        }
    }
}

/// path → UTF-8 窄串（ProcessSpec.env 的契约编码——ProcessRunner 以
/// UTF-8 解释再转宽字符，非 ASCII 临时路径无损）。
std::string utf8(const fs::path& p)
{
    return p.u8string();
}

/// 恢复诊断码出现性（RecoveryReport.diagnostics 快照的用户级码扫描）。
bool hasRecoveryCode(const OpenStoreResult& opened, const std::string& code)
{
    for (const auto& record : opened.recovery.diagnostics) {
        if (record.code == code) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// F8 参数化：边界 token×断言族（未提交＝旧状态不变；已提交＝新状态不变）
// ---------------------------------------------------------------------

/// 单边界参数：停靠 token（Tx15Child.cpp 分派表）＋该边界崩溃后的预期族。
struct CrashBoundary {
    const char* park;   ///< 停靠 token（IRD_TX15_PARK 值）
    bool committed;     ///< true＝提交点（T5/T6）后崩溃——新状态在盘
};

/// 参数集（§7.6 F8"kill 于 T1～T7 每步后"的真进程覆盖；T2/T3 进程内
/// 同构合并——TxEngineTest 同口径；T7 完成后＝正常退出，无 kill 语义）。
const CrashBoundary kBoundaries[] = {
    {"T1", false},
    {"T2T3", false},
    {"T4", false},
    {"T5", true},
    {"T6", true},
};

}  // namespace

// =====================================================================
// 用例组：F8ProcessCrash（套件名登记入 gtest XML 与任务证据）
// =====================================================================

/**
 * 父进程夹具：每用例独立 TempDir（testkit §6.2 资源隔离）＋健康项目
 * （createNew 的 r0）＋子进程启动/等待/强杀辅助。TempDir 失败保留语义
 * 经 noteTestFailure(HasFailure()) 接缝接通（§6.2 keepOnFailure）。
 */
class F8ProcessCrash : public ::testing::TestWithParam<CrashBoundary> {
protected:
    void SetUp() override
    {
        // 用例专属临时目录（tag 仅 [A-Za-z0-9._-]——TempDir 契约）。
        m_tmp = std::make_unique<TempDir>("tx15-f8");
        m_tmp->keepOnFailure(true);
        m_dir = m_tmp->path() / "proj.rwdesign";

        // 前置：健康项目（r0）——可写打开后立即关闭（锁让给子进程）。
        OpenStoreResult opened
            = ProjectStoreFactory::createNew(m_dir, "f8 crash project");
        ASSERT_NE(opened.store, nullptr);
        ASSERT_TRUE(opened.store->writable());
        m_r0 = opened.store->query().head().id;
        ASSERT_EQ(opened.store->requestClose(), 0u);
        ASSERT_TRUE(opened.store->closed());

        // 已提交内容快照（HEAD＋revisions＋objects——旧状态字节基准）。
        collectFiles(m_dir / "HEAD", m_dir, m_before);
        collectFiles(m_dir / "revisions", m_dir, m_before);
        collectFiles(m_dir / "objects", m_dir, m_before);
        ASSERT_FALSE(m_before.empty()) << "快照为空——前置项目未就位";
    }

    void TearDown() override
    {
        if (m_tmp != nullptr) {
            m_tmp->noteTestFailure(::testing::Test::HasFailure());
        }
    }

    /// 启动子进程（mode＋park 经 ProcessSpec.env 传递——UTF-8 路径无损；
    /// timeout＝兜底回收上限，正常路径停靠后被 kill、不会到点）。
    void startChild(TestProcessRunner& runner, const char* mode,
                    const fs::path& marker, const char* park) const
    {
        ProcessSpec spec;
        wchar_t exePath[MAX_PATH];
        ASSERT_NE(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u)
            << "无法取自身可执行路径";
        spec.executable = exePath;
        spec.args = {"--ird-wp04-t15-child", mode};
        spec.env["IRD_TX15_DIR"] = utf8(m_dir);
        spec.env["IRD_TX15_MARKER"] = utf8(marker);
        if (park != nullptr) {
            spec.env["IRD_TX15_PARK"] = park;
        }
        spec.timeout = std::chrono::milliseconds(120000);
        ASSERT_NO_FATAL_FAILURE(runner.start(spec, {}));
    }

    std::unique_ptr<TempDir> m_tmp;   ///< 用例临时目录（析构递归清理）
    fs::path m_dir;                   ///< 项目 .rwdesign 目录
    sdurws::ird::core::RevisionId m_r0;  ///< 前置头修订（旧状态锚点）
    std::map<std::string, std::string> m_before;  ///< 已提交内容快照
};

/**
 * 锚定：§7.6 F8／NFR-REL-01／PM-08／AT-11/AT-13（真进程载体）。
 *
 * 前置：健康项目 r0（父进程创建后释放锁）；子进程在指定事务边界停靠
 * （标记文件＝可观察事件）。
 * 操作：启动子进程→EventWatch.awaitFile 等停靠标记→TestProcessRunner.
 * kill 强杀（outcome.Killed）→父进程重启打开（写锁接管——D-02 崩溃
 * 接管路径）→逐项断言。
 * 预期（未提交边界 T1/T2T3/T4）：
 *   - 旧状态字节不变（HEAD 文件字节∈快照且不变——提交点未过）；
 *   - 查询面 tip 仍＝r0（未提交修订对闭包不可见——§7.3）；
 *   - 恢复报告：完整性 true、暂存残留恰 1（忽略不删）、
 *     PRJ-RECOVERY-IGNORED-UNCOMMITTED 用户诊断在案、无 PRJ-STORE-CORRUPT。
 * 预期（已提交边界 T5/T6）：
 *   - 新状态字节在盘（HEAD 指向新修订、清单/命令文件就位）＋旧文件不变；
 *   - 查询面 tip＝新修订（已提交可见）；
 *   - 恢复报告：完整性 true、暂存残留恰 1（清理未跑——§7.5"目录存在
 *     不判定提交"的双向口径）、IGNORED-UNCOMMITTED 在案、无 CORRUPT。
 * 观测点：RecoveryReport 字段逐项＋HEAD/快照字节比对＋query().branchTips。
 */
TEST_P(F8ProcessCrash, KillAtBoundary_RecoveryReportAndByteInvariants)
{
    const CrashBoundary boundary = GetParam();

    // 子进程在目标边界停靠（标记＝可观察事件——禁止固定 sleep 的判据源）。
    // 失败时附子进程结果诊断（退出码/形态——分派违约 8/9、模式内失败
    // 3/4/5、未处理异常＝非零异常码，见 Tx15Child.cpp 错误语义表）。
    TestProcessRunner runner;
    const fs::path marker = m_tmp->path() / "parked.marker";
    ASSERT_NO_FATAL_FAILURE(
        startChild(runner, "crash-commit", marker, boundary.park));
    EventWatch watch;
    if (!watch.awaitFile(marker, std::chrono::milliseconds(60000))) {
        const ProcessOutcome failed = runner.outcome();
        FAIL() << "子进程未在 " << boundary.park
               << " 边界停靠（标记未出现）；child outcome kind="
               << static_cast<int>(failed.kind)
               << " exitCode=" << failed.exitCode;
    }

    // 崩溃注入：强杀进程树（AT-11 载体）——结果必须是 Killed 形态。
    runner.kill();
    const ProcessOutcome outcome = runner.outcome();
    EXPECT_EQ(outcome.kind, ProcessExitKind::Killed);

    // 重启＝父进程重新打开（子进程死亡遗留写锁——打开即走 D-02 接管）。
    OpenStoreRequest request;
    request.path = m_dir;
    OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);
    ASSERT_TRUE(reopened.store->writable())
        << "崩溃接管失败——死进程持有的锁未被夺取";

    // 恢复报告共同面：闭包完整性通过、暂存残留恰一（七步协议第 1 步
    // 建立的事务目录在 T7 前的任何边界都残留在盘）、无损坏误报。
    EXPECT_TRUE(reopened.recovery.headIntegrityVerified);
    std::string residueList;
    for (const auto& tx : reopened.recovery.ignoredStagingTxs) {
        residueList += tx + ";";
    }
    ASSERT_EQ(reopened.recovery.ignoredStagingTxs.size(), std::size_t{1})
        << "暂存残留清单: " << residueList;
    EXPECT_TRUE(hasRecoveryCode(reopened,
                                "PRJ-RECOVERY-IGNORED-UNCOMMITTED"));
    EXPECT_FALSE(hasRecoveryCode(reopened, "PRJ-STORE-CORRUPT"));

    if (!boundary.committed) {
        // ---- 未提交族：旧状态字节不变＋闭包外不可见 ----
        std::map<std::string, std::string> after;
        collectFiles(m_dir / "HEAD", m_dir, after);
        collectFiles(m_dir / "revisions", m_dir, after);
        collectFiles(m_dir / "objects", m_dir, after);
        for (const auto& [rel, bytes] : m_before) {
            const auto it = after.find(rel);
            ASSERT_TRUE(it != after.end()) << "已提交文件丢失: " << rel;
            EXPECT_EQ(it->second, bytes) << "已提交文件被改写: " << rel;
        }
        // 查询面：分支 tip 仍＝r0（未提交内容对会话索引不可见——§7.3；
        // T4 边界的已发布对象/修订目录是闭包外悬挂，不进查询）。
        const std::vector<BranchTip> tips
            = reopened.store->query().branchTips();
        ASSERT_FALSE(tips.empty());
        EXPECT_EQ(tips[0].tip, m_r0)
            << "未提交边界 " << boundary.park << " 的 tip 发生前移";
    } else {
        // ---- 已提交族：新状态在盘＋旧文件不变＋查询可见 ----
        const std::string headBytes = readAll(m_dir / "HEAD");
        ASSERT_FALSE(headBytes.empty());
        sdurws::ird::project::HeadRecord diskHead
            = sdurws::ird::project::codec::parseHeadRecord(headBytes);
        EXPECT_FALSE(diskHead.revisionId == m_r0)
            << "提交点后崩溃 HEAD 未前移（T5/T6 边界语义破坏）";
        // 新修订文件在盘：manifest.json＋command.json（§7.1 第 4 步产物）。
        const fs::path revDir
            = m_dir / "revisions" / diskHead.revisionId.toCanonical();
        EXPECT_TRUE(fs::exists(revDir / "manifest.json"));
        EXPECT_TRUE(fs::exists(revDir / "command.json"));
        // 旧文件（r0 全集）逐字节不变——提交只增不改（PA-2 的崩溃面）。
        std::map<std::string, std::string> after;
        collectFiles(m_dir / "revisions", m_dir, after);
        collectFiles(m_dir / "objects", m_dir, after);
        for (const auto& [rel, bytes] : m_before) {
            if (rel == "HEAD") {
                continue;  // HEAD 恰是切换目标——已在上方断言新值
            }
            const auto it = after.find(rel);
            ASSERT_TRUE(it != after.end()) << "已提交文件丢失: " << rel;
            EXPECT_EQ(it->second, bytes) << "已提交文件被改写: " << rel;
        }
        // 查询面：tip＝磁盘 HEAD 修订（重启后已提交事实对查询可见）。
        const std::vector<BranchTip> tips
            = reopened.store->query().branchTips();
        ASSERT_FALSE(tips.empty());
        EXPECT_EQ(tips[0].tip, diskHead.revisionId);
    }

    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

INSTANTIATE_TEST_SUITE_P(
    TransactionBoundaries, F8ProcessCrash,
    ::testing::ValuesIn(kBoundaries),
    [](const ::testing::TestParamInfo<CrashBoundary>& info) {
        // 用例名＝边界 token＋断言族（ASCII——gtest 名约束）。
        return std::string(info.param.park)
            + (info.param.committed ? "_Committed" : "_Uncommitted");
    });

// =====================================================================
// 用例组：Tx8ArchiveProcessKill（PRJ-TX-8 真进程半边）
// =====================================================================

class Tx8ArchiveProcessKill : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_tmp = std::make_unique<TempDir>("tx15-tx8");
        m_tmp->keepOnFailure(true);
        m_dir = m_tmp->path() / "proj.rwdesign";
        OpenStoreResult opened
            = ProjectStoreFactory::createNew(m_dir, "tx8 archive project");
        ASSERT_NE(opened.store, nullptr);
        m_r0 = opened.store->query().head().id;
        ASSERT_EQ(opened.store->requestClose(), 0u);
    }

    void TearDown() override
    {
        if (m_tmp != nullptr) {
            m_tmp->noteTestFailure(::testing::Test::HasFailure());
        }
    }

    void startChild(TestProcessRunner& runner, const char* mode,
                    const fs::path& marker) const
    {
        ProcessSpec spec;
        wchar_t exePath[MAX_PATH];
        ASSERT_NE(::GetModuleFileNameW(nullptr, exePath, MAX_PATH), 0u);
        spec.executable = exePath;
        spec.args = {"--ird-wp04-t15-child", mode};
        spec.env["IRD_TX15_DIR"] = utf8(m_dir);
        spec.env["IRD_TX15_MARKER"] = utf8(marker);
        spec.timeout = std::chrono::milliseconds(120000);
        ASSERT_NO_FATAL_FAILURE(runner.start(spec, {}));
    }

    std::unique_ptr<TempDir> m_tmp;
    fs::path m_dir;
    sdurws::ird::core::RevisionId m_r0;
};

/**
 * 锚定：PRJ-TX-8 真进程半边（在途归档＋进程强杀）／D-13／CON-04／AT-11。
 *
 * 前置：健康项目；子进程 begin 归档会话＋writeBatch（不 finalize）后
 * 停靠（标记携带 run 规范文本）。
 * 操作：等标记→强杀→重启打开→断言。
 * 预期：
 *   - results/<run>/inflight-result.json 在盘（批次文件残留——强杀时
 *     已写完的批次不回滚）；
 *   - results/<run>/manifest.json 不在（finalize 未达——D-13"manifest
 *     在＝运行完整"的反例面）；
 *   - 查询面 listRuns(r0) 为空（无 manifest＝不完整，不列入运行清单）；
 *   - 恢复报告干净：完整性 true、零暂存残留（归档不走 .staging 事务
 *     目录）、零悬挂对象（批次文件不在对象库编址内）。
 */
TEST_F(Tx8ArchiveProcessKill, KillWhileArchiveInFlight_NoManifestRunNotListed)
{
    TestProcessRunner runner;
    const fs::path marker = m_tmp->path() / "archive.marker";
    ASSERT_NO_FATAL_FAILURE(startChild(runner, "archive-inflight", marker));

    EventWatch watch;
    ASSERT_TRUE(watch.awaitLineInFile(marker, "run=",
                                      std::chrono::milliseconds(60000)))
        << "子进程未到达在途归档停靠点";
    runner.kill();
    EXPECT_EQ(runner.outcome().kind, ProcessExitKind::Killed);

    // 从标记解析 run 规范文本（子进程生成——父进程不重复推导，A8 口径）。
    const std::string markerText = readAll(marker);
    const auto pos = markerText.find("run=");
    ASSERT_NE(pos, std::string::npos);
    std::string runId = markerText.substr(pos + 4);
    while (!runId.empty() && (runId.back() == '\n' || runId.back() == '\r')) {
        runId.pop_back();
    }
    ASSERT_FALSE(runId.empty());

    // 重启打开（写锁接管——与 F8 同一崩溃现场形态）。
    OpenStoreRequest request;
    request.path = m_dir;
    OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);
    ASSERT_TRUE(reopened.store->writable());

    // 磁盘事实：批次文件残留、manifest 缺失（D-13 反例）。
    const fs::path runDir = m_dir / "results" / runId;
    EXPECT_TRUE(fs::exists(runDir / "inflight-result.json"))
        << "强杀前已写入的批次文件应残留（写通道不回滚）";
    EXPECT_FALSE(fs::exists(runDir / "manifest.json"))
        << "未 finalize 的运行不得出现 manifest（D-13 完整判据）";

    // 查询面：不完整运行不列入清单（CON-04——不作为正式缓存命中）。
    EXPECT_TRUE(reopened.store->query().listRuns(m_r0).empty());

    // 恢复报告干净（归档残留不进恢复语义——完整性/暂存/悬挂三维）。
    EXPECT_TRUE(reopened.recovery.headIntegrityVerified);
    EXPECT_TRUE(reopened.recovery.ignoredStagingTxs.empty());
    EXPECT_EQ(reopened.recovery.danglingObjectCount, 0u);

    EXPECT_EQ(reopened.store->requestClose(), 0u);
}
