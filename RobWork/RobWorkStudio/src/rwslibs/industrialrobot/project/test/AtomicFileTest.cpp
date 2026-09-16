/**
 * @file   AtomicFileTest.cpp
 * @brief  存储原语（AtomicFile/IFileOps）用例组——publishNew/原子替换/
 *         write-through 语义与"失败不留半写目标"（PRJ-T02 acceptance 1/3）。
 *
 * 设计依据：
 *   - units/project.md §7.1（七步协议第 2/4/5 步：暂存写持久化、只增发布、
 *     HEAD 原子替换＝唯一提交点）、§7.2（Windows 文件操作保证——原子替换
 *     与写入持久性是不同保证，本组用例按保证分面断言）、§7.6（故障注入
 *     矩阵 F2/F4/F5 的单文件原语层——经 IFileOps 接缝注入，真实崩溃类
 *     F8 归 PRJ-T15 的 TestProcessRunner，不在本组）、§11 头注（消费
 *     testkit 的 FaultInterceptor 形态——本组以本地 fake 先行承接同型
 *     接缝消费，PRJ-T15 落 testkit 后按 FaultInterceptor 重述）；
 *   - 需求 NFR-REL-01（多文件事务的单文件原语层："失败或中断时旧版本
 *     保持完整"；"单文件'暂存＋原子替换'只用于版本目录内资源写入"）；
 *   - 任务契约 tasks/foundation/PRJ-T02.json：
 *     acceptance 1（publishNew/原子替换/write-through 语义单测：提交
 *     （替换）点之前旧文件字节不变、任一步失败不留半写目标；§7.2 不超诺
 *     口径——只断言 Windows API 实测保证，不断言超出文档的崩溃一致性，
 *     即本组零断电/零进程杀灭注入——F8 归 PRJ-T15）；
 *     acceptance 3（IFileOps 生产窄接口接缝就位：本文件 fake 经该接缝
 *     注入，生产代码零 testkit 头——D-10 形态的测试侧证据）。
 *
 * 接缝消费说明（acceptance 3 自证载体）：FaultFileOps 是 testkit.md D-10
 * 形态的最小本地实例——装饰真实 Win32FileOps、按 faultpoint 标识在指定
 * occurrence 注入失败、记录调用序。它实现的是 project 生产代码自有的
 * 窄接口（src/win32/IFileOps.hpp），不依赖任何 testkit 头（T-1 红线）；
 * PRJ-T15 契约测试将把它替换为 testkit FaultInterceptor 经同一接缝注入。
 */

#include "win32/AtomicFile.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdurws::ird::project::win32::AtomicFile;
namespace fp = sdurws::ird::project::win32::faultpoint;
using sdurws::ird::project::win32::FileResult;
using sdurws::ird::project::win32::IFileOps;
using sdurws::ird::project::win32::kWriteChunkSize;
using sdurws::ird::project::win32::Win32FileOps;

namespace {

/// 共享真实实现（无状态——进程级单实例足够；测试显式取用以装饰，
/// 与生产默认构造的静态实例同型，避免两份对象徒增符号）。
Win32FileOps& realOps()
{
    static Win32FileOps s_real;
    return s_real;
}

// ---------------------------------------------------------------------
// 夹具辅助：临时目录与文件读写（testkit TempDir 落地前的最小本地形态；
// §6.2 口径：用例自持目录、清理失败不静默——析构 remove_all 的错误经
// error_code 吸收但目录隔离保证不污染其他用例）。
// ---------------------------------------------------------------------

/// 二进制整读；读失败显性失败（不留"读不到＝内容不符"的假阳性通道）。
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

/// 独立于被测对象的裸写（造"旧版本"文件用——不经 AtomicFile，保证
/// 前置数据不依赖被测代码的正确性）。失败返回 false 由调用方 ASSERT。
bool writeRaw(const fs::path& file, const std::string& bytes)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        ADD_FAILURE() << "无法写前置文件: " << file.string();
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/**
 * @brief 故障注入 fake——IFileOps 接缝的测试侧实现（D-10 形态）。
 *
 * 形态＝装饰器：默认全部转发给真实 Win32FileOps（文件系统行为真实），
 * 仅在被计划命中的故障点上按 occurrence 注入失败（不转发＝该 Win32
 * 动作未发生）。同时记录调用序与命中数——供"闸门失败绝不进入可见性
 * 切换"类时序断言。
 */
class FaultFileOps : public IFileOps {
public:
    /// @param real [in] 非 owning 真实实现；生存期须覆盖本对象
    explicit FaultFileOps(IFileOps* real) : m_real(real) {}

    /// 登记注入计划：第 occurrence 次命中 faultPoint 时返回 osError。
    void failAt(const char* faultPoint, unsigned long occurrence,
                unsigned long osError)
    {
        m_plan[faultPoint] = Plan{occurrence, osError};
    }

    /// 全部接口调用的先后序（故障点标识串；时序断言输入）。
    const std::vector<const char*>& callOrder() const { return m_order; }

    /// 某故障点的累计命中次数（转发与注入都计——occurrence 语义）。
    int hits(const char* faultPoint) const
    {
        const auto it = m_counts.find(faultPoint);
        return it == m_counts.end() ? 0 : it->second;
    }

    // ---- IFileOps：记录→判定注入→转发 ----

    FileResult openWriteThrough(const std::wstring& path,
                                HANDLE* handle) override
    {
        return step(fp::kOpen,
                    [&] { return m_real->openWriteThrough(path, handle); });
    }

    FileResult writeChunk(HANDLE handle, const char* data,
                          std::size_t length) override
    {
        return step(fp::kWriteChunk, [&] {
            return m_real->writeChunk(handle, data, length);
        });
    }

    FileResult flush(HANDLE handle) override
    {
        return step(fp::kFlush,
                    [&] { return m_real->flush(handle); });
    }

    FileResult closeHandle(HANDLE handle) override
    {
        // 关闭不设故障点（IFileOps.hpp 契约：持久性闸门是 flush，
        // 关闭仅承载资源释放）——直接转发。
        return m_real->closeHandle(handle);
    }

    FileResult publishNew(const std::wstring& tempPath,
                          const std::wstring& targetPath) override
    {
        return step(fp::kPublishNew,
                    [&] { return m_real->publishNew(tempPath, targetPath); });
    }

    FileResult replaceExisting(const std::wstring& tempPath,
                               const std::wstring& targetPath) override
    {
        return step(fp::kReplaceExisting, [&] {
            return m_real->replaceExisting(tempPath, targetPath);
        });
    }

    // 目录操作两方法（PRJ-T07 随 IFileOps 增补的接缝面——本文件用例不注入
    // 目录故障，转发真实实现保持装饰器全转发形态；故障点
    // project/tx-engine/* 的注入消费在 TxEngineTest.cpp）。
    FileResult createDirectories(const std::wstring& path) override
    {
        return step(fp::kCreateDirectories,
                    [&] { return m_real->createDirectories(path); });
    }

    FileResult removeTree(const std::wstring& path) override
    {
        return step(fp::kRemoveTree,
                    [&] { return m_real->removeTree(path); });
    }

private:
    /// 单条注入计划（occurrence 从 1 计——FaultTrigger 同语义）。
    struct Plan
    {
        unsigned long occurrence = 1;
        unsigned long osError = 0;
    };

    /// 通用步进：计数→记序→命中计划则注入失败（不转发），否则执行真实操作。
    template <class Fn>
    FileResult step(const char* faultPoint, Fn&& real)
    {
        ++m_counts[faultPoint];
        m_order.push_back(faultPoint);
        const auto it = m_plan.find(faultPoint);
        if (it != m_plan.end() && it->second.occurrence
                == static_cast<unsigned long>(m_counts[faultPoint])) {
            FileResult injected;
            injected.ok = false;
            injected.osError = it->second.osError;
            return injected;  // 计划命中：真实动作不发生（rename 未执行等）
        }
        return real();
    }

    IFileOps* m_real;  ///< 非 owning 真实实现
    std::map<std::string, Plan> m_plan;     ///< 故障点→注入计划
    std::map<std::string, int> m_counts;    ///< 故障点→命中计数
    std::vector<const char*> m_order;       ///< 调用序（faultpoint 常量指针）
};

}  // namespace

// ---------------------------------------------------------------------
// 用例组：ProjectAtomicFile（suite 名登记入 ird-test-report.json）。
// ---------------------------------------------------------------------

/**
 * 测试夹具：每用例独立子目录（隔离失败残留）；套件级建/删总根。
 */
class AtomicFileTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 用例子目录：总根＋自增序号（进程内唯一；进程级冲突由
        // GetCurrentProcessId 隔离——并行进程互不共享目录）。
        m_dir = baseDir()
                / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "建临时目录失败: " << ec.message();
    }

    static void SetUpTestSuite()
    {
        // 总根：系统临时目录下＋进程 ID 后缀（同一机器并行多进程不冲突）。
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_atomic_test"
                 / std::to_string(::GetCurrentProcessId());
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        // 建根失败属环境不可用（EnvUnavailable 类）——直接致命退出，
        // 不产生"全部用例失败"的噪声报告（testkit §6.5 同口径精神）。
        if (ec) {
            std::cerr << "无法创建测试根目录: " << s_base.string() << " ("
                      << ec.message() << ")\n";
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 总根清理：失败保留现场（§6.2"TempDir 失败保留"），不静默掩盖。
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    /// 本用例专属目录（SetUp 建，随套件根统一删除）。
    const fs::path& dir() const { return m_dir; }

private:
    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;

    static fs::path baseDir() { return s_base; }
};

fs::path AtomicFileTest::s_base;
int AtomicFileTest::s_caseCounter = 0;

// ----------------------- writeThrough（持久性面）----------------------

/**
 * 锚定：NFR-REL-01／§7.1 第 2 步／§7.2 行 1——write-through 写后字节完整。
 *
 * 不超诺口径：本用例只断言 API 语义层（数据完整写入＋flush 闸门通过）；
 * "断电后仍在"不可进程内断言（§7.2 行 1 复核记录——留痕见 traceability/
 * builds/wp04-t02/），崩溃一致性归 F8/PRJ-T15。
 */
TEST_F(AtomicFileTest, WriteThrough_WritesExactBytes_NFR_REL_01)
{
    const auto target = dir() / L"staging.bin";
    const std::string payload = "head-record-payload-{}";
    AtomicFile af;

    const FileResult r = af.writeThrough(target.wstring(), payload.data(),
                                         payload.size());

    ASSERT_TRUE(r.ok) << "osError=" << r.osError;
    EXPECT_EQ(readAll(target), payload) << "写后字节必须与输入逐一相等";
}

/**
 * 锚定：§7.1 第 2 步（CREATE_ALWAYS 截断语义）——同路径重写不留旧字节。
 *
 * 暂存路径在同一事务目录内可能被重写（重试场景）；截断残留会伪造
 * "新内容"的字节视图（旧尾巴混入），因此必须整文件替换为本次输入。
 */
TEST_F(AtomicFileTest, WriteThrough_TruncatesExistingContent)
{
    const auto target = dir() / L"staging.bin";
    ASSERT_TRUE(writeRaw(target, std::string(1024, 'X'))) << "前置旧内容写入";
    const std::string payload = "short";
    AtomicFile af;

    const FileResult r = af.writeThrough(target.wstring(), payload.data(),
                                         payload.size());

    ASSERT_TRUE(r.ok) << "osError=" << r.osError;
    EXPECT_EQ(readAll(target), payload) << "重写后不得残留旧内容尾字节";
}

/**
 * 锚定：§7.1 第 2 步（空文件暂存合法）——零字节载荷（如空 ProjectMetadata
 * 占位）须走完整闸门链（打开→0 块写→flush→关闭）并成功。
 */
TEST_F(AtomicFileTest, WriteThrough_EmptyBytes_CreatesEmptyFile)
{
    const auto target = dir() / L"empty.staging";
    AtomicFile af;

    const FileResult r = af.writeThrough(target.wstring(), nullptr, 0);

    ASSERT_TRUE(r.ok) << "空载荷须成功（osError=" << r.osError << "）";
    EXPECT_TRUE(fs::exists(target));
    EXPECT_EQ(fs::file_size(target), std::uintmax_t{0});
}

/**
 * 锚定：acceptance 1"任一步失败不留半写目标"／testkit §6.4 打开失败——
 * 打开失败时目标路径上不产生任何文件（无半写可能）。
 */
TEST_F(AtomicFileTest, WriteThrough_OpenFailure_CreatesNothing)
{
    const auto target = dir() / L"never-created.bin";
    FaultFileOps fake(&realOps());
    fake.failAt(fp::kOpen, 1, ERROR_ACCESS_DENIED);
    AtomicFile af(&fake);

    const FileResult r = af.writeThrough(target.wstring(), "data", 4);

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.osError, static_cast<unsigned long>(ERROR_ACCESS_DENIED))
        << "透传注入的原始错误码（事务层分类依据）";
    EXPECT_FALSE(fs::exists(target)) << "打开失败不得产生文件";
    EXPECT_EQ(fake.hits(fp::kWriteChunk), 0)
        << "打开失败后不得进入写步骤（时序中止）";
}

/**
 * 锚定：acceptance 1"任一步失败不留半写目标"／§7.6 F2（写中途失败）——
 * 第 2 块写失败：暂存残留恰为已成功块数，已提交目标字节不变。
 *
 * 残留是设计行为（§7.1 第 2 步"残留部分文件于 .staging"——恢复扫描
 * 处置）；断言"残留大小＝kWriteChunkSize"证明失败边界精确落在注入点，
 * 且证明提交目标（此处另立的 committed.bin）全程未被触碰。
 */
TEST_F(AtomicFileTest, WriteChunk_FailureMidway_ResidueConfined_NFR_REL_01)
{
    const auto staging = dir() / L"staging.bin";
    const auto committed = dir() / L"committed.bin";
    const std::string oldCommitted = "old-committed-bytes";
    ASSERT_TRUE(writeRaw(committed, oldCommitted));

    // 数据跨 3 块（2×64KiB＋10 字节尾块）；计划在第 2 块注入失败
    // （磁盘满模拟——testkit §6.4"写入 N 字节后失败"）。
    const std::string payload(2 * kWriteChunkSize + 10, 'P');
    FaultFileOps fake(&realOps());
    fake.failAt(fp::kWriteChunk, 2, ERROR_DISK_FULL);
    AtomicFile af(&fake);

    const FileResult r = af.writeThrough(staging.wstring(), payload.data(),
                                         payload.size());

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.osError, static_cast<unsigned long>(ERROR_DISK_FULL));
    ASSERT_TRUE(fs::exists(staging)) << "暂存残留＝设计行为（恢复扫描处置）";
    EXPECT_EQ(fs::file_size(staging), std::uintmax_t{kWriteChunkSize})
        << "残留须恰为第 1 块字节数（失败边界＝第 2 块注入点）";
    EXPECT_EQ(readAll(committed), oldCommitted)
        << "已提交目标字节不变（NFR-REL-01 单文件层）";
}

/**
 * 锚定：§7.2 行 1（持久性闸门）——flush 失败＝持久性承诺不成立，
 * writeThrough 整体失败；闸门在打开/写之后、任何发布之前（时序断言）。
 *
 * 原子/持久性分离语义（acceptance 1）：持久性闸门失败时，按协议调用方
 * 必须放弃发布——本断言锁定原语层"失败如实上报"，不给"字节已在文件里
 * 就当成功"的假阳性通道。
 */
TEST_F(AtomicFileTest, Flush_FailureFailsDurabilityGate_NeverPublishes)
{
    const auto target = dir() / L"staging.bin";
    FaultFileOps fake(&realOps());
    fake.failAt(fp::kFlush, 1, ERROR_DISK_FULL);
    AtomicFile af(&fake);

    const FileResult r = af.writeThrough(target.wstring(), "payload", 7);

    EXPECT_FALSE(r.ok) << "flush 失败＝未过持久性闸门，整体失败";
    EXPECT_EQ(r.osError, static_cast<unsigned long>(ERROR_DISK_FULL));
    // 时序：publishNew/replaceExisting 属调用方协议动作，AtomicFile 的
    // writeThrough 内部从不触发——fake 的调用序只能含 open/write/flush，
    // 出现任何发布点即门面越权（结构性保证的直接断言）。
    for (const char* point : fake.callOrder()) {
        EXPECT_NE(point, fp::kPublishNew);
        EXPECT_NE(point, fp::kReplaceExisting);
    }
}

// ----------------------- publishNew（只增发布面）----------------------

/**
 * 锚定：§7.1 第 4 步 publishNew／§7.2 行 2——目标不存在时 rename 就位，
 * 暂存名消失、目标字节完整（原子可见：读者见旧名或新名，无中间态）。
 */
TEST_F(AtomicFileTest, PublishNew_MovesTempToAbsentTarget)
{
    const auto temp = dir() / L"obj.staging";
    const auto target = dir() / L"obj";
    const std::string payload = "object-bytes";
    AtomicFile af;
    ASSERT_TRUE(af.writeThrough(temp.wstring(), payload.data(),
                                payload.size())
                    .ok)
        << "前置暂存写须成功";

    const FileResult r = af.publishNew(temp.wstring(), target.wstring());

    ASSERT_TRUE(r.ok) << "osError=" << r.osError;
    EXPECT_TRUE(fs::exists(target)) << "目标就位";
    EXPECT_FALSE(fs::exists(temp)) << "暂存名经 rename 消失（非复制）";
    EXPECT_EQ(readAll(target), payload);
}

/**
 * 锚定：acceptance 1"提交（替换）点之前旧文件字节不变"／§7.2 行 2
 * （目标已存在时不覆盖）——只增发布遇到已存在目标必须失败且两路径
 * 内容均保持原状（共享判定归上层对象库 PRJ-T05，本层一律拒绝）。
 */
TEST_F(AtomicFileTest, PublishNew_TargetExists_FailsAndKeepsBothSides)
{
    const auto temp = dir() / L"obj.staging";
    const auto target = dir() / L"obj";
    const std::string existingTarget = "already-published-object";
    ASSERT_TRUE(writeRaw(target, existingTarget));
    const std::string payload = "new-object-bytes";
    AtomicFile af;
    ASSERT_TRUE(af.writeThrough(temp.wstring(), payload.data(),
                                payload.size())
                    .ok);

    const FileResult r = af.publishNew(temp.wstring(), target.wstring());

    EXPECT_FALSE(r.ok) << "目标已存在＝只增语义拒绝覆盖";
    EXPECT_EQ(r.osError, static_cast<unsigned long>(ERROR_ALREADY_EXISTS))
        << "真实 MoveFileExW（无 REPLACE）的文档失败码——§7.2 行 2 复核口径";
    EXPECT_EQ(readAll(target), existingTarget) << "旧文件字节不变";
    EXPECT_TRUE(fs::exists(temp)) << "暂存保留（供上层共享判定/诊断）";
    EXPECT_EQ(readAll(temp), payload);
}

// --------------------- replaceFile（原子替换面＝提交点）---------------

/**
 * 锚定：§7.1 第 5 步（唯一提交点）／§7.2 行 3——替换后目标整体呈现
 * 新内容（读者见旧或新，无半文件），暂存名消失。
 */
TEST_F(AtomicFileTest, ReplaceFile_SwapsTargetToNewContent)
{
    const auto temp = dir() / L"HEAD.new";
    const auto target = dir() / L"HEAD";
    ASSERT_TRUE(writeRaw(target, "old-head-record"));
    const std::string newHead = "new-head-record";
    AtomicFile af;
    ASSERT_TRUE(af.writeThrough(temp.wstring(), newHead.data(),
                                newHead.size())
                    .ok);

    const FileResult r = af.replaceFile(temp.wstring(), target.wstring());

    ASSERT_TRUE(r.ok) << "osError=" << r.osError;
    EXPECT_EQ(readAll(target), newHead) << "提交点后目标＝新内容整体";
    EXPECT_FALSE(fs::exists(temp)) << "替换完成暂存名消失";
}

/**
 * 锚定：acceptance 1 核心语句"提交（替换）点之前旧文件字节不变"——
 * 持久性闸门通过（HEAD.new 已落盘）但替换未发生时，HEAD 保持旧字节。
 *
 * 这是 NFR-REL-01"失败或中断时旧版本保持完整"在单文件层的微观时序：
 * 新内容先落盘到独立路径，旧内容在提交点前逐字节可复核。
 */
TEST_F(AtomicFileTest, ReplaceFile_BeforeCommitPoint_OldBytesStable)
{
    const auto head = dir() / L"HEAD";
    const std::string oldHead = "committed-head-v1";
    ASSERT_TRUE(writeRaw(head, oldHead));
    const auto headNew = dir() / L"HEAD.new";
    const std::string newHead = "committed-head-v2";
    AtomicFile af;
    ASSERT_TRUE(af.writeThrough(headNew.wstring(), newHead.data(),
                                newHead.size())
                    .ok);

    // 提交点之前：HEAD.new 已持久化，但 HEAD 必须仍是旧字节。
    EXPECT_EQ(readAll(head), oldHead)
        << "暂存已持久化不等于已提交——HEAD 在替换前必须字节不变";

    // 提交点之后：整体切换（与上一断言同用例构成"前不变/后整体新"对照）。
    ASSERT_TRUE(af.replaceFile(headNew.wstring(), head.wstring()).ok);
    EXPECT_EQ(readAll(head), newHead);
}

/**
 * 锚定：acceptance 1"任一步失败不留半写目标"／§7.6 F5（HEAD 切换失败）
 * 的单文件层——替换步骤注入失败：旧目标字节不变、暂存保留（可重试）。
 */
TEST_F(AtomicFileTest, ReplaceFile_FailureKeepsOldTargetIntact_NFR_REL_01)
{
    const auto temp = dir() / L"HEAD.new";
    const auto target = dir() / L"HEAD";
    const std::string oldHead = "old-head-bytes";
    ASSERT_TRUE(writeRaw(target, oldHead));
    FaultFileOps fake(&realOps());
    fake.failAt(fp::kReplaceExisting, 1, ERROR_SHARING_VIOLATION);
    AtomicFile af(&fake);
    const std::string newHead = "new-head-bytes";
    ASSERT_TRUE(af.writeThrough(temp.wstring(), newHead.data(),
                                newHead.size())
                    .ok);

    const FileResult r = af.replaceFile(temp.wstring(), target.wstring());

    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.osError, static_cast<unsigned long>(ERROR_SHARING_VIOLATION));
    EXPECT_EQ(readAll(target), oldHead) << "替换失败＝旧内容完整（无半写）";
    EXPECT_TRUE(fs::exists(temp)) << "暂存保留（F5 残留语义——可识别未提交）";
}

/**
 * 锚定：§7.6 F4（发布部分失败）的单文件层——publishNew 注入失败：
 * 目标不存在时不产生半写目标，暂存保留。
 */
TEST_F(AtomicFileTest, PublishNew_FailureCreatesNoTarget_NFR_REL_01)
{
    const auto temp = dir() / L"obj.staging";
    const auto target = dir() / L"obj";
    FaultFileOps fake(&realOps());
    fake.failAt(fp::kPublishNew, 1, ERROR_ACCESS_DENIED);
    AtomicFile af(&fake);
    ASSERT_TRUE(af.writeThrough(temp.wstring(), "obj", 3).ok);

    const FileResult r = af.publishNew(temp.wstring(), target.wstring());

    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(fs::exists(target)) << "rename 未执行＝无目标出现";
    EXPECT_TRUE(fs::exists(temp)) << "暂存保留（F4 残留语义）";
}

/**
 * 锚定：acceptance 3（IFileOps 接缝消费面）＋§7.2 行 6——默认构造绑定
 * 真实实现（生产路径零注入也能工作），且三原语在同一门面上协同完成
 * "暂存→只增发布"链（行 6"不使用 ReplaceFileW"由 AtomicFile.cpp 实现
 * 复核承载——本用例证明真实链路可用）。
 */
TEST_F(AtomicFileTest, DefaultCtor_RealChain_StagingThenPublish)
{
    const auto temp = dir() / L"meta.staging";
    const auto target = dir() / L"metadata";
    const std::string payload = "{\"k\":\"v\"}";

    // 默认构造＝生产装配形态（内部绑定无状态 Win32FileOps）。
    AtomicFile af;
    ASSERT_TRUE(
        af.writeThrough(temp.wstring(), payload.data(), payload.size()).ok);
    ASSERT_TRUE(af.publishNew(temp.wstring(), target.wstring()).ok);

    EXPECT_EQ(readAll(target), payload);
    EXPECT_FALSE(fs::exists(temp));
}
