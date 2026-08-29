# WP-20 优化定义与候选编译实施计划

> 阶段/发布：阶段 B / R1，仅实现 OPT-B（OPT-01～04、06～08 的静态子集，需求 §8.7.1 唯一集合）；OPT-05、OPT-09、OPT-10 与完整轨迹/动力/器件联合归 WP-21（阶段 D / R2）。负责 WP：WP-20。
> 实施语义唯一来源：`module-design/optimization.md` v0.3（需求基线 v0.7；检查点 `IRD-D2-20260829`；与 WP-21 共用一文，本文只引用其 WP-20 部分）。
> 前置（总纲 §5.3，保持不变）：WP-13～15（交付前置）。人周：6～9。
> 模块详设补充（不改总纲口径）：代码前置 WP-03～09（平台内核经总纲 §5.2 交付）。
> 治理状态：Planned（D6 深化重写；需求、架构契约与模块详设均处 Proposed 时不得进入实现）。

**需求与契约：** OPT-01～04、06～08 的 OPT-B 唯一子集（需求 §8.7.1）、AT-12/18；清单见 §2。  
**拥有目录：** `industrialrobot/plugins/optimization/`（definition 侧）及其测试（文件树见 §3）。  
**输入/输出：** 输入＝基线修订＋变量域＋静态硬约束/指标；输出＝候选编译结果＋静态 Pareto（候选不产生修订）（见 §4）。

## 1. 目标与非目标

**目标**

- 支持新机型和既有机型改型的研究定义、变量引用、候选补丁和 R1 静态优化链路。WP-20 拥有（optimization.md §1）：`OptimizationStudyDefinition`（SYM-OPT-001）校验、变量绑定注册表、`CandidateInputSnapshot`（SYM-OPT-003）与 `CandidatePatch`（SYM-OPT-013）编译、`CompiledCandidateArtifact`（SYM-OPT-004）编译管线（组合 WP-06）、静态硬约束与 OPT-B 静态指标/静态 Pareto、缓存适配与确定性种子。
- 完成定义：OPT-B 唯一集合全部有测试与证据；R1 不依赖 WP-21 即可完成静态闭环；同种子下候选稳定 ID、可行集合与 Pareto 支配关系跨线程一致（candidate-compilation §5）。

**非目标**

- 各域评估算法（一律经 WP-08 调度注册评估器）、项目修订写入（"设为当前方案"经 WP-04 命令）、第二套缓存/调度实现。
- WP-21 的联合搜索编排、Quick 误淘汰审计实现、鲁棒性协议（optimization.md §1 分工；OPT-B 只允许可证明保守的直接淘汰，故审计实现归 WP-21）。
- 变量/补丁/候选身份语义不复述：以 `architecture/candidate-compilation.md` §1～§6 为准。

## 2. 需求、契约与发布切片

- 需求锚点（optimization.md §0）：§7.4、§8.7 与 §8.7.1（OPT-B 唯一集合）、§9、§15.3；场景 AT-09（静态子集）、AT-12。
- 架构契约：`architecture/candidate-compilation.md`（最高权威）、`architecture/evaluation-semantics.md`、`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/persistence-schema.md`、`architecture/symbol-registry.md`。
- 代码前置：WP-03 core、WP-04 命令端口、WP-05 evidence（评估端口＋结果仓库）、WP-06 runtime（编译管线）、WP-07 policy、WP-08 execution（调度/缓存/检查点）、WP-09 diagnostics、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui；构建/门禁入口 WP-01。
- 发布切片：T01～T08 属阶段 B / R1；`joint/` 目录与 `_joint` 目标归 WP-21，本 WP 不得实现或依赖。

## 3. 拥有目录、CMake 目标与依赖边界

拥有目录（optimization.md §2 文件树中除 `joint/` 外的本 WP 部分）：

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/
  definition/include/sdurws/ird/opt/
    StudyDefinition.hpp   VariableBindingRegistry.hpp   StudyValidation.hpp
  definition/src/StudyDefinition.cpp   VariableBindingRegistry.cpp   StudyValidation.cpp
  candidate/include/sdurws/ird/opt/
    DesignVector.hpp   CandidatePatchCompiler.hpp   CandidateCompiler.hpp
    StaticConstraints.hpp   StaticMetrics.hpp   StaticPareto.hpp
  candidate/src/DesignVector.cpp   CandidatePatchCompiler.cpp   CandidateCompiler.cpp
      StaticConstraints.cpp   StaticMetrics.cpp   StaticPareto.cpp
  gui/OptimizationPlugin.hpp   gui/OptimizationPlugin.cpp   gui/panels/
  test/StudyDefinitionTest.cpp   CandidatePatchTest.cpp   StaticConstraintTest.cpp
      StaticParetoTest.cpp   CacheDeterminismTest.cpp   ResultApplicationTest.cpp
      CrossEntryTest.cpp   OptimizationGuiTest.cpp
  testdata/optimization/{studies,vectors,candidates,pareto}/
  evidence/WP-20/
```

CMake 目标（与模块详设 v0.3 完全一致，不得增删改名）：`sdurws_ird_optimization_definition`（definition＋candidate 计算核心，无 Qt Widgets）、`sdurws_ird_optimization_definition_test`、`sdurws_ird_optimization_definition_contract_test`、`sdurws_ird_optimization_plugin`＋`sdurws_ird_optimization_gui_test`（WP-20-T07）。`sdurws_ird_optimization_joint`/`_joint_test` 归 WP-21，本 WP 不得创建或链接。

允许依赖：WP-03～09 公共端口（optimization.md §2）；禁止：业务插件互依、反射式字段写入、候选直写 revision、加权总分替代 Pareto、第二套调度/缓存。

## 4. 输入、输出与固定时序

| 方向 | 工件 |
| --- | --- |
| 输入 | 基线修订（`baselineRevisionRef`）、`EngineeringRequirements`、变量域、静态硬约束、目标、预算（`maxCandidates/maxWallClockS/maxVerifiedEvaluations`）、随机种子、`algorithmPolicy`（版本化策略对象） |
| 输出 | `OptimizationStudyDefinition`（SYM-OPT-001）、`DesignVector`→`CandidatePatch`（SYM-OPT-013）→`CandidateInputSnapshot`（SYM-OPT-003）→`CompiledCandidateArtifact`（SYM-OPT-004，身份独立于 `CompiledRobotArtifacts`，symbol-registry §4.5）、静态 `DesignCandidate`/`ParetoSet` 实例、`OptimizationRunResult` 追加（WP-05 仓库）、证据 |

管线固定时序（optimization.md §4，两 WP 共用骨架，不得重排）：研究定义校验（绑定注册→writeSet 两两互斥→DAG 无环→阶段锁→预算/种子/版本）→ 候选生成（`algorithmPolicy`）→ `DesignVector` → `CandidatePatch`（全成全败）→ `CandidateInputSnapshot` → 组合 WP-06 编译管线 → `CompiledCandidateArtifact` → 静态硬约束（拓扑/输入→运动学→碰撞；经 WP-08 调度评估器）→ 静态指标 → `DesignCandidate` → `ParetoSet` → `OptimizationRunResult` 追加（不产生修订）→ 用户"设为当前方案"→ WP-04 命令创建方案分支＋一个新修订＋完整复算（OPT-08/AT-12；运行期间修订数不随候选数量增长）。

失败分类与错误码（optimization.md §4 矩阵＋candidate-compilation §3～§6）：`IRD-OPT-UNREGISTERED-BINDING`/`-WRITE-CONFLICT`/`-DOMAIN-VIOLATION`/`-CYCLE`/`-PATCH-REJECTED`/`-STAGE-LOCKED`（以 candidate-compilation §3～§6 为准）；`IRD-OPT-HARD-CONSTRAINT`（Engineering/Error）、`IRD-OPT-CANDIDATE-COMPILE-FAILED`（Engineering/Error，无部分工件）、`IRD-OPT-BUDGET-EXHAUSTED`（Engineering/Warning，落 `Completed+DataInsufficient+Complete` 锚点，可加预算新 attempt 续跑）。新码待 diagnostics.md 登记后启用。

## 5. 任务 DAG

```text
T01 研究定义 → T02 候选补丁 → T03 静态硬约束 → T04 静态指标与 Pareto ─┬→ T06 结果与应用 → T07 阶段 B UI
                                                                      └→ T05 缓存与确定性 ┘        └→ T08 跨入口契约
```

| 任务 | WP 内前置 | 外部门禁 |
| --- | --- | --- |
| T01 | — | WP-13～15 交付前置 |
| T02 | T01 | WP-06 编译管线 |
| T03 | T02 | WP-08 调度、WP-15/07 评估器 |
| T04 | T03 | — |
| T05 | T02、T04 | WP-08 缓存契约 |
| T06 | T04、T05 | WP-04 命令端口、WP-05 结果仓库 |
| T07 | T06 | WP-10 公共组件 |
| T08 | T03、T07 | WP-15-T08 对侧联调 |

每任务一张任务卡、一个 worktree/分支/提交（总纲 §4.3）。

## 6. 逐任务计划

### 6.1 WP-20-T01 研究定义（1～1.5 人周）

- 代码范围：`definition/`（`StudyDefinition/VariableBindingRegistry/StudyValidation` 的 include＋src）；`test/StudyDefinitionTest.cpp`；`testdata/optimization/studies/`。
- 前置：无 WP 内前置；WP-13～15 交付前置。
- 输出工件：`OptimizationStudyDefinition` 校验（`studyId`、`baselineRevisionRef`、`studyDefinitionVersion`、`variables[]`、约束/指标/目标、`budget`、`algorithmPolicy`，字段以 candidate-compilation §1 为准）＋变量绑定注册表首批条目（见下表，模块冻结，optimization.md §5.1）。
- 验收断言：optimization.md §6「StudyDefinitionTest」——作用域引用、锁定、writeSet/DAG/域/阶段锁逐项拒绝且无部分状态；零值与未设置可区分（candidate-compilation §2）；稳定 ID 跨线程一致；StageD 绑定在 OPT-B 激活被拒（candidate-compilation §7.5）。

变量绑定注册表首批条目（8 行/10 个 bindingId；writeSet＝各 parameterKey 展开的物理字段全集，逐变量声明、两两交集必须为空；派生路径 mass/COM/inertia 不入 writeSet、按 DAG 拓扑序重算；改型项目默认锁定非授权参数 OPT-02；`links[i].section` 依赖 `robot-design.schema.json` 增补，D5 提名）：

| bindingId | 目标类型 | parameterKey 签名 | 阶段 |
| --- | --- | --- | --- |
| `robot.section-dimension` | RobotDesign | `links[i].section.{outerDiameter,innerDiameter,width,height}` | StageB |
| `robot.dh-length-offset` | RobotDesign | `nativeDh.rows[i].{a,d}` | StageB |
| `robot.joint-install` | RobotDesign | `joints[i].originPose.position.{x,y,z}` | StageB |
| `robot.joint-range` | RobotDesign | `joints[i].limits.{lower,upper}` | StageB |
| `robot.base-pose` / `robot.tcp-offset` | RobotDesign | `basePose.position.*` / `defaultTcp.offset.position.*` | StageB |
| `robot.link-material` | RobotDesign | `links[i].materialName` | StageB |
| `drivetrain.ratio` | DriveTrainDesign | `axes[i].ratio` | StageB |
| `drivetrain.reducer-key` / `drivetrain.motor-key` | DriveTrainDesign | `axes[i].{reducerKey,motorKey}` | StageD |

### 6.2 WP-20-T02 候选补丁（1.5～2 人周）

- 代码范围：`candidate/` 的 `DesignVector/CandidatePatchCompiler/CandidateCompiler`（include＋src）；`test/CandidatePatchTest.cpp`；`testdata/optimization/{vectors,candidates}/`。
- 前置：T01；WP-06 编译管线。
- 输出工件：`DesignVector`（不可变映射；规范序列化＝键按 UTF-8 字节序排序的 JSON、无空白、浮点十六进制或最短往返表示、SHA-256，candidate-compilation §2）→ `CandidatePatch`（`orderedMutations` 按注册序＋DAG 拓扑序、`derivedRecomputation`、`diagnostics`、`writeSetFingerprint`）→ `CandidateInputSnapshot` → `CompiledCandidateArtifact`（组合 WP-06 管线；候选稳定 ID 以 candidate-compilation §5 公式为准）。
- 验收断言：§6「CandidatePatchTest」——mutation 全成全败（任一失败整体拒绝且不留工件，`IRD-OPT-PATCH-REJECTED`）；派生重算 method∈{AnalyticEstimate, UniformScaling}——AnalyticEstimate 的解析公式以 robot-modeling.md §5 冻结公式表为唯一语义源（实现归属按 D5 报告裁决：提升共享 evaluation 包或端口注入，禁止双实现）；UniformScaling 仅在无截面几何信息时回退并记录；每个派生惯量张量执行正定性与三角不等式校验（candidate-compilation §3）；候选不创建项目修订（candidate-compilation §4）。

### 6.3 WP-20-T03 静态硬约束（0.5～1 人周）

- 代码范围：`candidate/src/StaticConstraints.cpp`＋`include/.../StaticConstraints.hpp`；`test/StaticConstraintTest.cpp`。
- 前置：T02；WP-08 调度、WP-15（运动学）与 WP-07（碰撞）评估器。
- 输出工件：静态硬约束执行器——按拓扑/输入→运动学→碰撞顺序（optimization.md §4），经 WP-08 调度注册评估器；OPT-B 硬约束仅运动学与碰撞静态子集（candidate-compilation §6；`kind` 值域以 candidate-compilation §1 为准）。
- 验收断言：§6「StaticConstraintTest」——硬约束失败矩阵（Must 违反不入可行集，`IRD-OPT-HARD-CONSTRAINT` 附 kind、实际值/阈值）；不可行候选不得进入可行集合与 Pareto；Quick 不能伪装为 Verified（evaluation-semantics §3；Quick 不得作正式通过证据，例外只有 §9.4 可证明保守的硬淘汰）；**OPT-B 只允许可证明保守的直接淘汰，非保守规则不得启用（审计实现归 WP-21）**。

### 6.4 WP-20-T04 静态指标与 Pareto（0.5～1 人周）

- 代码范围：`candidate/src/{StaticMetrics,StaticPareto}.cpp`＋同名公共头；`test/StaticParetoTest.cpp`；`testdata/optimization/pareto/`。
- 前置：T03。
- 输出工件：静态指标视图（metricId、value、unit、来源 `ResultRef`）与静态 Pareto。**OPT-B 可算三项（模块冻结，optimization.md §5.3）**：总体尺寸包络 `overallSizeEnvelope`（单位 m/m³ 随声明）、结构质量 `structureMass`（kg，派生重算后连杆质量合计）、最小关节裕量 `minJointMargin`（dimensionless，与 WP-15 同一公式）；节拍/器件成本/器件质量/关节侧正机械功/最小驱动裕量五项属阶段 C/D，StageB 研究定义中引用即拒（复用 `IRD-OPT-STAGE-LOCKED`）；全局默认激活目标三项中节拍/器件成本在 StageB 不可声明，界面提示并引导改选可算目标。
- 验收断言：§6「StaticParetoTest」——指标容差支配（支配关系＝对全部激活目标 A 不劣于 B 且至少一目标严格优于超过该目标 `comparisonTolerance`；全目标差异均在容差内＝互不支配，§9.3）；软约束只警告/次级排序（SYM-OPT-009），不参与支配；不出现加权总分；Pareto 集结论措辞固定为"当前变量域、算法与计算预算内发现"（§9.4 步骤 7）。

### 6.5 WP-20-T05 缓存与确定性（0.5～1 人周）

- 代码范围：缓存适配落 `definition/`＋`candidate/` 计算核心（组合 WP-08 缓存端口，不建第二套缓存）；`test/CacheDeterminismTest.cpp`。
- 前置：T02、T04；WP-08 缓存契约。
- 输出工件：缓存适配与确定性种子管理。
- 验收断言：§6「CacheDeterminismTest」——缓存键覆盖 studyDefinitionVersion、algorithmVersion、seed、threadCount（optimization.md §5.6；execution-model §3：缓存基于切片内容身份 sliceHash，不基于项目修订号；`Partial`/`Failed`/`Canceled` 与不兼容版本不得命中正式缓存）；同种子输出稳定候选 ID/排序/可行集合/Pareto 支配关系（candidate-compilation §5）。

### 6.6 WP-20-T06 结果与应用（1 人周）

- 代码范围：`candidate/` 运行结果装配（`OptimizationRunResult` 追加经 WP-05 仓库）＋方案分支应用包（`DesignVector`＋`writeSetFingerprint`＋目标分支名，optimization.md §3）；`test/ResultApplicationTest.cpp`。
- 前置：T04、T05；WP-04 命令端口、WP-05 结果仓库。
- 输出工件：候选结果落库（只追加、不产生修订）与"设为当前方案"命令包（OPT-B 静态候选）。
- 验收断言：§6「ResultApplicationTest」——AT-12：候选结果只归优化运行，"设为当前方案"经 WP-04 `DomainCommand` 创建方案分支＋一个新修订＋完整复算；运行期间修订数不随候选数量增长；预算耗尽落 `Completed + DataInsufficient + Complete` 锚点（evaluation-semantics §2）。

### 6.7 WP-20-T07 阶段 B UI（0.5～1 人周）

- 代码范围：`gui/OptimizationPlugin.hpp`、`gui/OptimizationPlugin.cpp`、`gui/panels/`；`test/OptimizationGuiTest.cpp`；CMake 目标 `sdurws_ird_optimization_plugin`、`sdurws_ird_optimization_gui_test`（模块详设标注归 WP-20-T07）。
- 前置：T06；WP-10 公共组件。
- 输出工件：变量域编辑、候选比较、静态证据、应用确认的薄界面。
- 验收断言：§6「OptimizationGuiTest」——变量域编辑、候选比较、静态证据、应用确认；不可行候选不可应用；不暴露内部哈希作为唯一标识；StageB 不可声明目标的引导提示（T04 冻结口径）。

### 6.8 WP-20-T08 跨入口契约（0.5 人周）

- 代码范围：`test/CrossEntryTest.cpp`（`_definition_test`/`_contract_test` 目标）。
- 前置：T03、T07；与 WP-15-T08 对侧联调。
- 输出工件：AT-19 跨入口一致性证据；供 WP-21 复用的稳定扩展接口（不改变已冻结签名）。
- 验收断言：§6「CrossEntryTest」——AT-19 与运动学入口一致：共享碰撞策略、对象 ID 对、判定与原因完全相同；显示开关不影响判定。

## 7. 测试矩阵

以 optimization.md §6 为唯一基准（本 WP 不自行扩大或放宽）：

| 测试文件（target） | 覆盖要点 | 归属任务 |
| --- | --- | --- |
| StudyDefinitionTest / CandidatePatchTest（`_definition_test`） | 作用域引用、锁定、writeSet/DAG/域/阶段锁逐项拒绝且无部分状态；零值与未设置可区分；稳定 ID 跨线程一致 | T01/T02 |
| StaticConstraintTest / StaticParetoTest（`_definition_test`） | 硬约束失败矩阵（Must 违反不入可行集）、指标容差支配、软约束不改变可行性、AT-09 静态子集 | T03/T04 |
| CacheDeterminismTest / ResultApplicationTest / CrossEntryTest（`_definition_test`/`_contract_test`） | 缓存命中/拒绝矩阵、同种子复现、AT-12 修订不随候选增长、AT-19 与运动学入口一致 | T05/T06/T08 |
| OptimizationGuiTest（`_gui_test`） | 变量域编辑、候选比较、静态证据、应用确认；不可行候选不可应用（WP-20-T07） | T07 |

## 验证命令（双形式，仓库根执行）

脚本形式：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_gui_test$'
```

原生回退（PowerShell 5.1，禁 pwsh）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test sdurws_ird_optimization_definition_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 9. 独立验证与评审

- 独立验证者（黑盒）：绑定注册/拒绝矩阵、补丁全成全败、静态约束与 Pareto 黄金集、缓存命中/拒绝、同种子复现、AT-09/AT-12/AT-19。
- 独立评审者：需求符合性（OPT-B 唯一集合 §8.7.1）、架构边界（无反射式写入、无候选直写 revision、无加权总分、无第二套调度/缓存）、代码质量。
- 优化工程师（产品评审）：确认变量域与首批绑定条目工程合理性、静态指标口径、`comparisonTolerance` 取值。
- 角色分离：实现者不得担任同任务最终评审者（总纲 §4.1）。

## 10. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `sdurws_structureoptimizer*` 加权总分与候选写回链路 | 删除（Rewrite 后不保留双轨） | 静态 Pareto 黄金数据与 AT-09/12 通过 |
| 旧结构优化器的采样/筛选行为 | 只读黄金对照（EvidenceOnly）后按新算法重写 | 行为差异报告归档 |
| 旧联合优化入口与重复缓存 | 验收后删除，不形成第二套调度/Pareto 实现 | WP-21 退出条件 |
| 旧目标 `sdurws_structureoptimizer*` | 不作依赖；对应阶段验收后退出构建与安装包 | 安装包审计 |

## 退出条件

- OPT-B 权威集合（需求 §8.7.1）、AT-09 静态子集、AT-12 通过（阶段 B 门禁，总纲 §8.2）。
- 不可行、Partial、DataInsufficient 候选不进入静态 Pareto；StageD 绑定/指标在 OPT-B 全部被拒（阶段锁生效）。
- 候选不产生修订；同种子跨线程稳定 ID 与 Pareto 关系一致；R1 不依赖 WP-21 才能完成静态闭环。
- 证据写入 `evidence/WP-20/` 并签署：研究定义 JSON 样例、候选差异报告、静态 Pareto 黄金集、缓存/复现矩阵、AT-09/12 阶段 B 记录与独立评审签名。

## 12. 人周与追踪

| 任务 | 人周 |
| --- | ---: |
| T01 | 1～1.5 |
| T02 | 1.5～2 |
| T03 | 0.5～1 |
| T04 | 0.5～1 |
| T05 | 0.5～1 |
| T06 | 1 |
| T07 | 0.5～1 |
| T08 | 0.5 |
| 合计 | 6～9（总纲 §5.3，保持不变） |

需求追踪：`requirement-traceability.csv` 中 OPT-01～04、06～08 阶段 B 子集主实现＝WP-20；OPT-05/09/10 主实现＝WP-21（阶段 D）。

## 任务卡索引

- [WP-20-T01 研究定义](../agent-tasks/WP-20-T01-study-definition.md)
- [WP-20-T02 候选补丁](../agent-tasks/WP-20-T02-candidate-patch.md)
- [WP-20-T03 静态硬约束](../agent-tasks/WP-20-T03-static-constraints.md)
- [WP-20-T04 静态指标与 Pareto](../agent-tasks/WP-20-T04-static-pareto.md)
- [WP-20-T05 缓存与确定性](../agent-tasks/WP-20-T05-cache-determinism.md)
- [WP-20-T06 结果与应用](../agent-tasks/WP-20-T06-result-application.md)
- [WP-20-T07 阶段 B UI](../agent-tasks/WP-20-T07-optimization-ui.md)
- [WP-20-T08 跨入口契约](../agent-tasks/WP-20-T08-cross-entry.md)
