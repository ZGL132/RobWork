/**
 * @file DynamicsPanelGuiTest.cpp
 * @brief DYN-08/UX-02 的真实 Windows 控件验收，覆盖量纲、峰值与回放反馈。
 *
 * 输入是确定性序列，曲线、回放和峰值使用真实投影/统计器；只在三维写点
 * 使用端口观测器。本测试不声称验证正式宿主场景和生产归档链。
 */
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <gtest/gtest.h>
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>
#include <sdurws/ird/testkit/gtest/AssertMacros.hpp>
#include <sdurws/ird/ui/UiText.hpp>
#include <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>
#include <memory>
#include "DynPanelModule.hpp"
#include "DynamicsPanelWidget.hpp"
#include "TwoLinkFixture.hpp"

namespace sdurws::ird::dynamics {
namespace {
/** @brief 每例独立的 UI 线程夹具；序列单位为 SI，生命周期覆盖控件。 */
class DynWindow : public ::testing::Test {
protected:
    DynamicsPluginAssembly bundle = createDynamicsPluginAssembly();
    DynamicsSeries series{};
    ReplayData replay{};
    std::unique_ptr<DynamicsPanelWidget> panel;
    int writes = 0; ///< 仅观测三维端口调用，不模拟数值算法。
    bool poseAccepted = true;

    /** @brief 构造两时刻两关节数据；运动、力、功率的符号刻意不同。 */
    void SetUp() override
    {
        IRD_TEST_INFO(std::vector<std::string>{"DYN-08", "UX-02"},
                      std::vector<std::string>{});
        using namespace testfixture;
        series.conditionId = idFrom<core::ObjectId>("dyn-gui-condition");
        series.contentIdentity.bytes = digestOf("dyn-gui-series");
        series.validity.completeness = DynamicsValidity::Completeness::Complete;
        for (int t = 0; t < 2; ++t) {
            for (std::uint32_t j = 0; j < 2; ++j) {
                DynamicsSample row{};
                row.t = t;
                row.conditionId = series.conditionId;
                row.jointIndex = j;
                row.jointObjectId = idFrom<core::ObjectId>("joint-" + std::to_string(j));
                row.jointType = j == 0 ? DynJointType::Revolute : DynJointType::Prismatic;
                row.q = t * 0.2;
                row.qd = -2.0 - t;
                row.qdd = -4.0 - t;
                row.tauTotal = -6.0 - t;
                row.mechanicalPower = row.tauTotal * row.qd;
                row.numericState = SampleNumericState::Ok;
                series.samples.push_back(row);
            }
        }
        replay = DynamicsReplayProjector{}.buildReplayData(series);
        bundle.bindTextResolver([](const std::string& key) { return ui::resolveText(key); });
        bundle.bindCurveSource([this] { return DynamicsCurveProjector{}.projectCurves(series); });
        bundle.bindReplaySource([this] { return replay; });
        const auto peaks = DynamicsEnvelopeCalculator{}.computePeaks(series);
        bundle.bindPeakLocate([this, peaks](std::uint32_t j, int token)
            -> std::optional<std::tuple<double, double, std::uint32_t>> {
            const auto peak = DynamicsPeakLocator{}.locate(series, peaks, j, token);
            if (!peak) { return std::nullopt; }
            return std::make_tuple(peak->tPeakS, peak->value, peak->segmentIndex);
        });
        bundle.bindReplayPoseApply([this](const std::vector<ReplayJointState>&) {
            ++writes;
            return poseAccepted;
        });
        bundle.bindCommandAvailability([](const std::string&) { return true; });
        bundle.bindCommandSubmit([](const std::string&) {});
        panel = std::make_unique<DynamicsPanelWidget>(*bundle.module());
        panel->resize(920, 620);
        panel->show();
        QApplication::processEvents();
    }

    /** @brief 按稳定对象名取控件；缺席即测试失败，不空转跳过。 */
    template<class T> T* widget(const char* name)
    {
        auto* value = panel->findChild<T*>(name);
        EXPECT_NE(value, nullptr) << name;
        return value;
    }
};

TEST_F(DynWindow, EmptyResultDisablesDataActions_DYN08)
{
    series.samples.clear();
    replay.frames.clear();
    panel->refreshFromSession(); // 本例直接持有面板，门面工厂未保存面板弱引用。
    auto* peak = widget<QPushButton>("dyn_locate_peak");
    auto* play = widget<QPushButton>("dyn_replay");
    ASSERT_NE(peak, nullptr); ASSERT_NE(play, nullptr);
    EXPECT_FALSE(peak->isEnabled());
    EXPECT_FALSE(play->isEnabled());
    EXPECT_EQ(writes, 0) << "无数据不能更新三维";
}

TEST_F(DynWindow, PeakSelectionSynchronizesChannelTimeAndPose_DYN08)
{
    auto* peak = widget<QComboBox>("dyn_peak_channel");
    auto* channel = widget<QComboBox>("dyn_channel");
    auto* locate = widget<QPushButton>("dyn_locate_peak");
    auto* time = widget<QDoubleSpinBox>("dyn_replay_time");
    ASSERT_NE(peak, nullptr); ASSERT_NE(channel, nullptr);
    ASSERT_NE(locate, nullptr); ASSERT_NE(time, nullptr);
    peak->setCurrentIndex(peak->findData(2)); // |速度|峰，原曲线值为负。
    EXPECT_EQ(channel->currentData().toInt(), int(DynChannelId::Velocity));
    locate->click();
    EXPECT_DOUBLE_EQ(time->value(), 1.0);
    EXPECT_EQ(writes, 1);
    auto* chart = dynamic_cast<DynCurveChartView*>(panel->findChild<QWidget*>("dyn_chart"));
    ASSERT_NE(chart, nullptr);
    EXPECT_DOUBLE_EQ(chart->peakValue(), -3.0) << "绝对峰值的标记应落在原负速度曲线上";
    auto* reading = widget<QLabel>("dyn_replay_reading"); ASSERT_NE(reading, nullptr);
    EXPECT_TRUE(reading->text().contains(QString::fromUtf8("已同步")));
    // 真实 Windows 窗口留痕；截图仅作为呈现证据，数值由上面断言核对。
    panel->findChild<QTabWidget*>()->setCurrentIndex(1);
    QApplication::processEvents();
    EXPECT_TRUE(panel->grab().save("dynamics-peak-window.png"));
}

TEST_F(DynWindow, PeakReportsPoseRejection_DYN08)
{
    poseAccepted = false;
    auto* locate = widget<QPushButton>("dyn_locate_peak"); ASSERT_NE(locate, nullptr);
    locate->click();
    auto* reading = widget<QLabel>("dyn_replay_reading"); ASSERT_NE(reading, nullptr);
    EXPECT_TRUE(reading->text().contains(QString::fromUtf8("保留上次姿态")));
}

TEST_F(DynWindow, PrismaticJointChangesUnitLabels_DYN08)
{
    auto* joint = widget<QComboBox>("dyn_joint");
    auto* channel = widget<QComboBox>("dyn_channel");
    ASSERT_NE(joint, nullptr); ASSERT_NE(channel, nullptr);
    joint->setCurrentIndex(joint->findData(1));
    EXPECT_TRUE(channel->itemText(0).contains(QStringLiteral("m")));
    EXPECT_FALSE(channel->itemText(0).contains(QStringLiteral("rad")));
    EXPECT_TRUE(channel->itemText(3).contains(QStringLiteral("N")));
    EXPECT_FALSE(channel->itemText(3).contains(QString::fromUtf8("N·m")));
}

TEST_F(DynWindow, DifferentResultIdentityRejectsReplay_DYN08)
{
    replay.sourceSeriesId.bytes = testfixture::digestOf("another-series");
    auto* play = widget<QPushButton>("dyn_replay"); ASSERT_NE(play, nullptr);
    play->click();
    EXPECT_EQ(writes, 0) << "跨结果数据不得更新真实姿态";
    auto* reading = widget<QLabel>("dyn_replay_reading"); ASSERT_NE(reading, nullptr);
    EXPECT_TRUE(reading->text().contains(QString::fromUtf8("来源不一致")));
}

TEST_F(DynWindow, ShowCurvesCommandNavigatesToCurves_UX02)
{
    auto* command = widget<QPushButton>("dynamics.show-curves"); ASSERT_NE(command, nullptr);
    command->click();
    EXPECT_EQ(panel->findChild<QTabWidget*>()->currentIndex(), 1);
}

TEST_F(DynWindow, EveryPeakTokenMarksSignedSample_DYN08)
{
    auto* choice = widget<QComboBox>("dyn_peak_channel");
    auto* locate = widget<QPushButton>("dyn_locate_peak");
    auto* chart = dynamic_cast<DynCurveChartView*>(panel->findChild<QWidget*>("dyn_chart"));
    ASSERT_NE(choice, nullptr); ASSERT_NE(locate, nullptr); ASSERT_NE(chart, nullptr);
    const int channels[] = {3, 3, 1, 2, 4, 4};
    for (int token = 0; token < 6; ++token) {
        choice->setCurrentIndex(choice->findData(token));
        locate->click();
        EXPECT_EQ(int(chart->channel()), channels[token]);
        EXPECT_TRUE(chart->hasPeak());
        const auto sample = DynamicsReplayProjector{}.sampleAt(replay, chart->cursorTimeS());
        ASSERT_TRUE(sample.has_value());
        const auto& joint = sample->joints.front();
        const double signedValue = token < 2 ? joint.generalizedForce : token == 2 ? joint.qd
                                  : token == 3 ? joint.qdd : joint.mechanicalPower;
        EXPECT_DOUBLE_EQ(chart->peakValue(), signedValue);
    }
    EXPECT_EQ(writes, 6);
}

TEST_F(DynWindow, InvalidAndOutOfRangeReplayPreservesLastPose_DYN08)
{
    auto* play = widget<QPushButton>("dyn_replay");
    auto* time = widget<QDoubleSpinBox>("dyn_replay_time");
    ASSERT_NE(play, nullptr); ASSERT_NE(time, nullptr);
    play->click(); EXPECT_EQ(writes, 1);
    time->setValue(2.0); play->click(); EXPECT_EQ(writes, 1);
    replay.frames.front().completeAllJoints = false;
    time->setValue(0.0); play->click(); EXPECT_EQ(writes, 1);
    auto* reading = widget<QLabel>("dyn_replay_reading"); ASSERT_NE(reading, nullptr);
    EXPECT_TRUE(reading->text().contains(QString::fromUtf8("保留上次姿态")));
}

TEST_F(DynWindow, HostAvailabilityDisablesAllEntrances_UX10)
{
    bundle.bindCommandAvailability([](const std::string&) { return false; });
    panel->refreshFromSession();
    for (auto* button : panel->findChildren<QPushButton*>()) {
        EXPECT_FALSE(button->isEnabled()) << button->objectName().toStdString();
    }
}

TEST_F(DynWindow, ChangingJointClearsPreviousPeak_DYN08)
{
    auto* locate = widget<QPushButton>("dyn_locate_peak");
    auto* joint = widget<QComboBox>("dyn_joint");
    auto* chart = dynamic_cast<DynCurveChartView*>(panel->findChild<QWidget*>("dyn_chart"));
    ASSERT_NE(locate, nullptr); ASSERT_NE(joint, nullptr); ASSERT_NE(chart, nullptr);
    locate->click(); ASSERT_TRUE(chart->hasPeak());
    joint->setCurrentIndex(joint->findData(1));
    EXPECT_FALSE(chart->hasPeak()) << "上一关节的峰值不能留在另一关节的图上";
}

/** @brief DYN-08：刷新到另一份结果时，旧结果峰值和回放读数不能残留。 */
TEST_F(DynWindow, ReloadedResultClearsPreviousMarkers_DYN08)
{
    auto* locate = widget<QPushButton>("dyn_locate_peak");
    auto* reading = widget<QLabel>("dyn_replay_reading");
    auto* chart = dynamic_cast<DynCurveChartView*>(panel->findChild<QWidget*>("dyn_chart"));
    ASSERT_NE(locate, nullptr); ASSERT_NE(chart, nullptr); ASSERT_NE(reading, nullptr);
    locate->click(); ASSERT_TRUE(chart->hasPeak()); ASSERT_FALSE(reading->text().isEmpty());
    series.contentIdentity.bytes = testfixture::digestOf("replacement-result");
    replay = DynamicsReplayProjector{}.buildReplayData(series);
    panel->refreshFromSession();
    EXPECT_FALSE(chart->hasPeak()) << "刷新后的标记必须重新定位，不能沿用旧结果";
    EXPECT_FALSE(reading->text().contains(QString::fromUtf8("已同步")))
        << "新结果尚未回放，不能显示上一结果的三维成功反馈";
}

/** @brief DYN-08：合法单时刻曲线仍须显示样本，不能因横轴零跨度丢图。 */
TEST_F(DynWindow, SingleTimestampDrawsVisibleSample_DYN08)
{
    auto* chart = dynamic_cast<DynCurveChartView*>(panel->findChild<QWidget*>("dyn_chart"));
    ASSERT_NE(chart, nullptr);
    panel->findChild<QTabWidget*>()->setCurrentIndex(1);
    chart->setCurve(0, DynJointType::Revolute, {0.0}, {5.0});
    QApplication::processEvents();
    const QImage pixels = chart->grab().toImage().convertToFormat(QImage::Format_RGB32);
    const QRgb sampleColor = chart->palette().highlight().color().rgb();
    int coloredPixels = 0;
    for (int y = 30; y < pixels.height() - 30; ++y) {
        for (int x = 50; x < pixels.width() - 30; ++x) {
            if (pixels.pixel(x, y) == sampleColor) { ++coloredPixels; }
        }
    }
    EXPECT_GT(coloredPixels, 0) << "绘图区必须有真实样本色像素，空轴不能当作曲线通过";
    EXPECT_TRUE(chart->grab().save("dynamics-single-sample-window.png"));
}
} // namespace
} // namespace sdurws::ird::dynamics

/** @brief 独立 QApplication 入口；串行运行、生成 XML 与 JSON 报告。 */
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
