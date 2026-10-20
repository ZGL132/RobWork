# 建模模块全功能验收报告

**结论：FAIL，不能验收为全功能交付。**

冻结基线：`601ffa0ed8b4f7887576dfb9a560acffea86b757`，分支 `wp14-t12`，执行日期2026-10-19。本轮按需求文件及建模单元交互约定展开47项检查。它是全功能审计记录，未替代全新上下文的正式任务合入验收，也未改动流水线状态。

## 范围与判定

清单及每项方法见 [MATRIX.md](MATRIX.md)，实际结果见 [MATRIX-RESULTS.md](MATRIX-RESULTS.md)。覆盖MDL-01～22、§2.1支持矩阵、相关AT以及窗口操作、文件/修订、三维呈现。每行分别记录计算/数据、交互、视觉；某行内核通过不代表整项通过。

|47项检查结果|数量|
|---|---:|
|PASS：列明范围通过|10|
|FAIL：有确定缺口|17|
|SKIP：门禁或环境跳过|2|
|NOT RUN：尚无完整交互/视觉实证|18|

这不是需求覆盖百分比：一行可能包含多个必需分支。视觉列的SKIP只表示非空间检查不适用；涉及空间而未完成画面对照的项均记NOT RUN。七轴generic-7r模板受P-03工程数值未冻结门禁限制，按登记禁用；不把该门禁当作本轮新增缺陷，也不把守卫测试通过当作七轴模板已交付。

## 实际执行与证据

Windows下使用VS2022 x64开发环境、Qt 6.11.1、`QT_QPA_PLATFORM=windows`，每次只运行一个GUI可执行文件且使用绝对路径。集成相关13目标构建成功；另行完成独立模式配置、建模测试目标构建和执行。重跑入口见 [run.ps1](run.ps1)。

|执行组|实际用例|通过|失败|XML跳过|
|---|---:|---:|---:|---:|
|集成模式：modeling/contract/gui、ui/gui、runtime、dynamics、drivetrain、kinematics、trajectory、selection、reporting|2286|2280|0|6|
|独立模式：modeling/contract/gui|364|363|0|1|
|补充建模验收探针（最终一轮）|12|0|12|0|

逐可执行文件及跳过名称见 [test-inventory.json](test-inventory.json)。运动学报告把1项记为envUnavailable，XML以skipped记录；不能将其计为通过。runtime父进程用例是worker占位跳过；dynamics/selection中各有Widget占位跳过；modeling_gui与ui_gui的剪贴板相关项因外部占用跳过，独立GUI同类1项跳过。

正式宿主tour退出0，日志中240条检查通过，0条FAIL，见 [tour-r2.log](tour-r2.log) 与 [tour-r2/](tour-r2/)。tour后半含需求模块操作，**不能把240条全算为建模全功能通过**。上述跨域套件使用各自fixture，不能据此宣称同一混合链模型贯穿建模→运动学→轨迹→动力学→碰撞→选型→报告的AT-36已经验收。

补充探针直接使用真实ModelingPanelWidget、模板、生产命令流和生产Xacro桥。只在对话框呈现边界捕获文本，未替换领域算法。入口类探针检查控件键/按钮/命令目录，并辅以源代码核对；它们不是逐项完整鼠标端到端测试。源码 [probe.cpp](probe.cpp)、构建注入 [probe.cmake](probe.cmake)、最终失败 [XML](acceptance_probe_complete.xml)、[日志](acceptance_probe_complete.log)、[JSON](acceptance_probe_complete-report.json) 与 [控件截图/字段清单](probe-evidence/) 一并保留。早期6/9/11项调试轮次与最终12项重复，不累计。

## 问题清单

下列17条对应矩阵17项FAIL；“实测”指补充探针的实际运行结果，“代码审查”不冒称已经复现真实窗口完整路径。F08/F12由同一探针检查，F13由两个探针检查，不能将问题条数等同于失败测试数。

|编号|矩阵/需求|证据级别|问题与影响|源码定位|重新验收条件|
|---|---|---|---|---|---|
|F01|M05 / MDL-01|实测＋schema核对|关节缺最大速度、最大加速度编辑和权威字段；模板参考值不进入模型。|[JointEntry](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/RobotDesign.hpp:421)|逐轴输入v/a，确认、取消、保存重开并核对实际消费者。|
|F02|M08 / MDL-03、11|代码审查|导入报告把默认/忽略/不支持/待确认等压成数量，用户无法逐项检查字段与对象。|[导入报告](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp:442)|导入含缺省、忽略、非法字段的样本，报告逐项列对象、来源、处理和原因。|
|F03|M09 / MDL-12|代码审查|Xacro多可动分支直接拒绝，没有URDF路径已有的显式选链交互。|[Xacro分支](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp:803)|展开后列出分支，选择主链再映射；取消不得写入。|
|F04|M10 / MDL-12|实测|continuous工程工作范围没有编辑与确认入口，已存在值只能展示。|[编辑字段](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp:1643)|用户确认有限工程区间后解除阻断，保留continuous类型与跨周语义。|
|F05|M13 / MDL-19|生产桥实测＋代码审查|真实磁盘include文件存在仍在ValidatedXacroSource契约检查失败；桥只填入口字节与快照，缺依赖树等输入，替换表也固定为空。|[Xacro桥](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingXacroBridge.cpp:39)|生产文件入口完成含include/参数展开；错误定位明确，取消和失败零写入。|
|F06|M14 / MDL-18|实测＋命令目录核对|WorkCell反向提取内核可用，但正式命令目录没有外部WorkCell导入入口。|[命令目录](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp:39)|从正式窗口选择文件/设备、逐项确认有损信息并生成可编辑草稿。|
|F07|M17 / MDL-04、ARC-05|代码审查|未找到自碰撞启停/排除对的完整可写配置入口；现有碰撞查询通过不能证明配置功能已交付。|[SelfCollisionSetup](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/RobotDesign.hpp:469)|配置排除对与安全距离，真实重算并在覆盖层观察一致结果。|
|F08|M19 / MDL-13|实测＋代码审查|空模板没有新建/删除工具完整入口，已有工具位姿/TCP可编辑不等于完整工具管理。|[命令目录](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp:39)|空模板新增工具，编辑几何/物性/安装/TCP，验证删除保护、持久化与三维附着。|
|F09|M21 / MDL-05|代码审查|生产估算命令固定采用默认半径单段实心圆柱，不能输入空心圆柱、矩形、多段及其尺寸。|[固定段元](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp:919)|从窗口分别选择三类几何与多段参数，对照独立解析值。|
|F10|M22 / MDL-05|实测＋代码审查|连杆质量/质心/惯量只读，缺少手工覆盖和质心迁移两种处理的真实确认入口。|[只读门控](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp:1281)|修改质量/质心/完整张量，验证平行轴或完整输入两路径、取消零写入和错误纠正。|
|F11|M24 / MDL-06、ARC-05|代码审查|正式宿主固定装配系统缺省行程策略，项目可写阈值路径未贯通。|[宿主策略装配](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:5201)|设置机型阈值并持久化，超限确认展示实际值、阈值、rad及凭据。|
|F12|M27 / MDL-15|实测＋代码审查|空模板没有新增/删除障碍物及选择几何来源的完整入口；现有场景位姿编辑不能覆盖CRUD。|[命令目录](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/PanelCommandCatalog.cpp:39)|窗口新增、改来源、改世界位姿、删除；核对环境碰撞与重开。|
|F13|M29 / MDL-17|两项实测＋代码审查|命名位姿页可新增/删除/保存，却不能恢复；Home/Zero命令仅showInfo且检查home/zero，与权威homeConfiguration/zeroConfiguration键不符。|[Home/Zero](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp:1125)|使用权威保留键及任意命名姿态恢复真实会话q和三维形态，模型修订不变。|
|F14|M31 / MDL-21|实测＋宿主装配审查|无耦合矩阵编辑入口；正式宿主HandlerServices未提供尾部couplingStage，落到默认R1Locked。后者尚未用宿主耦合文件应用复现。|[能力默认值](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/include/sdurws/ird/modeling/CommandHandlers.hpp:631)|真实宿主启用已批准R2，编辑非对角矩阵，核对位置/力矩/限位并拒绝病态矩阵。|
|F15|M32 / MDL-22|实测|任意安装姿态页只有EAA旋转矢量，没有需求指定的欧拉角输入。|[基座字段](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp:1911)|明确欧拉顺序/单位并输入任意姿态，持久化；三预设及自定义均核对画面、重力和碰撞。|
|F16|M33 / MDL-08、UX-13|真实命令流实测|Model Diff只显示结构/参数/物性差异条数，无对象、前后值和点击定位。实测j1零位0→0.25仅显示参数组1条。|[差异呈现](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingCommandFlows.cpp:1016)|真实差异表逐对象显示前后值，点击定位树/属性/三维，并保留候选与基线语义。|
|F17|H06 / 单元卡L-5|实测＋代码审查|草稿没有本地撤销/重做栈和入口，不能用项目级逆命令替代。|[无草稿撤销栈](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingPanelWidget.cpp:949)|多字段、结构及部件编辑均能本地撤销/重做，草稿操作不产生项目修订。|

## 真实三维与窗口验收边界

已在真实正式宿主中取得重开后的圆柱代理链、场景夹具、真实中心像素raycast/选择及重开变换证据。已审阅截图包括 `mtour-5.5-reopened.png`、`tour-r2/mtour-8-scene-update-reopened.png`，以及补充面板 `probe-evidence/joint.png`。这些证据证明局部窗口和呈现链可运行，不能证明完整真实机型外观正确。

仍需完成：

- 非退化六轴、自定义七轴、混合链的真实网格机型；STL/OBJ/DAE/WRL/IV逐格式形状、比例、位姿与工具附着。
- 地面/墙装/倒挂及任意角的机器人全景，与独立FK/安装变换及环境碰撞共同对照。
- 默认和多个命名TCP坐标架、工具与障碍物编辑后的及时显示。
- 树↔属性↔机器人连杆拾取；遮挡、深度、多选高亮及碰撞覆盖层的实际像素效果。
- 鼠标旋转/平移/缩放/适配/复位，线框/透明等显示模式及重开后的稳定性。
- 正常点击应用后的即时刷新、真实编译失败回滚；项目撤销/重做与参数、树、场景一致性。
- 五态DH结果模态页、导入错误窗口、基线冲突三选择、未应用完整草稿关闭重开的用户路径。

当前工具未提供computer-use要求的node_repl/@oai/sky调用通道。本轮使用项目既有Qt GUI测试、正式宿主tour与临时Qt探针；没有用自造Win32/UIA通道补作鼠标操作。缺少上述视觉实证的项保持NOT RUN。

**应用后空白截图的归因需要谨慎。** tour的step5走内部runDomainApply，绕过正常draft.apply命令的呈现刷新；正常命令路径在 [UiPlugin.cpp](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp:3555) 调用refreshPresentationAfterCommit。因此tour中应用后空白只能证明该测试通道存在缺口，不能据此判定正常用户点击应用也空白，更不能以重开显示正常证明即时刷新通过。

**项目撤销/重做仅登记待复现风险。** [ModelingUiModule.cpp](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/modeling/plugin/ModelingUiModule.cpp:630) 的修订事件处理前移baseRevision并清空基线/预览，未在该处重读实际内容；本轮未完成二次应用→撤销→重做→参数/三维全状态对照，不能下“已证实数据丢失”的结论。

## 后续处置与交付状态

先修复生产Xacro桥、Home/Zero与上述不可达功能，再以真实用户入口重跑对应红例；补齐其余18项完整路径和全部三维画面对照，清除可消除的环境跳过。只有每条必需分支都有明确证据后，才能重新判定全功能验收通过。

本轮没有修改产品代码，没有使用子代理，没有执行合入、提交或推送。新增内容为本次审计报告、临时探针源码和执行证据。未运行全仓ird_gates/完整gate-all，不宣称达到正式合入DoD；已运行范围及失败如上如实记录。

