# 工业机械臂设计 · 单元详细设计总目录

| 项目 | 内容 |
|---|---|
| 文档定位 | 20 个构建单元详细设计的索引，不承载 WP 排期 |
| 上游 | REQUIREMENTS.md、ARCHITECTURE.md |
| 任务计划 | development-task-breakdown.md 的 WP-A～WP-I |
| 单元任务编号 | 各 units/<unit>.md 内部编号（如 CORE-Txx） |

## 文档分工

- REQUIREMENTS.md：需求语义、发布范围和验收定义。
- ARCHITECTURE.md：单元边界、端口、分层和依赖白名单。
- units/<unit>.md：单元内部接口、数据模型、状态机、错误语义和单元测试。
- development-task-breakdown.md：主 WP 交付目标、跨单元集成、里程碑和退出条件。
- 需求追踪矩阵：位于 development-task-breakdown.md §3，维护需求 → AT → 单元任务 → 主 WP 的唯一映射。

## 20 单元状态

2026-09-10：第一批 11 份详设已编写，剩余 9 份待产出。五基础单元保持 contract-review，其余六单元保持 Draft；无单元因文档完成而自动 frozen。第一批包含 reporting，属于设计批次，不改变 REQUIREMENTS 阶段 A～E 的功能启用范围。编码准入与待办见 [phase-one-readiness.md](traceability/phase-one-readiness.md)。

| 类别 | 单元 | 详设状态 | 主 WP |
|---|---|---|---|
| 平台内核 | core | contract-review / 已有任务卡 | WP-B、WP-C |
| 平台内核 | evidence | contract-review / 已有任务卡 | WP-C、WP-D、WP-H |
| 平台内核 | policy | contract-review / 已有任务卡 | WP-C、WP-E、WP-F、WP-G |
| 平台内核 | runtime | contract-review / 已有任务卡 | WP-C、WP-D |
| 测试支撑 | testkit | contract-review / 已有任务卡 | WP-B |
| 平台服务 | project | Draft / 已有任务卡 | WP-C、WP-D |
| 平台服务 | execution | Draft / 已有任务卡 | WP-D、WP-F |
| 平台服务 | diagnostics | Draft / 已有任务卡 | WP-D |
| 平台服务 | io | Draft / 已有任务卡 | WP-E、WP-I |
| 平台服务 | ui | Draft / 已有任务卡 | WP-I |
| 平台服务 | reporting | Draft / 已有任务卡 | WP-H、WP-I |
| 业务域 | modeling | 待产出 | WP-E |
| 业务域 | requirements | 待产出 | WP-E |
| 业务域 | kinematics | 待产出 | WP-F |
| 业务域 | trajectory | 待产出 | WP-F |
| 业务域 | dynamics | 待产出 | WP-G |
| 业务域 | drivetrain | 待产出 | WP-G |
| 业务域 | selection | 待产出 | WP-G |
| 业务域 | optimization | 待产出 | WP-H |
| 编排 | workflow | 待产出 | WP-I |

## 单元详设最低结构

每个单元卡固定包含：职责与非职责、输入输出契约、公共接口、数据模型、状态机、错误与诊断、依赖、单元测试、与主 WP 的任务映射、开放问题。跨单元里程碑和系统验收不写入单元内部任务表。

## AI 执行基础设施

机器可读索引位于 `traceability/`：

- `unit-status.json`：20 个单元的详设状态和主 WP 归属。
- `requirements-to-units.json`：需求、单元、任务、AT 和主 WP 映射。
- `dependency-graph.json`：架构允许的单元依赖边。
- `foundation-contract-review.md`：基础单元（core/evidence/policy/runtime/testkit）公共契约审查记录；CR-01～CR-08 已设计级关闭（2026-09-10），五单元转 `frozen` 待联合契约测试通过。
- `foundation-api-diff.md`：上述审查的逐项 API 差异比对、裁决与联合契约测试登记。
- `traceability/phase-one-task-index.json`：11 单元全部任务行与执行契约覆盖索引（缺契约者保持 planned）。
- `tasks/foundation/*.json`：基础五单元的 canonical 任务 JSON；其中仅 `ready` 项是可领取的完整执行契约，`planned` 项是待补全需求追溯的任务占位，不能直接执行。
- `tasks/*.json`（根目录）：治理与入口任务（FOUNDATION-CR-01、DOC-T01～T03、foundation-tasks.json 入口索引）。

任何 `ready` 任务契约必须包含非空的需求、设计引用、允许/禁止文件、产物、验收和验证命令；`planned` 任务在补齐这些信息前不是执行任务。执行前运行：

```powershell
pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1
pwsh -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json
pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json
```
