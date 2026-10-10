# FIX-PANEL 三域面板启动崩溃修复批——构建/门禁/测试执行留痕

- 日期：2026-10-10
- 分支：`fix-panel`（宿主 `redesign-main` 任务分支；基点＝405b0772
  ASM-PANEL 三新域面板挂位收口合入后——六域注册全 Ok 形态在案）
- 范围：三域面板启动崩溃修复——UiText 补键（键族⑩三新域面板键 83 键
  ＋键族①b kinematics 八命令键补登＋键族⑩d 描述符承载键补登 3 键）＋
  宿主辅助键缺席忍耐（消费方两消费点＋挂位拍置灰镜像；登记面 fail-fast
  契约不弱化）＋取件空 continue 补 UI-PLUGIN-ASSEMBLY-FAILED 报告行＋
  Dev 通道＋ui_test 87 键钉扎用例＋ui_contract_test 六域描述符承载键
  现产对账段＋GUI 冒烟 layout 通道 aux-toggle-enabled 断言扩展＋四处
  登记注（units/ui.md v1.94＋units/dynamics.md＋units/selection.md＋
  units/optimization.md）。
- 三域 plugin 计算与装配面源码零改动（键值归 ui 文案面——PA-1）；
  白名单零新边；CMakeLists 零改动（新用例随既有测试 TU 编入面——
  ui_test 钉扎用例入 StageNavigationModelTest.cpp、contract 对账段入
  HostIntegrationContractTest.cpp 既有用例，零新文件零目标增量）。

## 1. 集成模式构建（真实执行）

构建缓存确认：`grep RWS_BUILD_INDUSTRIALROBOT <wt>/build/CMakeCache.txt`
→ `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（在位）。

逐目标 Release 增量构建（`cmake --build <wt>/build --config Release
--target <目标>`，各次退出码 0，错误段 grep 筛查零命中）：

- `sdurws_ird_ui`（UiText 键族⑩/①b/⑩d 补键）——零 error；
- `sdurws_ird_ui_plugin`（UiPlugin 忍耐＋留痕）——零 error（C4099 为
  UiPlugin.hpp 既有警告，本批未改该文件）；
- `sdurws_ird_studio`（产品主程序）——零 error；
- `sdurws_ird_ui_test` / `sdurws_ird_ui_contract_test`（新用例）——零 error；
- `sdurws_ird_workflow_test` / `_contract_test`、`sdurws_ird_dynamics_test`
  / `_contract_test`、`sdurws_ird_selection_test` / `_contract_test`、
  `sdurws_ird_optimization_test` / `_contract_test`（回归）——零 error。

## 2. 测试执行（真实运行——_report 目标口径，DTB §5.5）

gtest XML 留痕（十份，本目录同名拷贝自集成树各单元
`gtest_reports/<目标名>.xml`，failures 均 0）：

| 目标 | 用例数（XML tests 属性） | PASSED | SKIPPED | FAILED |
| --- | --- | --- | --- | --- |
| sdurws_ird_ui_test | 258 | 258 | 0 | 0 |
| sdurws_ird_ui_contract_test | 35 | 35 | 0 | 0 |
| sdurws_ird_workflow_test | 193 | 193 | 0 | 0 |
| sdurws_ird_workflow_contract_test | 77 | 77 | 0 | 0 |
| sdurws_ird_dynamics_test | 105 | 104 | 1 | 0 |
| sdurws_ird_dynamics_contract_test | 30 | 30 | 0 | 0 |
| sdurws_ird_selection_test | 277 | 276 | 1 | 0 |
| sdurws_ird_selection_contract_test | 58 | 58 | 0 | 0 |
| sdurws_ird_optimization_test | 164 | 163 | 1 | 0 |
| sdurws_ird_optimization_contract_test | 49 | 49 | 0 | 0 |

SKIPPED 三处均为既有 GUI envUnavailable 登记（DynPanelGui/
SelPanelGui/OptPanelGui 的 WidgetPresentationDeferredToHarness
EnvUnavailable——GTEST_SKIP，非本批新增；asm-panel 基线同形态）。

本批新增/扩展用例（真实运行通过，gtest XML 在案）：

- `UiText.ExtraDomainPanelKeysRegistered_UX02_FIXPANEL_ACC1`（ui_test
  新增——87 键清单测试内冻结 ⊆ registeredTextKeys＋逐键解析面非空
  不回显＋实测失败三键值锚＋判定词四值映射面）；
- `HostIntegrationContractTest.SixDomainHostAssemblySequence_Snapshot_
  ASMSTUDIO_ACC1` ⑪段扩展（ui_contract_test——六域描述符承载键**现产
  值**对账 UiText 登记面）。

★ 现产对账首跑真实失败记录（防漂移闸的失败能力实证）：首跑
`sdurws_ird_ui_contract_test` FAILED 1——⑪段抓出 `cmd.kinematics.*.title`
八键＋`stage.kinematics.panel.pose-metrics/solver-config.title`＋
`plugin.optimization.panel.title` 共 11 个**既有漏登键**（同族缺陷，
非本批引入）；随批补登（键族①b kinematics 八键＋键族⑩d 三键）后
复跑 35/35 全绿（gtest XML 为复跑产物）。ui_test 钉扎用例同步扩入
⑩d 三键后清单定格 87 键。

## 3. ird_gates 门禁（真实执行）

gate-all 第 2 步同款单跑（本目录 `ird-gates.log`）：

```
cmake --build <wt>/build --config Release --target ird_gates
```

退出码 0；`[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/
SUB/GRAPH/LIB/SA02 零命中`（"自测 fail_* 按预期检出"为引擎自测子进程
预期输出）。

## 4. 本地一键门禁 gate-all（真实执行——85/85 全过）

```
pwsh -File RobWork/scripts/industrialrobot/gate-all.ps1 \
  -BuildDir <wt>/build \
  -QtPrefix D:/software/Qt/6.11.1/msvc2022_64 \
  -Toolchain <repo>/vcpkg/scripts/buildsystems/vcpkg.cmake
```

退出码 0，`结果: 85/85 通过`——覆盖：集成树缓存确认、ird_gates、
集成模式全部 _test/_contract_test/_gui_test 注册目标、**独立冒烟树
全新配置＋构建＋全部冒烟测试**（gate-all 第 4 步——独立冒烟模式构建
零错误由本步真实执行承载；首次跑批 modeling_gui_test 单项失败经单独
ctest 复现通过判定为串行执行环境偶发，第二次全量运行该项 PASS 在案）。

## 5. 未执行项（如实登记）

- GUI 实机验证（⑥）：带重定向启动 `sdurws_ird_studio.exe` 捕获装配
  报告行（预期六域注册全 Ok＋`domain panel assembly` 零失败行＋主窗口
  呈现）由编排者在合入后于主构建树执行——本批未执行、未标注通过；
  通道已实证（ASM-PANEL 批所有者实机点验同通道）。
- GUI 冒烟通道新增断言（`aux-toggle-enabled:<key>` 七开关）随
  gate-all 冒烟树构建入编，其运行期点验归上述 GUI 实机验证（无人值守
  冒烟测试不触发 layout 通道——环境变量触发面）。

---

# FIX-PANEL 返工轮执行留痕（acc/fix-panel/1 B-1 判定后追加，2026-10-10）

- 判定依据：验收记录 acc/fix-panel/1 阻断 B-1——首轮穷举差集漏 2 键
  `plugin.optimization.candidates.not-assembled`／
  `plugin.optimization.candidates.none`（既不在 UiText 登记面也不在
  钉扎冻结 87 键清单），optimization 面板宿主装配仍失败（净室首启
  实测恰 1 行 `domain panel assembly: plugin=optimization
  UI-PLUGIN-ASSEMBLY-FAILED detail=ui/uitext/unknown-key: 未登记文案键
  plugin.optimization.candidates.not-assembled`，且 smoke 断言
  `aux-toggle-enabled:domain.optimization` FAIL）。
- 根因补正：两字面键直接写在 OptimizationPanelWidget 构造尾拍
  refreshFromSession→refreshCandidatePage 刷新路径（候选页 L-O6 空态
  分支——缝空→未装配态提示、缝在无结果→无结果提示），首轮穷举以
  词表常量/键族派生为主，构造/刷新路径直书字面键漏网。

## R-1. 穷举复核（返工要求④——构造/刷新路径可达键全查）

以 OptPanelWidget 漏网为鉴，对三域 widget 构造尾拍
refreshFromSession 全路径逐处核对（grep 键面＋派生组合点）：

- dynamics：五命令标题键族（DynPanelCommandCatalog 从 Commands.hpp
  kCommandTokens 派生——analyze/show-curves/locate-peak/replay-at/
  export-curve-data）＋两页键 kDynWorkflowPageKey/kDynCurvesPageKey＋
  plugin.dynamics.title——全部在册；
- selection：三页键 kSel*PageKey＋就绪行素材（readiness.*四键）＋
  判定词四值（state.*.label——SelCatalogPanelWidget refreshReadiness
  逐值）＋目录空态两键＋公共词三键＋回填行三键＋回填命令标题键＋
  模型层产出键七值（kSelReject*/kSelRecalcDomain* 常量＋axis-out-of-
  scope）——全部在册；
- optimization：四页键（kOptPanelPageKeys）＋就绪行三键＋run.phase
  九值（Types.cpp toToken(RunPhase) 词表逐值）＋随行三键＋action.start
  七值/action.cancel 五值（OptPanelModel.cpp:222-272 拒因产出点逐值
  对账）＋漏斗八段（OptPanelModel.cpp:201 titleKey 派生——
  kOptProgressPhaseTokens 词表）＋横幅四键（:138/:404 直书两键＋空态
  两键）＋status 七值（Types.cpp:41 toToken(CandidateStatus) 词表）＋
  **候选页空态两键（:393/:404 直书——唯二缺登，本轮补登）**。

结论：除返工点名两键外零其他缺登。

## R-2. 代码改动（返工要求①②③）

- `ui/src/UiText.cpp`：键族⑩c 补登两键（50→52 行；值＝
  "候选结果未装配（数据通道未接线）"／"暂无计算结果"——与键族⑩b
  selection 目录页空态两键同构的工程用语空态文案）；键族⑩合计
  83→85 键；registeredTextKeys 计数注 86→88、盘点合计 241→243；
  文件头/键族⑩横幅注/键族⑩c 返工注同步。
- `ui/test/StageNavigationModelTest.cpp`：钉扎用例
  ExtraDomainPanelKeysRegistered_UX02_FIXPANEL_ACC1 冻结清单
  87→89 键（两键扩入 optimization 段）＋两键值锚断言（装配报告
  逐字键）。
- 文档：units/ui.md v1.95（版本头＋§3.5 键族注＋§16.7 v1.95 行）
  ＋units/optimization.md 补键兑现注（50→52 键＋返工两键逐字登记
  ＋89 键钉扎计数）＋units/dynamics.md／selection.md 钉扎计数引用
  同步 89。
- 三域 plugin 计算与装配面源码零改动（键值归 ui 文案面——PA-1）；
  白名单零新边；CMakeLists 零改动。

## R-3. 集成模式构建与测试（真实执行）

逐目标 Release 增量构建（`cmake --build <wt>/build --config Release
--target <目标>`，错误段 grep 筛查零命中）：sdurws_ird_ui／
sdurws_ird_ui_test／sdurws_ird_ui_contract_test／sdurws_ird_ui_plugin／
sdurws_ird_studio——零 error。

十目标 _report 复跑（本目录同名 gtest XML 已刷新为返工轮产物，
failures 均 0）：

| 目标 | 用例数 | PASSED | SKIPPED | FAILED |
| --- | --- | --- | --- | --- |
| sdurws_ird_ui_test | 258 | 258 | 0 | 0 |
| sdurws_ird_ui_contract_test | 35 | 35 | 0 | 0 |
| sdurws_ird_workflow_test | 193 | 193 | 0 | 0 |
| sdurws_ird_workflow_contract_test | 77 | 77 | 0 | 0 |
| sdurws_ird_dynamics_test | 105 | 104 | 1 | 0 |
| sdurws_ird_dynamics_contract_test | 30 | 30 | 0 | 0 |
| sdurws_ird_selection_test | 277 | 276 | 1 | 0 |
| sdurws_ird_selection_contract_test | 58 | 58 | 0 | 0 |
| sdurws_ird_optimization_test | 164 | 163 | 1 | 0 |
| sdurws_ird_optimization_contract_test | 49 | 49 | 0 | 0 |

SKIPPED 三处＝既有 GUI envUnavailable 登记（首轮同形态，非本批新增）。
钉扎用例返工扩容后随 ui_test 真实运行通过（XML testcase 条目
status="run"／result="completed"）。

## R-4. ird_gates 门禁（真实执行）

`cmake --build <wt>/build --config Release --target ird_gates` 退出码 0；
`全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中`
（本目录 ird-gates.log 已刷新为返工轮产物）。

## R-5. 独立冒烟模式构建（真实执行）

`cmake --build <wt>/build-smoke --config Release` 全量增量构建零 error
（构建尾行确认各域 plugin/test 目标产出）。

## R-6. GUI layout 冒烟（真实执行——返工要求⑤取证通道）

带 stdout/stderr 重定向净室首启（acc/fix-panel/1 B-1 同通道）：
`IRD_UI_PLUGIN_SMOKE=layout`＋`IRD_UI_PLUGIN_SMOKE_OUT=<tmp>`，
PATH 前置 vcpkg/框架/Qt 运行时，净室 CWD，ini 夹具＝备份后清除
aux.domain.* 四行（跑后已还原——用户布局记忆零污染），启动集成树
`<wt>/build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe`。

证据（本目录 logs/layout-smoke-console.log＋layout-smoke-driver.log，
几何走查 geometry-report.txt）：

- 退出码 0；`[ird-ui-smoke-layout] DONE`；
- 六域注册全 Ok（与首轮取证逐字同形态）：`domain assembly:
  plugin=modeling ok=1 panels=1 commands=11`／requirements 9／
  kinematics ok=1 panels=2 commands=8／dynamics ok=1 panels=1
  commands=5／selection ok=1 panels=1 commands=1／optimization ok=1
  panels=1 commands=0；
- **`UI-PLUGIN-ASSEMBLY-FAILED` 行数＝0**（返工判据——首轮恰 1 行
  optimization unknown-key 失败行已消除）；
- **`pass aux-toggle-enabled:domain.optimization`**（返工判据——首轮
  FAIL 已翻转）且七开关全 pass（modeling/requirements/kinematics/
  kinematicsAdvanced/dynamics/selection/optimization）；
- smoke 断言 FAIL 行数＝0；default-hidden 七 Dock＋tasks 全 pass。

## R-7. 未执行项（如实登记）

- GUI 实机验证（⑥）：带重定向启动主构建树
  `build/RobWorkStudio/bin/Release/sdurws_ird_studio.exe` 捕获装配
  报告行＋主窗口呈现点验，仍由编排者在合入后于主构建树执行——本轮
  上述 R-6 为 pipeline-wt 工作树集成树上的无人值守 layout 通道取证，
  不替代编排者主构建树实机点验（任务依据⑥口径不变）。
