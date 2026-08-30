# WP-14-T07 需求阶段 B GUI

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T07；UX-01～UX-08、REQ-06/REQ-07（批量模板）、AT-02/AT-03；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.4 §8、`module-design/session-ui.md` v0.4 §8
- **前置任务及必需工件：**WP-14-T06（命令集成工件）；WP-10-T03（公共组件：工程表格/诊断面板/阶段导航——代码前置）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/gui/RequirementsPlugin.hpp`、`gui/RequirementsPlugin.cpp`、`gui/panels/`；`requirements/test/RequirementsGuiTest.cpp`；`requirements/out/test-evidence/wp-14/<run-id>/`；`requirements/CMakeLists.txt`（登记 `sdurws_ird_requirements_plugin`、`sdurws_ird_requirements_gui_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-10 公共组件与 `EditDraft`/`SessionState` 接口；WP-11 reader/writer（CSV 经 T02 适配器，不直读）；T01～T06 计算核心（GUI 只消费）；`architecture/`、`module-design/`；禁止 Widget 直接计算/执行公式/写文件、插件内第二套 CSV 解析
- **修改前接口：**无（薄插件不存在；旧 `sdurws_engineeringrequirements` 表格作权威数据的链路待 Rewrite：表格退为编辑视图）
- **修改后接口：**`RequirementsPlugin` 按 requirements-definition v0.4 §8 提供四分区：任务与工位、空间与区域、负载与工艺、验收与摘要；支持模板、阵列、镜像、批量编辑、筛选和 SI 单位存储；CSV 导入/导出经 T02，部分导入逐行报告且合法行留在草稿；Must/Should、验收摘要和错误定位始终可追溯；应用经 T06 命令。
- **实施步骤：**1) 四分区绑定模型与 `EditDraft`；2) 模板、阵列、镜像与批量粘贴经 WP-10 表格组件＋T02 校验；3) 单位显示换算 helper；4) Must/Should、部分导入错误、验收摘要和对象跳转；5) 登记 GUI 目标并录制三档缩放回归。
- **RED 测试：**Given 含错误字段的表单，When 提交，Then 定位对象/实际值/要求值并阻止应用、零修订（`RequirementsGuiTest` 先行）
- **最小实现：**四分区编辑流、模板/阵列/镜像、CSV 入口、Must/Should 与验收摘要；不做三维拖拽（REQ-08，P1 范围）。
- **正常/边界/失败测试：**
  - 正常：Given 合法数据，When 应用，Then 经项目命令产生一个新修订、就绪摘要与阶段状态更新
  - 边界：Given 批量粘贴或 CSV 含合法与非法行，When 导入，Then 逐行定位、合法行进入草稿、错误行保持可修复；150% 缩放下主按钮和验收状态可见
  - 失败：Given CSV 导入含公式注入文本，When 经 T02 适配器，Then 不执行、按行定位报告；导入失败时旧修订与已打开草稿保持
- **精确验证命令**（仓库根；GUI 测试须 Visual Studio x64 环境，`QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 测试可执行文件）：
  ```powershell
  $env:QT_QPA_PLATFORM='windows'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_gui_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_gui_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_gui_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "QFile|ifstream|getline|eval\(" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/gui; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（无直读/公式）；`rg -n "sha256|schemaVersion" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/gui/RequirementsPlugin.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（内部身份不作主操作展示）
- **证据工件：**`requirements/out/test-evidence/wp-14/<run-id>/`——GUI 回归录屏/截图（四分区、模板/阵列/镜像、批量编辑、部分导入、Must/Should、验收摘要）、AT-02/AT-03 断言输出、评审者签署
- **提交格式：** `WP-14-T07: 新增需求定义界面`

  - 新增需求表格编辑与预览界面
  - 新增批量粘贴与错误定位测试
  - 新增运行证据记录
- **停止与升级条件：**GUI 平台环境不满足 Windows 规则（testing-contract §5）、或面板需要绕过 T02/T06 直接读写数据时暂停；WP-10 组件能力不足时升级 WP-10，不得在插件内重建组件
