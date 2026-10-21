# 建模模块人工验收清单

基线：`redesign-main@4adc29f1bec2`；编制日期：2026-10-21；47个检查项。

**本文件是执行手册，不是已通过报告。全部数据、交互、真实三维结果初始均为NOT RUN。**

先阅读[准备数据与真实视觉方法](06-DATA-AND-VISUAL.md)，操作实际发布程序；不要以测试专用Harness代替正式宿主。

“界面代码存在”表示已查到控件/流程，不保证本机发布程序的服务供给完整。没有入口或出现未装配时，在记录表写BLOCKED并建问题，不能用内核通过代替人工PASS。

## 快速目录

|编号|功能|实现形态/边界|
|---|---|---|
|[MDL-M01](#mdl-m01)|六轴模板及安装预设|界面代码/域流程已存在，待人工验收|
|[MDL-M02](#mdl-m02)|七轴模板与显式权威|自定义七轴已实现；模板受P-03冻结边界约束|
|[MDL-M03](#mdl-m03)|结构增删/插入/重排/六轴重置|界面代码/域流程已存在，待人工验收|
|[MDL-M04](#mdl-m04)|关节类型/轴/原点/零位|界面代码/域流程已存在，待人工验收|
|[MDL-M05](#mdl-m05)|限位/速度/加速度|界面代码/域流程已存在，待人工验收|
|[MDL-M06](#mdl-m06)|双权威与只读派生|界面代码/域流程已存在，待人工验收|
|[MDL-M07](#mdl-m07)|显式转DH五状态|编辑/结果界面存在，数值消费需标准样例复核|
|[MDL-M08](#mdl-m08)|URDF字段/默认/忽略/不支持报告|界面代码/域流程已存在，待人工验收|
|[MDL-M09](#mdl-m09)|多分支显式选链|界面代码/域流程已存在，待人工验收|
|[MDL-M10](#mdl-m10)|continuous工程范围|界面代码/域流程已存在，待人工验收|
|[MDL-M11](#mdl-m11)|混合链与4/5轴阻止|界面代码/域流程已存在，待人工验收|
|[MDL-M12](#mdl-m12)|mimic/planar/floating/闭环|界面代码/域流程已存在，待人工验收|
|[MDL-M13](#mdl-m13)|Xacro参数与受控展开|界面代码/域流程已存在，待人工验收|
|[MDL-M14](#mdl-m14)|外部WorkCell反向导入|界面代码/域流程已存在，待人工验收|
|[MDL-M15](#mdl-m15)|视觉/碰撞几何加载与解绑|界面代码/域流程已存在，待人工验收|
|[MDL-M16](#mdl-m16)|占位连杆几何生成|界面代码/域流程已存在，待人工验收|
|[MDL-M17](#mdl-m17)|自碰撞/排除对/安全距离|界面代码/域流程已存在，待人工验收|
|[MDL-M18](#mdl-m18)|法兰与默认TCP|界面代码/域流程已存在，待人工验收|
|[MDL-M19](#mdl-m19)|工具创建/删除/几何与物性|界面代码/域流程已存在，待人工验收|
|[MDL-M20](#mdl-m20)|多个命名TCP|界面代码/域流程已存在，待人工验收|
|[MDL-M21](#mdl-m21)|实心/空心圆柱/矩形物性估算|界面代码/域流程已存在，待人工验收|
|[MDL-M22](#mdl-m22)|物性手工覆盖及质心确认|界面代码/域流程已存在，待人工验收|
|[MDL-M23](#mdl-m23)|质量/SPD/三角不等式断言|界面代码/域流程已存在，待人工验收|
|[MDL-M24](#mdl-m24)|行程策略与显式确认|界面代码/域流程已存在，待人工验收|
|[MDL-M25](#mdl-m25)|WC/DWC双编译原子应用|界面代码/域流程已存在，待人工验收|
|[MDL-M26](#mdl-m26)|名称映射与改名|界面代码/域流程已存在，待人工验收|
|[MDL-M27](#mdl-m27)|场景固定帧/障碍物管理|界面代码/域流程已存在，待人工验收|
|[MDL-M28](#mdl-m28)|额定/峰值力矩及摩擦|编辑/结果界面存在，数值消费需标准样例复核|
|[MDL-M29](#mdl-m29)|命名位姿/Home/Zero|界面代码/域流程已存在，待人工验收|
|[MDL-M30](#mdl-m30)|规范包与XML导出/导入|界面代码/域流程已存在，待人工验收|
|[MDL-M31](#mdl-m31)|耦合矩阵位置/力矩/限位映射|编辑/结果界面存在，数值消费需标准样例复核|
|[MDL-M32](#mdl-m32)|基座三预设与任意角|界面代码/域流程已存在，待人工验收|
|[MDL-M33](#mdl-m33)|结构/参数/物性差异|界面代码/域流程已存在，待人工验收|
|[MDL-H01](#mdl-h01)|进入建模/目标和就绪状态|界面代码/域流程已存在，待人工验收|
|[MDL-H02](#mdl-h02)|树↔属性↔三维选择|界面代码/域流程已存在，待人工验收|
|[MDL-H03](#mdl-h03)|批量粘贴/筛选/单位|界面代码/域流程已存在，待人工验收|
|[MDL-H04](#mdl-h04)|错误定位与中文工程文案|界面代码/域流程已存在，待人工验收|
|[MDL-H05](#mdl-h05)|暂存/确认/取消/脏标记|界面代码/域流程已存在，待人工验收|
|[MDL-H06](#mdl-h06)|本地撤销/重做|界面代码/域流程已存在，待人工验收|
|[MDL-H07](#mdl-h07)|项目撤销/重做|界面代码/域流程已存在，待人工验收|
|[MDL-H08](#mdl-h08)|保存草稿/关闭重开|界面代码/域流程已存在，待人工验收|
|[MDL-H09](#mdl-h09)|基线冲突/编辑重演|界面代码/域流程已存在，待人工验收|
|[MDL-H10](#mdl-h10)|只读会话|界面代码/域流程已存在，待人工验收|
|[MDL-H11](#mdl-h11)|命令可达/搜索/快捷键|界面代码/域流程已存在，待人工验收|
|[MDL-V01](#mdl-v01)|真实三维显示正确性|界面代码/域流程已存在，待人工验收|
|[MDL-V02](#mdl-v02)|三维显示模式与导航|界面代码/域流程已存在，待人工验收|
|[MDL-V03](#mdl-v03)|三维覆盖层与拾取|界面代码/域流程已存在，待人工验收|

## MDL-M01

**六轴模板及安装预设**；依据：MDL-01/AT-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具→模板新建。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 创建六轴模板。
2. 检查6可动关节及7连杆。
3. 分别选地面/壁装/倒挂。
4. 先取消再确认。

通过标准：数量、默认轴和限位符合模板；安装姿态在三维真实变化，取消不留对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M02

**七轴模板与显式权威**；依据：MDL-01/02、P-03。

- 实现形态：自定义七轴已实现；模板受P-03冻结边界约束。
- 入口：建模→模板/显式七轴编辑。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 查看七轴模板是否启用及说明。
2. 用支持的显式编辑创建7轴。
3. 尝试正式应用。

通过标准：已启用能力可以应用；P-03未冻结模板不能冒充可用，合法自定义七轴不被一概拒绝。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M03

**结构增删/插入/重排/六轴重置**；依据：MDL-01、§2.1。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→编辑→结构操作。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次新增、选中插入、删除、上移、下移。
2. 最后六轴重置并测试取消。

通过标准：树顺序/链计数正确，对象参数随身份保留；边界按钮不产生断链，重置有确认。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M04

**关节类型/轴/原点/零位**；依据：MDL-01/07/09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→编辑→关节编辑。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改关节类型、轴、Origin和零位。
2. 试零轴/非法值。
3. 确认与取消后换对象。

通过标准：仅权威可写字段改变，坐标系/单位清楚；不串到另一关节，非法原值保留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M05

**限位/速度/加速度**；依据：MDL-01/06/16。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→关节→限位/速度/加速度。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐轴填不同合法上下限、速度和加速度。
2. 试反向区间。
3. 应用后重开。

通过标准：逐轴保存且正式消费者读取；移动轴用m制、旋转轴用rad制，非法区间阻断。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M06

**双权威与只读派生**；依据：MDL-02/09/10/AT-16。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具→切换权威。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在DH模式尝试Axis/Origin编辑。
2. 切显式后编辑。
3. 再请求切回DH。

通过标准：派生项只读、权威项可写；切换先等价验证，不能两套权威同时生效。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M07

**显式转DH五状态**；依据：MDL-10/AT-16。

- 实现形态：编辑/结果界面存在，数值消费需标准样例复核。
- 入口：建模→切换权威→转换报告。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用五状态标准样例逐个分析。
2. 展开逐轴误差。
3. 对非Exact尝试切换。

通过标准：Exact/ExactNonUnique才可切DH；Approximate/NotExpressible/AnalysisFailed区分，误差/收敛状态完整。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M08

**URDF字段/默认/忽略/不支持报告**；依据：MDL-03/11/AT-15。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→导入URDF。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入带任意非Z轴的文件。
2. 再导入缺axis、零轴和NaN样例。
3. 预览/取消/确认。

通过标准：任意非零轴保留；缺轴按局部+X待确认；非法仅报告不提交，默认/忽略/不支持项可读。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M09

**多分支显式选链**；依据：MDL-12/AT-17。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→URDF/Xacro导入→主链选择。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选多分支文件。
2. 不选链尝试继续。
3. 明确选6/7轴主链再确认。

通过标准：必须显式选链，按所选主链判定；辅助分支不误拒也不被静默当主链。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M10

**continuous工程范围**；依据：MDL-06/12。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→continuous关节→工程工作范围。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 不确认范围尝试正式应用。
2. 输入有限区间并确认。
3. 检查跨周q。

通过标准：未确认阻断正式分析；确认不把continuous转revolute、不回写有限限位；多周不取模。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M11

**混合链与4/5轴阻止**；依据：MDL-12-S1/AT-36。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→混合链导入/类型编辑。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别应用六/七轴含prismatic链。
2. 再尝试4/5轴。
3. 进入运动学。

通过标准：合法6/7轴移动关节正式消费且量纲正确；4/5轴仍阻断并说明支持范围。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M12

**mimic/planar/floating/闭环**；依据：MDL-12/21。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→导入报告/耦合编辑。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入mimic、planar、floating、闭环样例。
2. 对支持的常量线性耦合单独确认。

通过标准：不支持链明确拒绝，不伪装Fixed；已启用线性耦合按约束模型处理。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M13

**Xacro参数与受控展开**；依据：MDL-19/AT-31。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→导入Xacro→添加参数。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择合法Xacro。
2. 增加用户参数并看展开结果。
3. 试缺依赖、语法错、安全拒绝。
4. 取消。

通过标准：参数实际参与生产展开，多链选择贯通；失败定位，来源记录，取消无草稿污染。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M14

**外部WorkCell反向导入**；依据：MDL-18/AT-33。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→导入WorkCell。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选含多个设备的.wc.xml。
2. 选目标链。
3. 逐项审阅有损报告后确认。
4. 取消另一轮。

通过标准：关节/限位/位姿/支持资源进入草稿，未恢复内容明确标缺；不冒充完整工程反向还原。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M14`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M15

**视觉/碰撞几何加载与解绑**；依据：MDL-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→连杆→视觉/碰撞几何挂接/替换。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 挂接已知非对称STL与OBJ。
2. 核对尺度/帧变换。
3. 替换、摘除、从视觉复制。
4. 试丢失/损坏资源。

通过标准：支持资源真实外形、位置和尺度正确，引用持久；未支持DAE/WRL/IV等须报缺口，不以代理几何代真网格。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-01：STL/OBJ基础路径存在；DAE/WRL/IV、scale和网格帧内变换完整性未封闭。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M15`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M16

**占位连杆几何生成**；依据：MDL-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具→生成占位几何。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 对无资源连杆生成。
2. 试重合相邻原点。
3. 检查各连杆端点。

通过标准：正常代理连续连接且明确标占位；退化链失败可定位，不声称已导入原始网格。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M16`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M17

**自碰撞/排除对/安全距离**；依据：MDL-04/ARC-05/AT-19。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：工程策略→碰撞/排除/安全距离。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用已知碰撞和无碰撞姿态。
2. 切自碰撞/环境、相邻排除/强制对。
3. 试正安全距离。

通过标准：策略真实消费且跨入口一致；当前正安全距离拒绝属未封闭边界，不能记完整碰撞PASS。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-02：正安全距离尚不支持，完整碰撞策略与真实高亮矩阵未齐。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M17`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M18

**法兰与默认TCP**；依据：MDL-04/13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具管理→编辑安装与TCP。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改工具安装链及默认TCP偏移。
2. 应用。
3. 运动学对照法兰与TCP。

通过标准：法兰→工具→TCP变换完整，三维坐标架/数值一致；默认引用受删除保护。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M18`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M19

**工具创建/删除/几何与物性**；依据：MDL-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具管理。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新增、改名、编辑安装/网格/质量/质心/惯量。
2. 确认/取消。
3. 删除被引用工具。

通过标准：完整工具属性保存与重开一致，非法物性阻断；删除保护明确，工具真实附着法兰。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M19`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M20

**多个命名TCP**；依据：MDL-13/AT-18。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→TCP列表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新增两个不同偏移TCP。
2. 改显示名/默认。
3. 删除非默认再尝试删除默认。
4. 运行指定TCP的FK/IK。

通过标准：各TCP身份和偏移独立、默认保护；求解命中具名TCP，不退回工具名或默认别名。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M20`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M21

**实心/空心圆柱/矩形物性估算**；依据：MDL-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→物性估算。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 增加圆柱/空心圆柱/矩形段元。
2. 选材料密度及旋转/平移。
3. 删除段元。
4. 确认。

通过标准：质量/质心/惯量符合解析与平行轴合成，估算来源可见；取消不覆盖原物性。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M21`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M22

**物性手工覆盖及质心确认**；依据：MDL-05/16。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→连杆物性手动覆盖。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 修改质心。
2. 分别选平行轴迁移与完整张量覆盖。
3. 先取消再确认。

通过标准：必须明确惯量基准处理，预览量纲/数值正确；取消不改，不能静默只改质心。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M22`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M23

**质量/SPD/三角不等式断言**；依据：MDL-06/AT-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→物性/就绪/应用。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐个给连杆非法质量、非SPD惯量及三角不等式反例。
2. 再留空物性。

通过标准：已提供非法值阻断并定位；缺物性标数据不足，不把缺失当零或跳过其他连杆检查。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M23`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M24

**行程策略与显式确认**；依据：MDL-06/ARC-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→限位→行程策略确认。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 把有限旋转行程设超过默认4π。
2. 先拒绝再确认。
3. 修改策略阈值重试。

通过标准：显示实际/阈值/rad及明确确认记录；硬非法区间不可确认绕过，continuous不套有限行程阈值。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M24`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M25

**WC/DWC双编译原子应用**；依据：MDL-06/AT-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→应用草稿。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录旧修订和三维。
2. 尝试可复现的编译失败样例。
3. 恢复合法后应用。

通过标准：WC/DWC任一失败不提交、不替换旧快照；成功才一次发布完整新修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M25`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M26

**名称映射与改名**；依据：MDL-14/AT-18。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→名称/导入名称映射。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改机械臂及关节名并应用。
2. 树选/三维拾取。
3. 看XML预览的各引用。

通过标准：同一对象映射唯一，旧前缀消失，无双前缀；几何/规则/动力学引用不失配。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M26`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M27

**场景固定帧/障碍物管理**；依据：MDL-15/AT-28。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→场景管理。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新增环境物体和工件。
2. 改世界位姿/资源。
3. 取消后确认。
4. 删除。
5. 改变基座安装。

通过标准：场景对象按世界固定，基座变化不把障碍拖走；环境碰撞确实消费同一对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M27`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M28

**额定/峰值力矩及摩擦**；依据：MDL-16。

- 实现形态：编辑/结果界面存在，数值消费需标准样例复核。
- 入口：建模→关节驱动/摩擦。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐轴填额定/峰值力矩、fv/fc/偏置。
2. 保留一个缺失字段。
3. 应用并进动力学。

通过标准：各字段和来源持久，单位可读；缺失触发相应降级，不以默认0假造完整动力学。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M28`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M29

**命名位姿/Home/Zero**；依据：MDL-17/AT-28。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→命名位姿/复位Home-Zero。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 保存自定义q。
2. 改名/移除。
3. 恢复到已应用模型。
4. 分别调用Home/Zero。

通过标准：实际关节姿态改变，使用正确保留键；恢复只改会话，HEAD不变；缺定义明确提示。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M29`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M30

**规范包与XML导出/导入**；依据：MDL-20/AT-28。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具→规范包/WorkCell导出与导入。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导出规范包、WC/DWC XML。
2. 在隔离项目回导。
3. 试覆盖取消/非法包。
4. 核对资源。

通过标准：支持字段与资源完整往返，预览只读已应用快照；失败保旧输出，无伪包静默接受。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M30`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M31

**耦合矩阵位置/力矩/限位映射**；依据：MDL-21/AT-38。

- 实现形态：编辑/结果界面存在，数值消费需标准样例复核。
- 入口：建模→编辑耦合矩阵…。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 输入非对角常量矩阵、窗口/电机参数。
2. 试奇异/病态矩阵。
3. 确认并在消费者对照。

通过标准：位置、限位和虚功力矩映射一致；非法矩阵拒绝，移除配置可撤销。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M31`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M32

**基座三预设与任意角**；依据：MDL-22/AT-37。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→基座安装…。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别选地面/壁装/倒挂。
2. Custom输入平移与RPY/EAA。
3. 还原/应用/重开。

通过标准：整组原子保存，三维与FK变换一致；旋转表示切换等价，世界障碍不动，重力消费正确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M32`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-M33

**结构/参数/物性差异**；依据：MDL-08/UX-13/AT-12。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→工具→与基线对比。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别改结构、参数和物性。
2. 打开差异页。
3. 点对象并取消。

通过标准：分组显示旧/新具体值和对象，非只显示计数；基线不被覆盖。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-M33`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H01

**进入建模/目标和就绪状态**；依据：MDL-07/UX-01/09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→编辑/工具/就绪/预览四页。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 从空项目到模板、缺参数、非法和可应用依次观察。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：目标、状态、阻断原因与下一步清楚；长文案不遮挡实际编辑。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-04：长表单机制修复不代表所有页面/DPI人工验齐。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H02

**树↔属性↔三维选择**；依据：MDL-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：共享树→建模属性→三维拾取。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 树选关节/连杆/工具/场景。
2. 三维反选。
3. 多选/清空。

通过标准：高亮与属性关联同一对象，域外运行时对象不误开放业务编辑。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H03

**批量粘贴/筛选/单位**；依据：UX-05/07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模参数表/筛选/批量粘贴。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 筛选某关节。
2. 粘多行带单位值。
3. 试非法一行。
4. 切m/mm和rad/deg。

通过标准：目标映射正确，单位分列；非法批次无未说明半更新，同对象刷新不丢暂存。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H04

**错误定位与中文工程文案**；依据：UX-03/05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模就绪条/导入报告/表单错误。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 触发非法轴、限位、物性和缺资源。
2. 定位对象并展开说明。

通过标准：中文可读，有对象/原因/建议和比较单位；不抛开发异常关闭程序。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H05

**暂存/确认/取消/脏标记**；依据：PM-04/UX-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模表单暂存/确认/取消。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 编辑A未确认。
2. 触发同对象刷新。
3. 换B。
4. 返回A。
5. 取消再确认。

通过标准：暂存不丢不串对象；确认写草稿、项目应用另有边界；脏标记真实。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H06

**本地撤销/重做**；依据：PM-18。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→撤销草稿编辑/重做草稿编辑。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次做字段、结构、工具、场景四类修改。
2. 逐次撤销/重做。

通过标准：恢复完整草稿对象与选择，无新项目修订；批次不会部分回滚。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H07

**项目撤销/重做**；依据：PM-18/PA-2。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：编辑→项目撤销/重做。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 应用两次含模型/工具/场景的变更。
2. 项目撤销再重做。
3. 重开。

通过标准：旧内容通过新修订恢复，三维及引用齐全，历史不可变。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H08

**保存草稿/关闭重开**；依据：PM-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：文件→保存草稿/关闭/重开。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 保存未应用草稿并重开。
2. 再应用后关闭重开。
3. 编辑同ID模型/工具/场景。

通过标准：草稿和正式模型可区分且均可恢复；根对象身份及可编辑性保留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-06：导入候选及逐对provenance的关闭重开持久语义仍未封闭。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H09

**基线冲突/编辑重演**；依据：PM-04/UX-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：建模→过期草稿/冲突旧稿对照。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 制造同项目基线变化。
2. 选择保留对照/重演/重新加载。
3. 逐个取消与确认。

通过标准：失败重演有对象定位，取消保留输入；导入候选/逐对来源重开完整性另按已知边界记录。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-06：导入候选及逐对provenance的关闭重开持久语义仍未封闭。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H10

**只读会话**；依据：PM-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：只读项目→建模。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 尝试字段/结构/工具/场景/应用。
2. 浏览差异/预览/导出。
3. 刷新。

通过标准：全部写路径持续禁用并解释，浏览和会话类操作按契约可用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-H11

**命令可达/搜索/快捷键**；依据：UX-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：命令面板→建模/工具页更多操作。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 搜索模板、URDF、Xacro、WorkCell、物性、差异、规范包和复位。
2. 键盘执行。

通过标准：实际可用命令均可到达，无内部键作主标题；未装配操作有诚实原因。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-H11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-V01

**真实三维显示正确性**；依据：MDL-04/22、UX-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维视图＋非对称真实网格模型。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用六轴/七轴/混合链，逐个安装预设。
2. 看非零q、工具TCP、世界障碍。

通过标准：真实外形/尺度/安装变换/末端数值与独立真值对应；代理、对象存在、DTO不能代替像素观察。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-08：完整真实视觉矩阵未齐；临时目标文字多重轮廓仍开放。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-V01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-V02

**三维显示模式与导航**；依据：UX-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维工具栏/相机操作。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 旋转平移缩放、标准视图、适配、Home、线框透明及分组显隐。
2. 重启。

通过标准：目标不漂移不丢失，恢复布局后仍可导航；无设计修订或错误碰撞开关变化。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-08：完整真实视觉矩阵未齐；临时目标文字多重轮廓仍开放。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-V02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## MDL-V03

**三维覆盖层与拾取**；依据：MDL-07/ARC-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维遮挡拾取/坐标架/碰撞高亮。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 让两对象前后遮挡。
2. 分别点可见与被遮区域。
3. 多选、清空、切项目。

通过标准：深度和命中正确，坐标架/高亮不误导；临时目标文字轮廓残留不得记通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：MA-08：完整真实视觉矩阵未齐；临时目标文字多重轮廓仍开放。

源码核对：[ModelingPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp)；[ModelingCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`MDL-V03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。
