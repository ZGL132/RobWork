# WP-20 优化定义与候选编译实施计划

**目标：** 支持新机型和既有机型改型的研究定义、变量引用、候选补丁和 R1 静态优化链路。

**阶段/发布：** 阶段 B，R1；只实现 OPT-B（OPT-01～04、06～08 的静态子集）。OPT-05、OPT-09、OPT-10 和完整轨迹/动力/器件联合留给 WP-21 阶段 D。

**需求与契约：** OPT-01～04、06～08 静态子集；AT-09 静态子集、AT-12；引用 `architecture/domain-model.md`、`public-interfaces.md`、`execution-model.md`、`testing-contract.md`。

**拥有目录：** `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/definition/`、`candidate/` 和测试。不得实现 WP-21 的并行联合搜索或鲁棒性规模化。

**输入/输出：** 输入为基线修订、`EngineeringRequirements`、变量域、静态硬约束、目标、预算和策略；输出为 `OptimizationStudyDefinition`、`CandidateInputSnapshot`、`CompiledCandidateArtifact`、静态 `ParetoSet` 和证据。

## 任务

1. **WP-20-T01 研究定义**：定义字段路径、连续/量化/离散变量、改型锁定、预算、随机种子和版本；引用对象必须属于基线作用域。
2. **WP-20-T02 候选补丁**：将设计向量编译为不可变候选输入；记录基线差异、来源和诊断；失败候选不得创建项目修订。
3. **WP-20-T03 静态硬约束**：按拓扑/输入、运动学、碰撞顺序执行；Quick 不能伪装为 Verified；不可行候选不得进入可行集合。
4. **WP-20-T04 静态指标与 Pareto**：实现尺寸、质量、节拍代理、成本和裕量比较；SoftConstraint 仅作警告/次级排序，不参与 Pareto，除非显式提升为 Objective。
5. **WP-20-T05 缓存与确定性**：缓存键覆盖基线、研究定义、策略、算法版本、线程和种子；兼容命中必须符合 WP-08 契约；同种子输出稳定候选 ID/排序。
6. **WP-20-T06 结果与应用**：候选结果只归优化运行；“设为当前方案”通过项目命令创建一个分支和新修订；运行期间修订号不随候选数量增长。
7. **WP-20-T07 阶段 B UI**：提供变量域编辑、候选比较、静态证据和应用确认；不暴露内部哈希作为唯一标识。
8. **WP-20-T08 跨入口契约**：从运动学和 WP-20 入口验证共享碰撞策略、对象 ID 对、判定和原因；为 WP-21 留出稳定扩展接口。

## 任务卡

详见 `agent-tasks/WP-20-T01-study-definition.md`～`WP-20-T08-cross-entry.md`。

## 验证

前置：WP-05～08、WP-13～15。

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'
```

必须提交：研究定义 JSON 样例、候选差异报告、静态 Pareto 黄金数据、缓存命中/拒绝矩阵、固定种子复现报告和 AT-09/12 阶段 B 记录。

## 迁移与删除

旧结构优化器只用于行为对照；阶段 B 通过后删除旧候选写回、重复缓存和加权总分逻辑。

## 独立评审

由优化工程师和独立测试人员复核静态硬约束、Pareto、缓存和固定种子证据。

## 退出条件

OPT-B 权威集合、AT-09 静态子集、AT-12 通过；不可行、Partial、DataInsufficient 候选不进入静态 Pareto；R1 不依赖 WP-21 才能完成静态闭环。
