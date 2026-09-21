# UI-T08（WP-10-T08）ird_gates 门禁命中集比对记录

执行方式：`cmake --build build --config Release --target ird_gates`
          （集成模式构建树 build/，RWS_BUILD_INDUSTRIALROBOT=ON；与
          WP-10-T03~T07 留痕同一执行面——gate-all.ps1 第 2 步同口径；
          另以 `cmake -DIRD_ROOT=<industrialrobot 根绝对路径> -P
          cmake/ird_gates.cmake` 脚本模式复跑取干净行序，退出码同为 1）
原始输出：builds/wp10-t08/ird_gates.log（MSBuild 输出，GBK 行合并形态）
          与 builds/wp10-t08/ird_gates-script.log（脚本模式逐行形态，
          含门禁自测段）；归一化命中集见 builds/wp10-t08/gates-hit-set.txt
          （供后继任务作基线比对——本任务沿用 wp10-t07 留痕文件形态，
          行序＝脚本模式原始行序排序，同型命中每扫描相位一行）。

## 结论

与 WP-10-T07 合入后基线（40 命中，见 ../wp10-t07/gates-hit-set.txt）逐码
比对**恰增 1 处**（去重后唯一文件数；脚本日志中同一命中打印两遍——扫描
相位与汇总回显各一行，基线文件同形态，行数 87→89 与门禁退出报告
"命中 41 项"〔基线 40〕一致）。本任务新增命中为下述 1 处 R3 头包含面
（F-259 同型机器面滞后——R-3 例外头包含半区回填责任方 WP-01-T03，先例
提交件 traceability/wp10-t02~t07-gate-registrations.md）：

| # | 命中码 | 文件（ui/ 相对） | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/ParamTablePanel.cpp | 参数表面板实现 TU（QTableWidget/QLineEdit/QComboBox/QLabel/QPushButton 等——§13 UI-T08 行"表单公共件第一渲染面"，就地编辑＋非模态确认区是 UX-05 禁止模态的红线承载形态；ui 是 R-3 例外本体目标 sdurws_ird_ui 的源集文件，与基线中 WorkbenchShell.cpp/CommandPalette.cpp/PolicySummaryCard.cpp 等命中同型同根源——同一例外登记范围，非新例外形态） |

本任务其余新增/改动文件零命中：

- include/sdurws/ird/ui/FormEditCommon.hpp　新公共契约头：纯标准库＋
  core/Units（表内登记边）值类型、纯函数与 ParamEditModel 模型类，
  QWidget 仅前置声明（IWorkbenchShell.hpp 同款——头文件不拖入
  Widgets），零 Qt include 零命中（基线 diff 无本文件行）；
- src/FormEditCommon.cpp　模型层实现 TU：零 Qt include（解析/格式化走
  std::from_chars/to_chars，换算经 core Units 唯一入口），零命中；
- test/FormEditModelTest.cpp、gui_test/ParamTablePanelGuiTest.cpp：
  测试面不在产品面扫描域；
- ui/CMakeLists.txt（增列源文件/测试文件）不在源码扫描域；产品目标
  sdurws_ird_ui 链接面零变化（R3 L2 三件套 Qt6::Core/Gui/Widgets 三条
  既有命中逐码不变——本次零新链接边，O-31 产品面零对端边界不变）。

基线 40 处（../wp10-t07/gates-hit-set.txt 逐码一致）＋上述 1 处＝41 处，
与门禁退出报告"命中 41 项"一致；ird_gates 自测
（fail_r1/fail_r3/fail_r4/fail_r5/fail_sub/fail_t1/fail_t2）在脚本日志
尾部照常全部按预期检出。ird_gates 对本次变更的净增命中（1 处）属 R-3
例外本体目标的既有登记类目（DTB §4.5"预登记（WP-10-T02 生效）"行），
不是新例外形态；本任务未改门禁数据面（cmake/ird_gates_whitelist.cmake
不在契约 allowedFiles 内，回填归 WP-01-T03）。
