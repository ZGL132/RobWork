# 项目、公共交互、策略、任务与证据人工验收清单

基线：`redesign-main@4adc29f1bec2`；编制日期：2026-10-21；30个检查项。

**本文件是执行手册，不是已通过报告。全部数据、交互、真实三维结果初始均为NOT RUN。**

先阅读[准备数据与真实视觉方法](06-DATA-AND-VISUAL.md)，操作实际发布程序；不要以测试专用Harness代替正式宿主。

“界面代码存在”表示已查到控件/流程，不保证本机发布程序的服务供给完整。没有入口或出现未装配时，在记录表写BLOCKED并建问题，不能用内核通过代替人工PASS。

## 快速目录

|编号|功能|实现形态/边界|
|---|---|---|
|[APP-01](#app-01)|新建向导/初始来源|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-02](#app-02)|打开/格式识别/失败保当前|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-03](#app-03)|关闭/切换/退出确认|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-04](#app-04)|草稿保存与应用/自动保存周期|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-05](#app-05)|草稿汇总/标题脏标记|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-06](#app-06)|另存为新项目|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-07](#app-07)|项目包导入/导出/取消|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-08](#app-08)|旧版/未来版本拒绝|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-09](#app-09)|锁冲突/只读介质|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-10](#app-10)|恢复横幅/中断任务/孤儿草稿|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-11](#app-11)|外部资源检测/重新关联|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-12](#app-12)|最近项目|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-13](#app-13)|方案分支/基线保护|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-14](#app-14)|项目属性/改名/体积/历史扩展|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-15](#app-15)|项目级撤销/重做/空历史|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-16](#app-16)|设置/上次目录/快捷键持久|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-17](#app-17)|布局/折叠/滚动/焦点|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-18](#app-18)|三维标准视图/缩放/相机Home|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-19](#app-19)|三维线框/透明/显隐/拾取|已实现公共服务/界面；子入口和边界待人工核实|
|[APP-20](#app-20)|命令搜索/帮助/版本|已实现公共服务/界面；子入口和边界待人工核实|
|[SYS-01](#sys-01)|完整工程策略编辑/按修订解析|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-02](#sys-02)|碰撞域/相邻忽略/强制对/组规则|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-03](#sys-03)|行程/近限位/奇异工程阈值|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-04](#sys-04)|任务排队/运行/完成/失败状态|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-05](#sys-05)|任务协作取消/关闭等待|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-06](#sys-06)|快照/切片/当前性/必验矩阵|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-07](#sys-07)|缓存/固定种子/线程重放|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-08](#sys-08)|工作进程故障/检查点恢复|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-09](#sys-09)|诊断汇总/日志/脱敏|内核实现存在；界面/正式宿主供给需逐项核实|
|[SYS-10](#sys-10)|资源安全/导入预算/原子写出|内核实现存在；界面/正式宿主供给需逐项核实|

## APP-01

**新建向导/初始来源**；依据：PM-01。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：首页/文件→新建（Ctrl+N）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别选空白、六轴模板、URDF/Xacro来源。
2. 每步检查摘要。
3. 测试取消/确认。

通过标准：步骤摘要与实际来源一致，取消不留半成品；七轴模板冻结边界明确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-02

**打开/格式识别/失败保当前**；依据：PM-02。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：文件→打开（Ctrl+O）/拖放/命令行。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 打开合法目录/包。
2. 再用缺文件、非法格式。
3. 分别核对拖放与命令行。

通过标准：打开按协议校验；失败给具体文件且当前项目不变，未接入口单列。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-03

**关闭/切换/退出确认**；依据：PM-03。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：关闭项目/打开另一个/退出程序。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 保留未应用草稿及在途任务。
2. 逐一测试保存、放弃、取消、等待/协作取消。

通过标准：取消可中止，任务列表状态真实；候选打开失败不丢当前项目。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-04

**草稿保存与应用/自动保存周期**；依据：PM-04/PM-04-S1。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：Ctrl+S/应用修改（Ctrl+Return）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别保存与应用。
2. 等待默认自动保存。
3. 检查周期设置是否可达。

通过标准：保存只落草稿，应用成功一次新修订；未提供可调周期不能记该扩展已交付。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-05

**草稿汇总/标题脏标记**；依据：PM-04/PM-11。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：标题栏/草稿汇总/状态栏。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 同时修改需求和建模。
2. 逐域保存/应用。
3. 观察标题与汇总。

通过标准：各域草稿及未应用标记真实，当前方案/结果过期/只读状态不混淆。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-06

**另存为新项目**；依据：PM-05。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：文件→另存为（若未接记录入口缺口）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择是否带results/reports/drafts。
2. 复制后打开两项目。

通过标准：projectId更换且引用自洽，原项目完整；不能只改路径复用同一项目身份。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-07

**项目包导入/导出/取消**；依据：PM-05。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：文件→项目包操作/导出评估包。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导出后隔离导入。
2. 比较目录哈希。
3. 测试取消及非法路径包。

通过标准：传输可逐字节恢复，路径穿越/超预算拒绝；取消清临时区不留半目录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-08

**旧版/未来版本拒绝**；依据：PM-06。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：文件→打开。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用只读副本的旧格式和未来schema文件打开。
2. 核对原文件哈希。

通过标准：稳定诊断、升级指引清楚；不自动改写原文件或半激活。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-09

**锁冲突/只读介质**；依据：PM-07。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：第二实例打开同一验收项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 第一实例持写锁。
2. 第二实例打开。
3. 查看PID/只读说明并尝试写入。

通过标准：第二写者不阻塞，明确只读；所有域写入口受限，合法浏览导出可用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-10

**恢复横幅/中断任务/孤儿草稿**；依据：PM-08/PM-15。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：打开受控故障副本→恢复提示。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在专用副本模拟未完成保存/中断任务。
2. 打开查看详情、恢复或放弃。

通过标准：已提交HEAD/对象不变，未完成项诚实报告；恢复草稿不自动提交。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-11

**外部资源检测/重新关联**；依据：PM-09/NFR-REL-04。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：资源缺失诊断→重新关联。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 复制验收项目后移动或改变外部网格。
2. 打开。
3. 选新来源并显式提交。

通过标准：缺失/变化可见，哈希校验；重关联新修订且可撤销，原文件不静默替换。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-12

**最近项目**；依据：PM-10/PM-14。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：无项目首页→最近项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 打开多个路径及同路径不同拼写。
2. 移动一个测试项目。
3. 重新选择/移除。

通过标准：按规范路径去重，上限10；失效项有位置不可用及恢复/移除动作。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-13

**方案分支/基线保护**；依据：PM-12。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：切换方案/创建方案入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在URDF基线派生方案。
2. 编辑并应用。
3. 切回基线。
4. 带草稿切换。

通过标准：只在方案写入，基线不可变；切换前正确处置草稿，不复制整套对象冒充分支。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-14

**项目属性/改名/体积/历史扩展**；依据：PM-11-S1～S3/PM-12-S1/PM-08-S1。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：项目菜单/只读属性与历史入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查各已提供入口。
2. 有入口则与project.json、revisions及目录大小对照。

通过标准：名字变更不改projectId，历史/可达性报告只读；没有入口的子项分别记边界。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-14`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-15

**项目级撤销/重做/空历史**；依据：PM-18。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：编辑→撤销/重做。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录历史后执行两次业务应用。
2. 撤销/重做。
3. 尝试空历史和过期请求。

通过标准：新修订承载逆命令，不改旧历史；空/过期有明确提示，不串草稿本地撤销。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-15`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-16

**设置/上次目录/快捷键持久**；依据：PM-14/UX-13。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：用户设置/快捷键配置/文件对话框。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改快捷键和显示设置。
2. 关重开。
3. 打开文件对话框。

通过标准：设置属于用户而非业务修订，重复快捷键拒绝，上次目录按说明恢复。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-16`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-17

**布局/折叠/滚动/焦点**；依据：UX-04/05/09。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：所有可见面板及视图菜单。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 各页全展开，窄窗/高DPI滚到末尾。
2. 隐藏恢复。
3. 键盘Tab/Esc。

通过标准：所有字段和确认/取消可达，不丢暂存，错误后焦点可继续编辑。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-17`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-18

**三维标准视图/缩放/相机Home**；依据：UX-11。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：三维工具栏。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用非对称真网格操作顶前右轴测、旋转平移缩放/适配/Home。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：方向和比例正确，Home仅复位相机；对象真值和修订不变。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-18`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-19

**三维线框/透明/显隐/拾取**；依据：UX-06/11。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：三维工具栏/鼠标拾取。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐组切显示。
2. 真实遮挡单选多选。
3. 坐标架与碰撞高亮。
4. 产品PNG。

通过标准：显示不改变碰撞策略；命中和图例准确，截图可解码；真实视觉逐项留图。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-19`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## APP-20

**命令搜索/帮助/版本**；依据：UX-13/14/PM-17。

- 实现形态：已实现公共服务/界面；子入口和边界待人工核实。
- 入口：Ctrl+Shift+P/帮助/关于。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 中文搜每域命令。
2. 键盘执行/取消。
3. 打开手册关于。

通过标准：可达性与可用性门控一致，版本可核对到实际exe；不以命令ID代主文案。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[UiPlugin.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp)；[WorkbenchContent.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/src/WorkbenchContent.cpp)；[WorkflowCommandCatalog.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/plugin/WorkflowCommandCatalog.cpp)；[Lifecycle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Lifecycle.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`APP-20`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-01

**完整工程策略编辑/按修订解析**；依据：ARC-05/UX-08。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：工程策略摘要→修改工程策略。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改阈值/碰撞规则后取消再确认。
2. 撤销。
3. 对照旧/新修订计算。

通过标准：确认原子命令且可撤销，旧修订解析旧策略；不把用户设置当工程策略。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-02

**碰撞域/相邻忽略/强制对/组规则**；依据：KIN-05/ARC-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：工程策略→碰撞。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用真实相交/分离对象逐一设规则。
2. 在FK/IK/批量对照。

通过标准：优先级及域规则一致，正安全距离未支持必须诚实拒绝，不能闭合MA-02。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-03

**行程/近限位/奇异工程阈值**；依据：ARC-05/MDL-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：工程策略编辑→阈值。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 修改有量纲阈值并确认。
2. 重新评估相同样例。

通过标准：阈值唯一且有单位/来源，影响判定但不混入求解配置或显示颜色开关。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-04

**任务排队/运行/完成/失败状态**；依据：TASK/UX-10。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：任务区/后台运行入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 提交两个有界任务。
2. 看队列、运行、完成与失败详情。

通过标准：状态转换真实，失败可定位；任务完成≠工程可行。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-05

**任务协作取消/关闭等待**；依据：TASK/PM-03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：任务区取消/关闭确认。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启动大预算任务。
2. 点取消。
3. 切项目或关闭等待。

通过标准：取消及时且无新派发；部分产物不算完整，旧回执不污染新项目。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-06

**快照/切片/当前性/必验矩阵**；依据：EVI-01/02/CON-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：结果列表→身份/当前性。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录输入修订。
2. 只改一个依赖/无关显示。
3. 核对已完成结果。

通过标准：按真实切片依赖判当前/过期，必验工况不漏，Preview不产生Verified证据。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-07

**缓存/固定种子/线程重放**；依据：NFR-COR-02/CON-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：重复运行/日志或证据包。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 固定快照配置重复运行。
2. 核对命中和集合。
3. 改一个真实输入后重跑。

通过标准：合法命中不换真值，身份变化不误用旧缓存；线程/seed记录可重放。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-08

**工作进程故障/检查点恢复**；依据：TASK/NFR-REL。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：受控测试副本/工程师运行通道。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 由工程师在专用副本终止工作进程。
2. 查看任务归因及恢复入口。

通过标准：失败不伪造成功，不改已提交修订；未装配用户恢复入口记边界。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-09

**诊断汇总/日志/脱敏**；依据：ERR/PM-15/17。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：诊断表/日志/异常诊断文件。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 触发缺资源、非法输入和受控异常。
2. 查看详情与导出。

通过标准：稳定码/对象/原因/建议清楚；无敏感信息，用户错误不关闭主窗口。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SYS-10

**资源安全/导入预算/原子写出**；依据：NFR-SEC/IO。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：CSV/JSON/包/网格导入及导出。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用专用畸形文件/路径穿越包/不可写目录。
2. 核对旧输出哈希。

通过标准：无外部执行/越界文件、无半提交；错误不覆盖有效旧文件。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[PolicyParsing.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/policy/src/PolicyParsing.cpp)；[Controller.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/execution/src/Controller.cpp)；[Currentness.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/evidence/src/Currentness.cpp)；[Redaction.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/src/Redaction.cpp)；[AtomicFile.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/io/src/AtomicFile.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SYS-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。
