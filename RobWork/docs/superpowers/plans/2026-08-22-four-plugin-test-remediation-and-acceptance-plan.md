# 四插件测试问题修复与最终验收实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. 每个 S 完成后必须执行验证、追加中文测试记录，并创建中文 commit；不得把 BLOCKED/NOT_RUN 写成 PASS。

**Goal:** 修复本轮测试发现的 P1/P2 问题，补齐独立 6DOF 最优性 oracle、四插件 E2E、宿主 GUI 与真实项目验收，使四插件发布门具备可复核证据。

**Architecture:** 先处理不依赖新测试基础设施的 P1 门禁错误，再修复多配置测试和测试插件运行库输出问题。随后新增独立 acceptance 测试层：用 GenericSixAxis 做 6DOF 结构断言和 729 候选 oracle，用资源指纹串联四插件，最后在干净 fixture 和真实 Qt Windows 环境下执行宿主 GUI 与发布门。

**Tech Stack:** C++、Qt 6 Widgets、RobWork WorkCell、CMake/CTest、MSVC x64、PowerShell、CSV/JSON、SHA-256 指纹。

---

## 执行前统一规则

1. 不恢复或覆盖工作树中已有的 UR-6-85-5-A 删除改动；真实 UR fixture 使用独立干净 checkout、只读 fixture 包或用户明确提供的资源目录。
2. Windows GUI 测试必须在 VS x64 Developer PowerShell 中执行，设置 `QT_QPA_PLATFORM=windows`，每次只启动一个绝对路径 executable；禁止 `offscreen`。
3. 所有新增/修改的 C++、PowerShell、CMake 测试脚本添加中文注释。
4. 每个 S 的顺序固定为：先写失败/回归测试 → 运行确认失败 → 最小实现 → 运行通过 → 更新报告 → 中文 commit。
5. 每个 S 完成后把命令、退出码、耗时、日志路径追加到：
   `RobWork/docs/superpowers/reports/2026-08-22-four-plugin-six-dof-test-report.md`。

## S-A1：定位并修复 managed-project gate 文案契约（P1）

**目标：** 使 EngineeringRequirements 在没有 managed WorkCell 时，冻结按钮保持未冻结、不发出发布请求，并显示唯一、稳定的门禁文案。

**文件：**

- Test/Modify: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsTest.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/engineeringrequirements/EngineeringRequirementsWidget.cpp`
- Inspect: `RobWork/RobWorkStudio/src/rws/RobWorkStudio.cpp`

- [ ] 在 `testWidgetBlocksFreezeUntilManagedWorkCellExists()` 中先增加 `INFO(widget.statusText().toStdString())`，直接运行 `sdurws_engineeringrequirements_test.exe managed_project_gate`，记录实际文本、按钮状态和 `freezePublicationRequested` 次数。
- [ ] 将回归断言固定为三项：状态文本等于 readiness contract；`requirementSet().frozen == false`；发布信号计数为 0。
- [ ] 若实际文本被其他状态覆盖，调整 `EngineeringRequirementsWidget::freezeRequirements()` 的失败路径，使 readiness 检查失败后立即 `setStatus(readinessError)` 并返回；不得继续执行模型绑定、冻结或发布逻辑。
- [ ] 不修改 readiness 文案的大小写、标点和换行，统一由 `robotProjectWorkCellReadinessError()` 提供。
- [ ] 验证：
  `ctest -C Debug -R '^sdurws_engineeringrequirements_managed_project_gate_test$' -j1 --output-on-failure`
  预期：1/1 PASS，退出码 0。
- [ ] 回归 EngineeringRequirements：
  `ctest -C Debug -R '^sdurws_engineeringrequirements_' -j1 --output-on-failure`
  预期：6/6 PASS。
- [ ] 更新报告中的 F-004 为 RESOLVED，并记录实际状态文本。
- [x] 已完成并验证：EngineeringRequirements 6/6 通过，项目门禁文案和冻结状态契约恢复。
- [x] Commit：`修复工程需求项目门禁状态契约`

## S-A2：修复测试插件 DLL 输出与动态加载（P2）

**目标：** 让 `test_plugin.rwplugin.dll` 在 Windows Debug 构建中生成到与 XML 相同目录，消除测试中反复出现的插件加载错误。

**文件：**

- Modify: `RobWork/RobWork/gtest/CMakeLists.txt`
- Inspect: `RobWork/RobWork/gtest/core/test_plugin.rwplugin.xml.in`
- Test: `RobWork/build/codex-vs-debug5/RobWork/bin/Debug/test_plugin.rwplugin.dll`

- [ ] 在 `test_plugin.rwplugin` 的 `set_target_properties` 中同时设置 `LIBRARY_OUTPUT_DIRECTORY` 与 Windows 所需的 `RUNTIME_OUTPUT_DIRECTORY`，两者均指向 `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}`。
- [ ] 保持 XML 中的 runtime 路径与 `$<TARGET_FILE:test_plugin.rwplugin>` 一致，不写死旧 build 目录。
- [ ] 重新配置 Debug 构建并只构建测试插件目标：
  `cmake --build RobWork/build/codex-vs-debug5 --config Debug --target test_plugin.rwplugin`
- [ ] 断言同目录同时存在 XML 和 DLL；用 `Test-Path` 输出绝对路径。
- [ ] 运行一个依赖插件加载的 Widget 测试，确认不再出现 `Error loading plugin`：
  `sdurws_robotmodelbuilder_widgettest.exe`
- [ ] 运行元对象测试：
  `sdurws_robotmodelbuilder_metatest.exe`
- [ ] 更新报告中的 F-005 为 RESOLVED；若仍有加载错误，保留完整 XML/runtime/DLL 路径作为新缺陷证据。
- [x] 已完成并验证：XML 与 DLL 同目录部署，RobotModelBuilder Widget/Meta 测试在 Windows Qt 平台下分别返回 0，未再出现插件加载错误。
- [x] Commit：`修复 Windows 测试插件运行库部署`

## S-A3：固定多配置 CTest 执行入口（P2）

**目标：** 防止未指定 `-C Debug` 造成整组测试假失败或 Not Run。

**文件：**

- Create: `RobWork/scripts/run-four-plugin-tests.ps1`
- Modify: `RobWork/docs/superpowers/plans/2026-08-22-four-plugin-six-dof-full-test-plan.md`
- Modify: `RobWork/docs/superpowers/reports/2026-08-22-four-plugin-six-dof-test-report.md`

- [ ] 脚本定义参数 `$Configuration = 'Debug'`、`$BuildDirectory`、`$Regex`，并拒绝空 build 目录。
- [ ] 脚本统一设置 `QT_QPA_PLATFORM=windows` 和 Debug DLL PATH，调用 `ctest -C $Configuration -j1 --output-on-failure -R $Regex`。
- [ ] 脚本在 CTest 退出码非 0 时原样返回非 0，并打印配置、正则和绝对 build 路径。
- [ ] 运行错误复现命令确认旧行为仍是 Not Run，仅作为报告记录；运行新脚本确认插件套件按 Debug 执行。
- [ ] 更新计划中的所有 CTest 命令，统一带 `-C Debug`。
- [ ] Commit：`补充多配置插件测试统一执行脚本`

## S-B1：建立四插件 acceptance fixture 与结果格式

**目标：** 生成可重复、机器可读的 GenericSixAxis acceptance 测试目标，避免把单插件测试误当作 E2E。

**文件：**

- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/CMakeLists.txt`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceFixture.hpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceFixture.cpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceTest.cpp`
- Modify: `RobWork/RobWorkStudio/CMakeLists.txt`

- [ ] Fixture 用绝对路径加载 `GenericSixAxis.wc.xml` 和 `GenericSixAxisScene.wc.xml`，验证设备名、6 个关节名、TCP、6 组限位和有限数值。
- [ ] 输出固定格式 `case-id,status,duration-ms,error-code,artifact-path`，并生成 `four-plugin-summary.json`。
- [ ] 所有 JSON/CSV 输出使用项目相对资源路径，不写入绝对路径、临时目录或 NaN/Inf。
- [ ] 增加 `sdurws_fourplugin_acceptance_test` CTest 目标，Debug 配置可直接运行。
- [ ] 先只运行 fixture 自检；失败时阻断后续 oracle/E2E，不伪造 PASS。
- [ ] Commit：`建立四插件六自由度验收测试夹具`

## S-B2：实现 OPT-DATA-01 独立 729 候选 oracle

**目标：** 在有限离散域内证明优化器返回域内全局最优，而不是只证明流程跑通。

**文件：**

- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/SixDofOracle.hpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/SixDofOracle.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceTest.cpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/data/opt-data-01.csv`

- [ ] 定义 6 个独立变量，每个变量离散值为 `{-1.0, 0.0, 1.0}`，按变量 ID 生成完整 `3^6=729` 行，不依赖优化器排序函数。
- [ ] 固定唯一目标向量 `q* = {1.0, -1.0, 0.0, 1.0, 0.0, -1.0}`；oracle 分数为 `sum((q_i-q*_i)^2)`，越小越优；加入一个显式硬约束并输出违反列表。
- [ ] CSV 每行必须包含：6 个变量值、feasible、每个指标、constraint violations、total score、candidate fingerprint。
- [ ] 断言 oracle 行数为 729、最优 fingerprint 唯一且为 q*、最优 score 为 0、CSV SHA-256 固定。
- [ ] 将同一 729 域映射为 StructureOptimizer `StructureOptimizationProblem`，运行 Grid/完整枚举策略。
- [ ] 断言 optimizer candidate count=729，best values、score、feasibility、fingerprint 与独立 oracle 完全一致；数值容差 `1e-12`。
- [ ] 添加变体：删除 q* 一个离散值、将 q* 一个维度设为硬约束不可行、打乱变量输入顺序；每个变体都必须得到域内真实最优，不能继续声称理论 q* 存在。
- [ ] Commit：`增加六自由度729候选独立最优性验证`

## S-B3：实现真实 GenericSixAxis 离散域与邻域证明

**目标：** 在真实 6DOF WorkCell 上验证 StructureOptimizer 的最优模型与独立枚举一致。

**文件：**

- Modify: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceTest.cpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/data/generic-six-axis-domain.json`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/data/generic-six-axis-oracle.csv`

- [ ] 从 `GenericSixAxis.wc.xml` 读取 6 个关节限位，声明固定离散域（每轴至少 3 个有限值），输出 domain fingerprint。
- [ ] 逐行生成真实候选，使用独立评分代码计算位置、姿态、约束和总分；保存全部候选而非只保存最优行。
- [ ] 使用同一输入运行 StructureOptimizer Grid，断言候选数、可行性、best values、score、fingerprint 与 oracle 一致。
- [ ] 对 oracle 最优解逐变量执行 `-step`、`+step` 邻域扰动；每个仍在域内的邻居必须不优于 q*，越界值必须被拒绝。
- [ ] 清空缓存、重启进程、重复运行 10 次，断言最优 fingerprint 和排序序列稳定，缓存只能改变耗时。
- [ ] Commit：`完成真实GenericSixAxis离散域最优性证明`

## S-B4：实现四插件资源指纹 E2E

**目标：** 验证 Builder → Requirements → KinematicAnalysis → StructureOptimizer 的资源契约、失效和保存失败行为。

**文件：**

- Modify: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginAcceptanceTest.cpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginWorkflowHarness.hpp`
- Create: `RobWork/RobWorkStudio/src/rwslibs/fourpluginacceptance/FourPluginWorkflowHarness.cpp`

- [ ] E2E-U01：Builder 保存模型后，Requirements 读取 `robot-model.main`，设备/TCP/fingerprint 自动匹配。
- [ ] E2E-U02：Requirements 冻结后，KinematicAnalysis 导入同一 execution/provenance fingerprint，Must 任务数量完全一致。
- [ ] E2E-U03：KinematicAnalysis 导出报告后，StructureOptimizer 只消费冻结契约，不从状态栏文案推断状态。
- [ ] E2E-U04：优化变量修改后返回分析，旧结果标记 stale，不能继续显示 Verified。
- [ ] E2E-U05：Builder 重生成模型后，下游需求、分析、优化项目全部报告 fingerprint mismatch，缓存失效。
- [ ] E2E-U06：同时打开两个项目并切换，项目 ID、资源 ID、标题和状态不串线。
- [ ] E2E-U07：模拟保存失败/权限拒绝，源文件不截断，下游不消费半成品，错误可恢复。
- [ ] E2E-U08：正序和反序关闭插件，后台线程、QObject、临时 WorkCell 和文件锁全部清理。
- [ ] 每个 E2E 用例输出资源 ID、输入/输出 fingerprint、状态转移和 artifact 路径。
- [ ] Commit：`补齐四插件资源指纹端到端验收`

## S-C1：恢复真实 UR-6-85-5-A 交叉验收环境

**目标：** 让真实项目测试和当前工作树删除改动隔离。

- [ ] 从干净 commit、只读 fixture 包或用户指定目录取得完整 `UR-6-85-5-A/a1` 资源；不在当前工作树执行恢复删除操作。
- [ ] 校验 WorkCell、scene、model、requirements、analysis、bindings、geometry 文件均存在，记录 fixture SHA-256。
- [ ] 运行已有 `sdurws_structureoptimizer_robot_file_acceptance_test`，再运行真实 UR acceptance 专用用例。
- [ ] 缺失资源时保持 F-002 BLOCKED；不得修改工作树删除状态来制造通过。
- [ ] Commit：`补充真实UR项目交叉验收夹具`（仅当资源以独立受控文件加入时）

## S-C2：构建宿主 RobWorkStudio 并执行 GUI 矩阵

**目标：** 覆盖自动 Widget 测试没有覆盖的宿主窗口、插件切换、文件对话框和截图证据。

- [ ] 构建 `RobWorkStudio` 主程序 Debug executable，确认绝对路径存在。
- [ ] 在 VS x64 Developer PowerShell 设置 `QT_QPA_PLATFORM=windows`，逐个启动主程序和插件，不并行启动。
- [ ] 手工记录：四插件加载/卸载、切换 WorkCell、取消文件对话框、保存失败、重复打开关闭、项目切换、后台运行取消、预览恢复。
- [ ] 每个状态矩阵项保存截图、窗口标题、状态文本和日志；截图至少包含默认页、编辑页、错误页、成功页、预览页、报告页。
- [ ] 关闭顺序正向/反向各执行一次，检查无崩溃、无悬挂进程、无文件锁。
- [ ] Commit：`完成四插件宿主界面矩阵验收`

## S-C3：全量回归与性能/资源复核

- [ ] 运行统一脚本：
  `powershell -File RobWork/scripts/run-four-plugin-tests.ps1 -Configuration Debug -Regex '^(sdurws_robotmodelbuilder_|sdurws_engineeringrequirements_|sdurws_kinematicanalysis_|sdurws_structureoptimizer_|sdurws_fourplugin_)'`
- [ ] 运行独立 oracle、E2E、GUI、异常矩阵；确认没有 P1、没有未解释的 P2。
- [ ] 重复 StructureOptimizer 性能门，记录候选数、耗时、峰值内存、缓存命中率、临时文件清理。
- [ ] 对所有 JSON/CSV/报告执行有限数值、绝对路径、悬挂资源和 fingerprint 检查。
- [ ] Commit：`完成四插件全量回归与性能复核`

## S-C4：发布门与交付

- [ ] 发布门必须同时满足：P1=0；E2E-U01～U08 全部 PASS；OPT-DATA-01 和真实 GenericSixAxis oracle 一致；宿主 GUI 矩阵完成；UR fixture 已通过或有书面豁免。
- [ ] 生成最终 `four-plugin-summary.json`、候选 CSV、oracle SHA-256、资源指纹、截图索引、性能摘要和失败重现命令。
- [ ] 再执行 `git diff --check`、干净 Debug 构建、CTest 全量和报告链接检查。
- [ ] 在最终报告中将每个问题标记为 RESOLVED、ACCEPTED-WAIVER 或 BLOCKED，并写明责任人和下一复测命令。
- [ ] Commit：`完成四插件六自由度最终发布验收`

## 明确停止条件

- F-004 仍为 P1 时，不进入发布门，只能继续修复和回归。
- UR fixture 缺失时，不得宣称真实 UR 交叉验收通过；可继续执行 GenericSixAxis 和合成 oracle。
- 独立 oracle 未产生完整候选表和 SHA-256 时，不得宣称“找到全局最优模型”。
- 任一 E2E 资源 fingerprint 不一致、stale 状态未失效或保存失败导致半成品时，发布门失败。
- 测试插件 DLL、宿主 GUI 或 Qt 平台无法初始化时，记录 BLOCKED 并保留完整环境诊断，不使用 `offscreen` 绕过。
