/**
 * @file   KinematicsPanelSessionGuiTest.cpp
 * @brief  运动学面板"指标显示与会话操作"GUI 回归（KIN-P3——审查条目
 *         A05：混合链逐轴单位投影；A09：复位 Home 具名来源）。
 *
 * 设计依据：
 *   - units/kinematics.md §9.8（KIN-12 显示单位纯投影——关节向量逐轴按
 *     其天然量纲投影；L-K4 会话姿态零修订——复位 Home 值由调用方从命名
 *     位姿集保留键 homeConfiguration 读取，绝不以当前姿态充当 Home）；
 *   - 审查报告 audit/kin-current/运动学模块重新审查-20261016.md §3 的
 *     A05/A09 反例与探针写法（真实 widget＋注入服务＋QMetaObject 直触
 *     槽函数——本文件即探针的产品测试化，与 KIN-P1 A07 的
 *     KinematicsPanelInputGuiTest 同形态分工）；
 *   - 夹具：KinFkFixture 转动＋移动混合链（revPrismModel）与六轴模型
 *     （sixAxisModel）＋TestView（kinematics/test——同单元测试夹具消费，
 *     header-only）。
 *
 * 用例与验收映射（审查报告 §5 KIN-P3 行）：
 *   - A05 逐轴单位：解表 q 列对混合链解（转动 0.1 rad＋移动 0.1 m）在
 *     mm/deg 制式下必须逐轴投影为 "5.72958, 100"——修复前全轴按角度
 *     投影（移动轴 0.1 m 误显示 5.72958，审查 A05 反例数值）；
 *   - A09 具名来源：无具名 Home 来源时点击"复位 Home"必须显式拒绝
 *     （状态行"不可用"＋会话姿态零写回）——修复前按钮把当前姿态原样
 *     写回并谎报"已复位 Home"。
 *
 * 数据面说明（解集直注入而非按钮求解）：A05 的缺陷面在显示投影半区
 * （reprojectSolutionColumns/解表渲染）——会话解集按域值直注入会话态
 * （KinModuleSessionState 契约承载），与 KIN-P2 已闭环的按钮请求装配
 * 解耦；求解器正确性归 IkTest（审查 A05 的定位即显示层）。
 *
 * 线程约束：全部用例在 main 线程（QApplication 线程）执行——widget 与
 * 槽函数直调同线程（UI 纪律的测试镜像）。
 */

#include <gtest/gtest.h>

#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>  // IRD_TEST_INFO——需求/AT 追溯

#include <QMetaObject>
#include <QPushButton>
#include <QTableWidget>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sdurws/ird/kinematics/Fk.hpp>        // FkEvaluator（真实产品实现）
#include <sdurws/ird/kinematics/SolutionSet.hpp>  // KinematicSolutionSet（会话视图）
#include "KinFkFixture.hpp"              // TestView/revPrismModel/sixAxisModel（同单元夹具）
#include "plugin/KinematicsPanelWidget.hpp"  // 被测面板（真实 QWidget）
#include "plugin/KinPanelTypes.hpp"          // KinPanelServices/会话态

// ---- 使用声明（与被测代码同一命名空间——用例可读性）--------------------
using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {

// =====================================================================
// 面板夹具（模型可注入——A05 用混合链、A09 用六轴；探针用例值语义装配
// ——与审查探针/KinematicsPanelInputGuiTest 同构）
// =====================================================================

/// 面板夹具：持有模型/视图/评估器/会话姿态/会话态/面板的存活期。
/// ★ 两段式装配（缝先注、面板后建）：widget 构造期**值拷贝**服务缝聚合
/// ——构造后再改 services 字段面板副本不可见；故本夹具构造器只装配基
/// 础缝，需要注入 homePose 等可选缝的用例先赋缝再调 createPanel()。
struct SessionPanelHarness {
    runtime::CanonicalModel model;   ///< 规范模型（值持有——构造器注入几何）
    TestView view;                   ///< 宿主注入替身（IKinRuntimeView 最小实现）
    FkEvaluator fk;                  ///< 真实 FK 评估器（位姿指标数据源）
    KinSessionPose sessionPose;      ///< 会话姿态（真实容器——L-K4 唯一写点）
    KinPanelServices services;       ///< 服务缝（homePose 缝由用例按需注入）
    KinModuleSessionState state;     ///< 插件会话态
    std::unique_ptr<KinematicsPanelWidget> panel;

    explicit SessionPanelHarness(runtime::CanonicalModel m)
        : model(std::move(m)),
          view(model)
    {
        services.modelView = &view;
        services.fkEvaluator = &fk;
        services.sessionPose = &sessionPose;
        // 求解配置基线（I-KIN-4 seed≥1——与产品装配纪律同则）。
        state.savedConfig.seed = 1;
        state.savedConfig.regionBudget.seed = 1;
    }

    /// 建面板（缝注入完成后的第二段——构造期全量初刷即含按钮可用性）。
    void createPanel()
    {
        ASSERT_FALSE(panelCreated) << "面板只建一次（缝注入须在此之前）";
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
        panelCreated = true;
    }

private:
    bool panelCreated = false;  ///< createPanel 一次性守卫

public:
    /// 向会话态注入单解集（原值＋排序视图双面——与 runSinglePointSolve
    /// 的唯一写点同构；A05 显示投影面的确定性输入）。
    void injectSessionSolution(std::vector<double> q)
    {
        IkSolutionSet set;
        KinematicSolution s;
        s.q = std::move(q);          // 混合链解（rad|m 链序——权威值）
        s.minimumJointMargin = 0.5;  // 无量纲（有限——A05 只看 q 列投影）
        s.manipulability = 1.0;
        s.conditionNumber = 2.0;
        s.positionResidual = 0.0;    // m
        s.orientationResidual = 0.0; // rad
        s.signature = "sig-mixed";
        set.solutions = {s};
        set.statistics.rawCount = 1;
        set.statistics.convergedCount = 1;
        set.statistics.dedupedCount = 1;
        state.lastSessionSolutionSet =
            std::make_shared<const IkSolutionSet>(set);
        state.lastSessionSolutionSetView =
            std::make_shared<const KinematicSolutionSet>(set);
    }

    /// 解表定位（水平表头首列文本＝"rank"——不依赖 findChildren 序）。
    QTableWidget* solutionTable() const
    {
        const QList<QTableWidget*> tables = panel->findChildren<QTableWidget*>();
        for (QTableWidget* table : tables) {
            if (table->horizontalHeaderItem(0) != nullptr
                && table->horizontalHeaderItem(0)->text()
                       == QStringLiteral("rank")) {
                return table;
            }
        }
        return nullptr;
    }

    /// "复位 Home"按钮定位（按可读文本——真实控件事件路径的点击靶）。
    QPushButton* homeButton() const
    {
        const QList<QPushButton*> buttons = panel->findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            if (button->text() == QStringLiteral("复位 Home")) {
                return button;
            }
        }
        return nullptr;
    }
};

/// 命令提交计数缝（零修订断言锚——复位 Home 全路径不得触碰命令出口）。
struct CountingSubmit {
    int count = 0;  ///< 提交计数（KIN-06/AT-04 零修订断言锚）
    KinCommandSubmitFn fn()
    {
        return [this](const std::string&) { ++count; };
    }
};

}  // namespace

// =====================================================================
// KIN-P3 回归（审查 A05 混合链逐轴单位／A09 具名 Home 来源）——widget
// 半区；零 Qt 半区在 sdurws_ird_kinematics_test（PluginPanelTest）。
// 修复前红证见构建留痕 traceability/builds/kin-p3/（红→绿两段记录）。
// =====================================================================

/// A05 回归：混合链解（转动 J1＝0.1 rad＋移动 J2＝0.1 m）的解表 q 列
/// 逐轴投影——mm/deg 制式下 "5.72958, 100"（移动轴 0.1 m＝100 mm）；
/// 切回 SI 直显制式恒等还原 "0.1, 0.1"（重投影零真值漂移——V-17）。
/// 修复前全轴按角度投影："5.72958, 5.72958"（审查 A05 反例数值）。
TEST(KinematicsPanelSession, SolutionColumn_PerAxisUnitsMixedChain_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12"}, std::vector<std::string>{"AT-27"});
    SessionPanelHarness h(revPrismModel());
    // 会话态注入混合链单解（J1 转动 0.1 rad；J2 移动 0.1 m）——先注后建
    // （widget 构造期值拷贝服务缝/初刷会话态）。
    h.injectSessionSolution({0.1, 0.1});
    h.createPanel();

    // mm/deg 制式：q 列逐轴投影（修复前此处为 "5.72958, 5.72958"——红）。
    const auto units = DisplayUnitProjection::tryFind("mm", "deg");
    ASSERT_TRUE(units.has_value());
    h.panel->setDisplayUnits(units);
    QTableWidget* table = h.solutionTable();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 1);
    const QString qTextDeg = table->item(0, 2)->text();  // 列 2＝q（表头登记序）
    EXPECT_EQ(qTextDeg.toStdString(), "5.72958, 100");

    // 切回 SI 直显制式：恒等还原（重投影以 SI 真值为唯一输入——结果
    // 不动、真值不漂移，L-K8/V-17 呈现半区）。
    h.panel->setDisplayUnits(std::optional<DisplayUnitProjection>{});
    QTableWidget* tableSi = h.solutionTable();
    ASSERT_NE(tableSi, nullptr);
    const QString qTextSi = tableSi->item(0, 2)->text();
    EXPECT_EQ(qTextSi.toStdString(), "0.1, 0.1");
}

/// A09 回归①：具名 Home 来源在场——点击"复位 Home"以来源值覆写会话
/// 姿态（绝不写当前姿态），状态行如实"已复位 Home"，命令出口零触碰
/// （零修订——KIN-06/AT-04）。
TEST(KinematicsPanelSession, ResetHome_UsesNamedSource_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{"AT-04"});
    SessionPanelHarness h(sixAxisModel());
    CountingSubmit submit;
    h.services.commandSubmit = submit.fn();
    // 具名 Home（装配层从命名位姿集保留键 homeConfiguration 解析的端口
    // 投影——与当前姿态 0.4 明显不同，写回值可分辨来源）。缝先注、面板
    // 后建（widget 构造期值拷贝服务缝——后注不可见）。
    const std::vector<double> home{1.0, 2.0, -1.0, 0.5, -0.5, 3.0};
    h.services.homePose = [&home] {
        return std::optional<std::vector<double>>(home);
    };
    h.sessionPose.setJointConfiguration(std::vector<double>(6, 0.4));
    h.createPanel();

    QPushButton* button = h.homeButton();
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isEnabled());  // 来源在场＝按钮可用
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onResetHomeClicked",
                                          Qt::DirectConnection));
    // 会话姿态＝具名 Home（非当前 0.4——修复前原样写回的假成功已废）。
    EXPECT_EQ(h.sessionPose.jointConfiguration(), home);
    EXPECT_NE(h.panel->lastStatusText().toStdString().find("已复位 Home"),
              std::string::npos)
        << h.panel->lastStatusText().toStdString();
    EXPECT_EQ(submit.count, 0);  // 零修订：命令出口零触碰
}

/// A09 回归②：来源缝未装配——按钮显式禁用（可达性不虚构，与其他缝
/// 降级纪律同案）；只读会话不禁用（reset-session-pose＝readOnlyAllowed
/// ——会话级命令，目录表权威）。
TEST(KinematicsPanelSession, ResetHome_MissingSourceDisablesButton_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{});
    // 无缝面板：来源未装配＝按钮显式禁用（可达性不虚构——降级纪律）。
    SessionPanelHarness h(sixAxisModel());  // homePose 缝未注入
    h.createPanel();
    QPushButton* button = h.homeButton();
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());  // 来源未装配＝显式禁用

    // 只读会话门控对照：复位 Home 允许只读（reset-session-pose＝
    // readOnlyAllowed=true——目录表权威）——缝在场时只读会话不禁用。
    SessionPanelHarness ro(sixAxisModel());
    ro.services.homePose = [] {
        return std::optional<std::vector<double>>(std::vector<double>(6, 0.0));
    };
    ro.state.writable = false;  // 只读会话（构造前注入——面板引用会话态）
    ro.createPanel();
    QPushButton* roButton = ro.homeButton();
    ASSERT_NE(roButton, nullptr);
    EXPECT_TRUE(roButton->isEnabled());  // 缝在场＋只读会话＝可用
    ro.panel->setWritable(false);        // 运行期只读切换——门控路径同则
    EXPECT_TRUE(roButton->isEnabled());
}

/// A09 回归③：缝在场但模型/会话未定义 Home——按钮可用、点击给明确
/// 提示（"未定义…未写回"），会话姿态零写回（绝不以当前姿态充当 Home）。
TEST(KinematicsPanelSession, ResetHome_SourceAbsentExplicitHintNoWriteback_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{"AT-04"});
    SessionPanelHarness h(sixAxisModel());
    // 缝在场但来源未定义 Home（nullopt）——先注缝后建面板（值拷贝纪律）。
    h.services.homePose = [] { return std::optional<std::vector<double>>{}; };
    h.sessionPose.setJointConfiguration(std::vector<double>(6, 0.4));
    h.createPanel();

    QPushButton* button = h.homeButton();
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isEnabled());  // 缝在场＝可用（提示在点击路径）
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onResetHomeClicked",
                                          Qt::DirectConnection));
    const std::string status = h.panel->lastStatusText().toStdString();
    EXPECT_NE(status.find("未定义"), std::string::npos) << status;
    EXPECT_NE(status.find("未写回"), std::string::npos) << status;
    EXPECT_EQ(h.sessionPose.jointConfiguration(), std::vector<double>(6, 0.4));
}

/// A09 回归④：来源值维度与设备自由度不符——点击显式反馈（"维度…未
/// 写回"），零写回（不钳制不截断——NFR-COR-03）。
TEST(KinematicsPanelSession, ResetHome_DimensionMismatchExplicitHint_KIN_P3)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-06"}, std::vector<std::string>{});
    SessionPanelHarness h(sixAxisModel());  // 六轴模型（dof=6）
    h.services.homePose = [] {
        // 维度 1≠6（违约形态）。
        return std::optional<std::vector<double>>(std::vector<double>{1.0});
    };
    h.sessionPose.setJointConfiguration(std::vector<double>(6, 0.4));
    h.createPanel();

    QPushButton* button = h.homeButton();
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(QMetaObject::invokeMethod(h.panel.get(), "onResetHomeClicked",
                                          Qt::DirectConnection));
    const std::string status = h.panel->lastStatusText().toStdString();
    EXPECT_NE(status.find("维度"), std::string::npos) << status;
    EXPECT_NE(status.find("未写回"), std::string::npos) << status;
    EXPECT_EQ(h.sessionPose.jointConfiguration(), std::vector<double>(6, 0.4));
}
