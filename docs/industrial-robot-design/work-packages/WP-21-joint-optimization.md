# WP-21 联合优化实施计划

> 阶段/发布：阶段 D / R2；完整实现 OPT-01～10（含 OPT-05、OPT-09、OPT-10）。负责 WP：WP-21。
> 实施语义唯一来源：`module-design/optimization.md` v0.3（需求基线 v0.8；检查点 `IRD-D2-20260829`；与 WP-20 共用一文，本文只引用其 WP-21 部分）。
> 前置（总纲 §5.3，保持不变）：WP-16～20。人周：10～16。
> 模块详设补充（不改总纲口径）：WP-08 平台经 WP-16～20 传递；WP-23 规模化基准与本 WP 协作采集性能证据（NFR-PERF-04～06），不构成总纲前置变更。
> 治理状态：Planned（D6 深化重写；需求、架构契约与模块详设契约与详设已于 IRD-D10-20260829 联合评审 Accepted；实现启动按总纲依赖顺序与任务状态账本）。

**需求与契约：** OPT-01～10 全量（OPT-D）、AT-09～14；清单见 §2。  
**拥有目录：** `industrialrobot/plugins/optimization/`（joint 侧）及其测试（文件树见 §3）。  
**输入/输出：** 输入＝WP-16～20 交付的各域评估器＋研究定义；输出＝`OptimizationRunResult`/`DesignCandidate`/`ParetoSet`＋方案分支应用（见 §4）。

## 1. 目标与非目标

**目标**

- 在 R2 中编排轨迹、动力学、传动和器件评估，提供可恢复、可复现、可审计的 Pareto 候选。WP-21 拥有（optimization.md §1）：联合搜索编排（外层结构/传动探索＋内层 Quick/Verified/器件匹配）、`OptimizationRunResult`（SYM-OPT-002）/`DesignCandidate`（SYM-OPT-005）/`ParetoSet`（SYM-OPT-006）全量判定、Quick 误淘汰审计与鲁棒性协议。
- 完成定义：OPT-01～10 全量、AT-09～14、NFR-PERF-04～06 通过；误淘汰率与恢复统计符合需求 §15.3；同种子跨线程候选稳定 ID、可行集合与 Pareto 支配关系一致。

**非目标**

- 各域评估算法（WP-15～19 评估器一律经 WP-08 调度与 `ResultEnvelope` 交互，无业务插件代码依赖）、项目修订写入（"设为当前方案"经 WP-04 命令）、第二套缓存/调度实现。
- 不复制 WP-20 的约束、缓存或 Pareto 语义——WP-20 静态链路作为输入复用。
- 变量/补丁/候选身份语义以 `architecture/candidate-compilation.md` §1～§6 为准，本文不复述。

## 2. 需求、契约与发布切片

- 需求锚点（optimization.md §0）：§7.4、§8.7、§9（含 §9.3/§9.4）、§15.3；场景 AT-09～14；NFR-PERF-04～06。
- 架构契约：`architecture/candidate-compilation.md`（最高权威）、`architecture/evaluation-semantics.md`、`architecture/execution-model.md`（§3 缓存、§4 取消/检查点/恢复）、`architecture/public-interfaces.md`、`architecture/persistence-schema.md`、`architecture/symbol-registry.md`。
- 代码前置：WP-20 静态链路（definition/candidate 核心与其公共头）、WP-16～19 评估器（仅经 WP-08 调度）、WP-03～09 平台端口；GUI 面板扩展基于 WP-20-T07 交付的 `OptimizationPlugin`；构建/门禁入口 WP-01。
- 发布切片：T01～T06 全部属阶段 D / R2；不回改 WP-20 已冻结的 OPT-B 行为（发现缺陷走上游所有者流程，总纲 §4.3）。

## 3. 拥有目录、CMake 目标与依赖边界

拥有目录（optimization.md §2 文件树中 `joint/` 及 WP-21 专属测试/数据/证据）：

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/
  joint/include/sdurws/ird/opt/
    JointSearchOrchestrator.hpp   FeasibilityLayers.hpp   MiseliminationAudit.hpp
    RobustnessProtocols.hpp   CandidateApplication.hpp
  joint/src/JointSearchOrchestrator.cpp   FeasibilityLayers.cpp   MiseliminationAudit.cpp
      RobustnessProtocols.cpp   CandidateApplication.cpp
  test/JointSearchTest.cpp   FeasibilityLayersTest.cpp   SchedulerCheckpointTest.cpp
      ParetoRobustnessTest.cpp   AcceptanceEvidenceTest.cpp
  testdata/optimization/{audit,robustness}/
  # 证据 → out/test-evidence/wp-21/<run-id>/（AGENTS §3，不入源码树）
```

共享范围（与 WP-20 协作、不独自拥有）：`gui/panels/` 的候选预览/应用面板扩展、`test/ResultApplicationTest.cpp` 的联合应用子句、`testdata/optimization/` 其余目录。

CMake 目标（与模块详设 v0.3 完全一致，不得增删改名）：`sdurws_ird_optimization_joint`、`sdurws_ird_optimization_joint_test`。本 WP 不新建 GUI 测试目标——联合候选预览/应用的面板行为由既有 `sdurws_ird_optimization_gui_test` 扩展用例覆盖（optimization.md §2 目标清单）。

允许依赖：WP-03 core、WP-04 命令端口、WP-05 evidence（评估端口＋结果仓库）、WP-06 runtime（编译管线）、WP-07 policy、WP-08 execution（调度/缓存/检查点）、WP-09 diagnostics、WP-20 definition/candidate 公共头、Qt Core；WP-21 对 WP-16～19 评估器仅经 WP-08 调度与 `ResultEnvelope` 交互。禁止：业务插件互依、反射式字段写入、候选直写 revision、加权总分替代 Pareto、第二套调度/缓存、复现 WP-20 静态语义的第二实现。

## 4. 输入、输出与固定时序

| 方向 | 工件 |
| --- | --- |
| 输入 | 基线修订、`OptimizationStudyDefinition`（阶段 D 可激活 StageD 绑定）、WP-20 编译管线、WP-16/17 轨迹/动力评估器（经 WP-08）、WP-19 器件目录（`CatalogRef` 匹配）、预算与 `algorithmPolicy` |
| 输出 | 可恢复 `OptimizationRunResult`（SYM-OPT-002，追加式）、全量 `DesignCandidate`（SYM-OPT-005）集合、`ParetoSet`（SYM-OPT-006）、误淘汰审计与鲁棒性证据、方案分支应用包 |

管线固定时序：复用 optimization.md §4 骨架（研究定义校验→候选生成→补丁→快照→编译→硬约束→指标→`DesignCandidate`→`ParetoSet`→`OptimizationRunResult` 追加→用户"设为当前方案"→WP-04 命令创建方案分支＋一个新修订＋完整复算）。分层搜索时序（optimization.md §5.6）：外层探索 → 内层 Quick 运动学筛选 → Verified 轨迹/动力（WP-16/17）→ 器件目录匹配（WP-19）→ 无组合可行时把原因反馈外层。

失败分类与错误码（optimization.md §4 矩阵）：`IRD-OPT-HARD-CONSTRAINT`（Engineering/Error）、`IRD-OPT-CANDIDATE-COMPILE-FAILED`（Engineering/Error，无部分工件）、`IRD-OPT-BUDGET-EXHAUSTED`（Engineering/Warning：终止并落 DataInsufficient 锚点；可加预算后新 attempt 续跑）、`IRD-OPT-AUDIT-THRESHOLD-EXCEEDED`（Engineering/Error：扩大保留池、禁用对应 Quick 规则重跑；禁发正式候选报告）；绑定/补丁/身份错误码以 candidate-compilation §3～§6 为准。

## 5. 任务 DAG

```text
T01 联合搜索策略 ─┬→ T02 约束与指标判定 ─┬→ T04 Pareto 与鲁棒性 ─┬→ T05 候选预览与应用 ─┐
                  └→ T03 调度缓存检查点 ───────────────────────────┴──────────────────────┴→ T06 验收证据
```

| 任务 | WP 内前置 | 外部门禁 |
| --- | --- | --- |
| T01 | — | WP-16～20 交付前置 |
| T02 | T01 | WP-08 调度、WP-15～19 评估器 |
| T03 | T01 | WP-08 检查点/缓存契约 |
| T04 | T02 | 需求 §15.3 冻结值 |
| T05 | T02、T04 | WP-04 命令端口、WP-10/WP-20 GUI 面板 |
| T06 | T03、T04、T05 | 阶段 D 门禁（总纲 §8.4）、WP-23 协作 |

每任务一张任务卡、一个 worktree/分支/提交（总纲 §4.3）。

## 6. 逐任务计划

### 6.1 WP-21-T01 联合搜索策略（2～3 人周）

- 代码范围：`joint/src/JointSearchOrchestrator.cpp`＋`include/.../JointSearchOrchestrator.hpp`；`test/JointSearchTest.cpp`。
- 前置：无 WP 内前置；WP-16～20 交付前置。
- 输出工件：联合搜索首版算法（模块冻结、可经 `algorithmPolicy` 替换，OPT-10）——外层＝量化/离散网格枚举或固定种子拉丁超立方采样＋保留池；局部改进＝对非支配候选沿激活目标做坐标步长搜索（步长＝量化步长或域宽 2%）；内层＝Quick 运动学筛选→Verified 轨迹/动力（WP-16/17）→器件目录匹配（WP-19），无组合可行时把原因反馈外层。
- 验收断言：optimization.md §6「JointSearchTest」——OPT-01～10 全量；同种子/线程数下搜索轨迹、保留池与候选集合可复现；`algorithmPolicy` 替换不改变冻结语义的对外契约。

### 6.2 WP-21-T02 约束与指标判定（2～3 人周）

- 代码范围：`joint/src/FeasibilityLayers.cpp`＋`include/.../FeasibilityLayers.hpp`；`test/FeasibilityLayersTest.cpp`。
- 前置：T01；WP-08 调度、WP-15～19 评估器。
- 输出工件：HardConstraint、SoftConstraint、Metric、Objective 四层判定（类型语义以 symbol-registry §3 SYM-OPT-007～010 为准）；全量 `DesignCandidate`/`ParetoSet` 判定。
- 验收断言：§6「FeasibilityLayersTest」——四层判定、证据不足和 Partial 不进入可行集（evaluation-semantics §4 `isFormallyFeasible()` 谓词：硬约束失败、证据不足或 Partial 候选不得进入可行 Pareto 集，总纲 §8.4）；`RequiredEvidenceProfile` 缺口按 `gaps` 列出；Quick 不得作正式通过证据（evaluation-semantics §3）。

### 6.3 WP-21-T03 调度缓存检查点（1.5～2.5 人周）

- 代码范围：`joint/src/JointSearchOrchestrator.cpp` 的批次/检查点策略（组合 WP-08，不建第二套调度/缓存）；`test/SchedulerCheckpointTest.cpp`。
- 前置：T01；WP-08 检查点/缓存契约。
- 输出工件：检查点/恢复/缓存接入——批次边界检查点与兼容校验、恢复（原 `runId`＋新 `attemptId`、批次去重）、缓存键覆盖 studyDefinitionVersion、algorithmVersion、seed、threadCount（optimization.md §5.6；execution-model §3）。
- 验收断言：§6「SchedulerCheckpointTest」——检查点恢复统计（统计不得重复，execution-model §4）；预算耗尽＝`Completed + DataInsufficient + Complete`，可加预算后新 attempt 续跑（`IRD-OPT-BUDGET-EXHAUSTED`）；不兼容检查点不得用于恢复（`IRD-EXEC-CHECKPOINT-INCOMPATIBLE`）。

### 6.4 WP-21-T04 Pareto 与鲁棒性（2～3 人周）

- 代码范围：`joint/src/MiseliminationAudit.cpp`＋`joint/src/RobustnessProtocols.cpp`（含同名公共头）；`test/ParetoRobustnessTest.cpp`；`testdata/optimization/{audit,robustness}/`。
- 前置：T02。
- 输出工件：`AuditSample`（候选 ID、淘汰原因、分层键、Verified 复核结论）审计流水线；鲁棒性三模式执行器。
- 验收断言：§6「ParetoRobustnessTest」——误淘汰审计、鲁棒性三模式、AT-10～13 相关面与 NFR-PERF-04～06。冻结值（optimization.md §5.5，需求 §15.3）：固定种子、按淘汰原因与目标值区间分层抽取淘汰候选的 5%、且不少于 200 个（淘汰数不足时全抽）执行 Verified 复核；"Quick 淘汰但 Verified 满足硬约束且相对已验证集合非支配"计误淘汰；误淘汰率 ≤1%、95% 置信上界 ≤3%，超限按错误矩阵处置（扩大保留池、禁用对应 Quick 规则重跑、禁发正式候选报告）。Pareto 容差支配与措辞复用 WP-20-T04 冻结口径（§9.3/§9.4 步骤 7），不建第二实现。

### 6.5 WP-21-T05 候选预览与应用（1～1.5 人周）

- 代码范围：`joint/src/CandidateApplication.cpp`＋`include/.../CandidateApplication.hpp`；`gui/panels/` 候选预览/应用面板扩展；`test/ResultApplicationTest.cpp` 联合应用子句（`_definition_test`，与 WP-20-T06 共享夹具）。
- 前置：T02、T04；WP-04 命令端口、WP-20 GUI 面板。
- 输出工件：方案分支应用包（`DesignVector`＋`writeSetFingerprint`＋目标分支名，optimization.md §3）经 WP-04 `DomainCommand`；候选预览（不建修订）与"设为当前方案"入口。
- 验收断言：§6「ResultApplicationTest」联合子句＋「AcceptanceEvidenceTest」相关用例——应用只创建一个新修订并触发完整复算（OPT-08/AT-12）；运行期间修订数不随候选数量增长；不可行候选不可应用；GUI 面板行为经 `sdurws_ird_optimization_gui_test` 扩展用例验证。

### 6.6 WP-21-T06 联合优化验收证据（1.5～3 人周）

- 代码范围：`test/AcceptanceEvidenceTest.cpp`；证据装配入 `out/test-evidence/wp-21/<run-id>/`。
- 前置：T03、T04、T05；阶段 D 门禁与 WP-23 协作。
- 输出工件：AT-10～14（分支切换、恢复、崩溃、性能）证据收集；R2 基准报告、误淘汰审计、恢复统计、Pareto 黄金集。
- 验收断言：§6「AcceptanceEvidenceTest」——AT-10～14 与 NFR-PERF-04～06；误淘汰率、恢复统计和 Pareto 关系符合需求 §15.3；证据含独立评审签名。

## 7. 测试矩阵

以 optimization.md §6 为唯一基准（本 WP 不自行扩大或放宽）：

| 测试文件（target） | 覆盖要点 | 归属任务 |
| --- | --- | --- |
| JointSearchTest / FeasibilityLayersTest / SchedulerCheckpointTest / ParetoRobustnessTest / AcceptanceEvidenceTest（`_joint_test`） | OPT-01～10 全量、四层判定、检查点恢复统计、误淘汰审计、鲁棒性三模式、AT-10～14 与 NFR-PERF-04～06 | T01～T06 |
| ResultApplicationTest 联合应用子句（`_definition_test`，共享） | AT-12 修订不随候选增长、方案分支应用＝一个新修订＋复算 | T05 |
| OptimizationGuiTest 扩展用例（`_gui_test`，共享） | 联合候选预览/应用面板行为；不可行候选不可应用 | T05 |

## 验证命令（双形式，仓库根执行）

脚本形式：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_gui_test$'
```

原生回退（PowerShell 5.1，禁 pwsh）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件；`_gui_test` 仅在本轮扩展了 GUI 用例时运行。

## 9. 独立验证与评审

- 独立验证者（黑盒/数值）：OPT-01～10 全量矩阵、检查点恢复统计、误淘汰审计抽样可复现、鲁棒性三模式、同种子跨线程复现、AT-09～14。
- 独立评审者：需求符合性、架构边界（对 WP-16～19 仅经 WP-08 调度、无第二套调度/Pareto/缓存、无候选直写 revision）、代码质量。
- 独立优化验证者：复核分层搜索策略、Quick/Verified 分层语义、Pareto 容差支配关系与审计统计方法。
- 角色分离：实现者不得担任同任务最终评审者（总纲 §4.1）。

## 10. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 旧联合优化入口与重复缓存 | 验收后删除，不形成第二套调度/Pareto 实现 | WP-21 退出条件 |
| `sdurws_structureoptimizer*` 加权总分与候选写回链路 | 删除（WP-20 已启动；本 WP 完成全量替换后清尾） | 静态 Pareto 黄金数据与 AT-09/12 通过 |
| 旧结构优化器的采样/筛选行为 | 只读黄金对照（EvidenceOnly）后按新算法重写 | 行为差异报告归档 |
| 旧目标 `sdurws_structureoptimizer*` | 不作依赖；对应阶段验收后退出构建与安装包 | 安装包审计 |

## 退出条件

- OPT-01～10 全量、AT-09～14、NFR-PERF-04～06 通过（阶段 D 门禁，总纲 §8.4，形成 R2）。
- 硬约束失败、证据不足或 Partial 候选不得进入可行 Pareto 集；误淘汰率 ≤1%、95% 置信上界 ≤3%；恢复统计不重复且检查点兼容校验生效。
- 同种子跨线程候选稳定 ID、可行集合与 Pareto 支配关系一致；候选不产生修订，"设为当前方案"只创建一个新修订并触发完整复算。
- 证据写入 `out/test-evidence/wp-21/<run-id>/` 并签署：R2 基准报告、误淘汰审计、恢复统计、Pareto 黄金集、AT-09～14 记录与独立评审签名。

## 12. 人周与追踪

| 任务 | 人周 |
| --- | ---: |
| T01 | 2～3 |
| T02 | 2～3 |
| T03 | 1.5～2.5 |
| T04 | 2～3 |
| T05 | 1～1.5 |
| T06 | 1.5～3 |
| 合计 | 10～16（总纲 §5.3，保持不变） |

需求追踪：`requirement-traceability.csv` 中 OPT-05、OPT-09、OPT-10 与 OPT-01～04/06～08 阶段 D 增量主实现＝WP-21。

## 任务卡索引

- [WP-21-T01 联合搜索策略](../agent-tasks/WP-21-T01-search-strategy.md)
- [WP-21-T02 约束与指标判定](../agent-tasks/WP-21-T02-feasibility-layers.md)
- [WP-21-T03 调度缓存检查点](../agent-tasks/WP-21-T03-scheduler-checkpoint.md)
- [WP-21-T04 Pareto 与鲁棒性](../agent-tasks/WP-21-T04-pareto-robustness.md)
- [WP-21-T05 候选预览与应用](../agent-tasks/WP-21-T05-apply-candidate.md)
- [WP-21-T06 联合优化验收证据](../agent-tasks/WP-21-T06-acceptance-evidence.md)
