# UI-T06（WP-10-T06）ird_gates 门禁命中集比对记录

执行方式：`cmake --build build --config Release --target ird_gates`
          （集成模式构建树 build/，RWS_BUILD_INDUSTRIALROBOT=ON；
            与 WP-10-T03/T04/T05 留痕同一执行面——gate-all.ps1 第 2 步同口径；
            另以 `cmake -DIRD_ROOT=<industrialrobot 根> -P cmake/ird_gates.cmake`
            脚本模式复跑取干净行序，两模式命中集一致）
原始输出：本目录 gates-branch-run.log（MSBuild 输出为 GBK 行合并形态，
          脚本模式输出为逐行形态；本文件为归一化结论——逐码比对基线）

## 结论

与 WP-10-T05 合入后基线（33 命中，见 ../wp10-t05/gates-hit-set.txt）逐码
比对**恰增 6 处**；本任务新增命中全部为下述 6 处 R3 头包含面（F-259 同型
机器面滞后——R-3 例外头包含半区回填责任方 WP-01-T03，先例提交件
traceability/wp10-t02-gate-registrations.md、wp10-t03-gate-registrations.md、
wp10-t05-gate-registrations.md）：

| # | 命中码 | 文件（ui/ 相对） | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | include/sdurws/ird/ui/ICommandRegistry.hpp | 新公共头：QKeySequence（§7.1 defaultShortcut/§10.4 HotkeyBinding 契约类型——ui 是 R-3 例外本体，公共头按 §3.3 布局落位） |
| 2 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | include/sdurws/ird/ui/IGlobalShortcutRegistry.hpp | 新公共头：QKeySequence＋QWidget 前置声明（§10.4 attach 承载面契约） |
| 3 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/CommandPalette.cpp | 面板实现 TU（QLineEdit/QListWidget/QLabel——§7.4 呈现面） |
| 4 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/CommandPalette_p.hpp | 面板私有头（R-2：src/ 内，不进 include/ 扫描域外——本行为 src/ 私有头同型命中） |
| 5 | IRD-GATE-R3（包含 Qt 头） | src/CommandRegistry.cpp | 注册表实现 TU（QKeySequence/QtGlobal——§10.3 模型面） |
| 6 | IRD-GATE-R3（包含 Qt 头） | src/GlobalShortcutRegistry.cpp | 快捷键表实现 TU（QShortcut/QWidget——§7.3 唯一创建点本体） |

本任务其余新增/改动文件零命中：

- include/sdurws/ird/ui/UiTypes.hpp　纯标准库别名头，零 Qt 零命中；
- src/WorkbenchShell.cpp／src/WorkbenchShell_p.hpp／src/ShellSupport.cpp
  既有命中文件（本次改动不新增命中行——命令设施装配并入既有 TU）；
- test/CommandRegistryModelTest.cpp、test/ShortcutRegistryModelTest.cpp、
  test/BuildRedLineTest.cpp（改动）、gui_test/CommandPaletteGuiTest.cpp
  测试面不在产品面扫描域；
- ui/CMakeLists.txt（Qt6 Test 组件 find_package／gui_test 链接 Qt6::Test）
  不在源码扫描域；Qt6::Test 为**测试目标**链接面（§3.1 表 v0.8 增量），
  产品目标 sdurws_ird_ui 链接面零变化（R3 L2 三件套 Qt6::Core/Gui/Widgets
  三条既有命中逐码不变）。

基线 33 处（本目录 gates-branch-run.log 脚本模式段与
../wp10-t05/gates-hit-set.txt 逐码一致）＋上述 6 处＝39 处，与门禁退出
报告的"命中 39 项"一致；ird_gates 自测（fail_r1/fail_t1/fail_r5/fail_sub/
fail_r3/fail_r4/fail_t2＋pass_clean/pass_r4comment）全部按预期通过。

机器面回填登记（R-3 例外头包含半区，含本批 6 文件）维持既有口径：
责任方 WP-01-T03，登记提交件即本文件（findings.json 对应条目由治理批次
合并登记）。
