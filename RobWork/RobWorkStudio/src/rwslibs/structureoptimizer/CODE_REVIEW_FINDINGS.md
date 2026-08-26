# 结构优化插件最终审查清单与修改计划

审查范围：优化器、评估器、评分器、变异器、缓存、区域覆盖、参数适配器、JSON 持久化、运行快照及 Widget 操作链路。

已存在的基线评估兼容性修复不在本计划中回退或重复实现。

## 0. 产品决策（2026-08-26 已确认）

| 编号 | 决策 | 结论 |
|---|---|---|
| D1 | 冻结需求是否允许在优化器内编辑 | **编辑标 stale 并阻断评估**：Tasks/Constraints 可编辑；一旦与 requirementExecution 指纹不一致，禁止 Start、Verified、Baseline 和导出正式结果；仅允许 Preflight/编辑；必须从需求源重新冻结 |
| D2 | Task Weight 的含义 | **参与可达性评分**：weightedReachability 改为 Σ(w·reachable)/Σw（报告中注明行为变更） |
| D3 | Objective Weight 的计分规则 | **自动归一**：total = Σ(w·score)/Σw ×100，存量项目无需迁移 |
| D4 | 纯 DH 模型支持范围 | **先明确拒绝**：三处门禁统一拒绝并给出明确提示；待补齐 CandidateCompiler/CanonicalForwardKinematics 审查后再评估支持 |
| D5 | `DataInsufficient` 与 `Infeasible` 的优先级 | **两类原因同时展示**：状态可仍判 DataInsufficient，但 violatedConstraints 与报告同时列出硬违反和数据缺口，证据不丢失 |

---

## 1. Critical：结果可能属于错误项目或错误需求

| 编号 | 问题 | 位置与修复目标 |
|---|---|---|
| C1 | **可编辑 Tasks 与 `requirementExecution` 双数据源脱节** | [StructureOptimizerWidget.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizerWidget.cpp)、[KinematicEngineeringEvaluator.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/KinematicEngineeringEvaluator.cpp)。不能直接用表格 Tasks 重建冻结契约；按 D1 实现只读或 stale/re-freeze 流程。Validation、Baseline、Verified 必须消费同一版本快照。 |
| C2 | **异步结果可写入新项目会话** | [StructureOptimizationController.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationController.cpp)、Widget。引入 `projectEpoch`；New/Open/Close/setProblem 时递增；优化和基线启动时捕获 `{projectEpoch, runId}`；完成与进度回调均须双重校验。禁用按钮只是辅助，不是安全边界。 |

---

## 2. Major：候选模型、约束或持久化结果不可信

| 编号 | 问题 | 修复要求 |
|---|---|---|
| M1 | 缺失变量 target 仅告警，仍按未修改模型评估 | [StructureDesignMutator.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureDesignMutator.cpp)。未知 target 必须令 mutation 失败；候选状态应为 Failed，不能进入缓存或评分。 |
| M2 | `RequiredTaskCollisionFree` 不强制碰撞检测 | 评估入口从全局配置、冻结契约和显式约束统一推导 `requiresCollisionEvidence`；没有检测器时返回 DataInsufficient/Failed，不能把未检测当作无碰撞。 |
| M3 | Link 几何变量轴映射错误 | [StructureOptimizationUiLogic.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationUiLogic.cpp)。先明确 `DrawableSpec::dimensions` 的 X/Y/Z 契约；建议以 `dimensionAxis` 或 `LinkDimensionX/Y/Z` 表达，不要继续依赖含义冲突的 `LinkWidth/LinkHeight`。 |
| M4 | ToolFrame 同时生成 `JointPosition*` 与 `TcpOffset*` | ToolFrame 只生成一组语义明确的变量；加载旧项目时检测同字段重复绑定，拒绝或迁移并告警。 |
| M5 | JointOrigin 单轴 patch 写入完整 XYZ，导致多轴冲突 | [JointParameterAdapters.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/JointParameterAdapters.cpp)。patch 只写其所属分量；增加 X+Y、X+Z、三轴合并测试。 |
| M6 | 缓存命中覆盖本次 candidate identity | 缓存只保存评估载荷；命中后保留本次 `index`、`values`、stage 和运行身份。 |
| M7 | 离散选项经 SI Envelope 往返后变空字符串 | [StructureOptimizationJson.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/StructureOptimizationJson.cpp)。JSON 类型必须对称；读取兼容历史 string/number 两种编码。 |
| M8 | TaskPoint 的 tolerance/type/note 未持久化 | 补齐双向 JSON；保证 Quick 评估保存重载后语义不变。 |
| M9 | 非有限数写为 null 后读为 0 | 建立统一 `checkedDouble()`；null、非法类型、非有限数必须报告解析错误，绝不静默归零。 |
| M10 | DH 与几何变量被误判为混合运动学 | 仅 Transform 类变量参与 `hasTransform`；DH 与几何变量必须允许共存。 |
| M11 | 纯 DH 在 Preflight、Validation、Baseline 三处结论矛盾 | 按 D4 决策。未完成全链路验证前，宁可明确拒绝，不可让按钮可点但运行失败。 |
| M12 | 区域方向覆盖失败没有违反项 | Region 结果应同时记录 position/orientation coverage；方向阈值未达标时写入有 region id 的 `violatedConstraints`。 |
| M13 | `randomSeed` 写 qint64、读 int | [OptimizationRunJson.cpp](D:/10_Source_Repos/21_robot/RobWork/RobWork/RobWorkStudio/src/rwslibs/structureoptimizer/OptimizationRunJson.cpp)。使用无损整数读取并校验范围，保证复现链完整。 |
| M14 | 基线运行时可关闭/切换项目；完成回调会误写状态 | `canCloseProjectDocument()` 检查 baseline；Start/Baseline/New/Open 共用会话状态门禁，并与 C2 的 epoch 机制配合。 |
| M15 | Controller 可绕开 UI Preflight 进入不安全核心路径 | Controller/优化入口必须执行模型级校验；Cache 和 Generator 自身也要安全处理空 values、`step <= 0`、非法数量。 |
| M16 | 历史候选预览使用当前 `collectProblem()` | 预览应使用运行时 immutable snapshot；快照不匹配时拒绝预览并说明原因。 |

---

## 3. Minor / 韧性与体验

| 编号 | 问题 | 修复要求 |
|---|---|---|
| S1 | CSV 使用 `std::to_string`，精度只有约 6 位 | 使用 `QByteArray::number(value, 'g', 17)` 或 classic-locale stream；CSV 数值统一不依赖环境 locale。 |
| S2 | Grid 不包含 maximum 上界 | `steps > 1` 时步长使用 `(max-min)/(steps-1)`；`steps == 1` 明确采样 current 或 minimum。 |
| S3 | 进度 bestScore 包含不可行候选 | 分开显示 `bestAnyScore` 与 `bestFeasibleScore`，或者仅显示后者。 |
| S4 | `maxLocalSweeps == 0` 被悄悄强制为 1 | 由入口校验拒绝，或明确让 0 表示关闭局部搜索。 |
| S5 | 排序比较器未防御 NaN | 所有参与排序的指标必须有限；NaN 候选直接 Failed 或稳定排到末尾。 |
| S6 | PointAtTarget 的 cells × rollSamples 可绕开复合采样上限 | 统一按实际生成目标数计算上限，覆盖所有 orientation mode。 |
| S7 | 每次单元格编辑同步读盘、解析并计算模型指纹 | 以文件变更事件或去抖异步检查替代；运行前仍需强制一次同步确认。 |
| S8 | `syncAssociatedGeometry` 无实际语义消费者 | 删除该字段，或实现真正可控的联动逻辑；不能保留装饰性配置。 |
| S9 | baseline 取消仍可能走 completed 语义 | 完成信号需保留 cancellation 状态；UI 显示“已取消”而非成功基线。 |
| S10 | 时间戳使用本地时间与 `std::localtime` | 使用 UTC ISO-8601，例如 `2026-08-26T12:34:56Z`。 |
| S11 | Constraint/Task 表模型编辑语义不一致 | 不可编辑列不要设置 Editable；ID 需非空、唯一；数值需有限校验。 |
| S12 | Run JSON 的 warnings、负 byteSize、数值字段校验不完整 | 补齐 warnings 往返，拒绝负值和非法数值。 |
| S13 | CSV audit 仍偏 legacy 单 coverage box | 多区域完整导出；变量数和值数不一致时明确失败，不能静默错位。 |
| S14 | UNC 路径公共目录解析风险 | 对 `//server/share/...` 增加专门单测并保留 UNC 前缀。 |

---

## 4. 观察项：暂不纳入修复承诺

- Region evaluator 将“中途中止”归为 DataInsufficient：底层行为存在，但最终状态如何表达取决于 D5。
- `DataInsufficient` 覆盖 `Infeasible`：是状态优先级策略，不应在没有产品决策时修改。
- Variable Table 的 `CheckStateRole` 未同时发 `DisplayRole`：没有可靠复现，不列为缺陷。
- 参数适配器的锥角守卫、直连 `compilePatch` 校验：需要结合 AdapterRegistry 的调用边界继续验证。
- CandidateCompiler、PatchApply/Merge、CanonicalForwardKinematics、TargetEvaluator、KinematicAnalyzer 仍未逐行完成审查，属于残余风险。

---

# 修改计划

## Phase 0：先写决策与不可变快照边界

完成 D1–D5，并定义：

```text
OptimizationSessionSnapshot
  - projectEpoch
  - runId
  - immutable problem
  - requirementExecution fingerprint
  - variable schema fingerprint
  - model fingerprint
```

所有优化、基线、预览、导出都必须引用明确的 snapshot，而不是运行后重新读取 UI。

验收：

- 编辑后，系统要么阻止 Verified/Baseline，要么明确显示重新冻结后的契约版本。
- 切换项目后，旧任务任何完成事件都不能写入新项目。

## Phase 1：结果可信性

修复：C1、C2、M1、M2、M6、M14、M15、M16。

顺序：

1. 建立 project epoch 与 run snapshot；
2. 完成回调双重校验；
3. 缺失 target 直接失败；
4. 显式碰撞约束强制产生碰撞证据；
5. 缓存载荷与候选身份分离；
6. 预览/导出绑定运行快照。

测试：

- 编辑冻结项目任务后无法静默跑旧契约；
- 优化中切换项目，旧结果被丢弃；
- 无 target、无碰撞检测器、缓存命中重复 index 均有回归测试；
- 预览历史候选时修改变量定义，必须拒绝或使用原快照。

## Phase 2：变量与模型表达正确性

修复：M3、M4、M5、M10、M11。

顺序：

1. 定义 dimensions 正式轴语义；
2. 引入显式轴绑定并迁移旧变量；
3. 删除 ToolFrame 重复变量；
4. 让 patch 做分量级写入；
5. 修正 DH/几何混合检查；
6. 按 D4 完成纯 DH 支持或明确拒绝。

测试：

- 三轴尺寸变量各自只影响对应分量；
- ToolFrame 同一字段不可重复绑定；
- JointOrigin XY/XZ/XYZ 合并均成功；
- DH + LinkRadius 可运行；
- 纯 DH 三个门禁结论一致。

## Phase 3：持久化、导出与可复现性

修复：M7–M9、M13、S1、S12–S14。

原则：

- 所有 JSON 数值使用统一 checked reader；
- schema version 必须验证；
- 读旧格式要兼容，写新格式要唯一；
- 数值 round-trip 采用精确比较或明确容差；
- 不允许“解析失败后采用 0”。

测试：

- 离散选项、Task tolerance/type/note、随机种子、warnings 全部往返保值；
- null、NaN、Inf、负 byteSize、未来 schema 被明确拒绝；
- CSV 以 17 位有效数字输出，多区域 audit 可完整回读。

## Phase 4：评分与区域语义

在 D2、D3、D5 确认后修复：M12、S3、S5，以及 Task/Objectives 权重逻辑。

验收：

- 权重改变会按已定义的产品语义改变排序；
- 方向覆盖失败显示区域 id、实际值、阈值；
- 进度不会将不可行高分误称为最佳可行解；
- NaN 不参与正常排序。

## Phase 5：性能与 UX 收尾

修复：S2、S4、S6–S11。

验收：

- Grid 同时采样 minimum 和 maximum；
- 大区域姿态采样在生成前被限制；
- 连续编辑不会反复读盘、解析和哈希；
- 基线取消、关闭项目、按钮禁用、表格编辑提示语义一致。

---

## 回归策略

所有纯模型测试放入 `StructureOptimizationTest.cpp`；Widget 测试在 Windows Visual Studio x64 环境下单独运行，设置 `QT_QPA_PLATFORM=windows`。每个修复至少包含：

1. 一个最小复现测试；
2. 一个保存/重载或异步边界测试（如适用）；
3. 修复前失败、修复后通过的断言；
4. 相关已有测试全量回归。
