# UI-T07（WP-10-T07）ird_gates 门禁命中集比对记录

执行方式：`cmake --build build --config Release --target ird_gates`
          （集成模式构建树 build/，RWS_BUILD_INDUSTRIALROBOT=ON；
            与 WP-10-T03~T06 留痕同一执行面——gate-all.ps1 第 2 步同口径；
            另以 `cmake -DIRD_ROOT=<industrialrobot 根> -P cmake/ird_gates.cmake`
            脚本模式复跑取干净行序，两模式命中集一致）
原始输出：本目录 ird_gates.log（MSBuild 输出为 GBK 行合并形态）与
          ird_gates-script.log（脚本模式逐行形态）；归一化命中集见
          gates-hit-set.txt（脚本模式去重行序，供后继任务作基线比对）

## 结论

与 WP-10-T06 合入后基线（39 命中，见 ../wp10-t06/gates-branch-run.log 的
`[ird_gates]` 段去重行序）逐码比对**恰增 1 处**；本任务新增命中为下述
1 处 R3 头包含面（F-259 同型机器面滞后——R-3 例外头包含半区回填责任方
WP-01-T03，先例提交件 traceability/wp10-t02~t06-gate-registrations.md）：

| # | 命中码 | 文件（ui/ 相对） | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/PolicySummaryCard.cpp | 摘要卡实现 TU（QLabel/QPushButton/QVBoxLayout——§6.7 右栏摘要卡呈现面；ui 是 R-3 例外本体目标 sdurws_ird_ui 的源集文件，与基线中 WorkbenchShell.cpp/CommandPalette.cpp 等命中同型同根源——同一例外登记范围，非新例外形态） |

本任务其余新增/改动文件零命中：

- include/sdurws/ird/ui/PolicySummaryCard.hpp　新公共契约头：纯标准库
  ＋core/Units（表内登记边）值类型与纯函数，零 Qt 头零命中；
- include/sdurws/ird/ui/UiProjections.hpp（改动）：PolicyThresholdProjection/
  PolicySummaryProjection 逐字段细化——纯标准库值类型，零 Qt 零命中；
- src/WorkbenchShell.cpp／src/WorkbenchShell_p.hpp（改动）：既有命中文件，
  本次改动（右栏占位标签换接 createPolicySummaryCard、刷新钩子成员）不
  新增命中行——命中面按文件级判定，逐码比对无新增行；
- test/PolicySummaryCardTest.cpp、gui_test/PolicySummaryCardGuiTest.cpp：
  测试面不在产品面扫描域；
- ui/CMakeLists.txt（增列源文件/测试文件）不在源码扫描域；产品目标
  sdurws_ird_ui 链接面零变化（R3 L2 三件套 Qt6::Core/Gui/Widgets 三条
  既有命中逐码不变——本次零新链接边，O-31 产品面零对端边界不变）。

基线 39 处（../wp10-t06/gates-branch-run.log `[ird_gates]` 段逐码一致）
＋上述 1 处＝40 处，与门禁退出报告的"命中 40 项"一致；ird_gates 自测
（fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2＋pass_clean/
pass_r4comment）全部按预期通过。

机器面回填登记（R-3 例外头包含半区，含本批 1 文件）维持既有口径：
责任方 WP-01-T03，登记提交件即本文件（findings.json 对应条目由治理批次
合并登记）。

## 附：gate-all.ps1 全量一键运行的环境面失败项如实登记（非本任务改动引入）

本次额外执行了 gate-all.ps1 全量一键门禁（留痕 gate-all.log），除 ird_gates
命中项（上节，预期内登记态）外另有 3 项 FAIL，逐项核实均与本任务改动无关：

1. `集成测试 sdurws_ird_testkit_test`：ctest 失败——
   ProductBoundary.ZeroTestkitHeadersAndTestIfdef_UT_FAULT 两断言命中
   project/src/win32/IFileOps.hpp:12 与 ILockOps.hpp:14 的既有
   `#ifdef TEST` 字样（project 单元既有产物面，先于本分支存在；
   本任务未触碰 project 单元任何文件）；
2. `集成测试 sdurws_ird_ui_gui_test`：ctest 直跑无 GUI 环境
   （QT_QPA_PLATFORM/平台插件路径为进程环境变量，ctest 不继承本
   shell 的设置）——按 ui.md §12.2 纪律以绝对路径直接执行通过
   （本目录 sdurws_ird_ui_gui_test.console.log，15/15）；
3. `冒烟树配置`：gate-all 第 4 步冒烟配置未携带 Qt6_DIR 前缀参数——
   ui 单元（自 WP-10-T02 起）的 CMake 需要 Qt6 包位置，工具面参数
   滞后（gate-all 辖区 WP-01，不在本任务 allowedFiles，按"裁决前不
   私改"纪律保留并登记）；本任务冒烟模式按 AGENTS §4.1 手工口径
   （vcpkg toolchain＋Qt6_DIR）配置并构建通过、三测试目标全量通过
   （本目录 smoke-configure.log/smoke-build.log/smoke-*-console.log）。
