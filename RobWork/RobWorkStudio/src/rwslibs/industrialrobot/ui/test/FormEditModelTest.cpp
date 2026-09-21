/**
 * @file   FormEditModelTest.cpp
 * @brief  UI-T08 模型层用例（QCoreApplication 级——§12.1 第一层分工）：
 *         参数表与表单公共件的公共编辑规则——数值＋单位同显（KIN-12）、
 *         非法输入就地显示原因保留原值、表单级确认应用（UX-07）、批量
 *         粘贴/筛选/错误定位（UX-05）、取消恢复、非模态红线与 O-31 面。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T08.json：acceptance 1（UX-04/05/07
 *     公共编辑规则全量——同显/就地错误/确认应用/批量粘贴/筛选/错误定位/
 *     取消恢复；不用模态对话框做大量重复编辑）/ acceptance 2（O-31
 *     处置：表单公共件不持有 C-3/4/5/7/8/10/11 对端类型——单位显示走
 *     core Units 显示投影；草稿接入经 ui 自有 IDraftController.
 *     attachModule 接口，模型只面向 ui 自有 IFormEditOutlet 移交；
 *     include 面由 NoCrossUnitInclude_O31_UI_BUILD 常驻扫描，本文件
 *     不重复）；
 *   - units/ui.md §13 UI-T08 行、§14.1（阶段 B 承接行——单位换算唯一
 *     入口 core::Quantity::displayValueIn）、§4.5/§4.6（单位显示是会话
 *     显示设置）、§16.7 v1.0（本文件登记行）；
 *   - 需求 UX-04/05/07、KIN-12；
 *   - 分层理由：编辑规则全部收口在无 Qt 的 ParamEditModel（模型层逐条
 *     断言），GUI 行为面（控件结构/点击流/剪贴板/开关行为）在
 *     ParamTablePanelGuiTest 测——两半对同一规则互证（UI-T07 同案）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/FormEditCommon.hpp>

#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::BatchPasteReport;
using sdurws::ird::ui::ConfirmApplyResult;
using sdurws::ird::ui::IFormEditOutlet;
using sdurws::ird::ui::ParamChange;
using sdurws::ird::ui::ParamEditModel;
using sdurws::ird::ui::ParamEditSet;
using sdurws::ird::ui::QuantityFieldSpec;
using sdurws::ird::ui::advancedPanelDefaultFields;
using sdurws::ird::ui::formatFieldValueText;
using sdurws::ird::ui::kApplyNoChangesText;
using sdurws::ird::ui::kFieldEmptyReason;
using sdurws::ird::ui::kFieldNotFiniteReason;
using sdurws::ird::ui::kFieldNotIntegerReason;
using sdurws::ird::ui::kFieldUnsetText;
using sdurws::ird::ui::makeQuantityFieldSpec;
using sdurws::ird::ui::parseFieldValueText;

// =====================================================================
// 测试替身与夹具
// =====================================================================

/// 编辑出口替身：记录全部移交的修改集（确认应用的观测面——UI-T07
/// "ui 测试以可控替身承载"同案；真实现＝域编辑器，经 attachModule
/// 接入，归阶段 B 域消费者）。
class RecordingOutlet final : public ui::IFormEditOutlet {
public:
    void applyEdits(const ParamEditSet& editSet) override { m_sets.push_back(editSet); }
    const std::vector<ParamEditSet>& sets() const { return m_sets; }

private:
    std::vector<ParamEditSet> m_sets;
};

/// 夹具字段集（四量纲覆盖：长度 m→mm 显示投影、角度 rad→deg、无量纲
/// 整数计数、时间 s 恒等制式——键/标签与 advancedPanelDefaultFields
/// 无耦合，避免用例间隐式依赖）。
std::vector<QuantityFieldSpec> fixtureFields()
{
    std::vector<QuantityFieldSpec> fields;
    fields.push_back(makeQuantityFieldSpec(
        "joint-clearance", "关节间隙", core::QuantityKind::Length,
        *core::UnitToken::find("m"), *core::UnitToken::find("mm")));
    fields.push_back(makeQuantityFieldSpec(
        "travel-limit", "行程上限", core::QuantityKind::Angle,
        *core::UnitToken::find("rad"), *core::UnitToken::find("deg")));
    fields.push_back(makeQuantityFieldSpec(
        "solver-max-iterations", "求解器最大迭代次数",
        core::QuantityKind::Dimensionless, *core::UnitToken::find("1"),
        *core::UnitToken::find("1"), std::nullopt, true));
    fields.push_back(makeQuantityFieldSpec(
        "run-duration", "运行时长", core::QuantityKind::Time,
        *core::UnitToken::find("s"), *core::UnitToken::find("s")));
    return fields;
}

/// 已注入基线的模型（间隙 0.3 m、行程 3.14159…/2 rad、迭代 100、时长
/// 5 s——覆盖"已设"半区；夹具用例按需再改）。
ParamEditModel makeLoadedModel()
{
    ParamEditModel model(fixtureFields());
    model.setBaseline("joint-clearance", 0.3);
    model.setBaseline("travel-limit", 1.5707963267948966);
    model.setBaseline("solver-max-iterations", 100.0);
    model.setBaseline("run-duration", 5.0);
    return model;
}

// =====================================================================
// acceptance 1①：数值＋单位同显（KIN-12——切换仅影响显示不改 SI 真值）
// =====================================================================

/**
 * UX-05/KIN-12：值列与单位列同显（displayNumberText/displayUnitText 分列
 * ＋displayValueText 合显）；长度显示单位 mm→cm 切换后——显示文本随动、
 * SI 真值不变、不产生脏标记/待应用修改（KIN-12"切换不产生修订"的模型
 * 层等价物）、也绝不触发任何编辑出口移交（UX-07"仅改变会话显示的操作
 * 不弹出保存提示"）。
 */
TEST(FormEditModel, UnitDisplaySwitchIsProjectionOnly_KIN12_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {"KIN-12", "UX-07"}, std::nullopt);
    RecordingOutlet outlet;
    ParamEditModel model = makeLoadedModel();

    // 同显：分列读点（数值 300＋单位 mm）与合显读点（"300 mm"）一致。
    EXPECT_EQ(model.displayNumberText("joint-clearance"), "300");
    EXPECT_EQ(std::string{model.displayUnit("joint-clearance").symbol()}, "mm");
    EXPECT_EQ(model.displayValueText("joint-clearance"), "300 mm");

    // 切换显示单位（按量纲批量——单位切换控件的语义）：显示随动。
    model.setDisplayUnitForKind(core::QuantityKind::Length, *core::UnitToken::find("cm"));
    EXPECT_EQ(model.displayNumberText("joint-clearance"), "30");
    EXPECT_EQ(model.displayValueText("joint-clearance"), "30 cm");

    // KIN-12：切换不改 SI 真值——切回 SI 制式读回真值仍为 0.3 m。
    model.setDisplayUnit("joint-clearance", *core::UnitToken::find("m"));
    EXPECT_EQ(model.displayValueText("joint-clearance"), "0.3 m");
    ASSERT_TRUE(model.currentValueSi("joint-clearance").has_value());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.3);

    // 切换不是编辑：零脏、零待应用、零移交（显示操作与修改集正交）。
    EXPECT_FALSE(model.isDirty("joint-clearance"));
    EXPECT_TRUE(model.pendingChanges().empty());
    EXPECT_EQ(outlet.sets().size(), 0u) << "显示单位切换不得触发编辑出口";
}

/**
 * 未设值不伪造 0（§6.6 占位口径）：未注入基线的字段显示"未设"占位，
 * 与 0 值严格区分；合显/分列两读点同口径。
 */
TEST(FormEditModel, UnsetValueShowsPlaceholderNotZero_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model(fixtureFields());
    EXPECT_EQ(model.displayValueText("joint-clearance"), kFieldUnsetText);
    EXPECT_EQ(model.displayNumberText("joint-clearance"), kFieldUnsetText);
    EXPECT_FALSE(model.currentValueSi("joint-clearance").has_value());
}

// =====================================================================
// acceptance 1②：非法输入就地显示原因保留原值（UX-05）
// =====================================================================

/**
 * 非法输入的完整判定谱系：非数值/空串/非有限/非整数/越界——全部就地
 * 给原因、原值一字不动（保留原值），且错误可枚举可定位（errors()/
 * firstErrorKey——错误定位的模型半区）。有效输入清除该字段旧错误。
 */
TEST(FormEditModel, IllegalInputInPlaceReasonKeepsValue_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();

    // 非数值：原因回显原文；显示回到"最后一次有效值"（保留原值）。
    EXPECT_FALSE(model.setEditText("joint-clearance", "abc"));
    EXPECT_NE(model.statusText("joint-clearance").find("不是有效数值"), std::string::npos)
        << "就地原因缺失: " << model.statusText("joint-clearance");
    EXPECT_NE(model.statusText("joint-clearance").find("abc"), std::string::npos)
        << "原因应回显输入原文";
    EXPECT_NE(model.statusText("joint-clearance").find("保留原值"), std::string::npos);
    EXPECT_EQ(model.displayNumberText("joint-clearance"), "300");
    ASSERT_TRUE(model.currentValueSi("joint-clearance").has_value());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.3);

    // 空串/纯空白：空值是非法输入而非 0（不静默当 0——§6.6 同案）。
    EXPECT_FALSE(model.setEditText("joint-clearance", "  "));
    EXPECT_EQ(model.statusText("joint-clearance"), kFieldEmptyReason);

    // 非有限（nan/inf 文本被 general 语法接受——显式拒绝不出假值）。
    EXPECT_FALSE(model.setEditText("joint-clearance", "nan"));
    EXPECT_EQ(model.statusText("joint-clearance"), kFieldNotFiniteReason);

    // 整数约束（计数类字段）：3.5 拒绝、3.0 与负整数接受。
    EXPECT_FALSE(model.setEditText("solver-max-iterations", "3.5"));
    EXPECT_EQ(model.statusText("solver-max-iterations"), kFieldNotIntegerReason);
    EXPECT_TRUE(model.setEditText("solver-max-iterations", "3.0")) << "整数值的浮点写法应接受";
    EXPECT_TRUE(model.setEditText("solver-max-iterations", "-2")) << "负整数按整数接受（范围约束另核）";

    // 错误可枚举、可定位（注册序首个——定位行为确定）。
    ASSERT_TRUE(model.hasErrors());
    EXPECT_EQ(*model.firstErrorKey(), "joint-clearance");
    ASSERT_EQ(model.errors().size(), 1u);

    // 有效输入清除本字段错误（新有效值使旧错误失据）。
    EXPECT_TRUE(model.setEditText("joint-clearance", "250"));
    EXPECT_FALSE(model.hasErrors());
    EXPECT_FALSE(model.firstErrorKey().has_value()) << "错误清空后定位应为空";
    EXPECT_TRUE(model.currentValueSi("joint-clearance").has_value());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.25);
}

/**
 * 就地解析的输入语法边界：前导 '+' 接受、整串消费（"12abc" 不是数值）、
 * 科学计数法接受、显示单位制式下解析（mm 输入→SI 真值 m）。
 */
TEST(FormEditModel, ParseSyntaxAndDisplayUnitSemantics_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {"KIN-12"}, std::nullopt);
    const QuantityFieldSpec spec = makeQuantityFieldSpec(
        "joint-clearance", "关节间隙", core::QuantityKind::Length,
        *core::UnitToken::find("m"), *core::UnitToken::find("mm"));

    // 显示单位语义：mm 输入按 mm 解析（500 mm → 0.5 m SI 真值）。
    const auto mm = parseFieldValueText("500", spec);
    ASSERT_TRUE(mm.ok);
    EXPECT_DOUBLE_EQ(mm.siValue, 0.5);
    // 前导 '+' 与科学计数法：工程输入形态接受。
    EXPECT_TRUE(parseFieldValueText("+0.5", spec).ok);
    EXPECT_TRUE(parseFieldValueText("5e2", spec).ok);
    // 整串消费：半截接受会静默丢尾——拒绝。
    EXPECT_FALSE(parseFieldValueText("12abc", spec).ok);
    // 非有限/空：同就地谱系（纯函数面直接断言）。
    EXPECT_FALSE(parseFieldValueText("", spec).ok);
    EXPECT_FALSE(parseFieldValueText("inf", spec).ok);
}

// =====================================================================
// acceptance 1③：表单级确认应用（UX-07——应用前确认交互，断言与放行
// 归 project）
// =====================================================================

/**
 * 确认应用主链路：编辑→pendingChanges 比较型明细（旧→新）→confirmApply
 * 移交编辑出口（替身记录）→基线推进、暂存清空。移交载荷全是 SI 真值
 * （KIN-12：显示制式不进移交——接收方拿到的永远是 SI 语义）；二次确认
 * 被拒（无待应用修改——不做无效移交）。
 */
TEST(FormEditModel, FormLevelConfirmApplyHandsOffEditSet_UX07_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-07", {}, std::nullopt);
    RecordingOutlet outlet;
    ParamEditModel model = makeLoadedModel();

    // 两处编辑（一个改值、一个从无到有由 setBaseline 对齐后跳过——
    // 本用例聚焦改值＋整数计数）。
    ASSERT_TRUE(model.setEditText("joint-clearance", "250"));  // 0.3 m → 0.25 m
    ASSERT_TRUE(model.setEditText("solver-max-iterations", "200"));
    ASSERT_EQ(model.dirtyKeys().size(), 2u);

    // 比较型明细：old→new 成对、SI 语义、标签随行（确认区呈现数据）。
    const auto changes = model.pendingChanges();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].key, "joint-clearance");  // 注册序
    ASSERT_TRUE(changes[0].oldSi.has_value());
    EXPECT_DOUBLE_EQ(*changes[0].oldSi, 0.3);
    EXPECT_DOUBLE_EQ(changes[0].newSi, 0.25);
    EXPECT_EQ(changes[1].key, "solver-max-iterations");
    EXPECT_DOUBLE_EQ(changes[1].newSi, 200.0);

    // 确认应用：移交出口一次、载荷与明细一致（SI 真值）。
    const ConfirmApplyResult result = model.confirmApply(outlet);
    EXPECT_TRUE(result.ok) << "确认应用应移交编辑出口";
    ASSERT_EQ(outlet.sets().size(), 1u);
    ASSERT_EQ(outlet.sets()[0].changes.size(), 2u);
    EXPECT_DOUBLE_EQ(outlet.sets()[0].changes[0].newSi, 0.25);

    // 应用后表单与权威对齐：基线推进、暂存清空、脏标记消失。
    EXPECT_TRUE(model.dirtyKeys().empty());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.25);
    // 二次确认：无修改→就地拒绝原因（不做无效移交）。
    const ConfirmApplyResult again = model.confirmApply(outlet);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ(again.reason, kApplyNoChangesText);
    EXPECT_EQ(outlet.sets().size(), 1u) << "无修改不得二次移交";
}

/**
 * 应用前置的表单级阻断：存在未解决就地错误时不移交（部分应用会制造
 * "看似成功实际残缺"的状态）；拒绝原因携带首个错误定位。断言与放行
 * 归 project 命令边界——ui 侧语义止步于"用户确认的修改集移交"。
 */
TEST(FormEditModel, ApplyBlockedUntilErrorsResolved_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-07", {"UX-05"}, std::nullopt);
    RecordingOutlet outlet;
    ParamEditModel model = makeLoadedModel();
    ASSERT_TRUE(model.setEditText("travel-limit", "60"));  // 合法编辑（deg→rad）
    EXPECT_FALSE(model.setEditText("run-duration", "abc"));  // 非法输入

    const ConfirmApplyResult result = model.confirmApply(outlet);
    EXPECT_FALSE(result.ok) << "存在就地错误不得移交";
    EXPECT_NE(result.reason.find("存在非法输入"), std::string::npos) << "拒绝原因缺失: " << result.reason;
    EXPECT_NE(result.reason.find("不是有效数值"), std::string::npos)
        << "拒绝原因应携带首个错误的原因: " << result.reason;
    // 错误定位的锚＝首个错误键（注册序——呈现层据此滚入视野）。
    ASSERT_TRUE(model.firstErrorKey().has_value());
    EXPECT_EQ(*model.firstErrorKey(), "run-duration");
    EXPECT_EQ(outlet.sets().size(), 0u) << "错误未修复不得触达编辑出口";

    // 修复后同一表单可正常应用（合法编辑仍在暂存——错误清除不丢修改）。
    ASSERT_TRUE(model.setEditText("run-duration", "8"));
    const ConfirmApplyResult fixed = model.confirmApply(outlet);
    EXPECT_TRUE(fixed.ok);
    ASSERT_EQ(outlet.sets().size(), 1u);
    EXPECT_EQ(outlet.sets()[0].changes.size(), 2u);
}

// =====================================================================
// acceptance 1④：批量粘贴/筛选/错误定位（UX-05）
// =====================================================================

/**
 * 批量粘贴：逐行独立判定（合法暂存/未知键拒绝/非法值拒绝不影响其他
 * 行）、同键后行覆盖前行、报告携带逐行原因与影响明细（"批量影响明细"
 * 验收点——影响只含本次涉及键的最终态）；被拒值保留原值。
 */
TEST(FormEditModel, BatchPasteIndependentLinesWithImpact_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();

    // 四行：①合法 ②未知键 ③非法值 ④同键覆盖（以最终态 260 为准）。
    const std::string pasteText =
        "solver-max-iterations\t150\n"
        "no-such-key\t1\n"
        "solver-max-iterations\tabc\n"
        "solver-max-iterations\t260\n";
    const BatchPasteReport report = model.batchPasteText(pasteText);

    // 计数与逐行结果（行号 1 起且空行不计——本组无空行，行号即源序）。
    EXPECT_EQ(report.lines.size(), 4u);
    EXPECT_EQ(report.acceptedCount, 2u);
    EXPECT_EQ(report.rejectedCount, 2u);
    EXPECT_TRUE(report.lines[0].accepted);
    EXPECT_FALSE(report.lines[1].accepted);
    EXPECT_NE(report.lines[1].reason.find("未知参数"), std::string::npos);
    EXPECT_FALSE(report.lines[2].accepted);
    EXPECT_NE(report.lines[2].reason.find("保留原值"), std::string::npos);
    EXPECT_TRUE(report.lines[3].accepted);

    // 影响明细：只含本次涉及键的最终态（solver-max-iterations: 100→260；
    // 第 3 行被拒不影响第 4 行的暂存）。
    ASSERT_EQ(report.impact.size(), 1u);
    EXPECT_EQ(report.impact[0].key, "solver-max-iterations");
    EXPECT_DOUBLE_EQ(report.impact[0].oldSi.value_or(0.0), 100.0);
    EXPECT_DOUBLE_EQ(report.impact[0].newSi, 260.0);
    EXPECT_DOUBLE_EQ(*model.currentValueSi("solver-max-iterations"), 260.0);

    // 未涉及字段原值不动（批量判定的独立粒度）。
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.3);
}

/**
 * 空行跳过不计入报告；逗号分隔与 TAB 分隔同价（手工清单形态）。
 */
TEST(FormEditModel, BatchPasteToleratesBlankLinesAndComma_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();
    const std::string pasteText =
        "\n"
        "run-duration,12\n"
        "   \n"
        "travel-limit, 90 \n";
    const BatchPasteReport report = model.batchPasteText(pasteText);
    EXPECT_EQ(report.lines.size(), 2u) << "空行/纯空白行应跳过不计数";
    EXPECT_EQ(report.acceptedCount, 2u);
    EXPECT_DOUBLE_EQ(*model.currentValueSi("run-duration"), 12.0);
    EXPECT_DOUBLE_EQ(*model.currentValueSi("travel-limit"), 1.5707963267948966)  // 90 deg
        << "逗号行＋两侧空白应按 deg→rad 换算暂存";
}

/**
 * 筛选：键或标签的 ASCII 大小写不敏感子串匹配；空词全量；筛选是纯
 * 可见性裁剪——不改任何编辑态（visibleKeys 保持注册序）。
 */
TEST(FormEditModel, FilterByKeyOrLabelKeepsOrder_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();
    ASSERT_TRUE(model.setEditText("travel-limit", "60"));  // 预置一处暂存

    // 键子串（大小写不敏感）。
    model.setFilterText("ITER");
    const auto byKey = model.visibleKeys();
    ASSERT_EQ(byKey.size(), 1u);
    EXPECT_EQ(byKey[0], "solver-max-iterations");
    // 中文标签子串同口径。
    model.setFilterText("间隙");
    const auto byLabel = model.visibleKeys();
    ASSERT_EQ(byLabel.size(), 1u);
    EXPECT_EQ(byLabel[0], "joint-clearance");
    // 空词＝全量（注册序）。
    model.setFilterText("");
    const auto all = model.visibleKeys();
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all[0], "joint-clearance");
    EXPECT_EQ(all[3], "run-duration");
    // 筛选不动编辑态（纯可见性裁剪）。
    EXPECT_TRUE(model.isDirty("travel-limit"));
    EXPECT_EQ(model.pendingChanges().size(), 1u);
}

/**
 * 错误定位的确定性：多处错误时 firstErrorKey 恒定位注册序首个（同一
 * 错误组恒定位同一格——呈现层据此滚入视野）。
 */
TEST(FormEditModel, FirstErrorIsRegistrationOrderStable_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();
    EXPECT_FALSE(model.setEditText("run-duration", "xyz"));       // 注册序第 4
    EXPECT_FALSE(model.setEditText("joint-clearance", "oops"));   // 注册序第 1
    EXPECT_EQ(*model.firstErrorKey(), "joint-clearance");
    EXPECT_EQ(model.errors().size(), 2u);
    EXPECT_EQ(model.errors()[0].first, "joint-clearance");
}

// =====================================================================
// acceptance 1⑤：取消恢复（UX-05/DTB 验收列）
// =====================================================================

/**
 * 取消恢复：暂存与就地错误整体丢弃、显示回基线；单位制式与筛选是
 * 会话显示设置——不随取消回滚（显示操作与修改集正交，KIN-12/UX-07）。
 */
TEST(FormEditModel, CancelRestoreDropsStagedKeepsDisplaySettings_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();

    // 显示制式先切（mm→cm）、再编辑＋制造一处错误。
    model.setDisplayUnitForKind(core::QuantityKind::Length, *core::UnitToken::find("cm"));
    ASSERT_TRUE(model.setEditText("joint-clearance", "25"));  // 25 cm
    ASSERT_FALSE(model.setEditText("run-duration", "bad"));   // 非法输入→就地错误

    model.cancelRestore();

    // 暂存/错误全部清空，显示回基线（按当前显示制式 cm 投影）。
    EXPECT_TRUE(model.dirtyKeys().empty());
    EXPECT_TRUE(model.pendingChanges().empty());
    EXPECT_FALSE(model.hasErrors());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.3);
    EXPECT_EQ(model.displayNumberText("joint-clearance"), "30") << "取消后显示回基线（cm 制式）";
    // 显示设置保留：长度仍是 cm 制式（未被取消回滚）。
    EXPECT_EQ(std::string{model.displayUnit("joint-clearance").symbol()}, "cm");
}

// =====================================================================
// acceptance 1⑥：不用模态对话框做大量重复编辑（UX-05——静态红线）
// =====================================================================

/**
 * 非模态红线（本任务交付面的常驻自证）：表单公共件的契约头与面板实现
 * 代码（注释剥离后——判定对象是代码行为，注释散文中的字样属文档，
 * BuildRedLineTest 同款口径）零 QDialog 类型、零 exec()/setModal() 调用
 * 形态——就地编辑＋内嵌确认区是唯一交互形态。正向控制：扫描对象必须
 * 真实携带面板 objectName 契约（防"扫了个空文件"的假绿灯）。
 */
TEST(FormEditModel, NoModalDialogInFormCommon_UX05_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {}, std::nullopt);
    // ui 单元树根（IRD_UI_UNIT_ROOT 注入——BuildRedLineTest 同源口径）。
    const fs::path unitRoot = fs::path{IRD_UI_UNIT_ROOT}.lexically_normal();
    const std::vector<fs::path> targets = {
        fs::path{"ui"} / "src" / "ParamTablePanel.cpp",
        fs::path{"ui"} / "include" / "sdurws" / "ird" / "ui" / "FormEditCommon.hpp",
    };
    for (const auto& rel : targets) {
        std::ifstream in(unitRoot / rel, std::ios::binary);
        ASSERT_TRUE(in.is_open()) << "无法读取扫描对象: " << rel.string();
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string src = buffer.str();
        // 正向控制：面板契约 objectName 必须在位（扫描面有效性锚——
        // 该字面量在代码常量区，不受注释剥离影响）。
        EXPECT_NE(src.find("ird_param_table_panel"), std::string::npos)
            << rel.string() << ": 未找到面板 objectName（扫描面失效）";
        // 剥离行注释与块注释（红线判定对象是代码行为；注释散文中
        // "零对话框"的红线说明字样属文档——ird_gates 第 4 系列同款
        // 口径，避免双口径漂移与自证误伤）。
        std::string stripped;
        stripped.reserve(src.size());
        bool inLine = false;
        bool inBlock = false;
        for (std::size_t i = 0; i < src.size(); ++i) {
            const char c = src[i];
            const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
            if (inLine) {
                if (c == '\n') { inLine = false; stripped.push_back(c); }
                continue;
            }
            if (inBlock) {
                if (c == '*' && next == '/') { inBlock = false; ++i; stripped.push_back(' '); }
                continue;
            }
            if (c == '/' && next == '/') { inLine = true; continue; }
            if (c == '/' && next == '*') { inBlock = true; ++i; stripped.push_back(' '); continue; }
            stripped.push_back(c);
        }
        // 红线 ①：QDialog 类型零出现（代码面——对话框依赖即违例）。
        EXPECT_EQ(stripped.find("QDialog"), std::string::npos)
            << rel.string() << ": 出现 QDialog（UX-05 非模态红线）";
        // 红线 ②：exec()/setModal() 调用形态零出现（模态运行形态）。
        const std::regex execCall(R"(exec\s*\()");
        std::smatch match;
        EXPECT_FALSE(std::regex_search(stripped, match, execCall))
            << rel.string() << ": 出现 exec() 调用（模态对话运行形态，UX-05 红线）";
        const std::regex setModalCall(R"(setModal\s*\()");
        EXPECT_FALSE(std::regex_search(stripped, match, setModalCall))
            << rel.string() << ": 出现 setModal() 调用（模态化形态，UX-05 红线）";
    }
}

// =====================================================================
// acceptance 2：O-31 处置与装配契约（调用方错误 fail-fast 面）
// =====================================================================

/**
 * 高级面板字段集（UX-04 承载）：三字段全为计数类（无量纲＋仅整数），
 * 键/标签为契约词表；基线一律未设（零默认数值——不虚构业务能力，
 * 权威值由域消费者注入）。
 */
TEST(FormEditModel, AdvancedPanelDefaultFields_UX04_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-04", {}, std::nullopt);
    const auto fields = advancedPanelDefaultFields();
    ASSERT_EQ(fields.size(), 3u);
    EXPECT_EQ(fields[0].key, "solver-max-iterations");
    EXPECT_EQ(fields[1].key, "sampling-points");
    EXPECT_EQ(fields[2].key, "random-seed");
    for (const auto& field : fields) {
        EXPECT_EQ(field.kind, core::QuantityKind::Dimensionless);
        EXPECT_EQ(field.siUnit.kind(), core::QuantityKind::Dimensionless);
        EXPECT_TRUE(field.integerOnly) << "计数类字段应仅接受整数: " << field.key;
        EXPECT_EQ(field.label.empty(), false);
    }
    // 零默认数值：构造模型后全部字段未设（域消费者经 setBaseline 注入）。
    ParamEditModel model(advancedPanelDefaultFields());
    for (const auto& field : fields) {
        EXPECT_EQ(model.displayValueText(field.key), kFieldUnsetText);
    }
}

/**
 * 装配契约的 fail-fast 面（调用方错误——越早炸越好）：单位量纲不匹配/
 * 无效 token/空键/空标签在工厂拒绝；模型拒绝空字段集与重复键。
 */
TEST(FormEditModel, SpecFactoryAndModelRejectCallerErrors_UI_T08_ACC2)
{
    IRD_TEST_INFO("UX-04", {}, std::nullopt);
    const auto length = *core::UnitToken::find("m");
    const auto degree = *core::UnitToken::find("deg");
    // SI 单位量纲与字段量纲不匹配——工厂拒绝。
    EXPECT_THROW(makeQuantityFieldSpec("k", "l", core::QuantityKind::Length, length, degree),
                 std::invalid_argument);
    // 显示单位量纲不匹配——工厂拒绝。
    EXPECT_THROW(makeQuantityFieldSpec("k", "l", core::QuantityKind::Angle, degree, length),
                 std::invalid_argument);
    // 无效 token（默认构造的 0xFFFF 句柄）——工厂拒绝。
    EXPECT_THROW(makeQuantityFieldSpec("k", "l", core::QuantityKind::Length,
                                       core::UnitToken{}, length),
                 std::invalid_argument);
    // 空 key/空 label——工厂拒绝。
    EXPECT_THROW(makeQuantityFieldSpec("", "l", core::QuantityKind::Length, length, length),
                 std::invalid_argument);
    EXPECT_THROW(makeQuantityFieldSpec("k", "", core::QuantityKind::Length, length, length),
                 std::invalid_argument);
    // 模型：空字段集拒绝、重复键拒绝。
    EXPECT_THROW((ParamEditModel{std::vector<QuantityFieldSpec>{}}), std::invalid_argument);
    std::vector<QuantityFieldSpec> dup;
    dup.push_back(makeQuantityFieldSpec("k", "l", core::QuantityKind::Length, length, length));
    dup.push_back(makeQuantityFieldSpec("k", "l2", core::QuantityKind::Length, length, length));
    EXPECT_THROW((ParamEditModel{dup}), std::invalid_argument);
    // 未知键查询/编辑——out_of_range（行定位键应来自本模型输出）。
    ParamEditModel model(fixtureFields());
    EXPECT_THROW(model.statusText("no-such"), std::out_of_range);
    EXPECT_THROW(model.setEditText("no-such", "1"), std::out_of_range);
}

/**
 * 基线注入边界：非有限基线拒绝（不静默出"nan mm"显示）；基线更新后
 * 旧暂存与旧错误失据（一并清除——显示立即回新基线）。
 */
TEST(FormEditModel, BaselineInjectionClearsStaleStagedState_UI_T08_ACC2)
{
    IRD_TEST_INFO("UX-04", {}, std::nullopt);
    ParamEditModel model = makeLoadedModel();
    EXPECT_THROW(model.setBaseline("joint-clearance", std::numeric_limits<double>::quiet_NaN()),
                 std::invalid_argument);
    // 旧暂存＋旧错误在位；新基线到达 → 两者清空、显示回新基线。
    ASSERT_TRUE(model.setEditText("joint-clearance", "250"));
    EXPECT_FALSE(model.setEditText("joint-clearance", "oops"));
    EXPECT_FALSE(model.statusText("joint-clearance").empty());
    model.setBaseline("joint-clearance", 0.4);
    EXPECT_TRUE(model.statusText("joint-clearance").empty());
    EXPECT_TRUE(model.dirtyKeys().empty());
    EXPECT_DOUBLE_EQ(*model.currentValueSi("joint-clearance"), 0.4);
}

// =====================================================================
// 显示投影纯函数面（格式化/合显文本——模型读点的同源核对）
// =====================================================================

/**
 * 合显格式化：SI→显示一次换算＋单位后缀；无量纲"1"不带后缀（工程
 * 惯例——PolicySummaryCard 同案）；量纲不匹配 fail-fast（调用方错误）。
 */
TEST(FormEditModel, FormatValueTextUnitSuffixAndDimensionless_UI_T08_ACC1)
{
    IRD_TEST_INFO("UX-05", {"KIN-12"}, std::nullopt);
    const auto m = *core::UnitToken::find("m");
    const auto mm = *core::UnitToken::find("mm");
    const auto one = *core::UnitToken::find("1");
    EXPECT_EQ(formatFieldValueText(0.3, m, mm), "300 mm");
    EXPECT_EQ(formatFieldValueText(0.3, m, m), "0.3 m");
    EXPECT_EQ(formatFieldValueText(42.0, one, one), "42") << "无量纲不带\"1\"后缀";
    EXPECT_THROW(formatFieldValueText(0.3, m, *core::UnitToken::find("deg")),
                 std::logic_error) << "量纲不匹配应 fail-fast";
    // 越界原因文案携带实际范围（SI 制式呈现——与约束定义同制式）。
    const auto spec = makeQuantityFieldSpec(
        "t", "时长", core::QuantityKind::Time, *core::UnitToken::find("s"),
        *core::UnitToken::find("s"), ui::QuantityBounds{1.0, 10.0});
    const auto out = parseFieldValueText("20", spec);
    EXPECT_FALSE(out.ok);
    EXPECT_NE(out.reason.find("[1, 10]"), std::string::npos) << "越界原因应含范围: " << out.reason;
}

}  // namespace
