# REUSE-T07 独立验收 r1 FAIL 返工登记（2026-10-24）

对应验收记录：[REUSE-T07-20261024-r1.json](../REUSE-T07-20261024-r1.json)（verdict **fail**，2 issues）。
任务契约：[REUSE-T07](../../../tasks/REUSE-T07.json)（status 保持 `ready`，待所有者按本登记口径重跑第 2 轮独立验收；本登记不代替独立验收，独验仍由所有者另会话或人工执行，实施者不自签）。
返工基线：redesign-main `5fb4ebd32`（REUSE-T01 收口之后、本返工提交之前）；返工段＝`5fb4ebd32..<本返工提交>`，仅含本登记所列文件（r1 验收原件入库＋本登记目录＋契约 note/allowedFiles 补登），零产品源码、零脚本、零构建配置变更。

## Issue 1（范围）：送验范围收敛为 `c27349a80..8e4a08ac8`（实现提交本体）

### 根因

r1 送验范围 `bfe024cd0..8e4a08ac8` 按「grep 首提交 `c27349a80` 之父」推导，把准入提交
`c27349a80`（「按完整能力包推进复用替换并准入耦合SVD」，REUSE-T01 审计包的能力包准入
治理提交）整包带入——303 变更路径中 17 个越界（r1 记录 issues[0]，[02-scope.log](../REUSE-T07-20261024-r1/02-scope.log)）：

- 治理文本 3 件：`reuse/audit-contract.md`、`reuse/migration.md`、`reuse/policy.md`；
- 契约 4 件：`tasks/REUSE-T01.json`、`tasks/REUSE-T02.json`、`tasks/REUSE-T05.json`、`tasks/REUSE-T06.json`；
- T01 证据 10 件：`traceability/reuse/REUSE-T01/capability-plan/**`。

这 17 项全部出自 `c27349a80` 单提交、全部属 REUSE-T01 审计包治理面（该包返工登记
[REUSE-T01-20261024-r1-fix/README.md](../REUSE-T01-20261024-r1-fix/README.md) 剔出项
表格在册把 `c27349a80` 列为 T01 字面区间剔出项），不属本契约授权变更。**不可通过补
allowedFiles 消化**（补入即把 T01 审计包治理面伪装成本任务授权变更）。

### 处置（r1 记录处置选项 A；REUSE-T01 返工 `9d889781c` 同型先例）

- 送验范围收敛为 `c27349a80..8e4a08ac8`：仅含实现提交 `8e4a08ac8`（「将耦合SVD接入上游
  并删除自写Jacobi核心」）。
- 本人复跑（不引用验收者记录）：`verify-task-scope -Base c27349a80 -Head 8e4a08ac8` →
  **PASS，286 个路径全部在允许面内**
  （[01-scope-collapsed.log](01-scope-collapsed.log)，与 r1 验收者探针
  [05-alt-scope-probe.log](../REUSE-T07-20261024-r1/05-alt-scope-probe.log) 同数字）；
  `commit-lint -Range c27349a80..8e4a08ac8` → **PASS，1 提交全部合规**
  （[02-commit-lint-collapsed.log](02-commit-lint-collapsed.log)）。
- 剔出项出路：`c27349a80` 的 17 个治理路径属 REUSE-T01 审计包治理面，由该包自身送验
  链处置——r1 记录 checks[1] 已核该提交包含于 REUSE-T01-20261024-r2（verdict **pass**，
  字面区间 `6509e5e4..9d889781`，ancestor 关系已核）并经其三段甄别为送验面外其他任务包
  内容；本返工不重复登记、不改写历史。
- **allowedFiles/forbiddenFiles 未为消化越界而放宽**：本轮仅追加本任务自身验收产物 3 条
  （`traceability/acceptance/REUSE-T07-20261024-r1.json`、`…-r1/**`、`…-r1-fix/**`），
  用于 r1 原件入库与本登记目录；forbiddenFiles 3 条零改动。

## Issue 2（性能）：所有者裁决登记——接受当前退化

### 裁决事实与依据

所有者 **2026-10-24 经主会话（AskUserQuestion，工作流 dwfrun-d425945b「返工·REUSE-T07」
升级）明示接受**该性能退化，传达原件存
[03-owner-decision.txt](03-owner-decision.txt)；按 REUSE-T20 性能接受先例（`2ac114f33`）
形态登记。裁决覆盖的关键数据（r1 记录 benchmark 段在册）：

| 项 | 值 | 证据 |
| --- | --- | --- |
| 原件测量 | baseline 中位 0.09174µs → migrated 0.42767µs（4.662 倍） | [comparison.json](../../reuse/REUSE-T07/benchmark/comparison.json)、[coupling-svd-migration.md §运行效果](../../../reuse/coupling-svd-migration.md) |
| 独验复跑 | baseline 0.11802µs vs migrated 0.40896µs（约 3.46 倍；两环境 migrated 0.409/0.428 吻合） | [07-benchmark-run.log](../REUSE-T07-20261024-r1/07-benchmark-run.log)、r1 记录 benchmark.rerun |
| 数值等价 | checksum 均 280400、两版 exit 0 | 同上 |
| 生产路径 | 建模编辑/就绪校验（非实时回路），绝对增量约 0.41µs/次 | r1 记录 benchmark.interpretation |

### 边界（T20 形态）

- 接受只覆盖本能力（耦合矩阵 SVD 迁移）已展示的上述数据与实现范围，**不推导其他能力的
  性能预算或接受**；后续新增实质退化仍需另行裁决。
- 运行效果仍交独验核查；acceptance#1「无依据退化不得自判可接受」以本裁决登记为依据，
  随裁决落地闭合——实施者此前「不得自判已接受或关闭」的登记与此一致：本登记记录的是
  所有者明示裁决，非实施者自判。
- 不做产品回滚、不改动迁移成果；Jacobi 删除与上游 SVD 生产链维持送验 head 原样。

## 本返工的验证（提交后自检三门禁，供所有者复核）

```
pwsh -NoProfile -File RobWork/scripts/industrialrobot/commit-lint.ps1 -Range 5fb4ebd3..HEAD
pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T07.json
pwsh -NoProfile -File RobWork/scripts/industrialrobot/verify-task-scope.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T07.json -Base 5fb4ebd3 -Head HEAD
```

返工包范围＝`5fb4ebd3..HEAD`（单提交）：r1 验收原件入库（`REUSE-T07-20261024-r1.json`＋
同名目录 12 份 log/xml）、本登记目录（README＋01—03 号证据）、契约 note 处置段＋
allowedFiles 追加 3 条——全部落在补后 allowedFiles 内。

第 2 轮独立验收建议核验面（REUSE-T01 r2 同型两段口径）＝收敛段
`c27349a80..8e4a08ac8`（上述 01/02 号 log）＋返工段 `5fb4ebd3..HEAD`（提交后复跑为准）。
r1 已实测的 verify#1（CMake 注释门禁 PS7/PS5.1）、verify#2（治理自测 152 例）、verify#3/#4
（validate 三合一）及集成 modeling 479/479、benchmark 复跑，因本轮零产品/零脚本变更按
AGENTS §4 增量口径直接引用 r1 原件，不重复执行；独验不由实施者自签。
