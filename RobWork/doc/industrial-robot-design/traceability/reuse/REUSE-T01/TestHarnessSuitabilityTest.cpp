/**
 * @file TestHarnessSuitabilityTest.cpp
 * @brief REUSE-T01：通过GTest公开事件集合验证监听器所有权契约，纯审计探测。
 * 依据：testkit §7.3 / TK-T10；不执行静态对象非法delete，不修改生产监听器。
 */
#include <gtest/gtest.h>

namespace {
/**
 * @brief 析构观察器；计数由本测试持有，集合析构前始终有效，单线程使用。
 */
class CountingListener final : public ::testing::EmptyTestEventListener {
public:
    /// @brief 借持析构计数；@param count [in,out] 测试栈上的计数器，无单位。
    explicit CountingListener(int& count) : m_count(count) {}
    /// @brief 记录真实析构；不抛异常，计数器生命周期由测试保证。
    ~CountingListener() override { ++m_count; }
private:
    int& m_count; ///< 借持计数器，所有权仍归测试栈。
};
}

// TK-T10：使用公开Append，证明集合接管所有权；不把产品错误注释当规范。
TEST(ReuseTestHarnessAudit, ListenerCollectionOwnsAppendedListener) {
    int destroyed = 0;
    {
        ::testing::TestEventListeners listeners;
        listeners.Append(new CountingListener(destroyed));
        EXPECT_EQ(0,destroyed);
    }
    EXPECT_EQ(1,destroyed);
}
