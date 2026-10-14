# WP-23-T06 内存与节流规模化基准首测报告（B-05／NFR-PERF-04）

> 基准清单行：performance-baseline §2 B-05（承接任务 WP-23-T06，分支 wp23-t06）。
> 本报告为**真实测量**留痕（协议 §4.8：环境/版本/数据集/样本数/统计约定/原始样本序列必含）——测量数字全部由运行产生，零预填零虚构。B-05 为**内存阈值类判定**（§2 统计口径区分行）——判定统计量＝有效样本最大值（全样本须达标），不涉及分位数统计；报告 JSON 内的 P95/最小值仅为分布参考。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation（ZGL） |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 线程） |
| 内存 | 63.5 GB（物理——MemProbe querySystemMemory 实测分母，与 BIOS 标称一致） |
| 存储 | SK hynix PC801 HFS002TEJ9X101N 2TB NVMe SSD（D: 仓库所在盘） |
| 操作系统 | Windows 11 专业版，10.0.26200 |
| 电源策略 | HP Optimized (Modern Standby)（`powercfg /getactivescheme` 实录） |
| 后台进程基线 | 常规开发工作站会话；测量期间每样本一个独立 worker 进程顺序启动/终结，无并行基准（协议 §4 测量纪律——串行跑分，本卡测量按队列独占执行） |
| 软件版本 | 分支 wp23-t06；commit `11e0cc69`（B-05 基准测量目标落位提交——报告 JSON gitCommit 字段锚）；构建配置 Release（MSVC 2022 x64，冒烟模式独立配置） |
| 测量时点 | 2026-10-14（证据目录生成时间戳为准） |

同一轮基准的全部样本在同一台机器、同一硬件配置、同一软件版本上采集（协议 §4.1——零跨机样本）。

## 2. 数据集与驱动编排（performance-baseline §5.5 挂点）

- **数据集**：`ird-perf-b05-memory@1`（manifest＝`industrialrobot/testdata/performance/b05-memory-manifest.json`，2026-10-14 冻结）。
- **驱动编排**（被测对象＝EX-T07 资源治理生产采样链的真实装配）：真实 `WorkerSupervisor`＋真实独立 worker 进程（`sdurws_ird_execution_worker`，阶段 A 内置脚本替身评估器——execution.md §3.4 口径）＋生产采样链 `ResourceController`＋`SupervisorMemorySampler`（MemProbe 三探针〔GlobalMemoryStatusEx 系统／GetProcessMemoryInfo 主进程工作集〕＋监督器作业内存合计〔QueryInformationJobObject PeakJobMemoryUsed〕——EX-T07 §6.6/§15.4 v0.9 落位件）；驱动线程（本基准的"调度线程"）以 10 ms 周期 `supervisor.poll()＋rc.evaluate()`——采样与决策同在调度域（§6.2"决策仍由调度线程执行"约束的满足位），`Config::sampleInterval=10 ms`（实现参数，manifest 冻结）。
- **装配同步锚点＝DispatchAccepted 事件**（WP-23-T03 同款——status 相位 Running 在派发请求"发出"即置位，早于受理，不作锚点）。
- **大任务脚本**（manifest 冻结）：`stage-a-script:v1 / checkpoint 1 / batches 100000 / checkpoint 2 / final`——大任务管线三机制：检查点×2 经 CheckpointCoordinator 真实落盘（归档端口＝真实写盘测试面替身 DiskArchivePort）、ResultBatch 10 万条流式回传、FinalOutput 正常终结（退出码 0）。批量取 10 万＝批次相位运行窗 ~0.3 s，对 10 ms 采样间隔即 10~17 个采样点/样本（实测），对覆盖下限 5 有 ≥2× 余量。
- **样本计划**：1 次预热（剔除）＋15 有效样本；逐样本 fresh worker 进程（独立 Job Scope——作业峰值不跨样本累积）。协议 §4.2 的"每组有效样本 ≥15"系 P95 分组下限，本基准按同数自律取 15（WP-23-T02/T03 先例）。

## 3. 测量口径（逐条对应 NFR-PERF-04 原文）

| 条款 | 测量对象 | 阈值（出处） | 判定面 |
| --- | --- | --- | --- |
| 合计峰值内存 | 主进程工作集＋全部 worker 作业内存（§4.7 合计口径——各半区独立计量后逐采样点汇总，取样本内最大） | ≤70% 物理内存（REQUIREMENTS NFR-PERF-04 原文——零私改） | 腿一逐有效样本阈值判定（判定统计量＝有效样本最大值） |
| 大任务机制面 | 分批（批次计数恰 100000）／流式（批次先于 FinalOutput 到达）／检查点（×2 经协调器发布且磁盘 manifest＋批次文件留存） | 行为判定（无数值阈值） | 腿一逐样本断言（15/15 成立） |
| 先节流后诊断时序 | 决策序列迹（脚本占比采样器九步攀升剖面——0.60→0.649→0.650→0.699→0.700→0.710→0.690→0.640→0.705） | 节流 65%（units/execution.md §15.1 D-05 实现参数）；诊断 70%（NFR-PERF-04 上游值） | 腿二决策迹判定（非时延测量）：首次 Throttled 严格先于首次诊断；诊断只在进入 Exhausted 的边沿恰一次每回合；三段 ≥ 边界逐格；恢复撤销停派发；复耗尽新边沿 |

**保守口径登记（§4.7"采样须覆盖工作进程峰值并存时刻"的承载）**：worker 半区取 `PeakJobMemoryUsed`（作业创建以来提交内存峰值——单调不回落），主进程半区取采样序列最大工作集——两半区峰值均为"期间最大"，合计占比不低估任何半区的峰值贡献；且两半区在**同一采样时刻**读取后逐点汇总（SupervisorMemorySampler 单点组装），并存时刻由构造保证。

**阈值双钉**：腿二用例内 `EXPECT_DOUBLE_EQ(ResourceBudget{}.maxMemoryRatio, 0.70)`（NFR-PERF-04 原文）／`EXPECT_DOUBLE_EQ(ResourceBudget{}.throttleRatio, 0.65)`（D-05 实现参数）——判定与生产默认同源，杜绝抄写漂移。

## 4. 替身边界与等价口径（如实申报）

1. **worker 评估器＝阶段 A 脚本替身**（execution.md §3.4 登记形态）：本基准证明的是"合计计量口径覆盖主进程＋全部工作进程并存时刻"＋"分批/流式/检查点机制在大任务管线真实发生"，**不是业务评估算法的内存画像**（替身评估器的稳态内存 ~1.3 MB/worker）。业务评估器的大内存场景归**集成模式扩展测量挂点**（§7——集成构建由合入批执行；届时以真实评估器管线复测本基准，或按 manifest 升级规则换版扩登场景）。
2. **归档端口＝真实写盘的测试面替身**（DiskArchivePort，T03 同形）：检查点产物面真实在盘；project 存储白名单校验语义归 project 契约测试面（EX-T04/PRJ-T14 覆盖），不在 B-05 测量面。
3. **腿二采样器＝脚本占比采样器**（units/execution.md §11 登记的 IMemorySampler 注入面——EX-RES-2 功能面同源接缝）：把本进程组合真实推至物理内存 65%/70% 需分配数 GB 量级——侵入宿主机、与串行测量纪律冲突且不可控，故时序语义腿以接缝注入驱动；其**真实面**＝分母取自 MemProbe 真实探针（本机 63.5 GB 实测）＋决策对象为生产判定本体（真实 DiagnosticRecord 构造与 §3.3 sink 出具）＋字节定标整数化（below=floor／at-above=ceil——防 `(r×T)/T` 浮点舍入翻转边界，任意真实 T 下确定可复现）。与 EX-RES-2 的分工：功能面（ManualClock 虚拟时钟证正确性）已由 EX-RES-2 承载，本腿补时序语义的决策迹证据——两者互补不互替。
4. **NFR-PERF-04 原文的"缓存"机制面**＝execution 缓存协调器（EX-T08 EX-CCH-1/2 用例）与 optimization 消费面承载，随同套件 ctest 全绿佐证（本次 157/157＋38/38 含之）——DTB §2.24 本卡验收行明文为"大任务分批/流式/检查点＋先节流后诊断"，与腿一场景面一致，本基准零缓存机制新面（如实申报，不越界）。
5. **单进程单任务形态**：每样本一个独立 worker 进程＋一个大任务（样本间零共享状态）；"多 worker 并发内存合计"的汇总能力由 `aggregateJobMemoryBytes`（全部活 worker 求和）承载并于同目标契约测试 EX-T07 增用例（真进程回池→合计非零→回收归零）覆盖——多 worker 并发场景的内存画像归集成模式扩展测量挂点。

## 5. 首测结果（真实测量——2026-10-14）

### 5.1 腿一：大任务合计峰值内存（用例 LargeTaskBatchStreamCheckpointPeak_WP23T06_B05——全绿，5338 ms）

| 组 | 有效/无效 | P95 | **max（判定统计量）** | min | 阈值 | 判定 |
| --- | --- | --- | --- | --- | --- | --- |
| combined-peak-memory-ratio | 15/0 | 0.000193 | **0.000194** | 0.000176 | 0.70 | ✓（余量 ~3600×） |

判定统计量为**合计峰值占物理内存比例**（无量纲——报告 JSON 的 durationMs/maxValidMs 字段沿夹具 schema 的登记性复用，notes 已声明口径）。逐样本原始序列（main/worker/aggregate 峰值单位 MB，total 单位 GB，readings＝采样点数，level＝0 即 Normal）：

```
sample#1  main=10.2 worker=1.3 agg=11.5 total=63.5 ratio=0.000176 readings=15 level=0
sample#2  main=10.4 worker=1.3 agg=11.7 total=63.5 ratio=0.000180 readings=13 level=0
sample#3  main=10.2 worker=1.3 agg=11.5 total=63.5 ratio=0.000177 readings=16 level=0
sample#4  main=10.1 worker=1.3 agg=11.4 total=63.5 ratio=0.000176 readings=17 level=0
sample#5  main=10.3 worker=1.3 agg=11.6 total=63.5 ratio=0.000178 readings=13 level=0
sample#6  main=10.5 worker=1.3 agg=11.8 total=63.5 ratio=0.000181 readings=16 level=0
sample#7  main=10.6 worker=1.3 agg=11.9 total=63.5 ratio=0.000183 readings=16 level=0
sample#8  main=10.6 worker=1.3 agg=11.9 total=63.5 ratio=0.000183 readings=16 level=0
sample#9  main=11.1 worker=1.3 agg=12.4 total=63.5 ratio=0.000191 readings=15 level=0
sample#10 main=10.9 worker=1.3 agg=12.2 total=63.5 ratio=0.000187 readings=15 level=0
sample#11 main=10.9 worker=1.3 agg=12.2 total=63.5 ratio=0.000188 readings=15 level=0
sample#12 main=10.6 worker=1.3 agg=11.9 total=63.5 ratio=0.000183 readings=12 level=0
sample#13 main=11.0 worker=1.3 agg=12.3 total=63.5 ratio=0.000188 readings=14 level=0
sample#14 main=11.3 worker=1.3 agg=12.6 total=63.5 ratio=0.000194 readings=14 level=0
sample#15 main=11.2 worker=1.3 agg=12.5 total=63.5 ratio=0.000192 readings=10 level=0
```

预热腿（剔除不计入判定）实测 peakRatio≈0.000176。15/15 有效样本零无效；行为面 15/15 全数成立：批次流恰 100000 条、批次先于 FinalOutput 到达（流式回传到达序）、检查点×2 经 CheckpointCoordinator 发布且磁盘 manifest＋part-000.bin 留存、worker 退出码 0（NormalCompletion）、治理层全程 Normal、零 EX-RESOURCE-INSUFFICIENT（本进程组合未触达 65%/70%——健康机器预期面）。

### 5.2 腿二：先节流后诊断时序语义（用例 ThrottleBeforeDiagnosticSequence_WP23T06_B05——全绿，0 ms）

决策迹九拍全过（真实探针分母 T＝68,205,227,328 字节；名义占比→字节定标 below=floor／at-above=ceil）：

| tick | 名义占比 | 重算占比 | 层级 | pauseDispatch | reclaimIdle | 诊断边沿 |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 0.600 | 0.600000 | Normal | 0 | 0 | 0 |
| 1 | 0.649 | 0.649000 | Normal | 0 | 0 | 0 |
| 2 | 0.650 | 0.650000 | **Throttled（首次节流）** | 1 | 1 | 0 |
| 3 | 0.699 | 0.699000 | Throttled（节流带内零诊断） | 1 | 1 | 0 |
| 4 | 0.700 | 0.700000 | **Exhausted（首次诊断）** | 1 | 1 | 1 |
| 5 | 0.710 | 0.710000 | Exhausted（持续超限零重复） | 1 | 1 | 0 |
| 6 | 0.690 | 0.690000 | Throttled（退出耗尽无诊断） | 1 | 1 | 0 |
| 7 | 0.640 | 0.640000 | Normal（恢复——两标志撤销） | 0 | 0 | 0 |
| 8 | 0.705 | 0.705000 | Exhausted（复耗尽＝新边沿） | 1 | 1 | 1 |

时序不变量逐项成立：①首次节流 tick#2 **严格先于**首次诊断 tick#4（"接近上限先节流再给资源不足诊断"——§6.1「仍」字语义）；②三段 ≥ 边界逐格命中（0.649 Normal／0.650 Throttled／0.699 Throttled／0.700 Exhausted）；③诊断恰 2 次（两个耗尽回合各恰一次）且经 §3.3 注入 sink 外报恰 2 次；④诊断比较型三要素完整（code=EX-RESOURCE-INSUFFICIENT、context=execution/resource、expected=0.70、单位 "1"、cause/recommendedAction 非空）；⑤ResourceBudget 消费默认值双钉（0.70/0.65）；⑥真实采样次数＝9（sampleInterval=0 逐拍语义）。

**判定结论：B-05 达标**——腿一合计峰值内存阈值判定全样本通过（max 占比 0.000194 ≤ 0.70，余量 ~3600×）＋大任务分批/流式/检查点行为面 15/15 成立；腿二先节流后诊断时序语义决策迹全过。

## 6. 实施期排障记录（如实登记——两处实施期迭代，均发生在参数冻结前）

1. **采样覆盖度迭代**（数据集参数定标）：初稿批量 20,000 条的批次相位仅 ~40 ms，10 ms 采样间隔下每样本仅 4~5 个采样点，触发覆盖下限（≥5）断言（2/16 样本越界）——峰值读数覆盖不足属测量设计缺陷非产品缺陷。修复＝批量 20,000→100,000（运行窗 ~0.3 s，实测 10~17 点/样本）。**冻结前修正**：committed 数据集 v1 即修正后参数；未产出任何被登记的达标测量于修正前参数（失败运行仅存在于实施期工作树，未提交未冻结）。
2. **报告 gitCommit 锚修正**：首次全绿运行误设 `IRD_PERF_GIT_COMMIT=59b3abdb`（契约编译放行提交）——测量二进制实为 11e0cc69 工作树产物，锚与内容不一致。修复＝以正确锚 `11e0cc69` 重跑全套件（4/4 全绿）——**本报告登记的即 11e0cc69 锚复跑的报告 JSON**（`_m6-evidence/perf-reports/` 现存版本），59b3abdb 锚的过渡产物已被覆盖，如实留痕于直跑日志。
3. 两处迭代均属测量编排面；被测产品代码（EX-T07 采样链/治理本体）与协议阈值全程零变更。

## 7. 开放项与挂点

- **集成模式复测挂点**：本首测在冒烟模式独立构建树执行（Release）；集成构建（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON）**由合入批执行**，复测命令＝§2 复现命令位的集成形态（ctest 路径随集成布局）。业务评估器大内存场景与多 worker 并发画像的扩展测量同挂本点（§4.1/§4.5 边界申报）。
- **ird_gates 直跑核定**：实施段未直跑门禁引擎（冒烟构建树不含全量目标面）——预期命中集登记册＝`traceability/wp23-t06-gate-registrations.md`（**预期恰增＝零**——本批零新增目标/链接边），双态直跑核定随合入批/治理面补做（WP-23-T03 先例口径）。
- **判定阈值边界重申（P-04 部分冻结现状）**：本卡达标面数值＝70%（NFR-PERF-04 原文）＋65%（D-05 实现参数），与 P-04 扰动/鲁棒性协议的判定阈值（通过线/降级线——所有者裁定维持待定、未补充确认前按未冻结处理）**零交集**；本基准零鲁棒性判定结论、零 P-04 数值私设（REQUIREMENTS 附录 C《P-04 冻结数值表》2026-10-13 现状）。
- **样本量口径**：15 有效样本为阈值类自律下限（协议 §4.2 的 ≥15 系 P95 分组特有）；如验收者裁定需加大样本量或加多 worker 并发场景，按 manifest 升级规则换版（不得原地改写）。

## 8. 证据清单（build/wp23-t06-wt/_m6-evidence/）

| 文件 | 内容 |
| --- | --- |
| `perf-reports/b05-large-task-memory.json` | 腿一会话报告（allPassed=true，gitCommit=11e0cc69，dataset ird-perf-b05-memory@1；15 样本原始序列＋逐样本字节明细 notes＋verdicts） |
| `perf-reports/b05-throttle-ordering.json` | 腿二会话报告（allPassed=true；决策迹九拍 notes＋双 verdicts＋阈值出处登记） |
| `perf-reports/b02-cancel-normalsettle.json`／`b02-cancel-timeout-kill.json` | 同目标 B-02 存量用例随目标全绿运行的报告（本卡零触碰其协议面——回归佐证） |
| `gtest-reports/sdurws_ird_execution_perf_test.xml` | 基准套件 gtest XML（4/4，零失败） |
| `gtest-reports/sdurws_ird_execution_test.xml` | 功能套件 gtest XML（157/157，零失败——与 wp21-t07 基线逐数一致） |
| `gtest-reports/sdurws_ird_execution_contract_test.xml` | 契约套件 gtest XML（38/38，零失败） |
| `logs/execution_perf_test-direct.log` | 首测直跑全量日志（stderr 实测打点＋四用例 PASS） |
| `logs/execution_test-direct.log`／`logs/execution_contract_test-direct.log` | 功能/契约套件直跑日志 |
| `logs/ctest-execution-all.log` | ctest 复现命令位执行日志（100% passed，3/3，43.28 s） |
