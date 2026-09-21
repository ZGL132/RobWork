/**
 * @file   InteractionTaskContractTest.cpp
 * @brief  UI-T13 契约层用例（§12.1 第二层——跨单元契约面，可控替身
 *         承载）：①C-6 外翻形态的结构自证——CommandInteractionBridge
 *         实现 ui 自有 IUiCommandInteraction 端口（O-31 虚派发），探针
 *         任意线程可读；②Marshal 线程契约——呈现器只在 UI 线程被调用
 *         （§9.2/§3.4 M-1）；③C-8 呈现面端口协议——控制请求按到达序
 *         送达＋轻查询半区（progress）＋即发即忘；④C-9"经 project
 *         间接读"的装配缝——IUiFindingQueryPort 供 FindingRecord、桥经
 *         补全回调消费（L5 装配形态的契约投影）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T13.json verify 行（ui_contract_test
 *     目标——"ICommandInteraction/ITaskScheduler 桩对接属 §12.1 契约
 *     测试面"）＋acceptance 2/3/5；
 *   - units/ui.md §9.2/§9.4/§10.7/§10.8、§3.1（O-31：产品面零对端
 *     链接/include，测试以可控替身承载）、§3.4（线程模型）；
 *   - 先例：SessionContractTest.cpp/DraftContractTest.cpp 的替身协议面。
 */

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>

#include <sdurws/ird/diagnostics/Confirmable.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/CommandInteractionBridge.hpp>
#include <sdurws/ird/ui/IDiagnosticPresentationModel.hpp>
#include <sdurws/ird/ui/ITaskPresentationModel.hpp>
#include <sdurws/ird/ui/UiPorts.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

using namespace sdurws::ird;
using ui::CommandInteractionBridge;
using ui::ConfirmDialogData;
using ui::ConfirmDialogOutcome;
using ui::ConfirmDialogResolution;
using ui::TaskViewProjection;
using ui::UiTaskAck;

// =====================================================================
// 测试替身
// =====================================================================

/// 确认对话呈现器替身（记录调用线程＋可编程决议）。
class ContractPresenter final : public ui::IUiConfirmDialogPresenter {
public:
    ConfirmDialogOutcome programmed = ConfirmDialogOutcome::Confirmed;
    std::vector<ConfirmDialogData> seen;   ///< 收到的装配值（补全缝断言面）
    std::atomic<int> entered{0};
    const QThread* callThread = nullptr;

    ConfirmDialogResolution showConfirmDialog(const ConfirmDialogData& data) override
    {
        callThread = QThread::currentThread();
        seen.push_back(data);
        entered.fetch_add(1);
        ConfirmDialogResolution resolution;
        resolution.outcome = programmed;
        return resolution;
    }
    void noteInputChanged() override {}
};

/// C-8 呈现面端口替身（协议面：到达序记录＋轻查询半区）。
class ContractTaskPort final : public ui::IUiTaskPresentationPort {
public:
    std::vector<TaskViewProjection> rows;
    std::vector<std::string> arrivalOrder;   ///< 控制请求到达序（协议面证据）
    UiTaskAck nextAck;                        ///< 可编程应答

    std::vector<TaskViewProjection>
    tasksByProject(const core::ProjectId&) const override { return rows; }
    std::optional<TaskViewProjection>
    task(const core::TaskIdentity&) const override { return std::nullopt; }
    std::optional<ui::TaskProgressProjection>
    progress(const core::TaskIdentity& id) const override
    {
        // 轻查询半区（§9.4"轮询 progress(taskId)"的端口契约面——从快照
        // 折叠返回，协议有效性即"字段可独立消费"）。
        for (const auto& row : rows) {
            if (row.identity == id) {
                return row.progress;
            }
        }
        return std::nullopt;
    }
    UiTaskAck requestCancel(const core::TaskIdentity&) override
    {
        arrivalOrder.emplace_back("cancel");
        return nextAck;
    }
    UiTaskAck requestPause(const core::TaskIdentity&) override
    {
        arrivalOrder.emplace_back("pause");
        return nextAck;
    }
    UiTaskAck requestResume(const core::TaskIdentity&) override
    {
        arrivalOrder.emplace_back("resume");
        return nextAck;
    }
    UiTaskAck requestForceTerminate(const core::TaskIdentity&) override
    {
        arrivalOrder.emplace_back("force");
        return nextAck;
    }
};

/// C-9 确认投影呈现半区替身（"经 project 间接读"的注入面）。
class ContractFindingQuery final : public ui::IUiFindingQueryPort {
public:
    std::vector<diagnostics::FindingRecord> records;
    mutable int calls = 0;
    std::vector<diagnostics::FindingRecord> pendingConfirmations() const override
    {
        ++calls;
        return records;
    }
};

/// 测试环境（确定性时钟＋固定主体）。
struct ContractHarness {
    /// 手动时钟（confirmedAtUtc 注入——确定性断言）。
    struct ManualClock final : diagnostics::IClock {
        std::chrono::system_clock::time_point m_now{std::chrono::seconds{777}};
        std::chrono::system_clock::time_point nowUtc() const override { return m_now; }
    };

    std::shared_ptr<ContractPresenter> presenter = std::make_shared<ContractPresenter>();
    ManualClock clock;
    core::ProjectId project = core::ProjectId::generate();

    CommandInteractionBridge::Deps bridgeDeps()
    {
        CommandInteractionBridge::Deps deps;
        deps.presenter = presenter;
        deps.clock = &clock;
        deps.principalProvider = [] { return std::string("contract-user"); };
        return deps;
    }

    static core::ConfirmableFinding makeFinding()
    {
        core::ComparativeFields comparison;
        comparison.actual.quantity = core::SourcedValue<double>::provided(
            1.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.actual.unit = core::UnitToken::find("m").value();
        comparison.expected.quantity = core::SourcedValue<double>::provided(
            2.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        comparison.expected.unit = core::UnitToken::find("m").value();
        return core::ConfirmableFinding::make(core::DiagnosticRecord::make(
            "POLICY-THRESHOLD-OUT-OF-RANGE", core::ObjectId::generate(),
            std::nullopt, std::nullopt, "上下文", "原因", "建议动作", comparison));
    }

    static void pumpUntil(const std::function<bool()>& predicate)
    {
        const int spinGuard = 20000;
        for (int i = 0; i < spinGuard && !predicate(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::yield();
        }
    }
};

// =====================================================================
// O-31 虚派发结构自证（C-6 外翻形态——acceptance 5）
// =====================================================================

/** O-31：Bridge 实现 ui 自有交互回调端口（类型关系＋基面虚派发＋探针
 *  任意线程可读）——L5 适配器委托面的结构前提。 */
TEST(InteractionTaskContractTest, BridgeImplementsUiInteractionPort_O31)
{
    IRD_TEST_INFO("O-31", {}, std::nullopt);
    // 类型关系：C-6 端口由桥实现（外翻形态的类型层证据——ui 头零对端
    // 类型，端口即委托目标）。
    static_assert(std::is_base_of_v<ui::IUiCommandInteraction, CommandInteractionBridge>,
                  "C-6 外翻：CommandInteractionBridge 必须实现 ui 自有 IUiCommandInteraction");

    ContractHarness h;
    auto bridge = std::make_unique<CommandInteractionBridge>(h.bridgeDeps());

    // 基面虚派发：探针经 IUiCommandInteraction& 可读（L5 适配器的委托面）。
    ui::IUiCommandInteraction& asPort = *bridge;
    EXPECT_TRUE(asPort.isAlive());

    // 探针任意线程可读（§9.2"isAlive()……任意线程（原子读）"）。
    std::atomic<bool> remoteRead{false};
    std::thread reader([&] {
        remoteRead = asPort.isAlive();   // 任意线程读——值可取即契约
    });
    reader.join();
    EXPECT_TRUE(remoteRead.load());

    // 拆除经派生类型翻转、基面可见（会话控制器拆除路径的接线前提）。
    bridge->markSessionDismantled();
    EXPECT_FALSE(asPort.isAlive());
}

/** §9.2/§3.4 M-1：呈现器只在 UI 线程被调用（Marshal 线程契约）。 */
TEST(InteractionTaskContractTest, PresenterInvokedOnUiThreadOnly_M_1)
{
    IRD_TEST_INFO("P-PR-7", {}, std::nullopt);
    ContractHarness h;
    h.presenter->programmed = ConfirmDialogOutcome::Confirmed;
    auto bridge = std::make_unique<CommandInteractionBridge>(h.bridgeDeps());

    std::optional<std::vector<core::ConfirmationCredential>> outcome;
    std::thread commandThread([&] {
        outcome = bridge->requestConfirmations({ContractHarness::makeFinding()});
    });
    ContractHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    commandThread.join();

    EXPECT_EQ(h.presenter->entered.load(), 1);
    EXPECT_EQ(h.presenter->callThread, QThread::currentThread());   // UI 线程
    ASSERT_TRUE(outcome.has_value());   // 决议凭据回传（Marshal 闭环）
    EXPECT_EQ((*outcome)[0].principal, "contract-user");
    EXPECT_EQ((*outcome)[0].confirmedAtUtc, h.clock.m_now);
}

// =====================================================================
// C-8 呈现面端口协议（§9.4/§10.7——acceptance 3）
// =====================================================================

/** C-8：控制请求按到达序送达端口；轻查询半区 progress 可独立消费。 */
TEST(InteractionTaskContractTest, TaskPortControlOrderAndProgressProtocol)
{
    IRD_TEST_INFO("TASK-01", {}, std::nullopt);
    ContractTaskPort port;
    port.nextAck.accepted = true;

    // 身份组自洽（五元组 project 段＝会话项目——tasksByProject 等值键）。
    const core::ProjectId projectId = core::ProjectId::generate();
    TaskViewProjection snap;
    snap.identity.project = projectId;
    snap.identity.run = core::RunId::generate();
    snap.state = core::TaskState::Running;
    ui::TaskProgressProjection progress;
    progress.percent = 55;
    snap.progress = progress;
    port.rows = {snap};

    ui::TaskPresentationDeps deps;
    deps.taskPort = &port;
    auto model = ui::createTaskPresentationModel(std::move(deps));
    model->attachProject(projectId);
    model->refresh();

    // 到达序契约（§10.7 后置行"控制请求按到达序送达"）。
    (void)model->requestCancel(snap.identity);
    (void)model->requestPause(snap.identity);
    (void)model->requestResume(snap.identity);
    EXPECT_EQ((port.arrivalOrder), (std::vector<std::string>{"cancel", "pause", "resume"}));

    // 轻查询半区：progress(task) 独立可消费（§9.4"轮询 progress(taskId)"
    // 的端口命名锚——模型主路径用整行快照，端口契约面保持完整）。
    const auto light = port.progress(snap.identity);
    ASSERT_TRUE(light.has_value());
    EXPECT_EQ(light->percent, 55);
}

/** 即发即忘契约：控制请求返回即完成（ui 零阻塞等待——NFR-PERF-02）。 */
TEST(InteractionTaskContractTest, ControlRequestsAreFireAndForget_NFR_PERF_02)
{
    IRD_TEST_INFO("TASK-01", {}, std::nullopt);
    ContractTaskPort port;
    port.nextAck.accepted = true;

    const core::ProjectId projectId = core::ProjectId::generate();
    TaskViewProjection snap;
    snap.identity.project = projectId;
    snap.identity.run = core::RunId::generate();
    snap.state = core::TaskState::Running;
    port.rows = {snap};

    ui::TaskPresentationDeps deps;
    deps.taskPort = &port;
    auto model = ui::createTaskPresentationModel(std::move(deps));
    model->attachProject(projectId);
    model->refresh();

    // Ack 即返（无等待 API——接口面不存在"等待任务完成"形态；收敛归
    // 状态事件/轮询回看，§10.7 副作用行）。
    const auto ack = model->requestCancel(snap.identity);
    EXPECT_TRUE(ack.accepted);
    EXPECT_EQ(port.arrivalOrder.size(), 1u);   // 端口同步受理记录
}

// =====================================================================
// C-9"经 project 间接读"装配缝（§9.2/§10.8——acceptance 2）
// =====================================================================

/** C-9 装配缝投影：FindingQueryPort 供 FindingRecord、桥经补全回调
 *  消费（L5 装配形态的契约投影——ui 侧零服务端接口直调）。 */
TEST(InteractionTaskContractTest, FindingQueryPortFeedsBridgeEnrichment)
{
    IRD_TEST_INFO("SA-15", {}, std::nullopt);
    ContractHarness h;
    ContractFindingQuery query;

    // 同一发现贯穿记录与请求（记录与发现同 record——diagnostics create
    // 语义的投影：finding.record 即服务端记录的底层发现）。
    const auto finding = ContractHarness::makeFinding();

    // 端口持有"经 project 间接读"的记录（L5 适配 pendingFor 的投影）；
    // 补全回调按记录内容匹配。
    diagnostics::FindingRecord record;
    record.finding = finding;
    record.baseRevisionId = core::RevisionId::generate();
    record.sourceCommandType = "policy.update";
    record.subjectScope = {core::ObjectId::generate()};
    query.records = {record};

    CommandInteractionBridge::Deps deps = h.bridgeDeps();
    deps.enrich = [&query](const core::ConfirmableFinding& candidate0)
        -> std::optional<diagnostics::FindingRecord> {
        // "经 project 间接读"→按底层发现匹配（ui 不直接调服务端接口——
        // 端口是唯一通路，§9.2 规则表）。
        for (const auto& candidate : query.pendingConfirmations()) {
            if (candidate.finding == candidate0) {
                return candidate;
            }
        }
        return std::nullopt;
    };
    auto bridge = std::make_unique<CommandInteractionBridge>(std::move(deps));

    h.presenter->programmed = ConfirmDialogOutcome::Rejected;
    std::thread commandThread([&] {
        (void)bridge->requestConfirmations({finding});
    });
    ContractHarness::pumpUntil([&] { return h.presenter->entered.load() >= 1; });
    commandThread.join();

    EXPECT_GE(query.calls, 1);   // 补全消费了端口（间接读路径证据）
    ASSERT_EQ(h.presenter->seen.size(), 1u);
    EXPECT_EQ(h.presenter->seen[0].items[0].baseRevisionCanonical.rfind("rev-", 0), 0);
    EXPECT_EQ(h.presenter->seen[0].devFoldLines.size(), 1u);   // Dev 折叠区只带命令类型
}

}  // namespace
