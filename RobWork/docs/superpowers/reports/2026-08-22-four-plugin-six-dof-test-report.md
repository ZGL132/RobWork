# 四插件六自由度机械臂全功能测试报告

> 测试计划：`docs/superpowers/plans/2026-08-22-four-plugin-six-dof-full-test-plan.md`
>
> 本报告持续追加。未执行项必须保持 `NOT_RUN` 或 `BLOCKED`，不得伪装为通过。

## 运行信息

| 字段 | 值 |
|---|---|
| 测试日期 | 2026-08-22 |
| 主仓库 | `D:\10_Source_Repos\21_robot\RobWork\RobWork` |
| 计划构建目录 | `build/codex-vs-debug5` |
| Qt 平台 | Windows 规则要求 `QT_QPA_PLATFORM=windows` |
| 6DOF 基线 | `GenericSixAxis.wc.xml` / `GenericSixAxisScene.wc.xml` |
| 固定 seed | `20260727`（适用时） |
| 当前提交 | 工作树未提交（保留用户已有修改） |
| 测试构建 | `codex-vs-debug5`，Debug，MSVC x64 |
| CTest 注册总数 | 109 |

## 状态定义

- `PASS`：有完整日志和可复核证据。
- `FAIL`：观察到功能、数据、界面或一致性错误。
- `BLOCKED`：环境、缺失可执行文件或外部依赖阻断，不能当作通过。
- `NOT_RUN`：尚未执行。

## 已发现问题

### F-001：RobotModelBuilder CTest 注册了未生成的三个 executable（已解除）

- 严重级别：P1（发布阻断）
- 状态：RESOLVED（重新构建后已生成并执行）
- 发现阶段：环境基线检查
- 复现命令：

  ```powershell
  ctest --test-dir RobWork/build/codex-vs-debug5 -N
  ```

- 初始现象：CTest 注册了三个目标，但构建输出目录未找到对应 executable。
- 复核结果：重新构建后已生成 `sdurws_robotmodelbuilder_metatest.exe`、`sdurws_robotmodelbuilder_jsontest.exe`、`sdurws_robotmodelbuilder_workcellconvertertest.exe`，三个目标均以绝对路径直接执行并返回 0。
- 证据：构建目录 `RobWork/build/codex-vs-debug5/RobWorkStudio/bin/Debug`；RobotModelBuilder 5/5 用例通过。

### F-002：UR-6-85-5-A acceptance 资源缺失导致真实项目交叉验收阻塞

- 严重级别：P1（验收阻断）
- 状态：BLOCKED（环境/工作树资源缺失，不能归因于插件代码）
- 现象：`RobWork/example/ModelData/XMLDevices/UR-6-85-5-A/` 递归文件数为 0；工作树显示该目录下资源全部为删除改动。
- 影响：不能把真实项目交叉验收失败归因于插件代码，必须区分 fixture 缺失与功能错误。

### F-003：CTest 未指定配置时将所有多配置测试报告为 Not Run

- 严重级别：P2（测试基础设施易用性）
- 状态：OPEN
- 复现：在 `build/codex-vs-debug5` 执行 `ctest -R '^sdurws_engineeringrequirements_' -j1`，6 项全部显示 `Test not available without configuration (Missing "-C <config>?")`，退出码 8。
- 正确命令：`ctest -C Debug -R '^sdurws_engineeringrequirements_' -j1 --output-on-failure`。
- 影响：未指定 `-C Debug` 的 CI/人工命令会产生假失败；计划和 CI 包装脚本应固定配置参数。

### F-004：EngineeringRequirements managed-project gate 状态文本断言失败（已修复）

- 严重级别：P1（发布门）
- 状态：RESOLVED
- 复现：`ctest -C Debug -R '^sdurws_engineeringrequirements_' -j1 --output-on-failure`，测试 `sdurws_engineeringrequirements_managed_project_gate_test` 失败，源文件 `EngineeringRequirementsTest.cpp:2153` 的 `widget.statusText()` 精确字符串断言失败。
- 预期：`The robot project has not generated its managed WorkCell. Review the model in RobotModelBuilder and run Save and Load first.`
- 根因：需求集缺少模型指纹时，编辑器把唯一的“缺少机器人模型指纹”诊断当作普通阻断项，直接禁用了冻结按钮；项目门禁回调因此不会执行，状态栏停留在初始提示。
- 修复：保留该唯一诊断的可点击入口，使宿主 managed-project readiness 检查优先给出明确门禁文案；其他阻断性诊断仍继续禁用冻结按钮。
- 验证：`ctest -C Debug -R '^sdurws_engineeringrequirements_' -j1 --output-on-failure`，6/6 PASS，11.10 秒。

### F-005：测试插件运行库缺失，插件加载器每次测试输出错误（已修复）

- 严重级别：P2（测试环境/插件发现）
- 状态：RESOLVED
- 现象：RobotModelBuilder、EngineeringRequirements、StructureOptimizer 的 Widget/宿主相关测试反复输出 `Error loading plugin ... test_plugin.rwplugin.xml ... plugin file ... does not exist`；构建树只有 `test_plugin.rwplugin.xml`，没有对应 `test_plugin.rwplugin.dll`。
- 修复：在 Windows MODULE 目标上补充 `RUNTIME_OUTPUT_DIRECTORY`，使 `test_plugin.rwplugin.dll` 与 XML 清单部署到同一 Debug 目录；新增部署检查脚本，避免清单存在而运行库缺失。
- 验证：`test-test-plugin-deployment.ps1` 返回 0；`sdurws_robotmodelbuilder_widgettest.exe` 和 `sdurws_robotmodelbuilder_metatest.exe` 在 `QT_QPA_PLATFORM=windows` 下分别以绝对路径启动并返回 0，未再出现 `Error loading plugin`。

## 用例执行总表

| 范围 | 用例数量 | PASS | FAIL | BLOCKED | NOT_RUN |
|---|---:|---:|---:|---:|---:|
| RobotModelBuilder | 5 | 5 | 0 | 0 | 0 |
| EngineeringRequirements | 6 | 6 | 0 | 0 | 0 |
| KinematicAnalysis | 25 | 25 | 0 | 0 | 0 |
| StructureOptimizer | 43 | 43 | 0 | 0 | 0 |
| 四插件 E2E | 8 个计划用例 | 0 | 0 | 1 | 7 |
| 发布门 | 4 个 Phase 8 聚合门 | 4 | 0 | 0 | 0 |

## 执行证据

### RobotModelBuilder（5/5）

- `sdurws_robotmodelbuilder_xmltest.exe`：PASS，退出码 0；生成 GenericSixAxis WorkCell/Scene/DWC，并输出 dump 文件。
- `sdurws_robotmodelbuilder_jsontest.exe`：PASS，退出码 0；JSON round-trip 通过。
- `sdurws_robotmodelbuilder_workcellconvertertest.exe`：PASS，退出码 0；WorkCell converter smoke 通过。
- `sdurws_robotmodelbuilder_metatest.exe`：PASS，退出码 0；元对象/插件元数据测试通过。
- `sdurws_robotmodelbuilder_widgettest.exe`：PASS，退出码 0；同时观察到预期的 cyclic-dependency 负向场景日志，未导致进程失败。
- S-A2 动态加载复核：`test_plugin.rwplugin.xml` 与 `test_plugin.rwplugin.dll` 同目录部署检查 PASS；Widget/Meta 两个 executable 单独启动均退出码 0，未出现插件清单加载错误。

CTest 复核命令：`ctest -C Debug -R '^sdurws_robotmodelbuilder_' -j1 --output-on-failure`，5/5 PASS，16.08 秒。

### EngineeringRequirements（5 PASS / 1 FAIL）

命令：`ctest -C Debug -R '^sdurws_engineeringrequirements_' -j1 --output-on-failure`。

- PASS：基础、迁移、Widget、relative source base、managed project root。
- FAIL：`sdurws_engineeringrequirements_managed_project_gate_test`，对应 F-004。

### KinematicAnalysis（25/25）

命令：`ctest -C Debug -R '^sdurws_kinematicanalysis_' -j1 --output-on-failure`。

FK/IK、目标姿态、批处理、取消、verified region、指标、当前姿态、任务点、工作空间、报告、缓存、Workflow UI、阈值、绘图对话框和聚合测试全部 PASS。

### StructureOptimizer（43/43）

命令：`ctest -C Debug -R '^sdurws_structureoptimizer_' -j1 --output-on-failure`。

全部 43 项 PASS，总耗时 4085.14 秒（约 68.1 分钟）。包含基础优化器、评估器一致性、缓存、资源依赖、候选/局部搜索/最终验证、当前 JSON Envelope、迁移、快照、Run Store、Workflow Resolver、Preflight、Phase 6 集成、Phase 8 acceptance/performance/resource/manifest/release gate、Phase 7 变量表/操作/控制器/候选比较/预览/报告/GUI 集成及真实机器人文件 acceptance。

## GenericSixAxis 六自由度基线核验

- `GenericSixAxis.wc.xml` 与 `GenericSixAxisScene.wc.xml` 均存在。
- XML 结构：设备 `GenericSixAxis`；`Joint1`–`Joint6` 共 6 个 Revolute 关节；`TCP` Frame 1 个；位置限位 6 组。
- SHA-256：
  - WorkCell：`CFF943280572FE6E987E135764B52748AF6B64C6DB6F6B2F631FE15EBA28E81E`
  - Scene：`4B955643C6AE2648B604E81E7A97C23F3A1DB4C8A9F65991A0A2D0AD83FA2CE2`
- 6DOF 结构断言：PASS（设备、关节数量/名称、TCP、限位数量）。

## 尚未完成或不能宣称通过的验收项

- 独立 OPT-DATA-01 729 候选 CSV oracle 与 optimizer 逐行/逐分数比对：NOT_RUN；当前仓库未找到独立 oracle 工件或 `fourpluginacceptance` 测试目标。
- 真实 GenericSixAxis 离散设计域完整枚举、邻域扰动证明、重复 10 次排序一致性：NOT_RUN；现有 StructureOptimizer 测试覆盖算法契约和长时优化，但没有计划要求的独立全枚举证据。
- E2E-U01–E2E-U08 四插件跨插件资源/指纹/失效交互：BLOCKED/NOT_RUN；未生成 `fourpluginacceptance` 目标，且 RobWorkStudio GUI 主程序 executable 未出现在 Debug 输出目录。
- 真实 UR-6-85-5-A 交叉验收：BLOCKED，见 F-002。
- 宿主 GUI 手工截图矩阵：NOT_RUN；当前构建仅有各插件测试 executable，没有可启动的 RobWorkStudio 主程序。

## 后续追加区

测试执行过程中按计划用例 ID 追加结果、日志路径、截图路径和缺陷编号。
