/**
 * @file   ArchiveContractTest.cpp
 * @brief  归档协作契约用例组（跨单元契约面，§3.3"归档协作"）——
 *         PRJ-TX-8 进程内模拟：requestClose 后迟到完成事件到达，上下文
 *         存活至归档 finalize、完成后才 Closed；跨项目事件不写入；
 *         ResultArchived 事件经 core IDomainEventBus（P-PR-1 行为级
 *         自证）；并发归档流与 writer 互斥（P-PR-4 行为级自证）。
 *
 * 设计依据：
 *   - units/project.md §11 PRJ-TX-8 行（前置：项目 A 在途运行＋已切 B；
 *     操作：关闭请求→事件迟到；预期：上下文存活至归档 finalize、完成
 *     后才 Closed、跨项目事件不写入）、§10.2（项目切换后的迟到结果归
 *     档流程图——本组即其进程内模拟）、§9.7（引用持有与最终释放——
 *     "r1 引用释放→pending==0→Closed"的时序断言面）、§9.8（writer
 *     互斥——P-PR-4 单侧冻结）、§3.2（core IDomainEventBus 消费清单
 *     ——ResultArchived）；
 *   - 需求 TASK-03/PM-13（迟到结果归属：五元组核对归 execution，绝不
 *     写项目 B——AT-10）、PM-03（关闭排空存储侧）、ARCH §4.5 A1/A8；
 *   - 任务契约 tasks/foundation/PRJ-T14.json acceptance 1/4。
 *
 * 测试口径登记（DTB §5.4）：
 *   1. 事件总线＝core ReferenceEventBus（core.md §5.8"测试内参考总线"
 *      的预期用途——生产总线归 execution，本组只验证 project 侧的
 *      发布契约：事件类别/载荷/发布时点）。同步投递（publish 内回调）
 *      ——事件与关闭回调的先后用单调序号捕获（事件发布先于票据释放
 *      ＝§10.2 流程序）。
 *   2. "跨项目事件不写入"的观测面＝项目 B 的 results 目录磁盘事实
 *      （无外来 run 目录）＋B 的 listRuns（空）双通道；存储侧闸门＝
 *      begin 的项目归属校验（AT-10）。
 *   3. P-PR-4 行为级自证＝双会话并发（两个线程各自 begin→batch→
 *      finalize 不同 run）——调用线程不设限（execution 调度线程模型
 *      未定稿前不预设），全部变更性文件操作经宿主 writer 互斥串行；
 *      RunRegistry 对接细节不预设、不私裁（待 execution 详设二次对齐
 *      ——acceptance 4 处置口径）。
 */

#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/project/ArchivePort.hpp>
#include <sdurws/ird/project/ProjectStore.hpp>
#include <sdurws/ird/project/QueryPort.hpp>
#include <sdurws/ird/project/StoreTypes.hpp>

#include "Codec.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace pd = sdurws::ird::project;
using sdurws::ird::core::AttemptId;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::DomainEvent;
using sdurws::ird::core::DomainEventKind;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::ReferenceEventBus;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;
using pd::ArchiveBatch;
using pd::ArchiveItem;
using pd::ArchiveRequest;
using pd::ArchiveSessionRef;
using pd::ArchiveStatus;
using pd::IResultArchivePort;
using pd::OpenStoreRequest;
using pd::OpenStoreResult;
using pd::ProjectStore;
using pd::ProjectStoreFactory;
using pd::RunManifest;
using pd::RunManifestItem;
using pd::StoreError;
using pd::StoreErrorCode;

namespace {

// ---------------------------------------------------------------------
// 事件观测（core IDomainEventSink 实现——ResultArchived 发布契约断言面）
// ---------------------------------------------------------------------

/// 全局单调序号（事件/关闭回调先后的进程内全序戳——同步投递下无竞争）。
std::atomic<int> g_orderSeq{0};

class RecordingEventSink : public sdurws::ird::core::IDomainEventSink {
public:
    std::vector<DomainEvent> events;
    std::vector<int> arrivalOrder;

    void onEvent(const DomainEvent& event) override
    {
        arrivalOrder.push_back(g_orderSeq.fetch_add(1));
        events.push_back(event);
    }
};

/// 关闭一次性回调（排空完成时序戳）。
class OrderCloseObserver : public pd::ICloseObserver {
public:
    int calls = 0;
    int closedOrder = -1;
    void onStoreClosed(ProjectStore& /*store*/) override
    {
        closedOrder = g_orderSeq.fetch_add(1);
        ++calls;
    }
};

// ---------------------------------------------------------------------
// 诊断 sink（开发诊断捕获——跨项目拒绝观察面）
// ---------------------------------------------------------------------

class DevSink : public pd::IDiagnosticsSink {
public:
    std::vector<std::pair<std::string, std::string>> devs;
    void report(const sdurws::ird::core::DiagnosticRecord& /*record*/) override
    {
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
// 构造辅助（ArchiveServiceTest 同源——契约组独立成文件故重复最小面）
// ---------------------------------------------------------------------

TaskIdentity makeTask(const ProjectId& project)
{
    TaskIdentity t;
    t.project = project;
    t.branch = BranchId::generate();
    t.revision = RevisionId::generate();
    t.run = RunId::generate();
    t.attempt = AttemptId{1};
    return t;
}

ArchiveRequest makeRequest(const TaskIdentity& task, const fs::path& storeRoot)
{
    ArchiveRequest r;
    r.task = task;
    r.runDir = storeRoot / "results" / task.run.toCanonical();
    r.runKind = "kinematics-eval";
    r.evaluationKey = "eval-key-1";
    return r;
}

ArchiveItem makeItem(const std::string& relPath, const std::string& bytes)
{
    ArchiveItem item;
    item.relPath = relPath;
    item.bytes.assign(bytes.begin(), bytes.end());
    return item;
}

RunManifest makeManifest(const TaskIdentity& task,
                         const std::vector<ArchiveItem>& items)
{
    RunManifest m;
    m.taskIdentity = task;
    for (const ArchiveItem& item : items) {
        RunManifestItem mi;
        mi.relPath = item.relPath;
        // 批次字节 64hex 摘要（cv- 前缀剥离——§4.4.7 sha256 字段口径；
        // CR-02 唯一哈希路径＝codec::contentVersionOf，私有头同单元测试
        // 消费——contract 目标已注入 src/ include 路径）。
        mi.sha256
            = pd::codec::contentVersionOf(
                  std::string{item.bytes.begin(), item.bytes.end()})
                  .toCanonical()
                  .substr(3);
        mi.sizeBytes = item.bytes.size();
        m.items.push_back(mi);
    }
    m.runKind = "kinematics-eval";
    m.evaluationKey = "eval-key-1";
    m.finalizedAtUtc = "2026-09-16T11:00:00.000Z";
    return m;
}

}  // namespace

// ---------------------------------------------------------------------
// 用例组：ArchiveContract（套件名登记入 ird-test-report.json）
// ---------------------------------------------------------------------

class ArchiveContractTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_prj_archive_contract"
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
        m_dirA = s_base / ("caseA" + std::to_string(++s_caseCounter))
                 / "projA.rwdesign";
        m_dirB = s_base / ("caseB" + std::to_string(s_caseCounter))
                 / "projB.rwdesign";
        ASSERT_FALSE(ec);
        m_sink = std::make_shared<DevSink>();
        m_bus = std::make_unique<ReferenceEventBus>();
    }

    /// 新建项目 A（注入参考总线＋开发诊断 sink——事件契约断言面；
    /// createNew 签名＝目录/显示名/总线/sink——§5.1 原文形态）。
    OpenStoreResult createA()
    {
        return ProjectStoreFactory::createNew(m_dirA, "Contract A", m_bus.get(),
                                              m_sink.get());
    }

    /// 新建项目 B（独立上下文——跨项目隔离观测面；无总线）。
    OpenStoreResult createB()
    {
        return ProjectStoreFactory::createNew(m_dirB, "Contract B", nullptr,
                                              m_sink.get());
    }

    const fs::path& dirA() const { return m_dirA; }
    const fs::path& dirB() const { return m_dirB; }
    ReferenceEventBus& bus() { return *m_bus; }
    DevSink& sink() { return *m_sink; }

private:
    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dirA;
    fs::path m_dirB;
    std::shared_ptr<DevSink> m_sink;
    std::unique_ptr<ReferenceEventBus> m_bus;
};

fs::path ArchiveContractTest::s_base;
int ArchiveContractTest::s_caseCounter = 0;

// =====================================================================
// acceptance 1：PRJ-TX-8 进程内模拟——上下文存活至 finalize、完成后才
// Closed；ResultArchived 事件经 core 总线；跨项目事件不写入
// =====================================================================

/**
 * 锚定：PRJ-TX-8/§10.2/§9.7/TASK-03/PM-03——acceptance 1（存活时序）。
 *
 * 前置：项目 A 在途运行（begin＝派发时归档预留）；事件订阅就位。
 * 操作：requestClose（用户切走）→迟到完成事件到达→writeBatch→
 * finalize。
 * 预期：requestClose 返回在途数 ≥1 且不关闭（会话持票据——§9.7）；
 * 排空期既有会话写照常；finalize 成功＝归档完成；ResultArchived 事件
 * 恰一条且载荷五元组一致、发布先于关闭回调（§10.2 流程序：事件→引用
 * 释放→Closed）；finalize 返回后 closed()==true、一次性回调恰一次。
 */
TEST_F(ArchiveContractTest, PRJ_TX8_ContextSurvivesUntilFinalizeThenClosed)
{
    OpenStoreResult opened = createA();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;

    // 事件订阅（P-PR-1 行为面：ResultArchived 经 core IDomainEventBus）。
    RecordingEventSink eventSink;
    auto subscription = bus().subscribe(eventSink);

    // 派发时归档预留（§10.1"在途运行获取使用权"——begin 或预登记引用）。
    const TaskIdentity task = makeTask(store.projectId());
    const ArchiveRequest request = makeRequest(task, dirA());
    IResultArchivePort& archive = store.archive();
    ArchiveSessionRef session = archive.begin(request);
    ASSERT_TRUE(static_cast<bool>(session));

    // 用户切走：requestClose 进入排空（不关闭——会话持票据）。
    OrderCloseObserver closer;
    store.subscribeClose(closer);
    const std::uint32_t pending = store.requestClose();
    EXPECT_GE(pending, 1u);
    EXPECT_FALSE(store.closed());

    // 迟到完成事件到达：execution 完成既有归档序列（batch→finalize）。
    const std::vector<ArchiveItem> batch{makeItem("late-result.json", "OK")};
    const ArchiveStatus written
        = archive.writeBatch(session, ArchiveBatch{batch});
    ASSERT_TRUE(written.ok) << (written.error ? written.error->what() : "");
    const ArchiveStatus fin
        = archive.finalize(session, makeManifest(task, batch));
    ASSERT_TRUE(fin.ok) << (fin.error ? fin.error->what() : "");

    // 归档完成 ⇒ 排空归零 ⇒ Closed（完成后才 Closed）＋一次性回调。
    EXPECT_TRUE(store.closed());
    EXPECT_EQ(closer.calls, 1);

    // 事件契约：恰一条 ResultArchived、载荷五元组一致（core §4.9——
    // 事件不携路径，消费者经登记记录取数）。
    ASSERT_EQ(eventSink.events.size(), 1u);
    EXPECT_EQ(eventSink.events[0].kind, DomainEventKind::ResultArchived);
    EXPECT_TRUE(eventSink.events[0].asResultArchived().task == task);

    // 时序：事件发布先于关闭回调（§10.2——finalize→事件→引用释放→
    // Closed；进程内全序戳断言）。
    ASSERT_EQ(eventSink.arrivalOrder.size(), 1u);
    EXPECT_LT(eventSink.arrivalOrder[0], closer.closedOrder);
}

/**
 * 锚定：PRJ-TX-8 观测点"跨项目事件不写入"＋AT-10——acceptance 1。
 *
 * 前置：项目 B 独立上下文。操作：以项目 A 的五元组调用 B 的归档端口
 * （模拟五元组误路由——存储侧最后闸门）。
 * 预期：begin 抛 invalid_argument（项目归属校验）＋开发诊断；B 的
 * results 目录不含外来 run（磁盘事实）；B 的 listRuns 为空；B 上下文
 * 不受影响（不关闭、可正常关闭收尾）。
 */
TEST_F(ArchiveContractTest, PRJ_TX8_CrossProjectEventNotWritten)
{
    OpenStoreResult a = createA();
    ASSERT_NE(a.store, nullptr);
    OpenStoreResult b = createB();
    ASSERT_NE(b.store, nullptr);
    ProjectStore& storeB = *b.store;

    // 外来五元组（项目 A 身份）误入 B 的归档端口。
    const TaskIdentity foreignTask = makeTask(a.store->projectId());
    const ArchiveRequest wrongTarget = makeRequest(foreignTask, dirB());
    try {
        storeB.archive().begin(wrongTarget);
        ADD_FAILURE() << "期望跨项目 begin 被拒绝";
    } catch (const std::invalid_argument&) {
    }
    EXPECT_TRUE(sink().devContains("跨项目归档请求"));

    // 项目 B 磁盘事实：results/ 无任何 run 目录（外来事件不写入）。
    std::error_code ec;
    const fs::path resultsB = dirB() / "results";
    EXPECT_FALSE(fs::exists(resultsB / foreignTask.run.toCanonical(), ec));
    if (fs::exists(resultsB, ec)) {
        EXPECT_TRUE(fs::is_empty(resultsB, ec));
    }
    // 查询面：B 名下无任何运行。
    EXPECT_TRUE(storeB.query().listRuns(foreignTask.revision).empty());

    // B 上下文不受影响：正常关闭收尾（pending==0 同步完成）。
    EXPECT_EQ(storeB.requestClose(), 0u);
    EXPECT_TRUE(storeB.closed());
    a.store->requestClose();
}

/**
 * 锚定：P-PR-4 处置（acceptance 4）——归档端口调用线程单侧冻结的
 * 行为级自证：任意线程进入、writer 互斥内部串行（§9.8）。
 *
 * 操作：两个线程并发驱动各自的 begin→writeBatch→finalize（不同 run）。
 * 预期：两路全部成功（互斥串行不丢写）；两份 manifest 均在磁盘且可
 * 解析（一致性）；随后 requestClose 同步完成（在途归零——无挂起）。
 * RunRegistry 对接细节不在本组预设（待 execution 详设二次对齐——
 * 不私改对端契约）。
 */
TEST_F(ArchiveContractTest, Ppr4_ConcurrentSessionsSerializedByWriterMutex)
{
    OpenStoreResult opened = createA();
    ASSERT_NE(opened.store, nullptr);
    ProjectStore& store = *opened.store;
    IResultArchivePort& archive = store.archive();

    // 两路会话的输入（主线程预构造身份——线程内只驱动端口调用）。
    const TaskIdentity task1 = makeTask(store.projectId());
    const TaskIdentity task2 = makeTask(store.projectId());

    // 线程内不做 gtest 断言（QueryPortTest 并发纪律——互斥消息表汇合，
    // 断言汇合到主线程）：驱动结果以字段收集，join 后统一断言。
    struct DriveResult
    {
        bool beginOk = false;
        bool writeOk = false;
        bool finOk = false;
        std::string firstError;
    };
    auto drive = [&](const TaskIdentity& task) {
        DriveResult r;
        const ArchiveRequest request = makeRequest(task, dirA());
        ArchiveSessionRef session = archive.begin(request);
        r.beginOk = static_cast<bool>(session);
        if (!r.beginOk) {
            r.firstError = "begin failed";
            return r;
        }
        const ArchiveStatus written = archive.writeBatch(
            session,
            ArchiveBatch{{makeItem("out.bin", task.run.toCanonical())}});
        r.writeOk = written.ok;
        if (!written.ok && written.error) {
            r.firstError = written.error->what();
            return r;
        }
        const ArchiveStatus fin = archive.finalize(
            session,
            makeManifest(task,
                         {makeItem("out.bin", task.run.toCanonical())}));
        r.finOk = fin.ok;
        if (!fin.ok && fin.error) {
            r.firstError = fin.error->what();
        }
        return r;
    };

    DriveResult r1;
    DriveResult r2;
    std::thread t1([&] { r1 = drive(task1); });
    std::thread t2([&] { r2 = drive(task2); });
    t1.join();
    t2.join();

    // 汇合断言：两路全部成功（互斥串行不丢写）。
    ASSERT_TRUE(r1.beginOk && r1.writeOk && r1.finOk) << r1.firstError;
    ASSERT_TRUE(r2.beginOk && r2.writeOk && r2.finOk) << r2.firstError;

    // 一致性：两份 manifest 均在磁盘、五元组各自正确；无交错损坏。
    EXPECT_TRUE(fs::exists(dirA() / "results" / task1.run.toCanonical()
                           / "manifest.json"));
    EXPECT_TRUE(fs::exists(dirA() / "results" / task2.run.toCanonical()
                           / "manifest.json"));
    EXPECT_FALSE(store.query().listRuns(task1.revision).empty());
    EXPECT_FALSE(store.query().listRuns(task2.revision).empty());

    // 在途归零：requestClose 同步完成（并发会话全部终结——无永久等待）。
    EXPECT_EQ(store.requestClose(), 0u);
    EXPECT_TRUE(store.closed());
}
