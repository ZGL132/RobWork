# 性能基准清单与测量协议（performance-baseline）

> **文档定位**：本文档是 development-task-breakdown.md §2.24 行 WP-23-T01 的登记产物，为 WP-23 性能规模化与确定性各执行任务（T02~T09）提供统一的基准清单、黄金项目定义与测量协议。文档代号 **performance-baseline**。
>
> **权威边界**：需求语义唯一权威为 `REQUIREMENTS.md`（§19.3 NFR-PERF-01~06、§17 PM-16、§19.1 NFR-COR-02、§25 附录 D）；本文档只做协议登记与执行挂点组织，**不重定义、不收窄、不扩大任何需求语义**——达标数值一律照抄需求原文，与需求冲突时以需求为准。
>
> **状态**：v0.1（2026-10-13，WP-23-T01 初版）。基准数据集类别挂靠 `units/testkit.md` §4.2.1 预留的 `performance-baseline` 类；计时/吞吐夹具的代码侧细则按 testkit.md §10.1 登记由 WP-23 消费者（T02~T09）届时提出。

---

## 1. 文档定位与阅读阶梯

| 项 | 内容 |
| --- | --- |
| 产物来源 | DTB §2.24 WP-23-T01（横切文档任务，阶段 A 可产出、阶段 C 启用） |
| 上游输入 | REQUIREMENTS §19.3（NFR-PERF-01~06）、§17 PM-16（F-03 定义＋V12/V13 细化）、§19.1 NFR-COR-02、§25 附录 D（第 8 项并行归约容差） |
| 数据集类别 | testkit.md §4.2.1 `performance-baseline` 性能基准——吞吐/时延/内存的基准场景与统计口径；**不作正确性依据，只作性能对照**（NFR-PERF 系列，阶段 C/D） |
| 消费者 | WP-23-T02~T09（各基准执行与达标任务）；各域 M5 达标判定 |
| 本文档的委托定义权 | NFR-PERF-01 原文将"后台负载"定义委托给基准清单（§2 B-01 行）——本文档 §4.3 的两态后台负载登记即该委托的兑现；除此之外本文档不新增任何达标数值 |

---

## 2. 基准清单

> 覆盖 NFR-PERF-01~06 全部六条＋PM-16，另列 NFR-COR-02 确定性复现基准（契约 requirements 在册、T07/T09 追溯项）。**达标口径列一律照抄需求原文语义**；「复现命令位」列是各执行任务的回填挂点——执行任务（T02~T09）完成时必须在本表对应行回填可独立执行的完整复现命令，回填前该列如实标注"待回填"，**不得预先伪造命令**。

| 编号 | 基准场景 | 需求 ID（阶段/发布） | 达标口径（照抄需求原文语义） | 承接任务 | 复现命令位 |
| --- | --- | --- | --- | --- | --- |
| B-01 | 交互响应 | NFR-PERF-01（C/R1） | 标准规模数据和本清单定义的后台负载下，导航、选择、筛选、编辑和视图切换 P95 响应 ≤200 ms；不出现超过 2 s 的无响应窗口；超过 1 s 的工作转后台。测量口径按 PM-16 协议注记：P95 分位数采样（原"三次运行取中位数"口径为继承缺陷，已作废——F-03） | WP-23-T02 | 已回填（2026-10-13，T02）：冒烟模式复现命令＝`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt msvc 目录>` → `cmake --build <冒烟目录> --config Release --target sdurws_ird_ui_perf_test` → 设 `IRD_PERF_REPORT_DIR=<证据目录>`（可再设 `IRD_PERF_GIT_COMMIT=<SHA>` 入报告）后 `ctest --test-dir <冒烟目录>/ui -C Release -R "^sdurws_ird_ui_perf_test$" --output-on-failure`——三用例全绿（空闲态五操作 P95 ≤200 ms／后台态同口径＋独立 worker 进程证据／>1 s 转后台用例）＋逐例报告 JSON（`<证据目录>/b01-*.json`：p95Convention＝线性插值、原始样本序列、阈值判定留痕）即 B-01 达标。标准规模数据＝黄金项目锚定的 ui 侧投影数据集（datasetId ird-perf-b01-interaction@1——manifest＝`testdata/performance/b01-interaction-manifest.json`）；后台负载注入形态＝独立 worker 进程流式评估批次（manifest backgroundLoad 节冻结）；测量夹具＝testkit.md §6.3/§10.1 PerfBenchmark.hpp；无头模型层等价口径与边界登记＝用例文件头＋首测报告 traceability/benchmarks/wp23-t02-interaction-baseline.md（集成模式复测挂点登记于报告 §4——集成构建由合入批执行） |
| B-02 | 取消时序 | NFR-PERF-02（C/R1） | 取消请求在 2 s 内进入 Canceling 并停止派发新批次；普通批次 10 s 内结束；超时后允许终止独立工作进程并保留最近检查点 | WP-23-T03 | 已回填（2026-10-13，T03）：冒烟模式复现命令＝`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt msvc 目录>` → `cmake --build <冒烟目录> --config Release --target sdurws_ird_execution_perf_test` → 设 `IRD_PERF_REPORT_DIR=<证据目录>`（可再设 `IRD_PERF_GIT_COMMIT=<SHA>` 入报告）后 `ctest --test-dir <冒烟目录>/execution -C Release -R "^sdurws_ird_execution_perf_test$" --output-on-failure`——两用例全绿即 B-02 达标：正常收敛腿（1 预热剔除＋15 有效样本——条款 1 取消→入 Canceling＋停派发 ≤2 s／条款 2 入 Canceling→协作收敛 ≤10 s；判定统计量＝**有效样本最大值**——B-02 为时序阈值类判定非 P95，§2 统计口径区分行）＋超时强杀腿（3 场景行为判定——条款 3：永不收敛脚本驱动 10 s 窗耗尽→进程树终止→OS 级 PID 死亡核对→检查点强杀后留存核对〔磁盘重读字节一致＋复合摘要＝发布摘要〕）。逐例报告 JSON（`<证据目录>/b02-cancel-*.json`：原始样本序列全量、阈值判定留痕 thresholdSource＝REQUIREMENTS NFR-PERF-02、判定统计量口径注记）。驱动编排＝真实 TaskController（缺省 steady_clock）＋真实 WorkerSupervisor＋真实独立 worker 进程（sdurws_ird_execution_worker 阶段 A 脚本替身评估器），驱动线程 10 ms poll，装配同步锚点＝DispatchAccepted 事件；数据集＝ird-perf-b02-cancel@2（manifest＝`testdata/performance/b02-cancel-manifest.json`）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋测量目标内 CancelTimingRig（阈值类采样执行器——零新增 testkit 头）；无头等价口径与边界登记＝用例文件头＋首测报告 traceability/benchmarks/wp23-t03-cancel-baseline.md（集成模式复测挂点登记于报告——集成构建由合入批执行） |
| B-03 | 大规模数据加载/保存/分页 | NFR-PERF-03（C/R1） | 成功加载、保存和分页浏览 5,000 个任务点、100,000 个采样结果和 10,000 个候选摘要；不一次性把全部明细装入界面 | WP-23-T04 | 已回填（2026-10-13，T04）：冒烟模式复现命令＝`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt msvc 目录>` → `cmake --build <冒烟目录> --config Release --target sdurws_ird_ui_largedata_perf_test` → 设 `IRD_PERF_REPORT_DIR=<证据目录>`（可再设 `IRD_PERF_GIT_COMMIT=<SHA>` 入报告）后 `ctest --test-dir <冒烟目录>/ui -C Release -R "^sdurws_ird_ui_largedata_perf_test$" --output-on-failure`——三用例全绿即 B-03 达标：腿 A 任务点 5,000（RequirementCodec canonical 编码＋io 原子文件写出的保存/加载＋全量往返等值核对）＋腿 B 采样结果 100,000（产品生成器 generateSampleSet/computeCoverage 真跑＋区域覆盖 canonical 载荷 IRDCV01＋project 归档端口 begin→writeBatch→finalize 的保存＋listRuns/runDir 产品编址读回＋SHA-256 完整性核对＋冻结布局独立最小解析×产品覆盖率计算分段交叉核对互证的加载）＋腿 C 候选摘要 10,000（产品工厂 assembleRunResult＋六工件数据面组装＋工件 2 候选 CSV canonical 写出的保存＋io ICsvReader 单遍流式回读的加载）；三腿分页窗口投影单页物化恒 ≤100 行、全量翻页累计恰为 N（50/1,000/100 页）、每组 1 预热＋15 有效零无效样本。逐例报告 JSON（`<证据目录>/b03-*.json`：原始样本序列、规模/功能判定口径注记〔B-03 为规模/功能类判定非 P95，§2 统计口径区分行——需求无数值时延阈值，本行零私设数值阈值〕、结构观测账本）。数据集＝ird-perf-b03-largedata@1（manifest＝`testdata/performance/b03-largedata-manifest.json`）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋runInteractionGroup 分组采样（零新增 testkit 头）；基准驱动产品优化（区域覆盖载荷编码对齐 O(N²)→O(N)，输出字节逐位不变）与优化前/后实测对照（save P95 9,011.87 ms→124.10 ms）＝首测报告 traceability/benchmarks/wp23-t04-largedata-baseline.md §5（无头等价口径与边界＝用例文件头＋报告 §4；集成构建由合入批执行） |
| B-04 | 项目打开（冷/热启动） | PM-16（C/R1） | 黄金项目（1,000 对象/100 修订）**热启动**打开 P95 ≤ 3 s（验收门禁仅限热启动组——V13 §4 收窄）；冷启动组单独报告、超限登记原因与改进项（不阻断验收）；冷启动软指标 P95 ≤ 8 s 为参考目标（M-14，v1.8，不作门禁） | WP-23-T05 | 已回填（2026-10-13，T05）：冒烟模式复现命令＝`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <冒烟目录> -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt msvc 目录>` → `cmake --build <冒烟目录> --config Release --target sdurws_ird_project_perf_test`（连带冷启动采样子进程 sdurws_ird_project_openbench_worker）→ 设 `IRD_PERF_REPORT_DIR=<证据目录>`（可再设 `IRD_PERF_GIT_COMMIT=<SHA>` 入报告）后 `ctest --test-dir <冒烟目录>/project -C Release -R "^sdurws_ird_project_perf_test$" --output-on-failure`——两用例全绿即 B-04 达标：热启动门禁腿（进程已运行＋黄金项目磁盘数据就绪状态下反复 ProjectStoreFactory::open〔Writable 完整协议——锁获取＋恢复扫描＋HEAD 闭包完整性校验〕；测量区间＝open 调用＋HEAD 首读，核对与关闭排空在区间外；1 预热剔除＋≥15 有效零无效；P95 线性插值 ≤3,000 ms 判门禁——verdict 进报告）＋冷启动软指标腿（每样本全新 openbench_worker 子进程＝PM-16 原文"冷启动重置＝采样前重启进程"；1 预热＋≥15 有效零无效；P95 对照 8,000 ms **只登记不判定**——用例断言面＝样本纪律＋零无效，DTB 行禁止项"冷启动不作门禁"的用例结构承载）。逐例报告 JSON（`<证据目录>/b04-warm-open.json`／`b04-cold-open.json`：p95Convention 线性插值、原始样本序列、阈值判定留痕 thresholdSource＝REQUIREMENTS PM-16）。数据集＝ird-perf-b04-golden-project@1（manifest＝`testdata/performance/b04-golden-project-manifest.json`——黄金项目生成器＝真实产品命令管线：createNew→handlerRegistry() 公共装配通道注册基准处理器→100 数据修订×10 对象＝1,000 数据对象〔另有 createNew 骨架修订 r0＝机制面，项目修订总数 101 如实登记〕；实现口径观测〔HEAD 引用集＝元数据＋本修订计划对象〕如实登记 manifest implObservation，归 product 侧澄清）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋目标内采样执行器（T03 CancelTimingRig 先例——零新增 testkit 头）；无头等价口径与边界登记＝用例文件头＋首测报告 traceability/benchmarks/wp23-t05-golden-project-baseline.md §4/§5（真实 Studio 宿主冷启动形态归集成模式扩展测量挂点——集成构建由合入批执行） |
| B-05 | 内存与节流规模化 | NFR-PERF-04（D/R2） | 大任务使用分批、流式结果、缓存和检查点；主进程与全部工作进程合计峰值内存默认不超过物理内存 70%；接近上限先节流再给资源不足诊断 | WP-23-T06 | 待回填（T06 落位时登记于本行） |
| B-06 | 并行吞吐 | NFR-PERF-05（D/R2） | 固定候选评估基准上，8 个工作线程相对单线程**中位吞吐**提升 ≥4 倍；**不含首次编译与导入** | WP-23-T07 | 待回填（T07 落位时登记于本行） |
| B-07 | 优化运行时限 | NFR-PERF-06（D/R2） | 10,000 Quick＋100 Verified 候选标准优化基准 8 小时内结束；运行期间满足 NFR-PERF-01；可从同版本兼容检查点恢复 | WP-23-T08 | 待回填（T08 落位时登记于本行） |
| B-08 | 确定性复现 | NFR-COR-02（B~E/R1） | 同一输入、软件版本、配置、线程数和随机种子产生**等价候选集合与稳定排序**；并行归约数值满足附录 D 第 8 项容差（1×10⁻¹²，通用比较公式逐元素判定）；**不要求浮点文件逐字节相同** | WP-23-T09 | 已回填（2026-10-13，T09）：集成模式复现命令＝`cmake -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` → `cmake --build build --config Release --target sdurws_ird_testkit_test sdurws_ird_kinematics_test sdurws_ird_trajectory_test sdurws_ird_dynamics_test sdurws_ird_selection_test sdurws_ird_optimization_test` → 设 `IRD_REPRO_REPORT_DIR=<证据目录>` 后 `ctest --test-dir build -C Release -R "sdurws_ird_(testkit\|kinematics\|trajectory\|dynamics\|selection\|optimization)_test" --output-on-failure`——五域 ReproBenchmark 用例全绿＋逐例报告 JSON（`<证据目录>/<caseId>.json`：集合等价＋稳定排序＋1e-12 归约容差三面）即 B-08 达标。夹具细则＝testkit.md §6.3/§10.1；首测报告与证据清单＝traceability/benchmarks/wp23-t09-repro-baseline.md（trajectory 用例随 sdurw_kinematics gating 仅集成模式编译执行——smoke 冒烟变体以四例运行，登记于报告） |

**清单覆盖自查**（对照 DTB §2.24 T01 验收标准"基准清单覆盖全部 PERF 条目"）：NFR-PERF-01→B-01、NFR-PERF-02→B-02、NFR-PERF-03→B-03、NFR-PERF-04→B-05、NFR-PERF-05→B-06、NFR-PERF-06→B-07、PM-16→B-04——七条全覆盖；B-08 为 NFR-COR-02 的确定性复现基准（非 PERF 条目，不占覆盖计数）。

**统计口径区分（防混淆）**：P95 类基准＝B-01、B-04（P95 分位数，禁中位数替代）；中位吞吐基准＝B-06（需求原文口径即中位数，与 P95 类基准**互不替代**）；B-02/B-03/B-05/B-07 为时序/规模/内存阈值类判定，不涉及分位数统计；B-08 为集合等价与容差判定。

---

## 3. 黄金项目定义（PM-16 口径）

**来源（唯一权威）**：REQUIREMENTS §17 PM-16 原文——「项目打开性能：黄金项目（1,000 对象/100 修订）**热启动**打开 P95 ≤ 3 s」。黄金项目的规模数值以该原文为准，本文档**零增删**：1,000 对象、100 修订。

### 3.1 规模构成

| 项 | 数值 | 依据 |
| --- | --- | --- |
| 对象数 | 1,000 | PM-16 原文 |
| 修订数 | 100 | PM-16 原文 |

### 3.2 明细构成与生成器登记

需求原文未固定黄金项目的对象类型分布与修订内容分布。按「不私设数值」纪律，本定义只登记机制、不登记数字：明细构成为黄金项目**生成器的参数**，由 WP-23-T05 在首次基线测量时登记并冻结进数据集 manifest（datasetId＋版本＋`history` 留痕——testkit.md §4.2.1 升级规则约束：升级＝新建数据集或新 major 版本，不得原地改写）。生成器参数不得使黄金项目规模偏离 PM-16 原文的 1,000 对象/100 修订。

### 3.3 数据集类别与存储策略

- **类别挂靠**：`performance-baseline` 类（testkit.md §4.2.1）——生产实现测量＋硬件/负载口径；不作正确性依据，只作性能对照。
- **存储与 CI 获取策略＝已登记未决裁决**：大体绩黄金数据（点云/大规模采样快照）存储策略（git 内 vs LFS vs 制品）为 DTB §4 O-22 / testkit.md §12.3 P-TK-5 在册登记项，裁决人＝构建负责人＋WP-02，**首个大数据集出现时裁决**。裁决前黄金项目数据按仓库内小体量数据纪律存放（O-22 行既有口径：阶段 A 数据小，源内即可）；黄金项目生成器如产生大体量中间数据，按 P-TK-5 裁决前不得引入第二存储渠道。
- **与大规模数据基准的关系**：黄金项目（1,000 对象/100 修订，B-04 用）与 NFR-PERF-03 大规模数据（5,000 任务点/100,000 采样结果/10,000 候选摘要，B-03 用）是**两个不同基准的两套数据集**，不得混用；PM-16 与 NFR-PERF-01 亦为不同测试（项目加载时长 vs 交互响应时延——PM-16 原文括注）。

---

## 4. 测量协议

> 本协议落实 PM-16 原文的测量协议（F-03 定义＋V12/V13 细化），并将其口径推广至全部 P95 类基准（B-01/B-04）；阈值类基准（B-02/B-03/B-05/B-07）与中位吞吐基准（B-06）、确定性基准（B-08）按各自条目口径执行。本协议为测量方法登记，不改变任何达标数值。

### 4.1 硬件与环境固定（PM-16 原文：固定硬件）

- 同一轮基准的全部样本必须在**同一台机器、同一硬件配置**上采集；跨机器样本不得混入同一统计。
- 报告必须登记：机器型号、CPU/内存/存储配置、操作系统版本、电源策略、测量期间的后台进程基线。
- 软件版本锁定：commit SHA＋构建配置（Release）；跨软件版本的样本不得混入同一统计。

### 4.2 冷/热启动分组与样本量（PM-16 原文：冷/热启动分组，每组有效样本 ≥15 次，合计 ≥30）

- **分组定义**：冷启动组＝每次采样前重启进程（冷启动重置＝采样前重启进程——PM-16 原文）；热启动组＝进程已运行、项目所需资源已就绪的状态下采样。
- **样本量下限**：每组**有效样本 ≥15 次**（两组合计 ≥30）；由于剔除预热，每组实际执行次数 ≥16（首跑预热＋≥15 有效样本）。
- **预热剔除**：剔除各组首次预热运行（PM-16 原文），预热样本不计入统计。
- **有效样本判据**：运行完整结束且产生完整测量区间的样本方为有效；中途崩溃、进程被外部终止或测量区间不完整的运行计为**无效样本**——不计入分母，并留痕登记失效原因。除首次预热剔除外，**不得以任何理由（含"离群值"）剔除样本**；确需剔除必须登记原因并经评审，且剔除后有效样本数仍须满足 ≥15 下限。
- **P95 计算＝线性插值分位数**（PM-16 原文）：样本升序排列，位置 h＝(n−1)×0.95（n 为有效样本数），P95＝x⌊h⌋＋(h−⌊h⌋)×(x⌊h⌋+1−x⌊h⌋)。实现须在报告中登记所用分位数约定与实现代码位；同一报告内不得混用两种分位数约定。

### 4.3 交互响应测量口径（NFR-PERF-01）

- **标准规模数据**：本清单将 B-01 的标准规模数据锚定为 §3 黄金项目（1,000 对象/100 修订）——黄金项目是需求原文唯一给出的项目级规模口径；此锚定为基准清单的协议登记（NFR-PERF-01 未单独给定项目规模），调整须修订本文档并留痕，不得在执行任务中静默替换。
- **后台负载**（NFR-PERF-01 原文委托基准清单定义）：B-01 在两态后台负载下分别测量——
  1. **空闲态**：无后台评估任务运行；
  2. **后台态**：至少一个评估批次正在独立工作进程中运行（覆盖"后台负载"语义）。
  负载注入的具体形态（批次规模与构成）由 WP-23-T02 落位时登记并冻结进数据集 manifest；两态各自满足 P95 ≤ 200 ms 判定。
- **测量对象**：导航、选择、筛选、编辑、视图切换五类交互操作的端到端响应时长（自操作发起至用户可感知完成）。
- **无响应窗口**：单次交互中 UI 线程不可交互时长超过 2 s 即违约；超过 1 s 的工作必须转后台执行。
- P95 采样与计算按 §4.2；**禁止以 3 次中位数替代 P95**（F-03：原"三次运行取中位数"口径已作废；3 次中位数仅作快速回归参考，不作为 P95 达标证据——PM-16 原文）。

### 4.4 项目打开冷/热分组口径（PM-16 专条）

- **门禁分组（V13 §4 收窄）**：验收门禁**仅限热启动组**（热启动 P95 ≤ 3 s，≥15 有效样本）。
- **冷启动组单独报告**：不阻断验收；超限登记原因与改进项。
- **冷启动软指标（M-14，v1.8）**：冷启动组 P95 ≤ 8 s 为参考目标，**不作验收门禁**——冷启动受进程启动、插件加载与框架初始化支配（含 DLL 加载、Qt 资源解压、RobWork 符号绑定）；先以软指标牵引优化，超限仍须登记原因与改进项；待积累基线数据后再评估是否转门禁（转门禁属需求变更，须走变更流程）。

### 4.5 并行吞吐口径（NFR-PERF-05）

- **指标**：8 个工作线程相对单线程的**中位吞吐比 ≥4**——统计口径为中位数（需求原文"中位吞吐"），与 §4.2 P95 类基准口径不同，二者不得互相替代。
- **排除项**：**不含首次编译与导入**——计时区间排除首次模型编译与数据导入；正式计时前执行预热运行。
- **基准固定**：候选评估基准的候选集构成由 WP-23-T07 登记并冻结进数据集 manifest；同一吞吐比的分子（8 线程）与分母（单线程）必须使用同一候选集、同一硬件、同一软件版本。
- **并行归约正确性**：吞吐基准运行中的并行归约数值须满足 §4.6 容差（NFR-COR-02/附录 D 第 8 项——DTB §2.24 T07 禁止项栏原文：「并行归约满足附录 D 第 8 项容差」）。

### 4.6 并行归约与确定性容差（NFR-COR-02＋附录 D 第 8 项）

- **容差值（P-01 冻结产物，照抄）**：附录 D 第 8 项——并行归约相对容差 **1×10⁻¹²**，验证载体 AT-09~11。
- **判定公式（附录 D 通用比较公式，C4）**：|a−b| ≤ ε_rel·|ref| ＋ ε_abs（零参考时退化为 ε_abs，不要求精确零）；比较一律**逐元素**进行（向量/序列逐项比较），不以差值总和替代（防正负抵消掩盖）。
- **适用范围（C7）**：第 8 项属**测试对照容差**——ref＝参考值；导出量（时间/功率等）的 ε_abs 由黄金算例逐例声明，未声明即判"容差未定义"并报错，不静默通过（NFR-COR-03）。
- **确定性口径（NFR-COR-02 原文）**：同一输入、软件版本、配置、线程数和随机种子产生**等价候选集合与稳定排序**；并行归约数值满足上述容差；**不要求浮点文件逐字节相同**。

### 4.7 内存与时限测量（NFR-PERF-04/06）

- **B-05 峰值内存**：计量对象＝主进程＋全部工作进程**合计**峰值（各进程独立计量后汇总，采样须覆盖工作进程峰值并存时刻）；"默认不超过物理内存 70%"的"默认"语义以需求原文为准，本清单不放宽；"接近上限先节流再给资源不足诊断"的时序语义由 WP-23-T06 用例承载。
- **B-07 运行时限**：8 小时为端到端墙钟时限；运行期间交互响应按 §4.3 口径抽查（NFR-PERF-06 原文"运行期间满足 NFR-PERF-01"）；检查点恢复限定**同版本兼容**（NFR-PERF-06 原文）。

### 4.8 报告与留痕

- 每次基准执行产出测量报告并在 `traceability/**` 留痕，必含字段：硬件环境（§4.1 清单）、软件版本（commit SHA＋构建配置）、数据集 datasetId＋版本、样本数（原始/有效/剔除及其原因）、分位数约定与计算结果、**原始样本序列**（不得只报统计值）。
- 未执行的基准如实标注"未执行"；未达标的如实登记差距与原因——不带病前进（DTB 通用纪律）。
- 本文档各基准行的「复现命令位」由对应执行任务回填；回填后任何人应能仅凭该命令＋登记的环境前提复现该基准。

---

## 5. 达标判据与执行挂点（WP-23-T02~T09）

> 本节为 DTB §2.24 各执行行在本文档的挂点：验收标准语义照抄 DTB 各行原文（数值零改动），执行任务完成时同步回填 §2 对应行复现命令位。本表不新增、不收窄任何判据。

### 5.1 WP-23-T02 交互响应基准达标（阶段 C）

- **前置**：WP-23-T01（本文档）、M5 各域。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-01。
- **达标判据**（DTB §2.24 行原文语义）：导航/选择/筛选/编辑/视图切换 P95 ≤200 ms；无 >2 s 无响应；>1 s 转后台用例通过。
- **挂点动作**：回填 B-01 复现命令位；登记 §4.3 后台负载注入形态与标准规模数据集 manifest。
- **挂点动作（2026-10-13，T02 实施段兑现）**：B-01 复现命令位已回填（§2 行）；§4.3 后台负载注入形态已登记并冻结进数据集 manifest（datasetId ird-perf-b01-interaction@1——`testdata/performance/b01-interaction-manifest.json`：标准规模 ui 侧投影数据集生成器参数〔1,000 对象/100 修订语境/五分组分布/表单 24 字段/筛选词/选中批量〕＋backgroundLoad 节〔独立 worker 进程流式评估批次——`sdurws_ird_execution_worker` 阶段 A 内置脚本替身评估器、`stage-a-script:v1/batches 5,000,000`、主进程泵线程＋1/16 抽样排队 Marshal〕）；测量夹具细则登记于 testkit.md §6.3/§10.1（`PerfBenchmark.hpp`——P95 线性插值＋分组采样执行器＋报告拖挂）；首测报告与证据清单＝traceability/benchmarks/wp23-t02-interaction-baseline.md（无头模型层等价口径与边界如实申报于报告 §4）。

### 5.2 WP-23-T03 取消时序达标（阶段 C）

- **前置**：WP-23-T01（本文档）、WP-08-T04。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-02。
- **达标判据**：2 s/10 s 协议时序测量通过。
- **挂点动作（2026-10-13，T03 实施段兑现）**：B-02 复现命令位已回填（§2 行）；驱动编排与测量口径已冻结进数据集 manifest（datasetId ird-perf-b02-cancel@2——`testdata/performance/b02-cancel-manifest.json`：真实 TaskController/WorkerSupervisor/worker 进程三件套＋驱动线程 10 ms poll＋装配锚点 DispatchAccepted 事件＋脚本形态〔正常腿 batches 500000／超时腿 checkpoint 1＋heartbeat-off＋hang〕＋样本计划〔正常腿 1 预热＋15 有效、超时腿 3 场景〕＋判定统计量＝有效样本最大值；history 留痕 v1 批次洪流缺陷未产出测量即换版）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋测量目标内 CancelTimingRig 阈值类采样执行器（零新增 testkit 头——testkit.md 本版零变更）；首测报告与证据清单＝traceability/benchmarks/wp23-t03-cancel-baseline.md（首测全绿：正常腿 15/15 有效零无效、超时腿 3 场景检查点留存全核对——集成模式复测挂点见报告 §7）。

### 5.3 WP-23-T04 大规模数据加载与分页达标（阶段 C）

- **前置**：WP-23-T01（本文档）、M5 各域。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-03。
- **达标判据**：5,000 任务点/100,000 采样结果/10,000 候选摘要加载保存分页通过；不整体装入界面。
- **挂点动作（2026-10-13，T04 实施段兑现）**：B-03 复现命令位已回填（§2 行）；数据集 manifest 已冻结（datasetId ird-perf-b03-largedata@1——`testdata/performance/b03-largedata-manifest.json`：三类生成器参数〔任务点确定性内容与 canonical 编码面／采样计划 Grid 100×100×1＋姿态 9×1＝100,000 样本＋状态模式／候选 Quick 9,000＋Verified 1,000 身份互异〕＋页大小 100＋规模/功能判定口径）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋runInteractionGroup 分组采样执行器（零新增 testkit 头——testkit.md 本版零变更）；测量目标＝ui `sdurws_ird_ui_largedata_perf_test`（五域测试面直链 requirements/kinematics/optimization/project/io——O-31 测试面特权先例同型，登记册 traceability/wp23-t04-gate-registrations.md）；基准驱动产品优化＝kinematics 区域覆盖载荷编码对齐回查 O(N²)→索引 O(N)（输出字节逐位不变——提交 07dd0113）；首测报告与证据清单＝traceability/benchmarks/wp23-t04-largedata-baseline.md（首测三腿九组全绿——优化前/后实测对照 save P95 9,011.87 ms→124.10 ms 如实留痕；集成模式复测挂点见报告 §7）。

### 5.4 WP-23-T05 项目打开热启动达标（阶段 C）

- **前置**：WP-23-T01（本文档）、WP-04-T08。
- **输入**：performance-baseline.md；PM-16。
- **需求追溯**：PM-16。
- **达标判据**：热启动组 P95 ≤3 s（≥15 有效样本）；冷启动软指标 P95 ≤8 s 单独报告、超限登记。
- **禁止项**（DTB 行原文）：冷启动不作门禁。
- **挂点动作**：登记黄金项目生成器参数并冻结进数据集 manifest（§3.2）。
- **挂点动作（2026-10-13，T05 实施段兑现）**：B-04 复现命令位已回填（§2 行）；黄金项目生成器明细参数已登记并冻结进数据集 manifest（datasetId ird-perf-b04-golden-project@1——`testdata/performance/b04-golden-project-manifest.json`：真实产品命令管线〔createNew→handlerRegistry() 公共装配通道注册基准处理器 golden-append-batch v1→100 次命令提交〕×生成器参数〔1,000 数据对象／100 数据修订／每修订 10 对象／对象负载 2,048 字节确定性文本／对象类型 token "GoldenData"〕；createNew 初始骨架修订 r0＝机制面非生成器参数——项目修订总数 101 如实登记，PM-16 两数值零偏移）；测量夹具＝testkit PerfBenchmark 报告拖挂面＋目标内采样执行器（T03 CancelTimingRig 先例同型——零新增 testkit 头，testkit.md 本版零变更）＋testkit TestProcessRunner（冷启动全新子进程承载——PM-16 原文"冷启动重置"）；测量目标＝project `sdurws_ird_project_perf_test`＋`sdurws_ird_project_openbench_worker`（测试面目标——T-1 允许形态仅测试目标链 testkit，产品目标零变更）；首测报告与证据清单＝traceability/benchmarks/wp23-t05-golden-project-baseline.md（首测两组全绿：热启动门禁组 P95 153.01 ms ≤3,000 ms〔15 有效零无效〕／冷启动软指标组 P95 149.17 ms ≤8,000 ms 参考达标〔15 有效零无效，不作门禁〕——实现口径观测〔HEAD 引用集＝元数据＋本修订计划对象，TxEngine 装配口径〕如实登记报告 §5.4 与 manifest implObservation 归 product 侧澄清；集成模式复测挂点见报告 §7）。

### 5.5 WP-23-T06 内存与节流规模化达标（R2/D）

- **前置**：WP-23-T01（本文档）、WP-08-T08、WP-21-T04。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-04。
- **达标判据**：主进程＋全部工作进程峰值 ≤70% 物理内存；大任务分批/流式/检查点＋先节流后诊断用例与基准通过。

### 5.6 WP-23-T07 并行吞吐达标（R2/D）

- **前置**：WP-23-T01（本文档）、WP-21-T04。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-05、NFR-COR-02。
- **达标判据**：吞吐比测量通过（不含首次编译与导入）。
- **禁止项**（DTB 行原文）：并行归约满足附录 D 第 8 项容差（§4.6 口径）。

### 5.7 WP-23-T08 优化运行基准达标（R2/D）

- **前置**：WP-23-T01（本文档）、WP-21-T04。
- **输入**：performance-baseline.md（本文档）。
- **需求追溯**：NFR-PERF-06。
- **达标判据**：基准时限内完成；运行期间满足 PERF-01；同版本检查点恢复用例通过。

### 5.8 WP-23-T09 确定性复现基准

- **前置**：WP-23-T01（本文档）、WP-02-T06/T08。
- **输入**：performance-baseline.md（本文档）；附录 D 第 8 项。
- **需求追溯**：NFR-COR-02。
- **达标判据**：各域抽样复现用例通过（不要求浮点逐字节相同）。
- **挂点动作（2026-10-13，T09 实施段兑现）**：B-08 复现命令位已回填（§2 行）；复现夹具细则按 §10.1 预留路径登记于 testkit.md §6.3/§10.1（`ReproBenchmark.hpp` 三腿执行——计时仅作原始样本留痕）；对照容差档案＝`testdata/tolerance/repro-reduction`（附录 D 第 8 项固定值承载）；抽样面＝kinematics（批量 1vs8 线程）/trajectory（PTP 段链复跑）/dynamics（逆动力学复跑）/selection（电机硬筛选复跑）/optimization（seeded-lhs 复跑）五域各一例；首测报告＝traceability/benchmarks/wp23-t09-repro-baseline.md。

---

## 6. 裁决与开放项登记

| 项 | 状态 | 裁决人与时点 |
| --- | --- | --- |
| 大体绩黄金数据存储与 CI 获取策略（git 内 vs LFS vs 制品）——P-TK-5/O-22 | 未决（在册） | 构建负责人＋WP-02，首个大数据集出现时裁决（DTB §4 O-22 行） |
| 冷启动软指标是否转门禁 | 待基线数据积累后评估 | 需求所有者（转门禁属需求变更，走变更流程——M-14 登记） |
| §4.3 后台负载注入形态、黄金项目生成器明细参数 | 已全部落位（2026-10-13）——T02 半区：后台负载注入形态＋标准规模 ui 侧数据集 manifest 已冻结（ird-perf-b01-interaction@1）；T05 半区：黄金项目生成器明细参数已冻结（ird-perf-b04-golden-project@1——真实产品命令管线＋1,000 数据对象/100 数据修订/每修订 10 对象/负载 2,048 字节确定性文本，另有 createNew 骨架修订 r0 机制面如实登记）。两数据集分属两个基准，不得混用（§3.3） | 已落位（WP-23-T02／WP-23-T05） |

---

## 7. 修订记录

| 版本 | 日期 | 内容 |
| --- | --- | --- |
| v0.1 | 2026-10-13 | WP-23-T01 初版：基准清单（B-01~B-08，覆盖 NFR-PERF-01~06＋PM-16＋NFR-COR-02）、黄金项目定义（PM-16 口径：1,000 对象/100 修订）、测量协议（冷/热分组、每组有效样本 ≥15＋≥15、P95 线性插值、冷启动软指标单独报告不作门禁、并行吞吐不含首次编译与导入、归约容差引附录 D 第 8 项）、T02~T09 达标判据挂点。分支 wp23-t01（DTB 版本行登记延后至验收合入批统一处理） |
| v0.2 | 2026-10-13 | WP-23-T09 执行登记：§2 B-08 复现命令位回填（集成模式三步命令——构建六测试目标＋IRD_REPRO_REPORT_DIR＋ctest 六套件；报告 JSON 三面判定＝集合等价＋稳定排序＋1e-12 归约容差）；§5.8 挂点动作行补登（夹具细则＝testkit.md §6.3/§10.1 ReproBenchmark.hpp；容差档案＝testdata/tolerance/repro-reduction；五域抽样面与首测报告位 traceability/benchmarks/wp23-t09-repro-baseline.md）。本版零协议数值变更——达标口径与统计口径原文照旧（B-08 为集合等价与容差判定类，P95 协议不适用） |
| v0.3 | 2026-10-13 | WP-23-T02 执行登记：§2 B-01 复现命令位回填（冒烟模式三步命令——构建 sdurws_ird_ui_perf_test＋IRD_PERF_REPORT_DIR＋ctest ui 叶子目录；三用例判定＝空闲态五操作 P95 ≤200 ms／后台态同口径＋独立 worker 进程证据／>1 s 转后台用例；逐例报告 JSON 含 p95Convention 线性插值登记与原始样本序列）；§5.1 挂点动作行补登（§4.3 后台负载注入形态＋标准规模 ui 侧数据集 manifest＝testdata/performance/b01-interaction-manifest.json〔ird-perf-b01-interaction@1〕；测量夹具＝testkit.md §6.3/§10.1 PerfBenchmark.hpp；首测报告＝traceability/benchmarks/wp23-t02-interaction-baseline.md）；§6 裁决表后台负载半区翻已落位（黄金项目生成器明细参数仍待 T05）。本版零协议数值变更——达标口径（200 ms/2 s/1 s）与统计口径（P95 线性插值、每组有效 ≥15、禁 3 次中位数）原文照旧 |
| v0.4 | 2026-10-13 | WP-23-T03 执行登记：§2 B-02 复现命令位回填（冒烟模式三步命令——构建 sdurws_ird_execution_perf_test＋IRD_PERF_REPORT_DIR＋ctest execution 叶子目录；两用例判定＝正常收敛腿 1+15 样本阈值判定〔判定统计量＝有效样本最大值——B-02 为时序阈值类非 P95〕＋超时强杀腿 3 场景行为判定〔进程树终止＋OS 级死亡核对＋检查点留存核对〕；逐例报告 JSON 含原始样本序列与 thresholdSource 登记）；§5.2 挂点动作行补登（数据集 manifest＝testdata/performance/b02-cancel-manifest.json〔ird-perf-b02-cancel@2——history 留痕 v1 批次洪流缺陷未产出测量即换版〕；测量夹具＝testkit PerfBenchmark 报告拖挂面＋CancelTimingRig 阈值类采样执行器〔零新增 testkit 头——testkit.md 本版零变更〕；首测报告＝traceability/benchmarks/wp23-t03-cancel-baseline.md）。本版零协议数值变更——达标口径（2 s/10 s）与统计口径区分行原文照旧 |
| v0.5 | 2026-10-13 | WP-23-T04 执行登记：§2 B-03 复现命令位回填（冒烟模式三步命令——构建 sdurws_ird_ui_largedata_perf_test＋IRD_PERF_REPORT_DIR＋ctest ui 叶子目录；三用例判定＝规模/功能类四面断言〔操作全成功＋规模恰为 5,000/100,000/10,000＋不整体装入界面结构不变量〔单页物化 ≤100 行＋全量翻页累计＝N〕＋零无效样本——B-03 无数值时延阈值，零私设〕；逐例报告 JSON 含原始样本序列与判定口径注记）；§5.3 挂点动作行补登（数据集 manifest＝testdata/performance/b03-largedata-manifest.json〔ird-perf-b03-largedata@1〕；测量夹具＝testkit PerfBenchmark＋runInteractionGroup〔零新增 testkit 头——testkit.md 本版零变更〕；测量目标＝sdurws_ird_ui_largedata_perf_test 五域测试面直链〔登记册 traceability/wp23-t04-gate-registrations.md〕；基准驱动产品优化＝kinematics 编码对齐 O(N²)→O(N)〔输出字节逐位不变〕；首测报告＝traceability/benchmarks/wp23-t04-largedata-baseline.md——优化前/后实测对照 save P95 9,011.87 ms→124.10 ms）。本版零协议数值变更——达标口径（5,000/100,000/10,000 规模数值照抄需求原文）与统计口径区分行原文照旧 |
| v0.6 | 2026-10-13 | WP-23-T05 执行登记：§2 B-04 复现命令位回填（冒烟模式三步命令——构建 sdurws_ird_project_perf_test〔连带冷启动采样子进程 sdurws_ird_project_openbench_worker〕＋IRD_PERF_REPORT_DIR＋ctest project 叶子目录；两用例判定＝热启动门禁腿〔进程已运行＋磁盘数据就绪下反复 ProjectStoreFactory::open，测量区间＝open＋HEAD 首读，1 预热＋≥15 有效零无效，P95 线性插值 ≤3,000 ms 判门禁〕＋冷启动软指标腿〔每样本全新子进程＝PM-16"冷启动重置"，P95 对照 8,000 ms 只登记不判定——用例断言面＝样本纪律＋零无效，DTB 行禁止项"冷启动不作门禁"的用例结构承载〕；逐例报告 JSON 含 p95Convention 线性插值登记与原始样本序列）；§5.4 挂点动作行补登（黄金项目生成器明细参数冻结＝testdata/performance/b04-golden-project-manifest.json〔ird-perf-b04-golden-project@1——真实产品命令管线：createNew→handlerRegistry() 公共装配通道注册基准处理器→100 数据修订×10 对象＝1,000 数据对象；createNew 骨架修订 r0＝机制面，项目修订总数 101 如实登记；测量夹具＝testkit PerfBenchmark 报告拖挂＋目标内采样执行器〔T03 CancelTimingRig 先例——零新增 testkit 头〕＋TestProcessRunner 进程夹具；首测报告＝traceability/benchmarks/wp23-t05-golden-project-baseline.md——热启动门禁组 P95 153.01 ms／冷启动软指标组 P95 149.17 ms，两组各 15 有效零无效〕）；§6 裁决表黄金项目生成器明细参数半区翻已落位。本版零协议数值变更——达标口径（热启动 3 s／冷启动软指标 8 s／1,000 对象/100 修订照抄 REQUIREMENTS PM-16 原文）与统计口径区分行原文照旧 |
