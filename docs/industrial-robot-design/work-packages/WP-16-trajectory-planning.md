# WP-16 轨迹规划实施计划

> 阶段/发布：阶段 C / R1；方案对齐 `module-design/trajectory-planning.md` v0.3（本模块唯一权威，本文只做实施深化，不复述其冻结语义）；架构检查点 `IRD-D2-20260829`；需求基线 v0.8。
> 实现者、独立验证者与独立评审者必须是不同执行上下文（总纲 §4.1）；构建/门禁入口由 WP-01 交付。

**需求与契约：** TRJ-01～08、AT-04/06/18/19（阶段 C 链路子集）；架构契约与模块方案清单见 §2。
**拥有目录：** `industrialrobot/plugins/trajectory/` 及其测试（文件树见 §3）。
**输入/输出：** 输入＝任务序列（`EngineeringRequirements`＋`LoadCase`）＋上游 `KinematicResult` 候选＋策略/限值切片；输出＝`TrajectoryPlan`＋`ResolvedIkBranchSequence`＋分段诊断与节拍证据（见 §4）。

## 1. 目标与非目标

**目标：** 实现 `IEngineeringEvaluator` 在轨迹域的唯一实现 `TrajectoryEvaluator`：从任务序列与上游 `KinematicResult` 的 IK 候选生成关节空间 PTP 段、笛卡尔直线接近/撤离段与驻留段（TRJ-01/02）；接入 RobWork 规划器完成避障搜索（TRJ-03）；执行路径简化、平滑与限值守恒的时间参数化（TRJ-04/05），复检复用 WP-07 共享 `CollisionEvaluator`；输出含 `ResolvedIkBranchSequence` 的 `TrajectoryPlan`、分段诊断与节拍（TRJ-06），供 WP-17 复算与 WP-10 播放。
- 目标交付：`sdurws_ird_trajectory` 及其模型/契约测试、路径黄金数据、时间参数报告、AT-06/AT-19 证据。
- 完成定义：TRJ-01～06 P0 全部通过；轨迹输出可被 WP-17 逐采样复算；固定输入切片/规划器版本/种子/线程数时结果逐字节确定。

**非目标：** IK 求解（WP-15）、碰撞策略与算法（WP-07）、播放/动画/曲线查看会话态（TRJ-07，归 WP-10 session-ui 与 WP-22）、jerk 上限与工艺速度（TRJ-08 P1，仅保持 Schema 与评估接口可扩展，不建空占位）、动力学计算（WP-17）、第二套 `TrajectoryPlan` DTO。

## 2. 需求、契约与发布切片

- 需求：TRJ-01～08；§7.2（`TrajectoryPlan`/`ResolvedIkBranchSequence`/`IkBranchPolicy` 语义）、§7.4（依赖与失效）、§15.3（轨迹限制行、碰撞验证协议、Jacobian 统一尺度）；AT-04（候选双击只预览）、AT-06、AT-19（三入口碰撞一致，阶段 C 优化入口为 WP-20 静态链路）、AT-18 阶段 C 链路子集。
- 架构契约：`architecture/public-interfaces.md` §3/§7、`architecture/evaluation-semantics.md` §1～2（合法组合，本文不复制）、`architecture/execution-model.md` §1～3（取消/迟到回调/输入切片）、`architecture/canonical-kinematics.md`、`architecture/testing-contract.md`。
- 平台方案：policy-collision（共享 `CollisionEvaluator` 与 `pathValidationProfile`）、execution-platform、snapshot-result、runtime-model、session-ui（均引用，不复述）。
- 发布切片：阶段 C 形成 R1；不以 TRJ-07/08 P1 能力作为退出条件。

## 3. 文件所有权与 CMake 目标

拥有目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`，子目录 `include/sdurws/ird/trajectory/`（TrajectoryPlan.hpp、ResolvedIkBranchSequence.hpp、IkBranchPolicy.hpp、TrajectoryEvaluator.hpp、TrajectoryDiagnostics.hpp）、`src/`（TrajectoryEvaluator.cpp、PtpCartesianPlanner.cpp、PlannerAdapter.cpp、PathSimplifier.cpp、TimeParameterizer.cpp、LimitVerifier.cpp、IkContinuityChecker.cpp、TrajectoryJson.cpp）、`test/`（PtpCartesianTest.cpp、PlannerAdapterTest.cpp、SmoothingTimeTest.cpp、CollisionLimitsTest.cpp、TrajectoryResultTest.cpp、LifecycleTest.cpp）、`testdata/trajectory/{ptp,cartesian,planner,collision,golden,failpoints}/`、`out/test-evidence/wp-16/<run-id>/`。文件树以模块详设 §2 为权威。

CMake 目标：`sdurws_ird_trajectory`、`sdurws_ird_trajectory_test`、`sdurws_ird_trajectory_contract_test`。允许依赖：WP-03 core、WP-05 evidence（评估端口头；经 `IResultRepository` 按 `ResultRef` 读上游 payload）、WP-06 runtime（`CompiledRobotArtifacts`）、WP-07 policy（共享 `CollisionEvaluator`，代码依赖）、RobWork pathplanning/trajectory/proximity 稳定 API、Qt Core（`QJson*`）；契约引用（只含公共领域类型头，不链接实现）：WP-14 `EngineeringRequirements`/`LoadCase`、WP-15 `KinematicResult`；调度经 WP-08 装配（契约引用）。禁止：Qt Widgets、其他插件私有头、本地碰撞开关/采样参数/安全距离副本、直接文件 IO、读取 UI 会话态、第二套 `TrajectoryPlan` DTO。

## 4. 输入/输出与数据流

- 输入：`AnalysisSnapshot`（canonical 物理身份、nameMapId、策略内容身份含 `pathValidationProfile`、上游 `KinematicResult` 引用、需求/负载字段、规划器与运动律 `algorithmVersion`＋`randomSeed`，经 `dependencyManifest()` 声明进 `EvaluatorInputSlice`）；`IkBranchPolicy`（自动分支策略、用户显式锁定、连续性阈值）经 `AnalysisConfiguration` 进入输入切片。
- 输出：`TrajectoryPlan`（对象 Schema 版本 1：`segments[]`，`kind ∈ {JointPtp, CartesianLine, Dwell}`，`CartesianArc` 为已登记枚举值、产生该类段属 TRJ-08 后续交付；每段含路点、`tStart/tDuration`、来源任务点引用；时间参数含节点时刻表、每轴速度/加速度剖面采样、`motionLawId+version`、含驻留总节拍 `cycleTime`）、`ResolvedIkBranchSequence`（每路点实际采用 IK 解：q、分支标识、残差、候选 ID）＋分段诊断。只读采样求值（q/v/a at t）供 WP-17 消费与 WP-10 播放（会话态，不改切片）。
- 主数据流（模块详设 §4）：snapshot → 校验（上游 KinematicResult 存在且 Current、需求/限值/策略完整）→ 任务序列分段 → IK 分支解析与连续性检查 → RobWork 规划器避障搜索 → 路径简化 → 平滑与时间参数化 → 限值校验 → 共享 `CollisionEvaluator` 协议化复检 → `TrajectoryPlan`＋`ResolvedIkBranchSequence` → WP-05 结果接纳；取消/恢复/迟到回调按执行模型，安全点＝段边界。
- 错误面（模块详设 §4 表，此处只列码）：`IRD-TRJ-UPSTREAM-MISSING`、`IRD-TRJ-NO-PATH`、`IRD-TRJ-PLANNER-TIMEOUT`、`IRD-TRJ-BRANCH-JUMP`、`IRD-TRJ-SINGULARITY`（Warning，默认条件数阈值 100 可评审，不阻断）、`IRD-TRJ-TIME-PARAM-FAILED`、`IRD-TRJ-VALIDATION-REJECTED`、`IRD-TRJ-PLANNER-FAILED`。

## 5. 冻结算法裁决（引用模块详设 §5，实施不得偏离）

1. **时间参数化选型**："同步梯形速度剖面＋五次样条平滑"自研；RobWork 只复用几何插值（`LinearInterpolator`/`CircularInterpolator`）、轨迹容器与 TRJ-03 规划器。理由：`ParabolicBlend` 系运动律仅速度连续，不满足 §15.3"P0 运动律至少加速度连续"；`CubicSplineFactory` 无限值守恒与迭代语义；梯形剖面给各段时间下界（多轴取最慢轴同步缩放），五次样条（节点给定位移/速度/加速度边界，C²）保证加速度连续且可解析复核。
2. **时间参数化流程**：段几何度量 → 梯形剖面求节点时刻 → 五次样条重构连续运动律 → 按 §15.3 轨迹限制行校验（位置/速度/加速度相对限值超差 ≤1e-6）→ 违规按固定因子 1.1 全局延长时间并重构（上限 32 次，超限 `IRD-TRJ-TIME-PARAM-FAILED`）；笛卡尔段先几何插值再离散转关节空间，同时校验笛卡尔速度/加速度限值。
3. **IK 连续性**：笛卡尔段按关节步长采样（转动 0.05 rad、移动 0.01 m）逐点以上一解为初值求 IK；相邻解逐轴差超 `IkBranchPolicy` 连续性阈值（模块私有默认 0.05 rad/0.01 m，可评审）判分支跳变；奇异邻域按 `J_norm`（§15.3 统一尺度）条件数诊断。
4. **避障与复检**：`PlannerAdapter` 构造约束只经 WP-07 从 `CollisionPolicy` 的投影；平滑后复检调用同一共享 `CollisionEvaluator` 与 `pathValidationProfile`（§15.3 冻结协议：自适应细分、结论措辞"在本策略与分辨率下未发现碰撞"）；本模块不得覆盖启用状态、配对、安全距离或分辨率。
5. **路径简化**：剔除零位移段并合并关节空间共线路点（直线偏差容差 1e-6 rad/m，模块私有可评审）；不改任务点语义，路点保留来源引用。
6. **确定性**：固定输入切片、规划器版本、种子与线程数时，路点集合、段序、节点时刻与诊断顺序逐字节一致；播放/jog 只经只读求值接口。

## 6. 任务依赖 DAG

```text
WP-16-T01 → WP-16-T02 → WP-16-T03 → WP-16-T04
WP-16-T01～T04 全部完成 → WP-16-T05 → WP-16-T06
```

## 7. 逐任务深化

### WP-16-T01 PTP 与笛卡尔接近撤离
- 代码范围：`include/sdurws/ird/trajectory/IkBranchPolicy.hpp`、`TrajectoryDiagnostics.hpp`（段级诊断骨架）、`TrajectoryEvaluator.hpp/.cpp`（端口骨架与上游校验）、`src/PtpCartesianPlanner.cpp`、`src/IkContinuityChecker.cpp`；`test/PtpCartesianTest.cpp`；`testdata/trajectory/{ptp,cartesian}/`。
- 前置任务：无（包内首任务；外部前置 WP-07、08、14、15 由总纲 §5.3 规定）。
- 输出工件：段类型 Schema v1 生成器（JointPtp/CartesianLine/Dwell，驻留段携带持续时间与 `LoadCase` 工况切换引用）、`IkBranchPolicy` 公共类型、IK 连续性检查器及诊断。
- 验收断言：`PtpCartesianTest`（模块详设 §6）——TRJ-01/02 段生成、路点/时间戳/单位、驻留段、缺上游与非法输入诊断；上游缺失/非 Current → `IRD-TRJ-UPSTREAM-MISSING`；分支跳变 → `IRD-TRJ-BRANCH-JUMP`（附段、点对、差值）；奇异 → `IRD-TRJ-SINGULARITY`（`J_norm` 条件数阈值 100，Warning 不阻断）。

### WP-16-T02 RobWork 规划器适配
- 代码范围：`src/PlannerAdapter.cpp`；`test/PlannerAdapterTest.cpp`；`testdata/trajectory/planner/`。
- 前置任务：WP-16-T01。
- 输出工件：`PlannerAdapter`（RobWork rwlibs/pathplanners 稳定 API 适配；版本/参数/种子/失败段进快照），约束构造只经 WP-07 `CollisionPolicy` 投影。
- 验收断言：`PlannerAdapterTest`（模块详设 §6）——规划器版本/参数/种子入快照、失败段定位、超时诊断；无碰路径不存在 → `IRD-TRJ-NO-PATH`（附段与端点）；超时（预算来自 resourceBudget）→ `IRD-TRJ-PLANNER-TIMEOUT`；系统故障 → `IRD-TRJ-PLANNER-FAILED`；本地无碰撞参数副本（静态检查）。

### WP-16-T03 路径平滑与时间参数化
- 代码范围：`src/PathSimplifier.cpp`、`src/TimeParameterizer.cpp`；`test/SmoothingTimeTest.cpp`；`testdata/trajectory/golden/`。
- 前置任务：WP-16-T01、WP-16-T02。
- 输出工件：路径简化器（零位移剔除、共线合并，容差 1e-6 rad/m）、同步梯形＋五次样条时间参数化器（含 1.1 延长因子与 32 次迭代上限）、黄金时间参数夹具（WP-02 登记版本/哈希）。
- 验收断言：`SmoothingTimeTest`（模块详设 §6）——C² 连续性（节点加速度一致）、限值超差 ≤1e-6 相对（§15.3 轨迹限制行）、迭代收敛（32 次内；超限 → `IRD-TRJ-TIME-PARAM-FAILED`）、黄金时间参数复算；简化不改变任务点语义。

### WP-16-T04 轨迹碰撞与运动限制复检
- 代码范围：`src/LimitVerifier.cpp`；`test/CollisionLimitsTest.cpp`；`testdata/trajectory/{collision,failpoints}/`。
- 前置任务：WP-16-T03。
- 输出工件：限值守恒校验器（位置/速度/加速度，含笛卡尔段速度/加速度限值）＋平滑后协议化复检（走共享 `CollisionEvaluator` 与 `pathValidationProfile`，不覆盖任何策略项）。
- 验收断言：`CollisionLimitsTest`（模块详设 §6）——复检走共享评估器、三入口一致（AT-19）、验证不足＝DataInsufficient、措辞固定"在本策略与分辨率下未发现碰撞"；复检发现碰撞/限制超标 → `IRD-TRJ-VALIDATION-REJECTED`（透传 WP-07 证据）。

### WP-16-T05 轨迹结果与候选预览
- 代码范围：`include/sdurws/ird/trajectory/TrajectoryPlan.hpp`、`ResolvedIkBranchSequence.hpp`、`TrajectoryEvaluator.hpp`（完整装配）；`src/TrajectoryEvaluator.cpp`（评估主流程）、`src/TrajectoryJson.cpp`；`test/TrajectoryResultTest.cpp`。
- 前置任务：WP-16-T01～T04。
- 输出工件：`TrajectoryPlan` 公共领域类型（Schema 版本 1）与只读采样求值、`ResolvedIkBranchSequence` 记录（引用 `KinematicResult` 候选，不复制完整 payload）、`dependencyManifest()` 声明、`ResultEnvelope` 填充（合法组合按 evaluation-semantics §2）、JSON 往返。
- 验收断言：`TrajectoryResultTest`（模块详设 §6）——`ResolvedIkBranchSequence` 记录实际采用解序列（P0 验收）、payload 完整性、快照身份、候选双击只预览不产生修订（AT-04；"用于规划/锁定分支"才产生设计修改修订）。

### WP-16-T06 轨迹任务生命周期
- 代码范围：`src/TrajectoryEvaluator.cpp`（取消/恢复/迟到回调路径）；`test/LifecycleTest.cpp`。
- 前置任务：WP-16-T05。
- 输出工件：WP-08 调度接入（`CancellationToken` 协作取消、安全点＝段边界、`TaskCapabilities` 声明）与确定性重复运行证明。
- 验收断言：`LifecycleTest`（模块详设 §6）——取消/恢复（安全点＝段）、迟到回调只追加原分支不改写终态（执行模型 §1）、确定性重复运行（固定切片/版本/种子/线程数逐字节一致）。

## 8. 测试矩阵（模块详设 §6 为断言权威）

| 测试目标/文件 | 断言要点 | 覆盖需求 |
| --- | --- | --- |
| `sdurws_ird_trajectory_test` / PtpCartesianTest.cpp | 段生成、路点/时间戳/单位、驻留段、缺上游与非法输入诊断 | TRJ-01/02、AT-06 |
| 同上 / PlannerAdapterTest.cpp | 版本/参数/种子入快照、失败段定位、超时诊断 | TRJ-03、AT-06 |
| 同上 / SmoothingTimeTest.cpp | C² 连续性、限值超差 ≤1e-6 相对、1.1 延长因子与 32 次上限、黄金复算 | TRJ-05、§15.3 |
| 同上 / CollisionLimitsTest.cpp | 共享评估器复检、三入口一致（AT-19）、DataInsufficient、措辞固定 | TRJ-04、AT-19 |
| 同上 / TrajectoryResultTest.cpp | `ResolvedIkBranchSequence`、payload 完整性、双击不产生修订 | TRJ-06、AT-04 |
| 同上 / LifecycleTest.cpp | 取消/恢复（安全点＝段）、迟到回调隔离、确定性 | TRJ-01～06、执行模型 |
| `sdurws_ird_trajectory_contract_test` | 评估端口契约（合法组合、取消、进度、能力声明）与上游 payload 只读引用 | public-interfaces §3 |

模型测试均为 `QCoreApplication`；GUI（TRJ-07 曲线查看/动画）归 WP-10/WP-22 会话态，按 `QT_QPA_PLATFORM=windows` 一次一个执行，本包不建 GUI 测试目标。

## 验证命令（双形式，仓库根执行）

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory(_contract)?_test$"
```

## 10. 独立验证与独立评审

- 独立验证（黑盒）：黄金时间参数复算、平滑前后碰撞报告比对、三入口碰撞一致性（AT-19）、取消/迟到事件注入（failpoints 夹具）。
- 独立评审：由规划负责人与独立测试负责人复核段 Schema、时间参数化选型与 1e-6/1.1/32 冻结值、复检协议引用、路径黄金数据与证据签署。
- 证据写入 `out/test-evidence/wp-16/<run-id>/`：路径黄金数据（WP-02 版本/哈希）、平滑前后碰撞报告、时间参数报告、AT-06/AT-19 记录、独立评审签名。

## 11. 迁移与删除（requirements §13）

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| RobWork 规划器既有调用链与参数 | 迁移（§13.2 纯计算部分），经 `PlannerAdapter` 只读对照 | 路径黄金数据一致后切换 |
| 旧插件播放/轨迹导出适配 | 删除（Rewrite）；播放归 WP-10 会话态 | AT-04/AT-06 通过且输出可被 WP-17 复算 |
| 插件私有路径验证参数与碰撞开关副本 | 删除 | WP-07 静态扫描零命中 |
| 无法证明来源的旧轨迹结果 | EvidenceOnly | 评审记录在案 |

## 退出条件

- TRJ-01～06 全部 P0、AT-06、AT-19 与 §15.3 轨迹限制/碰撞协议断言通过；TRJ-07/08 保持可扩展且无空占位。
- `TrajectoryPlan`（Schema 版本 1）＋`ResolvedIkBranchSequence` 为唯一轨迹结果形态，可被 WP-17 逐采样复算、被 WP-10 只读播放。
- 复检只经共享 `CollisionEvaluator` 与 `pathValidationProfile`，无本地策略副本；确定性重复运行成立。
- §11 删除清单执行完毕，旧播放/导出适配退出构建。

## 13. 人周（总纲 §5.3：8～12 人周，含实现/测试/评审/修正）

| 任务 | 人周 |
| --- | ---: |
| WP-16-T01 | 1.5～2 |
| WP-16-T02 | 1～1.5 |
| WP-16-T03 | 2～3 |
| WP-16-T04 | 1～1.5 |
| WP-16-T05 | 1.5～2 |
| WP-16-T06 | 1～2 |

## 任务卡索引

- [WP-16-T01 PTP 与笛卡尔接近撤离](../agent-tasks/WP-16-T01-ptp-cartesian.md)
- [WP-16-T02 RobWork 规划器适配](../agent-tasks/WP-16-T02-planner-adapter.md)
- [WP-16-T03 路径平滑与时间参数化](../agent-tasks/WP-16-T03-smoothing-time.md)
- [WP-16-T04 轨迹碰撞与运动限制](../agent-tasks/WP-16-T04-collision-limits.md)
- [WP-16-T05 轨迹结果与候选预览](../agent-tasks/WP-16-T05-trajectory-result.md)
- [WP-16-T06 轨迹任务生命周期](../agent-tasks/WP-16-T06-lifecycle.md)
