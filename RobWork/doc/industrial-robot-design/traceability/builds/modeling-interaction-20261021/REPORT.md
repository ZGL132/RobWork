# WP-13-T30 建模交互修复第1批验证报告

日期：2026-10-21。实现分支：wp13-t30；基线：04b69c307a5af0e64904193bddca39f861407f6a。

本批实施验证通过，等待用户人工验收；不是建模47项全功能通过，也不是全新上下文的独立验收。未自动合入redesign-main。用户要求每批输出步骤后暂停，本批交付后不继续下一批。

## 修复与验证映射

| 问题 | 修复 | 自动验证与人工入口 |
| --- | --- | --- |
| F01 常用入口被折叠 | 模板及URDF/Xacro/WorkCell导入常驻 | ManualEntryCommandsVisibleWithoutExpansion；人工§1 |
| F02 长表挤出应用按钮 | 属性摘要限高滚动，编辑/工具整页滚动 | ManualWindowFitsAndJointApplyIsReachable，1100×760截图；人工§1 |
| F03 树选择未切编辑页 | 对象切换时正确分派，刷新不抢主动页面 | TreeSelectionReturnsToJointEditor；人工§2 |
| F04 同目标刷新丢暂存 | 对象OID、子键和真实基线绑定；来源变化才重绑 | SameTargetRefreshKeepsPoseInputs、RefreshKeepsNamedPoseAndDrivetrainInputs；人工§3～5 |
| F05 占位资源悬空、零长异常 | 封闭米制OBJ、Recorded登记、原子候选写入；零长中文拒绝 | PlaceholderGeometry、PlaceholderZeroLengthIsActionableAndAtomic；正式tour；人工§6 |
| F06 排队编辑取消后复活 | 输入同步归属当前表单，只有重绘排队；同行集保留item | QueuedCellEditCannotSurviveCancel、QueuedCellEditCannotCrossBaselineRefresh；人工§3 |
| F07 同下标新对象串值、权威回填被覆盖 | 关节按OID隔离，销毁聚焦属性控件前断提交信号，领域回填优先 | ReplacedJointCannotInheritOldInput、AuthoritativeRebindDuringConfirmWins；人工§3 |
| F08 TCP选项与偏移不一致 | 切换即时重绘，有工具恢复下拉浏览；同目标保留显示名 | TcpSelectionImmediatelyRepaintsOffset；人工§4 |
| F09 位姿选项跳回、非法构型仍保存备注 | 记录新键后刷新，错误阻止整条保存，空会话保护 | NamedPoseComboActuallySelectsNewEntry、InvalidPoseConfigBlocksSave；人工§5 |
| F10 正式提交把存在文件报缺失 | io扩展盘符剥前缀正确且规范化幂等 | ExtendedUserSourceReentersWithSameEntity、ExtendedProjectBaseIsIdempotent |
| F11 canonical无路径导致网格只剩代理 | 按已校验内容选择OBJ/STL既有解码器，不把路径加入身份 | ContentMeshesWithoutPathHint；真实OBJ/二进制STL |
| F12 正式快照丢资源供给 | 工厂将请求已有resources传入真实S6 | FactoryRetainsRequestedMeshResources；正式tour重开六段网格 |

问题编号为本批局部编号，不复用此前47项审计编号。全部对应最终XML中的T30或既有具名测试。计算与变换使用真实实现；工厂测试仅编译端口使用既有ScriptedCompiler，真实S6负责几何；正式tour提供生产装配接线证据。

## 最终验证

- [PASS] 集成全量构建：final/batch1-final-build.log。
- [PASS] 独立全量构建：final/batch1-final-standalone-build.log。
- [PASS] ird_gates：final/batch1-final-gates.log，零红线命中。
- [PASS] 下列17套测试XML与原生ird-test-report.json逐项计数对账，0失败。

| 套件 | 通过 | 跳过 | 失败 |
| --- | ---: | ---: | ---: |
| core | 65 | 0 | 0 |
| io | 132 | 0 | 0 |
| modeling | 463 | 0 | 0 |
| modeling_contract | 17 | 0 | 0 |
| modeling_gui | 100 | 0 | 0 |
| runtime | 301 | 1 | 0 |
| standalone_io | 132 | 0 | 0 |
| standalone_modeling | 361 | 0 | 0 |
| standalone_modeling_contract | 8 | 0 | 0 |
| standalone_modeling_gui | 69 | 0 | 0 |
| standalone_runtime | 195 | 0 | 0 |
| standalone_ui | 276 | 0 | 0 |
| standalone_ui_contract | 34 | 0 | 0 |
| standalone_ui_gui | 78 | 0 | 0 |
| ui | 342 | 0 | 0 |
| ui_contract | 63 | 0 | 0 |
| ui_gui | 98 | 0 | 0 |

集成合计：1581通过、1跳过、0失败；独立合计：1153通过、0跳过、0失败。两模式重复覆盖不当作不同功能数量。

- [SKIP] runtime的ContractSuite.ChildProcessWorker_RT_ID_1仅由父用例派生时执行，直接枚举按既有设计跳过；父用例CrossProcessIdentityStable_RT_ID_1实际通过。不是GUI或网格验收跳过。
- [PASS] 正式宿主modeling-tour退出0：final/batch1-final-modeling-tour.log及stderr。真实应用/保存重开后六个PlainTriMesh(type=2)、每段128三角面；轴向起点0、终点sqrt(0.1²+0.15²)m、最大半径0.05m，容差1e-6m。六次generated-mesh-local-metre-dimensions及six-generated-meshes-restored-in-host-geometry均通过。
- [PASS] 实际查看正式截图mtour-t30-visible-meshes-after-reopen.png：正式三维有可见红色圆柱链，默认相机较远；不是代理坐标架或替代绘图。实际查看t30-joint-window.png：1100×760下关节编辑表及应用/取消按钮可达。
- [NOT RUN] 用户人工验收；按MANUAL-BATCH-01.md执行后反馈。其余安装姿态全矩阵、导航/透明/遮挡等全功能真实视觉不在本批完成声明内。

## 红绿与中间失败

red/保留9组有效红回归的日志、XML、原生报告：入口/布局/选择/位姿、暂存/TCP、排队取消、占位资源/零长、权威回填、位姿保存、io扩展路径、无hint网格、正式工厂资源。以上对应项均在final/完整套件通过。

实施中另有错误测试输入单位/key、工厂夹具对象引用不完整，不作为有效红证。完整GUI曾暴露空会话访问5项SEH，已补空模型保护；最终集成GUI100通过、独立69通过。尝试盲送OBJ/STL解码曾被完整runtime反例发现2项SEH，已撤去该实现，先按内容选择解码器；最终runtime301通过、1辅助跳过。独立树曾遇到Transform/Rotation外联符号链接失败，已改显式矩阵/逐元素变换，最终全量构建通过。未删除反例或削弱断言。旧失败不覆盖最终结果，也不隐藏。

## 留待后续，不能冒称封闭

1. 任意外部几何非单位localTransform尚缺runtime公共投影；本批占位生成将定位烘焙入连杆系顶点、引用单位变换，只解决本批生成路径。
2. worker物化资源供给仍为原能力边界；正式同步快照工厂已接通不代表worker已接通。
3. 生成网格为本机持久缓存中的Recorded外部资源；未固化前依赖缓存，不能认为工程独立打包携带。
4. 47项全面人工/真实视觉复验尚未完成，后续批次和用户反馈决定继续范围。

## 复现与交付

run.ps1、test-batch.ps1为本机复现脚本（路径及工具链见脚本）；GUI严格VS x64、QT_QPA_PLATFORM=windows、绝对路径串行执行。Start-Manual.ps1启动本工作树正式程序并清理tour模式，不自动修改工程。根start_studio.bat启动D盘旧工作树，本批人工验收不要使用。

治理校验日志位于本机build/modeling-interaction-20261021/，提交前执行validate-task、verify-task、validate-docs、范围和commit-lint检查。精确提交由本文件所属Git提交标识；binary-and-source-sha256.json绑定验证时的源文件与正式程序，最终代码不再变更。

本批建模GUI精确用例：

- ManualEntryCommandsVisibleWithoutExpansion_UX01_T30
- TreeSelectionReturnsToJointEditor_MDL07_T30
- ManualWindowFitsAndJointApplyIsReachable_UX09_T30
- NamedPoseComboActuallySelectsNewEntry_MDL17_T30
- SameTargetRefreshKeepsPoseInputs_UX05_T30
- TcpSelectionImmediatelyRepaintsOffset_MDL13_T30
- RefreshKeepsNamedPoseAndDrivetrainInputs_UX05_T30
- InvalidPoseConfigBlocksSave_MDL17_T30
- ReplacedJointCannotInheritOldInput_UX05_T30
