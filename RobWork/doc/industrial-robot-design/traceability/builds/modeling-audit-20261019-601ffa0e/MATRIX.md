# 建模模块全功能验收矩阵（执行前清单）

- 基线：601ffa0ed8b4f7887576dfb9a560acffea86b757；分支 wp14-t12；2026-10-19。
- 范围：REQUIREMENTS §2.1、MDL-01～22（含已启用R2）、AT-01/12/15/16/17/18/19/20/28/31/33/36/37/38及通用UX/PM交互。
- 方法：每行分别审查真实计算/持久化、Qt窗口交互和三维画面；自动测试仅覆盖其实际断言，不代替人工视觉结论。
- 状态仅 PASS / FAIL / SKIP / NOT RUN；未找到测试或无法操作的路径标 NOT RUN。七轴模板P-03未冻结状态单独报告。
- 当前工具目录未暴露 node_repl/cua/sky，无法调用 computer-use 所要求的 @oai/sky；真实窗口将先使用项目既有Qt自动验收通道，不能据此宣称鼠标人工操作已覆盖。

|编号|来源|功能|如何验证|自动化定位关键词|交互/视觉重点|初始状态|
|---|---|---|---|---|---|---|
|M01|MDL-01/AT-01|六轴模板及安装预设|创建地面/壁装/倒挂模板，核对6关节7连杆、默认轴限位；取消不留对象|MdlTemplate / GoldenTemplate|模板向导、可见机器人|NOT RUN|
|M02|MDL-01/02、P-03|七轴模板与显式权威|核对启用状态和未冻结阻止原因；自定义7轴编辑；不得误称模板可用|MdlTemplate / MdlGolden|七轴创建入口/阻止提示|NOT RUN|
|M03|MDL-01、§2.1|结构增删/插入/重排/六轴重置|逐操作核对身份、参数随对象保留、链计数、边界拒绝及重置取消|Structure / Template|操作按钮、确认和树顺序|NOT RUN|
|M04|MDL-01/07/09|关节类型/轴/原点/零位|分别编辑、暂存、确认和取消，非法值保留原权威值；更换选择不串对象|Joint / Editing|真实参数页|NOT RUN|
|M05|MDL-01/06/16|限位/速度/加速度|有限区间硬拒绝；逐轴速度加速度写入、单位、重开一致|Joint / Assert / Codec|完整字段入口|NOT RUN|
|M06|MDL-02/09/10/AT-16|双权威与只读派生|DH下Axis/Origin不可写；显式可写；切换经等价验证|Dh / Authority|模式切换结果页|NOT RUN|
|M07|MDL-10/AT-16|显式转DH五状态|各取Exact/NonUnique/Approximate/NotExpressible/AnalysisFailed，逐轴误差和收敛信息；近似禁止权威切换|DhConvert / DhEquivalence|五状态可读呈现|NOT RUN|
|M08|MDL-03/11/AT-15|URDF字段/默认/忽略/不支持报告|任意非零轴、缺axis默认X待确认；零/NaN轴仅报告零修订|Import / GoldenImport|文件选择和报告确认/取消|NOT RUN|
|M09|MDL-12/AT-17|多分支显式选链|未选阻断；选中主链按该链判断；辅助分支不误拒|Import|分支目录与确认|NOT RUN|
|M10|MDL-06/12|continuous工程范围|未确认阻断；确认有限区间后保持continuous类型且不回写bounds；跨周不取模|Continuous / Readiness|确认范围的用户入口|NOT RUN|
|M11|MDL-12-S1/AT-36|混合链与4/5轴阻止|6/7轴prismatic全链路；4/5轴正式计算/报告始终阻断|Prismatic / Mixed / Range|类型切换、单位及诊断|NOT RUN|
|M12|MDL-12/21|mimic/planar/floating/闭环|非法目标链不伪装Fixed；耦合仅按已启用支持面开放|Import / Coupling|不可表达报告|NOT RUN|
|M13|MDL-19/AT-31|Xacro参数与受控展开|正常展开、缺依赖、语法失败、取消、安全边界、来源记录|Xacro|参数输入/错误定位|NOT RUN|
|M14|MDL-18/AT-33|外部WorkCell反向导入|关节/限位/位姿/资源提取；多设备选择；有损条目逐项可见；仅草稿|WorkCellImport|入口、文件对话框、提取报告|NOT RUN|
|M15|MDL-04|视觉/碰撞几何加载与解绑|按资源类型加载、丢失/损坏拒绝、绑定来源和变换持久；复制视觉到碰撞|Geometry / Package|外部网格实际形状/比例/位置|NOT RUN|
|M16|MDL-04|占位连杆几何生成|相邻原点生成圆柱、退化链失败；无资源时不可假称真实网格|Geometry / Canonical|连杆连续性/颜色/外形|NOT RUN|
|M17|MDL-04/ARC-05/AT-19|自碰撞/排除对/安全距离|启停/排除/相邻规则参与真实碰撞；改配置后重算|Collision / Policy|碰撞覆盖层与排除反馈|NOT RUN|
|M18|MDL-04/13|法兰与默认TCP|编辑安装链、默认引用保护；编译FK末端位置一致|Tool / Canonical|坐标架与TCP标记|NOT RUN|
|M19|MDL-13|工具创建/删除/几何与物性|完整工具编辑写入、质量惯量合法性、被引用对象删除保护|Part / Tool|工具树及附着三维形状|NOT RUN|
|M20|MDL-13/AT-18|多个命名TCP|增删/重命名/偏移、默认删除保护；非默认TCP真正FK/IK消费|Tcp / ToolDescription|TCP列表/名称/坐标架|NOT RUN|
|M21|MDL-05|实心/空心圆柱/矩形物性估算|解析公式、材料密度、旋转合成、平行轴，与独立算值比对|PropertyEstimation|估算输入与来源徽标|NOT RUN|
|M22|MDL-05/16|物性手工覆盖及质心确认|质心移位两路径：平行轴迁移或完整张量；取消零写入|Centroid / Inertia / Part|内联二选一、预览数值|NOT RUN|
|M23|MDL-06/AT-01|质量/SPD/三角不等式断言|非法已提供值阻断并定位；缺失仅DataInsufficient；所有连杆均检查|Assert / Readiness|就地错误、对象和建议|NOT RUN|
|M24|MDL-06/ARC-05|行程策略与显式确认|默认4π边界；超限实际/阈值/rad；拒绝/接受及自定义阈值|Travel / Readiness / Command|确认凭据和取消|NOT RUN|
|M25|MDL-06/AT-01|WC/DWC双编译原子应用|真实双编译；任一失败不修订、旧快照不变；再次应用成功|Compile / Command|应用状态与三维刷新|NOT RUN|
|M26|MDL-14/AT-18|名称映射与改名|关节Frame几何规则全部ID↔全名唯一往返，无旧/双前缀|Name / Canonical|改名后树/三维拾取一致|NOT RUN|
|M27|MDL-15/AT-28|场景固定帧/障碍物管理|新增/改位姿/改来源/删除；参与真实环境碰撞，安装姿态不拖动世界障碍|Scene / Collision|世界固定几何、树联动|NOT RUN|
|M28|MDL-16|额定/峰值力矩及摩擦|逐轴编辑fv/fc/偏置和力矩；来源/缺失降级，进入动力学|Drivetrain / Friction|完整输入字段与单位|NOT RUN|
|M29|MDL-17/AT-28|命名位姿/Home/Zero|保存命名/恢复/改名/移除；q匹配；会话姿态不改模型修订|PoseSet / NamedPose|恢复后真实关节形态|NOT RUN|
|M30|MDL-20/AT-28|规范包与XML导出/导入|完整roundtrip；拒外部伪包；失败恢复原输出；WC/DWC预览只基于已应用|Package / Preview|路径选择/覆盖确认/导出内容|NOT RUN|
|M31|MDL-21/AT-38|耦合矩阵位置/力矩/限位映射|非对角矩阵、双向位置、虚功力矩、病态/非常量拒绝，真实消费者|Coupling / Drivetrain|矩阵编辑入口与错误呈现|NOT RUN|
|M32|MDL-22/AT-37|基座三预设与任意角|整组原子编辑/持久；缺省地面；倒挂重力符号量值；同一编译变换|BasePlacement / Gravity|地面/墙装/倒挂真实画面|NOT RUN|
|M33|MDL-08/UX-13/AT-12|结构/参数/物性差异|分组差异与身份定位，基线不变，候选设当前新分支新修订|ModelDiff|差异表点击定位|NOT RUN|
|H01|MDL-07/UX-01/09|进入建模/目标和就绪状态|空/已创建/缺输入/阻断/警告状态可观察且下一步可达|Readiness / HostWiring|真实宿主布局/文字/裁切|NOT RUN|
|H02|MDL-07|树↔属性↔三维选择|树选/拾取/清空/域外对象、单选多选，属性只关联目标|HostMigration / Selection|真实拾取/高亮准确|NOT RUN|
|H03|UX-05/07|批量粘贴/筛选/单位|数值与单位分列、m/mm/rad/deg换算；非法粘贴不半更新；暂存保留|ParamTable / Unit|键盘输入、筛选和焦点|NOT RUN|
|H04|UX-03/05|错误定位与中文工程文案|失败对象/原因/建议，比较实际/阈值/单位；无内部键泄漏|Errors / UiText|中文、长提示与可读性|NOT RUN|
|H05|PM-04/UX-07|暂存/确认/取消/脏标记|取消零写入；同对象刷新不丢暂存；换对象不串值；应用后基线回填|Pending / Refresh / Editing|按钮与键盘焦点|NOT RUN|
|H06|PM-18|本地撤销/重做|多字段、结构、工具、场景操作可回退和重做，零修订|Undo / Redo / CommandFlows|两级撤销入口区分|NOT RUN|
|H07|PM-18/PA-2|项目撤销/重做|第二次应用后逆命令新修订，旧修订不变、内容和场景回退|Command / UndoRedo|窗口状态、三维同步|NOT RUN|
|H08|PM-04|保存草稿/关闭重开|保存未应用草稿并恢复；已应用根同ID可编辑；工具场景和TCP齐全|Restore / Draft / Codec|真实文件重开后的树和三维|NOT RUN|
|H09|PM-04/UX-03|基线冲突/编辑重演|Stale三选项、分组重演失败提示、取消保留输入|Rebase / Replay / Stale|冲突对话与对象差异|NOT RUN|
|H10|PM-07|只读会话|写控件/命令禁用；浏览、选择、单位、预览可用；刷新不得复活写入|ReadOnly / Writable|灰显原因及菜单|NOT RUN|
|H11|UX-13|命令可达/搜索/快捷键|建模命令注册、模糊搜索和键盘导航；重复快捷键拒绝|CommandRegistry / Palette|正式宿主命令入口|NOT RUN|
|V01|MDL-04/22、UX-09|真实三维显示正确性|非退化六轴/七轴/混合链；独立FK与坐标架、网格/工具/环境对照；截图人工审阅|Host tour|实际渲染而非仅对象存在|NOT RUN|
|V02|UX-13|三维显示模式与导航|旋转/平移/缩放/适配/复位；线框/透明/渲染分组；关闭重开稳定|View3D|真实窗口逐操作截图|NOT RUN|
|V03|MDL-07/ARC-05|三维覆盖层与拾取|遮挡/深度/坐标架/高亮/碰撞覆盖可见且命中正确对象|View3D / Selection|实际鼠标拾取和画面|NOT RUN|

