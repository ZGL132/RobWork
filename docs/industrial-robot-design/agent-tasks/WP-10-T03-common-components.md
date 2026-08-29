# WP-10-T03 公共工程组件

- **Task ID / 需求 ID / ADR / 阶段：**WP-10-T03；UX-01～UX-05、NFR-PERF-01、NFR-PERF-03；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/session-ui.md` v0.3
- **前置任务及必需工件：**WP-10-T01（`StageStatusModel` 工件）；WP-09-T03（错误映射 `Diagnostic` 目录公共头）；WP-07-T01（`EngineeringPolicySet` 类型头——端口契约前置，签名已由 D2 冻结，本任务以契约测试替身先行，集成期接 WP-07 实现）；WP-01-T03（测试入口）。`SelectionModel` 按 WP-10-T02 冻结签名消费，未合入时以替身先行
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/StageNavigationView.hpp`、`DiagnosticPanel.hpp`、`EngineeringTableView.hpp`；`ui/src/DiagnosticPanel.cpp`、`EngineeringTableView.cpp`；`ui/test/CommonComponentsTest.cpp`；`ui/testdata/components/`；`ui/out/test-evidence/wp-10/<run-id>/`；`ui/CMakeLists.txt`（仅追加本任务文件到 `sdurws_ird_ui`/`sdurws_ird_ui_widget_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**业务算法不得进入 Widget（评估/碰撞/求值一律经端口）；禁止硬编码诊断文案（统一经 WP-09 目录）；禁止绕过 `IDiagnosticCatalog`/策略 provider 公共头、修改 `requirements.md`/`architecture/`/`module-design/`、其他模块目录
- **修改前接口：**无（组件不存在；旧插件各持私有表格与文案）
- **修改后接口：**`StageNavigationView`（每阶段状态＋阻塞诊断＋下一步建议，消费 `StageStatusModel` 八值）；`DiagnosticPanel`（经 `IDiagnosticCatalog.lookup` 渲染，禁止本地文案表）；`EngineeringTableView`（批量粘贴、单位显示 m/rad、逐行诊断、分页接口 `setPage/window`）
- **实施步骤：**1) `StageNavigationView` 绑定八值状态与建议动作；2) `DiagnosticPanel` 接 WP-09 目录替身；3) `EngineeringTableView` 实现批量粘贴解析＋逐行 `IRD-UI-PASTE-INVALID` 定位；4) 高级设置折叠（seed/求解器/开发诊断默认收起）；5) 注册 `sdurws_ird_ui_widget_test`
- **RED 测试：**Given 批量粘贴含非法单位/越界/悬空引用行，When commit draft，Then 该行报 `IRD-UI-PASTE-INVALID`（Input/Error）并逐行定位，合法行不越过应用边界、命令服务零调用（`CommonComponentsTest` 先行）
- **最小实现：**三个组件的可测状态机与粘贴校验分支；渲染细节以可编程断言为准，不实现主题样式
- **正常/边界/失败测试：**
  - 正常：Given 输入缺失/过期/证据不足，When 渲染阶段导航，Then 显示对应状态、阻塞原因和下一步 action
  - 边界：Given 高级设置折叠，When 默认打开页面，Then 主流程断言不依赖 seed/求解器/开发诊断字段；分页翻页只实例化当前窗口行
  - 失败：Given 诊断目录查询未知码，When 渲染，Then 显示码原文与"目录缺失"占位，不崩溃、不编造文案
- **精确验证命令**（仓库根；GUI 测试须 Visual Studio x64 环境，`QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 测试可执行文件）：
  ```powershell
  $env:QT_QPA_PLATFORM='windows'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_widget_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_widget_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_widget_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "evaluate|collision|ik\(" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/DiagnosticPanel.cpp RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/EngineeringTableView.cpp` 零命中（无业务算法）；`rg -n "QStringLiteral.*失败|错误：" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/src/DiagnosticPanel.cpp` 零命中（无硬编码文案）
- **证据工件：**`ui/out/test-evidence/wp-10/<run-id>/`——组件状态截图、批量粘贴矩阵（合法/非法行组合）、诊断映射对照表、评审者签署
- **提交格式：**`WP-10-T03: 新增公共工程组件`

  - 新增 阶段导航、诊断面板与工程表格组件实现
  - 新增 批量粘贴校验测试与 `sdurws_ird_ui_widget_test` 登记
  - 新增 组件状态与诊断映射对照证据记录
- **停止与升级条件：**组件需要自行解释业务状态、WP-09 目录或 WP-07 类型头未冻结、或出现重复文案目录时暂停并上报；GUI 环境不满足 Windows 规则（testing-contract §5）时停止执行而非放宽
