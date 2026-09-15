/**
 * @file   StoreLockTest.cpp
 * @brief  写锁（StoreLock/PathCanonical 前置：锁面）用例组——获取/心跳/
 *         失权三道防线/只读拒绝（PRJ-T03 acceptance 1 锁面/2/4）。
 *
 * 设计依据：
 *   - units/project.md §9.1（独占句柄机制＋RAII＋写 PID 记录）、§9.2
 *     （撕裂容忍读）、§9.4（永不删除重建 D-03——文件身份稳定观测；
 *     心跳仅诊断）、§9.5（三类失败）、§9.6（失权三道防线——acceptance 2
 *     的"写前权威检查"具名落点；SA-17 承接：心跳不得作写权限判据）、
 *     §9.8（心跳线程/writer 互斥）、§11 PRJ-TX-13 行（只读上下文全写
 *     入口拒绝——本组验证锁面写入口；命令/草稿/归档写入口的遍历随
 *     PRJ-T08/T10/T15 的写入口面建立后由同一门卫机制承接）、§11 头注
 *     （故障接缝消费形态——本组以本地 fake 先行承接，PRJ-T15 落
 *     testkit FaultInterceptor 后按其重述）；
 *   - 需求 PM-07（只读打开＋PID 提示，第二写者不阻塞）、PM-08（锁残留
 *     恢复诊断）；SA-17（权限即锁）；D-02（内核原子裁决）、D-03（心跳
 *     仅诊断）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 1（锁用例——
 *     双实例/卡顿/接管中进程内可证面）/2（TX-13 锁部分＋三道防线）/
 *     4（诊断经 IDiagnosticsSink 产出 core::DiagnosticRecord——P-PR-6
 *     注入式先行）。
 *
 * 范围声明：真双进程形态（第二实例/崩溃接管/并发竞速）在
 * LockContractTest.cpp（§3.3 `_contract_test`＝锁双实例契约面）；本组
 * 以同进程多实例承载同一内核裁决路径（CreateFileW 共享模式仲裁在
 * 进程内外同源），并覆盖 fake 注入的三道失权防线（真实句柄失效无法
 * 用真实磁盘稳定复现——接缝注入，D-10 形态）。
 */

#include "win32/StoreLock.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdurws::ird::core::DiagnosticRecord;
using sdurws::ird::project::IDiagnosticsSink;
using sdurws::ird::project::LockHolderRecord;
using sdurws::ird::project::win32::AcquireStatus;
using sdurws::ird::project::win32::FileResult;
using sdurws::ird::project::win32::ILockOps;
using sdurws::ird::project::win32::kLockHeartbeatPeriod;
using sdurws::ird::project::win32::kLockRecordSize;
using sdurws::ird::project::win32::LockRecordRead;
using sdurws::ird::project::win32::LockSelfRecord;
using sdurws::ird::project::win32::readHolderRecord;
using sdurws::ird::project::win32::StoreLock;
using sdurws::ird::project::win32::utcNowIsoMilli;
using sdurws::ird::project::win32::Win32LockOps;

namespace {

// ---------------------------------------------------------------------
// 捕获型 sink（acceptance 4 的观测面：诊断以 core::DiagnosticRecord 形态
// 经 IDiagnosticsSink 适配器到达——本类实现该接口并记录全部产出）。
// ---------------------------------------------------------------------

class CapturingSink : public IDiagnosticsSink {
public:
    std::vector<DiagnosticRecord> records;
    std::vector<std::pair<std::string, std::string>> devMessages;

    void report(const DiagnosticRecord& record) override
    {
        records.push_back(record);
    }
    void reportDev(const std::string& channel,
                   const std::string& message) override
    {
        devMessages.emplace_back(channel, message);
    }

    /// 统计指定码值的记录数（诊断断言的常用折叠）。
    std::size_t countOf(const char* code) const
    {
        std::size_t n = 0;
        for (const auto& r : records) {
            if (r.code == code) { ++n; }
        }
        return n;
    }
};

// ---------------------------------------------------------------------
// 故障注入 fake——ILockOps 接缝的测试侧实现（D-10 形态装饰器）。
// 默认全部转发真实 Win32LockOps（文件系统行为真实）；开关位注入失败
// 驱动防线②③路径（真实句柄异常失效不可稳定复现——testkit §6.4）。
// ---------------------------------------------------------------------

class FaultLockOps : public ILockOps {
public:
    Win32LockOps real;              ///< 真实实现（默认转发目标）
    bool failProbe = false;         ///< 注入点 project/store-lock/probe
    unsigned long failRewriteWith = 0;  ///< 非 0＝rewriteRecord 注入该错误码

    FileResult openExclusive(const std::wstring& path,
                             HANDLE* handle) override
    {
        return real.openExclusive(path, handle);
    }
    FileResult openReadShared(const std::wstring& path,
                              HANDLE* handle) override
    {
        return real.openReadShared(path, handle);
    }
    FileResult readAll(HANDLE handle, std::string* bytes) override
    {
        return real.readAll(handle, bytes);
    }
    FileResult rewriteRecord(HANDLE handle, const char* data,
                             std::size_t length) override
    {
        if (failRewriteWith != 0) {
            return FileResult{false, failRewriteWith};
        }
        return real.rewriteRecord(handle, data, length);
    }
    FileResult probe(HANDLE handle) override
    {
        if (failProbe) {
            return FileResult{false, 6UL /*ERROR_INVALID_HANDLE*/};
        }
        return real.probe(handle);
    }
    FileResult closeHandle(HANDLE handle) override
    {
        return real.closeHandle(handle);
    }
};

// ---------------------------------------------------------------------
// 夹具：用例自持临时目录（testkit TempDir 落地前的最小本地形态——
// AtomicFileTest 同款口径：目录隔离＋清理失败不静默）。
// ---------------------------------------------------------------------

class StoreLockTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()
                               ->current_test_info();
        // 目录名含用例名＋PID——多用例/多进程隔离（契约测试并行不互扰）。
        m_dir = fs::temp_directory_path() / "ird_wp04_t03_unit"
                / (std::string(info->name()) + "_"
                   + std::to_string(::GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败: " << m_dir.string();
        m_lockPath = m_dir / L"store.rwdesign" / L"lock";
        fs::create_directories(m_dir / L"store.rwdesign", ec);
        ASSERT_FALSE(ec) << "存储根创建失败";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);  // 清理失败不静默（句柄泄漏会在此显形）
        EXPECT_FALSE(ec) << "临时目录清理失败: " << m_dir.string();
    }

    /// 自我身份（默认参数；心跳陈旧构造用 makeSelf 变体）。
    static LockSelfRecord self(std::uint32_t pid = 4242)
    {
        LockSelfRecord s;
        s.pid = pid;
        s.host = "unit-test-host";
        s.initialHeartbeatUtc = utcNowIsoMilli();
        return s;
    }

    /// 指定初始心跳的自我身份（D-03 卡顿构造：陈旧时间戳）。
    static LockSelfRecord selfWithHeartbeat(const std::string& hb)
    {
        LockSelfRecord s = self();
        s.initialHeartbeatUtc = hb;
        return s;
    }

    /// 原生读取锁文件字节数（不经被测对象——独立观测通道）。
    static std::optional<std::uint64_t> rawFileSize(const fs::path& p)
    {
        HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { return std::nullopt; }
        LARGE_INTEGER size;
        const BOOL ok = ::GetFileSizeEx(h, &size);
        ::CloseHandle(h);
        if (!ok) { return std::nullopt; }
        return static_cast<std::uint64_t>(size.QuadPart);
    }

    /// 锁文件身份三元组（卷序号＋文件索引＋创建时间）——"永不删除重建"
    /// （D-03）的观测面：删除重建会改变全部三项。
    struct FileIdentity {
        DWORD volumeSerial = 0;
        DWORD indexHigh = 0;
        DWORD indexLow = 0;
        FILETIME creation{};
        bool operator==(const FileIdentity& o) const
        {
            return volumeSerial == o.volumeSerial && indexHigh == o.indexHigh
                && indexLow == o.indexLow
                && creation.dwLowDateTime == o.creation.dwLowDateTime
                && creation.dwHighDateTime == o.creation.dwHighDateTime;
        }
    };
    static std::optional<FileIdentity> fileIdentity(const fs::path& p)
    {
        HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { return std::nullopt; }
        BY_HANDLE_FILE_INFORMATION info{};
        const BOOL ok = ::GetFileInformationByHandle(h, &info);
        ::CloseHandle(h);
        if (!ok) { return std::nullopt; }
        FileIdentity id;
        id.volumeSerial = info.dwVolumeSerialNumber;
        id.indexHigh = info.nFileIndexHigh;
        id.indexLow = info.nFileIndexLow;
        id.creation = info.ftCreationTime;
        return id;
    }

    /// 读取当前锁文件记录（便利包装；读不到＝返回值 ok 为 false）。
    LockRecordRead readRecord()
    {
        Win32LockOps ops;
        return readHolderRecord(&ops, m_lockPath.wstring());
    }

    fs::path m_dir;       ///< 用例临时目录
    fs::path m_lockPath;  ///< 锁文件路径（store.rwdesign/lock，§4.1 布局）
};

/// 在记录的 context 文本中查找子串（PID 提示断言的辅助）。
bool anyRecordContains(const CapturingSink& sink, const char* code,
                       const std::string& needle)
{
    for (const auto& r : sink.records) {
        if (r.code == code && r.context.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ---------------------------------------------------------------------
// 获取与 PID 记录（§9.1）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1（锁用例·获取面）／§9.1"构造＝获取＋写 PID 记录"。
 * 前置：无锁持有。操作：构造 StoreLock。预期：Held；锁文件为定宽记录，
 * PID 字段可读回。观测点：status()/readHolderRecord/文件长度。
 */
TEST_F(StoreLockTest, Acquire_WritesPidRecord_FixedWidth)
{
    CapturingSink sink;
    Win32LockOps ops;
    StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(777));

    EXPECT_EQ(lock.status(), AcquireStatus::Held);
    EXPECT_TRUE(lock.held());

    // 记录可被第二实例语义读回（§9.2 读取路径）且 PID 一致。
    const auto rec = readRecord();
    ASSERT_TRUE(rec.ok) << "锁文件读取失败: " << rec.osError;
    ASSERT_TRUE(rec.parsed);
    EXPECT_EQ(rec.record.pid, 777u);
    EXPECT_EQ(rec.record.host, "unit-test-host");

    // 定宽约束：文件长度恒等于记录编码总长（§9.4 原地重写的长度前提）。
    const auto size = rawFileSize(m_lockPath);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, static_cast<std::uint64_t>(kLockRecordSize));
}

/**
 * 锚定：§9.1 契约违约 fail-fast——空接缝/空路径/负周期不得产生半构造
 * 对象（AGENTS §3 错误语义：调用方错误走异常）。
 */
TEST_F(StoreLockTest, Constructor_ContractViolations_FailFast)
{
    Win32LockOps ops;
    CapturingSink sink;
    EXPECT_THROW((StoreLock(nullptr, &sink, m_lockPath.wstring(), self())),
                 std::invalid_argument);
    EXPECT_THROW((StoreLock(&ops, &sink, std::wstring(), self())),
                 std::invalid_argument);
    EXPECT_THROW((StoreLock(&ops, &sink, m_lockPath.wstring(), self(),
                            std::chrono::milliseconds(-1))),
                 std::invalid_argument);
    // 违约构造不得留下锁文件（三次都在打开前被拦截）。
    EXPECT_FALSE(fs::exists(m_lockPath));
}

// ---------------------------------------------------------------------
// 心跳与 D-03 永不删除重建（§9.4）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 3 锁文件面（§9.4 防锁对象分裂——D-03）／§9.4 心跳
 * 原地重写。前置：50 ms 周期持有。操作：等待 ≥4 个心跳周期。预期：心跳
 * 字段前进；文件身份（卷序号/索引/创建时间）与长度不变＝文件从未删除
 * 重建。观测点：记录内容＋GetFileInformationByHandle 三元组。
 */
TEST_F(StoreLockTest, Heartbeat_RewritesInPlace_FileIdentityStable_D03)
{
    CapturingSink sink;
    Win32LockOps ops;
    // 初始心跳＝远古时间戳：任何内容前进都可归因于心跳线程（时钟差排除）。
    StoreLock lock(&ops, &sink, m_lockPath.wstring(),
                   selfWithHeartbeat("2000-01-01T00:00:00.000Z"),
                   std::chrono::milliseconds(50));
    ASSERT_TRUE(lock.held());

    const auto identityBefore = fileIdentity(m_lockPath);
    ASSERT_TRUE(identityBefore.has_value());

    // 200 ms ≈ 4 个周期（50 ms）——心跳至少前进一次；循环等待内容前进，
    // 避免对调度抖动的脆弱时序假设。
    bool advanced = false;
    for (int i = 0; i < 40 && !advanced; ++i) {
        ::Sleep(20);
        const auto rec = readRecord();
        advanced = rec.ok && rec.record.heartbeatUtc
                   != "2000-01-01T00:00:00.000Z";
    }
    ASSERT_TRUE(advanced) << "心跳未前进（线程/周期异常）";

    // 身份不变＝删除重建从未发生（删除重建将改写创建时间与文件索引）。
    const auto identityAfter = fileIdentity(m_lockPath);
    ASSERT_TRUE(identityAfter.has_value());
    EXPECT_TRUE(*identityBefore == *identityAfter)
        << "锁文件身份变化＝发生过删除重建（违反 §9.4/D-03）";
    const auto size = rawFileSize(m_lockPath);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(*size, static_cast<std::uint64_t>(kLockRecordSize))
        << "心跳重写后长度改变＝非定宽重写（§9.4 固定宽度约束）";
}

/**
 * 锚定：acceptance 2（SA-17 承接：心跳不得作为写权限判据）——双向证明。
 * 前置：持有者心跳内容为远古时间戳且周期禁用（内容永不前进）。
 * 预期：持有者自己的写权限不受陈旧心跳影响（requireWriteAuthority 仍
 * true——心跳不撤销权限）；第二实例仍被拒（拒绝依据是内核互斥而非心跳
 * 活性——D-03"卡顿不接管"的判据面）。
 * 观测点：requireWriteAuthority 门卫返回值（结构上零心跳读取）。
 */
TEST_F(StoreLockTest, StaleHeartbeat_IsNeverWriteAuthorityCriterion_SA17_D03)
{
    CapturingSink sink;
    Win32LockOps ops;
    // 周期 0＝禁用心跳线程：内容冻结在远古戳——比真实卡顿更极端的形态。
    StoreLock holder(&ops, &sink, m_lockPath.wstring(),
                     selfWithHeartbeat("2000-01-01T00:00:00.000Z"),
                     std::chrono::milliseconds(0));
    ASSERT_TRUE(holder.held());

    // 方向一：陈旧心跳不撤销持有者权限（权限唯一依据＝句柄在握）。
    EXPECT_TRUE(holder.requireWriteAuthority());

    // 方向二：陈旧心跳不授予第二实例权限（拒绝依据＝内核共享冲突）。
    StoreLock second(&ops, &sink, m_lockPath.wstring(), self(999),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(second.status(), AcquireStatus::HeldByOther);
    EXPECT_FALSE(second.requireWriteAuthority());
    // 第二实例的拒绝诊断＝PRJ-LOCK-HELD（锁事实），而非任何心跳派生码。
    EXPECT_EQ(sink.countOf("PRJ-LOCK-HELD"), 2);
}

// ---------------------------------------------------------------------
// 第二实例只读拒绝与 TX-13 锁部分（§9.2/§9.3；acceptance 1/2）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1（第二实例只读打开＋持锁 PID 提示——PM-07 的进程内
 * 形态）＋acceptance 2（TX-13 锁部分：只读上下文遍历全部写入口一律拒绝
 * ＋PRJ-LOCK-HELD 含持有 PID）＋§9.3（同规范路径进程内重复 writable 打开
 * ＝直接拒绝，防自我双写——holder.pid==当前 PID 即 isSelf 语义的数据面）。
 * 前置：实例 1 持有。操作：实例 2 构造（内核裁决拒绝）→ 遍历其全部锁面
 * 写入口。预期：拒绝＋每次拒绝产出 PRJ-LOCK-HELD（PID 文本在上下文中）；
 * 锁文件内容不被拒绝方改动。观测点：status/sink 记录/记录字节。
 */
TEST_F(StoreLockTest, SecondInstance_WriteEntries_UniformlyRejected_TX13)
{
    CapturingSink sink;
    Win32LockOps ops;
    StoreLock holder(&ops, &sink, m_lockPath.wstring(), self(321));
    ASSERT_TRUE(holder.held());
    const auto contentBefore = readRecord();
    ASSERT_TRUE(contentBefore.ok && contentBefore.parsed);

    // 实例 2：writable 意图打开——内核共享冲突（§9.2 流程①）→ 只读上下文。
    StoreLock second(&ops, &sink, m_lockPath.wstring(), self(654),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(second.status(), AcquireStatus::HeldByOther);
    EXPECT_FALSE(second.held());
    // 持有者 PID 来自锁记录（§9.2 流程②）——提示"项目被 PID=<n> 持有"。
    EXPECT_EQ(second.lastHolder().pid, 321u);
    // 获取被拒即产出 PRJ-LOCK-HELD（PM-07 的稳定诊断面）。
    EXPECT_EQ(sink.countOf("PRJ-LOCK-HELD"), 1);

    // TX-13 锁部分：只读上下文遍历**全部锁面写入口**一律拒绝。本阶段
    // 锁面写入口＝门卫本身＋锁文件重写入口（命令/草稿/归档写入口随
    // PRJ-T08+ 建立，统一经 requireWriteAuthority 门卫——§9.6①）。
    EXPECT_FALSE(second.requireWriteAuthority());
    EXPECT_FALSE(second.rewriteHeartbeat());
    // 每次写入口拒绝都伴随稳定诊断，且含持有 PID（§5.0 码表：
    // PRJ-LOCK-HELD paramSchema{pid,host}→上下文文本）。
    EXPECT_EQ(sink.countOf("PRJ-LOCK-HELD"), 3);
    EXPECT_TRUE(anyRecordContains(sink, "PRJ-LOCK-HELD", "321"))
        << "PRJ-LOCK-HELD 上下文须含持有 PID（PM-07 提示数据面）";

    // 拒绝方的一切尝试不得改动锁文件内容（只读上下文零写入副作用）。
    const auto contentAfter = readRecord();
    ASSERT_TRUE(contentAfter.ok && contentAfter.parsed);
    EXPECT_EQ(contentAfter.record.pid, contentBefore.record.pid);
    EXPECT_EQ(contentAfter.record.heartbeatUtc,
              contentBefore.record.heartbeatUtc);
}

/**
 * 锚定：acceptance 4（P-PR-6 处置）——锁诊断经 §5.0 IDiagnosticsSink
 * 适配器产出 core::DiagnosticRecord（本文件 CapturingSink 即该接口的
 * 测试实现；记录经 core::DiagnosticRecord::make 工厂产出＝C-3 校验通过
 * ＝必填字段非空）；码值精确等于 diagnostics.md §4.6 收编串（不私造）。
 */
TEST_F(StoreLockTest, Diagnostics_ProducedThroughCoreRecordFactory_PPR6)
{
    CapturingSink sink;
    Win32LockOps ops;
    StoreLock holder(&ops, &sink, m_lockPath.wstring(), self(111));
    ASSERT_TRUE(holder.held());
    StoreLock second(&ops, &sink, m_lockPath.wstring(), self(222),
                     std::chrono::milliseconds(0));
    ASSERT_EQ(second.status(), AcquireStatus::HeldByOther);

    ASSERT_FALSE(sink.records.empty());
    for (const auto& r : sink.records) {
        // C-3（core::DiagnosticRecord::make 的工厂校验）在产码侧强制：
        // 三个必填串非空——经工厂构造的实现不可能产出空字段记录。
        EXPECT_FALSE(r.context.empty());
        EXPECT_FALSE(r.cause.empty());
        EXPECT_FALSE(r.recommendedAction.empty());
    }
    // 码值＝diagnostics.md §4.6 收编串逐字相等（v0.2 码值消账口径）。
    bool sawLockHeld = false;
    for (const auto& r : sink.records) {
        if (r.code == "PRJ-LOCK-HELD") {
            sawLockHeld = true;
            EXPECT_EQ(r.code, "PRJ-LOCK-HELD");
        }
    }
    EXPECT_TRUE(sawLockHeld) << "获取被拒路径未产出 PRJ-LOCK-HELD";
}

// ---------------------------------------------------------------------
// 释放与失权防线①（§9.1 析构/§9.6 状态机）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 2（失权三道防线之①状态机）＋§9.1（析构＝关闭即
 * 失权）＋§5.0 映射（context-closed→PRJ-WRITE-AUTHORITY-LOST）。
 * 操作：持有→显式 release→遍历写入口；随后新实例获取。
 * 预期：释放后写入口全拒＋失权诊断；OS 锁即时可被新实例取得
 * （释放的即时性归 OS——无超时无残留）。
 */
TEST_F(StoreLockTest, Release_RejectsWrites_AuthorityLost_ThenReacquirable)
{
    CapturingSink sink;
    Win32LockOps ops;
    auto lock = std::make_unique<StoreLock>(&ops, &sink, m_lockPath.wstring(),
                                            self(111));
    ASSERT_TRUE(lock->held());

    lock->release();
    EXPECT_FALSE(lock->held());
    // 防线①：已释放态写入口全拒（PRJ-WRITE-AUTHORITY-LOST——§5.0 映射）。
    EXPECT_FALSE(lock->requireWriteAuthority());
    EXPECT_FALSE(lock->rewriteHeartbeat());
    EXPECT_EQ(sink.countOf("PRJ-WRITE-AUTHORITY-LOST"), 2);

    // OS 层面写权限即时失效：新实例立即可获取（§9.1 崩溃/退出释放语义
    // 的正常退出对照面）。
    StoreLock next(&ops, &sink, m_lockPath.wstring(), self(222),
                   std::chrono::milliseconds(0));
    EXPECT_EQ(next.status(), AcquireStatus::Held);
}

// ---------------------------------------------------------------------
// 失权防线②③（§9.6——fake 经 ILockOps 接缝注入，D-10）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 2（防线②权威探测）／§9.6"GetHandleInformation 失败
 * 即失权"。操作：fake 探测失败→门卫拒绝；fake 恢复后再探测。
 * 预期：首次拒绝即锁存 LostWrite（不可逆）＋PRJ-WRITE-AUTHORITY-LOST；
 * 探测恢复不解除锁存（失权不可逆——重获权限＝新实例，SA-17）。
 */
TEST_F(StoreLockTest, ProbeFailure_LatchesLostWrite_SecondLine)
{
    CapturingSink sink;
    FaultLockOps ops;
    StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                   std::chrono::milliseconds(0));
    ASSERT_TRUE(lock.held());

    ops.failProbe = true;  // 注入点 project/store-lock/probe
    EXPECT_FALSE(lock.requireWriteAuthority());
    EXPECT_FALSE(lock.held()) << "失权后 held() 必须翻转（状态机①）";
    EXPECT_GE(sink.countOf("PRJ-WRITE-AUTHORITY-LOST"), 1);

    // 锁存语义：注入撤销后仍拒绝——失权判定不依赖后续探测结果。
    ops.failProbe = false;
    EXPECT_FALSE(lock.requireWriteAuthority());
    EXPECT_FALSE(lock.rewriteHeartbeat());
}

/**
 * 锚定：acceptance 2（防线③ I/O 分类）／§9.6 三码表
 * （SHARING_VIOLATION/INVALID_HANDLE/ACCESS_DENIED→LostWrite；非三码
 * ——如磁盘满——不触发失权）。操作：逐码经 classifyIoError 分类。
 * 预期：三码各锁存失权；112（ERROR_DISK_FULL）不锁存。
 */
TEST_F(StoreLockTest, IoErrorClassification_ThreeCodesLatch_OthersNot)
{
    CapturingSink sink;
    Win32LockOps ops;
    // 码 32（ERROR_SHARING_VIOLATION）。
    {
        StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                       std::chrono::milliseconds(0));
        ASSERT_TRUE(lock.held());
        lock.classifyIoError(32UL);
        EXPECT_FALSE(lock.held());
        EXPECT_FALSE(lock.requireWriteAuthority());
    }
    // 码 6（ERROR_INVALID_HANDLE）。
    {
        StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                       std::chrono::milliseconds(0));
        ASSERT_TRUE(lock.held());
        lock.classifyIoError(6UL);
        EXPECT_FALSE(lock.held());
    }
    // 码 5（ERROR_ACCESS_DENIED）。
    {
        StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                       std::chrono::milliseconds(0));
        ASSERT_TRUE(lock.held());
        lock.classifyIoError(5UL);
        EXPECT_FALSE(lock.held());
    }
    // 反例：磁盘满（112）＝环境错误——心跳丢失无害（D-03），不失权。
    {
        StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                       std::chrono::milliseconds(0));
        ASSERT_TRUE(lock.held());
        lock.classifyIoError(112UL /*ERROR_DISK_FULL*/);
        EXPECT_TRUE(lock.held())
            << "非三码错误不得触发失权（§9.6 分类表封闭）";
    }
}

/**
 * 锚定：acceptance 2（防线③经真实写入口通道）／§9.4 心跳写失败分类。
 * 操作：fake 重写注入 ERROR_INVALID_HANDLE→rewriteHeartbeat。
 * 预期：重写失败＋防线③锁存失权＋PRJ-WRITE-AUTHORITY-LOST；后续门卫
 * 拒绝（与显式 classifyIoError 同一状态机汇合点）。
 */
TEST_F(StoreLockTest, HeartbeatWriteFailure_ClassifiedAsAuthorityLoss)
{
    CapturingSink sink;
    FaultLockOps ops;
    StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(),
                   std::chrono::milliseconds(0));
    ASSERT_TRUE(lock.held());

    ops.failRewriteWith = 6UL;  // 注入点 project/store-lock/rewrite-record
    EXPECT_FALSE(lock.rewriteHeartbeat());
    EXPECT_FALSE(lock.held());
    EXPECT_GE(sink.countOf("PRJ-WRITE-AUTHORITY-LOST"), 1);
    EXPECT_FALSE(lock.requireWriteAuthority()) << "失权后续写全拒（§9.6）";
}

// ---------------------------------------------------------------------
// 接管与撕裂容忍（§9.4/§9.2；PM-08）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1（崩溃后接管面——残留读取与重写，进程内形态；
 * 真崩溃跨进程形态在 LockContractTest）＋PM-08（锁残留恢复诊断）。
 * 前置：裸写一条异 PID 残留记录（模拟崩溃残留——原持有者句柄已随进程
 * 消失，仅内容残留）。操作：新实例获取。预期：接管成功；开发诊断报告
 * 残留 PID（PM-08"锁残留…并报告"）；文件内容重写为接管者身份。
 */
TEST_F(StoreLockTest, Takeover_ReadsResidual_RecoveryDevDiagnostic_PM08)
{
    // 前置：不经 StoreLock 裸写残留记录（崩溃残留的等价物——内容在、
    // 句柄无）。
    {
        std::ofstream out(m_lockPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << "pid=0000005432\n";
        out << "host=" << std::string(64, 'x') << "\n";
        out << "hb=2020-05-05T05:05:05.050Z\n";
    }

    CapturingSink sink;
    Win32LockOps ops;
    StoreLock lock(&ops, &sink, m_lockPath.wstring(), self(9999),
                   std::chrono::milliseconds(0));
    ASSERT_EQ(lock.status(), AcquireStatus::Held);

    // 恢复诊断（开发级）：channel=project/lock-recovery，含残留 PID。
    bool sawResidual = false;
    for (const auto& [channel, message] : sink.devMessages) {
        if (channel == "project/lock-recovery"
            && message.find("5432") != std::string::npos) {
            sawResidual = true;
        }
    }
    EXPECT_TRUE(sawResidual) << "接管未报告残留记录（PM-08 锁残留报告缺失）";

    // 接管者身份已落地（残留被重写覆盖——§9.4"再重写为自己的 PID"）。
    const auto rec = readRecord();
    ASSERT_TRUE(rec.ok && rec.parsed);
    EXPECT_EQ(rec.record.pid, 9999u);
}

/**
 * 锚定：§9.2"固定宽度记录，容忍撕裂读——心跳仅诊断"。操作：向锁文件
 * 写入撕裂/垃圾字节后读取。预期：读取成功不抛异常；解析零命中时
 * parsed==false、字段零值（"未知"呈现口径）——绝不可误报存储损坏。
 */
TEST_F(StoreLockTest, ReadHolderRecord_TornContent_Tolerated)
{
    // 半条记录（截断于 pid 行中段——truncate→write 重写窗口的典型视图）。
    {
        std::ofstream out(m_lockPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << "pid=00000";  // 截断
    }
    auto torn = readRecord();
    EXPECT_TRUE(torn.ok);           // 读取本身成功
    EXPECT_TRUE(torn.parsed);       // pid 前缀命中（半截数字解析为部分值）
    EXPECT_EQ(torn.record.host, "");
    EXPECT_EQ(torn.record.heartbeatUtc, "");

    // 全垃圾内容（无任何字段前缀——外部工具污染的等价物）。
    {
        std::ofstream out(m_lockPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(static_cast<bool>(out));
        out << "\x01\x02garbage without fields";
    }
    auto junk = readRecord();
    EXPECT_TRUE(junk.ok);
    EXPECT_FALSE(junk.parsed);      // 零字段命中＝"未知持有者"
    EXPECT_EQ(junk.record.pid, 0u);
}

/**
 * 锚定：acceptance 1（心跳卡顿不接管——D-03 的真实内核面：持有者心跳
 * 线程停跳但句柄在握时，第二实例的拒绝依据与心跳内容无关）。进程内
 * 形态：holder 心跳冻结（周期 0），second 在 holder 心跳"过期"状态下
 * 仍被内核拒绝——与 StaleHeartbeat_IsNeverWriteAuthorityCriterion 组成
 * 判据双向覆盖。
 * 观测点：拒绝状态＋拒绝不随时间推移而翻转（轮询两次，间隔 60 ms）。
 */
TEST_F(StoreLockTest, StalledHolder_NotTakenOver_OverTime_D03)
{
    CapturingSink sink;
    Win32LockOps ops;
    StoreLock holder(&ops, &sink, m_lockPath.wstring(),
                     selfWithHeartbeat("2000-01-01T00:00:00.000Z"),
                     std::chrono::milliseconds(0));
    ASSERT_TRUE(holder.held());

    for (int round = 0; round < 2; ++round) {
        StoreLock second(&ops, &sink, m_lockPath.wstring(), self(),
                         std::chrono::milliseconds(0));
        // 时间推移（心跳早已"过期"）不改变裁决——接管唯一途径＝OS 锁
        // 可被获取（§9.4 原文）。
        EXPECT_EQ(second.status(), AcquireStatus::HeldByOther) << "轮次 " << round;
        ::Sleep(60);
    }
}
