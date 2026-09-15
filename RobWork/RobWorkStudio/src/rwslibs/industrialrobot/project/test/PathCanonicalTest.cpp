/**
 * @file   PathCanonicalTest.cpp
 * @brief  路径规范化（PathCanonical）用例组——规范化一致性/大小写不敏感/
 *         同项目多路径打开（PRJ-T03 acceptance 3）。
 *
 * 设计依据：
 *   - units/project.md §9.3（路径规范化与同项目多路径打开：存储实例身份
 *     ＝GetFinalPathNameByHandleW 最终路径；Windows 默认大小写不敏感比较
 *     〔NTFS〕；8.3 短名/相对路径经规范层统一后不再歧义；同一规范路径的
 *     第二次 writable 打开＝拒绝 lock-held-by-other——进程内防自我双写）、
 *     §7.2 行 4（GetFinalPathNameByHandleW 官方口径）；
 *   - 任务契约 tasks/foundation/PRJ-T03.json acceptance 3（PathCanonical
 *     用例：路径规范化与同项目多路径打开）。
 *
 * 边界声明：subst/符号链接拼写在多数环境需要特权或改动机器全局状态
 * （subst 盘符映射进程全局、符号链接需 SeCreateSymbolicLinkPrivilege），
 * 不在用例内构造——API 解析能力由 §7.2 行 4 文档口径与 PRJ-T02 复核留痕
 * 承载；本组以无特权拼写族（大小写/斜杠/冗余段/相对路径/8.3 短名）验证
 * 规范层收敛性与锁判定联动。8.3 短名在卷上禁用时（NtfsDisable8dot3Name-
 * Creation）用例如实降级为 SUCCEED 注记——不伪造通过。
 */

#include "win32/PathCanonical.hpp"
#include "win32/StoreLock.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using sdurws::ird::project::win32::AcquireStatus;
using sdurws::ird::project::win32::canonicalStorePath;
using sdurws::ird::project::win32::LockSelfRecord;
using sdurws::ird::project::win32::sameStorePath;
using sdurws::ird::project::win32::StoreLock;
using sdurws::ird::project::win32::utcNowIsoMilli;
using sdurws::ird::project::win32::Win32LockOps;

namespace {

// 进程级共享 sink/ops（本组无诊断断言依赖，null sink 走"可空"装配口径）。
Win32LockOps& sharedOps()
{
    static Win32LockOps s_ops;
    return s_ops;
}

/// 用例自持临时目录（StoreLockTest 同款最小形态；目录名含用例名＋PID）。
class PathCanonicalTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info =
            ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp04_t03_pathcanon"
                / (std::string(info->name()) + "_"
                   + std::to_string(::GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir / L"store.rwdesign" / L"sub", ec);
        ASSERT_FALSE(ec) << "临时目录创建失败: " << m_dir.string();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败";
    }

    /// 项目根（存储根）目录路径。
    fs::path storeDir() const { return m_dir / L"store.rwdesign"; }

    /// 进程内自我身份（锁联动用例的构造参数）。
    static LockSelfRecord self()
    {
        LockSelfRecord s;
        s.pid = ::GetCurrentProcessId();
        s.host = "path-canonical-test";
        s.initialHeartbeatUtc = utcNowIsoMilli();
        return s;
    }

    fs::path m_dir;  ///< 用例临时目录
};

}  // namespace

/**
 * 锚定：acceptance 3（路径规范化）／§7.2 行 4——已存在目录解析成功且
 * 呈现 VOLUME_NAME_DOS 形态（"\\\\?\\" 前缀＝规范形态的组成部分，不剥离）。
 */
TEST_F(PathCanonicalTest, Canonicalize_ExistingDir_FinalPathForm)
{
    const auto res = canonicalStorePath(storeDir().wstring());
    ASSERT_TRUE(res.ok) << "规范化失败: " << res.osError;
    EXPECT_EQ(res.osError, 0UL);
    // 前缀断言：\\?\（超长路径安全的完整 DOS 形态——规范形态的稳定观测量）。
    EXPECT_EQ(res.canonical.substr(0, 4), L"\\\\?\\");
    // 解析结果必须真实指向目标（以规范化路径再解析＝幂等）。
    const auto again = canonicalStorePath(res.canonical);
    ASSERT_TRUE(again.ok);
    EXPECT_TRUE(sameStorePath(res.canonical, again.canonical))
        << "规范化不幂等（§9.3 存储身份的稳定性破坏）";
}

/**
 * 锚定：acceptance 3（同项目多路径打开的规范层基础）／§9.3"8.3 短名/
 * 相对路径经规范层统一后不再歧义"——无特权拼写族全部收敛到同一规范
 * 形态：大小写变体、正斜杠、尾分隔符、冗余中间段（sub\..\）。
 * 观测点：canonical 逐对 sameStorePath 判等。
 */
TEST_F(PathCanonicalTest, SameProject_DifferentSpellings_CanonicalizeEqual)
{
    const auto base = canonicalStorePath(storeDir().wstring());
    ASSERT_TRUE(base.ok);

    // 大小写变体（NTFS 默认大小写不敏感——§9.3）。
    std::wstring upperCase = storeDir().wstring();
    for (auto& c : upperCase) { c = static_cast<wchar_t>(::towupper(c)); }
    // 正斜杠变体（Win32 路径 API 双斜杠兼容）。
    std::wstring forwardSlash = storeDir().wstring();
    for (auto& c : forwardSlash) {
        if (c == L'\\') { c = L'/'; }
    }
    // 尾分隔符变体。
    const std::wstring trailing = storeDir().wstring() + L"\\";
    // 冗余中间段（进入 sub 再回退——拼写不同、目标相同）。
    const std::wstring withDotDot =
        (storeDir() / L"sub" / L"..").wstring();

    const std::vector<std::pair<std::wstring, const char*>> spellings = {
        {upperCase, "upperCase"},
        {forwardSlash, "forwardSlash"},
        {trailing, "trailing"},
        {withDotDot, "withDotDot"},
    };
    for (const auto& [spelling, name] : spellings) {
        const auto res = canonicalStorePath(spelling);
        ASSERT_TRUE(res.ok) << name << " 规范化失败: " << res.osError;
        EXPECT_TRUE(sameStorePath(base.canonical, res.canonical))
            << name << " 未收敛到同一规范形态: "
            << std::string(res.canonical.begin(), res.canonical.end());
    }
}

/**
 * 锚定：§9.3"相对路径经规范层统一"——以进程工作目录为基的相对拼写与
 * 绝对拼写收敛到同一规范形态。工作目录改动用 RAII 守卫恢复（不影响
 * 其他用例；gtest 用例串行执行）。
 */
TEST_F(PathCanonicalTest, RelativeSpelling_CanonicalizesToSameStore)
{
    const auto base = canonicalStorePath(storeDir().wstring());
    ASSERT_TRUE(base.ok);

    // RAII 守卫：改 cwd → 用例结束恢复（清理路径也用绝对路径，双保险）。
    const fs::path savedCwd = fs::current_path();
    struct CwdGuard {
        const fs::path& saved;
        ~CwdGuard() { std::error_code ec; fs::current_path(saved, ec); }
    } guard{savedCwd};

    std::error_code ec;
    fs::current_path(m_dir, ec);
    ASSERT_FALSE(ec);

    const auto res = canonicalStorePath(std::wstring(L"store.rwdesign"));
    ASSERT_TRUE(res.ok) << "相对路径规范化失败: " << res.osError;
    EXPECT_TRUE(sameStorePath(base.canonical, res.canonical))
        << "相对路径未收敛到同一规范形态（§9.3）";
}

/**
 * 锚定：§9.3"8.3 短名"——短名拼写（如 STORE~1）必须解析到同一存储。
 * 卷禁用 8.3 名生成时（GetShortPathNameW 返回原串）如实降级 SUCCEED
 * ——不伪造通过（验证留痕如实陈述口径）。
 */
TEST_F(PathCanonicalTest, ShortPathSpelling_ResolvedWhenAvailable)
{
    const auto base = canonicalStorePath(storeDir().wstring());
    ASSERT_TRUE(base.ok);

    const DWORD shortLen = ::GetShortPathNameW(storeDir().c_str(), nullptr, 0);
    ASSERT_NE(shortLen, 0UL) << "GetShortPathNameW 失败: " << ::GetLastError();
    std::wstring shortPath(shortLen, L'\0');
    const DWORD written =
        ::GetShortPathNameW(storeDir().c_str(), shortPath.data(), shortLen);
    ASSERT_NE(written, 0UL);
    shortPath.resize(written);

    if (shortPath == storeDir().wstring()) {
        // 卷禁用 8.3 名生成：短名拼写不存在——如实记录降级，不断言。
        SUCCEED() << "当前卷未生成 8.3 短名（NtfsDisable8dot3NameCreation），"
                     "短名收敛用例降级——API 解析能力由 §7.2 行 4 文档口径承载";
        return;
    }
    const auto res = canonicalStorePath(shortPath);
    ASSERT_TRUE(res.ok);
    EXPECT_TRUE(sameStorePath(base.canonical, res.canonical))
        << "8.3 短名未解析到同一存储（§9.3 规范层缺陷）";
}

/**
 * 锚定：规范形态的区分度——不同目录不得误判同一（sameStorePath 的
 * 反例面；存储实例身份的判定既不漏判也不冤判）。
 */
TEST_F(PathCanonicalTest, DifferentDirectories_Differ)
{
    const auto a = canonicalStorePath((m_dir / L"store.rwdesign").wstring());
    const auto b = canonicalStorePath((m_dir / L"store.rwdesign" / L"sub").wstring());
    const auto c = canonicalStorePath(m_dir.wstring());
    ASSERT_TRUE(a.ok && b.ok && c.ok);
    EXPECT_FALSE(sameStorePath(a.canonical, b.canonical));
    EXPECT_FALSE(sameStorePath(a.canonical, c.canonical));
    EXPECT_FALSE(sameStorePath(b.canonical, c.canonical));
}

/**
 * 锚定：错误语义——不存在的路径＝环境错误，原始码如实返回（打开协议
 * PM-02 步骤①的存在预检数据面）；空输入＝契约违约面（值语义失败，
 * ERROR_INVALID_PARAMETER）。
 */
TEST_F(PathCanonicalTest, NonexistentAndEmpty_FailWithOsError)
{
    const auto missing =
        canonicalStorePath((m_dir / L"no-such-dir").wstring());
    EXPECT_FALSE(missing.ok);
    EXPECT_EQ(missing.canonical, std::wstring());
    EXPECT_EQ(missing.osError, 2UL /*ERROR_FILE_NOT_FOUND*/);

    const auto empty = canonicalStorePath(std::wstring());
    EXPECT_FALSE(empty.ok);
    EXPECT_EQ(empty.osError, 87UL /*ERROR_INVALID_PARAMETER*/);
}

/**
 * 锚定：§9.3 同项目多路径打开（acceptance 3 第二半句）——同一存储经
 * 两种拼写打开时，第二次 writable 打开必须被拒绝（进程内防自我双写；
 * 锁判定以规范路径为身份前提）。操作：以拼写 A 持有写锁→以拼写 B
 * （大小写变体）再获取。预期：拒绝（HeldByOther）＋持有 PID＝本进程
 * （isSelf 语义的数据面——§9.3"进程内重复打开"判定依据）。
 */
TEST_F(PathCanonicalTest, SameProjectMultiPathOpen_SecondWritableRefused)
{
    const auto canonicalA = canonicalStorePath(storeDir().wstring());
    ASSERT_TRUE(canonicalA.ok);

    // 大小写变体拼写 B（规范化后与 A 同一存储的另一种用户输入形态）。
    std::wstring spellingB = storeDir().wstring();
    for (auto& c : spellingB) { c = static_cast<wchar_t>(::towupper(c)); }
    const auto canonicalB = canonicalStorePath(spellingB);
    ASSERT_TRUE(canonicalB.ok);
    ASSERT_TRUE(sameStorePath(canonicalA.canonical, canonicalB.canonical))
        << "前置失败：两种拼写未被规范层收敛为同一存储";

    const fs::path lockA = storeDir() / L"lock";
    Win32LockOps& ops = sharedOps();

    // 实例 1：经拼写 A 获取写锁（持有）。
    StoreLock holder(&ops, nullptr, lockA.wstring(), self(),
                     std::chrono::milliseconds(0));
    ASSERT_EQ(holder.status(), AcquireStatus::Held);

    // 实例 2：经拼写 B 以 writable 意图打开——锁文件同目录（同一存储），
    // 内核按物理文件互斥（规范化保证这"应当"是同一存储的判定基础）。
    const fs::path lockB = fs::path(spellingB) / L"lock";
    StoreLock second(&ops, nullptr, lockB.wstring(), self(),
                     std::chrono::milliseconds(0));
    EXPECT_EQ(second.status(), AcquireStatus::HeldByOther)
        << "同项目多路径第二次 writable 打开未被拒绝（§9.3 防自我双写）";
    EXPECT_EQ(second.lastHolder().pid, ::GetCurrentProcessId())
        << "持有者 PID 应为本进程（进程内重复打开的 isSelf 判定数据面）";
}
