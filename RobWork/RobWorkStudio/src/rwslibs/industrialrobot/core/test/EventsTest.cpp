/**
 * @file   EventsTest.cpp
 * @brief  事件契约用例组——UT-EVT（units/core.md §8）：四类载荷构造与 kind 一致性/
 *         访问守卫/值拷贝移动相等/参考总线 FIFO 与退订语义（CORE-T08）。
 *
 * 设计依据：
 *   - units/core.md §4.9（四类事件/总线契约）、§5.8（签名）、§8 UT-EVT 行
 *   - 需求 TASK-03（五元组）；任务契约 tasks/foundation/CORE-T08.json
 *     acceptance（载荷/一致性/FIFO 参考总线用例）
 */

#include <gtest/gtest.h>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Events.hpp>

#include <memory>
#include <vector>

namespace {
using namespace sdurws::ird::core;

/// 五元组样例（TASK-03）。
TaskIdentity sampleTask()
{
    TaskIdentity t;
    t.project = ProjectId::generate();
    t.branch = BranchId::generate();
    t.revision = RevisionId::generate();
    t.run = RunId::generate();
    t.attempt = AttemptId::fromCanonical("att-1");
    return t;
}

/// 事件收集 sink（记录投递序）。
class RecordingSink final : public IDomainEventSink {
public:
    void onEvent(const DomainEvent& event) override { received_.push_back(event); }
    const std::vector<DomainEvent>& received() const noexcept { return received_; }

private:
    std::vector<DomainEvent> received_;
};

/** 四类载荷构造：kind/variant/访问器三者一致（§8"载荷构造与 kind 一致性"）。 */
TEST(EventPayload, KindConsistency_UT_EVT)
{
    // RevisionCommitted：首修订 parent=nullopt。
    auto rev = DomainEvent::make(RevisionCommittedPayload{
        ProjectId::generate(), BranchId::generate(), RevisionId::generate(), std::nullopt});
    EXPECT_EQ(rev.kind, DomainEventKind::RevisionCommitted);
    EXPECT_FALSE(rev.asRevisionCommitted().parent.has_value());   // 首修订无 parent
    EXPECT_EQ(rev.asRevisionCommitted().project.isValid(), true);

    // DependencyInvalidated：三字段。
    auto inv = DomainEvent::make(DependencyInvalidatedPayload{
        ProjectId::generate(), BranchId::generate(), RevisionId::generate()});
    EXPECT_EQ(inv.kind, DomainEventKind::DependencyInvalidated);
    EXPECT_TRUE(inv.asDependencyInvalidated().revision.isValid());

    // TaskStatusChanged：五元组＋九态。
    auto st = DomainEvent::make(TaskStatusChangedPayload{sampleTask(), TaskState::Running});
    EXPECT_EQ(st.kind, DomainEventKind::TaskStatusChanged);
    EXPECT_EQ(st.asTaskStatusChanged().newState, TaskState::Running);
    EXPECT_TRUE(st.asTaskStatusChanged().task.isValid());

    // ResultArchived：仅五元组（不携带归档路径——§4.9）。
    auto arch = DomainEvent::make(ResultArchivedPayload{sampleTask()});
    EXPECT_EQ(arch.kind, DomainEventKind::ResultArchived);
    EXPECT_TRUE(arch.asResultArchived().task.isValid());

    // token 映射往返（四类）。
    EXPECT_STREQ(toToken(DomainEventKind::RevisionCommitted), "revision-committed");
    EXPECT_EQ(domainEventKindFromToken("task-status-changed"),
              DomainEventKind::TaskStatusChanged);
    EXPECT_EQ(domainEventKindFromToken("no-such"), std::nullopt);
}

/** 访问守卫：kind 与访问器错配抛 CoreError（前缀 core/events/kind-payload）。 */
TEST(EventPayload, AccessorGuardThrows_UT_EVT)
{
    auto rev = DomainEvent::make(RevisionCommittedPayload{
        ProjectId::generate(), BranchId::generate(), RevisionId::generate(), std::nullopt});
    EXPECT_THROW(rev.asTaskStatusChanged(), CoreError);
    EXPECT_THROW(rev.asResultArchived(), CoreError);
    try {
        (void)rev.asDependencyInvalidated();
        FAIL() << "必须抛出";
    } catch (const CoreError& e) {
        EXPECT_EQ(std::string(e.what()).find("core/events/kind-payload: "), 0u);
    }
}

/** 事件值语义：拷贝/移动/相等（§8"事件值拷贝/移动/相等"）。 */
TEST(EventValue, CopyMoveEquality_UT_EVT)
{
    const auto orig = DomainEvent::make(TaskStatusChangedPayload{
        sampleTask(), TaskState::Completed});
    const auto copy = orig;                       // 拷贝
    EXPECT_TRUE(orig == copy);
    const auto moved = std::move(copy);           // 移动（平凡值语义）
    EXPECT_TRUE(moved == orig);

    // 不同载荷不等。
    const auto other = DomainEvent::make(ResultArchivedPayload{sampleTask()});
    EXPECT_FALSE(orig == other);

    // 相等含 payload 深比较：同 id 同 kind 但 payload 差异 → 不等。
    auto sameIdOtherPayload = orig;
    sameIdOtherPayload.payload = ResultArchivedPayload{sampleTask()};
    EXPECT_FALSE(orig == sameIdOtherPayload);
}

/** 记录 sink：收集投递序（供 FIFO 断言）。 */
/** 参考总线：FIFO 投递序（§5.8"修订事件先于其失效事件"的顺序敏感场景）。 */
TEST(ReferenceBus, FifoDeliveryOrder_UT_EVT)
{
    ReferenceEventBus bus;
    RecordingSink sink;
    ASSERT_NE(bus.subscribe(sink).get(), nullptr);

    // 同发布者顺序发布：修订→失效（顺序敏感——§4.9 总线契约示例）。
    const auto commit = DomainEvent::make(RevisionCommittedPayload{
        ProjectId::generate(), BranchId::generate(), RevisionId::generate(), std::nullopt});
    const auto invalidate = DomainEvent::make(DependencyInvalidatedPayload{
        commit.asRevisionCommitted().project, commit.asRevisionCommitted().branch,
        commit.asRevisionCommitted().revision});
    bus.publish(commit);
    bus.publish(invalidate);

    ASSERT_EQ(sink.received().size(), 2u);
    EXPECT_EQ(sink.received()[0].kind, DomainEventKind::RevisionCommitted);   // 先修订
    EXPECT_EQ(sink.received()[1].kind, DomainEventKind::DependencyInvalidated); // 后失效
}

/** 退订语义：退订后不再投递；重复退订幂等（句柄 RAII 语义的参考实现）。 */
TEST(ReferenceBus, UnsubscribeStopsDelivery_UT_EVT)
{
    ReferenceEventBus bus;
    RecordingSink sink;
    auto sub = bus.subscribe(sink);
    ASSERT_NE(sub.get(), nullptr);
    EXPECT_EQ(bus.subscriberCount(), 1u);

    sub->unsubscribe();
    EXPECT_EQ(bus.subscriberCount(), 0u);

    // 退订后发布：不再投递。
    bus.publish(DomainEvent::make(ResultArchivedPayload{sampleTask()}));
    EXPECT_EQ(sink.received().size(), 0u);

    // 重复退订幂等（§4.9 IEventSubscription 行）。
    EXPECT_NO_THROW(sub->unsubscribe());
    EXPECT_EQ(bus.subscriberCount(), 0u);
}

/** 多订阅方：同一事件全部收到（R1 无过滤——订阅方收全部四类）。 */
TEST(ReferenceBus, MultipleSubscribersAllReceive_UT_EVT)
{
    ReferenceEventBus bus;
    RecordingSink s1;
    RecordingSink s2;
    auto h1 = bus.subscribe(s1);
    auto h2 = bus.subscribe(s2);
    EXPECT_EQ(bus.subscriberCount(), 2u);

    const auto event = DomainEvent::make(TaskStatusChangedPayload{
        sampleTask(), TaskState::Paused});
    bus.publish(event);

    EXPECT_EQ(s1.received().size(), 1u);
    EXPECT_EQ(s2.received().size(), 1u);
    EXPECT_TRUE(s1.received()[0] == event);
    EXPECT_TRUE(s2.received()[0] == event);

    // s1 退订后：仅 s2 收到。
    h1->unsubscribe();
    const auto second = DomainEvent::make(ResultArchivedPayload{sampleTask()});
    bus.publish(second);
    EXPECT_EQ(s1.received().size(), 1u);
    EXPECT_EQ(s2.received().size(), 2u);
}
}  // namespace
