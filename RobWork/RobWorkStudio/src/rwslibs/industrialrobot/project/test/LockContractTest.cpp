/**
 * @file   LockContractTest.cpp
 * @brief  写锁跨实例契约测试（真双进程）——PRJ-TX-7 锁用例＋§9.3 多路径
 *         打开（PRJ-T03 acceptance 1/3 的跨进程面）。
 *
 * 设计依据：
 *   - units/project.md §3.3（`_contract_test`＝跨单元契约面：锁双实例…）、
 *     §11 PRJ-TX-7 行（第二实例只读；持锁进程卡顿〔心跳停滞〕；崩溃后
 *     并发接管〔两实例同时获取〕——预期：只读＋PID 提示；卡顿不接管；
 *     接管仅一胜者）、§9.9（双实例锁竞争流程图）、§9.1～§9.4（D-02
 *     内核原子裁决／D-03 心跳仅诊断·永不删除重建）、§11 头注（崩溃类
 *     TestProcessRunner 按 testkit §6.5/§10.2 交付节奏归 PRJ-T15——本组
 *     以测试自再执行的子进程形态先行承接真双进程语义，PRJ-T15 落
 *     testkit 后按 FaultInterceptor/TestProcessRunner 重述）；
 *   - 需求 PM-07（双实例第二写者不阻塞等待＋PID 提示）、PM-08（崩溃后
 *     锁残留与接管）、SA-17（写权限唯一依据＝OS 独占句柄）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 1（第二实例只读
 *     ＋PID；心跳卡顿不接管；崩溃后双实例并发接管仅一胜者）。
 *
 * 双进程形态说明（为什么用子进程而不用线程）：acceptance 1 的三个场景
 * 语义锚定在"实例＝进程"上——崩溃释放（OS 关闭崩溃进程的全部句柄）
 * 与心跳停滞（另一进程不再写记录）无法在单进程内如实构造（线程退出不
 * 产生"OS 收句柄"的崩溃语义；Terminatethread 不等价）。子进程＝测试
 * 可执行文件自再执行（--ird-wp04-t03-child 分派，经环境变量传递宽字符
 * 参数——窄 argv 会破坏非 ASCII 路径），Exit Code 承载获取结果。
 */

#include "Tx15Child.hpp"
#include "win32/PathCanonical.hpp"
#include "win32/StoreLock.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <ctime>
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
using sdurws::ird::project::win32::canonicalStorePath;
using sdurws::ird::project::win32::LockRecordRead;
using sdurws::ird::project::win32::LockSelfRecord;
using sdurws::ird::project::win32::readHolderRecord;
using sdurws::ird::project::win32::sameStorePath;
using sdurws::ird::project::win32::StoreLock;
using sdurws::ird::project::win32::Win32LockOps;

namespace {

// =====================================================================
// 子进程模式（测试可执行文件自再执行）。分派在 main() 中先于 gtest
// 初始化——子进程不进入测试框架。参数经环境变量传递（宽字符安全；
// 窄 argv 经 ANSI 代码页会破坏非 ASCII 临时路径）。
// =====================================================================

/// 读取环境变量（宽字符；缺失/超长＝返回空并置 ok=false）。
std::wstring envWide(const wchar_t* name, bool* ok = nullptr)
{
    const DWORD need = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) {
        if (ok != nullptr) { *ok = false; }
        return {};
    }
    std::wstring value(need, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(name, value.data(), need);
    if (ok != nullptr) { *ok = got > 0; }
    value.resize(got);
    return value;
}

/// 绝对 time_t → ISO-8601 带毫秒 UTC（毫秒恒 .000——子进程初始心跳的
/// 生成格式，与产品 utcNowIsoMilli 同构定宽）。
std::string isoFromTimeT(std::time_t t)
{
    std::tm tmUtc{};
    if (::gmtime_s(&tmUtc, &t) != 0) { return {}; }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
                  tmUtc.tm_year + 1900, tmUtc.tm_mon + 1, tmUtc.tm_mday,
                  tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec);
    return buf;
}

/// 子进程 hold 模式：获取写锁并持有 holdMs 毫秒后正常退出（Exit 0）。
/// 心跳周期 intervalMs；初始心跳＝当前时间回拨 hbAgeSec 秒（构造"陈旧
/// 心跳"验证 D-03——内容陈旧但句柄在握＝真实卡顿形态）。
int runChildHold()
{
    const auto lockPath = envWide(L"IRD_T03_LOCK");
    const long holdMs = std::wcstol(envWide(L"IRD_T03_HOLD_MS").c_str(),
                                    nullptr, 10);
    const long intervalMs = std::wcstol(envWide(L"IRD_T03_INTERVAL_MS").c_str(),
                                        nullptr, 10);
    const long hbAgeSec = std::wcstol(envWide(L"IRD_T03_HB_AGE_SEC").c_str(),
                                      nullptr, 10);
    if (lockPath.empty()) { return 8; }

    Win32LockOps ops;
    LockSelfRecord self;
    self.pid = ::GetCurrentProcessId();
    self.host = "wp04-t03-child";
    self.initialHeartbeatUtc =
        isoFromTimeT(std::time(nullptr) - static_cast<std::time_t>(hbAgeSec));
    StoreLock lock(&ops, nullptr, lockPath, self,
                   std::chrono::milliseconds(intervalMs));
    if (lock.status() != AcquireStatus::Held) {
        return 5;  // 获取失败（前置竞争失利）——父进程按失败处置
    }
    ::Sleep(static_cast<DWORD>(holdMs));  // 持有窗口（父进程在此窗口断言）
    return 0;                             // 析构释放（RAII 正常路径）
}

/// 子进程 race 模式：等待命名事件（父进程统一放行）→ 单次获取尝试 →
/// Exit 0＝获取成功（胜者）／3＝锁被持（败者）／其他＝异常。**单次**
/// 尝试而非重试轮询——并发竞速的胜者判定必须来自同一时刻的竞争。
int runChildRace()
{
    const auto lockPath = envWide(L"IRD_T03_LOCK");
    const auto eventName = envWide(L"IRD_T03_EVENT");
    if (lockPath.empty() || eventName.empty()) { return 8; }

    // 同名 CreateEventW＝打开父进程创建的事件（manual-reset——一次
    // SetEvent 同时放行全部竞争者，保证真并发起点）。
    HANDLE ev = ::CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (ev == nullptr) { return 7; }
    const DWORD wait = ::WaitForSingleObject(ev, 15000);
    ::CloseHandle(ev);
    if (wait != WAIT_OBJECT_0) { return 4; }  // 放行未达（父进程侧异常）

    Win32LockOps ops;
    LockSelfRecord self;
    self.pid = ::GetCurrentProcessId();
    self.host = "wp04-t03-racer";
    self.initialHeartbeatUtc = isoFromTimeT(std::time(nullptr));
    StoreLock lock(&ops, nullptr, lockPath, self, std::chrono::milliseconds(0));
    if (lock.status() == AcquireStatus::Held) { return 0; }
    if (lock.status() == AcquireStatus::HeldByOther) { return 3; }
    return 6;
}

/// 子进程模式入口（窄 argv 仅承载 ASCII 分派 token——参数走环境变量）。
int runChildMode(const char* mode)
{
    const std::string m = mode;
    if (m == "hold") { return runChildHold(); }
    if (m == "race") { return runChildRace(); }
    return 9;  // 未知模式——分派表违约
}

// =====================================================================
// 父进程辅助：子进程生命周期／记录轮询／文件身份／ISO 解析。
// =====================================================================

/// 子进程 RAII 守卫：正常路径由用例显式收尾；失败路径兜底终止＋关闭
/// 句柄（防挂起进程泄漏——失败不留环境污染，与临时目录口径一致）。
struct ChildProc {
    HANDLE process = nullptr;
    HANDLE thread = nullptr;
    DWORD pid = 0;

    ChildProc() = default;
    ~ChildProc()
    {
        if (process != nullptr) {
            if (::WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
                ::TerminateProcess(process, 2);
                ::WaitForSingleObject(process, 5000);
            }
            ::CloseHandle(process);
        }
        if (thread != nullptr) { ::CloseHandle(thread); }
    }
    ChildProc(const ChildProc&) = delete;
    ChildProc& operator=(const ChildProc&) = delete;
};

/// 以当前环境（父进程先行 SetEnvironmentVariableW 传参）spawn 子进程。
bool spawnChild(ChildProc* child, const char* mode)
{
    wchar_t exePath[MAX_PATH];
    if (::GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) { return false; }
    // 命令行＝"<自身>" --ird-wp04-t03-child <mode>（均为 ASCII 安全面；
    // 路径参数在环境变量中，不经命令行——宽字符无损）。
    std::wstring cmdline = std::wstring(L"\"") + exePath
                           + L"\" --ird-wp04-t03-child "
                           + std::wstring(mode, mode + std::strlen(mode));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // lpEnvironment=nullptr＝继承父进程环境（父进程已写入本用例参数）。
    const BOOL ok = ::CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr,
                                     FALSE, 0, nullptr, nullptr, &si, &pi);
    if (!ok) { return false; }
    child->process = pi.hProcess;
    child->thread = pi.hThread;
    child->pid = pi.dwProcessId;
    return true;
}

/// 等待子进程退出并返回 Exit Code；超时返回 nullopt（调用方显性失败）。
std::optional<DWORD> waitExit(const ChildProc& child, DWORD timeoutMs)
{
    if (::WaitForSingleObject(child.process, timeoutMs) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 0;
    if (!::GetExitCodeProcess(child.process, &code)) { return std::nullopt; }
    return code;
}

/// 终止子进程（崩溃语义注入：TerminateProcess＝OS 立即回收全部句柄——
/// §9.1"崩溃释放"的等价物；等待回收完成消除接续判定的竞态）。
void crashTerminate(const ChildProc& child)
{
    ::TerminateProcess(child.process, 1);
    ::WaitForSingleObject(child.process, 10000);
}

/// 捕获型 sink（父进程获取尝试的诊断观测面）。
class CapturingSink : public IDiagnosticsSink {
public:
    std::vector<DiagnosticRecord> records;
    void report(const DiagnosticRecord& record) override
    {
        records.push_back(record);
    }
    void reportDev(const std::string&, const std::string&) override {}
};

/// 轮询等待锁文件出现指定 PID 的持有者记录（§9.2 只读探测通道；不发起
 /// 可写打开——父进程不得抢在子进程前持锁）。超时＝nullopt。
std::optional<LockHolderRecord> waitForHolderPid(Win32LockOps& ops,
                                                 const std::wstring& lockPath,
                                                 std::uint32_t pid,
                                                 DWORD timeoutMs)
{
    const ULONGLONG deadline = ::GetTickCount64() + timeoutMs;
    for (;;) {
        const auto rec = readHolderRecord(&ops, lockPath);
        if (rec.ok && rec.parsed && rec.record.pid == pid) {
            return rec.record;
        }
        if (::GetTickCount64() >= deadline) { return std::nullopt; }
        ::Sleep(50);
    }
}

/// 锁文件身份（卷序号＋文件索引＋创建时间）——"永不删除重建"（D-03）
/// 的跨进程观测面：删除重建将改写全部三项。
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

std::optional<FileIdentity> fileIdentity(const std::wstring& path)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
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

/// ISO-8601 心跳文本 → time_t（_mkgmtime；解析失败＝nullopt）。
std::optional<std::time_t> isoToTimeT(const std::string& iso)
{
    if (iso.size() < 19) { return std::nullopt; }
    const auto num = [&iso](std::size_t off, std::size_t len) {
        return std::atoi(iso.substr(off, len).c_str());
    };
    std::tm t{};
    t.tm_year = num(0, 4) - 1900;
    t.tm_mon = num(5, 2) - 1;
    t.tm_mday = num(8, 2);
    t.tm_hour = num(11, 2);
    t.tm_min = num(14, 2);
    t.tm_sec = num(17, 2);
    const std::time_t out = ::_mkgmtime(&t);
    if (out == -1) { return std::nullopt; }
    return out;
}

/// 夹具：用例自持存储目录（子进程共享该目录——目录创建先于 spawn）。
class LockContractTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp04_t03_contract"
                / (std::string(info->name()) + "_"
                   + std::to_string(::GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir / L"store.rwdesign", ec);
        ASSERT_FALSE(ec) << "临时目录创建失败";
        m_lockPath = m_dir / L"store.rwdesign" / L"lock";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败（子进程句柄残留会在此显形）";
    }

    /// 配置子进程环境参数（继承传递——CreateProcess 前调用）。
    void setChildEnv(const fs::path& lockPath, long holdMs, long intervalMs,
                     long hbAgeSec) const
    {
        ::SetEnvironmentVariableW(L"IRD_T03_LOCK", lockPath.c_str());
        ::SetEnvironmentVariableW(
            L"IRD_T03_HOLD_MS", std::to_wstring(holdMs).c_str());
        ::SetEnvironmentVariableW(
            L"IRD_T03_INTERVAL_MS", std::to_wstring(intervalMs).c_str());
        ::SetEnvironmentVariableW(
            L"IRD_T03_HB_AGE_SEC", std::to_wstring(hbAgeSec).c_str());
    }

    fs::path m_dir;       ///< 用例临时目录
    fs::path m_lockPath;  ///< 锁文件路径
};

}  // namespace

// ---------------------------------------------------------------------
// PRJ-TX-7 场景一：第二实例只读打开＋持锁 PID 提示（PM-07）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1 前半／§9.2 流程全走查（真双进程）。前置：子进程
 * 实例 1 可写打开并持有。操作：父进程以 writable 意图构造 StoreLock。
 * 预期：HeldByOther（不阻塞等待）＋持有者 PID＝子进程 PID＋PRJ-LOCK-HELD
 * 诊断（上下文含 PID）；子进程退出后锁即时可取（§9.1 退出释放）。
 * 观测点：status/lastHolder/sink 记录/二次获取结果。
 */
TEST_F(LockContractTest, SecondInstance_ReadOnlyDegrade_ReportsHolderPid_PM07)
{
    setChildEnv(m_lockPath, /*holdMs=*/4000, /*intervalMs=*/10000,
                /*hbAgeSec=*/0);
    ChildProc holder;
    ASSERT_TRUE(spawnChild(&holder, "hold"));

    // 前置就位：子进程已持锁（锁记录 PID＝子进程 PID——§9.2 探测通道）。
    Win32LockOps ops;
    const auto holderRec = waitForHolderPid(ops, m_lockPath.wstring(),
                                            holder.pid, 10000);
    ASSERT_TRUE(holderRec.has_value()) << "子进程未在超时内持锁";

    // 第二实例：writable 意图打开——内核共享冲突 → 只读降级（不等待）。
    CapturingSink sink;
    StoreLock second(&ops, &sink, m_lockPath.wstring(),
                     [] {
                         LockSelfRecord s;
                         s.pid = ::GetCurrentProcessId();
                         s.host = "contract-parent";
                         s.initialHeartbeatUtc = "2026-09-15T00:00:00.000Z";
                         return s;
                     }(),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(second.status(), AcquireStatus::HeldByOther);
    EXPECT_FALSE(second.held());
    EXPECT_EQ(second.lastHolder().pid, holder.pid)
        << "持有 PID 提示与真实持有进程不符（PM-07）";
    // 稳定诊断：PRJ-LOCK-HELD 含持有 PID（§5.0 码表——P-PR-6 收编清单）。
    ASSERT_FALSE(sink.records.empty());
    bool sawPidInContext = false;
    for (const auto& r : sink.records) {
        if (r.code == "PRJ-LOCK-HELD"
            && r.context.find(std::to_string(holder.pid)) != std::string::npos) {
            sawPidInContext = true;
        }
    }
    EXPECT_TRUE(sawPidInContext) << "PRJ-LOCK-HELD 未携带持有 PID";

    // 持有者正常退出（holdMs 到时）→ OS 关闭句柄 → 锁即时可取。
    const auto code = waitExit(holder, 15000);
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 0UL);
    StoreLock after(&ops, nullptr, m_lockPath.wstring(),
                    [] {
                        LockSelfRecord s;
                        s.pid = ::GetCurrentProcessId();
                        s.host = "contract-parent";
                        s.initialHeartbeatUtc = "2026-09-15T00:00:00.000Z";
                        return s;
                    }(),
                    std::chrono::milliseconds(0));
    EXPECT_EQ(after.status(), AcquireStatus::Held)
        << "持有者退出后锁未即时可取（§9.1 释放即时性）";
}

// ---------------------------------------------------------------------
// PRJ-TX-7 场景二：持锁进程卡顿（心跳停滞）不接管（D-03）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1 中段／§9.4"残留心跳过期**不**触发任何接管（卡顿
 * 不接管——ARCH §6.8 原文；接管唯一途径＝OS 锁可被获取）"。前置：子
 * 进程持锁，锁记录心跳被构造为 2 小时前（远超任何心跳周期＝深度卡顿）
 * 且心跳线程周期 10 s（用例窗口内不再前进＝停滞）。操作：父进程读记录
 * 证陈旧→尝试获取→等待后复测。预期：两次获取均被拒（陈旧不授权）；
 * 心跳内容冻结不变（停滞实据）。观测点：记录时间戳/获取状态。
 */
TEST_F(LockContractTest, StalledHeartbeat_NeverEnablesTakeover_D03)
{
    setChildEnv(m_lockPath, /*holdMs=*/30000, /*intervalMs=*/10000,
                /*hbAgeSec=*/7200);
    ChildProc holder;
    ASSERT_TRUE(spawnChild(&holder, "hold"));

    Win32LockOps ops;
    const auto rec = waitForHolderPid(ops, m_lockPath.wstring(), holder.pid,
                                      10000);
    ASSERT_TRUE(rec.has_value());
    // 陈旧实据：心跳时间戳距此刻 > 1 小时（构造值 2 小时前）。
    const auto stamp = isoToTimeT(rec->heartbeatUtc);
    ASSERT_TRUE(stamp.has_value()) << "心跳时间戳不可解析: "
                                   << rec->heartbeatUtc;
    const long long ageSec = static_cast<long long>(std::time(nullptr))
                             - static_cast<long long>(*stamp);
    EXPECT_GT(ageSec, 3600LL) << "前置失败：心跳并未陈旧（构造失效）";

    // 获取尝试一：陈旧心跳不触发接管——拒绝依据是内核互斥。
    CapturingSink sink;
    StoreLock taker(&ops, &sink, m_lockPath.wstring(),
                    [] {
                        LockSelfRecord s;
                        s.pid = ::GetCurrentProcessId();
                        s.host = "contract-parent";
                        s.initialHeartbeatUtc = "2026-09-15T00:00:00.000Z";
                        return s;
                    }(),
                    std::chrono::milliseconds(0));
    EXPECT_EQ(taker.status(), AcquireStatus::HeldByOther)
        << "陈旧心跳触发了接管（违反 D-03/ARCH §6.8）";

    // 等待后复测：拒绝不随时间翻转；心跳内容冻结（卡顿实据）。
    ::Sleep(300);
    const auto again = readHolderRecord(&ops, m_lockPath.wstring());
    ASSERT_TRUE(again.ok && again.parsed);
    EXPECT_EQ(again.record.heartbeatUtc, rec->heartbeatUtc)
        << "心跳内容前进了——子进程并未停滞（构造失效）";
    StoreLock taker2(&ops, nullptr, m_lockPath.wstring(),
                     [] {
                         LockSelfRecord s;
                         s.pid = ::GetCurrentProcessId();
                         s.host = "contract-parent";
                         s.initialHeartbeatUtc = "2026-09-15T00:00:00.000Z";
                         return s;
                     }(),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(taker2.status(), AcquireStatus::HeldByOther);
}

// ---------------------------------------------------------------------
// PRJ-TX-7 场景三：崩溃后双实例并发接管仅一胜者（D-02）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 1 后段／§9.9 流程图尾段＋§9.9 并发接管注（"内核对
 * 共享模式的裁决天然原子——仅一个成功"）。前置：子进程实例 1 持锁→
 * TerminateProcess（崩溃语义：OS 立即关闭其全部句柄）。操作：两个竞争
 * 子进程在命名事件放行下**同时**各做一次获取尝试。预期：Exit Code 恰为
 * {0（胜者）,3（锁被持）} 各一——仅一胜者；锁记录 PID＝胜者 PID。
 * 观测点：退出码集合/记录 PID。
 */
TEST_F(LockContractTest, CrashThenConcurrentTakeover_SingleWinner_D02)
{
    setChildEnv(m_lockPath, /*holdMs=*/60000, /*intervalMs=*/10000,
                /*hbAgeSec=*/0);
    ChildProc holder;
    ASSERT_TRUE(spawnChild(&holder, "hold"));

    Win32LockOps ops;
    ASSERT_TRUE(waitForHolderPid(ops, m_lockPath.wstring(), holder.pid, 10000)
                    .has_value())
        << "前置失败：实例 1 未持锁";

    // 崩溃（非正常退出）：TerminateProcess＝OS 回收全部句柄（§9.1 崩溃
    // 释放语义）——锁文件内容残留、排他性即时消失。
    crashTerminate(holder);

    // 双竞争者就位：manual-reset 事件统一放行（一次 SetEvent 同时唤醒，
    // 保证竞争起点并发）；500 ms 余量等待两进程都进入等待态。
    const std::wstring eventName =
        L"Local\\ird-wp04-t03-race-" + std::to_wstring(::GetCurrentProcessId())
        + L"-" + std::to_wstring(::GetTickCount64());
    HANDLE go = ::CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    ASSERT_NE(go, nullptr);
    ::SetEnvironmentVariableW(L"IRD_T03_LOCK", m_lockPath.c_str());
    ::SetEnvironmentVariableW(L"IRD_T03_EVENT", eventName.c_str());
    ChildProc racerA;
    ChildProc racerB;
    ASSERT_TRUE(spawnChild(&racerA, "race"));
    ASSERT_TRUE(spawnChild(&racerB, "race"));
    ::Sleep(500);
    ::SetEvent(go);
    ::CloseHandle(go);

    const auto codeA = waitExit(racerA, 20000);
    const auto codeB = waitExit(racerB, 20000);
    ASSERT_TRUE(codeA.has_value()) << "竞争者 A 未退出";
    ASSERT_TRUE(codeB.has_value()) << "竞争者 B 未退出";

    // 恰一胜者：{0,3} 各一（0＝获取成功；3＝锁被持；其他码＝环境异常，
    // 直接失败——不给"双失败/双成功"留解释空间）。
    std::vector<DWORD> codes{*codeA, *codeB};
    std::sort(codes.begin(), codes.end());
    EXPECT_EQ(codes, (std::vector<DWORD>{0, 3}))
        << "并发接管未呈现『仅一胜者』（D-02 内核原子裁决破坏）——A="
        << *codeA << " B=" << *codeB;

    // 胜者身份落锁：记录 PID＝退出码 0 的那个进程。
    const DWORD winnerPid = *codeA == 0 ? racerA.pid : racerB.pid;
    const auto after = readHolderRecord(&ops, m_lockPath.wstring());
    ASSERT_TRUE(after.ok && after.parsed);
    EXPECT_EQ(after.record.pid, winnerPid) << "锁记录 PID 与胜者不符";
}

// ---------------------------------------------------------------------
// D-03 锁文件身份：跨崩溃-接管的永不删除重建（§9.4 防锁对象分裂）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 3 锁文件面／§9.4"文件本身**永不删除重建**（D-03）：
 * 删除会在旧锁对象与新锁对象间产生分裂窗口"。前置：实例 1 持锁（文件
 * 身份 A）。操作：崩溃实例 1→竞争者接管成功。预期：接管后文件身份 B
 * 与 A 逐字段相等（卷序号/文件索引/创建时间）＝同一文件对象存续——
 * 接管是"原地获取＋重写内容"，从未删除重建。观测点：GetFileInformation-
 * ByHandle 三元组。
 */
TEST_F(LockContractTest, LockFileIdentity_StableAcrossCrashTakeover_D03)
{
    setChildEnv(m_lockPath, /*holdMs=*/30000, /*intervalMs=*/10000,
                /*hbAgeSec=*/0);
    ChildProc holder;
    ASSERT_TRUE(spawnChild(&holder, "hold"));

    Win32LockOps ops;
    ASSERT_TRUE(waitForHolderPid(ops, m_lockPath.wstring(), holder.pid, 10000)
                    .has_value());
    const auto before = fileIdentity(m_lockPath.wstring());
    ASSERT_TRUE(before.has_value());

    crashTerminate(holder);

    // 单竞争者接管（事件创建即有信号——子进程打开后立即放行）。
    // 内核对象生存期注意：父进程句柄必须保持到子进程退出之后——若先
    // 关闭，对象引用计数归零即销毁，子进程的同名 CreateEventW 会**新建**
    // 一个未置位事件（实施期实测：子进程等满 15 s 超时 Exit 4）。
    const std::wstring eventName =
        L"Local\\ird-wp04-t03-ident-" + std::to_wstring(::GetTickCount64());
    HANDLE go = ::CreateEventW(nullptr, TRUE, TRUE /*初始有信号*/, eventName.c_str());
    ASSERT_NE(go, nullptr);
    ::SetEnvironmentVariableW(L"IRD_T03_LOCK", m_lockPath.c_str());
    ::SetEnvironmentVariableW(L"IRD_T03_EVENT", eventName.c_str());
    ChildProc taker;
    ASSERT_TRUE(spawnChild(&taker, "race"));

    const auto code = waitExit(taker, 20000);
    ::CloseHandle(go);  // 子进程已退出——现在关闭才安全
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(*code, 0UL) << "接管未成功（前置破坏）";

    const auto after = fileIdentity(m_lockPath.wstring());
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(*before == *after)
        << "锁文件身份改变＝崩溃接管中发生了删除重建（§9.4/D-03 违反——"
           "锁对象分裂窗口）";
}

// ---------------------------------------------------------------------
// §9.3 同项目多路径打开（跨拼写拒绝；跨进程形态）。
// ---------------------------------------------------------------------

/**
 * 锚定：acceptance 3 前半／§9.3"存储实例身份＝最终路径…同一规范路径的
 * 第二次 writable 打开…lock-held-by-other"（跨进程形态：拼写不同但
 * 规范化同源的锁文件，拒绝语义与路径拼写无关）。前置：子进程经规范
 * 路径持锁。操作：父进程以大写变体拼写构造 writable 打开。预期：拒绝
 * ＋持有 PID＝子进程（同一持有实例被不同拼写路径正确识别）。
 */
TEST_F(LockContractTest, SameProjectMultiPathOpen_CrossSpelling_Refused)
{
    setChildEnv(m_lockPath, /*holdMs=*/30000, /*intervalMs=*/10000,
                /*hbAgeSec=*/0);
    ChildProc holder;
    ASSERT_TRUE(spawnChild(&holder, "hold"));

    Win32LockOps ops;
    ASSERT_TRUE(waitForHolderPid(ops, m_lockPath.wstring(), holder.pid, 10000)
                    .has_value());

    // 大写变体拼写——规范化后与原拼写同一存储（§9.3 规范层收敛）。
    std::wstring variant = m_lockPath.wstring();
    for (auto& c : variant) { c = static_cast<wchar_t>(::towupper(c)); }
    const auto canonA = canonicalStorePath(m_lockPath.parent_path().wstring());
    const auto canonB = canonicalStorePath(
        fs::path(variant).parent_path().wstring());
    ASSERT_TRUE(canonA.ok && canonB.ok);
    ASSERT_TRUE(sameStorePath(canonA.canonical, canonB.canonical))
        << "前置失败：变体拼写未收敛到同一存储";

    StoreLock second(&ops, nullptr, variant,
                     [] {
                         LockSelfRecord s;
                         s.pid = ::GetCurrentProcessId();
                         s.host = "contract-parent";
                         s.initialHeartbeatUtc = "2026-09-15T00:00:00.000Z";
                         return s;
                     }(),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(second.status(), AcquireStatus::HeldByOther)
        << "变体拼写的 writable 打开未被拒绝（§9.3 多路径判定失效）";
    EXPECT_EQ(second.lastHolder().pid, holder.pid)
        << "变体拼写下持有 PID 识别错误";
}

// ---------------------------------------------------------------------
// 入口：子进程模式拦截（先于 gtest 初始化——子进程不进入测试框架）。
// ---------------------------------------------------------------------

int main(int argc, char** argv)
{
    // 子进程再执行分派：argv 标记为纯 ASCII——窄字符比较无损；实际参数
    // 已经环境变量传递（宽字符安全，见文件头说明）。两代分派 token 并存：
    // --ird-wp04-t03-child＝PRJ-T03 锁原语子进程（既有）；
    // --ird-wp04-t15-child＝PRJ-T15 真进程契约子进程（F8 崩溃边界/
    // TX-7 锁角色/TX-8 在途归档——Tx15Child.hpp 分派契约）。
    if (argc >= 3 && std::string(argv[1]) == "--ird-wp04-t03-child") {
        return runChildMode(argv[2]);
    }
    if (argc >= 3 && std::string(argv[1]) == "--ird-wp04-t15-child") {
        return sdurws::ird::project::tx15::runChild(argv[2]);
    }
    ::testing::InitGoogleTest(&argc, argv);
    // 机器可读测试报告（testkit §7.2"与 gtest XML 并存"；§7.3 原文签名
    // ——PRJ-T15 消费登记：IRD_TEST_INFO 追溯与 IRD_* 断言详情经监听器
    // 聚合为 ird-test-report.json，--ird_report=<path> 可指定落盘位置，
    // 参数在 gtest 解析前被摘除）。
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
