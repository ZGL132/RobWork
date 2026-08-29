# WP-10-T04 统一策略入口与显示隔离

- **Task ID / 需求 ID / ADR / 阶段：**WP-10-T04；UX-08、AT-19、需求 §6.7.2；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/session-ui.md` v0.3、`architecture/public-interfaces.md` §6
- **前置任务及必需工件：**WP-10-T01（`EditDraft`/`DraftController` 工件）；WP-07-T05（`IEngineeringPolicyProvider` 端口与策略唯一入口——端口契约前置，签名按 public-interfaces §6 已冻结，本卡以契约测试替身先行，集成期接 WP-07 实现）；WP-09-T01（`Diagnostic` 公共头）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/include/sdurws/ird/ui/EngineeringPolicyPanel.hpp`；`ui/src/PolicyPanel.cpp`；`ui/test/PolicyUiTest.cpp`；`ui/testdata/policy/`；`ui/evidence/WP-10/T04/`；`ui/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`EngineeringPolicySet`/`CollisionPolicy` 字段（WP-07 所有）、碰撞算法实现、`IEngineeringPolicyProvider` 签名；禁止插件私有策略开关；`architecture/`、`module-design/`、其他模块目录
- **修改前接口：**无（旧插件各持私有策略对话框）
- **修改后接口：**`EngineeringPolicyPanel`：计算模式/安全距离编辑写入 `EditDraft`，应用后经 `IEngineeringPolicyProvider.resolvedPolicy` 读取（不得叠加私有默认值）；"显示碰撞几何/高亮"开关只写 `SessionState.colorMode/visibility`，立即生效且不触发任何命令
- **实施步骤：**1) 页面分组："计算模式"（EditDraft＋应用）与"显示碰撞几何/高亮"（SessionState 直写）；2) 策略摘要只读渲染 `EngineeringPolicySet`；3) 静态断言插件无法注册同名私有开关；4) 隔离测试（toggle 显示开关时 revision/sliceHash/缓存/结果计数全零变化）
- **RED 测试：**Given 切换"显示碰撞几何/高亮"，When toggle，Then 仅 `SessionState` 变化，命令服务调用计数＝0、sliceHash 不变、缓存失效计数＝0（`PolicyUiTest` 先行）
- **最小实现：**两组开关的状态模型与替身 provider 读取路径；不含碰撞几何渲染本身
- **正常/边界/失败测试：**
  - 正常：Given 修改计算模式/安全距离，When 用户编辑并应用，Then 进入 `EditDraft`→命令服务一次调用→恰好一个新修订
  - 边界：Given 插件尝试提供同名私有策略开关，When 注册扫描，Then 拒绝并指出冲突来源（AT-19）
  - 失败：Given provider 替身返回 `ProjectError`，When 刷新摘要，Then 显示诊断、页面保持上次值，不本地补默认值
- **精确验证命令**（仓库根；GUI 测试须 Visual Studio x64 环境，`QT_QPA_PLATFORM=windows`，一次只启动一个 GUI 测试可执行文件）：
  ```powershell
  $env:QT_QPA_PLATFORM='windows'
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_ui_widget_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_ui_widget_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_ui_widget_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "CollisionPolicy\|EngineeringPolicySet" ui/src/PolicyPanel.cpp` 命中处仅限只读渲染（无字段写回）；`grep -rni "default" ui/src/PolicyPanel.cpp` 不得出现私补策略默认值分支
- **证据工件：**`ui/evidence/WP-10/T04/`——策略摘要截图、显示隔离计数日志（命令/sliceHash/缓存前后对照）、私有开关拒绝记录
- **提交格式：**`WP-10-T04: implement unified policy UI`
- **停止与升级条件：**计算设置与显示设置无法区分、UI 需要复制策略默认值、或 WP-07 端口签名变更时暂停并升级 WP-07/WP-10 联合评审；替身与 WP-07 实现集成失败时停止并回滚集成，不放宽断言
