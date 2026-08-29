# WP-00-T03 建立文档门禁

- **Task ID / 需求 ID / ADR / 阶段：** WP-00-T03；治理对象同 T01（门禁覆盖全部文档/CSV/manifest/任务卡）；ADR-004（权威层次与冲突处理顺序：需求正文 → 架构契约 → 总纲/WP → 模块方案 → 任务卡 → CSV）；阶段 A 前提 / R1。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-00-T02；工件：`docs/industrial-robot-design/requirement-traceability.csv`（行数与生成器输出一致、逐字节一致）与 `out/test-evidence/wp-00/<run-id>/t02-generation-log.md`。
- **允许创建/修改/删除的文件：**
  - 修改：`docs/industrial-robot-design/validate-development-docs.ps1`（验证顺序与诊断）
  - 创建：`docs/industrial-robot-design/fixtures/wp-00/run-fixtures.ps1` 与 8 个夹具目录 `fixtures/wp-00/{missing-requirement,duplicate-requirement,empty-acceptance,missing-work-package,stale-csv,invalid-release,orphan-task,table-shape}/`（各为最小文档副本，仅注入一种违规）
  - 创建：`out/test-evidence/wp-00/<run-id>/t03-gate-and-fixtures.md`
- **禁止修改的文件和公共接口：** `requirements.md`、`generate-traceability.ps1`、`requirement-traceability.csv`、`schemas/`、`architecture/`、`module-design/`、`work-packages/` 与其余任务卡；门禁不得自动修正任何文档；无参数签名与成功行格式不得变更。
- **修改前接口：** `validate-development-docs.ps1` 无参数、路径全部由 `$PSScriptRoot` 派生；成功行 `<派生> requirements, 19 acceptance tests, <派生> contracts, <派生> symbols, <派生> ADRs, 0 trace gaps`；失败经 `Write-Error` 累积后 `exit 1`。
- **修改后接口：** 签名与成功行格式不变；固化验证顺序（WP-00 计划 §5.3）：路径 → benchmark JSON → 需求/AT 计数 → CSV 字段/行/ID/release/phase → 总纲 WP → WP 文件与章节 → architecture/module-design → agent-tasks 字段与反向 Task ID → 表格/占位扫描 → 临时生成 CSV 逐字节比较；诊断包含文件、行、字段、ID 与修复动作；错误状态不得打印成功行；`run-fixtures.ps1` 在临时目录逐夹具执行门禁并断言"非零＋关键词＋正式目录哈希不变"。
- **实施步骤：**
  1. 先建 8 个夹具与 `run-fixtures.ps1`，在门禁未达契约时逐夹具运行，确认"预期非零"断言失败（RED）。
  2. 实现基础门禁：必需路径、`benchmark-manifest.json` 字段、需求/验收测试计数（与当前文档派生值一致）、CSV 字段/行/ID/release/phase 检查。
  3. 实现总纲 WP 清单、WP 文件唯一性与章节检查、architecture/module-design 必需文件检查。
  4. 实现 agent-tasks 文件名规则、任务卡字段与 WP 声明 Task ID 的双向覆盖检查。
  5. 实现表格形状、占位/替换字符扫描与临时 CSV 逐字节比较。
  6. 重跑全部夹具与正式门禁，全部转绿后写证据。
- **RED 测试：** 夹具即失败断言（先建夹具、后改门禁），每夹具断言三件事：非零退出、诊断关键词、正式目录哈希不变；关键词取 WP-00 计划 §6：`missing-requirement`→数量错误/CSV 缺失；`duplicate-requirement`→`IDs not unique`；`empty-acceptance`→`no acceptance trace`；`missing-work-package`→`unique detailed plan` 缺失；`stale-csv`→`CSV stale`；`invalid-release`→`invalid release`；`orphan-task`→`Task ID without card`；`table-shape`→`table separators`。
- **最小实现：** 仅在 `validate-development-docs.ps1` 内按固定顺序补齐检查与诊断文案、在 `fixtures/wp-00/` 内补齐夹具与执行器；不实现自动修复、不改成功行格式、不新增参数。
- **正常/边界/失败测试：**
  - 正常：Given 正式文档树，When 运行门禁，Then 退出码 0 并输出成功行。
  - 边界：Given 夹具在临时目录执行（最小副本），When 门禁失败，Then 正式目录文件哈希与运行前一致（夹具不覆盖正式文件、不进安装包）。
  - 失败：Given 任一夹具注入的违规，When 运行门禁，Then 非零退出、输出含关键词的稳定诊断、不产生部分修复或部分样本。
- **精确验证命令：**
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1`；预期退出码 0 与成功行。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\fixtures\wp-00\run-fixtures.ps1`；预期 8 个夹具全部"非零＋关键词＋哈希不变"，执行器自身退出码 0。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `validate-development-docs.ps1`、`fixtures/wp-00/`、`out/test-evidence/wp-00/<run-id>/t03-gate-and-fixtures.md`；`requirements.md`、`requirement-traceability.csv`、`generate-traceability.ps1` 零变化；夹具为最小副本，不含完整正式文档拷贝。
- **证据工件：** `out/test-evidence/wp-00/<run-id>/t03-gate-and-fixtures.md`：门禁成功行原文、8 夹具各自的退出码/诊断摘录/前后正式目录哈希、验证顺序清单。
- **提交格式：** `WP-00-T03: 建立文档门禁`
- **停止与升级条件：** 夹具期望诊断无法从 WP-00 计划 §6 推导，或合法文档被门禁误判失败时，停止并升级给文档治理负责人；门禁修改者不得同时担任 WP-00-T04 独立验证者。
