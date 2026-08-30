# 优化与候选编译模块详细方案（optimization）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（IRD-D10-20260829 联合评审通过，待签署）
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
  out/test-evidence/wp-20/<run-id>/   out/test-evidence/wp-21/<run-id>/
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

证据写入 `out/test-evidence/wp-20/<run-id>/`、`out/test-evidence/wp-21/<run-id>/`：研究定义 JSON 样例、候选差异报告、Pareto 黄金集、缓存/复现矩阵、误淘汰审计、恢复统计、AT-09～14 记录与独立评审签名。验证命令（双形式，仓库根执行）：

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

## 8. 联合分层优化工作台界面

本节定义优化研究、分层运行、Pareto 候选和方案采用的工程界面。优化页引用当前模型、需求、轨迹、动力学和选型结果，不在本页重复编辑工位、负载或公共工程设置。

### 8.1 页面结构与模式

```text
┌────────── 优化 ──────────┬──────────── 三维视图 ───────────────┬── 候选详情 ──┐
│ 新机设计 / 改造设计      │                                     │ 变量摘要     │
│ 静态评估 / 关节优化      │          候选方案预览               │ 目标/约束     │
│ 变量  目标  约束  设置   │                                     │ 主要指标     │
│                          │                                     │ 稳健性       │
│ [开始] [暂停] [取消]     │                                     │ [正式复核]   │
├──────────────────────────┴─────────────────────────────────────┴───────────────┤
│ 候选 │ Pareto │ 比较 │ 快速淘汰 │ 稳健性 │ 任务 │ 诊断                     │
└────────────────────────────────────────────────────────────────────────────────┘
```

| 模式 | 适用场景 | 界面限制 |
| --- | --- | --- |
| 新机设计 | 允许在批准范围内改变结构和传动 | 显示全部可用变量 |
| 改造设计 | 保留既有设备并只开放授权变量 | 锁定变量显示只读原因 |
| 静态评估 | 使用结构、运动学和静态指标快速探索 | 隐藏节拍、动力学和器件类不可算目标 |
| 关节优化 | 完整分层计算轨迹、动力学和选型 | 仅在所需阶段能力可用时启用 |

### 8.2 变量

变量表：

| 变量 | 对象 | 类型 | 下限 | 上限 | 步长/候选集 | 启用 | 状态 |
| --- | --- | --- | ---: | ---: | --- | --- | --- |

首批用户名称：连杆截面、DH 长度/偏置、关节安装位置、关节范围、基座位置、TCP 偏置、连杆材料、传动比、电机、减速器。表格只显示工程名称和对象名称，不显示内部字段路径或对象标识。

操作：添加变量、复制、移除、启用/停用、批量设置范围。变量范围必须位于允许域内；写入范围冲突时在对应行显示问题并禁用“开始”。改造模式下未授权变量不可启用。

### 8.3 目标与约束

目标从八项指标中选择：尺寸包络、结构质量、节拍、关节侧正机械功、器件质量、器件成本、最小关节裕量、最小驱动裕量。

| 目标 | 方向 | 目标值 | 容差 | 启用 | 状态 |
| --- | --- | ---: | ---: | --- | --- |

默认目标为节拍、质量、成本；静态模式自动改为当前可计算的指标，并说明“完整目标需要关节优化”。选择五项及以上时显示“目标较多，结果可能较难比较”，但不阻止运行。

约束表：

| 约束 | 对象 | 类型 | 要求 | 当前值 | 启用 | 状态 |
| --- | --- | --- | --- | --- | --- | --- |

类型为硬约束、工程约束、偏好。硬约束必须包括：模型有效；Must 工位可达且无碰撞；Must 区域达到覆盖率；关节限制、轨迹限制、动力学限制；器件能力与兼容性。安全要求和需求中的必须项固定为硬约束，不能降级或关闭；工程约束可调整，偏好只用于查看和次级排序，不改变可行性。页面不提供偏好权重，也不计算加权总分。

### 8.4 设置与运行控制

| 分组 | 字段 |
| --- | --- |
| 常用 | 搜索方式、候选数量、正式复核数量、计算预算 |
| 高级 | 随机种子、线程数、局部搜索步长、保留池、快速淘汰审计、稳健性采样 |

运行区顶部显示当前运行阶段和总体进度百分比，进度只反映预算消耗和批次完成情况。任务区显示漏斗统计：

| 阶段 | 输入数 | 通过数 | 淘汰数 | 进行中 | 主要原因 |
| --- | ---: | ---: | ---: | ---: | --- |
| 候选生成 |  |  |  |  |  |
| 静态约束 |  |  |  |  |  |
| 运动学 |  |  |  |  |  |
| 轨迹/动力学 |  |  |  |  |  |
| 器件匹配 |  |  |  |  |  |
| 正式复核 |  |  |  |  |  |
| Pareto 整理 |  |  |  |  |  |
| 鲁棒性复核 |  |  |  |  |  |

| 操作 | 启用条件 | 状态规则 |
| --- | --- | --- |
| 开始 | 变量、目标、约束和预算合法 | 运行中变为禁用 |
| 暂停 | 当前任务支持暂停 | 请求后显示“正在暂停” |
| 继续 | 任务已暂停 | 从已完成批次继续 |
| 取消 | 任务运行或暂停 | 保留已完成候选并标明完整性 |
| 继续上次 | 存在兼容的未完成研究 | 输入已变化时禁用并提示重新开始 |

进度自动保存，界面只显示研究名称、进度和更新时间，不显示内部任务标识。

### 8.5 候选与 Pareto 图

候选表：

| 候选 | 可行性 | 节拍 | 质量 | 成本 | 能耗 | 关节裕量 | 碰撞裕量 | 稳健性 | 状态 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |

不可计算指标显示“—”并说明阶段限制，不填零。候选表不提供总分列；默认先显示可行且非支配候选，再显示可行候选和不可行候选。

Pareto 图位于底部结果标签页，中央区域仍为 RobWorkStudio 三维视图。图形设置：横轴、纵轴、颜色、点大小、筛选、仅显示正式复核。点的悬停卡片显示候选名称、四个主要指标和状态；点击后同步三维预览和候选详情。

结论措辞统一为“在当前变量范围和计算预算内发现这些候选”，不得表述为已找到全局最优。

### 8.6 候选详情、比较与审计

候选详情：变量变化、目标指标、硬约束、主要裕量、目录版本、计算依据、稳健性和遗留问题。内部路径和中间文件不作为字段显示。

候选比较支持 2～4 个候选，按八项指标、关键变量、硬约束和稳健性并列。只高亮各指标差异，不生成推荐分或综合排名。

快速淘汰表：

| 原因 | 淘汰数 | 抽查数 | 误淘汰数 | 结论 | 操作 |
| --- | ---: | ---: | ---: | --- | --- |

误淘汰审计未通过时，正式候选报告和“设为当前方案”禁用，并提示扩大保留池或关闭对应快速规则。

稳健性表：

| 候选 | 工况变化 | 样本数 | 可行比例 | 最差指标 | 结论 |
| --- | --- | ---: | ---: | --- | --- |

稳健性页只对最终候选显示，模式为有界公差验证、概率鲁棒性和敏感度抽查。未提供公差或概率分布时，只能显示敏感度参考，不得给出概率可靠性结论。

### 8.7 预览、正式复核与采用

| 操作 | 启用条件 | 行为 |
| --- | --- | --- |
| 预览 | 已选择候选 | 三维显示候选，不修改当前方案 |
| 加入比较 | 比较项少于 4 | 加入候选比较 |
| 正式复核 | 候选通过当前层级且输入有效 | 使用正式计算复核完整链路 |
| 设为当前方案 | 正式复核通过、误淘汰审计通过、稳健性复核完成且结果有效 | 打开采用确认 |

采用确认显示候选名称、关键变量变化、主要指标、硬约束和将要重算的阶段。用户确认后创建一个新方案版本；原方案保留，候选运行期间不产生项目版本。新方案必须重新计算相关阶段后才能用于正式报告。

### 8.8 页面状态

| 场景 | 提示 | 主要操作 |
| --- | --- | --- |
| 无变量 | 请先选择优化变量 | 添加变量 |
| 无可用目标 | 当前模式没有可计算目标 | 切换模式 |
| 输入冲突 | 变量范围或约束存在冲突 | 定位问题 |
| 无可行候选 | 当前范围内没有满足硬约束的候选 | 查看淘汰 |
| 预算耗尽 | 已保留当前发现的候选，结论依据不足 | 增加预算 |
| 审计未通过 | 快速淘汰可能遗漏有效候选 | 调整规则 |
| 运行失败 | 已保留完成候选和研究设置 | 重试 |

系统错误不得清空变量、目标、约束、比较列表和已完成候选。
