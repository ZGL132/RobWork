/**
 * @file   ShellModelTest.cpp
 * @brief  UI-T03 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         ①ui 稳定诊断码描述符表 × StableCodeRegistry 注册/查询契约
 *         （acceptance 1 的 UI-LAYOUT-RESTORE-FAILED 码值面＋acceptance 4
 *         的码表登记面）；②PM-11 状态栏格式唯一权威的格式矩阵（acceptance 2）；
 *         ③禁用原因 tooltip 决策矩阵＋原因键词表完备性（F-553——ERR-01
 *         「禁用必带原因」呈现半区，UiText::disableReasonToolTip 唯一书写点）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T03.json acceptance 1（布局记忆损坏回退
 *     ＋UI-LAYOUT-RESTORE-FAILED——本处验证该码可注册、Dev 属性强制生效；
 *     GUI 侧行为用例见 WorkbenchShellGuiTest）/ acceptance 2（PM-11 格式
 *     `<显示名>[*][（只读）]`——formatProjectStatusText 唯一权威的矩阵断言）；
 *   - units/ui.md §3.5（UI-* 九码建议值表——码值权威归 diagnostics 码表，
 *     "最终以注册表现为准"＝必须通过 StableCodeRegistry::registerCode 的
 *     注册期验证）、§10.1（副作用行"注册 UI-* 诊断码（经 diagnostics 码表）"
 *     ——ui 供描述符、L5 注册的落地面）、§2.2 PM-11 行；
 *   - diagnostics DiagCodes.hpp（CodeDescriptor 注册期验证表：Dev 码强制
 *     userVisible/reportable/historical=false、paramSchema 必填、前缀-所有权
 *     一致——ui 码描述符必须逐条通过）；
 *   - 先例：execution/test、io/test 的模型层用例形态（QCoreApplication 级、
 *     零 Widget——AGENTS 模型测试豁免）。
 *
 * 为什么这两组用例放在模型层而非 GUI 层：码表注册契约与文本格式化均为
 * 纯函数面（零 Widget、零事件循环），按 §12.1 分层归 sdurws_ird_ui_test；
 * GUI 层（WorkbenchShellGuiTest）只测需要真实 Widget 树的行为。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/diagnostics/DiagCodes.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/IWorkbenchShell.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>
#include <sdurws/ird/ui/UiText.hpp>  // disableReasonToolTip（F-553——禁用原因 tooltip 决策）

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::ProjectMetadataProjection;
using sdurws::ird::ui::DraftPresenceProjection;

// =====================================================================
// 用例组一：ui 稳定诊断码描述符表（§3.5 十码——码值权威面的常驻自证）
// =====================================================================

/**
 * ui 码描述符必须全部通过 StableCodeRegistry 注册期验证（§10.1"注册 UI-*
 * 诊断码（经 diagnostics 码表）"的契约半区：ui 供描述符、L5 经
 * registerBuiltinCodes 同序装配——描述符一旦违约，L5 装配期即抛）。
 *
 * 验证面：
 *   - 十码（ui.md §3.5 表行数；第 10 码 UI-PRESENTATION-REFRESH-FAILED
 *     随 UI-T20 增量登记——v1.19 九码钉住值的合法扩展）与内置 87 码共存
 *     注册无冲突（前缀表 UI→ui 所有权一致；titleKey/detailKey 全表唯一
 *     ——NFR-MNT-03）；
 *   - UI-LAYOUT-RESTORE-FAILED（acceptance 1 依赖码）注册后可查且 Dev
 *     强制属性生效（userVisible=false——Dev 码不入用户目录/报告/历史）；
 *   - manifest 摘要两次计算稳定（DT-REG-1——L5 装配自检的观测面）。
 */
TEST(ShellModel, UiDiagnosticCodeDescriptors_RegisterContract_UI_T03_ACC1)
{
    IRD_TEST_INFO("UI-LAYOUT-RESTORE-FAILED", {}, std::nullopt);
    diagnostics::StableCodeRegistry registry;

    // L5 装配序的实测形态：内置码表（diagnostics §4.6）＋ui 码表（§3.5）
    // 先后注册——任一违约即抛 DiagnosticsError（注册期验证是逐条的）。
    diagnostics::registerBuiltinCodes(registry);
    const std::vector<diagnostics::CodeDescriptor> uiCodes =
        ui::uiDiagnosticCodeDescriptors();

    // 行数契约：ui.md §3.5 登记十码（少一行＝登记面缺码；多一行＝私扩）。
    ASSERT_EQ(uiCodes.size(), std::size_t{10});
    for (const auto& descriptor : uiCodes) {
        ASSERT_NO_THROW(registry.registerCode(descriptor))
            << "ui 码注册被拒（注册期验证违约）: " << descriptor.code;
    }

    // 全部十码归属 ui（前缀-所有权一致——§4.5 前缀表 UI→ui）。
    const std::vector<std::string> owned = registry.registeredCodes("ui");
    ASSERT_EQ(owned.size(), std::size_t{10});

    // acceptance 1 依赖码：注册可查＋Dev 属性强制（§4.5——Dev 码
    // userVisible/reportable/historical 必为 false）。
    const diagnostics::CodeDescriptor* layoutFailed =
        registry.find("UI-LAYOUT-RESTORE-FAILED");
    ASSERT_NE(layoutFailed, nullptr) << "UI-LAYOUT-RESTORE-FAILED 未注册";
    EXPECT_EQ(layoutFailed->severity, diagnostics::DiagnosticSeverity::Dev);
    EXPECT_FALSE(layoutFailed->userVisible);
    EXPECT_FALSE(layoutFailed->reportable);
    EXPECT_FALSE(layoutFailed->historical);
    EXPECT_EQ(layoutFailed->ownerUnit, "ui");
    // paramSchema 必填字段的显式空形态（§4.5"空数组＝无参数"）。
    EXPECT_EQ(layoutFailed->paramSchema, std::string("[]"));

    // 重复注册拒绝（DuplicateCode——装配期即暴露，不静默覆盖，§7.2 同款）。
    EXPECT_THROW(registry.registerCode(uiCodes.front()), diagnostics::DiagnosticsError);

    // manifest 摘要两次计算稳定（DT-REG-1；码表一致性是"同一码同义"的
    // 装配侧保障——L5/worker 握手比对的数据基础）。
    EXPECT_TRUE(registry.manifest() == registry.manifest());
}

// =====================================================================
// 用例组二：PM-11 状态栏格式唯一权威（`<显示名>[*][（只读）]`）
// =====================================================================

/**
 * 格式矩阵逐行断言（PM-11 原文格式；acceptance 2 的模型层证据——GUI 层
 * WorkbenchShellGuiTest 另有状态栏控件端到端断言，同一权威函数双面覆盖）。
 *
 * 矩阵（§4.2 状态栏行语义）：
 *   - 无项目（project=nullopt）→"未打开项目"（PM-10 首页态呈现）；
 *   - 显示名 → `<显示名>`；
 *   - drafts.present＝true（磁盘草稿——DraftProjection.present 任一模块）→ 追加 `*`；
 *   - sessionDirty＝true（会话脏标记）→ 追加 `*`（两输入任一即标记）；
 *   - writable=false → 追加全角"（只读）"；
 *   - 组合态：`*` 在前、"（只读）"在后（PM-11 格式占位次序原文）。
 */
TEST(ShellModel, StatusBarFormat_PM11_Matrix_UI_T03_ACC2)
{
    IRD_TEST_INFO("PM-11", {}, std::nullopt);

    // 无项目：首页态（PM-10），不虚构项目事实。
    ProjectContextProjection noProject;
    EXPECT_EQ(ui::formatProjectStatusText(noProject), std::string(u8"未打开项目"));

    // 基线：可写项目、无草稿——纯显示名。
    ProjectContextProjection baseline;
    ProjectMetadataProjection metadata;
    metadata.projectId = core::ProjectId::generate();
    metadata.projectDisplayName = u8"演示项目";
    metadata.writable = true;
    baseline.project = metadata;
    EXPECT_EQ(ui::formatProjectStatusText(baseline), std::string(u8"演示项目"));

    // 磁盘草稿（present）→ `*`（§4.2："DraftProjection.present ∨ 会话脏标记"）。
    ProjectContextProjection withDiskDraft = baseline;
    withDiskDraft.drafts.present = true;
    EXPECT_EQ(ui::formatProjectStatusText(withDiskDraft), std::string(u8"演示项目*"));

    // 会话脏标记 → 同样 `*`（两输入任一即标记——来源不同语义同呈现）。
    ProjectContextProjection withSessionDirty = baseline;
    withSessionDirty.drafts.sessionDirty = true;
    EXPECT_EQ(ui::formatProjectStatusText(withSessionDirty), std::string(u8"演示项目*"));

    // 只读 → 全角"（只读）"后缀（PM-07 呈现；括号为需求格式原文）。
    ProjectContextProjection readOnly = baseline;
    readOnly.project->writable = false;
    EXPECT_EQ(ui::formatProjectStatusText(readOnly), std::string(u8"演示项目（只读）"));

    // 组合：未应用修改＋只读 → `*` 在前、"（只读）"在后（PM-11 占位次序）。
    ProjectContextProjection both = readOnly;
    both.drafts.present = true;
    both.drafts.sessionDirty = true;
    EXPECT_EQ(ui::formatProjectStatusText(both), std::string(u8"演示项目*（只读）"));
}

// =====================================================================
// 用例组三：禁用原因 tooltip 决策（F-553——ERR-01「禁用必带原因」呈现
// 半区的唯一书写点；消费面＝宿主菜单动作 refreshHostMenuActions）
// =====================================================================

/**
 * F-553 回归（决策表矩阵）：可用性快照刷新点对动作 tooltip 的三态分岔——
 *   - 禁用＋原因键→统一原因解析文案（此前宿主菜单循环丢弃 reasonKey＝
 *     置灰动作悬停无禁用原因，即 finding 登记的呈现缺口）；
 *   - 可用→还原基础悬停说明（F-501 三条长文案不因禁用刷新丢失）；
 *   - 禁用＋空键→保持基础说明（F-557 同面守卫——空键不进 resolveText，
 *     解析器对未知键 fail-fast，空键守卫是诚实降级面）。
 */
TEST(ShellModel, DisableReasonToolTip_DecisionMatrix_F553)
{
    IRD_TEST_INFO("UX-02", {"F-553"});

    // ①禁用＋就绪阻断键→统一原因解析（与 resolveText 同值——同源一词表，
    //   状态行/命令面板/菜单 tooltip 三呈现面同文案）。
    const std::string base = u8"预检并应用当前草稿——通过门禁后生成新项目修订。";
    const std::string tooltip =
        ui::disableReasonToolTip(false, "reason.readiness-blocking", base);
    EXPECT_EQ(tooltip, ui::resolveText("reason.readiness-blocking"));
    EXPECT_EQ(tooltip, std::string(u8"存在未就绪阻断项——先处理校验页阻断"));

    // ②可用→还原基础悬停说明（可用态不残留上一次禁用原因）。
    EXPECT_EQ(ui::disableReasonToolTip(true, "reason.readiness-blocking", base), base);
    // 可用且键为空→同样还原（可用态与原因键无涉）。
    EXPECT_EQ(ui::disableReasonToolTip(true, "", base), base);

    // ③禁用＋空键→F-557 同面守卫（空键不进 resolveText——保持基础文案
    //   诚实降级，不渲染空白 tooltip、不因解析 fail-fast 中断刷新循环）。
    EXPECT_EQ(ui::disableReasonToolTip(false, "", base), base);
    // 无基础文案的命令动作（缺省空）→空 tooltip（与顶栏按钮先例等价）。
    EXPECT_EQ(ui::disableReasonToolTip(false, "", ""), std::string());
}

/**
 * F-553 回归（词表完备性）：可用性链全部已知禁用原因键必须在 UiText
 * 登记——refreshHostMenuActions 每拍对禁用动作现调 resolveText（未登记
 * 键 fail-fast 即菜单刷新中断），装配层新增原因键而漏登记文案＝宿主菜
 * 单刷新即崩的潜伏缺陷。本用例钉住当前注册表全部缺省/注入原因键。
 */
TEST(ShellModel, DisableReasonToolTip_AllRegistryReasonKeysRegistered_F553)
{
    IRD_TEST_INFO("UX-02", {"F-553", "F-557"});

    // 注册表缺省使能规则（CommandRegistry defaultEnablement）＋宿主注入
    // 门控（applyDraftDisablement/undoRedoDisablement）＋注册期撤牌谓词
    // 的全部原因键——逐一应可解析且非空（能进 tooltip 呈现面）。
    const char* const reasonKeys[] = {
        "reason.no-project",               // 缺省规则：无活动项目（PM-10）
        "reason.readonly",                 // 缺省规则：项目只读（§7.6）
        "reason.readiness-blocking",       // draft.apply：就绪阻断（F-553 主键）
        "reason.no-undo-revision",         // project.undo：无可撤销修订
        "reason.no-redo-revision",         // project.redo：无可重做修订
        "cmd.flow-not-assembled.reason",   // 注册期撤牌：域流程未装配（UI-T27）
    };
    for (const char* key : reasonKeys) {
        const std::string text = ui::disableReasonToolTip(false, key, std::string());
        EXPECT_FALSE(text.empty()) << "原因键缺文案：" << key;
    }
}

}  // namespace
