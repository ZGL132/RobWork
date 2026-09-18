/**
 * @file   EventBus.hpp
 * @brief  领域事件总线（DomainEventBusImpl）——core IDomainEventBus 接口的
 *         execution 侧进程内参考实现（§10.4）：专用投递线程、publish 线程
 *         安全、同发布者 FIFO、RAII 退订、退订与投递互不死锁。
 *
 * 设计依据：
 *   - units/execution.md §10.4（EventBus 原文契约：publish 线程安全；同一
 *     发布者 FIFO——调度线程发布的 TaskStatusChanged 序＝状态机转移序，
 *     ResultArchived 在归档 finalize 后；subscribe 返回 RAII 句柄、退订
 *     幂等不与投递死锁；事件不持久化〔core D-09〕；execution 发布的事件
 *     ＝TaskStatusChanged{task:五元组, newState} 与 ResultArchived{task}，
 *     进度不入事件）、§6.2（线程模型表"事件总线投递线程"行：execution
 *     EventBusImpl 独占一条投递线程，UI 线程 marshaling 由 ui 侧扩展承担
 *     ——本头零 Qt）、§3.1（EventBus.hpp 组成行：DomainEventBusImpl——
 *     core IDomainEventBus 的进程内参考实现：线程安全、同发布者 FIFO、
 *     RAII 退订）、§13（ui 行交接：EventBusImpl（UI 线程 marshaling 扩展
 *     点）——包装归 ui，不在本头预建）
 *   - core 公共头 sdurws/ird/core/Events.hpp（IDomainEventBus/
 *     IEventSubscription/IDomainEventSink 接口与 DomainEvent 四类事件家族
 *     ——值类型与接口归 core，本实现零重定义）
 *   - 需求 TASK-03（请求/完成事件携带五元组）、ARCH §7.2⑤（⑤端口——
 *     事件通道）；O-10（进程内事件总线参考实现归 execution 拥有——§2.1）
 *   - 任务契约 tasks/foundation/EX-T05.json acceptance 2（EventBus 为 core
 *     IDomainEventBus 参考实现——§10.4；TaskState 九态事件流＋ResultArchived
 *     等事件家族按序投递；事件 FIFO 与投递线程契约用例通过）
 *
 * 背景说明（为什么总线在 execution、接口在 core）：
 *   core 只冻结"事件长什么样、总线怎么订阅"的契约面（Events.hpp）；总线
 *   的实现归 execution 与 ui 并由 L5 应用壳装配期选择注入（core.md §4.9
 *   原文）。本类是 execution 交付的**生产参考实现**：单进程内一条投递
 *   线程按发布序串行分发，满足"调度线程发布的任务状态事件序＝状态机
 *   转移序"的强序要求（同发布者 FIFO——§10.4 括注）。ui 侧的 UI 线程
 *   marshaling（把回调转发到 Qt 主线程）由 ui 包装/扩展本实现完成
 *   （§13 交接行），本单元零 Qt（D-01）。
 *
 * P-EX-1 处置（登记单元卡 §15.4）：消费的 core 事件家族（DomainEvent 四类
 *   载荷）与总线接口（IDomainEventBus/IEventSubscription）为 core.md
 *   v0.1 Draft 未冻结契约——本实现按其现状承接，冻结 diff 后按影响面
 *   增量同步留痕，不私改对端、不预判冻结结果。
 *
 * 投递语义（本类的核心承诺，逐条对应 §10.4/§6.2）：
 *   1. **publish 线程安全且即返**：任意线程并发调用；调用只做入队＋唤醒，
 *      绝不在调用线程执行订阅者回调（慢消费者不阻塞发布方——§6.2 投递
 *      线程行的职责分离）；
 *   2. **同发布者 FIFO（且实现为全局 FIFO）**：单队列按入队序投递，
 *      任一发布者的事件之间保持其发布序——调度线程发布的 TaskStatusChanged
 *      序因此严格等于状态机转移序（§10.4 原文；跨发布者之间的相对序虽
 *      未被 core 契约要求，本实现同样按全局到达序保持，语义更强且可测）；
 *   3. **投递线程唯一**：全部回调发生在总线自有的投递线程上——订阅方
 *      回调串行执行，单订阅方无须为自己的并发安全付费（§6.2：投递线程
 *      归 execution EventBusImpl）；
 *   4. **退订幂等、不与投递死锁**：退订只拿订阅表锁做移除；投递线程
 *      先在锁内拷贝订阅表快照、再在**锁外**逐个回调——回调期间不持有
 *      任何总线内部锁，另一线程的退订（或订阅）永不等回调完成
 *      （core IDomainEventSink 注明 onEvent 内不得回调总线——本实现
 *      从锁结构上保证即使误用也只影响序、不产生死锁环）；
 *   5. **事件不持久化**（core D-09）：队列纯内存；关停时已入队事件
 *      **投递完再退出**（drain on close——"至少一次"承诺的进程内兑现），
 *      不落盘、不重放。
 *
 * 线程约束汇总：publish/subscribe/unsubscribe/waitForIdle/subscriberCount
 *   任意线程；回调（onEvent）恒在投递线程；析构前须保证全部 RAII 句柄
 *   已退订或即将随句柄析构退订（句柄存活期不得越过总线——调用方纪律）。
 */

#ifndef SDURWS_IRD_EXECUTION_EVENTBUS_HPP
#define SDURWS_IRD_EXECUTION_EVENTBUS_HPP

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <sdurws/ird/core/Events.hpp>   // IDomainEventBus/IEventSubscription/IDomainEventSink/DomainEvent

namespace sdurws::ird::execution {

/**
 * @brief core IDomainEventBus 的 execution 侧进程内参考实现（§3.1/§10.4）。
 *
 * 生命周期：L5 装配期创建（通常 shared_ptr 或长寿命成员），发布方（调度
 *   编排、归档路径）与订阅方（ui/workflow 投影）经注入共享；析构即停止
 *   投递线程（先投递完已入队事件——见文件头投递语义 5），此后一切 publish
 *   调用为调用方违约（总线已死，无"关闭后调用"的运行态可言——调用方
 *   以生命周期保证，不加运行态检查负担）。
 *
 * 实现结构（与投递语义逐条对应的锁设计）：
 *   - 一把内部互斥保护 {事件队列, 订阅表, 停止位, 在投计数}——临界区只做
 *     簿记（入队/拷表/取帧），任何订阅者回调都在锁外执行；
 *   - 一条投递线程：等待事件→锁内取事件＋订阅表快照→锁外逐订阅方回调；
 *   - RAII 句柄 DeliverySubscription：析构即 unsubscribe；重复 unsubscribe
 *     幂等（表内不存在即 no-op）。
 *
 * 线程安全：全部公共方法线程安全（见文件头）；本类不可拷贝/移动（内部
 *   同步原语＋线程所有权）。
 */
class DomainEventBusImpl final : public core::IDomainEventBus {
public:
    /**
     * @brief 构造并启动投递线程（§6.2"事件总线投递线程"——每总线一条）。
     *
     * 线程以成员初始化序最后启动（保证其余成员先行构造完成）；启动失败
     * （系统资源耗尽）抛 std::system_error——装配期 fail-fast，不带病运行
     * （AGENTS §3 错误语义：环境不可恢复，构造失败优于静默降级为同步分发）。
     */
    DomainEventBusImpl();

    /**
     * @brief 析构：停止投递（先投递完已入队事件再退出线程——drain on
     *        close，文件头投递语义 5）。
     *
     * 序：置停止位→唤醒→join。join 期间订阅方回调仍会执行（队列残余），
     * 调用方保证析构时不再有新的 publish 并发（生命周期纪律）；RAII 句柄
     * 若晚于总线析构属调用方违约（句柄持有裸回指——见 DeliverySubscription）。
     */
    ~DomainEventBusImpl() override;

    DomainEventBusImpl(const DomainEventBusImpl&) = delete;             ///< 同步原语不可拷贝
    DomainEventBusImpl& operator=(const DomainEventBusImpl&) = delete;  ///< 同上

    // ---- core IDomainEventBus 契约（§10.4） ----

    /**
     * @brief 投递事件（core 接口实现——线程安全入队，即返不回调）。
     *
     * @param event [in] 领域事件（值拷贝入队——发布方此后对实参的任何
     *              改动不影响投递内容；事件为纯值类型，拷贝廉价）
     *
     * 后置：事件进入全局 FIFO 队列，投递线程按入队序在投递线程上分发
     * （同发布者 FIFO 的保证点——文件头投递语义 2）。本方法不等待投递
     * 完成（即返）；投递完成的同步面见 waitForIdle。
     */
    void publish(const core::DomainEvent& event) override;

    /**
     * @brief 订阅（core 接口实现——返回 RAII 句柄）。
     *
     * @param sink [in] 订阅方（非所有权——本表存裸指针，调用方保证 sink
     *             存活期覆盖订阅期〔至句柄退订〕；同一 sink 重复订阅得到
     *             两个独立订阅位，各自退订——core ReferenceEventBus 同语义）
     * @return RAII 句柄（析构即退订；unsubscribe 幂等——文件头投递语义 4）
     *
     * 后置：订阅后的下一帧起收到事件（订阅前的队列残余不补投——订阅点
     * 语义，与 core 参考总线一致）；投递序＝订阅序（多订阅方按订阅先后
     * 逐个回调——core"订阅序即投递序"同款）。
     */
    std::unique_ptr<core::IEventSubscription> subscribe(core::IDomainEventSink& sink) override;

    // ---- execution 侧管理与观测面（core 接口之外的本实现职责面） ----

    /**
     * @brief 等待队列清空且在投回调全部返回（测试与关停的同步面——
     *        §11"断言不 sleep"纪律的替代设施：投递完成以确定性条件等待
     *        而非时限猜测）。
     *
     * 语义：阻塞至 {队列空 ∧ 无线程正在执行订阅方回调}。有订阅方回调
     * 永不返回则本调用同样不返回（等待方纪律：回调必须有限时长——core
     * IDomainEventSink 注"onEvent 内不得回调总线"即防自环）。供测试在
     * 断言前同步投递完成；生产关停路径无须调用（析构已 drain）。
     */
    void waitForIdle() const;

    /// 当前订阅位数（测试观测面；仅诊断/测试消费，无业务语义）。
    std::size_t subscriberCount() const;

private:
    /// RAII 订阅句柄（subscribe 的返回体）——析构即退订；幂等。
    class DeliverySubscription final : public core::IEventSubscription {
    public:
        /// bus/sink 均为裸回指（非所有权）：总线与订阅方的存活期由调用方
        /// 保证覆盖句柄存活期（AGENTS §2.5 所有权标注——句柄只是退订凭据，
        /// 不延长任何一方生命）。
        DeliverySubscription(DomainEventBusImpl* bus, core::IDomainEventSink* sink);
        ~DeliverySubscription() override;
        /// 幂等退订（二次调用 no-op——表内已无此 sink）。
        void unsubscribe() override;

    private:
        DomainEventBusImpl* m_bus;            ///< 回指总线（非所有权）
        core::IDomainEventSink* m_sink;       ///< 回指订阅方（非所有权）
    };

    /// 退订实现（DeliverySubscription 与析构清理共用；锁内移除，幂等）。
    void removeSink(core::IDomainEventSink* sink);

    /// 投递线程主循环：等待→取帧＋订阅表快照→锁外逐个回调；停止位且
    /// 队列空时退出（drain on close 的判定点）。
    void deliveryLoop();

    mutable std::mutex m_mutex;                     ///< 内部簿记互斥（临界区只做簿记——见文件头）
    std::condition_variable m_hasWork;              ///< 事件到达/停止唤醒（投递线程等待面）
    mutable std::condition_variable m_idle;         ///< 队列清空/在投归零唤醒（waitForIdle 等待面）
    std::deque<core::DomainEvent> m_queue;          ///< 全局 FIFO 事件队列（无界——事件为小值对象，
                                                    ///  投递速率受订阅方回调支配；容量上限与丢弃策略未在
                                                    ///  契约定义，阶段 A 不私设丢弃语义以保"至少一次"）
    std::vector<core::IDomainEventSink*> m_sinks;   ///< 订阅表（订阅序＝投递序——subscribe 后置注）
    bool m_stopped = false;                         ///< 停止位（析构置位；投递线程的退出条件之一）
    std::size_t m_inDelivery = 0;                   ///< 正在执行回调的线程数（0/1——投递线程唯一；
                                                    ///  计数化以便 waitForIdle 判定与未来扩展同形）
    std::thread m_deliveryThread;                   ///< 投递线程（§6.2——每总线一条，成员最后初始化）
};

}  // namespace sdurws::ird::execution

#endif  // SDURWS_IRD_EXECUTION_EVENTBUS_HPP
