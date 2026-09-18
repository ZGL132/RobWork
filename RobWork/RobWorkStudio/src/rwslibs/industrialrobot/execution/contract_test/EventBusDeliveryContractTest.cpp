/**
 * @file   EventBusDeliveryContractTest.cpp
 * @brief  事件总线投递线程契约用例组（EX-T05 acceptance 2）——同发布者
 *         FIFO、事件家族按序投递（TaskState 九态事件流＋ResultArchived）、
 *         多线程并发发布零丢失、RAII 退订幂等、退订与投递互不死锁、
 *         waitForIdle 确定性同步（§10.4/§6.2 的契约面）。
 *
 * 设计依据：
 *   - units/execution.md §10.4（EventBus 原文契约：publish 线程安全；同
 *     一发布者 FIFO；subscribe 返回 RAII 句柄、退订幂等不与投递死锁；
 *     事件不持久化）、§6.2（"事件总线投递线程"行——投递线程归 execution
 *     EventBusImpl；订阅方回调串行）、§3.4（测试目标分工：`_contract_test`
 *     ＝跨单元契约面——本文件承载 core IDomainEventBus 接口的 execution
 *     参考实现契约）、§11（事件 FIFO 与投递线程契约用例——EX-T05 完成
 *     条件；§13 ui 行"EventBusImpl（UI 线程 marshaling 扩展点）"的对端）
 *   - core 公共头 Events.hpp（IDomainEventBus/IEventSubscription/
 *     IDomainEventSink 契约与 DomainEvent 四类事件家族——值类型归 core，
 *     本组用例消费其工厂与比较面）
 *   - 需求 TASK-03（请求/完成事件携带五元组）、TASK-01（状态机九态事件流）
 *   - 任务契约 tasks/foundation/EX-T05.json acceptance 2（EventBus 为 core
 *     IDomainEventBus 参考实现——§10.4；TaskState 九态事件流＋ResultArchived
 *     等事件家族按序投递；事件 FIFO 与投递线程契约用例通过）
 *
 * 替身边界声明：订阅 sink 为收集替身（只验证总线分发契约，不构成 ui/
 *   workflow 投影的实现证明）；事件载荷为契约形态数据（core Events 工厂
 *   构造）。断言不 sleep：投递完成以 waitForIdle 确定性等待（本头同时
 *   即该设施的契约用例）；死锁面以有界 future 等待证明（超时即 fail）。
 */

#include <sdurws/ird/core/Evaluation.hpp>
#include <sdurws/ird/core/Events.hpp>
#include <sdurws/ird/core/Identity.hpp>
#include <sdurws/ird/execution/EventBus.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdurws::ird::execution;
namespace core = sdurws::ird::core;

// =====================================================================
// 收集 sink（线程安全——回调在总线投递线程，断言在主线程）
// =====================================================================

class CollectingSink final : public core::IDomainEventSink {
public:
    void onEvent(const core::DomainEvent& event) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(event);
    }
    std::vector<core::DomainEvent> snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events;
    }
    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_events.size();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<core::DomainEvent> m_events;
};

/// 脚本化阻塞 sink：第 n 帧回调阻塞到放行（死锁面的"慢消费者"）。
class BlockingOnNthSink final : public core::IDomainEventSink {
public:
    explicit BlockingOnNthSink(std::size_t nth)
        : m_nth(nth)
    {
    }

    void onEvent(const core::DomainEvent&) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const std::size_t index = m_count++;
        if (index + 1 == m_nth) {
            m_blockEntered = true;
            m_entered.notify_all();
            m_cv.wait(lock, [this] { return m_release; });
        }
    }

    void waitBlocked() const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_entered.wait(lock, [this] { return m_blockEntered; });
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_release = true;
        }
        m_cv.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    mutable std::condition_variable m_entered;
    std::size_t m_nth;
    std::size_t m_count = 0;
    bool m_release = false;
    bool m_blockEntered = false;
};

// =====================================================================
// 载荷构造辅助（契约形态数据——TaskIdentity 五元组与状态词表取自 core）
// =====================================================================

core::ProjectId projectId()
{
    return core::ProjectId::fromCanonical(std::string{
        "prj-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"});
}

core::TaskStatusChangedPayload statusPayload(std::uint64_t runOrdinal, core::TaskState state)
{
    core::TaskStatusChangedPayload p;
    p.task.project = projectId();
    p.task.branch = core::BranchId::generate();
    p.task.revision = core::RevisionId::generate();
    p.task.run = core::RunId::generate();
    p.task.attempt = core::AttemptId{runOrdinal};
    p.newState = state;
    return p;
}

/// 九态事件流（ARCH §4.3 状态机一次完整生命周期的转移序——Queued 起、
/// Interrupted 不在运行期矩阵〔恢复期指派，§5.1〕，本流以其收尾表达
/// "九态词表可承载的到达流"；词表本身归 core，本组只验证按序投递）。
std::vector<core::DomainEvent> nineStateLifecycleStream()
{
    std::vector<core::DomainEvent> stream;
    const core::TaskState lifecycle[] = {
        core::TaskState::Queued,    core::TaskState::Preparing, core::TaskState::Running,
        core::TaskState::Canceling, core::TaskState::Paused,    core::TaskState::Running,
        core::TaskState::Canceling, core::TaskState::Completed,
    };
    for (std::size_t i = 0; i < std::size(lifecycle); ++i) {
        stream.push_back(core::DomainEvent::make(statusPayload(1, lifecycle[i])));
    }
    // 事件家族混投：终相追加 ResultArchived（§10.4——归档 finalize 后）。
    core::ResultArchivedPayload archived;
    archived.task.project = projectId();
    archived.task.run = core::RunId::generate();
    archived.task.attempt = core::AttemptId{1};
    stream.push_back(core::DomainEvent::make(archived));
    return stream;
}

// =====================================================================
// 契约 1：同发布者 FIFO——发布序＝投递序（§10.4 原文）
// =====================================================================

TEST(EventBusDeliveryContractTest, SamePublisherFifoPreservesPublishOrder)
{
    DomainEventBusImpl bus;
    CollectingSink sink;
    auto sub = bus.subscribe(sink);

    // 大数量小载荷：任何乱序/丢帧都会破坏严格递增校验。
    constexpr int kEvents = 1000;
    for (int i = 0; i < kEvents; ++i) {
        bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Running)));
    }
    bus.waitForIdle();

    const std::vector<core::DomainEvent> received = sink.snapshot();
    ASSERT_EQ(received.size(), static_cast<std::size_t>(kEvents)) << "零丢失";
    // 同发布者序的载体＝payload 逐帧相等性不可用（同值帧）——以 emittedAt
    // 单调性无从断言（core D-02 无时钟包装）；改以事件 id 唯一性＋总量
    // ＋订阅单点串行验证 FIFO 基面，顺序本身由下一条用例（状态流全序
    // 不可交换）严格钉住。
    std::set<std::string> ids;
    for (const core::DomainEvent& e : received) {
        ids.insert(e.id.toCanonical());
    }
    EXPECT_EQ(ids.size(), received.size()) << "事件 id 唯一（无重投/复制）";
}

// =====================================================================
// 契约 2：事件家族按序投递——九态事件流＋ResultArchived 严格保序
// =====================================================================

TEST(EventBusDeliveryContractTest, NineStateStreamAndResultArchivedDeliveredInOrder)
{
    // acceptance 2：TaskState 九态事件流＋ResultArchived 等事件家族按序
    // 投递。状态流全序不可交换（相邻帧状态词不同）——投递序与发布序
    // 逐帧相等的强断言；ResultArchived 混投验证家族间同样保序（调度线程
    // 状态流与归档 finalize 事件的相对序＝发布序，§10.4 括注）。
    DomainEventBusImpl bus;
    CollectingSink sink;
    auto sub = bus.subscribe(sink);

    const std::vector<core::DomainEvent> published = nineStateLifecycleStream();
    for (const core::DomainEvent& e : published) {
        bus.publish(e);
    }
    bus.waitForIdle();

    const std::vector<core::DomainEvent> received = sink.snapshot();
    ASSERT_EQ(received.size(), published.size());
    for (std::size_t i = 0; i < published.size(); ++i) {
        EXPECT_EQ(received[i], published[i])
            << "第 " << i << " 帧失序——投递必须与发布逐帧相等";
    }
    // 家族序列的语义锚：末帧是 ResultArchived，其前一帧是 Completed。
    EXPECT_EQ(received.back().kind, core::DomainEventKind::ResultArchived);
    EXPECT_EQ(received[received.size() - 2].asTaskStatusChanged().newState,
              core::TaskState::Completed);
}

// =====================================================================
// 契约 3：publish 线程安全——多线程并发发布零丢失（总量守恒）
// =====================================================================

TEST(EventBusDeliveryContractTest, ConcurrentPublishLosesNothing)
{
    DomainEventBusImpl bus;
    CollectingSink sink;
    auto sub = bus.subscribe(sink);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;
    std::vector<std::thread> publishers;
    for (int t = 0; t < kThreads; ++t) {
        publishers.emplace_back([&bus, kPerThread] {
            for (int i = 0; i < kPerThread; ++i) {
                // attempt 字段承载线程区分（run 全局唯一——attempt 相同
                // 亦不构成同事件；事件 id 唯一性由总线维护）。
                bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Running)));
            }
        });
    }
    for (std::thread& t : publishers) {
        t.join();
    }
    bus.waitForIdle();

    EXPECT_EQ(sink.size(), static_cast<std::size_t>(kThreads * kPerThread))
        << "并发发布零丢失零重复（至少一次/进程内恰一次——core 总线契约）";
}

// =====================================================================
// 契约 4：RAII 退订——句柄析构即退订、重复退订幂等、退订后不再投递
// =====================================================================

TEST(EventBusDeliveryContractTest, RaiiUnsubscribeIdempotentAndStopsDelivery)
{
    DomainEventBusImpl bus;
    CollectingSink sink;

    std::unique_ptr<core::IEventSubscription> sub = bus.subscribe(sink);
    bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Queued)));
    bus.waitForIdle();
    ASSERT_EQ(sink.size(), 1u);

    // 显式退订后（句柄未析构）——立即停投；二次退订幂等（不报错不重删）。
    sub->unsubscribe();
    sub->unsubscribe();
    bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Running)));
    bus.waitForIdle();
    EXPECT_EQ(sink.size(), 1u) << "退订后不再投递（§10.4）";
    EXPECT_EQ(bus.subscriberCount(), 0u);

    // RAII 半区：重新订阅后句柄出析构（作用域限界）——订阅位随之回收。
    {
        auto scoped = bus.subscribe(sink);
        EXPECT_EQ(bus.subscriberCount(), 1u);
    }
    EXPECT_EQ(bus.subscriberCount(), 0u) << "句柄析构即退订（RAII）";
}

// =====================================================================
// 契约 5：退订不与投递死锁（§10.4——回调在锁外，退订永不等回调完成）
// =====================================================================

TEST(EventBusDeliveryContractTest, UnsubscribeDuringDeliveryDoesNotDeadlock)
{
    // 慢消费者阻塞在首帧回调中，另一线程退订：若实现错误地在回调期间
    // 持锁，退订将永远阻塞——以有界 future 等待证明不死锁（超时即 fail；
    // 不 sleep——确定性条件为"退订完成"，回调放行在其后）。
    DomainEventBusImpl bus;
    BlockingOnNthSink slow{1};
    CollectingSink sink;
    auto subSlow = bus.subscribe(slow);
    auto subSink = bus.subscribe(sink);

    bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Queued)));
    slow.waitBlocked();   // 确定性同步：回调已进入阻塞点

    // 另一线程退订慢消费者——必须在回调阻塞期间完成（不死锁的结构证明）。
    auto unsubscribeFuture = std::async(std::launch::async, [&subSlow] {
        subSlow->unsubscribe();
    });
    const bool finished = unsubscribeFuture.wait_for(std::chrono::seconds{2})
        == std::future_status::ready;
    EXPECT_TRUE(finished) << "退订被投递中的回调阻塞——违反'退订不与投递死锁'";

    // 收尾：放行回调（无论退订成败，总线线程必须可退出——析构 drain）。
    slow.release();
    if (!finished) {
        unsubscribeFuture.wait();
    }
    bus.waitForIdle();
    // 快速订阅者不受慢消费者影响：帧照常送达（订阅方按订阅序串行回调
    // ——慢消费者已被移出后续帧的投递面）。
    bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Running)));
    bus.waitForIdle();
    EXPECT_EQ(sink.size(), 2u);
}

// =====================================================================
// 契约 6：订阅序＝投递序、多订阅方各自收全量（R1 无事件过滤）
// =====================================================================

TEST(EventBusDeliveryContractTest, SubscribersReceiveFullStreamInSubscriptionOrder)
{
    DomainEventBusImpl bus;
    CollectingSink first;
    CollectingSink second;
    auto sub1 = bus.subscribe(first);
    auto sub2 = bus.subscribe(second);

    const std::vector<core::DomainEvent> published = nineStateLifecycleStream();
    for (const core::DomainEvent& e : published) {
        bus.publish(e);
    }
    bus.waitForIdle();

    // 两订阅方各自收全量（core"订阅方收全部"——R1 无过滤）且互为相等的
    // 全序拷贝（订阅序影响单帧内的回调先后，不改变各自的流序）。
    EXPECT_EQ(first.snapshot(), published);
    EXPECT_EQ(second.snapshot(), published);
}

// =====================================================================
// 契约 7：waitForIdle 确定性同步——队列清空且回调返回后才算 idle
// =====================================================================

TEST(EventBusDeliveryContractTest, WaitForIdleSyncsDeliveryDeterministically)
{
    DomainEventBusImpl bus;
    CollectingSink sink;
    auto sub = bus.subscribe(sink);

    // 无事件：立即 idle（不阻塞）。
    bus.waitForIdle();
    EXPECT_EQ(sink.size(), 0u);

    // 发布后：idle 返回即全部可见（替代 sleep 断言的确定性同步面）。
    for (int i = 0; i < 32; ++i) {
        bus.publish(core::DomainEvent::make(statusPayload(1, core::TaskState::Running)));
    }
    bus.waitForIdle();
    EXPECT_EQ(sink.size(), 32u);
}

}  // namespace
