# WP-23-T03 取消时序基准首测报告（B-02／NFR-PERF-02）

> 基准清单行：performance-baseline §2 B-02（承接任务 WP-23-T03，分支 wp23-t03）。
> 本报告为**真实测量**留痕（协议 §4.8：环境/版本/数据集/样本数/统计约定/原始样本序列必含）——测量数字全部由运行产生，零预填零虚构。B-02 为**时序阈值类判定**（§2 统计口径区分行）——判定统计量＝有效样本最大值（全样本须达标），不涉及分位数统计；报告 JSON 内的 P95/最小值仅为分布参考。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation（ZGL） |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 线程） |
| 内存 | 63.5 GB（物理） |
| 存储 | SK hynix PC801 2TB NVMe SSD（D: 仓库所在盘） |
| 操作系统 | Windows 11 专业版，10.0.26200 |
| 电源策略 | HP Optimized (Modern Standby)（`powercfg /getactivescheme` 实录） |
| 后台进程基线 | 常规开发工作站会话；测量期间每样本一个独立 worker 进程顺序启动/终结，无并行基准 |
| 软件版本 | 分支 wp23-t03；测量可执行文件＝**v2 数据集工作树产物**（＝登记批提交内容——相对 `3644d8dc`〔B-02 基准测量目标落位提交，报告 JSON gitCommit 字段锚〕的差量仅为数据集 v2 换版＋§6 两处测量修正＋文档登记，协议面零变更）；构建配置 Release（MSVC 2022 x64，冒烟模式独立配置） |
| 测量时点 | 2026-10-13（证据目录生成时间戳为准） |

同一轮基准的全部样本在同一台机器、同一硬件配置、同一软件版本上采集（协议 §4.1——零跨机样本）。

## 2. 数据集与驱动编排（performance-baseline §5.2 挂点）

- **数据集**：`ird-perf-b02-cancel@2`（manifest＝`industrialrobot/testdata/performance/b02-cancel-manifest.json`，2026-10-13 冻结；**history 留痕**：v1 的 normalSettleScript 批次量 5×10⁶ 在首次测量前即发现缺陷——批次洪流把 WorkerExited 事件积压到退出观测窗之外，未产出任何达标测量即换版作废；v2 批次量 5×10⁵，对样本周期余量 ≥8×，**协议阈值零变更**）。
- **驱动编排**（被测对象＝EX-T03 取消协议本体的真实装配）：`TaskController`（缺省 steady_clock——**真实墙钟**）＋`WorkerSupervisor`＋真实独立 worker 进程（`sdurws_ird_execution_worker`，阶段 A 内置脚本替身评估器——execution.md §3.4 口径）；驱动线程（本基准的"调度线程"）以 10 ms 周期 `controller.poll()＋supervisor.poll()`；worker 句柄经 `WorkerSupervisor::workerHandle` 适配器交 `TaskController::bindWorker`（§10.3 IWorkerHandle 接缝的生产装配形态）。
- **装配同步锚点＝DispatchAccepted 事件**：status 相位 Running 在派发请求"发出"即置位（实测早于受理帧 ~70 ms）——以相位为锚会让取消请求竞态打到尚未受理派发的 worker（CancelRequest 落在派发等待段被丢弃，测得的是丢弃伪影而非协议时序；实施期实证，见 §6 排障记录）。以受理事件为锚后，取消请求的 earliest 前提＝worker 已在脚本内（超时腿已进入 hang 段）。
- **脚本形态**（manifest 冻结）：正常腿 `stage-a-script:v1 / batches 500000`（流式批次——测量窗内持续 Running，协作取消在批次边界收敛：CancelAck→退出码 20）；超时腿 `stage-a-script:v1 / checkpoint 1 / heartbeat-off / hang`（先写 1 个检查点→关心跳→静默挂起永不收敛——驱动 10 s 协作窗耗尽→强杀路径；checkpoint 1 提供第三腿的"最近检查点"）。
- **样本计划**：正常收敛腿＝1 次预热（剔除）＋15 有效样本；超时强杀腿＝3 次有效场景（阈值类无统计预处理——不剔除任何样本）。协议 §4.2 的"每组有效样本 ≥15"系 P95 分组下限，本基准按同数自律取 15（正常腿）。
- **时点约定**：时点＝驱动线程内事件到达时刻（steady_clock::now()）——状态机事件在 poll 内同步发布，观测滞后 ≤1 个 poll 周期（10 ms，保守方向：实测值 ≥ 真实转移时刻，不虚低）。

## 3. 测量口径（三条款逐条对应 NFR-PERF-02 原文）

| 条款 | 测量区间 | 阈值（REQUIREMENTS 原文） | 判定面 |
| --- | --- | --- | --- |
| 1 | 取消请求受理 → 进入 Canceling（＋IDispatchGate 停派发回调） | ≤2 s | 正常腿逐样本 + 超时腿逐场景 |
| 2 | 进入 Canceling → 协作收敛 Canceled（CancelAck 帧） | ≤10 s | 正常腿逐样本 |
| 3 | 10 s 收敛窗耗尽 → 强杀序列（T13）→ 进程树终止 | 行为判定（非毫秒阈值） | 超时腿逐场景四断言：EX-FORCE-TERMINATED＋TerminationCause::ForceTerminated／worker 进程 OS 级死亡（PID 探测）／检查点留存（磁盘重读字节一致＋computePayloadDigest 复合摘要＝发布摘要） |

附加证据面：正常腿逐样本断言 CancelAck 帧到达、任务诊断空（UX-03——正常取消不是错误）、退出分类＝协作取消（码 20）；超时腿断言 CancelAck **未**到达（卡死脚本不应收敛）、退出分类＝ForceTerminatedBySupervisor。

## 4. 替身边界与等价口径（如实申报）

1. **worker 评估器＝阶段 A 脚本替身**（execution.md §3.4 登记形态）：本基准证明的是"取消协议对真实进程/真实通道的时序"，不是业务评估算法的时序。业务评估器的批次边界协作形态与脚本替身同构（协作点轮询 ICancelSignal——execution.md §7.1 信号传递链）；批次收敛时延的量级由通道往返主导（实测 ms 级），与评估计算量无关的部分即协议编排时序。
2. **归档端口＝真实写盘的测试面替身**（DiskArchivePort）：检查点批次字节与 manifest.json 真实落盘、强杀后真实留存并重读核对——产物面真实；project 存储白名单校验语义归 project 契约测试面（EX-T04/PRJ-T14 覆盖），不在 B-02 测量面（PA-1 不伪造第二持久化路径——本替身是 CheckpointCacheTest FakeArchiveDisk 同形的测试面设施）。
3. **与功能用例的分工**：`CancelProtocolTest`（EX-T03）以 ManualClock 虚拟时钟证协议**正确性**（testkit §6.5 纪律——正确性判据禁止 sleep）；本基准以真实墙钟证**时序达标**（NFR-PERF-02 是性能需求——数字必须真实产生）。两者互补不互替，零重复。
4. **单进程单任务形态**：每样本一个独立 worker 进程＋一个受管任务（样本间零共享状态）；"多任务并发取消"不在 B-02 口径内（需求原文为单任务取消协议时序；并发面归 NFR-PERF-05 吞吐基准）。

## 5. 首测结果（真实测量——2026-10-13）

### 5.1 正常收敛腿（用例 NormalCancelSettleRealWorkerThresholds_WP23T03_B02——全绿，2822 ms）

| 组 | 有效/无效 | P95 (ms) | **max (ms)（判定统计量）** | min (ms) | 阈值 (ms) | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| cancel-to-canceling（条款 1） | 15/0 | 23.19 | **24.60** | 10.46 | 2000 | ✓（余量 81×） |
| canceling-to-settled（条款 2） | 15/0 | 17.95 | **18.72** | 12.40 | 10000 | ✓（余量 534×） |

预热腿（剔除不计入判定）实测 d1=24.78 ms、d2b=14.12 ms。15/15 有效样本零无效；CancelAck 帧到达 15/15；任务诊断空 15/15（UX-03）；退出事件落地窗内到达 14/15——到达样本退出分类＝协作取消（码 20）全数成立，错过窗的 1 样本其退出分类断言按 exitSeen 前提跳过（不虚判，§6 排障记录第 2 条同源纪律；协议面判定以 CancelAck＋Canceled 为准，不受影响）。

### 5.2 超时强杀腿（用例 TimeoutForceKillRetainsCheckpoint_WP23T03_B02——全绿，30466 ms）

| 场景 | d1 (ms)（条款 1 同判） | 强杀完成 (ms)（自入 Canceling 起，含 10 s 窗） | 超出窗部分 (ms) | 进程死亡（OS 级） | 检查点留存 |
| --- | --- | --- | --- | --- | --- |
| #1 | 13.71 | 10008.33 | 8.33 | ✓ | ✓（字节一致＋摘要相符） |
| #2 | 14.59 | 10000.71 | 0.71 | ✓ | ✓ |
| #3 | 12.76 | 10002.43 | 2.43 | ✓ | ✓ |

三场景逐项：终态 Failed＋TerminationCause::ForceTerminated＋EX-FORCE-TERMINATED（不伪装普通失败）；CancelAck 未到达（卡死脚本未收敛——10 s 窗真实耗尽）；退出分类＝ForceTerminatedBySupervisor；归档 manifest＋part-000.bin 强杀后在盘且磁盘重读复合摘要＝发布摘要（可续跑的物质基础）。强杀序列真实开销＝0.7~8.3 ms（10 s 协议窗之外），worker PID 逐场景实录于报告 JSON notes。

**判定结论：B-02 达标**——条款 1/2 全样本阈值判定通过（余量 ≥81×），条款 3 三场景行为判定全成立。

## 6. 实施期排障记录（如实登记——两处测量设计缺陷与修复）

1. **装配同步锚点缺陷**（v1 期间发现）：以 status 相位 Running 为装配完成锚点会使取消请求竞态打到尚未受理派发的 worker——CancelRequest 落在派发等待段被 worker 丢弃（伪影 A：永不收敛）或恰在脚本首协作点被消费（伪影 B：卡死脚本反而协作退出）。修复＝锚点改 DispatchAccepted 事件（受理帧处理事件），manifest v2 登记。
2. **批次洪流的退出事件积压**（v1 首跑发现）：5×10⁶ 批次的 ResultBatch 流把 WorkerExited 事件积压到退出观测窗（5 s）之外——协议面 CancelAck/Canceled 真实发生（阈值判定不受影响），仅进程退出证据面失真。修复＝批次量降至 5×10⁵（v2，余量 ≥8×）＋退出分类断言以 exitSeen 为前提（错过观测窗不虚判，样本计数入报告 notes）。
3. 两处缺陷均未产出任何被登记的达标测量（v1 未产出通过报告）；上表数字全部来自 v2 数据集的首次完整测量。

## 7. 开放项与挂点

- **集成模式复测挂点**：本首测在冒烟模式独立构建树执行（Release）；集成构建（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON）**由合入批执行**，复测命令＝§2 复现命令位的集成形态（ctest 路径随集成布局）。
- **ird_gates 直跑核定**：实施段未直跑门禁引擎（冒烟构建树不含全量目标面）——预期命中集登记册＝`traceability/wp23-t03-gate-registrations.md`，双态直跑核定随合入批/治理面补做（WP-23-T02 先例口径）。
- **协议常量双钉**：2 s/10 s 以 static_assert 钉住 `TaskController::kCancelAcceptWindow/kCancelCooperativeWindow`（上游需求值不改动）——基准读数与被测协议同源常量，杜绝抄写漂移。
- **样本量口径**：正常腿 15 有效样本为阈值类自律下限（协议 §4.2 的 ≥15 系 P95 分组特有）；如验收者裁定需加大样本量，按 manifest 升级规则换版（不得原地改写）。

## 8. 证据清单（build/wp23-t03-wt/_m5-evidence/）

| 文件 | 内容 |
| --- | --- |
| `perf-reports/b02-cancel-normalsettle.json` | 正常腿会话报告（allPassed=true，gitCommit=3644d8dc，dataset ird-perf-b02-cancel@2；两组原始样本序列全量＋verdicts） |
| `perf-reports/b02-cancel-timeout-kill.json` | 超时腿会话报告（allPassed=true；逐场景证据 notes 含 worker PID 与强杀时序） |
| `sdurws_ird_execution_perf_test.xml` | 基准套件 gtest XML（2/2，零失败） |
| `sdurws_ird_execution_test.xml` | 功能套件 gtest XML（151/151，零失败） |
| `sdurws_ird_execution_contract_test.xml` | 契约套件 gtest XML（38/38，零失败） |
| `perf-first-run.log` | 首测直跑全量日志（stderr 实测打点＋两用例 PASS） |
| `perf-ctest-run.log` | ctest 复现命令位执行日志（100% passed，38.17 s） |
