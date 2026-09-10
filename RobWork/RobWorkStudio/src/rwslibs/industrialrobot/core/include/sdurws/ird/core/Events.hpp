/**
 * @file   Events.hpp
 * @brief  共享事件与端口基础契约——四类领域事件＋sink/订阅/总线接口（⑤端口归 core）。
 *
 * 设计依据：
 *   - units/core.md §4.9（四类事件/载荷字段/总线契约/"为何必须在 core"）、
 *     §5.8（签名）、§8 UT-EVT（载荷一致性/错配抛错/值语义/参考总线 FIFO 与退订）
 *   - 需求 TASK-03（五元组身份）、ARCH §7.2⑤；P-D-1（词表归 core——TaskState 消费）
 *   - 任务契约 tasks/foundation/CORE-T08.json（≙WP-03-T08）
 *
 * 责任边界（§4.9 明文）：接口与数据形状归 core；总线实现归 execution·ui 并由
 * L5 装配注入；分发/调度/订阅管理归总线实现方。事件不携带失效对象清单/归档路径
 * （防 DTO 膨胀——消费者经端口取数）；进度/心跳不入领域事件（execution 自有通道）。
 *
 * core 提供：DomainEvent 工厂（kind↔variant 一致性校验）＋**测试内参考总线**
 * （FIFO/退订语义的钉子——§5.8"core 测试以参考实现校验语义"；生产总线归
 * execution，不生产 vendored 复制品）。
 *
 * 线程安全：DomainEvent 纯值；参考总线单线程使用（生产总线线程契约由其实现方
 * 承诺——core 测试串行即可钉住语义）。
 */

#ifndef SDURWS_IRD_CORE_EVENTS_HPP
#define SDURWS_IRD_CORE_EVENTS_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include <sdurws/ird/core/Errors.hpp>
#include <sdurws/ird/core/Evaluation.hpp>   // TaskState（TaskStatusChangedPayload）
#include <sdurws/ird/core/Identity.hpp>     // EventId/ProjectId/BranchId/RevisionId/TaskIdentity

namespace sdurws::ird::core {

/// 领域事件四类（§4.9：覆盖修订/失效/任务状态/结果写入——token 冻结）。
enum class DomainEventKind {
    RevisionCommitted,        ///< revision-committed  修订提交
    DependencyInvalidated,    ///< dependency-invalidated  依赖失效通知
    TaskStatusChanged,        ///< task-status-changed  任务状态变更
    ResultArchived,           ///< result-archived  结果归档
};

/// token 映射（报告/日志机器判读用；§4.9 token 列）。
const char* toToken(DomainEventKind kind) noexcept;
/// token→枚举（try 轨）。
std::optional<DomainEventKind> domainEventKindFromToken(std::string_view token) noexcept;

/// 修订提交载荷：首修订无 parent（nullopt）。
struct RevisionCommittedPayload {
    ProjectId project;
    BranchId branch;
    RevisionId revision;
    std::optional<RevisionId> parent;

    bool operator==(const RevisionCommittedPayload& o) const noexcept
    {
        return project == o.project && branch == o.branch
            && revision == o.revision && parent == o.parent;
    }
    bool operator!=(const RevisionCommittedPayload& o) const noexcept { return !(*this == o); }
};

/// 依赖失效通知载荷：不携带失效对象清单（消费者经②查询端口取数——§4.9）。
struct DependencyInvalidatedPayload {
    ProjectId project;
    BranchId branch;
    RevisionId revision;

    bool operator==(const DependencyInvalidatedPayload& o) const noexcept
    {
        return project == o.project && branch == o.branch && revision == o.revision;
    }
    bool operator!=(const DependencyInvalidatedPayload& o) const noexcept { return !(*this == o); }
};

/// 任务状态变更载荷：进度/心跳不入领域事件（execution 自有通道）。
struct TaskStatusChangedPayload {
    TaskIdentity task;
    TaskState newState;

    bool operator==(const TaskStatusChangedPayload& o) const noexcept
    {
        return task == o.task && newState == o.newState;
    }
    bool operator!=(const TaskStatusChangedPayload& o) const noexcept { return !(*this == o); }
};

/// 结果归档载荷：归档位置取自 RunRegistry 登记记录（事件不携带路径）。
struct ResultArchivedPayload {
    TaskIdentity task;

    bool operator==(const ResultArchivedPayload& o) const noexcept
    {
        return task == o.task;
    }
    bool operator!=(const ResultArchivedPayload& o) const noexcept { return !(*this == o); }
};

/// 事件载体：id/emittedAtUtc/kind/payload 四元组——kind 与 variant 备选一致由工厂保证。
struct DomainEvent {
    EventId id;                                             ///< 发布方 generate()
    std::chrono::system_clock::time_point emittedAtUtc{};   ///< UTC 发出时刻（D-02 无包装）
    DomainEventKind kind = DomainEventKind::ResultArchived; ///< 事件类别
    std::variant<RevisionCommittedPayload, DependencyInvalidatedPayload,
                 TaskStatusChangedPayload, ResultArchivedPayload>
        payload;                                            ///< 与 kind 一致的载荷

    /**
     * @brief 各类工厂（kind 与 variant 备选一致性由构造路径保证）。
     */
    static DomainEvent make(RevisionCommittedPayload payload);
    static DomainEvent make(DependencyInvalidatedPayload payload);
    static DomainEvent make(TaskStatusChangedPayload payload);
    static DomainEvent make(ResultArchivedPayload payload);

    /**
     * @brief 变体访问守卫：返回与 kind 一致的载荷；不一致抛
     *        CoreError("core/events/kind-payload:")（正常路径不可达——防御性钉住）。
     */
    const RevisionCommittedPayload& asRevisionCommitted() const;
    const DependencyInvalidatedPayload& asDependencyInvalidated() const;
    const TaskStatusChangedPayload& asTaskStatusChanged() const;
    const ResultArchivedPayload& asResultArchived() const;

    /// 值相等（四元组全等；payload 按 variant 同备选深比较）。
    bool operator==(const DomainEvent& o) const;
    bool operator!=(const DomainEvent& o) const { return !(*this == o); }
};

/// 订阅方接口（实现方：ui/workflow/插件——onEvent 内不得回调总线以防死锁）。
class IDomainEventSink {
public:
    virtual ~IDomainEventSink() = default;
    virtual void onEvent(const DomainEvent& event) = 0;
};

/// 订阅句柄接口（RAII 语义由实现方包装；重复 unsubscribe 幂等）。
class IEventSubscription {
public:
    virtual ~IEventSubscription() = default;
    virtual void unsubscribe() = 0;
};

/// 事件总线接口（实现归 execution·ui，L5 装配注入；R1 无事件过滤——订阅方收全部）。
class IDomainEventBus {
public:
    virtual ~IDomainEventBus() = default;
    /// 投递事件（同发布者 FIFO；至少一次/进程内恰一次——实现方契约）。
    virtual void publish(const DomainEvent& event) = 0;
    /// 订阅（返回 RAII 句柄；退订后不再投递）。
    virtual std::unique_ptr<IEventSubscription> subscribe(IDomainEventSink& sink) = 0;
};

// =====================================================================
// 参考总线（测试内最小实现——§5.8"core 测试以参考实现校验语义"的钉子载体；
// 非生产组件：生产总线归 execution·ui，本类不进入任何产品链接面——T-1 由
// TkBuild/ProductBoundary 扫描钉住）。
// =====================================================================

/// 参考总线：同发布者 FIFO 投递＋幂等退订＋退订后不再投递（UT-EVT 语义钉子）。
class ReferenceEventBus final : public IDomainEventBus {
public:
    void publish(const DomainEvent& event) override;
    std::unique_ptr<IEventSubscription> subscribe(IDomainEventSink& sink) override;

    /// 已订阅 sink 数（测试观测面）。
    std::size_t subscriberCount() const noexcept;

    /// 移除指定 sink（订阅句柄的退订路径；幂等——不存在时为 no-op）。
    void removeSink(IDomainEventSink* sink);

private:
    std::vector<IDomainEventSink*> sinks_;   ///< 订阅序即投递序（跨订阅方不承诺顺序）
};

}  // namespace sdurws::ird::core

#endif  // SDURWS_IRD_CORE_EVENTS_HPP
