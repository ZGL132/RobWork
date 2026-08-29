# 候选编译契约

> 契约 ID：`CTR-OPT-002`（新增登记）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（IRD-D10-20260829 联合评审通过，待签署）  
> 权威边界：本文件是设计变量定义、设计向量、变量绑定注册、候选补丁和候选稳定身份的唯一权威；需求 §7.4、§8.7.1、§9.1～9.3 是产品语义决策来源。`module-design/optimization.md` 只能引用本文件。

## 1. DesignVariableDefinition（字段冻结）

```text
DesignVariableDefinition
  - variableId           研究内稳定 ID，唯一
  - bindingId            引用已注册 VariableBinding（§3）
  - targetObjectId       权威对象 ID（RobotDesign / EnvironmentModel / DriveTrainDesign 等）
  - parameterKey         该对象内的类型化参数路径（如 "joints[2].originPose"）
  - valueType            Real | Integer | Enumeration | CatalogRef
  - unit                 SI 单位（rad / m / 无量纲）
  - domain               连续 [min, max]；量化 [min, max] + step；离散合法值集合或目录键集合
  - quantization         可空；null = 连续变量
  - activationCondition  可空；仅条件满足时变量激活（如 "joints[i].type == Revolute"）
  - writeSet             该变量写入的全部物理字段路径集合（冲突检测用）
  - dependencies         依赖的其他 variableId 集合，构成 DAG
```

- 所有变量都是对权威对象字段的引用，**不存在第二份候选数据存储**（需求 §9.1）；轴数、关节拓扑、并联支链和复杂齿轮传动拓扑不是首版变量。
- 改型项目默认锁定非授权参数：被锁变量不进入 `DesignVector` 且激活条件不满足（OPT-02）。

**`OptimizationStudyDefinition` 补充冻结字段**：`studyId`、`baselineRevisionRef`、`studyDefinitionVersion`、`variables[]`（§1）、`hardConstraints[]`（`kind` 值域：`MustCoverage / JointLimit / Collision / PathContinuity / DriveCapability / EvidenceProfile / StructuralBound`）、`softConstraints[]`、`metrics[]`、`objectives[]`（需求 §9.3 四字段）、`budget`（`maxCandidates / maxWallClockS / maxVerifiedEvaluations`）、`algorithmPolicy`（版本化策略对象）。

## 2. DesignVector（冻结）

- `DesignVector` 是 `variableId → 类型化值` 的不可变映射。
- **未出现的变量 = 未设置；已出现且值为 0 = 合法设置值**。零值不得被解释为“未设置”，未设置不得以零值补默认。
- 规范序列化（用于缓存键与稳定 ID）：键按 UTF-8 字节序排序的 JSON，无空白，浮点十六进制或最短往返表示；序列化结果取 SHA-256。

## 3. VariableBinding 注册表（冻结）

- 每个绑定必须先注册：`bindingId`、所有者 WP、目标类型、`parameterKey` 类型签名、`valueType/unit` 校验器、允许的阶段（`StageB | StageD`）。
- **禁止任意字符串反射式字段写入**：候选编译器只接受已注册绑定；未注册路径返回 `IRD-OPT-UNREGISTERED-BINDING`，不落盘、不产生部分状态。
- **多变量写入同一物理字段拒绝**：任意两个激活变量的 `writeSet` 交集非空 → `IRD-OPT-WRITE-CONFLICT`，研究定义校验失败。
- `dependencies` 出现环 → `IRD-OPT-CYCLE`。
- 派生变量（截面尺寸变化引起质量/质心/惯量变化）按 DAG 拓扑序重算；物理一致性规则：几何相关扰动按解析估算规则重算质量/质心/惯量，或按“惯量随质量与特征尺寸同步缩放”一致性规则生成，禁止独立缩放质量与惯量（需求 §15.3）；每个派生惯量张量执行正定性与三角不等式校验。

## 4. CandidatePatch（冻结）

```text
CandidatePatch
  - orderedMutations        按注册序 + DAG 拓扑序排列的原子修改列表
  - derivedRecomputation    派生值重算结果（质量/质心/惯量等）
  - diagnostics             逐项结构化诊断
  - writeSetFingerprint     全部激活变量 writeSet 规范排序后的 SHA-256
```

- **全成全败**：任一 mutation 校验失败（域越界 `IRD-OPT-DOMAIN-VIOLATION`、绑定未注册、类型/单位不符）→ 整个 patch 拒绝（`IRD-OPT-PATCH-REJECTED`），不产生部分应用状态，不创建任何工件。
- **候选不得创建项目修订**：`CandidatePatch` 应用于 `CandidateInputSnapshot`（基线修订 + 研究定义版本 + `DesignVector`），编译为 `CompiledCandidateArtifact`（含 `RuntimeNameMap`），归 `OptimizationRunResult` 管理、可整体丢弃；只有用户执行“设为当前方案”才通过项目命令创建方案分支和新 `ProjectRevision`（需求 §7.4、OPT-08）。
- `CompiledCandidateArtifact` 可组合 `CompiledRobotArtifacts` 的编译管线，但身份独立，不得混用（符号裁决 #5）。

## 5. 候选稳定身份（冻结）

```text
candidateStableId = SHA-256( studyDefinitionVersion ‖ canonical(DesignVector) ‖ randomSeed )
```

- 同一研究版本、设计向量和种子在任意线程/并行度下产生相同 `candidateStableId`、相同可行集合和相同 Pareto 支配关系（OPT-06、需求 §15.3 并行复现）。

## 6. OPT-B 静态子集边界

- 阶段 B 允许的绑定类别：结构尺寸/关节安装/DH 长度与偏置类、传动比类静态变量；硬约束仅运动学与碰撞静态子集；指标为静态指标与静态 Pareto（需求 §8.7.1 的 OPT-B 唯一集合）。
- 登记 `StageD` 的绑定（轨迹、动力、器件联合变量）在阶段 B 不得被激活，激活尝试返回 `IRD-OPT-STAGE-LOCKED`。

## 7. 契约测试

1. 零值语义：值为 0 的变量与未设置变量在序列化和域校验中可区分。
2. writeSet 冲突、未注册绑定、依赖环、域越界逐项拒绝且无部分状态。
3. 派生重算：截面扰动后质量/质心/惯量按一致规则变化；非法惯量样本单独报告。
4. 稳定 ID：同输入跨线程/并行度一致；patch 失败不留工件。
5. 阶段锁：StageD 绑定在 OPT-B 激活被拒。
