/**
 * @file   DomainReadinessSummaryCardGuiTest.cpp
 * @brief  跨域就绪摘要卡 Widgets 级测试（UI-T44——sdurws_ird_ui_gui_test
 *         载体）——判定词映射／逐域行渲染／诚实空态三面。
 *
 * 设计依据：
 *   - units/ui.md §4.2 右栏行（诊断摘要的最小兑现——UI-T44）；§6.5/§11.2
 *     （DomainReadinessItem 值语义直投——本卡零判定，测试钉"直投不加工"）；
 *   - UX-02（判定词经 UiText verdict.* 键——词表键与映射函数一致断言）。
 *
 * 先例：PolicySummaryCardGuiTest（QApplication main＋findChild 定位——
 * 卡类测试同款形态；testkit §6.7 单实例纪律，ird_gui 串行标签）。
 *
 * 断言纪律：正文 QLabel 经 findChildren<QLabel*> 文本匹配定位（卡内部
 * 构造私有——测试经公共控件树，不触私有面）。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QString>
#include <vector>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯登记

#include <sdurws/ird/ui/DomainReadinessSummaryCard.hpp>  // 被测卡＋行值＋判定词映射
#include <sdurws/ird/ui/UiText.hpp>                      // resolveText（词表一致性对账）
// （注：本卡消费面＝DomainReadinessSummaryRow 值＋engineeringStatusDisplayName
//  映射——零 IPluginUiModule 依赖；"域模块投影→行"的宿主装配语义由宿主
//  集成路径承载，O-31 ui 对 project 零包含纪律在此同样约束测试替身——
//  不为替身引入 project 完整类型。）

using namespace sdurws::ird;
using namespace sdurws::ird::ui;

namespace {

/// 正文标签现取（卡内含标题/正文两 QLabel——返回含目标子串的那只）。
QLabel* findBodyLabel(const DomainReadinessSummaryCard& card, const QString& fragment)
{
    for (QLabel* label : card.findChildren<QLabel*>()) {
        if (label->text().contains(fragment)) {
            return label;
        }
    }
    return nullptr;
}

}  // namespace

/// 判定词映射：四态与 UiText verdict.* 键一一对应（UX-02 词表唯一出口；
/// 键值漂移即本用例翻红——映射函数与词表的对账面）。
TEST(DomainReadinessSummaryCard, VerdictMapping_CoversFourStatuses_UX02_UI_T44)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-02"}, std::vector<std::string>{});

    EXPECT_EQ(ui::engineeringStatusDisplayName(core::EngineeringStatus::Feasible),
              QString::fromStdString(ui::resolveText("verdict.feasible")));
    EXPECT_EQ(ui::engineeringStatusDisplayName(
                  core::EngineeringStatus::EngineeringInfeasible),
              QString::fromStdString(ui::resolveText("verdict.engineering-infeasible")));
    EXPECT_EQ(ui::engineeringStatusDisplayName(
                  core::EngineeringStatus::DataInsufficient),
              QString::fromStdString(ui::resolveText("verdict.data-insufficient")));
    EXPECT_EQ(ui::engineeringStatusDisplayName(core::EngineeringStatus::NotApplicable),
              QString::fromStdString(ui::resolveText("verdict.not-applicable")));

    // 词值工程用语抽验（键登记缺失/被改名→resolveText 回退键名原文，与
    // 期望中文不等即翻红——词表完备性对账，F-430 处置同款纪律）。
    EXPECT_EQ(ui::engineeringStatusDisplayName(core::EngineeringStatus::Feasible),
              QString::fromUtf8("可行"));
    EXPECT_EQ(ui::engineeringStatusDisplayName(
                  core::EngineeringStatus::NotApplicable),
              QString::fromUtf8("待提交后判定"));
}

/// 逐域行渲染：多域投影行 →"域名：判定词（附注）"逐行呈现（§6.5 值语义
/// 直投——verdict/inputComplete 不加工；DataInsufficient 的"输入不完整"
/// 由判定词承载，附注不重复）。
TEST(DomainReadinessSummaryCard, SetRows_RendersDomainVerdictLines_UI_T44)
{
    IRD_TEST_INFO(std::vector<std::string>{"REQ-06"}, std::vector<std::string>{});

    DomainReadinessSummaryCard card(QStringLiteral("跨域就绪摘要"));

    std::vector<ui::DomainReadinessSummaryRow> rows;
    ui::DomainReadinessSummaryRow modeling;
    modeling.domainLabel = QStringLiteral("建模");
    modeling.verdictText = ui::engineeringStatusDisplayName(
        core::EngineeringStatus::Feasible);
    rows.push_back(modeling);
    ui::DomainReadinessSummaryRow requirements;
    requirements.domainLabel = QStringLiteral("需求");
    requirements.verdictText = ui::engineeringStatusDisplayName(
        core::EngineeringStatus::DataInsufficient);
    rows.push_back(requirements);
    ui::DomainReadinessSummaryRow kinematics;
    kinematics.domainLabel = QStringLiteral("运动学");
    kinematics.verdictText = ui::engineeringStatusDisplayName(
        core::EngineeringStatus::NotApplicable);
    kinematics.noteText = QStringLiteral("输入不完整");  // 非 DataInsufficient 态才带附注
    rows.push_back(kinematics);

    card.setRows(rows);

    ASSERT_NE(findBodyLabel(card, QStringLiteral("建模：可行")), nullptr)
        << "建模行判定词缺失";
    ASSERT_NE(findBodyLabel(card, QStringLiteral("需求：输入不完整")), nullptr)
        << "需求行判定词缺失";
    ASSERT_NE(findBodyLabel(card,
                            QStringLiteral("运动学：待提交后判定（输入不完整）")),
              nullptr)
        << "运动行判定词/附注缺失";
}

/// 诚实空态：空行集 →"暂无域就绪投影"（不虚构三域齐全——域装配缺席/
/// 投影空集的合法二态，ERR-01 呈现纪律）。
TEST(DomainReadinessSummaryCard, SetRows_EmptyRows_HonestEmptyState_ERR01_UI_T44)
{
    IRD_TEST_INFO(std::vector<std::string>{"ERR-01"}, std::vector<std::string>{});

    DomainReadinessSummaryCard card(QStringLiteral("跨域就绪摘要"));
    card.setRows({});
    ASSERT_NE(findBodyLabel(card, QStringLiteral("暂无域就绪投影")), nullptr)
        << "空态行缺失（可能虚构了域行）";
}
