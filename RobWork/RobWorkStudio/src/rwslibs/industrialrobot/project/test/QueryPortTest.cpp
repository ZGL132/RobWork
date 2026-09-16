/**
 * @file   QueryPortTest.cpp
 * @brief  查询端口用例组——并发只读与值拷贝不可变（acceptance 1）、闭包
 *         外不可见/try 轨/context-closed 拒绝/只读实例查询（acceptance 2）、
 *         ②端口六类视图齐备＋ObjectCache 预算生效（acceptance 3）、
 *         P-PR-1 core 身份基线消费自证（acceptance 4）。
 *
 * 设计依据：
 *   - units/project.md §5.2（查询端口契约表——各方法前置/后置/错误）、
 *     §4.5（闭包规则/INV-M3 权威纪律——metadataAt 历史快照与 currentMetadata
 *     权威的区分）、§4.6（tryObject 引用存在性/LRU 预算）、§4.7（线程与
 *     生命周期契约——并发只读/context-closed/只读实例可查询〔PM-07〕）、
 *     §4.4.5/§4.4.7/§4.1（草稿文档/运行清单/目录命名与"仅 finalize 运行"）、
 *     §6.3（hasUnresolvedPayload）、§15.3 P-PR-1（core v0.1 基线消费）；
 *   - 需求 ARC-02（②端口所有者——消费面无私有互访）、CON-01（从不可变
 *     快照运行——值拷贝视图承载）、PA-2/PA-3（不可变历史/读写无共享
 *     可变状态）；PM-07（只读打开可查看）；NFR-COR-02（清单确定性排序）；
 *   - 任务契约 tasks/foundation/PRJ-T09.json acceptance 1～4（逐条对应
 *     具名用例，见各用例锚定注释）。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. 提交通道＝ProjectStoreImpl::executeCommit（与 ProjectStoreTest 同款
 *      ——T10 命令服务挂载前的写权威通道）；分支/父修订自磁盘 HEAD 读取
 *      （地面事实）。
 *   2. 草稿/运行的前置数据经 codec::dump（草稿文档）与手写 canonical
 *      JSON（运行清单——本任务只落 parse，样本即磁盘事实形态）直接落盘，
 *      不经被测端口写入（前置不依赖被测正确性）。
 *   3. 闭包外修订的植入复刻 ProjectStoreTest::Tx9_UncommittedRevision_
 *      InvisibleAfterReopen 形态（合法格式清单＋悬挂元数据引用——F5
 *      发布中途失败形态），重开后经查询端口断言不可见（对 ProjectStore
 *      Test 的索引级断言升级为端口级——本任务的落位面）。
 *   4. 并发用例以"结果一致性断言"承载线程安全证据（head 单调不回退、
 *      对象字节恒等、全方法无异常完成）；数据竞争的静态检测归 TSAN/
 *      人工评审（本测试环境无 TSAN——如实登记，不用"跑过＝无竞争"
 *      的超诺表述）。工作线程**不直接调用 gtest 断言**（FAIL/ADD_FAILURE
 *      非线程安全——失败经互斥保护的消息表汇合到主线程断言）。
 */

#include <sdurws/ird/project/PersistenceFormat.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "Codec.hpp"
#include "ObjectStore.hpp"  // objstore::ObjectKey——缓存预算用例的 LRU 观测面
                            // （ObjectStore::cacheStats/cacheLruOrder 同款
                            // 私有头先例：ProjectStoreImpl.hpp——R-2 禁令
                            // 是跨单元暴露，同单元测试消费既定形态）
#include "ProjectStoreImpl.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using sdurws::ird::core::BranchId;
using sdurws::ird::core::ContentVersion;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::project::BranchTip;
using sdurws::ird::project::DraftDocument;
using sdurws::ird::project::DraftInfo;
using sdurws::ird::project::DraftOrigin;
using sdurws::ird::project::HeadRecord;
using sdurws::ird::project::IDiagnosticsSink;
using sdurws::ird::project::InverseCommand;
using sdurws::ird::project::ObjectRef;
using sdurws::ird::project::ObjectRefPair;
using sdurws::ird::project::OpenMode;
using sdurws::ird::project::OpenStoreRequest;
using sdurws::ird::project::ProjectMetadataView;
using sdurws::ird::project::ProjectStoreFactory;
using sdurws::ird::project::ProjectStoreImpl;
using sdurws::ird::project::RevisionManifest;
using sdurws::ird::project::RevisionView;
using sdurws::ird::project::RunInfo;
using sdurws::ird::project::StoreError;
using sdurws::ird::project::StoreErrorCode;
using sdurws::ird::project::codec::contentVersionOf;
using sdurws::ird::project::revindex::TipUpdate;
using sdurws::ird::project::tx::CommitPlan;
using sdurws::ird::project::tx::CommitResult;
using sdurws::ird::project::tx::PlannedObject;
namespace pd = sdurws::ird::project;
// 对象库命名空间别名（缓存预算用例的 LRU 观测面——嵌套命名空间在全局
// 测试作用域的解析路径）。
namespace objstore = sdurws::ird::project::objstore;

namespace {

// ---------------------------------------------------------------------
// fake：诊断 sink（开发诊断捕获——tryObject 降级/清单跳过的可见性面）
// ---------------------------------------------------------------------

/// 捕获型 sink（ProjectStoreTest::CapturingSink 同款形态）。
class CapturingSink : public IDiagnosticsSink {
public:
    std::vector<sdurws::ird::core::DiagnosticRecord> reports;
    std::vector<std::pair<std::string, std::string>> devs;

    void report(const sdurws::ird::core::DiagnosticRecord& record) override
    {
        reports.push_back(record);
    }

    void reportDev(const std::string& channel, const std::string& message) override
    {
        devs.emplace_back(channel, message);
    }

    bool devContains(const std::string& needle) const
    {
        for (const auto& d : devs) {
            if (d.second.find(needle) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------------
// 磁盘与断言辅助
// ---------------------------------------------------------------------

/// 二进制整读（前置数据核对用——不经被测代码）。
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

/// 裸写前置文件（前置数据不经被测代码——保证前置不依赖被测正确性）。
bool writeRaw(const fs::path& file, const std::string& bytes)
{
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        ADD_FAILURE() << "建前置目录失败: " << ec.message();
        return false;
    }
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        ADD_FAILURE() << "无法写前置文件: " << file.string();
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// 从磁盘 HEAD 读取提交指针（地面事实——提交计划与断言的锚点）。
HeadRecord readHeadFromDisk(const fs::path& projectDir)
{
    return pd::codec::parseHeadRecord(readAll(projectDir / "HEAD"));
}

/// 断言动作抛出指定稳定码的 StoreError 并返回异常（ProjectStoreTest 同款）。
template <class Fn>
StoreError expectStoreError(StoreErrorCode code, Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 StoreError(" << static_cast<int>(code)
                      << ") 未抛出";
    } catch (const StoreError& e) {
        EXPECT_EQ(e.code(), code) << "稳定码不符: " << e.what();
        return e;
    }
    throw std::runtime_error("expectStoreError: action did not throw");
}

/// 断言动作抛出 std::invalid_argument（调用方违约 fail-fast 面）。
template <class Fn>
void expectInvalidArgument(Fn&& action)
{
    try {
        action();
        ADD_FAILURE() << "期望 std::invalid_argument 未抛出";
    } catch (const std::invalid_argument&) {
        // 预期路径——调用方契约违约的 fail-fast 表达。
    }
}

/// 一次提交的可观测信息（对象字节/编址——对象读取断言的数据源）。
struct CommittedInfo {
    CommitResult result;
    ObjectId oid{};
    ContentVersion cv{};
    std::vector<std::uint8_t> payload;
};

/// 手写运行清单 canonical JSON（§4.4.7 字段级契约的磁盘事实形态；
/// 本任务只落 parse——写侧编码随 PRJ-T14，前置样本手写即"磁盘事实"）。
std::string runManifestJson(const std::string& project, const std::string& branch,
                            const std::string& revision, const std::string& run,
                            const std::string& relPath, std::uint64_t sizeBytes)
{
    // canonical 固定字段序＝§4.4.7 表列序；manifestDigest 为 64 小写 hex
    // 形态值（内容真实性归归档写路径校验——本样本只做合法形态前置）。
    return "{\"taskIdentity\":{\"project\":\"" + project + "\",\"branch\":\""
           + branch + "\",\"revision\":\"" + revision + "\",\"run\":\"" + run
           + "\",\"attempt\":\"att-1\"},\"items\":[{\"relPath\":\"" + relPath
           + "\",\"sha256\":\"" + std::string(64, 'a') + "\",\"sizeBytes\":"
           + std::to_string(sizeBytes) + "}],\"runKind\":\"evaluation\","
             "\"evaluationKey\":\"eval-key-1\",\"finalizedAtUtc\":"
             "\"2026-09-16T03:00:00Z\",\"manifestDigest\":\""
           + std::string(64, 'b') + "\"}";
}

}  // namespace

// ---------------------------------------------------------------------
// 用例组：QueryPort（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

/**
 * 测试夹具：套件级临时总根；各用例独立项目目录（进程级隔离）。种子
 * 项目一律经 createNew 产出（PM-01 组装路径），特殊残留态的用例在
 * close 后对磁盘做定向改造（口径登记 2）。
 */
class QueryPortTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_query_test"
                 / std::to_string(::GetCurrentProcessId());
        ASSERT_FALSE(ec);
        fs::create_directories(s_base, ec);
        if (ec) {
            std::cerr << "无法创建测试根目录: " << s_base.string() << " ("
                      << ec.message() << ")\n";
            std::exit(2);
        }
    }

    static void TearDownTestSuite()
    {
        // 总根清理：失败保留现场（"TempDir 失败保留"惯例），不静默掩盖。
        std::error_code ec;
        fs::remove_all(s_base, ec);
        if (ec) {
            std::cerr << "警告：测试根目录清理失败（保留现场）: "
                      << s_base.string() << " (" << ec.message() << ")\n";
        }
    }

    void SetUp() override
    {
        std::error_code ec;
        m_dir = baseDir() / ("case" + std::to_string(++s_caseCounter))
                / "proj.rwdesign";
        ASSERT_FALSE(ec);
    }

    /// 新建项目并返回打开结果（注入捕获 sink——开发诊断断言面）。
    pd::OpenStoreResult createNew(std::string_view name = "Query Test Project")
    {
        return ProjectStoreFactory::createNew(m_dir, name, nullptr,
                                              m_sink.get());
    }

    /// 经内部写通道提交一次合法修订（分支/父修订自磁盘 HEAD 读取——
    /// 地面事实）。withInverse=true 时携带逆命令表达（视图 inverse 断言）。
    CommittedInfo commitOne(pd::ProjectStore& store, bool withInverse = true,
                            std::size_t payloadBytes = 32)
    {
        const HeadRecord head = readHeadFromDisk(m_dir);
        CommitPlan plan;
        plan.revisionId = RevisionId::generate();
        plan.branchId = head.branchId;
        plan.parentRevisionId = head.revisionId;
        plan.committedAtUtc = "2026-09-16T04:00:00Z";
        PlannedObject obj;
        obj.oid = ObjectId::generate();
        obj.objectTypeToken = "RobotDesign";
        obj.payload.assign(payloadBytes, 'Q');
        plan.newObjects.push_back(obj);
        TipUpdate tip;
        tip.branchId = head.branchId;
        tip.newTip = plan.revisionId;
        plan.metadataDelta.tipUpdate = tip;
        plan.command.commandType = "add-mass-point";  // ^[a-z0-9-]{3,64}
        plan.command.payloadFormatVersion = 1;
        plan.command.payloadCanonical = "{\"point\":1}";
        plan.command.summary = "query test commit";
        if (withInverse) {
            // 可逆命令的逆命令表达（§6.9——视图 inverse 字段的数据源；
            // 可逆性是处理器声明的事实，project 透传）。
            plan.command.inverse = InverseCommand{"remove-mass-point", 1,
                                                  "{\"point\":1}"};
        }
        auto* impl = dynamic_cast<ProjectStoreImpl*>(&store);
        EXPECT_NE(impl, nullptr);

        CommittedInfo info;
        info.result = impl->executeCommit(plan);
        info.oid = obj.oid;
        info.cv = contentVersionOf(std::string_view(
            reinterpret_cast<const char*>(obj.payload.data()),
            obj.payload.size()));
        info.payload = obj.payload;
        return info;
    }

    /// 以指定预算重开当前项目目录（ObjectCache 预算注入口——§5.1
    /// OpenStoreRequest.cacheBudget；预算传导路径＝工厂→ObjectStore——
    /// T08 已落，本用例经查询端口消费面断言生效）。
    pd::OpenStoreResult reopenWithBudget(std::size_t budgetBytes)
    {
        OpenStoreRequest request;
        request.path = m_dir;
        request.diagnostics = m_sink.get();
        request.cacheBudget.budgetBytes = budgetBytes;
        return ProjectStoreFactory::open(request);
    }

    static fs::path baseDir() { return s_base; }

    fs::path m_dir;  ///< 用例专属项目目录
    std::shared_ptr<CapturingSink> m_sink{std::make_shared<CapturingSink>()};

    static fs::path s_base;
    static int s_caseCounter;
};

fs::path QueryPortTest::s_base;
int QueryPortTest::s_caseCounter = 0;

// =====================================================================
// acceptance 3（②端口所有者语义——视图齐备）：修订视图逐字段
// =====================================================================

/**
 * 锚定：§5.2 RevisionView 字段级形态／§4.4.2 清单引用集／acceptance 3
 * （"只读快照/对象/修订/分支/草稿/结果清单视图齐备"——修订半区）。
 *
 * 前置：createNew（初始修订 r0, seq=1）后提交一次（r1, seq=2，含逆命令）。
 * 操作：head()／revision(r1)／tryRevision(r1)。
 * 预期：head==r1（与磁盘 HEAD 一致）；视图字段逐一与提交事实一致
 * （seq/parent/branch/引用集含元数据对象〔token=ProjectMetadata〕/
 * metadataRef 完整形态/命令摘要/inverse/hasUnresolvedPayload=false）；
 * revision 与 tryRevision 同值；r0 历史可查（PA-2）。
 */
TEST_F(QueryPortTest, RevisionView_HeadAndRevision_FieldsAssembled)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const HeadRecord r0Head = readHeadFromDisk(m_dir);
    const CommittedInfo commit = commitOne(*opened.store);

    pd::IProjectQueryPort& port = opened.store->query();
    const RevisionView head = port.head();
    // HEAD 锚点：与磁盘 HEAD 一致（"重读 HEAD 文件"语义的观测面）。
    EXPECT_TRUE(head.id == commit.result.revisionId);
    EXPECT_TRUE(head.id == readHeadFromDisk(m_dir).revisionId);
    EXPECT_EQ(head.seq, 2u);  // r0=1 → 本次提交=2（O-15 单调序号）
    ASSERT_TRUE(head.parent.has_value());
    EXPECT_TRUE(*head.parent == r0Head.revisionId);
    EXPECT_TRUE(head.branch == r0Head.branchId);
    EXPECT_EQ(head.commandSummary, "query test commit");
    // 逆命令表达（§6.9）：可逆命令的视图携带 inverse。
    ASSERT_TRUE(head.inverse.has_value());
    EXPECT_EQ(head.inverse->commandType, "remove-mass-point");
    EXPECT_EQ(head.inverse->payloadFormatVersion, 1u);
    // hasUnresolvedPayload 真判定（§6.3 末行；T10 注入注册表判据——本用
    // 例的提交 token "remove-mass-point" 未在该上下文注册 → 修订可读、
    // payload 不解析＝true。T09 落位时的 false 预期对应"注册表未装配、
    // 判据为空＝无法判定"阶段，随 T10 注入兑现为真判定，见 QueryPort.hpp
    // 增量落位说明 3 的预留口径）。
    EXPECT_TRUE(head.hasUnresolvedPayload);

    // 引用集：完整非增量（§4.4.2 ≥1）——含本次域对象与元数据对象。
    ASSERT_EQ(head.objectRefs.size(), 2u);
    const ObjectRef* domainRef = nullptr;
    const ObjectRef* metadataRef = nullptr;
    for (const ObjectRef& ref : head.objectRefs) {
        if (ref.objectId == commit.oid) {
            domainRef = &ref;
        } else if (ref.objectTypeToken == "ProjectMetadata") {
            metadataRef = &ref;
        }
    }
    ASSERT_NE(domainRef, nullptr);
    ASSERT_NE(metadataRef, nullptr);
    // 域对象引用含内容版本与负载摘要（digest256＝cv 规范文本剥离 tag——
    // §4.4.2 表；口径同 ProjectStoreTest 植入样本）。
    EXPECT_TRUE(domainRef->contentVersion == commit.cv);
    EXPECT_EQ(domainRef->digest256, commit.cv.toCanonical().substr(3));
    // metadataRef＝清单 metadataRef 的完整引用形态（视图装配契约——
    // QueryPort.hpp RevisionView.metadataRef 注释）。局部变量承载比较值
    // （gtest 宏参数不可含带逗号的花括号初始化列表——预处理器拆参）。
    const ObjectRefPair metadataKey{head.metadataRef.objectId,
                                    head.metadataRef.contentVersion};
    EXPECT_TRUE(head.metadataRef == *metadataRef);
    EXPECT_TRUE(metadataKey == commit.result.metadataRef);

    // revision 强语义与 tryRevision try 轨同值（同一数据源）。
    const RevisionView strong = port.revision(commit.result.revisionId);
    EXPECT_TRUE(strong == head);
    const auto tried = port.tryRevision(commit.result.revisionId);
    ASSERT_TRUE(tried.has_value());
    EXPECT_TRUE(*tried == head);
    // 初始修订 r0 仍可查（PA-2——历史只增不改）。
    const auto r0 = port.tryRevision(r0Head.revisionId);
    ASSERT_TRUE(r0.has_value());
    EXPECT_EQ(r0->seq, 1u);
    EXPECT_FALSE(r0->parent.has_value());  // 首修订无父（§4.5）
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 3：权威元数据/分支视图（INV-M3）＋历史浏览
// =====================================================================

/**
 * 锚定：§5.2 currentMetadata/branchTips/metadataAt 行／§4.5.1 走查结论②
 * （INV-M3：分支 tip 一律取自 HEAD 引用权威版本）／acceptance 3
 * （快照/分支视图齐备）／R2 历史浏览（PM-12-S1）。
 *
 * 前置：createNew 后提交两次（r1、r2）。
 * 操作：currentMetadata／branchTips／metadataAt(r1)／metadataAt(不存在)。
 * 预期：currentMetadata.ref==HEAD 引用键、分支表 tip==r2；metadataAt(r1)
 * 返回**历史快照**（tip==r1——该修订时点状态，非权威）；branchTips 与
 * 权威一致（main 分支 label/base/tip）；不存在的修订 → nullopt。
 */
TEST_F(QueryPortTest, MetadataAndBranchViews_AuthoritativeDiscipline)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const HeadRecord r0Head = readHeadFromDisk(m_dir);
    const CommittedInfo r1 = commitOne(*opened.store);
    const CommittedInfo r2 = commitOne(*opened.store);

    pd::IProjectQueryPort& port = opened.store->query();

    // 权威视图：ref 与 record 同源（最后一次提交的权威元数据——ref 与
    // record 由同一 writer 锁内快照拷出，无撕裂对）。
    const ProjectMetadataView authoritative = port.currentMetadata();
    EXPECT_TRUE(authoritative.ref == r2.result.metadataRef);
    ASSERT_EQ(authoritative.record.branches.size(), 1u);
    EXPECT_TRUE(authoritative.record.branches[0].tipRevisionId
                == r2.result.revisionId)
        << "权威分支表 tip 必须是 HEAD 引用版本的（INV-M3）";

    // 分支视图：自权威投影（label 创建时一次写入——P-PR-8）。
    const std::vector<BranchTip> tips = port.branchTips();
    ASSERT_EQ(tips.size(), 1u);
    EXPECT_TRUE(tips[0].id == r0Head.branchId);
    EXPECT_EQ(tips[0].label, "main");
    EXPECT_TRUE(tips[0].base == r0Head.revisionId);  // base 创建时记录后不变
    EXPECT_TRUE(tips[0].tip == r2.result.revisionId);

    // 历史浏览：r1 时点的元数据快照（其分支表 tip 冻结在 r1——不是权威；
    // 正是 INV-M3 禁止拿来做当前查询的那份"旧分支表"——展示语义）。
    const auto historical = port.metadataAt(r1.result.revisionId);
    ASSERT_TRUE(historical.has_value());
    EXPECT_TRUE(historical->ref == r1.result.metadataRef);
    ASSERT_EQ(historical->record.branches.size(), 1u);
    EXPECT_TRUE(historical->record.branches[0].tipRevisionId
                == r1.result.revisionId);
    EXPECT_FALSE(historical->ref == authoritative.ref);

    // 闭包外/不存在的修订 → nullopt（可见性纪律同 tryRevision）。
    EXPECT_FALSE(port.metadataAt(RevisionId::generate()).has_value());
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

/**
 * 锚定：§5.2 branchHistory 行（"沿 parent 链"；表"未知分支抛"）／§4.5
 * （分支 tip 维度 parent——各分支历史只含本分支提交）／acceptance 3。
 *
 * 前置：main 上提交三次（链 r0←r1←r2←r3）。
 * 操作：branchHistory(main,10)／branchHistory(main,2)／branchHistory(main,0)
 * ／branchHistory(未知分支,10)。
 * 预期：全链 4 条新→旧（seq 严格递减、parent 逐级相扣）；maxCount=2
 * 截取最新两条；maxCount=0 空表（字面语义）；未知分支 invalid_argument。
 */
TEST_F(QueryPortTest, BranchHistory_WalksParentChainWithLimit)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const HeadRecord r0Head = readHeadFromDisk(m_dir);
    const CommittedInfo r1 = commitOne(*opened.store);
    const CommittedInfo r2 = commitOne(*opened.store);
    const CommittedInfo r3 = commitOne(*opened.store);

    pd::IProjectQueryPort& port = opened.store->query();

    // 全链：新→旧（tip 在前），条数＝提交总数＋初始修订。
    const std::vector<RevisionView> full = port.branchHistory(r0Head.branchId, 10);
    ASSERT_EQ(full.size(), 4u);
    EXPECT_TRUE(full[0].id == r3.result.revisionId);
    EXPECT_TRUE(full[1].id == r2.result.revisionId);
    EXPECT_TRUE(full[2].id == r1.result.revisionId);
    EXPECT_TRUE(full[3].id == r0Head.revisionId);
    for (std::size_t i = 1; i < full.size(); ++i) {
        EXPECT_LT(full[i].seq, full[i - 1].seq) << "沿链 seq 严格递减（新→旧）";
        ASSERT_TRUE(full[i - 1].parent.has_value());
        EXPECT_TRUE(*full[i - 1].parent == full[i].id) << "parent 链逐级相扣";
    }

    // maxCount 截断（最新优先）与平凡请求（maxCount=0 → 空表——字面
    // 语义"最多 0 条"，实现口径）。
    const std::vector<RevisionView> limited = port.branchHistory(r0Head.branchId, 2);
    ASSERT_EQ(limited.size(), 2u);
    EXPECT_TRUE(limited[0].id == r3.result.revisionId);
    EXPECT_TRUE(limited[1].id == r2.result.revisionId);
    const std::vector<RevisionView> none = port.branchHistory(r0Head.branchId, 0);
    EXPECT_TRUE(none.empty());

    // 未知分支＝调用方违约 → invalid_argument（§5.2 表"未知分支抛"；
    // 实现口径：封闭集无对应稳定码，不私扩——fail-fast）。
    expectInvalidArgument([&] {
        (void)port.branchHistory(BranchId::generate(), 10);
    });
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 3：对象读取二分（try 轨/强轨）＋ CON-01 快照读取
// =====================================================================

/**
 * 锚定：§5.2 tryObject/object 行＋错误表（"引用缺失＝store-corrupt"）／
 * §4.6 引用存在性语义／CON-01（§2.2 行"快照读取"——对象字节与磁盘
 * 一致）／acceptance 3（对象视图）。
 *
 * 前置：提交一次（对象 payload 已知）。
 * 操作：object(已引用)/tryObject(已引用)/tryObject(未发布)/object(未发布)
 * ／object(全零身份)。
 * 预期：已引用读取字节与提交负载全等（首次校验通过；缓存命中路径幂等）；
 * 未引用 try→nullopt 不抛、强轨→StoreCorrupt；全零身份强轨 fail-fast。
 */
TEST_F(QueryPortTest, ObjectRead_TryTrackAndStrongTrack)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo commit = commitOne(*opened.store);

    pd::IProjectQueryPort& port = opened.store->query();

    // 已引用对象：字节与写入一致（内容寻址＋首读校验——§4.6）。
    const std::vector<std::uint8_t> bytes = port.object(commit.oid, commit.cv);
    EXPECT_EQ(bytes, commit.payload);
    // try 轨对已引用对象同值；幂等读取（缓存命中路径）一致。
    const auto tried = port.tryObject(commit.oid, commit.cv);
    ASSERT_TRUE(tried.has_value());
    EXPECT_EQ(*tried, commit.payload);
    EXPECT_EQ(port.object(commit.oid, commit.cv), commit.payload);

    // 未发布引用：try→nullopt 不抛（§4.6"引用不存在于清单的请求返回空
    // optional"）；强轨→store-corrupt（§5.2 错误表——断言存在而查不到
    // 即数据侧）。
    const ObjectId stranger = ObjectId::generate();
    EXPECT_FALSE(port.tryObject(stranger, commit.cv).has_value());
    expectStoreError(StoreErrorCode::StoreCorrupt,
                     [&] { (void)port.object(stranger, commit.cv); });

    // 全零身份：强轨调用方违约 fail-fast（try 轨 noexcept 面降级 nullopt
    // ——见 IProjectQueryPort::tryObject 契约注释）。
    expectInvalidArgument([&] { (void)port.object(ObjectId{}, commit.cv); });
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 3（具名验收点）：ObjectCache 预算参数生效（§5.2/§4.6）
// =====================================================================

/**
 * 锚定：§4.6（LRU ObjectCache 预算可配；"缓存互斥内部实现"）／§5.2
 * object 行（读经缓存）／acceptance 3（"ObjectCache 预算参数生效"）。
 *
 * 前置：默认预算建项目并提交 3 个 600 字节对象→close；以 1024 字节
 * 预算重开（OpenStoreRequest.cacheBudget→ObjectStore 传导——T08 已落）。
 * 操作：经查询端口按序读 3 个对象（读侧缓存推进——预算消费的真实路径）。
 * 预期：cacheStats.budgetBytes==1024（传导到位）；cacheStats.byteCount
 * ≤ 预算（超预算 LRU 逐出）；最后读取的键在缓存（最旧的被逐出——LRU
 * 可观测面）。
 */
TEST_F(QueryPortTest, ObjectCacheBudget_EnforcedThroughQueryPort)
{
    {
        // 第一步：默认预算建项目＋提交 3 个对象（发布路径不经预算——
        // 预算是读取侧缓存参数，§4.6"惰性加载"）。
        pd::OpenStoreResult seeded = createNew("Budget Project");
        ASSERT_NE(seeded.store, nullptr);
        (void)commitOne(*seeded.store, true, 600);
        (void)commitOne(*seeded.store, true, 600);
        (void)commitOne(*seeded.store, true, 600);
        EXPECT_EQ(seeded.store->requestClose(), 0u);
    }

    // 第二步：小预算重开（1024 字节——最多容纳 1 个 600 字节条目，
    // 预算语义"超预算逐出 LRU"的压力形态）。
    pd::OpenStoreResult reopened = reopenWithBudget(1024);
    ASSERT_NE(reopened.store, nullptr);
    auto* impl = dynamic_cast<ProjectStoreImpl*>(reopened.store.get());
    ASSERT_NE(impl, nullptr);

    // 收集三个已提交对象（自磁盘 HEAD 沿链解析——前置核对）。
    pd::IProjectQueryPort& port = reopened.store->query();
    std::vector<std::pair<ObjectId, ContentVersion>> refs;
    RevisionId cursor = readHeadFromDisk(m_dir).revisionId;
    for (int i = 0; i < 3; ++i) {
        const RevisionView view = port.revision(cursor);
        for (const ObjectRef& ref : view.objectRefs) {
            if (ref.objectTypeToken != "ProjectMetadata") {
                refs.emplace_back(ref.objectId, ref.contentVersion);
            }
        }
        ASSERT_TRUE(view.parent.has_value());
        cursor = *view.parent;
    }
    ASSERT_EQ(refs.size(), 3u);

    // 经查询端口按序强读（读侧缓存推进——预算消费的真实路径）。
    for (const auto& ref : refs) {
        const std::vector<std::uint8_t> bytes = port.object(ref.first, ref.second);
        EXPECT_EQ(bytes.size(), 600u);
    }

    // 观测面：预算传导到位＋约束生效（byteCount ≤ 预算）＋LRU 逐出。
    const auto stats = impl->objectStore().cacheStats();
    EXPECT_EQ(stats.budgetBytes, 1024u);
    EXPECT_LE(stats.byteCount, 1024u)
        << "缓存负载必须受打开请求的预算约束（§4.6）";
    const auto lru = impl->objectStore().cacheLruOrder();
    EXPECT_EQ(lru.size(), stats.entryCount);
    // 最后读取的键必须在缓存中（最旧的被逐出——LRU 语义的可观测面）。
    ASSERT_FALSE(lru.empty());
    const bool newestPresent = std::any_of(
        lru.begin(), lru.end(), [&](const objstore::ObjectKey& k) {
            return k.oid == refs.back().first && k.cv == refs.back().second;
        });
    EXPECT_TRUE(newestPresent);
    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 2：闭包外修订不可见（端口级落位面）
// =====================================================================

/**
 * 锚定：§4.5 闭包规则（闭包外＝未提交残留）／§7.3／PRJ-TX-9（"F5 后
 * 重开浏览'已发布未提交'修订→查询返回空"）／自审项 A-6（防误认已
 * 提交）／acceptance 2（"修订视图按 HEAD 闭包约束"）。
 *
 * 前置：提交一次后 close；对磁盘植入闭包外修订（合法格式清单＋悬挂
 * 元数据引用——F5 发布中途失败形态，同 ProjectStoreTest 口径 3）。
 * 操作：重开→查询端口 tryRevision/revision/metadataAt(闭包外修订)；
 * head()/branchHistory() 对照腿。
 * 预期：tryRevision/metadataAt 返回 nullopt；revision 抛 StoreCorrupt
 * （强语义不降级）；head() 仍指向闭包内 HEAD；branchHistory 不含闭包外
 * 修订（排除"全不可用"假阳性）。
 */
TEST_F(QueryPortTest, ClosureOutsideRevision_InvisibleToQueryPort)
{
    {
        pd::OpenStoreResult seeded = createNew("Closure Project");
        ASSERT_NE(seeded.store, nullptr);
        (void)commitOne(*seeded.store);
        EXPECT_EQ(seeded.store->requestClose(), 0u);
    }

    // 植入闭包外修订（内容已发布、HEAD 未指——合法格式清单，其元数据
    // 引用指向悬挂对象：闭包外内容完整性不被校验，§7.3 残留语义）。
    const RevisionId uncommitted = RevisionId::generate();
    const ObjectId danglingOid = ObjectId::generate();
    const std::string danglingBytes = "dangling payload";
    const ContentVersion danglingCv = contentVersionOf(danglingBytes);
    RevisionManifest leftover;
    leftover.revisionId = uncommitted;
    leftover.revisionSeq = 99;
    leftover.branchId = readHeadFromDisk(m_dir).branchId;
    leftover.committedAtUtc = "2026-09-16T03:00:00Z";
    leftover.metadataRef = ObjectRefPair{danglingOid, danglingCv};
    ObjectRef ref;
    ref.objectId = danglingOid;
    ref.contentVersion = danglingCv;
    ref.objectTypeToken = "ProjectMetadata";
    ref.digest256 = danglingCv.toCanonical().substr(3);
    leftover.objectRefs.push_back(ref);
    ASSERT_TRUE(writeRaw(m_dir / "revisions" / uncommitted.toCanonical()
                             / "manifest.json",
                         pd::codec::dump(leftover)));
    // 悬挂对象本体（磁盘有对象、无引用——⑤计数源）。
    ASSERT_TRUE(writeRaw(m_dir / "objects" / danglingOid.toCanonical()
                             / danglingCv.toCanonical().substr(3),
                         danglingBytes));

    OpenStoreRequest request;
    request.path = m_dir;
    request.diagnostics = m_sink.get();
    pd::OpenStoreResult reopened = ProjectStoreFactory::open(request);
    ASSERT_NE(reopened.store, nullptr);

    pd::IProjectQueryPort& port = reopened.store->query();
    // 闭包外修订：try 轨 nullopt（未提交残留不误认已提交——A-6）。
    EXPECT_FALSE(port.tryRevision(uncommitted).has_value());
    EXPECT_FALSE(port.metadataAt(uncommitted).has_value());
    // 强语义：查询方已断言存在——查不到即数据侧（store-corrupt）。
    expectStoreError(StoreErrorCode::StoreCorrupt,
                     [&] { (void)port.revision(uncommitted); });

    // 对照腿：闭包内 HEAD 完全可查（head 重读磁盘并命中会话索引）。
    const HeadRecord diskHead = readHeadFromDisk(m_dir);
    const RevisionView head = port.head();
    EXPECT_TRUE(head.id == diskHead.revisionId);
    // branchHistory 也不含闭包外修订（沿 parent 链只到闭包内成员）。
    const std::vector<RevisionView> history
        = port.branchHistory(diskHead.branchId, 100);
    for (const RevisionView& v : history) {
        EXPECT_FALSE(v.id == uncommitted)
            << "闭包外修订绝不可进入任何历史视图";
    }
    EXPECT_EQ(reopened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 2：try 轨（不存在/全零）不抛
// =====================================================================

/**
 * 锚定：§5.2 tryObject 行（"引用不存在返回空 optional 不抛"）／§4.6
 * try 轨语义／acceptance 2（"tryObject/tryRevision 引用不存在返回空
 * optional 不抛"）。
 *
 * 前置：健康项目（初始修订 r0）。
 * 操作：tryRevision(随机不存在)/tryRevision(全零)/metadataAt(全零)/
 * listRuns(全零)/tryObject(未发布)。
 * 预期：全部 nullopt/空表返回、不抛（try 轨对"没有"与"未设置"的统一
 * 表达）；健康面对照——r0 可查（排除"全空"假阳性）。
 */
TEST_F(QueryPortTest, TryTrack_MissingReturnsNullopt_NoThrow)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const HeadRecord head = readHeadFromDisk(m_dir);

    pd::IProjectQueryPort& port = opened.store->query();
    EXPECT_FALSE(port.tryRevision(RevisionId::generate()).has_value());
    EXPECT_FALSE(port.tryRevision(RevisionId{}).has_value());  // 全零＝未设置
    EXPECT_FALSE(port.metadataAt(RevisionId{}).has_value());
    EXPECT_TRUE(port.listRuns(RevisionId{}).empty());
    EXPECT_FALSE(port.tryObject(ObjectId::generate(), ContentVersion{}).has_value());
    // 健康面对照：r0 可查（排除"全空"假阳性）。
    EXPECT_TRUE(port.tryRevision(head.revisionId).has_value());
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 2（具名验收点）：context-closed 拒绝
// =====================================================================

/**
 * 锚定：§4.7（"上下文关闭后查询端口进入 context-closed 拒绝"）／
 * §5.2 错误表（head/currentMetadata 行 ContextClosed）／acceptance 2。
 *
 * 前置：健康项目（一次提交）后 requestClose（同步 Closed）。
 * 操作：逐个调用全部查询方法（tryObject 除外——noexcept 以 nullopt
 * 表达拒绝）。
 * 预期：抛轨方法全部 StoreError(ContextClosed)；tryRevision 亦抛
 * （关闭是比"不存在"更强的状态——拒绝态优先）；tryObject 返回 nullopt
 * ＋开发诊断（noexcept 契约面——实现口径 1，损坏/拒绝不静默）。
 */
TEST_F(QueryPortTest, ContextClosed_AllMethodsRejected)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo commit = commitOne(*opened.store);
    const BranchId branch = readHeadFromDisk(m_dir).branchId;
    EXPECT_EQ(opened.store->requestClose(), 0u);
    ASSERT_TRUE(opened.store->closed());

    pd::IProjectQueryPort& port = opened.store->query();  // 引用仍可获取
    expectStoreError(StoreErrorCode::ContextClosed, [&] { (void)port.head(); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.tryRevision(commit.result.revisionId); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.revision(commit.result.revisionId); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.currentMetadata(); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.metadataAt(commit.result.revisionId); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.branchTips(); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.branchHistory(branch, 10); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.object(commit.oid, commit.cv); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.listDrafts(branch); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.listRuns(commit.result.revisionId); });
    expectStoreError(StoreErrorCode::ContextClosed,
                     [&] { (void)port.runDir(RunId::generate()); });

    // noexcept 面：拒绝以 nullopt 表达（noexcept 契约——不可抛）；
    // 开发诊断保留可见性（实现口径 1——"tryObject " 前缀覆盖拒绝与
    // 读取失败两类消息，不绑死具体措辞）。
    EXPECT_FALSE(port.tryObject(commit.oid, commit.cv).has_value());
    EXPECT_TRUE(m_sink->devContains("tryObject "));
}

// =====================================================================
// acceptance 2（具名验收点）：只读打开实例同样提供查询（PM-07 可查看）
// =====================================================================

/**
 * 锚定：§4.7（"只读打开的实例同样提供查询"）／PM-07（可查看禁编辑）／
 * §9.2（显式 ReadOnly 不取锁——持锁者存活时仍可打开）／acceptance 2。
 *
 * 前置：实例 1 可写打开并提交对象（保持 Active 持锁）。
 * 操作：实例 2 显式 ReadOnly 打开同一路径→全套查询。
 * 预期：实例 2 writable==false（禁编辑面）但查询全量可用——head/
 * object/currentMetadata/branchTips 与磁盘事实一致（PM-07"可查看"）。
 */
TEST_F(QueryPortTest, ReadOnlyInstance_QueriesFullyServed)
{
    pd::OpenStoreResult writer = createNew();
    ASSERT_NE(writer.store, nullptr);
    const CommittedInfo commit = commitOne(*writer.store);
    const HeadRecord diskHead = readHeadFromDisk(m_dir);

    // 显式只读打开（不尝试取写锁——§9.2②；写者持锁存活时仍可打开）。
    OpenStoreRequest request;
    request.path = m_dir;
    request.mode = OpenMode::ReadOnly;
    request.diagnostics = m_sink.get();
    pd::OpenStoreResult reader = ProjectStoreFactory::open(request);
    ASSERT_NE(reader.store, nullptr);
    EXPECT_FALSE(reader.store->writable());  // 禁编辑面（PM-07）

    // 查询面全量可用：视图与磁盘事实一致。
    pd::IProjectQueryPort& port = reader.store->query();
    const RevisionView head = port.head();
    EXPECT_TRUE(head.id == diskHead.revisionId);
    EXPECT_EQ(head.seq, 2u);
    EXPECT_EQ(port.object(commit.oid, commit.cv), commit.payload);
    const std::vector<BranchTip> tips = port.branchTips();
    ASSERT_FALSE(tips.empty());
    EXPECT_TRUE(tips[0].tip == diskHead.revisionId);
    EXPECT_TRUE(port.currentMetadata().record.branches[0].tipRevisionId
                == diskHead.revisionId);
    // 草稿/运行清单同样可用（空表——无前置数据的合法常态）。
    EXPECT_TRUE(port.listDrafts(diskHead.branchId).empty());
    EXPECT_TRUE(port.listRuns(diskHead.revisionId).empty());

    // 只读实例的上下文独立关闭（§9.3 实例隔离——读到的都是同一磁盘
    // 事实，锁面互不干扰）。
    EXPECT_EQ(reader.store->requestClose(), 0u);
    EXPECT_EQ(writer.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 1：并发只读（全方法线程安全＋与提交并发）
// =====================================================================

/**
 * 锚定：§4.7（"查询端口全部方法线程安全（并发只读）"）／§9.8（"调用方
 * 线程：查询端口并发只读"）／PA-2/PA-3／acceptance 1（"并发只读单测
 * 通过……读写无共享可变状态"）。
 *
 * 前置：提交一次（种子修订 r2 供对象/修订断言）。
 * 操作：4 个查询线程×40 轮并发调用全部查询方法；主线程同批并发提交
 * 3 次（读写并发——一致性窗口的真实压力面）。
 * 预期：查询线程全部无错误完成（工作线程的校验失败经互斥消息表汇合
 * ——口径 4：gtest 断言不进工作线程）；磁盘 HEAD seq＝5（1 创建＋1
 * 种子＋3 并发批）——提交全部成功，查询未干扰写入。
 */
TEST_F(QueryPortTest, ConcurrentReadOnly_ThreadSafeWithConcurrentCommits)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo seed = commitOne(*opened.store);
    const BranchId branch = readHeadFromDisk(m_dir).branchId;

    // 工作线程失败汇合面（口径 4——gtest 断言非线程安全，主线程统一
    // 断言；mutex 保护消息表）。
    std::mutex failureMutex;
    std::vector<std::string> failures;
    auto recordFailure = [&](const std::string& message) {
        std::lock_guard<std::mutex> lock(failureMutex);
        failures.push_back(message);
    };

    constexpr int kThreads = 4;
    constexpr int kRounds = 40;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            try {
                pd::IProjectQueryPort& port = opened.store->query();
                std::uint64_t lastHeadSeq = 0;
                for (int round = 0; round < kRounds; ++round) {
                    // head：重读磁盘 HEAD——同线程内快照序列单调不回退
                    // （提交只前进——PA-2；撕裂视图不可能——一致性窗口
                    // 与 writer 互斥）。
                    const RevisionView head = port.head();
                    if (head.seq < lastHeadSeq || head.seq < 2u) {
                        recordFailure("head seq 回退: "
                                      + std::to_string(lastHeadSeq) + " -> "
                                      + std::to_string(head.seq));
                        return;
                    }
                    lastHeadSeq = head.seq;
                    // 种子修订的视图恒定（一经返回不可变的历史面——PA-2）。
                    const auto seedView = port.tryRevision(seed.result.revisionId);
                    if (!seedView.has_value() || seedView->seq != 2u) {
                        recordFailure("种子修订视图漂移");
                        return;
                    }
                    // 对象字节恒等（内容寻址——任何时刻读都一样，PA-2）。
                    if (port.object(seed.oid, seed.cv) != seed.payload) {
                        recordFailure("对象字节漂移");
                        return;
                    }
                    // 元数据/分支/历史：结构与链完整性。
                    const ProjectMetadataView meta = port.currentMetadata();
                    if (meta.record.branches.empty()) {
                        recordFailure("权威分支表为空");
                        return;
                    }
                    const std::vector<BranchTip> tips = port.branchTips();
                    if (tips.empty()) {
                        recordFailure("分支视图为空");
                        return;
                    }
                    const std::vector<RevisionView> history
                        = port.branchHistory(branch, 100);
                    for (std::size_t i = 1; i < history.size(); ++i) {
                        if (history[i].seq >= history[i - 1].seq) {
                            recordFailure("历史链 seq 非严格递减");
                            return;
                        }
                    }
                    // 清单面：无前置数据的空表稳定返回；runDir 路径纯计算。
                    if (!port.listDrafts(branch).empty()) {
                        recordFailure("无草稿场景不应有清单");
                        return;
                    }
                    (void)port.listRuns(seed.result.revisionId);
                    (void)port.runDir(RunId::generate());
                }
            } catch (const std::exception& e) {
                recordFailure(std::string("查询线程异常: ") + e.what());
            } catch (...) {
                recordFailure("查询线程未知异常");
            }
        });
    }

    // 主线程并发提交（读写并发——查询与提交互斥窗口的真实压力）。
    for (int i = 0; i < 3; ++i) {
        (void)commitOne(*opened.store);
    }

    for (std::thread& w : workers) {
        w.join();
    }
    // 主线程统一断言：工作线程零失败。
    for (const std::string& message : failures) {
        ADD_FAILURE() << message;
    }

    // 收尾一致性：磁盘 HEAD seq＝1(创建)+1(种子)+3(并发批)=5——提交
    // 全部成功（查询未干扰写入），全部闭包内可查。
    const HeadRecord finalHead = readHeadFromDisk(m_dir);
    EXPECT_EQ(finalHead.revisionSeq, 5u);
    pd::IProjectQueryPort& port = opened.store->query();
    EXPECT_EQ(port.branchHistory(finalHead.branchId, 100).size(), 5u);
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

/**
 * 锚定：§4.6（"查询快照与写入隔离：修订视图一经返回不可变"）／§4.7
 * （"返回视图为进程内值拷贝"）／PA-2/PA-3／acceptance 1。
 *
 * 前置：提交 r2。
 * 操作：取 head 视图→再提交 r3→比对旧视图内容；修改返回副本→再查询。
 * 预期：r2 视图内容与返回时逐字段全等（后续提交不改变已返回视图——
 * 值拷贝＋底层只增）；对副本的修改不影响端口后续返回（进程内值拷贝，
 * 非共享引用）；新 HEAD 的 parent 指向旧 HEAD（历史链推进）。
 */
TEST_F(QueryPortTest, ViewIsValueCopy_ImmutableAfterReturn)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo r2 = commitOne(*opened.store);

    pd::IProjectQueryPort& port = opened.store->query();
    const RevisionView snapshot = port.head();

    // 后续提交（r3——HEAD 前进）。
    (void)commitOne(*opened.store);

    // 已返回视图不可变：逐字段与返回时全等（== 为全字段比较——PA-2）。
    const RevisionView r2Now = port.revision(r2.result.revisionId);
    EXPECT_TRUE(r2Now == snapshot) << "同一修订任何时刻查询结果全等（PA-2）";
    const RevisionView newHead = port.head();
    EXPECT_TRUE(newHead.id != snapshot.id) << "HEAD 已前进（对照腿）";
    ASSERT_TRUE(newHead.parent.has_value());
    EXPECT_TRUE(*newHead.parent == snapshot.id) << "新修订 parent 指向旧 HEAD";

    // 进程内值拷贝：修改副本不影响端口（objectRefs 追加／summary 改写
    // 后，重新查询仍为存储事实——视图是深拷贝非共享引用，PA-3）。
    RevisionView mutated = snapshot;
    mutated.objectRefs.emplace_back();
    mutated.commandSummary = "mutated";
    EXPECT_TRUE(mutated != snapshot);
    EXPECT_TRUE(port.revision(snapshot.id) == snapshot);
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 3：草稿清单视图
// =====================================================================

/**
 * 锚定：§5.2 listDrafts 行（"含 baseRevision 与过期标记"）／§4.4.5
 * DraftDocument／§4.1 drafts 行（.new/.bak 残留不进当前清单）／§8.4／
 * acceptance 3（草稿视图）。
 *
 * 前置：提交两次（r1、r2，HEAD=r2）后植入两个草稿：alpha-stale（基线
 * r1——落后 tip r2）与 zeta-fresh（基线 r2——与 tip 一致）＋一个 .new
 * 崩溃残留。
 * 操作：listDrafts(main)／listDrafts(不存在分支)。
 * 预期：只列两个当前有效草稿（.new 残留不列）；stale 标记逐项正确；
 * 字段（moduleId/baseRevision/savedAtUtc/origin）与落盘一致；排序按
 * moduleId 字典序（NFR-COR-02）；不存在的分支＝空表（无前置常态）。
 */
TEST_F(QueryPortTest, DraftListing_CurrentOnly_StaleAndOrder)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo r1 = commitOne(*opened.store);
    const CommittedInfo r2 = commitOne(*opened.store);
    const BranchId branch = readHeadFromDisk(m_dir).branchId;
    const ProjectId pid = opened.store->projectId();

    // 前置植入：两个当前草稿＋一个 .new 残留（写侧不经被测端口——口径 2；
    // dump 产出合法 canonical 磁盘形态）。
    auto writeDraft = [&](const std::string& module, const RevisionId& base,
                          DraftOrigin origin) {
        DraftDocument doc;
        doc.projectId = pid;
        doc.branchId = branch;
        doc.moduleId = module;
        doc.baseRevisionId = base;
        doc.payload = "{\"edit\":1}";
        doc.savedAtUtc = "2026-09-16T05:00:00Z";
        doc.origin = origin;
        return writeRaw(m_dir / "drafts" / branch.toCanonical()
                            / (module + ".draft.json"),
                        pd::codec::dump(doc));
    };
    ASSERT_TRUE(writeDraft("alpha-stale", r1.result.revisionId, DraftOrigin::Manual));
    ASSERT_TRUE(writeDraft("zeta-fresh", r2.result.revisionId,
                           DraftOrigin::Autosave));
    ASSERT_TRUE(writeRaw(m_dir / "drafts" / branch.toCanonical()
                             / "crash.draft.json.new",
                         "{\"incomplete\":true}"));

    pd::IProjectQueryPort& port = opened.store->query();
    const std::vector<DraftInfo> drafts = port.listDrafts(branch);
    // 只列当前有效草稿；.new 残留不列（§8.4——恢复处置归恢复流程）。
    ASSERT_EQ(drafts.size(), 2u);
    // 排序＝moduleId 字典序（alpha < zeta——目录枚举序的确定性收敛）。
    EXPECT_EQ(drafts[0].moduleId, "alpha-stale");
    EXPECT_EQ(drafts[1].moduleId, "zeta-fresh");
    // stale 判据：base ≠ 权威 tip（r2）→ 应用被拒（PM-04）。
    EXPECT_TRUE(drafts[0].stale);
    EXPECT_FALSE(drafts[1].stale);
    EXPECT_TRUE(drafts[0].baseRevision == r1.result.revisionId);
    EXPECT_TRUE(drafts[1].baseRevision == r2.result.revisionId);
    EXPECT_EQ(drafts[0].savedAtUtc, "2026-09-16T05:00:00Z");
    EXPECT_EQ(drafts[0].origin, DraftOrigin::Manual);
    EXPECT_EQ(drafts[1].origin, DraftOrigin::Autosave);

    // 不存在的分支目录＝空表（无草稿是常态，不是错误——§5.2 表无前置）。
    EXPECT_TRUE(port.listDrafts(BranchId::generate()).empty());
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 3：运行清单视图（仅 finalize）＋runDir
// =====================================================================

/**
 * 锚定：§5.2 listRuns 行（"仅 finalize（有 manifest）的运行"）／§4.1
 * results 行（"无 manifest 的目录＝未完成归档：查询不列为完整运行"）／
 * §10.1/D-13／runDir 行（"供 reporting/evidence 读工件"）／acceptance 3
 * （结果清单视图）。
 *
 * 前置：提交两次（r1、r2）；植入 results/：run-aaa（完整 manifest 绑定
 * r2＋批次工件）、run-bbb（无 manifest——写入中）、run-ccc（完整
 * manifest 绑定 r1——其他修订，过滤反例腿）。
 * 操作：listRuns(r2)／listRuns(r1)／runDir(run-aaa)／runDir(全零)。
 * 预期：listRuns(r2) 只列 run-aaa（D-13 只认完整＋按修订过滤），字段
 * 与 manifest 一致；runDir 为规范 results/<run-id> 路径且工件真实可寻；
 * 全零 runId fail-fast。
 */
TEST_F(QueryPortTest, RunListing_OnlyFinalizedFiltered_runDir)
{
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo r1 = commitOne(*opened.store);
    const CommittedInfo r2 = commitOne(*opened.store);
    const HeadRecord head = readHeadFromDisk(m_dir);
    const std::string pid = opened.store->projectId().toCanonical();
    const std::string branch = head.branchId.toCanonical();

    // run-aaa：完整归档（manifest 原子发布完成）绑定 r2。
    const RunId runAaa = RunId::generate();
    ASSERT_TRUE(writeRaw(m_dir / "results" / runAaa.toCanonical()
                             / "manifest.json",
                         runManifestJson(pid, branch,
                                         r2.result.revisionId.toCanonical(),
                                         runAaa.toCanonical(), "batch-0.bin", 4)));
    // run-aaa 的批次工件（runDir 消费锚点实物）。
    ASSERT_TRUE(writeRaw(m_dir / "results" / runAaa.toCanonical() / "batch-0.bin",
                         "DATA"));
    // run-bbb：未完成归档（批次文件在、manifest 未发布——写入中形态）。
    const RunId runBbb = RunId::generate();
    ASSERT_TRUE(writeRaw(m_dir / "results" / runBbb.toCanonical() / "batch-0.bin",
                         "PARTIAL"));
    // run-ccc：完整但绑定 r1（其他修订——过滤反例腿）。
    const RunId runCcc = RunId::generate();
    ASSERT_TRUE(writeRaw(m_dir / "results" / runCcc.toCanonical()
                             / "manifest.json",
                         runManifestJson(pid, branch,
                                         r1.result.revisionId.toCanonical(),
                                         runCcc.toCanonical(), "batch-0.bin", 4)));

    pd::IProjectQueryPort& port = opened.store->query();
    const std::vector<RunInfo> runs = port.listRuns(r2.result.revisionId);
    // 只列完整且绑定 r2 的运行（run-bbb 无 manifest 不列；run-ccc 绑定
    // r1 被过滤——D-13＋§10.1 绑定语义）。
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_TRUE(runs[0].runId == runAaa);
    EXPECT_TRUE(runs[0].task.project == opened.store->projectId());
    EXPECT_TRUE(runs[0].task.branch == head.branchId);
    EXPECT_TRUE(runs[0].task.revision == r2.result.revisionId);
    EXPECT_TRUE(runs[0].task.run == runAaa);
    EXPECT_EQ(runs[0].task.attempt.value, 1u);  // AttemptId 解析自 att-1
    EXPECT_EQ(runs[0].runKind, "evaluation");
    EXPECT_EQ(runs[0].evaluationKey, "eval-key-1");
    EXPECT_EQ(runs[0].finalizedAtUtc, "2026-09-16T03:00:00Z");

    // 对照腿：r1 的清单恰为 run-ccc。
    const std::vector<RunInfo> runsR1 = port.listRuns(r1.result.revisionId);
    ASSERT_EQ(runsR1.size(), 1u);
    EXPECT_TRUE(runsR1[0].runId == runCcc);

    // runDir：规范编址（results/<run-id 规范文本>）＋全零 fail-fast。
    const fs::path dir = port.runDir(runAaa);
    EXPECT_EQ(dir.filename().string(), runAaa.toCanonical());
    EXPECT_EQ(dir.parent_path().filename().string(), "results");
    EXPECT_TRUE(fs::exists(dir / "batch-0.bin"));  // 工件消费锚点真实存在
    expectInvalidArgument([&] { (void)port.runDir(RunId{}); });
    EXPECT_EQ(opened.store->requestClose(), 0u);
}

// =====================================================================
// acceptance 4（P-PR-1 处置）：视图身份类型＝core v0.1 契约类型
// =====================================================================

/**
 * 锚定：单元卡 §15.3 P-PR-1／§3.2 消费清单／acceptance 4（"视图携带的
 * ObjectId/RevisionId 等身份类型消费以 core.md v0.1 为基线，冻结 diff
 * 后增量同步、不私改 core"）。
 *
 * 断言面（编译期＋运行期双半）：
 *   - 编译期：视图字段类型与 core 公共契约类型**同一**（static_assert
 *     ——project 未定义任何第二套身份类型，core 冻结出 diff 时这些
 *     消费点即增量同步点；同步前编译失败即影响面暴露）；
 *   - 运行期：视图身份的规范文本为 core 规范形态（tag＋32 小写 hex，
 *     tryFromCanonical(toCanonical(x))==x 往返）——本地无第二格式化
 *     路径（与 ObjectStoreTest/RevisionIndexTest 的 P-PR-1 自证同款）。
 */
TEST_F(QueryPortTest, Ppr1_ViewIdentityTypes_AreCoreContractTypes)
{
    // 编译期：字段类型与 core 契约类型同一。
    static_assert(std::is_same_v<decltype(pd::RevisionView::id), RevisionId>,
                  "RevisionView.id 必须是 core::RevisionId（P-PR-1 基线）");
    static_assert(std::is_same_v<decltype(pd::BranchTip::id), BranchId>,
                  "BranchTip.id 必须是 core::BranchId（P-PR-1 基线）");
    static_assert(std::is_same_v<decltype(pd::RunInfo::runId), RunId>,
                  "RunInfo.runId 必须是 core::RunId（P-PR-1 基线）");
    static_assert(
        std::is_same_v<decltype(pd::ProjectMetadataView::ref), ObjectRefPair>,
        "ProjectMetadataView.ref 必须是 ObjectRefPair（core 身份的 using"
        " 声明形态——PersistenceFormat.hpp 消费基线）");
    static_assert(std::is_same_v<pd::InverseRef, InverseCommand>,
                  "InverseRef 必须是磁盘契约 InverseCommand 的别名（不重"
                  "定义第二份逆命令契约）");

    // 运行期：视图身份的规范文本形态与往返（core §5.1 契约）。
    pd::OpenStoreResult opened = createNew();
    ASSERT_NE(opened.store, nullptr);
    const CommittedInfo commit = commitOne(*opened.store);
    pd::IProjectQueryPort& port = opened.store->query();
    const RevisionView head = port.head();
    const std::string revText = head.id.toCanonical();
    EXPECT_TRUE(revText.rfind("rev-", 0) == 0);
    EXPECT_EQ(revText.size(), 36u);  // "rev-" + 32 小写 hex
    const auto roundTrip = RevisionId::tryFromCanonical(revText);
    ASSERT_TRUE(roundTrip.has_value());
    EXPECT_TRUE(*roundTrip == head.id);
    // 元数据/对象引用的规范 tag（brn-/obj-——同一 core 格式化路径）。
    const ProjectMetadataView meta = port.currentMetadata();
    EXPECT_TRUE(meta.record.primaryBranchId.toCanonical().rfind("brn-", 0) == 0);
    EXPECT_TRUE(head.metadataRef.objectId.toCanonical().rfind("obj-", 0) == 0);
    EXPECT_TRUE(head.metadataRef.contentVersion == commit.result.metadataRef
                    .contentVersion);
    EXPECT_EQ(opened.store->requestClose(), 0u);
}
