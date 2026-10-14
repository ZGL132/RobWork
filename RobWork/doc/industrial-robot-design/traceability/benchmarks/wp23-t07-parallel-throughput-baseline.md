# WP-23-T07 并行吞吐基准首测报告（B-06／NFR-PERF-05＋NFR-COR-02）

> 基准清单行：performance-baseline §2 B-06（承接任务 WP-23-T07，分支 wp23-t07）。
> 本报告为**真实测量**留痕（协议 §4.8：环境/版本/数据集/样本数/统计约定/原始样本序列必含）——测量数字全部由运行产生，零预填零虚构。B-06 为**中位吞吐类判定**（§2 统计口径区分行）——判定统计量＝两组有效样本吞吐**中位数**之比（需求 NFR-PERF-05 原文"中位吞吐"），与 P95 类基准互不替代；报告 JSON 内的 P95/最小值仅为分布参考。

## 1. 环境与版本（协议 §4.1）

| 项 | 值 |
| --- | --- |
| 机器 | HP ZBook Fury 16 G11 Mobile Workstation（ZGL） |
| CPU | 13th Gen Intel Core i9-13950HX（24 核 32 线程——8 性能核＋16 能效核；8 工作线程腿落在性能核域） |
| 内存 | 63.5 GB（物理——Win32_OperatingSystem TotalVisibleMemorySize 66,634,028 KB 实录） |
| 存储 | SK hynix PC801 HFS002TEJ9X101N 2TB NVMe SSD（D: 仓库所在盘） |
| 操作系统 | Windows 11 专业版，10.0.26200 |
| 电源策略 | HP Optimized (Modern Standby)（`powercfg /getactivescheme` 实录） |
| 后台进程基线 | 常规开发工作站会话；测量期间单进程单测试串行执行（run 内部并行除外），无并行基准（协议 §4 测量纪律——串行跑分，本卡测量按队列独占执行） |
| 软件版本 | 分支 wp23-t07；commit `83e1a6bc937499b20d057502884942d8975111c3`（B-06 基准测量目标落位提交——报告 JSON gitCommit 字段锚）；构建配置 Release（MSVC 2022 x64，冒烟模式独立配置——vcpkg toolchain＋Qt 6.11.1 msvc2022_64 前缀） |
| 测量时点 | 2026-10-14（证据目录生成时间戳为准） |

同一轮基准的全部样本在同一台机器、同一硬件配置、同一软件版本上采集（协议 §4.1——零跨机样本）；吞吐比分子（8 线程腿）与分母（单线程腿）使用同一候选集、同一进程内二进制（§4.5"同一候选集、同一硬件、同一软件版本"口径）。

## 2. 数据集与驱动编排（performance-baseline §5.6 挂点）

- **数据集**：`ird-perf-b06-parallel-throughput@1`（manifest＝`industrialrobot/testdata/performance/b06-parallel-throughput-manifest.json`，2026-10-14 冻结；§4.5"候选集构成由 WP-23-T07 登记并冻结进数据集 manifest"的兑现位）。
- **被测对象**（optimization.md §9.2 规模化口径锚"达标归 WP-23 对应基准任务"的本卡承接位）：WP-21-T04 交付的 `JointRunOrchestrator`（units/optimization.md §8.5 外层两级批量编排＋§9.2 并行分片——候选批固定容量划分〔kBatchCapacity=4，与线程数无关〕、`config.parallel.threadCount` 派生 worker 线程〔每 worker 各持 JointLayeredChecker——§6.7 SingleThread 口径〕、按批序稳定合并）。OPT-VER-238（§13.2 用例矩阵——"进程（WP-23-T07）"载体）即本测量。
- **固定候选集**（manifest 冻结）：seeded-lhs 策略（`generateStageDJointCandidates` 单点——基线恒批首＋CandidateId 去重）×单连续绑定 `mdl.drivetrain.ratio[1]`（无量纲，域 [0.8,1.2]）×seed 7×maxCandidates 128×maxVerifiedCandidates 4——去重后候选数恒 132（Quick 128＋Verified 4）；批划分＝Quick 32 批＋Verified 1 批（8 线程 Quick 相满员并行，Verified 相 1 批串行尾部与生产形态一致）；两腿同基线根身份（CandidateId＝f(baselineRoot, baselineCv, patch) 公式锚——JointRunTest 恢复用例"基线身份钉住"同款纪律）。
- **工作量替身**：内层四域评估器（trj-sequence-plan/dyn-rnea-analysis/dt-mapping/sel-combination-check 登记键）＝确定性浮点迭代链替身——每派发 400,000 次串行 FP 乘加依赖链（x=x·(1+2⁻²⁰)+1e-9，不可向量化/不可折叠；结果 IEEE-754 位模式沉入进程级原子汇防消除）；单候选六层派发，单次全量运行恒 792 次派发。指标投影＝P-OPT-2 测试面替身（补丁标量→D-only 五项确定性线性映射，逐候选互异使 Pareto 非平凡）。
- **样本计划**：两腿（threadCount=1 vs 8）各 1 次预热（剔除）＋15 有效样本，零无效（§4.2"每组有效样本 ≥15"；T02/T03/T05/T06 先例同数自律）。

## 3. 测量口径（逐条对应 NFR-PERF-05/NFR-COR-02 原文）

| 条款 | 测量对象 | 阈值（出处） | 判定面 |
| --- | --- | --- | --- |
| 中位吞吐比 | 每有效样本吞吐＝(quickEvaluated＋verifiedEvaluated)/墙钟秒（candidates/s）；判定统计量＝两组有效样本吞吐中位数之比 | ≥4 倍（REQUIREMENTS NFR-PERF-05 原文——零私改） | 腿一 verdict（比值 5.5893 ≥ 4.0——pass） |
| 不含首次编译与导入 | 计时区间＝`JointRunOrchestrator::run()` 全程（steady_clock）——候选集构造/快照组装/注册表装配/编排器构造在区间外；每组首跑预热剔除承担组内冷路径（§4.5"正式计时前执行预热运行"） | 排除项（NFR-PERF-05 原文括注） | 区间结构承载＋预热样本 excluded 标记留痕 |
| 并行归约容差 | 每个并行有效样本结论面对照单线程参照（首个有效串行样本）逐元素判定：候选序（生成序＝合并序）＋逐候选状态＋八项指标 \|a−b\| ≤ 1×10⁻¹²·\|ref\|＋ε_abs＋Pareto 序与可行集规模＋审计计数＋工作汇增量（确定性链位模式累加） | 1×10⁻¹²（REQUIREMENTS 附录 D 第 8 项——P-01 冻结产物照抄）；ε_abs 黄金算例声明值＝0.0（§4.6/C7 逐例声明——零浮点重排结构下最严口径） | 腿二 verdict（最大逐元素相对差 0 ≤ 1×10⁻¹²——pass；bitwise=true） |

**中位数约定登记**：升序排序后奇数样本取中位元素、偶数样本取两中位均值（本基准每组 15 有效样本恒走奇数路径）——报告 JSON notes 字段同文登记（§4.8"登记所用约定"，全报告单一约定）。

## 4. 替身边界与等价口径（如实申报）

- **本基准证明的**：编排（JointRunOrchestrator）并行分片/按批序稳定合并的**吞吐规模化**（8 线程 ≥4× 中位吞吐）与吞吐运行中的**并行归约等价**（NFR-COR-02 六层对账）——"固定候选评估基准"的基准语义（OPT-VER-238 行"固定候选评估基准"）由固定候选集＋确定性工作量替身承载。
- **本基准不证明的（归集成模式扩展测量挂点——随合入批登记）**：业务评估器（trj/dyn/sel 真实现）端到端吞吐画像（本基准内层评估器为确定性浮点迭代链替身——§11 同源纪律"替身不构成业务证明"）；execution 池化 worker 进程间派发面的吞吐（DTB 行单元＝"横切（execution/各域）"的进程间半区——optimization.md §9.2 明文"worker 内并行线程数来自 config.opt"，本基准测的是该 config 线程面的编排并行；worker 进程池治理吞吐无独立需求阈值，不私设）。
- **等价口径**：两腿同进程同二进制同候选集同种子，唯 threadCount 不同（threadCount 入 config.opt canonical＝新 sliceId——KIN-13 同型"改线程数＝新身份"；结论面等价恰是 NFR-COR-02"与线程数无关"的需求语义，身份不同不妨等价判定）。
- **与功能用例分工**：JointRunTest OPT-VER-220/221/222（threadCount=1 vs 4 小候选集）证并行分片/稳定合并/归约容差的"正确性"；本基准以 128 候选真实串行测量补"规模化达标"证据（吞吐数字必须真实产生）——两者互补不互替。与 B-08（NFR-COR-02 确定性复现基准，T09 已回填）不互相替代：B-08 五域抽样复跑证集合等价与稳定排序，本基准在其上叠加吞吐判定与归约容差判定（DTB §2.24 本卡禁止项栏）。

## 5. 首测结果（真实测量——2026-10-14，ctest 串行独跑 8.66 s）

**复现命令**（performance-baseline §2 B-06 行同文回填）：

```
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> \
  -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <冒烟目录> --config Release --target sdurws_ird_optimization_perf_test
IRD_PERF_REPORT_DIR=<证据目录> IRD_PERF_GIT_COMMIT=83e1a6bc937499b20d057502884942d8975111c3 \
  ctest --test-dir <冒烟目录>/optimization -C Release -R "^sdurws_ird_optimization_perf_test$" --output-on-failure
```

### 5.1 吞吐判定（用例 FixedCandidateMedianThroughputRatio_WP23T07_B06——全绿，8.66 s）

| 组 | 有效/无效/预热 | 中位时长（ms） | 中位吞吐（candidates/s） | P95/最小（ms，分布参考） |
| --- | --- | --- | --- | --- |
| t1（单线程——分母） | 15/0/1 | 457.8498 | 288.27 | 470.28 / 439.25 |
| t8（8 线程——分子） | 15/0/1 | 81.9154 | 1611.43 | 85.87 / 77.29 |

- **中位吞吐比＝5.5893 ≥ 4.0**（阈值来源＝REQUIREMENTS NFR-PERF-05 原文——零私改）——**B-06 达标**。
- 原始样本序列全量（16 点/组：1 预热＋15 有效）见报告 JSON `b06-parallel-throughput.json` groups 数组（§4.8"不得只报统计值"）；复算核对：132/0.4578498 s＝288.268、132/0.0819154 s＝1611.427、比值 1611.427/288.268＝5.5893——与 JSON verdicts[0].measuredP95Ms 逐位一致。

### 5.2 并行归约等价（NFR-COR-02——同用例内嵌六层对账）

- 15 个并行有效样本 × 132 记录＝1980 条记录、8 项指标×1980＝15840 个数值元素全部对照单线程参照逐元素判定通过——**最大逐元素相对差＝0 ≤ 1×10⁻¹²**（附录 D 第 8 项 P-01 冻结值），且 **bitwise=true（全元素逐位相等）**——与 ε_abs=0 黄金算例声明（零浮点重排结构下最严口径）实测相符。
- 结构等价面全过：候选序（生成序＝合并序）逐位一致、逐候选状态一致、Pareto 序与可行集规模一致、审计计数（candidatesGenerated/quickEvaluated/quickScreenedOut/verifiedEvaluated）与线程数无关一致、工作汇增量（确定性链位模式累加）两腿逐位一致——计算确实发生且与线程数无关。
- 串行组内一致性（同种子重放半边）：15 个串行有效样本结论面互相比对全部逐位一致。

## 6. 实施期排障记录（如实登记——两处实施期迭代，均发生在参数冻结前）

1. **基线根身份跨腿未钉扎（等价判定误报"候选序漂移"）**：首次校准跑两腿 fixture 各自生成 baselineRoot ⇒ CandidateId 公式输入不同 ⇒ 跨腿逐候选比对在首记录即失配（吞吐比 5.672 本身成立）。修正＝readyRequest 显式接收基线根身份、两腿同值（JointRunTest 恢复用例"基线身份钉住"同款纪律）——修正后等价面 1980 记录/15840 元素逐位相等。该缺陷属测量夹具构造，非被测编排缺陷。
2. **compareConclusions 成员误用编译错**：等价判定函数误在 JointRunResult 上取 workDelta（该字段属采样载体 ThroughputSample）——编译期拦截，修正后工作汇对账落在调用方采样层。零语义影响。

工作量参数校准过程：kWorkIterations=400,000 首定即命中目标窗（串行单样本 ~456 ms——远大于计时/调度噪声、远小于套件节奏可承受），manifest 冻结值与正式测量值一致，零返工。

## 7. 开放项与挂点

- **集成模式复测挂点**：本首测为冒烟模式（独立配置面）测量；集成模式（仓库根 build/＋RWS_BUILD_INDUSTRIALROBOT=ON）构建与同口径复测**随合入批执行**（AGENTS §6.4 三段式——集成构建由合入收尾员执行）。业务评估器真实现端到端吞吐画像与 execution worker 进程池半区画像归集成模式扩展测量挂点（§4 边界申报）。
- **ird_gates 直跑核定**：实施段未直跑门禁引擎（不操作集成构建树）——预期恰增两条测试面引用登记于 `traceability/wp23-t07-gate-registrations.md`，双态直跑核定随合入批/治理面补做（T03/T06 先例口径）。
- **P-04 判定阈值边界重申**：本卡达标面为吞吐比（NFR-PERF-05 原文 ≥4×）与归约容差（附录 D 第 8 项 1×10⁻¹²）——数值全部源自需求原文与 P-01 冻结容差表；REQUIREMENTS 附录 C《P-04 冻结数值表》判定阈值两槽位（通过线/降级线）所有者裁定维持待定，本卡零交集、零私设、零鲁棒性判定结论。

## 8. 证据清单（build/wp23-t07-wt/_m6-evidence/）

| 证据 | 路径 |
| --- | --- |
| 正式测量报告 JSON（原始样本序列＋verdicts＋口径 notes，gitCommit 锚 83e1a6bc） | perf-reports/b06-parallel-throughput.json |
| gtest XML（三套件——模型 215+1 skip／契约 52／perf 1） | gtest-reports/sdurws_ird_optimization_test.xml、sdurws_ird_optimization_contract_test.xml、sdurws_ird_optimization_perf_test.xml |
| 冒烟构建日志（三目标零错误，BUILD_EXIT=0） | logs/build-smoke-pass1.log（首建）、build-smoke-pass2.log（perf 重建） |
| 正式测量 ctest 日志（串行独跑，CTEST_EXIT=0） | logs/perf-official-run.log |
| 套件亲跑控制台日志 | logs/opt-test-run.log、logs/opt-contract-run.log、logs/perf-xml-run.log |
