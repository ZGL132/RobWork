# 需求模块人工验收清单

基线：`redesign-main@4adc29f1bec2`；编制日期：2026-10-21；120个检查项。

**本文件是执行手册，不是已通过报告。全部数据、交互、真实三维结果初始均为NOT RUN。**

先阅读[准备数据与真实视觉方法](06-DATA-AND-VISUAL.md)，操作实际发布程序；不要以测试专用Harness代替正式宿主。

“界面代码存在”表示已查到控件/流程，不保证本机发布程序的服务供给完整。没有入口或出现未装配时，在记录表写BLOCKED并建问题，不能用内核通过代替人工PASS。

## 快速目录

|编号|功能|实现形态/边界|
|---|---|---|
|[REQ-R01-01](#req-r01-01)|新建关键任务点|界面代码/域流程已存在，待人工验收|
|[REQ-R01-02](#req-r01-02)|复制工位|界面代码/域流程已存在，待人工验收|
|[REQ-R01-03](#req-r01-03)|删除工位及选中回退|界面代码/域流程已存在，待人工验收|
|[REQ-R01-04](#req-r01-04)|被引用工位删除保护|界面代码/域流程已存在，待人工验收|
|[REQ-R01-05](#req-r01-05)|名称和工艺标签编辑|界面代码/域流程已存在，待人工验收|
|[REQ-R01-06](#req-r01-06)|参考坐标系选择|界面代码/域流程已存在，待人工验收|
|[REQ-R01-07](#req-r01-07)|TCP 引用选择|界面代码/域流程已存在，待人工验收|
|[REQ-R01-08](#req-r01-08)|任务点 XYZ 位置|界面代码/域流程已存在，待人工验收|
|[REQ-R01-09](#req-r01-09)|六个受约束/自由分量|界面代码/域流程已存在，待人工验收|
|[REQ-R01-10](#req-r01-10)|位置与姿态容差|界面代码/域流程已存在，待人工验收|
|[REQ-R01-11](#req-r01-11)|Must/Should 等级与启用|界面代码/域流程已存在，待人工验收|
|[REQ-R01-12](#req-r01-12)|唯一标识、重名和字段状态|界面代码/域流程已存在，待人工验收|
|[REQ-R02-01](#req-r02-01)|接近段启用、方向、距离|界面代码/域流程已存在，待人工验收|
|[REQ-R02-02](#req-r02-02)|作业段定义|界面代码/域流程已存在，待人工验收|
|[REQ-R02-03](#req-r02-03)|撤离段启用、方向、距离|界面代码/域流程已存在，待人工验收|
|[REQ-R02-04](#req-r02-04)|顺序键、重复与环路|相关内核/视图能力存在，具体人工入口需核实|
|[REQ-R03-01](#req-r03-01)|Box 区域新增与中心尺寸|界面代码/域流程已存在，待人工验收|
|[REQ-R03-02](#req-r03-02)|按数量网格采样|界面代码/域流程已存在，待人工验收|
|[REQ-R03-03](#req-r03-03)|按间距网格采样|界面代码/域流程已存在，待人工验收|
|[REQ-R03-04](#req-r03-04)|随机位置采样与种子|界面代码/域流程已存在，待人工验收|
|[REQ-R03-05](#req-r03-05)|方向姿态采样|界面代码/域流程已存在，待人工验收|
|[REQ-R03-06](#req-r03-06)|工具滚转姿态采样|界面代码/域流程已存在，待人工验收|
|[REQ-R03-07](#req-r03-07)|最低覆盖率|界面代码/域流程已存在，待人工验收|
|[REQ-R03-08](#req-r03-08)|退化区域、零计划与计数溢出|界面代码/域流程已存在，待人工验收|
|[REQ-R03-09](#req-r03-09)|中性采样网格预览|界面代码/域流程已存在，待人工验收|
|[REQ-R03-10](#req-r03-10)|Good/Weak/Failed 样本着色与图例|界面/内核存在，跨模块或三维完整链待验|
|[REQ-R03-11](#req-r03-11)|选中样本蓝圈/三维定位|界面/内核存在，跨模块或三维完整链待验|
|[REQ-R04-01](#req-r04-01)|工况新增、复制与删除|界面代码/域流程已存在，待人工验收|
|[REQ-R04-02](#req-r04-02)|工况改名|界面代码/域流程已存在，待人工验收|
|[REQ-R04-03](#req-r04-03)|引用环境障碍/场景对象|界面代码/域流程已存在，待人工验收|
|[REQ-R04-04](#req-r04-04)|负载质量 kg|界面代码/域流程已存在，待人工验收|
|[REQ-R04-05](#req-r04-05)|负载质心 m 与惯量 kg·m²|界面代码/域流程已存在，待人工验收|
|[REQ-R04-06](#req-r04-06)|Grasp/Release/Dwell 事件|界面代码/域流程已存在，待人工验收|
|[REQ-R04-07](#req-r04-07)|最小关节裕量与无碰撞要求|界面代码/域流程已存在，待人工验收|
|[REQ-R04-08](#req-r04-08)|工况适用范围和工位清单|界面代码/域流程已存在，待人工验收|
|[REQ-R04-09](#req-r04-09)|目标节拍 s|界面代码/域流程已存在，待人工验收|
|[REQ-R04-10](#req-r04-10)|必验集合唯一派生规则|界面代码/域流程已存在，待人工验收|
|[REQ-R05-01](#req-r05-01)|CSV 真实文件读取|界面代码/域流程已存在，待人工验收|
|[REQ-R05-02](#req-r05-02)|字段字典、别名自动识别|界面代码/域流程已存在，待人工验收|
|[REQ-R05-03](#req-r05-03)|手动字段映射|界面代码/域流程已存在，待人工验收|
|[REQ-R05-04](#req-r05-04)|单位声明与 mm/deg→SI 预览|界面代码/域流程已存在，待人工验收|
|[REQ-R05-05](#req-r05-05)|错误行定位到行/列/原文|界面代码/域流程已存在，待人工验收|
|[REQ-R05-06](#req-r05-06)|正确行保留并跳过坏行|界面代码/域流程已存在，待人工验收|
|[REQ-R05-07](#req-r05-07)|缺列、重复标识/名称、非法单位|界面代码/域流程已存在，待人工验收|
|[REQ-R05-08](#req-r05-08)|公式只按文本数据解析|界面代码/域流程已存在，待人工验收|
|[REQ-R05-09](#req-r05-09)|CSV 可逆转义与方言标识|界面代码/域流程已存在，待人工验收|
|[REQ-R05-10](#req-r05-10)|取消或失败不写草稿|界面代码/域流程已存在，待人工验收|
|[REQ-R05-11](#req-r05-11)|导入溯源摘要与原始行号|界面代码/域流程已存在，待人工验收|
|[REQ-R06-01](#req-r06-01)|任意草稿保存与恢复|界面代码/域流程已存在，待人工验收|
|[REQ-R06-02](#req-r06-02)|应用生成新修订|界面代码/域流程已存在，待人工验收|
|[REQ-R06-03](#req-r06-03)|启用非法 Must 阻断正式运行|界面代码/域流程已存在，待人工验收|
|[REQ-R06-04](#req-r06-04)|有效 Must 不满足与 Should 警告|界面代码/域流程已存在，待人工验收|
|[REQ-R06-05](#req-r06-05)|部分预览只检查有效条目|相关内核/视图能力存在，具体人工入口需核实|
|[REQ-R06-06](#req-r06-06)|过期修订冲突及草稿保留|界面代码/域流程已存在，待人工验收|
|[REQ-R06-07](#req-r06-07)|未应用编辑不使正式结果失效|界面代码/域流程已存在，待人工验收|
|[REQ-R06-08](#req-r06-08)|分层就绪校验（当前界面R1～R8；保留旧R0～R9检查来源）|界面代码/域流程已存在，待人工验收|
|[REQ-R06-09](#req-r06-09)|校验页筛选、对象跳转和三要素|界面代码/域流程已存在，待人工验收|
|[REQ-R07-01](#req-r07-01)|拆垛/拣选模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-02](#req-r07-02)|机床上下料模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-03](#req-r07-03)|码垛模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-04](#req-r07-04)|检测模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-05](#req-r07-05)|换工具模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-06](#req-r07-06)|交接模板|界面代码/域流程已存在，待人工验收|
|[REQ-R07-07](#req-r07-07)|模板参数、数量实时预览|界面代码/域流程已存在，待人工验收|
|[REQ-R07-08](#req-r07-08)|来源徽标和批次溯源|界面代码/域流程已存在，待人工验收|
|[REQ-R07-09](#req-r07-09)|按参数重生成及手改冲突|界面代码/域流程已存在，待人工验收|
|[REQ-R07-10](#req-r07-10)|解除关联保留条目|界面代码/域流程已存在，待人工验收|
|[REQ-R08-01](#req-r08-01)|三维场景创建目标|界面代码/域流程已存在，待人工验收|
|[REQ-R08-02](#req-r08-02)|三维拖动与检查目标|界面代码/域流程已存在，待人工验收|
|[REQ-R08-03](#req-r08-03)|拾取/捕获写回前确认|界面代码/域流程已存在，待人工验收|
|[REQ-R09-01](#req-r09-01)|固定姿态 Fixed|界面代码/域流程已存在，待人工验收|
|[REQ-R09-02](#req-r09-02)|对齐参考系 AlignFrame|界面代码/域流程已存在，待人工验收|
|[REQ-R09-03](#req-r09-03)|对齐几何法向 AlignGeometryNormal|界面代码/域流程已存在，待人工验收|
|[REQ-R09-04](#req-r09-04)|指向目标 PointAtTarget|界面代码/域流程已存在，待人工验收|
|[REQ-R09-05](#req-r09-05)|工具滚转自由 ToolRollFree|界面代码/域流程已存在，待人工验收|
|[REQ-R09-06](#req-r09-06)|规则解析来源、确定性和失败定位|界面代码/域流程已存在，待人工验收|
|[REQ-R10-01](#req-r10-01)|从Frame拾取参考对象|界面代码/域流程已存在，待人工验收|
|[REQ-R10-02](#req-r10-02)|从几何特征拾取法向|界面代码/域流程已存在，待人工验收|
|[REQ-R10-03](#req-r10-03)|从当前场景捕获TCP|界面代码/域流程已存在，待人工验收|
|[REQ-R10-04](#req-r10-04)|过期会话诊断与来源标签|界面代码/域流程已存在，待人工验收|
|[REQ-R11-01](#req-r11-01)|过参考系镜像面镜像|界面代码/域流程已存在，待人工验收|
|[REQ-R11-02](#req-r11-02)|不可镜像规则与派生无环|界面代码/域流程已存在，待人工验收|
|[REQ-R11-03](#req-r11-03)|线性阵列完整参数|界面代码/域流程已存在，待人工验收|
|[REQ-R11-04](#req-r11-04)|矩形阵列行列层、各轴间距|界面代码/域流程已存在，待人工验收|
|[REQ-R11-05](#req-r11-05)|圆形阵列半径、角度和轴|界面代码/域流程已存在，待人工验收|
|[REQ-R11-06](#req-r11-06)|三维折线阵列控制点|界面代码/域流程已存在，待人工验收|
|[REQ-R11-07](#req-r11-07)|局部撤销/重做与批次整体回滚|界面代码/域流程已存在，待人工验收|
|[REQ-R11-08](#req-r11-08)|项目撤销与需求草稿撤销分离|界面代码/域流程已存在，待人工验收|
|[REQ-R12-01](#req-r12-01)|需求JSON导入|界面代码/域流程已存在，待人工验收|
|[REQ-R12-02](#req-r12-02)|JSON同字段字典、安全与版本错误|界面代码/域流程已存在，待人工验收|
|[REQ-R12-03](#req-r12-03)|导出JSON草稿副本|界面代码/域流程已存在，待人工验收|
|[REQ-R12-04](#req-r12-04)|副本不修改项目、原子失败保旧文件|界面代码/域流程已存在，待人工验收|
|[REQ-U01](#req-u01)|空态引导与缺输入呈现|界面代码/域流程已存在，待人工验收|
|[REQ-U02](#req-u02)|工程用语与隐藏内部实现词|界面代码/域流程已存在，待人工验收|
|[REQ-U03](#req-u03)|失败提示对象、原因、建议及比较值单位|界面代码/域流程已存在，待人工验收|
|[REQ-U04](#req-u04)|高级采样/种子与开发诊断折叠|界面代码/域流程已存在，待人工验收|
|[REQ-U05](#req-u05)|批量粘贴及筛选需求参数|界面代码/域流程已存在，待人工验收|
|[REQ-U06](#req-u06)|单位显示与显示单位切换|界面代码/域流程已存在，待人工验收|
|[REQ-U07](#req-u07)|四页树表联动及面包屑|界面代码/域流程已存在，待人工验收|
|[REQ-U08](#req-u08)|列表状态词、三维颜色与图例一致|界面代码/域流程已存在，待人工验收|
|[REQ-U09](#req-u09)|表单应用确认、取消及会话显示不弹保存|界面代码/域流程已存在，待人工验收|
|[REQ-U10](#req-u10)|工程策略摘要、入口与显示开关分离|界面代码/域流程已存在，待人工验收|
|[REQ-U11](#req-u11)|停靠、隐藏、恢复和跨会话布局|界面代码/域流程已存在，待人工验收|
|[REQ-U12](#req-u12)|窄Dock与常用分辨率可读可操作|界面代码/域流程已存在，待人工验收|
|[REQ-U13](#req-u13)|七种公共状态及取消/过期原因|界面代码/域流程已存在，待人工验收|
|[REQ-U14](#req-u14)|顶/前/右及轴测标准视图|界面代码/域流程已存在，待人工验收|
|[REQ-U15](#req-u15)|缩放、自动缩放和Home|界面代码/域流程已存在，待人工验收|
|[REQ-U16](#req-u16)|相机旋转与透视/正交|相关内核/视图能力存在，具体人工入口需核实|
|[REQ-U17](#req-u17)|线框/透明及渲染分组显隐|界面代码/域流程已存在，待人工验收|
|[REQ-U18](#req-u18)|碰撞高亮与组掩码|界面/内核存在，跨模块或三维完整链待验|
|[REQ-U19](#req-u19)|PNG视图截图与真实ray-cast|相关内核/视图能力存在，具体人工入口需核实|
|[REQ-U20](#req-u20)|需求阶段门控与下游失效提示|界面/内核存在，跨模块或三维完整链待验|
|[REQ-U21](#req-u21)|需求命令搜索、快捷键与键盘导航|界面代码/域流程已存在，待人工验收|
|[REQ-U22](#req-u22)|帮助、版本和插件清单|界面代码/域流程已存在，待人工验收|
|[REQ-U23](#req-u23)|只读项目禁编辑但可浏览/导出|界面代码/域流程已存在，待人工验收|
|[REQ-U24](#req-u24)|项目关闭/切换清理三维预览|界面/内核存在，跨模块或三维完整链待验|
|[REQ-J-01](#req-j-01)|对象身份、切片与修订隔离|界面/内核存在，跨模块或三维完整链待验|
|[REQ-J-02](#req-j-02)|下游覆盖分母与必验工况全覆盖|界面/内核存在，跨模块或三维完整链待验|
|[REQ-J-03](#req-j-03)|段/负载/事件进入轨迹动力学|界面/内核存在，跨模块或三维完整链待验|
|[REQ-J-04](#req-j-04)|工具回填后需求引用不改、下游重算|界面/内核存在，跨模块或三维完整链待验|

## REQ-R01-01

**新建关键任务点**；依据：REQ-01／V-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→新增。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 打开可写验收项目。
2. 点新增工位。
3. 选中新点检查完整定义。

通过标准：列表增加1项，默认值明确，树和属性选中同一点；应用前只是草稿。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-02

**复制工位**；依据：REQ-01／V-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→复制。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选有名称/姿态/容差的工位。
2. 点复制。
3. 只修改副本名称和X。

通过标准：副本初始业务字段一致、身份独立；修改副本不影响源点。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-03

**删除工位及选中回退**；依据：REQ-01／V-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→删除。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 建立3点并选中间项。
2. 删除后撤销/重做。
3. 再删除最后项。

通过标准：仅目标被删，选中回退合法；空集合无悬空检查器，撤销可还原。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-04

**被引用工位删除保护**；依据：REQ-01／V-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→适用范围/事件，再回工位删除。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 把工位加入工况清单及事件。
2. 尝试删除该点。
3. 解除引用后重试。

通过标准：被引用时给对象/原因/处理建议且不半删；解除引用后才能删除。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-05

**名称和工艺标签编辑**；依据：REQ-01／UX-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→编辑名称、参考系/TCP与完整姿态…。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 改为“验收点A”并改工艺类别。
2. 确认。
3. 切换工位再返回。

通过标准：名称、工艺类别、树标签一致；取消编辑不改原数据，中文不乱码。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-06

**参考坐标系选择**；依据：REQ-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→完整定义→参考系。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次选择World、模型Frame、场景Frame。
2. 保持局部XYZ并确认。
3. 对照三维世界位置。

通过标准：局部值与所选参考系绑定，世界点按完整变换链变化；失效引用可定位，不能静默按World解释。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-07

**TCP 引用选择**；依据：REQ-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→完整定义→TCP。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选默认TCP后改为具名偏移TCP。
2. 应用需求。
3. 到运动学批量验证。

通过标准：引用在重开后仍有效，求解实际使用该TCP；切换项目无旧目录残留。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-08

**任务点 XYZ 位置**；依据：REQ-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→位置XYZ。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 输入0.20、0.10、0.30 m。
2. 确认后分别只改一轴。
3. 看顶/前/右视图。

通过标准：每次只变指定分量；三维位置与所选参考系下数值一致，不把mm当m。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-09

**六个受约束/自由分量**；依据：REQ-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→六分量约束。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐一切换XYZ/Roll/Pitch/Yaw约束/自由。
2. 确认并运行对应IK。
3. 再恢复全约束。

通过标准：掩码持久且求解遵守自由分量；不得只改变控件而仍按全姿态求解。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-10

**位置与姿态容差**；依据：REQ-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→位置/姿态容差。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别输入正值。
2. 再试0、负数、空值和非法文本。
3. 取消后重新打开。

通过标准：按需求合法域校验，就地提示，非法提交不覆盖有效值；m/rad或当前显示单位明确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-11

**Must/Should 等级与启用**；依据：REQ-01／REQ-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→等级/启用。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 切换Must、Should。
2. 关闭/开启该点。
3. 执行校验并观察下游任务表。

通过标准：启用状态和强软约束一致；禁用条目不冒充必验输入。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R01-12

**唯一标识、重名和字段状态**；依据：REQ-01／V-01。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位完整定义/导入预览。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 复制工位后输入同名。
2. 从副本JSON构造重复身份。
3. 观察状态徽标。

通过标准：重复身份被拒绝且无半提交；重名按权威规则处理，不静默改名；未知值不显示为已提供。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R01-12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R02-01

**接近段启用、方向、距离**；依据：REQ-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→接近段。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启用接近段。
2. 切换ToolZ/ReferenceZ并输入不同距离。
3. 换为旋转参考系。

通过标准：启停、方向、距离独立保存；路径方向遵守所选轴，合法域及单位明确。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R02-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R02-02

**作业段定义**；依据：REQ-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→作业段/任务顺序。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 配置作业段。
2. 把多点加入任务序列。
3. 到轨迹模块预览。

通过标准：段定义和顺序被真实消费者读取；仅保存字段不能替代轨迹联动通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R02-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R02-03

**撤离段启用、方向、距离**；依据：REQ-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→撤离段。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置与接近不同的轴及距离。
2. 单独取消再确认。
3. 预览任务路径。

通过标准：撤离不串用接近参数；停用后不生成撤离运动，取消零写入。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R02-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R02-04

**顺序键、重复与环路**；依据：REQ-02／V-19。

- 实现形态：相关内核/视图能力存在，具体人工入口需核实。
- 入口：需求→任务序列及校验页。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 建立A→B→C。
2. 用支持的编辑/导入入口加入重复、环或悬空锚。
3. 执行校验。

通过标准：正常顺序保留；异常定位到序列/工位且阻止正式运行；找不到入口须登记入口缺口。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：入口缺席时记BLOCKED并建问题，不能通过测试夹具代替正式宿主操作。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R02-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-01

**Box 区域新增与中心尺寸**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→新增→区域定义与采样。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 建立中心(0.2,0.1,0.3)m、尺寸(0.4,0.2,0.1)m的非对称Box。
2. 分别改变三轴。
3. 试0/负尺寸。

通过标准：轮廓中心与三个边长正确；退化定义被拒绝或阻断正式就绪，不能悄悄钳为有效区域。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-02

**按数量网格采样**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→网格计数(Grid)。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置4×3×2。
2. 观察实时总数。
3. 确认后查看中性预览。

通过标准：计划位置数为24，预览无重复/漏层；此计数不等于24点已可达。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-03

**按间距网格采样**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→网格间距(GridBySpacing)。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 给三轴不同正间距。
2. 记下归一计数。
3. 改为等价计数方案再确认。

通过标准：预览与保存计数一致，端点/间距规则明确；负值/零间距不可形成非法计划。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-04

**随机位置采样与种子**；依据：REQ-03／KIN-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→随机计数(Random)/种子。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 固定样本数和种子执行两次覆盖。
2. 导出坐标比对。
3. 再换种子。

通过标准：相同冻结计划位置及顺序确定；换种子真实参与生成，计划身份可区分。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-05

**方向姿态采样**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→方向采样。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先设1方向再设多个方向。
2. 记录位置数与姿态总数。
3. 运行小预算覆盖。

通过标准：位置数不随方向数变化，位置×姿态总数按计划增加；方向实际参与IK。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-06

**工具滚转姿态采样**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→工具滚转采样。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设非对称滚转区间及多个样本。
2. 运行覆盖。
3. 再输入倒序区间。

通过标准：滚转围绕工具轴，范围/数量被消费；逆序或非有限输入有可定位诊断。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-07

**最低覆盖率**；依据：REQ-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→最低覆盖率。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设85%。
2. 试0%、100%及越界。
3. 确认再重开。

通过标准：85%显示不成为85倍；持久值对应0.85，边界正确，不把最低要求当实际覆盖结果。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-08

**退化区域、零计划与计数溢出**；依据：REQ-03／V-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域定义/校验。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别试零尺寸、零计划数和极大计数。
2. 看预计总数及校验。

通过标准：计数不回绕，不启动无界工作；零计划覆盖率未定义且数据不足，不显示0%或100%通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-09

**中性采样网格预览**；依据：REQ-03／UX-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→区域→三维预览。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在从未评估的区域显示采样网格。
2. 切换区域和项目。

通过标准：未评估区域使用中性预览并明确尚未评估；旧区域轮廓及时清理。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-10

**Good/Weak/Failed 样本着色与图例**；依据：REQ-03／KIN-04／UX-06。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：需求→区域样本表＋运动学→区域覆盖。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行含可用/弱/失败状态的标准样例。
2. 逐行选择样本并看三维。

通过标准：表状态、图例、颜色及真实位置一一对应；数据不足不得绘成成功。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R03-11

**选中样本蓝圈/三维定位**；依据：REQ-03／UX-06。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：需求→区域→样本结果列表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 点选已知编号。
2. 切换两个不同样本。
3. 再清空选择和关闭项目。

通过标准：仅当前样本有选中标记，世界坐标正确；选中与取消不改需求草稿。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R03-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-01

**工况新增、复制与删除**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→新增/复制/删除。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新建必验和可选工况。
2. 各复制一次。
3. 删中间及最后工况。

通过标准：数量、独立身份和选中回退正确；必验列由启用与等级派生。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-02

**工况改名**；依据：REQ-04／UX-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→名称。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 修改中文名称后按Tab。
2. 切页返回。
3. 取消另一次改名。

通过标准：主窗口保留，名称同步到树和表；无词表外键异常，取消保留原值。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-03

**引用环境障碍/场景对象**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→结构编辑→环境对象。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 从已有场景目录选障碍物。
2. 确认。
3. 尝试删除被引用对象再重开。

通过标准：按真实对象引用保存，悬空引用可定位；不复制另一套障碍物或错绑其他项目。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-04

**负载质量 kg**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→负载。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新增质量并填2 kg。
2. 改为另一正值。
3. 试0/负值/未知。

通过标准：已提供非法质量被拒绝；未知与已提供分开表达，合法负载进入后续计算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-05

**负载质心 m 与惯量 kg·m²**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→负载质心/惯量。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 输入非零质心和合法惯量。
2. 试非对称、非正定或三角不等式失败张量。

通过标准：坐标系和kg·m²明确；非法值阻断，质心与惯量基准不能静默脱钩。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-06

**Grasp/Release/Dwell 事件**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→事件表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 新增Grasp、Release、Dwell并绑定工位。
2. 修改驻留时长。
3. 删事件并试负时长。

通过标准：三种事件独立保存、时序对应；非法时长和悬空工位有诊断；取消整组不写入。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-07

**最小关节裕量与无碰撞要求**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→最小裕量/无碰撞。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设不同最小裕量及无碰撞要求。
2. 运行相同目标的两组工况。

通过标准：要求真正影响正式评估且单位正确；缺碰撞设施不能视为无碰撞。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-08

**工况适用范围和工位清单**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→结构编辑→适用范围。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在全部工位、所选工位、不适用间切换。
2. 选择A/C并确认。
3. 再取消另一组修改。

通过标准：作用范围和选中清单一致；其他工位不被误纳入；取消保持原组。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-09

**目标节拍 s**；依据：REQ-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→目标节拍。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 从未设改为12.5 s再改13.5 s。
2. 切换对象重开。

通过标准：表格、草稿和重开值一致；未设不伪装为0秒硬约束。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R04-10

**必验集合唯一派生规则**；依据：REQ-04／EVI-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工况→等级/启用及必验列。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别构造Must/Should×启用/禁用四组。
2. 执行校验。
3. 禁用所有Must。

通过标准：仅启用Must入必验集合；空集合有明确警告，不能凭可选工况伪造覆盖完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R04-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-01

**CSV 真实文件读取**；依据：REQ-05／AT-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导入→CSV。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选fixtures/requirements-good.csv。
2. 预览表头/行数。
3. 再打开mixed文件。

通过标准：文件真实读取，中文和引号保留；好坏行可区分，不把选路径当导入成功。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-02

**字段字典、别名自动识别**；依据：REQ-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→映射表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用非标准列名。
2. 先看自动映射。
3. 逐列改为正确字段并查看忽略项。

通过标准：自动识别不覆盖用户显式映射，未知列清楚标为忽略。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-03

**手动字段映射**；依据：REQ-05／L-R10。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→字段映射。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 把pos_a/pos_b/pos_c手动映射到x/y/z。
2. 核对预览。
3. 确认。

通过标准：保存值来自映射列而非固定列序；重复映射或必需字段缺失有明确诊断。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-04

**单位声明与 mm/deg→SI 预览**；依据：REQ-05／NFR-COR-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→单位列/预览。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择mm和deg。
2. 使用fixtures/requirements-units.csv。
3. 核对100 mm、90 deg换算。

通过标准：SI分别为0.1 m、π/2 rad；预览与导入值一致，不重复换算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-05

**错误行定位到行/列/原文**；依据：REQ-05／AT-02／UX-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→逐行错误。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入mixed文件。
2. 选择x=bad的错误项。
3. 查看详细信息。

通过标准：显示行号、字段/列、原文bad和修正建议，能区分表头行与数据行。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-06

**正确行保留并跳过坏行**；依据：REQ-05／AT-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→部分成功确认。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入mixed。
2. 确认保留合法行。
3. 导出副本核对。

通过标准：仅合法行进入草稿，错误行未悄悄变0；总行数/成功/跳过计数一致。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-07

**缺列、重复标识/名称、非法单位**；依据：REQ-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 依次去掉必需列、重复名称、填非法单位。
2. 每次取消后重开合法文件。

通过标准：结构错误与行级错误分开，均不遗留半批数据；已存在草稿不被覆盖。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-08

**公式只按文本数据解析**；依据：REQ-05／NFR-SEC-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入/导出。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入名称“=1+1”。
2. 查看名称和导出原文。

通过标准：保留文本，不计算成2，不执行公式或外部命令。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-09

**CSV 可逆转义与方言标识**；依据：REQ-05／NFR-SEC-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导出副本CSV，再导入。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用公式前缀、引号、逗号、换行名称的安全样例。
2. 导出并回读。

通过标准：安全方言标识/可逆转义明确；回读逐字符一致，原有单引号不丢失。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-10

**取消或失败不写草稿**；依据：REQ-05／V-20。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→CSV导入→预览取消。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录原点数与草稿脏状态。
2. 预览混合文件后取消。
3. 再确认合法批次。

通过标准：取消或失败不改草稿/修订；确认后只增加成功行，且可一次撤销。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R05-11

**导入溯源摘要与原始行号**；依据：REQ-05／I-REQ-8。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导入来源信息/副本。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 导入相同内容两次并导出来源。
2. 仅改一行后重导。

通过标准：源内容摘要稳定且随内容变化，原始行号可追溯；不以本机绝对路径替代来源摘要。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R05-11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-01

**任意草稿保存与恢复**；依据：REQ-06／V-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：文件→保存草稿；关闭/打开项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 创建未完成点与区域并保存。
2. 关闭程序后重开。
3. 逐字段检查。

通过标准：未完成草稿能恢复、保持未应用状态；不得自动晋升为正式修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-02

**应用生成新修订**；依据：REQ-06／L-R3。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→应用草稿/全局应用修改。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录修订。
2. 应用两次不同修改。
3. 关闭重开。

通过标准：每次成功应用产生新修订，树显示正确已应用内容；历史修订不被原地改写。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-03

**启用非法 Must 阻断正式运行**；依据：REQ-06／V-12。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→校验/应用；运动学正式运行。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 让启用Must带悬空引用或非法必填。
2. 尝试应用/正式运行。

通过标准：显示输入未完成及对象原因；不产生正式工程可行结论或半提交。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-04

**有效 Must 不满足与 Should 警告**；依据：REQ-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求定义＋运动学任务点结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用合法但不满足的Must和Should样例分别评估。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：合法输入的不满足与输入非法分开；Must影响工程判定，Should给警告而非冒充硬失败。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-05

**部分预览只检查有效条目**；依据：REQ-06／V-20。

- 实现形态：相关内核/视图能力存在，具体人工入口需核实。
- 入口：需求→部分预览（若未暴露入口记录缺口）。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 混合有效和无效草稿。
2. 尝试预览并核对消费条目及修订。

通过标准：只预览有效条目，明确Preview；不写正式证据，不改变HEAD。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：入口缺席时记BLOCKED并建问题，不能通过测试夹具代替正式宿主操作。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-06

**过期修订冲突及草稿保留**；依据：REQ-06／V-14。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求草稿→过期基线处理。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 保留旧草稿。
2. 通过另一已授权修订变更使其过期。
3. 尝试应用并取消处理。

通过标准：拒绝过期写入并保留原草稿；给重新加载/重演/保留对照等实际可用路径。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-07

**未应用编辑不使正式结果失效**；依据：REQ-06／V-20。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求草稿＋已有评估结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 完成一次评估。
2. 仅改草稿未应用。
3. 再应用并查看结果状态。

通过标准：草稿编辑不改变正式结果身份；成功应用后实际依赖结果才标需要重算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-08

**分层就绪校验（当前界面R1～R8；保留旧R0～R9检查来源）**；依据：REQ-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→校验页。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别制作缺引用、非法姿态、缺必验集、零计划、序列错误样例。
2. 执行校验。

通过标准：各层诊断和严重级别可区分；界面当前筛选为R1～R8，旧表R0～R9不得误当已有控件。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R06-09

**校验页筛选、对象跳转和三要素**；依据：REQ-06／UX-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→校验→级别/层/稳定码筛选。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行校验。
2. 组合筛选。
3. 点一条带对象的诊断。
4. 清空筛选。

通过标准：结果过滤正确并定位对应对象；原因/建议/比较值单位可读；清空恢复完整列表。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R06-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-01

**拆垛/拣选模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→拆垛/拣选。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选模板并设小数量。
2. 先取消。
3. 再确认生成。

通过标准：点数与预览一致，Pick等工艺角色正确，来源记录可见；取消零新增。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-02

**机床上下料模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→机床上下料。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置前缀和数量。
2. 确认。
3. 查看生成顺序及角色。

通过标准：装卸角色及默认段参数符合模板定义，不能都退化为同一通用工位。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-03

**码垛模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→码垛。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置行列小阵列。
2. 核对预览。
3. 确认。

通过标准：数量、行列位置和Place标签一致，命名无静默覆盖。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-04

**检测模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→检测。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选检测，X数量3。
2. 确认。
3. 查看工艺类别与角色。

通过标准：生成3项并标Inspection/Inspect，不误用搬运标签。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-05

**换工具模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→换工具。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 生成小批次。
2. 检查工位角色和序列。

通过标准：按模板生成换工具任务，参数、顺序及来源类型一致。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-06

**交接模板**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板→交接。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置前缀/数量。
2. 确认并检查每个角色。

通过标准：交接角色和来源类型正确；可追溯到同一生成批次。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-07

**模板参数、数量实时预览**；依据：REQ-07／L-R9。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工艺模板参数窗。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 连续改变X/Y数量、间距和前缀。
2. 看实时预览。
3. 确认。

通过标准：数量/参数在预览和实际生成一致；非法数量不能启动巨大或空异常批次。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-08

**来源徽标和批次溯源**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→生成工位→来源信息。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 生成同一模板两批。
2. 比较来源徽标、参数和各点身份。

通过标准：批次可区分，生成参数保留；不能用同一ObjectId复制多点。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-09

**按参数重生成及手改冲突**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→按模板重生成。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 生成后改一个点。
2. 修改生成参数并重生成。
3. 取消冲突处理。

通过标准：报告手改冲突且不覆盖；无冲突整批原子更新，引用不悬空。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R07-10

**解除关联保留条目**；依据：REQ-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→生成批次→解除关联。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 生成并引用一个点。
2. 解除关联。
3. 尝试重生成。
4. 撤销/重做。

通过标准：原点、身份及引用保留；解除关联一次撤销；解链后不继续作为关联批次更新。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R07-10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R08-01

**三维场景创建目标**；依据：REQ-08。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→三维创建目标…。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启动临时编辑。
2. 在三维指定位置并调整。
3. 先取消再确认。

通过标准：取消零写入，确认仅新增一个草稿点；临时图形清理，真实世界位置一致。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R08-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R08-02

**三维拖动与检查目标**；依据：REQ-08。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→三维移动选中…。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选已知点。
2. 改变位置和姿态。
3. 取消后重试确认。

通过标准：预览与最终字段一致，取消恢复原点；确认一次撤销，无多份残留文字/轮廓。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R08-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R08-03

**拾取/捕获写回前确认**；依据：REQ-08／L-R6/L-R7。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→捕获TCP/拾取/三维编辑确认。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别停在未决、取消及确认。
2. 比较草稿点数/字段和修订。

通过标准：未决与取消不写回；确认后只发生约定的一次草稿修改，非自动项目应用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R08-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-01

**固定姿态 Fixed**；依据：REQ-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→姿态规则→固定姿态。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 输入非零RPY。
2. 切单位再确认。
3. 运行IK并看TCP方向。

通过标准：固定旋转被实际消费；非法角不提交，轴/角度约定清楚。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-02

**对齐参考系 AlignFrame**；依据：REQ-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→姿态规则→对齐参考系。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择旋转Frame。
2. 确认并求解。
3. 再使引用失效。

通过标准：TCP朝向与参考系对应；缺引用阻断且定位，不能继续用旧旋转。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-03

**对齐几何法向 AlignGeometryNormal**；依据：REQ-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→姿态规则→对齐几何法向。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选支持的真实平面特征。
2. 切换取反。
3. 确认并求解。

通过标准：法向与所选面/方向一致，取反生效；没有真实特征能力时记录阻断而非手填假特征PASS。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：真实曲面/网格特征法向全链未齐；MA-01/MA-08。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-04

**指向目标 PointAtTarget**；依据：REQ-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→姿态规则→指向目标。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 填与工位不同的目标XYZ。
2. 求解看工具轴。
3. 再令二者重合。

通过标准：工具轴朝向目标；零方向不能伪造单位轴，应给诊断并保留输入。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-05

**工具滚转自由 ToolRollFree**；依据：REQ-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→工位→姿态规则→工具滚转自由。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置非对称区间。
2. 用需要滚转自由的目标求解。
3. 再设倒序区间。

通过标准：滚转自由真实影响解，轴语义正确；倒序拒绝，不误当固定完整姿态。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R09-06

**规则解析来源、确定性和失败定位**；依据：REQ-09／AT-23。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→五种姿态规则/校验。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 每种规则各用一个有效/无效样例。
2. 应用并导出来源。
3. 到运动学求解。

通过标准：解析来源/对象/修订确定且失败可定位；不能只以规则枚举保存成功认定消费通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R09-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R10-01

**从Frame拾取参考对象**；依据：REQ-10／AT-23。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→拾取参考对象＋三维视图。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 启动拾取。
2. 点已知模型/场景Frame。
3. 确认后查看引用。

通过标准：选中真实对象且反解正确，域外对象不误写；取消不改草稿。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R10-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R10-02

**从几何特征拾取法向**；依据：REQ-10／AT-23。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→拾取几何特征＋三维Ctrl双击。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 对有法向真值的平面操作。
2. 核对选中面/法向。
3. 取消再确认。

通过标准：命中的是该可见几何特征，世界/局部法向一致；非支持资源诚实拒绝。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：真实曲面/网格特征法向全链未齐；MA-01/MA-08。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R10-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R10-03

**从当前场景捕获TCP**；依据：REQ-10／AT-23。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→捕获TCP。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先在会话中给机器人一个已知非零q。
2. 选择具名TCP。
3. 捕获并确认。

通过标准：新点与该TCP真实世界FK一致，保留捕获来源；不能总捕获默认或法兰。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R10-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R10-04

**过期会话诊断与来源标签**；依据：REQ-10。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→捕获TCP/来源。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 捕获预览后改变项目或修订。
2. 尝试确认。
3. 重新捕获。

通过标准：过期预览拒绝写回且要求重捕，来源不指向旧会话。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R10-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-01

**过参考系镜像面镜像**；依据：REQ-11／AT-24。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→镜像工位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选非World参考面及非轴对称点姿态。
2. 预览、取消、确认。
3. 用独立反射计算对照。

通过标准：位置/姿态符合参考面反射定义，源点不变，生成点身份独立。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-02

**不可镜像规则与派生无环**；依据：REQ-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→镜像/派生诊断。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 镜像不支持直接反射的规则。
2. 再次派生并尝试移除源。

通过标准：不能伪造已解析姿态；Pending/来源及悬空诊断明确，无派生环。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-03

**线性阵列完整参数**；依据：REQ-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→创建阵列→线性。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设3点、方向(1,0,0)、间距0.1 m。
2. 取消再确认。

通过标准：生成位置相差0.1 m，预览/数量一致，一次撤销整批。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-04

**矩形阵列行列层、各轴间距**；依据：REQ-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→创建阵列→矩形。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设列3、行2、层2和三向不同间距。
2. 确认并看三视图。

通过标准：共12点且各向间距、层方向独立生效；不是二维重复叠点。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-05

**圆形阵列半径、角度和轴**；依据：REQ-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→创建阵列→圆形。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设半径0.2 m、非整圆角步长和非Z法向。
2. 确认。

通过标准：点落在指定圆平面且角步长/半径正确；不能固定在XY平面。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-06

**三维折线阵列控制点**；依据：REQ-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→创建阵列→三维折线。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 输入至少3个XYZ非共线顶点且Z非恒定。
2. 设数量。
3. 确认。

通过标准：按弧长规则沿三维折线布点，拐点正确；不丢Z或退化成端点直线。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-07

**局部撤销/重做与批次整体回滚**；依据：REQ-11／L-R4。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→草稿撤销/重做。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 创建多点阵列。
2. 只撤销一次。
3. 重做一次。
4. 再解链并撤销。

通过标准：每个批次整体回退/恢复，无半批状态和历史指针错位。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R11-08

**项目撤销与需求草稿撤销分离**；依据：REQ-11／PM-18。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求草稿撤销＋编辑→项目撤销。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先草稿编辑并局部撤销。
2. 应用后再项目撤销/重做。

通过标准：局部操作不增修订，项目撤销以新修订表达；两级入口作用域清楚。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R11-08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R12-01

**需求JSON导入**；依据：REQ-12／AT-24。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导入→JSON。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 先导出小需求副本。
2. 在新验收项目导入。
3. 预览取消再确认。

通过标准：支持字段逐项一致，引用映射明确；不把跨项目悬空引用静默指向同名对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R12-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R12-02

**JSON同字段字典、安全与版本错误**；依据：REQ-12。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→JSON导入/导出。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 对副本改版本、单位、非法数字和未知字段。
2. 逐个导入。

通过标准：遵守同一字段字典与安全语义，不扩称副本包含所有未定义对象；错误可定位无半导入。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R12-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R12-03

**导出JSON草稿副本**；依据：REQ-12／AT-24。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导出副本→JSON。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 选择独立新路径。
2. 导出后用文本编辑器打开。

通过标准：草稿标志和被支持对象字段正确，非ASCII/公式文本保留，不触发项目应用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R12-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-R12-04

**副本不修改项目、原子失败保旧文件**；依据：REQ-12。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→导出副本。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 记录项目修订和已有输出内容。
2. 取消一次。
3. 向不可写路径导出。

通过标准：取消无输出，失败保留旧文件；项目修订和草稿不受影响，错误路径/建议可见。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-R12-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U01

**空态引导与缺输入呈现**；依据：UX-01／UX-10。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：启动首页→新建/打开项目→需求。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 无项目启动。
2. 新建空项目。
3. 进入空需求页。

通过标准：目标、缺项和下一步可见，新增可达；无项目写命令禁用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U02

**工程用语与隐藏内部实现词**；依据：UX-02。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：菜单/命令面板/状态栏。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐个打开三个重点模块菜单和对话框。
2. 触发一次用户输入错误。

通过标准：主文案用工程语言，内部ID仅可作为辅助诊断；错误不关闭主窗口。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U03

**失败提示对象、原因、建议及比较值单位**；依据：UX-03。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求表单/导入错误/校验详情。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 提交非法输入和缺引用。
2. 展开完整提示。
3. 点对象定位。

通过标准：对象、原因、建议齐全；比较型错误有实际/要求/单位，文字不被裁切。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U04

**高级采样/种子与开发诊断折叠**；依据：UX-04。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求各页折叠卡片/高级区。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐一展开再折叠。
2. 切页返回。
3. 观察草稿脏状态。

通过标准：常用路径清楚，高级项能到达；纯折叠/显示操作不改业务数据。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U05

**批量粘贴及筛选需求参数**；依据：UX-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→参数表：筛选 / 批量粘贴。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 复制当前筛选表格TSV。
2. 在文本编辑器只改一项后粘贴。
3. 再粘非法行。

通过标准：字段键与对象对应，合法批次可整体撤销；非法值有行定位且无未说明的半更新。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U05`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U06

**单位显示与显示单位切换**；依据：UX-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：分析显示单位/表格单位。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. m→mm、rad→deg来回切换。
2. 观察同一点及导出。

通过标准：物理真值与修订不变，显示/输入/导出单位一致，无重复换算。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U06`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U07

**四页树表联动及面包屑**；依据：UX-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：共享树＋需求四页。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 树选工位、区域、工况及计划。
2. 点表行。
3. 到三维选择再清空。

通过标准：树、页签、面包屑、检查器和高亮是同一对象；跨域选择不误改当前对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U07`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U08

**列表状态词、三维颜色与图例一致**；依据：UX-06。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求样本表/运动学结果/三维图例。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用有不同结果状态的样例。
2. 逐一选择并对照图例。

通过标准：成功、弱、失败、数据不足各有明确文字语义；颜色一致且非唯一判断渠道。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U08`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U09

**表单应用确认、取消及会话显示不弹保存**；依据：UX-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：需求→应用草稿/确认区。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 修改表单。
2. 取消应用再确认应用。
3. 之后只改变相机。

通过标准：取消不提交，确认一次产生约定修订；相机变化不提示业务保存、不生成修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U09`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U10

**工程策略摘要、入口与显示开关分离**；依据：UX-08。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：工程策略摘要→修改工程策略。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 查看策略摘要。
2. 编辑碰撞要求后取消再确认。
3. 切换显示几何。

通过标准：策略确认走新修订且可撤销；三维显隐不改碰撞策略。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U10`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U11

**停靠、隐藏、恢复和跨会话布局**；依据：UX-09。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：视图→各IRD面板/恢复布局。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 隐藏并恢复面板。
2. 拖为浮动再停靠。
3. 恢复默认布局。
4. 正常退出重启。

通过标准：各面板可找回，恢复后可操作，布局持久/复位符合菜单说明。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U11`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U12

**窄Dock与常用分辨率可读可操作**；依据：UX-09／UX-05。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：窗口/Dock尺寸和系统DPI。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 在常用分辨率、窄Dock及125%/150%DPI逐页展开长表单。
2. 滚到最底操作确认/取消。

通过标准：无不可达字段或截断按钮，必要滚动条有效；机制存在不等于所有页面均已验齐。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U12`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U13

**七种公共状态及取消/过期原因**；依据：UX-10。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：状态栏/就绪条/任务区。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别观察空、未完成、可计算、计算中、过期、数据不足及失败。
2. 逐项记录实际输出、窗口状态和相关数值，按本项通过标准对照；把不支持/未装配与执行错误分开登记。

通过标准：状态可分辨、原因和下一步明确；取消/未执行不伪装为成功完成。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U13`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U14

**顶/前/右及轴测标准视图**；依据：UX-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维工具栏→顶/前/右/轴测。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用非对称模型依次点击四个视图。
2. 每次留图。

通过标准：投影方向正确，模型/目标仍在；相机变化不移动世界对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U14`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U15

**缩放、自动缩放和Home**；依据：UX-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维工具栏→缩放/自动缩放/Home。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 缩小放大并把模型移出中心。
2. 点适配和Home。

通过标准：能恢复可用视野；相机Home与机器人Home名称虽近但作用不同，均不改设计修订。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U15`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U16

**相机旋转与透视/正交**；依据：UX-11。

- 实现形态：相关内核/视图能力存在，具体人工入口需核实。
- 入口：三维相机导航/投影模式入口。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 绕非对称对象旋转、平移。
2. 切换已有投影入口。

通过标准：坐标和模型不变，投影变化正确；没有透视/正交切换入口时记录缺口，不虚构菜单。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：入口缺席时记BLOCKED并建问题，不能通过测试夹具代替正式宿主操作。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U16`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U17

**线框/透明及渲染分组显隐**；依据：UX-11。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：三维工具栏→线框/透明/显示分组。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 逐一切Physical/Virtual/Drawable/Collision/User。
2. 试线框和透明。

通过标准：只改变对应显示层，恢复后图形完整；碰撞计算不随显隐被关闭。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U17`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U18

**碰撞高亮与组掩码**；依据：UX-11。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：碰撞结果→对象高亮/显示分组。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 运行真实碰撞正反例。
2. 点报告对象对。
3. 遮挡时尝试定位。

通过标准：仅正确对象高亮，组掩码与图例一致；无碰撞或切项目后旧高亮清除。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U18`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U19

**PNG视图截图与真实ray-cast**；依据：UX-11。

- 实现形态：相关内核/视图能力存在，具体人工入口需核实。
- 入口：产品PNG截图入口/三维拾取。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用产品提供的截图动作（若无入口记缺口）。
2. 在已知Frame/网格点选。

通过标准：PNG可打开且与视图一致；ray-cast反解到真实对象；外部截屏不能代替产品截图导出验收。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：入口缺席时记BLOCKED并建问题，不能通过测试夹具代替正式宿主操作。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U19`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U20

**需求阶段门控与下游失效提示**；依据：UX-12。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：阶段入口/下游结果列表。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用未完成输入尝试正式计算。
2. 补全并应用。
3. 运行后再改依赖输入。

通过标准：门控源一致；缺项提示指向可处理对象，变更后旧结果提示重算而非仍为当前。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U20`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U21

**需求命令搜索、快捷键与键盘导航**；依据：UX-13。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：Ctrl+Shift+P→中文命令搜索/快捷键设置。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 搜索“需求”“阵列”。
2. 上下键选择回车执行。
3. 测试Esc和重复快捷键。

通过标准：中文可搜索，执行当前可用命令；重复绑定明确拒绝，禁用命令不能绕过门控。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U21`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U22

**帮助、版本和插件清单**；依据：UX-14。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：帮助/关于。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 打开用户手册及关于窗口。
2. 记录版本、插件及可执行路径。

通过标准：文档可访问，缺失时有反馈；构建信息明确可核对，不以分支名代替程序来源。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U22`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U23

**只读项目禁编辑但可浏览/导出**；依据：L-R12／PM-07。

- 实现形态：界面代码/域流程已存在，待人工验收。
- 入口：只读打开项目→需求。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 只读会话浏览选择/导出。
2. 尝试新增、修改、应用和草稿写入。

通过标准：写路径禁用并说明原因，合法浏览/副本导出可用；刷新不会重新启用写按钮。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U23`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-U24

**项目关闭/切换清理三维预览**；依据：UX-06／V-20。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：关闭项目/切换项目。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 显示需求区域及选中样本。
2. 关闭项目后打开另一个。
3. 后台运行时重复。

通过标准：旧轮廓/高亮/检查器被清理，迟到结果不进入新项目。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-U24`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-J-01

**对象身份、切片与修订隔离**；依据：V-15/16/18／CON-05/06。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：需求应用→项目历史/结果身份。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 分别只改点、工况、计划。
2. 每次应用并查看结果绑定。

通过标准：对象身份/内容版本按依赖更新，旧修订不变；跨项目同名对象不串引用。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-J-01`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-J-02

**下游覆盖分母与必验工况全覆盖**；依据：V-05/19／EVI-02／KIN-04。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：需求必验工况＋运动学区域结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 设置多个启用Must。
2. 运行同一冻结计划。
3. 逐工况核对计划数与有效数。

通过标准：分母为计划总数而非成功数；缺一工况不得宣布完整正式通过。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-J-02`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-J-03

**段/负载/事件进入轨迹动力学**；依据：REQ-02/04／AT-06。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：需求任务序列→轨迹→动力学。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 使用含接近/撤离、夹放/驻留、负载的项目。
2. 规划再评估。
3. 回放曲线。

通过标准：顺序/距离/驻留/负载真实消费，节拍和力矩随相应变更变化；未装配链单列阻断。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-J-03`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。

## REQ-J-04

**工具回填后需求引用不改、下游重算**；依据：V-21／SEL-10。

- 实现形态：界面/内核存在，跨模块或三维完整链待验。
- 入口：选型回填→需求引用/下游结果。
- 准备：使用独立验收副本；通用数据和工程师标准样例见[准备说明](06-DATA-AND-VISUAL.md)。记录当前项目、方案、修订及显示单位。

操作：

1. 用可回填候选确认工具/驱动变更。
2. 核对需求TCP引用。
3. 撤销并重算。

通过标准：需求原引用保持，变更的物性/模型引起实际依赖失效；不能复制替换需求对象。

留证：操作前后截图；实际输入与输出；项目修订/会话状态；涉及三维时附同相机多视图与对象对应关系；涉及文件时附输出副本。

当前限制/复验重点：对应MA-07/MA-08联合终验未齐；本清单不关闭原问题。

源码核对：[RequirementsPanelWidget.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsPanelWidget.cpp)；[RequirementsCommandFlows.cpp](../../../../../RobWorkStudio/src/rwslibs/industrialrobot/requirements/plugin/RequirementsCommandFlows.cpp)。

填写结果：在[验收记录](09-RECORDS.md)找到`REQ-J-04`，分别填写数据/交互/真实三维，不能用单个总PASS掩盖未验维度。
