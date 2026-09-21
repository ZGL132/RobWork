/**
 * @file   PeerBridgeContractTest.cpp
 * @brief  UI-T14 对端公共头桥接契约用例组（§12.1 第二层行"跨单元契约：
 *         对接 project/execution/diagnostics 公共头与桩实现（testkit
 *         FaultInterceptor 伪造 ITaskScheduler/ICommandInteraction）"
 *         的落地面）：O-31 测试面直链登记、C-6 交互回调的外翻桥接形状、
 *         ITaskScheduler 快照投影到 ui 任务呈现端口的适配桥、脱敏配置
 *         端口对 diagnostics 值面的直用钉。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T14.json acceptance 3（O-31 处置——
 *     已裁决 2026-09-19 随整链放行：ui_contract_test 目标链接 project/
 *     execution/diagnostics＋testkit 属测试面契约测试既定形态〔ui.md
 *     v0.4 §3.1 行，F-228 一并消解〕；产品面守卫不变；契约测试以
 *     testkit FaultInterceptor 伪造对端接口、经 ui 自有端口适配桥接，
 *     对端类型形状漂移由测试面捕获）；
 *   - units/ui.md §3.1（sdurws_ird_ui_contract_test 目标行——直链面登记）、
 *     §12.1（第二层测试目标）、§12.3 通用判据（fault 注入用
 *     FaultInterceptor/FaultPlan；不使用固定 sleep 判据）；
 *   - units/testkit.md §6.4（Fault.hpp 接缝原则：fake 实现被测单元自有
 *     接口，FaultInterceptor 把"第 N 次命中触发"的计划接到接口方法流）、
 *     §2.4 T-1（testkit 仅测试目标可链）；
 *   - units/project.md §5.3.3（ICommandInteraction 冻结点——P-PR-7"对端
 *     单侧冻结互为起点不私改"）；
 *   - 先例：SessionContractTest.cpp（O-31 虚派发结构自证）、
 *     InteractionTaskContractTest.cpp（C-6/C-8/C-9 端口行为面）。
 *
 * 为什么"形状漂移由测试面捕获"能成立（acceptance 3 括注的机制解释）：
 * 本 TU 同时 include 对端公共头与 ui 公共头，桥接 adapter 以 `override`
 * 实现对端接口、以字段访问消费对端值类型、以成员函数指针 static_assert
 * 钉住 ui/对端两侧签名镜像——project/execution/diagnostics 任何一侧的
 * 类型形状变化（字段改名/删除、虚签名变更）都会让本 TU 编译失败，而
 * 编译失败发生在测试面＝发现点前移到集成前（这正是 O-31 裁决保留该
 * 测试面直链的目的）。
 *
 * 线程模型：单线程（gtest 串行纪律）；QCoreApplication 由目标 main 构造
 * （本文件用例不泵事件——行为面归 InteractionTaskContractTest）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/project/CommandService.hpp>   // project::ICommandInteraction（C-6 对端冻结点）
#include <sdurws/ird/execution/Errors.hpp>         // execution::ExecutionError/ExecutionErrorCode
#include <sdurws/ird/execution/Scheduler.hpp>      // execution::ITaskScheduler（§12.1 具名对端）
#include <sdurws/ird/execution/TaskTypes.hpp>      // execution::TaskSnapshot/ProgressReport/枚举
#include <sdurws/ird/diagnostics/Redaction.hpp>    // diagnostics::RedactionPolicy（表内登记边值面）
#include <sdurws/ird/ui/UiPorts.hpp>               // ui 自有端口（IUiCommandInteraction 等）
#include <sdurws/ird/testkit/Fault.hpp>            // FaultPlan/FaultInterceptor（§6.4）

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <filesystem>
#include <fstream>

namespace {

using namespace sdurws::ird;
using sdurws::ird::testkit::FaultInterceptor;
using sdurws::ird::testkit::FaultPlan;
using sdurws::ird::testkit::FaultTrigger;

// =====================================================================
// CMake 链接面文本工具（与 LinkageContractTest.cpp 同款小工具——各 TU
// 自持不共享，NFR-MNT-04 同案；本组只需只读截取）
// =====================================================================

/// ui 单元树根（industrialrobot 目录——IRD_UI_UNIT_ROOT 注入；规范化
/// 消除尾部 ".." 段，LinkageContractTest 同款口径）。
const std::filesystem::path& unitRoot()
{
    static const std::filesystem::path dir =
        std::filesystem::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    return dir;
}

/// 全文读取；读失败显性失败（不留"读不到＝零命中"的假阳性通道）。
std::string readFile(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        ADD_FAILURE() << "无法读取: " << file.string();
        return {};
    }
    return std::string{std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>()};
}

/// 截取 target_link_libraries(<target> 起始的括号块文本（找不到返回空串）。
/// 目标名后带一个空白字符再匹配——"sdurws_ird_ui" 是 "sdurws_ird_ui_test"
/// 的前缀，必须以词边界区分（LinkageContractTest 同款前提）。
std::string extractLinkBlock(const std::string& cmakeText, const std::string& target)
{
    const std::string key = "target_link_libraries(" + target + " ";
    const std::size_t begin = cmakeText.find(key);
    if (begin == std::string::npos) { return {}; }
    const std::size_t openParen = cmakeText.find('(', begin);
    const std::size_t end = cmakeText.find(')', openParen);
    if (openParen == std::string::npos || end == std::string::npos) { return {}; }
    return cmakeText.substr(begin, end - begin + 1);
}

// =====================================================================
// 可比较型发现工厂（与 CommandInteractionBridgeTest.cpp 同款构造——
// C-1：comparison 必在；本 TU 自持副本，不跨 TU 共享 fixture）
// =====================================================================

/// 造一个比较型可确认发现（码值任意——本套件只关心形状与透传）。
core::ConfirmableFinding makeFinding(const char* code)
{
    core::ComparativeFields comparison;
    comparison.actual.quantity = core::SourcedValue<double>::provided(
        42.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.actual.unit = core::UnitToken::find("N*m").value();
    comparison.expected.quantity = core::SourcedValue<double>::provided(
        40.0, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
    comparison.expected.unit = core::UnitToken::find("N*m").value();
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        code, core::ObjectId::generate(), std::string("obj-").append(32, 'b'),
        std::nullopt, "关节力矩超限", "实际力矩 42 N·m 超过阈值 40 N·m",
        "确认继续或修正输入", comparison);
    return core::ConfirmableFinding::make(record);
}

// =====================================================================
// O-31 测试面直链登记（acceptance 3 第一半：链接面落位＋产品面守卫不变）
// =====================================================================

/**
 * O-31 测试面直链契约：sdurws_ird_ui_contract_test 的链接声明包含
 * project/execution/diagnostics＋testkit＋被测目标五者（ui.md §3.1 该
 * 目标行 v0.4 原文"落位直链面"的落位证据——F-228 消解面）；同时产品库
 * sdurws_ird_ui 的链接块仍零 project/execution（产品面守卫不变——O-31
 * 常驻红线，acceptance 3 括注原文）。include 面守卫（产品源码零对端
 * 头）由 BuildRedLineTest 的 NoCrossUnitInclude_O31_UI_BUILD 承载，本
 * 用例不重复。
 */
TEST(PeerBridge, ContractTestDirectLinkFaceRegistered_O31_UI_T14)
{
    IRD_TEST_INFO("NFR-MNT-01", {"UX-13"}, std::nullopt);
    const std::string cmake = readFile(unitRoot() / "ui" / "CMakeLists.txt");
    ASSERT_FALSE(cmake.empty()) << "无法读取 ui/CMakeLists.txt";

    // 测试面：五条链接必须在位（缺一＝O-31 裁决的测试面承载残缺——
    // 对端公共头 include 无链接库支撑时形状漂移即使编译通过也可能在
    // 链接期才暴露，直链把发现点固定在构建期）。
    const std::string contractLink =
        extractLinkBlock(cmake, "sdurws_ird_ui_contract_test");
    ASSERT_FALSE(contractLink.empty()) << "未找到 contract_test 的链接声明块";
    EXPECT_NE(contractLink.find("sdurws_ird_project"), std::string::npos)
        << "contract_test 未直链 sdurws_ird_project（C-6 对端形状承载缺失）";
    EXPECT_NE(contractLink.find("sdurws_ird_execution"), std::string::npos)
        << "contract_test 未直链 sdurws_ird_execution（ITaskScheduler 形状承载缺失）";
    EXPECT_NE(contractLink.find("sdurws_ird_diagnostics"), std::string::npos)
        << "contract_test 未直链 sdurws_ird_diagnostics（表内登记边值面缺失）";
    EXPECT_NE(contractLink.find("sdurws_ird_testkit"), std::string::npos)
        << "contract_test 未链 testkit（FaultInterceptor/RecordListener 缺失）";
    EXPECT_NE(contractLink.find("sdurws_ird_ui"), std::string::npos)
        << "contract_test 未链被测目标 sdurws_ird_ui";

    // 产品面：链接块仍零对端单元＋零 testkit（O-31 常驻——本任务的直链
    // 只许落在测试目标，产品库链接面漂移即违红线）。目标名传"sdurws_ird_ui"
    // 本体即可——helper 已用尾随空格做词边界（区分 _test 前缀族）。
    const std::string productLink = extractLinkBlock(cmake, "sdurws_ird_ui");
    ASSERT_FALSE(productLink.empty()) << "未找到 sdurws_ird_ui 的链接声明块";
    for (const auto* forbidden : {"sdurws_ird_project", "sdurws_ird_execution",
                                  "sdurws_ird_evidence", "sdurws_ird_policy",
                                  "sdurws_ird_runtime", "sdurws_ird_testkit"}) {
        EXPECT_EQ(productLink.find(forbidden), std::string::npos)
            << "产品库 sdurws_ird_ui 链接声明出现 " << forbidden
            << "（O-31 产品面守卫被破坏——直链仅属测试目标）";
    }
}

// =====================================================================
// C-6 外翻桥接（project::ICommandInteraction → ui::IUiCommandInteraction）
// =====================================================================

// 形状镜像钉（P-PR-7"形状逐字镜像"的编译期承载）：ui 自有交互端口与
// project 冻结点的 requestConfirmations 成员函数指针类型必须同构——
// 签名、返回值、const 性任一漂移即编译失败（对端单侧冻结的测试面
// 反向钉；两断言分别钉两侧，任何一侧改动都逃不过）。
static_assert(std::is_same_v<
              decltype(&ui::IUiCommandInteraction::requestConfirmations),
              std::optional<std::vector<core::ConfirmationCredential>> (
                  ui::IUiCommandInteraction::*)(
                  const std::vector<core::ConfirmableFinding>&)>,
              "ui::IUiCommandInteraction::requestConfirmations 形状漂移"
              "（必须逐字镜像 project.md §5.3.3 冻结点——P-PR-7）");
static_assert(std::is_same_v<
              decltype(&project::ICommandInteraction::requestConfirmations),
              std::optional<std::vector<core::ConfirmationCredential>> (
                  project::ICommandInteraction::*)(
                  const std::vector<core::ConfirmableFinding>&)>,
              "project::ICommandInteraction::requestConfirmations 冻结点"
              "漂移（P-PR-7 单侧冻结被对端改动——须按影响面增量同步）");

/// ui 自有交互端口的"真实现"替身：记录入参并按编程值应答（记录型——
/// 桥接透传断言的观测面）。
class RecordingInteraction final : public ui::IUiCommandInteraction {
public:
    bool alive = true;                                        ///< isAlive 应答值
    std::vector<core::ConfirmableFinding> lastFindings;       ///< 最近一次入参快照
    std::optional<std::vector<core::ConfirmationCredential>> next =
        std::vector<core::ConfirmationCredential>{};          ///< 下次应答（可编程）

    [[nodiscard]] bool isAlive() const override { return alive; }

    std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(const std::vector<core::ConfirmableFinding>& findings) override
    {
        lastFindings = findings;   // 透传观测：入参逐字到达 ui 侧
        return next;
    }
};

/**
 * L5 适配器形态的测试侧镜像（O-31 v0.4 C-6 行"L5 适配器实现
 * project::ICommandInteraction，委托 ui 自有交互回调端口"）：adapter
 * 实现对端接口，方法内先经 FaultInterceptor 裁决（命中即注入"会话
 * 失效/整体拒绝"路径——返回 nullopt，对端按不产生修订处置），未命中
 * 转调 real。override＋对端头 include＝对端虚签名漂移的编译期捕获点。
 */
class InteractionL5Adapter final : public project::ICommandInteraction {
public:
    // mutable：isAlive 是 const 探针（直通无故障缝——存活判定不做注入，
    // 故障缝选在 requestConfirmations——§5.3.3 的失效路径正是由它表达）。
    mutable FaultInterceptor<ui::IUiCommandInteraction> fault;

    InteractionL5Adapter(std::shared_ptr<ui::IUiCommandInteraction> real,
                         FaultPlan plan)
        : fault(std::move(real), std::move(plan)) {}

    [[nodiscard]] bool isAlive() const override
    {
        return fault.real()->isAlive();   // 探针直通（镜像语义——不改义）
    }

    std::optional<std::vector<core::ConfirmationCredential>>
    requestConfirmations(const std::vector<core::ConfirmableFinding>& findings) override
    {
        // 故障点标识按 testkit §6.4 约定 <unit>/<接口>/<动作>。
        if (fault.shouldFire("ui/command-interaction/request-confirmations",
                             ++calls_)) {
            return std::nullopt;   // 注入失效/拒绝路径（对端按无修订处置）
        }
        return fault.real()->requestConfirmations(findings);
    }

private:
    mutable int calls_ = 0;   ///< requestConfirmations 命中计数（const 修约）
};

/** C-6 外翻桥接：透传、故障注入与探针镜像（acceptance 3 具名面——
 *  §12.1 第二层行"FaultInterceptor 伪造 ICommandInteraction"）。 */
TEST(PeerBridge, CommandInteractionL5AdapterBridgesUiPort_UI_T14)
{
    IRD_TEST_INFO("SA-15", {"P-PR-7"}, std::nullopt);
    auto real = std::make_shared<RecordingInteraction>();

    // 故障计划：第 1 次 requestConfirmations 命中注入失效路径。
    FaultPlan plan;
    plan.triggers.push_back(
        FaultTrigger{"ui/command-interaction/request-confirmations", 1});
    InteractionL5Adapter adapter(real, plan);

    const std::vector<core::ConfirmableFinding> findings{makeFinding("POLICY-THRESHOLD-OUT-OF-RANGE")};

    // 第 1 次：计划命中 → adapter 注入 nullopt（不透传——ui 端口未被调）。
    const auto faulted = adapter.requestConfirmations(findings);
    EXPECT_FALSE(faulted.has_value()) << "故障未按计划注入（第 1 次应命中）";
    EXPECT_TRUE(real->lastFindings.empty()) << "命中路径泄漏到了 ui 端口";

    // 命中日志：恰一条，故障点与命中序号如实记录（FaultLog 观测面）。
    const auto log = adapter.fault.log();
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].first, "ui/command-interaction/request-confirmations");
    EXPECT_EQ(log.hits[0].second, 1u);

    // 第 2 次：未命中 → 透传。凭据一一对应（SA-15——实现侧只交凭据），
    // 入参逐字到达（findings 恒等透传——适配器不得增删改义）。
    core::ConfirmationCredential credential;
    credential.principal = "tester";
    credential.confirmedAtUtc = std::chrono::system_clock::now();
    real->next = std::vector<core::ConfirmationCredential>{credential};
    const auto passed = adapter.requestConfirmations(findings);
    ASSERT_TRUE(passed.has_value());
    ASSERT_EQ(passed->size(), findings.size()) << "凭据与 findings 不一一对应";
    EXPECT_EQ((*passed)[0], credential);
    ASSERT_EQ(real->lastFindings.size(), findings.size());
    EXPECT_EQ(real->lastFindings[0].record.code, findings[0].record.code)
        << "透传改变了发现内容（适配器改义——P-PR-7 违约）";

    // 探针镜像：isAlive 逐字反映 ui 端口存活位（镜像语义——不改义）。
    EXPECT_TRUE(adapter.isAlive());
    real->alive = false;
    EXPECT_FALSE(adapter.isAlive());
}

// =====================================================================
// ITaskScheduler 快照投影桥（execution 对端 → ui::IUiTaskPresentationPort）
// =====================================================================

/// 可编程快照调度器（对端"真实现"替身——实现 execution::ITaskScheduler
/// 全部纯虚；登记 run→TaskId 侧表供 ui 五元组寻址的映射半区）。
class SnapshotScheduler final : public execution::ITaskScheduler {
public:
    std::vector<execution::TaskSnapshot> rows;                 ///< tasksByProject 返回值
    std::map<std::string, execution::TaskId> byRunCanonical;   ///< run 规范文本→TaskId
    mutable int tasksByProjectCalls = 0;                       ///< 清单查询计数（const 查询内推进）

    execution::SubmitResult submit(execution::TaskSubmission&&) override
    {
        // 本套件不消费提交面（呈现桥的查询半区）——按对端契约返回拒绝
        // 形态（accepted=false 无诊断），不做任何登记。
        return {};
    }

    std::optional<execution::TaskSnapshot>
    tryTask(execution::TaskId task) const noexcept override
    {
        for (const auto& snap : rows) {
            if (snap.taskId == task) {
                return snap;   // 深拷贝投影（§4.2"查询经快照拷贝"）
            }
        }
        return std::nullopt;
    }

    std::vector<execution::TaskSnapshot>
    tasksByProject(core::ProjectId) const override
    {
        ++tasksByProjectCalls;
        return rows;
    }

    void setResourceBudget(const execution::ResourceBudget&) override {}
    void shutdown(execution::DrainPolicy) override {}
    bool drained() const noexcept override { return true; }
};

/// 故障注入壳（testkit §6.4 用法形态）：实现同一对端接口，方法内先
/// shouldFire 裁决（命中即抛接口声明的错误类型 ExecutionError），未命中
/// 转调 real——"第 N 次命中触发"的计划接到接口方法流。
class FaultingScheduler final : public execution::ITaskScheduler {
public:
    // mutable：shouldFire 推进命中日志，而清单查询是 const 方法（测试
    // 串行纪律下无并发——Fault.hpp 线程约束）。
    mutable FaultInterceptor<execution::ITaskScheduler> fault;

    FaultingScheduler(std::shared_ptr<execution::ITaskScheduler> real,
                      FaultPlan plan)
        : fault(std::move(real), std::move(plan))
    {
    }

    execution::SubmitResult submit(execution::TaskSubmission&& submission) override
    {
        if (fault.shouldFire("execution/itask-scheduler/submit", ++submitCalls_)) {
            throw execution::ExecutionError(
                execution::ExecutionErrorCode::ContextClosed,
                "execution/fault: closed-submit-simulated");
        }
        return fault.real()->submit(std::move(submission));
    }

    std::optional<execution::TaskSnapshot>
    tryTask(execution::TaskId task) const noexcept override
    {
        // noexcept 方法内不设故障缝（抛出即违约 noexcept 契约）——故障缝
        // 选在 tasksByProject（清单查询是呈现桥的主路径）。
        return fault.real()->tryTask(task);
    }

    std::vector<execution::TaskSnapshot>
    tasksByProject(core::ProjectId project) const override
    {
        if (fault.shouldFire("execution/itask-scheduler/tasks-by-project",
                             ++listCalls_)) {
            throw execution::ExecutionError(
                execution::ExecutionErrorCode::ContextClosed,
                "execution/fault: tasks-by-project-simulated");
        }
        return fault.real()->tasksByProject(project);
    }

    void setResourceBudget(const execution::ResourceBudget& budget) override
    {
        fault.real()->setResourceBudget(budget);
    }

    void shutdown(execution::DrainPolicy policy) override
    {
        fault.real()->shutdown(policy);
    }

    bool drained() const noexcept override { return fault.real()->drained(); }

private:
    mutable int submitCalls_ = 0;   ///< submit 命中计数（const 修约）
    mutable int listCalls_ = 0;     ///< tasksByProject 命中计数
};

/// 归档阶段枚举 → 呈现 token（execution 词表投影——空串＝无归档语义，
/// UiProjections.hpp TaskViewProjection.archivePhaseToken 注释词表）。
std::string archivePhaseTokenOf(execution::ArchivePhase phase)
{
    switch (phase) {
    case execution::ArchivePhase::NotApplicable: return "";
    case execution::ArchivePhase::Reserved:      return "reserved";
    case execution::ArchivePhase::Archiving:     return "archiving";
    case execution::ArchivePhase::Archived:      return "archived";
    case execution::ArchivePhase::ArchiveFailed: return "archive-failed";
    }
    return "";   // 全枚举不可达；保守按"无归档语义"呈现
}

/// 终结原因枚举 → 呈现 token（空串＝非终态；强杀有独立 token——§9.4）。
std::string terminationTokenOf(
    const std::optional<execution::TerminationCause>& cause)
{
    if (!cause.has_value()) {
        return "";
    }
    switch (*cause) {
    case execution::TerminationCause::Canceled:         return "canceled";
    case execution::TerminationCause::Failed:           return "failed";
    case execution::TerminationCause::Completed:        return "completed";
    case execution::TerminationCause::Interrupted:      return "interrupted";
    case execution::TerminationCause::ForceTerminated:  return "force-terminated";
    }
    return "";
}

/**
 * 任务呈现端口的 L5 适配桥（测试侧镜像——O-31 裁决"经 ui 自有端口适配
 * 桥接"的形状承载）：实现 ui::IUiTaskPresentationPort 查询半区，内部
 * 转调 execution::ITaskScheduler 并把对端值类型投影为 ui 值类型。对端
 * 形状漂移在本类编译期暴露：TaskSnapshot/ProgressReport 字段访问＋
 * ITaskScheduler override 双重钉（字段改名/删除、虚签名变化即编译失败）。
 *
 * 映射口径（形状钉，不钉词表权威）：
 *   - identity：五元组的 project 取自端口入参、branch/revision 取会话
 *     上下文（构造注入——真实 L5 从当前会话绑定取）、run/attempt 取
 *     快照值（Queued 快照 run 为空→五元组 run 留零值——P-EX-6"无磁盘
 *     身份"的投影形态）；
 *   - supportsPause：TaskCapability 不在 TaskSnapshot 内——真实 L5 以
 *     RunRegistry/TaskRecord 联结，测试以构造注入的会话缺省值承载
 *     （联结点形状由消费方 Owned，不在本桥虚构）；
 *   - phaseLabelKey：对端 phaseToken 经注入映射表投影（未知 token→空串
     ——呈现层"只显示百分比"，不虚构文案键）；
 *   - 控制半区（requestCancel/Pause/Resume/ForceTerminate）：对端执行体
 *     TaskController 是具体类（无抽象接缝——testkit §6.4 接缝原则下
 *     FaultInterceptor 只接接口缝），其即发即忘行为面已由
 *     InteractionTaskContractTest.ControlRequestsAreFireAndForget 承载，
 *     本桥按端口契约返回缺省受理 Ack，不重复那半区。
 */
class SchedulerPresentationAdapter final : public ui::IUiTaskPresentationPort {
public:
    SchedulerPresentationAdapter(
        execution::ITaskScheduler& peer,
        const std::map<std::string, execution::TaskId>& runIndex,
        core::BranchId branch, core::RevisionId revision,
        bool supportsPauseDefault, std::map<std::string, ui::TextKey> phaseKeys)
        : peer_(peer), runIndex_(runIndex), branch_(branch), revision_(revision),
          supportsPauseDefault_(supportsPauseDefault),
          phaseKeys_(std::move(phaseKeys)) {}

    std::vector<ui::TaskViewProjection>
    tasksByProject(const core::ProjectId& project) const override
    {
        // 对端故障折叠面：IUiTaskPresentationPort 契约"纯查询，不抛"——
        // 对端 ExecutionError 折叠为空投影（不崩溃、不虚构行，环境错误
        // 的呈现归诊断面）。tryTask/progress 同此纪律。
        try {
            const auto snapshots = peer_.tasksByProject(project);
            std::vector<ui::TaskViewProjection> out;
            out.reserve(snapshots.size());
            for (const auto& snap : snapshots) {
                out.push_back(projectRow(project, snap));
            }
            return out;
        } catch (const execution::ExecutionError&) {
            return {};
        }
    }

    std::optional<ui::TaskViewProjection>
    task(const core::TaskIdentity& id) const override
    {
        return rowByIdentity(id);
    }

    std::optional<ui::TaskProgressProjection>
    progress(const core::TaskIdentity& id) const override
    {
        const auto row = rowByIdentity(id);
        if (!row.has_value()) {
            return std::nullopt;
        }
        return row->progress;
    }

    ui::UiTaskAck requestCancel(const core::TaskIdentity&) override { return {}; }
    ui::UiTaskAck requestPause(const core::TaskIdentity&) override { return {}; }
    ui::UiTaskAck requestResume(const core::TaskIdentity&) override { return {}; }
    ui::UiTaskAck requestForceTerminate(const core::TaskIdentity&) override
    {
        return {};
    }

private:
    /// run 规范文本寻址 → 对端 TaskId（五元组↔TaskId 的映射半区——真实
    /// L5 经 RunRegistry 核对面；测试以替身侧表承载）。
    std::optional<execution::TaskId>
    taskIdOf(const core::TaskIdentity& id) const
    {
        const auto it = runIndex_.find(id.run.toCanonical());
        if (it == runIndex_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::optional<ui::TaskViewProjection>
    rowByIdentity(const core::TaskIdentity& id) const
    {
        const auto taskId = taskIdOf(id);
        if (!taskId.has_value()) {
            return std::nullopt;
        }
        try {
            const auto snap = peer_.tryTask(*taskId);
            if (!snap.has_value()) {
                return std::nullopt;
            }
            return projectRow(id.project, *snap);
        } catch (const execution::ExecutionError&) {
            return std::nullopt;
        }
    }

    /// 对端快照 → ui 行投影（形状漂移的编译期捕获点：字段访问逐个钉）。
    ui::TaskViewProjection projectRow(const core::ProjectId& project,
                                      const execution::TaskSnapshot& snap) const
    {
        ui::TaskViewProjection row;
        row.identity.project = project;
        row.identity.branch = branch_;
        row.identity.revision = revision_;
        if (snap.run.has_value()) {
            row.identity.run = *snap.run;   // Queued 快照 run 为空→留零值（P-EX-6）
        }
        row.identity.attempt = snap.attempt;
        row.state = snap.state;
        row.attempt = snap.attempt;
        row.archivePhaseToken = archivePhaseTokenOf(snap.archivePhase);
        row.terminationReasonToken = terminationTokenOf(snap.termination);
        row.supportsPause = supportsPauseDefault_;
        if (snap.progress.has_value()) {
            ui::TaskProgressProjection progress;
            progress.percent = snap.progress->percent;
            const auto key = phaseKeys_.find(snap.progress->phaseToken);
            progress.phaseLabelKey =
                key == phaseKeys_.end() ? ui::TextKey{} : key->second;
            progress.batchesDone = snap.progress->batchesDone;
            progress.batchesTotal = snap.progress->batchesTotal;
            row.progress = std::move(progress);
        }
        return row;
    }

    execution::ITaskScheduler& peer_;                          ///< 对端调度器（非 owning）
    const std::map<std::string, execution::TaskId>& runIndex_; ///< run→TaskId 侧表
    core::BranchId branch_;                                    ///< 会话上下文（映射素材）
    core::RevisionId revision_;                                ///< 会话上下文（映射素材）
    bool supportsPauseDefault_;                                ///< 能力联结缺省（见类注释）
    std::map<std::string, ui::TextKey> phaseKeys_;             ///< phaseToken→文案键映射表
};

/// 组装一个带进度的运行中快照（登记侧表——run 规范文本可寻址）。
execution::TaskSnapshot
makeRunningSnapshot(SnapshotScheduler& scheduler, core::RunId run)
{
    execution::TaskSnapshot snap;
    snap.taskId = execution::TaskId::generate();
    snap.state = core::TaskState::Running;
    snap.run = run;
    snap.attempt = core::AttemptId{1};
    snap.progress = execution::ProgressReport::make(40, "eval", 4, 10);
    scheduler.byRunCanonical[run.toCanonical()] = snap.taskId;
    return snap;
}

/** ITaskScheduler 伪造＋FaultInterceptor 注入 → ui 呈现端口适配桥
 *  （acceptance 3 具名面——§12.1 第二层行"FaultInterceptor 伪造
 *  ITaskScheduler"；形状漂移由本 TU 编译期捕获的运行期半区）。 */
TEST(PeerBridge, SchedulerFakeFeedsPresentationPortViaFaultInterceptor_UI_T14)
{
    IRD_TEST_INFO("NFR-PERF-01", {"TASK-01"}, std::nullopt);

    // 对端替身：一运行中（带进度）＋一排队（无进度、无运行身份）。
    auto scheduler = std::make_shared<SnapshotScheduler>();
    const core::ProjectId project = core::ProjectId::generate();
    const core::RunId run = core::RunId::generate();
    scheduler->rows.push_back(makeRunningSnapshot(*scheduler, run));
    execution::TaskSnapshot queued;
    queued.taskId = execution::TaskId::generate();
    queued.state = core::TaskState::Queued;   // run 空/attempt 0——P-EX-6 形态
    scheduler->rows.push_back(queued);

    // 故障计划：tasksByProject 第 1 次命中注入对端故障（ExecutionError
    // ——接口声明的错误类型，testkit §6.4 用法形态）。
    FaultPlan plan;
    plan.triggers.push_back(
        FaultTrigger{"execution/itask-scheduler/tasks-by-project", 1});
    FaultingScheduler faulting(scheduler, plan);

    // 会话映射素材（run 索引/branch/revision/能力缺省/phase 键表——构造
    // 注入；run 索引取替身侧表，五元组寻址的映射半区）。
    SchedulerPresentationAdapter adapter(
        faulting, scheduler->byRunCanonical, core::BranchId::generate(),
        core::RevisionId::generate(), true,
        std::map<std::string, ui::TextKey>{{"eval", "stage.kinematics.title"}});

    // 第 1 次：故障命中 → adapter 折叠空投影（端口"纯查询不抛"契约——
    // 环境错误不崩溃不虚构），FaultLog 如实留痕。
    const auto faultedRows = adapter.tasksByProject(project);
    EXPECT_TRUE(faultedRows.empty()) << "对端故障未被折叠为空投影";
    const auto log = faulting.fault.log();
    ASSERT_EQ(log.hits.size(), 1u);
    EXPECT_EQ(log.hits[0].first, "execution/itask-scheduler/tasks-by-project");
    EXPECT_EQ(log.hits[0].second, 1u);

    // 第 2 次：未命中 → 快照投影到达（值映射逐字段断言——形状钉的
    // 运行期半区；字段漂移时本段随编译失败共同捕获）。
    const auto rows = adapter.tasksByProject(project);
    ASSERT_EQ(rows.size(), 2u);
    const ui::TaskViewProjection* running = nullptr;
    const ui::TaskViewProjection* queuedRow = nullptr;
    for (const auto& row : rows) {
        if (row.state == core::TaskState::Running) {
            running = &row;
        } else if (row.state == core::TaskState::Queued) {
            queuedRow = &row;
        }
    }
    ASSERT_NE(running, nullptr);
    ASSERT_NE(queuedRow, nullptr);
    // 运行中行：五元组映射＋进度投影（token→文案键命中；批次计数直传）。
    EXPECT_EQ(running->identity.project, project);
    EXPECT_EQ(running->identity.run, run);
    EXPECT_EQ(running->identity.attempt, core::AttemptId{1});
    EXPECT_EQ(running->attempt, core::AttemptId{1});
    ASSERT_TRUE(running->progress.has_value());
    EXPECT_EQ(running->progress->percent, 40);
    EXPECT_EQ(running->progress->phaseLabelKey, "stage.kinematics.title");
    EXPECT_EQ(running->progress->batchesDone, 4u);
    EXPECT_EQ(running->progress->batchesTotal, 10u);
    EXPECT_TRUE(running->archivePhaseToken.empty());
    EXPECT_TRUE(running->terminationReasonToken.empty());
    EXPECT_TRUE(running->supportsPause);
    // 排队行：run 留零值（P-EX-6）、零进度（不虚构百分比——UI-EXEC-1
    // 同源纪律）。
    EXPECT_FALSE(queuedRow->identity.run.isValid());
    EXPECT_FALSE(queuedRow->progress.has_value());

    // 单任务寻址：ui 五元组 → 侧表 TaskId → tryTask → 投影往返一致。
    const auto addressed = adapter.task(running->identity);
    ASSERT_TRUE(addressed.has_value());
    EXPECT_EQ(addressed->state, core::TaskState::Running);
    ASSERT_TRUE(addressed->progress.has_value());
    EXPECT_EQ(addressed->progress->percent, 40);
    // 未知 run 寻址 → nullopt（不虚构行——端口契约"不存在→nullopt"）。
    core::TaskIdentity stranger;
    stranger.project = project;
    stranger.branch = core::BranchId::generate();
    stranger.revision = core::RevisionId::generate();
    stranger.run = core::RunId::generate();
    stranger.attempt = core::AttemptId{1};
    EXPECT_FALSE(adapter.task(stranger).has_value());
    EXPECT_FALSE(adapter.progress(stranger).has_value());
}

// =====================================================================
// diagnostics 值面直用钉（表内登记边 ui→diagnostics 的测试侧承载）
// =====================================================================

// 形状钉：ui 脱敏配置端口必须直用 diagnostics::RedactionPolicy（表内
// 登记边值面——ui.md §3.2 冻结引用清单的消费实证）。对端值类型漂移
// （字段改名/删除）在此编译失败。
static_assert(std::is_same_v<
              decltype(&ui::IUiRedactionPolicyPort::applyPolicy),
              void (ui::IUiRedactionPolicyPort::*)(
                  const diagnostics::RedactionPolicy&)>,
              "ui 脱敏配置端口未直用 diagnostics::RedactionPolicy（表内"
              "登记边值面漂移）");

/// 脱敏配置端口的最小记录型替身（配置写路径的透传观测面——L5 转调
/// IRedactionService::setPolicy 的落点在 L5，本替身只承载 ui 侧语义）。
class RecordingRedactionPort final : public ui::IUiRedactionPolicyPort {
public:
    std::optional<diagnostics::RedactionPolicy> applied;   ///< 最近应用的策略
    int applyCalls = 0;                                    ///< 应用计数

    std::optional<diagnostics::RedactionPolicy> currentPolicy() const override
    {
        return applied;
    }

    void applyPolicy(const diagnostics::RedactionPolicy& policy) override
    {
        applied = policy;   // 写时拷贝切换的 ui 侧承载（值拷贝）
        ++applyCalls;
    }
};

/** diagnostics 公共头对接：RedactionPolicy 值面经 ui 自有端口直用
 *  （§12.1 第二层行"对接 diagnostics 公共头"的值半区；服务接口行为面
 *  归 DiagnosticPresentation 的模型测试——分工见各文件头）。 */
TEST(PeerBridge, RedactionPolicyValueFaceBindsDiagnosticsHeader_UI_T14)
{
    IRD_TEST_INFO("NFR-SEC-07", {}, std::nullopt);
    RecordingRedactionPort port;

    // 未配置态：nullopt（界面呈"未配置"占位——不虚构默认值，端口契约）。
    EXPECT_FALSE(port.currentPolicy().has_value());

    // 应用注入策略：值拷贝往返逐字段一致（路径策略＋预览上限字节）。
    diagnostics::RedactionPolicy policy;
    policy.pathPolicy = diagnostics::PathPolicy::Hash;
    policy.maxPreviewBytes = 64;
    port.applyPolicy(policy);
    EXPECT_EQ(port.applyCalls, 1);
    const auto current = port.currentPolicy();
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->pathPolicy, diagnostics::PathPolicy::Hash);
    EXPECT_EQ(current->maxPreviewBytes, 64u);

    // 二次应用覆盖（配置界面[确定]可重复提交——最新值生效）。
    diagnostics::RedactionPolicy updated;
    updated.pathPolicy = diagnostics::PathPolicy::RootOnly;
    updated.maxPreviewBytes = 32;
    port.applyPolicy(updated);
    EXPECT_EQ(port.applyCalls, 2);
    EXPECT_EQ(port.currentPolicy()->pathPolicy,
              diagnostics::PathPolicy::RootOnly);
}

}  // namespace
