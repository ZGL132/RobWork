/**
 * @file KinematicsAcceptanceGuiTest.cpp
 * @brief WP-15-T20 对抗式窗口回归：实际控件事件、真实求解及失效状态。
 *
 * 设计依据：KIN-02/09/12、UX-03/05、AT-03/27。旧测试分别设置单位后再
 * 填值，未覆盖“用户已填目标，再切单位”；旧采样测试仅运行关节随机，
 * 未覆盖默认区域盒运行。此处使用真实 FK/IK/采样设施，避免替身给出假绿。
 * 生命周期：夹具拥有模型、会话与控件，控件先销毁；全部只在 UI 线程运行。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/kinematics/Fk.hpp>
#include <sdurws/ird/kinematics/Ik.hpp>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <cmath>
#include <memory>
#include "KinFkFixture.hpp"
#include "plugin/KinematicsPanelWidget.hpp"

using namespace sdurws::ird;
using namespace sdurws::ird::kinematics;
using namespace sdurws::ird::kinematics::testfixture;

namespace {
/**
 * @brief 拥有窗口与真实算法的两连杆夹具；线程不安全，仅测试 UI 线程使用。
 *
 * 模型仅用于独立几何金标（L1=1.1 m、L2=0.7 m），不冒充正式宿主真实网格。
 */
struct AcceptanceWindow {
    TestView view{twoLinkModel()};
    IkSolver solver;
    FkEvaluator fk;
    KinSessionPose pose;
    KinPanelServices services;
    KinModuleSessionState state;
    std::unique_ptr<KinematicsPanelWidget> panel;

    /** @brief 装配真求解器，配置足够的小预算；构造不产生项目写入。 */
    AcceptanceWindow(std::function<void(std::function<void()>,
        std::function<void()>, const QObject*)> workspaceAsync = {},
        std::function<bool(const ui::View3DPreviewUpdate&,
            const std::vector<core::ObjectId>&)> preview = {})
    {
        services.modelView = &view;
        services.ikSolver = &solver;
        services.fkEvaluator = &fk;
        services.sessionPose = &pose;
        services.workspaceAsync = std::move(workspaceAsync);
        services.previewSink = std::move(preview);
        state.savedConfig.seed = 7;
        state.savedConfig.regionBudget.seed = 7;
        state.savedConfig.initialValuesCount = 4;
        state.savedConfig.iterationLimit = 256;
        panel = std::make_unique<KinematicsPanelWidget>(services, state);
        panel->resize(1400, 1000);
        panel->show();
        QApplication::processEvents();
    }

    /**
     * @brief 按稳定对象名查编辑框，仅UI线程调用。
     * @param name [in] UTF-8对象名，无物理单位；不可为空指针。
     * @return 面板持有的借用指针，缺件返回空，不转移所有权。
     */
    QLineEdit* edit(const char* name) const
    { return panel->findChild<QLineEdit*>(QString::fromUtf8(name)); }

    /**
     * @brief 按工程用语定位按钮，仅UI线程调用。
     * @param text [in] 按钮完整文案，无单位；空串仅匹配空标题。
     * @return 面板持有的借用指针，缺件返回空，不转移所有权。
     */
    QPushButton* button(const QString& text) const
    {
        for (auto* candidate : panel->findChildren<QPushButton*>()) {
            if (candidate->text() == text) { return candidate; }
        }
        return nullptr;
    }

    /** @brief 将位置目标填为已知 q=(0.3,-0.3) 的独立解析结果，长度单位 m。 */
    void fillReachableTarget()
    {
        edit("target-x")->setText(QString::number(1.1 * std::cos(0.3) + 0.7, 'g', 17));
        edit("target-y")->setText(QString::number(1.1 * std::sin(0.3), 'g', 17));
        edit("target-z")->setText("0");
        edit("target-roll")->setText("0");
        edit("target-pitch")->setText("0");
        edit("target-yaw")->setText("0");
    }

    /**
     * @brief 对真实按钮发送鼠标按下/释放事件，走事件分发和信号槽完整路径。
     * @param target [in,out] 窗口持有的按钮；不可为空，测试前置断言负责检查。
     */
    void click(QPushButton* target)
    {
        const QPointF local(target->rect().center());
        const QPointF global(target->mapToGlobal(local.toPoint()));
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(target, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &release);
        QApplication::processEvents();
    }

    /**
     * @brief 保存当前窗口像素，仅UI线程调用；写失败即测试失败。
     * @param name [in] 无扩展名的固定测试文件名，无物理单位。
     * 未设置证据目录时不写文件；截图只证明当前面板，不证明宿主真实网格。
     */
    void capture(const QString& name)
    {
        const QString out = qEnvironmentVariable("IRD_KIN_GUI_EVIDENCE_OUT");
        if (!out.isEmpty()) {
            ASSERT_TRUE(QDir().mkpath(out));
            ASSERT_TRUE(panel->grab().save(out + '/' + name + ".png"));
        }
    }
};
} // namespace

/// K07/AT-27：用户已输入物理目标，再切制式，真实IK仍应解原目标。
TEST(KinematicsAcceptance, FilledTargetSurvivesUnitSwitch_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-12"},
                  std::vector<std::string>{"AT-27"});
    AcceptanceWindow h;
    h.fillReachableTarget();
    h.panel->setDisplayUnits(DisplayUnitProjection::tryFind("mm", "deg"));
    h.capture("filled-target-mm");
    ASSERT_NE(h.button(QStringLiteral("求解 IK")), nullptr);
    h.click(h.button(QStringLiteral("求解 IK")));
    ASSERT_NE(h.state.lastSessionSolutionSet, nullptr);
    ASSERT_FALSE(h.state.lastSessionSolutionSet->solutions.empty())
        << h.panel->lastStatusText().toStdString();
    // 断言实际解的FK等于独立解析的原目标，不能只检查标签或格式化后的数值。
    const auto& q = h.state.lastSessionSolutionSet->solutions.front().q;
    const auto result = h.fk.evaluate(h.view, TcpRef{
        h.view.model().tools().front().objectId, ""}, q);
    ASSERT_TRUE(result.ok());
    EXPECT_NEAR(result.get().tcpInBase.P()[0], 1.1 * std::cos(0.3) + 0.7, 1e-6);
    EXPECT_NEAR(result.get().tcpInBase.P()[1], 1.1 * std::sin(0.3), 1e-6);
}

/// 求解完成后编辑目标必须立即使旧结果过期，避免结果页和导出入口继续
/// 暗示结果对应当前编辑目标。
TEST(KinematicsAcceptance, EditingTargetExpiresExistingResult_WP15T20)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "NFR-REL-03"},
                  std::vector<std::string>{});
    AcceptanceWindow h;
    h.fillReachableTarget();
    h.click(h.button(QStringLiteral("求解 IK")));
    ASSERT_NE(h.state.lastSessionSolutionSet, nullptr);

    QLabel* solveStatus =
        h.panel->findChild<QLabel*>(QStringLiteral("kin-solve-status"));
    ASSERT_NE(solveStatus, nullptr);
    EXPECT_TRUE(solveStatus->text().contains(QStringLiteral("已有当前会话结果")))
        << solveStatus->text().toStdString();

    h.edit("target-x")->setText(QStringLiteral("0.2"));
    EXPECT_TRUE(solveStatus->text().contains(QStringLiteral("结果已过期")))
        << solveStatus->text().toStdString();
}

/// KIN-08：复算成功应更新当前目标基线，等价文本与真实目标变化分别判定。
TEST(KinematicsAcceptance, RecomputeRefreshesTargetBaseline_WP15T20)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-02", "KIN-08"},
                  std::vector<std::string>{"AT-27"});
    AcceptanceWindow h;
    h.state.snapshotId = core::ContentIdentity::tryFromCanonical(
        "cid-1111111111111111111111111111111111111111111111111111111111111111").value();
    h.fillReachableTarget();
    h.click(h.button(QStringLiteral("求解 IK")));
    ASSERT_NE(h.state.lastSessionSolutionSet, nullptr);
    const auto oldResult = h.state.lastSessionSolutionSet;
    auto* status = h.panel->findChild<QLabel*>(QStringLiteral("kin-solve-status"));
    ASSERT_NE(status, nullptr);
    h.edit("target-x")->setText("0.2");
    ASSERT_TRUE(status->text().contains(QStringLiteral("过期")));
    auto* tabs = h.panel->findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(3);
    auto* recompute = h.button(QStringLiteral("复算（当前快照）"));
    ASSERT_NE(recompute, nullptr);
    h.click(recompute);
    ASSERT_NE(h.state.lastSessionSolutionSet, oldResult)
        << h.panel->lastStatusText().toStdString();
    EXPECT_FALSE(status->text().contains(QStringLiteral("过期")));
    // KIN-08：同一物理目标的不同文本不失效；换回旧目标则应过期。
    h.edit("target-x")->setText("0.2000");
    EXPECT_FALSE(status->text().contains(QStringLiteral("过期")));
    h.fillReachableTarget();
    EXPECT_TRUE(status->text().contains(QStringLiteral("过期")));
}

/// KIN-09：执行端口暂停期间仍可操作取消；实际采样核消费取消并留下partial。
TEST(KinematicsAcceptance, WorkspaceAsyncCancelAndLateResult_KIN09)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "NFR-PERF-01"},
                  std::vector<std::string>{"AT-25"});
    std::function<void()> job, done;
    AcceptanceWindow h([&](auto work, auto completion, const QObject*) {
        job = std::move(work); done = std::move(completion);
    });
    h.panel->findChild<QTabWidget*>()->setCurrentIndex(3);
    h.edit("ws-region-random")->setText("2");
    auto* run = h.button(QStringLiteral("运行采样"));
    h.click(run);
    ASSERT_TRUE(static_cast<bool>(job)) << "必须交给宿主执行，不能同步冻结界面";
    EXPECT_EQ(h.state.lastWorkspaceRun, nullptr);
    EXPECT_FALSE(run->isEnabled());
    auto* cancel = h.panel->findChild<QPushButton*>("ws-cancel");
    ASSERT_NE(cancel, nullptr);
    EXPECT_TRUE(cancel->isEnabled());
    h.click(cancel);
    job(); done();
    ASSERT_NE(h.state.lastWorkspaceRun, nullptr);
    EXPECT_TRUE(h.state.lastWorkspaceRun->cancelled);
    EXPECT_TRUE(run->isEnabled());
    const auto original = h.state.lastWorkspaceRun;
    h.click(run);
    ++h.state.epoch; // 会话换绑后的旧回执不得入账。
    job(); done();
    EXPECT_EQ(h.state.lastWorkspaceRun, original);
    EXPECT_TRUE(h.panel->lastStatusText().contains(QStringLiteral("过期")));
    h.capture(QStringLiteral("workspace-cancel-late"));
}

/// KIN-09：真实FK采样完成后显示域上报的100%，销毁后作业仍可安全退出。
TEST(KinematicsAcceptance, WorkspaceProgressAndDestroyedPanel_KIN09)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09", "NFR-REL-03"},
                  std::vector<std::string>{"AT-25"});
    std::function<void()> job, done;
    AcceptanceWindow h([&](auto work, auto completion, const QObject*) {
        job = std::move(work); done = std::move(completion);
    });
    h.panel->findChild<QTabWidget*>()->setCurrentIndex(3);
    h.panel->findChild<QComboBox*>("ws-mode-box")->setCurrentIndex(1);
    h.edit("ws-joint-random")->setText("3");
    h.click(h.button(QStringLiteral("运行采样")));
    ASSERT_TRUE(static_cast<bool>(job));
    job(); done();
    ASSERT_NE(h.state.lastWorkspaceRun, nullptr);
    EXPECT_EQ(h.state.lastWorkspaceRun->results.size(), 3U);
    auto* progress = h.panel->findChild<QProgressBar*>("ws-progress");
    ASSERT_NE(progress, nullptr);
    EXPECT_EQ(progress->value(), 100);
    const auto original = h.state.lastWorkspaceRun;
    h.click(h.button(QStringLiteral("运行采样")));
    h.panel.reset();
    // 销毁请求取消；后台只用冻结值，完成前不碰宿主会话。
    EXPECT_NO_THROW(job());
    EXPECT_EQ(h.state.lastWorkspaceRun, original);
    // context析构后宿主按契约弃置done，测试不越权调用已销毁控件回调。
    done = {};
}

/// KIN-07/09：真实采样点投放既有三维值端口，画布双击沿唯一回写路径。
TEST(KinematicsAcceptance, WorkspacePreviewAndCanvasPick_KIN07_KIN09)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-07", "KIN-09", "KIN-06"},
                  std::vector<std::string>{"AT-25", "AT-04"});
    ui::View3DPreviewUpdate preview;
    AcceptanceWindow h({}, [&](const auto& value, const auto&) { preview=value; return true; });
    h.panel->findChild<QTabWidget*>()->setCurrentIndex(3);
    h.panel->findChild<QComboBox*>("ws-mode-box")->setCurrentIndex(1);
    h.edit("ws-joint-random")->setText("1");
    h.click(h.button(QStringLiteral("运行采样")));
    ASSERT_TRUE(preview.sampleGrid.has_value());
    ASSERT_EQ(preview.sampleGrid->samples.size(), 1U);
    ASSERT_NE(h.state.lastWorkspaceRun, nullptr);
    const auto& sample = h.state.lastWorkspaceRun->results.front();
    EXPECT_EQ(preview.sampleGrid->samples.front(), sample.tcpPosition);
    auto* canvas=h.panel->findChild<QLabel*>("ws-projection-canvas");
    ASSERT_NE(canvas, nullptr);
    // 实际像素查找绿点（单样本FK可达），避免测试复刻像素映射实现。
    const auto image=canvas->pixmap().toImage();
    QPoint pixel(-1,-1);
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const auto c=image.pixelColor(x,y);
        if(c.green()>80 && c.red()<30 && c.blue()<30) { pixel=QPoint(x,y); }
    }
    ASSERT_GE(pixel.x(),0);
    const QPointF local=pixel+QPoint((canvas->width()-image.width())/2,
                                    (canvas->height()-image.height())/2);
    QMouseEvent press(QEvent::MouseButtonPress,local,canvas->mapToGlobal(local.toPoint()),
                      Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&press);
    EXPECT_EQ(h.edit("ws-sample-index")->text(),QString::number(sample.sampleIndex));
    EXPECT_EQ(preview.sampleGrid->highlightedSampleIndex,sample.sampleIndex);
    QMouseEvent dbl(QEvent::MouseButtonDblClick,local,canvas->mapToGlobal(local.toPoint()),
                    Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(canvas,&dbl);
    EXPECT_TRUE(h.pose.isSet());
    h.capture(QStringLiteral("workspace-picked"));
}

/// K40：默认区域盒模式必须真正生成样本，合法字段不能被统一误报为非法。
TEST(KinematicsAcceptance, CartesianBoxRunsThroughVisibleButton_KIN09)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-09"},
                  std::vector<std::string>{"AT-25"});
    AcceptanceWindow h;
    auto* tabs = h.panel->findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(3);
    QApplication::processEvents();
    ASSERT_NE(h.edit("ws-region-random"), nullptr);
    h.edit("ws-region-random")->setText("2");
    ASSERT_NE(h.button(QStringLiteral("运行采样")), nullptr);
    h.click(h.button(QStringLiteral("运行采样")));
    h.capture("cartesian-box-run");
    ASSERT_NE(h.state.lastWorkspaceRun, nullptr)
        << h.panel->lastStatusText().toStdString();
}

/// K07/UX-05：非法原文在切单位时必须保留，不能变成0或丢失错误定位依据。
TEST(KinematicsAcceptance, InvalidTargetTextPreservedOnUnitSwitch_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "UX-05"},
                  std::vector<std::string>{"AT-27"});
    AcceptanceWindow h;
    h.fillReachableTarget();
    h.edit("target-y")->setText(QStringLiteral("invalid-value"));
    h.panel->setDisplayUnits(DisplayUnitProjection::tryFind("mm", "deg"));
    EXPECT_EQ(h.edit("target-y")->text(), QStringLiteral("invalid-value"));
    EXPECT_NEAR(h.edit("target-x")->text().toDouble(),
                (1.1 * std::cos(0.3) + 0.7) * 1000.0, 1e-9);
    h.click(h.button(QStringLiteral("求解 IK")));
    EXPECT_EQ(h.state.lastSessionSolutionSet, nullptr);
}

/// K07/K22：姿态角和区域长度同样保物理量，往返不能用显示6位精度量化。
TEST(KinematicsAcceptance, AllPhysicalEditorsRoundtripUnits_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "KIN-09"},
                  std::vector<std::string>{"AT-27"});
    AcceptanceWindow h;
    h.fillReachableTarget();
    h.edit("target-roll")->setText("0.123456789012345");
    h.edit("ws-region-cx")->setText("0.4321987654321");
    h.edit("ws-region-sx")->setText("0.1234567898765");
    for (int round = 0; round < 10; ++round) {
        h.panel->setDisplayUnits(DisplayUnitProjection::tryFind("mm", "deg"));
        EXPECT_NEAR(h.edit("target-roll")->text().toDouble(),
                    0.123456789012345 * 180.0 / std::acos(-1.0), 1e-12);
        EXPECT_NEAR(h.edit("ws-region-cx")->text().toDouble(), 432.1987654321, 1e-10);
        h.panel->setDisplayUnits(std::nullopt);
    }
    EXPECT_NEAR(h.edit("target-roll")->text().toDouble(), 0.123456789012345, 1e-14);
    EXPECT_NEAR(h.edit("ws-region-sx")->text().toDouble(), 0.1234567898765, 1e-14);
}

/// F02失败原子性：换算溢出时不能半换单位/半改文本，更不能抛异常穿过UI。
TEST(KinematicsAcceptance, UnitProjectionOverflowPreservesAllInputs_AT27)
{
    IRD_TEST_INFO(std::vector<std::string>{"KIN-12", "NFR-COR-03"},
                  std::vector<std::string>{"AT-27"});
    AcceptanceWindow h;
    h.fillReachableTarget();
    const QString originalX = h.edit("target-x")->text();
    h.edit("target-y")->setText("1.7e308");
    EXPECT_NO_THROW(h.panel->setDisplayUnits(DisplayUnitProjection::tryFind("mm", "deg")));
    EXPECT_FALSE(h.state.displayUnits.has_value());
    EXPECT_EQ(h.edit("target-x")->text(), originalX);
    EXPECT_EQ(h.edit("target-y")->text(), QStringLiteral("1.7e308"));
}

/// K66：最重结果页须在普通工作台尺寸内滚动，底部操作仍能实际到达。
TEST(KinematicsAcceptance, ResultsPageFitsAndBottomActionsRemainReachable_UX09)
{
    IRD_TEST_INFO(std::vector<std::string>{"UX-09", "UX-05"},
                  std::vector<std::string>{});
    AcceptanceWindow h;
    auto* tabs = h.panel->findChild<QTabWidget*>();
    ASSERT_NE(tabs, nullptr);
    tabs->setCurrentIndex(3);
    h.panel->resize(960, 720);
    QApplication::processEvents();
    EXPECT_LE(h.panel->height(), 720);
    EXPECT_LE(h.panel->width(), 960);
    auto* scroll = qobject_cast<QScrollArea*>(tabs->currentWidget());
    ASSERT_NE(scroll, nullptr) << "长页须可滚动，不能扩大主窗口最低尺寸";
    auto* action = h.button(QStringLiteral("设默认 TCP"));
    ASSERT_NE(action, nullptr);
    scroll->ensureWidgetVisible(action);
    QApplication::processEvents();
    const QRect actionRect(action->mapTo(scroll->viewport(), QPoint()), action->size());
    EXPECT_TRUE(scroll->viewport()->rect().intersects(actionRect));
    h.capture("results-small-window-bottom");
}
