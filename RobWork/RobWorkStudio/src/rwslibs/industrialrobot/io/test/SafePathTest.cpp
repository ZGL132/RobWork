/**
 * @file   SafePathTest.cpp
 * @brief  SafePath 用例组（IoSec）——路径穿越逐条拒绝（IO-V09）、同项目
 *         多路径等价键与会话互斥（IO-V10）、symlink/junction 逃逸
 *         （IO-V11）、例外不扩大化（P-1/P-7 通道不受资源区管辖）与
 *         io 源码头红线扫描。
 *
 * 设计依据：
 *   - units/io.md §4.3.3（合法/非法示例表——本文件的表驱动断言逐行对应，
 *     V09"逐条拒绝＋正确码；消解后合法者放行"）、§4.2.4（等价类键——
 *     V10"等价键相同；单会话；无重复临时区"）、§4.2.3＋§11.2 IO-V11 行
 *     （symlink/junction→IO-SEC-SYMLINK）、§4.1/§7.9（P-1 例外口径——
 *     契约 acceptance 4"例外不扩大化"）、§3.2/§1.4（头红线——零 Qt/
 *     零跨单元 include）
 *   - 需求 NFR-SEC-01（穿越防护；P-1 一次性读取不在此限）
 *   - 任务契约 tasks/foundation/IO-T02.json acceptance 1/4/5
 *
 * 环境边界（如实声明）：符号链接/junction 在本开发环境实测可创建
 * （Windows 开发者模式/管理员语义——IO-T02 实施段 2026-09-17 探测：
 * mklink 文件/目录链接与 /J junction 全部成功），故 IO-V11 以**真实
 * reparse point** 断言，不以替身降级；若未来环境禁用链接创建，用例
 * 以断言失败如实呈现（不伪造通过——§11.1 通过判定）。
 */

#include <sdurws/ird/io/SafePath.hpp>

#include <gtest/gtest.h>

// Win32（junction 创建经 mklink；GetProcessMemoryInfo 供峰值内存观测——
// BudgetTest 同款）：宏守卫防重定义告警。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using sdurws::ird::io::EquivKeyMutex;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::NormalizedPath;
using sdurws::ird::io::PathRole;
using sdurws::ird::io::SafePathRuleSet;
using sdurws::ird::io::makeSafePathResolver;

namespace {

/// 用例自持临时目录（project 单元 PathCanonicalTest 同款最小形态：
/// 目录名含用例名＋PID——并行运行隔离）。
class IoSecTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_dir = fs::temp_directory_path() / "ird_wp11_t03_safepath"
                / (std::string(info->name()) + "_" + std::to_string(::GetCurrentProcessId()));
        std::error_code ec;
        fs::remove_all(m_dir, ec);
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "临时目录创建失败: " << m_dir.string();
        m_resolver = makeSafePathResolver();
    }

    void TearDown() override
    {
        std::error_code ec;
        // 先摘除已知 reparse point（remove 对链接只删链接本体），再删树
        // ——避免 remove_all 对 junction 的递归行为差异影响清理面。
        fs::remove(m_dir / L"proj" / L"objects" / L"lnk_file", ec);
        fs::remove(m_dir / L"proj" / L"objects" / L"lnk_dir", ec);
        fs::remove(m_dir / L"proj" / L"objects" / L"junc", ec);
        fs::remove_all(m_dir, ec);
        EXPECT_FALSE(ec) << "临时目录清理失败";
    }

    /// 项目根目录（P-5 解析基点；含 objects/ 与 catalog/ 资源区与区外
    /// outside/——V11 的逃逸目标）。
    fs::path projectRoot() const { return m_dir / L"proj"; }

    fs::path m_dir;                                       ///< 用例临时根
    std::shared_ptr<sdurws::ird::io::ISafePathResolver> m_resolver; ///< 被测解析器（产品默认规则集）
};

/// 断言失败结果的码与出处（params 携带 role/path——脱敏 display）。
void expectError(const IoResult<NormalizedPath>& r, IoErrorCode code, const char* context)
{
    ASSERT_FALSE(r) << context << "：预期拒绝却成功（display=" << r.value.display << "）";
    EXPECT_EQ(r.error.code, code) << context << "：码不符，实际 detail=" << r.error.detail;
}

/// 断言成功结果的 relKey（P-4/P-5 折叠相对键）。
void expectRelKey(const IoResult<NormalizedPath>& r, const std::string& relKey, const char* context)
{
    ASSERT_TRUE(r) << context << "：预期合法却拒绝（code=" << static_cast<int>(r.error.code)
                   << " detail=" << r.error.detail << "）";
    EXPECT_EQ(r.value.relKey, relKey) << context << "：relKey 不符";
}

} // namespace

// =====================================================================
// IO-V09：IoSec/PathTraversal——§4.3.3 非法表逐条拒绝＋正确码
// =====================================================================

/**
 * P-4（包内条目）非法表：§4.3.3 P-4 表全部 ❌ 行＋非法字符/保留名/尾随
 * 点空白/长度边界（SP-2/SP-8/SP-9——最严校验角色）。每行断言"拒绝＋正
 * 确码"（V09 观测点"表驱动断言全集"）。
 */
TEST_F(IoSecTest, PathTraversalP4IllegalTableRejectedWithExactCodes)
{
    struct Row {
        const wchar_t* input;
        IoErrorCode code;
        const char* why;
    };
    const Row rows[] = {
        // §4.3.3 P-4 表 ❌ 行（原文形态，'/' 与 '\' 混排属条目名现实形态）。
        {L"payload/../evil", IoErrorCode::SecPathEscape, "表行 payload/../evil"},
        {L"/etc/passwd", IoErrorCode::SecPathEscape, "表行 /etc/passwd（根成分）"},
        {L"C:\\evil", IoErrorCode::SecPathEscape, "表行 C:\\evil（盘符）"},
        {L"\\\\?\\C:\\evil", IoErrorCode::SecPathEscape, "表行 \\\\?\\C:\\evil（设备前缀＋盘符）"},
        {L"payload/objects/a/b:", IoErrorCode::FormatPackEntry, "表行 段含 ':'（非法字符族）"},
        // 非法字符族其余成员（§4.3.3 P-4 表 ':' 行括注：NUL、*、?、"、<、>、|、控制字符）。
        {L"payload/x*y", IoErrorCode::FormatPackEntry, "条目段含 '*'"},
        {L"payload/x?y", IoErrorCode::FormatPackEntry, "条目段含 '?'"},
        {L"payload/x\"y", IoErrorCode::FormatPackEntry, "条目段含 '\"'"},
        {L"payload/x<y", IoErrorCode::FormatPackEntry, "条目段含 '<'"},
        {L"payload/x>y", IoErrorCode::FormatPackEntry, "条目段含 '>'"},
        {L"payload/x|y", IoErrorCode::FormatPackEntry, "条目段含 '|'"},
        // 保留名/尾随点空白（SP-8——§4.2.1 步骤 6/7 仅 P-4/P-5 拒绝）。
        {L"payload/con", IoErrorCode::SecPathReserved, "保留名 con"},
        {L"payload/NUL.txt", IoErrorCode::SecPathReserved, "保留名含扩展形式 NUL.txt"},
        {L"payload/com1", IoErrorCode::SecPathReserved, "保留名 com1"},
        {L"payload/obj.", IoErrorCode::SecPathReserved, "段尾随点（NTFS 剥离→写入名≠引用名）"},
        {L"payload/obj ", IoErrorCode::SecPathReserved, "段尾随空白"},
        // 空形态（§4.2.2 表：P-4 拒绝）。
        {L"", IoErrorCode::SecPathEscape, "空条目"},
        {L".", IoErrorCode::SecPathEscape, "点条目（消解为空＝无条目名）"},
    };
    for (const Row& row : rows) {
        // 每行独立断言（首失败即中止并指明行出处——表驱动全集可观测）。
        SCOPED_TRACE(row.why);
        expectError(m_resolver->normalize(PathRole::PackEntry, row.input), row.code, row.why);
    }
    // 单段 256>255（SP-9）：程序化构造（手数点数的字面量不可靠）。
    std::wstring longSeg = L"payload/";
    longSeg.append(256, L'a');
    expectError(m_resolver->normalize(PathRole::PackEntry, longSeg), IoErrorCode::SecPathTooLong,
                "段长 256>255（SP-9）");
    // 总长 4097（>SP-9 上限 4096）：构造合法段字符组成的超长相对条目。
    std::wstring longPath = L"payload/";
    longPath.append(4090, L'a');
    expectError(m_resolver->normalize(PathRole::PackEntry, longPath), IoErrorCode::SecPathTooLong,
                "总长 4097>4096（SP-9）");
}

/**
 * P-4 合法行＋批量查重：§4.3.3 P-4 表 ✅ 行、折叠键重复（仅大小写异的
 * 两条件目）→ IO-PACK-DUPLICATE-ENTRY（Windows 落盘冲突预防）；任一非
 * 法整批拒绝且错误清单全量（§9.1）。
 */
TEST_F(IoSecTest, PathTraversalP4LegalRowsAndBatchDuplicateDetection)
{
    // ✅ 行：规范相对条目放行，relKey＝正斜杠＋小写折叠（§4.3.2 字段注）。
    expectRelKey(m_resolver->normalize(PathRole::PackEntry, L"payload/objects/obj-a/cv-64hex"),
                 "payload/objects/obj-a/cv-64hex", "表行 payload/objects/obj-a/cv…");

    // 批量查重：§4.3.3 P-4 表末行（两条同名条目仅大小写异）→ 整批拒绝。
    const std::vector<std::string> dup = {"payload/objects/A", "payload/objects/b", "payload/objects/a"};
    const IoResult<void> dupR = m_resolver->normalizePackEntries(dup.size(),
                                                                 [&](std::size_t i) { return dup[i]; });
    ASSERT_FALSE(dupR) << "折叠键重复（A vs a）须整批拒绝";
    EXPECT_EQ(dupR.error.code, IoErrorCode::PackDuplicateEntry) << dupR.error.detail;
    // 错误清单全量（§9.1"任一非法即整批拒绝（errors 全量列出）"）：首个
    // 出现的折叠键不是违规（它本就唯一），违规项＝重复出现处的条目
    // （index=2）——明细同时携带其 relKey 供落盘冲突定位。
    EXPECT_NE(dupR.error.detail.find("index=2"), std::string::npos) << dupR.error.detail;
    EXPECT_NE(dupR.error.detail.find("relKey=payload/objects/a"), std::string::npos) << dupR.error.detail;

    // 混合违规：非法条目（index=0 码优先——最小序号定码）＋重复对同时
    // 在案（detail 全量）。
    const std::vector<std::string> mixed = {"payload/../evil", "payload/objects/A", "payload/objects/a"};
    const IoResult<void> mixedR = m_resolver->normalizePackEntries(mixed.size(),
                                                                   [&](std::size_t i) { return mixed[i]; });
    ASSERT_FALSE(mixedR) << "混合违规须整批拒绝";
    EXPECT_EQ(mixedR.error.code, IoErrorCode::SecPathEscape) << "最小序号（index=0 穿越条目）定码";
    EXPECT_NE(mixedR.error.detail.find("duplicate-entry"), std::string::npos) << "重复对仍在全量清单";

    // 空包（0 条目）＝合法（无条目即无违规——§9.1 批量方法注释）。
    const IoResult<void> emptyR = m_resolver->normalizePackEntries(0, [](std::size_t) { return std::string(); });
    EXPECT_TRUE(emptyR) << "空包合法";

    // 全合法批量＝成功。
    const std::vector<std::string> okBatch = {"payload/a.txt", "payload/objects/b.bin", "payload/manifest.json"};
    const IoResult<void> okR = m_resolver->normalizePackEntries(okBatch.size(),
                                                                [&](std::size_t i) { return okBatch[i]; });
    EXPECT_TRUE(okR) << "全合法批量放行";
}

/**
 * P-5（项目内持久化资源引用）非法表：§4.3.3 P-5 表全部 ❌ 行＋SP-3
 * 区外首段（NFR-SEC-01 主断言面：资源引用不得逃逸 objects|catalog）。
 */
TEST_F(IoSecTest, PathTraversalP5IllegalTableRejectedWithExactCodes)
{
    const fs::path root = projectRoot();
    struct Row {
        const wchar_t* input;
        IoErrorCode code;
        const char* why;
    };
    const Row rows[] = {
        {L"objects/../../evil.dll", IoErrorCode::SecPathEscape, "表行 objects/../../evil.dll（净逃逸）"},
        {L"objects/..\\..\\evil.dll", IoErrorCode::SecPathEscape, "表行 分隔符混用不影响段解析"},
        {L"objects/nul", IoErrorCode::SecPathReserved, "表行 objects/nul（保留名）"},
        {L"objects/a./x", IoErrorCode::SecPathReserved, "表行 objects/a./x（段尾随点）"},
        {L"C:\\proj\\objects\\x", IoErrorCode::SecPathEscape, "表行 绝对形式持久化引用非法（P-5 要求纯相对）"},
        {L"\\\\server\\share\\objects\\x", IoErrorCode::SecPathEscape, "表行 UNC 不得出现在项目内引用"},
        {L"objects/../../objects/secret", IoErrorCode::SecPathEscape, "越过基点后未回落（净逃逸）"},
        {L"../evil", IoErrorCode::SecPathEscape, "起点即净逃逸"},
        {L"other/x", IoErrorCode::SecPathEscape, "首段不在资源区（SP-3——非 objects/catalog）"},
        {L"objects/../evil", IoErrorCode::SecPathEscape, "消解后越过基点"},
    };
    for (const Row& row : rows) {
        SCOPED_TRACE(row.why);
        expectError(m_resolver->normalize(PathRole::ProjectResourceRef, row.input, root), row.code, row.why);
    }
    // 调用方契约：P-5 未传 base＝防御性拒绝 IO-FORMAT-INTERNAL（§9.1
    // "非法调用"行）；P-1 误传 base 同拒（角色/基点不匹配）。
    expectError(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/x"),
                IoErrorCode::FormatInternal, "P-5 缺 base");
    expectError(m_resolver->normalize(PathRole::UserSource, L"anything", root),
                IoErrorCode::FormatInternal, "P-1 多传 base");
}

/**
 * P-5 合法行：§4.3.3 P-5 表 ✅ 行——规范相对引用、**消解后回落仍在区内
 * 的 `..` 拼写**、大小写折叠命中（NTFS 大小写不敏感）；catalog 区同权。
 * （V09 观测点"消解后合法者放行"的半区。）
 */
TEST_F(IoSecTest, PathTraversalP5LegalRowsAccepted)
{
    const fs::path root = projectRoot();
    // ✅ 规范相对引用（对象内容版本路径形态——project.md §4.1 布局）。
    expectRelKey(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/obj-abcdef/cv-00ff", root),
                 "objects/obj-abcdef/cv-00ff", "表行 objects/obj-abc…/cv-64hex");
    // ✅ 消解后合法（卡面原文："消解后仍在区内；消解前不上报"）。
    expectRelKey(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/obj-a/../../objects/obj-b/x", root),
                 "objects/obj-b/x", "表行 objects/obj-a/../../objects/obj-b/x");
    // ✅ 大小写折叠命中（表行 OBJECTS/OBJ-A/X——NTFS 大小写不敏感）。
    expectRelKey(m_resolver->normalize(PathRole::ProjectResourceRef, L"OBJECTS/OBJ-A/X", root),
                 "objects/obj-a/x", "表行 OBJECTS/OBJ-A/X");
    // catalog 区同权（SP-3"objects/ 或 catalog/"）。
    expectRelKey(m_resolver->normalize(PathRole::ProjectResourceRef, L"catalog/motor/mt-1/v1", root),
                 "catalog/motor/mt-1/v1", "catalog 区引用");
}

// =====================================================================
// IO-V10：IoSec/SameProjectAltPaths——等价键一致＋TempArea 互斥（§4.2.4）
// =====================================================================

/**
 * 同一项目经 D:\P / d:\p\ / D:/P 三种拼写打开：等价类键相同（大小写折
 * 叠＋分隔符归一＋词法规范化＋weakly_canonical 实路径——§4.2.4）；等价
 * 键会话互斥（EquivKeyMutex——§9.10"TempArea 会话互斥经等价键"）：第一
 * 拼写持有租约期间，其余拼写的会话创建被拒（"单会话；无重复临时区"——
 * V10 观测点"等价键相等断言；临时区计数"以 heldCount 承载）。
 */
TEST_F(IoSecTest, SameProjectAltPathsEquivalentKeysAndSessionMutex)
{
    // 实体目录（真实存在——canonical 取实路径大小写）。
    const fs::path real = m_dir / L"ProjRoot";
    std::error_code ec;
    fs::create_directories(real / L"objects", ec);
    ASSERT_FALSE(ec);

    // 三种拼写（§11.2 IO-V10 行：D:\P、d:\p\、D:/P）。
    const std::wstring s1 = real.wstring();                       // 原生大小写
    std::wstring s2 = s1;                                         // 全折叠＋尾随分隔符（"d:\p\"）
    for (wchar_t& c : s2) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    s2.push_back(L'\\');
    std::wstring s3 = s1;                                         // 正斜杠（"D:/P"）
    std::replace(s3.begin(), s3.end(), L'\\', L'/');

    const IoResult<NormalizedPath> n1 = m_resolver->normalize(PathRole::ProjectRoot, s1);
    const IoResult<NormalizedPath> n2 = m_resolver->normalize(PathRole::ProjectRoot, s2);
    const IoResult<NormalizedPath> n3 = m_resolver->normalize(PathRole::ProjectRoot, s3);
    ASSERT_TRUE(n1) << n1.error.detail;
    ASSERT_TRUE(n2) << n2.error.detail;
    ASSERT_TRUE(n3) << n3.error.detail;
    // 等价键相等（V10 主断言）。
    EXPECT_EQ(n1.value.equivKey, n2.value.equivKey) << "全折叠＋尾随分隔符拼写须同键";
    EXPECT_EQ(n1.value.equivKey, n3.value.equivKey) << "正斜杠拼写须同键";
    EXPECT_FALSE(n1.value.equivKey.empty()) << "等价键非空";

    // TempArea 会话互斥（§9.10"经等价键"）：alt 拼写派生键同 key → 第二
    // 会话 acquire 失败（无重复临时区的进程内判定面）。
    EquivKeyMutex mutexTable;
    EXPECT_TRUE(mutexTable.tryAcquire(n1.value.equivKey)) << "首会话获取租约";
    EXPECT_FALSE(mutexTable.tryAcquire(n2.value.equivKey)) << "同项目 alt 拼写的第二会话被互斥";
    EXPECT_EQ(mutexTable.heldCount(), 1u) << "临时区计数＝1（单会话——V10 观测点）";
    EXPECT_TRUE(mutexTable.isHeld(n3.value.equivKey)) << "第三拼写命中同一租约";

    // 释放后可重新开会话（导入完成→cleanup→下次导入新会话）。
    mutexTable.release(n1.value.equivKey);
    EXPECT_EQ(mutexTable.heldCount(), 0u) << "释放后计数归零";
    EXPECT_TRUE(mutexTable.tryAcquire(n3.value.equivKey)) << "同键可再次获取（会话串行复用）";

    // 不同项目＝不同键，互不互斥（反例钉住：互斥粒度是等价类不是全局）。
    const fs::path other = m_dir / L"OtherRoot";
    fs::create_directories(other, ec);
    const IoResult<NormalizedPath> n4 = m_resolver->normalize(PathRole::ProjectRoot, other.wstring());
    ASSERT_TRUE(n4) << n4.error.detail;
    EXPECT_NE(n4.value.equivKey, n1.value.equivKey) << "不同项目不同键";
    EXPECT_TRUE(mutexTable.tryAcquire(n4.value.equivKey)) << "异项目会话不受同项目租约互斥";
    EXPECT_EQ(mutexTable.heldCount(), 2u) << "两项目并行＝两个临时区（各一）";
}

// =====================================================================
// IO-V11：IoSec/SymlinkJunction——文件/目录 symlink 与 junction
// =====================================================================

/**
 * 资源区内名字指向区外实体：文件 symlink、目录 symlink、junction 三形
 * 态一律 IO-SEC-SYMLINK（§4.2.3 P-5"不区分目标是否仍在区内——一律拒
 * 绝"；V11 观测点"码＋final-path 记录"——错误 params.path 携带 offender
 * 层级的脱敏 display）。反例：同形态用户源（P-1）放行——NFR-SEC-01 例
 * 外不扩大化（见下一用例）。
 */
TEST_F(IoSecTest, SymlinkJunctionEscapeRejectedWithIoSecSymlink)
{
    const fs::path root = projectRoot();
    std::error_code ec;
    // 布景：资源区实体＋区外目标。
    fs::create_directories(root / L"objects" / L"real-dir", ec);
    fs::create_directories(root / L"outside", ec);
    ASSERT_FALSE(ec);
    {
        std::ofstream f(root / L"outside" / L"evil.txt", std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << "outside";
    }

    // ① 文件符号链接：objects/lnk_file → 区外 evil.txt。
    ec.clear();
    fs::create_symlink(root / L"outside" / L"evil.txt", root / L"objects" / L"lnk_file", ec);
    ASSERT_FALSE(ec) << "文件 symlink 创建失败（环境禁用链接创建——V11 需真实 reparse point）: "
                     << ec.message();

    // ② 目录符号链接：objects/lnk_dir → 区外目录。
    ec.clear();
    fs::create_directory_symlink(root / L"outside", root / L"objects" / L"lnk_dir", ec);
    ASSERT_FALSE(ec) << "目录 symlink 创建失败: " << ec.message();

    // ③ junction（挂载点）：objects/junc → 区外目录（mklink /J——无特
    //    权要求；std::filesystem 无 junction API）。
    const std::wstring juncCmd = std::wstring(L"cmd /c mklink /J \"")
                                 + (root / L"objects" / L"junc").wstring() + L"\" \""
                                 + (root / L"outside").wstring() + L"\"";
    ASSERT_EQ(0, _wsystem(juncCmd.c_str())) << "junction 创建失败";
    ASSERT_TRUE(fs::exists(root / L"objects" / L"junc")) << "junction 落盘确认";

    // 三形态 P-5 引用解析一律 IO-SEC-SYMLINK（区内名字→区外实体同样拒）。
    expectError(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/lnk_file", root),
                IoErrorCode::SecPathSymlink, "文件 symlink 指向区外");
    expectError(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/lnk_dir/x", root),
                IoErrorCode::SecPathSymlink, "目录 symlink 指向区外（经链段）");
    expectError(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/junc/evil.txt", root),
                IoErrorCode::SecPathSymlink, "junction 指向区外");
    // 错误 params 携带 offender 层级（final-path 记录——V11 观测点；脱敏
    // display 形态、可定位到具体链接段）。
    const IoResult<NormalizedPath> r = m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/lnk_file", root);
    ASSERT_FALSE(r);
    bool hasPathParam = false;
    for (const auto& kv : r.error.params) {
        if (kv.first == "path" && !kv.second.empty()) {
            hasPathParam = true;
        }
    }
    EXPECT_TRUE(hasPathParam) << "IO-SEC-SYMLINK 诊断须携带 offender 路径参数";

    // 区内真实实体不受牵连（链上无 reparse point→放行——防护不误伤）。
    expectRelKey(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/real-dir/inner", root),
                 "objects/real-dir/inner", "区内真实实体放行");
}

/**
 * 例外不扩大化（契约 acceptance 4；§4.1 P-1/P-7 行）：用户显式选择的源
 * （P-1）与导出目标（P-7）不受资源区/链接管辖——symlink 目标照常解析
 * （读取快照按最终实体路径＝equivKey 承载）；反例钉住：同一路径换 P-5
 * 角色即拒（角色决定规则集，io 不从形态推断——§4.1）。
 */
TEST_F(IoSecTest, ExceptionNotWidenedUserSourceAndExportTargetUnrestricted)
{
    const fs::path root = projectRoot();
    std::error_code ec;
    fs::create_directories(root / L"outside", ec);
    fs::create_directories(root / L"objects", ec);      // 链接父目录必须存在
    ASSERT_FALSE(ec);
    const fs::path target = root / L"outside" / L"evil.txt";
    {
        std::ofstream f(target, std::ios::binary);
        ASSERT_TRUE(f.is_open());
        f << "outside";
    }
    // 区外符号链接照常存在（复用上例布景方式）。
    ec.clear();
    fs::create_symlink(target, root / L"objects" / L"lnk_file", ec);
    ASSERT_FALSE(ec);

    // P-1：指向区外文件（经 symlink 入口）→ 放行（存在性准入通过；无逃
    // 逸检查——§4.1 P-1"不施加资源区逃逸检查"）。
    const IoResult<NormalizedPath> src = m_resolver->normalize(PathRole::UserSource,
                                                               (root / L"objects" / L"lnk_file").wstring());
    ASSERT_TRUE(src) << src.error.detail;
    EXPECT_TRUE(src.value.fileSize.has_value()) << "P-1 实体大小提示性字段就位";
    EXPECT_EQ(src.value.fileSize.value(), 7u) << "大小＝实体字节数（outside）";
    // equivKey＝最终实体路径（weakly_canonical 解析 symlink——§4.2.3 P-1
    // "读取快照按最终实体路径记录"）：链接入口与实体直连同键（变化检测
    // 基准不因入口拼写漂移）。
    const IoResult<NormalizedPath> direct = m_resolver->normalize(PathRole::UserSource, target.wstring());
    ASSERT_TRUE(direct) << direct.error.detail;
    EXPECT_EQ(src.value.equivKey, direct.value.equivKey) << "symlink 入口与实体直连同键";

    // P-1 反例：路径不存在 → IO-RES-NOT-FOUND（§4.2.5 分类一——环境错
    // 误码，不是安全码：一次性读取不虚报安全违规）。
    expectError(m_resolver->normalize(PathRole::UserSource, (root / L"no-such.bin").wstring()),
                IoErrorCode::ResNotFound, "P-1 不存在");

    // P-7：导出目标允许尚不存在（可写性预检归 IAtomicFileWriter——§4.6），
    // 保留名/穿越拼写不适用（P-7 放行——§4.2.1 步骤 6）。
    const IoResult<NormalizedPath> exp = m_resolver->normalize(PathRole::ExportTarget,
                                                               (root / L"outside" / L"new.pack").wstring());
    EXPECT_TRUE(exp) << exp.error.detail;

    // 反例钉住：同一实体路径换 P-5 角色（区内链接名）即拒——规则集随角
    // 色切换，"例外"不被路径形态扩大（角色由调用方声明——§4.1 原文）。
    expectError(m_resolver->normalize(PathRole::ProjectResourceRef, L"objects/lnk_file", root),
                IoErrorCode::SecPathSymlink, "同路径 P-5 角色仍受管辖");
}

// =====================================================================
// 头红线扫描（DTB §5.3 R-3/R-1 的运行期第二道防线——diagnostics
// BuildRedLineTest 同款机制；与 CMake 配置期守卫互为两面）
// =====================================================================

/**
 * io 公共头与实现零 Qt include/符号、零跨单元（io/core/diagnostics 之
 * 外的 ird 单元）include——R-3（L2 零 Qt，比 L3 更严）＋R-1/R-2（业务单
 * 元互链/私有头禁止）。扫描面＝IRD_IO_UNIT_ROOT 下的 include/ 与 src/
 * 全部 .hpp/.cpp。
 */
TEST(IoSecHeaderRedLines, IoHeadersFreeOfQtAndCrossUnitIncludes)
{
    // 注入值＝industrialrobot 根（io/..——diagnostics UNIT_ROOT 同款口径
    // ；CMake 正斜杠规范化，无转义问题），io 单元树＝其下 io/ 子目录。
    const fs::path root = fs::path(IRD_IO_UNIT_ROOT) / "io";
    ASSERT_TRUE(fs::exists(root / "include")) << "单元根注入缺失（IRD_IO_UNIT_ROOT）";

    // 禁止形态：Qt include/类型符号（注释里的"零 Qt"字样不匹配——均为
    // 带尖括号/类名前缀的代码形态）；跨单元 ird include（io 只允许
    // ird/io、ird/core、ird/diagnostics——§3.2 两条登记边）。
    static const char* kForbidden[] = {
        "#include <Qt", "#include \"Qt", "QString", "QObject", "QFile", "QDir", "QVector",
        "ird/project",  "ird/runtime",  "ird/modeling", "ird/requirements",
        "ird/selection", "ird/kinematics", "ird/trajectory", "ird/dynamics",
        "ird/optimization", "ird/policy", "ird/evidence", "ird/execution",
        "ird/ui", "ird/workflow", "ird/reporting", "ird/testkit",
    };
    std::size_t scanned = 0;
    for (const fs::path dir : {root / "include", root / "src"}) {
        ASSERT_TRUE(fs::exists(dir)) << dir.string();
        for (const fs::directory_entry& e : fs::recursive_directory_iterator(dir)) {
            if (!e.is_regular_file() || (e.path().extension() != ".hpp" && e.path().extension() != ".cpp")) {
                continue;
            }
            ++scanned;
            std::ifstream f(e.path(), std::ios::binary);
            ASSERT_TRUE(f.is_open()) << e.path().string();
            std::string line;
            int lineNo = 0;
            while (std::getline(f, line)) {
                ++lineNo;
                // 跳过纯注释行（首非空白为 '*' 或 '//'）——禁止形态指代码
                // 出现（注释中的"零 Qt""不经 QString"等负向表述不是违例；
                // 注释内夹带真实代码的形态由人工 review 把关，扫描器只做
                // 机检第一道）。
                const std::size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos
                    && (line[first] == '*' || line.compare(first, 2, "//") == 0)) {
                    continue;
                }
                for (const char* bad : kForbidden) {
                    EXPECT_EQ(line.find(bad), std::string::npos)
                        << e.path().filename().string() << ":" << lineNo << " 命中红线 \"" << bad << "\"";
                }
            }
        }
    }
    EXPECT_GE(scanned, 7u) << "扫描面过小——include 3 头＋src 4 实现文件应至少 7 个";
}
