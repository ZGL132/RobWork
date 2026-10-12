# WP-15-T16 验证留痕（独立冒烟模式）

- 任务：WP-15-T16 独立位姿可达性分析（KIN-11/AT-26），分支 `wp15-t16`，工作树 `build/wp15-t16-wt`。
- 日期：2026-10-13。
- 口径：独立冒烟模式（AGENTS §4.1——vcpkg toolchain＋Qt6 msvc2022_64 前缀；构建树
  `build-smoke-wp15-t16/`，不入库）。

## 构建结论

- 配置＋产品库 `sdurws_ird_kinematics`：零错误（`build-log-smoke.txt`）。
- 测试目标 `sdurws_ird_kinematics_test` / `sdurws_ird_kinematics_contract_test`：零错误。

## 测试结论（gtest 两目标）

- `sdurws_ird_kinematics_test`：**195 用例＝194 通过＋1 跳过＋0 失败**
  （跳过项 `PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1` 为既有 gating
  口径——WP-15-T11 留痕先例同款；其中 KinPoseReachability 17 用例为本卡新增，
  全部通过）。
- `sdurws_ird_kinematics_contract_test`：**57/57 通过**（含 BuildGraphContractTest
  产品面红线扫描——`PoseReach.hpp/.cpp` 零 Qt/六边 include 面自证）。
- 集成模式构建：**未执行——WIP**（超出本会话预算，未声称零错误）。

## 文件清单

| 文件 | 内容 |
| --- | --- |
| `build-log-smoke.txt` | 冒烟模式配置＋三目标构建日志（尾部） |
| `kin_test_smoke.xml` | 单元测试 gtest XML（gtest_output） |
| `kin_test_smoke_log.txt` | 单元测试控制台输出全文 |
| `kin_contract_smoke.xml` | 契约测试 gtest XML |
| `kin_contract_smoke_log.txt` | 契约测试控制台输出全文 |
| `ird-test-report.json` | testkit 用例级明细（IRD_TEST_INFO 追溯字段） |

## 门禁（ird_gates）

- 未执行——本会话未运行 ird_gates 引擎比对（如实登记；WP-01-T01 门禁的
  base..head 归一化比对留待验收段执行）。
