# wp08-t10（EX-T10 文档与门禁同步）零偏差核对审计记录

任务：EX-T10（卡内治理任务，契约 `tasks/foundation/EX-T10.json`；分支 `wp08-t10`，
base `ddf2031f85fac89e9c0042a0bba2f4c8f1cf882a`）。
性质：**纯文档任务零代码变更**（execution 的 include/src/CMakeLists/worker 零触碰——
`git diff --stat` 可复核），DoD §5.2-1 构建项按"纯文档任务免除"免除；本目录另以
**两套件复跑＋ird_gates 命中集合比对**提供复证证据（非本任务构建义务）。
核对日期：2026-09-19。核对者：EX-T10 实施段。

## §1 文件清单盘点（units/execution.md §3.1/§3.5/§15.4 vs 磁盘实测）

| 项 | §3.1/登记口径 | 磁盘实测（industrialrobot/execution/） | 结论 |
| --- | --- | --- | --- |
| 公共头 | 12 契约头（Errors/TaskTypes/Scheduler/Controller/StateMachine/RunRegistry/WorkerSupervisor/ChannelProtocol/Checkpoint/CacheCoordinator/EventBus/Ports）＋README.md | `include/sdurws/ird/execution/` 下恰 12 头＋README.md，逐名对应、零缺零增 | 一致 |
| worker 入口 | `worker/main.cpp`（目标 `sdurws_ird_execution_worker`） | `worker/main.cpp` 存在；CMake 目标同注 | 一致 |
| src/ 翻译单元 | §15.4 v0.3~v0.11 累加：EX-T01 `Execution.cpp`（1）→ EX-T02 TaskTypes/Errors/StateMachine（3）→ EX-T03 Controller/Scheduler（2）→ EX-T04 RunRegistry/Admission（2）→ EX-T05 EventBus（1）→ EX-T06 ChannelProtocol/WorkerSupervisor（2）→ EX-T08 Checkpoint/CacheCoordinator（2）＝13；win32/ 4 对（EX-T06 ChannelPair/JobScope/ProcessLauncher＋EX-T07 MemProbe） | `src/` 恰 13 个 .cpp＋`src/win32/{ChannelPair,JobScope,MemProbe,ProcessLauncher}.{hpp,cpp}` 4 对 | 一致（累加口径同 DIAG-T11 先例） |
| test/ | EX-T01~T09 逐任务增列累加 | 10 个 .cpp（8 用例文件＋TestMainReport＋ContractSuiteFacilitiesTest）＋ContractSuiteFacilities.hpp（替身设施仅头） | 一致 |
| contract_test/ | 同上 | 10 个 .cpp（8 契约用例＋ContractTestMain＋RecoveryChild）＋RecoveryChild.hpp | 一致 |
| 链接面（§3.2/CMakeLists 注） | 产品目标 PUBLIC 链 core/evidence/project；diagnostics 边按注入形态不落链接（P-EX-8）；worker 链 core/evidence；测试目标链 testkit（T-1 允许形态）；零 Qt | `execution/CMakeLists.txt` 第 208/162/285/359 行逐一实测相符 | 一致 |

## §2 README 指向核对（acceptance 3 后半）

`execution/include/sdurws/ird/execution/README.md` 任务卡指向＝本文 §12（§9→§12 修正于
2026-09-10 版完成；EX-T01 复核零偏差＝§15.4 v0.3；本次再复核零偏差，无回退、无第二指向）。

## §3 两套件复跑（零偏差登记的复证）

| 套件 | 结果 | 与 §15.4 v0.11 登记基数 |
| --- | --- | --- |
| `sdurws_ird_execution_test` | 149/149 PASSED（36 套件，集成构建树 Release 二进制原样复跑） | 逐数一致（149/149） |
| `sdurws_ird_execution_contract_test` | 38/38 PASSED（8 套件，真进程用例含 worker 派发/崩溃/失联） | 逐数一致（38/38） |

日志：`ird-execution-test-reverification-run.log`／`ird-execution-contract-test-reverification-run.log`。
两套件零改动（本任务无代码变更），零回归。

## §4 ird_gates 命中集合比对（acceptance 4 前提事实）

`cmake --build build --config Release --target ird_gates`（fail-loud 设计，命中均为
findings.json 已登记项）；归一化排序后与 wp08-t09 验收基线比对：**GATES-IDENTICAL-18**
（18 行逐项一致，新增 0／移除 0；F-220 同型 GBK 吞字修复与 CRLF 行尾归一说明见
`gates-hit-set-diff.txt`）。原始日志 `gates-ird-gates-target.log`、当前命中集合
`gates-t10-hits-sorted.txt`。

## §5 治理脚本（契约 verify ＋ 工程自查）

| 脚本 | 结果 |
| --- | --- |
| `validate-docs.ps1`（契约 verify 命令） | PASS（20 units, 12 trace entries, 150 task files）——`validate-docs.log` |
| `validate-task.ps1`（工程流程自查） | PASS（1 tasks）——`validate-task.log` |
| 测试执行留痕说明 | 本任务纯文档零代码变更、无新增/修改用例——不产出新 gtest XML／ird-test-report.json；执行证据＝本目录 §3 复跑日志（基线 XML 见 builds/wp08-t09/）。如实登记，不虚构留痕 |

## §6 索引一致性核对（P-EX-10 消账依据，acceptance 2/3）

| 索引 | 实测值 | 与实际状态 |
| --- | --- | --- |
| `DETAILED-DESIGN.md` 20 单元状态表 execution 行（第 30 行） | "Draft / 已有任务卡" | 与 §1.2 预登记的目标状态一致（该行已于 2026-09-10 首批索引同步落位）；与卡头 `状态 Draft`、卡已编写一致 |
| `traceability/unit-status.json` execution 条目 | status=draft、designCompletion=written、implementationStatus=not-verified | 前两项一致；implementationStatus 为 2026-09-10 快照字段（全 20 单元均 not-verified，含已实现并验收的 project/diagnostics 等——跨单元索引治理面，不属本任务单方改写范围） |
| 处置 | P-EX-10 消账登记于本文 §15.3（依据＋日期）；实现进度反映的状态更新申请提交详设目录维护者（`traceability/wp08-t10-detailed-design-status-request.md`），不私改共享索引 |
