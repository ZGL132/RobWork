<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->

# 上下文包：CORE-T11 —— 单元卡与实现一致性核查（治理试点：上下文包新会话接手）

| 字段 | 值 |
| --- | --- |
| 生成时间 | 2026-10-17T14:07:55+08:00 |
| 生成基线 | git HEAD 0e5714fc7abd4a8e3fbb7838ba8b5501c3e71966 |
| 任务状态（契约） | ready |
| 单元 / 主 WP | core / WP-B |
| 分支 | wp03-t11 |
| 跨单元 | False |

## 1. 阅读阶梯（规则入口）

- 仓库根 `AGENTS.md` §6.1 分层阅读顺序 → 设计区/源码区 `AGENTS.md`（按任务性质取其一，跨区任务两个都读）；
- 本任务阅读路径：**路径一：核心算法（context/reading-paths.md §1）**；
- 规则执法分级：`context/rule-enforcement.md`；
- **流水线相位：读 `traceability/pipeline/state.json` 本体**（本包不复制状态——PIPE 权威红线）。

## 2. 权威章节（必须读，章节级取用）

- 需求条目：NFR-MNT-05（REQUIREMENTS.md 按 ID 检索，勿通读）；
- 设计锚点：units/core.md#9；units/core.md#3；units/core.md#10；
- DTB 任务行：development-task-breakdown.md §2 对应 WP 节内的本任务行（**只取该行**）。

## 3. 代码根与改动面

- 代码根：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/`（include/sdurws/ird/core/、src/、test|contract_test|gui_test|perf_test 按单元实有）；
- 允许：`RobWork/doc/industrial-robot-design/units/core.md`
- 允许：`RobWork/doc/industrial-robot-design/context/handoff-drill.md`
- 允许：`RobWork/doc/industrial-robot-design/context/packs/*`
- 允许：`RobWork/doc/industrial-robot-design/traceability/findings.json`
- **禁止**：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/**`
- **禁止**：`RobWork/doc/industrial-robot-design/REQUIREMENTS.md`
- **禁止**：`RobWork/doc/industrial-robot-design/ARCHITECTURE.md`
- **禁止**：`RobWork/doc/industrial-robot-design/development-task-breakdown.md`

## 4. 验证命令

- 契约 verify：`pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1`
- 契约 verify：`pwsh -File RobWork/scripts/industrialrobot/validate-context-pack.ps1 -All`
- 任务级：`pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T11.json`；
- 门禁矩阵：ird-verify-change Skill（`RobWork/.agents/skills/ird-verify-change/SKILL.md`）。

## 5. 已知阻塞

- 前置依赖：全部 done（无阻塞）。
- open findings 引用本任务：无。

## 6. 验收要点（契约 acceptance）

- core.md §3 公共头清单与 include/sdurws/ird/core/ 实测文件集比对：一致或差异逐条登记处置（笔误修正/语义偏差转 findings）
- core.md §10 测试目标登记与 core/CMakeLists.txt 实测注册比对：同上口径
- 卡面增量修订以最小增量落盘（版本行＋受影响节），不改写既有语义
- 发现语义级偏差登记 findings.json（不私改设计语义）
- 交接记录含实际 SHA、命令与四态验证结果

## 7. 源文件指纹（git blob SHA，行尾无关；相对仓库根；validate-context-pack.ps1 校验基准）

| 源文件 | SHA-256 |
| --- | --- |
| RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T11.json | 0f5eff39e400784a52c7d98892c42f2b506c4225 |
| RobWork/doc/industrial-robot-design/units/core.md | 07e3b38a4bc98d0e2223cf5de83d7c8880c99dce |
| RobWork/doc/industrial-robot-design/REQUIREMENTS.md | 349489fe5cf47c956a8007a58c187568642533ad |
| RobWork/doc/industrial-robot-design/context/reading-paths.md | 1b7d1bcc50fca5b8e0efd7947bb76ae79a77e87f |
| RobWork/doc/industrial-robot-design/context/asset-inventory.json | 9f671a19141b195ce9912af9915e9a1cd4022ccb |
| AGENTS.md | c660ccea2fef899091407b67c32a49a1e47446a9 |
| RobWork/doc/industrial-robot-design/AGENTS.md | 86b3f664b81da562f1ec0fbfb2c1995e8f360c5f |
| RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md | 2afad9e486d04d220bf5f077a0f577cd86992d71 |
