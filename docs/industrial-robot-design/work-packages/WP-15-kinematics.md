# WP-15 运动学实施计划

> 阶段/发布：阶段 B / R1（阶段 B 只交付模型—需求—运动学—静态优化链路）；负责 WP：WP-15。
> 实施语义唯一来源：`module-design/kinematics.md` v0.3（需求基线 v0.7；检查点 `IRD-D2-20260829`）。
> 前置（总纲 §5.3，保持不变）：WP-07、WP-08、WP-13、WP-14。人周：7～10。
> 模块详设补充（不改总纲口径）：代码前置为 WP-03、05、06、07、08、09（WP-03/05/06/09 经 WP-07/08 平台交付传递）；WP-13、WP-14 为交付前置——模型与需求经快照获得，无业务插件代码依赖。
> 治理状态：Planned（D6 深化重写；需求、架构契约与模块详设均处 Proposed 时不得进入实现）。

**需求与契约：** KIN-01～08、AT-03～05/18/19（阶段 B 链路）；清单见 §2。  
**拥有目录：** `industrialrobot/plugins/kinematics/` 及其测试（文件树见 §3）。  
**输入/输出：** 输入＝规范模型/`RuntimeNameMap`/需求/策略切片；输出＝`KinematicResult`（FK/IK/Jacobian/覆盖/碰撞证据）（见 §4）。

## 1. 目标与非目标

**目标**

- 实现评估器 `ird.kinematics`（`IEngineeringEvaluator`，evaluatorId 模块冻结，kinematics.md §1）：当前姿态 FK、多初值 IK 与候选排序、Jacobian/奇异性（`J_norm`/`L*`）、任务点批量验证、区域覆盖、碰撞证据（调 WP-07 共享 `CollisionEvaluator`）。
- 结果 payload 为 `KinematicResult`（需求 §7.2 规范名；SYM-KIN-006，symbol-registry 补登记提名）；批处理一律经 WP-08 调度并携带完整 `RunIdentity`。
- 完成定义：KIN-01～08 全部有测试与证据；固定输入/种子/线程数下输出与排序逐字节稳定；容差满足需求 §15.3。

**非目标**

- 规范模型与名称编译（WP-06）、碰撞策略（WP-07）、调度/缓存/检查点实现（WP-08）、需求语义（WP-14）、轨迹与分支连续性判定（WP-16）。
- 本地 CollisionPolicy 或默认值、自建线程池/调度、读取当前 Widget 状态（kinematics.md §2 禁止项）。

## 2. 需求、契约与发布切片

- 需求锚点（kinematics.md §0）：§7.3.1、§8.3（KIN-01～08）、§15.3；场景 AT-03～05、AT-18～19 阶段 B 子链路。
- 架构契约：`architecture/canonical-kinematics.md`（最高权威）、`architecture/evaluation-semantics.md` §1～§2、`architecture/public-interfaces.md` §3～§4/§7、`architecture/execution-model.md`、`architecture/symbol-registry.md`。
- 代码前置：WP-03 core、WP-05 evidence（评估端口头）、WP-06 runtime（canonical 模型/`IRuntimeNameResolver`）、WP-07 policy（`CollisionEvaluator`）、WP-08 execution（调度端口）、WP-09 diagnostics、RobWork 稳定 API（数值 IK/FK/Jacobian）、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui；构建/门禁入口 WP-01。
- 发布切片：八项任务全部属阶段 B / R1；阶段 C/D 无本 WP 新增范围。

## 3. 拥有目录、CMake 目标与依赖边界

拥有目录（kinematics.md §2 文件树，唯一允许修改范围）：

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

CMake 目标（与模块详设 v0.3 完全一致，不得增删改名）：`sdurws_ird_kinematics`（计算核心，无 Qt Widgets）、`sdurws_ird_kinematics_plugin`（薄插件）、`sdurws_ird_kinematics_test`、`sdurws_ird_kinematics_contract_test`、`sdurws_ird_kinematics_gui_test`。

碰撞规则只调用 WP-07 共享 `CollisionEvaluator`，名称只调用 WP-06 `IRuntimeNameResolver`；禁止依赖其他业务插件私有头。

## 4. 输入、输出与固定时序

| 方向 | 工件 |
| --- | --- |
| 输入 | `AnalysisSnapshot`（canonical 物理身份＋nameMapId＋策略内容 ID＋需求切片）、`CollisionPolicy`（经快照）、`KinematicsSettings`（IK 初值数、迭代上限、内部收敛阈值、区域采样线程数；持久化为 `AnalysisConfiguration`，SYM-DOM-009，symbol-registry §4.8 裁决 8） |
| 输出 | `ResultEnvelope`（payload＝`KinematicResult`，SYM-KIN-006）、`IkCandidate` 列表（含排序）、`CoverageReport`、`EvidenceBundle`；经 WP-05 仓库接纳 |

批处理固定时序（kinematics.md §4，不得重排）：WP-08 `submit(EvaluationRequest)` → worker 消费 `AnalysisSnapshot` → 批次＝任务点或区域采样组合（安全点＝批次边界）→ FK/IK/排序 → 需要碰撞证据时调 WP-07 → envelope（构造边界先过 evaluation-semantics §2 合法组合校验）→ WP-05 接纳；迟到回调只追加原分支历史。

区域覆盖两锚点（evaluation-semantics §2）：用户取消＝`Canceled + NotEvaluated + Partial/None`；预算耗尽＝`Completed + DataInsufficient + Complete`；两者均不得输出 Verified 通过。

失败分类与错误码（kinematics.md §4 矩阵；新码待 diagnostics.md 登记后启用）：`IRD-KIN-IK-NO-SOLUTION`（Engineering/Error）、`IRD-KIN-JOINT-LIMIT`（Engineering/Error）、`IRD-KIN-COLLIDING`（Engineering/Error）、`IRD-KIN-EVIDENCE-MISSING`（Engineering/Error，KIN-05：缺碰撞检测器判 DataInsufficient，不得视为无碰撞）、`IRD-KIN-LSTAR-INVALID`（Engineering/Error）、`IRD-KIN-SOLVER-FAILED`（System/Error，保留批次与检查点，修复后新 attempt 重试）。

## 5. 任务 DAG

```text
T01 FK 与规范模型 ─┬→ T02 IK 候选 ─┬→ T04 区域覆盖 ──┐
                   │               └→ T05 碰撞证据 ──┼→ T06 批处理执行 → T07 应用与 GUI
                   └→ T03 Jacobian 与奇异性 ─────────┘         └────→ T08 契约回归
```

| 任务 | WP 内前置 | 外部门禁 |
| --- | --- | --- |
| T01 | — | WP-06 canonical 工件、WP-13 交付前置 |
| T02 | T01 | WP-14 需求切片（交付前置） |
| T03 | T01 | — |
| T04 | T02 | WP-14 冻结分母 |
| T05 | T01、T02 | WP-07 `CollisionEvaluator` |
| T06 | T02、T04、T05 | WP-08 调度端口、WP-05 评估端口 |
| T07 | T06 | WP-10 公共组件、WP-04 命令端口 |
| T08 | T06、T07 | WP-20-T08 对侧联调 |

每任务一张任务卡、一个 worktree/分支/提交（总纲 §4.3）。

## 6. 逐任务计划

### 6.1 WP-15-T01 FK 与规范模型（1～1.5 人周）

- 代码范围：`src/FkJacobian.cpp`（FK 部分）＋`include/.../FkJacobian.hpp`；`test/FkTest.cpp`；`testdata/kinematics/fk-golden/`。
- 前置：无 WP 内前置；WP-06 编译工件与 WP-13 模型交付可用。
- 输出工件：`FkQuery/FkResult`（q 按 qIndex、tcpFrame；`T_WORLD_tcp`、各 Joint/Link 世界位姿、世界关节轴线），接入 `RuntimeNameMap` 编译工件。
- 验收断言：kinematics.md §6「FkTest」——Zero/Home/边界/固定种子姿态 FK 解析对照（§15.3：TCP 1e-9 m/1e-9 rad）；引用失败必须阻止求值（名称/引用不可解析即失败，不取默认）。

### 6.2 WP-15-T02 IK 候选（2～2.5 人周，本 WP 最重任务）

- 代码范围：`src/IkSolver.cpp`＋`src/CandidateRanking.cpp`（含同名公共头）；`test/IkCandidateTest.cpp`；`testdata/kinematics/ik-golden/`。
- 前置：T01；WP-14 需求切片（容差与受约束分量）。
- 输出工件：`IkCandidate`（q、residual{position m, orientation rad(测地角)}、jointMargin、manipulability、distanceToCurrent、applicable{residual,jointLimit,collision}、stableIndex）。
- 验收断言：§6「IkCandidateTest」——多初值收敛、残差/限位过滤、去重（同解判据：逐轴转动/连续差 ≤1e-6 rad、移动 ≤1e-8 m，§15.3）、排序键全序与稳定性、**3627db1 回归夹具**（历史缺陷：排序只优先"残差小"导致首候选因 JointLimit 不可应用、双击取首候选三维模型不动——不得复现）。
- **排序键（模块冻结，kinematics.md §5.1，两阶段）**：
  1. 过滤——不可应用候选（残差超任务容差 1 mm/1 deg、关节超限、要求碰撞证据时碰撞失败）移出主列表、单列分组并保留逐项原因；
  2. 排序——可应用候选按全序键排列：(a) 关节裕量降序（裕量＝min over joints of min((q−lo)/(hi−lo),(hi−q)/(hi−lo))，转动/移动各按自身半区间归一）；(b) 可操作度（基于 `J_norm`）降序；(c) 与当前位姿距离升序（距离＝‖D⁻¹(q−q_cur)‖₂、D＝diag(半区间)）；(d) 稳定编号升序（stableIndex＝候选在（初值序，q 规范序列化字典序）确定序中的位置）。固定输入/种子/线程数下排序逐字节稳定。
- **初值池（模块冻结，kinematics.md §5.2）**：生产路径＝RobWork 数值 IK（阻尼最小二乘）多初值；初值池按固定次序 q_home → 会话当前 q → 关节区间中点 → 固定种子 Latin 超立方补足至默认 16 个（数量入 `KinematicsSettings`）；任务级收敛判据＝§15.3（位置 1 mm、姿态 1 deg 测地角）；求解器内部步长阈值 1e-10、每初值迭代上限 200；解析 IK 夹具仅用于黄金对照（标准六轴构型解析解集合一致性），不进生产路径。

### 6.3 WP-15-T03 Jacobian 与奇异性（0.5～1 人周）

- 代码范围：`src/FkJacobian.cpp`（Jacobian 部分）；`test/JacobianTest.cpp`；`testdata/kinematics/jacobian/`。
- 前置：T01。
- 输出工件：`JacobianResult`（J、`J_norm`、σmin/σmax、条件数、可操作度、`L*` 及来源/回退诊断）。
- 验收断言：§6「JacobianTest」——`J_norm`/`L*`/回退/DataInsufficient、任务子空间、解析对照 1e-6。语义引用（kinematics.md §5.3，以 §15.3 冻结为准）：`J_norm=[J_v/L*; J_ω]`；可操作度＝√det(J_norm·J_normᵀ)；`L*` 默认 Zero 位姿基座原点到 TCP 距离，回退须项目配置正值并记诊断，无法确定→DataInsufficient；4/5 轴任务用受约束分量构成的任务子空间 Jacobian（分量由 REQ-01 声明）。

### 6.4 WP-15-T04 区域覆盖（1～1.5 人周）

- 代码范围：`src/RegionCoverage.cpp`＋`include/.../RegionCoverage.hpp`；`test/RegionCoverageTest.cpp`；`testdata/kinematics/regions/`。
- 前置：T02；WP-14 冻结分母定义。
- 输出工件：`CoverageReport`（plannedCombinations、evaluatedCombinations、达标组合数、positionCoverage、orientationCoverage；分母＝计划的位置—姿态组合，WP-14 冻结定义）。
- 验收断言：§6「RegionCoverageTest」——分母/边界、取消与预算耗尽两锚点、Partial 不得 Verified。口径（模块冻结，kinematics.md §5.4）：网格含边界；位置覆盖＝存在至少一个达标姿态组合的采样位置占比，姿态覆盖＝存在至少一个达标位置组合的采样姿态占比，组合级统计同时保留；Verified 区域每空间轴至少两个样本（§15.3）。

### 6.5 WP-15-T05 碰撞证据（0.5～1 人周）

- 代码范围：`src/CollisionEvidenceAdapter.cpp`＋`include/.../CollisionEvidenceAdapter.hpp`；`test/CollisionEvidenceTest.cpp`；`testdata/kinematics/collisions/`。
- 前置：T01、T02；WP-07 `CollisionEvaluator`。
- 输出工件：碰撞证据（对象 ID 对、策略内容 ID、分辨率、最近距离）。
- 验收断言：§6「CollisionEvidenceTest」——共享评估器一致性、缺检测器 DataInsufficient（KIN-05）、限定措辞。规则（kinematics.md §5.5）：只调 WP-07 `CollisionEvaluator`（同一快照/策略）；结论措辞冻结"在本策略与分辨率下未发现碰撞"；距离查询不可用不推断安全。

### 6.6 WP-15-T06 批处理执行（1 人周）

- 代码范围：`src/KinematicsEvaluator.cpp`＋`src/KinematicsSettings.cpp`（含同名公共头）；`test/BatchExecutionTest.cpp`；`testdata/kinematics/batches/`。
- 前置：T02、T04、T05；WP-08 调度端口、WP-05 评估端口。
- 输出工件：`IEngineeringEvaluator` 实现（evaluatorId `ird.kinematics` 冻结；`dependencyManifest/validate/evaluate/capabilities` 按 public-interfaces §3）；批次＝任务点或区域采样组合，安全点＝批次边界。
- 验收断言：§6「BatchExecutionTest」——身份校验（`RunIdentity`）、迟到回调、取消/检查点、固定种子复现。取消/检查点/缓存按 WP-08 契约（execution-model §1～§5）；`KinematicsSettings` 进入输入切片与缓存键。

### 6.7 WP-15-T07 应用与 GUI（0.5～1 人周）

- 代码范围：`gui/KinematicsPlugin.hpp`、`gui/KinematicsPlugin.cpp`、`gui/panels/`；`test/KinematicsGuiTest.cpp`；CMake 目标 `sdurws_ird_kinematics_plugin`、`sdurws_ird_kinematics_gui_test`。
- 前置：T06；WP-10 公共组件、WP-04 命令端口。
- 输出工件：候选预览、显式应用入口、导出。
- 验收断言：§6「KinematicsGuiTest」——候选预览不建修订、显式应用、导出引用快照（QT_QPA_PLATFORM=windows）。会话规则（kinematics.md §5.6）：双击任务/候选只改会话姿态（KIN-06），"用于规划/锁定分支"才经 WP-04 命令应用 `IkBranchPolicy`；导出引用快照而非当前 Widget。

### 6.8 WP-15-T08 契约回归（0.5 人周）

- 代码范围：`test/CrossEntryTest.cpp`（`_contract_test` 目标）。
- 前置：T06、T07；与 WP-20-T08 对侧联调。
- 输出工件：AT-19 静态入口一致性证据。
- 验收断言：§6「CrossEntryTest」——AT-19 静态入口：与 WP-20 入口返回完全相同的对象 ID 对、判定与原因；显示开关不影响判定。

## 7. 测试矩阵

以 kinematics.md §6 为唯一基准（本 WP 不自行扩大或放宽）：

| 测试文件 | 覆盖要点 | 归属任务 |
| --- | --- | --- |
| FkTest | Zero/Home/边界/固定种子姿态 FK 解析对照（1e-9）；引用失败阻止求值 | T01 |
| IkCandidateTest | 多初值收敛、残差/限位过滤、去重（1e-6 rad/1e-8 m）、排序键全序与稳定性、3627db1 回归夹具 | T02 |
| JacobianTest | `J_norm`/`L*`/回退/DataInsufficient、任务子空间、解析对照 1e-6 | T03 |
| RegionCoverageTest | 分母/边界、取消与预算耗尽两锚点、Partial 不得 Verified | T04 |
| CollisionEvidenceTest | 共享评估器一致性、缺检测器 DataInsufficient、限定措辞 | T05 |
| BatchExecutionTest | 身份校验、迟到回调、取消/检查点、固定种子复现 | T06 |
| KinematicsGuiTest | 候选预览不建修订、显式应用、导出引用快照（QT_QPA_PLATFORM=windows） | T07 |
| CrossEntryTest | AT-19 静态入口：对象 ID 对/判定/原因一致；显示开关不影响判定 | T08 |

## 验证命令（双形式，仓库根执行）

脚本形式：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_gui_test$'
```

原生回退（PowerShell 5.1，禁 pwsh）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 9. 独立验证与评审

- 独立验证者（黑盒/数值）：FK/IK/Jacobian 解析对照、排序稳定性、区域覆盖锚点、碰撞证据协议、批处理身份与复现、AT-19 一致性。
- 独立评审者：需求符合性（KIN-01～08）、架构边界（无本地碰撞策略、无自建调度、无 Widget 状态读取）、代码质量。
- 独立算法验证者：复核 3627db1 回归夹具与排序键全序性、初值池次序、容差（§15.3）。
- 角色分离：实现者不得担任同任务最终评审者（总纲 §4.1）。

## 10. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `sdurws_kinematicanalysis` 主链路（TargetEvaluator 等） | 只读黄金对照后 Rewrite | FK/IK/排序黄金数据通过后切换 |
| 3627db1 修复的候选排序逻辑 | 迁移：行为清单＋排序键回归夹具固定 | 排序稳定性报告 |
| 插件内重复碰撞适配/开关 | 删除，统一 WP-07 共享评估器 | AT-19 一致性门禁 |
| 旧目标 `sdurws_kinematicanalysis` | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |

## 退出条件

- KIN-01～08、AT-03～05、AT-18 阶段 B 子链路和 AT-19 静态入口通过（阶段 B 门禁，总纲 §8.2）。
- 容差符合需求 §15.3（模块不得自行扩大容差，总纲 §10.3）；固定输入/种子/线程数下排序逐字节稳定。
- 不可行、数据不足和取消结果不进入正式可行集（evaluation-semantics §2/§4）。
- 证据写入 `evidence/WP-15/` 并签署：FK/IK/Jacobian 解析对照报告、覆盖率矩阵、排序稳定性报告、AT-18/19 阶段 B 记录与独立评审签名。

## 12. 人周与追踪

| 任务 | 人周 |
| --- | ---: |
| T01 | 1～1.5 |
| T02 | 2～2.5 |
| T03 | 0.5～1 |
| T04 | 1～1.5 |
| T05 | 0.5～1 |
| T06 | 1 |
| T07 | 0.5～1 |
| T08 | 0.5 |
| 合计 | 7～10（总纲 §5.3，保持不变） |

需求追踪：`requirement-traceability.csv` 中 KIN-01～08 主实现＝WP-15。

## 任务卡索引

- [WP-15-T01 FK 与规范模型](../agent-tasks/WP-15-T01-fk.md)
- [WP-15-T02 IK 候选](../agent-tasks/WP-15-T02-ik.md)
- [WP-15-T03 Jacobian 与奇异性](../agent-tasks/WP-15-T03-jacobian.md)
- [WP-15-T04 区域覆盖](../agent-tasks/WP-15-T04-region-coverage.md)
- [WP-15-T05 碰撞证据](../agent-tasks/WP-15-T05-collision-evidence.md)
- [WP-15-T06 批处理执行](../agent-tasks/WP-15-T06-batch-execution.md)
- [WP-15-T07 应用与 GUI](../agent-tasks/WP-15-T07-kinematics-ui.md)
- [WP-15-T08 契约回归](../agent-tasks/WP-15-T08-cross-entry.md)
