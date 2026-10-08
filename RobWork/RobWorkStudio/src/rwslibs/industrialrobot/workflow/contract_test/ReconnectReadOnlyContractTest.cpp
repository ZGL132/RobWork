/**
 * @file   ReconnectReadOnlyContractTest.cpp
 * @brief  旧格式拒绝/只读打开/外部源重关联编排的契约测试（WF-VER-207/
 *         208/216——units/workflow.md §11.2 生命周期主线；PM-06/07/09
 *         ＋AT-20/21 的真实落盘承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.5（旧格式/未来版本＝稳定只读拒绝＋诊断码
 *     PRJ-FORMAT-LEGACY/PRJ-SCHEMA-FUTURE＋升级指引〔当前版本/项目版本/
 *     升级工具入口，不自动升级〕＋原文件不动；只读打开＝锁被持提示 PID、
 *     writable=false 禁编辑与应用提交、双实例第二写者不阻塞等待；重关联
 *     ＝重新关联入口＋显式提交产生新修订＋流程失败不动当前项目）、§11.2
 *     （207 观测点 PRJ-FORMAT-LEGACY、208 观测点只读状态、216 观测点
 *     修订）、§11.3 AT-20/21 承接行
 *   - REQUIREMENTS.md §17 PM-06/PM-07/PM-09 原文、AT-20（旧格式拒绝/
 *     原文件不动）、AT-21（重关联显式提交产生新修订）
 *   - project.md §5.1（open ②步 FormatLegacy/SchemaFuture 稳定拒绝＋
 *     升级指引 detail 三键；锁半步 HeldByOther 降级 ReadOnly）、§9.2
 *     （第二实例读 PID）、§9.3（进程内重复 Writable 直接拒绝——防自我
 *     双写，降级路径只在 OS 层锁竞争触达）、§9.6（只读上下文写入口
 *     统一门卫拒绝）
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：与 project 真实存储
 * 实现（ProjectStoreFactory::createNew/open＋真实 lock 文件＋真实命令
 * 七步事务）联合。旧格式/未来版本项目以篡改 project.json schemaVersion
 * 构造（真实打开②步版本判定路径）；外部持锁以 Win32 独占句柄模拟
 * （CreateFileW 共享模式仅 FILE_SHARE_READ——同 StoreLock 原语，内核
 * 裁决 ERROR_SHARING_VIOLATION→HeldByOther 降级，同 project 单元
 * ProjectStoreTest 降级用例口径；不经工厂注册表——§9.3 进程内重复
 * Writable 走直接拒绝而非降级）；重关联提交经真实 store.commands().
 * submit()（测试处理器 wf-contract-relink-seed——无点 token，P-PR-9
 * 未裁决不私置含点词形；handlerRegistry 装配通道——UI-T39 公共落位）。
 *
 * "原文件不动""失败不动当前项目"不是声明：以目录树字节面快照逐文件
 * 复核（snapshotTree——排除 lock 心跳重写面）＋branchTips/HEAD 双观测。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Identity.hpp>              // core::RevisionId（新修订断言）
#include <sdurws/ird/project/CommandService.hpp>     // project::CommandEnvelope/ICommandHandler（重关联显式提交——①端口）
#include <sdurws/ird/project/DraftService.hpp>       // project::DraftDocument（只读写轨拒绝断言）
#include <sdurws/ird/project/QueryPort.hpp>          // project::BranchTip（tip 前进观测）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <windows.h>  // CreateFileW/CloseHandle（外部持锁模拟——内核裁决面）

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::ExternalSourceState;
using workflow::ExternalSourceStatus;
using workflow::RelinkDisposition;
using workflow::RelinkOutcome;
using workflow::RelinkRequest;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立目录——SaveAsPackageContract 同型）
// =====================================================================

class ReconnectReadOnlyContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_reconnect_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);  // 失败保留现场惯例——总根清理
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 目录树字节面快照（相对路径 UTF-8 → 文件大小）。排除 lock 文件：
    /// 持有方心跳线程按固定周期原地重写（project.md §9.4——内容变化是
    /// 持有中的正常事实；SaveAsPackageContract::snapshotTree 同款口径）。
    static std::map<std::string, std::uintmax_t> snapshotTree(const fs::path& root)
    {
        std::map<std::string, std::uintmax_t> snapshot;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            if (it->path().filename().string() == "lock") {
                continue;  // 心跳重写面排除（见函数注）
            }
            snapshot[it->path().lexically_relative(root).u8string()]
                = it->file_size(ec);
        }
        return snapshot;
    }

    /// 以 createNew 产出黄金项目（真实落盘——调用方持有 store，释放写锁
    /// 须 requestClose＋reset）。
    static project::OpenStoreResult createGolden(const fs::path& dir)
    {
        return project::ProjectStoreFactory::createNew(dir, "重关联只读契约黄金项目");
    }

    /// 篡改 project.json 的 schemaVersion 字段值（旧格式/未来版本夹具——
    /// 词形来自 Codec dump：`"schemaVersion":<整数>`；只替换数字 token，
    /// 其余字节不动——打开②步版本判定的真实消费路径）。
    static void rewriteSchemaVersion(const fs::path& projectJson,
                                     const std::string& newDigits)
    {
        std::ifstream in(projectJson, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << projectJson.string();
        const std::string bytes((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        in.close();

        const std::size_t keyPos = bytes.find("\"schemaVersion\"");
        ASSERT_NE(keyPos, std::string::npos) << "project.json 缺 schemaVersion 键";
        const std::size_t colonPos = bytes.find(':', keyPos);
        ASSERT_NE(colonPos, std::string::npos);
        const std::size_t digitStart =
            bytes.find_first_of("-0123456789", colonPos);
        ASSERT_NE(digitStart, std::string::npos);
        std::size_t digitEnd =
            bytes.find_first_not_of("-0123456789", digitStart);
        if (digitEnd == std::string::npos) {
            digitEnd = bytes.size();
        }

        std::string rewritten = bytes;
        rewritten.replace(digitStart, digitEnd - digitStart, newDigits);
        std::ofstream out(projectJson, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out.write(rewritten.data(),
                  static_cast<std::streamsize>(rewritten.size()));
    }

    /// 手工写一条"他方持有者"固定宽度 lock 记录（伪造 PID——定宽布局
    /// 同 project StoreLock serializeRecord：pid=%010u\nhost=64 宽\n
    /// hb=24 宽\n；撕裂容忍解析按前缀搜索，此词形可被完整读回）。
    static void writeForeignLockRecord(const fs::path& lockFile,
                                       std::uint32_t foreignPid)
    {
        std::string host = "contract-test-host";
        host.resize(64, ' ');                       // 定宽填充（kLockHostWidth）
        std::string hb = "2026-10-09T00:00:00.000Z";
        hb.resize(24, ' ');                         // 定宽填充（kLockHeartbeatWidth）
        char pidLine[32];
        std::snprintf(pidLine, sizeof(pidLine), "pid=%010u\n",
                      static_cast<unsigned>(foreignPid));
        std::string record;
        record += pidLine;
        record += "host=" + host + "\n";
        record += "hb=" + hb + "\n";

        std::ofstream out(lockFile, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open()) << lockFile.string();
        out.write(record.data(), static_cast<std::streamsize>(record.size()));
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
};

fs::path ReconnectReadOnlyContract::s_base;
int ReconnectReadOnlyContract::s_caseCounter = 0;

// =====================================================================
// 捕获型诊断 sink（project IDiagnosticsSink 实现——PRJ-* 码观测面；
// ProjectStoreTest CapturingSink 同款形态）
// =====================================================================

class CapturingSink final : public project::IDiagnosticsSink {
public:
    std::vector<core::DiagnosticRecord> reports;///< 用户级诊断捕获（码观测）

    void report(const core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }
    void reportDev(const std::string& /*channel*/,
                   const std::string& /*message*/) override
    {
        // 开发级诊断不参与本契约的观测点（PRJ-FORMAT-LEGACY/PRJ-LOCK-HELD
        // 都是用户级）——丢弃。
    }

    /// 码值逐字匹配（P-PR-6：码值＝diagnostics.md §4.6 收编清单）。
    bool hasCode(const std::string& code) const
    {
        for (const auto& r : reports) {
            if (r.code == code) {
                return true;
            }
        }
        return false;
    }
};

// =====================================================================
// 外部持锁模拟（Win32 独占句柄 RAII——内核裁决面，非任何单元内部 API）
// =====================================================================

/// 以 StoreLock 同款共享模式（GENERIC_READ|GENERIC_WRITE＋仅
/// FILE_SHARE_READ）持有 lock 文件——后续任何写访问请求得
/// ERROR_SHARING_VIOLATION（StoreLock 判 HeldByOther→降级只读）。
class ScopedForeignLockHandle {
public:
    explicit ScopedForeignLockHandle(const fs::path& lockFile)
    {
        m_handle = ::CreateFileW(lockFile.wstring().c_str(),
                                 GENERIC_READ | GENERIC_WRITE,
                                 FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~ScopedForeignLockHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(m_handle);
        }
    }
    /// 句柄是否就位（CreateFileW 成功——外部持锁面建立）。
    bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_handle = INVALID_HANDLE_VALUE;///< OS 排他句柄（RAII 持有）
};

// =====================================================================
// 测试桩：重关联命令处理器（真实提交的领域命令面——BaselineSeedHandler
// 同款；token 无点形态，P-PR-9 未裁决不私置含点词形）
// =====================================================================

class RelinkSeedHandler final : public project::ICommandHandler {
public:
    /// 注册 token（§4.4.4 语法 ^[a-z0-9-]{3,64}——无点形态）。
    [[nodiscard]] std::string commandType() const override
    {
        return "wf-contract-relink-seed";
    }
    /// 受理载荷版本（无单位格式版本号）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    /**
     * @brief 产出最小合法计划（一个探测对象写入——重关联修订的实体面；
     *        payloadCanonical 不解释，D-10 透传存储）。
     */
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& /*envelope*/,
                                    const project::RevisionView& /*baseSnapshot*/,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::ObjectWrite write;
        write.objectId = ctx.objectId();  // project 分配对象身份（PA-1）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'r', 'e', 'l', 'i', 'n', 'k'};
        out.objectWrites.push_back(std::move(write));
        out.summary = "wf 契约测试：重关联显式提交修订（PM-09/AT-21 观测面）";
        return project::PrepareOutcome::Planned;
    }
};

// =====================================================================
// 测试桩：重关联检测/提交端口（真实提交桥接——L5 装配层的测试等价物：
// probe 脚本化〔检测执行归 io，本桩代呈结论〕；relink 经
// store.commands().submit() 真实提交——七步事务产生新修订）
// =====================================================================

class RealSubmitRelinkPort final : public workflow::IExternalRelinkPort {
public:
    ExternalSourceStatus probeStatus;///< probe 结论脚本位（检测材料）
    bool failRelink = false;         ///< true＝relink 报告提交未成功（失败注入）
    project::ProjectStore* store = nullptr;///< 提交目标（非 owning——黄金项目上下文）

    ExternalSourceStatus probe(const RelinkRequest& /*request*/) override
    {
        return probeStatus;  // 检测结论透传（登记/现内容对照材料齐备）
    }

    RelinkExecution relink(const RelinkRequest& /*request*/,
                           const ExternalSourceStatus& /*status*/) override
    {
        RelinkExecution e;
        if (failRelink) {
            e.relinked = false;
            e.cause = "注入的重关联提交失败（契约测试）";
            e.action = "请重试重关联操作";
            return e;  // 零触达 store——失败注入面
        }
        // 真实提交：主分支（createNew 后唯一分支，label "main"——P-PR-8）
        // 取 branchTips 首条 id；处理器已在装配段注册（用例内先行）。
        project::CommandEnvelope envelope;
        envelope.branch = store->query().branchTips().at(0).id;
        envelope.commandType = "wf-contract-relink-seed";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'r', 'l'};
        const project::CommandResult result = store->commands().submit(envelope);
        e.relinked = result.committed();
        if (result.committed()) {
            e.revisionId = result.newRevision.value();  // 新修订（七步事务产物）
        } else {
            e.cause = "提交未成功（契约测试兜底呈现）";
            e.action = "请检查项目写权限后重试";
        }
        return e;
    }
};

/// 用户确认决策端口桩（Proceed/Cancel 脚本位＋呈现材料录制）。
class DecisionPortStub final : public workflow::IRelinkDecisionPort {
public:
    RelinkDisposition disposition = RelinkDisposition::Proceed;///< 决策脚本位
    ExternalSourceStatus lastStatus{};///< 最近呈现材料（检测状态透传断言）

    RelinkDisposition confirmRelink(const ExternalSourceStatus& status) override
    {
        lastStatus = status;
        return disposition;
    }
};

// =====================================================================
// WF-VER-207：旧格式稳定拒绝＋升级指引＋诊断码＋原文件不动（PM-06/AT-20）
// =====================================================================

/// 旧 schemaVersion（主版本 0<1）项目：打开稳定拒绝（FormatLegacy）＋
/// 升级指引数据面（document/supported 提取）＋PRJ-FORMAT-LEGACY 诊断
/// ＋目录树字节面逐文件一致（原文件不动——AT-20 观测点）。
TEST_F(ReconnectReadOnlyContract, LegacySchemaVersion_Rejected_GuidanceDiag_FileUntouched_P06_AT20)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{"AT-20"});

    // 前置：真实黄金项目→释放锁→篡改 schemaVersion 为旧主版本 0
    // （真实打开②步版本判定路径——FormatLegacy）。
    const fs::path dir = m_dir / "legacy-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();
    rewriteSchemaVersion(dir / "project.json", "0");

    const auto before = snapshotTree(dir);

    // 操作：经打开编排（诊断 sink 注入——PRJ-* 码观测面）。
    CapturingSink sink;
    const workflow::OpenProjectOutcome outcome = workflow::OpenProjectFlow::run(
        workflow::OpenSource::Dialog, dir, nullptr, &sink);

    // 稳定只读拒绝：opened=false＋失败呈现（UX-03；推荐动作含"不自动
    // 升级"指引）。
    EXPECT_FALSE(outcome.opened);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("format-legacy"), std::string::npos)
        << "原因须透传对端 format-legacy 词形（cause=" << outcome.failure->cause
        << "）";
    EXPECT_NE(outcome.failure->recommendedAction.find("不自动升级"),
              std::string::npos)
        << "建议动作须含不自动升级指引（PM-06）";
    // 升级指引数据面（PM-06）：document=0（项目版本）/supported=10000
    // （当前支持版本）自对端 detail 提取——零加工透传。
    ASSERT_TRUE(outcome.failure->upgradeGuidance.has_value());
    EXPECT_EQ(outcome.failure->upgradeGuidance->documentVersion, "0");
    EXPECT_EQ(outcome.failure->upgradeGuidance->supportedVersion, "10000");
    // 诊断码观测点（WF-VER-207）：PRJ-FORMAT-LEGACY 经 sink 上报。
    EXPECT_TRUE(sink.hasCode("PRJ-FORMAT-LEGACY"))
        << "旧格式稳定拒绝必须产出 PRJ-FORMAT-LEGACY 稳定码（PM-06）";
    // 原文件不动（AT-20）：目录树字节面逐文件一致（打开路径零写入）。
    EXPECT_EQ(snapshotTree(dir), before)
        << "旧格式拒绝后项目目录不得有任何文件变化（PM-06/AT-20）";
}

/// 未来 schemaVersion（主版本 2>1）项目：打开稳定拒绝（SchemaFuture）＋
/// 升级指引三键齐备（document/supported/upgrade——PM-06 后半句"显示
/// 当前版本、项目版本、升级工具入口"）＋PRJ-SCHEMA-FUTURE＋原文件不动。
TEST_F(ReconnectReadOnlyContract, FutureSchemaVersion_Rejected_ThreeKeysGuidance_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{});

    const fs::path dir = m_dir / "future-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();
    rewriteSchemaVersion(dir / "project.json", "20000");  // 主版本 2×10000

    const auto before = snapshotTree(dir);

    CapturingSink sink;
    const workflow::OpenProjectOutcome outcome = workflow::OpenProjectFlow::run(
        workflow::OpenSource::DragDrop, dir, nullptr, &sink);

    EXPECT_FALSE(outcome.opened);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("schema-future"), std::string::npos)
        << "原因须透传对端 schema-future 词形（cause=" << outcome.failure->cause
        << "）";
    // 升级指引三键齐备（PM-06：当前版本/项目版本/升级工具入口——
    // upgrade 键为阶段 B ISchemaUpgrader 冻结前的固定 token 词形）。
    ASSERT_TRUE(outcome.failure->upgradeGuidance.has_value());
    EXPECT_EQ(outcome.failure->upgradeGuidance->documentVersion, "20000");
    EXPECT_EQ(outcome.failure->upgradeGuidance->supportedVersion, "10000");
    EXPECT_EQ(outcome.failure->upgradeGuidance->upgradeToolEntry,
              "ISchemaUpgrader-stage-b");
    EXPECT_TRUE(sink.hasCode("PRJ-SCHEMA-FUTURE"));
    // 原文件不动（PM-06 上半句同款承诺——未来版本同样稳定拒绝零写入）。
    EXPECT_EQ(snapshotTree(dir), before);
}

/// .rwproj 词面（老版本 RobWork 项目格式文件）：入口分流 Unknown→失败
/// 呈现零副作用——原文件不动且不产生任何目录（PM-06"旧格式（.rwproj
/// 等）稳定只读拒绝"的入口面；目录形态细检归打开②步——PA-1 不越权）。
TEST_F(ReconnectReadOnlyContract, LegacyRwprojFile_RoutedUnknown_ZeroSideEffect_P06)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-06"}, std::vector<std::string>{"AT-20"});

    // 前置：一个 .rwproj 文件（词面即老格式——老 RobWork 单文件项目）。
    const fs::path rwproj = m_dir / "old-project.rwproj";
    {
        std::ofstream out(rwproj, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << "RWS-PROJECT-V1\n";  // 老 RobWork 词面（内容不参与判定）
    }
    const auto beforeSize = fs::file_size(rwproj);

    // 入口分流：非目录非 .rwpack → Unknown（分类纯函数面直证）。
    EXPECT_EQ(workflow::classifyOpenTarget(rwproj),
              workflow::OpenTargetKind::Unknown);

    CapturingSink sink;
    const workflow::OpenProjectOutcome outcome = workflow::OpenProjectFlow::run(
        workflow::OpenSource::CommandLine, rwproj, nullptr, &sink);

    // 失败呈现＋零副作用：opened=false＋failure 定位到该文件＋原文件
    // 字节不动＋无新产物。
    EXPECT_FALSE(outcome.opened);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->file, rwproj.u8string());  // 具体文件定位
    EXPECT_FALSE(outcome.failure->upgradeGuidance.has_value());  // 非版本类失败
    EXPECT_EQ(fs::file_size(rwproj), beforeSize);  // 原文件不动（AT-20）
}

// =====================================================================
// WF-VER-208：只读打开——双实例第二写者降级＋PID 提示＋禁编辑与应用
// 提交＋不阻塞等待（PM-07）
// =====================================================================

/// 外部持锁（Win32 独占句柄模拟进程外第二实例）下的 Writable 打开：
/// 降级只读（不阻塞等待——open 立即返回）＋writable=false＋lockInfo
/// 携带他方 PID＋PRJ-LOCK-HELD 诊断＋只读会话数据面（禁编辑与应用
/// 提交）＋写入口端到端拒绝（命令提交/草稿落盘双轨）＋锁释放后恢复
/// 可写（对照腿）。
TEST_F(ReconnectReadOnlyContract, SecondWriterDegradesReadOnly_PidNotice_SubmitRejected_P07)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{"AT-20"});

    // 前置一：真实黄金项目（首个写者取得锁）→ 释放（工厂注册表与 OS
    // 锁同步退出；lock 文件按 §9.4 永不删除——记录保留）。
    const fs::path dir = m_dir / "readonly-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();

    // 前置二：伪造"他方"持有者记录（PID＝本进程＋1——与真实记录区分）
    // ＋独占句柄持有 lock 文件（内核裁决面——同 StoreLock 原语形态）。
    const std::uint32_t foreignPid = ::GetCurrentProcessId() + 1;
    writeForeignLockRecord(dir / "lock", foreignPid);
    ScopedForeignLockHandle foreignLock(dir / "lock");
    ASSERT_TRUE(foreignLock.valid()) << "外部持锁句柄建立失败（夹具前置）";

    // 操作：第二写者 Writable 打开（经打开编排——PM-07 降级路径）。
    // 非 const：收尾需要释放 outcome.store 的写锁（reset——const 对象的
    // unique_ptr 成员不可移转，SaveAsPackageContract 同款口径注）。
    CapturingSink sink;
    workflow::OpenProjectOutcome outcome = workflow::OpenProjectFlow::run(
        workflow::OpenSource::Dialog, dir, nullptr, &sink);

    // 降级语义（PM-07 不阻塞等待）：open 正常返回（非异常/非失败）——
    // opened=true＋readonly=true（Writable 请求被持锁降级的如实登记）。
    EXPECT_TRUE(outcome.opened);
    EXPECT_TRUE(outcome.readonly);
    ASSERT_TRUE(outcome.store);
    EXPECT_FALSE(outcome.store->writable());  // 唯一依据＝OS 排他句柄（SA-17）
    // 锁被持提示 PID（PM-07）：lockInfo 携带他方记录（§9.2② 读持有者）。
    EXPECT_FALSE(outcome.store->lockInfo().isSelf);
    EXPECT_EQ(outcome.store->lockInfo().holder.pid, foreignPid);
    EXPECT_EQ(outcome.store->lockInfo().holder.host, "contract-test-host");
    EXPECT_TRUE(sink.hasCode("PRJ-LOCK-HELD"))
        << "降级只读必须产出 PRJ-LOCK-HELD 稳定码（PM-07 提示面）";
    // 只读会话数据面（workflow 承接半区）：PID 提示＋禁编辑与应用提交。
    const workflow::ReadOnlySessionNotice notice =
        workflow::buildReadOnlySessionNotice(outcome.store->lockInfo());
    EXPECT_TRUE(notice.readOnly);
    EXPECT_EQ(notice.holderPid, foreignPid);       // 提示 PID＝他方持有者
    EXPECT_TRUE(notice.lockHeldByOther);
    EXPECT_TRUE(notice.editingDisabled);           // 禁编辑入口（PM-07）
    EXPECT_TRUE(notice.applyCommitDisabled);       // 禁应用提交入口（PM-07）
    EXPECT_EQ(notice.noticeKey, workflow::kReadOnlyLockHeldKey);

    // 禁应用提交（端到端——写入口权威拒绝归 project 收口）：注册测试
    // 处理器后提交，S1 形式校验以 not-writable 拒绝（只读上下文门卫）。
    {
        project::HandlerRegistry& registry = outcome.store->handlerRegistry();
        registry.registerHandler(std::make_unique<RelinkSeedHandler>());
        project::CommandEnvelope envelope;
        envelope.branch = outcome.store->query().branchTips().at(0).id;
        envelope.commandType = "wf-contract-relink-seed";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'r', 'o'};
        const project::CommandResult result =
            outcome.store->commands().submit(envelope);
        EXPECT_FALSE(result.committed());
        EXPECT_EQ(result.status.rejection,
                  project::CommandStatus::Rejection::NotWritable)
            << "只读上下文的应用提交必须被拒（PM-07 禁应用提交）";
    }
    // 禁编辑（草稿写轨同表——DraftService 只读上下文拒绝）。
    {
        project::DraftDocument doc;
        doc.projectId = outcome.store->projectId();
        doc.branchId = outcome.store->query().branchTips().at(0).id;
        doc.moduleId = "requirements";
        doc.baseRevisionId = outcome.store->query().branchTips().at(0).tip;
        doc.payload = "{\"note\":\"readonly-reject\"}";
        doc.savedAtUtc = "2026-10-09T00:00:00Z";
        doc.origin = project::DraftOrigin::Manual;
        const project::SaveResult saved = outcome.store->drafts().save(doc);
        EXPECT_FALSE(saved.ok)
            << "只读上下文的草稿写轨必须被拒（PM-07 禁编辑）";
    }

    // 收尾：关闭第二实例（降级上下文不触碰他方锁）→ 释放外部句柄 →
    // 对照腿：锁释放后 Writable 打开取得写权限（降级恢复）。
    EXPECT_EQ(outcome.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    outcome.store.reset();
}

/// 降级恢复对照腿（PM-07 完整语义——只读是锁的函数不是项目的属性）：
/// 外部句柄释放后同路径 Writable 打开 writable=true。
TEST_F(ReconnectReadOnlyContract, LockRelease_RestoresWritable_P07)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-07"}, std::vector<std::string>{});

    const fs::path dir = m_dir / "release-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();

    const std::uint32_t foreignPid = ::GetCurrentProcessId() + 1;
    writeForeignLockRecord(dir / "lock", foreignPid);
    {
        ScopedForeignLockHandle foreignLock(dir / "lock");
        ASSERT_TRUE(foreignLock.valid());
        CapturingSink sink;
        workflow::OpenProjectOutcome degraded =
            workflow::OpenProjectFlow::run(workflow::OpenSource::Dialog, dir,
                                           nullptr, &sink);
        ASSERT_TRUE(degraded.opened);
        EXPECT_TRUE(degraded.readonly);  // 持锁期降级
        EXPECT_EQ(degraded.store->requestClose(), 0u);  // 同步关闭
        degraded.store.reset();
    }  // RAII 释放外部句柄——锁面解除

    // 对照腿：锁释放后 Writable 打开取得写权限（只读随锁解除而恢复）。
    CapturingSink sink2;
    workflow::OpenProjectOutcome acquired =
        workflow::OpenProjectFlow::run(workflow::OpenSource::Dialog, dir,
                                       nullptr, &sink2);
    ASSERT_TRUE(acquired.opened);
    EXPECT_FALSE(acquired.readonly);
    EXPECT_TRUE(acquired.store->writable());
    EXPECT_EQ(acquired.store->requestClose(), 0u);  // 同步关闭
    acquired.store.reset();
}

// =====================================================================
// WF-VER-216：外部源重关联——显式提交产生新修订＋失败零写（PM-09/AT-21）
// =====================================================================

/// 重关联主线：检测 Changed→用户显式确认→真实提交（store.commands().
/// submit 七步事务）→新修订产生（AT-21 观测点"修订"）——编排结果
/// revisionId 与提交产物一致＋分支 tip 前进＋修订可读＋检测材料透传。
TEST_F(ReconnectReadOnlyContract, Relink_ExplicitCommit_ProducesNewRevision_P09_AT21)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    // 前置：真实黄金项目（写者持锁）＋装配期注册测试处理器（relink
    // 提交的命令面）＋tip 基线观测。
    const fs::path dir = m_dir / "relink-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    golden.store->handlerRegistry().registerHandler(
        std::make_unique<RelinkSeedHandler>());
    const std::vector<project::BranchTip> tipsBefore =
        golden.store->query().branchTips();
    ASSERT_FALSE(tipsBefore.empty());

    // 端口装配：检测材料（变化形态——登记/现内容对照齐备）＋真实提交
    // 桥接（store.commands().submit——显式提交的 L5 测试等价物）。
    RealSubmitRelinkPort port;
    port.store = golden.store.get();
    port.probeStatus.state = ExternalSourceState::Changed;
    port.probeStatus.externalRefId = "ext-robot-mesh";
    port.probeStatus.absolutePath = "D:/assets/robot-mesh.stl";
    port.probeStatus.recordedHash256 = std::string(64, 'a');
    port.probeStatus.recordedSizeBytes = 1024;  // 登记基准：1024 字节
    port.probeStatus.currentHash256 = std::string(64, 'b');
    port.probeStatus.currentSizeBytes = 2048;   // 现内容：2048 字节（已变化）
    DecisionPortStub decisions;  // 默认 Proceed——用户显式确认

    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome outcome =
        workflow::RelinkFlow::run(request, port, decisions);

    // 显式提交产生新修订（AT-21 观测点）：Relinked＋修订身份回传＋
    // 分支 tip 前进＋新修订真实可读。
    EXPECT_EQ(outcome.result, RelinkOutcome::Result::Relinked);
    ASSERT_TRUE(outcome.revisionId.has_value());
    EXPECT_TRUE(outcome.revisionId->isValid());
    const std::vector<project::BranchTip> tipsAfter =
        golden.store->query().branchTips();
    ASSERT_FALSE(tipsAfter.empty());
    EXPECT_EQ(tipsAfter[0].tip, outcome.revisionId.value())
        << "显式提交的新修订必须是分支新 tip（AT-21 修订观测点）";
    EXPECT_NE(tipsAfter[0].tip, tipsBefore[0].tip) << "tip 必须前进";
    ASSERT_TRUE(golden.store->query().tryRevision(outcome.revisionId.value()))
        << "新修订必须在查询端口可读（修订真实存在）";
    // 检测材料透传：确认对话框的呈现材料＝probe 产物（用户确认的是
    // 真实检测事实——编排核零加工）。
    EXPECT_EQ(decisions.lastStatus, port.probeStatus);
    EXPECT_EQ(decisions.lastStatus.state, ExternalSourceState::Changed);

    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();
}

/// 失败零写半区（"流程失败不动当前项目"——§7.5 行三）：提交未成功→
/// Failed＋branchTips/目录树字节面零变化（零修订零写入的盘面复核）；
/// 另含 NotNeeded 早退腿（检测 Ok 零确认零提交）。
TEST_F(ReconnectReadOnlyContract, RelinkFailedOrNotNeeded_NoProjectMutation_P09)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-09"}, std::vector<std::string>{"AT-21"});

    const fs::path dir = m_dir / "relink-fail-design";
    auto golden = createGolden(dir);
    ASSERT_TRUE(golden.store);
    golden.store->handlerRegistry().registerHandler(
        std::make_unique<RelinkSeedHandler>());
    const std::vector<project::BranchTip> tipsBefore =
        golden.store->query().branchTips();
    const auto treeBefore = snapshotTree(dir);

    // 腿一：提交失败注入（端口报 relinked=false——不触 store）。
    RealSubmitRelinkPort failPort;
    failPort.store = golden.store.get();
    failPort.failRelink = true;
    failPort.probeStatus.state = ExternalSourceState::Missing;  // 缺失——有事实
    DecisionPortStub decisions;
    RelinkRequest request;
    request.resource = core::ObjectId::generate();
    const RelinkOutcome failed =
        workflow::RelinkFlow::run(request, failPort, decisions);
    EXPECT_EQ(failed.result, RelinkOutcome::Result::Failed);
    ASSERT_TRUE(failed.failure.has_value());
    EXPECT_FALSE(failed.revisionId.has_value());
    // 盘面复核：零修订（tip 不变）＋目录树零变化（失败不动当前项目）。
    EXPECT_EQ(golden.store->query().branchTips(), tipsBefore)
        << "重关联失败后分支 tip 不得变化（零提交）";
    EXPECT_EQ(snapshotTree(dir), treeBefore)
        << "重关联失败后项目目录不得有任何文件变化（§7.5 流程失败不动"
           "当前项目）";

    // 腿二：检测无事实早退（Ok→NotNeeded）——同样零提交零写入。
    RealSubmitRelinkPort okPort;
    okPort.store = golden.store.get();
    okPort.probeStatus.state = ExternalSourceState::Ok;
    const RelinkOutcome notNeeded =
        workflow::RelinkFlow::run(request, okPort, decisions);
    EXPECT_EQ(notNeeded.result, RelinkOutcome::Result::NotNeeded);
    EXPECT_FALSE(notNeeded.revisionId.has_value());
    EXPECT_EQ(golden.store->query().branchTips(), tipsBefore)
        << "无重关联事实不得产生修订（NotNeeded 零提交）";
    EXPECT_EQ(snapshotTree(dir), treeBefore);

    EXPECT_EQ(golden.store->requestClose(), 0u);  // 同步关闭（无在途操作）
    golden.store.reset();
}

}  // namespace
