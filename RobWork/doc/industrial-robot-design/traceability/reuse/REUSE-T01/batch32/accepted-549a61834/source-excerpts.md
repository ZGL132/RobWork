### core.audit-b32.RevisionCommittedPayload.notEqual | sdurws::ird::core::RevisionCommittedPayload::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:65-65
    bool operator!=(const RevisionCommittedPayload& o) const noexcept { return !(*this == o); }

### core.audit-b32.DependencyInvalidatedPayload.notEqual | sdurws::ird::core::DependencyInvalidatedPayload::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:78-78
    bool operator!=(const DependencyInvalidatedPayload& o) const noexcept { return !(*this == o); }

### core.audit-b32.TaskStatusChangedPayload.notEqual | sdurws::ird::core::TaskStatusChangedPayload::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:90-90
    bool operator!=(const TaskStatusChangedPayload& o) const noexcept { return !(*this == o); }

### core.audit-b32.ResultArchivedPayload.notEqual | sdurws::ird::core::ResultArchivedPayload::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:101-101
    bool operator!=(const ResultArchivedPayload& o) const noexcept { return !(*this == o); }

### core.audit-b32.DomainEvent.notEqual | sdurws::ird::core::DomainEvent::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:132-132
    bool operator!=(const DomainEvent& o) const { return !(*this == o); }

### core.audit-b32.ComparativeValue.notEqual | sdurws::ird::core::ComparativeValue::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:50-50
    bool operator!=(const ComparativeValue& o) const noexcept { return !(*this == o); }

### core.audit-b32.ComparativeFields.notEqual | sdurws::ird::core::ComparativeFields::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:62-62
    bool operator!=(const ComparativeFields& o) const noexcept { return !(*this == o); }

### core.audit-b32.DiagnosticRecord.notEqual | sdurws::ird::core::DiagnosticRecord::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:129-129
    bool operator!=(const DiagnosticRecord& o) const noexcept { return !(*this == o); }

### core.audit-b32.ConfirmationCredential.notEqual | sdurws::ird::core::ConfirmationCredential::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:144-144
    bool operator!=(const ConfirmationCredential& o) const noexcept { return !(*this == o); }

### core.audit-b32.ConfirmableFinding.notEqual | sdurws::ird::core::ConfirmableFinding::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:172-172
    bool operator!=(const ConfirmableFinding& o) const noexcept { return !(*this == o); }

### core.audit-b32.SourcedValue.notEqual | sdurws::ird::core::SourcedValue<T>::operator!= | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Provenance.hpp:229-229
    bool operator!=(const SourcedValue& o) const noexcept { return !(*this == o); }

### core.audit-b32.CoreError.inheritedConstructors | sdurws::ird::core::CoreError | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Errors.hpp:38-41
class CoreError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;   ///< 透传构造（string 与 const char* 两形）
};

### core.audit-b32.RevisionCommittedPayload.equal | sdurws::ird::core::RevisionCommittedPayload::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:60-64
    bool operator==(const RevisionCommittedPayload& o) const noexcept
    {
        return project == o.project && branch == o.branch
            && revision == o.revision && parent == o.parent;
    }

### core.audit-b32.DependencyInvalidatedPayload.equal | sdurws::ird::core::DependencyInvalidatedPayload::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:74-77
    bool operator==(const DependencyInvalidatedPayload& o) const noexcept
    {
        return project == o.project && branch == o.branch && revision == o.revision;
    }

### core.audit-b32.TaskStatusChangedPayload.equal | sdurws::ird::core::TaskStatusChangedPayload::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:86-89
    bool operator==(const TaskStatusChangedPayload& o) const noexcept
    {
        return task == o.task && newState == o.newState;
    }

### core.audit-b32.ResultArchivedPayload.equal | sdurws::ird::core::ResultArchivedPayload::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Events.hpp:97-100
    bool operator==(const ResultArchivedPayload& o) const noexcept
    {
        return task == o.task;
    }

### core.audit-b32.ComparativeValue.equal | sdurws::ird::core::ComparativeValue::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:46-49
    bool operator==(const ComparativeValue& o) const noexcept
    {
        return quantity == o.quantity && unit == o.unit;
    }

### core.audit-b32.ComparativeFields.equal | sdurws::ird::core::ComparativeFields::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:58-61
    bool operator==(const ComparativeFields& o) const noexcept
    {
        return actual == o.actual && expected == o.expected;
    }

### core.audit-b32.ConfirmationCredential.equal | sdurws::ird::core::ConfirmationCredential::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/DiagData.hpp:140-143
    bool operator==(const ConfirmationCredential& o) const noexcept
    {
        return principal == o.principal && confirmedAtUtc == o.confirmedAtUtc;
    }

### core.audit-b28.Events.ReferenceSubscription | sdurws::ird::core::(anonymous namespace)::ReferenceSubscription::unsubscribe | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp:127-144
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

### core.audit-b28.Events.ReferenceEventBus.publish | sdurws::ird::core::ReferenceEventBus::publish | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp:148-156
void ReferenceEventBus::publish(const DomainEvent& event)
{
    // 投递序＝订阅序（同发布者 FIFO——参考实现单线程投递）；
    // 快照副本遍历：sink 在回调内退订不影响本次遍历（避免迭代器失效）。
    const auto snapshot = sinks_;
    for (IDomainEventSink* sink : snapshot) {
        sink->onEvent(event);
    }
}

### core.audit-b28.Events.ReferenceEventBus.subscribe | sdurws::ird::core::ReferenceEventBus::subscribe | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp:158-162
std::unique_ptr<IEventSubscription> ReferenceEventBus::subscribe(IDomainEventSink& sink)
{
    sinks_.push_back(&sink);
    return std::make_unique<ReferenceSubscription>(this, &sink);
}

### core.audit-b28.Events.ReferenceEventBus.removeSink | sdurws::ird::core::ReferenceEventBus::removeSink | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp:164-169
void ReferenceEventBus::removeSink(IDomainEventSink* sink)
{
    // 友元访问路径（ReferenceSubscription 的成员函数可调用）：
    // 本类声明 removeSink 为公有方法并在头文件作为实现细节注释——测试内组件。
    sinks_.erase(std::remove(sinks_.begin(), sinks_.end(), sink), sinks_.end());
}

### core.audit-b28.Events.ReferenceEventBus.subscriberCount | sdurws::ird::core::ReferenceEventBus::subscriberCount | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/src/Events.cpp:171-174
std::size_t ReferenceEventBus::subscriberCount() const noexcept
{
    return sinks_.size();
}

### core.audit-b28.Provenance.SourcedValue.tryValue | sdurws::ird::core::SourcedValue<T>::tryValue | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Provenance.hpp:174-178
    std::optional<T> tryValue() const noexcept
    {
        if (state_ != FieldState::Provided) { return std::nullopt; }
        return value_;
    }

### core.audit-b28.Provenance.SourcedValue.equal | sdurws::ird::core::SourcedValue<T>::operator== | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/include/sdurws/ird/core/Provenance.hpp:217-228
    bool operator==(const SourcedValue& o) const noexcept
    {
        if (state_ != o.state_) { return false; }
        switch (state_) {
        case FieldState::Provided:
            return value_ == o.value_ && provenance_ == o.provenance_;
        case FieldState::Invalid:
            return rawInput_ == o.rawInput_;
        default:
            return true;   // NotProvided/NotApplicable 无载荷
        }
    }

### execution.audit-b32.EventBus.constructor | sdurws::ird::execution::DomainEventBusImpl::DomainEventBusImpl | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:29-34
DomainEventBusImpl::DomainEventBusImpl()
{
    // 成员初始化序保证 m_deliveryThread 最后构造（其余成员已就绪）——
    // 线程主循环一启动即可安全触碰队列/订阅表/停止位。
    m_deliveryThread = std::thread([this] { deliveryLoop(); });
}

### execution.audit-b32.EventBus.destructor | sdurws::ird::execution::DomainEventBusImpl::~DomainEventBusImpl | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:36-46
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

### execution.audit-b32.EventBus.publish | sdurws::ird::execution::DomainEventBusImpl::publish | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:52-61
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

### execution.audit-b32.EventBus.subscribe | sdurws::ird::execution::DomainEventBusImpl::subscribe | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:63-73
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

### execution.audit-b32.EventBus.waitForIdle | sdurws::ird::execution::DomainEventBusImpl::waitForIdle | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:75-82
void DomainEventBusImpl::waitForIdle() const
{
    // 确定性同步面（替代 sleep 断言——testkit §6.5 同纪律）：谓词＝队列空
    // 且无线程在回调中。两条件缺一不可：队列空但回调未返（最后一帧仍在
    // 派发）或回调已返但队列还有帧，都不算 idle。
    std::unique_lock<std::mutex> lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_inDelivery == 0; });
}

### execution.audit-b32.EventBus.subscriberCount | sdurws::ird::execution::DomainEventBusImpl::subscriberCount | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:84-88
std::size_t DomainEventBusImpl::subscriberCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sinks.size();
}

### execution.audit-b32.EventBus.deliveryLoop | sdurws::ird::execution::DomainEventBusImpl::deliveryLoop | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:94-136
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

### execution.audit-b32.EventBus.subscriptionConstructor | sdurws::ird::execution::DomainEventBusImpl::DeliverySubscription::DeliverySubscription | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:142-147
DomainEventBusImpl::DeliverySubscription::DeliverySubscription(DomainEventBusImpl* bus,
                                                               core::IDomainEventSink* sink)
    : m_bus(bus)
    , m_sink(sink)
{
}

### execution.audit-b32.EventBus.subscriptionDestructor | sdurws::ird::execution::DomainEventBusImpl::DeliverySubscription::~DeliverySubscription | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:149-153
DomainEventBusImpl::DeliverySubscription::~DeliverySubscription()
{
    // RAII 语义：句柄死亡即退订（异常路径同样生效）；removeSink 幂等。
    unsubscribe();
}

### execution.audit-b32.EventBus.unsubscribe | sdurws::ird::execution::DomainEventBusImpl::DeliverySubscription::unsubscribe | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:155-164
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

### execution.audit-b32.EventBus.removeSink | sdurws::ird::execution::DomainEventBusImpl::removeSink | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:166-177
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

### ui.audit-b32.eventBus.productionWiring | sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:746-756
    // 域事件总线（WP-24-T03b 收口）：修订提交事件面——store 打开请求挂接
    // ＋宿主订阅转达域模块（撤销/重做等非 apply 路径的会话同步由此达成，
    // 不引入第二套事件机制——core 事件总线是唯一机制）。
    m_eventBus = std::make_shared<core::ReferenceEventBus>();
    // 打开工厂＝捕获包装（UI-T17）包住对端翻译适配器：包装只透传并捕获
    // 成功绑定集（草稿写半区端口来源），对端翻译语义零改动。
    // T03b-2：成功打开回调同步保存强类型适配器（apply 网关取命令端口）。
    // T03b 收口：适配器挂接事件总线（打开请求转交对端——修订事件出线）。
    auto storeBundle = std::make_shared<BundleCapturingStoreFactory>(
        std::make_shared<app::StoreFactoryPortAdapter>(*m_bridge, m_eventBus.get()),
        [this](std::shared_ptr<app::StorePortAdapter> adapter) {

### ui.audit-b32.eventBus.productionWiring | sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:823-845
            m_revisionSink = std::make_unique<DomainRevisionEventBridge>(
                [this](const core::RevisionCommittedPayload& event) {
                    const auto payload=event;
                    const auto source=m_lastStoreAdapter;
                    QTimer::singleShot(0,this,[this,payload,source] {
                        const auto adapter=m_lastStoreAdapter;
                        // 同路径重新打开也是不同会话；迟到事件不得读取或改变新项目。
                        if(!adapter||adapter!=source||adapter->isClosed()||!m_domains){return;}
                        try {
                            if(adapter->projectStore().projectId()!=payload.project){return;}
                            m_domains->modeling.onRevisionCommitted(payload.branch,payload.revision);
                            if(m_diag.pipeline){m_diag.pipeline->logDev(kPluginDevChannel,
                                "revision-committed -> modeling event delivered after commit: branch="
                                +payload.branch.toCanonical()+" rev="+payload.revision.toCanonical());}
                        } catch(const std::exception& error) {
                            // 已离开总线的异常边界：日志定位环境失败，不让异常穿透Qt。
                            reportLine(std::string("建模修订同步失败：")+error.what());
                            if(m_hostStatusBar){m_hostStatusBar->showMessage(QStringLiteral(
                                "建模修订同步失败，原工作集已保留；请重新打开项目，详情见日志"),8000);}
                        }
                    });
                });
            m_revisionSubscription = m_eventBus->subscribe(*m_revisionSink);

### ui.audit-b32.eventBus.productionWiring | sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:850-925
            class RequirementsRevisionSyncSink final
                : public core::IDomainEventSink {
            public:
                explicit RequirementsRevisionSyncSink(
                    IrdWorkbenchHostPlugin* host)
                    : m_host(host)
                {
                }
                void onEvent(const core::DomainEvent& event) override
                {
                    if (event.kind != core::DomainEventKind::RevisionCommitted) {
                        return;
                    }
                    // WP-14-T12/REQ-10：事务发布事件时仍持有存储独占锁，
                    // 本订阅不能同步回查HEAD，否则同线程再次取writer互斥锁会失败。
                    // 只复制值事件，待命令返回后由UI队列重读权威；QObject上下文
                    // 保证宿主销毁时取消，项目身份与关闭守卫阻止迟到事件污染新项目。
                    const auto payload = event.asRevisionCommitted();
                    const auto source = m_host->m_lastStoreAdapter;
                    QTimer::singleShot(0, m_host, [host = m_host, payload, source] {
                        const auto adapter = host->m_lastStoreAdapter;
                        if (adapter && adapter == source && !adapter->isClosed()
                            && adapter->projectStore().projectId() == payload.project) {
                            try {
                                const auto head=adapter->projectStore().query().head();
                                if (head.id==payload.revision && head.inverse
                                    && head.inverse->commandType=="edit-engineering-policy" && head.parent) {
                                    const auto previous=adapter->projectStore().query().tryRevision(*head.parent);
                                    auto businessRefs=[](const project::RevisionView& revision) {
                                        std::vector<project::ObjectRef> refs;
                                        for(const auto& ref:revision.objectRefs) {
                                            if(ref.objectTypeToken!="EngineeringPolicy" && ref.objectId!=revision.metadataRef.objectId)refs.push_back(ref);
                                        }
                                        // 闭包本身不承诺输入顺序；按稳定身份排序后比较完整引用值。
                                        std::sort(refs.begin(),refs.end(),[](const auto& a,const auto& b) { return a.objectId<b.objectId; });
                                        return refs;
                                    };
                                    // 只有策略变化且其余业务对象逐项相同时，才安全前移建模草稿基线；
                                    // 不把同时发生的模型编辑/分支变化静默重基，防止旧草稿覆盖新模型。
                                    if (previous && businessRefs(*previous)==businessRefs(head) && host->m_domains
                                        && host->m_domains->modeling.module) {
                                        const auto draft=host->m_domains->modeling.module->buildDraftCommand("modeling");
                                        // 已过期或属于其它分支的草稿仍应被project拒绝；策略提交不能
                                        // 越过此前模型修订，只有恰好基于父修订的草稿才可安全前移。
                                        if (!draft || (draft->branch==head.branch && draft->expectedRevision==previous->id)) {
                                            host->m_domains->modeling.bindSessionAnchor(head.branch,head.id);
                                        }
                                    }
                                }
                                host->m_policySource->refreshPublishedPolicy();
                                // 策略修订即使不编译模型也必须推进评估纪元；旧任务晚到不入新账。
                                host->syncKinematicsSessionFacts();
                                if (host->m_content && host->m_lastContextProjection) {
                                    host->m_content->presentProjectContext(*host->m_lastContextProjection);
                                }
                                host->onRequirementExternalRevision(payload.revision);
                            } catch (const std::exception& error) {
                                // 队列任务已离开事件总线的异常边界，必须在本操作边界
                                // 保留可归因错误并提示重开，禁止异常穿透Qt事件循环。
                                reportLine(std::string("需求会话同步失败：") + error.what());
                                if (host->m_hostStatusBar) {
                                    host->m_hostStatusBar->showMessage(
                                        QStringLiteral("需求会话同步失败，请重新打开项目；详情见日志"), 8000);
                                }
                            }
                        }
                    });
                }

            private:
                IrdWorkbenchHostPlugin* m_host;  ///< 宿主（非 owning——插件存活期）
            };
            m_requirementsRevisionSink =
                std::make_unique<RequirementsRevisionSyncSink>(this);
            m_requirementsRevisionSubscription =
                m_eventBus->subscribe(*m_requirementsRevisionSink);

### ui.audit-b32.eventBus.productionWiring | sdurws::ird::ui::IrdWorkbenchHostPlugin::initialize | RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:1022-1022
    contentDeps.wiring.eventBus = m_eventBus;

