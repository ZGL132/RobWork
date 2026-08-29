# WP-23 系统质量与规模化实施计划

> 阶段/发布：阶段 A 起持续建设，规模化门禁阶段 D 收口 / R1+R2（总纲 §9：WP-23 从阶段 A 起持续建设，不在阶段 D 才开始补测试）；人周 8～12（总纲 §5.4）。
> 负责范围：AT-01～19 系统级执行清单、故障注入矩阵、确定性/并行复现测试、性能与内存基准、恢复演练和 R1/R2 发布门禁证据。
> 责任分离：门禁资产实现者、被测模块实现者与独立质量负责人必须是不同执行上下文；评审者不得参与被测实现（module-design/system-quality.md §7）。

**需求与契约：** AT-01～19、NFR-COR、NFR-PERF、NFR-REL；引用 `architecture/testing-contract.md`、`architecture/evaluation-semantics.md`、`architecture/execution-model.md`、`architecture/persistence-schema.md`；模块方案 `module-design/system-quality.md`（v0.3）。

**拥有目录：** `industrialrobot/testkit/system/`（含 scenarios/faultpoints/benchmarks/drills/test）与 `out/test-evidence/wp-23/<run-id>/`；`benchmark-manifest.json` 为只读输入；不得修改业务实现以绕过门禁。

**输入/输出：** 输入＝各阶段模块测试目标与 failpoint 夹具、WP-02 黄金数据与断言库、WP-01 门禁入口、基准清单；输出＝AT 执行矩阵、故障注入日志、基准报告（含全部样本）、恢复统计与 R1/R2 门禁证据。

## 1. 目标与非目标

**目标：** 把需求 §15.2 的 19 个核心场景与 §11 非功能需求转为可重复执行、可审计的系统级验收资产，并按阶段持续交付，使 R1/R2 发布门禁证据可由脚本与清单复核。
- 完成定义：AT-01～19 逐条有登记的执行入口、数据集与通过判据（§4 表）；8 类故障注入全部有恢复断言证据；固定 seed/threadCount 下集合与数值可复现；性能交付断言在验收机全部满足；R1/R2 门禁清单字段完整并由独立质量负责人签署。
- 目标交付：`AtRegistry`、`FaultInjectionMatrix`、`BenchmarkRunner`、`RecoveryDrill`、`ReleaseGate` 五组实现与五个测试目标、`scenarios/faultpoints/benchmarks/drills` 夹具、`out/test-evidence/wp-23/<run-id>/` 证据集。

**非目标：** 不修改业务实现以绕过门禁；不拥有业务黄金数据（WP-02 所有）；不替代 WP-01 构建门禁；不无批准修改 `benchmark-manifest.json`（module-design/system-quality.md §1、§2）。

## 2. 需求、契约与持续建设节奏

- 需求：AT-01～19、NFR-COR-01～05、NFR-PERF-01～06、NFR-REL-01～05；§15.3（冻结容差与"并行复现"行）、§15.4（缺陷等级与发布规则）、§16 追踪行。
- 架构契约：`architecture/testing-contract.md`（§1 分层、§2 数值与性能、§3 Given/When/Then、§4 证据命名、§5 Windows 规则）、`architecture/execution-model.md`、`architecture/persistence-schema.md`、`architecture/evaluation-semantics.md`（§1～2 状态语义与合法组合、§5 展示义务）、`architecture/symbol-registry.md`。
- 模块方案：`module-design/system-quality.md`（v0.3；本计划对齐其 §2～§8）。
- 输入：各阶段模块测试目标与 failpoint 夹具、WP-02 黄金数据集与断言库、WP-01 门禁入口、`benchmark-manifest.json`（只读，变更须产品与测试双负责人批准）。

持续建设节奏（module-design/system-quality.md §1，阶段门禁对齐需求 §14）：

| 阶段 | WP-23 交付子集 |
| --- | --- |
| A | A-GATE-01～07 脚本化＋故障注入雏形 |
| B | AT-01～05、15～18 的 B 链路子集 |
| C | AT-06～08、19 的 C 链路子集 |
| D | AT-09～14 与 AT-18/19 全链路＋规模化基准 |
| E | 发布门禁汇总（对接 WP-24/WP-25） |

## 3. 文件所有权与构建目标

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/
  include/sdurws/ird/system/
    AtRegistry.hpp FaultInjectionMatrix.hpp BenchmarkRunner.hpp RecoveryDrill.hpp ReleaseGate.hpp
  src/AtRegistry.cpp FaultInjectionMatrix.cpp BenchmarkRunner.cpp RecoveryDrill.cpp ReleaseGate.cpp
  scenarios/at-01…at-19/  faultpoints/  benchmarks/  drills/
  test/SystemSuiteTest.cpp FaultInjectionTest.cpp BenchmarkTest.cpp DeterminismTest.cpp ReleaseGateTest.cpp
  # 证据 → out/test-evidence/wp-23/<run-id>/（AGENTS §3，不入源码树）
```

- CMake 目标（与任务卡命令一致）：`sdurws_ird_system_suite_test`、`sdurws_ird_fault_injection_test`、`sdurws_ird_benchmark_test`、`sdurws_ird_determinism_test`、`sdurws_ird_release_gate_test`；`sdurws_ird_system_test` 为聚合别名目标（运行全部五域）。
- 允许依赖：WP-02 testkit、各模块测试目标与 failpoint 夹具、WP-01 脚本。
- 禁止：绕过统一测试入口直改业务代码；写回 testdata；无批准修改 `benchmark-manifest.json`（module-design/system-quality.md §2）。

## 4. AT-01～19 执行清单（T01 交付的 AtRegistry 内容）

权威判据以 `module-design/system-quality.md` §3 与需求 §15.2/§16 为准；本表为执行视图，追加"归属任务"列标示本 WP 内分工。

| AT | 覆盖需求（§16） | 阶段 | 执行入口 | 数据集（WP-02） | 通过判据 | 归属任务 |
| --- | --- | --- | --- | --- | --- | --- |
| 01 | MDL-01～04、07、09～12 | B | 建模：DH/显式/URDF 三入口建同一模型 | models 三套等价模型 | 权威类型正确；Origin/Axis/限制/Home/Zero/FK 在 §15.3 关节/FK 容差内一致 | T01 |
| 02 | REQ-05、07 | B | 需求：导入含错误行任务表 CSV | requirements 错误行样本 | 正确行保留、错误行定位字段与单位；预览不入正式证据 | T01 |
| 03 | REQ-01～04、KIN-01～05 | B | 运动学：批量工位验证 | requirements 可行/不可行任务集 | 状态、原因、候选详情与三维姿态一致；缺证据＝DataInsufficient | T01 |
| 04 | KIN-06～08、TRJ-07 | B | Jog、IK 预览、轨迹播放 | models＋requirements 标准工位 | 修订号与结果当前性不变（A-GATE-01 同口径） | T01 |
| 05 | CON-05、KIN-08 | B | 修改 TCP 并应用；单独改电机成本 | 同上 | TCP→实际依赖下游 Superseded；电机成本不使运动学/轨迹失效 | T01 |
| 06 | TRJ-01～06 | C | 轨迹：Verified 规划搬运轨迹 | trajectory 基准（接近/撤离/避障） | 碰撞协议结论；限值超差≤1e-6 且节拍规则满足（§15.3 轨迹行） | T01 |
| 07 | DYN-01～03、05～08 | C | 动力学：逆动力学＋正动力学收敛 | dynamics 解析算例 | 相对误差≤1e-6（近零下限 1e-8）；h 与 h/2 收敛；峰值可定位 | T01 |
| 08 | SEL-01～09 | C | 选型：器件筛选 | catalog 可行/淘汰/范围外样本 | 可行通过；淘汰含实际值/阈值；移动关节"范围外"诊断 | T01 |
| 09 | OPT-01～10 | B（静态子集）/D（全量） | 优化：分层联合优化 | optimization 基线/改进/硬约束失败候选 | 可行 Pareto 集不含硬约束失败候选；固定种子复现 | T01（复现断言对接 T04） |
| 10 | TASK-03、NFR-REL | D | 优化期间切换/关闭项目 | optimization 长任务样本 | 旧结果只追加原分支历史；项目完整 | T01 |
| 11 | CON-04、TASK-01 | D | 中断并恢复长任务 | optimization 检查点样本 | 从兼容检查点恢复；已完成批次不重复计入 | T01（恢复统计对接 T02/T03） |
| 12 | OPT-07～09、MDL-08 | D | 应用优化候选 | 同 AT-09 | 方案分支＋恰好一个新修订；基线不覆盖；运行期间修订数不随候选增长 | T01 |
| 13 | NFR-REL-02、TASK-01 | A 起（D 全链路） | 注入 worker 崩溃 | faultpoints | 主程序存活、项目可恢复、任务显示 Failed（A-GATE-05 同口径） | T01＋T02 |
| 14 | NFR-PERF-03～06 | D | 大型负载 | performance 样本（5,000 任务/100k 采样/10k 候选） | 界面持续响应（§11.2 预算）；无候选静默丢失、无错误写入 | T01＋T03 |
| 15 | MDL-03、11、12 | B | URDF 轴样本导入 | models URDF 边界集 | 非 Z 轴保持原值＋运行时等价；缺失轴＋X 草稿；零轴仅报告且修订不变 | T01 |
| 16 | MDL-01、02、09、10 | B | DH↔显式权威转换 | models 四类转换判定样本 | 四类判定正确；`AnalysisFailed` 注入生效；切换只产生一个新修订 | T01 |
| 17 | MDL-03、11、12 | B | URDF 拓扑样本 | models 分支/关节类型样本 | 固定附件完整导入；多分支未选择不成模；不支持关节阻断不降级 | T01 |
| 18 | ARC-04、MDL-14 | B/C/D 分链路 | 编译＋导入＋重命名后八链路解析 | models 名称作用域样本 | objectId 绑定不变；无未解析/旧/双前缀（A-GATE-06 同口径） | T01 |
| 19 | ARC-05、NFR-COR-05 | B/C/D 分入口 | 运动学/轨迹/优化三入口同状态求值 | collision 策略样本 | 对象 ID 碰撞对、判定、原因完全一致；显示开关不改判定（A-GATE-07 同口径） | T01 |

## 5. 故障注入矩阵（T02）

对接 WP-04/WP-08 failpoint（module-design/system-quality.md §4）；恢复断言即测试断言，逐条出证据。

| 故障类别 | 注入点 | 恢复断言 |
| --- | --- | --- |
| 版本目录写入失败 | WP-04 staging failpoint | 旧 HEAD/树哈希不变；重启忽略 staging（A-GATE-04） |
| 资源写入失败／提交指针切换失败 | WP-04 资源与 HEAD failpoint | 逐文件校验一致；项目可再次打开 |
| worker 崩溃／IPC 中断 | WP-08 worker failpoint | 任务 Failed 附退出原因；主界面存活；检查点保留（AT-13） |
| 取消超时强杀 | WP-08 取消时限 failpoint | Canceled＋"强制终止"诊断；最近兼容检查点可恢复 |
| 磁盘满／内存阈值 | WP-08 资源 failpoint | 先节流后 `IRD-EXEC-RESOURCE-BUDGET`；无项目损坏 |
| 外部资源缺失/变化 | WP-05 源监控样本 | `IRD-PERSIST-SOURCE-*`；重新关联入口可用 |
| 结果工件损坏 | results 目录哈希篡改 | 读回 `ArtifactIntegrity=Corrupt`；拒绝正式用途 |
| 报告渲染失败 | WP-12 渲染 failpoint | 既有工件保留；`IRD-RPT-RENDER-FAILED` 可重试 |

## 6. 确定性、性能基准与恢复演练

- 并行复现（T04，§15.3"并行复现"行）：固定 seed 20260828、threadCounts [1,8]（取自 `benchmark-manifest.json`）下候选稳定 ID、可行集合与 Pareto 支配关系一致；数值满足算法级相对/绝对误差；不要求浮点文件逐字节相同（NFR-COR-02）。
- 性能与内存基准（T03）：按 `benchmark-manifest.json` 全字段执行——验收机 16 核/64GB/SSD、数据集 `ird-standard-six-axis-v1`、预热 2＋测量 3、冷热启动均记录、中位数聚合且保留全部样本、后台负载（1 个 5,000 任务批处理 worker）、排除首次依赖编译与数据导入。交付断言：交互 P95≤200 ms（NFR-PERF-01）、5,000 任务批处理 30 min、100k Quick 样本 20 min、8 线程加速比≥4、标准优化基准（10,000 Quick＋100 Verified 候选）8 h 内、内存峰值≤物理内存 70% 且节流先于诊断。
- 恢复演练（T02）：断电（进程终止注入）→ 重启发现 `Interrupted` 不伪装完整结果；崩溃 → `Failed`；锁夺取 → 心跳超时夹具（WP-04 默认 30 s）后安全夺取；恢复后批次统计不重复（A-GATE-05、AT-11）。

## 7. R1/R2 发布门禁证据（T05）

- 每条门禁证据按 testing-contract §4 字段：任务 ID、需求 ID、AT/门禁 ID、提交 SHA、环境（机器/OS/编译器/线程）、命令原文、输入哈希（datasetVersion＋manifest SHA-256）、实际/期望结果、日志路径、评审者、结论。
- R1 gate＝阶段 B/C 退出条件 AT 子集＋NFR-PERF-01～03＋NFR-REL；R2 追加＝阶段 D 退出条件 AT-09～14＋NFR-PERF-04～06＋确定性＋恢复演练。
- 缺陷口径按 §15.4：发布时开放 Blocker＝0、未关闭 Critical 均有负责人与计划日期；"不得以关闭测试、降低阈值或隐藏诊断代替修复"由门禁脚本断言（与既有阈值/诊断清单比对）。

## 任务

### WP-23-T01 系统验收测试套件

- **范围：** `AtRegistry.hpp/.cpp`、`scenarios/at-01…at-19/`、`test/SystemSuiteTest.cpp`；按 §4 清单逐条登记执行入口、数据集与判据，并按 §2 节奏分阶段接入（A 阶段先落 A-GATE-01～07 脚本化所需场景骨架）。
- **前置：** 任务级无；WP 级＝WP-01 门禁入口、WP-02 黄金数据与断言库、对应阶段被测模块测试目标。
- **输出工件：** AtRegistry（19 条执行条目）、at-01～at-19 场景夹具、AT 执行矩阵（`out/test-evidence/wp-23/<run-id>/`）、目标 `sdurws_ird_system_suite_test`。
- **验收断言：** ①§4 表 19 条逐条可执行且判据与 system-quality.md §3 一致；②AT-03 缺证据结果判 `DataInsufficient` 且展示符合 evaluation-semantics §5（不与"不可行"混排）；AT-05 下游显示"需要重算"（Superseded 口径）；③每条 AT 至少含失败/正常/边界断言（testing-contract §1、§3）；④GUI 相关 AT 单独启动、一次一个（testing-contract §5）；⑤固定种子与输入哈希进入每条证据。

### WP-23-T02 故障注入与恢复

- **范围：** `FaultInjectionMatrix.hpp/.cpp`、`faultpoints/` 夹具、`RecoveryDrill.hpp/.cpp`、`drills/`、`test/FaultInjectionTest.cpp`；落地 §5 的 8 类矩阵与 §6 恢复演练三场景。
- **前置：** T01；WP 级＝WP-04/WP-08 failpoint、WP-05 源监控样本、WP-12 渲染 failpoint。
- **输出工件：** 故障注入矩阵夹具、恢复演练脚本与恢复统计（`out/test-evidence/wp-23/<run-id>/`）、目标 `sdurws_ird_fault_injection_test`。
- **验收断言：** ①§5 矩阵 8 类逐条恢复断言成立且留日志（system-quality.md §4）；②A-GATE-04/05 口径在系统级复验通过；③锁夺取演练使用心跳超时夹具（WP-04 默认 30 s）后安全夺取；④结果工件损坏读回 `ArtifactIntegrity=Corrupt` 并拒绝正式用途（evaluation-semantics §1～2）；⑤恢复后批次统计不重复（AT-11）。

### WP-23-T03 性能与规模基准

- **范围：** `BenchmarkRunner.hpp/.cpp`、`benchmarks/`、`test/BenchmarkTest.cpp`；只读消费 `benchmark-manifest.json` 全字段，产出 AT-14 大型负载与 NFR-PERF 门禁数据。
- **前置：** T01；WP 级＝WP-02 performance 数据集（5,000 任务/100k 采样/10k 候选）、`benchmark-manifest.json` 已冻结。
- **输出工件：** 基准报告（含全部样本，中位数聚合）、验收机/开发机区分标注、目标 `sdurws_ird_benchmark_test`。
- **验收断言：** ①§6 六项交付断言在验收机全部满足（system-quality.md §5）；②预热 2＋测量 3、冷热启动、后台负载与排除项按 manifest 执行；③无批准不改 manifest（`approvalRequiredForChange`）；④开发机结果一律标注"非基准"；⑤过程无候选静默丢失、无错误写入（AT-14）。

### WP-23-T04 并行确定性

- **范围：** `test/DeterminismTest.cpp`，复用 T01 优化场景与 WP-02 `StableSetAssertions`；覆盖 §15.3"并行复现"行。
- **前置：** T01、T03（seed/threadCount 口径来自 manifest）。
- **输出工件：** 确定性对照报告（`out/test-evidence/wp-23/<run-id>/`）、目标 `sdurws_ird_determinism_test`。
- **验收断言：** ①seed 20260828、threadCounts [1,8] 下候选稳定 ID、可行集合与 Pareto 支配关系一致；②数值满足 §15.3 算法级相对/绝对误差；③不要求浮点文件逐字节相同（NFR-COR-02）；④AT-09 固定种子复现断言由本目标证据支撑。

### WP-23-T05 质量与发布门禁

- **范围：** `ReleaseGate.hpp/.cpp`、`test/ReleaseGateTest.cpp`；汇总 T01～T04 证据为 R1/R2 门禁清单，实现防作弊断言脚本。
- **前置：** T01、T02、T03、T04。
- **输出工件：** R1/R2 门禁清单（testing-contract §4 字段）、防作弊比对输出、独立质量负责人签名页（`out/test-evidence/wp-23/<run-id>/`）、目标 `sdurws_ird_release_gate_test`。
- **验收断言：** ①R1/R2 gate 构成与 system-quality.md §6 一致且逐条有证据；②发布时开放 Blocker＝0、未关闭 Critical 均有负责人与计划日期（§15.4）；③"不得以关闭测试、降低阈值或隐藏诊断代替修复"由脚本与既有阈值/诊断清单比对断言；④每条证据含输入哈希（datasetVersion＋manifest SHA-256）与评审者。

## 验证

脚本与原生双形式，均在仓库根执行；仅此 WP（真实代码测试）使用双形式命令：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_(system_suite|fault_injection|benchmark|determinism|release_gate)_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_system_suite_test sdurws_ird_fault_injection_test sdurws_ird_benchmark_test sdurws_ird_determinism_test sdurws_ird_release_gate_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_(system_suite|fault_injection|benchmark|determinism|release_gate)_test$"
```

- GUI 相关 AT 单独启动、一次一个并设置 `QT_QPA_PLATFORM=windows`；模型级断言可用 `QCoreApplication` 入口（testing-contract §5）。
- 性能与内存目标只在验收机执行（module-design/system-quality.md §7）。
- 失败处理：任何 AT/门禁失败均登记缺陷（§15.4 等级），不得以调整容差、跳过场景或改写 manifest 方式"通过"。

## 10. 独立评审与证据

- 独立质量负责人复核 AT 执行矩阵、故障注入日志、基准报告（含全部样本）、恢复统计、门禁清单与防作弊断言输出，并签署（评审者不得参与被测实现）。
- 证据归档于 `out/test-evidence/wp-23/<run-id>/`，字段按 testing-contract §4；报告须能追溯 datasetVersion、manifest SHA-256、seed、threadCount、命令原文与提交 SHA。

## 11. 迁移与删除

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 历史测试结果 | 只读归档 | — |
| 失效测试入口与重复基准配置 | Delete | 新系统门禁稳定后（本计划 T05 收口） |
| 绕过统一入口的私有测试脚本 | Delete | WP-01 入口覆盖全部五域 |

## 退出条件

- AT-01～19、NFR-COR、NFR-PERF、NFR-REL 按阶段子集全部通过，全量于阶段 D 收口。
- §5 矩阵 8 类恢复断言、§6 六项性能交付断言、确定性对照与恢复演练均有证据。
- R1/R2 门禁清单完整、字段齐全、防作弊断言通过、开放 Blocker＝0。
- 性能报告引用 `benchmark-manifest.json` 且所有失败可追溯到缺陷登记。

## 任务卡索引

- [WP-23-T01 系统验收测试套件](../agent-tasks/WP-23-T01-system-suite.md)
- [WP-23-T02 故障注入与恢复](../agent-tasks/WP-23-T02-fault-injection.md)
- [WP-23-T03 性能与规模基准](../agent-tasks/WP-23-T03-benchmark.md)
- [WP-23-T04 并行确定性](../agent-tasks/WP-23-T04-determinism.md)
- [WP-23-T05 质量与发布门禁](../agent-tasks/WP-23-T05-release-gate.md)
