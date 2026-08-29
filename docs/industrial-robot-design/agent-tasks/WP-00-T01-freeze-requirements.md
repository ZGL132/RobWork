# WP-00-T01 冻结需求版本

- **Task ID / 需求 ID / ADR / 阶段：** WP-00-T01；治理对象为 `requirements.md` v0.8 全部 128 项需求（覆盖全部需求前缀与 NFR-* 五族）及 AT-01～19，WP-00 为治理层、无单条需求主包映射；ADR-001（单机械臂范围）、ADR-003（R1/R2 切片与 OPT-B 权威集合）；阶段 A 前提 / R1。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** 无（WP-00 首个任务）；启动输入为 `docs/industrial-robot-design/requirements.md` v0.7、`docs/industrial-robot-design/DOCUMENT-BASELINE.md` 基线身份表、`docs/industrial-robot-design/architecture/adr/ADR-001-single-robot-owner-scope.md` 与 `ADR-003-release-slices-and-opt-b.md`。
- **允许创建/修改/删除的文件：**
  - 创建：`out/test-evidence/wp-00/<run-id>/t01-requirements-review.md`
  - 修改：`docs/industrial-robot-design/DOCUMENT-BASELINE.md`（仅在文末追加"WP-00-T01 需求基线复核记录"一节，不改既有表格与状态）
- **禁止修改的文件和公共接口：** `requirements.md`、`requirement-traceability.csv`、`generate-traceability.ps1`、`validate-development-docs.ps1`、`schemas/`、`architecture/`、`module-design/`、`work-packages/` 与其余任务卡；不得改写任何需求语义、AT 定义或权威层次顺序。
- **修改前接口：** `DOCUMENT-BASELINE.md` 现有 §1 基线身份与 §2 基线组成表格，无 T01 复核记录章节。
- **修改后接口：** `DOCUMENT-BASELINE.md` 文末新增章节"WP-00-T01 需求基线复核记录"，内容仅引用既有事实（需求版本 v0.8、代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844、复核范围与结论）；`out/test-evidence/wp-00/<run-id>/t01-requirements-review.md` 为只读证据。
- **实施步骤：**
  1. 运行"RED 测试"两条计数断言，记录基线结果。
  2. 逐前缀核对 128 项需求的 ID 唯一性、优先级（P0=114、P1=14）与阶段/发布标注。
  3. 核对 AT-01～19 与 `requirements.md` 第 16 章追踪表一一对应。
  4. 核对单机械臂边界（ADR-001）与 OPT-B 唯一权威集合（ADR-003：OPT-01～04/06～08=R1/R2，OPT-05/09/10=R2）。
  5. 记录全部疑似不一致项（只记录，不修改权威文档）。
  6. 运行"精确验证命令"两条命令并保存原始输出，写入证据文件并追加基线章节。
- **RED 测试：** 文档任务无测试函数；先执行的基线断言（任一不成立即任务失败并上报）：`t01-req-count`：`(Select-String -Path docs\industrial-robot-design\requirements.md -Pattern '^\|\s*[A-Z][A-Z0-9-]*-\d+\s*\|\s*P[01]\s*\|').Count -eq 128`；`t01-at-count`：`(Select-String -Path docs\industrial-robot-design\requirements.md -Pattern '^\|\s*AT-\d+\s*\|').Count -eq 19`。
- **最小实现：** 复核记录（证据文件＋`DOCUMENT-BASELINE.md` 追加章节）为唯一实现范围；不修改任何脚本、需求正文或 CSV。
- **正常/边界/失败测试：**
  - 正常：Given 冻结后的 v0.8 基线，When 运行生成器与门禁，Then 生成器输出 128 行、门禁退出码 0 并输出成功行。
  - 边界：Given P0=114、P1=14 与 OPT-B 发布切片规则，When 逐前缀抽样核对，Then 计数与 ADR-003 切片完全一致、无前缀遗漏。
  - 失败：Given 发现重复 ID、计数偏差或 OPT-B 集合不一致，When 形成复核结论，Then 任务停止并上报需求维护者，不修改 `requirements.md`。
- **精确验证命令：**（仓库根目录、无交互 PowerShell）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1`（参数缺省：RequirementsPath/OutputPath 均取脚本同目录文件）；预期输出 `Generated 128 traceability rows at <OutputPath 绝对路径>`（OutputPath 末段为 requirement-traceability.csv）。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1`（无参数）；预期退出码 0 与成功行 `124 requirements, 19 acceptance tests, <派生> contracts, <派生> symbols, <派生> ADRs, 0 trace gaps`（计数由脚本按当前文档派生）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `DOCUMENT-BASELINE.md` 与新增 `out/test-evidence/wp-00/<run-id>/t01-requirements-review.md`；`requirements.md` 与 `requirement-traceability.csv` 零字节变化；两个文件均无替换字符与占位内容。
- **证据工件：** `out/test-evidence/wp-00/<run-id>/t01-requirements-review.md`：逐前缀需求计数表、AT-01～19 对应表、ADR-001/003 一致性结论、两条命令原始输出、执行时 commit SHA 与日期。
- **提交格式：** `WP-00-T01: 冻结需求版本`
- **停止与升级条件：** 发现需求正文、ADR 与追踪表语义冲突，或两条基线命令失败且原因不在本任务范围时，停止并升级给需求维护者；本任务复核者不得同时担任 WP-00-T02 实现者。
