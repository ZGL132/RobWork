# 运动学模块人工验收清单

基线：`redesign-main@4adc29f1bec2`；编制日期：2026-10-21；70个检查项。

**本文件是执行手册，不是已通过报告。全部数据、交互、真实三维结果初始均为NOT RUN。**

先阅读[准备数据与真实视觉方法](06-DATA-AND-VISUAL.md)，操作实际发布程序；不要以测试专用Harness代替正式宿主。

“界面代码存在”表示已查到控件/流程，不保证本机发布程序的服务供给完整。没有入口或出现未装配时，在记录表写BLOCKED并建问题，不能用内核通过代替人工PASS。

## 快速目录

|编号|功能|实现形态/边界|
|---|---|---|
|[KIN-K01](#kin-k01)|当前 FK 与双参考系 TCP|界面代码/分析路径存在，待人工验收|
|[KIN-K02](#kin-k02)|Jacobian 完整显示|界面可观察，内核语义需独立标准样例复核|
|[KIN-K03](#kin-k03)|奇异值、条件数、可操作度|界面可观察，内核语义需独立标准样例复核|
|[KIN-K04](#kin-k04)|逐关节裕量与最差关节|界面代码/分析路径存在，待人工验收|
|[KIN-K05](#kin-k05)|设备和 TCP 选择|界面代码/分析路径存在，待人工验收|
|[KIN-K06](#kin-k06)|XYZ＋RPY 单目标输入|界面代码/分析路径存在，待人工验收|
|[KIN-K07](#kin-k07)|目标单位切换|界面代码/分析路径存在，待人工验收|
|[KIN-K08](#kin-k08)|非法/空/非有限输入|界面代码/分析路径存在，待人工验收|
|[KIN-K09](#kin-k09)|三类多初值策略|界面代码/分析路径存在，待人工验收|
|[KIN-K10](#kin-k10)|残差与限位硬过滤|界面可观察，内核语义需独立标准样例复核|
|[KIN-K11](#kin-k11)|碰撞硬过滤|界面代码/分析路径存在，待人工验收|
|[KIN-K12](#kin-k12)|逐轴去重|界面可观察，内核语义需独立标准样例复核|
|[KIN-K13](#kin-k13)|四键稳定排序|界面可观察，内核语义需独立标准样例复核|
|[KIN-K14](#kin-k14)|搜索未果与不可达分离|界面可观察，内核语义需独立标准样例复核|
|[KIN-K15](#kin-k15)|候选检查器与双击|界面代码/分析路径存在，待人工验收|
|[KIN-K16](#kin-k16)|需求点只读消费|界面代码/分析路径存在，待人工验收|
|[KIN-K17](#kin-k17)|批量三态报告|界面代码/分析路径存在，待人工验收|
|[KIN-K18](#kin-k18)|多必验工况覆盖|界面代码/分析路径存在，待人工验收|
|[KIN-K19](#kin-k19)|后台进度/取消|界面代码/分析路径存在，待人工验收|
|[KIN-K20](#kin-k20)|跨项目与迟到回执|界面代码/分析路径存在，待人工验收|
|[KIN-K21](#kin-k21)|只读与无项目门控|界面代码/分析路径存在，待人工验收|
|[KIN-K22](#kin-k22)|区域参考系与独立TCP|界面代码/分析路径存在，待人工验收|
|[KIN-K23](#kin-k23)|冻结采样与身份|界面可观察，内核语义需独立标准样例复核|
|[KIN-K24](#kin-k24)|位置与姿态覆盖分母|界面可观察，内核语义需独立标准样例复核|
|[KIN-K25](#kin-k25)|混合数据不足/零样本|界面代码/分析路径存在，待人工验收|
|[KIN-K26](#kin-k26)|区域操作入口与结果|界面代码/分析路径存在，待人工验收|
|[KIN-K27](#kin-k27)|缺检测器/几何错误|界面代码/分析路径存在，待人工验收|
|[KIN-K28](#kin-k28)|共享策略一致性|界面代码/分析路径存在，待人工验收|
|[KIN-K29](#kin-k29)|Home/Zero与命名姿态|界面代码/分析路径存在，待人工验收|
|[KIN-K30](#kin-k30)|Jog|部分界面/内核存在，完整人工入口待核实|
|[KIN-K31](#kin-k31)|Playback|部分界面/内核存在，完整人工入口待核实|
|[KIN-K32](#kin-k32)|失败点三维|界面代码/分析路径存在，待人工验收|
|[KIN-K33](#kin-k33)|薄弱区三维|界面代码/分析路径存在，待人工验收|
|[KIN-K34](#kin-k34)|碰撞对象高亮|界面代码/分析路径存在，待人工验收|
|[KIN-K35](#kin-k35)|最差裕量三维|界面代码/分析路径存在，待人工验收|
|[KIN-K36](#kin-k36)|筛选与最差项排序入口|部分界面/内核存在，完整人工入口待核实|
|[KIN-K37](#kin-k37)|JSON/CSV导出|界面代码/分析路径存在，待人工验收|
|[KIN-K38](#kin-k38)|导出失败原子性|界面代码/分析路径存在，待人工验收|
|[KIN-K39](#kin-k39)|批量复算|界面代码/分析路径存在，待人工验收|
|[KIN-K40](#kin-k40)|随机均匀采样|界面代码/分析路径存在，待人工验收|
|[KIN-K41](#kin-k41)|关节网格采样|界面代码/分析路径存在，待人工验收|
|[KIN-K42](#kin-k42)|采样后台与进度取消|探索采样内联入口存在；后台进度/取消缺口|
|[KIN-K43](#kin-k43)|七种指标着色|界面代码/分析路径存在，待人工验收|
|[KIN-K44](#kin-k44)|XY/XZ/YZ投影|界面代码/分析路径存在，待人工验收|
|[KIN-K45](#kin-k45)|散点/包络/标签图例|界面代码/分析路径存在，待人工验收|
|[KIN-K46](#kin-k46)|PNG导出|界面代码/分析路径存在，待人工验收|
|[KIN-K47](#kin-k47)|选样本与三维回写|界面代码/分析路径存在，待人工验收|
|[KIN-K48](#kin-k48)|采样碰撞与策略跳转|界面代码/分析路径存在，待人工验收|
|[KIN-K49](#kin-k49)|外包络方向/默认180|内核能力存在；正式面板无已核实人工入口|
|[KIN-K50](#kin-k50)|外包络后台取消与标识|内核能力存在；正式面板无已核实人工入口|
|[KIN-K51](#kin-k51)|位姿计划预览|内核能力存在；正式面板无已核实人工入口|
|[KIN-K52](#kin-k52)|位姿后台报告|内核能力存在；正式面板无已核实人工入口|
|[KIN-K53](#kin-k53)|显示单位入口|界面代码/分析路径存在，待人工验收|
|[KIN-K54](#kin-k54)|inch/grad/turn子项|部分界面/内核存在，完整人工入口待核实|
|[KIN-K55](#kin-k55)|完整配置编辑|界面代码/分析路径存在，待人工验收|
|[KIN-K56](#kin-k56)|保存取消重开|界面代码/分析路径存在，待人工验收|
|[KIN-K57](#kin-k57)|运行身份和过期提示|界面可观察，内核语义需独立标准样例复核|
|[KIN-K58](#kin-k58)|策略权威|界面代码/分析路径存在，待人工验收|
|[KIN-K59](#kin-k59)|设默认TCP|界面代码/分析路径存在，待人工验收|
|[KIN-K60](#kin-k60)|设默认设备|界面代码/分析路径存在，待人工验收|
|[KIN-K61](#kin-k61)|空态/缺项/失败/过期|界面代码/分析路径存在，待人工验收|
|[KIN-K62](#kin-k62)|诊断定位及比较字段|界面代码/分析路径存在，待人工验收|
|[KIN-K63](#kin-k63)|键盘/复制粘贴/选择|界面代码/分析路径存在，待人工验收|
|[KIN-K64](#kin-k64)|真网格与TCP坐标架|界面代码/分析路径存在，待人工验收|
|[KIN-K65](#kin-k65)|三维导航与显示|界面代码/分析路径存在，待人工验收|
|[KIN-K66](#kin-k66)|DPI/缩放/布局恢复|界面代码/分析路径存在，待人工验收|
|[KIN-K67](#kin-k67)|响应与耗时隔离|界面代码/分析路径存在，待人工验收|
|[KIN-K68](#kin-k68)|黄金对照与确定性|界面可观察，内核语义需独立标准样例复核|
|[KIN-K69](#kin-k69)|修改与失效边界|界面代码/分析路径存在，待人工验收|
|[KIN-K70](#kin-k70)|归档重开与结果追溯|界面代码/分析路径存在，待人工验收|

## KIN-K01

**当前 FK 与双参考系 TCP**；依据：KIN-01。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→位姿指标。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选设备/TCP并设非零q。
2. 记录基座系/世界系FK。
3. 改基座安装后再读。

通过标准：两参考系满足世界安装变换，末端三维对应；精度须与独立矩阵链标准样例比对。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K02

**Jacobian 完整显示**；依据：KIN-01。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→位姿指标→Jacobian。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐关节小幅Jog。
2. 核对完整矩阵和坐标系说明。
3. 与标准样例有限差分表比较。

通过标准：全部行列可读，移动轴与旋转轴量纲明确，误差按样例/需求容差评定。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K03

**奇异值、条件数、可操作度**；依据：KIN-01。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→位姿指标→奇异指标。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 切正常、近奇异、奇异标准姿态。
2. 读奇异值、条件数、可操作度。

通过标准：数值与独立参考一致；无穷/不可定义有明确语义，NaN不得显示为正常数值。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K04

**逐关节裕量与最差关节**；依据：KIN-01。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→位姿指标→关节裕量。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 把各轴移动到中间、近界及受控越界样例。
2. 检查最差项。

通过标准：逐轴裕量及最差关节对应真实q/工作范围，单位明确；不把所有轴都标同一状态。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K05

**设备和 TCP 选择**；依据：KIN-01/14。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→位姿指标→设备/TCP选择。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在多具名偏移TCP和支持设备间切换。
2. 刷新FK再求同一目标。

通过标准：当前选择真实参与FK/IK，三维末端标记一致；选择不自动变更项目默认。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K06

**XYZ＋RPY 单目标输入**；依据：KIN-02。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→位姿指标→XYZ/RPY→求解IK。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 抄入已知FK可达位姿。
2. 加入非零工具偏移和混合RPY。
3. 点击求解。

通过标准：至少提供满足约束的解或诚实搜索状态；候选FK残差按实际配置核对。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K07

**目标单位切换**；依据：KIN-02/12。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学目标输入＋显示单位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 填目标后m/rad→mm/deg再求解。
2. 来回切三次。

通过标准：物理目标和解集合语义一致，不重复乘比例；相机/单位变化不新建设计修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K08

**非法/空/非有限输入**；依据：KIN-02。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→XYZ/RPY输入。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐字段试空、非法文本、NaN/Inf。
2. 点击求解。
3. 恢复合法输入。

通过标准：就地阻断并保留原文，提示字段/单位，非法输入不触发计算或覆盖旧结果。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K09

**三类多初值策略**；依据：KIN-02/13。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→求解配置→初值策略。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次ReferenceQ、SeededRandom、JointGrid。
2. 设不同数量/预算及固定seed。
3. 运行标准样例。

通过标准：策略和预算实际参与求解，重复运行可重放；预算不足不能证明不可达。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K10

**残差与限位硬过滤**；依据：KIN-02。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→IK结果/候选检查器。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用有超限/超残差候选的标准样例。
2. 查看过滤原因。
3. 尝试回写。

通过标准：不满足硬条件的候选不能列为可用解或回写，实际残差/阈值单位齐全。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K11

**碰撞硬过滤**；依据：KIN-02/05。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→IK结果→仅可用解。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用同目标有碰撞/无碰撞候选的真实模型。
2. 切可用筛选。
3. 再用全部碰撞样例。

通过标准：硬碰撞候选被过滤，存在安全解可用；全部过滤/缺证据不假报正式可行。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K12

**逐轴去重**；依据：KIN-02。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学多解表＋内核黄金对照。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 固定seed运行去重标准样例。
2. 导出逐轴q。
3. 按配置阈值独立比较。

通过标准：同末端不同构型保留，逐轴去重正确；continuous多周不按2π错误合并。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K13

**四键稳定排序**；依据：KIN-02/08。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→IK结果排序。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 固定相同输入和seed重复运行。
2. 导出并比较rank及四个排序指标。

通过标准：先硬过滤再逐轴去重；裕量、可操作度、当前距离、稳定编号按契约稳定排序。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K14

**搜索未果与不可达分离**；依据：KIN-02。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→IK结果状态。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用预算很小的可达难例和有解析不可达证明的样例。
2. 分别运行。

通过标准：搜索未果/预算耗尽为数据不足；确定不可行需证明来源，不能统写“不可达”。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K14`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K15

**候选检查器与双击**；依据：KIN-02/06。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→候选表/检查器。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 单击一行看详情。
2. 双击另一有效候选。
3. 记录机器人q与项目修订。

通过标准：详情对应行，三维机器人切到正确构型；仅会话变化，不触发设计失效。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K15`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K16

**需求点只读消费**；依据：KIN-03。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→任务点验证。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在需求中启用/禁用点并指定不同参考系/TCP后应用。
2. 刷新只读任务表。

通过标准：显示当前已应用需求且引用完整；不能从只读表修改需求权威或使用旧草稿。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K16`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K17

**批量三态报告**；依据：KIN-03。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→任务点验证→批量验证（后台）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 准备可行、确定不可行、搜索未果三态标准项目。
2. 运行并逐点查看。

通过标准：逐点三态和总计相符，搜索不足不变成工程失败，部分结果不伪装完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K17`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K18

**多必验工况覆盖**；依据：KIN-03/EVI-02。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→任务点验证→必验覆盖。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 需求建立至少两个启用Must工况。
2. 运行全部。
3. 对照逐点/工况矩阵。

通过标准：每个必验工况均有对应证据；缺一个仍不可正式通过，分母不缩小。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K18`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K19

**后台进度/取消**；依据：KIN-03/TASK。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：后台批量任务→进度/取消。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启动足够长但有界批量任务。
2. 真实点击取消。
3. 记录点击及状态时间。

通过标准：按项目取消要求及时进入取消中并停止派发；已产生部分结果明确标未完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K19`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K20

**跨项目与迟到回执**；依据：KIN-03/PM。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：批量运行期间→切换/关闭项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启动A任务。
2. 立即切B或关闭。
3. 等待A回执再重新打开A。

通过标准：旧回执不写B、不恢复A的三维覆盖到B；进程和关闭操作不挂死。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K20`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K21

**只读与无项目门控**；依据：KIN-03/PM-07。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：无项目/只读项目→运动学。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别尝试会话FK/IK和正式批量/覆盖。
2. 查看禁用说明。

通过标准：无模型给缺项路径；只读按契约禁止正式写结果，会话浏览不被过度禁止。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K21`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K22

**区域参考系与独立TCP**；依据：KIN-04。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：需求→区域参考系/TCP；运动学→区域覆盖。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用旋转参考Frame与非默认偏移TCP的非对称Box。
2. 运行小计划并导出。

通过标准：Box方向与TCP进入真实采样/IK；样本世界坐标和三维对应，非仅摘要变化。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K22`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K23

**冻结采样与身份**；依据：KIN-04。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：区域覆盖→同计划复算/导出。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 固定计划/seed运行两次。
2. 补全证据后复评。
3. 改变方向或TCP再运行。

通过标准：同计划冻结样本集合不变；改变实际采样输入能区分身份，不能仅替换摘要文字。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K23`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K24

**位置与姿态覆盖分母**；依据：KIN-04。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学→区域覆盖报告。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 查看计划位置总数与可达数。
2. 独立相除。
3. 对姿态用全局成功/总数计算。

通过标准：位置按存在至少一有效姿态判定，姿态按全局计数；不可达和数据不足均留分母。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K24`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K25

**混合数据不足/零样本**；依据：KIN-04。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→区域覆盖→零计划/缺证据。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行零样本及含数据不足样例。
2. 查看计数和结论。

通过标准：零样本覆盖率未定义；数据不足保留分母/单列计数并降级，不宣布正式覆盖通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K25`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K26

**区域操作入口与结果**；依据：KIN-04/UX。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→区域覆盖→运行覆盖评估（后台）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在需求定义至少两个区域并应用。
2. 分别选择运行。
3. 查看逐样本与定位。

通过标准：区域身份/名称不串，结果可从区域入口打开；缺入口须单独记失败。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K26`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K27

**缺检测器/几何错误**；依据：KIN-05。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→碰撞要求/结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用碰撞启用但设施/几何缺供的受控样例。
2. 对照正常PQP场景。

通过标准：缺证据明确DataInsufficient而非无碰撞；加载/运行失败原因可见，不降成假成功。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K27`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K28

**共享策略一致性**；依据：KIN-05/AT-19。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：工程策略→碰撞；FK/IK/批量三个消费入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 冻结同快照/姿态/策略。
2. 分别触发碰撞检查。
3. 比较对象对和排除来源。

通过标准：同输入判定一致；改显示掩码不改变碰撞结论，正安全距离仍按当前限制记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K28`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K29

**Home/Zero与命名姿态**；依据：KIN-06。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→复位Home；建模→Home/Zero/命名位姿。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先设不同的具名q。
2. 分别恢复。
3. 删去定义再调用。

通过标准：实际q和三维改变，缺定义明确；项目HEAD/模型身份和结果当前性不变。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K29`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K30

**Jog**；依据：KIN-06/UX。

- 实现形态：部分界面/内核存在，完整人工入口待核实。
- 入口：会话关节控制/Jog入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐轴通过现有键盘/滑块入口改变q。
2. 试边界。
3. 观察TCP。

通过标准：显示单位和限位正确，真实模型同步；入口未提供须记缺口，不能用测试调用代替鼠标操作。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K30`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K31

**Playback**；依据：KIN-06/AT-04。

- 实现形态：部分界面/内核存在，完整人工入口待核实。
- 入口：轨迹播放/运动学会话姿态。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用真实轨迹逐帧定位。
2. 检查是否实际提供播放/暂停/停止。
3. 操作现有按钮。

通过标准：姿态按时间正确；未暴露控制逐项记缺口，所有回放零修订且不触发结果失效。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K31`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K32

**失败点三维**；依据：KIN-07。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→结果与可视化→失败点。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行有失败点样例。
2. 点选该结果。
3. 切地面/倒挂模型与相机视图。

通过标准：失败点世界坐标、颜色、标签对应输入，不随相机或基座被错误变换两次。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K32`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K33

**薄弱区三维**；依据：KIN-07。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→结果可视化→弱区。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用近奇异/低可操作度样例。
2. 切指标并选点。

通过标准：弱状态与表格和图例一致，位置与真实采样对应；不能用二维散点冒充三维覆盖已验。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K33`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K34

**碰撞对象高亮**；依据：KIN-07。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→碰撞对象结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用已知实际相交的网格对。
2. 点击碰撞项。
3. 遮挡后定位。

通过标准：正确对象被高亮，有可见定位反馈；无碰撞/切项目后清理旧高亮。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K34`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K35

**最差裕量三维**；依据：KIN-07。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→最差关节裕量。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 构造特定一轴近界。
2. 点最差项。
3. 旋转安装后重复。

通过标准：显示正确轴与单位/数值，标记随真实关节变换，不指向同索引的错误对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K35`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K36

**筛选与最差项排序入口**；依据：KIN-08。

- 实现形态：部分界面/内核存在，完整人工入口待核实。
- 入口：运动学→结果表筛选/排序。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 切仅可用解。
2. 尝试最差项排序。
3. 选择和双击剩余行。
4. 清空筛选。

通过标准：筛选/排序不改变候选身份，检查器与回写一致；若无最差排序入口如实记边界。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K36`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K37

**JSON/CSV导出**；依据：KIN-08。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→结果与可视化→导出JSON/CSV。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选独立路径并导出。
2. 用文本编辑器/解析器查看字段。
3. 取消另一轮。

通过标准：状态、数值单位、模型/策略/配置身份完整，转义正确；导出不应用项目修改。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K37`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K38

**导出失败原子性**；依据：KIN-08。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→导出结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先导出有效文件。
2. 向不可写目录再次导出。
3. 取消覆盖。

通过标准：失败有路径/原因/建议且保留原文件字节，取消不写文件。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K38`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K39

**批量复算**；依据：KIN-08。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→复算（当前快照）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先运行批量并记录计划/seed/referenceQ。
2. 改变模型应用。
3. 点击复算。

通过标准：保留原任务定义并绑定新快照，覆盖原任务全集而非只算单点；身份/过期状态明确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K39`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K40

**随机均匀采样**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→工作空间采样→关节随机（FK采样）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设小样本数和seed。
2. 运行两次。
3. 导出点云。

通过标准：同seed样本可重放，范围合法，真实FK/策略被消费；不把随机样本当完整空间证明。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K40`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K41

**关节网格采样**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→工作空间采样→关节网格（FK采样）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 每轴设小网格数。
2. 检查总量。
3. 运行。
4. 逐步增量而非直接极大预算。

通过标准：按工作范围生成，合法端点和组合数正确，无整数溢出；内联运行规模限制明确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K41`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K42

**采样后台与进度取消**；依据：KIN-09。

- 实现形态：探索采样内联入口存在；后台进度/取消缺口。
- 入口：运动学→工作空间采样→运行采样。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 观察运行期间窗口/任务区。
2. 检查是否存在进度/取消。
3. 仅用小预算验证。

通过标准：当前按钮明确内联演示/诊断运行，不能记已实现后台进度取消；作为验收缺口保留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：KinematicsPanelWidget.cpp的运行采样tooltip明确为内联演示/诊断量级；不宣称异步已实现。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K42`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K43

**七种指标着色**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→工作空间指标着色。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次选状态、可操作度、条件数、余量、位置误差、姿态误差、碰撞。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：七种着色与原始值和图例一致，不支持/未定义量明确显示缺失，不编造0。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K43`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K44

**XY/XZ/YZ投影**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→工作空间投影。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用非对称点云依次XY俯视、XZ正视、YZ侧视。
2. 核对同一编号坐标。

通过标准：投影坐标、方向、轴标签和单位正确；不同平面不能只是更换标题。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K44`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K45

**散点/包络/标签图例**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→工作空间画布。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 切已有散点/包络/标签图例控件。
2. 试空、单点、共线点集。

通过标准：绘制与数据相符，无退化崩溃；包络为近似且不能冒充精确可达边界。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K45`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K46

**PNG导出**；依据：KIN-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→导出投影视图PNG。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 每种投影各导出一张。
2. 取消一次。
3. 用图片查看器打开。

通过标准：PNG可解码，投影、轴、单位、图例与窗口一致，无空白/裁切。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K46`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K47

**选样本与三维回写**；依据：KIN-09/06。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→样本编号→回写样本构型到会话。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选两个有效样本编号依次回写。
2. 试无效/不可用编号。
3. 查看三维与HEAD。

通过标准：回写对应q和末端，不可用样本拒绝；零修订且不使既有结果失效。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K47`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K48

**采样碰撞与策略跳转**；依据：KIN-09/05。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→策略摘要/策略入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 查看采样策略。
2. 从统一入口编辑碰撞策略并确认。
3. 只改显示分组后再运行。

通过标准：采样引用唯一工程策略，显示开关不成为第二碰撞开关；输入身份真实区分。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K48`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K49

**外包络方向/默认180**；依据：KIN-10。

- 实现形态：内核能力存在；正式面板无已核实人工入口。
- 入口：近似外包络内核/当前面板无已核实入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在正式程序搜索外包络。
2. 若无入口登记。
3. 由工程师用标准样例复核默认180方向的内核结果。

通过标准：核心存在不等于可点击交付；输出明确近似/误差信息，不伪装精确半径。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：有核内实现不等于正式宿主可达；入口/运行/取消/报告分别登记，不以区域覆盖替代。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K49`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K50

**外包络后台取消与标识**；依据：KIN-10。

- 实现形态：内核能力存在；正式面板无已核实人工入口。
- 入口：外包络运行/取消入口（未核实可达）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查正式宿主是否能提交、看进度、取消。
2. 若缺入口停止该人工路径并记录。

通过标准：不能借工作空间散点按钮冒充外包络后台取消；未执行保持NOT RUN/BLOCKED。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：有核内实现不等于正式宿主可达；入口/运行/取消/报告分别登记，不以区域覆盖替代。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K50`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K51

**位姿计划预览**；依据：KIN-11。

- 实现形态：内核能力存在；正式面板无已核实人工入口。
- 入口：独立位姿可达性内核/当前面板无已核实入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 搜索独立位姿分析。
2. 检查位置×方向×roll计划预览。
3. 工程师对照标准计划。

通过标准：计划数量和工具轴语义正确；无可达入口不能记人工已通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：有核内实现不等于正式宿主可达；入口/运行/取消/报告分别登记，不以区域覆盖替代。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K51`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K52

**位姿后台报告**；依据：KIN-11。

- 实现形态：内核能力存在；正式面板无已核实人工入口。
- 入口：独立位姿可达性后台/报告入口（未核实可达）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 查找运行/取消/方向覆盖报告。
2. 若可达用小计划执行。
3. 否则登记缺口。

通过标准：失败保分母且数据不足降级；不能用区域覆盖替代该独立功能的全部验收。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：有核内实现不等于正式宿主可达；入口/运行/取消/报告分别登记，不以区域覆盖替代。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K52`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K53

**显示单位入口**；依据：KIN-12。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→分析显示单位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 切m/cm/mm与deg/rad。
2. 查看目标、指标、图表和导出。
3. 比较修订。

通过标准：所有适用呈现一致换算、SI真值不变；仅改显示不提示重算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K53`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K54

**inch/grad/turn子项**；依据：KIN-12-S1。

- 实现形态：部分界面/内核存在，完整人工入口待核实。
- 入口：显示单位扩展项inch/grad/turn。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查真实下拉是否提供各项。
2. 若提供用25.4 mm=1 inch、400 grad=1 turn对照。

通过标准：换算正确且零修订；内核换算支持不能冒称界面单位选项已全部开放。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K54`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K55

**完整配置编辑**；依据：KIN-13。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→求解配置。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐项编辑初值策略/数量、迭代、位置/角容差、逐轴去重、区域预算/线程和seed。
2. 试非法值。

通过标准：每项实际参与配置，校验能定位，保存整组原子；不夹带碰撞/工程阈值副本。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K55`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K56

**保存取消重开**；依据：KIN-13。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→保存配置…/确认保存/再改改/取消恢复。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 修改整组。
2. 先再改改再确认。
3. 关重开。
4. 另做非法输入并取消恢复。

通过标准：确认后真实持久、取消恢复原组；缺持久供给明确禁用，不假报保存成功。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K56`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K57

**运行身份和过期提示**；依据：KIN-13。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学配置/结果当前性。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别只改一个实际求解字段后运行。
2. 再仅换单位。
3. 比较运行身份和过期提示。

通过标准：求解输入按依赖影响身份/当前性；单位不改变SI计算身份，新旧配置不可误作同一结果。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K57`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K58

**策略权威**；依据：KIN-13/UX-08。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学配置＋工程策略。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 检查配置页无策略重复权威。
2. 按项目修订编辑策略再撤销。
3. 运行。

通过标准：实际解析对应修订的不可变策略；当前策略不能反向污染旧修订计算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K58`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K59

**设默认TCP**；依据：KIN-14。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→设默认TCP。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先选择非默认具名TCP。
2. 确认默认变更。
3. 重开并求解。
4. 项目撤销。

通过标准：产生一个命令修订，后续默认真实消费；旧显式引用保持，撤销恢复。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K59`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K60

**设默认设备**；依据：KIN-14。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学→设默认设备。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选合法设备确认。
2. 只读/过期下重试。
3. 撤销。

通过标准：只对合法项目对象产生新修订，拒绝无半提交；后续消费者使用新默认。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K60`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K61

**空态/缺项/失败/过期**；依据：UX-01/02/10。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学各页空态/运行状态。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别无模型、缺需求、未运行、运行中、过期、数据不足和失败。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：状态与下一步清楚，空值不以0冒充结果，内部hash不替代工程说明。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K61`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K62

**诊断定位及比较字段**；依据：UX-03。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学表单/诊断区。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 触发非法目标、超限、超预算、缺设施。
2. 打开完整详情并定位。

通过标准：对象、原因、建议完整；比较型条目列要求/实际/单位，中文长文本可读。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K62`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K63

**键盘/复制粘贴/选择**；依据：UX-05。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：运动学只读结果表/目标输入。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用Tab/方向键/Enter/Esc操作。
2. 复制结果。
3. 粘合法与非法目标文本。

通过标准：焦点和选择正确、复制内容可用；只读表不产生脏业务修改，非法粘贴不触发计算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K63`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K64

**真网格与TCP坐标架**；依据：UX-06/11。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：真实三维模型/TCP坐标架。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 加载支持的非对称真网格。
2. 用偏移TCP及多安装姿态。
3. 对照独立FK。

通过标准：网格尺度/姿态、TCP原点和轴正确；图例与表一致，代理模型不能替代。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K64`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K65

**三维导航与显示**；依据：UX-09/11。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：三维工具栏/鼠标导航。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐一旋转、平移、缩放、标准视图、线框/透明/显隐。
2. 检查拾取与叠层。

通过标准：相机变化不改对象真值；叠层不漂移，缺投影入口须单列，不能默认已支持。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K65`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K66

**DPI/缩放/布局恢复**；依据：UX-09。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：主窗口/运动学Dock/系统DPI。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 窄窗、高DPI逐页到最底。
2. 隐藏恢复面板和布局。
3. 导出PNG。

通过标准：全部输入/按钮可达，不水平挤出屏幕；恢复布局不丢正在编辑的合法配置。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K66`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K67

**响应与耗时隔离**；依据：NFR-PERF-01。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：后台任务负载下的窗口交互。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 按性能清单固定项目。
2. 人工操作至少15次并录像/记时。
3. 统计响应样本。

通过标准：按NFR标准计算P95≤200ms且无>2s冻结；仅主观觉得流畅不能作为性能PASS。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K67`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K68

**黄金对照与确定性**；依据：NFR-COR-01/02。

- 实现形态：界面可观察，内核语义需独立标准样例复核。
- 入口：运动学标准样例→独立参考结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 六/七轴、移动轴、正常/奇异/零近零逐组运行。
2. 固定线程和seed重复。

通过标准：数值在需求/样例容差内，集合及排序按确定性约定一致；不以同一实现自比作独立参考。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K68`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K69

**修改与失效边界**；依据：CON-05/AT-05。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：模型/TCP/策略与会话/显示变更。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先有正式结果。
2. 逐次应用真实依赖修改。
3. 再只Jog、改颜色/单位。

通过标准：依赖修改正确提示重算；会话姿态与呈现不误使设计结果过期。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K69`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## KIN-K70

**归档重开与结果追溯**；依据：PM/EVI。

- 实现形态：界面代码/分析路径存在，待人工验收。
- 入口：已完成结果→关闭/重开项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 完成正式运行后关闭程序重开。
2. 查结果与快照/工况/配置绑定。
3. 对照内存预览。

通过标准：归档结果可追溯并区分当前/过期；内存预览不得冒充持久正式证据。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：完整正式宿主联合/视觉证据仍需本次人工取得；涉及MA-01/MA-02/MA-07/MA-08。

源码核对：[KinematicsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp)；[KinEvaluationChannel.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`KIN-K70`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。
