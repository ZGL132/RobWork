# WP-00-T04 独立验证与评审

- **Task ID / 需求 ID / ADR / 阶段：** WP-00-T04；治理对象同 T01（抽样回溯全部追踪链并复核 ADR-001～005 与需求一致性）；无新增 ADR；阶段 A 前提 / R1。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-00-T01（`out/test-evidence/wp-00/<run-id>/t01-requirements-review.md`）、WP-00-T02（`requirement-traceability.csv`＋`out/test-evidence/wp-00/<run-id>/t02-generation-log.md`）、WP-00-T03（`validate-development-docs.ps1` 门禁通过＋`fixtures/wp-00/run-fixtures.ps1` 8 夹具通过＋`out/test-evidence/wp-00/<run-id>/t03-gate-and-fixtures.md`）。
- **允许创建/修改/删除的文件：** 创建 `out/test-evidence/wp-00/<run-id>/t04-independent-review.md` 与 `out/test-evidence/wp-00/<run-id>/t04-dual-shell-byte-compare.md`；不修改任何代码或文档正文。
- **禁止修改的文件和公共接口：** 除上述两个新增证据文件外的一切文件（含 `requirements.md`、CSV、两个脚本、`fixtures/`、`work-packages/`、`architecture/`、`module-design/`、其余任务卡）；不得边评审边修复，发现问题退回对应任务重开。
- **修改前接口：** 无（新增）。
- **修改后接口：** 两份只读评审记录：`t04-independent-review.md` 含逐前缀抽样表（每个需求前缀至少 2 项）与六环节链路回溯结论（需求 → CSV 行 → work-packages 计划 → module-design 方案 → agent-tasks 任务卡 → 测试/评审任务）；`t04-dual-shell-byte-compare.md` 含 powershell.exe 与 pwsh.exe 门禁输出对照与 CSV 哈希。
- **实施步骤：**
  1. 在 Visual Studio x64 PowerShell 环境分别用 powershell.exe 与 pwsh.exe 运行门禁，记录输出与 `requirement-traceability.csv` 的 `Get-FileHash` 值。
  2. 按全部需求前缀各抽至少 2 项需求，沿六环节链正向与反向回溯。
  3. 复核 T01～T03 证据工件齐全、结论一致、夹具全部按预期非零。
  4. 记录全部断点（文件、行、字段/ID、修复动作建议），不直接修改任何文件。
  5. 出具评审结论并签署（独立执行上下文）。
- **RED 测试：** 抽样断言先于结论执行：每条抽样链若任一环节缺失（CSV 行缺失、WP 未声明 Task ID、卡片孤立、模块方案未引用），评审即失败并生成断点记录；门禁应已拦截该情形，本任务验证其确实拦截（复跑 `run-fixtures.ps1` 确认 8 夹具仍全部非零）。
- **最小实现：** 两份证据文件为唯一产出；不修改任何被评审对象。
- **正常/边界/失败测试：**
  - 正常：Given T01～T03 全部通过，When 双 shell 运行门禁，Then 两环境退出码 0、成功行一致、CSV 哈希一致。
  - 边界：Given 每前缀至少 2 项抽样，When 双向回溯，Then 每条链六环节齐全且语义一致、P0 覆盖 100%。
  - 失败：Given 任一链断裂或双 shell 输出不一致，When 记录断点，Then 评审不通过，问题退回 T01～T03 对应任务并升级。
- **精确验证命令：**（仓库根目录、VS x64 PowerShell 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1`；预期退出码 0。
  - `pwsh.exe -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1`；预期退出码 0、成功行与上一条完全一致。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\fixtures\wp-00\run-fixtures.ps1`；预期 8 夹具全部非零命中。
- **diff 和禁止项检查：** `git diff --name-only` 仅含两个新增证据文件；其余文件（含 `requirement-traceability.csv`）零变化；评审者与 T01～T03 执行者非同一执行上下文。
- **证据工件：** `t04-independent-review.md`（逐前缀抽样表、链路结论、断点清单、评审签署）；`t04-dual-shell-byte-compare.md`（两 shell 原始输出、CSV 哈希对照、PowerShell 与操作系统版本）。
- **提交格式：** `WP-00-T04: 独立验证与评审`
- **停止与升级条件：** 发现需求、架构契约、WP、模块方案与任务卡之间语义冲突，或双 shell 结果不一致时，停止并升级给文档治理负责人；抽样不足、证据缺失或责任分离不满足时不得签署通过。
