# 运动学设计空间与候选模型编译器设计

## 目标

将现有 `structureoptimizer` 插件从依赖模型字段猜测优化变量的结构优化工具，重构为面向机械臂研发工程师的“运动学设计空间定义器 + 候选模型编译器 + 多阶段评估器”。第一阶段聚焦运动学结构最优设计，同时为轨迹规划、动力学和电机/减速器选型提供稳定扩展边界。

## 范围与非目标

本阶段包含：

- 独立的规范运动学链模型；
- 由设计意图驱动的变量定义和模板；
- 变量到机器人模型、显示几何和碰撞几何的真实适配器；
- 运动学、碰撞和工作空间的分阶段评估；
- 可配置目标、硬约束和软约束；
- 工作台式 UI、项目持久化、结果诊断和报告追溯；
- 对现有 `StructureOptimizationProblem`、JSON 和异步控制器的兼容。

本阶段不实现轨迹规划、动力学求解、电机/减速器数据库或混合离散优化算法，但接口必须允许后续以流水线阶段和指标注册方式接入。

## 设计决策

### 规范运动学模型

新增 `KinematicChainModel` 作为优化器唯一的运动学语义来源：

- `Joint[i]`：关节类型、单位轴向量、关节原点、父到关节刚体变换、连杆长度/偏置、零位偏置；
- `basePose` 和 `tcpPose`：完整 6D 位姿；
- 关节限位和模型来源信息。

模型表达与优化变量表达分离。关节轴内部使用单位向量或旋转增量，界面提供轴偏转 X/Y 与零位偏置；基座和 TCP 对外显示 6D 位姿，内部不把欧拉角作为持久化计算状态。DH 仅作为导出或检查视图，不作为优化器的主状态。

### 变量定义与模板

`DesignVariableDefinition` 至少包含：

`id`、`semanticKind`、`targetPath`、`nominalValue`、`minimum`、`maximum`、`unit`、`domain`、`enabled`、`dependencies`、`adapterId`、`source`。

第一阶段语义类型为 `LinkLength`、`JointAxisTiltX`、`JointAxisTiltY`、`JointZeroOffset`、`BasePoseTranslation`、`BasePoseRotation`、`TcpPoseTranslation`、`TcpPoseRotation`、`JointLimitLower` 和 `JointLimitUpper`。

变量是否存在由设计意图或用户选择决定，与名义值是否为零无关。提供 `Kinematic Basic`、`Kinematic + Joint Axis`、`Kinematic + Base/TCP`、`Full Kinematic Design` 四个模板。模板只生成完整变量定义集合，用户可以逐项启用、禁用和修改范围。后续动力学和执行器模板可复用同一协议。

### 适配器与几何边界

候选模型编译器通过显式适配器修改规范模型并生成输出：

- `LinkPoseAdapter`：由长度、偏置和轴参数计算关节/连杆刚体变换；
- `ParameterizedGeometryAdapter`：生成由参数控制的显示或碰撞几何；
- `MeshTransformAdapter`：对真实网格执行受控变换，仅开放明确支持的缩放和位姿参数。

运动学几何、显示几何和碰撞几何分别建模。没有真实适配器的字段不能成为优化变量，避免出现界面值变化但网格或碰撞模型不变的假变量。

### 目标与约束

目标使用可扩展的 `ObjectiveTerm`：指标 ID、最大化/最小化方向、权重、归一化方式、启用状态和软/硬属性。第一阶段指标包括任务可达率、工作空间覆盖率、可操作度、碰撞安全率、关节裕度和机构总长度/紧凑度。

约束使用 `ConstraintRule`：指标或白名单表达式、关系（`<=`/`>=`/`==`/区间）、阈值、容差、严重级别、软约束惩罚和启用状态。硬约束决定候选可行性，软约束进入评分惩罚。表达式不开放任意脚本，保证可审计和可持久化。

### 候选编译与评估流水线

候选处理流程为：

```text
DesignVector
  -> KinematicChainModel
  -> CandidateModelCompiler
  -> RobotModelSpec / PresentationGeometry / CollisionGeometry
  -> ModelValidation
  -> KinematicEvaluator
  -> ObjectiveAggregator + ConstraintEvaluator
  -> CandidateResult
```

编译结果包含规范模型、机器人模型、两类几何、派生指标、警告和错误。非法轴向量、退化几何、越界限位、依赖循环和适配器失败在进入评估器前拒绝。候选失败保留为有诊断的结果，不中断整个批次。

现有 Random/Grid/Hybrid 生成器继续使用，但消费变量域：连续变量按范围采样或网格化，整数变量按步长采样，离散变量按选项枚举。后续动力学和执行器模块通过注册新流水线阶段和指标接入。

## UI 结构

界面重组为四个工程工作区：

1. **设计空间**：模板、语义分类、名义值、范围、单位、域、依赖和适配器状态；支持批量启用/禁用、恢复名义值和范围校验。
2. **目标与约束**：可排序目标表和约束表，编辑指标、方向/关系、权重/阈值、归一化、软硬属性；显示问题预检查结果。
3. **求解与结果**：保留异步运行、暂停、取消、进度、候选表格和 3D 预览；增加编译诊断、分项得分、约束违反原因、基线对比、Pareto 和敏感性入口。
4. **报告与工程资源**：延续 Provider、JSON/CSV/Markdown 和最佳模型导出；增加设计空间、适配器、指标配置、编译器版本和运行指纹摘要。

结果页只消费 `CandidateResult`，不在 UI 内重复计算工程指标。

## 持久化与兼容

保留 `StructureOptimizationProblem` 顶层对象，新增 `designSpace`、`compiler` 和 `pipeline` 字段，并继续支持既有 `objectives` 与 `metricConstraints`。旧项目加载时将旧变量映射为兼容变量定义；无法可靠映射的字段标记为 `legacy/unbound`，禁止参与优化并显示迁移提示。保存时保留未知 JSON 字段，避免未来动力学和选型数据丢失。项目和结果写入模板版本、编译器版本和指标注册表版本。

## 错误处理

- 编译失败候选进入 `CompileFailed` 状态并携带字段路径诊断；
- 约束无效、范围反转、依赖循环和未知指标阻止运行并在 UI 指明修复项；
- 单个候选评估失败不影响独立候选；全局模型或评估器错误终止运行并保留已完成结果；
- 未绑定旧变量默认禁用，用户完成迁移后才能重新启用。

## 验证策略

核心测试覆盖：规范模型与 `RobotModelSpec` 映射、零值参数模板生成、轴偏转与基线保持、几何适配器实际变更、编译失败诊断、约束依赖循环、变量域候选生成和新旧 JSON round-trip。优化器测试验证与现有异步流程的结果一致性。Qt Widget 测试覆盖模板应用、变量/目标/约束编辑、迁移提示、候选诊断和结果摘要。Windows GUI 测试遵守 `QT_QPA_PLATFORM=windows`、Visual Studio x64 开发环境和一次只启动一个绝对路径可执行文件的仓库规则。

## 后续扩展

动力学阶段增加质量、质心、惯量（采用正定参数化，如 Cholesky 分解）和负载变量；执行器阶段增加电机/减速器离散变量、档位和派生额定指标。混合优化可采用外层离散组合枚举、内层连续变量优化、最终局部精修与约束复核，保持本阶段编译器和评估器协议不变。
