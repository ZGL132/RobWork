/**
 * @file   DraftContractTest.cpp
 * @brief  UI-T12 契约测试（QCoreApplication 级 headless——§12.1 第二层）：
 *         草稿写半区端口与 DraftService 语义锚的对接契约、PM-04 保存/应用
 *         分离的端口面证据、StaleRevisionRejected 数据传递（O-31 虚派发
 *         结构自证）、真后台落盘线程的同步完成纪律（§8.4）。
 *
 * 设计依据：
 *   - 任务契约 UI-T12.json verify 行"ui_contract_test（DraftService 对接
 *     属 §12.1 契约测试面）"——本套件即该行的承接面；
 *   - units/ui.md §8.1（分工红线：save 不产生修订/应用恰好一个新修订）、
 *     §8.4（手动与定时共用同一落盘入口；关闭流程 saveAll 同步完成——
 *     等待≠执行 IO）、§8.5（冲突定位数据由 CommandResult 附带）；
 *   - 冻结基准 project.md §5.4 DraftService（save/tryLoad/discard 契约
 *     表）与 §4.4.5 DraftOrigin 冻结三值（autosave/manual/apply-retained
 *     ——词表锚点，ui 不另立拼写）；
 *   - O-31 处置（acceptance 3）：对端类型零进入——端口经 ui 自有接口
 *     替身以虚派发调用（结构自证），L5 适配器把 project::DraftService
 *     翻译为投影（适配器本体归装配层任务）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IDraftController.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CommandResultProjection;
using ui::DraftDocumentProjection;
using ui::DraftLoadOutcome;
using ui::DraftDiscardOutcome;
using ui::DraftOrigin;
using ui::DraftSaveOutcome;
using ui::DraftSessionBinding;
using ui::IDraftController;

// =====================================================================
// 端口替身（模拟 DraftService 契约——§5.4 语义锚；与产品面零耦合）
// =====================================================================

/// 写半区替身：save→tryLoad 闭环（内存草稿表——磁盘格式语义由对端承载，
/// 本替身只验证"落盘成功后可读回同一文档"的契约形）。
class MemoryStorePort final : public ui::IUiDraftStorePort {
public:
    std::map<std::string, DraftDocumentProjection> drafts;
    int revisionCount = 0;                 ///< 修订计数——本测试内零写入者
    std::vector<std::string> discardCalls;
    std::atomic<int> uiThreadSaves{0};     ///< UI 线程发生的 save 次数（应为 0）
    std::thread::id uiThreadId{};          ///< UI 线程锚（装配时注入）

    DraftSaveOutcome save(const DraftDocumentProjection& document) override
    {
        if (std::this_thread::get_id() == uiThreadId) {
            ++uiThreadSaves;  // 契约违例计数（UI 线程零磁盘 IO 红线）
        }
        drafts[document.moduleId] = document;
        return DraftSaveOutcome{true, {}, {}};
    }

    DraftLoadOutcome tryLoad(const std::string& moduleId) override
    {
        DraftLoadOutcome outcome;
        const auto it = drafts.find(moduleId);
        if (it == drafts.end()) {
            outcome.status = DraftLoadOutcome::Status::Missing;
            return outcome;
        }
        outcome.status = DraftLoadOutcome::Status::Loaded;
        outcome.document = it->second;
        return outcome;
    }

    DraftDiscardOutcome discard(const std::string& moduleId) override
    {
        discardCalls.push_back(moduleId);
        drafts.erase(moduleId);  // 幂等：不存在也是目标态达成
        return DraftDiscardOutcome{true, {}, {}};
    }
};

/// 串行单线程落盘执行器（真后台线程——§3.4"ui 后台落盘线程（1 条）"
/// 的生产形态；析构排空在途任务）。
class WorkerThreadExecutor final {
public:
    WorkerThreadExecutor()
        : m_worker([this]() {
              for (;;) {
                  std::function<void()> task;
                  {
                      std::unique_lock<std::mutex> lock(m_mutex);
                      m_signal.wait(lock,
                                    [this]() { return m_stopped || !m_tasks.empty(); });
                      if (m_tasks.empty()) {
                          return;  // stopped 且队列空——退出
                      }
                      task = std::move(m_tasks.front());
                      m_tasks.erase(m_tasks.begin());
                  }
                  task();
              }
          })
    {
    }

    ~WorkerThreadExecutor()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopped = true;
        }
        m_signal.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
    }

    WorkerThreadExecutor(const WorkerThreadExecutor&) = delete;
    WorkerThreadExecutor& operator=(const WorkerThreadExecutor&) = delete;

    void post(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.push_back(std::move(task));
        }
        m_signal.notify_all();
    }

    /// 等待队列排空（留痕面：所有已分派任务已执行——事件等待不用固定
    /// sleep 判据，§12.3 通用判据；此处以队列为零为准出条件）。
    void drain()
    {
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_tasks.empty()) {
                    return;
                }
            }
            std::this_thread::yield();
        }
    }

private:
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_signal;
    std::vector<std::function<void()>> m_tasks;
    bool m_stopped = false;
};

// =====================================================================
// 域模块草稿源替身（§8.3-5 桩模块——控制器协议的域侧承接面）
// =====================================================================

class StubSource final : public ui::IModuleDraftSource {
public:
    std::string name = "契约桩模块";
    DraftDocumentProjection doc;
    std::vector<DraftDocumentProjection> adopted;
    std::vector<std::string> rebuildCalls;

    std::string displayName() const override { return name; }

    DraftDocumentProjection buildDraftDocument() const override { return doc; }

    void adoptRestoredDocument(const DraftDocumentProjection& document) override
    {
        adopted.push_back(document);
    }

    void rebuildOnRevision(const std::string& tipRevisionCanonical) override
    {
        rebuildCalls.push_back(tipRevisionCanonical);
    }
};

class StubQueryPort final : public ui::IUiDraftQueryPort {
public:
    std::vector<ui::DraftRowProjection> rows;

    std::vector<ui::DraftRowProjection> listDrafts() const override { return rows; }
};

// =====================================================================
// 测试环境（契约面最小装配——真后台执行器注入＋可写绑定）
// =====================================================================

struct DraftContractHarness {
    std::shared_ptr<StubQueryPort> query = std::make_shared<StubQueryPort>();
    std::shared_ptr<ui::IUiDraftStorePort> store;
    std::unique_ptr<IDraftController> controller;
    std::vector<StubSource> sources;

    DraftContractHarness(std::shared_ptr<ui::IUiDraftStorePort> storePort,
                         std::function<void(std::function<void()>)> postToDiskThread)
        : store(std::move(storePort))
    {
        ui::DraftControllerDeps deps;
        deps.postToDiskThread = std::move(postToDiskThread);
        // UI 回投内联（QCoreApplication 级模型测试无事件循环——回执同步
        // 消费；Marshal 纪律的"队列化"形态由生产执行器承载）。
        deps.postToUiThread = [](std::function<void()> task) { task(); };
        deps.nowUtcIso = []() { return std::string("2026-09-21T12:00:00Z"); };
        deps.steadyClock = []() {
            return std::chrono::steady_clock::now();
        };
        controller = ui::createDraftController(std::move(deps));
    }

    void bindWritable()
    {
        DraftSessionBinding binding;
        binding.projectId = core::ProjectId::generate();
        binding.branchId = core::BranchId::generate();
        binding.writable = true;
        binding.drafts = query;
        binding.store = store;
        controller->bindSession(binding);
    }

    StubSource& attachDirty(const std::string& id, const std::string& payload)
    {
        sources.emplace_back();
        StubSource& source = sources.back();
        source.doc.schemaVersion = 1;
        source.doc.moduleId = id;
        source.doc.payload = payload;
        controller->attachModule(id, source);
        controller->notifySessionDirty(id);
        return source;
    }

    IDraftController& ctl() { return *controller; }
};

// =====================================================================
// acceptance 3（O-31/契约面）：写半区端口协议与 DraftService 语义锚
// =====================================================================

/**
 * PRJ-T12 对接契约（§5.4/§4.4.5 词表锚）：save→tryLoad 闭环保真（来源
 * token 冻结三值往返）；Missing/Discard 幂等语义；PM-04 端口面证据＝
 * save 全程零修订（revisionCount 无写入者），且零次发生在 UI 线程
 * （真后台线程执行器——UI 线程零磁盘 IO 的运行期证据）。
 */
TEST(DraftContractTest, StorePortRoundTripAnchorsDraftServiceSemantics)
{
    IRD_TEST_INFO("PM-04", {"AT-28"}, std::nullopt);
    // 来源 token 词表冻结（§4.4.5 三值——L5 适配器装回 project::
    // DraftDocument.origin 的映射依据；ui 不另立拼写）。
    EXPECT_STREQ(ui::draftOriginToken(DraftOrigin::Autosave), "autosave");
    EXPECT_STREQ(ui::draftOriginToken(DraftOrigin::Manual), "manual");
    EXPECT_STREQ(ui::draftOriginToken(DraftOrigin::ApplyRetained), "apply-retained");

    WorkerThreadExecutor executor;
    auto store = std::make_shared<MemoryStorePort>();
    store->uiThreadId = std::this_thread::get_id();
    DraftContractHarness harness(store, [&executor](std::function<void()> task) {
        executor.post(std::move(task));
    });
    harness.bindWritable();
    StubSource& source = harness.attachDirty("kinematics", "payload-a");
    (void)source;

    // 手动保存（同步入口）→落盘在真后台线程完成。
    const ui::SaveOutcome outcome = harness.ctl().saveAll(ui::SaveTrigger::Manual);
    EXPECT_EQ(outcome.savedCount, std::size_t{1});
    executor.drain();

    // 闭环：落盘文档可读回，字段保真（来源 token 逐字一致）。
    const DraftLoadOutcome loaded = store->tryLoad("kinematics");
    EXPECT_EQ(loaded.status, DraftLoadOutcome::Status::Loaded);
    EXPECT_EQ(loaded.document.payload, "payload-a");
    EXPECT_EQ(loaded.document.origin, DraftOrigin::Manual);
    EXPECT_EQ(loaded.document.savedAtUtc, "2026-09-21T12:00:00Z");

    // 放弃（幂等——不存在也是目标态达成）；Missing 常态（非错误）。
    const DraftDiscardOutcome discarded = harness.ctl().discardDraft("kinematics");
    EXPECT_TRUE(discarded.ok);
    const DraftLoadOutcome missing = store->tryLoad("kinematics");
    EXPECT_EQ(missing.status, DraftLoadOutcome::Status::Missing);

    // PM-04 端口面：全程零修订＋零 UI 线程落盘（真后台线程证据）。
    EXPECT_EQ(store->revisionCount, 0);
    EXPECT_EQ(store->uiThreadSaves.load(), 0);
}

/**
 * O-31 虚派发结构自证＋§8.5 数据传递：控制器只经 ui 自有接口（IDraft
 * Controller 虚表）触达对端能力；冲突定位投影逐字段原样进对话框数据
 * （投影≠重定义——NFR-MNT-03 单一权威）。
 */
TEST(DraftContractTest, StaleDetailPassesThroughProjectedInterfaces)
{
    IRD_TEST_INFO("RV-10", {"AT-29"}, std::nullopt);
    WorkerThreadExecutor executor;
    auto store = std::make_shared<MemoryStorePort>();
    DraftContractHarness harness(store, [&executor](std::function<void()> task) {
        executor.post(std::move(task));
    });
    harness.bindWritable();
    StubSource& source = harness.attachDirty("kinematics", "draft-edit");
    (void)source;
    // O-31 结构自证：消费面全部经基类引用虚派发（对端类型零出现——
    // include 面由 NoCrossUnitInclude_O31_UI_BUILD 常驻守卫复核，本用例
    // 钉住运行期形态：IDraftController 接口即可完成全部协作）。
    IDraftController& controller = harness.ctl();

    CommandResultProjection result;
    result.status = CommandResultProjection::Status::Rejected;
    result.rejectionReason = "stale-revision";
    ui::StaleRevisionDetailProjection detail;
    detail.currentTipRevision = "rev-tip-9";
    detail.draftBaseRevision = "rev-base-4";
    detail.advancedSummary = "分支前进摘要";
    detail.involvedObjectNames = {"对象甲", "对象乙"};
    result.staleDetail = detail;
    controller.onCommandResult("kinematics", result);

    // 逐字段一致（装配不加工——§8.5"显示当前 tip vs 草稿 baseRevisionId"）。
    const ui::StaleConflictDialogData data = controller.staleConflictData("kinematics");
    EXPECT_TRUE(data.present);
    EXPECT_EQ(data.currentTipRevision, "rev-tip-9");
    EXPECT_EQ(data.draftBaseRevision, "rev-base-4");
    EXPECT_EQ(data.advancedSummary, "分支前进摘要");
    EXPECT_EQ(data.involvedObjectNames, (std::vector<std::string>{"对象甲", "对象乙"}));
}

/**
 * §8.4 关闭流程语境：saveAll(CloseDialog) 在真后台线程执行器上同步
 * 完成——返回时落盘已达成（同步等待≠执行 IO）；只读会话写入口拒绝
 * （PM-07/§5.5 门卫语义的调用面执行点）。
 */
TEST(DraftContractTest, CloseDialogSaveCompletesSynchronouslyOnDiskThread)
{
    IRD_TEST_INFO("PM-03", {}, std::nullopt);
    WorkerThreadExecutor executor;
    auto store = std::make_shared<MemoryStorePort>();
    store->uiThreadId = std::this_thread::get_id();
    DraftContractHarness harness(store, [&executor](std::function<void()> task) {
        executor.post(std::move(task));
    });
    harness.bindWritable();
    (void)harness.attachDirty("trajectory", "close-save");

    // 同步返回＝结局已定（真后台线程完成落盘——等待语义的运行期实证）。
    const ui::SaveOutcome outcome = harness.ctl().saveAll(ui::SaveTrigger::CloseDialog);
    EXPECT_EQ(outcome.savedCount, std::size_t{1});
    const DraftLoadOutcome loaded = store->tryLoad("trajectory");
    EXPECT_EQ(loaded.status, DraftLoadOutcome::Status::Loaded);
    EXPECT_EQ(store->revisionCount, 0);

    // 只读会话：saveAll 拒绝（写门卫的调用面前置——§5.5 只读禁用清单；
    // 对端门卫是第二道，ui 侧不把注定被拒的写递出去）。
    harness.ctl().unbindSession();
    DraftSessionBinding binding;
    binding.projectId = core::ProjectId::generate();
    binding.branchId = core::BranchId::generate();
    binding.writable = false;
    binding.drafts = harness.query;
    harness.ctl().bindSession(binding);
    EXPECT_THROW(harness.ctl().saveAll(ui::SaveTrigger::Manual), std::logic_error);
}

}  // namespace
