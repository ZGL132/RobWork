/**
 * @file   ObjectStoreTest.cpp
 * @brief  对象库（ObjectStore）用例组——内容编址/只增发布/共享去重/篡改检测/
 *         引用存在性/LRU 缓存预算（任务契约 PRJ-T05.json acceptance 1～4）。
 *
 * 设计依据：
 *   - units/project.md §4.6（对象库五段语义——编址/只增/校验/缓存/引用
 *     存在性）、§4.4.6（对象文件＝负载原样字节——D-10）、§4.1（objects/
 *     <oid>/<cv> 命名规则）、§5.2（tryObject/object 错误表——引用缺失＝
 *     store-corrupt）、§7.1 第 2/4 步（暂存写闸门→publishNew 只增发布）、
 *     PA-2（既有读者引用永不被覆盖）、PA-3（值语义隔离）；
 *   - 需求 CON-01（身份/版本包络的持久化编址——本组钉住"包络的持久化
 *     编址"半边；快照组装归 evidence，不在本组）；
 *   - 任务契约 tasks/foundation/PRJ-T05.json acceptance 逐条（用例名后缀
 *     标注对应条目序号；dtb 行＝WP-04-T05）。
 *
 * 陷阱处置自证（acceptance 4）：
 *   - P-PR-1：本组对 core 的消费面仅限公共契约头（Identity.hpp 的
 *     ObjectId/ContentVersion 解析与格式化、Digest.hpp 的 ContentDigester
 *     直算对照）；include 面红线由 BuildRedLineTest/LinkageContractTest
 *     持续扫描（零 core 修改的构建图证据在 contract_test 目标）。
 *   - CR-02：CoreContract 组对"摘要只经 core ContentDigester"做三重钉住
 *     ——发布返回值／磁盘文件名解析回读／core 直算三方逐字节相等；本地
 *     出现第二哈希路径时三者必然失谐。
 */

#include "ObjectStore.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Codec.hpp"
#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

namespace fs = std::filesystem;
using sdurws::ird::core::ContentDigester;
using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::ObjectId;
using sdurws::ird::project::IDiagnosticsSink;
using sdurws::ird::project::StoreError;
using sdurws::ird::project::StoreErrorCode;
namespace objstore = sdurws::ird::project::objstore;
using objstore::ObjectKey;
using objstore::ObjectScanReport;

namespace {

// ---------------------------------------------------------------------
// 夹具辅助（AtomicFileTest 同款最小本地形态；testkit TempDir 落地前的
// 承接——§11 头注口径：用例自持目录、失败保留现场不静默掩盖）。
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

/// 独立于被测对象的裸写（造前置/篡改文件用——不经 ObjectStore，保证前置
/// 数据不依赖被测代码的正确性）。失败由调用方对返回值 ASSERT。
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
 * @brief 开发诊断捕获 sink——§5.0 IDiagnosticsSink 的测试侧实现。
 *
 * 只记录不判断（report 记录、reportDev 记通道＋消息对）；断言在用例内做。
 */
class CapturingSink : public IDiagnosticsSink {
public:
    void report(const sdurws::ird::core::DiagnosticRecord& record) override
    {
        m_reports.push_back(record);
    }
    void reportDev(const std::string& channel,
                   const std::string& message) override
    {
        m_devCalls.emplace_back(channel, message);
    }

    /// 已捕获的开发诊断（channel, message）对列表。
    const std::vector<std::pair<std::string, std::string>>& devCalls() const
    {
        return m_devCalls;
    }

private:
    std::vector<sdurws::ird::core::DiagnosticRecord> m_reports;
    std::vector<std::pair<std::string, std::string>> m_devCalls;
};

/// 独立于被测实现的核心摘要直算（CR-02 三方对照的"core 原始"一臂）。
ContentVersion directCoreDigest(const std::string& bytes)
{
    ContentDigester d;
    d.update(bytes.data(), bytes.size());
    ContentVersion cv;
    cv.bytes = d.finalize();
    return cv;
}

/// cv → 对象文件名（64 hex 无 tag）——测试侧路径构造与实现同源
/// （toCanonical 剥 "cv-" tag；这是 core 公共契约的消费，不是第二格式化）。
std::string cvFileName(const ContentVersion& cv)
{
    return cv.toCanonical().substr(3);
}

/// 断言可调用体抛出携带指定稳定码的 StoreError（单次执行——不重复触发
/// 副作用；抛出其他异常类型时该异常向上传播，由 gtest 判失败）。
template <class Fn>
void expectStoreCode(Fn&& fn, StoreErrorCode code, const char* what)
{
    try {
        fn();
        ADD_FAILURE() << what << ": 预期 StoreError 未抛出";
    } catch (const StoreError& e) {
        EXPECT_TRUE(e.code() == code)
            << what << ": 稳定码不符，实得 " << static_cast<int>(e.code())
            << "，detail=" << e.what();
    }
}

}  // namespace

// ---------------------------------------------------------------------
// 用例组夹具：每用例独立子目录；objects/、staging/ 双目录。
// ---------------------------------------------------------------------

class ObjectStoreTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_dir = baseDir() / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec) << "建临时目录失败: " << ec.message();
    }

    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_objstore_test"
                 / std::to_string(::GetCurrentProcessId());
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            // 建根失败属环境不可用——致命退出，不产生全红噪声报告。
            std::cerr << "无法创建测试根目录: " << s_base.string() << " ("
                      << ec.message() << ")\n";
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    /// 用例专属根目录。
    const fs::path& dir() const { return m_dir; }
    /// 对象区路径（默认布局位）。
    fs::path objectsDir() const { return m_dir / "objects"; }
    /// 暂存区路径（默认布局位）。
    fs::path stagingDir() const { return m_dir / "staging"; }

    /// 构造绑定本用例目录的 ObjectStore（budget 可配；sink 可选注入）。
    objstore::ObjectStore makeStore(std::size_t budgetBytes
                                    = objstore::ObjectStore::kDefaultCacheBudgetBytes,
                                    IDiagnosticsSink* sink = nullptr)
    {
        return objstore::ObjectStore(objectsDir(), stagingDir(), budgetBytes,
                                     sink);
    }

    /// 某对象的磁盘文件路径（按 §4.1 命名规则拼装——测试侧独立构造）。
    fs::path objectFile(const ObjectId& oid, const ContentVersion& cv) const
    {
        return objectsDir() / oid.toCanonical() / cvFileName(cv);
    }

private:
    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;

    static fs::path baseDir() { return s_base; }
};

fs::path ObjectStoreTest::s_base;
int ObjectStoreTest::s_caseCounter = 0;

// =====================================================================
// 用例组 1：ObjectStorePublish——内容编址/只增发布/共享去重
// （acceptance 1：内容编址与只增发布/同对象共享去重单测）
// =====================================================================

/**
 * acceptance 1／§4.1/§4.4.6：发布后对象文件就位于 objects/<oid>/<cv>，
 * 文件名为 cv 的 64 hex 无 tag 形态，目录名为 oid 规范文本。
 */
TEST_F(ObjectStoreTest, Publish_CreatesContentAddressedPath_CON01)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "robot-design-payload-v1";
    const ContentVersion cv = store.publishObject(oid, payload);

    const fs::path expected = objectsDir() / oid.toCanonical() / cvFileName(cv);
    ASSERT_TRUE(fs::exists(expected)) << "对象文件未就位: " << expected.string();
    // 目录名必须恰为 oid 规范文本（§4.1 命名规则——防自由命名漂移）。
    EXPECT_EQ(objectsDir() / oid.toCanonical(), expected.parent_path());
    // 文件内容＝负载原样字节（无信封——D-10 的磁盘半边）。
    EXPECT_EQ(payload, readAll(expected));
}

/**
 * acceptance 1＋4（CR-02）／§4.6/§4.8：发布返回的内容版本＝core
 * ContentDigester 对负载的直算结果＝本单元唯一哈希入口 contentVersionOf。
 * 三方失谐即意味着出现了第二哈希路径。
 */
TEST_F(ObjectStoreTest, Publish_ReturnsCv_MatchesCoreDigesterAndCodecEntry_CR02)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "canonical-bytes-0123456789";

    const ContentVersion cv = store.publishObject(oid, payload);
    EXPECT_TRUE(cv.isValid());
    // 一臂：core 直算（绕开 project 全部代码）。
    EXPECT_TRUE(cv == directCoreDigest(payload));
    // 另一臂：本单元唯一哈希入口（CR-02——ObjectStore 内部路径与
    // contentVersionOf 一致，而 contentVersionOf 已在 PRJ-T04 钉住＝core）。
    EXPECT_TRUE(cv == sdurws::ird::project::codec::contentVersionOf(payload));
}

/**
 * acceptance 1／§4.6 只增发布共享分支：同 oid 同内容二次发布＝共享去重
 * ——磁盘上恰一个文件、既有文件字节与修改时间零变化（不被触碰）。
 */
TEST_F(ObjectStoreTest, Publish_SameContent_SharesSingleFileUnchanged_CON01)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "shared-content-bytes";

    const ContentVersion cv1 = store.publishObject(oid, payload);
    const fs::path file = objectFile(oid, cv1);
    ASSERT_TRUE(fs::exists(file));
    const auto mtimeBefore = fs::last_write_time(file);
    const std::string bytesBefore = readAll(file);

    // 同 oid 同内容再发布——publishNew 命中 ERROR_ALREADY_EXISTS →
    // 摘要一致 → 共享（磁盘零写入）。
    const ContentVersion cv2 = store.publishObject(oid, payload);
    EXPECT_TRUE(cv1 == cv2) << "同内容二次发布应返回同一内容版本";

    // 磁盘证据：该 oid 目录下恰一个文件；字节与 mtime 均未变。
    std::error_code ec;
    std::size_t fileCount = 0;
    for (fs::directory_iterator it(objectFile(oid, cv1).parent_path(), ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        ++fileCount;
    }
    ASSERT_FALSE(ec);
    EXPECT_EQ(1u, fileCount) << "同内容共享去重失败：目录内出现多份文件";
    EXPECT_EQ(bytesBefore, readAll(file));
    EXPECT_EQ(mtimeBefore, fs::last_write_time(file))
        << "共享发布不得触碰既有文件（只增不改）";
}

/**
 * acceptance 1／§4.1.1 四概念区分：同内容不同 ObjectId＝不同对象——
 * 各有各的目录与文件，对象身份不承载内容信息（内容寻址不并拢对象）。
 */
TEST_F(ObjectStoreTest, Publish_SameContentDifferentOid_NoCrossObjectSharing_CON01)
{
    auto store = makeStore();
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const std::string payload = "identical-bytes";

    const ContentVersion cvA = store.publishObject(oidA, payload);
    const ContentVersion cvB = store.publishObject(oidB, payload);
    EXPECT_TRUE(cvA == cvB) << "内容相同则内容版本相同（内容寻址）";
    EXPECT_FALSE(oidA == oidB);
    // 两个对象各持一份文件（对象身份维度不共享——§4.6"键＝(oid,cv)"）。
    EXPECT_TRUE(fs::exists(objectFile(oidA, cvA)));
    EXPECT_TRUE(fs::exists(objectFile(oidB, cvB)));
}

/**
 * acceptance 1／§4.6：同 oid 新内容版本＝旁置新文件（objects/<oid>/ 下
 * 两个 cv 文件并存），两版本均可读回各自字节。
 */
TEST_F(ObjectStoreTest, Publish_NewVersion_AlongsidesOldBothReadable_CON01)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string v1 = "version-one-payload";
    const std::string v2 = "version-two-payload-different";

    const ContentVersion cv1 = store.publishObject(oid, v1);
    const ContentVersion cv2 = store.publishObject(oid, v2);
    EXPECT_FALSE(cv1 == cv2) << "内容不同则内容版本必不同";

    // 两版本并存（只增——v1 文件不被 v2 替换或删除）。
    EXPECT_TRUE(fs::exists(objectFile(oid, cv1)));
    EXPECT_TRUE(fs::exists(objectFile(oid, cv2)));
    EXPECT_EQ(v1, readAll(objectFile(oid, cv1)));
    EXPECT_EQ(v2, readAll(objectFile(oid, cv2)));
}

/**
 * acceptance 1＋3／§4.4.6：空负载（0 字节）是合法对象——cv＝FIPS 180-2
 * 空串向量 e3b0c442…，读回为空字节序列。
 */
TEST_F(ObjectStoreTest, Publish_EmptyPayload_RoundTrip_FipsEmptyVector_CON01)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = store.publishObject(oid, std::string_view{});

    // SHA-256("") 标准向量（FIPS 180-2——与 CORE-T02/PRJ-T04 同源钉法）。
    EXPECT_EQ("cv-e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              cv.toCanonical());
    const auto bytes = store.tryObject(oid, cv);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_TRUE(bytes->empty());
}

/**
 * acceptance 1（边界）／AGENTS 错误二分：全零 ObjectId＝保留值不是身份
 * ——调用方契约违约走 std::invalid_argument fail-fast（不是 StoreError）。
 */
TEST_F(ObjectStoreTest, Publish_AllZeroOid_FailFastInvalidArgument)
{
    auto store = makeStore();
    const ObjectId zero{};
    EXPECT_THROW(store.publishObject(zero, "bytes"), std::invalid_argument);
}

/**
 * acceptance 2（发布侧篡改拒绝）／§4.6"目标已存在时…不一致→store-corrupt"：
 * 预置一个内容与编址名不符的文件，发布同编址对象 → StoreCorrupt，且既有
 * 文件字节不被覆盖（只增不改——篡改内容留待人工/恢复处置，绝不静默重写）。
 */
TEST_F(ObjectStoreTest, Publish_ExistingTargetDigestMismatch_RejectsStoreCorruptNoOverwrite)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "genuine-payload";
    const ContentVersion cv = store.publishObject(oid, payload);

    // 外部篡改：同路径写入异内容（模拟磁盘被外部改写后的重发布场景）。
    const std::string tampered = "tampered-content!!";
    const fs::path file = objectFile(oid, cv);
    ASSERT_TRUE(writeRaw(file, tampered));

    // 同 oid 同内容重发布：目标已存在→校验既有摘要≠cv→store-corrupt。
    expectStoreCode([&] { store.publishObject(oid, payload); },
                    StoreErrorCode::StoreCorrupt,
                    "发布侧既有文件摘要不符");

    // 关键断言：失败路径绝不覆盖既有文件（内容仍是被篡改后的字节——
    // 覆盖决策不属于对象库）。
    EXPECT_EQ(tampered, readAll(file));
}

// =====================================================================
// 用例组 2：ObjectStoreRead——读取校验/tryObject 语义/D-10
// （acceptance 2 篡改检测；acceptance 3 引用存在性＋原样存储）
// =====================================================================

/**
 * acceptance 3／§4.6 引用存在性：对象不存在的请求（含从未发布、目录不
 * 存在两种形态）返回空 optional 且不抛——存在性判定与升级决策归调用方。
 */
TEST_F(ObjectStoreTest, TryObject_MissingObject_ReturnsNulloptNoThrow_CON01)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "present-object";
    const ContentVersion cv = store.publishObject(oid, payload);

    // 形态一：同 oid、从未发布过的内容版本（文件不存在，目录存在）。
    const ContentVersion ghostCv = directCoreDigest("never-published");
    bool threw = false;
    std::optional<std::vector<std::uint8_t>> missing;
    try {
        missing = store.tryObject(oid, ghostCv);
    } catch (...) {
        threw = true;
    }
    EXPECT_FALSE(threw) << "缺失请求不得抛（tryObject 语义）";
    EXPECT_FALSE(missing.has_value());

    // 形态二：oid 目录整体不存在（从未发布过的对象）。
    const ObjectId ghostOid = ObjectId::generate();
    EXPECT_FALSE(store.tryObject(ghostOid, cv).has_value());
}

/**
 * acceptance 3／§5.2 错误表：object()（断言形态）对缺失引用抛
 * StoreCorrupt——调用方已断言引用存在，缺失即损坏而非"没有"。
 */
TEST_F(ObjectStoreTest, Object_MissingReferenced_ThrowsStoreCorrupt)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = directCoreDigest("absent");
    expectStoreCode([&] { store.object(oid, cv); },
                    StoreErrorCode::StoreCorrupt, "引用缺失");
}

/**
 * acceptance 2（读取侧篡改检测）／§4.6 首次加载校验：已发布对象的文件
 * 内容被外部翻转一个字节后，tryObject/object 读取即拒（StoreCorrupt）；
 * 扫描全量档同判（corruptReferenced）。
 */
TEST_F(ObjectStoreTest, TryObject_TamperedContent_RejectedStoreCorrupt)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "tamper-detection-payload";
    const ContentVersion cv = store.publishObject(oid, payload);
    const fs::path file = objectFile(oid, cv);

    // 篡改：等长翻转一个字节（size 不变——专测摘要半边的检出能力）。
    std::string evil = payload;
    evil[3] = static_cast<char>(evil[3] ^ 0x01);
    ASSERT_TRUE(writeRaw(file, evil));

    expectStoreCode([&] { store.tryObject(oid, cv); },
                    StoreErrorCode::StoreCorrupt, "tryObject 篡改拒绝");
    expectStoreCode([&] { store.object(oid, cv); },
                    StoreErrorCode::StoreCorrupt, "object 篡改拒绝");

    // 扫描全量档同判：引用该对象 → corruptReferenced（数据侧定位到键）。
    const ObjectScanReport scan =
        store.scanObjects({ObjectKey{oid, cv}}, /*verifyDigest=*/true);
    EXPECT_TRUE(scan.missingReferenced.empty());
    ASSERT_EQ(1u, scan.corruptReferenced.size());
    // 断言表达式整体加括号：ObjectKey{a, b} 内的逗号会被宏当参数分隔符。
    EXPECT_TRUE((scan.corruptReferenced[0] == ObjectKey{oid, cv}));
}

/**
 * acceptance 2（边界）／§4.6 size＋SHA-256 校验：追加/截断（size 半边）
 * 的篡改同样拒绝——校验是"size＋摘要"双半边，不是只比摘要。
 */
TEST_F(ObjectStoreTest, TryObject_TamperedSize_RejectedStoreCorrupt)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string payload = "size-tamper-payload";
    const ContentVersion cv = store.publishObject(oid, payload);
    const fs::path file = objectFile(oid, cv);

    // 追加字节（size 变化；内容前缀仍与 cv 前缀一致——只有 size 校验能查）。
    ASSERT_TRUE(writeRaw(file, payload + "extra"));

    expectStoreCode([&] { store.tryObject(oid, cv); },
                    StoreErrorCode::StoreCorrupt, "size 篡改拒绝");
}

/**
 * acceptance 3（D-10/N-9 边界）／§4.4.6：任意二进制负载（全 256 字节值，
 * 含 0x00/0xFF/非法 UTF-8 序列）原样存储原样读回——磁盘字节逐位等于负载
 * （无信封、无转码、无业务解释）。
 */
TEST_F(ObjectStoreTest, Read_BinaryPayloadPreservedByteExactly_D10)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    // 构造覆盖全部 256 字节值的负载（含控制字符与非法 UTF-8 序列 0x80…）。
    std::string payload;
    for (int b = 0; b < 256; ++b) {
        payload.push_back(static_cast<char>(b));
    }
    payload += "\x80\x81\x82";  // 裸 UTF-8 尾随字节＝无效序列（D-10：不解释）

    const ContentVersion cv = store.publishObject(oid, payload);
    const auto bytes = store.tryObject(oid, cv);
    ASSERT_TRUE(bytes.has_value());
    ASSERT_EQ(payload.size(), bytes->size());
    EXPECT_TRUE(0 == std::memcmp(payload.data(), bytes->data(), bytes->size()))
        << "读回字节与负载不一致（D-10 违约）";
    // 磁盘半边：文件字节逐位等于负载。
    EXPECT_EQ(payload, readAll(objectFile(oid, cv)));
}

/**
 * acceptance 2（免复检语义钉住）／§4.6"摘要缓存后免复检"：同一上下文内
 * 已通过首次校验的 (oid,cv)，其校验结论不随负载缓存逐出失效——文件在
 * 校验之后被篡改，同上下文再读不再检出（按 §4.6 语义交付）；换新上下文
 * （重新打开）则检出。两组断言共同钉住"校验结论的生命周期＝存储上下文"。
 */
TEST_F(ObjectStoreTest, Read_VerifiedOnce_FreeOfRecheckWithinContext_PinnedSemantic)
{
    const std::size_t kBudget = 100;  // 字节——恰容一个 100B 对象
    const std::string payload(100, 'a');

    const ObjectId oid = ObjectId::generate();
    auto store = makeStore(kBudget);
    const ContentVersion cv = store.publishObject(oid, payload);
    const ObjectId oidB = ObjectId::generate();
    const ContentVersion cvB = store.publishObject(oidB, std::string(100, 'b'));

    // 读 A：缓存 miss→首次读盘实算校验→通过并记入已校验集→A 进缓存。
    ASSERT_TRUE(store.tryObject(oid, cv).has_value()) << "A 首次读取应通过校验";
    // 读 B 把 A 的字节缓存条目逐出（预算 100B）——已校验集不受逐出影响。
    ASSERT_TRUE(store.tryObject(oidB, cvB).has_value());
    EXPECT_EQ(1u, store.cacheStats().entryCount);

    // 篡改 A 的文件（校验之后发生）。
    ASSERT_TRUE(writeRaw(objectFile(oid, cv), std::string(100, 'X')));

    // 同上下文再读：已校验集命中→免复检（§4.6 原文语义）→交付读得字节。
    const auto reread = store.tryObject(oid, cv);
    ASSERT_TRUE(reread.has_value());
    EXPECT_EQ(std::string(100, 'X'),
              std::string(reread->begin(), reread->end()));

    // 新上下文（新 ObjectStore 实例＝重开存储）：无已校验集→首读实算→
    // 检出篡改＝StoreCorrupt。
    auto reopened = makeStore(kBudget);
    expectStoreCode([&] { reopened.tryObject(oid, cv); },
                    StoreErrorCode::StoreCorrupt, "重开后首读应检出篡改");
}

/**
 * acceptance 3（边界）／AGENTS 错误二分：tryObject 对全零保留值键
 * fail-fast（std::invalid_argument）——不伪装成"对象不存在"。
 */
TEST_F(ObjectStoreTest, TryObject_AllZeroKeys_FailFastInvalidArgument)
{
    auto store = makeStore();
    const ObjectId validOid = ObjectId::generate();
    const ContentVersion validCv = directCoreDigest("x");
    EXPECT_THROW(store.tryObject(ObjectId{}, validCv), std::invalid_argument);
    EXPECT_THROW(store.tryObject(validOid, ContentVersion{}),
                 std::invalid_argument);
}

// =====================================================================
// 用例组 3：ObjectStoreImmutability——只增不改/既有读者隔离（PA-2/PA-3）
// （acceptance 2：对象文件不可变——写入只增新内容、既有读者引用永不被覆盖）
// =====================================================================

/**
 * acceptance 2（PA-2）／§4.6 查询快照与写入隔离：发布 v1→读者取字节→
 * 发布 v2→断言 v1 磁盘字节不变、v1 仍可读回、读者持有的拷贝不受影响、
 * v1 重复发布仍走共享路径。写入只增新内容，既有引用永不被覆盖。
 */
TEST_F(ObjectStoreTest, PublishNewVersion_OldVersionAndReadersNeverOverwritten_PA2)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const std::string v1 = "immutable-old-version";
    const std::string v2 = "brand-new-version-content";

    const ContentVersion cv1 = store.publishObject(oid, v1);
    const auto reader = store.tryObject(oid, cv1);
    ASSERT_TRUE(reader.has_value());
    const auto mtime1 = fs::last_write_time(objectFile(oid, cv1));

    // 写入只增新内容（v2＝新文件，v1 文件不被触碰）。
    const ContentVersion cv2 = store.publishObject(oid, v2);
    EXPECT_FALSE(cv1 == cv2);

    // v1 磁盘字节与 mtime 不变（未被覆盖/未 touch）。
    EXPECT_EQ(v1, readAll(objectFile(oid, cv1)));
    EXPECT_EQ(mtime1, fs::last_write_time(objectFile(oid, cv1)));

    // 既有读者引用：读回仍为 v1 字节；此前取走的拷贝保持原值（PA-3）。
    const auto readerAgain = store.tryObject(oid, cv1);
    ASSERT_TRUE(readerAgain.has_value());
    EXPECT_EQ(v1, std::string(readerAgain->begin(), readerAgain->end()));
    EXPECT_EQ(v1, std::string(reader->begin(), reader->end()));

    // v1 重发布＝共享路径（不报错、不产生第二文件）。
    EXPECT_TRUE(cv1 == store.publishObject(oid, v1));
    EXPECT_EQ(v1, readAll(objectFile(oid, cv1)));
}

// =====================================================================
// 用例组 4：ObjectStoreCache——LRU 缓存预算（acceptance 1 缓存预算半边）
// =====================================================================

/**
 * acceptance 1／§4.6 LRU：预算 200B、三个 100B 对象——缓存只由读路径
 * 填充（§4.6 惰性加载）；读序 A→B→A（触碰）→C 触发逐出时，最久未用的
 * B 被逐出，最近使用的 A 保留。
 */
TEST_F(ObjectStoreTest, Cache_EvictsLeastRecentlyUsed_WithinBudget)
{
    const std::size_t kBudget = 200;
    auto store = makeStore(kBudget);
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const ContentVersion cvA = store.publishObject(oidA, std::string(100, 'a'));
    const ContentVersion cvB = store.publishObject(oidB, std::string(100, 'b'));
    const ObjectKey keyA{oidA, cvA};
    const ObjectKey keyB{oidB, cvB};

    // 发布不触缓存（惰性加载语义）。
    EXPECT_EQ(0u, store.cacheStats().entryCount);

    // 读 A、读 B：缓存＝[A, B]（A 更旧）；再读 A 触碰：[B, A]。
    ASSERT_TRUE(store.tryObject(oidA, cvA).has_value());
    ASSERT_TRUE(store.tryObject(oidB, cvB).has_value());
    auto order = store.cacheLruOrder();
    ASSERT_EQ(2u, order.size());
    EXPECT_TRUE((order[0] == keyA && order[1] == keyB));
    ASSERT_TRUE(store.tryObject(oidA, cvA).has_value());
    order = store.cacheLruOrder();
    ASSERT_EQ(2u, order.size());
    EXPECT_TRUE((order[0] == keyB && order[1] == keyA));

    // 读 C（100B）入缓存超预算 → 逐出最久未用的 B；A 因刚被触碰而保留。
    const ObjectId oidC = ObjectId::generate();
    const ContentVersion cvC = store.publishObject(oidC, std::string(100, 'c'));
    ASSERT_TRUE(store.tryObject(oidC, cvC).has_value());
    order = store.cacheLruOrder();
    ASSERT_EQ(2u, order.size());
    EXPECT_TRUE((order[0] == keyA && order[1] == ObjectKey{oidC, cvC}))
        << "LRU 应保留最近使用的 A、逐出最久未用的 B";

    const auto stats = store.cacheStats();
    EXPECT_EQ(2u, stats.entryCount);
    EXPECT_EQ(200u, stats.byteCount);
    EXPECT_EQ(kBudget, stats.budgetBytes);

    // 被逐出的 B 仍可正常读（缓存是纯加速层——读路径不依赖缓存存在）。
    const auto b = store.tryObject(oidB, cvB);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(std::string(100, 'b'), std::string(b->begin(), b->end()));
}

/**
 * acceptance 1（边界）／§4.6 预算语义：单条目超预算＝整条不缓存（抖动
 * 防护），但读取不受影响；恰等于预算＝缓存（≤ 口径）。
 */
TEST_F(ObjectStoreTest, Cache_OversizeNotCached_ExactFitCached)
{
    // 半一：150B 对象 vs 100B 预算——可读、不缓存。
    auto store = makeStore(100);
    const ObjectId big = ObjectId::generate();
    const ContentVersion bigCv = store.publishObject(big, std::string(150, 'B'));
    const auto bytes = store.tryObject(big, bigCv);
    ASSERT_TRUE(bytes.has_value());
    EXPECT_EQ(150u, bytes->size());
    EXPECT_EQ(0u, store.cacheStats().entryCount)
        << "超预算条目不得进入缓存";
    EXPECT_EQ(0u, store.cacheStats().byteCount);

    // 半二：恰等预算——缓存。
    auto exactStore = makeStore(100);
    const ObjectId fit = ObjectId::generate();
    const ContentVersion fitCv =
        exactStore.publishObject(fit, std::string(100, 'f'));
    ASSERT_TRUE(exactStore.tryObject(fit, fitCv).has_value());
    EXPECT_EQ(1u, exactStore.cacheStats().entryCount);
    EXPECT_EQ(100u, exactStore.cacheStats().byteCount);
}

/**
 * acceptance 2（PA-3）／§4.6 值语义：读者取走的深拷贝不随缓存逐出/磁盘
 * 变化而变——交付后即与存储状态完全隔离。
 */
TEST_F(ObjectStoreTest, Cache_EvictionDoesNotAffectHeldCopies_PA3)
{
    auto store = makeStore(100);
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = store.publishObject(oid, std::string(100, 'p'));
    const auto held = store.tryObject(oid, cv);
    ASSERT_TRUE(held.has_value());

    // 读入另一对象把 A 挤出缓存，再篡改 A 的磁盘文件（极端组合——证明
    // 隔离的完备性：缓存与磁盘都变了，持有者不受影响）。
    const ObjectId oidQ = ObjectId::generate();
    const ContentVersion cvQ =
        store.publishObject(oidQ, std::string(100, 'q'));
    ASSERT_TRUE(store.tryObject(oidQ, cvQ).has_value());
    ASSERT_EQ(1u, store.cacheStats().entryCount);  // 预算 100B：恰剩 Q
    ASSERT_TRUE(writeRaw(objectFile(oid, cv), std::string(100, '!')));

    EXPECT_EQ(std::string(100, 'p'), std::string(held->begin(), held->end()))
        << "读者持有的拷贝必须与存储后续状态无关（PA-3）";
}

/**
 * acceptance 1（并发面）／§4.6"缓存互斥内部实现"：多线程并发读＋主线程
 * 并发发布的混合压力下无数据竞争崩溃、读结果始终正确（线程安全自证；
 * 端口级并发归 PRJ-T09 的 PRJ-TX 并发只读单测）。
 */
TEST_F(ObjectStoreTest, ConcurrentReads_WhilePublishing_ThreadSafe)
{
    auto store = makeStore();  // 默认预算——不触发逐出路径的纯并发读
    const ObjectId oidP = ObjectId::generate();
    const std::string payloadP(1000, 'P');
    const ContentVersion cvP = store.publishObject(oidP, payloadP);

    // 后台发布两个新版本（与读者并发——只增语义下读者永不见半文件）。
    const ObjectId oid = oidP;
    std::thread publisher([&store, oid] {
        for (int i = 0; i < 20; ++i) {
            store.publishObject(oid, "concurrent-publish-" + std::to_string(i));
        }
    });

    auto reader = [&store, oidP, cvP, &payloadP] {
        for (int i = 0; i < 200; ++i) {
            const auto bytes = store.tryObject(oidP, cvP);
            ASSERT_TRUE(bytes.has_value());
            ASSERT_EQ(payloadP.size(), bytes->size());
            EXPECT_EQ(0, std::memcmp(payloadP.data(), bytes->data(),
                                     bytes->size()))
                << "并发读得字节失真（迭代 " << i << "）";
        }
    };
    std::thread r1(reader), r2(reader), r3(reader);
    publisher.join();
    r1.join();
    r2.join();
    r3.join();
}

// =====================================================================
// 用例组 5：ObjectStoreScan——引用存在性（acceptance 3）
// =====================================================================

/**
 * acceptance 3／§4.6：引用与磁盘一致的 healthy 状态——三桶全空。
 */
TEST_F(ObjectStoreTest, Scan_AllReferencedPresent_NoFindings)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = store.publishObject(oid, "referenced");

    const ObjectScanReport scan = store.scanObjects({ObjectKey{oid, cv}}, true);
    EXPECT_TRUE(scan.missingReferenced.empty());
    EXPECT_TRUE(scan.corruptReferenced.empty());
    EXPECT_TRUE(scan.danglingOnDisk.empty());
    EXPECT_TRUE(scan.malformedEntries.empty());
}

/**
 * acceptance 3／§4.6"有引用无对象＝store-corrupt"：引用指向未发布对象 →
 * missingReferenced 报告（调用方映射 store-corrupt 稳定码）；扫描不抛。
 */
TEST_F(ObjectStoreTest, Scan_MissingReferenced_ReportedNoThrow)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    store.publishObject(oid, "healthy-one");

    const ObjectId ghost = ObjectId::generate();
    const ContentVersion ghostCv = directCoreDigest("missing-content");
    const ObjectScanReport scan =
        store.scanObjects({ObjectKey{oid, directCoreDigest("healthy-one")},
                           ObjectKey{ghost, ghostCv}},
                          false);
    EXPECT_TRUE(scan.corruptReferenced.empty());
    ASSERT_EQ(1u, scan.missingReferenced.size());
    EXPECT_TRUE((scan.missingReferenced[0] == ObjectKey{ghost, ghostCv}));
}

/**
 * acceptance 3／§4.6"有对象无引用＝悬挂对象开发诊断"：磁盘上存在而引用
 * 集不含的对象 → danglingOnDisk ＋ sink 开发诊断；且**不删除**文件
 * （GC 明确不做——只读报告）。
 */
TEST_F(ObjectStoreTest, Scan_DanglingObject_ReportedAsDevDiagnostic_NotDeleted)
{
    CapturingSink sink;
    auto store = makeStore(objstore::ObjectStore::kDefaultCacheBudgetBytes,
                           &sink);
    const ObjectId oidA = ObjectId::generate();
    const ObjectId oidB = ObjectId::generate();
    const ContentVersion cvA = store.publishObject(oidA, "referenced-obj");
    const ContentVersion cvB = store.publishObject(oidB, "dangling-obj");
    const ObjectKey keyB{oidB, cvB};

    const ObjectScanReport scan = store.scanObjects({ObjectKey{oidA, cvA}}, false);
    ASSERT_EQ(1u, scan.danglingOnDisk.size());
    EXPECT_TRUE(scan.danglingOnDisk[0] == keyB);

    // 开发诊断面：channel＝project/object-store、消息带悬挂计数。
    bool found = false;
    for (const auto& call : sink.devCalls()) {
        if (call.first == "project/object-store"
            && call.second.find("dangling-objects count=1") != std::string::npos) {
            found = true;
        }
    }
    EXPECT_TRUE(found) << "悬挂对象应产生开发诊断（project/object-store）";

    // 只读报告：悬挂文件仍在盘上（删除决策永不属于对象库/扫描）。
    EXPECT_TRUE(fs::exists(objectFile(oidB, cvB)));
}

/**
 * acceptance 3／§7.4 ④⑤两档：全量档（verifyDigest=true）检出被篡改的
 * 引用对象；存在性档（false）不查摘要（廉价计数场景）。
 */
TEST_F(ObjectStoreTest, Scan_CorruptReferenced_FullAndExistenceModes)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = store.publishObject(oid, "will-be-tampered");
    ASSERT_TRUE(writeRaw(objectFile(oid, cv), "tampered-bytes-instead"));

    const ObjectScanReport full = store.scanObjects({ObjectKey{oid, cv}}, true);
    ASSERT_EQ(1u, full.corruptReferenced.size());
    EXPECT_TRUE((full.corruptReferenced[0] == ObjectKey{oid, cv}));
    EXPECT_TRUE(full.missingReferenced.empty());

    const ObjectScanReport existence =
        store.scanObjects({ObjectKey{oid, cv}}, false);
    EXPECT_TRUE(existence.corruptReferenced.empty())
        << "存在性档不查摘要——文件在即不算损坏";
    EXPECT_TRUE(existence.missingReferenced.empty());
}

/**
 * acceptance 3（综合）／§4.6：缺失＋损坏＋悬挂＋不合规四类发现一次扫描
 * 全部如实分桶；报告经确定性排序（两次扫描逐键相等——可复现）。
 */
TEST_F(ObjectStoreTest, Scan_MixedFindings_AllBuckets_DeterministicReport)
{
    auto store = makeStore();
    // 健康＋将被篡改的对象（同 oid 两版本——制造同目录多文件形态）。
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cvHealthy = store.publishObject(oid, "healthy");
    const ContentVersion cvDamaged = store.publishObject(oid, "damaged");
    ASSERT_TRUE(writeRaw(objectFile(oid, cvDamaged), "corrupted-now"));
    // 悬挂对象（不在引用集）。
    const ObjectId danglingOid = ObjectId::generate();
    const ContentVersion danglingCv =
        store.publishObject(danglingOid, "unreferenced");
    const ObjectKey danglingKey{danglingOid, danglingCv};
    // 缺失引用（从未发布）。
    const ObjectId missingOid = ObjectId::generate();
    const ObjectKey missingKey{missingOid, directCoreDigest("no-such-bytes")};
    // 不合规条目：对象区根下的裸文件＋合法 oid 目录内的非 hex 文件名。
    ASSERT_TRUE(writeRaw(objectsDir() / "stray-file.txt", "junk"));
    const fs::path rogueDir = objectsDir() / oid.toCanonical();
    ASSERT_TRUE(writeRaw(rogueDir / "zzz-not-hex.txt", "junk"));

    // 引用集乱序装配（healthy/damaged/missing）——报告顺序必须与装配序无关。
    const std::vector<ObjectKey> refs{
        missingKey, ObjectKey{oid, cvDamaged}, ObjectKey{oid, cvHealthy}};

    const ObjectScanReport scan = store.scanObjects(refs, true);

    // 分桶断言（成员判定与桶尺寸）。
    ASSERT_EQ(1u, scan.missingReferenced.size());
    EXPECT_TRUE(scan.missingReferenced[0] == missingKey);
    ASSERT_EQ(1u, scan.corruptReferenced.size());
    EXPECT_TRUE((scan.corruptReferenced[0] == ObjectKey{oid, cvDamaged}));
    ASSERT_EQ(1u, scan.danglingOnDisk.size());
    EXPECT_TRUE(scan.danglingOnDisk[0] == danglingKey);
    ASSERT_EQ(2u, scan.malformedEntries.size());
    // 排序后："obj-…/zzz-not-hex.txt"（'o'）先于 "stray-file.txt"（'s'）。
    EXPECT_EQ(oid.toCanonical() + "/zzz-not-hex.txt", scan.malformedEntries[0]);
    EXPECT_EQ("stray-file.txt", scan.malformedEntries[1]);

    // 确定性：同一磁盘状态两次扫描报告逐键相等（排序消除枚举顺序噪声）。
    const ObjectScanReport again = store.scanObjects(refs, true);
    EXPECT_TRUE(scan.missingReferenced == again.missingReferenced);
    EXPECT_TRUE(scan.corruptReferenced == again.corruptReferenced);
    EXPECT_TRUE(scan.danglingOnDisk == again.danglingOnDisk);
    EXPECT_TRUE(scan.malformedEntries == again.malformedEntries);
}

/**
 * acceptance 3（边界）：空库（无引用无对象）扫描＝全空报告不报错；
 * 引用表混入全零键＝调用方违约 fail-fast。
 */
TEST_F(ObjectStoreTest, Scan_EmptyStore_NoFindings_InvalidRefFailsFast)
{
    auto store = makeStore();
    const ObjectScanReport scan = store.scanObjects({}, true);
    EXPECT_TRUE(scan.missingReferenced.empty());
    EXPECT_TRUE(scan.corruptReferenced.empty());
    EXPECT_TRUE(scan.danglingOnDisk.empty());
    EXPECT_TRUE(scan.malformedEntries.empty());

    EXPECT_THROW(store.scanObjects({ObjectKey{ObjectId{}, ContentVersion{}}},
                                   false),
                 std::invalid_argument);
}

// =====================================================================
// 用例组 6：ObjectStoreCoreContract——P-PR-1/CR-02 处置自证（acceptance 4）
// =====================================================================

/**
 * acceptance 4（CR-02）／§4.6/§4.8：编址全链路只经 core——对多组负载，
 * 发布返回值、磁盘文件名解析回读（core ContentVersion::tryFromCanonical）、
 * core ContentDigester 直算三方逐值相等；目录名经 core ObjectId 解析回读
 * 相等。任何本地第二哈希/第二格式化都会打破三方相等。
 */
TEST_F(ObjectStoreTest, CoreContract_AddressingPath_SingleHashViaCore_CR02_PPR1)
{
    auto store = makeStore();
    const std::vector<std::string> payloads{
        "",
        "a",
        "0123456789abcdef",
        std::string(64 * 1024, '\xAB')  // 跨多分块的大负载（SHA 分块路径）
    };
    for (const std::string& payload : payloads) {
        const ObjectId oid = ObjectId::generate();
        const ContentVersion cv = store.publishObject(oid, payload);

        // 臂一：core 直算。
        EXPECT_TRUE(cv == directCoreDigest(payload))
            << "负载长度 " << payload.size();
        // 臂二：磁盘文件名经 core 解析回读＝同一内容版本。
        const std::string fileName = cvFileName(cv);
        const auto parsed = ContentVersion::tryFromCanonical("cv-" + fileName);
        ASSERT_TRUE(parsed.has_value()) << "文件名须为合法 cv 规范文本";
        EXPECT_TRUE(*parsed == cv);
        // 臂三：目录名经 core 解析回读＝同一对象身份。
        const fs::path dir = objectsDir() / oid.toCanonical();
        const auto parsedOid =
            ObjectId::tryFromCanonical(dir.filename().string());
        ASSERT_TRUE(parsedOid.has_value());
        EXPECT_TRUE(*parsedOid == oid);
        // 编址自洽：解析回读的键能直接定位到同一文件。
        EXPECT_TRUE(fs::exists(objectsDir() / parsedOid->toCanonical()
                               / cvFileName(*parsed)));
    }
}

/**
 * acceptance 4（P-PR-1）／core.md v0.1 基线：core 严格解析契约在对象库
 * 路径上的消费基线——大写 hex/缺 tag/错 tag 的编址文本不被接受（防本地
 * 宽松解析旁路 core 契约）。
 */
TEST_F(ObjectStoreTest, CoreContract_StrictCanonicalParse_ConsumedAsBaseline)
{
    auto store = makeStore();
    const ObjectId oid = ObjectId::generate();
    const ContentVersion cv = store.publishObject(oid, "strictness");

    // 大写形式拒绝（core 规范文本小写口径——P-PR-1 消费基线的一部分）。
    std::string upper = cvFileName(cv);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(::toupper(c)); });
    EXPECT_FALSE(ContentVersion::tryFromCanonical("cv-" + upper).has_value());
    // 错 tag 拒绝（cid- 是内容身份的 tag，不是内容版本——强类型纪律）。
    EXPECT_FALSE(ContentVersion::tryFromCanonical("cid-" + cvFileName(cv))
                     .has_value());
    // 目录名侧同判（错 tag 的 ObjectId 文本不被接受）。
    EXPECT_FALSE(ObjectId::tryFromCanonical("rev-" + oid.toCanonical().substr(4))
                     .has_value());
}
