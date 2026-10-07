/**
 * @file   NewProjectWizardContractTest.cpp
 * @brief  新建项目三步向导的契约测试（WF-VER-201~204——units/workflow.md
 *         §11.2 生命周期主线；PM-01/AT-20 的落盘承载半区）。
 *
 * 设计依据：
 *   - units/workflow.md §7.1（三步向导——确认→①命令端口/项目创建协议→
 *     外部资源处置→进入项目；取消清理临时区不留半成品）、§11.2（用例
 *     201 新建主线〔观测点＝修订/元数据〕、202 取消不留半成品〔观测点＝
 *     .staging/目录〕、203 失败留输入〔观测点＝诊断字段〕、204 URDF
 *     来源与外部引用〔观测点＝引用记录〕——四用例类型均为"契约"）、
 *     §10.3（接口属性表——向导可取消，取消即清理）
 *   - REQUIREMENTS.md §17 PM-01/AT-20 原文（取消或失败不留半成品；URDF
 *     项目以不可修改基线修订保存；外部资源复制入资源区或登记外部引用
 *     记录二选一；向导不预填数值——P-03）
 *   - project.md §5.1（createNew＝PM-01 存储侧——失败清理目标目录）、
 *     §13.2 workflow 行（open/createNew 服务调用序列＝向导编排）
 *   - 任务契约 tasks/foundation/WP-22-T04.json acceptance 1/2/3
 *
 * 契约测试形态（§11.0——"契约测试＝跨单元联合"）：本文件与 project
 * 真实存储实现联合（ProjectStoreFactory::createNew 真实落盘临时目录、
 * ①端口真实提交）——"模板基线修订"观测点经真实修订链承载；领域命令
 * 处理器为**测试桩**（模拟 L5 装配层注册的建模处理器——本目标不链
 * modeling，R-1：领域载荷组装归装配层的 modeling 公共契约消费面，
 * workflow 侧只经 IDomainInitSubmitter 端口触达，acceptance 3"不直链"
 * 的结构性证明面）。创建编排在确认前零副作用（202）与失败收尾清理
 * （203）都在真实磁盘上验证——不留半成品不是声明，是可复核事实。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <sdurws/ird/core/DiagData.hpp>         // DiagnosticRecord（失败注入诊断——C-3 工厂）
#include <sdurws/ird/project/CommandService.hpp> // CommandEnvelope/ICommandHandler（①端口测试桩）
#include <sdurws/ird/project/QueryPort.hpp>     // branchTips/branchHistory（修订观测点）
#include <sdurws/ird/workflow/Lifecycle.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using workflow::DomainInitRequest;
using workflow::DomainInitResult;
using workflow::InitialSourceKind;
using workflow::InstallPreset;
using workflow::NewProjectInputs;
using workflow::TemplateKind;

// =====================================================================
// 测试桩：领域基线种子处理器（①端口真实提交的领域命令面）
// =====================================================================

/**
 * @brief 领域基线种子处理器（测试桩——模拟 L5 装配层注册的建模处理器）。
 *
 * 为什么不是注册真实 modeling 处理器：R-1——workflow 契约测试目标只链
 * 同单元产品目标（BuildGraphContractTest 判据），modeling 库不可达。
 * 本桩证明的是 workflow 侧的编排契约：向导确认后领域初始化**经①端口
 * 提交并真实产生基线修订**（CommandEnvelope→ProjectCommandService::
 * submit→Committed{newRevision}——七步事务）；载荷组装的 modeling 公共
 * 契约消费面归装配层（接口边界注释见 Lifecycle.hpp IDomainInitSubmitter）。
 */
class BaselineSeedHandler final : public project::ICommandHandler {
public:
    /// 注册 token（§4.4.4 语法 ^[a-z0-9-]{3,64}——无点形态，D-MDL-6 同源）。
    [[nodiscard]] std::string commandType() const override
    {
        return "wf-contract-baseline-seed";
    }
    /// 受理载荷版本（无单位格式版本号）。
    [[nodiscard]] std::uint32_t currentPayloadVersion() const override { return 1; }

    /**
     * @brief 产出最小合法计划（一个探测对象写入——基线修订的实体面）。
     *
     * payloadCanonical/project 不解释（D-10 透传存储）；objectTypeToken
     * 用域登记词形 "TestProbe"（测试桩域——随修订清单透传登记）。
     */
    project::PrepareOutcome prepare(project::HandlerContext& ctx,
                                    const project::CommandEnvelope& /*envelope*/,
                                    const project::RevisionView& /*baseSnapshot*/,
                                    project::CommandPlan& out,
                                    std::vector<core::DiagnosticRecord>& /*diags*/) override
    {
        project::ObjectWrite write;
        write.objectId = ctx.objectId();  // project 分配对象身份（PA-1）
        write.objectTypeToken = "TestProbe";
        write.payloadCanonical = {'s', 'e', 'e', 'd'};  // 不透明载荷字节
        out.objectWrites.push_back(std::move(write));
        out.summary = "wf 契约测试：领域基线种子修订（模拟建模模板/导入基线）";
        return project::PrepareOutcome::Planned;
    }
};

// =====================================================================
// 测试桩：录制式领域初始化提交器（IDomainInitSubmitter——装配层替身）
// =====================================================================

/**
 * @brief 录制式提交器——模拟 L5 装配层实现：捕获请求（204 的处置断言面）、
 *        经①端口真实提交（201 的基线修订观测面）、可注入失败（203 的
 *        失败收尾触发面）。
 *
 * 失败注入语义：failMode=true 时返回 committed=false＋一条注入诊断
 * （码 WF-CONTRACT-TEST-INJECTED——**测试材料**，标注注入语义，非产品
 * 登记码；产品面零新增码 D-WF-7 不受影响）＋UX-03 原因/建议半区。
 */
class RecordingSubmitter final : public workflow::IDomainInitSubmitter {
public:
    bool failMode = false;                              ///< 失败注入开关（203）
    int calls = 0;                                      ///< 调用计数（blank 零触达断言）
    std::optional<DomainInitRequest> lastRequest;       ///< 最近请求（204 断言面）

    DomainInitResult submitInitialization(project::ProjectStore& store,
                                          const DomainInitRequest& request) override
    {
        ++calls;
        lastRequest = request;  // 原样录制（编排器组装结果的照抄面）

        if (failMode) {
            DomainInitResult failed;
            failed.committed = false;
            // 注入诊断（C-3 工厂校验——码句法 ^[A-Z0-9]+(-[A-Z0-9]+)*$）。
            failed.diagnostics.push_back(core::DiagnosticRecord::make(
                "WF-CONTRACT-TEST-INJECTED", std::nullopt, std::nullopt,
                std::nullopt, "向导领域初始化（契约测试注入）",
                "注入的领域初始化失败（模拟模板被对端拒绝/源文件不可读）",
                "请检查来源与模板可用性后重试"));
            failed.causeText = "注入的领域初始化失败（契约测试）";
            failed.actionText = "请检查来源与模板可用性后重试（输入已保留）";
            return failed;
        }

        // 真实提交：主分支（createNew 后唯一分支，label "main"——P-PR-8）
        // 取 branchTips 首条 id（分支表投影——workflow 侧不生成新 ID）。
        // 领域处理器注册（装配期通道——store.handlerRegistry() 公共访问器；
        // project 契约"打开成功回调内、任何 submit 之前"的注册时序：本桩
        // 在 submitInitialization 内注册正是该时序的模拟——store 由
        // commit 内部 createNew 产生，装配层拿到 store 后注册、再提交）。
        // 判重注册（新 store 新注册表；防同 store 二次调用重复注册被
        // invalid_argument 拒绝——注册表边界契约）。
        {
            project::HandlerRegistry& registry = store.handlerRegistry();
            const auto tokens = registry.registeredCommandTypes();
            const bool already =
                std::find(tokens.begin(), tokens.end(),
                          std::string{"wf-contract-baseline-seed"})
                != tokens.end();
            if (!already) {
                registry.registerHandler(std::make_unique<BaselineSeedHandler>());
            }
        }
        DomainInitResult ok;
        project::CommandEnvelope envelope;
        envelope.branch = store.query().branchTips().at(0).id;
        envelope.commandType = "wf-contract-baseline-seed";
        envelope.payloadFormatVersion = 1;
        envelope.payloadCanonical = {'b', 'a', 's', 'e'};
        const project::CommandResult result = store.commands().submit(envelope);
        ok.committed = result.committed();
        ok.baselineRevision = result.newRevision;
        ok.diagnostics = result.diagnostics;  // 对端诊断原样透传（零加工）
        if (!ok.committed) {
            // 提交失败的兜底呈现材料（拒绝理由 → 原因半区——UX-03）。
            ok.causeText = "领域命令提交未提交成功（契约测试兜底呈现）";
            ok.actionText = "请检查来源与模板可用性后重试";
        }
        return ok;
    }
};

// =====================================================================
// 夹具：临时目录（套件级总根＋用例级独立项目目录——project 测试同型）
// =====================================================================

class NewProjectWizardContract : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        std::error_code ec;
        s_base = fs::temp_directory_path(ec) / "ird_wf_wizard_contract_test";
        ASSERT_FALSE(ec);
        fs::remove_all(s_base, ec);  // 前次运行残留防御（总根重建）
        fs::create_directories(s_base, ec);
        ASSERT_FALSE(ec) << "无法创建测试根目录: " << s_base.string();
    }

    static void TearDownTestSuite()
    {
        // 总根清理：失败保留现场（"TempDir 失败保留"惯例——与 project
        // 测试同型）。
        std::error_code ec;
        fs::remove_all(s_base, ec);
    }

    void SetUp() override
    {
        // 用例级项目目录（自增子目录——用例间零共享状态）。
        m_dir = s_base / ("case" + std::to_string(++s_caseCounter))
                / "proj.rwdesign";
    }

    /// 模板输入夹具（目录指向本用例的独立路径；P-03 零数值——夹具无
    /// 任何模板数值字段可填，即"向导不预填数值"的构造面证据）。
    NewProjectInputs templateInputs() const
    {
        NewProjectInputs inputs;
        inputs.displayName = "契约线体";
        inputs.directory = m_dir;
        inputs.source = InitialSourceKind::Template;
        inputs.templateKind = TemplateKind::SixAxis;
        inputs.installPreset = InstallPreset::Ground;
        inputs.templateLocalName = "robot-1";
        return inputs;
    }

    /// URDF 输入夹具（处置缺省＝复制入资源区）。
    NewProjectInputs urdfInputs() const
    {
        NewProjectInputs inputs;
        inputs.displayName = "契约导入";
        inputs.directory = m_dir;
        inputs.source = InitialSourceKind::UrdfXacro;
        inputs.sourceFile = s_base / "sources" / "robot.urdf";
        return inputs;
    }

    static fs::path s_base;      ///< 套件级总根（SetUpTestSuite 建/拆）
    static int s_caseCounter;    ///< 用例自增计数（目录唯一性）
    fs::path m_dir;              ///< 本用例项目目录
};

fs::path NewProjectWizardContract::s_base;
int NewProjectWizardContract::s_caseCounter = 0;

}  // namespace

// =====================================================================
// WF-VER-201 新建主线（PM-01/AT-20——观测点：修订/元数据）
// =====================================================================

/**
 * 模板主线：三步确认→createNew＋领域初始化经①端口→基线修订真实产生。
 * 断言面：created＋projectId 有效＋baselineRevision 有效＋上下文可写＋
 * 主分支修订历史计数＝2（r0 骨架＋基线种子——"模板基线修订"观测点）＋
 * 提交请求字段与输入一致（baselineReadOnly=false——模板基线可编辑）。
 */
TEST_F(NewProjectWizardContract, WF_VER_201_TemplateMainlineCreatesBaselineRevision)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    BaselineSeedHandler seedHandler;   // 桩处理器（注册面经公共通道）
    RecordingSubmitter submitter;
    const NewProjectInputs inputs = templateInputs();

    workflow::NewProjectOutcome outcome =
        workflow::NewProjectWizardFlow::commit(inputs, &submitter);

    // 创建成功（不留半成品的正面：半成品反面即全量创建完成）。
    ASSERT_TRUE(outcome.created) << "模板主线创建失败";
    ASSERT_TRUE(outcome.store != nullptr);
    ASSERT_TRUE(outcome.projectId.has_value());
    EXPECT_TRUE(outcome.projectId->isValid());          // prj-（非全零——core 保留值纪律）
    ASSERT_TRUE(outcome.baselineRevision.has_value());
    EXPECT_TRUE(outcome.baselineRevision->isValid());   // rev-（基线修订身份）
    EXPECT_TRUE(outcome.store->writable());             // 创建者即首个写权限持有者

    // 修订/元数据观测点：主分支历史恰好 2 修订（r0＋基线——createNew
    // 初始修订＋领域基线种子；确定性面）。
    const auto tips = outcome.store->query().branchTips();
    ASSERT_EQ(tips.size(), 1u);                          // createNew 单主分支
    const auto history =
        outcome.store->query().branchHistory(tips[0].id, 10);
    EXPECT_EQ(history.size(), 2u)
        << "主分支修订数应为 2（r0＋模板基线），实得 " << history.size();
    EXPECT_EQ(tips[0].tip, *outcome.baselineRevision);   // tip＝基线修订

    // 编排请求面：来源参数照抄输入；模板基线可编辑（baselineReadOnly=false）。
    ASSERT_TRUE(submitter.lastRequest.has_value());
    EXPECT_EQ(submitter.lastRequest->source, InitialSourceKind::Template);
    EXPECT_EQ(submitter.lastRequest->templateKind, TemplateKind::SixAxis);
    EXPECT_EQ(submitter.lastRequest->installPreset, InstallPreset::Ground);
    EXPECT_EQ(submitter.lastRequest->localName, "robot-1");
    EXPECT_FALSE(submitter.lastRequest->baselineReadOnly);

    outcome.store.reset();  // 用例收尾释放写锁（目录随套件总根清理）
}

/**
 * URDF 主线：基线修订只读保存的**编排登记面**（PM-12/PM-01——URDF 项目
 * 以不可修改基线修订保存；只读强制在 project/modeling 侧，workflow 侧
 * 的可复核事实是编排请求恒带 baselineReadOnly=true——用户不可改的非
 * 输入项，编排器填充规则契约）。
 */
TEST_F(NewProjectWizardContract, WF_VER_201_UrdfSourceRegistersReadOnlyBaseline)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01", "PM-12"}, std::vector<std::string>{"AT-20"});

    RecordingSubmitter submitter;
    workflow::NewProjectOutcome outcome =
        workflow::NewProjectWizardFlow::commit(urdfInputs(), &submitter);

    ASSERT_TRUE(outcome.created);
    ASSERT_TRUE(outcome.baselineRevision.has_value());
    EXPECT_TRUE(outcome.baselineRevision->isValid());

    ASSERT_TRUE(submitter.lastRequest.has_value());
    EXPECT_EQ(submitter.lastRequest->source, InitialSourceKind::UrdfXacro);
    EXPECT_TRUE(submitter.lastRequest->baselineReadOnly)
        << "URDF 来源的领域初始化请求必须登记基线只读（PM-01/PM-12）";

    outcome.store.reset();
}

/**
 * 空白主线：止于 createNew 的初始修订 r0——领域初始化零触达（提交端口
 * 未消费），无基线修订可登记（baselineRevision=nullopt）。
 */
TEST_F(NewProjectWizardContract, WF_VER_201_BlankMainlineStopsAtR0)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    RecordingSubmitter submitter;
    workflow::NewProjectOutcome outcome =
        workflow::NewProjectWizardFlow::commit(
            [&] {
                NewProjectInputs in;
                in.displayName = "契约空白";
                in.directory = m_dir;
                return in;  // source 缺省＝Blank（最小输入面）
            }(),
            &submitter);

    ASSERT_TRUE(outcome.created);
    EXPECT_FALSE(outcome.baselineRevision.has_value()) << "空白来源无基线修订";
    EXPECT_EQ(submitter.calls, 0) << "空白来源不得触达领域初始化端口";

    // 修订观测点：主分支恰好 1 修订（r0 骨架）。
    const auto tips = outcome.store->query().branchTips();
    ASSERT_EQ(tips.size(), 1u);
    EXPECT_EQ(outcome.store->query().branchHistory(tips[0].id, 10).size(), 1u);

    outcome.store.reset();
}

// =====================================================================
// WF-VER-202 新建取消不留半成品（PM-01/AT-20——观测点：.staging/目录）
// =====================================================================

/**
 * 取消＝确认前放弃：编排器只有 commit 一个写路径入口——确认前任何步骤
 * （校验/摘要组装/步骤流转）零文件系统副作用，取消后目标位置零残留
 * （无目录、无 .staging 组装区——createNew 从未被调用）。
 */
TEST_F(NewProjectWizardContract, WF_VER_202_CancelBeforeConfirmLeavesNothing)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    RecordingSubmitter submitter;
    const NewProjectInputs inputs = templateInputs();

    // 确认前的向导动作（步骤①②流转＝校验与摘要的纯函数消费）。
    EXPECT_TRUE(workflow::validateStep(workflow::NewProjectStep::ProjectInfo, inputs).empty());
    EXPECT_TRUE(workflow::validateStep(workflow::NewProjectStep::InitialSource, inputs).empty());
    (void)workflow::buildNewProjectSummary(inputs);

    // 用户在此取消（不调 commit）——目标位置零残留（目录与任何组装区
    // 均不存在——"不留半成品"的取消语义在真实磁盘上复核）。
    std::error_code ec;
    EXPECT_FALSE(fs::exists(m_dir, ec) && !ec)
        << "确认前取消不得产生任何项目目录/组装区残留";

    // 提交端口零触达（取消路径不经过领域初始化）。
    EXPECT_EQ(submitter.calls, 0);
}

// =====================================================================
// WF-VER-203 新建失败留输入（PM-01——观测点：诊断字段）
// =====================================================================

/**
 * 领域初始化失败（创建命令失败——project 保证无修订）：失败呈现 UX-03
 * 三字段齐备；本次创建的项目目录被收尾清理（不留半成品——PM-01"取消
 * 或失败不留半成品"的失败支）；输入零改写可原样重试（重试成功）。
 */
TEST_F(NewProjectWizardContract, WF_VER_203_DomainInitFailureCleansAndKeepsInputs)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01", "UX-03"}, std::vector<std::string>{"AT-20"});

    RecordingSubmitter submitter;
    submitter.failMode = true;  // 注入领域初始化失败（模板被对端拒绝形态）
    NewProjectInputs inputs = templateInputs();
    const NewProjectInputs inputsBefore = inputs;  // 保留对照（零改写断言）

    workflow::NewProjectOutcome outcome =
        workflow::NewProjectWizardFlow::commit(inputs, &submitter);

    // 失败呈现（UX-03 三字段：对象/上下文、原因、建议动作——诊断字段
    // 观测点；原因含注入标记防"吞错"假绿）。
    ASSERT_FALSE(outcome.created);
    EXPECT_EQ(outcome.store, nullptr) << "失败路径不得移交存储上下文";
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_EQ(outcome.failure->context, inputs.directory.u8string());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_NE(outcome.failure->cause.find("注入"), std::string::npos)
        << "失败原因应承载提交端口的原因半区（吞错即假绿）";
    EXPECT_FALSE(outcome.failure->recommendedAction.empty());

    // 不留半成品：目录已被收尾清理（requestClose→remove_all——编排责任段）。
    std::error_code ec;
    EXPECT_FALSE(fs::exists(m_dir, ec) && !ec)
        << "领域初始化失败后不得残留项目目录（取消/失败不留半成品）";

    // 输入保留：inputs 与提交前逐字段相等（编排器零改写——WF-VER-203
    // 观测点"输入保留可重试"的机器面）。
    EXPECT_EQ(inputs, inputsBefore);

    // 原样重试（失败消除后）→ 成功——"保留输入供重试"的闭环。
    submitter.failMode = false;
    workflow::NewProjectOutcome retry =
        workflow::NewProjectWizardFlow::commit(inputs, &submitter);
    ASSERT_TRUE(retry.created);
    ASSERT_TRUE(retry.baselineRevision.has_value());
    retry.store.reset();
}

/**
 * 项目创建本身失败（createNew 环境失败——project 侧保证失败清理目标
 * 目录）：注入形态＝目标位置是**普通文件**（createNew @pre"须不存在
 * 或为空目录"违约；向导校验对该形态放行——exists 但非目录不触
 * directory-exists-nonempty 键，失败最终在 createNew 侧暴露并被编排
 * 器折叠为失败呈现；非空目录形态则被前置校验提前拦截〔fail-fast，
 * 到不了创建——那正是校验的呈现性前置语义〕）。断言：错误呈现＋
 * 输入保留＋换位置重试成功。
 */
TEST_F(NewProjectWizardContract, WF_VER_203_CreateNewFailurePresentsAndRetries)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01", "UX-03"}, std::vector<std::string>{"AT-20"});

    RecordingSubmitter submitter;

    // 预置普通文件占用目标位置（createNew @pre 违约形态——校验放行、
    // 创建侧暴露）。
    std::error_code ec;
    fs::create_directories(m_dir.parent_path(), ec);
    ASSERT_FALSE(ec);
    { std::ofstream marker(m_dir); marker << "occupied"; }
    ASSERT_TRUE(fs::is_regular_file(m_dir, ec));

    // 前置校验放行（文件非目录——校验键不命中，证明失败注入确实
    // 到达创建段而非被前置拦截）。
    EXPECT_TRUE(workflow::validateNewProjectInputs(templateInputs()).empty());

    workflow::NewProjectOutcome outcome =
        workflow::NewProjectWizardFlow::commit(templateInputs(), &submitter);

    // 失败呈现（cause 透传对端 what()——非空）。
    ASSERT_FALSE(outcome.created);
    EXPECT_EQ(outcome.store, nullptr);
    ASSERT_TRUE(outcome.failure.has_value());
    EXPECT_FALSE(outcome.failure->cause.empty());
    EXPECT_EQ(outcome.failure->context, m_dir.u8string());

    // 占位文件不因失败路径被二次破坏（对端"失败清理目标目录"只清
    // 自己组装的半成品；本用例不假设文件去留，只断言失败后无半成品
    // 上下文残留——store 为空）。

    // 换位置重试（输入修正）→ 成功——失败呈现后的用户出路（UX-03
    // 建议动作"更换项目位置"的可执行闭环）。
    NewProjectInputs retried = templateInputs();
    retried.directory = s_base / ("case" + std::to_string(s_caseCounter))
                        / "retried.rwdesign";
    workflow::NewProjectOutcome second =
        workflow::NewProjectWizardFlow::commit(retried, &submitter);
    ASSERT_TRUE(second.created);
    second.store.reset();
}

// =====================================================================
// WF-VER-204 URDF 来源与外部引用（PM-01/CON-03——观测点：引用记录）
// =====================================================================

/**
 * 外部资源二选一处置的编排传递面：两种处置选择都被编排器**原样**装进
 * 领域初始化请求（记录实体与固化执行归 io/project——workflow 侧的可
 * 复核契约是处置选择零丢失零改写）；两例均登记基线只读（URDF）。
 */
TEST_F(NewProjectWizardContract, WF_VER_204_ExternalHandlingBothWaysForwarded)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01", "CON-03"}, std::vector<std::string>{"AT-20"});

    // 两子块各自独立项目目录（commit 是完整创建——同目录二次创建会被
    // 前置校验 directory-exists-nonempty 正确拦截，那不是本用例场景）。
    int subCase = 0;
    const auto freshDir = [&]() {
        return s_base / ("case" + std::to_string(s_caseCounter))
               / ("sub" + std::to_string(++subCase) + ".rwdesign");
    };

    // 处置一：复制入项目资源区（Recorded→Solidified 提前——CON-03）。
    {
        RecordingSubmitter submitter;
        NewProjectInputs inputs = urdfInputs();
        inputs.directory = freshDir();
        inputs.externalHandling =
            workflow::ExternalResourceHandling::CopyIntoResources;
        auto outcome =
            workflow::NewProjectWizardFlow::commit(inputs, &submitter);
        ASSERT_TRUE(outcome.created);
        ASSERT_TRUE(submitter.lastRequest.has_value());
        EXPECT_EQ(submitter.lastRequest->externalHandling,
                  workflow::ExternalResourceHandling::CopyIntoResources)
            << "处置选择（复制入资源区）必须原样传递给领域链路";
        EXPECT_TRUE(submitter.lastRequest->baselineReadOnly);
        EXPECT_EQ(submitter.lastRequest->sourceFile, inputs.sourceFile);
        outcome.store.reset();
    }

    // 处置二：登记外部引用记录（维持 Recorded——{绝对路径＋内容哈希}，
    // 转正式固化 CON-03；记录实体归 io ExternalRefRecord/project 持久层）。
    {
        RecordingSubmitter submitter;
        NewProjectInputs inputs = urdfInputs();
        inputs.directory = freshDir();
        inputs.externalHandling =
            workflow::ExternalResourceHandling::RecordExternalReference;
        auto outcome =
            workflow::NewProjectWizardFlow::commit(inputs, &submitter);
        ASSERT_TRUE(outcome.created);
        ASSERT_TRUE(submitter.lastRequest.has_value());
        EXPECT_EQ(submitter.lastRequest->externalHandling,
                  workflow::ExternalResourceHandling::RecordExternalReference)
            << "处置选择（登记外部引用）必须原样传递给领域链路";
        EXPECT_TRUE(submitter.lastRequest->baselineReadOnly);
        outcome.store.reset();
    }
}

// =====================================================================
// acceptance 3：模板/导入经①端口不直链（workflow 侧结构性证据）
// =====================================================================

/**
 * 产品面零 modeling 触达（acceptance 3——"模板/导入经 modeling 公共
 * 契约与①端口，不直链"）：Lifecycle.hpp/.cpp 等产品面源码（include/＋
 * src/）零 "ird/modeling" include 词形；链接面由既有
 * WfBuildGraph.NoBusinessUnitOrExtraPlatformEdge_WP22T02_ACC2（八边外
 * 零链接边）承载——本用例补源码面的具名自证（两判据合取即 R-1 完整面）。
 */
TEST_F(NewProjectWizardContract, WF_VER_Port_ModelingNotDirectlyLinked)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 产品面文件收集（与 BuildRedLineTest 同判据：include/＋src/）。
    const fs::path unitRoot = fs::path{IRD_WORKFLOW_UNIT_ROOT} / "workflow";
    std::vector<fs::path> files;
    for (const auto* sub : {"include", "src"}) {
        const auto base = unitRoot / sub;
        ASSERT_TRUE(fs::exists(base)) << "产品面目录缺失: " << sub;
        std::error_code iec;
        for (auto it = fs::recursive_directory_iterator(base, iec);
             it != fs::recursive_directory_iterator(); it.increment(iec)) {
            if (iec || !it->is_regular_file(iec)) { continue; }
            const auto ext = it->path().extension().string();
            if (ext == ".hpp" || ext == ".cpp") { files.push_back(it->path()); }
        }
    }
    ASSERT_FALSE(files.empty());

    for (const auto& file : files) {
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(static_cast<bool>(in)) << "无法读取: " << file.string();
        const std::string text{std::istreambuf_iterator<char>(in),
                               std::istreambuf_iterator<char>()};
        EXPECT_EQ(text.find("ird/modeling"), std::string::npos)
            << "产品面出现 modeling include（R-1 不直链——acceptance 3）: "
            << file.string();
    }
}
