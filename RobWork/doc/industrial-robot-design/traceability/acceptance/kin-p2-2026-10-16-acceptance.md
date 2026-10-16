# KIN-P2 独立验收记录（attempt 1）

- 验收对象：工作包 KIN-P2 求解请求与位置任务（审查 20261016 A02/A08 修复批）
- 分支 / 提交：`kin-p2` @ `2c163349d2145b0fba77cc5352fcbdfd86b21bde`（local = origin/kin-p2，推送已核实）
- 验收人：P2 验收者（对抗式独立验收会话，与实施者零共享上下文；本记录为唯一写入库文件）
- 验收日期：2026-10-16
- **结论：FAIL（1 项 blocker——文档失实；代码修复面本身经受住了全部对抗攻击）**

---

## 1. 结论摘要

A02/A08 两项的**代码级根因修复经读码定位与独立对抗探针验证均成立**（见 §3/§4）；留痕、门禁、回归零回归、提交四段式均属实（见 §6）。唯一 blocker 是单元卡 `units/kinematics.md` v0.24 版本表头行**声称 Envelope 调用侧已改 PositionOnly，与代码事实相反**（Envelope.cpp 未在本提交触碰，旧 π 机制仍在位且被测试钉住）——文档失实违反 AGENTS.md §6.1（"不允许文档失实"）与任务书交付要求 6（文档同步），且会误导 KIN-P5（包络/工作空间，A04 范围）的实施者以为包络通道已迁移。按"零 blocker 才 pass"裁定 FAIL。

## 2. 阻断项（blocker）

### B-1 单元卡 v0.24 头行对 Envelope 的改造声明失实

- **what**：必须修复（改文档行去除 Envelope 字样并如实登记包络通道残留边界，或补做 Envelope 通道迁移）才能通过。
- **evidence**：
  - 声明原文：`doc/industrial-robot-design/units/kinematics.md:7`（v0.24 版本表行）："…＋Ik.cpp 迭代/收敛/残差复验任务维度化＋**Workspace/Evaluators/Envelope 调用侧位置存在性改 PositionOnly（旧姿态容差 π 落值机制废除）**…"（本提交新增行，`git show 2c163349` 可证）。
  - 代码事实 1：`git show 2c163349 --name-only` 文件清单**不含任何 Envelope 文件**；`git log -- Envelope.cpp` 最后触碰为 `413d9392`（WP-15-T15）。
  - 代码事实 2：`kinematics/src/Envelope.cpp:156` 仍为 `request.orientationTolerance = kEnvelopePoseFreeTolerance;`（π，定义于 `Envelope.hpp:97`），且未设 `taskSpace`（默认 FullPose）——即 A02 同型"伪姿态自由"机制在包络位置存在性探针上原样在位。
  - 代码事实 3：`kinematics/test/EnvelopeTest.cpp:540-541` 仍断言 `rec.orientationTolerance == kin::kEnvelopePoseFreeTolerance`；本验收实测 `--gtest_filter=KinEnvelope.*` **11/11 通过**——测试钉住旧机制，与文档声明直接矛盾。
  - 文档自相矛盾：同提交 `kinematics.md` §14.6 v0.24 详情行只登记 Workspace.cpp / Evaluators.cpp / Sampling.cpp 三常量移除，未含 Envelope——头行超出事实。

## 3. A02 核对（仅位置任务独立任务维度）——代码修复成立

根因层读码（非"确认有改动"）：

| 检查点 | 位置 | 结论 |
| --- | --- | --- |
| 任务维度来源 | `kinematics/src/Ik.cpp:427-429` | PositionOnly→taskDim=3、FullPose→6（默认逐位保持 v1） |
| 迭代目标函数降维 | `Ik.cpp:187-209`＋`Ik.cpp:598-599` | DLS 正规方程 m×m，Jₜ 取 Jacobian **前 taskDim 行**，e 只进 `e.head(taskDim)`；poseError e(0..2)=位置误差（`Ik.cpp:150-157`）、Jacobian 前 3 行=线速度子块（`Fk.cpp:318-323`）——行列对齐正确 |
| 收敛判据单一实现点 | `Ik.cpp:215-222`，消费于 `Ik.cpp:577`（迭代内）与 `Ik.cpp:664`（硬过滤①残差复验） | PositionOnly 只查位置残差；两处**同源同判据**，无"迭代放行、复验拦截"漂移 |
| 词表外值 fail-fast | `Ik.cpp:417-423` | uint8 强转越界拒绝（NFR-COR-03） |
| 词表与默认值追加纪律 | `Ik.hpp:284-291`、`Ik.hpp:351`、`Ik.hpp:77` | 表尾追加＋默认 FullPose（既有聚合初始化零破坏）；契约版本 1→2（CON-04，升级理由在头注 67-76 行成立） |
| 调用侧迁移 | `Workspace.cpp:474`、`Evaluators.cpp:2505` | 位置样本改 PositionOnly；旧 π 常量移除（`Workspace.cpp:58-61`、`Evaluators.cpp:2141-2145`、`Sampling.cpp:77-79` 注）；`kWorkspacePoseFreeTolerance/kPoseFreeTolerance/kOrientationFreeTolerance` 全仓无残留消费点（grep 证实） |

## 4. 独立对抗探针（验收者自造数据/模型，尝试打破修复）

探针源码 `%TEMP%/kin-p2-acc-probe/KinP2AcceptanceProbe.cpp`（不入库），复用审查留下的 `compile-integrated.rsp` 编译面（VsDevCmd x64＋cl，链接集成树静态库），与实施者测试用例零共享数据。结果：

| 探针 | 数据/模型（实施者未用过） | 判定 | 结果 |
| --- | --- | --- | --- |
| P1 | 平面 2R（twoLinkModel），目标 (0,1.2,0)——位置可达、恒等姿态在该点不可达 | FullPose+π 不收敛（kind=2）✓ | **单初值 PositionOnly 收敛到限位外分支被硬过滤②拒绝（kind=3）**→见 advisory A-2 |
| P1b | 同上，产品真实形态多初值 JointGrid×4/16/64 | PositionOnly 必须 Reached | **PASS**（三种初值数均 kind=1，独立 FkEvaluator 复验 TCP=(0,1.2) 误差 ≤1.3e-8 m） |
| P2 | 转动＋移动混合链（revPrismModel），目标 (0,1,0)（q1=π/2、q2=−0.3）——攻击移动列在 3 维 Jacobian 子块的正确性 | PositionOnly 收敛＋独立 FK | **PASS**（FullPose+π 对照 kind=2；PositionOnly 独立 FK 误差 1e-6 m；姿态残差 1.5708 rad 如实记录不拦截） |
| P3 假阳性攻击 | 同轴退化链 PositionOnly 目标 (0,0,1)（解析界限 2 m 之内、物理不可达），JointGrid×4 | 绝不许 SolutionsFound | **PASS**（kind=2 搜索未果、零解、无不可行证明素材——保守语义保持） |
| P4 短路完好性 | 同轴链 PositionOnly 目标 (0,0,5)（超解析半径） | 必须 AnalyticBoundExceeded | **PASS**（kind=5） |
| P5 平凡可达不回归 | twoLinkModel PositionOnly 目标 (1.8,0,0)（姿态恒等恰可达） | 必须收敛 | **PASS**（双任务均收敛，独立 FK 误差 0） |
| P6 NaN 攻击 | 会话姿态注入 NaN（维度合法）→装配放行则 fail-fast 穿越 Qt 槽 | 装配必须拒绝 | **PASS——攻击被上游封闭**：`KinSessionPose::setJointConfiguration` 在会话写点即抛 invalid_argument（`Commands.hpp:124-126` 契约、`Commands.cpp:120` 实现），NaN 进不了会话态，referenceQ 无非有限来源 |
| P6b 零位回退 | 会话未设置态装配 | 装配成功、维度齐全 | **PASS**（intervals/referenceQ 均 2 维、初值 1 组） |

探针小结：A02 修复在**任务语义、Jacobian 子块（含移动列）、假阳性防护、解析短路、平凡可达**五个攻击面全部守住；唯一未守住的单初值形态属求解器已登记的局部搜索边界（非本包缺陷）。

## 5. A08 核对（按钮请求装配）——代码修复成立

| 检查点 | 位置 | 结论 |
| --- | --- | --- |
| 装配唯一编排点 | `kinematics/plugin/KinPanelFlows.cpp:46-152` | TCP=KIN-14 `defaultTcpIndex` 解析为 `TcpRef{toolObject, tcpKey=tool.localName}`（失败=KIN-NO-TCP 不提交）；配置前置 `validateAnalysisConfiguration`（非法=KIN-CONFIG-ILLEGAL，防 seed=0 流入 makeInitialValues）；intervals=`evaluationIntervals(view)` 同快照；referenceQ=会话姿态（维度违约显式拒绝）／零位回退；初值=`makeInitialValues(config, intervals, referenceQ)`；容差/迭代/去重=配置直传 |
| 按钮接线 | `KinematicsPanelWidget.cpp:538-545` | 装配失败→状态行显式诊断码并 return（零求解器触达）；修复前默认构造 IkRequest 直传的残缺路径已消除（diff 证实仅 15 行改动） |
| 零 Qt 编排声明 | `KinPanelFlows.cpp` include 面 | 证实（无 Qt 头） |
| 遗留自报核实 | `KinematicsPanelWidget.cpp:581` | `onSetDefaultTcpClicked` 仍传空 `TcpRef{}`——与实施者自报一致，单元卡已登记 KIN-P4 范围 |

## 6. 回归测试、定向复跑与留痕

### 6.1 测试用例核对（9 项新增/改造，全部存在、断言有强度、名带追溯）

- `KinIk.PositionOnlyTaskIndependentDimension_KIN_P2`（`IkTest.cpp:303`）：三段钉——a) FullPose+π 现症对照（该退化链 6 维任务**真实无解**的机理钉，防回退）；b) PositionOnly 姿态容差收紧 1e-12 仍收敛＋位置残差入容差＋**FkEvaluator 独立 FK 复验 TCP≈(0,1,0)**＋姿态残差 >1e-6 如实记录（若姿态回判据必红）；c) 默认 FullPose/枚举值/契约版本=2 钉。**非恒真**。
- `KinIk.TaskSpaceOutOfRangeFailsFast_KIN_P2`（`IkTest.cpp:396`）：词表外强转 7U → EXPECT_THROW＋消息含 "taskSpace"。
- `KinRegionCoverage.PositionSampleRealSolverDegenerateChain_KIN_P2`（`RegionCoverageTest.cpp:821`）：**真实 IkSolver**（非脚本）位置样本 DataInsufficient→Reached 翻转钉。
- `KinRegionCoverage.PerSampleIkWiringPositionVsPose_WP15T06_ACC5`（改造，diff 审阅）：样本判别从"姿态容差≥π"改为 `taskSpace==PositionOnly`——**强化非弱化**（语义字段判别替代偶然值判别）。
- `KinWorkspace.PositionSampleRealSolverDegenerateChain_KIN_P2`（`WorkspaceTest.cpp:550`）：探索式通道同源反例＋bestSolution 位置残差断言。
- `PluginPanelLinks.LK2_AssembleSessionSolveRequestComplete_KIN_P2`（`PluginPanelTest.cpp:308`）：装配逐字段来源钉（TCP 解析/findObject、初值=makeInitialValues 同参全等、referenceQ=会话姿态、intervals 逐轴三字段、配置直传四值）。
- `PluginPanelLinks.LK2_AssembleSessionSolveRequestFailures_KIN_P2`（`PluginPanelTest.cpp:370`）：五态失败面（无工具 KIN-NO-TCP/seed=0 KIN-CONFIG-ILLEGAL/姿态维度违约/视图空/零位回退）。
- gui_test 三项（`KinematicsPanelInputGuiTest.cpp:343/400/434`）：真实控件事件探针——装配字段断言／**真实 IkSolver 闭环**（修复前 invalid_argument 穿越 Qt 槽的复现点，ASSERT_NO_THROW＋解集入会话态＋状态行内联完成）／无工具 KIN-NO-TCP 零触达。载体取舍（twoLink 非奇异，避开 A03 显示面）在注释与单元卡如实登记。
- 夹具 `KinFkFixture.hpp:301` `coaxialChainModel`：与审查探针同源几何（六轴同轴、TCP 1 m）。

### 6.2 本验收真实执行的定向复跑（不重跑全量——编排脚本已全绿）

| 命令（集成树 exe，PATH 含 build RobWork/RWS bin、vcpkg bin、Qt bin） | 结果 |
| --- | --- |
| `sdurws_ird_kinematics_test.exe --gtest_filter=KinIk.*:KinRegionCoverage.PositionSampleRealSolverDegenerateChain_KIN_P2:KinRegionCoverage.PerSampleIkWiringPositionVsPose_WP15T06_ACC5:KinWorkspace.PositionSampleRealSolverDegenerateChain_KIN_P2:PluginPanelLinks.LK2_AssembleSessionSolveRequestComplete_KIN_P2:PluginPanelLinks.LK2_AssembleSessionSolveRequestFailures_KIN_P2` | **26/26 PASSED**（KinIk 全组 21＋新增 5） |
| `sdurws_ird_kinematics_gui_test.exe --gtest_filter=KinematicsPanelInput.SolveButton_*_KIN_P2` | **3/3 PASSED** |
| `sdurws_ird_kinematics_contract_test.exe --gtest_filter=KinTaskPointIkEval.*` | **11/11 PASSED** |
| `sdurws_ird_kinematics_test.exe --gtest_filter=KinEnvelope.*` | **11/11 PASSED**（实证 Envelope 仍钉 π 机制——B-1 证据） |
| `cmake -DIRD_ROOT=<industrialrobot 绝对路径> -P cmake/ird_gates.cmake` | **exit=0，R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（独立复跑） |
| `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace, 355 task files）**（独立复跑） |
| 对抗探针 `kinp2probe.exe`（§4） | 8 PASS / 1 FAIL（该 FAIL 定性为 advisory A-2，非修复缺陷） |

### 6.3 留痕与提交核对

- `traceability/builds/kin-p2/` 16 件在库：build-integrated.log（27 KB 真实 MSBuild 输出、零 error 行）、kinematics-test.xml（root `tests="238" failures="0"`——231 基线＋P1 增 1＋本包增 6，KIN_P2 计数 6 与代码一致）、contract XML（57/0）、gui XML（7/0，KIN_P2 计 3）、ird-gates.log（零命中）、冒烟 configure/build/三套件日志（smoke-kinematics-test.log 231 PASSED＝225＋1＋6-1skip 口径自洽）。
- 二进制新鲜度：集成树三测试 exe mtime（16:23~16:28）晚于最新源码（16:22）——复跑结果反映当前提交源码。
- 提交 `2c163349`：四段式齐备（What 按文件/Why 对 A02·A08 与需求 ID/How 真实数字/影响面），local=origin（`git rev-parse kin-p2 origin/kin-p2` 同值）。
- 框架零修改：提交文件清单全部位于 `industrialrobot/`＋文档留痕＋单元卡；`ird_gates` SA02 零命中。
- 注释规范抽查（AGENTS.md §2）：Ik.hpp 词表/契约版本注（含升级理由）、Ik.cpp taskDim/convergedForTask 逐步注、KinPanelFlows.cpp 装配七步注、gui_test 用例头注（审查条目/修复前形态）——单位、坐标系、错误语义、追溯齐全；未发现 §2.6 反面模式。
- PA-2 抽查：IkRequest 表尾追加带默认值；单元卡版本行链式追加；无历史改写（分支线性基于 kin-p1 验收记录 0afc080a）。

## 7. 建议项（advisory，不阻断）

- **A-1 Envelope 通道 A02 同型机制残留**：`Envelope.cpp:156`＋`Envelope.hpp:97` 的 π 容差位置探针（KIN-10/A04，审查划归 KIN-P5）行为与 v1 逐位相同、无回归，但属 A02 同根因的第四个活消费点。处理 B-1 时建议一并把该残留显式登记为 KIN-P5 待办（迁移需同步评估 `kEnvelopeContractVersion` 升版）。
- **A-2 单初值局部搜索边界（探针 P1）**：默认配置 `initialValuesCount=1`（`AnalysisConfig.hpp:145`）下，平面 2R 目标 (0,1.2,0) 单零初值 PositionOnly 收敛到限位外分支、被硬过滤②诚实拒绝（kind=3，无假解、过滤记录如实）；JointGrid×4/16/64 均正确收敛（独立 FK 复验）。属审查 KIN-02 行已登记"局部数值搜索不保证全部解或全局收敛"的既有边界，非 A02 回归（同输入旧机制更差——kind=2 零候选）。
- **A-3** `onSetDefaultTcpClicked` 空 TcpRef（KIN-P4，实施者自报与代码核实一致）；**A-4** A03 +∞ 显示拒绝未动（KIN-P3，gui 闭环载体 twoLink 化已在测试注释与单元卡如实登记）。

## 8. 未验证事项（诚实边界）

- 未重跑全量四套件与全量构建（编排脚本已执行且全绿，按验收指示仅定向复跑——见 §6.2 清单，复跑口径窄于全量，如实声明）。
- 未做 RobWorkStudio 真实主程序人工交互验证（正式宿主 FK/IK/命令缝未装配——审查 §4/KIN-P4 范围）。
- 对抗探针覆盖 A02 任务语义与 A08 装配边界，未覆盖 Qt 渲染/呈现面与跨平台编译。
- 碰撞证据链路仅验证"缺会话→collisionNotEvaluated 诚实标记"（KIN-05 语义），真实④端口会话组装归 KIN-P4。

## 9. 裁定

FAIL——B-1 修复（文档行更正或 Envelope 补迁移，其一即可）并重新推送后，按协议对 B-1 复核即可（代码面无需重验；若选择补做 Envelope 迁移则需按本记录 §3/§4 口径对 Envelope 通道另行验证）。
