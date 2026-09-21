# UI-T13（WP-10-T13）ird_gates 门禁命中集比对记录

执行方式：`cmake -DIRD_ROOT=<industrialrobot 根绝对路径>
          -DIRD_SELFTEST_DIR=<临时目录> -P cmake/ird_gates.cmake`
          脚本模式直跑（与 WP-10-T08/T09/T10/T11/T12 留痕同引擎同口径）；
          本任务另以同引擎 detached worktree 方式对 base 提交
          7b62d221b338db7fd01a07724da0b7e580b12628（＝UI-T12 合入后状态）
          复跑基线，双态同引擎同归一化口径，diff 零口径漂移。

原始输出：builds/wp10-t13/ird_gates-integration.log（本分支集成树运行）、
          builds/wp10-t13/ird_gates-base.log（base worktree 基线运行）、
          builds/wp10-t13/ird_gates-smoke.log（冒烟树 ird_gates 目标复跑——
          引擎为纯源码扫描，冒烟与集成共用同一源码树，命中集逐码一致）；
          归一化命中集见 builds/wp10-t13/gates-hit-set.txt（95 条原始行；
          逐码计数见下表），逐码 diff 见 builds/wp10-t13/
          gates-hit-set-diff.txt，R3 命中文件级 diff 见 builds/wp10-t13/
          gates-hit-set-files-diff.txt。

## 结论

与 base（7b62d221，wp10-t12 合入后）基线（91 命中行；逐码 LIB×8/R-1×1/
R-3×33/R-4×3/R-5×1/SUB×39/T-1×5/T-2×1）同引擎逐码比对：**仅 R3 恰增
4 处（33→37），其余七码逐码零变化、目标集合 diff 空**。R3 的 +4＝
2 个新增实现 TU（每 TU 在日志的两遍扫描中各命中 1 次，文件级 diff 见
gates-hit-set-files-diff.txt——恰为以下两文件，既有 15 个命中文件零变化），
F-259 同型机器面滞后（R-3 例外头包含半区回填责任方 WP-01-T03，先例提交
件 traceability/wp10-t02~t10-gate-registrations.md）：

| # | 命中码 | 文件（ui/ 相对） | 说明 |
| --- | --- | --- | --- |
| 1 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/DiagnosticPresentation.cpp | 诊断呈现模型实现 TU（QMetaObject/QObject——§10.8 subscribe 的 M-1 Marshal 半区：目录通知线程→UI 线程排队投递，经 QMetaObject::invokeMethod 函子重载〔不需要 Q_OBJECT，零 AUTOMOC 维持〕；头文件零 Qt——§12.1 第一层模型可测分工不变）。ui 是 R-3 例外本体目标 sdurws_ird_ui 的源集文件，与基线中 WorkbenchShell.cpp/ParamTablePanel.cpp 等命中同型同根源——同一例外登记范围，非新例外形态 |
| 2 | IRD-GATE-R3（疑似包含 Qt 类头，Q+大写约定） | src/CommandInteractionBridge.cpp | 确认交互桥实现 TU（QMetaObject/QObject——§9.2 Marshal＋阻塞等待的落位半区：命令执行线程→UI 线程排队打开对话，同一函子重载形态；头文件零 Qt，P-PR-7 线程模型的机制半区）。同上同型同根源 |

本任务其余新增/改动文件零命中：

- include/sdurws/ird/ui/IDiagnosticPresentationModel.hpp　新公共契约头：
  纯标准库值类型＋纯函数＋接口，QObject 仅以实现 TU 内部嵌套类承载
  （头内无 Qt 类型出现——与 AboutDialog.hpp 零命中同案）；
- include/sdurws/ird/ui/ITaskPresentationModel.hpp　新公共契约头：零 Qt
  纯模型层（v1.3 UiSessionController.hpp 同案）；
- include/sdurws/ird/ui/CommandInteractionBridge.hpp　新公共契约头：
  Marshal 上下文以私有嵌套类前置声明承载，头内零 Qt 类型（析构函数
  移实现文件定义——不完整类型 unique_ptr 成员的删除器实例化点）；
- include/sdurws/ird/ui/UiPorts.hpp／UiProjections.hpp（增量）：新增端口
  IUiCommandInteraction/IUiTaskPresentationPort/IUiFindingQueryPort/
  IUiUserLogSource/IUiRedactionPolicyPort 与投影
  TaskProgressProjection/TaskViewProjection/UiTaskAck/UserLogEntry——
  纯 ui/core/diagnostics 表内登记边类型，零对端（project/execution/
  evidence/policy/runtime）类型出现，`NoCrossUnitInclude_O31_UI_BUILD`
  守卫（BuildRedLineTest，含新增文件自动扫描）回归通过；
- src/TaskPresentation.cpp　任务呈现模型实现 TU：零 Qt（v1.3
  UiSessionController.cpp 同案）；
- src/UiText.cpp（键族⑧增量）：纯标准库表，零命中零变化。

测试目标链接面零变化（sdurws_ird_ui_test/_contract_test 链被测目标＋
testkit＋gtest——既有 SUB/T-1 登记；gui_test 链接面未动），无新增表外
依赖边、无第三方新依赖。

## 与先例登记件的关系

本记录延续 wp10-t02/t03/t05/t06/t07/t08/t10-gate-registrations.md 的
同型登记（R-3 例外范围登记册的机器面滞后回填责任方＝WP-01-T03，
findings.json F-259）；命中集绝对路径已按先例剥离为单元相对形态供
基线比对。
