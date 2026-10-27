/**
 * @file RegistryBoundaryTest.cpp
 * @brief REUSE-T01 登记写边界只读探测，复用原RunRegistryAdmissionTest夹具。
 * 设计依据：execution §9.1 / §10.8，失败调用不应留下半写终结标记。
 * 本文件仅用于隔离审计，不接入产品，不提供第二登记或证据核心。
 */
#include IRD_REGISTRY_EXISTING_TEST

namespace {

TEST_F(RunRegistryAdmissionTest, ReuseBoundaryInvalidCompletedPreservesRegistration)
{
    // Completed 必须走接纳入口；错误调用被拒绝后原运行应仍可合法取消。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    EXPECT_THROW(m_registry.markTerminated(id.run, TerminationCause::Completed), ExecutionError);
    const auto after = m_registry.tryRun(id.run);
    ASSERT_TRUE(after.has_value());
    EXPECT_FALSE(after->terminationCause.has_value());
    EXPECT_EQ(after->currentState, core::TaskState::Running);
    EXPECT_NO_THROW(m_registry.markTerminated(id.run, TerminationCause::Canceled));
}

TEST_F(RunRegistryAdmissionTest, ReuseBoundaryValidCancelWritesOnce)
{
    // 健康正例确认真实登记/状态镜像可写，第二次终结按既有契约拒绝。
    const core::TaskIdentity id = makeIdentity();
    (void)registerStandardRun(id);
    EXPECT_NO_THROW(m_registry.markTerminated(id.run, TerminationCause::Canceled));
    const auto after = m_registry.tryRun(id.run);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->terminationCause, TerminationCause::Canceled);
    EXPECT_EQ(after->currentState, core::TaskState::Canceled);
    EXPECT_THROW(m_registry.markTerminated(id.run, TerminationCause::Failed), ExecutionError);
}

}  // namespace
