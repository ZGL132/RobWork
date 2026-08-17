# 机械臂优化设计工作台：运动学设计空间与候选模型编译器

## 1. 文档信息

| 项目 | 内容 |
| --- | --- |
| 文档类型 | 技术设计规范 |
| 目标插件 | `RobWorkStudio/src/rwslibs/structureoptimizer` |
| 第一阶段 | 运动学结构优化 |
| 目标用户 | 机械臂结构、运动学和系统研发工程师 |
| 状态 | 已确认设计 |
| 兼容要求 | 既有优化项目 JSON、Project Provider 和异步控制器 |

## 2. 背景与问题

现有插件已经具备设计变量、任务点、约束、多目标评分、候选解、异步运行和项目导入导出能力，但变量生成仍然容易依赖模型字段和当前值推断。该方式会产生以下问题：

- `rpyDeg + pos`、DH 参数和 `RobotModelSpec.transformJoints` 可能同时表达同一运动学语义；
- 零值字段被误判为“没有优化意义”，导致可设计参数无法出现；
- 界面变量可能没有真实模型适配器，修改后几何、碰撞模型或运动学模型不发生变化；
- 模型表达、优化向量、显示几何和碰撞几何的边界不清晰；
- 后续动力学和执行器选型无法在不改写现有优化器的情况下加入质量、惯量、离散型号等变量。

本设计将插件的核心职责重新定义为：

> 运动学/动力学设计空间定义器 + 候选模型编译器 + 多阶段评估器。

第一阶段只实现运动学，但所有接口按后续完整系统优化设计预留。

## 3. 目标与非目标

### 3.1 目标

1. 用独立规范模型表达机械臂运动学链，消除 DH、Transform 和 UI 字段之间的隐式耦合。
2. 让优化变量由设计意图、变量模板和真实适配器决定，而不是由当前值是否为零决定。
3. 支持连续、整数和离散变量域，为后续减速比和器件型号选择保留统一协议。
4. 将候选方案生成、模型编译、有效性检查、指标评估和结果解释拆成可测试阶段。
5. 允许工程师灵活配置运动学目标、硬约束和软约束，并能解释每个候选为什么可行或失败。
6. 保持现有项目资源管理、异步控制、候选预览、JSON/CSV/报告导出能力。

### 3.2 非目标

本阶段不实现：轨迹规划和轨迹跟踪误差评估、逆动力学和热模型、电机/减速器数据库和选型求解、全新的进化算法，以及任意用户脚本作为目标或约束表达式。

## 4. 总体架构

```text
WorkCell / RobotModelSpec / Requirement
              |
              v
      KinematicModelImporter
              |
              v
      KinematicChainModel
              |
       DesignSpaceRegistry <--- VariableTemplateRegistry
              |
              v
      DesignVectorGenerator
              |
              v
      CandidateModelCompiler
       |        |        |
       v        v        v
 RobotModel  Display   Collision
   Spec      Geometry  Geometry
              |
              v
       ModelValidationStage
              |
              v
       KinematicEvaluationStage
              |
              v
 ObjectiveAggregator + ConstraintEvaluator
              |
              v
 CandidateResult / Report / Preview
```

### 4.1 组件边界

| 组件 | 职责 | 不负责的内容 |
| --- | --- | --- |
| `KinematicModelImporter` | 从当前机器人模型构造规范链模型 | 不创建优化变量 |
| `KinematicChainModel` | 保存唯一运动学语义 | 不执行评分 |
| `DesignSpaceRegistry` | 保存变量定义、模板、依赖和适配器注册 | 不直接修改 WorkCell |
| `DesignVectorGenerator` | 按变量域生成候选向量 | 不编译模型 |
| `CandidateModelCompiler` | 将规范模型和设计向量编译为候选模型 | 不决定目标权重 |
| `ModelValidationStage` | 检查模型、几何和变量依赖合法性 | 不做优化排序 |
| `KinematicEvaluationStage` | 计算运动学指标 | 不修改候选模型 |
| `ConstraintEvaluator` | 评估硬/软约束并生成诊断 | 不生成候选 |
| `ObjectiveAggregator` | 归一化并计算加权结果 | 不访问 UI |
| `EvaluationPipeline` | 编排阶段、缓存、取消和阶段结果 | 不持有 Qt 控件 |
| `StructureOptimizerWidget` | 编辑配置、展示状态和触发控制器 | 不计算工程指标 |

## 5. 规范运动学模型

### 5.1 数据结构

```text
KinematicChainModel
  schemaVersion
  modelId
  sourceFingerprint
  basePose: RigidPose
  tcpPose: RigidPose
  joints: [KinematicJoint]
  links: [KinematicLink]
  limits: [JointLimit]
  metadata

KinematicJoint
  id
  name
  type: Revolute | Prismatic | Fixed
  parentLinkId
  childLinkId
  axis: UnitVector3
  origin: Vector3
  parentToJoint: RigidTransform
  zeroPositionOffset

KinematicLink
  id
  parentJointId
  childJointId
  nominalLength
  nominalOffset
  geometryRefs
```

### 5.2 姿态约定

- 内部姿态使用 `RigidTransform` 和旋转增量/SO(3) 表达；
- 关节轴使用单位向量，优化变量使用相对基准轴的 X/Y 偏转；
- 关节绕自身轴的旋转不作为轴姿态变量，避免与 `JointZeroOffset` 重复；
- 基座和 TCP 对外提供 Tx/Ty/Tz 与 Rx/Ry/Rz 编辑项，保存时转换为无奇异性的内部表示；
- DH 参数只作为导出、诊断和人工检查视图，不能成为第二套运行时状态。

### 5.3 导入与基线快照

导入器必须同时生成规范链模型、原始 `RobotModelSpec` 快照、WorkCell/设备/工具来源指纹，以及规范模型到原始字段的映射表。基线快照用于恢复名义模型、比较候选和检测模型源文件陈旧。导入失败必须指出设备 ID、关节路径和具体字段。

## 6. 设计变量与变量域

### 6.1 创建流程

变量创建不再根据当前值是否为零进行推断，而按以下流程完成：

1. `KinematicModelImporter` 导入规范运动学模型和基线快照；
2. 用户选择变量模板；
3. 模板根据模型拓扑、关节类型和已注册适配器生成变量定义；
4. UI 预览新增、冲突、未绑定和不适用变量；
5. 用户确认后写入 `DesignSpaceRegistry`；
6. 运行前执行范围、依赖、坐标系、单位和适配器预检查。

名义值为零的参数仍然生成并显示。模板不能静默覆盖已经被用户修改过的变量。

### 6.2 变量定义

```text
DesignVariableDefinition
  id
  semanticKind
  targetPath
  nominalValue
  minimum
  maximum
  step                 # 整数/离散变量可选
  unit                 # 内部单位：m 或 rad；UI 可显示 mm 或 deg
  frameId              # 对位置/姿态变量必填
  domain: Continuous | Integer | Discrete
  discreteOptions[]
  enabled
  dependencies[]
  adapterId
  source: User | Template | Imported | Legacy
  applicability        # joint/link type 和模型能力条件
  description
```

`nominalValue` 永远独立于 `enabled`。每个变量必须能追溯到唯一语义路径、坐标系和真实适配器。

### 6.3 第一阶段六大变量类别

第一阶段只优化机械臂运动学结构和参数化几何，不包含质量、惯量、电机、减速器或轨迹参数。

#### A. 连杆与关节安装位置

| semanticKind | 含义 | 适用条件 |
| --- | --- | --- |
| `LinkLength(linkId)` | `Joint[i]` 到 `Joint[i+1]` 的结构长度 | 连杆存在参数化长度适配器 |
| `JointOriginOffsetX/Y/Z(jointId)` | 父连杆坐标系到当前关节原点的固定平移 | 关节原点采用笛卡尔偏置表达 |
| `JointOffsetAlongAxis(jointId)` | 沿当前关节基准轴的固定偏置 | 模型选择轴向偏置表达 |

`JointOriginOffsetX/Y/Z` 与 `JointOffsetAlongAxis` 不能同时表达同一物理自由度。`LinkLength` 与相邻关节原点距离也必须明确一个为主变量，另一个只能作为派生值。

#### B. 关节轴线

| semanticKind | 含义 | 适用条件 |
| --- | --- | --- |
| `JointAxisTiltX(jointId)` | 基准轴局部 X 方向偏转 | Revolute 或 Prismatic |
| `JointAxisTiltY(jointId)` | 基准轴局部 Y 方向偏转 | Revolute 或 Prismatic |

基准轴来自导入模型的关节局部坐标系，不是世界坐标系。编译器将 `nominalAxis` 和旋转增量转换为新的单位轴向量，并检查：

```text
axis.norm == 1
axisTiltMagnitude <= maxTiltAngle
```

通常不开放绕关节自身轴的旋转变量，因为它会与 `JointZeroOffset` 重复。

#### C. 关节零位与运动范围

| semanticKind | 含义 | 适用条件 |
| --- | --- | --- |
| `JointZeroOffset(jointId)` | 编码器零位或机械零位的关节位置偏置 | 可动关节 |
| `JointLimitLower(jointId)` | 关节运动下限 | 可动关节 |
| `JointLimitUpper(jointId)` | 关节运动上限 | 可动关节 |

必须满足：

```text
JointLimitLower < JointLimitUpper
```

转动关节使用弧度存储、角度显示；移动关节使用米存储，可显示毫米。零位偏置不改变关节轴方向。

#### D. 基座位姿

| semanticKind | 含义 |
| --- | --- |
| `BaseTx` / `BaseTy` / `BaseTz` | 基座相对于场景参考系的 X/Y/Z 平移 |
| `BaseRx` / `BaseRy` / `BaseRz` | 相对于原始基座姿态的旋转增量 |

`BaseTz` 包含传统 `BaseHeight` 的语义，但不再限制只能优化 Z 方向。位置变量必须声明场景参考系；旋转变量内部使用旋转向量或 SO(3) 增量，不能把欧拉角作为运行时主状态。

#### E. TCP / 法兰位姿

| semanticKind | 含义 | 适用条件 |
| --- | --- | --- |
| `TcpTx` / `TcpTy` / `TcpTz` | 法兰到 TCP 的平移偏置 | 模型定义 TCP |
| `TcpRx` / `TcpRy` / `TcpRz` | 法兰到 TCP 的旋转偏置 | 模型定义 TCP |
| `FlangeOffsetX/Y/Z` | 法兰安装偏置 | 模型显式区分法兰 |
| `FlangeRotationX/Y/Z` | 法兰安装旋转偏置 | 模型显式区分法兰 |

`Tcp*` 与 `Flange*` 必须根据模型边界分工，不能同时修改同一个末端刚体变换。若模型没有显式法兰层，则只开放 TCP 变量。

#### F. 参数化连杆几何

| semanticKind | 含义 | 适用条件 |
| --- | --- | --- |
| `LinkRadius(linkId)` | 圆柱/圆管连杆半径 | `ParameterizedGeometryAdapter` |
| `LinkWidth(linkId)` | 矩形或盒状截面宽度 | 参数化矩形截面 |
| `LinkHeight(linkId)` | 矩形或盒状截面高度 | 参数化矩形截面 |
| `LinkCrossSectionX/Y(linkId)` | 截面参数 | 对应截面适配器 |
| `LinkWallThickness(linkId)` | 管壁厚度 | 管状几何适配器 |

普通 STL/Polytope 网格不能虚构出半径、宽度或高度变量。真实网格只有在注册 `MeshTransformAdapter` 或参数化重建适配器后，才能开放对应变量。

### 6.4 第一阶段完整语义白名单

```text
LinkLength
JointOriginOffsetX
JointOriginOffsetY
JointOriginOffsetZ
JointOffsetAlongAxis
JointAxisTiltX
JointAxisTiltY
JointZeroOffset
JointLimitLower
JointLimitUpper
BaseTx
BaseTy
BaseTz
BaseRx
BaseRy
BaseRz
TcpTx
TcpTy
TcpTz
TcpRx
TcpRy
TcpRz
FlangeOffsetX
FlangeOffsetY
FlangeOffsetZ
FlangeRotationX
FlangeRotationY
FlangeRotationZ
LinkRadius
LinkWidth
LinkHeight
LinkCrossSectionX
LinkCrossSectionY
LinkWallThickness
```

白名单表示系统允许注册的语义类型，不代表每个机器人都会拥有全部变量。实际变量集合由模型拓扑、关节类型、几何能力、模板和适配器共同决定。

### 6.5 不作为第一阶段主变量的类型

以下类型不进入新方案的第一阶段主变量白名单：

```text
JointRotationRoll
JointRotationPitch
JointRotationYaw
DhA
DhD
```

关节轴姿态统一使用 `JointAxisTiltX/Y`，关节零位统一使用 `JointZeroOffset`。DH `a/d` 只作为规范模型的投影、检查和导出视图，不作为第二套运行时状态；不允许 DH 参数与规范 Transform/轴变量同时修改同一运动学关系。

### 6.6 坐标系、单位和表达互斥

- 长度内部统一使用米，角度内部统一使用弧度；UI 可显示毫米和度；
- `JointOriginOffset*` 的坐标系必须是父连杆/父关节局部坐标系，并写入 `frameId`；
- `JointOffsetAlongAxis` 使用当前关节的基准轴，而非世界轴；
- `BaseT*` 使用明确的场景参考系；
- `Tcp*` 使用法兰到 TCP 的末端局部变换；
- `Flange*` 只有存在独立法兰层时启用；
- `JointOriginOffset*` 与 `JointOffsetAlongAxis`、`Tcp*` 与 `Flange*`、主变量与其派生值均不得重复修改同一物理自由度；
- DH `a/d` 只作为检查或导出视图，不作为第一阶段主变量。

### 6.7 模板

模板必须是静态、可版本化的变量生成规则：

- `Kinematic Basic`：连杆长度、安装位置、零位偏置、关节限位；
- `Kinematic + Joint Axis`：增加关节轴 X/Y 偏转；
- `Kinematic + Base/TCP`：增加基座、法兰或 TCP 位姿；
- `Full Kinematic Design`：启用所有满足模型适用条件的第一阶段变量。

### 6.8 依赖和适用性规则

依赖用于表达 `JointLimitLower < JointLimitUpper`、轴偏转总角限制、最小连杆长度、截面尺寸正值等关系。依赖图必须是有向无环图。未知变量、循环依赖、单位不一致、变量不适用于当前关节/几何类型，均阻止运行。

## 7. 适配器与候选模型编译器

### 7.1 适配器接口契约

```text
IModelParameterAdapter
  adapterId()
  supportedKinds()
  validate(variable, baseModel)
  apply(variable, value, model, diagnostics)
  describeEffect(variable)
```

适配器必须声明它修改的规范路径和输出对象。适配器不能只更新 UI 或中间字段而不影响编译结果。

### 7.2 内置适配器

`LinkPoseAdapter` 将长度和偏置转换为父子刚体变换；`JointAxisAdapter` 将轴偏转约束到单位向量和最大偏转锥；`JointPoseAdapter` 应用零位偏置并保持关节类型语义；`BasePoseAdapter`/`TcpPoseAdapter` 应用 6D 位姿增量；`JointLimitAdapter` 检查上下限并生成限位派生数据；`ParameterizedGeometryAdapter` 根据参数生成圆柱、盒体或截面几何；`MeshTransformAdapter` 仅支持明确声明的网格变换。

### 7.3 编译结果

```text
CompiledCandidate
  candidateId
  designVector
  kinematicModel
  robotModelSpec
  presentationGeometry
  collisionGeometry
  derivedValues
  diagnostics[]
  status: Compiled | CompileFailed
```

编译器采用“基线副本 + 变量顺序应用”策略，不直接修改宿主 WorkCell。候选预览和评估均使用编译结果。任何候选都必须能从同一输入重新编译，以保证结果可复现。

## 8. 多阶段评估器

### 8.1 阶段接口

```text
IEvaluationStage
  stageId()
  requiredInputs()
  producedMetrics()
  evaluate(compiledCandidate, context, cancellation, diagnostics)
```

### 8.2 第一阶段流水线

1. **设计空间预检查**：变量范围、依赖、适配器和目标/约束注册检查；
2. **模型有效性**：链拓扑、轴向量、退化长度、关节限位和几何有效性；
3. **任务可达性**：必需任务点和可选任务点的 IK/姿态约束检查；
4. **工作空间覆盖**：按配置区域采样并计算覆盖率；
5. **可操作度与关节裕度**：采样点统计分位数和最小值；
6. **碰撞安全**：自碰撞、环境碰撞和碰撞样本比例；
7. **目标聚合与约束评估**：归一化、软惩罚、可行性和诊断。

### 8.3 候选状态

`Pending`、`CompileFailed`、`InvalidModel`、`Evaluating`、`Infeasible`、`Feasible`、`EvaluationFailed`、`Canceled`。状态转移必须由控制器/流水线管理，UI 不得直接改写状态。

### 8.4 缓存与确定性

候选缓存键包含基线模型指纹、设计向量、评估配置版本和阶段版本。相同输入必须得到相同指标和诊断；并行评估结果按候选稳定索引合并，不按完成顺序排序。

## 9. 目标与约束

### 9.1 目标项

```text
ObjectiveTerm
  id
  metricId
  direction: Maximize | Minimize
  weight
  normalization: None | FixedRange | BaselineRelative
  enabled
```

第一阶段内置指标：`ReachabilityRate`、`WorkspaceCoverage`、`ManipulabilityP10`、`CollisionFreeRate`、`JointMarginP10`、`TotalKinematicLength`、`BaseHeight`、`MaxCrossSection`。

### 9.2 约束项

```text
ConstraintRule
  id
  metricId
  relation: LessEqual | GreaterEqual | Equal | InRange
  lower
  upper
  tolerance
  severity: Hard | Soft
  penalty
  enabled
```

未知指标、无效范围和单位不一致在运行前阻止执行。硬约束违反会将候选标记为不可行；软约束记录违反量并加入总分惩罚，同时保留原始指标。

## 10. 优化算法适配

现有 `Random`、`Grid`、`Hybrid` 策略保留控制器接口，但候选生成器改为读取设计变量：连续变量均匀/分层/网格采样，整数变量按 `step` 生成并去重，离散变量按选项枚举，依赖变量先生成主变量再解析派生范围，被禁用或未绑定变量固定为名义值。

本阶段不把算法升级为混合进化算法，但 `DesignVector` 协议必须允许未来采用“外层离散组合 + 内层连续优化 + 局部精修”的策略。

## 11. UI 工作台设计

### 11.1 设计空间区

模板选择、模板版本和应用预览；按链、关节、基座、TCP、几何分类的变量树；表格列包括启用、名称、语义、当前值、名义值、最小值、最大值、单位、域、适配器、来源和状态；右侧详情显示目标路径、依赖、适配器效果、范围错误和基线恢复按钮。

### 11.2 目标与约束区

目标表和约束表分离。指标选择只显示已注册且具有单位/方向定义的指标。支持权重、归一化、阈值、容差、软硬切换。顶部显示错误、警告、未绑定变量和不可用指标数量。

### 11.3 求解与结果区

保留开始、暂停、继续、取消和进度。候选表显示状态、可行性、总分、关键指标、违反约束数量和编译诊断入口。详情面板显示设计向量、派生指标、目标分项和约束逐项证据。3D 预览只加载已编译候选，支持恢复基线。基线对比、Pareto 和灵敏度分析作为结果工具，不改变原始加权排序。

### 11.4 报告与工程资源区

报告必须包含设计空间快照、变量模板版本、适配器清单、模型和需求指纹、目标/约束配置、运行配置、最佳候选、失败候选摘要和诊断。继续使用现有 Project Document Provider 和生成资源机制。

## 12. 项目持久化与迁移

在 `StructureOptimizationProblem` 中增加：

```text
designSpace
  schemaVersion
  templates[]
  variables[]
  adapters[]
compiler
  version
  modelFingerprint
pipeline
  stages[]
  registryVersion
```

现有 `variables`、`objectives` 和 `metricConstraints` 保留，作为兼容读取和报告字段。旧项目迁移时按目标名、类型和索引映射；可可靠映射的变量转换为 `source=Legacy`，没有适配器的字段标记 `legacy/unbound` 并禁用。保存时保留未知 JSON 字段，且不能静默改变变量范围、目标方向或约束语义。

## 13. 错误、诊断与可追溯性

```text
Diagnostic
  code
  severity: Info | Warning | Error
  stage
  objectId
  fieldPath
  message
  suggestion
```

配置错误阻止运行；单候选错误只淘汰该候选；全局错误停止运行但保留已完成结果。每个结果携带输入指纹、候选索引、阶段版本和诊断列表。

## 14. 后续动力学与选型扩展

动力学阶段增加质量、质心、惯量张量、摩擦和负载变量。惯量采用 Cholesky 或等价正定参数化，避免物理非法矩阵。新增 `DynamicsEvaluationStage` 输出峰值转矩、峰值功率、热负荷和动态约束证据。

执行器阶段支持电机型号、减速器型号和减速比档位等离散变量。额定转矩、最大转速、效率和热容量作为器件数据派生值，不允许直接优化。推荐外层离散组合枚举、内层连续参数优化、最后局部精修和约束复核。

## 15. 分阶段实施建议

### Phase A：规范模型和编译器

定义规范模型、导入器、基线快照、字段映射、变量语义、模板、依赖校验和适配器；实现候选编译器和模型有效性诊断。

### Phase B：评估器和持久化

将运动学、碰撞和工作空间评估接入流水线，统一目标/约束注册和分项评分，扩展 JSON、迁移器和运行指纹，保持异步控制器和候选结果兼容。

### Phase C：工作台 UI

重组设计空间、目标约束、求解结果和报告区，增加预检查、迁移提示、候选诊断和模板应用预览，并更新 Widget 测试。

### Phase D：工程化增强

增加串行/并行确定性验证、检查点恢复、Pareto、鲁棒性分析和完整设计空间报告追溯。

## 16. 验收标准

### 功能验收

- 零值连杆长度、轴偏角、基座/TCP 位姿参数能通过模板显示并启用；
- 每个可启用变量都有真实适配器，修改后至少一个声明输出发生可验证变化；
- 关节轴优化不会把绕自身轴旋转重复计入零位偏置；
- 目标方向、归一化、软硬约束可保存、加载并在结果中解释；
- 编译失败和约束失败均保留候选诊断；
- 旧项目可加载，未绑定字段不会静默参与优化；
- 候选预览使用编译结果，恢复基线后宿主 WorkCell 不残留候选修改。

### 质量验收

- 核心模型和编译器不依赖 Qt UI；
- 相同基线、设计向量和配置得到确定性结果；
- JSON round-trip 不丢失已知字段，未知字段按兼容策略保留；
- 取消、暂停和单候选失败不会破坏控制器状态；
- Windows GUI 测试使用 Visual Studio x64 开发环境、`QT_QPA_PLATFORM=windows`，一次只启动一个绝对路径可执行文件；
- 现有结构优化测试和新增核心测试均通过。

## 17. 风险与缓解

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 旧模型无法完整映射到规范链 | 迁移后变量减少 | 保留 `legacy/unbound` 状态，提供人工绑定路径 |
| 几何参数与真实网格不一致 | 评估结果失真 | 强制适配器声明和几何变化测试 |
| 旋转参数奇异或语义重复 | 搜索空间病态 | 内部使用轴向量/旋转增量，限制偏转锥 |
| 变量依赖导致组合爆炸 | 运行时间增加 | 预检查依赖、固定未启用变量、保留候选预算 |
| 后续动力学破坏现有 JSON | 项目不可重开 | schema/version、未知字段保留和迁移测试 |

## 18. 结论

本设计把优化问题从“模型字段列表”提升为可审计的工程设计空间。规范运动学模型负责语义一致性，适配器负责真实模型变化，编译器负责候选可复现，流水线负责分阶段评估，UI 负责配置和解释。第一阶段可以聚焦运动学最优设计，同时为后续轨迹、动力学和电机/减速器选型提供不需要推翻重做的扩展基础。
