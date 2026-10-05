/**
 * @file   ModelingUiModuleHostWiringGuiTest.cpp
 * @brief  建模模块宿主接线 Widgets 级回归钉（UI-T43——集成模式专属 gating）。
 *
 * 设计依据：
 *   - units/ui.md §7.6（只读条件——L-7 门控输入的面板半区）、§11.2
 *     （IPluginUiModule 装配缝——bindTextResolver/bindCommandSubmit 暂存
 *     时序语义）；
 *   - units/modeling.md §9.7.2/§9.7.3（面板接线与命令目录）；
 *   - 先例：ui/gui_test（QApplication main＋findChild 定位）、
 *     modeling/test/PluginModuleT03BTest.cpp（模块级被测面先例）。
 *
 * ★ gating 说明（集成模式专属）：本文件被测类型 ModelingUiModule 构造期
 * 持 policy 行程评估器符号（makeJointLimitEvaluator——policy/JointLimits.cpp
 * 仅集成模式编译），冒烟模式链接该 obj 即 LNK2019。gating 同
 * PluginModuleT03BTest/ReadinessTest 先例（modeling/CMakeLists.txt TARGET
 * sdurw_kinematics 条件编入）——冒烟口径"目标注册＋include 路径"不受影响。
 *
 * 断言纪律：与 ModelingPanelGuiTest 同款——findChild 公共控件树定位，
 * 不触面板私有面；命令按钮与 modelingDomainCommands() 目录按下标对账
 * （size 相等断言作序漂移守卫）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWidget>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/ui/UiText.hpp>       // ui::resolveText（生产装配序解析值——UX-02）
#include "plugin/ModelingUiModule.hpp"    // 被测模块（同单元 PRIVATE include 面）
#include "plugin/ModelingPanelWidget.hpp" // 面板具体类型（createPanel 返回值下转型——
                                          // 命令按钮对账需要目录序，QWidget 面不够）
#include "plugin/PanelCommandCatalog.hpp" // modelingDomainCommands（对账目录）

using namespace sdurws::ird;
using namespace sdurws::ird::modeling;

namespace {

/// 命令按钮全集（构造序＝目录序——Qt 子对象按挂树序枚举；诊断历史折叠
/// 钮非命令按钮，按 objectName 剔除）。与 ModelingPanelGuiTest 同款辅助
/// （两文件分目标编入，不共享匿名命名空间——复制面最小化）。
QList<QPushButton*> commandButtonsOf(
    const QWidget& panel, const std::vector<ui::CommandDescriptor>& catalog)
{
    QMap<QString, QPushButton*> byId;
    for (QPushButton* btn : panel.findChildren<QPushButton*>()) {
        if (btn->objectName() == QStringLiteral("ird_modeling_history_toggle")) {
            continue;  // 历史折叠钮不入命令对账（B2 呈现件）
        }
        if (btn->objectName() == QStringLiteral("ird_modeling_more_toggle")) {
            continue;  // 工具区折叠开关不入命令对账（F-502 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_struct_"))) {
            continue;  // 结构操作钮不入命令对账（UI-T47 呈现件——非命令目录按钮）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_edit_"))) {
            continue;  // 编辑页模式钮不入命令对账（UI-T53/T54 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_base_"))) {
            continue;  // 基座页应用/还原钮不入命令对账（UI-T54 呈现件）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_param_"))) {
            continue;  // 编辑页参数表内部钮不入命令对账（UI-T53~T55 呈现件——apply/取消/确认区）
        }
        if (btn->objectName().startsWith(QStringLiteral("ird_modeling_cmd_"))) {
            byId.insert(btn->objectName(), btn);  // 命令钮按 id 归位（目录序输出）
        }
    }
    // F-502 折叠重排后子树序≠目录序——按命令钮 objectName 挂载的 id 后缀
    // 恢复目录序（UI-T56；与 ModelingPanelGuiTest 同款）。
    QList<QPushButton*> ordered;
    for (const auto& desc : catalog) {
        const QString key = QStringLiteral("ird_modeling_cmd_")
                            + QString::fromStdString(desc.id);
        if (byId.contains(key)) { ordered.push_back(byId.value(key)); }
    }
    return ordered;
}

}  // namespace

// =====================================================================
// UI-T43：生产装配序下的文案解析回归（审核建议 4 的实质面——
// DomainAssembly::assembleDomainPlugins 对建模域先 bindTextResolver 后经
// 面板工厂 createPanel 的顺序，必须产出全中文按钮文本；titleKey 原文＝
// "cmd." 前缀内部键，泄漏即 UX-02 违约）。此前该绑定零测试钉住。
// =====================================================================

TEST(ModelingUiModuleHostWiring, TextResolverBoundBeforePanelCreation_NoInternalKeyLeak_UX02_UI_T43)
{
    IRD_TEST_INFO("UX-02", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    // 生产装配序复刻（DomainAssembly.cpp 装配段同序）：解析器先于面板
    // 工厂——走模块暂存面（m_textResolver→createPanel 应用），装配序
    // 无关性同时被本用例覆盖。
    module.bindTextResolver([](const std::string& key) {
        const std::string text = ui::resolveText(key);
        return QString::fromStdString(text.empty() ? key : text);
    });
    module.bindCommandSubmit([](const ui::CommandId&) {});

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*widget, catalog);
    ASSERT_EQ(buttons.size(), catalog.size())
        << "命令按钮与目录错位（构造序漂移）";

    for (std::size_t i = 0; i < buttons.size(); ++i) {
        // 标题＝UiText 解析值（键已登记非空——解析空回退键名原文的兜底
        // 形态在本套目录下不应出现，出现即 UiText 登记缺失）。
        const std::string expected = ui::resolveText(catalog[i].titleKey);
        ASSERT_FALSE(expected.empty()) << "UiText 缺标题登记：" << catalog[i].titleKey;
        EXPECT_EQ(buttons[i]->text(), QString::fromStdString(expected))
            << "标题未按宿主解析器呈现工程中文：" << catalog[i].id;
        EXPECT_FALSE(buttons[i]->text().startsWith(QStringLiteral("cmd.")))
            << "按钮文本泄漏内部命令键（UX-02 违约）：" << catalog[i].id;
    }
    // tooltip 同源复验（B4 反断言在"模块暂存解析器"生产序下的回归形态）：
    // 悬停文案不得是裸命令 id。
    for (const QPushButton* btn : buttons) {
        for (const auto& desc : catalog) {
            EXPECT_NE(btn->toolTip(), QString::fromStdString(desc.id))
                << "tooltip 呈现裸命令 id：" << desc.id;
        }
    }
}

// =====================================================================
// UI-T43：模块可写链路（新增 setWritable 面）——①面板创建前切换只读＝
// 暂存初值（createPanel 按只读创建——装配序无关，杜绝面板恒按可写创建
// 的初值失实）；②面板创建后切换＝即时转发（双向）。可用性快照恒全放行
// ——使能态只随 writable 翻转，即链路转发的直接观测面。
// =====================================================================

TEST(ModelingUiModuleHostWiring, WritableChain_PendingInitialAndLiveForward_L7_UI_T43)
{
    IRD_TEST_INFO("ERR-01", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();
    module.setWritable(false);  // 面板创建前——暂存初值形态
    module.bindCommandSubmit([](const ui::CommandId&) {});
    module.bindCommandAvailability([](const ui::CommandId&) {
        return ui::CommandAvailability{true, true, true, ui::DisableReason{}};
    });

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    const auto catalog = modelingDomainCommands();
    const auto buttons = commandButtonsOf(*widget, catalog);
    ASSERT_EQ(buttons.size(), catalog.size());

    // ①初始态兑现：写命令按只读禁用、只读命令可用（修复前面板恒按
    //   可写创建，全部亮起）。
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "面板初始可写态未兑现模块暂存值：" << catalog[i].id;
    }

    // ②创建后即时转发（恢复→再降级双向）——面板指针在模块侧，宿主经
    //   门面 setWritable 驱动（本用例直驱模块同语义，转发链少一层）。
    module.setWritable(true);
    for (const QPushButton* btn : buttons) {
        EXPECT_TRUE(btn->isEnabled()) << "恢复可写未即时转发面板";
    }
    module.setWritable(false);
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        EXPECT_EQ(buttons[i]->isEnabled(), catalog[i].readOnlyAllowed)
            << "再降级只读未即时转发面板：" << catalog[i].id;
    }
}

// =====================================================================
// UI-T59：appliedPreview 推送接线缺口修复——UI-T41 A3 残留（模块有存有清
// 但从未推送面板，预览页恒空态）。本组用例钉扎预览态三个变迁点。
// =====================================================================

/**
 * 预览态变迁推送链（UI-T59）：createPanel 即接线（构造后空态）→
 * noteAppliedRevision 定格即推送（摘要呈现——修复前恒空态的缺口面）→
 * onRevisionCommitted（撤销/重做形态）失效即推送（空态诚实回落）；
 * bindWorkCellPreview 晚绑定＝面板供给器即时补线（XML 类请求达宿主）。
 */
TEST(ModelingUiModuleHostWiring, AppliedPreviewPushChain_Fix_UI_T59)
{
    IRD_TEST_INFO("D-MDL-10", {}, std::nullopt);

    ModelingUiModule module;
    module.seedTemplateSession();

    // 预览供给替身（宿主形态——缺席轨迹返回 false；应答轨迹返回 stub 文本
    // ＋来源头行。本用例直调模块绑定面（module.bindWorkCellPreview）——
    // 装配门面转发器的接线语义由 UiPlugin 装配面承载，模块级用例不经过
    // 门面（F-519③ 表述修正：原注"经装配门面绑定"与实际直调不符））。
    bool asked = false;
    module.bindWorkCellPreview(
        [&asked](const std::string& kind, std::string& headerLine,
                 std::string& sourceObject, std::string& text,
                 std::string&) -> bool {
            asked = true;
            headerLine = "来源修订 stub｜模型身份 stub｜生成于 stub";
            sourceObject = "stub=1";
            text = "<ird-" + kind + "-stub/>";
            return true;
        });

    QWidget* widget = module.createPanel();
    ASSERT_NE(widget, nullptr);
    QPlainTextEdit* preview =
        widget->findChild<QPlainTextEdit*>(QStringLiteral("ird_modeling_preview_text"));
    ASSERT_NE(preview, nullptr) << "预览页承载（区⑤）";
    QComboBox* combo =
        widget->findChild<QComboBox*>(QStringLiteral("ird_modeling_preview_kind"));
    ASSERT_NE(combo, nullptr);

    // ①创建后即空态（种子会话无已应用修订——D-MDL-10 占位）。
    EXPECT_EQ(combo->currentIndex(), 0);
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")));

    // ②XML 类经供给器（晚绑定转发器已补线——请求达宿主替身）。
    combo->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT_TRUE(asked) << "bindWorkCellPreview 应转发面板供给器";
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("<ird-serial-device-xml-stub/>")));

    // ③定格即推送（修复核心断言——修复前 appliedPreview 只存不推，预览页
    //   恒停留构造初态）：切回摘要轨→noteAppliedRevision→摘要呈现。
    combo->setCurrentIndex(0);
    QApplication::processEvents();
    const core::RevisionId appliedRev = core::RevisionId::generate();
    module.noteAppliedRevision(appliedRev, std::nullopt);
    QApplication::processEvents();
    EXPECT_FALSE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")))
        << "已应用修订定格后预览页不应停留空态（UI-T41 A3 残留缺口修复）";
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("已应用修订快照")))
        << "摘要轨呈现定格内容（generic-6r 种子草稿）";

    // ④失效即推送（撤销/重做形态——锚定分支事件触发复位，空态诚实回落）。
    const core::BranchId branch = core::BranchId::generate();
    module.bindSessionAnchor(branch, appliedRev);
    module.onRevisionCommitted(branch, core::RevisionId::generate());
    QApplication::processEvents();
    EXPECT_TRUE(preview->toPlainText().contains(QStringLiteral("D-MDL-10")))
        << "修订内容未知＝预览页空态诚实呈现（不虚构）";
}
