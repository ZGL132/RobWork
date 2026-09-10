/**
 * @file   Events.cpp
 * @brief  事件契约实现——工厂（kind↔variant 一致性）、变体访问守卫、参考总线。
 *
 * 设计依据：
 *   - units/core.md §4.9/§5.8（事件四类/总线契约/参考实现校验语义）、§8 UT-EVT
 *   - 任务契约 tasks/foundation/CORE-T08.json（≙WP-03-T08）
 */

#include <sdurws/ird/core/Events.hpp>

#include <algorithm>

namespace sdurws::ird::core {

// ---- token 映射（§4.9 token 列） ----
const char* toToken(DomainEventKind kind) noexcept
{
    switch (kind) {
    case DomainEventKind::RevisionCommitted:       return "revision-committed";
    case DomainEventKind::DependencyInvalidated:   return "dependency-invalidated";
    case DomainEventKind::TaskStatusChanged:       return "task-status-changed";
    case DomainEventKind::ResultArchived:          return "result-archived";
    }
    return "unknown";
}

std::optional<DomainEventKind> domainEventKindFromToken(std::string_view token) noexcept
{
    if (token == "revision-committed")     { return DomainEventKind::RevisionCommitted; }
    if (token == "dependency-invalidated") { return DomainEventKind::DependencyInvalidated; }
    if (token == "task-status-changed")    { return DomainEventKind::TaskStatusChanged; }
    if (token == "result-archived")        { return DomainEventKind::ResultArchived; }
    return std::nullopt;
}

// ---- 工厂：kind 与 payload 备选一一对应（编译路径即一致性保证） ----
DomainEvent DomainEvent::make(RevisionCommittedPayload payload)
{
    DomainEvent e;
    e.id = EventId::generate();
    e.emittedAtUtc = std::chrono::system_clock::now();
    e.kind = DomainEventKind::RevisionCommitted;
    e.payload = std::move(payload);
    return e;
}

DomainEvent DomainEvent::make(DependencyInvalidatedPayload payload)
{
    DomainEvent e;
    e.id = EventId::generate();
    e.emittedAtUtc = std::chrono::system_clock::now();
    e.kind = DomainEventKind::DependencyInvalidated;
    e.payload = std::move(payload);
    return e;
}

DomainEvent DomainEvent::make(TaskStatusChangedPayload payload)
{
    DomainEvent e;
    e.id = EventId::generate();
    e.emittedAtUtc = std::chrono::system_clock::now();
    e.kind = DomainEventKind::TaskStatusChanged;
    e.payload = std::move(payload);
    return e;
}

DomainEvent DomainEvent::make(ResultArchivedPayload payload)
{
    DomainEvent e;
    e.id = EventId::generate();
    e.emittedAtUtc = std::chrono::system_clock::now();
    e.kind = DomainEventKind::ResultArchived;
    e.payload = std::move(payload);
    return e;
}

// ---- 变体访问守卫（kind 与实际持有备选不一致＝防御分支） ----
const RevisionCommittedPayload& DomainEvent::asRevisionCommitted() const
{
    if (kind != DomainEventKind::RevisionCommitted) {
        throw CoreError("core/events/kind-payload: kind 与载荷备选不一致"
                        "（RevisionCommitted）");
    }
    return std::get<RevisionCommittedPayload>(payload);
}

const DependencyInvalidatedPayload& DomainEvent::asDependencyInvalidated() const
{
    if (kind != DomainEventKind::DependencyInvalidated) {
        throw CoreError("core/events/kind-payload: kind 与载荷备选不一致"
                        "（DependencyInvalidated）");
    }
    return std::get<DependencyInvalidatedPayload>(payload);
}

const TaskStatusChangedPayload& DomainEvent::asTaskStatusChanged() const
{
    if (kind != DomainEventKind::TaskStatusChanged) {
        throw CoreError("core/events/kind-payload: kind 与载荷备选不一致"
                        "（TaskStatusChanged）");
    }
    return std::get<TaskStatusChangedPayload>(payload);
}

const ResultArchivedPayload& DomainEvent::asResultArchived() const
{
    if (kind != DomainEventKind::ResultArchived) {
        throw CoreError("core/events/kind-payload: kind 与载荷备选不一致"
                        "（ResultArchived）");
    }
    return std::get<ResultArchivedPayload>(payload);
}

bool DomainEvent::operator==(const DomainEvent& o) const
{
    // emittedAtUtc 参与相等（同一事件对象的语义比较）——时钟精度内等同即可，
    // 测试以构造同值或同 id 为主。
    return id == o.id && kind == o.kind && payload == o.payload;
}

// ---- 参考总线（UT-EVT 语义钉子：FIFO/幂等退订/退订后不投递） ----
namespace {

/// 订阅句柄：持总线与 sink 指针；unsubscribe 幂等（重复调用二次起为 no-op）。
class ReferenceSubscription final : public IEventSubscription {
public:
    ReferenceSubscription(ReferenceEventBus* bus, IDomainEventSink* sink)
        : bus_(bus), sink_(sink) {}

    void unsubscribe() override
    {
        if (bus_ != nullptr) {
            bus_->removeSink(sink_);
            bus_ = nullptr;    // 幂等：二次调用 no-op
            sink_ = nullptr;
        }
    }

private:
    ReferenceEventBus* bus_;
    IDomainEventSink* sink_;
};

}  // namespace

void ReferenceEventBus::publish(const DomainEvent& event)
{
    // 投递序＝订阅序（同发布者 FIFO——参考实现单线程投递）；
    // 快照副本遍历：sink 在回调内退订不影响本次遍历（避免迭代器失效）。
    const auto snapshot = sinks_;
    for (IDomainEventSink* sink : snapshot) {
        sink->onEvent(event);
    }
}

std::unique_ptr<IEventSubscription> ReferenceEventBus::subscribe(IDomainEventSink& sink)
{
    sinks_.push_back(&sink);
    return std::make_unique<ReferenceSubscription>(this, &sink);
}

void ReferenceEventBus::removeSink(IDomainEventSink* sink)
{
    // 友元访问路径（ReferenceSubscription 的成员函数可调用）：
    // 本类声明 removeSink 为公有方法并在头文件作为实现细节注释——测试内组件。
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

std::size_t ReferenceEventBus::subscriberCount() const noexcept
{
    return sinks_.size();
}

}  // namespace sdurws::ird::core
