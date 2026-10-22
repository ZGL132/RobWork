<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->

# 上下文包：LITE-T01 —— 上下文包边界

| 字段 | 值 |
| --- | --- |
| 生成时间 | 2026-10-23T05:39:08+08:00 |
| 生成基线 | git HEAD HEAD |
| 任务状态（契约） | ready |
| 单元 / 主 WP | foundation-governance / WP-A |
| 分支 | fixture |
| 跨单元 |  |

## 1. 阅读阶梯（规则入口）

- 仓库根 `AGENTS.md` §1：当前契约＋相关条款/源码；分区规则按实际修改区域取用；
- 本任务阅读路径：**context/reading-paths.md 按任务性质取用（可选导航）**；
- 规则执法分级：`context/rule-enforcement.md`；
- **仅自动编排读取 `traceability/pipeline/state.json` 本体**（人工任务不读；本包不复制状态——PIPE 权威红线）。

## 2. 权威章节（必须读，章节级取用）

- 需求条目：H6（REQUIREMENTS.md 按 ID 检索，勿通读）；
- 设计锚点：context/README.md#3；
- DTB 任务行：development-task-breakdown.md §2 对应 WP 节内的本任务行（**只取该行**）。

## 3. 代码根与改动面

- 代码根：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/foundation-governance/`（include/sdurws/ird/foundation-governance/、src/、test|contract_test|gui_test|perf_test 按单元实有）；

## 4. 验证命令

- 任务级：`pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/LITE-T01.json`；
- 门禁矩阵：ird-verify-change Skill（`RobWork/.agents/skills/ird-verify-change/SKILL.md`）。

## 5. 已知阻塞

- 前置依赖：全部 done（无阻塞）。
- open findings 引用本任务：无。

## 6. 验收要点（契约 acceptance）


## 7. 源文件指纹（git blob SHA-1，40 位，行尾无关；相对仓库根；validate-context-pack.ps1 校验基准——非 SHA-256，本仓对象库为 sha1 格式）

| 源文件 | git blob SHA-1 |
| --- | --- |
| RobWork/doc/industrial-robot-design/tasks/foundation/LITE-T01.json | 6e399698e3221bfd11662b7710907266065405cb |
| RobWork/doc/industrial-robot-design/context/README.md | 1b4d8acd2100150bee36fe5dacbad30780390c6c |
| RobWork/doc/industrial-robot-design/REQUIREMENTS.md | 56863d34de1f951ba34b71ee1278f87d13f7b4da |
| RobWork/doc/industrial-robot-design/context/reading-paths.md | e92eee9f4a8ebdafcfd5714cc9a17c6557d267e9 |
| RobWork/doc/industrial-robot-design/context/asset-inventory.json | a5ed33f3ee97bf03d5d0b868439ffc9582b2ff06 |
| AGENTS.md | 3fbce8a0d7e7539dda80058204a62d1766912d2c |
| RobWork/doc/industrial-robot-design/AGENTS.md | c5a922ee3b0d900381ec56cc21488250def25591 |
| RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md | 1af040a915cf94140019ef3627ac51b83c3fd940 |
