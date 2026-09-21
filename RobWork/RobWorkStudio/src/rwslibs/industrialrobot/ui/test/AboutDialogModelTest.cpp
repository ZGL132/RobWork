/**
 * @file   AboutDialogModelTest.cpp
 * @brief  UI-T10 模型层用例（QCoreApplication 级——§12.1 第一层分工）：帮助
 *         入口与关于对话框的呈现契约——静态白名单词表冻结、清单＝白名单∩
 *         装配报告（UI-PLG-2 数据源断言）、版本基线呈现行（NFR-DEP-05）与
 *         用户手册入口路径（§11.4）。
 *
 * 设计依据：
 *   - 任务契约 tasks/foundation/UI-T10.json acceptance 1（UI-PLG-2：关于
 *     对话框插件清单＝静态白名单 ∩ AssemblyReport、与静态白名单一致、产
 *     品/组件版本与注入基线一致、帮助入口链接用户手册）/ acceptance 2
 *     （O-31 处置：清单静态白名单、版本数据 L5 注入——本文件断言的全部
 *     输入均为 ui 自有值类型，替身注入，零对端类型）；
 *   - units/ui.md §11.1（白名单八 token 冻结词表与顺序）、§11.4（清单五列
 *     ＋版本区＋帮助入口）、§10.9（PluginAssemblyReport 值形状）、§3.5
 *     （plugin.<id>.title／plugin.assembly.<state>.label 键族——UI-T10 冻结
 *     登记；UX-02 零内部插件名）、ERR-01（「不适用」占位不伪造）；
 *   - 分层理由：白名单/行装配/版本行/路径解析均为纯函数面（零 Widget）
 *     ——模型层逐行断言；GUI 层（AboutDialogGuiTest）只测真实 Widget 树
 *     的呈现行为（同源渲染/占位形态/帮助命令路径）。
 */

#include <gtest/gtest.h>

#include <QFileInfo>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/AboutDialog.hpp>
#include <sdurws/ird/ui/UiText.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace sdurws::ird;
using sdurws::ird::ui::AboutComponentVersion;
using sdurws::ird::ui::AboutPluginRow;
using sdurws::ird::ui::AboutVersionBaseline;
using sdurws::ird::ui::AboutVersionRow;
using sdurws::ird::ui::PluginAssemblyReport;
using sdurws::ird::ui::PluginAssemblyStatus;
using sdurws::ird::ui::TextKey;
using sdurws::ird::ui::aboutPluginRows;
using sdurws::ird::ui::aboutVersionRows;
using sdurws::ird::ui::notApplicableText;
using sdurws::ird::ui::pluginAssemblyStatusLabelKey;
using sdurws::ird::ui::pluginTitleKey;
using sdurws::ird::ui::pluginUiWhitelist;
using sdurws::ird::ui::registeredTextKeys;
using sdurws::ird::ui::resolveText;
using sdurws::ird::ui::userManualPath;

/// §11.1 冻结词表（测试侧独立登记的字面——与被测函数对照防"两处漂移"；
/// 与 units/ui.md §11.1 原文逐字一致）。
const std::vector<std::string>& kFrozenWhitelist()
{
    static const std::vector<std::string> tokens{
        "modeling", "requirements", "kinematics", "trajectory",
        "dynamics", "selection", "optimization", "workflow",
    };
    return tokens;
}

/// 按 pluginId 查行（找不到返回 nullptr——断言侧显性失败）。
const AboutPluginRow* findRow(const std::vector<AboutPluginRow>& rows,
                              const std::string& pluginId)
{
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [&pluginId](const AboutPluginRow& r) {
                                     return r.pluginId == pluginId;
                                 });
    return it == rows.end() ? nullptr : &*it;
}

// =====================================================================
// 静态白名单词表（§11.1——acceptance 1"与静态白名单一致"的锚点半区）
// =====================================================================

/**
 * 白名单是清单一致性的锚：词表内容、顺序、唯一性都必须与 §11.1 逐字一致
 * （顺序即装配顺序与清单呈现序——重排属词表变更，必须走单元卡修订）。
 */
TEST(AboutModel, WhitelistFrozenEightTokens_11_1_UI_T10)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);
    const std::vector<std::string> tokens = pluginUiWhitelist();

    // 内容与顺序：与 §11.1 原文八 token 逐字一致（占位 token 先行登记）。
    ASSERT_EQ(tokens.size(), kFrozenWhitelist().size()) << "白名单规模漂移";
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        EXPECT_EQ(tokens[i], kFrozenWhitelist()[i])
            << "白名单第 " << i << " 项与 §11.1 冻结词表不一致: " << tokens[i];
    }
    // 唯一性：token 是注册标识，重复词表会使"每插件恰好一次"（§11.1）
    // 失去判定基准。
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        for (std::size_t j = i + 1; j < tokens.size(); ++j) {
            EXPECT_NE(tokens[i], tokens[j]) << "白名单 token 重复: " << tokens[i];
        }
    }
    // 稳定性：连续两次调用内容一致（编译期常量语义——NFR-COR-02 精神）。
    EXPECT_EQ(tokens, pluginUiWhitelist());
}

// =====================================================================
// 插件清单装配（§11.4 白名单 ∩ 报告——UI-PLG-2 数据源断言）
// =====================================================================

/**
 * UI-PLG-2 断言半区一（阶段 A 常态）：装配器未产出（报告空集）时，清单
 * 仍与静态白名单一致——八行占位（NotAssembled、计数 0、版本「不适用」），
 * 行序＝白名单序。不虚构任何装配事实。
 */
TEST(AboutModel, EmptyReportsYieldWhitelistPlaceholderRows_UI_PLG_2_UI_T10_ACC1)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);
    const std::vector<AboutPluginRow> rows = aboutPluginRows({});

    // 行集＝白名单全集（行数与行序一致——"清单与静态白名单一致"字面）。
    const std::vector<std::string> tokens = pluginUiWhitelist();
    ASSERT_EQ(rows.size(), tokens.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        EXPECT_EQ(rows[i].pluginId, tokens[i]) << "行序偏离白名单序: 第 " << i << " 行";
    }
    // 占位行形态：全部 NotAssembled＋零计数＋「不适用」版本。
    for (const AboutPluginRow& row : rows) {
        EXPECT_EQ(row.status, PluginAssemblyStatus::NotAssembled)
            << "无报告却呈装配态: " << row.pluginId;
        EXPECT_EQ(row.panelCount, 0u) << "无报告却有面板计数: " << row.pluginId;
        EXPECT_EQ(row.commandCount, 0u) << "无报告却有命令计数: " << row.pluginId;
        EXPECT_EQ(row.versionText, notApplicableText())
            << "版本列未按 ERR-01 占位: " << row.pluginId;
        EXPECT_EQ(row.failureDiagnostics.size(), 0u)
            << "无报告却有失败证据: " << row.pluginId;
    }
}

/**
 * UI-PLG-2 断言半区二：清单＝白名单 ∩ 报告——命中行取报告值充实（状态/
 * 面板数/命令数/失败码），白名单外报告条目被交集丢弃（清单不被污染，
 * 行集仍与白名单一致）。
 */
TEST(AboutModel, IntersectionEnrichesAndDropsForeignReports_UI_PLG_2_UI_T10_ACC1)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);

    // 报告集：两插件命中（一成一败）＋一插件零面板零命令命中＋一白名单外
    // 条目（rogue——注册边界本应拒绝，防御装配器报告数据异常）。
    std::vector<PluginAssemblyReport> reports;
    {
        PluginAssemblyReport okRow;
        okRow.pluginId = "kinematics";
        okRow.ok = true;
        okRow.panelsLoaded = 2;
        okRow.commandsRegistered = 3;
        reports.push_back(okRow);

        PluginAssemblyReport failedRow;
        failedRow.pluginId = "dynamics";
        failedRow.ok = false;
        failedRow.panelsLoaded = 1;
        failedRow.commandsRegistered = 0;
        failedRow.failureDiagnostics = {"UI-PLUGIN-ASSEMBLY-FAILED"};
        reports.push_back(failedRow);

        PluginAssemblyReport emptyOkRow;
        emptyOkRow.pluginId = "workflow";
        emptyOkRow.ok = true;  // 零面板零命令的合法装配（能力声明可为零）
        reports.push_back(emptyOkRow);

        PluginAssemblyReport foreignRow;
        foreignRow.pluginId = "rogue";
        foreignRow.ok = true;
        foreignRow.panelsLoaded = 9;
        foreignRow.commandsRegistered = 9;
        reports.push_back(foreignRow);
    }

    const std::vector<AboutPluginRow> rows = aboutPluginRows(reports);

    // 交集丢弃：行集仍＝白名单全集（白名单外条目不产生行）。
    const std::vector<std::string> tokens = pluginUiWhitelist();
    ASSERT_EQ(rows.size(), tokens.size()) << "清单被白名单外报告条目污染";
    EXPECT_EQ(findRow(rows, "rogue"), nullptr) << "白名单外条目进了清单";

    // 命中行（成功）：状态与计数取报告值。
    const AboutPluginRow* kinematics = findRow(rows, "kinematics");
    ASSERT_NE(kinematics, nullptr);
    EXPECT_EQ(kinematics->status, PluginAssemblyStatus::Ok);
    EXPECT_EQ(kinematics->statusText, u8"已装配");
    EXPECT_EQ(kinematics->panelCount, 2u);
    EXPECT_EQ(kinematics->commandCount, 3u);
    EXPECT_TRUE(kinematics->failureDiagnostics.empty());

    // 命中行（失败）：失败态如实呈现＋失败证据不丢弃（§11.3 失败隔离，
    // 清单如实呈现失败态——不伪装成功）。
    const AboutPluginRow* dynamics = findRow(rows, "dynamics");
    ASSERT_NE(dynamics, nullptr);
    EXPECT_EQ(dynamics->status, PluginAssemblyStatus::Failed);
    EXPECT_EQ(dynamics->statusText, u8"装配失败");
    EXPECT_EQ(dynamics->panelCount, 1u);
    EXPECT_EQ(dynamics->commandCount, 0u);
    ASSERT_EQ(dynamics->failureDiagnostics.size(), 1u);
    EXPECT_EQ(dynamics->failureDiagnostics[0], "UI-PLUGIN-ASSEMBLY-FAILED");

    // 命中行（零能力）：装配成功与计数为零并存（"装配了但没面板"是事实）。
    const AboutPluginRow* workflow = findRow(rows, "workflow");
    ASSERT_NE(workflow, nullptr);
    EXPECT_EQ(workflow->status, PluginAssemblyStatus::Ok);
    EXPECT_EQ(workflow->panelCount, 0u);
    EXPECT_EQ(workflow->commandCount, 0u);

    // 未命中行：占位形态不变（阶段 A 其余五插件）。
    const AboutPluginRow* modeling = findRow(rows, "modeling");
    ASSERT_NE(modeling, nullptr);
    EXPECT_EQ(modeling->status, PluginAssemblyStatus::NotAssembled);
    EXPECT_EQ(modeling->statusText, u8"未装配");
}

/**
 * 装配器违约防御：同一插件重复报告（§11.1"每插件恰好一次"被违反）时取
 * 首条——输出确定（NFR-COR-02 精神：同输入同输出），不因报告序抖动。
 */
TEST(AboutModel, DuplicateReportsTakeFirstDeterministically_UI_T10)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);
    PluginAssemblyReport first;
    first.pluginId = "selection";
    first.ok = true;
    first.panelsLoaded = 1;
    PluginAssemblyReport second;
    second.pluginId = "selection";
    second.ok = false;
    second.panelsLoaded = 5;

    const std::vector<AboutPluginRow> rows = aboutPluginRows({first, second});
    const AboutPluginRow* row = findRow(rows, "selection");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->status, PluginAssemblyStatus::Ok) << "重复报告未按首条取值";
    EXPECT_EQ(row->panelCount, 1u) << "重复报告未按首条取值";
}

// =====================================================================
// 标题与状态文案（§3.5 键族——UX-02 零内部插件名）
// =====================================================================

/**
 * UX-02 红线的清单侧自证：标题列必须出自 UiText 键解析（中文名），内部
 * 插件 token 不进用户可见文本；键族（八标题＋三状态）全部在 UiText 登记
 * ——键表与白名单同步由本用例钉住（漏登记＝aboutPluginRows fail-fast，
 * 本用例先行盘点给出可定位失败）。
 */
TEST(AboutModel, TitlesResolvedViaUiText_NoRawTokenLeak_UX02_UI_T10)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    // 键盘点：11 个 UI-T10 键全部已登记（UiText 唯一出口的登记面）。
    const std::vector<TextKey> keys = registeredTextKeys();
    const auto contains = [&keys](const TextKey& key) {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    };
    for (const std::string& token : pluginUiWhitelist()) {
        EXPECT_TRUE(contains(pluginTitleKey(token)))
            << "插件标题键未登记: " << pluginTitleKey(token);
    }
    for (const TextKey key : {pluginAssemblyStatusLabelKey(PluginAssemblyStatus::NotAssembled),
                              pluginAssemblyStatusLabelKey(PluginAssemblyStatus::Ok),
                              pluginAssemblyStatusLabelKey(PluginAssemblyStatus::Failed)}) {
        EXPECT_TRUE(contains(key)) << "装配状态键未登记: " << key;
    }

    // 呈现面：标题为非空中文解析值且不等于 token（零内部插件名字面）。
    for (const AboutPluginRow& row : aboutPluginRows({})) {
        EXPECT_FALSE(row.titleText.empty()) << "标题为空: " << row.pluginId;
        EXPECT_NE(row.titleText, row.pluginId)
            << "标题列泄漏内部插件 token（UX-02）: " << row.pluginId;
        EXPECT_EQ(row.titleText, resolveText(pluginTitleKey(row.pluginId)))
            << "标题列与 UiText 解析值不同源: " << row.pluginId;
    }
}

// =====================================================================
// 版本区装配（NFR-DEP-05——"与注入基线一致"的模型半区）
// =====================================================================

/**
 * UI-PLG-2 断言半区三：基线已注入时版本行与注入值逐字一致（产品行在首、
 * 组件行序＝注入序、值原样呈现——ui 不改写不补默认，NFR-DEP-05 呈现半区
 * 语义）。
 */
TEST(AboutModel, VersionRowsMirrorInjectedBaseline_NFR_DEP_05_UI_T10_ACC1)
{
    IRD_TEST_INFO("NFR-DEP-05", {}, std::nullopt);

    // 注入基线（L5 值形态的替身——O-31：版本数据注入，测试以值对象承载）。
    AboutVersionBaseline baseline;
    baseline.available = true;
    baseline.productVersion = "2026.9-stageA";
    baseline.components = {
        {"设计框架", "5.0.0"},
        {"界面框架", "6.11.1"},
        {"碰撞后端", ""},  // 基线未登记版本——呈现「不适用」
    };

    const std::vector<AboutVersionRow> rows = aboutVersionRows(baseline);

    // 形态：1 产品行＋3 组件行。
    ASSERT_EQ(rows.size(), 4u);
    // 产品行在首、值与注入逐字一致。
    EXPECT_EQ(rows[0].key, "product");
    EXPECT_EQ(rows[0].label, u8"产品版本");
    EXPECT_EQ(rows[0].valueText, "2026.9-stageA");
    // 组件行序＝注入序、名值原样（"与注入基线一致"的字面断言）。
    EXPECT_EQ(rows[1].key, "component-1");
    EXPECT_EQ(rows[1].label, "设计框架");
    EXPECT_EQ(rows[1].valueText, "5.0.0");
    EXPECT_EQ(rows[2].key, "component-2");
    EXPECT_EQ(rows[2].label, "界面框架");
    EXPECT_EQ(rows[2].valueText, "6.11.1");
    // 未登记版本项：「不适用」占位（ERR-01——不伪造版本号）。
    EXPECT_EQ(rows[3].label, "碰撞后端");
    EXPECT_EQ(rows[3].valueText, notApplicableText());
}

/**
 * 基线未注入（阶段 A 常态——WP-24-T01 产出前）：单行「未装载」占位，
 * 不虚构任何版本号（与策略摘要卡"未装载"同口径）。
 */
TEST(AboutModel, UnavailableBaselineSinglePlaceholderRow_NFR_DEP_05_UI_T10)
{
    IRD_TEST_INFO("NFR-DEP-05", {}, std::nullopt);
    AboutVersionBaseline baseline;  // available=false（默认）

    const std::vector<AboutVersionRow> rows = aboutVersionRows(baseline);
    ASSERT_EQ(rows.size(), 1u) << "未装载形态应只呈现单行占位";
    EXPECT_EQ(rows[0].label, u8"版本基线");
    EXPECT_EQ(rows[0].valueText, u8"未装载");

    // 占位行零数字（"不发明数值"字面自证——占位文案夹带数字即伪造版本）。
    EXPECT_EQ(std::any_of(rows[0].valueText.begin(), rows[0].valueText.end(),
                          [](unsigned char c) { return std::isdigit(c) != 0; }),
              false)
        << "占位值含数字（疑伪造版本号）";
}

/**
 * 占位与 fail-fast 面（ERR-01＋调用方错误语义）：空产品版本→「不适用」；
 * 组件名为空＝注入方装配错误→fail-fast（无名行无法呈现也无法定位）。
 */
TEST(AboutModel, EmptyVersionPlaceholdAndUnnamedComponentFailsFast_UI_T10)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    // 空产品版本：「不适用」占位（available==true 但该项未登记）。
    AboutVersionBaseline baseline;
    baseline.available = true;
    baseline.productVersion = "";
    baseline.components = {{"某组件", "1.0"}};
    const std::vector<AboutVersionRow> rows = aboutVersionRows(baseline);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].valueText, notApplicableText());

    // 空组件名：fail-fast（std::invalid_argument——调用方装配违约）。
    AboutVersionBaseline bad;
    bad.available = true;
    bad.components = {{"", "1.0"}};
    EXPECT_THROW(aboutVersionRows(bad), std::invalid_argument)
        << "空组件名未按调用方错误 fail-fast";
}

// =====================================================================
// 用户手册入口路径（§11.4——share\ 帮助文件落位口径）
// =====================================================================

/**
 * 手册入口路径按部署布局解析（<appDir>/../share/ird/user-manual/index.html
 * ——实现口径登记 ui.md §16.7 v1.2）：绝对路径＋固定相对布局，解析只依赖
 * 应用目录（不依赖工作目录——测试/部署环境同构）。
 */
TEST(AboutModel, ManualPathFollowsShareDeploymentLayout_UX14_UI_T10_ACC1)
{
    IRD_TEST_INFO("UX-14", {}, std::nullopt);
    const std::string path = userManualPath();

    // 绝对路径（QCoreApplication 已构造——applicationDirPath 前置成立）。
    EXPECT_FALSE(path.empty()) << "手册入口路径为空";
    EXPECT_TRUE(QFileInfo(QString::fromStdString(path)).isAbsolute())
        << "手册入口路径非绝对路径: " << path;
    // 固定相对布局（share\ 帮助文件约定——§11.4；分隔符统一 '/'）。
    EXPECT_NE(path.find("share/ird/user-manual/index.html"), std::string::npos)
        << "手册入口路径偏离部署布局: " << path;
}

/**
 * UI-PLG-1 模型半区（UI-T14 具名落位——§12.3 行"白名单插件工厂抛出→
 * 错误占位＋UI-PLUGIN-ASSEMBLY-FAILED；其余插件正常"）：装配失败的插件
 * 以"装配失败"占位行如实呈现＋失败诊断码保留（§11.3 失败隔离——不伪装
 * 成功也不吞证据），同一装配中成功插件行照常充实、未命中白名单项照常
 * 占位——失败只影响失败者自身，行集仍与白名单一致。
 *
 * 与 UI-PLG-2 用例（IntersectionEnrichesAndDropsForeignReports）的分工：
 * 那例钉"交集装配与外来条目防御"的装配规则，本例钉"失败降级"的呈现
 * 语义（SA-01 静态注册红线下的失败隔离面）——工厂抛出本身发生在 L5
 * 装配期（§11.3，IPluginUiRegistrar 归装配任务），ui 侧可观测的合同面
 * 即 PluginAssemblyReport.ok=false 的行投影，本用例以编程报告承载。
 */
TEST(AboutModel, PluginAssemblyFailureDegradationIsolatesRow_UI_PLG_1)
{
    IRD_TEST_INFO("SA-01", {"UX-14"}, std::nullopt);

    // 报告集：一插件装配失败（工厂抛出后的报告形态——ok=false＋稳定码），
    // 一插件装配成功（其余插件正常的对照组），其余白名单项无报告。
    std::vector<PluginAssemblyReport> reports;
    PluginAssemblyReport failed;
    failed.pluginId = "dynamics";
    failed.ok = false;
    failed.failureDiagnostics = {"UI-PLUGIN-ASSEMBLY-FAILED"};
    reports.push_back(failed);
    PluginAssemblyReport healthy;
    healthy.pluginId = "kinematics";
    healthy.ok = true;
    healthy.panelsLoaded = 2;
    healthy.commandsRegistered = 3;
    reports.push_back(healthy);

    const std::vector<AboutPluginRow> rows = aboutPluginRows(reports);
    // 失败隔离：行集仍＝白名单全集（失败不增删行——清单一致性不受损）。
    ASSERT_EQ(rows.size(), pluginUiWhitelist().size())
        << "装配失败改变了清单行集规模";

    // 失败行：错误占位呈现（"装配失败"）＋失败码保留（证据不丢弃）＋
    // 计数如实为零（报告未登记任何已装配能力——零虚构）。
    const AboutPluginRow* dynamics = findRow(rows, "dynamics");
    ASSERT_NE(dynamics, nullptr);
    EXPECT_EQ(dynamics->status, PluginAssemblyStatus::Failed);
    EXPECT_EQ(dynamics->statusText, u8"装配失败");
    ASSERT_FALSE(dynamics->failureDiagnostics.empty())
        << "失败行丢失了失败诊断码（§11.3 失败证据必须保留）";
    EXPECT_EQ(dynamics->failureDiagnostics[0], "UI-PLUGIN-ASSEMBLY-FAILED");
    EXPECT_EQ(dynamics->panelCount, 0u);
    EXPECT_EQ(dynamics->commandCount, 0u);

    // 成功行：其余插件正常——状态/计数照常充实（失败隔离的另一半）。
    const AboutPluginRow* kinematics = findRow(rows, "kinematics");
    ASSERT_NE(kinematics, nullptr);
    EXPECT_EQ(kinematics->status, PluginAssemblyStatus::Ok);
    EXPECT_EQ(kinematics->statusText, u8"已装配");
    EXPECT_EQ(kinematics->panelCount, 2u);
    EXPECT_EQ(kinematics->commandCount, 3u);
    EXPECT_TRUE(kinematics->failureDiagnostics.empty());

    // 未命中行：照常"未装配"占位（同一装配内的第三种合法形态——占位
    // ≠失败态，两词不混用）。
    const AboutPluginRow* modeling = findRow(rows, "modeling");
    ASSERT_NE(modeling, nullptr);
    EXPECT_EQ(modeling->status, PluginAssemblyStatus::NotAssembled);
    EXPECT_EQ(modeling->statusText, u8"未装配");
    EXPECT_TRUE(modeling->failureDiagnostics.empty());
}

}  // namespace
