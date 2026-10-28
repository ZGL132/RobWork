/**
 * @file EventBus.cpp
 * @brief execution 唯一生产总线：发布边界冻结接收位，退订后安全结束生命周期。
 * 依据 EXR-09/14 与所有者批准 C1；事件值归 core，调度/投递归 execution。
 */
#include <sdurws/ird/execution/EventBus.hpp>
#include <algorithm>
#include <utility>

namespace sdurws::ird::execution {

DomainEventBusImpl::DomainEventBusImpl()
{
    m_deliveryThread = std::thread([this] { deliveryLoop(); });
}

DomainEventBusImpl::~DomainEventBusImpl()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_hasWork.notify_all();
    m_deliveryThread.join();
}

void DomainEventBusImpl::publish(const core::DomainEvent& event)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // EXR-14：接收集合在发布时冻结，后订阅者不补收积压帧。
        m_queue.push_back(QueuedDelivery{event, m_sinks});
    }
    m_hasWork.notify_one();
}

std::unique_ptr<core::IEventSubscription> DomainEventBusImpl::subscribe(core::IDomainEventSink& sink)
{
    auto slot = std::make_shared<SubscriptionSlot>();
    slot->sink = &sink;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sinks.push_back(slot);
    }
    return std::unique_ptr<core::IEventSubscription>(new DeliverySubscription(this, std::move(slot)));
}

void DomainEventBusImpl::waitForIdle() const
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_inDelivery == 0; });
}

std::size_t DomainEventBusImpl::subscriberCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sinks.size();
}

void DomainEventBusImpl::deliveryLoop()
{
    for (;;) {
        QueuedDelivery delivery;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_hasWork.wait(lock, [this] { return m_stopped || !m_queue.empty(); });
            if (m_queue.empty()) return;
            delivery = std::move(m_queue.front());
            m_queue.pop_front();
            ++m_inDelivery;
        }
        for (const auto& slot : delivery.recipients) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!slot->active) continue;
                // 与退订使用同一锁决定“已开始”；检查后解锁而不登记会留下 UAF 窗口。
                slot->inCallback = true;
            }
            slot->sink->onEvent(delivery.event); // 外部回调不持总线锁；不得回调总线。
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                slot->inCallback = false;
                m_idle.notify_all(); // 只等待本订阅位的退订者可先于整队列排空返回。
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_inDelivery;
            if (m_queue.empty() && m_inDelivery == 0) m_idle.notify_all();
        }
    }
}

DomainEventBusImpl::DeliverySubscription::DeliverySubscription(
    DomainEventBusImpl* bus, std::shared_ptr<SubscriptionSlot> slot)
    : m_bus(bus), m_slot(std::move(slot))
{
}

DomainEventBusImpl::DeliverySubscription::~DeliverySubscription()
{
    unsubscribe();
}

void DomainEventBusImpl::DeliverySubscription::unsubscribe()
{
    // 句柄自身不改共享字段；重复/并发退订由订阅位在总线锁下实现幂等。
    m_bus->removeSink(m_slot);
}

void DomainEventBusImpl::removeSink(const std::shared_ptr<SubscriptionSlot>& slot)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    slot->active = false;
    m_sinks.erase(std::remove(m_sinks.begin(), m_sinks.end(), slot), m_sinks.end());
    // C1：条件等待释放总线锁；回调完成后返回，调用方此时可以销毁 sink。
    m_idle.wait(lock, [&slot] { return !slot->inCallback; });
}

} // namespace sdurws::ird::execution
