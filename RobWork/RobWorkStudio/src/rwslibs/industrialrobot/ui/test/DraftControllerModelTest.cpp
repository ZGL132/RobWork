/**
 * @file   DraftControllerModelTest.cpp
 * @brief  UI-T12 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         保存/应用分离与未保存标记（PM-04/PM-11——UI-DRF-1）、定时草稿
 *         保存的线程纪律与失败语义（UI-DRF-2——UI 线程零磁盘 IO/落盘
 *         线程串行/60 s 默认周期）、打开恢复与 .bak 回退横幅（PM-08/
 *         AT-21——UI-DRF-3）、StaleRevisionRejected 呈现与两栈边界
 *         （RV-10/AT-29/PM-18——UI-DRF-4）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T12.json acceptance 1~3（逐条对应各
 *     用例 IRD_TEST_INFO 追溯字段）；knownPitfalls O-31（已裁决 2026-09-19
 *     随整链放行——对端能力全部经 ui 自有端口替身承载，零对端类型）；
 *   - units/ui.md §8 全节（§8.1 分工红线/§8.2 数据流/§8.3 恢复/§8.4 线程
 *     纪律/§8.5 冲突/§8.6 处置/§8.7 两栈）、§10.5（IDraftController
 *     签名随本任务实现冻结——本套件即其行为自证面）、§12.3 UI-DRF-1~4
 *     行的机制半区观测点（GUI 呈现半区归 gui_test 层）；
 *   - 先例：SessionControllerModelTest.cpp 的诊断全链＋可控替身注入形态、
 *     FormEditModelTest.cpp 的纯模型值断言纪律。
 *
 * 为什么替身而不集成桩：O-31 裁决下 ui 产品面对 project 零链接零
 * include——替身实现 ui 自有端口（IUiDraftQueryPort/IUiDraftStorePort），
 * L5 装配期才以适配器绑定对端（§3.1"ui 测试以可控替身承载"原文）。
 * 落盘执行器替身以线程标记自证"端口调用只发生在执行器任务内"（UI
 * 线程零磁盘 IO 红线的执行面证据——§8.4）。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>  // uiDiagnosticCodeDescriptors（§3.5 码表描述符供体——诊断全链装配）
#include <sdurws/ird/ui/IDraftController.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiSessionController.hpp>
#include <sdurws/ird/ui/UiTypes.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CommandResultProjection;
using ui::DraftControllerDeps;
using ui::DraftDocumentProjection;
using ui::DraftingSummary;
using ui::DraftLoadOutcome;
using ui::DraftOrigin;
using ui::DraftRecoveryBannerProjection;
using ui::DraftRowProjection;
using ui::DraftDiscardOutcome;
using ui::DraftSaveOutcome;
using ui::DraftSessionBinding;
using ui::IDraftController;
using ui::IModuleDraftSource;
using ui::ModuleDraftView;
using ui::RestoreOutcome;
using ui::SaveOutcome;
using ui::SaveTrigger;
using ui::StaleConflictDialogData;
using ui::UiOpenMode;

// =====================================================================
// 测试替身（与 SessionControllerModelTest 同款纪律——ui 自有端口承载）
// =====================================================================

class DevLogRecorder final : public diagnostics::IDevLogSink {
public:
    void logDev(std::string_view channel, std::string message) override
    {
        m_entries.emplace_back(std::string(channel), std::move(message));
    }
    bool seen(const std::string& needle) const
    {
        return std::any_of(m_entries.begin(), m_entries.end(),
                           [&needle](const auto& e) {
                               return e.second.find(needle) != std::string::npos;
                           });
    }

private:
    std::vector<std::pair<std::string, std::string>> m_entries;
};

class ManualDiagClock final : public diagnostics::IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{1000000}};
};

/**
 * @brief 串行落盘执行器替身（手动步进/内联两形态）。
 *
 * 线程标记自证：每个任务执行期间 inDiskThread()==true——StubStorePort
 * 在 save/discard 入口断言该标记，证明"对端写调用只发生在执行器任务
 * 内"（UI 线程零磁盘 IO 的执行面证据；生产执行器的"任务在落盘线程
 * 运行"由 L5 装配契约承载——§3.4 线程表单行串行）。
 */
class ManualDiskExecutor final {
public:
    /// deferred=true＝任务入队待步进（autosave 在途场景）；默认 false＝
    /// post 即执行——同步入口 saveAll/discardDraft 阻塞等待的任务必须
    /// 立即完成，否则 future.get 死锁（测试纪律：延迟模式只配 autosave
    /// 用例，不触碰同步入口）。
    void post(std::function<void()> task)
    {
        if (m_deferred) {
            m_queue.push_back(std::move(task));
            return;
        }
        const ScopedTag tag;
        task();
    }

    void setDeferred(const bool deferred) { m_deferred = deferred; }

    bool runOne()
    {
        if (m_queue.empty()) {
            return false;
        }
        std::function<void()> task = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        const ScopedTag tag;
        task();
        return true;
    }

    void runAll()
    {
        while (runOne()) {
        }
    }

    std::size_t pending() const { return m_queue.size(); }

    static bool inDiskThread() { return tagSlot(); }

private:
    bool m_deferred = false;
    struct ScopedTag {
        bool m_prev;
        ScopedTag()
            : m_prev(tagSlot())
        {
            tagSlot() = true;
        }
        ~ScopedTag() { tagSlot() = m_prev; }
    };
    static bool& tagSlot()
    {
        thread_local bool tag = false;
        return tag;
    }
    std::vector<std::function<void()>> m_queue;
};

/// 草稿清单端口替身（C-5 读半区——行集可编程）。
class StubQueryPort final : public ui::IUiDraftQueryPort {
public:
    std::vector<DraftRowProjection> rows;

    std::vector<DraftRowProjection> listDrafts() const override { return rows; }
};

/// 草稿写半区端口替身（C-5 写半区——save/discard 记录＋tryLoad 可编程）。
class StubStorePort final : public ui::IUiDraftStorePort {
public:
    bool saveOk = true;                                ///< false＝注入落盘失败
    std::string failToken = "media-read-only";         ///< 注入失败的稳定 token
    std::vector<DraftDocumentProjection> saved;        ///< save 载荷记录
    std::vector<std::string> discardCalls;             ///< discard 记录
    std::map<std::string, DraftLoadOutcome> loadOutcomes;///< tryLoad 可编程结局
    int revisionCount = 0;                             ///< 修订计数——harness 中零写入者

    DraftSaveOutcome save(const DraftDocumentProjection& document) override
    {
        EXPECT_TRUE(ManualDiskExecutor::inDiskThread())
            << "ui/draft: save 调用必须在落盘执行器任务内（§8.4 UI 线程零"
               "磁盘 IO 的执行面）";
        saved.push_back(document);
        if (!saveOk) {
            return DraftSaveOutcome{false, failToken, "注入的落盘失败明细"};
        }
        return DraftSaveOutcome{true, {}, {}};
    }

    DraftLoadOutcome tryLoad(const std::string& moduleId) override
    {
        const auto it = loadOutcomes.find(moduleId);
        if (it != loadOutcomes.end()) {
            return it->second;
        }
        return DraftLoadOutcome{};  // Missing＝默认安全空值
    }

    DraftDiscardOutcome discard(const std::string& moduleId) override
    {
        EXPECT_TRUE(ManualDiskExecutor::inDiskThread())
            << "ui/draft: discard 调用必须在落盘执行器任务内（§8.4）";
        discardCalls.push_back(moduleId);
        return DraftDiscardOutcome{true, {}, {}};
    }
};

/// 域模块草稿源替身（§8.3-5"桩模块验证控制器协议"——阶段 A 原文形态）。
class StubModuleSource final : public IModuleDraftSource {
public:
    std::string name;                                  ///< 显示名（UX-02）
    DraftDocumentProjection doc;                       ///< buildDraftDocument 返回值
    std::vector<DraftDocumentProjection> adopted;      ///< adoptRestoredDocument 记录
    std::vector<std::string> rebuildCalls;             ///< rebuildOnRevision 记录

    std::string displayName() const override { return name; }

    DraftDocumentProjection buildDraftDocument() const override { return doc; }

    void adoptRestoredDocument(const DraftDocumentProjection& document) override
    {
        adopted.push_back(document);
        // 保真：真实域编辑器承接后文档成为活动编辑态——撤销链的"当前
        // 文档"快照随之推进（桩不同步会伪造第二次 undo 的当前值）。
        doc = document;
    }

    void rebuildOnRevision(const std::string& tipRevisionCanonical) override
    {
        rebuildCalls.push_back(tipRevisionCanonical);
    }
};

// =====================================================================
// 测试环境装配（诊断全链＋手动执行器＋可编程端口——SessionHarness 同案）
// =====================================================================

struct DraftHarness {
    diagnostics::StableCodeRegistry registry;
    ManualDiagClock diagClock;
    std::shared_ptr<diagnostics::DiagnosticsFactory> factory;
    std::shared_ptr<diagnostics::DiagCatalog> catalog;
    std::shared_ptr<DevLogRecorder> devLog = std::make_shared<DevLogRecorder>();
    ManualDiskExecutor disk;
    std::shared_ptr<StubQueryPort> query = std::make_shared<StubQueryPort>();
    std::shared_ptr<StubStorePort> store = std::make_shared<StubStorePort>();
    std::vector<bool> dirtyEvents;                     ///< onSessionDirtyChanged 记录
    std::chrono::steady_clock::time_point now{std::chrono::seconds{1000}};
    std::unique_ptr<IDraftController> controller;

    DraftHarness()
    {
        // 诊断全链（码表收编 UI-* 九码——autosave 失败目录出线依赖
        // UI-DRAFT-AUTOSAVE-FAILED 已注册：未注册码被工厂拒绝）。
        diagnostics::registerBuiltinCodes(registry);
        for (const auto& descriptor : ui::uiDiagnosticCodeDescriptors()) {
            registry.registerCode(descriptor);
        }
        factory = std::make_shared<diagnostics::DiagnosticsFactory>(registry, diagClock);
        catalog = std::make_shared<diagnostics::DiagCatalog>();
    }

    std::size_t countOf(const std::string& code) const
    {
        const auto items = catalog->snapshot(diagnostics::DiagQuery{});
        return static_cast<std::size_t>(std::count_if(
            items.begin(), items.end(),
            [&code](const diagnostics::DiagProjectionItem& i) { return i.code == code; }));
    }

    void buildController()
    {
        controller = ui::createDraftController(makeDeps());
    }

    IDraftController& ctl()
    {
        if (!controller) {
            buildController();
        }
        return *controller;
    }

    DraftControllerDeps makeDeps()
    {
        DraftControllerDeps deps;
        deps.postToDiskThread = [this](std::function<void()> task) {
            disk.post(std::move(task));
        };
        deps.postToUiThread = [](std::function<void()> task) { task(); };
        deps.onSessionDirtyChanged = [this](bool dirty) { dirtyEvents.push_back(dirty); };
        deps.diagSink = catalog;
        deps.diagFactory = factory;
        deps.devLog = devLog;
        deps.steadyClock = [this]() { return now; };
        deps.nowUtcIso = []() { return std::string("2026-09-21T12:00:00Z"); };
        return deps;
    }

    /// 绑定一个可写会话（store/drafts 端口在位——INV-SES-1 数据源直写）。
    void bindWritable()
    {
        DraftSessionBinding binding;
        binding.projectId = core::ProjectId::generate();
        binding.branchId = core::BranchId::generate();
        binding.writable = true;
        binding.drafts = query;
        binding.store = store;
        ctl().bindSession(binding);
    }

    /// 绑定一个只读会话（PM-07——写入口禁用；store 留空＝无写语义场景）。
    void bindReadOnly()
    {
        DraftSessionBinding binding;
        binding.projectId = core::ProjectId::generate();
        binding.branchId = core::BranchId::generate();
        binding.writable = false;
        binding.drafts = query;
        ctl().bindSession(binding);
    }

    /// 挂接一个桩模块并把其文档置为"已编辑"（dirty 通知）。
    StubModuleSource& attachDirty(const std::string& id, const std::string& payload)
    {
        auto source = std::make_unique<StubModuleSource>();
        source->name = id + "-编辑区";
        source->doc.schemaVersion = 1;
        source->doc.moduleId = id;
        source->doc.payload = payload;
        m_sources.push_back(std::move(source));
        ctl().attachModule(id, *m_sources.back());
        ctl().notifySessionDirty(id);
        return *m_sources.back();
    }

    /// 挂接一个干净桩模块（未编辑）。
    StubModuleSource& attachClean(const std::string& id)
    {
        auto source = std::make_unique<StubModuleSource>();
        source->name = id + "-编辑区";
        source->doc.schemaVersion = 1;
        source->doc.moduleId = id;
        m_sources.push_back(std::move(source));
        ctl().attachModule(id, *m_sources.back());
        return *m_sources.back();
    }

private:
    std::vector<std::unique_ptr<StubModuleSource>> m_sources;
};

/// 测试夹具：每个用例全新环境。
class DraftControllerModelTest : public ::testing::Test {
protected:
    DraftHarness& h() { return m_harness; }

private:
    DraftHarness m_harness;
};

/// 造一个磁盘草稿行（§5.4 DraftRowProjection 词表；moduleId 为关联键）。
static DraftRowProjection makeRow(const std::string& moduleId,
                                  const std::string& baseRevision,
                                  const bool stale)
{
    DraftRowProjection row;
    row.documentKey = "doc-" + moduleId;
    row.displayName = moduleId + "-编辑区";
    row.sessionDirty = false;
    row.moduleId = moduleId;
    row.stale = stale;
    row.baseRevisionCanonical = baseRevision;
    return row;
}

/// 造一份 tryLoad 结局（Loaded/RecoveredFromBackup——恢复编排数据面）。
static DraftLoadOutcome makeLoad(DraftLoadOutcome::Status status,
                                 const std::string& moduleId,
                                 const std::string& payload,
                                 const bool residueDropped = false)
{
    DraftLoadOutcome outcome;
    outcome.status = status;
    outcome.document.schemaVersion = 1;
    outcome.document.moduleId = moduleId;
    outcome.document.payload = payload;
    outcome.document.origin = status == DraftLoadOutcome::Status::RecoveredFromBackup
        ? DraftOrigin::Manual
        : DraftOrigin::Autosave;
    outcome.newResidueDropped = residueDropped;
    return outcome;
}

// =====================================================================
// acceptance 1：保存/应用分离与未保存标记（PM-04/PM-11——UI-DRF-1）
// =====================================================================

/// PM-04/§8.1：saveAll(Manual) 只落 drafts/（origin=manual），修订计数
/// 恒零（控制器无命令通道——保存/应用分离的结构红线），成功清脏。
TEST_F(DraftControllerModelTest, ManualSaveLandsDraftWithZeroRevision_PM04)
{
    IRD_TEST_INFO("PM-04", {"AT-28"}, std::nullopt);
    DraftHarness& harness = h();
    harness.bindWritable();
    StubModuleSource& source = harness.attachDirty("kinematics", "payload-v1");
    (void)source;

    const SaveOutcome outcome = harness.ctl().saveAll(SaveTrigger::Manual);

    EXPECT_EQ(outcome.savedCount, std::size_t{1});
    EXPECT_EQ(outcome.failedCount, std::size_t{0});
    // 落盘证据：载荷与来源/时刻由控制器补齐（域负载原样透传）。
    ASSERT_EQ(harness.store->saved.size(), std::size_t{1});
    EXPECT_EQ(harness.store->saved[0].payload, "payload-v1");
    EXPECT_EQ(harness.store->saved[0].origin, DraftOrigin::Manual);
    EXPECT_EQ(harness.store->saved[0].savedAtUtc, "2026-09-21T12:00:00Z");
    // PM-04 红线（ui 侧半区）：保存零修订——本 harness 中修订计数器没有
    // 任何写入者，控制器可调用的面里不存在命令提交（结构保证）。
    EXPECT_EQ(harness.store->revisionCount, 0);
    // 成功清脏（§8.2"成功→清脏标记＋状态栏 * 更新"）。
    EXPECT_FALSE(harness.ctl().summary().anyDirty);
    // 落盘动作只经执行器（队列已排空——同步入口的等待语义）。
    EXPECT_EQ(harness.disk.pending(), std::size_t{0});
}

/// PM-11/§8.2：未保存标记＝会话脏 OR 磁盘 present（标题 `*` 判定位——
/// DraftPresenceProjection.anyUnapplied 同源语义）。
TEST_F(DraftControllerModelTest, UnsavedMarkerMergesSessionAndDisk_PM11)
{
    IRD_TEST_INFO("PM-11", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.bindWritable();
    harness.attachDirty("kinematics", "payload");
    // ①仅会话脏（未首次落盘）——anyDirty=true。
    EXPECT_TRUE(harness.ctl().anyUnappliedChanges());
    // ②保存成功后会话脏清除、磁盘 present 落位——anyDirty 仍 true（
    // 存在未应用草稿）。
    (void)harness.ctl().saveAll(SaveTrigger::Manual);
    harness.query->rows.push_back(makeRow("kinematics", "rev-a", false));
    EXPECT_TRUE(harness.ctl().anyUnappliedChanges());
    // ③全部消费（应用成功：磁盘行消失＋会话脏清零）——anyDirty=false。
    harness.query->rows.clear();
    EXPECT_FALSE(harness.ctl().anyUnappliedChanges());
}

/// §8.2/acceptance 1：跨模块草稿汇总——挂接模块（attach 序）与磁盘孤儿
/// 行（端口序）合并，stale/基线字段直读（"基线已前进"提示素材）。
TEST_F(DraftControllerModelTest, CrossModuleSummaryMergesRowsAndSession)
{
    IRD_TEST_INFO("PM-04", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.bindWritable();
    harness.attachDirty("kinematics", "k");
    harness.attachClean("trajectory");
    // 磁盘：trajectory 有草稿（挂接匹配）＋孤儿行 reporting（无挂接源）；
    // kinematics 行 stale=true（基线已前进）。
    harness.query->rows.push_back(makeRow("trajectory", "rev-t1", false));
    DraftRowProjection staleRow = makeRow("kinematics", "rev-k0", true);
    harness.query->rows.insert(harness.query->rows.begin(), staleRow);
    harness.query->rows.push_back(makeRow("reporting", "rev-r1", false));

    const DraftingSummary summary = harness.ctl().summary();

    ASSERT_EQ(summary.modules.size(), std::size_t{3});
    // 挂接序在前（kinematics→trajectory），孤儿行最后（端口序）。
    EXPECT_EQ(summary.modules[0].moduleId, "kinematics");
    EXPECT_TRUE(summary.modules[0].sessionDirty);
    EXPECT_TRUE(summary.modules[0].diskPresent);
    EXPECT_TRUE(summary.modules[0].stale);
    EXPECT_EQ(summary.modules[0].baseRevisionCanonical, "rev-k0");
    EXPECT_EQ(summary.modules[1].moduleId, "trajectory");
    EXPECT_FALSE(summary.modules[1].sessionDirty);
    EXPECT_TRUE(summary.modules[1].diskPresent);
    EXPECT_EQ(summary.modules[2].moduleId, "reporting");
    EXPECT_FALSE(summary.modules[2].sessionDirty);
    EXPECT_TRUE(summary.modules[2].diskPresent);
    EXPECT_TRUE(summary.anyDirty);
}

// =====================================================================
// acceptance 1：定时草稿保存（UI-DRF-2——UI 线程零磁盘 IO/落盘线程串行/
// 60 s 默认周期）
// =====================================================================

/// PM-04-S1/§8.4：60 s 默认周期；周期内 tick 零分派、到点分派、双触发
/// 防御；非正周期拒绝。
TEST_F(DraftControllerModelTest, AutosaveDefaultIntervalAndTickGuards)
{
    IRD_TEST_INFO("PM-04", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.bindWritable();
    harness.attachDirty("kinematics", "k");
    // 默认周期＝60 s（PM-04-S1 原文；单位秒显式）。
    EXPECT_EQ(harness.ctl().autosaveInterval(), std::chrono::seconds{60});
    // 延迟模式（autosave 在途语义——同步入口在本用例中不触碰）。
    harness.disk.setDeferred(true);
    // 首个 tick（从未分派过——epoch 锚）即分派。
    EXPECT_EQ(harness.ctl().tickAutosave(harness.now), std::size_t{1});
    // 步进完成第一轮（脏清——完成回执消费）。
    harness.disk.runOne();
    EXPECT_FALSE(harness.ctl().summary().anyDirty);
    // 重新置脏后：周期内 tick（距上次分派 < 周期）＝双触发防御零分派。
    harness.ctl().notifySessionDirty("kinematics");
    EXPECT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{1}),
              std::size_t{0});
    // 周期到点 tick＝分派。
    const std::size_t dispatched = harness.ctl().tickAutosave(
        harness.now + std::chrono::seconds{60});
    EXPECT_EQ(dispatched, std::size_t{1});
    // 周期内二次 tick＝双触发防御零分派。
    EXPECT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{61}),
              std::size_t{0});
    // 周期可改（编程面接受任意正值；用户级 60–600 s 归 WP-04-T20）。
    harness.ctl().setAutosaveInterval(std::chrono::seconds{90});
    EXPECT_EQ(harness.ctl().autosaveInterval(), std::chrono::seconds{90});
    EXPECT_THROW(harness.ctl().setAutosaveInterval(std::chrono::seconds{0}),
                 std::invalid_argument);
    // 完成回执消费后排空（分派的任务在执行器内等待步进）。
    harness.disk.runAll();
    EXPECT_EQ(harness.store->revisionCount, 0);
}

/// UI-DRF-2/§8.4：autosave 落盘只发生在执行器任务内（端口入口的线程
/// 标记断言）；失败保留脏标记＋UI-DRAFT-AUTOSAVE-FAILED 目录条目；成功
/// 清脏（§8.2 成败两行）。
TEST_F(DraftControllerModelTest, AutosaveThreadDisciplineAndFailureDiagnostic)
{
    IRD_TEST_INFO("PM-04", {"AT-29"}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    harness.attachDirty("kinematics", "k");
    // 分派即返回（UI 线程零磁盘 IO——任务在执行器队列）。
    // 延迟模式——分派任务停在队列（在途形态由 runOne 步进产生）。
    harness.disk.setDeferred(true);
    EXPECT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{60}),
              std::size_t{1});
    EXPECT_EQ(harness.disk.pending(), std::size_t{1});
    EXPECT_TRUE(harness.devLog->seen("autosave") == false);
    // ①失败路径：脏标记保留＋目录 Warning（码表：用户可见，透传 token）。
    harness.store->saveOk = false;
    harness.disk.runAll();
    EXPECT_TRUE(harness.ctl().anyUnappliedChanges());
    EXPECT_EQ(harness.countOf("UI-DRAFT-AUTOSAVE-FAILED"), std::size_t{1});
    EXPECT_TRUE(harness.devLog->seen("UI-DRAFT-AUTOSAVE-FAILED"));
    // ②成功路径：清脏＋origin=autosave 落盘。
    harness.store->saveOk = true;
    harness.ctl().notifySessionDirty("kinematics");
    (void)harness.ctl().tickAutosave(harness.now + std::chrono::seconds{120});
    harness.disk.runAll();
    EXPECT_FALSE(harness.ctl().anyUnappliedChanges());
    ASSERT_EQ(harness.store->saved.size(), std::size_t{2});
    EXPECT_EQ(harness.store->saved[1].origin, DraftOrigin::Autosave);
}

/// §8.4/acceptance 1：落盘线程串行——在途期间新 tick 跳过（"save 期间的
/// 新脏数据进入下一周期，不合并半成品文档"——在途载荷是分派时刻快照）。
TEST_F(DraftControllerModelTest, AutosaveInFlightDefersNewDirtyToNextCycle)
{
    IRD_TEST_INFO("PM-04", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    StubModuleSource& source = harness.attachDirty("kinematics", "snapshot-v1");
    // 延迟模式——第一轮任务停在队列（在途形态；同步入口不触碰）。
    harness.disk.setDeferred(true);
    // 分派第一轮（载荷＝v1 快照）。
    ASSERT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{60}),
              std::size_t{1});
    // 在途期间的新编辑（代次前进）。
    source.doc.payload = "snapshot-v2";
    harness.ctl().notifySessionDirty("kinematics");
    // 在途未完成→新 tick 零分派（串行守卫）。
    EXPECT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{120}),
              std::size_t{0});
    // 完成第一轮：代次已前进→脏保留（v1 不是最新状态，不许清脏）。
    harness.disk.runAll();
    EXPECT_TRUE(harness.ctl().anyUnappliedChanges());
    // 下一周期分派第二轮：载荷＝v2（新周期新快照——不合并半成品）。
    ASSERT_EQ(harness.ctl().tickAutosave(harness.now + std::chrono::seconds{180}),
              std::size_t{1});
    harness.disk.runAll();
    EXPECT_FALSE(harness.ctl().anyUnappliedChanges());
    ASSERT_EQ(harness.store->saved.size(), std::size_t{2});
    EXPECT_EQ(harness.store->saved[0].payload, "snapshot-v1");
    EXPECT_EQ(harness.store->saved[1].payload, "snapshot-v2");
}

/// §8.4/§5.5/acceptance 1：saveAll 同步结局（关闭对话框语境）；只读/
/// 未绑定会话的写入口拒绝；只读会话拒绝挂接写源（§10.5 前置行）。
TEST_F(DraftControllerModelTest, ManualSaveSyncAndReadOnlyRejections)
{
    IRD_TEST_INFO("PM-03", {"AT-34"}, std::nullopt);
    DraftHarness& harness = h();
    // ①未绑定会话拒绝。
    EXPECT_THROW(harness.ctl().saveAll(SaveTrigger::CloseDialog), std::logic_error);
    // ②只读会话拒绝（PM-07/§5.5——draft.save 禁用的调用面执行点；
    // 挂接写源一并拒绝——§10.5 前置行原文"只读会话拒绝挂接写源"）。
    harness.bindReadOnly();
    EXPECT_THROW(harness.ctl().attachModule("kinematics",
                                            harness.attachClean("kinematics")),
                 std::logic_error);
    EXPECT_THROW(harness.ctl().saveAll(SaveTrigger::Manual), std::logic_error);
    // ③可写会话：CloseDialog 触发＝同步等待落盘完成（§8.4 关闭流程
    // 语境——返回时结局已定，执行器队列已排空）。
    harness.ctl().unbindSession();
    harness.bindWritable();
    (void)harness.attachDirty("kinematics", "k");
    const SaveOutcome outcome = harness.ctl().saveAll(SaveTrigger::CloseDialog);
    EXPECT_EQ(outcome.savedCount, std::size_t{1});
    EXPECT_EQ(harness.disk.pending(), std::size_t{0});
    // ④落盘失败＝返回值轨（失败明细可呈现），不抛环境错误。
    harness.ctl().notifySessionDirty("kinematics");
    harness.store->saveOk = false;
    const SaveOutcome failed = harness.ctl().saveAll(SaveTrigger::Manual);
    EXPECT_EQ(failed.savedCount, std::size_t{0});
    EXPECT_EQ(failed.failedCount, std::size_t{1});
    ASSERT_TRUE(failed.firstError.has_value());
    EXPECT_EQ(failed.firstError->errorToken, "media-read-only");
}

// =====================================================================
// acceptance 1：打开恢复与 .bak 回退（PM-08/AT-21——UI-DRF-3）
// =====================================================================

/// PM-08/AT-21/§8.3：恢复编排——Loaded/RecoveredFromBackup 进域编辑器
/// （adoptRestoredDocument），孤儿汇总，横幅三动作与[放弃]可用位。
TEST_F(DraftControllerModelTest, RestoreOnOpenRecoversBackupAndBanner)
{
    IRD_TEST_INFO("PM-08", {"AT-21"}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    StubModuleSource& kin = harness.attachClean("kinematics");
    StubModuleSource& traj = harness.attachClean("trajectory");
    // 磁盘：kinematics（current 损坏→.bak 回退）、trajectory（健康）、
    // reporting（孤儿——无挂接源）。
    harness.query->rows.push_back(makeRow("kinematics", "rev-k1", false));
    harness.query->rows.push_back(makeRow("trajectory", "rev-t1", false));
    harness.query->rows.push_back(makeRow("reporting", "rev-r1", false));
    harness.store->loadOutcomes["kinematics"] =
        makeLoad(DraftLoadOutcome::Status::RecoveredFromBackup, "kinematics", "bak-content");
    harness.store->loadOutcomes["trajectory"] =
        makeLoad(DraftLoadOutcome::Status::Loaded, "trajectory", "current-content");

    const RestoreOutcome outcome = harness.ctl().restoreOnOpen();

    // 恢复面：两模块恢复、其中 kinematics 经 .bak（旧损坏文件对端保留）。
    EXPECT_EQ(outcome.restoredModules.size(), std::size_t{2});
    ASSERT_EQ(outcome.backupRecoveredModules.size(), std::size_t{1});
    EXPECT_EQ(outcome.backupRecoveredModules[0], "kinematics");
    EXPECT_TRUE(outcome.corruptRecovered());
    EXPECT_EQ(outcome.orphanModuleIds.size(), std::size_t{1});
    // 域编辑器承接（§8.3-5——恢复的草稿进入域编辑器；桩模块验证协议）。
    ASSERT_EQ(kin.adopted.size(), std::size_t{1});
    EXPECT_EQ(kin.adopted[0].payload, "bak-content");
    ASSERT_EQ(traj.adopted.size(), std::size_t{1});
    EXPECT_EQ(traj.adopted[0].payload, "current-content");
    // 横幅（PM-15）：一句话汇总＋恢复名清单＋孤儿计数＋三动作＋[放弃]
    // 可用（可写会话）。
    const DraftRecoveryBannerProjection banner = harness.ctl().recoveryBanner(outcome);
    EXPECT_TRUE(banner.present);
    EXPECT_EQ(banner.messageKey, ui::kDraftRecoveryBannerSummaryKey);
    ASSERT_EQ(banner.restoredModuleNames.size(), std::size_t{2});
    EXPECT_EQ(banner.orphanCount, std::size_t{1});
    EXPECT_TRUE(banner.backupRecovered);
    EXPECT_TRUE(banner.discardAvailable);
    ASSERT_EQ(banner.actionKeys.size(), std::size_t{3});
    EXPECT_EQ(banner.actionKeys[0], ui::kDraftRecoveryActionViewDetailKey);
    EXPECT_EQ(banner.actionKeys[1], ui::kDraftRecoveryActionRestoreKey);
    EXPECT_EQ(banner.actionKeys[2], ui::kDraftRecoveryActionDiscardKey);
}

/// PM-08/§8.3-4：损坏且 .bak 亦不可用→损坏清单进横幅；[放弃]＝显式
/// discard（写轨经执行器；只读会话拒绝——写操作禁用）。
TEST_F(DraftControllerModelTest, CorruptDraftBannerAndDiscardDiscipline)
{
    IRD_TEST_INFO("PM-08", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    (void)harness.attachClean("kinematics");
    harness.query->rows.push_back(makeRow("kinematics", "rev-k1", false));
    DraftLoadOutcome corrupt;
    corrupt.status = DraftLoadOutcome::Status::Corrupt;
    corrupt.errorToken = "draft-corrupt";
    corrupt.detail = "归属校验失败";
    harness.store->loadOutcomes["kinematics"] = corrupt;

    const RestoreOutcome outcome = harness.ctl().restoreOnOpen();

    ASSERT_EQ(outcome.corruptModules.size(), std::size_t{1});
    EXPECT_EQ(outcome.restoredModules.size(), std::size_t{0});
    const DraftRecoveryBannerProjection banner = harness.ctl().recoveryBanner(outcome);
    EXPECT_TRUE(banner.present);
    // [放弃]（可写会话）→显式 discard 到达写半区端口。
    const DraftDiscardOutcome discarded = harness.ctl().discardDraft("kinematics");
    EXPECT_TRUE(discarded.ok);
    ASSERT_EQ(harness.store->discardCalls.size(), std::size_t{1});
    EXPECT_EQ(harness.store->discardCalls[0], "kinematics");
    // 孤儿草稿放弃（无挂接源 token 亦可寻址——横幅[放弃]对象形态）。
    const DraftDiscardOutcome orphanDiscard = harness.ctl().discardDraft("reporting");
    EXPECT_TRUE(orphanDiscard.ok);
    EXPECT_EQ(harness.store->discardCalls.back(), "reporting");
    // 只读会话：discard 拒绝（§8.3-4"放弃＝写操作，只读模式不可用"）。
    harness.ctl().unbindSession();
    harness.bindReadOnly();
    EXPECT_THROW(harness.ctl().discardDraft("kinematics"), std::logic_error);
}

// =====================================================================
// acceptance 2：StaleRevisionRejected 呈现（RV-10/AT-29——UI-DRF-4）
// =====================================================================

/// RV-10/AT-29/§8.5：拒绝→草稿保留（脏标记保持）＋冲突数据装配（当前
/// tip vs 草稿基线＋三选项键）；不自动重试提交（结构：无命令通道——
/// revisionCount 恒零，save 端口零额外调用）。
TEST_F(DraftControllerModelTest, StaleRevisionRetainsDraftAndPresentsConflict)
{
    IRD_TEST_INFO("PM-04", {"AT-29"}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    StubModuleSource& source = harness.attachDirty("kinematics", "draft-edit");
    // 应用回执：Rejected(stale-revision)＋冲突定位数据（CommandResult
    // 附带——project §6.2 词表）。
    CommandResultProjection result;
    result.status = CommandResultProjection::Status::Rejected;
    result.rejectionReason = "stale-revision";
    ui::StaleRevisionDetailProjection detail;
    detail.currentTipRevision = "rev-tip-2";
    detail.draftBaseRevision = "rev-base-1";
    detail.advancedSummary = "外部推进了 1 个修订";
    detail.involvedObjectNames.push_back("关节 2 最大角速度");
    result.staleDetail = detail;

    harness.ctl().onCommandResult("kinematics", result);

    // 草稿保留（拒绝永不销毁草稿——编辑不中断）。
    EXPECT_TRUE(harness.ctl().anyUnappliedChanges());
    EXPECT_TRUE(harness.devLog->seen("stale-revision"));
    // 冲突对话框数据（§8.5 原文三选项＋对照值）。
    const StaleConflictDialogData data = harness.ctl().staleConflictData("kinematics");
    EXPECT_TRUE(data.present);
    EXPECT_EQ(data.moduleDisplayName, "kinematics-编辑区");
    EXPECT_EQ(data.currentTipRevision, "rev-tip-2");
    EXPECT_EQ(data.draftBaseRevision, "rev-base-1");
    EXPECT_EQ(data.advancedSummary, "外部推进了 1 个修订");
    ASSERT_EQ(data.involvedObjectNames.size(), std::size_t{1});
    EXPECT_EQ(data.messageKey, ui::kDraftStaleConflictTitleKey);
    EXPECT_EQ(data.reEditActionKey, ui::kDraftStaleConflictReEditActionKey);
    EXPECT_EQ(data.viewDiffActionKey, ui::kDraftStaleConflictViewDiffActionKey);
    EXPECT_EQ(data.cancelActionKey, ui::kDraftStaleConflictCancelActionKey);
    // 不自动重试提交：控制器面里没有提交通道（结构），行为面＝本次
    // 冲突处理零 save 零修订（revisionCount 恒零——PM-04）。
    EXPECT_EQ(harness.store->saved.size(), std::size_t{0});
    EXPECT_EQ(harness.store->revisionCount, 0);
    // 未知句柄查询＝路由违约 fail-fast（呈现层只按已挂接句柄寻址）。
    EXPECT_THROW(harness.ctl().staleConflictData("nonexistent"), std::logic_error);
}

/// §8.5：[基于当前版本重新编辑]——域源以 tip 重建基线、控制器重置脏
/// 标记与冲突态；无冲突调用＝违约；重编辑清空旧检查点（基线已重建）。
TEST_F(DraftControllerModelTest, ReEditOnCurrentTipRebuildsBaseline)
{
    IRD_TEST_INFO("PM-04", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    StubModuleSource& source = harness.attachDirty("kinematics", "draft-edit");
    CommandResultProjection result;
    result.status = CommandResultProjection::Status::Rejected;
    result.rejectionReason = "stale-revision";
    ui::StaleRevisionDetailProjection detail;
    detail.currentTipRevision = "rev-tip-2";
    detail.draftBaseRevision = "rev-base-1";
    result.staleDetail = detail;
    harness.ctl().onCommandResult("kinematics", result);
    // 无冲突时调用＝路由违约（轨迹模块已挂接但无未决冲突）。
    harness.attachClean("trajectory");
    EXPECT_THROW(harness.ctl().reEditOnCurrentRevision("trajectory"), std::logic_error);

    harness.ctl().reEditOnCurrentRevision("kinematics");

    // 域源重建调用（tip 传入原样）＋冲突态清空＋脏标记重置（§8.5"ui
    // 重置该模块会话脏标记与 baseRevision 显示"）。
    ASSERT_EQ(source.rebuildCalls.size(), std::size_t{1});
    EXPECT_EQ(source.rebuildCalls[0], "rev-tip-2");
    EXPECT_FALSE(harness.ctl().staleConflictData("kinematics").present);
    EXPECT_FALSE(harness.ctl().summary().modules[0].sessionDirty);
}

// =====================================================================
// acceptance 2：两栈边界（§8.7/PM-18——草稿局部撤销 vs 项目命令撤销）
// =====================================================================

/// §8.7：局部撤销栈独立工作（检查点/撤销/重做回放经域源承接，零命令
/// 零修订）；应用成功清栈（"局部撤销不能撤销已应用修订"）；中止结果
/// 草稿原样保留（栈不动）。
TEST_F(DraftControllerModelTest, LocalUndoStackBoundToDraftNotRevisions)
{
    IRD_TEST_INFO("PM-18", {}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    StubModuleSource& source = harness.attachDirty("kinematics", "edit-1");
    const std::size_t saveCallsBefore = harness.store->saved.size();
    // 编辑级撤销：checkpoint(edit-1) → 编辑 → checkpoint(edit-2) → 编辑
    // → undo 回 edit-2 → undo 回 edit-1 → redo 回 edit-2。
    harness.ctl().pushLocalCheckpoint("kinematics");
    source.doc.payload = "edit-2";
    harness.ctl().pushLocalCheckpoint("kinematics");
    source.doc.payload = "edit-3";
    EXPECT_TRUE(harness.ctl().undoLocalEdit("kinematics"));
    ASSERT_EQ(source.adopted.size(), std::size_t{1});
    EXPECT_EQ(source.adopted.back().payload, "edit-2");
    EXPECT_TRUE(harness.ctl().undoLocalEdit("kinematics"));
    EXPECT_EQ(source.adopted.back().payload, "edit-1");
    EXPECT_FALSE(harness.ctl().undoLocalEdit("kinematics"));  // 栈底
    EXPECT_TRUE(harness.ctl().redoLocalEdit("kinematics"));
    EXPECT_EQ(source.adopted.back().payload, "edit-2");
    // 零命令零修订（两栈互不越界的结构半区——本栈不经命令服务）。
    EXPECT_EQ(harness.store->saved.size(), saveCallsBefore);
    EXPECT_EQ(harness.store->revisionCount, 0);
    // 应用成功＝草稿被消费：脏清＋栈清（撤销已应用修订＝false）。
    CommandResultProjection committed;
    committed.status = CommandResultProjection::Status::Committed;
    committed.newRevision = core::RevisionId::generate();
    harness.ctl().onCommandResult("kinematics", committed);
    EXPECT_FALSE(harness.ctl().anyUnappliedChanges());
    EXPECT_FALSE(harness.ctl().undoLocalEdit("kinematics"));
    EXPECT_FALSE(harness.ctl().redoLocalEdit("kinematics"));
    // 中止结局：草稿原样保留（栈不触碰——项目侧回滚不进草稿内容）。
    StubModuleSource& other = harness.attachDirty("trajectory", "t-edit");
    harness.ctl().pushLocalCheckpoint("trajectory");
    other.doc.payload = "t-edit-2";
    CommandResultProjection aborted;
    aborted.status = CommandResultProjection::Status::Aborted;
    aborted.abortReason = "context-closing";
    harness.ctl().onCommandResult("trajectory", aborted);
    EXPECT_TRUE(harness.ctl().summary().modules[1].sessionDirty);
    EXPECT_TRUE(harness.ctl().undoLocalEdit("trajectory"));  // 栈仍在
    EXPECT_EQ(source.adopted.back().payload, "edit-2");      // kinematics 栈已清
}

// =====================================================================
// acceptance 1/3：会话绑定生命周期与生产者接线（会话状态机界面侧）
// =====================================================================

/// §5.2/§8.6：绑定生命周期——编辑→脏事件 true；保存成功→false；解除
/// 绑定表清空、磁盘草稿不触碰；重复绑定/缺端口装配违约 fail-fast；
/// O-31 处置：翻转回调＝生产者接线（接 UiSessionController::


/// 会话工厂替身（生产者接线闭环的最小打开流——成功打开可写会话）。
class StubUiStoreFactory final : public ui::IUiStoreFactoryPort {
public:
    ui::OpenStoreOutcome open(const std::string& canonicalPath,
                              UiOpenMode mode,
                              ui::SessionPortBundle& outBindings) override
    {
        ui::OpenStoreOutcome outcome;
        outcome.ok = true;
        outcome.opened.metadata.projectId = core::ProjectId::generate();
        outcome.opened.metadata.projectDisplayName = "测试项目";
        outcome.opened.metadata.writable = mode == UiOpenMode::Writable;
        auto store = std::make_shared<SessionStoreStub>();
        store->project = outcome.opened.metadata.projectId;
        outBindings.store = store;
        outBindings.drafts = std::make_shared<StubQueryPort>();
        return outcome;
    }

private:
    class SessionStoreStub final : public ui::IUiProjectStorePort {
    public:
        core::ProjectId project;
        std::uint32_t requestClose() override { return 0; }
        bool isClosed() const override { return true; }
        std::unique_ptr<core::IEventSubscription> subscribeClose(
            ui::IUiStoreCloseObserver&) override
        {
            class Nop final : public core::IEventSubscription {
            public:
                void unsubscribe() override {}
            };
            return std::make_unique<Nop>();
        }
    };
};

/// reportSessionDirty——UI-T11 预留缺口随本任务闭合）。
TEST_F(DraftControllerModelTest, SessionLifecycleAndDirtyProducerWiring)
{
    IRD_TEST_INFO("PM-11", {"AT-34"}, std::nullopt);
    DraftHarness& harness = h();
    harness.buildController();
    harness.bindWritable();
    (void)harness.attachDirty("kinematics", "k");
    // 编辑→anyDirty 翻转为 true（生产者事件——L5 接线到会话控制器）。
    ASSERT_FALSE(harness.dirtyEvents.empty());
    EXPECT_TRUE(harness.dirtyEvents.back());
    (void)harness.ctl().saveAll(SaveTrigger::Manual);
    EXPECT_FALSE(harness.dirtyEvents.back());
    // 重复绑定拒绝（须先 unbind——单会话纪律，§5.2）。
    EXPECT_THROW(harness.ctl().bindSession([&] {
        DraftSessionBinding b;
        b.writable = true;
        b.drafts = harness.query;
        b.store = harness.store;
        return b;
    }()), std::logic_error);
    // 解除绑定：表清空、anyDirty=false（磁盘草稿不触碰——本 harness 无
    // discard 调用佐证）。
    harness.ctl().unbindSession();
    EXPECT_FALSE(harness.ctl().hasSession());
    EXPECT_TRUE(harness.ctl().summary().modules.empty());
    EXPECT_EQ(harness.store->discardCalls.size(), std::size_t{0});
    // 未绑定挂接拒绝（§10.5 前置行）。
    EXPECT_THROW(harness.attachDirty("dynamics", "d"), std::logic_error);
    // 生产者接线闭环（UI-T11 预留）：脏事件驱动 UiSessionController::
    // reportSessionDirty → 上下文投影 drafts.sessionDirty（标题 `*` 的
    // 会话半区）。
    StubUiStoreFactory factoryPort;
    ui::UiSessionControllerDeps sessionDeps;
    sessionDeps.storeFactory.reset(&factoryPort, [](ui::IUiStoreFactoryPort*) {});
    sessionDeps.saveAllDraftsManual = []() { return true; };
    ui::UiSessionController session(std::move(sessionDeps));
    (void)session.openProject("D:/proj/demo", UiOpenMode::Writable);
    session.reportSessionDirty(true);
    ASSERT_TRUE(session.context().project.has_value());
    EXPECT_TRUE(session.context().drafts.sessionDirty);
    EXPECT_FALSE(session.context().drafts.present);  // 磁盘无行——仅会话脏
    EXPECT_TRUE(session.context().drafts.anyUnapplied());
    session.reportSessionDirty(false);
    EXPECT_FALSE(session.context().drafts.anyUnapplied());
}

}  // namespace
