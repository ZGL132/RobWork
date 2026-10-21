/**
 * @file   TrajectoryPanelGuiTest.cpp
 * @brief  轨迹工作流面板 Widgets 级交互回归（TRJ-UI-01/02）。
 *
 * 这里验证真实 Qt 控件树的首屏投影和状态门控；规划计算仍由计算库测试
 * 覆盖，GUI 用例只在 UI 线程调用服务缝，不启动第二个规划线程。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <limits>
#include <stdexcept>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>

#include "../plugin/TrajPanelModule.hpp"
#include "../plugin/TrajectoryPanelWidget.hpp"

using namespace sdurws::ird::trajectory;

namespace {

class TrajectoryPanelGuiTest : public ::testing::Test {
protected:
    /** @brief 为每项真实控件回归登记需求，保证 XML 与逐用例报告均可追溯。 */
    void SetUp() override
    {
        IRD_TEST_INFO(std::vector<std::string>{"TRJ-06","TRJ-07","UX-10"},std::vector<std::string>{"AT-04"});
    }
};

TEST_F(TrajectoryPanelGuiTest, CurvesRefreshOnConstruction_WP16T12_ACC1)
{
    TrajPanelModule module;
    int calls = 0;
    module.services.curvesSource = [&calls](TrajectoryCurveChannel) {
        ++calls;
        return std::vector<TrajectoryCurveSeries>{{0, {0.0, 1.0}, {0.0, 1.0}},
                                                    {1, {0.0, 1.0}, {1.0, 2.0}}};
    };
    std::unique_ptr<QWidget> panel(module.createPanel());
    ASSERT_NE(panel, nullptr);
    EXPECT_GT(calls, 0);
    const auto combos = panel->findChildren<QComboBox*>();
    ASSERT_GE(combos.size(), 1);
    EXPECT_EQ(combos.front()->count(), 2);
}

TEST_F(TrajectoryPanelGuiTest, AvailabilityDisablesCommandButtons_WP16T12_ACC1)
{
    TrajPanelModule module;
    module.services.commandAvailability = [](const std::string&) { return false; };
    std::unique_ptr<QWidget> panel(module.createPanel());
    ASSERT_NE(panel, nullptr);
    const auto buttons = panel->findChildren<QPushButton*>();
    ASSERT_GE(buttons.size(), 7);
    for (QPushButton* button : buttons) {
        if (button->text() == QStringLiteral("播放定位")) {
            continue;
        }
        EXPECT_FALSE(button->isEnabled());
    }
}

/** @brief TRJ-07：无归档数据时回放入口必须禁用，并在首屏解释原因。 */
TEST_F(TrajectoryPanelGuiTest, MissingPlaybackIsDisabledAndExplained_TRJ07)
{
    TrajPanelModule module;
    std::unique_ptr<QWidget> panel(module.createPanel());
    const auto sliders = panel->findChildren<QSlider*>();
    ASSERT_EQ(sliders.size(), 1);
    EXPECT_FALSE(sliders.front()->isEnabled());
    bool explained = false;
    for (auto* label : panel->findChildren<QLabel*>()) {
        explained |= label->text().contains(QStringLiteral("轨迹数据未装配"));
    }
    EXPECT_TRUE(explained);
}

/** @brief UX-10：空提交出口的反馈不得被同一次会话刷新擦掉。 */
TEST_F(TrajectoryPanelGuiTest, MissingCommandOutletFeedbackPersists_TRJ06)
{
    TrajPanelModule module;
    std::unique_ptr<QWidget> panel(module.createPanel());
    panel->findChildren<QPushButton*>().front()->click();
    const auto lists = panel->findChildren<QPlainTextEdit*>();
    ASSERT_EQ(lists.size(), 1);
    EXPECT_TRUE(lists.front()->toPlainText().contains(QStringLiteral("命令出口未装配")));
    module.refreshFromSession();
    EXPECT_TRUE(lists.front()->toPlainText().contains(QStringLiteral("命令出口未装配")));
}

/** @brief TRJ-07：坏归档数据不得污染三维，异常不得越出 Qt 信号槽。 */
TEST_F(TrajectoryPanelGuiTest, InvalidPlaybackNeverReachesPoseSink_TRJ07)
{
    TrajPanelModule module;
    int writes = 0;
    module.services.durationSource = [] { return 1.0; };
    module.services.playbackSource = [](double t) {
        TrajPlaybackSample frame;
        frame.tS = t;
        frame.q = {std::numeric_limits<double>::quiet_NaN()};
        frame.qd = {0.0}; frame.qdd = {0.0};
        return frame;
    };
    module.services.sessionPoseSink = [&](const auto&) { ++writes; };
    std::unique_ptr<QWidget> panel(module.createPanel());
    panel->findChildren<QSlider*>().front()->setValue(500);
    EXPECT_EQ(writes, 0);
    module.services.playbackSource = [](double) -> TrajPlaybackSample {
        throw std::runtime_error("archive unavailable");
    };
    EXPECT_NO_THROW(panel->findChildren<QSlider*>().front()->setValue(600));
    EXPECT_EQ(writes, 0);
}

/** @brief TRJ-07：连续播放推进时刻，暂停保持姿态，复位回起点。 */
TEST_F(TrajectoryPanelGuiTest, PlaybackAdvancesPausesAndResets_TRJ07)
{
    TrajPanelModule module;
    double lastTime = -1.0;
    module.services.durationSource = [] { return 1.0; };
    module.services.playbackSource = [](double t) {
        TrajPlaybackSample frame;
        frame.tS=t; frame.q={t}; frame.qd={1.0}; frame.qdd={0.0};
        return frame;
    };
    module.services.sessionPoseSink = [&](const auto& frame) { lastTime=frame.tS; };
    std::unique_ptr<QWidget> panel(module.createPanel());
    panel->resize(680, 720); panel->show(); QApplication::processEvents();
    auto* play=panel->findChild<QPushButton*>(QStringLiteral("trajectory_play"));
    auto* reset=panel->findChild<QPushButton*>(QStringLiteral("trajectory_reset"));
    ASSERT_NE(play, nullptr); ASSERT_NE(reset, nullptr);
    play->click();
    QEventLoop loop;
    QTimer::singleShot(120, &loop, &QEventLoop::quit); loop.exec();
    EXPECT_GT(lastTime, 0.0);
    EXPECT_EQ(play->text(), QStringLiteral("暂停"));
    play->click();
    const double pausedTime=lastTime;
    QTimer::singleShot(80, &loop, &QEventLoop::quit); loop.exec();
    EXPECT_EQ(lastTime, pausedTime);
    reset->click(); EXPECT_DOUBLE_EQ(lastTime, 0.0);
    auto* time=panel->findChild<QDoubleSpinBox*>(QStringLiteral("trajectory_time"));
    ASSERT_NE(time, nullptr);
    time->setValue(0.75);
    EXPECT_NEAR(lastTime, 0.75, 0.001);
    // 真窗口截图是控件层证据；不冒充正式宿主三维验收。
    const QString out=qEnvironmentVariable("IRD_TRAJECTORY_GUI_EVIDENCE_OUT");
    if(!out.isEmpty()){EXPECT_TRUE(panel->grab().save(out+"/playback-controls.png"));}
}

/** @brief TRJ-07：曲线随会话刷新更新，源移除后清空旧关节，防陈旧结果。 */
TEST_F(TrajectoryPanelGuiTest, SessionRefreshClearsStaleCurves_TRJ07)
{
    TrajPanelModule module;
    module.services.curvesSource=[](auto) {
        return std::vector<TrajectoryCurveSeries>{{0,{0.0,1.0},{0.0,1.0}}};
    };
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* joint=panel->findChildren<QComboBox*>().front();
    ASSERT_EQ(joint->count(),1);
    module.services.curvesSource={}; module.refreshFromSession();
    EXPECT_EQ(joint->count(),0);
    EXPECT_FALSE(joint->isEnabled());
}

/** @brief UX-10/TRJ-06：缺定位缝时，失败段说明必须直接告知定位不可用。 */
TEST_F(TrajectoryPanelGuiTest, FailureWithoutLocatorExplainsDisabledAction_TRJ06)
{
    TrajPanelModule module; module.session.failedSegmentIndex=3;
    module.session.failureReasonToken="TRJ-NO-PATH";
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* locate=panel->findChild<QPushButton*>(QStringLiteral("trajectory_failure_locate"));
    ASSERT_NE(locate,nullptr); EXPECT_FALSE(locate->isEnabled());
    bool explained=false;
    for(auto* label:panel->findChildren<QLabel*>()) {
        explained |= label->text().contains(QStringLiteral("定位未装配"));
    }
    EXPECT_TRUE(explained);
}

/** @brief TRJ-07/AT-04：项目/修订纪元改变后停播，旧帧不得写入新会话。 */
TEST_F(TrajectoryPanelGuiTest, SessionEpochChangeStopsOldPlayback_TRJ07)
{
    TrajPanelModule module;
    int writes = 0;
    module.services.durationSource = [] { return 1.0; };
    module.services.playbackSource = [](double t) {
        TrajPlaybackSample frame;
        frame.tS = t; frame.q = {t}; frame.qd = {1.0}; frame.qdd = {0.0};
        return frame;
    };
    module.services.sessionPoseSink = [&](const auto&) { ++writes; };
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* play = panel->findChild<QPushButton*>("trajectory_play");
    auto* time = panel->findChild<QDoubleSpinBox*>("trajectory_time");
    ASSERT_NE(play, nullptr); ASSERT_NE(time, nullptr);
    time->setValue(0.6);
    play->click();
    ++module.session.epoch;
    module.refreshFromSession();
    const int writesAtSwitch = writes;
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit); loop.exec();
    EXPECT_EQ(writes, writesAtSwitch) << "切换后不能继续输出旧归档帧";
    EXPECT_EQ(play->text(), QStringLiteral("播放"));
    EXPECT_DOUBLE_EQ(time->value(), 0.0);
}

/** @brief TRJ-06：三维环境失效应显示定位失败，不得把异常抛出 Qt 槽。 */
TEST_F(TrajectoryPanelGuiTest, FailureLocatorExceptionIsVisible_TRJ06)
{
    TrajPanelModule module;
    module.session.failedSegmentIndex = 2;
    module.services.failureLocate = [](std::uint32_t) -> bool {
        throw std::runtime_error("view unavailable");
    };
    std::unique_ptr<QWidget> panel(module.createPanel());
    auto* locate = panel->findChild<QPushButton*>("trajectory_failure_locate");
    ASSERT_NE(locate, nullptr);
    EXPECT_NO_THROW(locate->click());
    bool explained = false;
    for (auto* label : panel->findChildren<QLabel*>()) {
        explained |= label->text().contains(QStringLiteral("定位失败"));
    }
    EXPECT_TRUE(explained);
}

}  // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
