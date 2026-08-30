# WP-00-T03 建立文档门禁证据

- Task ID：WP-00-T03（建立文档门禁）
- 执行日期：2026-08-30
- 执行时文档基线 commit：`ecf6505`（main HEAD；实现提交 SHA 于提交后补入完成报告）
- 前置工件：`requirement-traceability.csv`（与生成器输出逐字节一致）、`out/test-evidence/wp-00/20260830-da8ec71-impl/t02-generation-log.md`（WP-00-T02 已由治理提交 `ecf6505` 登记 `Done`）
- 执行环境：Windows 10 x64，Windows PowerShell 5.1，仓库根目录，无交互
- 结论：8 个夹具全部"非零退出＋§6 关键词＋正式目录哈希不变"，执行器退出码 0；正式门禁退出码 0 与成功行；Schema 门禁补充验证通过。

## 1. RED（先建夹具、后改门禁）

夹具与 `run-fixtures.ps1` 先于门禁修改建立。对未修改门禁的运行结果：

| 阶段 | 结果 |
| --- | --- |
| 第一次运行 | 门禁的 `git cat-file` 检查在临时树（非 git 仓库）内因 stderr ErrorRecord 与 `$ErrorActionPreference='Stop'` 相互作用直接终止，夹具无法执行（RED 发现 #0） |
| 第二次运行 | 执行器子进程调用被被测门禁/生成器 stderr 的 ErrorRecord 中断（执行器自身健壮性缺陷，修复执行器） |
| 第三次运行 | 执行器 `$LASTEXITCODE` 被上一夹具门禁退出码污染（进程内 `&` 调用不重置；改为子进程执行注入脚本） |
| 第四次运行（完整 RED） | 干净树 exit 0；8 夹具中 6 个关键词断言失败（`missing-requirement`、`duplicate-requirement`、`empty-acceptance`、`missing-work-package`、`stale-csv`、`orphan-task` 的门禁诊断不含 WP-00 计划 §6 关键词），2 个通过（`invalid-release`、`table-shape` 原文案已含连续关键词） |

## 2. 实现内容（仅 validate-development-docs.ps1 与 fixtures/wp-00/）

- **验证顺序固化（WP-00 计划 §5.3）**：临时生成 CSV 逐字节比较从脚本中段移至全部检查之后（末位），并仅当无既有错误且生成成功时执行。
- **§16 验收覆盖检查（新增）**：以 `## 16.`/`## 17.` 锚点切分、按 `Expand-RequirementCell` 同语义展开需求 ID，断言每个需求至少一条 Method/Scenario/Phase；诊断含 `no acceptance trace` 与未覆盖 ID 清单。
- **重复 ID 检测补强**：原 `Sort-Object -Unique` 计数检查无法检出重复行（唯一计数不变），新增"总数 vs 唯一数"比较，诊断含 `IDs not unique`。
- **诊断关键词对齐（§6）与修复动作**：数量错误（`Requirement count error (数量错误)`）、`IDs not unique`、`no acceptance trace`、`unique detailed plan`、`CSV stale`、`invalid release`（补修复提示）、`Task ID without card`、`table separators`（补修复提示）。
- **诊断输出改造**：`Write-Error` 会被宿主按控制台宽度折行、破坏关键词连续性且首条即终止（`Stop` 偏好），改为 `[Console]::Error.WriteLine` 逐条输出（stderr＋exit 1 语义不变，不折行，成功行格式不变）。
- **基线 commit 检查**：`git rev-parse --is-inside-work-tree` 先行，非 git 目录（临时夹具树）跳过；正式树行为不变；局部 `$ErrorActionPreference='Continue'` 防 stderr 终止。
- **夹具资产**：`fixtures/wp-00/run-fixtures.ps1`（执行器）、`inject-helper.ps1`（BOM/CRLF 保持的文本编辑助手）、8 个 `<fixture>/inject.ps1`（各注入一种违规；仓库内不含完整文档拷贝，被测树由执行器运行时复制到临时目录）。

## 3. GREEN：8 夹具结果（执行器最终运行，退出码 0）

| 夹具 | 门禁退出码 | 关键词 | 诊断摘录 |
| --- | --- | --- | --- |
| missing-requirement | 1 | 数量错误 | Requirement count error (数量错误): expected 128 requirement rows in requirements.md; found 127. |
| duplicate-requirement | 1 | IDs not unique | Requirement IDs not unique in requirements.md; remove the duplicated ID rows. |
| empty-acceptance | 1 | no acceptance trace | no acceptance trace for 128 requirement(s) in section 16 (需求—验收追踪): ARC-01, …; restore the missing trace rows. |
| missing-work-package | 1 | unique detailed plan | Missing unique detailed plan for WP-07-: expected exactly one work-packages/WP-XX-*.md. |
| stale-csv | 1 | CSV stale | Traceability CSV stale: regenerate it from the requirements document with generate-traceability.ps1. |
| invalid-release | 1 | invalid release | Trace row ARC-01 has invalid release R9. |
| orphan-task | 1 | Task ID without card | Task ID without card: work package WP-25-pilot-and-delivery.md declares WP-25-T05. |
| table-shape | 1 | table separators | …development-task-breakdown.md line 129 has 6 table separators; expected 7. |

- 干净树（未注入）：门禁退出码 0（`clean-tree exit=0 pass=True`）。
- 正式目录哈希：运行前后一致。

## 4. 精确验证命令（逐字执行，仓库根）

命令 1：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1
128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps
exit code: 0
```

命令 2：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\fixtures\wp-00\run-fixtures.ps1
clean-tree exit=0 pass=True … ALL FIXTURES PASSED (non-zero exit + keyword + official tree unchanged)
exit code: 0
```

补充验证（非卡内必执行，治理完整性）：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\schemas\validate-schemas.ps1
14 schemas, 14 examples valid, 45 invalid correctly rejected
All checks passed.
exit code: 0
```

## 5. 正式目录哈希（官方树状态）

- 运行前：`4C-4B-6D-8E-F3-C1-7B-17-EE-67-11-4E-4C-84-66-BE-5C-3B-96-35-A8-CD-16-8A-04-1D-68-30-36-CD-72-35`
- 运行后：`4C-4B-6D-8E-F3-C1-7B-17-EE-67-11-4E-4C-84-66-BE-5C-3B-96-35-A8-CD-16-8A-04-1D-68-30-36-CD-72-35`
- 断言 `official-tree-hash-unchanged=True`；夹具不覆盖正式文件。

## 6. 发现与记录

1. **生成器空 §16 防线失效（既有缺陷，本卡禁改 `generate-traceability.ps1`，仅记录）**：`@($acceptanceById[$id])` 在键缺失时得到 `@($null)`，Count 为 1，`no acceptance trace entry` 抛出永不触发；空 §16 时生成器以空 `acceptance_scenario` 成功产出。门禁新增的 §16 覆盖检查为该语义的唯一有效防线。建议治理将生成器修复列入后续任务。
2. **原门禁重复 ID 检测缺口（已在本卡修复）**：原 `Sort-Object -Unique` 计数检查只检"唯一数≠128"，重复行（唯一数仍为 128）漏检；新增总数 vs 唯一数比较。
3. PowerShell 7 不可用（本机未安装 `pwsh`），双环境证据仅 PS 5.1 路，已如实记录。

## 7. 范围与禁止项检查

- `git diff --name-only` 仅含 `validate-development-docs.ps1`、`fixtures/wp-00/` 新增文件与本证据文件。
- `requirements.md`、`requirement-traceability.csv`、`generate-traceability.ps1` 零字节变化。
- 门禁无参数签名与成功行格式未变；无自动修复逻辑；未触碰用户既有修改（WP-24 相关等）。
