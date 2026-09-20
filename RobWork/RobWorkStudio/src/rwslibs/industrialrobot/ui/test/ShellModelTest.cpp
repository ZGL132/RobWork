/**
 * @file   ShellModelTest.cpp
 * @brief  UI-T03 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         ①ui 稳定诊断码描述符表 × StableCodeRegistry 注册/查询契约
 *         （acceptance 1 的 UI-LAYOUT-RESTORE-FAILED 码值面＋acceptance 4
 *         的码表登记面）；②PM-11 状态栏格式唯一权威的格式矩阵（acceptance 2）。
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

#include <algorithm>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::ProjectContextProjection;
using sdurws::ird::ui::ProjectMetadataProjection;
using sdurws::ird::ui::DraftPresenceProjection;

// =====================================================================
// 用例组一：ui 稳定诊断码描述符表（§3.5 九码——码值权威面的常驻自证）
// =====================================================================

/**
 * ui 码描述符必须全部通过 StableCodeRegistry 注册期验证（§10.1"注册 UI-*
 * 诊断码（经 diagnostics 码表）"的契约半区：ui 供描述符、L5 经
 * registerBuiltinCodes 同序装配——描述符一旦违约，L5 装配期即抛）。
 *
 * 验证面：
 *   - 九码（ui.md §3.5 表行数）与内置 87 码共存注册无冲突（前缀表 UI→ui
 *     所有权一致；titleKey/detailKey 全表唯一——NFR-MNT-03）；
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

    // 行数契约：ui.md §3.5 登记九码（少一行＝登记面缺码；多一行＝私扩）。
    ASSERT_EQ(uiCodes.size(), std::size_t{9});
    for (const auto& descriptor : uiCodes) {
        ASSERT_NO_THROW(registry.registerCode(descriptor))
            << "ui 码注册被拒（注册期验证违约）: " << descriptor.code;
    }

    // 全部九码归属 ui（前缀-所有权一致——§4.5 前缀表 UI→ui）。
    const std::vector<std::string> owned = registry.registeredCodes("ui");
    ASSERT_EQ(owned.size(), std::size_t{9});

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

}  // namespace
