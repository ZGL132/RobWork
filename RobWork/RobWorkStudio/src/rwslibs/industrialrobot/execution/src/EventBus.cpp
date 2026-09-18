/**
 * @file   EventBus.cpp
 * @brief  DomainEventBusImpl 的实现——专用投递线程＋锁外回调的分发循环
 *         （契约面与线程语义见 EventBus.hpp 文件头，此处不重复）。
 *
 * 设计依据：units/execution.md §10.4/§6.2、core Events.hpp（接口与事件
 *   家族归 core——本文件零重定义）；任务契约 tasks/foundation/EX-T05.json
 *   acceptance 2（事件 FIFO 与投递线程契约用例的承载体）。
 *
 * 实现说明（锁结构的关键点，评审重点）：
 *   - 全部簿记（入队/取帧/订阅表增删/停止位/在投计数）在 m_mutex 内完成，
 *     临界区极短；订阅方回调（onEvent）在锁外执行——这是"退订不与投递
 *     死锁"的结构保证：退订线程拿 m_mutex 时，投递线程不在回调中持锁；
 *   - 投递线程取帧采用"每帧一取"而非整批快照：帧间回到锁内重取订阅表
 *     快照，使退订能在帧边界生效（退订后的残余队列帧不再投给该订阅方
 *     ——core"退订后不再投递"的帧级粒度）。
 */

#include <sdurws/ird/execution/EventBus.hpp>

#include <utility>

namespace sdurws::ird::execution {

// ---------------------------------------------------------------------
// 构造 / 析构（投递线程生命周期）
// ---------------------------------------------------------------------

DomainEventBusImpl::DomainEventBusImpl()
{
    // 成员初始化序保证 m_deliveryThread 最后构造（其余成员已就绪）——
    // 线程主循环一启动即可安全触碰队列/订阅表/停止位。
    m_deliveryThread = std::thread([this] { deliveryLoop(); });
}

DomainEventBusImpl::~DomainEventBusImpl()
{
    {
        // 置停止位并唤醒：投递线程在下一判定点（队列空）退出——已入队
        // 事件先投递完（drain on close，文件头投递语义 5），不丢帧。
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    m_hasWork.notify_all();
    m_deliveryThread.join();   // 等待残余帧投递完成；此后本对象不再有内部并发
}

// ---------------------------------------------------------------------
// core IDomainEventBus 契约
// ---------------------------------------------------------------------

void DomainEventBusImpl::publish(const core::DomainEvent& event)
{
    {
        // 临界区只做"拷贝入队＋唤醒"：不触碰订阅方、不执行回调——publish
        // 即返（文件头投递语义 1），慢消费者只拖慢投递线程，绝不阻塞发布方。
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(event);
    }
    m_hasWork.notify_one();
}

std::unique_ptr<core::IEventSubscription> DomainEventBusImpl::subscribe(core::IDomainEventSink& sink)
{
    {
        // 订阅序＝投递序（push_back 保序）；重复订阅得到独立订阅位——
        // 同一 sink 回调两次，各自独立退订（core 参考总线同语义）。
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sinks.push_back(&sink);
    }
    return std::unique_ptr<core::IEventSubscription>(
        new DeliverySubscription(this, &sink));
}

void DomainEventBusImpl::waitForIdle() const
{
    // 确定性同步面（替代 sleep 断言——testkit §6.5 同纪律）：谓词＝队列空
    // 且无线程在回调中。两条件缺一不可：队列空但回调未返（最后一帧仍在
    // 派发）或回调已返但队列还有帧，都不算 idle。
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_inDelivery == 0; });
}

std::size_t DomainEventBusImpl::subscriberCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sinks.size();
}

// ---------------------------------------------------------------------
// 投递线程主循环
// ---------------------------------------------------------------------

void DomainEventBusImpl::deliveryLoop()
{
    for (;;) {
        core::DomainEvent event;
        std::vector<core::IDomainEventSink*> snapshot;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // 等待条件：有帧可投，或（停止位且队列空＝退出点）。
            m_hasWork.wait(lock, [this] { return !m_queue.empty() || m_stopped; });
            if (m_queue.empty()) {
                // 停止位＋队列空：已入队事件全部投递完毕（drain on close），
                // 线程退出；停止位未置而队列空为虚假唤醒，回环继续等。
                if (m_stopped) {
                    return;
                }
                continue;
            }
            // 帧级取件：每帧回到锁内重取订阅表快照——退订在帧边界生效
            // （退订后的下一帧不再投给该订阅方；当前帧已在投递中，锁外
            // 完成本帧回调，不受并发退订影响——文件头投递语义 4）。
            event = m_queue.front();
            m_queue.pop_front();
            snapshot = m_sinks;
            ++m_inDelivery;   // 在投计数：waitForIdle 的判定输入
        }

        // 锁外回调：订阅方回调串行执行（投递线程唯一——文件头投递语义 3），
        // 期间不持有任何总线内部锁，另一线程的 publish/subscribe/unsubscribe/
        // waitForIdle 全部可前进（无死锁环的结构保证）。
        for (core::IDomainEventSink* sink : snapshot) {
            sink->onEvent(event);
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            --m_inDelivery;
            // 帧投递完毕可能恰好达成 idle（队列已空）——唤醒等待方。
            if (m_queue.empty() && m_inDelivery == 0) {
                m_idle.notify_all();
            }
        }
    }
}

// ---------------------------------------------------------------------
// RAII 订阅句柄
// ---------------------------------------------------------------------

DomainEventBusImpl::DeliverySubscription::DeliverySubscription(DomainEventBusImpl* bus,
                                                               core::IDomainEventSink* sink)
    : m_bus(bus)
    , m_sink(sink)
{
}

DomainEventBusImpl::DeliverySubscription::~DeliverySubscription()
{
    // RAII 语义：句柄死亡即退订（异常路径同样生效）；removeSink 幂等。
    unsubscribe();
}

void DomainEventBusImpl::DeliverySubscription::unsubscribe()
{
    if (m_bus != nullptr) {
        m_bus->removeSink(m_sink);
        // 置空回指：二次 unsubscribe（或句柄未析构而总线先亡的场景防御）
        // 时不再触碰——幂等的实现基座。
        m_bus = nullptr;
        m_sink = nullptr;
    }
}

void DomainEventBusImpl::removeSink(core::IDomainEventSink* sink)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // 线性查找删除：订阅位数量级为个位数（ui 面板/workflow 投影），无
    // 性能诉求；未找到＝已退订（幂等 no-op，不报错——§10.4"退订幂等"）。
    for (auto it = m_sinks.begin(); it != m_sinks.end(); ++it) {
        if (*it == sink) {
            m_sinks.erase(it);
            return;
        }
    }
}

}  // namespace sdurws::ird::execution
