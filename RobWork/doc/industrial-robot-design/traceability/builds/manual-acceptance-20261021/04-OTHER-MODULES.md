# 轨迹、动力学与传动、选型、优化、报告人工验收清单

基线：`redesign-main@4adc29f1bec2`；编制日期：2026-10-21；53个检查项。

**本文件是执行手册，不是已通过报告。全部数据、交互、真实三维结果初始均为NOT RUN。**

先阅读[准备数据与真实视觉方法](06-DATA-AND-VISUAL.md)，操作实际发布程序；不要以测试专用Harness代替正式宿主。

“界面代码存在”表示已查到控件/流程，不保证本机发布程序的服务供给完整。没有入口或出现未装配时，在记录表写BLOCKED并建问题，不能用内核通过代替人工PASS。

## 快速目录

|编号|功能|实现形态/边界|
|---|---|---|
|[TRJ-01](#trj-01)|关节PTP/有序任务序列|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-02](#trj-02)|笛卡尔接近/撤离/IK连续性|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-03](#trj-03)|避障规划器/配置|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-04](#trj-04)|简化平滑/双步长复检|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-05](#trj-05)|时间参数化/分段节拍|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-06](#trj-06)|失败段/奇异/限值定位|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-07](#trj-07)|播放/时刻定位|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-08](#trj-08)|q/qd/qdd曲线和游标|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-09](#trj-09)|局部重规划|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-10](#trj-10)|标准轨迹导出|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-11](#trj-11)|jerk上限启用能力|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-12](#trj-12)|作业段工艺速度|内核实现存在；界面/正式宿主供给需逐项核实|
|[TRJ-13](#trj-13)|连续工艺段/驻留边界|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-01](#dyn-01)|逆动力学|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-02](#dyn-02)|重力/惯性/负载/外力/摩擦|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-03](#dyn-03)|功率/峰值/RMS/持续窗口|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-04](#dyn-04)|传动映射/效率/惯量/能量|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-05](#dyn-05)|正动力学响应一致性|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-06](#dyn-06)|可信度/估算/数据不足|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-07](#dyn-07)|多负载/急停保持/包络合并|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-08](#dyn-08)|曲线联动/峰值定位/时刻回放|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-09](#dyn-09)|曲线数据导出|内核实现存在；界面/正式宿主供给需逐项核实|
|[DYN-10](#dyn-10)|旋转/直线传动类型与codec|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-01](#sel-01)|版本化器件目录/字段字典|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-02](#sel-02)|目录校验/曲线插值|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-03](#sel-03)|电机硬能力筛选|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-04](#sel-04)|减速器硬能力筛选|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-05](#sel-05)|组合兼容/惯量比/逐轴工作点|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-06](#sel-06)|候选/裕量/质量/成本/原因表|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-07](#sel-07)|企业偏好排序|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-08](#sel-08)|目录差异/项目锁版|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-09](#sel-09)|旋转/直线器件范围|内核实现存在；界面/正式宿主供给需逐项核实|
|[SEL-10](#sel-10)|选型回填/物性合成/撤销|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-01](#opt-01)|新机型/改型初始化|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-02](#opt-02)|连续/量化/离散变量绑定|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-03](#opt-03)|硬约束/分期预检门控|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-04](#opt-04)|Pareto非支配集合|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-05](#opt-05)|结构/传动外层＋联合内层|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-06](#opt-06)|Quick/Verified/局部改进/并行/检查点|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-07](#opt-07)|八指标A/B比较|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-08](#opt-08)|候选预览/采用新分支|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-09](#opt-09)|鲁棒性/灵敏度冻结边界|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-10](#opt-10)|搜索策略扩展/确定性生成|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-11](#opt-11)|基线评估/预检定位|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-12](#opt-12)|一站证据导出|内核实现存在；界面/正式宿主供给需逐项核实|
|[OPT-13](#opt-13)|运行进度/漏斗/取消|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-01](#rpt-01)|结构化报告/章节完整性|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-02](#rpt-02)|HTML/JSON/CSV一致导出|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-03](#rpt-03)|EvidenceBundle/复现要素|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-04](#rpt-04)|方案对比报告|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-05](#rpt-05)|限定语/正式通过门槛|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-06](#rpt-06)|报告往返复算/一致性|内核实现存在；界面/正式宿主供给需逐项核实|
|[RPT-07](#rpt-07)|归档/覆盖取消/失败原子性|内核实现存在；界面/正式宿主供给需逐项核实|

## TRJ-01

**关节PTP/有序任务序列**；依据：TRJ-01。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹面板→plan-sequence。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择已应用任务序列/工况。
2. 请求规划。
3. 核对各路点和顺序。

通过标准：路径连接正确首末q，顺序和工况完整；只有“已受理”不能证明真实规划已运行。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-02

**笛卡尔接近/撤离/IK连续性**；依据：TRJ-02。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹→preview-segment/规划。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用非零接近撤离距离。
2. 预览并核对TCP线段/沿途q。

通过标准：方向/距离及连续性正确；分支跳变有具体段落原因。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-03

**避障规划器/配置**；依据：TRJ-03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹规划/规划配置入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 加入真实障碍。
2. 换有界规划参数。
3. 运行有路与无路样例。

通过标准：真实避障且参数参与；超预算不误称确定无路径，未接配置入口记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-04

**简化平滑/双步长复检**；依据：TRJ-04。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹规划结果/复检报告。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 规划后平滑。
2. 用贴近障碍的标准路径。
3. 核对端点和段内采样证据。

通过标准：平滑后重新验碰撞和限位，步长取严；未复检不能作为正式安全路径。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-05

**时间参数化/分段节拍**；依据：TRJ-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹结果→曲线视图。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设不同速度/加速度。
2. 规划并核对q/qd/qdd和分段时间。

通过标准：限制满足且加速度连续，节拍含对应段，数值按附录D容差评定。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-06

**失败段/奇异/限值定位**；依据：TRJ-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹诊断/曲线定位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用无路径、跳变、奇异、超限标准样例。
2. 点失败条目。

通过标准：定位具体段/工位及原因建议，不只报全局失败。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-07

**播放/时刻定位**；依据：TRJ-07/KIN-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹→play/playback-seek/播放定位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用已归档真实轨迹。
2. 改变时刻。
3. 观察真实机器人及数值。

通过标准：插值与轨迹时刻一致，纯会话零修订；没有暂停/停止入口不宣称已提供。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-08

**q/qd/qdd曲线和游标**；依据：TRJ-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹→曲线视图/show-curve。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 换关节和通道。
2. 拖/选游标并核对时间读数。

通过标准：轴/单位/曲线对应真实归档数据，缺数据明确“未装配”。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-09

**局部重规划**；依据：TRJ-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹→local-replan。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选中一段并请求重规划。
2. 比较新旧RunId及其他段。

通过标准：冻结输入和段身份正确，新结果归档不改旧结果/设计修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-10

**标准轨迹导出**；依据：TRJ-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：轨迹→export。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选路径导出。
2. 核对时间/q/速度/加速度/单位。
3. 取消及不可写重试。

通过标准：内容完整且失败原子，导出不重新规划或写项目设计。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-11

**jerk上限启用能力**；依据：TRJ-08-S1。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：现有时间化配置/内核接口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查正式入口。
2. 用有jerk限制样例对照曲线/超限段。

通过标准：实际jerk约束满足并可定位；内核有实现但无UI要分开记。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-12

**作业段工艺速度**；依据：TRJ-08-S2。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：作业段速度配置/内核接口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置受限或恒定TCP速度。
2. 运行并核对段内速度。

通过标准：只对指定段约束，满足误差规则；不可用入口不冒称完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## TRJ-13

**连续工艺段/驻留边界**；依据：TRJ-08-S3。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：连续段配置/内核接口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 连接两个不停驻段再连接驻留段。
2. 查看边界q/qd/qdd。

通过标准：连续边界与驻留切换明确，不把所有段强行停零或无视驻留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[TrajectoryPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp)；[Planner.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Planner.cpp)；[TimeParam.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/TimeParam.cpp)；[ContinuousProcess.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/ContinuousProcess.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`TRJ-13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-01

**逆动力学**；依据：DYN-01。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：动力学→analyze。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选真实已归档轨迹和已应用物性。
2. 运行。
3. 与独立解析标准样例比较。

通过标准：关节侧广义力正确；只有面板/命令受理无数值不算评估完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-02

**重力/惯性/负载/外力/摩擦**；依据：DYN-02。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：建模参数/工况→动力学评估。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐次只改变一个物理输入并运行。
2. 对比曲线。

通过标准：各项真实参与，坐标/单位正确；缺物性摩擦有相应降级，不能默认补零。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-03

**功率/峰值/RMS/持续窗口**；依据：DYN-03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：动力学→曲线/包络。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择每轴q/qd/qdd/广义力/功率。
2. 看峰值时窗和RMS。
3. 含驻留再算。

通过标准：旋转N·m/移动N正确，峰值带段和持续窗，RMS覆盖完整循环含驻留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-04

**传动映射/效率/惯量/能量**；依据：DYN-04。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：共享传动评估结果/选型消费者。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用正反转不同效率与非单位速比标准样例。
2. 比较关节侧和电机侧。

通过标准：力矩/转速/功率、反射惯量、惯量比与能量分项一致，不重复或倒置速比。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-05

**正动力学响应一致性**；依据：DYN-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：正动力学内核/工程师验证通道。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 明确控制/初态/步长/容差。
2. 运行受控响应标准样例。

通过标准：配置可追溯，响应与参考在容差内；未开放界面不当可直接操作功能。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-06

**可信度/估算/数据不足**；依据：DYN-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：动力学结果/物性来源。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用完整、估算和缺失物性三组输入。
2. 查看报告限定语。

通过标准：三组可信等级分明，估算不包装为精确，缺失不输出正式通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-07

**多负载/急停保持/包络合并**；依据：DYN-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：工况选择→动力学包络。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行多必验负载及支持的保持/急停设计工况。
2. 核对覆盖矩阵。

通过标准：包络含全部要求工况且来源可追溯，合并不替代必验覆盖。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-08

**曲线联动/峰值定位/时刻回放**；依据：DYN-08。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：动力学→曲线页/峰值定位/按时刻查询。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 换轴/通道。
2. 点峰值。
3. 输入指定时刻。

通过标准：曲线游标、峰值段/时间、三维机器人一致，回放零修订；缺源时诚实提示。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-09

**曲线数据导出**；依据：DYN-08。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：动力学→export-curve-data。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导出曲线副本。
2. 核对时序、单位及身份。
3. 测试取消/失败。

通过标准：可独立读回，原输出失败保留；不以空文件视为完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## DYN-10

**旋转/直线传动类型与codec**；依据：DYN-04/SEL-09-S1。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：建模耦合/传动参数及共享内核。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 对旋转速比、丝杠导程、齿条/同步带/直线电机标准样例运行映射。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：类型化单位/效率/功率守恒与序列化一致，不用旋转模型静默替代直线传动。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[DynamicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/plugin/DynamicsPanelWidget.cpp)；[InverseDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/InverseDynamics.cpp)；[ForwardDynamics.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/dynamics/src/ForwardDynamics.cpp)；[Evaluator.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/drivetrain/src/Evaluator.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`DYN-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-01

**版本化器件目录/字段字典**；依据：SEL-01。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型→目录管理/目录导入入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查清单、型号、曲线、兼容表及来源。
2. 导入小目录。

通过标准：目录版本/来源完整且可锁定；没有导入编辑入口须单独记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-02

**目录校验/曲线插值**；依据：SEL-02。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：目录导入报告/能力曲线内核。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用缺文件、重复型号、错引用/单位样例。
2. 再比较区间内插值及区间外请求。

通过标准：逐项可定位拒绝；分段线性正确，默认不外推。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-03

**电机硬能力筛选**；依据：SEL-03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型候选/淘汰明细。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用连续/峰值力矩、速度功率、持续窗、温度、电压、制动分别触边的样例。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：每个硬条件真实消费，淘汰包含轴/工况/实际/要求/单位。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-04

**减速器硬能力筛选**；依据：SEL-04。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型候选/淘汰明细。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别改变速比、额定/峰值、输入速、寿命、回差、安装与外载。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：各项能力按目录和策略判定，数据缺失不当作无限能力。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-05

**组合兼容/惯量比/逐轴工作点**；依据：SEL-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型组合结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选不兼容组合与可行组合。
2. 调整工程惯量比阈值。

通过标准：统一传动映射，兼容理由可追溯；惯量比不硬编码成普适常数。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-06

**候选/裕量/质量/成本/原因表**；依据：SEL-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型→候选表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选多组候选，逐一看淘汰和缺口分区。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：可行、淘汰、数据缺口分开；字段真实缺失显示缺失，来源/阈值明示。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-07

**企业偏好排序**；依据：SEL-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：企业偏好设置/内核。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改品牌、供货、系列偏好。
2. 重复筛选。

通过标准：偏好改变排序/软约束，不救活硬能力不合格组合；缺UI按边界记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-08

**目录差异/项目锁版**；依据：SEL-08。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：目录管理/差异查询。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 比较两个版本。
2. 项目锁旧版后更新全局目录。
3. 重开旧项目。

通过标准：差异字段明确，旧项目结果和版本不被静默升级。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-09

**旋转/直线器件范围**；依据：SEL-09/SEL-09-S1。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型目录与类型化传动入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别用旋转和已启用直线传动样例。
2. 检查六/七轴混合链。

通过标准：已启用直线传动按类型映射；不支持类型诚实范围外，4/5轴不因该项放开。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## SEL-10

**选型回填/物性合成/撤销**；依据：SEL-10。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：选型→回填。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择组合。
2. 预览物性合成并确认。
3. 撤销/重做。
4. 检查模型/引用。

通过标准：新修订记录目录/安装，壳体质量和转子等效惯性不重复，实际依赖失效且可撤销。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[SelCatalogPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/plugin/SelCatalogPanelWidget.cpp)；[CatalogValidation.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/CatalogValidation.cpp)；[Screening.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Screening.cpp)；[Backfill.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/selection/src/Backfill.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`SEL-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-01

**新机型/改型初始化**；依据：OPT-01。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化初始化内核/现有面板。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别建立新机型和基线改型。
2. 查看变量/基线候选。

通过标准：来源和基线明确，改型不改原基线；正式宿主初始化入口需核实。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-02

**连续/量化/离散变量绑定**；依据：OPT-02。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→变量表/变量配置入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 核对单位/上下界/步长/默认/锁定/授权/阶段/权威字段。
2. 尝试设置。

通过标准：表当前为只读投影，不冒充可编辑；实际候选补丁仅修改授权绑定。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-03

**硬约束/分期预检门控**；依据：OPT-03/11。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→约束/运行控制。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 造写集冲突、未登记绑定或缺上游。
2. 点检查并计算。

通过标准：阻塞与警告可定位，阶段锁阻止启动而非淘汰候选；缺动力器件数据不伪装全量联合。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-04

**Pareto非支配集合**；依据：OPT-04。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→候选表/比较。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用相互取舍及严格支配候选标准集。
2. 看幸存与排序。

通过标准：真实非支配关系正确，不以单一总分替代工程比较。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-05

**结构/传动外层＋联合内层**；依据：OPT-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：联合运行内核/运行控制。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用支持OPT-D的真实项目运行。
2. 查看轨迹/动力学/选型反馈。

通过标准：内外层依赖/证据真实贯通；仅静态StageB不可宣称完整联合。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-06

**Quick/Verified/局部改进/并行/检查点**；依据：OPT-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：运行控制/任务或工程师运行配置。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐项核对当前阶段启用面。
2. 固定seed重放。
3. 检查缓存及恢复。

通过标准：各能力按实际实现独立记结论；Quick不作为Verified，缺用户入口记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-07

**八指标A/B比较**；依据：OPT-07。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→候选→A/B选择及比较表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择两个候选。
2. 逐项查看单位、A、B、B−A。

通过标准：八项全显示，可算值正确，缺失为“—”；不同基准候选不能伪造公平对比。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-08

**候选预览/采用新分支**；依据：OPT-08。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：候选预览/设为当前方案入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 预览候选后取消。
2. 再采用支持候选并复算。
3. 核对分支历史。

通过标准：预览零修订，采用新分支/新修订并复算；当前面板未见采用按钮需记入口缺口。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-09

**鲁棒性/灵敏度冻结边界**；依据：OPT-09。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：扰动协议/内核预检。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查P-04判定阈值冻结状态。
2. 尝试未冻结运行。

通过标准：未冻结/数据不足必须阻断或降级，不私造阈值/正式鲁棒通过；已实现抽查单独记。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-10

**搜索策略扩展/确定性生成**；依据：OPT-10。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：策略配置/Generator/JointStrategy。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择已实现策略，固定seed和预算重复。
2. 再改seed。

通过标准：授权变量/候选序可追溯，缺入口不声称用户可选全部策略。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-11

**基线评估/预检定位**；依据：OPT-11。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→就绪/约束/基线结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先评估基线再运行。
2. 造绑定错误。
3. 点定位。

通过标准：基线与候选使用一致基准，阻塞不启动；实际初始化供给缺失如实提示。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-12

**一站证据导出**；依据：OPT-12。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化导出内核/正式入口待核实。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查JSON、候选/任务/审计CSV、Markdown和模型包。
2. 用过期契约重试。

通过标准：字段和身份一致，过期阻断正式导出；没有入口不能用工程师API调用替代UI交付。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## OPT-13

**运行进度/漏斗/取消**；依据：OPT-06/UX-10。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：优化→运行控制。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 点检查并计算。
2. 看进度和八段漏斗。
3. 点取消。

通过标准：显示真实运行/取消回执；空服务提示未装配，不能仅改变按钮文本伪造执行。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[OptimizationPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/plugin/OptimizationPanelWidget.cpp)；[Preflight.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Preflight.cpp)；[Run.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Run.cpp)；[JointRun.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/JointRun.cpp)；[Export.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/optimization/src/Export.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`OPT-13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-01

**结构化报告/章节完整性**；依据：RPT-01-B/01-C。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：文件→导出报告/报告Builder。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 从真实已归档结果生成。
2. 逐章核项目/模型/需求/运动碰撞/候选。
3. 全量再核轨迹动力选型。

通过标准：章节按阶段完整，审阅/签署元数据可追溯；未评估章节明确缺失。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-02

**HTML/JSON/CSV一致导出**；依据：RPT-02。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：导出报告→输出选择。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导出后浏览HTML并独立解析JSON/CSV。
2. 核对相同字段/单位。

通过标准：一致且可读，HTML无乱码/错引用；PDF未立项，不伪造已有PDF入口。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-03

**EvidenceBundle/复现要素**；依据：RPT-03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：导出评估包/证据包。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导出并隔离检查输入快照、结果引用、版本/seed/线程/容差。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：包可校验且足以定位同一输入；不是仅压缩截图。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-04

**方案对比报告**；依据：RPT-04。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：方案比较报告内核/现有导出路径。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选两个引用相同需求/工况基准的方案。
2. 再试不兼容基准。

通过标准：可比性声明正确，不同基准必须提示，不能生成误导的优劣结论。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-05

**限定语/正式通过门槛**；依据：RPT-05。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：报告正文/结论。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别生成估算、数据不足、外部验证缺失、正式可行及确定不可行记录。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：限定语保留；仅证据完整可行结果写正式通过；不可行可做评审记录但非通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-06

**报告往返复算/一致性**；依据：RPT-06。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：报告数据源→Roundtrip。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用导出数据复算。
2. 再有控制地改变一项结果/身份。

通过标准：匹配可追溯；不一致禁作正式结论并报差异，缺UI路径另记。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## RPT-07

**归档/覆盖取消/失败原子性**；依据：RPT-02/03。

- 实现形态：内核实现存在；界面/正式宿主供给需逐项核实。
- 入口：报告导出/项目归档。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 覆盖前取消。
2. 向不可写目录导出。
3. 重开项目查原归档。

通过标准：旧文件/归档保留，失败给路径原因建议，取消不改变设计修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：若面板显示未装配/未提供真实数据，功能链记BLOCKED；仅表格或按钮出现不能记全功能PASS。

源码核对：[Builder.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Builder.cpp)；[Render.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Render.cpp)；[Bundle.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Bundle.cpp)；[Roundtrip.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/reporting/src/Roundtrip.cpp)；[Commands.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/workflow/src/Commands.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`RPT-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。
