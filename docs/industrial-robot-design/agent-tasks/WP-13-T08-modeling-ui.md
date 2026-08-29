# WP-13-T08 建模插件与 GUI

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T08；UX-01～UX-08、MDL-07、MDL-13；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`module-design/session-ui.md` v0.3
- **前置任务及必需工件：**WP-13-T02（编辑器/命令工件）；WP-13-T06（编译衔接工件）；WP-10-T03（公共组件：阶段导航/诊断面板/工程表格——代码前置）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/gui/ModelingPlugin.hpp`、`gui/ModelingPlugin.cpp`、`gui/panels/`；`modeling/test/ModelingGuiTest.cpp`；`modeling/out/test-evidence/wp-13/<run-id>/`；`modeling/CMakeLists.txt`（登记 `sdurws_ird_modeling_plugin`、`sdurws_ird_modeling_gui_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-10 公共组件与 `EditDraft`/`SessionState` 接口；WP-04/06 公共头；WP-13 计算核心（`include/`/`src/`——GUI 只消费）；`architecture/`、`module-design/`；禁止 Widget 直接写文件或领域对象、业务逻辑进入 Widget
- **修改前接口：**无（薄插件不存在；旧建模 UI 与业务耦合待 Rewrite/EvidenceOnly）
- **修改后接口：**`ModelingPlugin`：入口面板＝导入（T03 报告呈现）、编辑（T02 草稿）、转换预览（T04 判定/残差）、应用确认；一切用户意图写 `EditDraft`，应用经 T02 命令＋T06 预编译时序；错误定位跳转诊断面板
- **实施步骤：**1) 面板绑定 T02/T03/T04/T06 的模块接口（只发意图、不计算）；2) 应用确认对话框（显示将产生的修订摘要）；3) 未应用草稿状态隔离（切换视图不失效）；4) 诊断经 WP-09 目录渲染；5) 登记 GUI 目标并录制回归
- **RED 测试：**Given 含未应用草稿的会话，When 切换视图/关闭面板，Then 修订数不变、下游结果零失效、草稿可恢复（`ModelingGuiTest` 先行）
- **最小实现：**四入口面板与确认流；不做渲染增强与批量操作
- **正常/边界/失败测试：**
  - 正常：Given 点击应用且命令成功，When 确认，Then 恰好一个新修订、阶段状态刷新、面板状态重建
  - 边界：Given 导入报告含 Warning（如 `Defaulted` 轴），When 呈现，Then 待确认状态可见、确认记录持久化前禁止 Verified
  - 失败：Given 应用时预编译失败，When 确认，Then 错误定位到对象/字段、修订不变、草稿保留（`IRD-MDL-*` 透传显示）
- **精确验证命令**（仓库根；GUI 测试须 Visual Studio x64 环境，`QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 测试可执行文件）：
  ```powershell
  $env:QT_QPA_PLATFORM='windows'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_gui_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_gui_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_gui_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "ofstream\|QFile\|saveProject" modeling/gui/` 零命中（Widget 不写文件）；`grep -rn "apply(\|buildMutations" modeling/gui/ModelingPlugin.cpp` 命中处仅经命令服务调用（无直写领域对象）
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——GUI 回归录屏/截图（四入口＋确认流＋失败定位）、诊断截图、评审者签署
- **提交格式：** `WP-13-T08: 新增建模插件界面`

  - 新增建模编辑与校验界面
  - 新增界面交互测试
  - 新增运行证据记录
- **停止与升级条件：**GUI 测试无法按 Windows 规则（testing-contract §5）运行、或面板需要复制计算核心逻辑时暂停；WP-10 组件接口不足时升级 WP-10 需求，不得在插件内重建私有组件
