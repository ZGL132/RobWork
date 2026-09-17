/**
 * @file   ResourceIoTest.cpp
 * @brief  资源导入服务用例组（IoRes）——V15 CyclicIncludes（A→B→A 环报
 *         IO-FORMAT-XML-CYCLE 且含环路径清单；17 层深度链触发
 *         IO-SEC-BUDGET-INCLUDE）、V16 MeshBudget（二进制 STL 谎报三角
 *         形数读前即拒；真实超限 STL 读中触发 IO-SEC-BUDGET-MESH）、
 *         V25 AccessDenied（ACL 拒读不降级为 NOT-FOUND——§4.2.5 四分类）、
 *         V26 MissingResource（依赖树缺叶——缺失清单；资源事实类别，
 *         §2.5 无业务结论）、V27 DigestStability（100 次快照 digest 恒定；
 *         改名不改 digest——路径不作身份）；另含 XML 良构检查、include
 *         越界拒绝、open/read 句柄与格式识别的基础用例。
 *
 * 设计依据：
 *   - units/io.md §11.2 IO-V15/V16/V25/V26/V27 行（本文件用例一一对应）、
 *     §6.1~§6.6（被测语义）、§4.2.5（错误四分类）、§4.5.1（网格/深度预算）、
 *     §9.6（接口契约）、§11.1（组名 IoRes；替身＝真实临时文件）
 *   - 需求 NFR-SEC-02（预算）、NFR-REL-04（缺失/变化可检测——V26/V27）、
 *     MDL-19（依赖树文件层）、NFR-SEC-01（P-1 例外）
 *   - 任务契约 tasks/foundation/IO-T05.json acceptance 1/3（V15/V16 与
 *     V24~V27 的 reader 半边；V24/probe 半边见 ResourceSolidifyTest.cpp）
 *
 * 断言纪律（AGENTS.md §2.7）：每个用例中文注明验证的需求/验收条目；
 * 测试以真实临时文件为替身载体（§11.1"可控文件替身"），失败如实失败。
 */

#include <sdurws/ird/io/ResourceIo.hpp>

#include <sdurws/ird/core/Digest.hpp>
#include <sdurws/ird/io/Budget.hpp>
#include <sdurws/ird/io/IoDiagnostics.hpp>
#include <sdurws/ird/io/IoError.hpp>
#include <sdurws/ird/io/SafePath.hpp>

#include <gtest/gtest.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::Digest256;
using sdurws::ird::io::BudgetDimension;
using sdurws::ird::io::BudgetScopeId;
using sdurws::ird::io::BudgetSpec;
using sdurws::ird::io::IoError;
using sdurws::ird::io::IoErrorCode;
using sdurws::ird::io::IoResult;
using sdurws::ird::io::IResourceReaderPtr;
using sdurws::ird::io::PathRole;
using sdurws::ird::io::ResourceDependencyTree;
using sdurws::ird::io::ResourceKind;
using sdurws::ird::io::ResourceOpenSpec;
using sdurws::ird::io::ResourceSnapshot;
using sdurws::ird::io::ResourceStreamHandle;
using sdurws::ird::io::errorCodeToken;
using sdurws::ird::io::makeBudgetGuard;
using sdurws::ird::io::makeResourceReader;

namespace {

// =====================================================================
// 测试助手（真实临时文件替身——§11.1 TempDir 每用例隔离）
// =====================================================================

/// 从 IoError params 取键值（三要素/定位参数断言用——既有用例同款）。
std::string paramOf(const IoError& e, const char* key)
{
    for (const auto& kv : e.params) {
        if (kv.first == key) {
            return kv.second;
        }
    }
    return {};
}

/// 手工 SHA-256（core ContentDigester 直接算——与被测实现互为独立复核）。
Digest256 manualDigest(const std::vector<std::uint8_t>& bytes)
{
    ContentDigester d;
    d.update(bytes.data(), bytes.size());
    return d.finalize();
}

std::string hexOf(const Digest256& d)
{
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : d) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

class IoResTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // 每用例独立临时根（随机 8 字节 hex——并行/重跑不冲突）。
        std::random_device rd;
        std::uint64_t tag = (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
        char name[64];
        std::snprintf(name, sizeof(name), "ird-io-res-%016llx",
                      static_cast<unsigned long long>(tag));
        dir = fs::temp_directory_path() / name;
        fs::create_directories(dir);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(dir, ec);                // 清理失败不掩盖用例结论
    }

    /// 写字节文件（真实替身载体）。
    fs::path write(const std::string& name, const std::vector<std::uint8_t>& bytes)
    {
        const fs::path p = dir / name;
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
        return p;
    }

    fs::path writeText(const std::string& name, const std::string& text)
    {
        return write(name, std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    /// 二进制 STL 构造（84 字节头三角数声明＋可选真实三角面体）。
    std::vector<std::uint8_t> binaryStl(std::uint32_t declaredFaces, std::uint32_t realFaces)
    {
        std::vector<std::uint8_t> b(84 + 50u * realFaces, 0);
        std::memcpy(b.data() + 80, &declaredFaces, 4);      // 小端声明数
        return b;
    }

    /// ASCII STL 构造（facet 计数＝facets——流式统计的判定载体）。
    std::string asciiStl(int facets)
    {
        std::string s = "solid model\n";
        for (int i = 0; i < facets; ++i) {
            s += "facet normal 0 0 1\n"
                 "  outer loop\n"
                 "    vertex 0 0 0\n"
                 "    vertex 1 0 0\n"
                 "    vertex 0 1 0\n"
                 "  endloop\n"
                 "endfacet\n";
        }
        s += "endsolid model\n";
        return s;
    }

    /// Xacro 文档构造（可选 include 列表——依赖树载体）。
    std::string xacroDoc(const std::vector<std::string>& includes)
    {
        std::string s = "<?xml version=\"1.0\"?>\n"
                        "<robot xmlns:xacro=\"http://www.ros.org/wiki/xacro\">\n";
        for (const std::string& inc : includes) {
            s += "  <xacro:include filename=\"" + inc + "\"/>\n";
        }
        s += "</robot>\n";
        return s;
    }

    fs::path dir;   ///< 本用例临时根
};

// =====================================================================
// V15 CyclicIncludes（acceptance 1——NFR-SEC-02/MDL-19）
// =====================================================================

/// IO-V15：include 图 A→B→A 有环 → IO-FORMAT-XML-CYCLE，环路径清单含
/// 环上全部相对键（§6.2"列出环路径"；观测点"环清单内容"）。
TEST_F(IoResTest, CyclicIncludeReportsCycleWithPathList)
{
    writeText("a.xacro", xacroDoc({"b.xacro"}));
    writeText("b.xacro", xacroDoc({"a.xacro"}));

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "a.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r) << "环必须拒绝（§6.2）";
    EXPECT_EQ(r.error.code, IoErrorCode::FormatXmlCycle);
    const std::string cycle = paramOf(r.error, "cycle");
    // 环清单＝环路径序列（相对键）＋回边闭合。
    EXPECT_NE(cycle.find("a.xacro"), std::string::npos) << "环清单应含 a.xacro: " << cycle;
    EXPECT_NE(cycle.find("b.xacro"), std::string::npos) << "环清单应含 b.xacro: " << cycle;
    EXPECT_NE(cycle.find("->"), std::string::npos) << "环清单为路径序列: " << cycle;
}

/// IO-V15：自包含（A→A 自环）同样拒绝——环检测的最小情形（§6.2"自包含"）。
TEST_F(IoResTest, SelfIncludeReportsCycle)
{
    writeText("self.xacro", xacroDoc({"self.xacro"}));

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "self.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatXmlCycle);
    EXPECT_NE(paramOf(r.error, "cycle").find("self.xacro"), std::string::npos);
}

/// IO-V15：17 层 include 深度链 → IO-SEC-BUDGET-INCLUDE（默认 16——比较型
/// 三要素 actual=17/limit=16/unit=levels；§4.5.1 IncludeDepth 行；"循环与
/// 深度双保险"的深度半边）。
TEST_F(IoResTest, IncludeChainBeyondDepthTriggersIncludeBudget)
{
    // n01→n02→…→n17：17 个文件，链深 17 > 默认 16。
    for (int i = 1; i <= 17; ++i) {
        char cur[16];
        std::snprintf(cur, sizeof(cur), "n%02d.xacro", i);
        std::vector<std::string> inc;
        if (i < 17) {
            char next[16];
            std::snprintf(next, sizeof(next), "n%02d.xacro", i + 1);
            inc.push_back(next);
        }
        writeText(cur, xacroDoc(inc));
    }

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "n01.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r) << "深度 17 超默认 16 必须拒绝（§4.5.1）";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetInclude);
    EXPECT_EQ(paramOf(r.error, "actual"), "17");        // 比较型三要素（§4.5.2）
    EXPECT_EQ(paramOf(r.error, "limit"), "16");
    EXPECT_EQ(paramOf(r.error, "unit"), "levels");
}

/// IO-V15 对照：菱形引用（B/C 共享 D）是 DAG 共享不是环——树完整产出
/// （环判据＝回边指向**进行中**节点；已完成节点共享放行）。
TEST_F(IoResTest, DiamondIncludeIsNotCycle)
{
    writeText("top.xacro", xacroDoc({"left.xacro", "right.xacro"}));
    writeText("left.xacro", xacroDoc({"shared.xacro"}));
    writeText("right.xacro", xacroDoc({"shared.xacro"}));
    writeText("shared.xacro", xacroDoc({}));

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "top.xacro",
                                                                      nullptr, nullptr);
    ASSERT_TRUE(r) << "DAG 共享不得误判环（r.error=" << r.error.detail << "）";
    EXPECT_EQ(r.value.nodes.size(), 4u);
    EXPECT_EQ(r.value.edges.size(), 4u);
    EXPECT_EQ(r.value.rootRel, "top.xacro");
    // 稳定序（§9.6 确定性行）：节点按相对键字典序。
    EXPECT_EQ(r.value.nodes.front().relPath, "left.xacro");
    EXPECT_EQ(r.value.nodes.back().relPath, "top.xacro");
}

/// IO-V15 伴生：include 目标越出管辖根（../逃逸）→ IO-SEC-PATH-ESCAPE
/// （§6.2"include 目标必须落在导入根内"——NFR-SEC-01 红线抽查）。
TEST_F(IoResTest, IncludeEscapeOutsideImportRootRejected)
{
    writeText("outside.xacro", xacroDoc({}));
    writeText("entry.xacro", xacroDoc({"../outside.xacro"}));

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "entry.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecPathEscape);
}

/// XML 良构性违例（标签不闭合）→ IO-FORMAT-XML-SYNTAX＋行列定位（§6.1
/// "XML 良构检查"的 io 文件层职责；码面＝IO-T05 表尾追加——v0.8）。
TEST_F(IoResTest, MalformedXmlReportsSyntaxCodeWithLocation)
{
    writeText("bad.xacro", "<robot><xacro:include filename=\"b.xacro\"></robot>\n");

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "bad.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::FormatXmlSyntax);
    EXPECT_GT(std::stoi(paramOf(r.error, "row")), 0) << "解析器行定位（1 起）";
    EXPECT_GE(std::stoi(paramOf(r.error, "column")), 0) << "解析器列定位";
    EXPECT_FALSE(errorCodeToken(r.error.code).empty());
}

// =====================================================================
// V16 MeshBudget（acceptance 1——NFR-SEC-02）
// =====================================================================

/// IO-V16：二进制 STL 谎报三角形数 → 读前即拒（不读体）——
/// IO-SEC-BUDGET-MESH 比较型三要素（§6.3"84 字节头三角形数×50 字节与
/// 预算比对（超限即拒，不读体）"；观测点"触发时机读前"）。
TEST_F(IoResTest, BinaryStlDeclaredFacesLyingRejectedBeforeBodyRead)
{
    // 头声明 1,000,000 面，实体只有头（84 字节）——声明与实际严重不符。
    const fs::path p = write("lie.stl", binaryStl(1000000u, 0u));

    auto guard = makeBudgetGuard();
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::MeshFaceCount, 100);  // 收紧面数限额（调用方收紧——§4.5.2）
    const BudgetScopeId scope = guard->openScope(spec).value;

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> r = reader->snapshot(p, s, guard.get(), nullptr, scope);
    guard->closeScope(scope);

    ASSERT_FALSE(r) << "谎报面数必须读前拒绝（§6.3）";
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetMesh);
    EXPECT_EQ(paramOf(r.error, "actual"), "1000000");   // 三要素：声明面数
    EXPECT_EQ(paramOf(r.error, "limit"), "100");
    EXPECT_EQ(paramOf(r.error, "unit"), "count");
    EXPECT_NE(r.error.detail.find("读前"), std::string::npos)
        << "触发时机断言：声明面预检在读体前（detail 携带阶段短语）";
}

/// IO-V16：真实超限 ASCII STL → 流式统计在读中触发 IO-SEC-BUDGET-MESH
/// （§6.3"OBJ/DAE：流式统计…计数至预算——超限即中止"；观测点"触发时机
/// 读中"；"真实计数与预检不符→以实际计数触发预算"）。
TEST_F(IoResTest, AsciiStlRealOversizeTriggersMeshBudgetMidRead)
{
    const fs::path p = writeText("real.stl", asciiStl(12));     // 真实 12 面

    auto guard = makeBudgetGuard();
    BudgetSpec spec = BudgetSpec::productDefault();
    spec.tighten(BudgetDimension::MeshFaceCount, 10);
    const BudgetScopeId scope = guard->openScope(spec).value;

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> r = reader->snapshot(p, s, guard.get(), nullptr, scope);
    guard->closeScope(scope);

    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::SecBudgetMesh);
    // 单块读毕后计数检查：12 面 > 10 触发（actual=文件真实面数——单块
    // 文件的"读中"即整块计数点；无头声明谎报）。
    EXPECT_EQ(paramOf(r.error, "actual"), "12");
    EXPECT_EQ(paramOf(r.error, "limit"), "10");
    EXPECT_NE(r.error.detail.find("读中"), std::string::npos)
        << "触发时机断言：流式计数在读体中触发";
}

/// IO-V16 对照＋V27 伴生：真实小 STL（声明＝实际）正常快照——digest 与
/// 手工 core ContentDigester 复算一致（SA-12 唯一算法；传输完整性）。
TEST_F(IoResTest, ValidBinaryStlSnapshotDigestMatchesManual)
{
    const std::vector<std::uint8_t> bytes = binaryStl(5u, 5u);
    const fs::path p = write("ok.stl", bytes);

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> r = reader->snapshot(p, s, nullptr, nullptr);
    ASSERT_TRUE(r) << "预算内文件放行（r.error=" << r.error.detail << "）";
    EXPECT_EQ(r.value.contentDigest, manualDigest(bytes));
    EXPECT_EQ(r.value.sizeBytes, bytes.size());         // 实读总数＝文件大小
    EXPECT_NE(r.value.mtimeUtc, 0u) << "mtime 预筛字段就位（NT FILETIME）";
    const std::string expectName = "ok.stl";
    EXPECT_EQ(r.value.finalPath.filename().string(), expectName);
}

// =====================================================================
// V25 AccessDenied（acceptance 3——§4.2.5 四分类）
// =====================================================================

namespace aclutil {

/// ACL 拒读守卫：对目标文件注入 Everyone(S-1-1-0) 的 DENY ACE——只拒数据
/// 读（FILE_READ_DATA|FILE_READ_EA），保留属性读（GetFileAttributes/
/// status 仍可用——规范化存在性检查通过，失败必须发生在打开数据面）。
/// 析构恢复原 DACL（清理不留保护残留——失败不污染目标）。
class DenyReadGuard {
public:
    explicit DenyReadGuard(const fs::path& p) : m_path(p.wstring()) {}
    DenyReadGuard(const DenyReadGuard&) = delete;
    DenyReadGuard& operator=(const DenyReadGuard&) = delete;

    bool apply()
    {
        PSECURITY_DESCRIPTOR psd = nullptr;
        PACL dacl = nullptr;
        if (::GetNamedSecurityInfoW(m_path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                    nullptr, nullptr, &dacl, nullptr, &psd) != ERROR_SUCCESS) {
            return false;
        }
        SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
        PSID everyone = nullptr;
        if (!::AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                                        &everyone)) {
            ::LocalFree(psd);
            return false;
        }
        EXPLICIT_ACCESSW ea{};
        ea.grfAccessPermissions = FILE_READ_DATA | FILE_READ_EA;    // 拒数据读；属性读保留
        ea.grfAccessMode = DENY_ACCESS;
        ea.grfInheritance = NO_INHERITANCE;
        ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;                    // SID 形态——语言中立
        ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
        ea.Trustee.ptstrName = static_cast<LPWSTR>(everyone);
        PACL newDacl = nullptr;
        const DWORD rc = ::SetEntriesInAclW(1, &ea, dacl, &newDacl);
        ::FreeSid(everyone);
        if (rc != ERROR_SUCCESS) {
            ::LocalFree(psd);
            return false;
        }
        const DWORD rc2 = ::SetNamedSecurityInfoW(const_cast<LPWSTR>(m_path.c_str()),
                                                  SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                                  nullptr, nullptr, newDacl, nullptr);
        ::LocalFree(newDacl);
        if (rc2 != ERROR_SUCCESS) {
            ::LocalFree(psd);
            return false;
        }
        m_oldDacl = dacl;       // 保存原 DACL 与描述符——析构恢复
        m_sd = psd;
        return true;
    }

    ~DenyReadGuard()
    {
        if (m_sd != nullptr) {
            if (m_oldDacl != nullptr) {
                ::SetNamedSecurityInfoW(const_cast<LPWSTR>(m_path.c_str()), SE_FILE_OBJECT,
                                        DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                        m_oldDacl, nullptr);
            }
            ::LocalFree(m_sd);
        }
    }

private:
    std::wstring m_path;
    PACL m_oldDacl = nullptr;
    PSECURITY_DESCRIPTOR m_sd = nullptr;
};

} // namespace aclutil

/// IO-V25：ACL 拒读文件 → IO-RES-ACCESS-DENIED，**不降级为 NOT-FOUND**
/// （§4.2.5 四分类的互斥分码；V25 观测点"码＋方向参数"；规范化存在性
/// 检查通过（属性读保留）、失败发生在数据面打开——分类语义被精确钉住）。
TEST_F(IoResTest, AclDeniedReadMapsToAccessDeniedNotNotFound)
{
    const fs::path p = writeText("secret.txt", "top secret content\n");

    aclutil::DenyReadGuard guard(p);
    ASSERT_TRUE(guard.apply()) << "ACL 注入失败（环境不支持则本用例如实失败）";

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> r = reader->snapshot(p, s, nullptr, nullptr);
    ASSERT_FALSE(r) << "拒读必须失败";
    EXPECT_EQ(r.error.code, IoErrorCode::ResAccessDenied)
        << "不降级为 NOT-FOUND（§4.2.5 分类二）——实得 token: "
        << errorCodeToken(r.error.code);
    EXPECT_NE(r.error.code, IoErrorCode::ResNotFound);
    EXPECT_EQ(paramOf(r.error, "direction"), "read") << "方向参数（描述符 paramSchema 对齐）";
}

// =====================================================================
// V26 MissingResource（acceptance 3——NFR-REL-04/MDL-19；依赖树半边）
// =====================================================================

/// IO-V26：依赖树缺叶（include 指向不存在文件）→ IO-RES-MISSING＋缺失
/// 清单（params missing-count＋detail 逐项；缺失叶仍入树 exists=false
/// ——§6.5"节点缺席 → IO-RES-MISSING（缺失清单）"）。资源事实类别
/// （§2.5）：io 只上报文件层事实，错误面不携带、不暗示任何"工程不可行"
/// 结论（判定归 evidence 门禁——§6.6/§10.8；本断言面＝错误轨道只有
/// 资源码＋清单，无业务语义字段）。
TEST_F(IoResTest, DependencyTreeMissingLeafReportsMissingList)
{
    writeText("root.xacro", xacroDoc({"alive.xacro", "gone-a.xacro", "gone-b.xacro"}));
    writeText("alive.xacro", xacroDoc({}));

    const IResourceReaderPtr reader = makeResourceReader();
    const IoResult<ResourceDependencyTree> r = reader->dependencyTree(dir / "root.xacro",
                                                                      nullptr, nullptr);
    ASSERT_FALSE(r) << "存在缺失叶时整体失败（缺失清单聚合上报）";
    EXPECT_EQ(r.error.code, IoErrorCode::ResMissing);
    EXPECT_EQ(paramOf(r.error, "missing-count"), "2");
    EXPECT_NE(r.error.detail.find("gone-a.xacro"), std::string::npos) << "缺失清单逐项";
    EXPECT_NE(r.error.detail.find("gone-b.xacro"), std::string::npos) << "缺失清单逐项";
    EXPECT_NE(r.error.detail.find("资源事实"), std::string::npos)
        << "诊断类别＝资源事实（§2.5）——非业务结论";
}

// =====================================================================
// V27 DigestStability（acceptance 3——CON-05 内容身份）
// =====================================================================

/// IO-V27：同一文件 100 次快照 digest 恒定（§8.2"快照稳定性：同一未变
/// 文件重复读取 digest 必须相同"——确定性 NFR-COR-01 的资源面）。
TEST_F(IoResTest, SnapshotDigestStableAcross100Reads)
{
    std::vector<std::uint8_t> bytes(1024);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    const fs::path p = write("stable.bin", bytes);

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    Digest256 first{};
    for (int i = 0; i < 100; ++i) {
        const IoResult<ResourceSnapshot> r = reader->snapshot(p, s, nullptr, nullptr);
        ASSERT_TRUE(r) << "第 " << i << " 次快照失败: " << r.error.detail;
        if (i == 0) {
            first = r.value.contentDigest;
        }
        EXPECT_TRUE(r.value.contentDigest == first) << "第 " << i << " 次快照 digest 漂移";
        EXPECT_TRUE(r.value.sameContentAs(r.value)) << "同快照内容恒同";
    }
    EXPECT_EQ(first, manualDigest(bytes)) << "恒定值＝内容真实摘要（SA-12）";
}

/// IO-V27：改名/移动不改 digest——路径不作身份（§8.2"资源内容身份＝
/// contentDigest，不含路径"——SP-5 的资源面断言；V27 观测点"路径变化
/// 不改变 digest"）。
TEST_F(IoResTest, RenameDoesNotChangeDigestPathIsNotIdentity)
{
    const std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>('r'), static_cast<std::uint8_t>('e'),
                                             static_cast<std::uint8_t>('n'), static_cast<std::uint8_t>('a'),
                                             static_cast<std::uint8_t>('m'), static_cast<std::uint8_t>('e')};
    const fs::path p1 = write("before.bin", bytes);

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> before = reader->snapshot(p1, s, nullptr, nullptr);
    ASSERT_TRUE(before);

    const fs::path p2 = dir / "after-moved.bin";
    fs::rename(p1, p2);                                 // 同目录改名（保持内容）

    const IoResult<ResourceSnapshot> after = reader->snapshot(p2, s, nullptr, nullptr);
    ASSERT_TRUE(after);
    EXPECT_EQ(after.value.contentDigest, before.value.contentDigest) << "digest 不随路径变化";
    EXPECT_TRUE(before.value.contentId() == after.value.contentId());
    EXPECT_NE(before.value.finalPath, after.value.finalPath) << "实体路径确实不同（追溯提示变化）";
}

// =====================================================================
// 基础契约（open/read 句柄、格式识别、P-1 存在性四分类）
// =====================================================================

/// §9.6 open 契约：句柄流式读取至 EOF（读满→0）；finalPath＝实体路径。
TEST_F(IoResTest, OpenReadHandleReturnsBytesAndEof)
{
    const std::vector<std::uint8_t> bytes = {'A', 'B', 'C', 'D', 'E'};
    const fs::path p = write("abc.bin", bytes);

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    IoResult<ResourceStreamHandle> h = reader->open(p, s, nullptr, nullptr);
    ASSERT_TRUE(h) << "open 失败: " << h.error.detail;
    ResourceStreamHandle& handle = h.value;
    ASSERT_TRUE(handle.isOpen());
    EXPECT_EQ(handle.sizeBytes(), bytes.size());

    std::vector<std::uint8_t> got(bytes.size() + 8, 0);
    IoResult<std::size_t> r = handle.read(got.data(), got.size());
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value, bytes.size());                   // 一次读满（≤块大小）
    got.resize(r.value);
    EXPECT_EQ(got, bytes);
    r = handle.read(got.data(), got.size());
    ASSERT_TRUE(r);
    EXPECT_EQ(r.value, 0u) << "EOF＝0（§9.6 read 契约）";
}

/// §6.3 格式识别：STL 二进制/ASCII 判定、扩展名族、未知格式→
/// IO-FORMAT-MESH-UNKNOWN＋首 16 字节 hex（"脱敏无虞"）。
TEST_F(IoResTest, IdentifyKnowsMeshAndTextureFamilies)
{
    const IResourceReaderPtr reader = makeResourceReader();

    const IoResult<ResourceKind> bin = reader->identify(write("m.stl", binaryStl(3u, 3u)));
    ASSERT_TRUE(bin);
    EXPECT_EQ(bin.value, ResourceKind::BinaryStl);

    const IoResult<ResourceKind> asc = reader->identify(writeText("a.stl", asciiStl(2)));
    ASSERT_TRUE(asc);
    EXPECT_EQ(asc.value, ResourceKind::AsciiStl);

    const IoResult<ResourceKind> obj = reader->identify(writeText("m.obj", "v 0 0 0\nf 1 1 1\n"));
    ASSERT_TRUE(obj);
    EXPECT_EQ(obj.value, ResourceKind::WavefrontObj);

    const IoResult<ResourceKind> tex = reader->identify(
        write("t.png", {0x89, static_cast<std::uint8_t>('P'), static_cast<std::uint8_t>('N'),
                        static_cast<std::uint8_t>('G')}));
    ASSERT_TRUE(tex);
    EXPECT_EQ(tex.value, ResourceKind::TextureBytes);   // §6.3 纹理按字节资源

    const IoResult<ResourceKind> unknown = reader->identify(writeText("x.dat", "not-a-known-format!!"));
    ASSERT_FALSE(unknown) << "识别失败必须拒绝（§6.3）";
    EXPECT_EQ(unknown.error.code, IoErrorCode::FormatMeshUnknown);
    EXPECT_EQ(paramOf(unknown.error, "head-hex").size(), 32u) << "首 16 字节＝32 hex 字符";
}

/// §4.2.5 分类一：P-1 目标不存在 → IO-RES-NOT-FOUND（存在性检查在
/// SafePath 规范化步——§9.1 P-1 准入校验）。
TEST_F(IoResTest, UserSourceMissingFileIsNotFound)
{
    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::UserSource;
    const IoResult<ResourceSnapshot> r = reader->snapshot(dir / "no-such-file.bin", s,
                                                          nullptr, nullptr);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error.code, IoErrorCode::ResNotFound);
}

/// P-5 角色（项目内资源引用）非空 base 走 SafePath 管辖——区内引用快照
/// 正常产出（ResourceOpenSpec.base 的 P-5 通道冒烟；SP-3 区内判定由
/// SafePath 已验——此处验证资源通道接线正确）。
TEST_F(IoResTest, ProjectResourceRefRoleSnapshotWorksWithBase)
{
    const fs::path objects = dir / "objects";
    fs::create_directories(objects);
    const std::vector<std::uint8_t> bytes = {static_cast<std::uint8_t>('p'),
                                             static_cast<std::uint8_t>('5')};
    const fs::path p = objects / "res.bin";
    {
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    const IResourceReaderPtr reader = makeResourceReader();
    ResourceOpenSpec s;
    s.role = PathRole::ProjectResourceRef;
    s.base = dir;                                       // 项目根（资源区＝根下 objects/）
    // P-5 要求纯相对引用＋基点（§4.3.3：绝对形式持久化引用非法）。
    const IoResult<ResourceSnapshot> r = reader->snapshot(std::filesystem::path(L"objects/res.bin"),
                                                          s, nullptr, nullptr);
    ASSERT_TRUE(r) << "P-5 区内引用快照失败: " << r.error.detail;
    EXPECT_EQ(r.value.contentDigest, manualDigest(bytes));
}

} // namespace
