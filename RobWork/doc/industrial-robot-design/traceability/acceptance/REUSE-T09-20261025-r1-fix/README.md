# REUSE-T09 独立验收 r1 FAIL 返工登记（2026-10-25）

对应验收记录：[REUSE-T09-20261025-r1.json](../REUSE-T09-20261025-r1.json)（verdict **fail**，
2 issues，14 项检查中 13 项通过——fail 仅由 commit-lint 提交前缀违规触发，技术实质面全过）。
任务契约：[REUSE-T09](../../../tasks/REUSE-T09.json)（status 保持 `ready`，待所有者按本登记
依据复核第 2 轮独立验收；本登记不代替独立验收，独验仍由所有者另会话或人工执行，实施者不自签）。
返工基线：redesign-main `7227e387c`（承接时 HEAD，送验 head `b0997f8d8` 之后 trajectory 的
T09 四源文件零改动已经 r1 记录 checks[0] 核实）；返工段＝`7227e387c..<本返工提交>`，仅含
本登记所列文件（r1 验收原件入库＋本登记目录＋契约 note/allowedFiles 补登），零产品源码、
零治理脚本、零构建配置变更。

## Issue 1（阻断）：commit-lint FAIL 3/18 → 三提交均＝findings F-971 在册豁免，直接引用不重复登记

### 事实与复跑

- r1 原件 [09-commit-lint.log](../REUSE-T09-20261025-r1/09-commit-lint.log)：
  `commit-lint -Range 54f0beda6..b0997f8d8` FAIL 3/18——`9e1fe9c2e905`、`340dc88a4000`
  （T09 两实施提交，标题前缀 `[REUSE-T09]` 不在白名单）＋`3e8cec6ef9d2`（送验范围推导带入
  的非 T09 提交）。
- 本人复跑（不引用验收者记录，2026-10-25）：
  `pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 54f0beda6..b0997f8d8`
  → **FAIL 3/18，三个 FAIL 提交与 r1 原件逐项相同**
  （[01-commit-lint-fullrange-rerun.log](01-commit-lint-fullrange-rerun.log)，exit 1）；
  脚本输出自身声明「历史提交按 F-012 豁免，不回写」。

### 处置（r1 issues[0] 指定路径；REUSE-T01 `9d889781c` / REUSE-T07 `53768464c` 同型先例）

- **登记依据已在册，不重复登记**：findings **F-971**（`traceability/findings.json`，由
  `83224ba40` 于 2026-10-24 17:59 入库，早于 r1 开验 2026-10-25 00:33 约 6.5 小时）已按
  F-012/F-970 口径显式登记豁免**完全相同的三个提交**——`9e1fe9c2e905/340dc88a4000/
  3e8cec6ef9d2`，豁免依据＝历史提交不可改写（AGENTS §6.3 红线）＋三者为任务包分支侧历史
  经集成 merge 进入主线枚举面，能力内容按各自契约另行送验独验。F-972 亦在册复认
  「9e1fe9c2e/340dc88a4/3e8cec6ef＝F-971」。
- **零未登记违规**：本轮复跑 FAIL 集与 F-971 登记集完全相同，无新增违规提交。
- **整改（止增）**：新增提交过 commit-lint 白名单自检——本返工提交即以 `[governance]` 前缀
  自查提交；实施侧后续触碰 trajectory 的提交同样遵守（同范围合规形态参照 `f74f483d5`
  `[units]`＋任务号）。
- **r2 收口口径（r1 issues[0] 原文）**：复核验「commit-lint 对该两提交的 FAIL 有登记依据」
  ——依据＝F-971 在册条目＋本 README 固化引用＋契约 note 处置段。

### 第 3 个 FAIL（3e8cec6ef）随范围口径一并登记

见 Issue 2：该提交属送验范围推导带入的非 T09 提交（reuse-t01 分支侧历史），前缀豁免同样
在册 F-971，不属 T09 实施链。

## Issue 2（非阻断建议，已采纳）：固化 T09 专属送验范围口径＋登记全范围 FAIL 归因

### 固化口径（写入契约 note）

T09 送验链＝**七提交**（r1 记录 scope 字段同源）：

| 链 | 提交 | 说明 |
| --- | --- | --- |
| 实施链 | `340dc88a4` | 原始实施（[REUSE-T09] 前缀，F-971 豁免） |
| 实施链 | `9e1fe9c2e` | 修订重放（文件面与原始一致，F-971 豁免） |
| 实施链 | `aee6ac5bf` | 主会话整合 catalog 翻转（catalog.json forbidden 命中，契约 note 预登记） |
| 实施链 | `357d19e3d` | 门禁留痕 |
| 集成链 | `8db7a7e78` | 集成收口 3/3：并入 trajectory 多项式替换 |
| 集成链 | `a63157aec` | 集成收口 4/4：MATH-POLYNOMIAL 转 verified（catalog.json，同上预登记） |
| 集成链 | `b0997f8d8` | merge 357d19e3d into redesign-main（送验 head） |

- 本人复跑（r1 03b 同款机器核对，verify-task-scope 逐行同款正则、forbidden 优先）：
  七提交 first-parent diff 并集＝**38 文件＝37 命中 allowedFiles＋1 forbidden（catalog.json）
  ＋0 越界**（[02-scope-check.ps1](02-scope-check.ps1)＋[02-t09-chain-scope-rerun.log]
  (02-t09-chain-scope-rerun.log)，exit 0）——与 r1 [03b](../REUSE-T09-20261025-r1/
  03b-t09-commits-scope-check.log) 对四提交的核对同数（38/37/1），集成链三提交文件面为
  实施链子集。唯一 forbidden 命中＝`reuse/catalog.json`，来自 `aee6ac5bf`/`a63157aec`
  两个主会话整合提交，契约 note 预登记「catalog verified 翻转属主会话统一整合项」，
  实施者提交零触碰 catalog/permissions（r1 checks[1] 已核）。

### 全范围 FAIL 归因（登记，不补 allowedFiles 消化）

- 全范围 `54f0beda6..b0997f8d8`（ask 指定 base＝契约 note 冻结起点、reviewHead 系
  git log 推导）的 `verify-task-scope` FAIL **97 越界**
  （[02-scope-fullrange.log](../REUSE-T09-20261025-r1/02-scope-fullrange.log)＋
  [03-fullrange-diff-files.txt](../REUSE-T09-20261025-r1/03-fullrange-diff-files.txt)）：
  逐项核对均属送验范围内**非 T09 提交**（REUSE-T11 modeling 证据/矩阵、drivetrain-linalg
  ＝T12、治理脚本 reuse-contract.ps1/verify-task.ps1、units/modeling.md、RPY/EAA 债务关闭
  等），系主线串行集成链带入，无一项来自 T09 实施提交——**属送验范围推导问题而非 T09
  授权面缺陷**，由各自契约独立送验，不为消化越界而放宽 allowedFiles。
- 同区间 commit-lint FAIL 3/18 的第 3 项 `3e8cec6ef`（「并入主会话reuse-t01最新进展…」）
  同理属范围推导带入的非 T09 提交，豁免在册 F-971（见 Issue 1）。

### 契约变更面

- allowedFiles 仅追加本任务自身验收产物 3 条：`…acceptance/REUSE-T09-20261025-r1.json`、
  `…-r1/**`、`…-r1-fix/**`（用于 r1 原件入库与本登记目录）；forbiddenFiles 5 条零改动，
  不放宽门禁。T01/T07 返工同型。

## 本返工的验证（提交后自检三门禁，供所有者复核）

```
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 7227e387c..HEAD
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T09.json
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T09.json -Base 7227e387c -Head HEAD
```

返工包范围＝`7227e387c..HEAD`（单提交）：r1 验收原件入库（`REUSE-T09-20261025-r1.json`＋
同名目录；其中 `polynomial-replace-bench.obj` 受仓库 `.gitignore` `*.obj` 全局规则不入库，
以磁盘留痕＋[11-bench-run.bat](../REUSE-T09-20261025-r1/11-bench-run.bat) 可重跑为准）＋
本登记目录（README＋01/02 号证据与脚本）＋契约 note 处置段＋allowedFiles 追加 3 条——
全部落在补后 allowedFiles 内。

第 2 轮独立验收建议核验面（T07 r2 两段口径同型）＝T09 送验链七提交文件面（上述 02 号
复跑，38/37/1 与 r1 03b 同数）＋返工段 `7227e387c..HEAD`（提交后复跑为准）＋commit-lint
登记依据（F-971＋本 README）。r1 已实测的治理自测 152/152、validate-reuse（governance
only）、validate-docs、ird_gates、集成构建与 trajectory 191+69、性能基准独立重跑、SHA
绑定链与红线抽查，因本轮零产品/零脚本变更按 AGENTS §4 增量口径直接引用 r1 原件，不重复
执行；独验不由实施者自签。
