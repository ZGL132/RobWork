# WP-14-T09 验证留痕（requirements 契约测试套件与四套黄金数据集收口）

> 实施会话留痕（PIPE §5.7／AGENTS §4.2）。分支 `wp14-t09`；base＝
> a5b3b397f96109adb2fc377aefb0a9c1e5349fae。构建与测试在本任务 worktree
> `D:/10_Source_Repos/21_robot/wt-wp14-t09` 完成（主检出被 WP-24 系并行
> 任务占用——EX 系纪律，改用独立 worktree，未触碰主检出状态）。

## 文件清单

| 文件 | 内容 |
| --- | --- |
| `build-integrated-targets.log` | 集成模式构建日志（worktree 内新配 build 树：`cmake -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON`＋vcpkg toolchain＋Qt prefix；缓存确认 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 后构建 `sdurws_ird_requirements_test`/`_contract_test`/`sdurws_ird_testdata_lint` 三目标——零错误，`requirements.lib`/`_plugin.lib`/两测试 exe 全部产出） |
| `run-requirements-test.log` | 集成模式 `sdurws_ird_requirements_test` 运行输出：**183 用例＝182 通过＋1 跳过**（`GuiRegistration.V22RegisteredNotExecuted_ACC1`——V-22 GUI 仅登记，envUnavailable 如实登记，不绿灯） |
| `run-contract-test.log` | 集成模式 `sdurws_ird_requirements_contract_test` 运行输出：**11 用例全通过** |
| `gtest-requirements-test.xml` | gtest XML 原样留存（`--gtest_output=xml`，未手工转抄计数） |
| `gtest-requirements-contract-test.xml` | 同上（契约测试目标） |
| `ird-test-report-unit.json` | ird-test-report.json 用例级明细（TestRecordListener 聚合；V-22 记录 outcome=envUnavailable；GoldenFixture 用例携带数据集/档案/种子追溯字段） |
| `ird-test-report-contract.json` | 同上（契约测试目标） |
| `ctest.log` | `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"`——2/2 通过（契约 verify 第 3 条原样执行） |
| `testdata-lint.log` | lint 工具（TK-T03）全量扫描：**13 个数据集版本 0 违规**（含本任务四套 req-*）。注：lint 工具缺省字典路径 `<root>/../requirements-ids.json` 与数据根布局不符，本任务以 `--dict <root>/requirements-ids.json` 显式传参执行（TK-T03 侧修正归其所有者——已登记卡 §14.6 v0.9⑤） |
| `ird_gates-base.log` / `ird_gates-head.log` | 门禁引擎直跑原始输出（`cmake -DIRD_ROOT=<源码根> -P cmake/ird_gates.cmake`；base 侧临时 worktree `D:/wt-wp14-t09-base`） |
| `ird_gates_base_hits_normalized.txt` / `ird_gates_head_hits_normalized.txt` | 归一化输出（worktree 路径前缀→`<IRD_ROOT>`） |
| `ird_gates_base_head_comparison.md` | base..head 归一化比对结论：**全部命中码计数零增量**（LIB 5/R1 3/R3 20/R4 1/SUB 33/T1 2——本任务只增测试源文件与 testdata 资产，不动产品链接面/include 面） |
| `validate-docs.log` | `validate-docs.ps1`：PASS（20 units, 12 trace entries, 189 task files） |
| `validate-task.log` | `validate-task.ps1`（本任务契约）：PASS |

## 冒烟模式（AGENTS §4.1 双模式另一半）

worktree 内独立配置：`cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build_smoke_t09 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/vcpkg.cmake -DCMAKE_PREFIX_PATH=<Qt>`——三目标零错误构建后全量测试通过与集成模式一致（同一代码态）。

## verify 命令逐条执行记录（契约 verify 数组）

| 契约 verify | 执行 | 结果 |
| --- | --- | --- |
| `cmake --build build --config Release --target sdurws_ird_requirements_test` | 是（worktree 集成 build 树；本任务还配了 vcpkg toolchain＋Qt prefix——主检出 build/ 被 WP-24 并行占用，EX 系纪律） | 零错误 |
| `cmake --build build --config Release --target sdurws_ird_requirements_contract_test` | 是（同上） | 零错误 |
| `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"` | 是 | 2/2 通过 |
| `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | 是 | PASS |
