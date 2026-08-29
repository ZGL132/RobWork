# 轨迹规划模块详细方案（trajectory-planning）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Accepted（IRD-D10-20260829 联合评审通过）
- 负责 WP：WP-16；阶段/发布：阶段 C / R1；任务卡：agent-tasks/WP-16-T01～T06
- 架构契约：`architecture/public-interfaces.md` §3/§7、`architecture/evaluation-semantics.md` §1～2、`architecture/execution-model.md` §1～3、`architecture/canonical-kinematics.md`、`architecture/testing-contract.md`
- 需求锚点：requirements §8.4（TRJ-01～08）、§7.2/§7.4、§15.3（轨迹限制行、碰撞验证协议、Jacobian 统一尺度）；平台方案：policy-collision、execution-platform、snapshot-result、runtime-model、session-ui
- 代码前置：WP-07、08、14、15（总纲 §5.3；构建/门禁入口由 WP-01 交付）

## 1. 模块职责

实现轨迹评估器（`IEngineeringEvaluator` 的本域唯一实现，端口头位于 evidence/）：从任务序列（`EngineeringRequirements`＋`LoadCase`）与上游 `KinematicResult` 的 IK 候选生成关节空间 PTP 段（TRJ-01）、笛卡尔直线接近/撤离段（TRJ-02）与驻留段，接入 RobWork 规划器完成避障路径搜索（TRJ-03），执行路径简化、平滑与限值守恒的时间参数化（TRJ-04/05），复检复用 WP-07 共享 `CollisionEvaluator`（TRJ-04），输出 `TrajectoryPlan`（含 `ResolvedIkBranchSequence`）、分段诊断与节拍（TRJ-06）。本模块拥有公共领域类型 `TrajectoryPlan`、`ResolvedIkBranchSequence`、`IkBranchPolicy`（§7.2）。非目标：IK 求解（WP-15）、碰撞策略与算法（WP-07）、播放/动画/曲线查看会话态（TRJ-07，归 WP-10 session-ui）、jerk 上限与工艺速度（TRJ-08 P1，仅保持 Schema 与评估接口可扩展）、动力学计算（WP-17）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/
  include/sdurws/ird/trajectory/
    TrajectoryPlan.hpp   ResolvedIkBranchSequence.hpp   IkBranchPolicy.hpp
    TrajectoryEvaluator.hpp   TrajectoryDiagnostics.hpp
  src/TrajectoryEvaluator.cpp   PtpCartesianPlanner.cpp   PlannerAdapter.cpp
      PathSimplifier.cpp   TimeParameterizer.cpp   LimitVerifier.cpp
      IkContinuityChecker.cpp   TrajectoryJson.cpp
  test/PtpCartesianTest.cpp   PlannerAdapterTest.cpp   SmoothingTimeTest.cpp
      CollisionLimitsTest.cpp   TrajectoryResultTest.cpp   LifecycleTest.cpp
  testdata/trajectory/{ptp,cartesian,planner,collision,golden,failpoints}/
  # 证据 → out/test-evidence/wp-16/<run-id>/（AGENTS §3，不入源码树）
```

CMake target：`sdurws_ird_trajectory`、`sdurws_ird_trajectory_test`、`sdurws_ird_trajectory_contract_test`。允许依赖：WP-03 core、WP-05 evidence（评估端口头；经 `IResultRepository` 按 `ResultRef` 读上游 payload）、WP-06 runtime（`CompiledRobotArtifacts`）、WP-07 policy（共享 `CollisionEvaluator`，代码依赖）、RobWork pathplanning/trajectory/proximity 稳定 API、Qt Core（`QJson*`）；契约引用（只含公共领域类型头，不链接实现）：WP-14 `EngineeringRequirements`/`LoadCase`、WP-15 `KinematicResult`；调度经 WP-08 装配（契约引用）。禁止：Qt Widgets、其他插件私有头、本地碰撞开关/采样参数/安全距离副本、直接文件 IO、读取 UI 会话态、第二套 `TrajectoryPlan` DTO。

## 3. 数据与接口

`TrajectoryPlan` 内部结构（模块冻结，对象 Schema 版本 1；§7.2 语义总纲"路点、插值、路径、时间参数、速度/加速度和节拍"）：

| 成员 | 内容 | 规则 |
| --- | --- | --- |
| `segments[]` | `kind ∈ {JointPtp, CartesianLine, Dwell}`；`CartesianArc` 为已登记枚举值，产生该类段属 TRJ-08 后续交付 | 每段含路点（关节向量，或 TCP 位姿＋参考系 objectId）、`tStart/tDuration`、来源任务点引用；驻留段携带持续时间与 `LoadCase` 工况切换引用 |
| 时间参数 | 节点时刻表、每轴速度/加速度剖面采样、`motionLawId+version`、总节拍 `cycleTime`（含驻留） | 时间戳单调有限；运动律版本与限值来源（`AnalysisConfiguration`）进输入切片 |
| `resolvedIkBranchSequence` | 每路点实际采用 IK 解（q、分支标识、残差、候选 ID） | 记录实际采用解序列（P0 验收）；引用 `KinematicResult` 候选，不复制其完整 payload |

`IkBranchPolicy`（§7.2：自动分支策略、用户显式锁定、连续性阈值）经 `AnalysisConfiguration` 进入输入切片；"用于规划/锁定分支"是产生修订的设计修改，候选双击只预览（TRJ 验收、AT-04）。评估器 `dependencyManifest()` 声明：canonical 物理身份、nameMapId、策略内容身份（含 `pathValidationProfile`）、上游 `KinematicResult` 引用、需求/负载字段、规划器与运动律 `algorithmVersion`＋`randomSeed`（进 `EvaluatorInputSlice`）。输出经 `ResultEnvelope` 填充，合法组合按 evaluation-semantics §2。`TrajectoryPlan` 提供只读采样求值（q/v/a at t）供 WP-17 消费与 WP-10 播放（会话态，不改切片）。

## 4. 调用与状态

```text
snapshot → 校验（上游 KinematicResult 存在且 Current、需求/限值/策略完整）
  → 任务序列分段：PTP / 笛卡尔直线（TRJ-01/02）＋驻留
  → IK 分支解析与连续性检查（§5.3）→ RobWork 规划器避障搜索（TRJ-03，§5.4）
  → 路径简化 → 平滑与时间参数化（§5.1～5.2）→ 限值校验（§15.3 轨迹限制行）
  → WP-07 CollisionEvaluator 协议化复检（TRJ-04）→ TrajectoryPlan＋ResolvedIkBranchSequence
  → WP-05 结果接纳；取消/恢复/迟到回调按 execution-model（安全点＝段边界）
```

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-TRJ-UPSTREAM-MISSING` | 上游 `KinematicResult` 缺失/非 Current/版本不兼容 | Input | Error | 先完成或重算运动学 |
| `IRD-TRJ-NO-PATH` | 起终点不可行或无碰路径不存在（TRJ-06，附段与端点） | Engineering | Warning | 调整任务点、策略或锁定分支后重规划 |
| `IRD-TRJ-PLANNER-TIMEOUT` | 规划超时（预算来自 resourceBudget） | Engineering | Error | 提高超时预算或增设中间引导点 |
| `IRD-TRJ-BRANCH-JUMP` | 相邻采样 IK 解差超连续性阈值（附段、点对、差值） | Engineering | Warning | 锁定分支（产生修订）或调整路径 |
| `IRD-TRJ-SINGULARITY` | 笛卡尔段采样点 `J_norm` 条件数超阈值（默认 100，可评审） | Engineering | Warning | 改接近方向或降速；不阻断生成 |
| `IRD-TRJ-TIME-PARAM-FAILED` | 时间参数化迭代上限（32）内未满足限值 | Engineering | Error | 放宽节拍或限值后重算 |
| `IRD-TRJ-VALIDATION-REJECTED` | 平滑后复检发现碰撞/限制超标（透传 WP-07 证据） | Engineering | Warning | 按碰撞对象与段修正后重规划 |
| `IRD-TRJ-PLANNER-FAILED` | 规划器/适配层系统故障 | System | Error | 保留旧结果；按诊断重试 |

## 5. 关键实现约定

1. **插值与时间参数化算法选型（裁决）**：采用"同步梯形速度剖面＋五次样条平滑"自研时间参数化；RobWork 只复用几何插值（`LinearInterpolator`/`CircularInterpolator`）、轨迹容器与 TRJ-03 路径规划器。理由：RobWork `ParabolicBlend` 系运动律仅速度连续，不满足 §15.3"P0 运动律至少加速度连续"；`CubicSplineFactory` 无限值守恒与迭代语义，边界条件与节拍口径仍需本模块拥有；梯形剖面给出各段时间下界（多轴取最慢轴同步缩放），五次样条（节点给定位移/速度/加速度边界，C²）保证加速度连续且可解析复核；TRJ-08 jerk 扩展可在同一结构上升级运动律而不改 Schema。
2. **时间参数化流程（冻结）**：段几何度量（关节空间范数/笛卡尔弧长）→ 梯形剖面求节点时刻 → 五次样条重构连续运动律 → 按 §15.3 轨迹限制行校验（位置/速度/加速度相对限值超差 ≤1e-6）→ 违规按固定因子 1.1 全局延长时间并重构（上限 32 次，超限 `IRD-TRJ-TIME-PARAM-FAILED`）。笛卡尔段先几何插值再离散转关节空间做时间参数化，同时校验笛卡尔速度/加速度限值。
3. **IK 连续性（TRJ-02，冻结）**：笛卡尔段按关节步长采样（转动 0.05 rad、移动 0.01 m，与 §15.3 细分步长同级）逐点以上一解为初值求 IK；相邻解逐轴差超 `IkBranchPolicy` 连续性阈值（模块私有默认 0.05 rad/0.01 m，可评审）判分支跳变；实际采用序列写入 `resolvedIkBranchSequence`。奇异邻域按 `J_norm`（§15.3 统一尺度）条件数诊断。
4. **避障与复检（TRJ-03/04，引用协议）**：`PlannerAdapter`（RobWork rwlibs/pathplanners；版本/参数/种子/失败段进快照）构造约束只经 WP-07 从 `CollisionPolicy` 的投影；平滑后复检调用同一共享 `CollisionEvaluator` 与 `pathValidationProfile`（§15.3 冻结协议：自适应细分、结论措辞"在本策略与分辨率下未发现碰撞"）；本模块不得覆盖启用状态、配对、安全距离或分辨率。
5. **路径简化（TRJ-04）**：剔除零位移段并合并关节空间共线路点（直线偏差容差 1e-6 rad/m，模块私有可评审）；简化与平滑不改变任务点语义，路点保留来源引用。
6. **确定性与会话边界**：固定输入切片、规划器版本、种子与线程数时，路点集合、段序、节点时刻与诊断顺序逐字节一致；播放/jog 只经只读求值接口（WP-10 会话态），不产生修订、不触发失效。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| PtpCartesianTest | TRJ-01/02 段生成、路点/时间戳/单位、驻留段、缺上游与非法输入诊断 |
| PlannerAdapterTest | 规划器版本/参数/种子入快照、失败段定位、超时诊断 |
| SmoothingTimeTest | C² 连续性（节点加速度一致）、限值超差 ≤1e-6 相对、迭代收敛、黄金时间参数复算 |
| CollisionLimitsTest | 复检走共享评估器、三入口一致（AT-19）、验证不足＝DataInsufficient、措辞固定 |
| TrajectoryResultTest | `ResolvedIkBranchSequence` 记录、payload 完整性、快照身份、双击不产生修订 |
| LifecycleTest | 取消/恢复（安全点＝段）、迟到回调只追加原分支、确定性重复运行 |

GUI（TRJ-07 曲线查看/动画）归 WP-10/WP-22 会话态，按 `QT_QPA_PLATFORM=windows` 一次一个执行；本模块测试均为 `QCoreApplication` 模型测试。证据写入 `out/test-evidence/wp-16/<run-id>/`：路径黄金数据（WP-02 版本/哈希）、平滑前后碰撞报告、时间参数报告、AT-06/AT-19 记录、独立评审签名。验证命令（双形式，仓库根执行）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory(_contract)?_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| RobWork 规划器既有调用链与参数 | 迁移（§13.2 纯计算部分），经 `PlannerAdapter` 只读对照 | 路径黄金数据一致后切换 |
| 旧插件播放/轨迹导出适配 | 删除（Rewrite）；播放归 WP-10 会话态 | AT-04/AT-06 通过且输出可被 WP-17 复算 |
| 插件私有路径验证参数与碰撞开关副本 | 删除 | WP-07 静态扫描零命中 |
| 无法证明来源的旧轨迹结果 | EvidenceOnly | 评审记录在案 |
