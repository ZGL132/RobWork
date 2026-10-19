/**
 * @file mixed_probe.cpp
 * @brief REQ-03/KIN-04追加验收：真实单移动轴的100点混合结果与会话换绑。
 *
 * 仅复用现有运行时模型构造/编辑器端口夹具，FK、IK、采样、分母和回投
 * 均调用生产实现。该探针不证明正式证据、完整跨项目隔离或真实窗口颜色。
 */
#include "C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/test/KinEvaluationChannelTest.cpp"
#include <sdurws/ird/testkit/gtest/RecordListener.hpp>

/**
 * @brief 用单移动轴真实模型验证三种结局共存时分母守恒（单位m）。
 * 轴仅能沿X在[0,0.6]移动；y=0且X在区间内可达，其余样本或被解析
 * 半径界限排除，或数值搜索未收敛。禁止用脚本求解器指定这些结局。
 */
TEST(RequirementsClosureFollowup, RealMixedHundredAndSnapshotRebind_REQ03_KIN04)
{
    IRD_TEST_INFO((std::vector<std::string>{"REQ-03", "KIN-04", "CON-06"}),
                  (std::vector<std::string>{"AT-03"}));
    ExecutorHarness h;
    auto ws = makeOneRegionOnePlanWorkset();
    auto& region = ws.regions.entries.front();
    // 格心为X=0.05..0.95，Y=0..0.45，Z=0；参考系与基座均为单位变换。
    region.box.center = {0.5, 0.225, 0};
    region.box.size = {1.0, 0.5, 0.02};
    region.positionSampling.counts = {10, 10, 1};
    ws.plans.entries.front().positionSampling = region.positionSampling;
    ScriptedEditor editor(ws);
    h.rebuildExecutorWithEditor(&editor);
    h.view = std::make_shared<FixtureView>(makeModel(
        {prismatic("closure-x", {1, 0, 0}, 0, 0.6)}, trans(0, 0, 0)));
    h.executor->attachView(h.view, h.boundId);
    auto config = makeLegalConfig();
    config.initialStrategy = InitialValueStrategy::ReferenceQ;
    config.initialValuesCount = 1;
    config.iterationLimit = 80;
    h.executor->setConfiguration(config);
    KinChannelBackgroundRequest request;
    request.kind = KinChannelBackgroundKind::RegionCoverage;
    request.snapshotId = h.boundId;
    request.epoch = h.executor->epoch();
    request.configDigest = analysisConfigurationDigest(config);
    const auto ack = h.executor->submit(request);
    ASSERT_TRUE(ack.accepted) << ack.reason;
    h.drain();
    const auto result = h.executor->latestCoverageResult();
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->ok);
    EXPECT_EQ(result->position.planned, 100u);
    EXPECT_EQ(result->orientation.planned, 100u);
    EXPECT_EQ(result->samples.size(), 200u);
    EXPECT_EQ(result->position.reached, 6u);
    EXPECT_GT(result->position.unreachable, 0u);
    EXPECT_GT(result->position.dataInsufficient, 0u);
    EXPECT_EQ(result->position.notRun, 0u);
    EXPECT_TRUE(result->downgraded);  // 数据不足存在时只能报告参考覆盖率。
    EXPECT_FALSE(result->incomplete); // 全部运行完成与工程证据齐备是两种状态。
    EXPECT_EQ(result->position.reached + result->position.unreachable
                  + result->position.dataInsufficient, 100u);
    std::cout << "REAL_100 planned=" << result->position.planned
              << " reached=" << result->position.reached
              << " unreachable=" << result->position.unreachable
              << " insufficient=" << result->position.dataInsufficient << '\n';
    // 重复同一冻结输入，不允许依结果过滤或重采样；逐样本状态/坐标对账。
    h.noteTarget = 2;
    ASSERT_TRUE(h.executor->submit(request).accepted);
    h.drain();
    const auto repeat = h.executor->latestCoverageResult();
    ASSERT_TRUE(repeat.has_value());
    EXPECT_EQ(repeat->position, result->position);
    ASSERT_EQ(repeat->samples.size(), result->samples.size());
    for (std::size_t i = 0; i < result->samples.size(); ++i) {
        EXPECT_EQ(repeat->samples[i].position, result->samples[i].position);
        EXPECT_EQ(repeat->samples[i].state, result->samples[i].state);
    }
    // 这里只证明执行器的快照锚拒绝旧请求；并非真实项目全链隔离验收。
    const auto nextId = idFrom<core::ContentIdentity>("closure-other-snapshot");
    h.executor->attachView(h.view, nextId);
    EXPECT_FALSE(h.executor->latestCoverageResult().has_value());
    EXPECT_FALSE(h.executor->submit(request).accepted);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    sdurws::ird::testkit::installTestRecordListener(argc, argv);
    return RUN_ALL_TESTS();
}
