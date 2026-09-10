# WP and Unit Design Documentation Restructure

## Goal
Separate delivery planning (WP), unit ownership (unit cards), and architecture boundaries.

## Decisions
- Add nine capability-oriented master work packages WP-A through WP-I.
- Retain WP-00 through WP-25 as legacy implementation-task identifiers and map them to master WPs.
- Add DETAILED-DESIGN.md as the index for 20 unit design cards.
- Add a single traceability matrix linking requirements, acceptance tests, units, unit tasks, and master WPs.
- Do not rewrite unit interface details into the WP plan.

## 2026-09-10 文档同步状态

第一批 11/20 单元详设已编写。编写、公共契约冻结、实现验收分别登记；当前可先行基础构建任务，服务单元执行契约与接口交接按任务放行。当前权威状态见 RobWork/doc/industrial-robot-design/traceability/phase-one-readiness.md 与 traceability/phase-one-task-index.json。历史计划复选框不代表实际验收。需求范围、单元边界及依赖白名单保持原语义。
