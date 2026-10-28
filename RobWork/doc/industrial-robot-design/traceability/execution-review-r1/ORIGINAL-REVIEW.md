# execution 专项评审（只评审，未实施修复）

评审日期：2026-10-28。结论：当前组件测试能够通过，但在结果持久化、身份绑定、暂停确认、worker 通信与回收、订阅生命周期、缓存和采样治理方面仍有下列可复现或可由代码直接确认的缺陷。不能将 198 项现有测试通过解释为正式业务任务已由 execution 全链承载。

本报告是修复输入，不是修复后的独立验收；未改生产代码、正式测试、公共契约、治理规则或任务状态，未提交或执行远程操作。

## 1. 现场、版本、边界与证据

| 项目 | 实际记录 |
| --- | --- |
| 工作树 / toplevel | `C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork` |
| 分支 | `codex/core-review-fixes` |
| 启动 HEAD | `ab54c2b14684d99b9a3e82f3ee4d2546e80dde2a` |
| 收口观测 HEAD | `6a49e128c00f8cca3943083819ea59aea16a7be9` |
| 脏区 | 启动、收口 `git status --short` 均空；本轮产物只在忽略的 `build/review-execution-20261028/` |
| 并发现场变化 | 其他工作追加一个 POL-T13 独验登记提交；差异全为 traceability 下的 policy 记录，execution、其消费者、构建与测试源码未变；保留该提交，没有切换或回退 |
| 构建 | 本 C: 树 `build/CMakeCache.txt` 的 `CMAKE_HOME_DIRECTORY` 指向本树 `RobWork`，`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`；Release/x64 |
| D: 使用范围 | 仅既有 vcpkg 头/库供给及安装工具；产品源码、framework 链接产物、worker、测试均来自本 C: 树 |
| 审查范围 | execution 全部 35 个生产/构建文件：12 公共头、13 普通cpp、1 worker cpp、8 Win32头/实现、1 CMake；另有25个测试/设施/入口文件。未重审 project TxEngine 或 core 全单元 |

逐文件清单见 [production-coverage.txt](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/production-coverage.txt)，文件内容摘要见source-inputs.json；第6节按实际职责列出覆盖面。

原件均在同一目录：

- [启动记录](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/start.json)、[收口现场与二进制摘要](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/review-scene.json)、[并发提交差异](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/concurrent-head-change.txt)。
- [命令与探针方法](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/commands.txt)、[构建日志](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/build.log)、[文件输入 SHA-256](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/source-inputs.json)。
- [探针原始日志](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/audit-probes.log)、[XML](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/audit-probes.xml)、[原生报告](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/audit-probes-ird-test-report.json)。
- 隔离源码：[AdmissionProbe.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/AdmissionProbe.cpp)、[CheckpointProbe.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/CheckpointProbe.cpp)、[WorkerWriterProbe.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/WorkerWriterProbe.cpp)、[ProcessProbe.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/ProcessProbe.cpp)、[MalformedWorker.cpp](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/MalformedWorker.cpp)。

文中的“动态”包括直接调用真实组件和固定调度点的隔离编译；只有 EXR-07/11 及现有 worker 契约套件是真进程证据。未制造真实 UAF、未注入用户进程、未用短 sleep 代替取消预算测量。首轮探针的错误 IPC 用例已在 `commands.txt` 标记，不用于结论。

## 2. 实际数据流及装配

| 环节 | 当前代码与实际边界 |
| --- | --- |
| 提交 / 冻结输入 | `TaskScheduler::submit` 经项目、修订闭包、快照、评估器和预算端口校验，建立 TaskId、Queued；快照真值来自 evidence，模型真值来自 runtime 注入面 |
| 排队 / 准入 | Scheduler 双优先级、同级 FIFO，检查全局在途数、项目正式任务 cap、节流与排空；一次 tick 出一个队头。maxWorkers 由装配消费，当前未有强制装配证据 |
| 准备模型 | 普通 `dispatchOne` 只到 Preparing 后返回（Scheduler.cpp:598）；模型服务接口存在，没有在该函数自动 prepare/materialize |
| 登记 / 预留 | `RunRegistry::registerRun` 保存五元组、各内容身份、材料及原项目归档网关；`ResultAdmission::reserveForDispatch` 可预留。现有契约测试由夹具显式调用这些动作 |
| 内联 | 仅满足 Preview 内联条件且注入 executor 时运行；Preparing 中调用 executor，返回后推进 Running→终态。普通正式任务没有该短路径 |
| worker | Supervisor 真正 CreateProcess→Job→Resume→Hello/HelloAck→DispatchRequest；**阶段 A 一派发一进程**。worker 只有固定 `stage-a-double|1|7` manifest 和脚本评估器，正常退出即销毁，不是保活池 |
| 回传 | 真实管道承载 progress / heartbeat / batch / checkpoint / final；主进程 reader→inbox→调度线程 poll→seq→重组→事件 sink；sink 到 Admission 的桥接由装配方负责 |
| 接纳 | 九步登记核对及 evidence `aggregateVerdict`、`validateCombination`、`ResultEnvelope::make` 实际存在；HEAD 不参与登记拒绝，允许合法历史修订结果 |
| 归档 | 登记网关提供原项目端口；begin→writeBatch→finalize，失败两次重试、1s/4s 退避后 abandon；Completed 与 archivePhase 分离。实际写出的内容有 EXR-02 缺陷 |
| 缓存 / 事件 | 缓存是单独协调器，`storeResult` 需调用方显式调用；没有 Scheduler 自动短路径、新运行建链、接纳和归档的完整生产装配。归档成功后 Admission 发 ResultArchived |
| 释放 | Supervisor 正常进程退出后清账；Controller 终态资源由 releaseResources/宿主协议处理；不是每个组件自动替其他组件完成生命周期。非法帧异常分支见 EXR-07 |

生产调用检索的直接证据：UiPlugin.cpp:764 创建 **execution::DomainEventBusImpl**，:1567/:2331 执行 drain。workflow 的 CloseSupervisor/ExecutionCloseDrainAdapter 消费 Scheduler/Controller，但不是正式评估器派发装配。全产品定向检索未找到 execution 外生产代码实例化 TaskScheduler/WorkerSupervisor/ResultAdmission 的业务闭环。

另有正式宿主路径：UiPlugin.cpp:3462 创建 `KinEvaluationExecutor`；`ui/plugin/KinEvaluationChannel.cpp` 使用 std::async 和真实 kinematics 计算入口，冻结模型/策略、取消标志和会话 epoch 后回投 UI。该路径明确是会话 Preview/零 envelope/零归档；UiPlugin.cpp:3624 也有会话求解异步调度。它不能作为 execution worker 已运行正式业务的证据，也不能仅因另有该路径就判定业务违规。

`units/execution.md:580` 已批准阶段 A 无池化，`:1133` 明确不含业务评估器接入。故**不将缺少保活池或业务 evaluator 链接作为本轮确认缺陷**。当前正式任务全链治理能力不得按组件测试扩张宣传；后续 L5 接入必须补端到端证据。

四个轴保持区分：TaskState 表示执行状态，TaskOutcome 表示结果执行结局，EngineeringStatus 由 evidence 判定，Currentness 由切片内容判断；archivePhase 再独立表示持久化状态。归档失败保留 Completed 的既有行为正确，本报告不建议改为 Failed。

## 3. 确认缺陷（按严重程度）

严重程度：P1＝可能破坏身份/完整性/生命周期或使治理责任无法结束；P2＝局部资源或复用功能失效、契约投递错误。编号仅为本报告索引，未创建正式任务/缺陷状态。

### EXR-01 · P1 · 名称反解只验证存在，结果仍保留空或错误 ObjectId（动态复现）

- 位置：[Admission.cpp:297](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Admission.cpp:297)，`ResultAdmission::admitFinalEnvelope`；:301 丢弃 resolver 返回值，:386 原样复制 diagnostics。
- 契约：CON-06、execution §9.2 第8步、runtime §7.5/§9.3：用登记映射将运行时名称反解为 ObjectId，再接纳。诊断的 subject 必须对应绑定对象。
- 触发：登记映射 `axis.J1→A`，输出诊断带 runtimeName；subject 为空或另一个 B。预期空值补 A、冲突拒绝；实际两者均 Admitted，分别保持 nullopt 和 B。
- 影响：该 run 的诊断定位、后续报告/对象关联；不能据此声称域 payload 内所有对象也已反解，更未证明已串项目写盘。
- 原证：`AuditProbeAdmission.NameResolutionMustPopulateSubject`、`ResolvedNameMustRejectConflictingSubject`；日志 `resolved subject present=0`、`conflicting subject admission=0`。
- 最小修复/归属：保留 runtime 登记映射权威及 evidence 构造；execution 使用 resolver 返回值生成已归一化诊断，冲突先拒绝，再聚合/构造/归档。不得自行拼剥名称或调用当前项目映射。
- 回归/回滚：上述两例＋不存在名称、旧映射/新映射、项目切换；只回滚 Admission 与相应用例，无存储迁移。

### EXR-02 · P1 · 已发布归档丢失完整结果，manifest 遗漏已接纳批次（动态复现＋静态确认）

- 位置：[Admission.cpp:453](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Admission.cpp:453)，`admitFinalEnvelope`；`runArchiveTransaction`:754；批次去重/编号 :512。
- 契约：ARCH §3.6 evidence 承重边界；execution §9.2 第9步、§9.5“结果批次/最终 envelope”“manifest 唯一完整发布”，§9.3 按批次目标幂等；CON-02/04、TASK-02。
- 触发：接纳一个 ResultBatch，再接纳合法 final。预期持久化构造成功的完整 envelope，并使已发布清单覆盖正式批次；实际只写 `output.payload.canonicalBytes` 到 `envelope-payload.bin`，未保存工程判定、证据、诊断、输入绑定和 producer；无 payload 则写空文件。manifest.items 只有该一个文件。
- 原证：`AuditProbeAdmission.FinalArchiveMustContainEnvelopeAndBatchInventory`；真实 ProjectStore 下的 [归档原件目录](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/archive-observation/manifest.json) 只列4字节 payload，批次文件在盘却不在 manifest。没有把内存 envelope 相等断言当磁盘正确性。
- 相邻静态缺陷：ArrivalEnvelope 没有 batchIndex，Admission 仅用内容摘要去重、按首次到达编号。两个不同批次即使内容合法相同，也被折叠；同一 ordinal 的异内容冲突无法表达。IPC 的 batchIndex 在 WorkerEvent 有承载，进入 Admission 时缺少正式传递位置。
- 影响：重开项目、报告、缓存引用的可解释/可验证结果；清单不能保护遗漏批次。当前内存 envelope 正确不补偿磁盘丢失。project finalize 只验证调用者列出的 items，不能代补 execution 未提交的内容；不需重审 TxEngine。
- 最小修复/复用：保留 evidence 工厂与 project 事务；execution 归档编排必须保存正式完整表示、保留批次号并累计完整清单，按 `(attempt,batchIndex)` 幂等。**未找到可直接复用的公共 ResultEnvelope 编码器；新增持久化编码或 Arrival 公共字段须先形成缺口提案并获所有者批准，本轮不私造协议。** 可先独立准备当前批次清单缺陷的回归。
- 回归/回滚：真实 ProjectStore 重开并重建正式结果、payload缺失、同内容不同batch、同batch同/异内容、缺批/重复final、ArchiveFailed不进缓存；存储格式变更必须版本化并保留旧记录只读，不能靠回滚代码改写已发布目录。

### EXR-03 · P1 · 检查点主键可归属另一个 run（动态复现）

- 位置：[Checkpoint.cpp:105](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Checkpoint.cpp:105)，`CheckpointRecord::isValid`；`writeCheckpoint`:262/290/380。
- 契约：TASK-03、execution §4.1/§8.1 的 CheckpointId(run,seq) 与 task 五元组同源；不能把运行身份当任意缓存键。
- 触发：合法 task=A，checkpointId.run=B，保持其他字段合法。预期写入前 fail-fast；实际 isValid=true，写出不抛并按 B 入会话索引，而 ArchiveRequest.task 仍为 A。
- 证据：`AuditProbeCheckpoint.CheckpointIdMustBelongToTaskRun`。归档端口是原夹具 FakeArchiveDisk，故此探针证明 execution 接纳/索引错误，不冒称真实 project 已接受跨项目路径。
- 影响：按 B 查候选可得到 A 的恢复记录；后续 slice/evaluator 相同不等于运行归属正确。真实 project 仍有路径/五元组防线。
- 最小修复/复用：使用 core 身份相等运算，在既有结构校验要求 checkpointId.run==task.run；保持 evidence 兼容判定，勿把当前 HEAD 加入兼容判定。
- 回归/回滚：异run同切片、同run正常、零run、跨attempt检查点来源；仅校验＋测试的局部回滚，不动磁盘旧物。

### EXR-04 · P1 · 心跳与最终输出分片复用 seq，合法结果会被丢弃（固定交错复现）

- 位置：[worker/main.cpp:152](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/worker/main.cpp:152)，`DataWriter::sendFragmented`；`send`:113；runScript :489/:508/:520 等还无锁读取 seq。
- 契约：execution §6.5 seq单调、重复丢弃、心跳与大结果并行；C++ 并发读写必须同步。
- 交错：执行线程锁内复制 seq=n 后解锁；心跳线程 `send` 分配并发送 n，使 seq=n+1；执行线程以旧 cursor 生成/发送从 n 开始的分片并覆写 seq。主机 FrameSequencer 会把最终片当重复，后续无法完整接纳。普通 send 两段锁也能使字节顺序反转；重排窗口只能缓解顺序反转，不能修复复号和无锁数据竞争。
- 原证：`AuditProbeWorkerWriter.HeartbeatCannotReuseFinalFragmentSequence`，输出 `heartbeat seq=1 final seq=1`。探针原样包含 DataWriter，在真实 fragmentMessage 调用前插入固定交错钩子，未改生产文件；这是组件交错证据，不是真worker高负载概率实验。
- 影响：该 attempt 的 final 丢失/超时/错误终结；与大结果并发的心跳常规路径受影响，不只故障脚本。
- 最小修复/复用：保留协议编码/FrameSequencer；统一在同一写序列同步范围内分配 seq 与提交完整帧，分片不得覆盖并发新值；移除常规路径无锁 seq 读取，故障注入参照值经受控观测取得。无需替换线程库或新进程平台。
- 回归/回滚：固定交错、多个分片间心跳、并发progress、慢管道、断写；writer同步及测试一包回滚，协议字节不变。

### EXR-05 · P1 · IPC 缺消息绑定/阶段门，异尝试命令能生效（动态＋静态）

- 位置：[ChannelProtocol.cpp:506](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/ChannelProtocol.cpp:506)，`MessageReassembler::accept`，完成时 :555 取末片type/identity；[worker/main.cpp:378](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/worker/main.cpp:378)，`pollCommands`；派发换绑 :701；Supervisor::handleFrame :834。
- 契约：TASK-03、execution §6.5 五元组随行、握手后派发/接纳、消息完整性；取消/暂停只能作用于所属尝试。
- 触发/实际：手工两片头保持 total/count 连续，首片DispatchRequest/A，末片FinalOutput/B，重组器 `broken=0 delivered=1`，返回混合字节及B身份；异尝试PauseRequest进入真实 CommandQueue 后，真实runScript直接返回21。预期均拒绝、无控制副作用。
- 原证：`AuditProbeIpc.RejectMixedTypeAndIdentityFragments` 与 `AuditProbeWorkerWriter.RejectPauseCommandFromAnotherAttempt`。另有独立Hello预期字节及手工坏长度正/负例通过，非仅同实现往返。
- 防线与限制：主进程 `handleCompleteFrame`:806 **先逐帧检查五元组**，故混合身份探针不能证明主机已经接纳B结果；worker读端没有同等序列/身份核对，派发时还无条件换绑。主机静态上未以 handshakeDone 限制 FinalOutput/ack，未请求也会置ack标志；这些属于同一绑定/阶段守卫缺口，尚未做伪worker握手前final真进程复现。
- 影响：协议畸形被拼成另一消息，控制请求错误作用；若上层依赖ack标志会错误推进状态。未宣称已绕过RunRegistry五元组或正式算法成功。
- 最小修复/归属：execution 在现有重组状态记录首片type/identity并逐片比对；worker按当前绑定校验命令seq和身份；监督器在既有协议阶段与请求记录上守卫final/ack。阶段A不得以未来池化换绑理由绕过本尝试。保留只传值/字节边界和上层RunRegistry。
- 回归/回滚：同身份异type、同type异attempt、未握手final、未请求ack、重复/回退seq、迟到旧进程帧；仅协议状态与测试回滚，不改协议字段；需新增字段则另行批准。

### EXR-06 · P1 · PauseAck 未承诺检查点，resumeFrom 不参与真实派发（动态＋静态）

- 位置：[worker/main.cpp:442](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/worker/main.cpp:442)，`runScript`；Supervisor.cpp:982，Controller.cpp:475；Supervisor的 sendDispatchRequest 与 DispatchRequestPayload 无 resumeFrom 传递。
- 契约：TASK-01、AT-35、execution §7.2/§7.3；IWorkerHandle::pauseSettled 明确 true意味着带CheckpointId的检查点已写出并被宿主确认。§12虽排除规模化验收，但**机制A期交付**，不能将空ack算实现。
- 触发：支持暂停的调用者请求暂停，worker下一动作直接发空PauseAck并退出21；主机无校验置pauseAckFlag，Controller解除worker并转Paused。继续只经gate推进状态与尝试；WorkerAssignment.resumeFrom没有编码/加载链。
- 原证：`AuditProbeWorkerWriter.PauseAckRequiresCheckpoint`：`pause ack payload bytes=0 checkpoint frames=0`；余链静态确认。现有PauseResume测试使用FakeWorker的pauseSettled与FakeResumeGate，未覆盖这条进程链。
- 影响：Paused看似可续却无已确认状态，重启/继续可能重算或丢统计基点；不能把该链包装成已恢复成功。
- 最小方案/边界：保留Controller能力门控和evidence兼容权威；暂停确认必须消费已有CheckpointCoordinator正式发布结果，续跑必须使用新attempt和明确恢复字节。**若现有IPC不足，需要CheckpointId/恢复输入的协议设计批准，不能本轮擅加公共语义。** 暂未完整支持时应按已有能力契约诚实拒绝，不能放宽能力或预算。
- 回归/回滚：真worker暂停→写盘确认→退出→新attempt恢复、失败保持Running/Paused按既有契约、取消优先、重复统计为零；实现与接线成组回滚，不触旧检查点；跨会话元数据缺口见裁决C2。

### EXR-07 · P1 · reader错误分支不交付退出，进程死后登记与资源不回收（真进程复现）

- 位置：[WorkerSupervisor.cpp:1198](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/WorkerSupervisor.cpp:1198)，`readerLoop` 的 beginRead、assembler、repost、drain及WaitResult::Error失败分支；`drainInbox`:735。
- 契约：execution §7.6 通道损坏→Failed→worker强杀回收；§6.6资源所有权、无永久排空等待。
- 事件序：reader推ChannelError后return；poll处理错误并TerminateJobObject；reader已结束，无人再推Exited；processExit/exitProcessed/pendingDestroy永远不走。每次poll只保留僵尸记录，直到整个Supervisor析构。
- 原证：`AuditProbeProcess.MalformedFrameMustReclaimWorkerAndRestrictInheritedHandles`，自有子进程PID=64004；非法magic后 `violation=1 reclaimed=0 OS dead=1 workerExited=0`。2s有界条件泵未回收；“永久”结论来自退出事件生产者已结束的代码证明，不是把2s观察泛化。析构已清理本轮Job。
- 影响：该worker句柄/管道/登记、退出通知、宿主资源账/排空；错误路径与正常退出F-577修复不重叠，不重复上报历史正常回收问题。
- 最小修复/复用：统一reader终结路径；错误上报后监督并等待本Job退出，再恰一次执行现有processExit/reap。不要在reader里调用业务状态机，不按名称杀进程。
- 回归/回滚：非法magic/版本/截断帧、初读/续读失败、退出drain失败、Broken与Exited双报；核对OS死、exit事件一次、记录清零、句柄稳定。只回滚监督器异常清理分支及用例。

### EXR-08 · P1 · 取消OVERLAPPED读后释放其存储，未等待完成（静态确认，未复现崩溃）

- 位置：[ChannelPair.cpp:121](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/win32/ChannelPair.cpp:121)，析构；`drainAfterExit`:339。
- 契约：Win32 CancelIoEx只请求取消；异步完成前 OVERLAPPED、事件和缓冲必须保持有效。execution §6.6 异步I/O和资源清理保证。
- 交错：read仍pending→CancelIoEx返回→析构立即CloseHandle(event)/销毁OVERLAPPED与buffer；内核随后完成或取消。drain超时分支还先把m_readPending=false，绕过析构的pending处理。与取消是否最终成功无关，调用返回不能当完成确认。
- 原证：对应代码；同文件 `writeCommand`:190 已有CancelIoEx后GetOverlappedResult等待的正确模式。没有制造内核时序UAF或宣称测试已崩溃。
- 影响：单任务通道析构时的内存/句柄生命周期，可能影响宿主稳定性。
- 最小修复/复用：复用该薄适配内部既有取消后收敛模式，确认I/O完成/ERROR_OPERATION_ABORTED再释放；在现有停止预算内统一负责取消和回收，不放宽超时。不新建平台包。
- 回归/回滚：pending-read析构、进程退但写端尚存、取消与自然完成同时、反复create/destroy；可用有界私有进程/管道和句柄计数，需保留未完成读存储直至完成。局部ChannelPair＋用例回滚。

### EXR-09 · P1 · 退订返回后仍调用裸sink，订阅期生命周期承诺不成立（确定性交错复现）

- 位置：[EventBus.cpp:114](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:114)，`deliveryLoop`；`removeSink`:166；EventBus.hpp:127的生命周期注释。
- 契约：core IDomainEventBus“退订后不再投递”；execution承诺sink只需存活至句柄退订，任意线程退订、不持总线锁调用订阅者。
- 交错：投递线程锁内复制[A,B]后，阻塞在A回调；主线程unsubscribe(B)返回并可依法析构B；放行A后快照继续调用B。探针为安全观测保持B活着，收到 `calls after unsubscribe=1`，未实际解引用已释放内存。
- 原证：`AuditProbeEventBus.UnsubscribeMustPreventPendingCallback`；现有退订测试只覆盖已经开始的slow自身退订和后续发布，没有覆盖“快照中尚未开始的另一个sink”。
- 影响：生产总线所有订阅者生命周期。当前UiPlugin额外drain/弱引用可降低具体路径风险，本轮未复现GUI崩溃；F-967换核本身不解决本实现缺陷。
- 最小修复/复用：保留唯一execution总线、FIFO与锁外回调；使用每订阅位的有效/在投保护，使退订后尚未开始的调用跳过，并明确已经在投回调的寿命结束边界。不能只检查active后解锁再调（仍有窗口），不能造第三套总线。若要改变“退订不等待”公共承诺，先裁决，见C1。
- 回归/回滚：barrier固定交错、退订后析构、已经进入回调、重复退订、多订阅位、关停排空；总线实现与生命周期测试成组回滚，生产UiPlugin接线保持。

### EXR-10 · P1 · worker采样失败被当零，采样器仍返回成功（静态确认）

- 位置：[WorkerSupervisor.cpp:545](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/WorkerSupervisor.cpp:545)，`aggregateJobMemoryBytes`；[Scheduler.cpp:1064](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Scheduler.cpp:1064)，`SupervisorMemorySampler::sample`。
- 契约：NFR-PERF-04，ResourceGovernanceTest的探测失败保留上次level；采样器自注明确“任一失败整体无数据，避免半读数假低压力”。
- 触发：某在途job的QueryInformationJobObject失败。聚合只跳过，返回包括0在内的普通总数；外层只有系统/主进程失败返回nullopt，worker失败不可感知。预期无数据/显式保守处理；实际ResourceController可按偏低读数恢复派发。
- 原证：上述两函数的完整条件路径；现有ProbeFailure用例仅给IMemorySampler返回nullopt，未注入真实worker半区失败。**本轮未动态注入该API失败。**
- 影响：全部共享预算任务的派发/节流正确性，失败不应等于worker没有占用；与合法空Job内存为0必须区分。
- 最小修复/复用：在现有采样链传递成功性，复用ResourceController既有无数据处理，不另设计阈值/调度器；若公开返回类型需变化先提案，可先审查内部收敛方式。
- 回归/回滚：一个/多个worker失采、全部失采、合法零、恢复成功、已退出job不计；只采样链与正负例回滚，70%/65%不变。混合指标口径另见C3。

### EXR-11 · P2 · CreateProcess继承所有可继承句柄，非仅所需管道（真进程复现）

- 位置：[ProcessLauncher.cpp:27](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/win32/ProcessLauncher.cpp:27)，`RealProcessOps::createProcess`，TRUE继承＋普通STARTUPINFOW。
- 契约：启动薄适配注释声称继承面恰为两管道副本；用户要求必要句柄最小继承，避免EOF/锁/资源所有权串扰。Job不是安全沙箱。
- 触发：宿主另有可继承event/文件/管道句柄，同时launch。预期子进程只有批准传入的管道；实际子进程成功SetEvent该非管道句柄，`unexpected inherited handle signal=0`（WAIT_OBJECT_0）。
- 原证：EXR-07同一自有PID探针；事件句柄由本轮创建并关闭。现有LauncherClosesInheritedHandleDuplicates只证明父端副本已关，不证明白名单继承。
- 影响：子进程额外持有宿主其他资源，可能延长文件锁/EOF或保留其他任务句柄；未做攻击沙箱结论。
- 最小修复/复用：保留挂起→Job.assign→Resume，Win32薄适配使用明确HANDLE_LIST；并发启动仍不得临时开放全局句柄。复用现有ChannelPair提供的两副本，不引依赖。
- 回归/回滚：额外event/管道/文件句柄、并发launch、加入Job失败与Resume失败清理；ProcessLauncher私有实现与测试回滚，不改公共业务owner。

### EXR-12 · P2 · 正式结果缓存字节恒为零，预算永不淘汰（动态复现）

- 位置：[CacheCoordinator.cpp:362](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/CacheCoordinator.cpp:362)，`storeResult`；`CacheEntrySummary::payloadBytes`默认0。
- 契约：execution §8.2 LRU＋总字节预算/结果模型分账；CacheStats结果账户字节语义。
- 触发：真实归档完成后storeResult，再maxResultCacheBytes=0。预期真实字节入账并被预算淘汰；实际 `result entries=1 bytes=0`，零预算仍保留1项。
- 原证：`AuditProbeAdmission.ResultCacheMustAccountArchivedPayloadBytes`；不是使用未归档fake记录绕过双门槛。
- 影响：预算/统计不可信，索引可持续增长。当前缓存主要存summary，不能把零记账直接说成已有2GiB结果payload驻留内存。
- 最小修复/复用：保留evidence命中权威及Completed＋Archived门槛，从正式归档事实取正确大小入账，累计防溢出；勿按当前文件任意扫描或自造兼容规则。依赖EXR-02的完整归档表示稳定。
- 回归/回滚：非零载荷、零预算、边界等于预算、LRU命中更新、模型账户独立、取消/ArchiveFailed不入账；缓存为可重建优化，协调器＋测试独立回滚，不改归档。

### EXR-13 · P2 · 模型缓存只找完整键，WC部分复用判定永不到达；同长度冲突被吞（动态＋静态）

- 位置：[CacheCoordinator.cpp:268](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/CacheCoordinator.cpp:268)，`lookup`；`storeModelCache`:92。
- 契约：execution §8.2模型WC/DWC复用由已注入runtime judge判定；同键异摘要必须保留既有并诊断。
- 触发/实际：缓存(WC=A,DWC=B)，请求(WC=A,DWC=C)，已注册judge愿判WorkCellOnlyReuse；exact find先Miss，`compile judge calls=0 model verdict=1`。另同完整键、同长度不同bytes时直接返回并更新hit时间，未比内容或报冲突。
- 原证：`AuditProbeAdmission.WorkCellCandidateMustReachRegisteredJudge`；同长度分支静态确认，未另做动态冲突注入。现有测试让替身judge在“完全相等键”上返回部分复用，未覆盖真实部分键候选。
- 影响：不必要重编、错误冲突观测/LRU；没有覆盖旧内容，因此不直接宣称返回了新错误模型。
- 最小修复/复用：候选查找只做索引筛选，让ICompileCacheJudge决定兼容；内容比较复用core ContentDigester或字节等值，非仅size。execution不实现WC/DWC兼容公式。
- 回归/回滚：完整命中、同WC异DWC、异WC、judge拒绝/缺失、同长度异内容、同内容幂等；只模型索引/冲突分支与测试回滚。

### EXR-14 · P2 · 新订阅者收到订阅前已排队事件（动态复现）

- 位置：[EventBus.cpp:114](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/execution/src/EventBus.cpp:114)，投递时取当前订阅表；EventBus.hpp:148后置语义。
- 契约：subscribe明确“订阅前队列残余不补投”。
- 交错：A阻塞首事件；主线程发布第二事件入队，再订阅B；放行A，第二事件投给B。预期B=0，实际 `pre-subscription queued deliveries=1`。
- 原证：`AuditProbeEventBus.SubscriptionMustNotReceivePreviouslyQueuedEvent`；使用promise条件，不靠偶然sleep。
- 影响：新会话投影可能消费订阅前事件；身份过滤仍是消费者职责，未证明B项目已写入A结果。
- 最小修复/复用：随既有事件队列保存入队序/订阅生效序，投递选符合边界的有效订阅位；和EXR-09同一总线实现批处理，保留FIFO/进程内至少一次范围，不新增总线。
- 回归/回滚：积压队列中新订阅、订阅后事件、同发布者FIFO、退订/重订；同EXR-09一包回滚。

## 4. 疑点、契约争议与可选改进

### 有具体依据、尚待验证的疑点

| 项 | 依据、风险与下一个最小验证 |
| --- | --- |
| S1 终态同步 / 取消与final竞态 | Controller只写状态机，Registry第7步只看terminationCause；新run可Preparing且无terminationCause，noteAdmissionCompleted直接写Completed。正式装配需保证取消/Failed/ForceTerminated先同步登记，final不能抢占已生效取消。当前缺真实L5桥接，不能仅凭组件分离就声称产品已发生取消成功归档。补同一调度拍两种到达顺序、取消优先约定与原项目写入观测；Interrupted不在现第7步集合，现测试显式允许，应先裁决而非擅改 |
| S2 时间起点 / 内联阻塞 | Controller.cpp:738在首次poll见Running才记录开始；:664恢复后首次poll也重建时间点。评价超时可能延后一个轮询间隔。请求requestedAt未用，但头声明2s由宿主poll周期保证，因此不直接报漏计时。需要固定时钟、晚首次poll、零/极大timeout和Running时间戳契约。内联executor执行期任务仍Preparing，若超过1s会阻塞唯一调度线程，当前门槛依赖预测/装配；不以违约并发tick造竞态 |
| S3 IPC容量/边界 | ChannelProtocol.cpp:getBytes(:103)以64MiB限制字段，消息重组允许256MiB；>64MiB的outputCanon虽分片仍不能decode。fragmentSize加法/seq递增未防溢出，type/flags未穷举守卫，Attempt文本域24字节容不下最大u64十进制加NUL。待65MiB边界、最大attempt、手工flags/enum/截断EOF负例验证并核准实际支持上限；不放宽容量 |
| S4 缓冲总量 | 重排按256帧有界，每帧64MiB仍可有大总量；MessageReassembler reserve(totalLen)，inbox与worker CommandQueue无字节预算。当前离线同版本场景不等于可任意新增限流/丢弃语义，需用已批准内存预算验证，不能另造支持范围 |
| S5 检查点恢复分配 | Checkpoint.cpp:440先取磁盘file_size，:457按磁盘尺寸resize，再做完整性判断，未先与ref.sizeBytes比对；损坏扩长文件可先分配巨额内存或抛bad_alloc。建议用小型稀疏文件＋可控分配观测验证，先拒尺寸失配；本轮未造巨额分配。listCompatible还未查盘即标integrityVerified并忽略resumable，最终restore能拒，候选语义需核 |
| S6 外部回调异常 / 重入 | EventBus线程未catch sink异常，逃逸会std::terminate；core未明文noexcept。CacheCoordinator多处锁内reportDev，若sink合法回查stats会死锁；Scheduler锁内调用注入校验/事件。需先冻结回调不抛/不可重入边界，或在隔离子进程做一个抛出/重入探针，不能用接口明禁的onEvent回调总线行为作为缺陷证据 |
| S7 失败责任与异常安全 | decoder明确“不抛”，不以违反该约定的fake异常直接报生产缺陷；aggregate/工厂异常、archive abandon失败的接纳/引用清零还需针对合法失败类型核对。markTerminated(Completed)已修，不重复上报；其相邻noteAdmissionCompleted/appendAttempt没有完整跨组件事务，但缺宿主消费事实，不能泛称所有转换半写 |
| S8 队头阻塞 / 最大worker数 | 项目A cap满时队头阻塞B；Interactive持续到来会压Background，设计本身选择严格双优先级/FIFO，需求未承诺公平调度，不擅设计跳队或aging。maxConcurrentTasks/项目cap已消费，maxWorkers/workerShare没有生产强制装配；poolCapacity明确未来预留，不能冒充当前有效cap。实际L5接入时需证明预算约束位置 |

### 契约冲突或所有者裁决

| 项 | 冲突 / 所需最小裁决 |
| --- | --- |
| C1 总线退订生命周期 | “退订永不等回调完成”与“sink只存活至退订返回”在另线程已在投时不能同时保证。EXR-09对尚未开始的回调可直接修；已在投需由owner明确退订等待规则或单独drain/lifetime令牌。不得默默把sink寿命延长责任转给调用者 |
| C2 跨会话检查点 | CheckpointCoordinator只在内存m_index保留完整record，write仅落中间文件与普通RunManifest，无record编码/导入API；restore UnknownCheckpoint注释将扫描重建交project/宿主，但未找到装配。RecoveryProcess测试只证明无manifest run不变完整＋Interrupted重建，未证明跨会话续跑。需在execution记录、project容器、L5恢复之间确定已有持久化契约；新增格式/公共入口只提案 |
| C3 内存计量口径 | 主进程当前WorkingSetSize＋各Job历史PeakJobMemoryUsed（commit）直接求和，既非同时峰值也非统一物理驻留量；已在perf测试说明中固化此测法。performance-baseline §4.7要求覆盖并存峰值，NFR以物理内存70%为分母。需所有者确认可接受保守代理量还是统一口径；不以本轮建议改变70%或预算。MemoryReading u64求和溢出/total=0、NaN阈值及关系校验也需补负例 |
| C4 接口线程口径 | execution.md §10.8要求查询任意线程/深拷贝；Controller.hpp明确progress/tryState仅调度线程，RunRegistry头部“其他线程快照投影”和类注“全部仅调度线程”冲突。拷贝本身不保证对并发写安全。当前没有合法跨线程生产调用证明，先定权威线程模型，勿凭注释假定可并发查询 |
| C5 正式结果持久化表示 | EXR-02已确认丢字段，但正式Envelope codec所有权/版本未有可直接调用入口。沿evidence结果契约、execution编排、project存储边界补批准；不能让execution新增第二套判定/结果契约或改project内部事务 |

### 可选维护性改进（不自动进入修复）

- **M1 身份原语重复**：TaskTypes.cpp:102的TaskId生成仍单次random_device播种mt19937_64，复制了core旧32位熵瓶颈；core的63e2b6305已改四字seed_seq。若每次播种只有32位熵，新线程首TaskId碰撞概率不能靠128位输出长度解释。保留TaskId强类型与前缀，删除重复熵采样/解析代码的适配机会；core现helper是私有，不准跨单元私有头。公共复用面扩大需提案，不能新建随机服务或直接替成不匹配的RobWork全局Random。未做概率性碰撞实验，非本轮动态身份事故。
- **M2 重复字节/时间小工具**：ChannelProtocol自有LE/长度原语，Admission/Checkpoint重复SHA-hex/UTC格式化；先检索core公共格式化能力和现有协议版本，不强行让不同端序协议共用错的编码器。保留独立黄金oracle。只合并完全同契约helper，摘要继续core ContentDigester；不新增平台层。
- **M3 空TU/未来残留**：Execution.cpp无有效符号，poolCapacity/idle池预留易误读；保留已批准将来能力说明，删除无用途编译项可单独小包，但不借清理实现保活池。测试CMake宏也已有治理定案，不重开迁移债务。
- **M4 宿主闭环入口**：将来可用一个已有L5装配点统一“状态机—登记—监督—接纳—归档—缓存—释放”，删除测试中散落的手工假设；归属仍execution治理和L5接线。此次只是接线提案，不扩到业务算法接入或新工作流平台。

## 5. 架构、复用与简洁性评价

已遵守实际职责分析：execution自己的身份绑定、尝试接纳、归档责任和Win32薄适配有业务必要；未发现应以RobWork运动学/动力学代码替换的计算核心。其产品/worker目标零Qt，依赖core/evidence/project，runtime/policy/diagnostics经注入，不应为了调用数量强行引ThreadPool包调度器。

定向核对的上游入口及版本：本报告HEAD工作树中的 `RobWork/RobWork/src/rw/common/{ThreadPool,ThreadTask,ThreadSafeQueue}.hpp`、`rw/core/Event.hpp`、`RobWork/RobWorkSim/src/rwsim/simulator/ThreadSimulator.hpp`。它们分别提供线程池/任务/队列、通用事件、仿真循环；没有execution五元组、取消预算、正式接纳、project归档责任和异步退订drain语义。保留std同步＋Win32适配合理；[检索原件](C:/Users/zgl18/.codex/worktrees/core-review-fixes/RobWork/build/review-execution-20261028/upstream-search.log)。上游代码只读。

正向复用是真实的：core TaskIdentity/AttemptId/TaskState/DomainEvent/ContentDigester；evidence aggregateVerdict、validateCombination、ResultEnvelope::make、judgeCacheHit、judgeCheckpointCompatibility；project IResultArchivePort begin/write/finalize/abandon；runtime编译judge注入面。已区分“调用权威能力”和“正确编排”：调用make不能补偿EXR-01/02，judge存在不能补偿候选未到达EXR-13。

复用矩阵 `reuse/functions/execution.json` 中Scheduler、spawnNewWorker、Admission及多条EventBus仍pending；EventBusContractHeader已business-specific/migrated。pending不等于违规实现已批准，也不意味着整单元已审计通过；本报告不关闭迁移债务。migration §13中REUSE-T23/REUSE-T41及F-964/F-967的实际收口见第8节，不套用旧README“未独验”快照。

保留/删除/适配/owner原则已逐缺陷给出：保留现有单元职责和权威判定，删除错误的“存在即反解”“size即内容”“发ack即完成”等重复近似规则；适配用返回值、正式归档事实和生命周期；不存在需要新算法核心或上游扩展才能修的已确认平台问题。公共格式/线程语义需要裁决的部分明确隔离为提案。

## 6. 文件/功能覆盖与测试有效性

### 静态覆盖

| 文件集合（全部生产有效实现、内联、回调及注册均已读） | 核对重点 |
| --- | --- |
| 公共头 TaskTypes、StateMachine、Scheduler、Controller、WorkerSupervisor、RunRegistry、Ports、EventBus、ChannelProtocol、Checkpoint、CacheCoordinator、Errors（.hpp） | 身份、状态词表、线程/所有权、值与引用、能力/预算、端口责任、inline实现 |
| 实现 TaskTypes、StateMachine、Scheduler、Controller、WorkerSupervisor、RunRegistry、Admission、EventBus、ChannelProtocol、Checkpoint、CacheCoordinator、Errors、Execution（.cpp） | 状态/资源/取消/接纳/归档/缓存及错误路径；Execution为空占位 |
| worker/main.cpp | 握手、脚本替身、命令线程、心跳、seq、临时目录及退出 |
| win32 ProcessLauncher、ChannelPair、JobScope、MemProbe（各.hpp/.cpp） | 路径引用、继承、挂起/Job顺序、I/O、取消、句柄、内存API |
| CMakeLists.txt | 双模式依赖与零Qt、worker与测试目标、GTest配置、testkit仅测试、report会执行测试、编译红线 |

准确计数：12公共头＋13普通cpp＋1worker cpp＋8 Win32文件＋1 CMake＝35。25测试文件的哈希与清单和上述35均在source-inputs.json。测试按实际断言与设施核对，非对测试名称打勾。

### 实际执行

| 项目 | 状态 | passed | skipped | failed | 证据与解释 |
| --- | --- | ---: | ---: | ---: | --- |
| 本C:集成Release构建 execution_test/contract_test及worker | PASS | — | — | — | build.log，native exit0；没有新配置正式CMake |
| sdurws_ird_execution_test | PASS | 160 | 0 | 0 | `sdurws_ird_execution_test.log/.xml/-ird-test-report.json`；38 suites，exit0 |
| sdurws_ird_execution_contract_test | PASS | 38 | 0 | 0 | 对应log/XML/原生JSON；8 suites，exit0，真worker/恢复子进程启动失败会显式失败，无跳过 |
| 隔离探针最终r2 | FAIL（反例） | 1 | 0 | 13 | 14项/6 suites，exit1；黄金Hello＋坏长度通过，其余正确预期与现实现冲突；一个探针可含多个断言，不能将13失败数当缺陷数 |
| 首轮r1隔离探针 | FAIL（历史保留） | 1 | 0 | 9 | 10项；其中IPC探针前置选择错误，已修正，不作为证据；不与r2相加计唯一场景 |
| perf_test、独立冒烟、GUI、全库门禁、修复后回归、独立验收 | NOT RUN | — | — | — | 本轮只评审无生产变更；未发现需要本轮实测的取消时延新结论，未默认重跑perf或无关消费者 |

所有exe使用绝对路径、单独有界启动；不是_report再跑一次。日志中的失败探针不包装成PASS。命中数、原生exit、JSON/XML相互核对；零Qt测试没有使用offscreen或GUI插件。

### 测试有效性与局限

- **TaskStateMachine / IdentityVocabulary**：真实状态矩阵、能力守卫、拒绝反馈和词表测试有价值；不代表Controller与Registry在生产装配中自动同步。REUSE-T23已修的拒绝Completed半写有专门回归。
- **SchedulerEventBus / ResourceGovernance**：通过公开submit/tick测试优先级、项目cap、并发提交和节流；部分派发完成用夹具手工推进状态，未走正式模型准备/登记/launch链。资源失败测试从上层optional注入，漏EXR-10；FIFO按当前契约，不据此增公平性要求。
- **CancelProtocol / PauseResume**：ManualClock、FakeWorker、ResumeGate验证协议状态和2s/10s逻辑，避免sleep断言；不能证明真实PauseAck带已落盘检查点或真实恢复加载。CancelTimingBenchmark静态可见测量请求→Canceling、Canceling→收敛/强杀，真实worker＋驱动poll，非只测发送调用；本轮未执行，不能报实测预算PASS。
- **ChannelProtocol**：有拆包/粘包、magic/version/length、乱序/重复/窗口/缺口、payload守卫；大量encode/decode/fragment往返共用实现，缺跨type/身份、并发seq、最大值和EOF部分消息oracle。新探针包含独立Hello字节和手工坏载荷。
- **RunRegistryAdmission / ArchiveCollaboration / ArchiveFault**：真实ProjectStore隔离写入、跨项目、重投递、重试、丢权限、归档失败仍Completed/缓存跳过均有价值；内存envelope按evidence工厂比较也有效。磁盘检查主要manifest存在/列表/域payload，没重开恢复全envelope或核清单包含所有批次，漏EXR-02；名称测试只断言Admitted/Rejected，漏反解返回值。
- **WorkerProcessContract**：真实进程、管道、Job、崩溃、心跳、seq-gap、父句柄副本、清理失败、正常回收都实际执行。但`PresetDecoder::tryDecode`:264忽略worker canonical bytes，返回标准EvaluationOutput，外加夹具手动装配身份/状态/Admission；这是治理链替身证据。测试没有非法magic导致reader提前return，漏EXR-07；父副本关闭不等于继承白名单。
- **CheckpointCache / CacheJudge**：校验真实临时文件篡改、正式判定函数一致性、模式不升降级、显式评估键和不可续；部分归档为FakeArchiveDisk/手写manifest，不替代project真实写契约。恢复在同一协调器内；部分复用judge在exact key上脚本返回，漏实际候选路径。
- **RecoveryProcess**：确实启动自有子进程、kill/reopen真实存储，验证无manifest不进完整run查询并重建Interrupted；不证明业务恢复或跨会话检查点重建。
- **EventBusDelivery**：并发发布、FIFO、RAII、waitForIdle有确定性同步；原测试没有快照中“尚未开始B”退订或队列积压后新订阅，漏EXR-09/14。
- **MemoryThrottleBenchmark**：静态上真实脚本worker＋生产采样，另腿脚本ratio验证顺序；不是业务算法内存画像；复用同一采样器不能成为计量口径的独立oracle，见C3。本轮NOT RUN。

## 7. 最小修复工作包（按依赖，供直接领取；本轮均未实施）

| 包 | 范围 / 保留、删除、适配与owner | 必要回归 / 准入 / 回滚 |
| --- | --- | --- |
| W1 身份与接纳归一化 | EXR-01/03；只Admission、Checkpoint与相关测试。保留core身份/runtime映射/evidence判定；删除“has_value即反解”，加主键同run校验。execution负责装配 | 两组反例转正式＋真实接纳对端；受影响目标构建、ird_gates、复用凭证；无新公共语义，可先实施。一个局部提交回滚 |
| W2 结果归档完整性 | EXR-02，依W1。先提交C5最小持久化/批次身份提案，明确既有编码或批准缺口；之后只execution编排通过project正式端口写完整envelope及累计清单。不得改TxEngine或业务判定 | 真实重开/解码、重复batch和final冲突、故障重试、缓存只收完整归档；公共格式获批后才能实现，格式版本/旧只读为回滚边界 |
| W3 IPC顺序与绑定 | EXR-04/05；worker、ChannelProtocol、Supervisor阶段守卫。保留现帧字节与RunRegistry；删除无锁seq观测和无条件换绑，按现身份/阶段消费 | 固定交错、手工非法帧、真worker正常/错误退出；不要求业务算法接入。无字段变更可实施，需公共协议字段变更则停在提案；整组同步回滚 |
| W4 进程与事件生命周期 | EXR-07/08/09/11/14；私有Win32、Supervisor、唯一EventBus。保留Job边界/FIFO/UiPlugin接线；补退出恰一次、I/O取消收敛、必要句柄继承和有效订阅位 | 有界自有PID错误/部分创建/断管、handle计数、barrier退订/积压订阅、已有UI drain契约。EventBus在投退订C1先裁决；可先独立落进程清理小提交。不得自称整包独验 |
| W5 暂停/继续闭环 | EXR-06＋C2，依W1/W2/W3/W4。冻结PauseAck的正式确认材料、恢复输入和跨会话重建责任；新attempt/superseded登记与新worker派发成链。保留evidence兼容，execution不重写判定 | 同会话与跨会话真进程/隔离存储、取消优先、损坏/版本错配拒绝、统计不重计；新公共格式/owner变更先批准，不能顺带业务算法接入。整个暂停/恢复协议版本是回滚单位 |
| W6 资源与缓存治理 | EXR-10/12/13，结果字节依W2，模型候选可独立。保留65%/70%、既有evidence/runtime judge，修失采传播、真实大小、内容冲突和候选索引；C3先裁决计量口径 | 正/负采样、零/边界预算、WC部分复用、同键异字节、取消/归档失败不缓存；只因具体计量变化才补perf，禁止默认全库重测。结果/模型缓存可重建，无正式归档回滚 |
| W7 可选简化 | M1~M4逐项批准，最后实施，不成为前六包的前置 | 占位TU删除仅构建/相关结构测试；公共身份helper/正式L5闭环另提案，不扩大既有权限 |

每包同一依赖链一次收口：代码与有效回归稳定后，同步实际变动的逐文件凭证、逐功能证据及契约；仅跑影响目标/消费者和相关门禁，引用本报告仍适用证据。独验由所有者另开会话或人工执行。此表不是新任务状态机，不自动登记任务，不授权新的核心、上游修改、依赖渠道或跨单元owner变化。

## 8. 前序单元改动的实际影响

- **REUSE-T23/F-964**：当前RunRegistry.cpp:317先拒绝`Completed`作为markTerminated原因，再写terminationCause，原历史缺陷已修；独验 `traceability/acceptance/REUSE-T23-20261025-r1.json` reviewed=`d1e416b00433bbd3e62903e583b2ee2b103e0396`、pass（26用例）。本轮没有重报；其窄范围不能背书全部相邻转换/跨组件同步。
- **REUSE-T41/F-967**：生产UiPlugin已换execution总线、做Qt queued转接与关停drain；独验 `REUSE-T41-20261026-r1.json` reviewed=`8c402228b60c23a43b7ed26a2e4e9828e76dabb7`、pass，execution38/UI63；当时GUI端到端NOT RUN/F-974须保留。它没有改EventBus.cpp，EXR-09/14是仍存在的实现边界，不恢复core参考总线。
- **CORE-T12 / 63e2b6305**：参考订阅句柄析构退订、core多字熵播种与variant守卫已在当前代码。execution测试夹具持有RAII句柄，当前160项仍通过；生产总线不回退参考实现。TaskId生成是另一段复制逻辑，未自动享受core播种修复，列M1。core参考实现单线程，不能拿其通过证明异步总线安全。
- **EV-T13 / 4afdc201a**：证据/包络交叉绑定、切片v2身份、缺失证据、缓存有效性、显式requestEvaluatorKey已落实；execution查询/restore实际透传键，当前新增对端测试在160项内通过。无条件NA不能伪造Feasible，已由现有用例验证。保留evidence唯一汇总/工厂，不在本轮复制判定。Completed＋DataInsufficient仍合法。
- **PRJ-T19与POL-T13**：只消费真实project归档端口和policy身份；收口期间POL独验记录新增不改变本轮源码输入。未顺带重审TxEngine；project验证清单内文件不能弥补execution未写完整内容。diagnostics前序消费者证据可引用，不能外推新execution回调/生命周期结论。

## 9. 对下一单元 diagnostics 的必要提醒

本轮不启动其评审。接手时应携带：EXR-01“runtimeName—subject一致性”反例；执行状态/工程判定/当前性/归档轴必须分别呈现；正常用户取消无错误诊断，强杀/崩溃/中断/归档失败不混码；Completed＋ArchiveFailed不能翻译成计算Failed；未知run/错尝试为开发诊断且不得伪造当前项目subject。

diagnostics正式码与文案是权威，execution的admissionDiag及部分reportDev当前含字面文案/通道；P-EX-8已有空sink/注入占位，不能本轮擅自收编。核查PRJ诊断边界的既有GAP-PRJ-DIAG-BOUNDARY-01时沿原裁决，不另造同义码。还需明确sink回调是否允许抛出/重入，避免在execution锁内诊断导致死锁或事件线程终止。只移交具体接口影响，不把本轮疑点冒充diagnostics已确认缺陷。

在所列版本、范围和已执行验证内，未发现其他问题。评审至此停止；未自动修复、未推进下一单元、未自签独立验收。
