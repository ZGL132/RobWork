# 建模模块逐功能验收结果

基线：601ffa0ed8b4f7887576dfb9a560acffea86b757。47条功能/交互验收项；需求与方法见 MATRIX.md。按实际验证分别记录计算/数据、交互、视觉，不能以测试套件全绿代替整条用户路径通过。SKIP的视觉项仅表示该条为非空间功能；涉及空间的功能缺少画面对照一律NOT RUN。

总体：**FAIL，禁止宣称建模模块全功能验收通过**。表内PASS只对该行列明的功能与实测范围有效。

|编号|需求|功能|计算/数据|交互|视觉|整项结论|证据定位|限制/缺口|
|---|---|---|---|---|---|---|---|---|
|M01|MDL-01/AT-01|六轴模板及安装预设|PASS|PASS|NOT RUN|NOT RUN|MdlTemplate/MdlGoldenTemplate；tour step1/2|创建与默认值已验证；模板和完整机型的三维外观未全验|
|M02|MDL-01/02、P-03|七轴模板与显式权威|PASS|SKIP|SKIP|SKIP|MdlGoldenTemplate.SevenAxisRegisteredNotEnabled；Template.cpp:92|P-03工程数值未冻结，七轴模板按权威门禁未启用；不把守卫通过写成七轴模板已交付|
|M03|MDL-01、§2.1|结构增删/插入/重排/六轴重置|PASS|PASS|SKIP|PASS|StructureEditTest；ModelingPanelGuiTest.StructureButtons_*；tour step4|身份/参数保留/计数和按钮已验；SKIP仅指本条数组操作的独立视觉判断不适用|
|M04|MDL-01/07/09|关节类型/轴/原点/零位|PASS|PASS|SKIP|PASS|MdlTemplate.CreateEditChain_*；ModelingPanelGuiTest.JointDetailEditPane_*|12行既有字段及类型控件实测；空间正确性另列V01|
|M05|MDL-01/06/16|限位/速度/加速度|FAIL|FAIL|SKIP|FAIL|补充探针 JointVelocityAccelerationEditable_MDL01；Template.hpp:150；RobotDesign.hpp:421|速度/加速度没有JointEntry权威字段和编辑入口，模板参考值不进入计算|
|M06|MDL-02/09/10/AT-16|双权威与只读派生|PASS|PASS|SKIP|PASS|MdlDhCommand/MdlDhEquivalence；GUI DhAuthorityRejectsAxisEdit|真实转换/编译FK对照、权威锁与拒绝路径通过；完整五态结果页另列M07|
|M07|MDL-10/AT-16|显式转DH五状态|PASS|NOT RUN|SKIP|NOT RUN|MdlDhConvert 14；MdlDhEquivalence 6；MdlGoldenDhEquivalence 4|五态算法通过；五态真实模态结果页逐项可读性尚未操作|
|M08|MDL-03/11/AT-15|URDF字段/默认/忽略/不支持报告|PASS|FAIL|SKIP|FAIL|MdlImport/MdlGoldenImport；ModelingCommandFlows.cpp:442|核心导入报告完整；生产确认窗口只列映射/默认/忽略/不支持/待确认的计数，未逐项展示对象字段|
|M09|MDL-12/AT-17|多分支显式选链|PASS|FAIL|SKIP|FAIL|MdlImport；ModelingCommandFlows.cpp:803|URDF直接选链已有代码和核心测试；Xacro多分支生产流直接拒绝，未提供选链|
|M10|MDL-06/12|continuous工程范围|PASS|FAIL|SKIP|FAIL|MdlReadinessAssertionDomains；ContinuousWorkingRangeEditable_MDL12|核心确认范围语义通过；真实编辑页没有工程工作范围输入/确认|
|M11|MDL-12-S1/AT-36|混合链与4/5轴阻止|PASS|NOT RUN|NOT RUN|NOT RUN|MdlImport混合链/Template守卫；各消费者XML|6/7轴放行与4/5拒绝分别有测试；同一模型贯穿建模→报告的端到端实证未执行|
|M12|MDL-12/21|mimic/planar/floating/闭环|PASS|NOT RUN|SKIP|NOT RUN|MdlImport；MdlParts耦合校验|识别/阻断核心已验；各类真实导入提示及耦合宿主能力面尚缺全路径验证|
|M13|MDL-19/AT-31|Xacro参数与受控展开|PASS|FAIL|SKIP|FAIL|MdlXacroExpand 13；ExistingXacroIncludeConsumedByProductionBridge_MDL19|核心展开通过；真实生产桥空依赖树导致磁盘存在include仍失败，且无参数输入/多分支支持|
|M14|MDL-18/AT-33|外部WorkCell反向导入|PASS|FAIL|SKIP|FAIL|MdlWorkCellImport 9；WorkCellImportEntry_MDL18|反向提取内核通过；正式建模命令目录未提供外部WorkCell导入|
|M15|MDL-04|视觉/碰撞几何加载与解绑|PASS|PASS|NOT RUN|NOT RUN|GeometryLinkEditTest；GUI GeometryActionButtons_* / GeometryCopyButton_*|绑定/解绑/复制动作已验；STL/OBJ/DAE/WRL/IV逐格式实物形状、尺度和透明效果未全验|
|M16|MDL-04|占位连杆几何生成|PASS|PASS|NOT RUN|NOT RUN|MdlTemplate.PlaceholderCylinder_*；ModelingCommandFlows.PlaceholderGeometry_*|生成域原语和命令链通过；显示的圆柱代理不能代替外部视觉网格验收|
|M17|MDL-04/ARC-05/AT-19|自碰撞/排除对/安全距离|PASS|FAIL|NOT RUN|FAIL|KinCollisionPort；RobotDesign.hpp:469；正式宿主策略只读摘要|共享碰撞查询已验；未找到自碰撞排除配置可写入口，实际覆盖层未全验|
|M18|MDL-04/13|法兰与默认TCP|PASS|PASS|NOT RUN|NOT RUN|MdlCanonicalBridge；GUI ToolMountEdit_* / TcpListEdit_*|既有工具安装与TCP偏移编辑通过；三维坐标架/真实默认TCP选择未全验|
|M19|MDL-13|工具创建/删除/几何与物性|PASS|FAIL|NOT RUN|FAIL|MdlParts/MdlPartObjectCommands；ToolAndSceneCreationEntries_MDL13_MDL15|部件模型支持；空模板无新建/删除工具的完整UI，工具物性/几何编辑亦未完整承载|
|M20|MDL-13/AT-18|多个命名TCP|PASS|PASS|NOT RUN|NOT RUN|MdlCanonicalBridge；GUI TcpListEdit_* / TcpDisplayNameEdit_*；KinTcp/FK相关测试|命名TCP数据链、既有工具的列表编辑通过；多TCP真实坐标架显示尚未验|
|M21|MDL-05|实心/空心圆柱/矩形物性估算|PASS|FAIL|SKIP|FAIL|MdlPropertyEstimation 16；ModelingCommandFlows.cpp:922|三类几何解析公式通过；生产估算命令固定为默认半径的单段实心圆柱，没有空心/矩形/多段输入|
|M22|MDL-05/16|物性手工覆盖及质心确认|PASS|FAIL|SKIP|FAIL|MdlPropertyEstimation；PluginPanel.Centroid*；LinkPhysicalOverrideEditable_MDL05|平行轴算法/数据流有测试；控件将除zero-offset外物性行设只读，质心二选一仅数据层无真实入口|
|M23|MDL-06/AT-01|质量/SPD/三角不等式断言|PASS|NOT RUN|SKIP|NOT RUN|MdlReadinessAssertionDomains/MdlCommandPrepare；GUI就绪条|硬断言和缺失降级通过；质量/惯量非法值无法从当前只读物性页录入，故完整用户纠错闭环未验证|
|M24|MDL-06/ARC-05|行程策略与显式确认|PASS|FAIL|SKIP|FAIL|MdlReadinessTravelLimit/MdlCommandPrepare；UiPlugin.cpp:5201|策略核心及比较诊断通过；正式宿主固定系统缺省策略，无可配置机型行程阈值的完整写路径|
|M25|MDL-06/AT-01|WC/DWC双编译原子应用|PASS|NOT RUN|NOT RUN|NOT RUN|MdlCommandPrepare/MdlPartObjectCommands；runtime CompilerTest；tour step5|真实正向编译及失败计划测试已跑；tour绕过正常draft.apply呈现刷新，不足以证明真实点击后的三维与故障回滚全链路|
|M26|MDL-14/AT-18|名称映射与改名|PASS|NOT RUN|NOT RUN|NOT RUN|runtime ContractSuite.GoldenNameMapRoundtripDataset；tour step8|名称往返及重开选择通过；模型改名→全部消费者与三维刷新未执行|
|M27|MDL-15/AT-28|场景固定帧/障碍物管理|PASS|FAIL|NOT RUN|FAIL|MdlParts；GUI ScenePoseEdit_*；tour step5.7/8|已有场景位姿、真实raycast及重开变换通过；缺少新建/删除障碍物与选择几何来源的完整入口|
|M28|MDL-16|额定/峰值力矩及摩擦|PASS|PASS|SKIP|PASS|MdlDrivetrainSchema；GUI DrivetrainEdit_Chain；DynRnea/统计测试|既有传动页比率/摩擦/额定峰值字段和域消费已验；耦合另列M31|
|M29|MDL-17/AT-28|命名位姿/Home/Zero|PASS|FAIL|NOT RUN|FAIL|GUI PoseSetEdit_Chain；NamedPoseRestoreEntry_MDL17；HomeZeroUsesAuthoritativeReservedKeys_MDL04_MDL17|可增删保存条目；不能恢复任意命名姿态，Home/Zero命令只showInfo且使用错误保留键|
|M30|MDL-20/AT-28|规范包与XML导出/导入|PASS|PASS|SKIP|PASS|MdlPackage/MdlPackageWorkCell；ModelingCommandFlows Export*/Import*；tour step6|规范包roundtrip、失败原子性/取消/真实导出已验；预览剪贴板复制单项SKIP另列测试表|
|M31|MDL-21/AT-38|耦合矩阵位置/力矩/限位映射|PASS|FAIL|NOT RUN|FAIL|MdlParts耦合/Codec/R2映射；DtMappingR2Golden；CouplingMatrixEditable_MDL21|数学/编码/映射通过；无矩阵编辑入口，正式宿主HandlerServices还使用默认R1Locked|
|M32|MDL-22/AT-37|基座三预设与任意角|PASS|FAIL|NOT RUN|FAIL|MdlBasePlacementEdit；runtime BaseWorldTransform；DynRnea.InvertedMountGravitySignFlip；BaseEulerEditing_MDL22|三预设/任意旋转数据/重力通过；UI只给EAA不提供需求指定欧拉角，三安装姿态真实渲染/碰撞联合证据未齐|
|M33|MDL-08/UX-13/AT-12|结构/参数/物性差异|PASS|FAIL|SKIP|FAIL|MdlModelDiff 8；ModelDiffShowsObjectAndChangedValues_MDL08_UX13|真实差异算法识别参数变化；建模命令只showInfo统计条数，缺对象明细/前后值/点击定位|
|H01|MDL-07/UX-01/09|进入建模/目标和就绪状态|PASS|PASS|PASS|PASS|GUI ReadinessBar_*；HostWiring 5；tour截图1/2|空态、状态字段、模块接线和真实宿主基本布局可观察；不扩展为所有DPI均通过|
|H02|MDL-07|树↔属性↔三维选择|PASS|PASS|NOT RUN|NOT RUN|HostMigrationSelectionAdapter/EndToEnd；tour step5.7实际场景raycast|结构树/属性端口及场景拾取实测；机器人各连杆遮挡、多选高亮仍未全验|
|H03|UX-05/07|批量粘贴/筛选/单位|PASS|SKIP|SKIP|SKIP|ui_gui ParamTablePanelGuiTest；GUI UnitSeparation_*|单位/筛选已有断言；实际批量粘贴用例因剪贴板不可用跳过，不能判全通过|
|H04|UX-03/05|错误定位与中文工程文案|PASS|PASS|SKIP|PASS|MdlErrors；GUI Availability*/ReadinessBar*/CommandButtonTooltip*；UiText|已执行用例的错误语义与中文文案通过；未对所有导入错误窗口作视觉穷举|
|H05|PM-04/UX-07|暂存/确认/取消/脏标记|PASS|PASS|SKIP|PASS|GUI FirstSelectionShowsImportedValuesWithoutCancel；SameObjectRefreshKeepsPendingValue；EditingChain_*|回填、确认、暂存保留与焦点已验证；特定质量编辑缺口另列M22|
|H06|PM-18|本地撤销/重做|FAIL|FAIL|SKIP|FAIL|LocalUndoRedoEntries_L5；ModelingPanelWidget.cpp:949|建模草稿没有本地撤销栈/按钮，与单元卡L-5两级并存不符|
|H07|PM-18/PA-2|项目撤销/重做|NOT RUN|NOT RUN|NOT RUN|NOT RUN|ModelingUiModule.cpp:630；项目逆命令路径静态核对|未执行建模二次应用→项目撤销→重做→参数/三维全状态对照；事件只前移baseRevision，重载/重演存在待复现风险|
|H08|PM-04|保存草稿/关闭重开|PASS|NOT RUN|PASS|NOT RUN|ModelingCommittedRestore 3；HostMigrationModuleWiring恢复；tour step5.5/8|已保存已应用工作集同ID恢复与继续编辑通过；未应用草稿+工具/位姿/传动的真实关闭重开组合未全验|
|H09|PM-04/UX-03|基线冲突/编辑重演|PASS|NOT RUN|SKIP|NOT RUN|PluginPanel.Refresh_*；MdlCommandPrepare stale相关|编辑分组重演与stale核心有测试；正式宿主冲突对话三选择和用户输入保护未操作|
|H10|PM-07|只读会话|PASS|PASS|SKIP|PASS|GUI ReadOnly*/WritableChain* / PoseDrivetrainPanes_ReadOnlyDisables|只读控件/动态刷新防复活通过|
|H11|UX-13|命令可达/搜索/快捷键|PASS|PASS|SKIP|PASS|modeling命令目录对账；ui命令注册/快捷键/面板测试|已注册命令和面板按钮通过；缺失的建模功能命令并未因此被豁免|
|V01|MDL-04/22、UX-09|真实三维显示正确性|PASS|PASS|NOT RUN|NOT RUN|tour step5.5/5.7/8 + mtour-5.5-reopened.png|实际WorkCell/Drawable、末端FK/环境世界变换通过；仅看见圆柱代理链和拾取夹具，未完成真实网格机型/工具/混合链/各安装姿态全景对照|
|V02|UX-13|三维显示模式与导航|PASS|NOT RUN|NOT RUN|NOT RUN|ui HostView3DGateway / RuntimePublishBridge模型与Widgets测试|缺少可调用node_repl/@oai/sky；真实鼠标旋转、平移、缩放、适配、线框/透明逐操作未执行|
|V03|MDL-07/ARC-05|三维覆盖层与拾取|PASS|PASS|NOT RUN|NOT RUN|tour real-center-pixel-raycast / QObject输入链；ui SampleHighlight|真实夹具raycast与选择通过；机器人连杆深度/高亮与碰撞覆盖像素正确性未全验|

状态计数：NOT RUN 18；SKIP 2；PASS 10；FAIL 17。这不是需求覆盖百分比，每一行含多个必须满足的分支。

