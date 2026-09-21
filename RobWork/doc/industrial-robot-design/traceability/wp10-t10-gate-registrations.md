# UI-T10（WP-10-T10）ird_gates 门禁命中集比对记录

执行方式：`cmake -DIRD_ROOT=<industrialrobot 根绝对路径> -P
          cmake/ird_gates.cmake` 脚本模式（与 WP-10-T08/T09 留痕同口径；
          本任务另以同引擎 detached worktree 方式对 base 提交
          f6968386c6748fe6e7b17e88c18f6f22c00ffccd〔＝wp10-t09 合入后状态〕
          复跑基线，双态同引擎同归一化口径，diff 零口径漂移）
原始输出：builds/wp10-t10/ird_gates-integration.log（本分支集成树运行）、
          builds/wp10-t10/ird_gates-base.log（base worktree 基线运行）、
          builds/wp10-t10/ird_gates-smoke.log（冒烟口径复跑——引擎为纯源码
          扫描，冒烟与集成共用同一源码树，命中集逐码一致）；
          归一化命中集见 builds/wp10-t10/gates-hit-set.txt（42 条唯一——
          绝对路径剥离为 ui/ 相对、同型命中归一后去重排序），供后继任务
          作基线比对；逐码 diff 见 builds/wp10-t10/gates-hit-set-diff.txt。

## 结论

与 base（f6968386，wp10-t09 合入后）基线（41 命中，base worktree 同引擎
运行归一化）逐码比对**恰增 1 处**。本任务新增命中为下述 1 处 R3 头包含面
（F-259 同型机器面滞后——R-3 例外头包含半区回填责任方 WP-01-T03，先例
提交件 traceability/wp10-t02~t08-gate-registrations.md）：

| # | 命中码 | 文件（ui/ 相对） | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/AboutDialog.cpp | 关于对话框实现 TU（QDialog/QLabel/QTableWidget/QDesktopServices/QCoreApplication 等——§11.4 关于页呈现面：版本区只读文本行＋插件清单五列只读表＋对话框工厂；ui 是 R-3 例外本体目标 sdurws_ird_ui 的源集文件，与基线中 WorkbenchShell.cpp/CommandPalette.cpp/PolicySummaryCard.cpp/ParamTablePanel.cpp 等命中同型同根源——同一例外登记范围，非新例外形态） |

本任务其余新增/改动文件零命中：

- include/sdurws/ird/ui/AboutDialog.hpp　新公共契约头：纯标准库值类型、
  行装配纯函数与文案常量，QDialog/QWidget 仅前置声明（IWorkbenchShell.hpp
  同款——头文件不拖入 Widgets），零 Qt include 零命中（基线 diff 无本
  文件行）；
- src/UiText.cpp（增补键族⑥⑦共 11 键）、include/sdurws/ird/ui/UiPorts.hpp
  （增 IUiAboutDataSource 端口）、include/sdurws/ird/ui/IWorkbenchShell.hpp
  （ShellWiring 增 aboutSource 成员）、src/WorkbenchShell.cpp／
  WorkbenchShell_p.hpp（帮助菜单实条目＋壳自持处理器）：改动面均在既有
  41 处命中文件或非扫描域，归一化 diff 零新增零消失；
- test/AboutDialogModelTest.cpp、gui_test/AboutDialogGuiTest.cpp：
  测试面不在产品面扫描域；
- ui/CMakeLists.txt（增列源文件/测试文件）不在源码扫描域；产品目标
  sdurws_ird_ui 链接面零变化（R3 L2 三件套 Qt6::Core/Gui/Widgets 三条
  既有命中逐码不变——本次零新链接边，O-31 产品面零对端边界不变）。

基线 41 处（base worktree 同引擎运行逐码一致，亦即 wp10-t09 合入后状态）
＋上述 1 处＝42 处；ird_gates 自测
（pass_clean/pass_r4comment/fail_r1/fail_r3/fail_r4/fail_r5/fail_sub/
fail_t1/fail_t2）在脚本日志尾部照常全部按预期检出。ird_gates 对本次变更
的净增命中（1 处）属 R-3 例外本体目标的既有登记类目（DTB §4.5"预登记
（WP-10-T02 生效）"行），不是新例外形态；本任务未改门禁数据面
（cmake/ird_gates_whitelist.cmake 不在契约 allowedFiles 内，回填归
WP-01-T03）。
