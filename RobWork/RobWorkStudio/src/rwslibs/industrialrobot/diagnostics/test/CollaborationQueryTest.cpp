/**
 * @file   CollaborationQueryTest.cpp
 * @brief  跨单元关联查询与 AT 观测点映射用例组（DT-COLLAB-1＋DT-AT 映射行
 *         ——§10 矩阵在 DIAG-T02~T09 累积落位后的最后两个缺口组，随
 *         DIAG-T10 收口）。
 *
 * 设计依据：
 *   - units/diagnostics.md §10 DT-COLLAB-1 行（命令/任务/运行关联——TASK-03；
 *     观测点"DiagQuery 结果集"）、DT-AT 映射行（AT-01/10/11/13/19/30/34
 *     观测点数据形状就绪——"本文只提供观测点，不代验 AT"）、§9.7（DiagQuery
 *     过滤契约）、§6.3（原因链）、§4.3/§4.4（分类/严重/动作族）、§7.6（崩溃
 *     诊断文件）、§4.6（内置码表）
 *   - 任务契约 tasks/foundation/DIAG-T10.json acceptance 1（§10 验证矩阵
 *     逐条执行通过并留痕——本套件即 DT-COLLAB/DT-AT 两组的落位）
 *
 * ★ AT 观测点边界声明（§10 DT-AT 行原文的测试落点）：AT-xx 的**主验证责任**
 *   在各 AT 载体单元（kinematics/project/execution/optimization 的契约测试）；
 *   本套件只钉住"diagnostics 侧观测点数据形状就绪"——即 AT 载体用例将来
 *   从诊断设施读取的数据（比较型三要素、确认记录、原因链、码值、上下文
 *   参数、取消分类）形状正确且可查询。本套件任何用例**不构成对应 AT 的
 *   通过证明**。
 *
 * 线程约束：全部用例单线程（目录/工厂并发安全面以内部互斥承载——§9.7；
 * 多线程交错属集成观测面，§10 未设行，不私建）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/DiagData.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/diagnostics/Aggregation.hpp>
#include <sdurws/ird/diagnostics/Catalog.hpp>
#include <sdurws/ird/diagnostics/CrashReport.hpp>
#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/diagnostics/Errors.hpp>
#include <sdurws/ird/diagnostics/Factory.hpp>
#include <sdurws/ird/diagnostics/Redaction.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace sdurws::ird::diagnostics;
// 测试文件位于全局匿名 ns：`core` 是 sdurws::ird 的成员，using-directive 不
// 引入兄弟命名空间——以别名使 core::X 限定名可见（既有套件同款处理面）。
namespace core = sdurws::ird::core;
using sdurws::ird::core::BranchId;
using sdurws::ird::core::ObjectId;
using sdurws::ird::core::ProjectId;
using sdurws::ird::core::RevisionId;
using sdurws::ird::core::RunId;
using sdurws::ird::core::TaskIdentity;

// ---------------------------------------------------------------------
// 夹具辅助（与既有套件同风格——自持不共享）
// ---------------------------------------------------------------------

/// 可确认比较型测试码（ConfirmableTest 同款——MDL 前缀所有权归 modeling）。
inline constexpr const char* kTravelLimitCode = "MDL-06-TRAVEL-LIMIT";

/// 确定性测试时钟（§4.2 IClock 注释——时间形状断言的观测来源）。
class ManualClock final : public IClock {
public:
    std::chrono::system_clock::time_point nowUtc() const override { return m_now; }

private:
    std::chrono::system_clock::time_point m_now{std::chrono::seconds{5000000}};
};

/// 合法五元组（TASK-03——五字段全 isValid；任务路径上下文锚定用）。
TaskIdentity makeTaskIdentity()
{
    TaskIdentity task;
    task.project = ProjectId::generate();
    task.branch = BranchId::generate();
    task.revision = RevisionId::generate();
    task.run = RunId::generate();
    task.attempt = sdurws::ird::core::AttemptId::fromCanonical("att-1");
    return task;
}

/// 合法用户级记录（正例基线——各用例仅换码/subject）。
core::DiagnosticRecord makeUserRecord(const std::string& code, ObjectId subject)
{
    return core::DiagnosticRecord::make(
        code, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("评估路径输入非法"), std::string("启用 Must 条目取值越域"),
        std::string("修正输入后重新评估"));
}

/// 合法比较值侧（Provided 数值＋用户来源＋单位 token）。
core::ComparativeValue makeComparativeValue(double number, const char* unitSymbol)
{
    const auto unit = core::UnitToken::find(unitSymbol);
    return core::ComparativeValue{
        core::SourcedValue<double>::provided(
            number, core::ValueProvenance::make(core::ProvenanceKind::UserProvided)),
        *unit};
}

/// 读取整个文件（崩溃文件落盘观测辅助）。
std::string readFile(const fs::path& file)
{
    std::ifstream in(file);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// AT-11 用例的临时目录序号（文件级隔离——多次运行不串目录）。
inline int s_at11Seq = 0;

/// 套件夹具：注册表＋工厂＋目录（每用例独立装配）。
class DiagCollaboration : public ::testing::Test {
protected:
    void SetUp() override
    {
        registerBuiltinCodes(m_registry);
        // 可确认比较型测试码（AT-01 观测点形状的装载载体——阶段 A 内置表
        // 全为 confirmable=false，按"业务域码随域卡注册"机制注册）。
        CodeDescriptor confirmable;
        confirmable.code = kTravelLimitCode;
        confirmable.ownerUnit = "modeling";
        confirmable.category = DiagnosticCategory::Confirmable;
        confirmable.severity = DiagnosticSeverity::Warning;
        confirmable.titleKey = "diag.mdl-06-travel-limit.title";
        confirmable.detailKey = "diag.mdl-06-travel-limit.detail";
        confirmable.paramSchema = "[]";
        confirmable.confirmable = true;
        confirmable.requiresComparison = true;
        confirmable.retryable = RetryKind::UserRetry;
        m_registry.registerCode(confirmable);
        m_registry.seal();
        m_factory = std::make_unique<DiagnosticsFactory>(m_registry, m_clock);
    }

    StableCodeRegistry m_registry;    ///< 码表（每用例独立）
    ManualClock m_clock;              ///< 确定性时钟
    std::unique_ptr<DiagnosticsFactory> m_factory;
};

// =====================================================================
// DT-COLLAB-1——命令/任务/运行关联（TASK-03；§10 行原文：命令与任务路径
// 各产诊断 → snapshot 按 revision/task 过滤 → 各自命中且互不混；观测点
// ＝DiagQuery 结果集）
// =====================================================================

/// 命令路径与任务路径诊断经 DiagQuery 的 revision/task 两轴过滤各自命中
/// 且互不混：命令路径条目以 context.revision 锚定（无 task）、任务路径条目
/// 以 context.task 五元组锚定（内含自己的修订）——按命令修订过滤只命中
/// 命令条目，按五元组过滤只命中任务条目，未知修订过滤为空集，空查询全量
/// （§9.7"过滤：任务/修订/类别/严重"契约的关联面）。
TEST_F(DiagCollaboration, DtCollab1_CommandAndTaskDiagnosticsFilteredByRevisionAndTask)
{
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();

    // 命令路径诊断：PRJ-LOCK-HELD——上下文锚定项目/分支/修订＋命令类型与
    // 载荷摘要；params 键集恰合 paramSchema ["pid","host"]（工厂校验面）。
    // 命令服务归 project（§4.2 token 语义——本用例只承载已核验值形状）。
    DiagContext commandContext;
    commandContext.sourceUnit = "project";
    commandContext.sourceInterface = "commands.submit";
    commandContext.project = ProjectId::generate();
    commandContext.branch = BranchId::generate();
    const RevisionId commandRevision = RevisionId::generate();
    commandContext.revision = commandRevision;
    commandContext.commandType = "save.project";
    commandContext.commandDigest = core::ContentIdentity::fromCanonical(
        "cid-2222222222222222222222222222222222222222222222222222222222222222");
    commandContext.params = {{"pid", "prj-a"}, {"host", "workcell-1"}};
    catalog.append(m_factory->create(makeUserRecord("PRJ-LOCK-HELD", subject),
                                     commandContext));

    // 任务路径诊断：RT-INPUT-INVALID——上下文锚定运行五元组（其修订分量
    // 是五元组内部的 task.revision，与命令修订不同源不同轴）。
    DiagContext taskContext;
    taskContext.sourceUnit = "execution";
    taskContext.sourceInterface = "channel.error-report";
    const TaskIdentity task = makeTaskIdentity();
    taskContext.task = task;
    catalog.append(m_factory->create(makeUserRecord("RT-INPUT-INVALID", subject),
                                     taskContext));

    // 观测点：DiagQuery 结果集——四向过滤各自命中且互不混。
    const std::vector<DiagProjectionItem> all = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(all.size(), 2u);   // 空查询＝全量两条（路径互不吞并的前提）

    // 按命令修订过滤：只命中命令路径条目（PRJ-LOCK-HELD），任务条目不混入
    // （任务条目的 context 无 revision 分量——五元组修订不参与本轴匹配，
    // §9.7 DiagQuery.revision 语义按 context.revision 等值）。
    DiagQuery byCommandRevision;
    byCommandRevision.revision = commandRevision;
    const std::vector<DiagProjectionItem> commandHits = catalog.snapshot(byCommandRevision);
    ASSERT_EQ(commandHits.size(), 1u);
    EXPECT_EQ(commandHits[0].code, "PRJ-LOCK-HELD");
    ASSERT_TRUE(commandHits[0].context.revision.has_value());
    EXPECT_EQ(*commandHits[0].context.revision, commandRevision);
    EXPECT_FALSE(commandHits[0].context.task.has_value()) << "命令条目不得携带 task 锚定";

    // 按任务五元组过滤：只命中任务路径条目（RT-INPUT-INVALID），命令条目
    // 不混入（命令条目无 task——等值匹配落空）。
    DiagQuery byTask;
    byTask.task = task;
    const std::vector<DiagProjectionItem> taskHits = catalog.snapshot(byTask);
    ASSERT_EQ(taskHits.size(), 1u);
    EXPECT_EQ(taskHits[0].code, "RT-INPUT-INVALID");
    ASSERT_TRUE(taskHits[0].context.task.has_value());
    EXPECT_EQ(*taskHits[0].context.task, task);

    // 未知修订过滤：空集（等值匹配不命中任何路径——无"部分匹配"面）。
    DiagQuery byUnknownRevision;
    byUnknownRevision.revision = RevisionId::generate();
    EXPECT_TRUE(catalog.snapshot(byUnknownRevision).empty());
}

// =====================================================================
// DT-AT 映射——观测点数据形状就绪（§10 DT-AT 行：AT-01/10/11/13/19/30/34；
// "本文只提供观测点，不代验 AT"——每用例注明对应 AT 的主责载体单元）
// =====================================================================

/// AT-01 观测点（主责载体：kinematics 比较型字段＋project 确认留痕）：
/// 行程上限场景在 diagnostics 侧的数据形状——比较型条目携带三要素（实际/
/// 期望/已注册单位）＋确认记录携带 principal/confirmedAtUtc 且 core 投影
/// Confirmed（§10 行引用的 DT-CFM-1/10 数据形状的可独立观测面）。
TEST_F(DiagCollaboration, DtAt01_ComparativeAndConfirmationObservationShape)
{
    // 绑定复核环境探针（tip 与绑定基线一致、命令槽与绑定命令摘要一致——
    // 确认放行路径的形状观测；绑定复核行为本身已由 DT-CFM-1~5 全量验证，
    // 此处只取其产出形状。注意探针须如实回传创建时同一命令摘要——否则
    // 触发 binding-mismatch 复核拒绝，那是 DT-CFM-4/5 的被测面）。
    class TipEnv final : public IConfirmationEnvironment {
    public:
        TipEnv(core::RevisionId tip, core::ContentIdentity commandDigest)
            : m_tip(tip), m_command(commandDigest)
        {
        }
        core::RevisionId currentTipRevision(core::ProjectId, core::BranchId) const override
        {
            return m_tip;
        }
        std::optional<core::ContentIdentity> currentPolicyContentId(core::ProjectId,
                                                                    core::BranchId) const override
        {
            return std::nullopt;
        }
        std::optional<core::ContentIdentity> currentCommandDigest(core::ProjectId,
                                                                  core::BranchId) const override
        {
            return m_command;
        }

    private:
        core::RevisionId m_tip;
        core::ContentIdentity m_command;
    };

    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();
    const ProjectId project = ProjectId::generate();
    const BranchId branch = BranchId::generate();
    const RevisionId baseRevision = RevisionId::generate();
    const core::ContentIdentity commandDigest = core::ContentIdentity::fromCanonical(
        "cid-3333333333333333333333333333333333333333333333333333333333333333");
    TipEnv env(baseRevision, commandDigest);
    ConfirmableService service(m_registry, env, m_clock, nullptr);

    // 比较型三要素形状：实际 620mm / 期望 550mm（行程超限场景——AT-01 的
    // 原型输入；单位 token 已注册、数值 Provided 态）。
    core::DiagnosticRecord record = core::DiagnosticRecord::make(
        kTravelLimitCode, subject, std::string("joint_5"), std::string("Robot.joint_5"),
        std::string("行程超限待确认"), std::string("实际行程 620mm 超过上限 550mm"),
        std::string("确认或修正行程参数"),
        core::ComparativeFields{makeComparativeValue(620.0, "mm"),
                                makeComparativeValue(550.0, "mm")});
    const FindingRecord created = service.create(
        core::ConfirmableFinding::make(record), "apply.optimization",
        commandDigest, project, branch, baseRevision, std::nullopt, {subject});

    // 提交确认（凭据：主体＋UTC 时刻——core::ConfirmationCredential 形状）。
    const core::ConfirmationCredential credential{
        "operator-a", std::chrono::system_clock::time_point{std::chrono::seconds{5100000}}};
    EXPECT_EQ(service.submitConfirmation(created.findingId, created.callbackToken,
                                         credential),
              ConfirmOutcome::Confirmed);

    // 观测点形状断言（AT-01 载体将来读取的数据）：
    // ①确认留痕四元组＋凭据完整（DT-CFM-1/10 形状）；
    const std::optional<FindingRecord> confirmed = service.tryFind(created.findingId);
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_EQ(confirmed->state, FindingState::Confirmed);
    ASSERT_TRUE(confirmed->confirmation.has_value());
    EXPECT_EQ(confirmed->confirmation->principal, "operator-a");
    EXPECT_EQ(confirmed->confirmation->confirmedAtUtc, credential.confirmedAtUtc);
    // ②core 投影 Confirmed（凭证随附——确认状态跨单元可见的形状）。
    const core::ConfirmableFinding projection = confirmed->coreProjection();
    EXPECT_EQ(projection.state, core::ConfirmationState::Confirmed);
    ASSERT_TRUE(projection.credential.has_value());
    // ③比较型三要素仍在（确认不改写证据——AT-01 比较型字段形状保持）。
    ASSERT_TRUE(projection.record.comparison.has_value());
    EXPECT_EQ(projection.record.comparison->actual.unit.symbol(), std::string("mm"));
    EXPECT_EQ(projection.record.comparison->expected.unit.symbol(), std::string("mm"));
}

/// AT-10 观测点（主责载体：execution 迟到丢弃开发诊断）："EX-REGISTRY-*"
/// 形状在案＋崩溃原因链可遍历至根因（§10 行引用的 DT-CHAIN-1 形状——
/// 迟到事件须能沿链找到被丢弃前的根因诊断）。
TEST_F(DiagCollaboration, DtAt10_RegistryCodeShapeAndCauseChainToCrashRoot)
{
    // 形状 1：EX-REGISTRY-* 码在注册表（AT-10 载体引用的码值可查询——
    // 分类 Internal/ownerUnit execution 的登记形状）。
    const CodeDescriptor* unknownRun = m_registry.find("EX-REGISTRY-UNKNOWN-RUN");
    ASSERT_NE(unknownRun, nullptr) << "EX-REGISTRY-UNKNOWN-RUN 未注册（码表形状失守）";
    EXPECT_EQ(unknownRun->ownerUnit, "execution");
    EXPECT_EQ(unknownRun->category, DiagnosticCategory::Internal);
    const CodeDescriptor* mismatch = m_registry.find("EX-REGISTRY-MISMATCH");
    ASSERT_NE(mismatch, nullptr);

    // 形状 2：worker 崩溃场景原因链（根因 EX-WORKER-CRASHED → 派生
    // EX-TASK-REJECTED）——链遍历 API 从派生条目抵达根因（Aggregation.hpp
    // rootOf；链行为细节已由 DT-CHAIN-1 全量验证，此处钉观测点可达性）。
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    context.task = makeTaskIdentity();

    const DiagnosticEntry root =
        m_factory->create(makeUserRecord("EX-WORKER-CRASHED", subject), context);
    catalog.append(root);

    // 派生条目：经工厂创建（entryId/dedupKey 由工厂按新码分配——§9.2 唯一
    // 入口；绕过工厂的手造 id=0 会被 append Usage 拒绝），仅补根因链接
    // （causedBy 指向 root——append 校验指向目录内在场条目，§6.3）。
    DiagnosticEntry derived =
        m_factory->create(makeUserRecord("EX-TASK-REJECTED", subject), context);
    derived.causedBy = root.entryId;
    catalog.append(derived);

    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 2u);
    // 链遍历观测点：从派生条目 rootOf 一步抵达 EX-WORKER-CRASHED 根因。
    // rootOf 的输入是"链完整子集"（§9.4 契约——测试侧按值装载）：目录投影
    // 不含完整条目，取局部 root（create 已赋 entryId）＋以投影中找到的
    // 派生条目身份修正局部派生副本（append 分配的 id 在目录侧）后装载。
    DiagEntryId derivedId = 0;
    for (const auto& item : view) {
        if (item.code == "EX-TASK-REJECTED") { derivedId = item.entryId; }
    }
    ASSERT_NE(derivedId, 0u);
    DiagnosticEntry derivedForLookup = derived;
    derivedForLookup.entryId = derivedId;
    const std::vector<DiagnosticEntry> chainEntries{root, derivedForLookup};
    EXPECT_EQ(CauseLink::rootOf(chainEntries, derivedId), root.entryId);
}

/// AT-11 观测点（主责载体：project 恢复/WP-24 应用壳崩溃捕获）：崩溃诊断
/// 文件写出且内容脱敏——CrashReportInput 六段载体可装载（形状）＋写出文件
/// 含脱敏后的路径形态（RootOnly：盘符根保留、中间段抹除），不含原文路径
/// 与项目路径（§7.6；写出行为细节已由 RedactionTest AT-11 用例全量验证，
/// 此处钉观测点形状可达）。
TEST_F(DiagCollaboration, DtAt11_CrashReportInputShapeAndRedactedWrite)
{
    // 每用例独立临时目录（崩溃文件落盘观测面）。
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec)
                       / ("ird-at11-shape-" + std::to_string(++s_at11Seq));
    fs::create_directories(dir, ec);
    struct DirGuard {
        fs::path dir;
        ~DirGuard()
        {
            std::error_code cleanupEc;
            fs::remove_all(dir, cleanupEc);
        }
    } dirGuard{dir};

    // 六段载体全装载（形状自证——缺段即 AT 载体数据源不足）。
    CrashReportInput input;
    input.processInfo = "RobWorkStudio 1.2.3 (x64)";
    input.processId = 4242;                       // 固定 PID——确定性文件名
    input.exceptionType = "std::runtime_error";
    input.exceptionMessage = "save failed: cannot write C:\\Users\\operator-a\\proj\\model.rwdesign";
    input.devLogTail = {"<seq> 0 D INFO diag/test msg=tail-line-before-crash"};
    input.activeTasks = {makeTaskIdentity()};
    input.activeFindings = {{FindingId::fromCanonical(
                                 "fnd-11111111111111111111111111111111"),
                             FindingState::Pending}};
    input.openProject = ProjectId::generate();

    FileLogFileOps fileOps;
    RedactionService redaction;   // 默认策略：RootOnly/32（§7.7）
    CrashReportWriter writer(redaction, m_clock, fileOps, nullptr);

    const std::optional<fs::path> written = writer.write(input, dir);
    ASSERT_TRUE(written.has_value()) << "崩溃诊断文件未写出（观测点数据源缺失）";
    const std::string content = readFile(*written);
    // 脱敏观测点（产品实际规则序——§7.7 规则④"用户名无条件脱敏"先行于
    // ⑤路径策略："<盘>:\Users\<名>" 整体替换为 "[USER]"，AT-11 引用的
    // DT-SEC-1/2/4 联合形状）：用户名与 "Users" 目录段零命中（NFR-SEC-07），
    // 用户目录位保留 "[USER]" 标记（替换计数可见），文件名按策略保留
    // （形态保真——脱敏不摧毁排障可用性）。
    EXPECT_EQ(content.find("operator-a"), std::string::npos)
        << "崩溃文件泄露用户名原文：" << content;
    EXPECT_EQ(content.find("Users"), std::string::npos)
        << "崩溃文件泄露用户目录形态：" << content;
    EXPECT_NE(content.find("[USER]"), std::string::npos)
        << "用户目录未按规则④整体替换（[USER] 标记缺失）";
    EXPECT_NE(content.find("model.rwdesign"), std::string::npos)
        << "文件名未保留（脱敏不应摧毁排障可用性）——content=[" << content << "]";
    // 六段载体确有落痕（键值文本形态——format 段＋进程段在案）。
    EXPECT_NE(content.find("format"), std::string::npos);
    EXPECT_NE(content.find("RobWorkStudio 1.2.3"), std::string::npos);
}

/// AT-13 观测点（主责载体：project 恢复诊断）：PRJ-RECOVERY-* 恢复诊断码
/// 的呈现形状——Info 级用户可见＋项目上下文锚定＋文案键就绪（AT-13 载体
/// "恢复诊断字段"引用的数据源形状；行为细节归 project 契约测试）。
TEST_F(DiagCollaboration, DtAt13_RecoveryDiagnosticShape)
{
    const CodeDescriptor* orphan = m_registry.find("PRJ-RECOVERY-ORPHAN-DRAFT");
    ASSERT_NE(orphan, nullptr) << "PRJ-RECOVERY-ORPHAN-DRAFT 未注册";
    // 码表形状：Info（恢复提示非错误）＋权限/锁类＋文案键按 P-DIAG-9 约定。
    EXPECT_EQ(orphan->severity, DiagnosticSeverity::Info);
    EXPECT_EQ(orphan->category, DiagnosticCategory::PermissionOrLock);
    EXPECT_EQ(orphan->titleKey, "diag.prj-recovery-orphan-draft.title");
    EXPECT_EQ(orphan->detailKey, "diag.prj-recovery-orphan-draft.detail");

    // 工厂产出形状：项目上下文锚定（恢复诊断必携带其归属项目——§4.2 命令
    // 路径必填面的恢复半区）＋record 原样内嵌。
    DiagCatalog catalog;
    DiagContext context;
    context.sourceUnit = "project";
    context.sourceInterface = "session.recovery";
    context.project = ProjectId::generate();
    context.branch = BranchId::generate();
    context.revision = RevisionId::generate();
    const core::DiagnosticRecord record = core::DiagnosticRecord::make(
        "PRJ-RECOVERY-ORPHAN-DRAFT", ObjectId::generate(), std::string("draft-7"),
        std::string{}, std::string("发现上次会话遗留草稿"), std::string("会话异常关闭"),
        std::string("打开草稿或放弃"));
    const DiagnosticEntry entry = m_factory->create(record, context);
    catalog.append(entry);
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0].severity, DiagnosticSeverity::Info);
    EXPECT_EQ(view[0].titleKey, "diag.prj-recovery-orphan-draft.title");
    ASSERT_TRUE(view[0].context.project.has_value());
}

/// AT-19 观测点（主责载体：policy/kinematics 原因码一致契约）：码值唯一
/// 来源——同码多次查询返回逐字段一致的登记事实（§10 行"码表单点"的观测
/// 面：各域契约测试引用的码值必须与诊断注册表单点一致，无第二来源）。
TEST_F(DiagCollaboration, DtAt19_CauseCodesSingleSourceInRegistry)
{
    // AT-19 载体高频引用的三个原因码（取消/中断/崩溃——DT-CAT-2/DT-CHAIN-1
    // 行的码值来源）：两次查询逐字段一致＝单点权威可复现。
    for (const char* code : {"RT-CANCELLED", "EX-TASK-INTERRUPTED", "EX-WORKER-CRASHED"}) {
        const CodeDescriptor* first = m_registry.find(code);
        const CodeDescriptor* second = m_registry.find(code);
        ASSERT_NE(first, nullptr) << code << " 未注册";
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(*first, *second) << code << " 两次查询不一致（码表非单点）";
        EXPECT_EQ(first->code, code);
        EXPECT_FALSE(first->titleKey.empty()) << code << " 文案键缺失（P-DIAG-9 形状）";
    }
}

/// AT-30 观测点（主责载体：optimization 回填复算提示）：诊断上下文参数
/// 面可承载"失效原因清单"形状——paramSchema 声明参数槽、params 键值经
/// 工厂校验后原样进入条目上下文（AT-30 载体将来携带的失效原因数据源
/// 形状；清单的语义组装归载体单元，diagnostics 只保证承载可查询）。
TEST_F(DiagCollaboration, DtAt30_InvalidationReasonListParamsShape)
{
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();
    DiagContext context;
    context.sourceUnit = "project";
    context.sourceInterface = "commands.submit";
    context.project = ProjectId::generate();
    context.branch = BranchId::generate();
    context.revision = RevisionId::generate();
    // 失效原因清单形状：params 键值对（pid＋host 槽——PRJ-LOCK-HELD 的
    // paramSchema ["pid","host"]；工厂校验键集恰合后原样保留）。
    context.params = {{"pid", "prj-a"}, {"host", "workcell-1"}};

    const DiagnosticEntry entry =
        m_factory->create(makeUserRecord("PRJ-LOCK-HELD", subject), context);
    // 观测点：params 原样进入条目上下文（无丢弃/无重排——数据源保真形状）。
    EXPECT_EQ(entry.context.params.at("pid"), "prj-a");
    EXPECT_EQ(entry.context.params.at("host"), "workcell-1");
    catalog.append(entry);
    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0].context.params.size(), 2u);
    EXPECT_EQ(view[0].context.params.at("host"), "workcell-1");
}

/// AT-34 观测点（主责载体：execution 取消/进度）：正常取消非错误的分类
/// 形状——RT-CANCELLED 为 Info 且动作族 none、中断为 Info 且动作族
/// rerun-interrupted（§10 行引用的 DT-CAT-2 形状：AT-34 载体引用的诊断
/// 分类数据源；取消/进度的状态机行为归 execution）。
TEST_F(DiagCollaboration, DtAt34_CancelIsInfoNotErrorObservationShape)
{
    // 码表形状：取消/中断均 Info（用户可见提示而非错误）。
    const CodeDescriptor* cancelled = m_registry.find("RT-CANCELLED");
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ(cancelled->severity, DiagnosticSeverity::Info);
    EXPECT_EQ(cancelled->category, DiagnosticCategory::Canceled);
    const CodeDescriptor* interrupted = m_registry.find("EX-TASK-INTERRUPTED");
    ASSERT_NE(interrupted, nullptr);
    EXPECT_EQ(interrupted->severity, DiagnosticSeverity::Info);
    EXPECT_EQ(interrupted->category, DiagnosticCategory::Interrupted);

    // 产出形状：动作族 token（§4.4 机器锚点）——取消 none（无需用户动作）、
    // 中断 rerun-interrupted（§4.4 动作族登记值）。
    DiagCatalog catalog;
    const ObjectId subject = ObjectId::generate();
    DiagContext context;
    context.sourceUnit = "execution";
    context.sourceInterface = "channel.error-report";
    context.task = makeTaskIdentity();
    catalog.append(m_factory->create(makeUserRecord("RT-CANCELLED", subject), context));

    const std::vector<DiagProjectionItem> view = catalog.snapshot(DiagQuery{});
    ASSERT_EQ(view.size(), 1u);
    EXPECT_EQ(view[0].severity, DiagnosticSeverity::Info);
    EXPECT_EQ(view[0].actionKind, "none");
    // 分类过滤轴可达（AT-34 载体按类别统计取消事件的查询形状）。
    DiagQuery byCategory;
    byCategory.category = DiagnosticCategory::Canceled;
    EXPECT_EQ(catalog.snapshot(byCategory).size(), 1u);
}

}  // namespace
