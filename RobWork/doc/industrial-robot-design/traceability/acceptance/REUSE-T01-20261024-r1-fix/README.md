# REUSE-T01 独立验收 r1 FAIL 返工登记（2026-10-24）

对应验收记录：[REUSE-T01-20261024-r1.json](../REUSE-T01-20261024-r1.json)（verdict **fail**，4 issues）。
任务契约：[REUSE-T01](../../../tasks/REUSE-T01.json)（status 保持 `ready`、implementationState 保持
`audit-in-progress`，待所有者按本登记口径重新送验；本登记不代替独立验收，独验仍由所有者另会话
或人工执行，实施者不自签）。
返工基线：redesign-main `95874bf1b`（GOV-T26 收口之后、本返工提交之前）；返工段＝
`95874bf1b..<本返工提交>`，仅含本登记所列文件。

## Issue 1＋4（合并处置）：送验范围收敛为 REUSE-T01 审计本体提交段

### 根因（两 issue 同源）

r1 送验范围由 `git log --grep REUSE-T01` 首末推导为 `6509e5e4c..d50e824c7e8b`（80 提交、
6434 变更路径）。REUSE-T01 是**只读审计父任务**（契约 acceptance 第 3 条「零生产代码
变更」，forbiddenFiles 硬面 `industrialrobot/**`），但该整段主线在中后段必经链
`c27349a80 → 8e4a08ac8 → eae9d213e → d8bc82403 → 3a61e300f → fdd07d1ce → e453006ea
→ a467cc731 → 717594e5c → 54f0beda6 → 0032c8e78 → a1393ad4a → 30b5a2343 →
2daace508 → d50e824c7` 混入了其他任务能力包实施、所有者治理文本与跨会话合并提交——
任何**单一连续** `base..head` 范围都无法同时覆盖 grep 首末并剔除它们，故 694 路径必然
越界/命中禁止面（5 个 `[forbidden]` 产品 modeling 文件＋689 个 `[out-of-scope]`，见
[01-scope.log](../REUSE-T01-20261024-r1/01-scope.log)、
[04-range-files.txt](../REUSE-T01-20261024-r1/04-range-files.txt)）。产品 modeling 5
文件属 forbiddenFiles 且与「零生产代码变更」根本冲突，**不可通过补 allowedFiles 消化**
（验收记录处置口径；补入即放宽禁止面）。

### 处置：收敛送验段 `0d0d3ca3e..bfe024cd0`（64 提交，B01—B35 审计本体）

- **基点后移**：`6509e5e4c` 的直接子提交 `0d0d3ca3e` 本身是 GOV-T22 收口（标题
  「登记GOV-T22本地独立通过与远程交付阻断」，正文顺带提及 REUSE-T01 而被 grep 命中），
  其净变更 `tasks/GOV-T22.json` 是本段唯一残余越界（6509e5e4c..bfe024cd0 实测仅此 1 个
  out-of-scope），故以 `0d0d3ca3e` 为收敛段基点，把它剔出。
- **收敛段终点**：`bfe024cd0`（「保存在途日志探测并停止扩展诊断深审」，B35 批次收尾）。
  此后 `c27349a80` 起进入他任务能力包与治理文本区段，全部剔出。
- 实测：`verify-task-scope -Base 0d0d3ca3e -Head bfe024cd0` → **PASS，5725 路径全部在
  允许面内**（[01-verify-task-scope-collapsed.log](01-verify-task-scope-collapsed.log)）；
  `commit-lint -Range 0d0d3ca3e..bfe024cd0` → **PASS，64 提交全部合规**
  （[02-commit-lint-collapsed.log](02-commit-lint-collapsed.log)）。

**剔出项清单及各自出路**（均不属本契约授权变更，由各自契约/记录独立送验或已在册）：

| 提交 | 内容 | 出路 |
| --- | --- | --- |
| `0d0d3ca3e` | GOV-T22 收口（tasks/GOV-T22.json） | GOV-T22 自有验收链（GOV-T22-20261024 系列在册） |
| `c27349a80`＋`8e4a08ac8` | REUSE-T07 耦合 SVD 产品实施（modeling 5 文件中 3 个） | [tasks/REUSE-T07](../../../tasks/REUSE-T07.json) 单独送验独立验收（见 Issue 3） |
| `eae9d213e` | REUSE-T08 微小角 EAA 最小提案 | [tasks/REUSE-T08](../../../tasks/REUSE-T08.json)（status `planned`）自身送验 |
| `d8bc82403`＋`fdd07d1ce`＋`a467cc731` | 覆盖清查 R1/R2 与冻结证据修正（T01 自身审计增量） | 剔出本轮送验面；与两个跨会话合并提交绑定的该增量随任务后续轮次（仍 audit-in-progress）单独登记送验 |
| `3a61e300f`＋`717594e5c` | 跨会话合并提交（无合法中文前缀） | 见 Issue 2（F-970 在册豁免；随范围收敛剔出送验段） |
| `e453006ea`＋`54f0beda6` | EAA 补丁＋撤销（上游净零） | [tasks/REUSE-T10](../../../tasks/REUSE-T10.json)（撤销EAA补丁并按原库实测登记局部转换容差）自身送验 |
| `a1393ad4a`＋`30b5a2343`＋`2daace508` | REUSE-T11 产品实施＋独立验收＋债务关闭 | T11 已闭环（REUSE-T11-20261023.json verdict=pass 绑定 a1393ad4a，closures.json 在册，r1 记录 checks 第 10/12 条核验） |
| `d50e824c7` | 所有者治理文本（AGENTS.md、REQUIREMENTS.md、acceptance-protocol.md、units/*.md、.github 模板等） | 所有者治理文本改动，非任务契约授权面，不在任何能力包送验段内消化 |

**allowedFiles/forbiddenFiles 未为消化越界而放宽**：本轮仅追加本任务自身验收产物 3 条
（`traceability/acceptance/REUSE-T01-20261024-r1.json`、`…-r1/**`、`…-r1-fix/**`），
用于 r1 原件入库与本登记目录；禁止面 4 条零改动。

## Issue 2：commit-lint 两个跨会话合入提交——随范围收敛剔出，F-970 在册豁免引用

`717594e5c`、`3a61e300f` 两个无合法中文前缀的双亲合并提交已由 GOV-T21 返工轮按 F-012
口径在 [traceability/findings.json](../../findings.json) 登记 **F-970**（豁免依据＝
`717594e5c` 正文自证所有者指示合入且全程只读调查、内容已经主线后续验收链消化；责任归属
与杜绝再现条款在册，2026-10-22 精简治理起主工作树串行实施）。本返工**直接引用该在册
豁免**，不重复登记、不改写历史；收敛段 `0d0d3ca3e..bfe024cd0` 不含两提交，
commit-lint 实测 64/64 全 PASS（[02-commit-lint-collapsed.log](02-commit-lint-collapsed.log)）。

## Issue 3：REUSE-T07 产品实施无独立验收——剔出本包，由其契约单独送验独验

与 GOV-T21 r1-fix「REUSE-T07 出路」同口径：`c27349a80`/`8e4a08ac8` 的产品变更（
CouplingMath.hpp 删自写 Jacobi 改调 `rw::math/LinearAlgebra` 等）剔出本包送验范围，
按 [tasks/REUSE-T07](../../../tasks/REUSE-T07.json)（status `ready`，allowedFiles 含其
产品文件与 `traceability/reuse/REUSE-T07/**`）单独送验独立验收（完整能力包：对端契约
回归＋数值验收）；本返工不代其送验、不签署其验收、不关闭其债务。migration.md §13.1
「当前下一项产品交付为REUSE-T07」表述保持不动，与该缺口处置一致。

## Issue 4：重验（收敛口径全绿）

按 r1 记录同口径复跑，全部 exit=0：

| 检查 | 命令 | 结果 | 证据 |
| --- | --- | --- | --- |
| 范围（收敛段） | `verify-task-scope.ps1 -TaskFile tasks/REUSE-T01.json -Base 0d0d3ca3eced -Head bfe024cd0147` | **PASS** 5725 路径 | [01-verify-task-scope-collapsed.log](01-verify-task-scope-collapsed.log) |
| 提交前缀（收敛段） | `commit-lint.ps1 -Range 0d0d3ca3e..bfe024cd0` | **PASS** 64 提交 | [02-commit-lint-collapsed.log](02-commit-lint-collapsed.log) |
| 契约 verify ①（冻结送验树） | `validate-task.ps1 -TaskFile tasks/REUSE-T01.json`（临时 detached worktree 检出 `bfe024cd0`） | **PASS** (1 tasks；governance only inventory=1401 units=20 DEBT=16 UNRESOLVED=1399) | [03-validate-task-frozen.log](03-validate-task-frozen.log) |
| 契约 verify ②（冻结送验树） | `validate-reuse.ps1 -TaskFile tasks/REUSE-T01.json`（同上） | **PASS** functions=1105 closedFiles=0 | [04-validate-reuse-frozen.log](04-validate-reuse-frozen.log) |
| 契约 verify ③（冻结送验树） | `validate-docs.ps1`（同上） | **PASS** (20 units, 378 task files) | [05-validate-docs-frozen.log](05-validate-docs-frozen.log) |

冻结送验树三命令在临时 detached worktree（检出 `bfe024cd0`，取证后已 `git worktree
remove` 清理）执行，与 r1 验收者同法；`functions=1105` 为 B35 收敛时点真实状态
（r1 在 `d50e824c7` 树为 1128，差值 23 系 R1/R2 覆盖清查增量，已随该增量剔出本轮送验面）。

## 本返工的验证（提交后自检三门禁，供所有者复核）

```
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 95874bf1b..HEAD
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json -Base 95874bf1b -Head HEAD
```

返工包范围＝`95874bf1b..HEAD`（单提交）：r1 验收原件入库（`REUSE-T01-20261024-r1.json`
＋同名目录 6 份 log）、本登记目录、契约 allowedFiles 追加 3 条＋note 处置段——全部落在
补后 allowedFiles 内。第 2 轮独立验收建议核验面＝收敛段 `0d0d3ca3e..bfe024cd0`（上述
01—05 log）＋返工段 `95874bf1b..HEAD`（提交后复跑为准）；独验不由实施者自签。
