# R-1：业务域单元互链禁止，跨单元协作只经六类端口

| 字段 | 值 |
| --- | --- |
| 编号 | IRD-NOTE-002 |
| 状态 | implemented（2026-10-17 自 ARCHITECTURE §3.2 / §3.5 迁移登记，治理 Task 4） |
| 决策依据 | ARCHITECTURE v0.14 §3.2（SA-10 四条构建红线）、§3.5 单元依赖表、§7.2 六类端口、§1.3 PA-4；REQUIREMENTS ARC-02 |

## Problem

七个业务域单元（modeling/requirements/kinematics/trajectory/dynamics/selection/optimization）在功能上彼此需要（轨迹评估要消费运动学真值、优化要消费多域评估器）。一旦允许直接链接（链接对方库目标、调用实现细节）：依赖图退化为任意有环图——单元不可独立测试、不可替换，并行开发被构建顺序绑死，环出现时整图不可构建。

## Decision

业务域单元之间不直接依赖（计算库/插件目标互链禁止，红线 R-1）；跨单元协作只经六类稳定端口（命令/查询/评估器/策略/事件/名称，§7.2，PA-4）。§3.5 单元级依赖表是 R-1/R-2 构建门禁的白名单数据源：表外同层边构建失败（A3 处置）；红线是构建期硬门禁而非评审约定（SA-10），例外只能登记在权威所有者单元。

## Alternatives considered

（事后重构，非当时记录——原决策时文档链未逐项登记备选，2026-10-17 迁移时按决策语境辨识）

1. **允许自由链接＋架构评审约束**——否决：评审看不到全局图，环从局部合理性中生长出来，约束不可执法；
2. **纯事件总线全解耦（禁止一切直接调用）**——部分吸收：事件是六类端口之一，但查询/评估器这类同步语义需要类型化直接端口；纯总线丢失类型安全与确定性时序（与 Q3 冲突）；
3. **六类端口＋依赖表白名单门禁**（采纳）：协作面类型化、依赖边显式化、门禁机器执法。

## Consequences

- 业务协作代码写成端口＋适配器，样板多于直调；
- 新增协作需求＝先改 §3.5 依赖表（治理动作、留痕）再写代码——依赖变化全部可审计；
- 跨单元真实接线由 `<unit>/contract_test/` 契约测试验证（单元内可用端口边界测试替身，见源码区 AGENTS.md §4 测试替身政策）；
- 非编译期的注入式协作边（io↔project/io↔runtime，O-09 裁决）需在 ARCH §3.5 登记说明，R-1/R-2 编译门禁面不变化。

## Verification

- `core/test/BuildRedLineTest.cpp` 用例 NoCrossUnitInclude_R1_R2_T1（机器检查）；
- ird_gates＋`traceability/dependency-graph.json` 允许边门禁（WP-01-T01 承载）；
- 执法分级：`context/rule-enforcement.md` §3.1（R-1 行）。

## Related sources

- `ARCHITECTURE.md` §3.2（红线表与 SA-10）、§3.5（依赖表）、§7.2（六类端口）、§1.3 PA-4、§3.4（workflow 编排单元不直链业务域）
- `REQUIREMENTS.md` ARC-02
- 根 `AGENTS.md` §5.2；`RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md` §2
