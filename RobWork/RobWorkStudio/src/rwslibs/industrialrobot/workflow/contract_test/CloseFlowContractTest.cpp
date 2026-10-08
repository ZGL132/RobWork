/**
 * @file   CloseFlowContractTest.cpp
 * @brief  关闭/切换/退出统一确认编排的契约测试（WF-VER-209~212——units/
 *         workflow.md §11.2 生命周期主线；PM-03/PM-12/ARCH §6.8 A7 的
 *         真实落盘承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.3（流程图＋三条注记）、§11.2（用例 209 关闭
 *     三选/任务二选〔对话框流程＝GUI 设计，数据面在此承载〕、210 等待
 *     在途归档 A7〔契约〕、211 暂停中取消经关闭编排〔契约〕、212 分支
 *     切换零写入〔契约——观测点＝修订/字节〕）、§10.3、§5.4（关闭/
 *     切换编排调 shutdown(DrainPolicy)/drained——执行侧经端口承载）
 *   - REQUIREMENTS.md §17 PM-03 原文（统一确认对话框三选＋二选＋9 态
 *     短标签；切换＝关闭后候选验证成功才切上下文；退出复用同一流程，
 *     取消可中止）、PM-12 原文（分支切换前先处置未应用草稿〔PM-04〕；
 *     URDF 基线修订只读）、AT-20/AT-21
 *   - ARCHITECTURE.md §6.8 A7（界面会话与存储上下文分离——存储上下文
 *     保持到在途归档＋草稿落盘完成；S7 项目切换时序）、§11 场景 S7
 *   - project.md §4.5（切换分支不产生修订、不写任何文件含 HEAD——切换
 *     ＝纯会话选择）、§9.7（requestClose→Draining→pending==0→Closed；
 *     ui 关闭对话框"等待"分支轮询 closed()）、§8.7（激活前失败不影响
 *     当前项目）
 *   - 任务契约 tasks/foundation/WP-22-T06.json acceptance 1/2/3
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：本文件与 project
 * 真实存储实现联合（ProjectStoreFactory::createNew/open＋DraftService
 * 真实落盘）——关闭编排的存储面（候选验证/存储上下文排空/草稿处置）
 * 由 project 真实执行；execution 侧排空经脚本化端口承载（DrainCoordinator
 * 的排空语义归 EX 单元自身测试——本文件钉扎编排面对端口形状与调用序的
 * 消费契约；端到端在途任务归 WP-22-T13 主线）。
 *
 * "零写入"与"不动当前项目"不是声明：以 HEAD 字节快照＋目录树逐文件
 * 快照＋branchTips/branchHistory 修订计数三重复核（lock 心跳文件排除
 * ——持有期心跳重写是正常事实，见 snapshotTree 注）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Evaluation.hpp>  // core::TaskState（九态透传桩料）
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/project/DraftService.hpp>  // DraftService（drafts() 返回类型的完整定义——save/discard/summarize 真实消费）
#include <sdurws/ird/project/PersistenceFormat.hpp>  // DraftDocument/DraftOrigin（草稿落盘构造）
#include <sdurws/ird/project/QueryPort.hpp>  // branchTips/branchHistory（修订观测点）
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::CloseDialogData;
using workflow::CloseFlowOutcome;
using workflow::CloseKind;
using workflow::DraftDisposition;
using workflow::RunningTaskDecision;
using workflow::SchemeBranchSwitchOutcome;

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立目录——OpenWizardContract 同型）
// =====================================================================

class CloseFlowContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_close_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        std::error_code ec;
        fs::remove_all(s_base, ec);  // 失败保留现场惯例——总根清理
    }

    void SetUp() override
    {
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter));
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        ASSERT_FALSE(ec);
    }

    /// 目录树字节面快照（相对路径 UTF-8 → 文件大小）。排除 lock 文件：
    /// 持有方心跳线程按固定周期原地重写（project.md §9.4——内容变化是
    /// 持有中的正常事实）；因此零写入复核对 lock 只做"存在性"复核。
    static std::map<std::string, std::uintmax_t> snapshotTree(const fs::path& root)
    {
        std::map<std::string, std::uintmax_t> snapshot;
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(root, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) {
                continue;
            }
            const std::string name = it->path().filename().string();
            if (name == "lock") {
                continue;  // 心跳重写面排除（见函数注）
            }
            snapshot[it->path().lexically_relative(root).u8string()]
                = it->file_size(ec);
        }
        return snapshot;
    }

    /// 单文件字节面读回（HEAD 字节级复核——大小快照不辨内容）。
    static std::string readFileBytes(const fs::path& file)
    {
        std::ifstream in(file, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    }

    /// 以 createNew 产出黄金项目（调用方持有 store——释放写锁须
    /// requestClose＋reset，见各用例）。
    static project::OpenStoreResult createGolden(const fs::path& dir)
    {
        return project::ProjectStoreFactory::createNew(dir, "关闭编排黄金项目");
    }

    /// 打开既有项目（Writable 请求——被持锁时降级只读由 PM-07 承载，
    /// 本套件无双实例场景）。
    static project::OpenStoreResult openGolden(const fs::path& dir)
    {
        project::OpenStoreRequest request;
        request.path = dir;
        request.mode = project::OpenMode::Writable;
        return project::ProjectStoreFactory::open(request);
    }

    /// 在真实存储上下文的活动分支上落盘一份草稿（"编辑器会话产物"的
    /// 测试等价物——PM-04 保存语义；基线＝当前分支 tip）；返回草稿文件
    /// 路径（编排后的盘面复核用——Closed 后 DraftService 读轨拒绝，
    /// 盘面事实检查不受限）。
    static fs::path saveDraftViaService(project::ProjectStore& store,
                                        const std::string& moduleId)
    {
        // 夹具辅助函数（非 TEST 体）——gtest 的 ASSERT_* 宏以 return; 表达
        // 失败、与非 void 返回类型冲突，故用 ADD_FAILURE＋空路径兜底；
        // 调用方对返回值做空校验（空路径＝夹具已报失败）。
        const auto tips = store.query().branchTips();
        if (tips.empty()) {
            ADD_FAILURE() << "夹具前置失败：分支清单为空";
            return {};
        }
        project::DraftDocument doc;
        doc.projectId = store.projectId();
        doc.branchId = tips[0].id;
        doc.moduleId = moduleId;
        doc.baseRevisionId = tips[0].tip;  // 草稿基线＝当前 tip（PM-04 口径）
        doc.payload = "{\"note\":\"close-flow-contract\"}";  // 域 canonical 字节（project 不解释）
        doc.savedAtUtc = "2026-10-08T00:00:00Z";
        doc.origin = project::DraftOrigin::Manual;
        const project::SaveResult saved = store.drafts().save(doc);
        if (!saved.ok) {
            ADD_FAILURE() << "草稿落盘失败（夹具前置）: "
                          << (saved.error ? saved.error->what() : "?");
            return {};
        }
        return saved.file;
    }

    static fs::path s_base;
    static int s_caseCounter;
    fs::path m_dir;
};

fs::path CloseFlowContract::s_base;
int CloseFlowContract::s_caseCounter = 0;

// =====================================================================
// 脚本化端口（决策/排空——execution 侧排空语义的编排面承载；草稿端口
// 桥接真实 DraftService）
// =====================================================================

/// 决策脚本桩（同模型测试 DecisionStub——捕获呈现数据）。
class ScriptedDecisions final : public workflow::ICloseDecisionPort {
public:
    DraftDisposition draftAnswer = DraftDisposition::Cancel;
    RunningTaskDecision taskAnswer = RunningTaskDecision::CancelFlow;
    std::vector<CloseDialogData> draftPrompts;
    std::vector<CloseDialogData> taskPrompts;

    DraftDisposition collectDraftDisposition(const CloseDialogData& data) override
    {
        draftPrompts.push_back(data);
        return draftAnswer;
    }
    RunningTaskDecision collectRunningTaskDecision(const CloseDialogData& data) override
    {
        taskPrompts.push_back(data);
        return taskAnswer;
    }
};

/// 排空脚本桩（execution §7.5 端口形状的消费契约承载——编排核经本形状
/// 驱动执行侧；DrainCoordinator 自身语义归 EX 单元测试）。
class ScriptedDrain final : public workflow::ICloseDrainPort {
public:
    bool active = false;
    std::vector<core::TaskState> states;
    bool waitOk = true;
    bool cancelOk = true;
    int waitCalls = 0;
    int cancelCalls = 0;

    bool hasActiveTask(core::ProjectId) override { return active; }
    std::vector<core::TaskState> taskStates(core::ProjectId) override
    {
        return states;
    }
    bool waitDrain() override
    {
        ++waitCalls;
        return waitOk;
    }
    bool cooperativeCancel() override
    {
        ++cancelCalls;
        return cancelOk;
    }
};

/**
 * 草稿处置端口的真实桥接（L5 装配层实现形态的测试承载——消费 project
 * DraftService）：清单＝summarize 的 present 项（§8.5"未应用修改"标记
 * 同源口径）；放弃＝逐模块 discard（真实删文件）；保存＝逐模块 tryLoad
 * 验证在盘可读（测试场景中草稿已落盘——"保存"动作的落盘事实已成立，
 * 幂等成功；编辑器会话未保存态的组装不在本桥接承载面）。
 */
class DraftServiceBridge final : public workflow::ICloseDraftPort {
public:
    explicit DraftServiceBridge(project::ProjectStore& store)
        : m_store(store)
    {
        const auto tips = m_store.query().branchTips();
        m_branch = tips.empty() ? core::BranchId{} : tips[0].id;
    }

    std::vector<std::string> unappliedDraftModules() override
    {
        std::vector<std::string> modules;
        for (const auto& item : m_store.drafts().summarize(m_branch).items) {
            if (item.present) {
                modules.push_back(item.moduleId);
            }
        }
        return modules;
    }

    bool saveDrafts() override
    {
        // 测试桥接的保存语义：草稿已落盘（DraftService.save 在夹具前置
        // 完成）——逐模块验证在盘可读即"落盘事实成立"。
        std::vector<core::DiagnosticRecord> diags;
        for (const auto& moduleId : unappliedDraftModules()) {
            if (!m_store.drafts().tryLoad(m_branch, moduleId, diags).has_value()) {
                return false;
            }
        }
        return true;
    }

    bool discardDrafts() override
    {
        bool allOk = true;
        for (const auto& moduleId : unappliedDraftModules()) {
            const project::DiscardResult result =
                m_store.drafts().discard(m_branch, moduleId);
            allOk = allOk && result.ok;
        }
        return allOk;
    }

private:
    project::ProjectStore& m_store;///< 存储上下文（非 owning——夹具持有）
    core::BranchId m_branch;       ///< 活动分支（草稿投影查询键）
};

// =====================================================================
// WF-VER-209 统一确认对话框数据面＋取消可中止（PM-03/AT-20/21——
// 对话框流程为 GUI〔设计〕，本用例承载其数据面与决策半区）
// =====================================================================

/**
 * 对话框数据面真实承载：未应用草稿三选的呈现材料来自真实草稿投影
 * （DraftService summarize——present 项即"将丢失哪些编辑"）；任务二选
 * 的九态清单经端口透传（9 态短标签数据源＝execution 九态——词表零加工）。
 * 取消可中止：三选"取消"＝编排中止（Aborted{DraftPrompt}）且**草稿仍在
 * 盘**（tryLoad 非空——取消保留原状）、存储上下文未关闭（Active 原状）。
 */
TEST_F(CloseFlowContract, WF_VER_209_DialogDataPlaneAndCancelAbortsOverRealDrafts)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"},
                  std::vector<std::string>{"AT-20", "AT-21"});

    // ---- 当前项目：打开＋真实草稿落盘（requirements 模块——PM-04）。
    const fs::path projectDir = m_dir / "current.rwdesign";
    project::OpenStoreResult current = createGolden(projectDir);
    ASSERT_TRUE(current.store != nullptr);
    ASSERT_TRUE(current.store->writable());
    const fs::path draftFile = saveDraftViaService(*current.store, "requirements");
    ASSERT_FALSE(draftFile.empty()) << "夹具前置：草稿落盘失败";

    DraftServiceBridge draftsPort(*current.store);
    ASSERT_EQ(draftsPort.unappliedDraftModules(),
              (std::vector<std::string>{"requirements"}));

    ScriptedDecisions decisions;
    decisions.draftAnswer = DraftDisposition::Cancel;  // 用户选择"取消"
    ScriptedDrain drain;

    // ---- 关闭编排（三选取消路径）。
    workflow::CloseFlowRequest request;
    request.decisions = &decisions;
    request.drafts = &draftsPort;
    request.drain = &drain;
    const auto outcome =
        workflow::CloseFlow::run(CloseKind::Close, *current.store, request);

    // 取消可中止：Aborted{DraftPrompt}＋存储上下文原状（未排空未关闭）。
    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Aborted);
    EXPECT_EQ(outcome.abortedAt, CloseFlowOutcome::AbortStage::DraftPrompt);
    EXPECT_TRUE(current.store->writable()) << "取消后当前项目保持可写（原状）";
    EXPECT_FALSE(current.store->closed());

    // 决策点呈现数据：草稿清单来自真实投影（三选对话框材料）。
    ASSERT_EQ(decisions.draftPrompts.size(), 1u);
    EXPECT_EQ(decisions.draftPrompts[0].draftModules,
              (std::vector<std::string>{"requirements"}));
    EXPECT_EQ(decisions.draftPrompts[0].scenarioKey, "close.flow.close");
    // 任务决策点未到达（中止在前）。
    EXPECT_TRUE(decisions.taskPrompts.empty());

    // 取消保留草稿原状（盘面事实——放弃/保存都未发生，文件仍在）。
    EXPECT_TRUE(fs::exists(draftFile)) << "取消后草稿仍在盘（原状保留）";

    // ---- 二选数据面（同一会话继续）：先以"放弃"处置掉草稿（真实删
    // 文件——桥接端口对 DraftService 的消费），再运行编排——无草稿直达
    // 任务决策点（三选不弹出）。
    ASSERT_TRUE(draftsPort.discardDrafts());
    drain.active = true;
    drain.states = {core::TaskState::Running, core::TaskState::Canceled};
    decisions.taskAnswer = RunningTaskDecision::CancelFlow;
    const auto outcome2 =
        workflow::CloseFlow::run(CloseKind::Close, *current.store, request);
    ASSERT_EQ(outcome2.result, CloseFlowOutcome::Result::Aborted);
    EXPECT_EQ(outcome2.abortedAt, CloseFlowOutcome::AbortStage::TaskPrompt);
    ASSERT_EQ(decisions.taskPrompts.size(), 1u);
    EXPECT_EQ(decisions.taskPrompts[0].taskStates,
              (std::vector<core::TaskState>{core::TaskState::Running,
                                            core::TaskState::Canceled}))
        << "九态清单透传零加工（PM-03 任务清单 9 态短标签数据源）";

    // 收尾释放写锁。
    current.store->requestClose();
    current.store.reset();
}

// =====================================================================
// WF-VER-210 等待在途归档（PM-03/ARCH §6.8 A7——契约；观测点＝归档完成）
// =====================================================================

/**
 * 等待选项覆盖在途归档（A7：界面会话与存储上下文分离——等待在途运行
 * 接纳归档＋草稿落盘完成）：编排面承载＝①等待分支排空先行（shutdown
 * CancelQueuedAndWait＋drained——execution §7.5）②存储上下文排空收尾
 * （requestClose→轮询 closed——project §9.7"等待"分支实现锚点）③编排
 * 以 Proceed 结束时存储上下文必然已 Closed（排空完成＝A7 完成时点）。
 * 切换形态同时承载（S7：候选验证成功→旧上下文排空→候选移交——界面
 * 会话切至新项目）。端到端在途任务（execution worker 全链）归 WP-22-T13
 * 主线——本用例钉扎编排面与两对端的消费契约。
 */
TEST_F(CloseFlowContract, WF_VER_210_WaitOptionDrainsArchiveThenContextClosed)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03", "TASK-03"},
                  std::vector<std::string>{"AT-21"});

    // ---- 当前项目 A（有草稿——A7 的"草稿落盘完成"半区材料）。
    const fs::path dirA = m_dir / "a.rwdesign";
    project::OpenStoreResult a = createGolden(dirA);
    ASSERT_TRUE(a.store != nullptr);
    const fs::path draftFileA = saveDraftViaService(*a.store, "requirements");
    ASSERT_FALSE(draftFileA.empty()) << "夹具前置：草稿落盘失败";

    // ---- 候选项目 B（切换目标——落盘后关闭，等待被打开）。
    const fs::path dirB = m_dir / "b.rwdesign";
    core::ProjectId idB;
    {
        project::OpenStoreResult b = createGolden(dirB);
        ASSERT_TRUE(b.store != nullptr);
        idB = b.store->projectId();
        b.store->requestClose();
        b.store.reset();
    }

    DraftServiceBridge draftsPort(*a.store);
    ScriptedDecisions decisions;
    decisions.draftAnswer = DraftDisposition::Discard;  // 三选：放弃（真实删文件）
    ScriptedDrain drain;                                // 等待分支（无活动任务）

    // ---- 切换编排（等待选项）。
    workflow::CloseFlowRequest request;
    request.decisions = &decisions;
    request.drafts = &draftsPort;
    request.drain = &drain;
    request.candidatePath = dirB;
    auto outcome =
        workflow::CloseFlow::run(CloseKind::Switch, *a.store, request);

    // ---- A7 承接断言：
    // ① 等待分支排空恰好一次（shutdown+drained——在途归档完成的执行侧）。
    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(drain.waitCalls, 1);
    EXPECT_TRUE(outcome.waitedForArchiveDrain);
    // ② 存储上下文排空完成（requestClose→closed——草稿落盘完成＋在途
    //    归零＝A7 的释放时点；Closed 后写权限消失）。
    EXPECT_TRUE(a.store->closed());
    EXPECT_FALSE(a.store->writable()) << "存储上下文释放＝写权限消失（A7/SA-17）";
    // ③ 草稿按用户决策处置完成（放弃＝草稿文件删除——"草稿落盘完成"
    //    的排空定义在编排内闭合；盘面事实检查——Closed 后 DraftService
    //    读轨拒绝，文件系统检查不受限）。
    EXPECT_FALSE(fs::exists(draftFileA)) << "草稿已按用户决策处置（放弃＝删除）";
    // ④ 切换上下文＝候选验证成功后移交（SWITCH 节点——界面会话切至
    //    候选的材料：store 非空、身份一致、可写）。
    ASSERT_TRUE(outcome.candidateStore != nullptr);
    EXPECT_EQ(outcome.candidateStore->projectId().toCanonical(), idB.toCanonical());
    EXPECT_TRUE(outcome.candidateStore->writable());

    outcome.candidateStore->requestClose();
    outcome.candidateStore.reset();
}

// =====================================================================
// WF-VER-211 暂停中取消经关闭编排（PM-03/§9 排空语义——契约；观测点＝
// 任务终态）
// =====================================================================

/**
 * 协作取消分支（§7.3 CANCEL 节点）：逐/批量 requestCancel 后走取消协议
 * （取消即清理临时区——execution 取消协议承载），再排空、再关闭。
 * 编排面断言：协作取消恰好一次、免重复调度排空（取消分支已含排空）、
 * 存储上下文随后排空关闭（Proceed＋closed）＋零错误诊断（UX-03：关闭
 * 触发的取消不产生错误诊断——取消不是错误）。任务终态（Paused→Canceled
 * 等状态机观测）由执行侧取消协议承载（N4 分工——EX 单元测试域），本
 * 用例钉扎编排面对该协议的调用契约。
 */
TEST_F(CloseFlowContract, WF_VER_211_CooperativeCancelReachesDrainedClose)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-21"});

    const fs::path dirA = m_dir / "a.rwdesign";
    project::OpenStoreResult a = createGolden(dirA);
    ASSERT_TRUE(a.store != nullptr);

    ScriptedDecisions decisions;  // 无草稿——三选跳过
    ScriptedDrain drain;
    drain.active = true;  // 有运行中（非终态）任务——进入二选决策点
    drain.states = {core::TaskState::Paused};
    decisions.taskAnswer = RunningTaskDecision::CooperativeCancel;

    DraftServiceBridge draftsPort(*a.store);
    workflow::CloseFlowRequest request;
    request.decisions = &decisions;
    request.drafts = &draftsPort;
    request.drain = &drain;
    const auto outcome =
        workflow::CloseFlow::run(CloseKind::Close, *a.store, request);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_EQ(drain.cancelCalls, 1) << "协作取消恰好一次（逐任务 requestCancel→排空）";
    EXPECT_EQ(drain.waitCalls, 0) << "取消分支已含排空——免重复 shutdown";
    EXPECT_TRUE(outcome.cooperativeCancelled);
    EXPECT_FALSE(outcome.failure.has_value());
    EXPECT_TRUE(a.store->closed()) << "取消路径同样走完存储上下文排空（经归档检查点后结束——A7）";

    a.store.reset();
}

// =====================================================================
// WF-VER-212 方案分支切换零写入（PM-12——契约；观测点＝修订/字节）
// =====================================================================

/**
 * 零写入之取消路径（最强形态）：草稿已落盘→三选"取消"→Aborted→项目
 * 目录树逐文件一致（含 HEAD 字节级一致）＋修订计数不变——切换编排全程
 * 零写入（含零删除：草稿原状保留）。
 */
TEST_F(CloseFlowContract, WF_VER_212_SchemeSwitchCancelPathZeroWrite)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{"AT-21"});

    const fs::path dir = m_dir / "p.rwdesign";
    project::OpenStoreResult store = createGolden(dir);
    ASSERT_TRUE(store.store != nullptr);
    const auto tipsBefore = store.store->query().branchTips();
    const auto historyBefore =
        store.store->query().branchHistory(tipsBefore[0].id, 32);
    saveDraftViaService(*store.store, "requirements");
    const auto treeBefore = snapshotTree(dir);
    const std::string headBefore = readFileBytes(dir / "HEAD");

    DraftServiceBridge draftsPort(*store.store);
    ScriptedDecisions decisions;
    decisions.draftAnswer = DraftDisposition::Cancel;  // 三选：取消

    workflow::SchemeBranchSwitchRequest request;
    request.currentBranch = tipsBefore[0].id;
    request.targetBranch = core::BranchId::fromCanonical(
        "brn-0123456789abcdef0123456789abcdef");  // 假想目标分支（会话层对象——编排核不触达）

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(request, decisions, draftsPort);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Aborted);

    // 零写入复核（观测点＝修订/字节）：目录树逐文件一致（草稿原状——
    // 零删除）＋ HEAD 字节级一致（切换不写 HEAD）＋修订计数不变。
    const auto treeAfter = snapshotTree(dir);
    EXPECT_EQ(treeAfter, treeBefore) << "取消路径零写入（含草稿原状）";
    EXPECT_EQ(readFileBytes(dir / "HEAD"), headBefore) << "HEAD 字节不变（PM-12/§4.5）";
    const auto tipsAfter = store.store->query().branchTips();
    ASSERT_EQ(tipsAfter.size(), tipsBefore.size());
    EXPECT_EQ(tipsAfter[0], tipsBefore[0]) << "分支 tip 不变（零修订）";
    EXPECT_EQ(store.store->query().branchHistory(tipsBefore[0].id, 32).size(),
              historyBefore.size());

    store.store->requestClose();
    store.store.reset();
}

/**
 * 零写入之批准路径：无未应用草稿（三选跳过）→切换批准 Proceed→HEAD
 * 字节级一致＋分支表/tip 不变＋目录树（lock 心跳排除）逐文件一致——
 * 切换＝纯会话选择（project.md §4.5），编排核签名零存储上下文（结构性
 * 保证）＋盘面字节复核（本用例）双重承载。
 */
TEST_F(CloseFlowContract, WF_VER_212_SchemeSwitchProceedPathZeroWrite)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-12"}, std::vector<std::string>{});

    const fs::path dir = m_dir / "p.rwdesign";
    project::OpenStoreResult store = createGolden(dir);
    ASSERT_TRUE(store.store != nullptr);
    const auto tipsBefore = store.store->query().branchTips();
    const auto treeBefore = snapshotTree(dir);
    const std::string headBefore = readFileBytes(dir / "HEAD");

    DraftServiceBridge draftsPort(*store.store);  // 无草稿——三选不弹出
    ScriptedDecisions decisions;

    workflow::SchemeBranchSwitchRequest request;
    request.currentBranch = tipsBefore[0].id;
    request.targetBranch = core::BranchId::fromCanonical(
        "brn-fedcba9876543210fedcba9876543210");

    const auto outcome =
        workflow::SchemeBranchSwitchFlow::run(request, decisions, draftsPort);

    ASSERT_EQ(outcome.result, SchemeBranchSwitchOutcome::Result::Proceed);
    EXPECT_TRUE(decisions.draftPrompts.empty()) << "无草稿——零决策调用";

    // 零写入复核：HEAD 字节级一致＋tip/分支表不变＋目录树逐文件一致。
    EXPECT_EQ(readFileBytes(dir / "HEAD"), headBefore)
        << "切换批准路径不写 HEAD（HEAD 记录'最后一次提交所在分支'，非活动分支——§4.5）";
    const auto tipsAfter = store.store->query().branchTips();
    ASSERT_EQ(tipsAfter.size(), tipsBefore.size());
    EXPECT_EQ(tipsAfter[0], tipsBefore[0]);
    const auto treeAfter = snapshotTree(dir);
    EXPECT_EQ(treeAfter, treeBefore) << "批准路径零文件写入";

    store.store->requestClose();
    store.store.reset();
}

// =====================================================================
// 切换：候选验证成功才切上下文（SWITCH 节点——真实落盘双项目）
// =====================================================================

/**
 * 候选验证成功路径（§7.3 SWITCH 节点正向）：当前 A＋候选 B 均真实落盘→
 * 切换编排→候选验证（ProjectStoreFactory::open 完整构造）成功→当前
 * 存储上下文排空关闭（closed）＋候选移交（candidateStore 身份＝B、可写）
 * →Proceed——"关闭后候选验证成功才切上下文"的编排面承载。
 */
TEST_F(CloseFlowContract, SwitchCandidateSuccessClosesCurrentAndHandsOver)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    const fs::path dirA = m_dir / "a.rwdesign";
    project::OpenStoreResult a = createGolden(dirA);
    ASSERT_TRUE(a.store != nullptr);
    const fs::path dirB = m_dir / "b.rwdesign";
    project::OpenStoreResult b = createGolden(dirB);
    ASSERT_TRUE(b.store != nullptr);
    const core::ProjectId idB = b.store->projectId();
    b.store->requestClose();  // 候选关闭（等编排核重新打开）
    b.store.reset();

    ScriptedDecisions decisions;  // 无草稿无任务——直连候选验证
    ScriptedDrain drain;
    DraftServiceBridge draftsPort(*a.store);

    workflow::CloseFlowRequest request;
    request.decisions = &decisions;
    request.drafts = &draftsPort;
    request.drain = &drain;
    request.candidatePath = dirB;
    auto outcome =
        workflow::CloseFlow::run(CloseKind::Switch, *a.store, request);

    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Proceed);
    EXPECT_TRUE(a.store->closed()) << "切换成功＝当前存储上下文排空关闭";
    ASSERT_TRUE(outcome.candidateStore != nullptr);
    EXPECT_EQ(outcome.candidateStore->projectId().toCanonical(), idB.toCanonical());
    EXPECT_TRUE(outcome.candidateStore->writable());

    outcome.candidateStore->requestClose();
    outcome.candidateStore.reset();
    a.store.reset();
}

/**
 * 候选验证失败不动当前项目（SWITCH 节点括注——真实损坏注入）：候选 B
 * 的 HEAD 改写为垃圾字节→切换 Failed（UX-03 呈现）→当前 A 原状（可写/
 * 身份/查询面照常＋目录树逐文件一致）＋候选 B 原文件零写入——编排序
 * 保证（候选验证先于 requestClose）在真实存储上复核。
 */
TEST_F(CloseFlowContract, SwitchCandidateCorruptFailsAndCurrentUntouched)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-03"}, std::vector<std::string>{"AT-20"});

    const fs::path dirA = m_dir / "a.rwdesign";
    project::OpenStoreResult a = createGolden(dirA);
    ASSERT_TRUE(a.store != nullptr);
    ASSERT_TRUE(a.store->writable());
    const core::ProjectId idA = a.store->projectId();
    const auto tipsBefore = a.store->query().branchTips();

    // 候选 B：落盘→关闭→HEAD 注入垃圾字节（损坏注入——WF-VER-206 同型）。
    const fs::path dirB = m_dir / "b.rwdesign";
    {
        project::OpenStoreResult b = createGolden(dirB);
        ASSERT_TRUE(b.store != nullptr);
        b.store->requestClose();
        b.store.reset();
    }
    {
        std::ofstream vandal(dirB / "HEAD", std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(vandal.is_open());
        vandal << "\x01\x02NOT-A-HEAD-RECORD\xff\xfe";
    }
    const auto treeABefore = snapshotTree(dirA);
    const auto treeBBefore = snapshotTree(dirB);

    ScriptedDecisions decisions;
    ScriptedDrain drain;
    DraftServiceBridge draftsPort(*a.store);

    workflow::CloseFlowRequest request;
    request.decisions = &decisions;
    request.drafts = &draftsPort;
    request.drain = &drain;
    request.candidatePath = dirB;
    auto outcome =
        workflow::CloseFlow::run(CloseKind::Switch, *a.store, request);

    // 失败呈现（UX-03）：Failed＋三字段齐备＋候选 store 空。
    ASSERT_EQ(outcome.result, CloseFlowOutcome::Result::Failed);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_NE(outcome.failure->cause.find("HEAD"), std::string::npos)
        << "失败原因应定位到损坏对象（HEAD）——原文: " << outcome.failure->cause;
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());
    EXPECT_EQ(outcome.candidateStore, nullptr);

    // 不动当前项目（AT-20）：存储上下文 Active 原状＋目录树逐文件一致。
    EXPECT_FALSE(a.store->closed()) << "候选验证失败——当前存储上下文未被关闭（编排序保证）";
    EXPECT_TRUE(a.store->writable());
    EXPECT_EQ(a.store->projectId().toCanonical(), idA.toCanonical());
    const auto tipsAfter = a.store->query().branchTips();
    ASSERT_EQ(tipsAfter.size(), tipsBefore.size());
    EXPECT_EQ(tipsAfter[0], tipsBefore[0]);
    EXPECT_EQ(snapshotTree(dirA), treeABefore) << "当前项目目录树零变化";

    // 候选 B 原文件零写入（打开路径零写——失败呈现不是修复）。
    EXPECT_EQ(snapshotTree(dirB), treeBBefore);

    a.store->requestClose();
    a.store.reset();
}

}  // namespace
