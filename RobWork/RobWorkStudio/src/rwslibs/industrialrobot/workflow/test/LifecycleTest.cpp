/**
 * @file   LifecycleTest.cpp
 * @brief  新建项目三步向导的模型测试（直调纯函数面——NFR-MNT-01）：
 *         WfLifecycle 词表/校验/摘要/零数值钉扎与 commit 前置契约
 *         （units/workflow.md §7.1/§11.2——WP-22-T04 落位批次）。
 *
 * 设计依据：
 *   - units/workflow.md §7.1（三步向导——步骤/校验/摘要/处置词表）、
 *     §11.0（用例登记约定——模型测试直调计算库纯函数面）、§11.2
 *     （WF-VER-201~204 的纯函数承载半区；创建编排的落盘半区在
 *     contract_test/NewProjectWizardContractTest.cpp——契约类型用例）
 *   - 需求 PM-01（三步结构/实时摘要/取消失败不留半成品/URDF 基线只读/
 *     外部资源二选一/向导不预填数值——P-03）、UX-02（工程用语键）
 *   - 任务契约 tasks/foundation/WP-22-T04.json acceptance 1/3（三步向导
 *     ＋实时摘要；P-03 未冻结——向导不预填数值）
 *
 * 用例与期望值口径：本单元判定面为枚举/键串/路径事实——数值期望均为
 * 解析给定的精确串（无浮点容差需求，I-WF-3 零领域阈值同源）。每个公共
 * 接口方法至少一条经接口消费的用例钉扎（WP-20-T03 首轮教训——不留
 * 只测自由函数的盲区）：commit 的前置契约用例在本文件，落盘路径在契约
 * 测试（同接口，另一路径——两处合计覆盖公共面全部方法）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记（testkit §7.3）

#include <sdurws/ird/workflow/Lifecycle.hpp>
#include <sdurws/ird/workflow/Types.hpp>  // WorkflowError（前置契约断言）

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using workflow::InitialSourceKind;
using workflow::InstallPreset;
using workflow::NewProjectInputs;
using workflow::NewProjectStep;
using workflow::TemplateKind;

// =====================================================================
// 输入夹具（各用例独立构造——无共享可变状态；§11.0 夹具纪律）
// =====================================================================

/// 模板来源的最小合法输入（六轴×地面——P-03 零数值：夹具本身即证据，
/// 没有任何数值字段可填）。
NewProjectInputs makeTemplateInputs()
{
    NewProjectInputs inputs;
    inputs.displayName = "演示线体A";
    inputs.directory = std::filesystem::path{"X:/nonexistent-ro-demo/proj-a.rwdesign"};
    inputs.source = InitialSourceKind::Template;
    inputs.templateKind = TemplateKind::SixAxis;
    inputs.installPreset = InstallPreset::Ground;
    inputs.templateLocalName = "robot-a";
    return inputs;
}

/// URDF 来源的最小合法输入（处置缺省＝复制入资源区）。
NewProjectInputs makeUrdfInputs()
{
    NewProjectInputs inputs;
    inputs.displayName = "导入项目B";
    inputs.directory = std::filesystem::path{"X:/nonexistent-ro-demo/proj-b.rwdesign"};
    inputs.source = InitialSourceKind::UrdfXacro;
    inputs.sourceFile = std::filesystem::path{"D:/sources/robot-b.urdf"};
    inputs.externalHandling = workflow::ExternalResourceHandling::CopyIntoResources;
    return inputs;
}

/// 空白来源的最小合法输入（缺省来源即 Blank——最小输入面）。
NewProjectInputs makeBlankInputs()
{
    NewProjectInputs inputs;
    inputs.displayName = "空白项目C";
    inputs.directory = std::filesystem::path{"X:/nonexistent-ro-demo/proj-c.rwdesign"};
    return inputs;
}

}  // namespace

// =====================================================================
// 步骤词表（PM-01 三步冻结——WfLifecycle 组）
// =====================================================================

/// 三步冻结序与计数（PM-01：项目信息→初始来源→确认——§7.1 流程序）。
TEST(WfLifecycle, StepSequenceFrozen_PM01_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    const auto& seq = workflow::newProjectStepSequence();
    ASSERT_EQ(seq.size(), workflow::kNewProjectStepCount);
    ASSERT_EQ(seq.size(), 3u);  // 三步——需求冻结（1 项目信息 2 初始来源 3 确认）
    EXPECT_EQ(seq[0], NewProjectStep::ProjectInfo);
    EXPECT_EQ(seq[1], NewProjectStep::InitialSource);
    EXPECT_EQ(seq[2], NewProjectStep::Confirm);
    // 重复调用同输出（词表稳定性——确定性 NFR-COR-02 同型）。
    EXPECT_EQ(seq, workflow::newProjectStepSequence());
}

// =====================================================================
// 词表 token 规范化（唯一映射点——提交/摘要共用词形）
// =====================================================================

/// 各枚举 → token 的精确词形（安装三值/模板两值/来源三值/处置两值）。
TEST(WfLifecycle, TokenMappingExactForms_ACC1)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{});

    // 安装预设（§7.1"地面/墙面/倒挂"——modeling §5.1 词形对照）。
    EXPECT_EQ(workflow::installPresetToken(InstallPreset::Ground), "ground");
    EXPECT_EQ(workflow::installPresetToken(InstallPreset::Wall), "wall");
    EXPECT_EQ(workflow::installPresetToken(InstallPreset::Inverted), "inverted");
    // 模板类别（modeling.md §5.1 登记词形 generic-6r/generic-7r 对照）。
    EXPECT_EQ(workflow::templateKindToken(TemplateKind::SixAxis), "generic-6r");
    EXPECT_EQ(workflow::templateKindToken(TemplateKind::SevenAxis), "generic-7r");
    // 来源三值。
    EXPECT_EQ(workflow::initialSourceToken(InitialSourceKind::Template), "template");
    EXPECT_EQ(workflow::initialSourceToken(InitialSourceKind::UrdfXacro), "urdf-xacro");
    EXPECT_EQ(workflow::initialSourceToken(InitialSourceKind::Blank), "blank");
    // 外部资源处置二选一（PM-01/CON-03 两段词形）。
    EXPECT_EQ(workflow::externalHandlingToken(
                  workflow::ExternalResourceHandling::CopyIntoResources),
              "copy-into-resources");
    EXPECT_EQ(workflow::externalHandlingToken(
                  workflow::ExternalResourceHandling::RecordExternalReference),
              "record-external-reference");
}

// =====================================================================
// 输入校验（步骤放行判定的纯函数面）
// =====================================================================

/// 步骤①：空显示名/空目录/已存在非空目录三键逐项命中；合法输入放行。
TEST(WfLifecycle, ValidateProjectInfoStep_KeysAndPass)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 空显示名。
    NewProjectInputs noName = makeTemplateInputs();
    noName.displayName.clear();
    auto e1 = workflow::validateStep(NewProjectStep::ProjectInfo, noName);
    ASSERT_EQ(e1.size(), 1u);
    EXPECT_EQ(e1[0], "wizard.new-project.error.display-name-empty");

    // 空目录路径。
    NewProjectInputs noDir = makeTemplateInputs();
    noDir.directory.clear();
    auto e2 = workflow::validateStep(NewProjectStep::ProjectInfo, noDir);
    ASSERT_EQ(e2.size(), 1u);
    EXPECT_EQ(e2[0], "wizard.new-project.error.directory-empty");

    // 已存在且非空目录（临时真实目录——夹具自持自清）。
    // 用例在系统临时目录下建唯一子目录并放入一个文件（createNew @pre
    // 违约形态的前置呈现检查）。
    const auto base = std::filesystem::temp_directory_path()
                      / "wf-lifecycle-validate-projinfo";
    std::filesystem::remove_all(base);  // 先清（前次运行残留防御）
    const auto occupied = base / "occupied.rwdesign";
    std::filesystem::create_directories(occupied);
    { std::ofstream(occupied / "keep.txt") << "x"; }  // 非空标记文件
    NewProjectInputs badDir = makeTemplateInputs();
    badDir.directory = occupied;
    auto e3 = workflow::validateStep(NewProjectStep::ProjectInfo, badDir);
    ASSERT_EQ(e3.size(), 1u);
    EXPECT_EQ(e3[0], "wizard.new-project.error.directory-exists-nonempty");

    // 已存在但**空**目录放行（createNew @pre 允许复用空目录）。
    const auto emptyDir = base / "empty.rwdesign";
    std::filesystem::create_directories(emptyDir);
    NewProjectInputs okDir = makeTemplateInputs();
    okDir.directory = emptyDir;
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::ProjectInfo, okDir).empty());

    // 合法输入全放行。
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::ProjectInfo, makeTemplateInputs()).empty());
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::ProjectInfo, makeBlankInputs()).empty());

    std::error_code ec;
    std::filesystem::remove_all(base, ec);  // 夹具自清（失败不掩盖主断言）
}

/// 步骤②：模板缺局部名一键；URDF 缺源文件一键；空白来源零检查。
TEST(WfLifecycle, ValidateInitialSourceStep_KeysAndPass)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 模板缺局部名（createDraft @pre"localName 非空"的前置呈现）。
    NewProjectInputs noLocal = makeTemplateInputs();
    noLocal.templateLocalName.clear();
    auto e1 = workflow::validateStep(NewProjectStep::InitialSource, noLocal);
    ASSERT_EQ(e1.size(), 1u);
    EXPECT_EQ(e1[0], "wizard.new-project.error.template-local-name-empty");

    // URDF 缺外部源路径。
    NewProjectInputs noFile = makeUrdfInputs();
    noFile.sourceFile.clear();
    auto e2 = workflow::validateStep(NewProjectStep::InitialSource, noFile);
    ASSERT_EQ(e2.size(), 1u);
    EXPECT_EQ(e2[0], "wizard.new-project.error.source-file-empty");

    // 空白来源：无来源专属输入，恒放行（①步骤事实不在本步复检——
    // 前向累积语义，见 validateStep 实现注）。
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::InitialSource, makeBlankInputs()).empty());
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::InitialSource, makeTemplateInputs()).empty());
    EXPECT_TRUE(workflow::validateStep(NewProjectStep::InitialSource, makeUrdfInputs()).empty());
}

/// 步骤③（确认）：①②键集并集；多条错误按冻结键序稳定输出。
TEST(WfLifecycle, ValidateConfirmStep_UnionAndStableOrder)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 全空输入（缺名＋缺目录＋模板缺局部名）→ 三键按冻结键序：
    // display-name-empty → directory-empty → template-local-name-empty。
    NewProjectInputs bad;
    bad.source = InitialSourceKind::Template;
    const auto e = workflow::validateNewProjectInputs(bad);
    ASSERT_EQ(e.size(), 3u);
    EXPECT_EQ(e[0], "wizard.new-project.error.display-name-empty");
    EXPECT_EQ(e[1], "wizard.new-project.error.directory-empty");
    EXPECT_EQ(e[2], "wizard.new-project.error.template-local-name-empty");

    // 全量合法 → 放行（三来源各一）。
    EXPECT_TRUE(workflow::validateNewProjectInputs(makeTemplateInputs()).empty());
    EXPECT_TRUE(workflow::validateNewProjectInputs(makeUrdfInputs()).empty());
    EXPECT_TRUE(workflow::validateNewProjectInputs(makeBlankInputs()).empty());
}

/// 步骤越界 = 调用方契约违约 fail-fast（WorkflowError——§10.3 错误语义）。
TEST(WfLifecycle, ValidateStepOutOfRange_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{});

    // 三值枚举封闭，越界值只能经整型强制构造——防御性校验面用例。
    const auto bogus = static_cast<NewProjectStep>(99);
    EXPECT_THROW((void)workflow::validateStep(bogus, makeBlankInputs()),
                 workflow::WorkflowError);
}

// =====================================================================
// 右侧实时步骤摘要（PM-01——行集/键序/值词表精确断言）
// =====================================================================

/// 模板来源：5 行（name/location/source/template/installation）——值与序
/// 精确断言（解析期望值；键串即"黄金数据"——无浮点容差需求）。
TEST(WfLifecycle, SummaryTemplateSource_FiveLinesExact)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{});

    const auto lines = workflow::buildNewProjectSummary(makeTemplateInputs());
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[0].labelKey, "wizard.new-project.summary.name");
    EXPECT_EQ(lines[0].valueText, "演示线体A");           // 用户输入原文
    EXPECT_EQ(lines[1].labelKey, "wizard.new-project.summary.location");
    EXPECT_EQ(lines[1].valueText, "X:/nonexistent-ro-demo/proj-a.rwdesign");
    EXPECT_EQ(lines[2].labelKey, "wizard.new-project.summary.source");
    EXPECT_EQ(lines[2].valueText, "template");
    EXPECT_EQ(lines[3].labelKey, "wizard.new-project.summary.template");
    EXPECT_EQ(lines[3].valueText, "generic-6r");          // 类别 token——非数值
    EXPECT_EQ(lines[4].labelKey, "wizard.new-project.summary.installation");
    EXPECT_EQ(lines[4].valueText, "ground");
}

/// URDF 来源：5 行（name/location/source/source-file/external-handling）
/// ——处置两值行随用户选择变化（二选一处置的摘要承载，PM-01/CON-03）。
TEST(WfLifecycle, SummaryUrdfSource_FiveLinesAndHandling)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01", "CON-03"}, std::vector<std::string>{"AT-20"});

    // 处置＝登记外部引用记录。
    NewProjectInputs record = makeUrdfInputs();
    record.externalHandling = workflow::ExternalResourceHandling::RecordExternalReference;
    auto lines = workflow::buildNewProjectSummary(record);
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[2].valueText, "urdf-xacro");
    EXPECT_EQ(lines[3].labelKey, "wizard.new-project.summary.source-file");
    EXPECT_EQ(lines[3].valueText, "D:/sources/robot-b.urdf");
    EXPECT_EQ(lines[4].labelKey, "wizard.new-project.summary.external-handling");
    EXPECT_EQ(lines[4].valueText, "record-external-reference");

    // 处置＝复制入资源区（缺省）——同行异值。
    lines = workflow::buildNewProjectSummary(makeUrdfInputs());
    ASSERT_EQ(lines.size(), 5u);
    EXPECT_EQ(lines[4].valueText, "copy-into-resources");
}

/// 空白来源：3 行（无模板/URDF 行——零占位行，行数由来源决定）。
TEST(WfLifecycle, SummaryBlankSource_ThreeLines)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{});

    const auto lines = workflow::buildNewProjectSummary(makeBlankInputs());
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[2].valueText, "blank");
}

/// 确定性：同输入双跑同输出（NFR-COR-02 同型——纯函数面承诺）。
TEST(WfLifecycle, SummaryDeterministicReplay)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{});

    const auto a = workflow::buildNewProjectSummary(makeUrdfInputs());
    const auto b = workflow::buildNewProjectSummary(makeUrdfInputs());
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_EQ(a[i], b[i]) << "第 " << i << " 行重放不一致";
    }
}

// =====================================================================
// P-03 零数值钉扎（acceptance 3——"向导不预填数值"的机器证据）
// =====================================================================

/**
 * 向导公共头零浮点数值面（P-03：七轴模板数值未冻结——向导不预填数值；
 * 模板启用前置 WP-13-T07）。判据：Lifecycle.hpp 的代码面（注释剥离后）
 * 零 double/float 字段或字面量——模板数值边界只能落在 modeling 模板
 * 参数化（单元卡 §7.1"模板数值边界来自 modeling 模板参数化"）。
 * P-03 冻结后（WP-13-T07）向导仍无需预填——本用例是结构性证据。
 */
TEST(WfLifecycle, WizardHeaderHasNoNumericPrefill_P03)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    // 产品头路径（IRD_WORKFLOW_UNIT_ROOT 注入——与 BuildRedLineTest 同源）。
    const std::filesystem::path header =
        std::filesystem::path{IRD_WORKFLOW_UNIT_ROOT} / "workflow"
        / "include" / "sdurws" / "ird" / "workflow" / "Lifecycle.hpp";
    ASSERT_TRUE(std::filesystem::exists(header))
        << "Lifecycle.hpp 缺失: " << header.string();

    // 读全文（只扫代码面——注释剥离防文档性提及误报；头文件无字符串
    // 字面量含 double 词形的风险，直接整文扫描即可，仍剥注释保稳）。
    std::ifstream in(header, std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(in)) << "无法读取: " << header.string();
    const std::string text{std::istreambuf_iterator<char>(in),
                           std::istreambuf_iterator<char>()};
    // 行注释剥离（头文件中文注释含"数值"字样但不属代码面；块注释由
    // 简化状态机一并剥除——与 BuildRedLineTest::stripComments 同判据的
    // 最小子集，此处仅行/块注释两态足够：头文件无注释内伪装代码风险，
    // 误剥离字符串字面量的可能性为零——本头无含 double 词形的字符串）。
    std::string code;
    enum class State { Code, LineComment, BlockComment };
    State state = State::Code;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';
        switch (state) {
        case State::Code:
            if (ch == '/' && next == '/') { state = State::LineComment; ++i; }
            else if (ch == '/' && next == '*') { state = State::BlockComment; ++i; }
            else { code += ch; }
            break;
        case State::LineComment:
            if (ch == '\n') { state = State::Code; code += '\n'; }
            break;
        case State::BlockComment:
            if (ch == '*' && next == '/') { state = State::Code; ++i; }
            break;
        }
    }
    // double/float 词形零命中（词边界形态——避免命中 "std::double_t" 之
    // 类不存在的误报负担：直接判子串 "double" 与 "float"，向导头语义上
    // 不应出现任何浮点词形）。
    EXPECT_EQ(code.find("double"), std::string::npos)
        << "向导公共头出现 double 词形（P-03 零数值预填红线）";
    EXPECT_EQ(code.find("float"), std::string::npos)
        << "向导公共头出现 float 词形（P-03 零数值预填红线）";
}

// =====================================================================
// commit 前置契约（公共接口 fail-fast 面——落盘路径在契约测试）
// =====================================================================

/// 输入未过全量校验 → WorkflowError（调用方错误 fail-fast——§10.3；
/// 步骤放行判定失效的宿主装配缺陷不入值轨道）。
TEST(WfLifecycle, CommitRejectsInvalidInputs_FailFast)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    NewProjectInputs bad;  // 全空（缺名＋缺目录）
    EXPECT_THROW((void)workflow::NewProjectWizardFlow::commit(bad, nullptr),
                 workflow::WorkflowError);
}

/// 模板/URDF 来源未提供领域初始化提交端口 → WorkflowError（①端口触达
/// 契约——acceptance 3"经 modeling 公共契约与①端口不直链"的编排侧
/// 前置：没有端口就不允许创建，杜绝静默降级为空项目）。
TEST(WfLifecycle, CommitRequiresDomainInitSubmitterForTemplateSource)
{
    IRD_TEST_INFO(std::vector<std::string>{"PM-01"}, std::vector<std::string>{"AT-20"});

    EXPECT_THROW((void)workflow::NewProjectWizardFlow::commit(makeTemplateInputs(), nullptr),
                 workflow::WorkflowError);
    EXPECT_THROW((void)workflow::NewProjectWizardFlow::commit(makeUrdfInputs(), nullptr),
                 workflow::WorkflowError);
}
