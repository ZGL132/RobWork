# WP-00-T02 生成追踪矩阵

- **Task ID / 需求 ID / ADR / 阶段：** WP-00-T02；治理对象同 T01（124 需求＋AT-01～19 的派生索引）；ADR-004（单一权威：`requirements.md` 为唯一权威，CSV 为派生索引、不可手改）；阶段 A 前提 / R1。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-00-T01；工件：`out/test-evidence/wp-00/<run-id>/t01-requirements-review.md`（复核结论为无不一致项）与 `DOCUMENT-BASELINE.md` 中的"WP-00-T01 需求基线复核记录"章节。
- **允许创建/修改/删除的文件：**
  - 修改：`docs/industrial-robot-design/generate-traceability.ps1`（仅函数体实现与诊断输出）
  - 重生成：`docs/industrial-robot-design/requirement-traceability.csv`（只能由本脚本产出）
  - 创建：`out/test-evidence/wp-00/<run-id>/t02-generation-log.md`
- **禁止修改的文件和公共接口：** `requirements.md`、`validate-development-docs.ps1`、`DOCUMENT-BASELINE.md`、`schemas/`、`architecture/`、`module-design/`、`work-packages/` 与其余任务卡；脚本参数名（`RequirementsPath`/`OutputPath`）与 CSV 11 列字段顺序、UTF-8 BOM＋CRLF 编码契约不得变更。
- **修改前接口：** `generate-traceability.ps1` 顶部 `param([string]$RequirementsPath = <脚本目录>\requirements.md, [string]$OutputPath = <脚本目录>\requirement-traceability.csv)`；成功输出行 `Generated <N> traceability rows at <OutputPath>`。
- **修改后接口：** 签名与成功输出行不变；固化行为契约（对应 WP-00 计划 §5）：`Expand-RequirementCell`（单 ID、连续范围、前缀续接、重复去重、非法范围失败）；`Get-WorkPackages`（特殊规则先于前缀规则，未知 ID 失败）；`Get-Release`（固定规则派生，禁止手写 release）；第 16 章验收聚合（每需求至少一条 Method/Scenario/Phase）；有序临时 CSV＋显式 UTF-8 BOM＋统一 CRLF 原子写入，失败不覆盖正式 CSV。
- **实施步骤：**
  1. 先执行"RED 测试"四条断言，记录基线结果。
  2. 实现或修正 `Expand-RequirementCell`：覆盖 REQ-01～03 连续范围、前缀续接、重复与反向范围。
  3. 实现或修正 `Get-WorkPackages`/`Get-Release`：124 个 ID 全部有映射；OPT-B、CON-04=R1/R2、NFR-PERF-04～06=R2 等特殊规则先于前缀规则。
  4. 实现或修正第 16 章锚点定位（缺 `## 16.` 或 `## 17.` 立即失败）、四列追踪表拆分与 acceptanceById 聚合。
  5. 实现 11 列固定顺序与排序、临时文件写入、原子替换；status 初始 Planned，P1 仅经评审可 Deferred。
  6. 重新运行四条断言与验证命令，全部成立后写证据。
- **RED 测试：** 断言先于实现执行（任何一条在基线上失败即为本任务 RED）：
  - `t02-rows`：`(Import-Csv .\docs\industrial-robot-design\requirement-traceability.csv).Count -eq 128`
  - `t02-columns`：CSV 表头恰为 11 列固定顺序（requirement_id、priority、requirement_summary、work_package、implementation_task、test_task、review_task、acceptance_scenario、phase、release、status），无空字段与未转义换行。
  - `t02-encoding`：文件首三字节为 EF BB BF 且行结束为 CRLF。
  - `t02-byte-stable`：连续两次运行生成器后 `requirement-traceability.csv` 逐字节一致（`Get-FileHash` 相同）。
- **最小实现：** 仅修改 `generate-traceability.ps1` 内上述函数与写入路径，使四条断言与 WP-00 计划 §4 字段契约成立；不新增参数、不改输出路径语义。
- **正常/边界/失败测试：**
  - 正常：Given 正式 v0.8 需求，When 运行生成器，Then 输出 128 行、11 列、UTF-8 BOM/CRLF。
  - 边界：Given REQ-01～03 展开、前缀续接与重复 ID 单元，When 展开聚合，Then 去重且顺序稳定，第一条主包为 `Get-WorkPackages` 首项。
  - 失败：Given 缺第 16 章锚点或非法需求范围，When 运行生成器，Then 非零退出、正式 CSV 未被覆盖、临时文件被清理。
- **精确验证命令：**
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\generate-traceability.ps1`；预期 `Generated 128 traceability rows at <OutputPath 绝对路径>`（末段为 requirement-traceability.csv）。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\validate-development-docs.ps1`；预期退出码 0（含临时生成 CSV 与正式 CSV 逐字节比较通过）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `generate-traceability.ps1`、`requirement-traceability.csv`、`out/test-evidence/wp-00/<run-id>/t02-generation-log.md`；CSV 每行变更均能由重放生成器解释（无手工行）；`requirements.md` 零变化；无占位内容。
- **证据工件：** `out/test-evidence/wp-00/<run-id>/t02-generation-log.md`：四条断言前后结果、生成器输出行、CSV 行数/列数与前 3 行摘录、BOM/CRLF 字节证据、两次运行的 `Get-FileHash` 值。
- **提交格式：** `WP-00-T02: 生成追踪矩阵`
- **停止与升级条件：** 11 列字段契约、状态枚举或发布规则无法从 `requirements.md` 第 16 章与 WP-00 计划 §4 推导，或需求正文自相矛盾时，停止并升级给需求维护者；不得以 CSV 反推语义。
