/**
 * @file   PolicySummaryCardTest.cpp
 * @brief  UI-T07 模型层用例（QCoreApplication 级——§12.1 第一层分工）：工程
 *         策略摘要只读卡的呈现契约——分组异名（POL-ID-3）、逐字段投影行、
 *         单位换算投影、显式不适用、编辑路径说明与聚合派生。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T07.json：acceptance 1（摘要只读卡＝
 *     EngineeringPolicySet 公开字段投影〔碰撞域启用/安全间距/过滤对计数/
 *     行程上限阈值/判定阈值，含「显式不适用」态〕；显示值经单位换算投影
 *     不改变策略内容身份；显示开关与计算开关分组异名并说明影响范围——
 *     POL-ID-3）/ acceptance 2（策略编辑经①命令端口提交；表单归阶段 B；
 *     ui 不持有判定权与计算开关权威）/ acceptance 3（O-31 处置：C-10 经
 *     ui::IPolicySummarySource 自有端口承载；产品面零对端 include——
 *     NoCrossUnitInclude_O31_UI_BUILD 守卫常驻自证，本文件不重复扫描）；
 *   - units/ui.md §6.7（策略摘要只读卡原文）、§4.6（显示单位偏好默认
 *     m/rad——KIN-12；会话显示开关与策略严格分离）、policy.md §4.3/§4.4/
 *     §10.6（投影冻结基准：域 token/阈值域单位/只读投影交接行）；
 *   - 分层理由：行装配/单位换算/文案均为纯函数面（零 Widget、零事件循环）
 *     ——模型层逐行断言，GUI 层（PolicySummaryCardGuiTest）只测真实
 *     Widget 树的行为（控件只读性/点击反馈/端口消费/刷新）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Units.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/PolicySummaryCard.hpp>
#include <sdurws/ird/ui/UiProjections.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::kPolicyDisplayControlsDeferredNote;
using sdurws::ird::ui::kPolicyDisplaySeparationNote;
using sdurws::ird::ui::kPolicyEditDeferredNotice;
using sdurws::ird::ui::kPolicyEditPathNote;
using sdurws::ird::ui::kPolicyNotApplicableText;
using sdurws::ird::ui::kPolicyNotLoadedText;
using sdurws::ird::ui::PolicySummaryDisplayUnits;
using sdurws::ird::ui::PolicySummaryGroup;
using sdurws::ird::ui::PolicySummaryProjection;
using sdurws::ird::ui::PolicySummaryRow;
using sdurws::ird::ui::PolicyThresholdProjection;
using sdurws::ird::ui::policySummaryGroupTitle;
using sdurws::ird::ui::policySummaryRows;

/// 显示换算的浮点容差核对（换算经注册表因子两次乘除——二进制不可精确
/// 表示的十进制值存在 1 ulp 级偏差；呈现精度 6 位有效数字下的判别阈值）。
constexpr double kDisplayEpsilon = 1e-9;

/// 已装载形态的参考投影（五类字段齐备：碰撞启用三域、安全间距 0.3 m、
/// 近限位比 0.85、条件数显式不适用、行程上限 4π——唯一冻结默认；
/// 必检 2 对/过滤 1 对）。
PolicySummaryProjection loadedProjection()
{
    PolicySummaryProjection summary;
    summary.available = true;
    summary.collisionDomainEnabled = true;
    summary.enabledDomainTokens = {"self", "environment", "tool"};
    summary.safetyClearance = PolicyThresholdProjection{true, 0.3};
    summary.nearLimitRatio = PolicyThresholdProjection{true, 0.85};
    // conditionNumberWarning 保持默认（present=false）＝显式不适用。
    summary.travelLimit =
        PolicyThresholdProjection{true, 4.0 * 3.141592653589793};
    summary.mandatoryPairCount = 2;
    summary.filterPairCount = 1;
    return summary;
}

/// 按 key 查行（找不到返回 nullptr——断言侧显性失败）。
const PolicySummaryRow* findRow(const std::vector<PolicySummaryRow>& rows,
                                const std::string& key)
{
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&key](const PolicySummaryRow& r) { return r.key == key; });
    return it == rows.end() ? nullptr : &*it;
}

/// 行值是否含数字字符（「显式不适用」/「未装载」类文案不得夹带数值——
/// "不发明数值"的字面自证）。
bool containsDigit(const std::string& text)
{
    return std::any_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// =====================================================================
// 分组异名（POL-ID-3 的"名"——acc1 分组语义的常驻自证）
// =====================================================================

/**
 * 分组标题必须两两互异且各自点明影响范围归属（§6.7/POL-ID-3：显示开关
 * 与计算开关分组异名——同名单分组即违例；"不属策略字段"字样是显示组
 * 标题的语义下限，缺字样＝影响范围说明失明）。
 */
TEST(PolicyCardModel, GroupTitlesDistinctAndScoped_POLID3_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const char* authority = policySummaryGroupTitle(PolicySummaryGroup::PolicyAuthority);
    const char* display = policySummaryGroupTitle(PolicySummaryGroup::SessionDisplay);

    // 两名非空且互异（分组异名的最低要求）。
    ASSERT_NE(authority, nullptr);
    ASSERT_NE(display, nullptr);
    ASSERT_STRNE(authority, display);
    EXPECT_STRNE(authority, "") << "计算权威组标题为空";
    EXPECT_STRNE(display, "") << "显示设置组标题为空";

    // 影响范围归属可读：计算组点明"计算权威"（ARC-05 权威归策略）；
    // 显示组点明"不属策略字段"（POL-ID-3——显示设置不存在于策略字段）。
    EXPECT_NE(std::string(authority).find("计算权威"), std::string::npos)
        << "计算组标题未点明计算权威语义: " << authority;
    EXPECT_NE(std::string(display).find("不属策略字段"), std::string::npos)
        << "显示组标题未点明与策略的分离: " << display;
}

// =====================================================================
// 行装配：五类字段逐行投影（acc1 数据面）
// =====================================================================

/**
 * 已装载投影的行集逐行断言（§6.7 五类字段：碰撞域启用/安全间距/过滤对
 * 计数/行程上限阈值/判定阈值）：
 *   - 碰撞检查行附启用域 token（policy.md §4.3 冻结 token 直用）；
 *   - 安全间距/行程上限显示值经单位换算投影（mm/deg 目标——换算路径的
 *     行级实证；SI 默认制式的恒等换算另行专测）；
 *   - 碰撞对规则行＝"必检 n 对·过滤 m 对"（过滤对计数原文＋必检对偶）；
 *   - 判定阈值：近限位比显式提供、条件数显式不适用（四态承载之"不适用"
 *     态逐字段显式化——ERR-01）；
 *   - 修改策略行承载命令端口路径说明（acc2 的卡面语义）。
 * 行序＝装配冻结序；两次装配逐行全等（NFR-COR-02 确定性）。
 */
TEST(PolicyCardModel, LoadedProjectionRows_FiveFieldFamilies_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const PolicySummaryDisplayUnits units = PolicySummaryDisplayUnits::siDefaults();
    // 显示制式取 mm/deg（KIN-12 显示投影的换算路径——§4.6 默认 m/rad 的
    // 恒等形态由本用例的 SI 制式对照承载）。
    PolicySummaryDisplayUnits mmDeg = units;
    mmDeg.length = *core::UnitToken::find("mm");
    mmDeg.angle = *core::UnitToken::find("deg");

    const std::vector<PolicySummaryRow> rows = policySummaryRows(loadedProjection(), mmDeg);

    // 键序＝装配冻结序（稳定呈现；前 7 行计算权威组、后 2 行显示设置组）。
    const std::vector<std::string> expectedKeys = {
        "collision", "safety-clearance", "pairs", "travel-limit",
        "near-limit-ratio", "condition-number", "edit-path",
        "display-separation", "display-controls",
    };
    ASSERT_EQ(rows.size(), expectedKeys.size());
    for (std::size_t i = 0; i < expectedKeys.size(); ++i) {
        EXPECT_EQ(rows[i].key, expectedKeys[i]) << "行序漂移 @ " << i;
    }

    // 碰撞检查（计算开关）：启用＋启用域 token 清单。
    const PolicySummaryRow* collision = findRow(rows, "collision");
    ASSERT_NE(collision, nullptr);
    EXPECT_EQ(collision->label, "碰撞检查");
    EXPECT_EQ(collision->value, "启用（self、environment、tool）");

    // 安全间距：0.3 m→mm 显示投影（"300 mm"——%.6g 舍入消除换算 1 ulp 级
    // 偏差；SI 真值 0.3 m 不受影响——换算不改身份的字面形态）。
    const PolicySummaryRow* clearance = findRow(rows, "safety-clearance");
    ASSERT_NE(clearance, nullptr);
    EXPECT_EQ(clearance->label, "安全间距");
    EXPECT_EQ(clearance->value, "300 mm");

    // 碰撞对规则：必检/过滤计数行。
    const PolicySummaryRow* pairs = findRow(rows, "pairs");
    ASSERT_NE(pairs, nullptr);
    EXPECT_EQ(pairs->value, "必检 2 对·过滤 1 对");

    // 行程上限：4π rad→deg 显示投影（"720 deg"）。
    const PolicySummaryRow* travel = findRow(rows, "travel-limit");
    ASSERT_NE(travel, nullptr);
    EXPECT_EQ(travel->label, "行程上限");
    EXPECT_EQ(travel->value, "720 deg");

    // 判定阈值族：近限位比显式提供（无量纲只显数值）；条件数显式不适用。
    const PolicySummaryRow* nearLimit = findRow(rows, "near-limit-ratio");
    ASSERT_NE(nearLimit, nullptr);
    EXPECT_EQ(nearLimit->label, "近限位比");
    EXPECT_EQ(nearLimit->value, "0.85");
    const PolicySummaryRow* condition = findRow(rows, "condition-number");
    ASSERT_NE(condition, nullptr);
    EXPECT_EQ(condition->label, "条件数警告");
    EXPECT_EQ(condition->value, kPolicyNotApplicableText);

    // 分组归属：计算权威组 7 行、会话显示设置组 2 行（POL-ID-3 分组异名
    // 的行级归属；显示组恒在）。
    for (std::size_t i = 0; i < 7; ++i) {
        EXPECT_EQ(rows[i].group, PolicySummaryGroup::PolicyAuthority) << "行 " << i;
    }
    for (std::size_t i = 7; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].group, PolicySummaryGroup::SessionDisplay) << "行 " << i;
    }

    // 确定性：同输入两次装配逐行全等（NFR-COR-02）。
    const std::vector<PolicySummaryRow> again =
        policySummaryRows(loadedProjection(), mmDeg);
    ASSERT_EQ(again.size(), rows.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(again[i].key, rows[i].key);
        EXPECT_EQ(again[i].label, rows[i].label);
        EXPECT_EQ(again[i].value, rows[i].value);
        EXPECT_EQ(again[i].group, rows[i].group);
    }
}

/**
 * 显示制式恒等形态：§4.6 出厂默认（m/rad）下换算为恒等投影——显示值等
 * 于 SI 真值（换算路径仍经 core::convert——注册表因子 1.0 的一次乘除，
 * 非"跳过换算"的捷径）。
 */
TEST(PolicyCardModel, SiDefaultsIdentityDisplay_KIN12_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const PolicySummaryDisplayUnits units = PolicySummaryDisplayUnits::siDefaults();
    const std::vector<PolicySummaryRow> rows = policySummaryRows(loadedProjection(), units);

    // 恒等显示：0.3 m→"0.3 m"、4π rad→"12.5664 rad"（%.6g 呈现精度）。
    EXPECT_EQ(findRow(rows, "safety-clearance")->value, "0.3 m");
    EXPECT_EQ(findRow(rows, "travel-limit")->value, "12.5664 rad");
}

/**
 * 单位换算投影的数值面（policy.md §10.6"显示单位经 core 换算投影，不改
 * 身份"）：mm/deg 换算值与注册表因子期望一致（容差判别）；显式不适用
 * 输入直接空态（不换算、不出数值）；量纲违约 fail-fast（调用方错误——
 * 不静默出错误显示值）。
 */
TEST(PolicyCardModel, ThresholdConversionViaCoreUnits_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const auto m = *core::UnitToken::find("m");
    const auto mm = *core::UnitToken::find("mm");
    const auto rad = *core::UnitToken::find("rad");
    const auto deg = *core::UnitToken::find("deg");
    const auto one = *core::UnitToken::find("1");

    // 长度换算：0.3 m→mm（注册表因子 0.001——期望 300，容差判别）。
    const auto clearance = ui::formatPolicyThreshold(PolicyThresholdProjection{true, 0.3}, m, mm);
    ASSERT_TRUE(clearance.present);
    EXPECT_NEAR(clearance.displayValue, 300.0, kDisplayEpsilon);
    EXPECT_EQ(clearance.displayUnitSymbol, "mm");

    // 角度换算：4π rad→deg（期望 720）。
    const auto travel = ui::formatPolicyThreshold(
        PolicyThresholdProjection{true, 4.0 * 3.141592653589793}, rad, deg);
    ASSERT_TRUE(travel.present);
    EXPECT_NEAR(travel.displayValue, 720.0, kDisplayEpsilon);
    EXPECT_EQ(travel.displayUnitSymbol, "deg");

    // 无量纲恒等：0.85 经 "1" 注册表投影——数值不变、符号 "1"。
    const auto ratio = ui::formatPolicyThreshold(PolicyThresholdProjection{true, 0.85}, one, one);
    ASSERT_TRUE(ratio.present);
    EXPECT_NEAR(ratio.displayValue, 0.85, kDisplayEpsilon);
    EXPECT_EQ(ratio.displayUnitSymbol, "1");

    // 显式不适用：present=false 直接空态——displayValue 不被填充（调用方
    // 契约：读 displayValue 前必查 present；呈现层取「显式不适用」）。
    const auto absent = ui::formatPolicyThreshold(PolicyThresholdProjection{}, m, mm);
    EXPECT_FALSE(absent.present);
    EXPECT_EQ(absent.displayUnitSymbol, std::string{});

    // 量纲违约：长度字段喂角度显示单位＝调用方错误，fail-fast（core
    // convert 量纲核对；不得以错误数值继续渲染）。
    EXPECT_THROW(ui::formatPolicyThreshold(PolicyThresholdProjection{true, 0.3}, m, deg),
                 core::CoreError);
}

// =====================================================================
// 显式不适用与未装载占位（acc1"含显式不适用态"＋不虚构数值）
// =====================================================================

/**
 * 显式不适用逐字段呈现：optional 空字段行值恒为「显式不适用」且不含任何
 * 数字（P-POL-2/O-10"不发明数值"的字面自证——补默认值即违例）。
 */
TEST(PolicyCardModel, ExplicitNotApplicable_NoInventedNumbers_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    PolicySummaryProjection summary;
    summary.available = true;
    summary.collisionDomainEnabled = false;  // 碰撞停用：安全间距可显式不适用
    // 全部阈值槽位保持未提供（present=false）——全「显式不适用」形态。
    const std::vector<PolicySummaryRow> rows = policySummaryRows(
        summary, PolicySummaryDisplayUnits::siDefaults());

    for (const char* key : {"safety-clearance", "travel-limit",
                            "near-limit-ratio", "condition-number"}) {
        const PolicySummaryRow* row = findRow(rows, key);
        ASSERT_NE(row, nullptr) << key << " 行缺失";
        EXPECT_EQ(row->value, kPolicyNotApplicableText)
            << key << " 行值不是「显式不适用」: " << row->value;
        EXPECT_FALSE(containsDigit(row->value))
            << key << " 行值夹带数字（疑似发明数值）: " << row->value;
    }

    // 停用行明确"停用"（不做暗示性省略——计算开关的负向呈现）。
    EXPECT_EQ(findRow(rows, "collision")->value, "停用");
}

/**
 * 未装载占位（available==false）：占位行"工程策略→未装载"，不出现任何
 * 策略字段行（不虚构数值——§6.7 摘要只读面语义）；会话显示设置组照常
 * 呈现（显示设置边界与策略装载无关）。
 */
TEST(PolicyCardModel, UnavailablePlaceholder_NoFabrication_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const PolicySummaryProjection unloaded{};  // available=false（默认态）
    const std::vector<PolicySummaryRow> rows = policySummaryRows(
        unloaded, PolicySummaryDisplayUnits::siDefaults());

    // 占位行存在且无数字；策略字段行（碰撞/阈值/计数）不存在。
    const PolicySummaryRow* placeholder = findRow(rows, "policy");
    ASSERT_NE(placeholder, nullptr);
    EXPECT_EQ(placeholder->value, kPolicyNotLoadedText);
    EXPECT_FALSE(containsDigit(placeholder->value));
    for (const char* key : {"collision", "safety-clearance", "pairs", "travel-limit",
                            "near-limit-ratio", "condition-number"}) {
        EXPECT_EQ(findRow(rows, key), nullptr) << key << " 不应在未装载形态出现";
    }
    // 显示设置组两行恒在。
    EXPECT_NE(findRow(rows, "display-separation"), nullptr);
    EXPECT_NE(findRow(rows, "display-controls"), nullptr);
}

// =====================================================================
// 聚合派生（UI-T03 计数形态的复现面）
// =====================================================================

/**
 * 聚合计数由逐字段事实派生（UI-T07 细化：thresholdCount/explicitNot-
 * ApplicableCount 由数据成员改为派生方法——聚合与逐字段不可能漂移）：
 * 全提供 3/1、全不提供 0/4、未装载 0/4（四个阈值槽位恒为计数控）。
 */
TEST(PolicyCardModel, AggregatesDerivedFromPerFieldFacts_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const PolicySummaryProjection full = loadedProjection();
    EXPECT_EQ(full.thresholdCount(), std::size_t{3});
    EXPECT_EQ(full.explicitNotApplicableCount(), std::size_t{1});

    const PolicySummaryProjection none{};
    EXPECT_EQ(none.thresholdCount(), std::size_t{0});
    EXPECT_EQ(none.explicitNotApplicableCount(), std::size_t{4});

    // 计数控恒等：thresholdCount＋explicitNotApplicableCount＝4。
    PolicySummaryProjection partial = loadedProjection();
    partial.nearLimitRatio = PolicyThresholdProjection{};
    EXPECT_EQ(partial.thresholdCount() + partial.explicitNotApplicableCount(),
              std::size_t{4});
}

// =====================================================================
// 编辑路径说明（acc2：经①命令端口提交；表单归阶段 B）
// =====================================================================

/**
 * 修改策略行的路径说明（acc2 的卡面语义承载）：已装载形态必含 edit-path
 * 行，值＝kPolicyEditPathNote 且点明"命令端口"与"新修订"（ARC-05 权威
 * 归策略＋PA-2 不可变历史的界面表达）；延期提示文案非空并点明"后续版本
 * 提供"（§4.1 占位口径——编辑表单归阶段 B，入口不虚构编辑能力）。
 */
TEST(PolicyCardModel, EditPathNote_CommandPort_UI_T07_ACC2)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const std::vector<PolicySummaryRow> rows = policySummaryRows(
        loadedProjection(), PolicySummaryDisplayUnits::siDefaults());

    const PolicySummaryRow* edit = findRow(rows, "edit-path");
    ASSERT_NE(edit, nullptr) << "已装载形态缺修改策略路径说明行";
    EXPECT_EQ(edit->value, kPolicyEditPathNote);
    EXPECT_NE(edit->value.find("命令端口"), std::string::npos)
        << "路径说明未点明①命令端口: " << edit->value;
    EXPECT_NE(edit->value.find("新修订"), std::string::npos)
        << "路径说明未点明新修订语义: " << edit->value;
    // 行归属计算权威组（修改的是策略字段——不是显示设置）。
    EXPECT_EQ(edit->group, PolicySummaryGroup::PolicyAuthority);

    // 延期提示（点击入口的反馈文案——GUI 层验证真实点击行为）。
    ASSERT_NE(kPolicyEditDeferredNotice, nullptr);
    EXPECT_NE(std::string(kPolicyEditDeferredNotice).find("后续版本"), std::string::npos)
        << "延期提示未点明阶段归属: " << kPolicyEditDeferredNotice;
}

// =====================================================================
// 显示设置边界说明（acc1 POL-ID-3 内容面＋acc3 语义零变化）
// =====================================================================

/**
 * 会话显示设置组的边界说明（POL-ID-3 内容面）：显示开关与策略的关系行
 * 点明"不存在于工程策略字段"与"不改变计算与策略内容身份"；显示控件行
 * 点明阶段归属（阶段 A 无可交互显示开关——不虚构业务能力）。
 */
TEST(PolicyCardModel, DisplaySeparationNotes_POLID3_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const std::vector<PolicySummaryRow> rows = policySummaryRows(
        loadedProjection(), PolicySummaryDisplayUnits::siDefaults());

    const PolicySummaryRow* separation = findRow(rows, "display-separation");
    ASSERT_NE(separation, nullptr);
    EXPECT_EQ(separation->value, kPolicyDisplaySeparationNote);
    EXPECT_EQ(separation->group, PolicySummaryGroup::SessionDisplay);
    EXPECT_NE(separation->value.find("不存在于工程策略字段"), std::string::npos)
        << "边界说明未点明 POL-ID-3 字段分离: " << separation->value;
    EXPECT_NE(separation->value.find("不改变计算与策略内容身份"), std::string::npos)
        << "边界说明未点明零计算影响: " << separation->value;

    const PolicySummaryRow* controls = findRow(rows, "display-controls");
    ASSERT_NE(controls, nullptr);
    EXPECT_EQ(controls->value, kPolicyDisplayControlsDeferredNote);
    EXPECT_NE(controls->value.find("后续版本"), std::string::npos);
}

/**
 * 行键稳定性与唯一性（GUI objectName 与测试断言的锚——重排/改名会破坏
 * 验收对照）：已装载/未装载两形态各自键唯一；显示组两键跨形态恒在
 * （键＝契约面，一经交付只允许表尾追加）。
 */
TEST(PolicyCardModel, RowKeysUniqueAndStable_UI_T07_ACC1)
{
    IRD_TEST_INFO("UX-08", {}, std::nullopt);
    const auto loaded = policySummaryRows(loadedProjection(),
                                          PolicySummaryDisplayUnits::siDefaults());
    const auto unloaded = policySummaryRows(PolicySummaryProjection{},
                                            PolicySummaryDisplayUnits::siDefaults());
    for (const auto* rows : {&loaded, &unloaded}) {
        std::vector<std::string> keys;
        for (const auto& row : *rows) {
            keys.push_back(row.key);
        }
        std::sort(keys.begin(), keys.end());
        EXPECT_TRUE(std::adjacent_find(keys.begin(), keys.end()) == keys.end())
            << "行键重复（锚点失稳）";
    }
    // 跨形态恒在的两显示组键。
    EXPECT_NE(findRow(loaded, "display-separation"), nullptr);
    EXPECT_NE(findRow(unloaded, "display-separation"), nullptr);
}

}  // namespace
