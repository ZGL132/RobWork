# 运动学模块详细方案（kinematics）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（D5 重写，待消费者评审）
- 负责 WP：WP-15；阶段/发布：阶段 B / R1；任务卡：`agent-tasks/WP-15-T01～T08`
- 架构契约：`architecture/canonical-kinematics.md`（最高权威）、`architecture/evaluation-semantics.md` §1～§2、`architecture/public-interfaces.md` §3～§4/§7、`architecture/execution-model.md`、`architecture/symbol-registry.md`
- 代码前置：WP-03、05、06、07、08、09；WP-13、14 为交付前置（模型与需求经快照获得，无业务插件代码依赖，总纲 §5.3）；构建/门禁入口 WP-01
- 需求锚点：§7.3.1、§8.3（KIN-01～08）、§15.3；场景 AT-03～05、AT-18～19 阶段 B 子链路

## 1. 模块职责

实现评估器 `ird.kinematics`（`IEngineeringEvaluator`，evaluatorId 模块冻结）：当前姿态 FK、多初值 IK 与候选排序、Jacobian/奇异性（`J_norm`/`L*`）、任务点批量验证、区域覆盖、碰撞证据（调 WP-07 共享 `CollisionEvaluator`）；结果 payload 为 `KinematicResult`（需求 §7.2 规范名；symbol-registry 补登记提名）。批处理一律经 WP-08 调度并携带完整 `RunIdentity`。非目标：规范模型与名称编译（WP-06）、碰撞策略（WP-07）、调度/缓存/检查点实现（WP-08）、需求语义（WP-14）、轨迹与分支连续性判定（WP-16）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/
  include/sdurws/ird/kinematics/
    KinematicsEvaluator.hpp   FkJacobian.hpp   IkSolver.hpp   CandidateRanking.hpp
    RegionCoverage.hpp   CollisionEvidenceAdapter.hpp   KinematicsSettings.hpp
    KinematicsDiagnostics.hpp
  src/KinematicsEvaluator.cpp   FkJacobian.cpp   IkSolver.cpp   CandidateRanking.cpp
      RegionCoverage.cpp   CollisionEvidenceAdapter.cpp   KinematicsSettings.cpp
  gui/KinematicsPlugin.hpp   gui/KinematicsPlugin.cpp   gui/panels/
  test/FkTest.cpp   IkCandidateTest.cpp   JacobianTest.cpp   RegionCoverageTest.cpp
      CollisionEvidenceTest.cpp   BatchExecutionTest.cpp   CrossEntryTest.cpp   KinematicsGuiTest.cpp
  testdata/kinematics/{fk-golden,ik-golden,jacobian,regions,collisions,batches}/
  evidence/WP-15/
```

CMake target：`sdurws_ird_kinematics`（计算核心，无 Qt Widgets）、`sdurws_ird_kinematics_plugin`（薄插件）、`sdurws_ird_kinematics_test`、`sdurws_ird_kinematics_contract_test`、`sdurws_ird_kinematics_gui_test`。允许依赖：WP-03 core、WP-05 evidence（评估端口头）、WP-06 runtime（canonical 模型/`IRuntimeNameResolver`）、WP-07 policy（`CollisionEvaluator`）、WP-08 execution（调度端口）、WP-09 diagnostics、RobWork 稳定 API（数值 IK/FK/Jacobian）、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui。禁止：本地 CollisionPolicy 或默认值、自建线程池/调度、其他业务插件私有头、读取当前 Widget 状态。

## 3. 数据与接口

评估端口签名与 `ResultEnvelope` 字段以 public-interfaces §3/§7 为准；合法组合以 evaluation-semantics §2 为准。模块私有类型：

| 类型（模块私有） | 字段 | 规则 |
| --- | --- | --- |
| `FkQuery/FkResult` | q（按 qIndex）、tcpFrame；`T_WORLD_tcp`、各 Joint/Link 世界位姿、世界关节轴线 | 解析对照容差 §15.3（TCP 1e-9 m/1e-9 rad） |
| `JacobianResult` | J、`J_norm`、σmin/σmax、条件数、可操作度、`L*` 及来源/回退诊断 | `J_norm=[J_v/L*; J_ω]` 以 §15.3 冻结为准；可操作度＝√det(J_norm·J_normᵀ) |
| `IkCandidate` | q、residual{position m, orientation rad(测地角)}、jointMargin、manipulability、distanceToCurrent、applicable{residual,jointLimit,collision}、stableIndex | 去重＝同解判据：逐轴转动/连续差 ≤1e-6 rad、移动 ≤1e-8 m（§15.3） |
| `CoverageReport` | plannedCombinations、evaluatedCombinations、达标组合数、positionCoverage、orientationCoverage | 分母＝计划的位置—姿态组合（WP-14 冻结定义） |
| `KinematicsSettings` | IK 初值数、迭代上限、内部收敛阈值、区域采样线程数 | 持久化为 `AnalysisConfiguration`（规范名，symbol-registry §4.8）；进入输入切片与缓存键 |

## 4. 调用与状态

批处理时序（固定）：WP-08 `submit(EvaluationRequest)` → worker 消费 `AnalysisSnapshot`（canonical 物理身份＋nameMapId＋policy 内容 ID＋需求切片）→ 批次＝任务点或区域采样组合（安全点＝批次边界）→ FK/IK/排序 → 需要碰撞证据时调 WP-07 → envelope（构造边界先过 §2 合法组合校验）→ WP-05 接纳；迟到回调只追加原分支历史。区域覆盖两锚点（evaluation-semantics §2）：用户取消＝`Canceled + NotEvaluated + Partial/None`；预算耗尽＝`Completed + DataInsufficient + Complete`；两者均不得输出 Verified 通过。错误矩阵（新码待 diagnostics.md 登记）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-KIN-IK-NO-SOLUTION` | 目标无可应用候选（全部初值未收敛或被过滤） | Engineering | Warning | 列出最近残差与失败原因；放宽容差或调整任务定义 |
| `IRD-KIN-JOINT-LIMIT` | 候选关节超限 | Engineering | Warning | 候选标不可应用并保留逐轴实际/限值 |
| `IRD-KIN-COLLIDING` | 要求碰撞证据且发现碰撞 | Engineering | Warning | 报告对象 ID 对与最近距离；结论措辞见 §5 |
| `IRD-KIN-EVIDENCE-MISSING` | 需要碰撞证据但缺碰撞检测器（KIN-05） | Engineering | Warning | 判 DataInsufficient，不得视为无碰撞 |
| `IRD-KIN-LSTAR-INVALID` | `L*` 非有限/非正且无配置回退 | Engineering | Warning | 配置正值回退或输出 DataInsufficient（§15.3） |
| `IRD-KIN-CONTINUOUS-UNBOUNDED` | Continuous 关节未确认工程工作范围，排序归一跳过（§5.1） | Input | Info | 提示确认范围后重评；不阻断评估 |
| `IRD-KIN-SOLVER-FAILED` | RobWork 求解器异常/资源故障 | System | Error | 保留批次与检查点，修复后以新 attempt 重试 |

Engineering 类 KIN 码的 severity 以 `module-design/diagnostics.md` §3 登记表 `severityDefault`（Warning）为准；`severityDefault` 可由 `DiagnosticMapper` 按边界上下文调整。

## 5. 关键实现约定

1. **候选排序键（模块冻结；修复历史缺陷 3627db1——排序只优先"残差小"导致首候选因 JointLimit 不可应用，双击取首候选三维模型不动）**，两阶段：① 过滤——不可应用候选（残差超任务容差 1 mm/1 deg、关节超限、要求碰撞证据时碰撞失败）移出主列表、单列分组并保留逐项原因；② 排序——可应用候选按全序键排列：(a) 关节裕量降序，裕量＝min over joints of min((q−lo)/(hi−lo),(hi−q)/(hi−lo))，转动/移动各按自身半区间归一；(b) 可操作度（基于 `J_norm`）降序；(c) 与当前位姿距离升序，距离＝‖D⁻¹(q−q_cur)‖₂、D＝diag(半区间)；(d) 稳定编号升序，stableIndex＝候选在（初值序，q 规范序列化字典序）确定序中的位置。**Continuous 关节排序口径（冻结）**：用户已确认工程工作范围的 Continuous 关节按该确认范围参与 (a) 裕量与 (c) 距离归一化；未确认范围的 Continuous 关节不参与裕量与距离归一——(a) 中该关节裕量分量记 1.0、(c) 距离项剔除该关节分量，并在 `KinematicResult` 附加 Info 级说明诊断（码 `IRD-KIN-CONTINUOUS-UNBOUNDED`，类别 Input、severity Info，登记 diagnostics.md §3）。固定输入/种子/线程数下排序逐字节稳定。
2. IK 求解（模块冻结）：生产路径＝RobWork 数值 IK（阻尼最小二乘）多初值；初值池按固定次序：q_home → 会话当前 q → 关节区间中点 → 固定种子 Latin 超立方补足至默认 16 个（数量入 `KinematicsSettings`）；任务级收敛判据＝§15.3（位置 1 mm、姿态 1 deg 测地角）；求解器内部步长阈值 1e-10、每初值迭代上限 200。解析 IK 夹具仅用于黄金对照（标准六轴构型解析解集合一致性），不进生产路径。
3. Jacobian：`J_norm` 与 `L*` 规则以 §15.3 为准（`L*` 默认 Zero 位姿基座原点到 TCP 距离；回退须项目配置正值并记诊断，无法确定→DataInsufficient）；4/5 轴任务用受约束分量构成的任务子空间 Jacobian（分量由 REQ-01 声明），可操作度/条件数一律基于（任务）`J_norm`。
4. 区域覆盖：分母＝计划的位置—姿态组合；网格含边界；位置/姿态覆盖率分别计算（模块冻结口径：位置覆盖＝存在至少一个达标姿态组合的采样位置占比，姿态覆盖＝存在至少一个达标位置组合的采样姿态占比；组合级统计同时保留）；Verified 区域每空间轴至少两个样本（§15.3）。
5. 碰撞证据：只调 WP-07 `CollisionEvaluator`（同一快照/策略）；结论措辞冻结"在本策略与分辨率下未发现碰撞"；距离查询不可用不推断安全。AT-19：与 WP-20 入口返回完全相同的对象 ID 对、判定与原因（T08）。
6. 批处理与会话：取消/检查点/缓存按 WP-08 契约；双击任务/候选只改会话姿态（KIN-06），"用于规划/锁定分支"才经 WP-04 命令应用 `IkBranchPolicy`；导出引用快照而非当前 Widget。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| FkTest | Zero/Home/边界/固定种子姿态 FK 解析对照（1e-9）；引用失败阻止求值 |
| IkCandidateTest | 多初值收敛、残差/限位过滤、去重（1e-6 rad/1e-8 m）、排序键全序与稳定性、3627db1 回归夹具 |
| JacobianTest | `J_norm`/`L*`/回退/DataInsufficient、任务子空间、解析对照 1e-6 |
| RegionCoverageTest | 分母/边界、取消与预算耗尽两锚点、Partial 不得 Verified |
| CollisionEvidenceTest | 共享评估器一致性、缺检测器 DataInsufficient、限定措辞 |
| BatchExecutionTest | 身份校验、迟到回调、取消/检查点、固定种子复现 |
| CrossEntryTest | AT-19 静态入口：对象 ID 对/判定/原因一致；显示开关不影响判定 |
| KinematicsGuiTest | 候选预览不建修订、显式应用、导出引用快照（QT_QPA_PLATFORM=windows） |

证据写入 `evidence/WP-15/`：FK/IK/Jacobian 解析对照报告、覆盖率矩阵、排序稳定性报告、AT-18/19 阶段 B 记录与独立评审签名。验证命令（双形式，仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_gui_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `sdurws_kinematicanalysis` 主链路（TargetEvaluator 等） | 只读黄金对照后 Rewrite | FK/IK/排序黄金数据通过后切换 |
| 3627db1 修复的候选排序逻辑 | 迁移：行为清单＋排序键回归夹具固定 | 排序稳定性报告 |
| 插件内重复碰撞适配/开关 | 删除，统一 WP-07 共享评估器 | AT-19 一致性门禁 |
| 旧目标 `sdurws_kinematicanalysis` | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |
