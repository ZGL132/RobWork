# 优化与候选编译模块详细方案（optimization）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（D5 重写，待消费者评审）
- 负责 WP：WP-20（阶段 B / R1，OPT-B）＋WP-21（阶段 D / R2，全量）；任务卡：`agent-tasks/WP-20-T01～T08`、`WP-21-T01～T06`
- 架构契约：`architecture/candidate-compilation.md`（最高权威）、`architecture/evaluation-semantics.md`、`architecture/execution-model.md`、`architecture/public-interfaces.md`、`architecture/persistence-schema.md`、`architecture/symbol-registry.md`
- 代码前置：WP-03～09；WP-20＝WP-13～15（交付前置）；WP-21 另需 WP-16～20（总纲 §5.3）；构建/门禁入口 WP-01
- 需求锚点：§7.4、§8.7 与 §8.7.1（OPT-B 唯一集合）、§9、§15.3；场景 AT-09～14

## 1. 模块职责

WP-20 拥有 `OptimizationStudyDefinition`（SYM-OPT-001）校验、变量绑定注册表、`CandidateInputSnapshot`（SYM-OPT-003）、`CandidatePatch`（SYM-OPT-013）编译、`CompiledCandidateArtifact`（SYM-OPT-004）编译管线（组合 WP-06）、静态硬约束与 OPT-B 静态指标/静态 Pareto、缓存适配与确定性种子。WP-21 拥有联合搜索编排（外层结构/传动探索＋内层 Quick/Verified/器件匹配）、`OptimizationRunResult`（SYM-OPT-002）/`DesignCandidate`（SYM-OPT-005）/`ParetoSet`（SYM-OPT-006）全量判定、Quick 误淘汰审计与鲁棒性协议。变量/补丁/候选身份语义一律以 candidate-compilation §1～§6 为准（引用，不复述）。非目标：各域评估算法（一律经 WP-08 调度注册评估器）、项目修订写入（"设为当前方案"经 WP-04 命令）、第二套缓存/调度实现。

## 2. 目录与构建

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
  joint/include/sdurws/ird/opt/
    JointSearchOrchestrator.hpp   FeasibilityLayers.hpp   MiseliminationAudit.hpp
    RobustnessProtocols.hpp   CandidateApplication.hpp
  joint/src/JointSearchOrchestrator.cpp   FeasibilityLayers.cpp   MiseliminationAudit.cpp
      RobustnessProtocols.cpp   CandidateApplication.cpp
  gui/OptimizationPlugin.hpp   gui/OptimizationPlugin.cpp   gui/panels/
  test/StudyDefinitionTest.cpp   CandidatePatchTest.cpp   StaticConstraintTest.cpp
      StaticParetoTest.cpp   CacheDeterminismTest.cpp   ResultApplicationTest.cpp
      CrossEntryTest.cpp   JointSearchTest.cpp   FeasibilityLayersTest.cpp
      SchedulerCheckpointTest.cpp   ParetoRobustnessTest.cpp   AcceptanceEvidenceTest.cpp
  testdata/optimization/{studies,vectors,candidates,pareto,audit,robustness}/
  evidence/WP-20/   evidence/WP-21/
```

CMake target：`sdurws_ird_optimization_definition`（definition＋candidate 计算核心，无 Qt Widgets）、`sdurws_ird_optimization_definition_test`、`sdurws_ird_optimization_definition_contract_test`、`sdurws_ird_optimization_plugin`＋`sdurws_ird_optimization_gui_test`（WP-20-T07）、`sdurws_ird_optimization_joint`、`sdurws_ird_optimization_joint_test`。允许依赖：WP-03 core、WP-04 命令端口、WP-05 evidence（评估端口＋结果仓库）、WP-06 runtime（编译管线）、WP-07 policy、WP-08 execution（调度/缓存/检查点）、WP-09 diagnostics、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui；WP-21 对 WP-16～19 评估器仅经 WP-08 调度与 `ResultEnvelope` 交互，无业务插件代码依赖。禁止：业务插件互依、反射式字段写入、候选直写 revision、加权总分替代 Pareto、第二套调度/缓存。

## 3. 数据与接口

公共符号按注册名（SYM-OPT-001～013；`OptimizationStudyDefinition`/`OptimizationRunResult` 为规范名，单独 `OptimizationStudy` 禁用）；持久化以 `schemas/optimization-study.schema.json`、`schemas/candidate-patch.schema.json` 及同名 examples 为准。模块私有类型：

| 类型（模块私有） | 字段 | 规则 |
| --- | --- | --- |
| 注册表条目 | bindingId、所有者 WP、目标类型、parameterKey 签名、valueType/unit 校验器、允许阶段、writeSet | 首批条目见 §5；未注册路径→`IRD-OPT-UNREGISTERED-BINDING`（candidate-compilation §3） |
| 静态指标视图 | metricId、value、unit、来源 `ResultRef` | OPT-B 可算三项（§5.3）；其余五项阶段 C/D |
| `AuditSample` | 候选 ID、淘汰原因、分层键、Verified 复核结论 | §15.3 审计证据单元 |
| 方案分支应用包 | DesignVector＋writeSetFingerprint＋目标分支名 | 经 WP-04 `DomainCommand`；恰好一个新修订 |

## 4. 调用与状态

管线时序（两 WP 共用骨架）：研究定义校验（绑定注册→writeSet 两两互斥→DAG 无环→阶段锁→预算/种子/版本）→ 候选生成（`algorithmPolicy`）→ `DesignVector` → `CandidatePatch`（全成全败）→ `CandidateInputSnapshot` → 组合 WP-06 编译管线 → `CompiledCandidateArtifact` → 静态硬约束（拓扑/输入→运动学→碰撞；经 WP-08 调度评估器）→ 静态指标 → `DesignCandidate` → `ParetoSet` → `OptimizationRunResult` 追加（WP-05 仓库，不产生修订）→ 用户"设为当前方案"→ WP-04 命令创建方案分支＋一个新修订＋完整复算（OPT-08/AT-12；运行期间修订数不随候选数量增长）。预算耗尽＝`Completed + DataInsufficient + Complete`；取消/中断、检查点（批次边界）与恢复（原 runId＋新 attemptId、批次去重）按 WP-08 契约。错误矩阵（新码待 diagnostics.md 登记；`IRD-OPT-UNREGISTERED-BINDING/-WRITE-CONFLICT/-DOMAIN-VIOLATION/-CYCLE/-PATCH-REJECTED/-STAGE-LOCKED` 以 candidate-compilation §3～§6 为准）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-OPT-HARD-CONSTRAINT` | 候选违反硬约束（附 kind、实际值/阈值） | Engineering | Error | 候选标不可行，不入可行集与 Pareto |
| `IRD-OPT-CANDIDATE-COMPILE-FAILED` | 候选编译失败（结构边界/拓扑非法） | Engineering | Error | 候选淘汰并保留诊断，无部分工件 |
| `IRD-OPT-BUDGET-EXHAUSTED` | maxCandidates/maxWallClockS/maxVerifiedEvaluations 耗尽 | Engineering | Warning | 终止并落 DataInsufficient 锚点；可加预算后新 attempt 续跑 |
| `IRD-OPT-AUDIT-THRESHOLD-EXCEEDED` | 误淘汰率 >1% 或 95% 置信上界 >3% | Engineering | Error | 扩大保留池、禁用对应 Quick 规则重跑；禁发正式候选报告 |

## 5. 关键实现约定

1. **变量绑定注册表首批条目（模块冻结，登记入注册表；阶段标记按 candidate-compilation §3/§6）**：

| bindingId | 目标类型 | parameterKey 签名 | valueType/unit | 阶段 |
| --- | --- | --- | --- | --- |
| `robot.section-dimension` | RobotDesign | `links[i].section.{outerDiameter,innerDiameter,width,height}` | Real/m | StageB |
| `robot.dh-length-offset` | RobotDesign | `nativeDh.rows[i].{a,d}` | Real/m | StageB |
| `robot.joint-install` | RobotDesign | `joints[i].originPose.position.{x,y,z}` | Real/m | StageB |
| `robot.joint-range` | RobotDesign | `joints[i].limits.{lower,upper}` | Real/rad 或 m | StageB |
| `robot.base-pose` / `robot.tcp-offset` | RobotDesign | `basePose.position.*` / `defaultTcp.offset.position.*` | Real/m | StageB |
| `robot.link-material` | RobotDesign | `links[i].materialName` | Enumeration | StageB |
| `drivetrain.ratio` | DriveTrainDesign | `axes[i].ratio` | Real/dimensionless | StageB |
| `drivetrain.reducer-key` / `drivetrain.motor-key` | DriveTrainDesign | `axes[i].{reducerKey,motorKey}` | CatalogRef | StageD |

   writeSet＝各 parameterKey 展开的物理字段全集（逐变量声明、两两交集必须为空）；派生路径（mass/COM/inertia）不入 writeSet、按 DAG 拓扑序重算。`links[i].section` 依赖 `robot-design.schema.json` 增补（D5 提名）。改型项目默认锁定非授权参数（OPT-02）。
2. CandidatePatch 应用与候选编译：mutation 依"注册序＋DAG 拓扑序"；域/类型/单位逐项校验，任一失败整体拒绝且不留工件（§4 既有码）；派生重算 method∈{AnalyticEstimate, UniformScaling}——AnalyticEstimate 的解析公式以 robot-modeling.md §5 冻结公式表为唯一语义源（实现归属裁决见 D5 报告：提升共享 evaluation 包或端口注入，禁止双实现）；UniformScaling 仅在无截面几何信息时回退并记录。编译组合 WP-06 管线产出 `CompiledCandidateArtifact`（身份独立于 `CompiledRobotArtifacts`，symbol-registry §4.5）；候选稳定 ID 以 candidate-compilation §5 公式为准。
3. 静态指标（OPT-B 可算子集，八项中其余五项属阶段 C/D）：总体尺寸包络 `overallSizeEnvelope`（单位 m/m³ 随声明）、结构质量 `structureMass`（kg，派生重算后连杆质量合计）、最小关节裕量 `minJointMargin`（dimensionless，与 WP-15 同一公式）。节拍/器件成本/器件质量/关节侧正机械功/最小驱动裕量在 StageB 研究定义中引用即拒（复用 `IRD-OPT-STAGE-LOCKED`）；全局默认激活目标三项中节拍/器件成本在 StageB 不可声明，界面提示并引导改选可算目标。Quick 不得作正式通过证据；仅可证明保守的硬淘汰可直接执行（§9.4）。
4. Pareto：支配关系＝对全部激活目标 A 不劣于 B，且至少一目标严格优于超过该目标 `comparisonTolerance`；全目标差异均在容差内＝互不支配（无差别并列，§9.3）。软约束只警告/次级排序（SYM-OPT-009），不参与支配；不出现加权总分。Pareto 集结论措辞固定为"当前变量域、算法与计算预算内发现"（§9.4 步骤 7）。
5. Quick 淘汰与误淘汰审计（§9.4/§15.3 冻结值）：固定种子、按淘汰原因与目标值区间分层抽取淘汰候选的 5%、且不少于 200 个（淘汰数不足时全抽）执行 Verified 复核；"Quick 淘汰但 Verified 满足硬约束且相对已验证集合非支配"计误淘汰；误淘汰率 ≤1%、95% 置信上界 ≤3%，超限按 §4 矩阵处置。OPT-B 只允许可证明保守的直接淘汰；非保守规则一经启用必须审计（审计实现归 WP-21）。
6. 检查点/恢复/缓存走 WP-08（批次检查点与兼容校验；缓存键覆盖 studyDefinitionVersion、algorithmVersion、seed、threadCount，引用 execution-model §3）。联合搜索首版算法（WP-21，模块冻结、可经 `algorithmPolicy` 替换，OPT-10）：外层＝量化/离散网格枚举或固定种子拉丁超立方采样＋保留池；局部改进＝对非支配候选沿激活目标做坐标步长搜索（步长＝量化步长或域宽 2%）；内层＝Quick 运动学筛选→Verified 轨迹/动力（WP-16/17）→器件目录匹配（WP-19），无组合可行时把原因反馈外层。鲁棒性按 §15.3 三模式执行，覆盖对象与采样规则以该节为准。

## 6. 测试与证据

| 测试文件（target） | 覆盖 |
| --- | --- |
| StudyDefinitionTest / CandidatePatchTest（`_definition_test`） | 作用域引用、锁定、writeSet/DAG/域/阶段锁逐项拒绝且无部分状态；零值与未设置可区分；稳定 ID 跨线程一致 |
| StaticConstraintTest / StaticParetoTest（`_definition_test`） | 硬约束失败矩阵（Must 违反不入可行集）、指标容差支配、软约束不改变可行性、AT-09 静态子集 |
| CacheDeterminismTest / ResultApplicationTest / CrossEntryTest（`_definition_test`/`_contract_test`） | 缓存命中/拒绝矩阵、同种子复现、AT-12 修订不随候选增长、AT-19 与运动学入口一致 |
| JointSearchTest / FeasibilityLayersTest / SchedulerCheckpointTest / ParetoRobustnessTest / AcceptanceEvidenceTest（`_joint_test`） | OPT-01～10 全量、四层判定、检查点恢复统计、误淘汰审计、鲁棒性三模式、AT-10～14 与 NFR-PERF-04～06 |
| OptimizationGuiTest（`_gui_test`） | 变量域编辑、候选比较、静态证据、应用确认；不可行候选不可应用（WP-20-T07） |

证据写入 `evidence/WP-20/`、`evidence/WP-21/`：研究定义 JSON 样例、候选差异报告、Pareto 黄金集、缓存/复现矩阵、误淘汰审计、恢复统计、AT-09～14 记录与独立评审签名。验证命令（双形式，仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_(joint|gui)_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test sdurws_ird_optimization_definition_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test sdurws_ird_optimization_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_(joint|gui)_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `sdurws_structureoptimizer*` 加权总分与候选写回链路 | 删除（Rewrite 后不保留双轨） | 静态 Pareto 黄金数据与 AT-09/12 通过 |
| 旧结构优化器的采样/筛选行为 | 只读黄金对照（EvidenceOnly）后按新算法重写 | 行为差异报告归档 |
| 旧联合优化入口与重复缓存 | 验收后删除，不形成第二套调度/Pareto 实现 | WP-21 退出条件 |
| 旧目标 `sdurws_structureoptimizer*` | 不作依赖；对应阶段验收后退出构建与安装包 | 安装包审计 |
