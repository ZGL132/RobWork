/**
 * @file   BudgetTest.cpp
 * @brief  BudgetGuard 用例组（IoCsv/IoSec）——流式行预算（IO-V06）、比较
 *         型三要素诊断（acceptance 3）、双保险记账（§4.4/§4.5.2）、账本
 *         可观测、饱和与 scope 树回收、包导入通道强化（P-IO-4 数值钉住）。
 *
 * 设计依据：
 *   - units/io.md §11.2 IO-V06 行（行预算设 10⁵、第 10⁵+1 行触发
 *     IO-SEC-BUDGET-ROWS、峰值内存有界——"BudgetGuard 行数维度经流式
 *     读取路径生效"）、§4.4（防护流程③⑤：预检＋展开检双保险；检查点
 *     即取消点）、§4.5.1（16 维数值表——P-IO-4 Draft 档位钉住）、
 *     §4.5.2（语义细则：三要素/饱和/双侧比较/多阶段累计/包导入四维）
 *   - 需求 NFR-SEC-02（超限拒绝＋诊断）、NFR-PERF-03（V06 需求列）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 2/3/5
 *
 * 范围声明（IO-T02 交付边界）：CSV 读写器本体随 IO-T03（§12）落位；V06
 * 的"流式读取路径"在本任务以**最小流式行循环**（std::ifstream＋getline，
 * 每行经 BudgetGuard 检查点入账）承载——验证的是 BudgetGuard 行维在流式
 * 消费路径上的语义与内存有界性；IO-T03 的 ICsvReader 将把其行检查点接到
 * 同一 BudgetGuard 行维（同一守卫、同一码面），本组用例届时零改动复用。
 */

// windows/psapi 先于 gtest 包含（时序硬约束）：以 PSAPI_VERSION=2 缺省
// 词形取得 K32GetProcessMemoryInfo 声明（kernel32 导出——kernel32.lib 为
// MSVC 缺省链接库，零词表外库）。实测教训：若让 gtest 先行包含 windows
// 头，本文件的 PSAPI 宏时序即不可控（K32 声明缺失/psapi.lib 链接两面
// 都翻过车——ird_gates IRD-GATE-LIB 拦 Psapi.lib 后定稿此序）。
// NOMINMAX：windows.h 的 min/max 宏会击中 std::max（冒烟模式无全局
// NOMINMAX，必须本文件自带）。
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <sdurws/ird/io/Budget.hpp>

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetLedgerSnapshot;
using sdurws::ird::io::BudgetScopeId;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::IBudgetGuardPtr;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::makeBudgetGuard;

namespace {

/// 进程私有内存（PrivateUsage＝提交字节——峰值内存有界断言的计量面）。
/// 经 kernel32!K32GetProcessMemoryInfo 动态解析直调（GetProcAddress——
/// 零词表外链接面 [ird_gates IRD-GATE-LIB 零命中]、零 SDK 头声明冲突
/// 〔集成/冒烟两模式 PSAPI 宏环境不同，静态声明两面翻车后定稿此形〕；
/// kernel32 必随进程加载——句柄不释放，进程退出即回收）。
std::uint64_t privateUsageBytes()
{
    // 函数指针类型自持（psapi.h 的结构体定义不随 PSAPI_VERSION 变化，
    // 两模式一致可用；BOOL=int、HANDLE=void*、DWORD=unsigned long 对齐
    // SDK 原签名）。
    using K32GetProcessMemoryInfoFn = int(__stdcall*)(void*, PROCESS_MEMORY_COUNTERS*, unsigned long);
    static const K32GetProcessMemoryInfoFn k32GetProcessMemoryInfo =
        reinterpret_cast<K32GetProcessMemoryInfoFn>(
            reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"),
                                                     "K32GetProcessMemoryInfo")));
    if (k32GetProcessMemoryInfo == nullptr) {
        return 0;           // 解析失败（非 Windows 语义环境）——观测面退化
    }
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    // PrivateUsage 在 _EX 扩展结构——按 SDK 口径以基结构指针视图传入。
    if (!k32GetProcessMemoryInfo(::GetCurrentProcess(),
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        return 0;
    }
    return pmc.PrivateUsage;
}

/// 从 params 取键值（比较型三要素断言用）。
std::string paramOf(const IoResult<void>& r, const char* key)
{
    for (const auto& kv : r.error.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 用例自持临时目录（SafePathTest 同款形态）。
class BudgetTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp11_t03_budget"
                / (std::string(info->name()) + "_" + std::to_string(::GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败";
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败";
    }

    fs::path m_dir;   ///< 用例临时根
};

} // namespace

// =====================================================================
// IO-V06：IoCsv/StreamBudget——行预算（设 10⁵）经流式读取路径生效
// =====================================================================

/**
 * 生成 10⁶ 行 CSV（流式生成器——写盘不驻留），行预算收紧至 10⁵：流式
 * 逐行消费在第 100001 笔检查点触发 IO-SEC-BUDGET-ROWS（三要素 actual/
 * limit/unit 齐）；全程峰值提交内存有界（远小于整文件驻留量——V06 观
 * 测点"峰值内存有界"）。
 */
TEST_F(BudgetTest, StreamBudgetRowsOverBudgetTriggersIoSecBudgetRowsWithBoundedMemory)
{
    // 追溯登记（IO-T07——units/io.md §11.2 IO-V06 行；ird-test-report.json
    // 需求/AT 字段，testkit.md §7.3 IRD_TEST_INFO）。
    IRD_TEST_INFO(std::vector<std::string>{"NFR-SEC-02", "NFR-PERF-03"},
                  std::vector<std::string>{});
    // ①生成 10⁶ 行 CSV（每行 "i,cell-i"≈12 字节——文件约 12 MB）。
    const std::uint64_t kTotalRows = 1000000;
    const fs::path csv = m_dir / L"big.csv";
    {
        std::ofstream out(csv, std::ios::binary);
        ASSERT_TRUE(out.is_open());
        std::string buf;
        buf.reserve(1 << 16);
        for (std::uint64_t i = 1; i <= kTotalRows; ++i) {
            buf += std::to_string(i);
            buf += ",cell-";
            buf += std::to_string(i);
            buf += "\r\n";
            if (buf.size() >= (1u << 16) - 64) {       // 批量刷盘——生成器自身不驻留大块
                out << buf;
                buf.clear();
            }
        }
        out << buf;
    }
    const auto fileSize = static_cast<std::uint64_t>(fs::file_size(csv));

    // ②行预算收紧至 10⁵（§11.2 IO-V06 行"行预算设 10⁵"——tighten 只收
    //   紧，永远合法 §4.5.2）。
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::CsvRowCount, 100000);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    // ③流式行循环（最小流式读取路径）：getline 逐行——内存面＝单行＋
    //   流缓冲，不随文件规模增长；每行一笔 charge（检查点粒度＝每行，
    //   §4.4⑤）。
    std::ifstream in(csv, std::ios::binary);
    ASSERT_TRUE(in.is_open());
    std::uint64_t lines = 0;
    std::uint64_t peakUsage = privateUsageBytes();
    const std::uint64_t baseline = peakUsage;
    IoResult<void> charge;
    std::string line;
    while (std::getline(in, line)) {
        ++lines;
        charge = guard->charge(scope, BudgetDimension::CsvRowCount, 1);
        if (!charge) {
            break;                                      // 预算检查点命中——中止（§4.4⑤"超限即中止"）
        }
        if ((lines % 10000) == 0) {                     // 峰值采样（每万行＋终态）
            peakUsage = (std::max)(peakUsage, privateUsageBytes());
        }
    }
    peakUsage = (std::max)(peakUsage, privateUsageBytes());

    // ④断言：恰在第 100001 行触发；码与三要素正确。
    ASSERT_FALSE(charge) << "行预算 10⁵ 必须在第 100001 笔检查点拒绝";
    EXPECT_EQ(lines, 100001u) << "第 10⁵+1 行触发（§11.2 IO-V06 预期结果）";
    EXPECT_EQ(charge.error.code, IoErrorCode::SecBudgetRows) << charge.error.detail;
    EXPECT_EQ(paramOf(charge, "actual"), "100001") << "三要素·实际值＝触发时潜在累计";
    EXPECT_EQ(paramOf(charge, "limit"), "100000") << "三要素·上限＝生效限额（tighten 后）";
    EXPECT_EQ(paramOf(charge, "unit"), "count") << "三要素·单位＝行计数";

    // ⑤峰值内存有界（V06 观测点）：循环期进程提交内存增量远小于文件
    //   本体（整文件驻留增量≥fileSize；流式读取只应增加缓冲量级）。取
    //   文件大小的 1/2 为上界——宽裕但足以区分"驻留"与"流式"。
    EXPECT_LT(peakUsage - baseline, fileSize / 2)
        << "峰值提交内存增量 " << (peakUsage - baseline) << "B 须 << 文件 " << fileSize
        << "B（流式——不整文件驻留）";

    // ⑥账本可观测（acceptance 3）：行维 used 停在限额（失败笔不入账——
    //   §9.2 后置"失败＝状态不变"）。
    const BudgetLedgerSnapshot snap = guard->ledger(scope);
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::CsvRowCount) {
            EXPECT_EQ(st.used, 100000u) << "失败笔不入账——used＝限额";
            EXPECT_EQ(st.limit, 100000u);
        }
    }
}

// =====================================================================
// acceptance 3：单文件大小/递归深度/解压总量三要素＋账本可观测
// =====================================================================

/**
 * 三维超限分别触发 IO-SEC-BUDGET-FILE/-DEPTH/-TOTAL，诊断均带比较型三
 * 要素（actual/limit/unit——§4.5.2；diagnostics §8.6"实际字节数/上限/
 * 字节单位"）；单位随维度（bytes/levels）。
 */
TEST_F(BudgetTest, BudgetTriadFileDepthTotalRejectWithComparativeParams)
{
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::SingleFileBytes, 100);
    spec.tighten(BudgetDimension::DirDepth, 8);
    spec.tighten(BudgetDimension::TotalBytes, 1000);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    // ①单文件大小：100 满＋1 → IO-SEC-BUDGET-FILE（unit=bytes）。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::SingleFileBytes, 100)) << "恰满放行";
    IoResult<void> r = guard->charge(scope, BudgetDimension::SingleFileBytes, 1);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetFile) << r.error.detail;
    EXPECT_EQ(paramOf(r, "actual"), "101");
    EXPECT_EQ(paramOf(r, "limit"), "100");
    EXPECT_EQ(paramOf(r, "unit"), "bytes");

    // ②递归深度：深度值一笔入账（层级量纲）→ IO-SEC-BUDGET-DEPTH
    //   （unit=levels——NFR-SEC-02"递归深度"维度）。
    r = guard->charge(scope, BudgetDimension::DirDepth, 9);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetDepth) << r.error.detail;
    EXPECT_EQ(paramOf(r, "actual"), "9");
    EXPECT_EQ(paramOf(r, "limit"), "8");
    EXPECT_EQ(paramOf(r, "unit"), "levels");

    // ③会话累计总量：多笔累加越限 → IO-SEC-BUDGET-TOTAL（unit=bytes）。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::TotalBytes, 900)) << "首笔放行";
    r = guard->charge(scope, BudgetDimension::TotalBytes, 200);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetTotal) << r.error.detail;
    EXPECT_EQ(paramOf(r, "actual"), "1100");
    EXPECT_EQ(paramOf(r, "limit"), "1000");
    EXPECT_EQ(paramOf(r, "unit"), "bytes");
}

/**
 * 双保险记账（§4.4③⑤＋§4.5.2"条目声明大小与实际展开双重计数，以较大
 * 者入账"）：预检笔（声明大小）先行，实际展开小于声明不追加、大于声明
 * 只补差——累计恒等于 max(声明, 实际)；失败笔不入账（状态不变）。
 */
TEST_F(BudgetTest, DeclaredVsActualDoubleBookingKeepsLarger)
{
    // 限额取 1000（给两轮"声明/实际"对照留余量；形态三在文末触发越限）。
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::SingleFileBytes, 1000);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    // 形态一：声明 100、实际 60 → 预检笔 100 入账，补差 0——used=100。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::SingleFileBytes, 100)) << "预检笔（声明）";
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::SingleFileBytes, 0)) << "实际<声明——补差 0";
    BudgetLedgerSnapshot snap = guard->ledger(scope);
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::SingleFileBytes) {
            EXPECT_EQ(st.used, 100u) << "较大者＝声明";
        }
    }

    // 形态二：下一文件声明 60、实际 100 → 预检笔 60（used=160），展开检
    // 补差 40（used=200）——累计＝max(60,100) 按 文件累加（双保险的
    // "展开检"半区：补差笔把实际放大到与声明同表）。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::SingleFileBytes, 60)) << "预检笔 2";
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::SingleFileBytes, 40)) << "补差笔（实际>声明）";
    snap = guard->ledger(scope);
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::SingleFileBytes) {
            EXPECT_EQ(st.used, 200u) << "100（文件一按声明）＋100（文件二按实际）";
        }
    }

    // 形态三：预检笔越限——声明 900 使累计 1100>1000 → 预检即拒（③在④
    // 前——大文件先拒不开）；失败笔不入账（used 维持 200）。
    const IoResult<void> r = guard->charge(scope, BudgetDimension::SingleFileBytes, 900);
    ASSERT_FALSE(r) << "预检笔越限即拒（§4.4③——先拒超限文件再打开）";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetFile);
    snap = guard->ledger(scope);
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::SingleFileBytes) {
            EXPECT_EQ(st.used, 200u) << "失败笔不入账——状态不变（§9.2 后置）";
        }
    }
}

/**
 * 饱和加法（§4.5.2"累计器为 uint64_t＋饱和加法"）：巨量入账不回绕——
 * 潜在累计按饱和投影判超限（actual=饱和值），后续小额入账不被回绕假象
 * 放行。
 */
TEST_F(BudgetTest, SaturatingAccumulatorNeverWraps)
{
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::TotalBytes, 1000);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    // 巨量笔：990+UINT64_MAX 饱和投影 → 拒绝（actual=UINT64_MAX——不回
    // 绕成小值）。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::TotalBytes, 990));
    IoResult<void> r = guard->charge(scope, BudgetDimension::TotalBytes, 0xFFFFFFFFFFFFFFFFull);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetTotal);
    EXPECT_EQ(paramOf(r, "actual"), "18446744073709551615") << "饱和投影值（不回绕）";

    // 状态不变：失败笔后 used 仍 990，小额 10 可入账（恰满）。
    EXPECT_TRUE(guard->charge(scope, BudgetDimension::TotalBytes, 10)) << "恰满放行";
    r = guard->charge(scope, BudgetDimension::TotalBytes, 1);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetTotal) << "此后越限";
}

// =====================================================================
// scope 树与多阶段累计（§4.5.2"BudgetLedger 跨阶段传递/父回收"）
// =====================================================================

/**
 * 子 scope 用量回收至父：子消耗反映到父会话总量（父超限时 close 失败、
 * 父子状态均不变——否则 N 个子 scope 各自开满即绕过会话总量）；正常回
 * 收后父可观测子累计。
 */
TEST_F(BudgetTest, ScopeTreeRollupToParentAndParentLimitEnforced)
{
    BudgetSpec parentSpec = BudgetSpec::productDefault();
    parentSpec.tighten(BudgetDimension::TotalBytes, 1000);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId parent = guard->openScope(parentSpec).value;

    BudgetSpec childSpec = BudgetSpec::productDefault();
    childSpec.tighten(BudgetDimension::TotalBytes, 500);
    const BudgetScopeId child = guard->openScope(childSpec, parent).value;

    EXPECT_TRUE(guard->charge(child, BudgetDimension::TotalBytes, 400)) << "子 scope 入账（子限额内）";

    // 形态一：回收后父未越限——父 used=400（多阶段累计共享额度）。
    EXPECT_TRUE(guard->closeScope(child)) << "正常回收";
    BudgetLedgerSnapshot snap = guard->ledger(parent);
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::TotalBytes) {
            EXPECT_EQ(st.used, 400u) << "子用量已回收至父";
        }
    }
    // 父总量继续记账：400+700 越父限（1100>1000）→ IO-SEC-BUDGET-TOTAL。
    const IoResult<void> r = guard->charge(parent, BudgetDimension::TotalBytes, 700);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetTotal);

    // 形态二：回收本身触发父超限——close 失败、子仍打开（状态均不变，
    // 调用方按预算失败中止会话——§4.4）。编排：父 400＋本笔 350＝750，
    // 子 300——回收投影 1050>1000。
    EXPECT_TRUE(guard->charge(parent, BudgetDimension::TotalBytes, 350)) << "父续记（750）";
    const BudgetScopeId child2 = guard->openScope(childSpec, parent).value;
    EXPECT_TRUE(guard->charge(child2, BudgetDimension::TotalBytes, 300)) << "子限额（500）内";
    const IoResult<void> closeR = guard->closeScope(child2);
    ASSERT_FALSE(closeR) << "回收使父 750+300 越限——close 拒绝";
    EXPECT_EQ(closeR.error.code, IoErrorCode::SecBudgetTotal) << closeR.error.detail;
    EXPECT_TRUE(guard->ledger(child2).open) << "子仍打开（状态不变——§9.2 后置）";
    EXPECT_EQ(guard->ledger(parent).dimensions[static_cast<std::size_t>(BudgetDimension::TotalBytes)].second.used,
              750u) << "父未吸收被拒子账（回收整体拒绝）";
    // 已关闭 scope 的 charge＝非法（IO-FORMAT-INTERNAL——§9.2 错误类型）。
    const BudgetScopeId closed = guard->openScope(childSpec).value;
    EXPECT_TRUE(guard->closeScope(closed));
    EXPECT_EQ(guard->charge(closed, BudgetDimension::TotalBytes, 1).error.code,
              IoErrorCode::FormatInternal);
}

/**
 * 压缩包双侧重账（chargeArchive）：展开总量维（IO-SEC-BUDGET-EXPAND）与
 * 比例维（IO-SEC-BOMB-RATIO）分别触发；压缩总量为 0 且展开＞0＝比例无
 * 穷大拒绝（防除零式逃逸）；比例维不随子 scope 回收（per-archive 事实），
 * 展开总量维随回收（加性量）。
 */
TEST_F(BudgetTest, ArchiveDualSidedBookingExpandAndRatio)
{
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::ArchiveRatio, 2);       // 2:1（便于触发）
    spec.tighten(BudgetDimension::ArchiveExpandedBytes, 100);
    const IBudgetGuardPtr guard = makeBudgetGuard();
    const BudgetScopeId scope = guard->openScope(spec).value;

    // ①展开总量触发：展开 101>100 → IO-SEC-BUDGET-EXPAND，状态不变。
    IoResult<void> r = guard->chargeArchive(scope, 10, 101);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetExpand) << r.error.detail;

    // ②比例触发：压缩 10、展开累计 21 > 2×10 → IO-SEC-BOMB-RATIO
    //   （zip 炸弹判据——§4.5.1）。
    r = guard->chargeArchive(scope, 10, 21);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBombRatio) << r.error.detail;

    // ③防除零逃逸：压缩 0、展开＞0 → 比例无穷大 → IO-SEC-BOMB-RATIO。
    r = guard->chargeArchive(scope, 0, 1);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBombRatio);

    // ④合法双侧：压缩 10、展开 20（恰 2:1，不超）→ 双侧入账；账本可观测
    //   （archiveCompressedBytes＝压缩侧——§4.5.2 双侧记录）。
    EXPECT_TRUE(guard->chargeArchive(scope, 10, 20));
    BudgetLedgerSnapshot snap = guard->ledger(scope);
    EXPECT_EQ(snap.archiveCompressedBytes, 10u) << "压缩侧累计可见";
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::ArchiveExpandedBytes) {
            EXPECT_EQ(st.used, 20u) << "展开侧累计可见";
        }
    }

    // ⑤比例维不随回收（per-archive 事实）、展开维回收（加性量）：子
    // scope chargeArchive 后关闭——父展开 used 增加、父压缩侧不变。
    const BudgetScopeId child = guard->openScope(spec, scope).value;
    EXPECT_TRUE(guard->chargeArchive(child, 5, 10));
    EXPECT_TRUE(guard->closeScope(child));
    snap = guard->ledger(scope);
    EXPECT_EQ(snap.archiveCompressedBytes, 10u) << "压缩侧不回收（比例维语义）";
    for (const auto& [dim, st] : snap.dimensions) {
        if (dim == BudgetDimension::ArchiveExpandedBytes) {
            EXPECT_EQ(st.used, 30u) << "展开侧回收（加性量）";
        }
    }

    // ⑥接口误用防御：比较型维走单笔 charge → IO-FORMAT-INTERNAL（§4.5.2
    //   双侧语义不可单笔表达）。
    EXPECT_EQ(guard->charge(scope, BudgetDimension::ArchiveRatio, 1).error.code,
              IoErrorCode::FormatInternal);
}

// =====================================================================
// P-IO-4 数值钉住＋覆盖语义（acceptance 5）
// =====================================================================

/**
 * 产品默认数值逐维钉住 §4.5.1 表（Draft 档位——架构评审确认前不私改数
 * 值；本用例是"数值不被实现侧漂移"的回归锚）：16 维默认值＋硬上限与卡
 * 面表逐项相等。
 */
TEST_F(BudgetTest, ProductDefaultValuesPinnedToCardSection451)
{
    struct Row {
        BudgetDimension dim;
        std::uint64_t def;
        std::uint64_t hard;
    };
    // §4.5.1 表逐行（默认值列＋硬上限列）。
    const Row rows[] = {
        {BudgetDimension::SingleFileBytes,      256ull * 1024 * 1024,      2ull * 1024 * 1024 * 1024},
        {BudgetDimension::TotalBytes,           2ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024},
        {BudgetDimension::FileCount,            200000,                    1000000},
        {BudgetDimension::DirDepth,             32,                        64},
        {BudgetDimension::ArchiveExpandedBytes, 4ull * 1024 * 1024 * 1024, 16ull * 1024 * 1024 * 1024},
        {BudgetDimension::ArchiveRatio,         100,                       200},
        {BudgetDimension::CsvRowCount,          5000000,                   50000000},
        {BudgetDimension::CsvFieldChars,        64 * 1024,                 1024 * 1024},
        {BudgetDimension::JsonDocBytes,         64ull * 1024 * 1024,       512ull * 1024 * 1024},
        {BudgetDimension::JsonDepth,            64,                        128},
        {BudgetDimension::JsonStringChars,      16ull * 1024 * 1024,       64ull * 1024 * 1024},
        {BudgetDimension::MeshVertexCount,      20000000,                  100000000},
        {BudgetDimension::MeshFaceCount,        40000000,                  200000000},
        {BudgetDimension::IncludeDepth,         16,                        32},
        {BudgetDimension::RefGraphDepth,        64,                        128},
        // TempAreaBytes 硬上限＝§4.5.1"min(展开预算×2, 可用磁盘−1 GiB)"的
        // 编译期项（16 GiB×2＝32 GiB）；磁盘项是运行时量，由临时区会话
        // （IO-T06 TempAreaManager）按实侧 tighten 收紧——spec 常量承载
        // 其上界（Budget.cpp kDimTable 注释同源口径）。
        {BudgetDimension::TempAreaBytes,        8ull * 1024 * 1024 * 1024, 32ull * 1024 * 1024 * 1024},
    };
    const BudgetSpec def = BudgetSpec::productDefault();
    for (const Row& row : rows) {
        SCOPED_TRACE(static_cast<int>(row.dim));
        EXPECT_EQ(def.limit(row.dim), row.def) << "默认值＝§4.5.1 表（P-IO-4 Draft 档位）";
        // 硬上限经 relaxToHardLimit 显式放宽后可观测（放宽只到硬上限）。
        BudgetSpec s = BudgetSpec::productDefault();
        if (!s.isHardLimited(row.dim)) {
            s.relaxToHardLimit(row.dim);
            EXPECT_EQ(s.limit(row.dim), row.hard) << "硬上限＝§4.5.1 表";
            EXPECT_TRUE(s.isRelaxed(row.dim)) << "放宽标记（导入报告放宽项清单）";
        }
    }
}

/**
 * 包导入通道强化（§4.5.2"包导入禁用放宽"＋§9.2"合法调用：包导入 spec
 * isHardLimited 四维全真"）：四维禁放宽（relax 抛出——调用方契约违约
 * fail-fast）；非四维可放宽；tighten 超当前值抛出（只收紧语义）。
 */
TEST_F(BudgetTest, PackImportChannelHardenedSpecRelaxForbidden)
{
    const BudgetSpec pack = BudgetSpec::packImportHardened();
    // 四维 isHardLimited 全真（zip 炸弹主战场——§4.5.2）。
    EXPECT_TRUE(pack.isHardLimited(BudgetDimension::ArchiveExpandedBytes));
    EXPECT_TRUE(pack.isHardLimited(BudgetDimension::ArchiveRatio));
    EXPECT_TRUE(pack.isHardLimited(BudgetDimension::FileCount));
    EXPECT_TRUE(pack.isHardLimited(BudgetDimension::DirDepth));
    // 其余维不禁（普通收紧/放宽语义不变）。
    EXPECT_FALSE(pack.isHardLimited(BudgetDimension::SingleFileBytes));
    EXPECT_FALSE(pack.isHardLimited(BudgetDimension::CsvRowCount));

    // 禁放宽维 relax＝契约违约 fail-fast（头注释同源口径）。
    BudgetSpec s = BudgetSpec::packImportHardened();
    EXPECT_THROW(s.relaxToHardLimit(BudgetDimension::ArchiveExpandedBytes), std::invalid_argument);
    EXPECT_THROW(s.relaxToHardLimit(BudgetDimension::FileCount), std::invalid_argument);
    // 非禁维 relax 合法且到硬上限。
    s.relaxToHardLimit(BudgetDimension::SingleFileBytes);
    EXPECT_EQ(s.limit(BudgetDimension::SingleFileBytes), 2ull * 1024 * 1024 * 1024);

    // tighten 只收紧：超当前值＝违约抛出；收紧任意值合法（§9.2 合法调用行）。
    BudgetSpec t = BudgetSpec::productDefault();
    EXPECT_THROW(t.tighten(BudgetDimension::CsvRowCount, 5000001), std::invalid_argument);
    t.tighten(BudgetDimension::CsvRowCount, 10);        // 任意收紧合法
    EXPECT_EQ(t.limit(BudgetDimension::CsvRowCount), 10u);

    // openScope 防线：试图开"超硬上限"的 scope＝整体拒绝（IO-FORMAT-
    // INTERNAL——硬上限不可逾越 §9.2 非法调用行；不部分生效）。
    const IBudgetGuardPtr guard = makeBudgetGuard();
    BudgetSpec bad = BudgetSpec::productDefault();
    bad.m_limits[static_cast<std::size_t>(BudgetDimension::TotalBytes)] = 9ull * 1024 * 1024 * 1024;
    const IoResult<BudgetScopeId> rejected = guard->openScope(bad);
    ASSERT_FALSE(rejected) << "超硬上限 spec 必须整体拒绝";
    EXPECT_EQ(rejected.error.code, IoErrorCode::FormatInternal) << rejected.error.detail;

    // 非法句柄：父句柄不存在/已关闭 → IO-FORMAT-INTERNAL。
    const BudgetScopeId bogus{9999};
    EXPECT_EQ(guard->openScope(BudgetSpec::productDefault(), bogus).error.code,
              IoErrorCode::FormatInternal);
}
