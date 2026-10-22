<!-- 生成物：由 build-context-pack.ps1 生成，禁止手改；源文件变化后重跑生成（validate-context-pack.ps1 报过期） -->

# 上下文包：CORE-T01 —— 构建落位与零依赖门禁

| 字段 | 值 |
| --- | --- |
| 生成时间 | 2026-10-22T15:20:14+08:00 |
| 生成基线 | git HEAD b12e8503cc1c2a1acc040f506842a02e126b9515 |
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

## 6. 验收要点（契约 acceptance）

- 两模式（独立冒烟＋集成）构建零错误；C++17 与基线共存验证留痕（P-ENV-1 消账）
- gtest 按 development-task-breakdown §5.5 定稿接入（P-ENV-2 已关闭，CR-07）
- 产品目标零 testkit 链接、零跨单元私有头

## 7. 源文件指纹（git blob SHA-1，40 位，行尾无关；相对仓库根；validate-context-pack.ps1 校验基准——非 SHA-256，本仓对象库为 sha1 格式）

| 源文件 | git blob SHA-1 |
| --- | --- |
| RobWork/doc/industrial-robot-design/tasks/foundation/CORE-T01.json | 10da2bf3fbebe548c37efd15da8ac2d428381087 |
| RobWork/doc/industrial-robot-design/units/core.md | de26f8edbecf2c6c0f96dfa099b8f748e214412c |
| RobWork/doc/industrial-robot-design/REQUIREMENTS.md | 8e199911c049a36a155238ed4b4563209dccd8f9 |
| RobWork/doc/industrial-robot-design/context/reading-paths.md | 464bba0a31e6f65bfee22a8977b37c9618847b39 |
| RobWork/doc/industrial-robot-design/context/asset-inventory.json | bcd1b07861027bbc20292f35beff55beac5d1f1b |
| AGENTS.md | 46ab00a6d3f452d29cb9ca99e1802db9c40b35b1 |
| RobWork/doc/industrial-robot-design/AGENTS.md | d46e91a10921a65430c6f9f1e498f6c5f39a5e55 |
| RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md | b9e78389e14e0fd4e8560321a7d3c8b6fceef0b9 |
