<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->

# 上下文包：CORE-T01 —— 构建落位与零依赖门禁

| 字段 | 值 |
| --- | --- |
| 生成时间 | 2026-10-17T13:50:02+08:00 |
| 生成基线 | git HEAD 458250f0ed55d6cdb892d0334d7747f5977bdecd |
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

## 7. 源文件指纹（SHA-256，相对仓库根；validate-context-pack.ps1 校验基准）

| 源文件 | SHA-256 |
| --- | --- |
| RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json | 0186d0c392de4f16048091fd53d22a43c5b708e56429ef8042bc8857b2f12658 |
| RobWork/doc/industrial-robot-design/units/core.md | f4bc2933933d2a2e7d7cac4f4e7bb34797b790807156f33f3ce70c35edc820eb |
| RobWork/doc/industrial-robot-design/REQUIREMENTS.md | 765a7701d78e5cde24b6c682ccedce69afd30dca198c6e0635999cb1c3dba6fa |
| RobWork/doc/industrial-robot-design/context/reading-paths.md | d3a82675fb2aca3d672bae305d58271d695bba5bb6b82739e292fdc8e31d06ed |
| RobWork/doc/industrial-robot-design/context/asset-inventory.json | 10e3907cccc0ae119842420b740b12b0b606348ebd602d0040d96ec409935aea |
| AGENTS.md | 838e0bf6d28176822cc5da76143aa9d0ed668d9c18e3f7acb6034b509c005da8 |
| RobWork/doc/industrial-robot-design/AGENTS.md | 4b41e2c693cac9545161aeffea16bb3ba86f19f0c8fea9970c10ae15284b2d59 |
| RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md | 58d11d0ba4d46f73a97b6d78a94789534534091ac7525e8241ab2ffd0cb3844d |
