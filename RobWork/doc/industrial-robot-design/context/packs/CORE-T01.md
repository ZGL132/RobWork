<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->

# 上下文包：CORE-T01 —— 构建落位与零依赖门禁

| 字段 | 值 |
| --- | --- |
| 生成时间 | 2026-10-18T04:10:57+08:00 |
| 生成基线 | git HEAD d92902c9695485511cffc8b6b79256079f8773ee |
| 任务状态（契约） | done |
| 单元 / 主 WP | core / WP-B |
| 分支 | wp03-t01 |
| 跨单元 | False |

## 1. 阅读阶梯（规则入口）

- 仓库根 `AGENTS.md` §6.1 分层阅读顺序 → 设计区/源码区 `AGENTS.md`（按任务性质取其一，跨区任务两个都读）；
- 本任务阅读路径：**路径一：核心算法（context/reading-paths.md §1）**；
- 规则执法分级：`context/rule-enforcement.md`；
- **流水线相位：读 `traceability/pipeline/state.json` 本体**（本包不复制状态——PIPE 权威红线）。

## 2. 权威章节（必须读，章节级取用）

- 需求条目：NFR-MNT-01、ARC-02（REQUIREMENTS.md 按 ID 检索，勿通读）；
- 设计锚点：units/core.md#9；units/core.md#3.3；
- DTB 任务行：development-task-breakdown.md §2 对应 WP 节内的本任务行（**只取该行**）。

## 3. 代码根与改动面

- 代码根：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/`（include/sdurws/ird/core/、src/、test|contract_test|gui_test|perf_test 按单元实有）；
- 允许：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/core/**`
- 允许：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/CMakeLists.txt`
- **禁止**：`RobWork/doc/industrial-robot-design/REQUIREMENTS.md`
- **禁止**：`RobWork/doc/industrial-robot-design/ARCHITECTURE.md`

## 4. 验证命令

- 契约 verify：`cmake --build build --config Release --target sdurws_ird_core_test`
- 任务级：`pwsh -File RobWork/scripts/industrialrobot/verify-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json`；
- 门禁矩阵：ird-verify-change Skill（`RobWork/.agents/skills/ird-verify-change/SKILL.md`）。

## 5. 已知阻塞

- 前置依赖：全部 done（无阻塞）。
- open finding：F-004（suggestion）RWS 顶层全局 CMAKE_AUTOMOC 传播使 industrialrobot 各目标出现空转 MOC 步骤（实证零 Qt 依赖）
- open finding：F-201（suggestion）acceptance-protocol.md §4.10 条文『phase-one-validation.log 或 resultNote 已更新』与流水线实践脱节：phase-one-validation.log 尾行停在 CORE-T01（2026-09-10），其后的全部已合入任务（TK-T03 系至 IO-T05 系数十个）均未追加；traceability/pipeline/state.json 的 schema 中不存在 resultNote 字段（全文 grep 计数 0）——条文两个载体均无实际维护路径。任务级验证留痕的实际落位为 traceability/builds/<branch>/（gtest XML＋ird-test-report.json＋日志），IO-T06 attempt 1/2 验收均按此口径判定 4.10

## 6. 验收要点（契约 acceptance）

- 两模式（独立冒烟＋集成）构建零错误；C++17 与基线共存验证留痕（P-ENV-1 消账）
- gtest 按 development-task-breakdown §5.5 定稿接入（P-ENV-2 已关闭，CR-07）
- 产品目标零 testkit 链接、零跨单元私有头

## 7. 源文件指纹（git blob SHA-1，40 位，行尾无关；相对仓库根；validate-context-pack.ps1 校验基准——非 SHA-256，本仓对象库为 sha1 格式）

| 源文件 | git blob SHA-1 |
| --- | --- |
| RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json | 10da2bf3fbebe548c37efd15da8ac2d428381087 |
| RobWork/doc/industrial-robot-design/units/core.md | 07e3b38a4bc98d0e2223cf5de83d7c8880c99dce |
| RobWork/doc/industrial-robot-design/REQUIREMENTS.md | 349489fe5cf47c956a8007a58c187568642533ad |
| RobWork/doc/industrial-robot-design/context/reading-paths.md | 1b7d1bcc50fca5b8e0efd7947bb76ae79a77e87f |
| RobWork/doc/industrial-robot-design/context/asset-inventory.json | fd7a53dad41d88956fb88057bb17b653936a4128 |
| AGENTS.md | eed02a0ab6b7ccf9c0e643d56caa8db068049a85 |
| RobWork/doc/industrial-robot-design/AGENTS.md | 86b3f664b81da562f1ec0fbfb2c1995e8f360c5f |
| RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md | 2afad9e486d04d220bf5f077a0f577cd86992d71 |
