# execution 整改中的公共边界提案

本文件记录具体方案与逐项批准；未批准项不进入实现。依据本轮指令“新核心、上游修改、新依赖渠道、公共语义及 owner 变化只提交提案，不进入实现”。不改框架/vcpkg，不引入新依赖渠道。

## C1：退订完成边界（EXR-09）

所有者在本会话回复“批准推荐方案”。本轮据此实施等待本订阅位在投回调的方案。

推荐：退订先令该订阅位失效，再在不持总线锁的条件下等待该位已开始的唯一回调完成。返回后 sink 可销毁；其他订阅位不受等待影响，publish 不等待外部回调。保留 onEvent 禁止回调总线约束，调用方不得在持有回调所需锁时同步退订。尚未开始的快照回调跳过；每次订阅独立，不能按裸 sink 指针删错新订阅。修改 execution 公共注释及与之冲突的旧“退订永不等回调”测试/设计条款，core 接口及生产总线选择不变。

替代：保留非阻塞退订，明确 sink 必须存活到额外 waitForIdle 完成，由调用方负责排空。这会增加调用方生命周期责任，不自动采用。

验证：barrier 覆盖尚未开始/已开始两条交错、退订后析构、重复退订/同 sink 多订阅、新订阅不补旧队列、生产 UI 既有 drain 消费；不要求重建第三总线。回滚总线、注释和测试为一组。

## C5：正式结果持久化与批次身份（EXR-02）

推荐：由 evidence 新增版本化 ResultEnvelope 编码/解码公共能力，只序列化现有正式契约，不新增判定规则；解码须经 evidence 现有正式构造/校验恢复对象。优先复用现有 core 内容摘要及既有字节原语，不新增依赖渠道。execution 只调用编码器，经 project 正式归档端口写完整 envelope、批次工件及累计 manifest。project 不解析业务 envelope，不改 TxEngine。

ArrivalEnvelope 增加明确 batchIndex 的值字段（ResultBatch/CheckpointBatch 消费），从 WorkerEvent 传值，按 attempt+ordinal 去重；相同 ordinal 内容一致幂等，冲突拒绝，不同 ordinal 同内容分别保留。正式归档版本标记区分旧域 payload 文件，旧目录只读，不把缺 envelope 的旧物伪装成完整结果。结果缓存使用登记的正式归档字节数，不任意扫描项目目录。

验证：真实 ProjectStore 落盘、重开、完整字段往返＋独立格式负例；同内容异批/同批冲突/缺批/重复 final；故障重试保持 Completed 与 ArchiveFailed 正交；缓存门槛与大小。回滚必须连同格式版本及读路径处理，禁止改写已发布旧目录。

## C2：暂停确认及恢复责任（EXR-06）

推荐：PauseAck 携带明确 CheckpointId，只有当前 attempt 的检查点已完整落盘且由宿主接纳确认，pauseSettled 才为 true；仅收到 ack 不算完成。继续创建新 attempt、旧 attempt superseded；派发明确携带来源检查点及已校验恢复字节，worker 在执行前核对输入/版本并加载。检查点完整 record 的版本化元数据归 execution，project 只承担存储容器/正式事务；跨会话扫描导入由既有 L5 恢复编排调用 execution 入口，不改变 runtime 模型和 evidence 兼容判断所有权。

阶段 A 脚本替身只证明治理机制，不声明正式业务算法可恢复；不扩池化/业务接入范围。取消优先于暂停，损坏/不匹配恢复拒绝，不降级为从头算并宣称“已恢复”。需同步 worker/host 私有协议版本和受影响公共契约；此提案获批前不伪造 checkpoint 或暂停成功。

验证：同会话及隔离存储重开后的真进程暂停/继续、检查点落盘前 ack 不结算、新 attempt/迟到旧帧、损坏/版本/身份拒绝、取消并发、退出清理。协议/元数据格式为整组回滚边界。

## 保持既有裁决边界

C3 计量口径本轮保留既有值与 65%/70% 阈值，只修采样失败不可当零；统一物理驻留/commit 口径需另行选择，不在本轮私改。C4 查询线程冲突先保持当前合法调度线程调用，不凭文档赋予跨线程安全。

## EXR-10：失采表示（局部签名修订）

所有者在本会话回复“批准该局部签名修订”。仅批准下述 optional 表示，计量口径 C3 未改变。

推荐将 WorkerSupervisor::aggregateJobMemoryBytes 返回值从 uint64_t 改为 optional<uint64_t>；空池/合法零返回 0，任一在途 Job 探测失败或加总溢出返回 nullopt。SupervisorMemorySampler 原样传播无数据，ResourceController 沿现规则保留上次等级，不制造零压力。不抛环境异常、不用 UINT64_MAX 冒充真实内存量。唯一生产消费者为 Scheduler.cpp 的 SupervisorMemorySampler；直接测试、perf 消费随编译核对。计量指标与预算均不改变。此项只修“失败≠零”的既有承诺，签名/文档/消费者/回归为一组回滚。
