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
