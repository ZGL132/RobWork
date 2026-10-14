# WP-23-T08 优化运行时限基准首测报告（B-07／NFR-PERF-06）

> 基准清单行：performance-baseline §2 B-07（承接任务 WP-23-T08，分支 wp23-t08）。
> 本报告为**真实测量**留痕（协议 §4.8：环境/版本/数据集/样本数/统计约定/原始样本序列必含）——测量数字全部由运行产生，零预填零虚构。B-07 为**时序/规模阈值类判定**（§2 统计口径区分行）——时限判定统计量＝有效样本运行墙钟**最大值** ≤28,800,000 ms（8 小时，NFR-PERF-06 原文）；运行期交互抽查的 P95 判定属 **NFR-PERF-01 自身口径**（线性插值分位数，禁 3 次中位数替代）——两套口径各归其位，不互相换轨；报告 JSON 内的非判定统计量仅为分布参考。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation（ZGL） |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 线程——8 性能核＋16 能效核；8 worker 线程＋驱动线程＋常驻交互采样线程并存） |
| 内存 | 63.5 GB（物理——Win32_OperatingSystem TotalVisibleMemorySize 66,634,028 KB 实录） |
| 存储 | SK hynix PC801 HFS002TEJ9X101N 2TB NVMe SSD（D: 仓库所在盘） |
| 操作系统 | Windows 11 专业版，10.0.26200 |
| 电源策略 | HP Optimized (Modern Standby)（`powercfg /getactivescheme` 实录） |
| 后台进程基线 | 常规开发工作站会话；测量期间单进程单测试串行执行（run 内部 8 worker 并行＋常驻交互采样线程除外——采样线程是测量面本体），无并行基准（协议 §4 测量纪律——串行跑分，本卡测量按队列独占执行） |
| 软件版本 | 分支 wp23-t08；commit `5f6d7e9f88f143527d30ff91d01fee70abc82d00`（B-07 基准测量增列提交——报告 JSON gitCommit 字段锚）；构建配置 Release（MSVC 2022 x64，冒烟模式独立配置——vcpkg toolchain＋Qt 6.11.1 msvc2022_64 前缀） |
| 测量时点 | 2026-10-15（证据目录生成时间戳 01:33~01:36 为准） |

同一轮基准的全部样本在同一台机器、同一硬件配置、同一软件版本上采集（协议 §4.1——零跨机样本）；时限腿 16 次运行使用同一候选集、同一进程内二进制、同一 fixture 装配形态（§4.7 端到端墙钟口径）。

## 2. 数据集与驱动编排（performance-baseline §5.7 挂点）

- **数据集**：`ird-perf-b07-opt-run-time@1`（manifest＝`industrialrobot/testdata/performance/b07-opt-run-time-manifest.json`，2026-10-15 冻结；§4.7"运行时限/检查点恢复"口径的参数冻结位）。
- **被测对象**（optimization.md §9.2 规模化口径锚"达标归 WP-23 对应基准任务"的本卡承接位）：WP-21-T04 交付的 `JointRunOrchestrator` 两级批量编排（§8.5 外层两级批量＋§9.2 并行分片/检查点域中间状态/恢复消费）＋WP-21-T07 暂停面接缝位（本基准未触发暂停——恢复腿走协作取消截停，OPT-VER-223 同款路径）。
- **标准运行**（manifest 冻结）：seeded-lhs 策略（`generateStageDJointCandidates` 单点——基线恒批首＋CandidateId 去重）×单连续绑定 `mdl.drivetrain.ratio[1]`（无量纲，域 [0.8,1.2]）×seed 7×maxCandidates 10,000×maxVerifiedCandidates 100——**需求原文规模"10,000 Quick＋100 Verified"照抄零私改**；去重后候选数恒 10,100；批划分＝Quick 2,500 批＋Verified 25 批（批容量 4）；threadCount=8（R2 生产形态——§9.2/§9.4 并行分片入 config.opt 身份；需求未规定标准基准线程数，本值为本卡登记并冻结的生成器参数）。
- **工作量替身**：内层四域评估器＝确定性浮点迭代链替身——每派发 400,000 次串行 FP 乘加依赖链（与 B-06 数据集冻结工作量同源；单候选六层派发，单次全量运行恒 60,600 次派发；结果 IEEE-754 位模式沉入进程级原子汇防消除）。指标投影＝P-OPT-2 测试面替身（knownPitfalls 登记缝）。
- **运行期交互抽查**：常驻独立采样线程＝"UI 线程测试形态"（B-01 同款五操作无头模型层驱动——StageNavigationModel/SelectionService/ProjectTreeModel/ParamEditModel 产品模型本体，ui 侧数据集＝b01-interaction@1 **同参复刻**：1,000 对象/100 修订语境/五分组 10/420/380/90/100/表单 24 字段/筛选词 obj-007/选中批量 50——非同库混用，本数据集自持全量参数冻结）；驱动线程在 `run()` 前后置位/复位运行窗旗标，**仅开始与结束都落在运行窗内的样本计入统计**（"运行期间"语义的门控承载）；节奏＝每轮五操作后静默 50 ms（真实用户交互间隙形态——单运行窗每操作有效样本 ~150~200 量级，单窗独立满足协议 §4.2 每组 ≥15）；逐操作 1 次预热剔除。
- **恢复腿三段**：参照全量运行（AT-35 审计与结论面底座）→ 协作取消在 Quick 相约半程截停（CancelAfterNContext 层查询计数 31,000——取消查询＝每候选六层起点各一次＋每批领取一次；多 worker 在途批使实际截停点有 ±8 候选级漂移，断言面不依赖精确半程）→ 检查点域中间状态经**真 execution::CheckpointCoordinator** 写出真实磁盘文件（宿主角色归档端口替身——N8 磁盘编址/manifest/摘要全归 execution）→ **同版本** restore（requestContractVersion＝kOptJointFullContractVersion=1）→ 磁盘 `joint-state.bin` 装载→`decodeJointCheckpointState` round-trip → 新 AttemptId（+1）续跑至 Completed。
- **样本计划**：时限腿 1 次预热（剔除）＋15 有效样本零无效；交互抽查逐操作 1 次预热＋运行窗内有效样本（≥15 下限）零无效；恢复腿为单次行为判定用例（非统计采样）。

## 3. 测量口径（逐条对应 NFR-PERF-06/NFR-PERF-01 原文）

| 条款 | 测量对象 | 阈值（出处） | 判定面 |
| --- | --- | --- | --- |
| 基准时限（"8 小时内结束"） | 每有效样本＝`JointRunOrchestrator::run()` 全程端到端墙钟（steady_clock）——候选集构造/快照组装/注册表装配/编排器构造在区间外（§4.7"8 小时为端到端墙钟时限"）；判定统计量＝有效样本最大值（B-07 时序阈值类——T03/T06 阈值类先例同口径，非 P95 非中位数） | ≤28,800,000 ms（REQUIREMENTS NFR-PERF-06 原文——零私改） | 腿一 verdict[0]（9,301.0 ms ≤ 28,800,000 ms——pass） |
| 运行期间满足 NFR-PERF-01 | 运行窗内五类交互（导航/选择/筛选/编辑/视图切换）端到端响应时长（B-01 同款无头模型层等价口径）；逐操作 P95（线性插值）＋单样本 <2 s 无响应窗口＋运行窗旗标门控（窗内样本方计入——"运行期间"语义） | P95 ≤200 ms／窗口 <2,000 ms（REQUIREMENTS NFR-PERF-01 原文——零私改） | 腿一 verdicts[1..5]（五操作 P95 0.009~0.339 ms 全 pass） |
| ">1 s 的工作转后台" | 结构承载：优化运行的评估工作全部发生在编排器 worker 线程（threadCount=8）——五类交互操作自身不执行任何评估工作；驱动线程仅阻塞等待 run() 返回（非 UI 线程角色——交互采样线程才是"UI 线程测试形态"） | NFR-PERF-01 原文语义 | 用例文件头申报＋报告 notes 登记 |
| 同版本兼容检查点恢复 | 协作取消截停→真协调器落盘（IRDOPTC1 codec v1）→同版本 Restored→磁盘字节解码→AttemptId+1 续跑 Completed→四面对账：①AT-35 审计不重复（base 审计＝参照全量；批完成账目跨恢复边界衔接＝检查点 completedUnits＋续跑增量＝全量；resumedBatchesSkipped>0）；②结论面等价（候选序/逐候选状态/八项指标 \|a−b\| ≤ 1×10⁻¹²·\|ref\|＋ε_abs/Pareto 序/可行集规模）；③工作汇对账（续跑增量＝参照全量增量——I-J3"跳过批记录确定性重推进：账面不双计而计算真实发生"）；④恢复≠优化成功的独立完成断言（终态 Completed＋评估计数恰 10,100） | 行为判定（NFR-PERF-06 原文"可从同版本兼容检查点恢复"——无数值阈值）；等价容差 1×10⁻¹²＝附录 D 第 8 项 P-01 冻结值，ε_abs=0＝黄金算例逐例声明 | 腿二 verdict（pass——四面对账全过） |

**样本纪律登记**（§4.2）：时限腿 15/15 有效零无效；交互抽查五操作各 2,386 有效零无效；无效样本零容忍（invalidCount==0 收口）；原始序列全量随报告 JSON 留痕（§4.8"不得只报统计值"）。

## 4. 替身边界与等价口径（如实申报）

- **本基准证明的**：编排器（JointRunOrchestrator）在 10,000＋100 候选规模下两级批量推进＋并行分片（8 worker）＋检查点产出/同版本恢复的**端到端墙钟规模化**（8 小时时限余量 ~3096×）＋运行期间 UI 交互响应满足 NFR-PERF-01＋同版本检查点恢复的统计/结论正确性。
- **本基准不证明的（归集成模式扩展测量挂点——随合入批登记）**：业务评估器（trj/dyn/sel 真实现）算力成本下的 8 小时时限画像（本基准内层评估器为确定性浮点迭代链替身——§11 同源纪律"替身不构成业务证明"；业务评估器端到端时限归集成模式扩展测量挂点）；execution 池化 worker 进程间派发面的时限（本基准测的是 config.threadCount 线程面的进程内编排——optimization.md §9.2 明文"worker 内并行线程数来自 config.opt"；进程池治理时限无独立需求阈值，不私设）。
- **与 B-01 的关系**：B-01（T02 已回填）证两态后台负载下的五操作 P95（其后台＝独立 worker 进程评估批次）；本基准运行期抽查的后台＝优化运行本体（进程内 8 worker 线程）——这正是 NFR-PERF-06"运行期间"的负载语义，两者互补不互替。
- **与 B-06 的关系**：B-06（T07 已回填）证固定候选集并行吞吐比；B-07 证标准规模运行时限＋运行期交互响应＋同版本检查点恢复——工作量替身同参（400,000 迭代/派发）使两基准单候选成本同源可比，互补不互替。
- **与功能用例的分工**：JointRunTest OPT-VER-223~227（小候选集）证检查点恢复"协议对"的正确性（含不兼容/损坏拒绝面）；本基准以 10,100 候选真实运行补"规模化达标"证据——两者互补不互替。
- **分段留痕口径**：本基准全程会话内串行完成（~176 s，远小于 8 小时会话预算）——启动/结束段由逐样本完整墙钟承载（报告 groups 原始序列）；检查点/恢复段由恢复用例报告 notes 分段日志行承载（写盘 Published→Restored→续跑 Completed 的实测数值逐段登记）。8 小时级跨会话后台运行形态在本基准中未被触发——如实登记，不虚称。

## 5. 首测结果（真实测量——2026-10-15，串行独跑 175.849 s，三用例全绿）

**复现命令**（performance-baseline §2 B-07 行同文回填）：

```
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <冒烟目录> --config Release --target sdurws_ird_optimization_perf_test
IRD_PERF_REPORT_DIR=<证据目录> IRD_PERF_GIT_COMMIT=5f6d7e9f88f143527d30ff91d01fee70abc82d00 \
  ctest --test-dir <冒烟目录>/optimization -C Release -R "^sdurws_ird_optimization_perf_test$" --output-on-failure
```

### 5.1 时限判定（用例 StandardRunTimeLimitAndInteractionDuringRun_WP23T08_B07——全绿，146.238 s）

| 组 | 有效/无效/预热 | 最大有效墙钟（ms） | P95（ms，分布参考） | 判定 |
| --- | --- | --- | --- | --- |
| opt-run（标准运行——15,100 候选端到端） | 15/0/1 | **9,301.0** | 9,263.3 | **9,301.0 ≤ 28,800,000——B-07 时限达标**（余量 ~3096×） |

- 有效判据双门：runCompleted∧phase==Completed＋评估计数恰 10,100（漂移即无效）——15 个有效样本全过；工作汇增量全样本恒量（5,457,410,620,696,191,368——确定性链，计算确实发生且恒量）。
- 原始样本序列全量（16 点：1 预热＋15 有效）见报告 JSON `b07-opt-run-timelimit.json` groups[opt-run]（§4.8）。

### 5.2 运行期交互抽查（同用例——运行窗旗标门控，全绿）

| 操作 | 有效/无效 | P95（ms） | 最大（ms） | 判定（P95 ≤200／窗口 <2,000） |
| --- | --- | --- | --- | --- |
| nav | 2,386/0 | 0.015 | 0.453 | pass |
| select | 2,386/0 | 0.013 | 0.112 | pass |
| filter | 2,386/0 | 0.339 | 3.370 | pass |
| edit | 2,386/0 | 0.030 | 0.475 | pass |
| viewswitch | 2,386/0 | 0.009 | 0.448 | pass |

- 窗外丢弃 35 个样本（运行窗间隙的轮转采样——非统计分母，计数留痕）；选择广播消费 2,394 次、编辑出口移交 2,394 次（驱动器真实到达证据，防静默空转）。
- 原始序列全量（逐操作 2,387 点：1 预热＋2,386 有效）见报告 JSON `b07-opt-run-timelimit.json` groups[nav/select/filter/edit/viewswitch]。

### 5.3 同版本检查点恢复（用例 SameVersionCheckpointRecovery_WP23T08_B07——全绿，21.119 s）

分段日志（报告 JSON `b07-opt-run-recovery.json` notes 同文登记）：

1. **启动段**：参照全量运行 Completed——quick=10,000 verified=100、批 2,525/2,525、检查点产出 2,525 份、工作汇增量 5,457,410,620,696,191,368。
2. **中断腿**：CancelAfterNContext 查询计数 31,000 后 Canceled（Quick 相约半程截停）——检查点 completedUnits=1,243/2,500、完成批 1,243、审计基点 quickEvaluated=4,972（＝1,243×4，逐批 4 候选账目自洽）。
3. **检查点段**：真 execution::CheckpointCoordinator writeState **Published**（seq=1，archiveDir 真实磁盘三文件；域中间状态编码 magic=**IRDOPTC1** codecVersion=1——DOPT-10 魔数逐字节核对通过）。
4. **恢复段**：restore（requestContractVersion=1＝记录同版本）→ **Restored**；磁盘 joint-state.bin 装载→decode round-trip 逐字段核对通过（codecVersion/completedUnits/totalUnits/completedBatchIndices）。
5. **结束段**：续跑 AttemptId=2 **Completed**——resumedBatchesSkipped=1,243（前缀批按基点记账）；AT-35 审计不重复（candidatesGenerated/quickEvaluated/quickScreenedOut/verifiedEvaluated＝参照全量；批完成账目跨恢复边界衔接＝1,243＋1,282＝2,525）；结论面等价（10,100 记录/80,800 指标元素 **maxRelDiff=0 ≤ 1×10⁻¹²，bitwise=true**——候选序/状态/Pareto 序/可行集规模全同）；工作汇增量＝参照逐位相等（I-J3 重推进语义对账——账面不双计而计算真实发生）。

### 5.4 同轮回归佐证（B-06 既有用例——同目标共进程执行，全绿）

同轮串行执行包含 B-06 用例（FixedCandidateMedianThroughputRatio_WP23T07_B06，8.490 s）：t1 中位吞吐 291.32／t8 中位吞吐 1,679.94 candidates/s，**比值 5.767 ≥4.0**，归约等价 1,980 记录/15,840 元素 maxRelDiff=0 bitwise=true——T07 交付面在本批改动后回归无漂移（本批对 B-06 TU 零触碰的佐证）。

## 6. 实施期排障记录（如实登记——两处实施期迭代，均发生在正式测量前）

1. **批完成账目断言口径误设**：恢复用例首跑断言 `resumed.audit.batchesCompleted == full.audit.batchesCompleted` 翻红（实测 1,282 vs 2,525）——该字段口径为"本次 run 内增量"（JointRunAuditCounts 字段注释原文），跨恢复边界的正确对账＝检查点 completedUnits（1,243）＋续跑增量（1,282）＝全量（2,525）。修正为衔接式断言（AT-35 语义不变——base 审计相等的更强断言始终成立）。属测量夹具断言缺陷，非被测编排缺陷。
2. **交互采样留痕体量失控（节奏化改版）**：预验证跑采用全速轮转采样（操作时延微秒级 ⇒ 单运行窗累积每操作 >100 万样本 ⇒ 报告 JSON 达数百 MB，不可作验收证据）。修正＝每轮五操作后静默 50 ms（真实用户交互间隙形态——单运行窗每操作 ~150~200 样本，单窗独立满足 §4.2 每组 ≥15，报告 JSON 854 KB）。全速版预验证数据（时限腿最大有效墙钟 10,303.5 ms；五操作 P95 ≤0.179 ms 零窗口违约）作为校准跑留痕，正式测量以节奏化版执行——manifest 冻结值（50 ms 节奏）与正式测量一致。

## 7. 开放项与挂点

- **集成模式复测挂点**：本首测为冒烟模式（独立配置面）测量；集成模式（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON）构建与同口径复测**随合入批执行**（AGENTS §6.4 三段式——集成构建由合入收尾员执行）。业务评估器真实现端到端时限画像与 execution worker 进程池半区画像归集成模式扩展测量挂点（§4 边界申报）。
- **ird_gates 直跑核定**：实施段未直跑门禁引擎（不操作集成构建树）——预期恰增一条测试面引用登记于 `traceability/wp23-t08-gate-registrations.md`（perf_test→ui），双态直跑核定随合入批/治理面补做（T03/T06/T07 先例口径）。
- **units/optimization.md §9.2 锚行翻面**：§9.2 规模化口径锚"B-07 待回填"行的翻面随治理批（forbiddenFiles 纪律——T06 对 B-05/T07 对 B-06 锚行同款先例）。
- **P-04 判定阈值边界重申**：本卡达标面为运行时限（NFR-PERF-06 原文 8 小时）、运行期交互响应（NFR-PERF-01 原文三值）与同版本检查点恢复（行为判定）——数值全部源自需求原文与 P-01 冻结容差表；REQUIREMENTS 附录 C《P-04 冻结数值表》判定阈值两槽位（通过线/降级线）所有者裁定维持待定，本卡零交集、零私设、零鲁棒性判定结论（WP-21-T05 判定半区不随本卡启用）。

## 8. 证据清单（build/wp23-t08-wt/_m6-evidence/）

| 证据 | 路径 |
| --- | --- |
| 正式测量报告 JSON（原始样本序列＋verdicts＋分段日志＋口径 notes，gitCommit 锚 5f6d7e9f） | perf-reports/b07-opt-run-timelimit.json、perf-reports/b07-opt-run-recovery.json、perf-reports/b06-parallel-throughput.json（同轮 B-06 回归佐证） |
| gtest XML（正式测量直跑 wp23-t08-perf.xml＝三用例 0 失败；ctest 三套件 opt-test 215+1 skip／contract 52／perf 3） | gtest-reports/wp23-t08-perf.xml、gtest-reports/sdurws_ird_optimization_test.xml、gtest-reports/sdurws_ird_optimization_contract_test.xml |
| 正式测量直跑控制台日志（EXIT=0，三用例全绿，175.849 s） | logs/perf-b07-official-run.log |
| ctest 三套件日志（100% tests passed，3/3） | logs/ctest-optimization-three-suites.log |
| 洁净构建日志（--clean-first 全链重建，BUILD_EXIT=0；唯一 warning 为 project 单元既有 C4297 与本批无关） | logs/build-smoke-clean-perf.log |
