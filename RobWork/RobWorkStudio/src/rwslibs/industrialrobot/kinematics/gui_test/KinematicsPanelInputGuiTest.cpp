/**
 * @file   KinematicsPanelInputGuiTest.cpp
 * @brief  运动学面板目标输入的 GUI 回归（KIN-P1——审查条目 A07：显示
 *         单位与 SI 真值分离——标签跟随当前单位制、提交按所选单位逆换
 *         算回 SI；会话/求解器侧永远只见 SI 真值）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（KIN-12 显示单位纯投影——读侧换算不改
 *     SI 真值；L-K8 单位切换全面板重投影）；§5.3（IkRequest.targetInBase
 *     ——基座系 SI 目标，求解器只见 SI）；
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §3 A07
 *     的反例数据（切 mm 后标签仍标 m；输入 0.1 被按 ×1000 提交成
 *     100 m——按任一解释均非法）与探针写法（真实 widget＋注入记录
 *     求解器＋QMetaObject 直触槽函数——本文件即该探针的产品测试化）；
 *   - 夹具：KinFkFixture 六轴模型＋TestView（kinematics/test——同单元
 *     测试夹具消费，header-only）。
 *
 * 用例与验收映射（审查报告 §5 KIN-P1 行）：
 *   - 标签一致：mm 制式下三个目标轴标签携带 "mm" 且携带基座系标识；
 *     缺省（SI 直显）下标注 "m"——修复前标签恒为 "（m）"（缺陷面）；
 *   - SI 一致：m/cm/mm 三种制式输入"同一物理目标 0.1 m"（各制式的
 *     正确显示值 0.1/10/100），提交给求解器的 targetInBase 均为
 *     0.1 m——修复前 mm 制式输入 0.1 被正向投影成 100 m（反例形态）；
 *   - 逆换算方向：mm 制式输入 "0.1"（＝0.1 mm＝1e-4 m）提交 1e-4 m，
 *     绝非 100 m（A07 反例数值钉扎）。
 *
 * 线程约束：全部用例在 main 线程（QApplication 线程）执行——widget 与
 * 槽函数直调同线程（UI 纪律的测试镜像）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/Ik.hpp>  // IIkSolver/IkRequest（记录求解器）
#include "KinFkFixture.hpp"              // TestView/sixAxisModel（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelTypes.hpp"          // KinPanelServices/会话态

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 记录求解器替身（审查 A07 探针同款——只捕获请求，零求解语义）
// =====================================================================

/**
 * @brief 仅记录真实控件事件提交的请求（注入 m_services.ikSolver——
 *        隔离检查"输入→请求"数据流；不替代数值求解器正确性测试）。
 */
class WidgetRecordingSolver final : public IIkSolver {
public:
    mutable std::optional<IkRequest> received;  ///< 最近一次收到的请求
    IkOutcome solve(const IkRequest& request) const override
    {
        received = request;
        return IkOutcome{};
    }
};

// =====================================================================
// 面板夹具（六轴模型＋记录求解器——事件探针的最小装配）
// =====================================================================

/// 面板夹具：持有视图/求解器/会话态/面板的存活期（探针用例值语义装配
/// ——与审查探针同构；面板构造即全量初刷，标签随注入制式重建）。
struct PanelHarness {
    TestView view{sixAxisModel()};
    WidgetRecordingSolver solver;
    KinPanelServices services;
    KinModuleSessionState state;
    std::unique_ptr<KinematicsPanelWidget> panel;

    PanelHarness()
    {
        services.modelView = &view;
        services.ikSolver = &solver;
        // 求解配置基线（I-KIN-4 seed≥1——与产品装配纪律同则）。
        state.savedConfig.seed = 1;
        state.savedConfig.regionBudget.seed = 1;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
    }

    /// 目标输入三编辑（构造序＝X/Y/Z——表单登记序，探针按序取值）。
    QList<QLineEdit*> targetEdits() const
    {
        return panel->findChildren<QLineEdit*>();
    }

    /// 目标轴标签文本集合（表单 QLabel——标签随制式断言面）。
    QStringList targetLabelTexts() const
    {
        QStringList texts;
        const QList<QLabel*> labels = panel->findChildren<QLabel*>();
        for (const QLabel* label : labels) {
            const QString text = label->text();
            if (text.startsWith(QStringLiteral("目标 X"))
                || text.startsWith(QStringLiteral("目标 Y"))
                || text.startsWith(QStringLiteral("目标 Z"))) {
                texts << text;  // 只取目标轴行标签（状态行等无关标签不入集）
            }
        }
        return texts;
    }

    /// 直触求解槽（QMetaObject 按名调用——真实控件事件路径）。
    void clickSolve()
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(panel.get(), "onSolveClicked",
                                              Qt::DirectConnection));
    }
};

/// 断言提交目标 X＝期望 SI 值（记录求解器收到的 targetInBase 平移分量
/// ——求解器侧只见 SI 真值的契约钉扎面）。
void expectSubmittedTargetX(const PanelHarness& h, double expectedSiMeters,
                            double tol, const std::string& context = {})
{
    ASSERT_TRUE(h.solver.received.has_value())
        << "求解器未收到请求" << (context.empty() ? "" : ("：" + context));
    EXPECT_NEAR(h.solver.received->targetInBase.P()[0], expectedSiMeters, tol)
        << (context.empty() ? "" : context);
}

}  // namespace

// =====================================================================
// 标签一致（KIN-P1 验收"标签一致"半区——审查 A07 缺陷面①）
// =====================================================================

/// 切到 mm 后标签必须跟随制式（携带 "mm"＋基座系标识），不得仍标 m；
/// 缺省 SI 直显制式标注 "m"。修复前标签为构造期硬编码 "目标 X（m）"，
/// 切换制式后不更新（A07 缺陷面①——本用例修复前红）。
TEST(KinematicsPanelInput, TargetLabels_FollowDisplayUnitSystem_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    PanelHarness h;

    // 缺省（未注入制式＝SI 直显）：三轴标签标注 m＋基座系参考系标识。
    {
        const QStringList texts = h.targetLabelTexts();
        ASSERT_EQ(texts.size(), 3);
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（m，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Y（m，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Z（m，基座系）")));
    }

    // L-K8 切换 mm/deg：标签随制式重建（零重算/零修订——呈现半区）。
    const auto mm = DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_TRUE(mm.has_value());
    h.panel->setDisplayUnits(mm);
    {
        const QStringList texts = h.targetLabelTexts();
        ASSERT_EQ(texts.size(), 3);
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 X（mm，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Y（mm，基座系）")));
        EXPECT_TRUE(texts.contains(QStringLiteral("目标 Z（mm，基座系）")));
        // 不得残留旧制式硬编码形态 "（m）"（精确全串比对已保证制式正确；
        // 此处对修复前的旧文本再补一钉——防回归回退到"标签恒 m"）。
        for (const QString& text : texts) {
            EXPECT_FALSE(text.contains(QStringLiteral("（m）")))
                << text.toStdString();  // QString 无 ostream 运算符——转 std 串
        }
    }
}

// =====================================================================
// 提交逆换算（KIN-P1 验收"m/cm/mm 同一目标的 SI 值一致"——审查 A07
// 缺陷面②：修复前显示值被按 SI→显示的正向投影再放大）
// =====================================================================

/// m/cm/mm 三种制式输入"同一物理目标 X=0.1 m"（各制式正确显示值
/// 0.1/10/100），提交给求解器的 SI 真值必须一致＝0.1 m。修复前 mm
/// 制式输入 100 被 projectLength 正向再放大为 100000 m（方向反转缺陷
/// ——本用例修复前红）。
TEST(KinematicsPanelInput, TargetSubmit_ConsistentSiAcrossUnitSystems_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    // 制式→X 轴输入文本（同一物理目标 0.1 m 的各制式正确显示值）。
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"m", "0.1"},    // m 制式：0.1 m
        {"cm", "10"},    // cm 制式：10 cm＝0.1 m
        {"mm", "100"},   // mm 制式：100 mm＝0.1 m
    };
    for (const auto& [lengthSymbol, inputText] : cases) {
        PanelHarness h;
        const auto units =
            DisplayUnitProjection::tryFind(lengthSymbol, "deg");
        ASSERT_TRUE(units.has_value()) << lengthSymbol;
        h.panel->setDisplayUnits(units);

        const QList<QLineEdit*> edits = h.targetEdits();
        ASSERT_EQ(edits.size(), 3U);
        edits[0]->setText(QString::fromStdString(inputText));
        edits[1]->setText(QStringLiteral("0"));
        edits[2]->setText(QStringLiteral("0"));
        h.clickSolve();
        expectSubmittedTargetX(h, 0.1, 1e-12,
                               "制式 " + lengthSymbol + " 输入 " + inputText);
        EXPECT_DOUBLE_EQ(h.solver.received->targetInBase.P()[1], 0.0);
        EXPECT_DOUBLE_EQ(h.solver.received->targetInBase.P()[2], 0.0);
    }
}

/// A07 反例数值钉扎：mm 制式输入 "0.1"（标签标 mm——即 0.1 mm＝1e-4 m）
/// 提交 1e-4 m。按 m 标签解释应为 0.1 m、按 mm 解释应为 1e-4 m，均不
/// 是修复前的 100 m（审查 A07 原始反例——方向反转的回归钉）。
TEST(KinematicsPanelInput, TargetSubmit_mmInput01IsTenthMillimetre_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"},
                  std::vector<std::string>{"AT-27"});
    PanelHarness h;
    const auto mm = DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_TRUE(mm.has_value());
    h.panel->setDisplayUnits(mm);

    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 3U);
    edits[0]->setText(QStringLiteral("0.1"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    h.clickSolve();
    // 1e-4 m（≠100 m——修复前缺陷值；≠0.1 m——mm 制式按标签解释）。
    expectSubmittedTargetX(h, 1e-4, 1e-15);
}

/// 非有限输入拒绝（NFR-COR-03 非有限不静默——"inf"/"nan" 文本解析成
/// 功但非有限，提交面就地反馈并保留原值，不向求解器传递非有限目标）。
TEST(KinematicsPanelInput, TargetSubmit_RejectsNonFiniteInput_KIN_P1)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "NFR-COR-03"},
                  std::vector<std::string>{});
    PanelHarness h;

    const QList<QLineEdit*> edits = h.targetEdits();
    ASSERT_EQ(edits.size(), 3U);
    edits[0]->setText(QStringLiteral("inf"));
    edits[1]->setText(QStringLiteral("0"));
    edits[2]->setText(QStringLiteral("0"));
    h.clickSolve();
    // 求解器零触达（受理前拒绝）＋状态行如实反馈。
    EXPECT_FALSE(h.solver.received.has_value());
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("非法"),
              std::string::npos);
}
