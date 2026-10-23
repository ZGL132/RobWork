# F-958 共享治理整合片段（交协调主会话——本包未直接编辑共享文件）

来源：分支 `codex/reuse-testkit-listener`，起始 `6fb26b31d`。以下均为建议片段，由协调者核对编号后统一整合。

## 1. findings.json —— F-958 进展注记建议（**不标 closed**）

F-958 状态保持 `open`；`resolution` 建议追加（或按协调者格式登记为进展注记）：

> 2026-10-23 实施收口（分支 codex/reuse-testkit-listener）：installTestRecordListener 已改为堆对象经 Append 移交（gtest.h:1032 公开契约），新增常驻契约回归＋lsnchild 三态子进程 e2e；legacy 反例 Debug CRT 复现非法 delete 诊断（debug_heap.cpp 904/908 断言），Release 静默。独验通过后方可关闭；共享注释批量订正随协调批次。

## 2. 各单元测试 main 错误注释订正建议（29 文件——均非本分支直接修改）

同一错误声明（"gtest listeners 不接管所有权——官方 recommended 做法"）散布于下列文件。统一替换为：

```cpp
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。
```

（各 main 的调用行 `installTestRecordListener(argc, argv);` 与初始化顺序一律不动，仅订正注释。）

文件与行号（修复前基线 6fb26b31d）：

| 文件 | 注释行 |
| --- | --- |
| drivetrain/contract_test/ContractTestMain.cpp | 28 |
| drivetrain/test/TestMainReport.cpp | 63 |
| dynamics/contract_test/ContractTestMain.cpp | 28 |
| dynamics/test/TestMainReport.cpp | 30 |
| evidence/test/TestMainReport.cpp | 26 |
| execution/contract_test/ContractTestMain.cpp | 47 |
| execution/perf_test/TestMainPerf.cpp | 25 |
| execution/test/TestMainReport.cpp | 34 |
| io/test/TestMainReport.cpp | 38 |
| kinematics/test/TestMainReport.cpp | 34 |
| modeling/test/TestMainReport.cpp | 38 |
| optimization/contract_test/ContractTestMain.cpp | 30 |
| optimization/perf_test/TestMainPerf.cpp | 27 |
| optimization/test/TestMainReport.cpp | 31 |
| policy/test/TestMainReport.cpp | 26 |
| project/perf_test/TestMainPerf.cpp | 27 |
| project/test/TestMainReport.cpp | 35 |
| reporting/contract_test/TestMainReport.cpp | 38 |
| reporting/test/TestMainReport.cpp | 41 |
| requirements/test/TestMainReport.cpp | 35 |
| runtime/test/TestMainReport.cpp | 26 |
| selection/contract_test/ContractTestMain.cpp | 28 |
| selection/test/TestMainReport.cpp | 30 |
| trajectory/contract_test/ContractTestMain.cpp | 29 |
| trajectory/test/TestMainReport.cpp | 30 |
| ui/perf_test/TestMainLargeDataPerf.cpp | 27 |
| ui/perf_test/TestMainPerf.cpp | 37 |
| ui/test/TestMainReport.cpp | 45 |
| workflow/test/TestMainReport.cpp | 32 |

必须更新：上述 29 处注释与修复后实现直接矛盾（同型错误声明的最后残留）。无需更新：kinematics/gui_test/TestMainReport.cpp、ui/gui_test/TestMainReport.cpp、ui/contract_test/TestMainReport.cpp、requirements/gui_test 等仅描述"报告与 XML 并存"的 main 注释未含所有权声明；units/testkit.md、testkit README 无错误声明（已核）。

## 3. catalog.json / test-build-inventory.json —— testkit 共享头绑定摘要建议

- 绑定对象不变：`testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp::installTestRecordListener`（签名、消费者清单、`--ird_report` 语义零变化）。
- 摘要需刷新的语义点：所有权从"静态存储期对象（错误声明）"改为"堆对象经 Append 移交、gtest 集合统一释放（gtest.h:1032 公开契约）"。
- 新增测试面（如 inventory 收录 testkit 工具先例 prochelper，则同型增列）：`sdurws_ird_testkit_lsnchild`（tools/lsnchild，F-958 三态子进程载体，other 形态目标）＋`test/ListenerOwnershipTest.cpp`（UT_LSN 五用例）。
- 直接受影响绑定位置：test-build-inventory.json 监听器/testkit 入口条目；REUSE-T01 test-build-audit.md §"已证实适配契约问题：监听器所有权"段（T05 行可注明已实施待独验）。

## 4. 门禁说明

- 本包未放宽任何门禁；testkit 配置期依赖红线守卫（T-2/R-3/D-06）随集成配置成功通过。
- `validate-reuse.ps1 -Head 798f32368` 实跑 **FAIL（1 项，日志 `validate-reuse.log`）**：
  `REUSE-UNDECLARED : RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt : bdf5362e81df46b89bb4045a7244389245ae971d761a6ecc757f2b4920a3ea0d`
  原因＝本包新增/改动的产品侧文件尚未在共享逐文件凭证（catalog.json/permissions.json）登记——共享文件归协调会话整合，本包按约束不直接编辑。整合时需登记的文件清单（原因见 README"修复内容"）：
  1. `testkit/CMakeLists.txt`（M，blob bdf5362e…）——lsnchild 目标与 ListenerOwnershipTest 注册；
  2. `testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp`（M）——安装接缝所有权修复＋注释订正；
  3. `testkit/src/Report.cpp`（M）——失真注释最小订正；
  4. `testkit/test/ListenerOwnershipTest.cpp`（A）、`testkit/tools/lsnchild/main.cpp`（A）——F-958 定向测试面。
- 整合后由协调者复跑该门禁确认转绿；本包不再重复执行相同失败命令。
