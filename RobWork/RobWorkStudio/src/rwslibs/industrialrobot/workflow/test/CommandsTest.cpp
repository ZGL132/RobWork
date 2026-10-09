/**
 * @file   CommandsTest.cpp
 * @brief  命令集编排核的模型测试（WP-22-T12——units/workflow.md §8.3/§8.4
 *         编排面的直调半区；WF-VER-222 命令集词表与分流黄金表＋WF-VER-223
 *         复位关节零修订的编排半区＋P-WF-3 碰撞检查会话级最小语义的触达
 *         隔离＋RPT-02 报告导出编排＋P-WF-4 预览缺位如实呈现）。
 *
 * 设计依据：
 *   - units/workflow.md §8.3（最小命令集注册与工业高频命令——执行回调分
 *     流：产生修订的命令经①命令端口、会话态命令不产生修订〔KIN-06〕、
 *     报告导出编排 reporting 导出服务〔RPT-02〕）、§8.4（"运行碰撞检查"
 *     保持会话级最小语义——P-WF-3 裁决前不产生正式证据/不归档 envelope/
 *     不进报告正式章节）、§10.3（调用方错误 fail-fast——WorkflowError）
 *   - REQUIREMENTS.md §18 UX-13 原文（最小清单 10 条＋工业高频 M-13：运
 *     行碰撞检查/切换显示模式/复位关节至 Home/Zero〔会话姿态，KIN-06 语
 *     义，不产生修订〕）、KIN-06（双击/复位只改会话姿态不修改设计模型）、
 *     PM-18/AT-29（撤销/重做——机制归 project，本单元命令编排）
 *   - 任务契约 tasks/foundation/WP-22-T12.json acceptance 1/2/3
 *
 * 测试形态（§11.0——模型测试＝直调计算库纯函数面）：五条端口全部为脚
 * 本化桩（L5 装配桥接的测试等价物——记录调用计数、返回预置结果），编
 * 排核 runWorkflowCommand/runReportExportCommand 直调；桩实现经各端口抽
 * 象基类引用注入编排核——端口接口的消费路径由全部用例常态覆盖。描述符
 * 组装/真实注册/命令面板模糊搜索可达面（ui::CommandDescriptor——Qt 类
 * 型）在契约测试 CommandsContractTest.cpp（_test 目标零 Qt——R-3 红线）。
 *
 * 追溯约定（AGENTS.md §2.7）：用例名与断言注释标注需求/AT 编号。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记
#include <sdurws/ird/workflow/Commands.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（fail-fast 断言）

#include <optional>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::CollisionCheckOutcome;
using workflow::ICollisionCheckPort;
using workflow::ILifecycleFlowLauncher;
using workflow::IReportExportDialogPort;
using workflow::IRevisionCommandPort;
using workflow::IWorkflowSessionPort;
using workflow::ReportExportCommandOutcome;
using workflow::ReportExportDialogResult;
using workflow::ReportExportInputs;
using workflow::RevisionOutcome;
using workflow::SessionActionResult;
using workflow::WorkflowCommandKind;
using workflow::WorkflowCommandOutcome;
using workflow::WorkflowCommandPorts;
using workflow::WorkflowError;

// =====================================================================
// 脚本化桩（L5 装配桥的测试等价物——记录调用、返回预置结果）
// =====================================================================

/// 生命周期流程启动桩（四方法各自计数——分派正确性断言材料）。
class StubLauncher final : public ILifecycleFlowLauncher {
public:
    int newProject = 0;     ///< launchNewProjectWizard 调用计数
    int openProject = 0;    ///< launchOpenProjectDialog 调用计数
    int saveAs = 0;         ///< launchSaveAsWizard 调用计数
    int packageExport = 0;  ///< launchPackageExportWizard 调用计数

    void launchNewProjectWizard() override { ++newProject; }
    void launchOpenProjectDialog() override { ++openProject; }
    void launchSaveAsWizard() override { ++saveAs; }
    void launchPackageExportWizard() override { ++packageExport; }
};

/// 会话动作桩（五方法各自计数——KIN-06 零修订断言的触达观测面）。
class StubSession final : public IWorkflowSessionPort {
public:
    int switchScheme = 0;  ///< switchSchemeBranch 调用计数
    int displayMode = 0;   ///< cycleDisplayMode 调用计数
    int resetHome = 0;     ///< resetJointsToHome 调用计数
    int resetZero = 0;     ///< resetJointsToZero 调用计数
    int saveDrafts = 0;    ///< saveAllDrafts 调用计数
    /// 预置返回（默认成功无文案——被调用即受理）。
    SessionActionResult next{true, ""};

    SessionActionResult switchSchemeBranch() override { ++switchScheme; return next; }
    SessionActionResult cycleDisplayMode() override { ++displayMode; return next; }
    SessionActionResult resetJointsToHome() override { ++resetHome; return next; }
    SessionActionResult resetJointsToZero() override { ++resetZero; return next; }
    SessionActionResult saveAllDrafts() override { ++saveDrafts; return next; }
};

/// 修订命令桩（三方法各自计数——①端口触达与分派断言材料；计数成员名
/// 与虚函数名错开——同名成员与方法在同类内非法）。
class StubRevisions final : public IRevisionCommandPort {
public:
    int applyCalls = 0;  ///< applyDraft 调用计数
    int undoCalls = 0;   ///< undo 调用计数
    int redoCalls = 0;   ///< redo 调用计数
    /// 预置返回（默认提交成功＋新修订 id——修订类受理形态）。
    RevisionOutcome next{};

    RevisionOutcome applyDraft() override { ++applyCalls; return next; }
    RevisionOutcome undo() override { ++undoCalls; return next; }
    RevisionOutcome redo() override { ++redoCalls; return next; }
};

/// 碰撞检查桩（P-WF-3 触达隔离断言的观测面）。
class StubCollision final : public ICollisionCheckPort {
public:
    int calls = 0;  ///< startSessionCheck 调用计数
    CollisionCheckOutcome next{true, ""};  ///< 预置返回

    CollisionCheckOutcome startSessionCheck() override { ++calls; return next; }
};

/// 导出参数收集桩（对话框两态可编程——确认/取消路径分离断言）。
class StubDialog final : public IReportExportDialogPort {
public:
    int calls = 0;              ///< collectExportInputs 调用计数
    ReportExportDialogResult next{};  ///< 预置返回（默认取消态）

    ReportExportDialogResult collectExportInputs() override { ++calls; return next; }
};

/// 导出服务桩（记录收到的请求——请求组装黄金断言材料；返回可编程）。
class StubExportService final : public reporting::IReportExportService {
public:
    int calls = 0;                                   ///< exportReport 调用计数
    reporting::ReportExportRequest received{};       ///< 最近一次收到的请求（透传零加工断言）
    reporting::ReportExportResult next{};            ///< 预置返回（默认取消态＝五成员全空）

    reporting::ReportExportResult exportReport(
        const reporting::ReportExportRequest& request,
        const reporting::ReportCancelToken* /*cancel*/ = nullptr,
        reporting::ReportProgressCallback /*progress*/ = {}) override
    {
        ++calls;
        received = request;
        return next;
    }
};

/// 全桩端口组合（默认全接齐——单用例按需置空某端口验证装配违约）。
struct Harness {
    StubLauncher launcher;
    StubSession session;
    StubRevisions revisions;
    StubCollision collision;
    StubDialog dialog;
    StubExportService exportService;

    /// 组装全接齐端口集（指针非 owning——编排核存活期约定同生产装配）。
    [[nodiscard]] WorkflowCommandPorts ports() const
    {
        WorkflowCommandPorts p;
        p.lifecycle = const_cast<StubLauncher*>(&launcher);
        p.session = const_cast<StubSession*>(&session);
        p.revisions = const_cast<StubRevisions*>(&revisions);
        p.collision = const_cast<StubCollision*>(&collision);
        p.reportDialog = const_cast<StubDialog*>(&dialog);
        p.reportExport = const_cast<StubExportService*>(&exportService);
        return p;
    }
};

/// 词表内 id 的快速取用（下标即 workflowCommandIds() 行序——黄金表对位）。
constexpr int kIdxProjectNew = 0;
constexpr int kIdxProjectOpen = 1;
constexpr int kIdxDraftSave = 2;
constexpr int kIdxDraftApply = 3;
constexpr int kIdxUndo = 4;
constexpr int kIdxRedo = 5;
constexpr int kIdxSchemeSwitch = 6;
constexpr int kIdxSaveAs = 7;
constexpr int kIdxPackageExport = 8;
constexpr int kIdxReportExport = 9;
constexpr int kIdxCollision = 10;
constexpr int kIdxDisplayMode = 11;
constexpr int kIdxResetHome = 12;
constexpr int kIdxResetZero = 13;

// =====================================================================
// WF-VER-222——命令集词表与分流（UX-13/F5 最小清单＋工业高频）
// =====================================================================

/**
 * @brief WF-VER-222 词表黄金表——14 条 id 逐值冻结（ui.md §7.1 冻结表
 *        同串，SA-12 词表复用）＋行序稳定（登记序排序锚）＋两两唯一＋
 *        ui CommandId 句法（必含点分——与 project commandType 无点词形
 *        命名空间分离）。
 */
TEST(WfCommands, VocabularyFrozen_FourteenIds_GoldenOrder_UX13_F5)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    const std::vector<std::string>& ids = workflow::workflowCommandIds();

    // 黄金行序（＝ui.md §7.1 冻结表行序——描述符 registrationOrder 的
    // 稳定排序锚；行序变化即呈现序变化，契约级冻结）。
    const std::vector<std::string> golden = {
        "project.new",              // 最小清单——新建项目
        "project.open",             // 最小清单——打开项目
        "draft.save",               // 最小清单——保存草稿
        "draft.apply",              // 最小清单——应用修改
        "project.undo",             // 最小清单——撤销
        "project.redo",             // 最小清单——重做
        "scheme.switch",            // 最小清单——切换方案
        "project.saveAs",           // 最小清单——另存为
        "package.export",           // 最小清单——包导出
        "report.export",            // 最小清单——报告导出
        "analysis.collisionCheck",  // 工业高频——运行碰撞检查
        "view.displayMode",         // 工业高频——切换显示模式
        "view.resetHome",           // 工业高频——复位关节至 Home
        "view.resetZero",           // 工业高频——复位关节至 Zero
    };
    ASSERT_EQ(ids.size(), golden.size()) << "词表行数必须＝14（≥10 条 UX-13 底线＋工业高频全量）";
    for (std::size_t i = 0; i < golden.size(); ++i) {
        EXPECT_EQ(ids[i], golden[i]) << "词表行序冻结（第 " << i << " 行）";
    }

    // 两两唯一（ui §7.2 命令 id 全局唯一——贡献清单唯一性义务的词表半区）。
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            EXPECT_NE(ids[i], ids[j]) << "命令 id 重复（全局唯一违约）";
        }
    }

    // ui CommandId 句法：必含点分（与 project commandType 无点词形两套
    // 命名空间分离——modeling PanelCommandCatalog 同款交叉守卫）。
    for (const std::string& id : ids) {
        EXPECT_NE(id.find('.'), std::string::npos) << "命令 id 必含点分：" << id;
    }
}

/**
 * @brief WF-VER-222 分流黄金表——kindOfCommand 全表 14 行逐行冻结（§8.3
 *        执行回调分流的判定面）＋词表外 id fail-fast（WorkflowError）。
 */
TEST(WfCommands, KindPartitionFrozen_FullTable_GoldenValues)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"WF-VER-222"});

    // 黄金分流（修订类 3／流程类 4／会话类 5／碰撞 1／报告导出 1——
    // §8.3 行三分流的词化；逐行与单元卡分流表对位）。
    EXPECT_EQ(workflow::kindOfCommand("draft.apply"), WorkflowCommandKind::Revision);
    EXPECT_EQ(workflow::kindOfCommand("project.undo"), WorkflowCommandKind::Revision);
    EXPECT_EQ(workflow::kindOfCommand("project.redo"), WorkflowCommandKind::Revision);
    EXPECT_EQ(workflow::kindOfCommand("project.new"), WorkflowCommandKind::LifecycleFlow);
    EXPECT_EQ(workflow::kindOfCommand("project.open"), WorkflowCommandKind::LifecycleFlow);
    EXPECT_EQ(workflow::kindOfCommand("project.saveAs"), WorkflowCommandKind::LifecycleFlow);
    EXPECT_EQ(workflow::kindOfCommand("package.export"), WorkflowCommandKind::LifecycleFlow);
    EXPECT_EQ(workflow::kindOfCommand("draft.save"), WorkflowCommandKind::SessionAction);
    EXPECT_EQ(workflow::kindOfCommand("scheme.switch"), WorkflowCommandKind::SessionAction);
    EXPECT_EQ(workflow::kindOfCommand("view.displayMode"), WorkflowCommandKind::SessionAction);
    EXPECT_EQ(workflow::kindOfCommand("view.resetHome"), WorkflowCommandKind::SessionAction);
    EXPECT_EQ(workflow::kindOfCommand("view.resetZero"), WorkflowCommandKind::SessionAction);
    EXPECT_EQ(workflow::kindOfCommand("analysis.collisionCheck"),
              WorkflowCommandKind::CollisionCheck);
    EXPECT_EQ(workflow::kindOfCommand("report.export"), WorkflowCommandKind::ReportExport);

    // 词表外 id：调用方/对端契约违约 fail-fast（静默返回会把用户操作丢
    // 进黑洞——第二道防线，ui UI-CMD-UNKNOWN 是第一道）。
    EXPECT_THROW(workflow::kindOfCommand("nonexistent.command"), WorkflowError);
    EXPECT_THROW(workflow::kindOfCommand(""), WorkflowError);
}

// =====================================================================
// WF-VER-226 编排半区——修订类经①端口（PM-18/AT-29）
// =====================================================================

/**
 * @brief 修订类三命令按 id 分派①端口（draft.apply→applyDraft、
 *        project.undo→undo、project.redo→redo——"产生修订的命令经①命
 *        令端口"ARCH §7.11 的编排面）；提交成功→accepted=true。
 */
TEST(WfCommands, RevisionCommands_DispatchToCommandPort_P18_AT29)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "PM-18"}, std::vector<std::string>{"AT-29", "WF-VER-226"});

    Harness h;
    h.revisions.next.committed = true;
    h.revisions.next.newRevisionId = std::string{"rev-00000004"};
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome apply = workflow::runWorkflowCommand("draft.apply", ports);
    ASSERT_TRUE(apply.accepted) << "应用修改提交成功应受理";
    const WorkflowCommandOutcome undo = workflow::runWorkflowCommand("project.undo", ports);
    ASSERT_TRUE(undo.accepted) << "撤销（逆命令提交）成功应受理";
    const WorkflowCommandOutcome redo = workflow::runWorkflowCommand("project.redo", ports);
    ASSERT_TRUE(redo.accepted) << "重做（原始载荷重放）成功应受理";

    // 分派正确性：各自恰好一次、互不串扰（计数分派——编排核唯一实现点）。
    EXPECT_EQ(h.revisions.applyCalls, 1);
    EXPECT_EQ(h.revisions.undoCalls, 1);
    EXPECT_EQ(h.revisions.redoCalls, 1);
}

/**
 * @brief 修订类失败透传（UX-03 不吞错）——committed=false 时 accepted=
 *        false＋呈现键直通 messageKey（对端 stableCode 是机器半区，不
 *        上抛到呈现键位）。
 */
TEST(WfCommands, RevisionRejection_FoldsAcceptedFalse_ReasonKeyPassthrough_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{"WF-VER-226"});

    Harness h;
    h.revisions.next.committed = false;                          // 例：空历史撤销/只读拒绝
    h.revisions.next.stableCode = "rejected/not-writable";       // 对端状态词透传（零加工）
    h.revisions.next.reasonKey = "cmd.workflow.revision.rejected";
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out = workflow::runWorkflowCommand("project.undo", ports);
    EXPECT_FALSE(out.accepted) << "拒绝态不得伪装成功";
    EXPECT_EQ(out.messageKey, "cmd.workflow.revision.rejected") << "呈现键零加工直通";
}

// =====================================================================
// WF-VER-223 模型半区——会话类零修订（KIN-06 结构性分流）
// =====================================================================

/**
 * @brief 五条会话命令分派会话端口且**修订端口零触达**（KIN-06 会话语义
 *        ——复位关节 Home/Zero 只改会话姿态；保存草稿走草稿写轨；切换
 *        方案 PM-12 零写入；显示模式纯呈现偏好——acceptance 2 编排面）。
 */
TEST(WfCommands, SessionCommands_ZeroRevisionPortTouch_KIN06)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06", "UX-13"}, std::vector<std::string>{"WF-VER-223"});

    Harness h;
    h.session.next = SessionActionResult{true, ""};
    WorkflowCommandPorts ports = h.ports();

    // 五条会话命令逐条执行（id 列表即 kindOfCommand 会话行——词表对位）。
    const char* sessionIds[] = {"draft.save", "scheme.switch", "view.displayMode",
                                "view.resetHome", "view.resetZero"};
    for (const char* id : sessionIds) {
        const WorkflowCommandOutcome out = workflow::runWorkflowCommand(id, ports);
        EXPECT_TRUE(out.accepted) << "会话命令应受理：" << id;
    }

    // 分派正确性（各自恰好一次）。
    EXPECT_EQ(h.session.saveDrafts, 1);
    EXPECT_EQ(h.session.switchScheme, 1);
    EXPECT_EQ(h.session.displayMode, 1);
    EXPECT_EQ(h.session.resetHome, 1);
    EXPECT_EQ(h.session.resetZero, 1);

    // ★ KIN-06 结构性断言：会话分支不触达修订端口（复位关节不产生修订
    // ——编排面结构性保证，真实 store 盘面复核在契约测试）。
    EXPECT_EQ(h.revisions.applyCalls, 0);
    EXPECT_EQ(h.revisions.undoCalls, 0);
    EXPECT_EQ(h.revisions.redoCalls, 0);
}

/**
 * @brief 会话动作失败/取消折叠——端口返回 accepted=false＋reasonKey 时
 *        编排结果同样不伪装成功（取消非错误的呈现键直通）。
 */
TEST(WfCommands, SessionRejection_FoldsAcceptedFalse)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{});

    Harness h;
    h.session.next = SessionActionResult{false, "cmd.workflow.session.canceled"};
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out = workflow::runWorkflowCommand("view.resetHome", ports);
    EXPECT_FALSE(out.accepted);
    EXPECT_EQ(out.messageKey, "cmd.workflow.session.canceled");
}

// =====================================================================
// 生命周期流程类——触发向导入口（D-WF-6 宿主面）
// =====================================================================

/**
 * @brief 四条流程命令分派启动端口四入口（触发即受理——流程完成态经会
 *        话事件回投影面，命令处理器零阻塞）。
 */
TEST(WfCommands, LifecycleCommands_DispatchToFlowLauncher)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "PM-01", "PM-02", "PM-05"}, std::vector<std::string>{});

    Harness h;
    WorkflowCommandPorts ports = h.ports();

    EXPECT_TRUE(workflow::runWorkflowCommand("project.new", ports).accepted);
    EXPECT_TRUE(workflow::runWorkflowCommand("project.open", ports).accepted);
    EXPECT_TRUE(workflow::runWorkflowCommand("project.saveAs", ports).accepted);
    EXPECT_TRUE(workflow::runWorkflowCommand("package.export", ports).accepted);

    EXPECT_EQ(h.launcher.newProject, 1);
    EXPECT_EQ(h.launcher.openProject, 1);
    EXPECT_EQ(h.launcher.saveAs, 1);
    EXPECT_EQ(h.launcher.packageExport, 1);
}

// =====================================================================
// P-WF-3——碰撞检查会话级最小语义（触达隔离）
// =====================================================================

/**
 * @brief analysis.collisionCheck 只触达碰撞端口（修订/会话/报告端口零
 *        调用——P-WF-3 最小语义的结构性保证：不产生修订、不产生正式证
 *        据、不进报告正式章节；O-26 三方契约裁决前不扩大）。
 */
TEST(WfCommands, CollisionCheck_MinimalSemantics_IsolatedTouch_PWF3)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13"}, std::vector<std::string>{"P-WF-3", "WF-VER-222"});

    Harness h;
    h.collision.next = CollisionCheckOutcome{true, ""};
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out =
        workflow::runWorkflowCommand("analysis.collisionCheck", ports);
    EXPECT_TRUE(out.accepted) << "会话级检查任务受理";
    EXPECT_EQ(h.collision.calls, 1) << "恰好触达碰撞端口一次";

    // ★ 最小语义触达隔离：修订端口（零修订）/会话端口/报告链路（零正式
    // 证据归档承诺）全部零触达。
    EXPECT_EQ(h.revisions.applyCalls, 0);
    EXPECT_EQ(h.revisions.undoCalls, 0);
    EXPECT_EQ(h.revisions.redoCalls, 0);
    EXPECT_EQ(h.session.saveDrafts, 0);
    EXPECT_EQ(h.dialog.calls, 0);
    EXPECT_EQ(h.exportService.calls, 0);
}

/**
 * @brief 碰撞检查未受理折叠（会话前置不满足——如无在审模型）——不伪装
 *        成功，原因键直通。
 */
TEST(WfCommands, CollisionCheck_Rejection_FoldsAcceptedFalse)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{"P-WF-3"});

    Harness h;
    h.collision.next = CollisionCheckOutcome{false, "cmd.workflow.collision.no-model"};
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out =
        workflow::runWorkflowCommand("analysis.collisionCheck", ports);
    EXPECT_FALSE(out.accepted);
    EXPECT_EQ(out.messageKey, "cmd.workflow.collision.no-model");
}

// =====================================================================
// RPT-02——报告导出编排 reporting 服务（P-WF-4 预览缺位如实）
// =====================================================================

/**
 * @brief 报告导出主链——对话框确认后请求逐字段组装（零加工零补造）交
 *        reporting 服务，成功→Completed＋accepted（RPT-02 编排语义）。
 */
TEST(WfCommands, ReportExport_ComposesRequestAndCallsService_RPT02)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-13", "RPT-02"}, std::vector<std::string>{"WF-VER-222"});

    Harness h;
    // 黄金输入（对话框收集产物——五字段全定值）。
    ReportExportInputs inputs;
    inputs.project = core::ProjectId{};  // 全零身份＝测试值（值透传断言用，不涉解析）
    inputs.reportId.bytes = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                             0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    inputs.formats = {reporting::ReportRenderFormat::Html,
                      reporting::ReportRenderFormat::Json};  // ≥1 且必含 Html（RPT-02）
    inputs.destination.kind = reporting::ExportDestination::ExternalPath;
    inputs.destination.externalPath = "C:/exports/wf-t12";
    inputs.destination.replace = reporting::ReplacePolicy::NeverOverwrite;
    inputs.withEvidenceBundle = false;  // T12 阶段默认（RPT-03 能力位未开）
    h.dialog.next.confirmed = true;
    h.dialog.next.inputs = inputs;
    // 服务成功（项目外导出形态——files 在场）。
    reporting::ExportedFile file;
    file.format = reporting::ReportRenderFormat::Html;
    h.exportService.next.files.push_back(file);
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out = workflow::runWorkflowCommand("report.export", ports);
    EXPECT_TRUE(out.accepted) << "导出成功应受理";

    // 服务恰好一次＋请求逐字段零加工（组装是搬运不是再造——PA-1）。
    ASSERT_EQ(h.exportService.calls, 1);
    EXPECT_TRUE(h.exportService.received.project == inputs.project);
    EXPECT_TRUE(h.exportService.received.reportId == inputs.reportId);
    ASSERT_EQ(h.exportService.received.formats.size(), inputs.formats.size());
    EXPECT_EQ(h.exportService.received.formats[0], reporting::ReportRenderFormat::Html);
    EXPECT_EQ(h.exportService.received.formats[1], reporting::ReportRenderFormat::Json);
    EXPECT_EQ(h.exportService.received.destination.kind,
              reporting::ExportDestination::ExternalPath);
    EXPECT_EQ(h.exportService.received.destination.externalPath, inputs.destination.externalPath);
    EXPECT_EQ(h.exportService.received.destination.replace,
              reporting::ReplacePolicy::NeverOverwrite);
    EXPECT_EQ(h.exportService.received.withEvidenceBundle, false);
}

/**
 * @brief 对话框取消——Canceled 且导出服务**零触达**（UX-03 取消非错误；
 *        零副作用——半途放弃不留任何请求痕迹）。
 */
TEST(WfCommands, ReportExport_DialogCancel_ZeroServiceTouch_UX03)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{});

    Harness h;
    h.dialog.next.confirmed = false;  // 用户在导出对话框取消
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out = workflow::runWorkflowCommand("report.export", ports);
    EXPECT_FALSE(out.accepted) << "取消不是成功";
    EXPECT_EQ(h.dialog.calls, 1);
    EXPECT_EQ(h.exportService.calls, 0) << "取消路径不得触达导出服务";
}

/**
 * @brief 服务失败透传——Failed＋stableCode＝reporting 稳定错误词零加工
 *        （D-WF-7 零归码：R1 零新增 WF- 码——对端词形直通）。
 */
TEST(WfCommands, ReportExport_ServiceFailure_StableCodePassthrough)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03", "RPT-02"}, std::vector<std::string>{});

    Harness h;
    h.dialog.next.confirmed = true;
    h.exportService.next.error = reporting::ReportError{
        reporting::ReportErrorCode::ExportFailed, "disk io"};
    WorkflowCommandPorts ports = h.ports();

    const WorkflowCommandOutcome out = workflow::runWorkflowCommand("report.export", ports);
    EXPECT_FALSE(out.accepted);
    EXPECT_EQ(h.exportService.calls, 1);

    // 编排终态经编排核二次入口复核（runReportExportCommand 直调——
    // stableCode 透传是本核义务，折叠层不丢）。
    const ReportExportCommandOutcome detail =
        workflow::runReportExportCommand(h.dialog, h.exportService);
    EXPECT_EQ(detail.status, ReportExportCommandOutcome::Status::Failed);
    EXPECT_EQ(detail.stableCode, "reporting/export-failed") << "对端稳定词零加工透传";
    EXPECT_EQ(detail.reasonKey, "cmd.report.export.failed");
}

/**
 * @brief 导出检查点取消（服务返回五成员全空——reporting 落位偏差③形态）
 *        →Canceled 非错误。
 */
TEST(WfCommands, ReportExport_CancelAtCheckpoint_AllEmptyResult)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-03"}, std::vector<std::string>{});

    Harness h;
    h.dialog.next.confirmed = true;
    // next 默认全空＝取消形态（published/files/bundleDigest/error 全空）。
    WorkflowCommandPorts ports = h.ports();

    const ReportExportCommandOutcome detail =
        workflow::runReportExportCommand(h.dialog, h.exportService);
    EXPECT_EQ(detail.status, ReportExportCommandOutcome::Status::Canceled);
    EXPECT_EQ(detail.stableCode, "") << "取消非错误——无稳定码";
}

/**
 * @brief P-WF-4/P-UI-9 预览宿主缺位如实呈现——三种终态下
 *        previewHostAvailable 恒 false（不虚构预览通道；裁决后接线翻转）。
 */
TEST(WfCommands, ReportExport_PreviewHostHonestlyAbsent_PWF4)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{"P-WF-4", "P-UI-9"});

    Harness h;
    // 态一：对话框取消。
    h.dialog.next.confirmed = false;
    ReportExportCommandOutcome canceled =
        workflow::runReportExportCommand(h.dialog, h.exportService);
    EXPECT_FALSE(canceled.previewHostAvailable) << "缺位如实——取消态也不虚构";

    // 态二：成功（files 在场）。
    h.dialog.next.confirmed = true;
    reporting::ExportedFile file;
    file.format = reporting::ReportRenderFormat::Html;
    h.exportService.next.files.push_back(file);
    ReportExportCommandOutcome completed =
        workflow::runReportExportCommand(h.dialog, h.exportService);
    ASSERT_EQ(completed.status, ReportExportCommandOutcome::Status::Completed);
    EXPECT_FALSE(completed.previewHostAvailable) << "P-UI-9 缺位——成功态如实呈现不可用";

    // 态三：失败。
    h.exportService.next.files.clear();
    h.exportService.next.error = reporting::ReportError{
        reporting::ReportErrorCode::DiskFull, ""};
    ReportExportCommandOutcome failed =
        workflow::runReportExportCommand(h.dialog, h.exportService);
    ASSERT_EQ(failed.status, ReportExportCommandOutcome::Status::Failed);
    EXPECT_FALSE(failed.previewHostAvailable);
}

// =====================================================================
// 装配违约 fail-fast（§10.3 错误语义行）
// =====================================================================

/**
 * @brief 所需端口空指针＝装配违约 WorkflowError（五类各一——调用方错误
 *        fail-fast：静默执行会把命令丢进黑洞）。
 */
TEST(WfCommands, MissingPortDependency_FailFast_AssemblyViolation)
{
    IRD_TEST_INFO(std::vector<std::string>{}, std::vector<std::string>{});

    Harness h;
    WorkflowCommandPorts ports = h.ports();

    // 修订类：修订端口空。
    ports.revisions = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("project.undo", ports), WorkflowError);
    ports = h.ports();

    // 流程类：启动端口空。
    ports.lifecycle = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("project.new", ports), WorkflowError);
    ports = h.ports();

    // 会话类：会话端口空。
    ports.session = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("view.resetZero", ports), WorkflowError);
    ports = h.ports();

    // 碰撞类：碰撞端口空。
    ports.collision = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("analysis.collisionCheck", ports), WorkflowError);
    ports = h.ports();

    // 报告导出：双端口任一空即违约（参数收集缺位——无输入源；服务缺位
    // ——编排目标缺）。
    ports.reportDialog = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("report.export", ports), WorkflowError);
    ports = h.ports();
    ports.reportExport = nullptr;
    EXPECT_THROW(workflow::runWorkflowCommand("report.export", ports), WorkflowError);
}

/**
 * @brief 确定性重放（NFR-COR-02 同型）——同桩配置双跑编排核，输出逐字
 *        段一致（无隐藏会话态）。
 */
TEST(WfCommands, Determinism_ReplaySameOutcome)
{
    IRD_TEST_INFO(std::vector<std::string>{"NFR-COR-02"}, std::vector<std::string>{});

    Harness h;
    h.session.next = SessionActionResult{true, ""};
    h.revisions.next.committed = true;
    h.revisions.next.newRevisionId = std::string{"rev-00000007"};
    WorkflowCommandPorts ports = h.ports();

    for (const char* id : {"draft.apply", "draft.save", "view.displayMode",
                           "analysis.collisionCheck"}) {
        const WorkflowCommandOutcome first = workflow::runWorkflowCommand(id, ports);
        const WorkflowCommandOutcome second = workflow::runWorkflowCommand(id, ports);
        EXPECT_EQ(first.accepted, second.accepted) << "重放一致：" << id;
        EXPECT_EQ(first.messageKey, second.messageKey) << "重放一致：" << id;
    }
}

}  // namespace
