/**
 * @file EventLifetimeSuitabilityTest.cpp
 * @brief REUSE-T01/B32：核对现有总线生命周期和泛型异常边界，不实现替代总线。
 *
 * 依据：units/core.md §4.9/§5.8 的RAII句柄、§4.3四态值；测试只证明
 * 指定行为及候选差异。总线和sink始终活到退订后，禁止用悬垂指针制造反例。
 */
#include <gtest/gtest.h>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Provenance.hpp>
#include <sdurws/ird/execution/EventBus.hpp>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>

namespace {
using namespace sdurws::ird;

/** @brief 只记录次数和投递线程的非拥有sink；调用方用waitForIdle同步后读取。 */
struct ObservedSink final : core::IDomainEventSink {
    int count = 0;
    std::thread::id callbackThread;
    /** @brief 记录一次事件；event [in] 为总线持有的纯值，此处不保留其引用。 */
    void onEvent(const core::DomainEvent&) override
    {
        ++count;
        callbackThread = std::this_thread::get_id();
    }
};

/** @brief 生成本批仅作投递标记的事件；不要求无效占位身份具备业务真实性。 */
core::DomainEvent markerEvent()
{
    return core::DomainEvent::make(core::ResultArchivedPayload{});
}

/** RAII契约反例：句柄析构后应无订阅、无回调，sink和bus仍然存活。 */
TEST(EventLifetimeAudit, ReferenceHandleDestructionMustUnsubscribe_UT_EVT)
{
    core::ReferenceEventBus bus;
    ObservedSink sink;
    {
        auto subscription = bus.subscribe(sink);
        ASSERT_EQ(bus.subscriberCount(), 1u);
    }
    EXPECT_EQ(bus.subscriberCount(), 0u);
    bus.publish(markerEvent());
    EXPECT_EQ(sink.count, 0);
    // 原实现不退订；显式清理只是保证探测不留下非拥有订阅，不掩盖上述失败。
    bus.removeSink(&sink);
}

/** 既有execution候选具备RAII；同步后观察，不以sleep猜测投递完成。 */
TEST(EventLifetimeAudit, ExistingExecutionHandleDestructionUnsubscribes_UT_EVT)
{
    execution::DomainEventBusImpl bus;
    ObservedSink sink;
    {
        auto subscription = bus.subscribe(sink);
        ASSERT_EQ(bus.subscriberCount(), 1u);
    }
    EXPECT_EQ(bus.subscriberCount(), 0u);
    bus.publish(markerEvent());
    bus.waitForIdle();
    EXPECT_EQ(sink.count, 0);
}

/** 重复订阅的差异观察，不把该差异冒充已批准的产品语义。 */
TEST(EventLifetimeAudit, DuplicateSubscriptionsHaveDifferentRemovalGranularity)
{
    core::ReferenceEventBus reference;
    execution::DomainEventBusImpl executionBus;
    ObservedSink firstSink;
    ObservedSink secondSink;
    auto r1 = reference.subscribe(firstSink);
    auto r2 = reference.subscribe(firstSink);
    auto e1 = executionBus.subscribe(secondSink);
    auto e2 = executionBus.subscribe(secondSink);
    r1->unsubscribe();
    e1->unsubscribe();
    EXPECT_EQ(reference.subscriberCount(), 0u);
    EXPECT_EQ(executionBus.subscriberCount(), 1u);
    reference.publish(markerEvent());
    executionBus.publish(markerEvent());
    executionBus.waitForIdle();
    EXPECT_EQ(firstSink.count, 0);
    EXPECT_EQ(secondSink.count, 1);
}

/** 线程适配边界：同步总线回调在发布线程，execution候选在专用线程。 */
TEST(EventLifetimeAudit, ExistingExecutionChangesCallbackThread)
{
    core::ReferenceEventBus reference;
    execution::DomainEventBusImpl executionBus;
    ObservedSink firstSink;
    ObservedSink secondSink;
    auto r = reference.subscribe(firstSink);
    auto e = executionBus.subscribe(secondSink);
    const auto publisher = std::this_thread::get_id();
    reference.publish(markerEvent());
    executionBus.publish(markerEvent());
    executionBus.waitForIdle();
    EXPECT_EQ(firstSink.callbackThread, publisher);
    EXPECT_NE(secondSink.callbackThread, publisher);
    r->unsubscribe();
}

/**
 * @brief 满足模板声明的默认构造/可拷贝值；有意在复制或比较时抛异常。
 * 只在死亡测试子进程中使用。它不是产品值类型，也不证明double消费者会崩溃。
 */
struct ThrowingValue {
    static bool rejectCopy;
    ThrowingValue() = default;
    /** @brief 复制探针，rejectCopy [in] 为测试开关；开启时抛runtime_error。 */
    ThrowingValue(const ThrowingValue&)
    {
        if (rejectCopy) { throw std::runtime_error("audit copy"); }
    }
    ThrowingValue(ThrowingValue&&) = default;
    ThrowingValue& operator=(const ThrowingValue&) = default;
    ThrowingValue& operator=(ThrowingValue&&) = default;
    /** @brief 比较探针固定抛出，用于确认外层noexcept是否终止进程。 */
    bool operator==(const ThrowingValue&) const { throw std::runtime_error("audit compare"); }
};
bool ThrowingValue::rejectCopy = false;

/** 子进程退出73只证明当前终止行为；是否收窄模板约束仍待契约裁定。 */
TEST(SourcedValueLifetimeAudit, ThrowingCopyTerminatesAtNoexceptTryValue)
{
    EXPECT_EXIT({
        std::set_terminate([] { std::_Exit(73); });
        auto value = core::SourcedValue<ThrowingValue>::provided(
            ThrowingValue{}, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        ThrowingValue::rejectCopy = true;
        (void)value.tryValue();
        std::_Exit(74);
    }, ::testing::ExitedWithCode(73), "");
}

/** Provided比较调用T的比较，而模板只写可拷贝；不擅自改变公共noexcept。 */
TEST(SourcedValueLifetimeAudit, ThrowingComparisonTerminatesAtNoexceptEquality)
{
    EXPECT_EXIT({
        std::set_terminate([] { std::_Exit(73); });
        auto value = core::SourcedValue<ThrowingValue>::provided(
            ThrowingValue{}, core::ValueProvenance::make(core::ProvenanceKind::UserProvided));
        (void)(value == value);
        std::_Exit(74);
    }, ::testing::ExitedWithCode(73), "");
}
} // namespace
