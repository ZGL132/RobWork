# REUSE-T59 —— foundation-governance 脚本结项格式规则批（RC-10 规则面）

批次性质：**规则＋正例单行登记＋负例测试**；106 行脚本的逐行关闭**不在本批**（规则未独验前不据其关闭债务）。起始 SHA＝`0d1c27bf29a0cacba873fe230c00c6446a54a7f3`（redesign-main 串行主工作树，查重 2026-10-28 实测：changes/、tasks/、traceability/reuse/ 均无 REUSE-T59）。

授权口径：migration.md §13.5.4 RC-10（:426）——「先提出并独立验收与既有 closures/acceptance 一致的脚本结项格式/校验，再收这 106 行」「新增规则未独验前不得据其关闭债务」；所有者 2026-10-27 委托「后续所有裁决授权按建议进行」。本批按建议裁决登记；**规则独立验收 NOT RUN 待所有者**（初验＝本批负例＋正例真实执行，见下）。

## 1. 规则形态定义（reuse-contract.ps1 scriptAudit 结项分支）

被扩展的既有约束（前哨实测锚点，全部原样保留，无一放宽）：

- `reuse-contract.ps1:232` cap=NONE 行 decision ∈ {business-specific, pending, proposal}；
- `:240` execution 必备 {kind, entry, command}；
- `:244-245` entry 精确匹配 `^RobWork/scripts/industrialrobot/[^/]+\.ps1$` 且 command 包含 entry；
- `:249-253` sources 逐条全文区段＋sha256 绑定且 ⊆ inputs，仅限脚本目录；
- `:257` inputs 逐条 CheckBinding；
- `:265-272` sources 区段 Digest＋symbol 在区段内；
- `:284` business-specific 必备 nonOverlapEvidence（CheckBinding）。

新增结项分支（仅当 `decision=business-specific 且 status ∈ {migrated, closed}` 时激活；pending/proposal 行为逐字节不变；`audited` 不是脚本行准入状态——脚本行脱离 pending/proposal 必须直接带结项证据）：

1. **结项形态准入**：允许 `decision=business-specific`＋`status=migrated/closed`；其余 decision/status 组合仍按既有 pending/proposal 约束拒绝（`REUSE-EXECUTION`）。
2. **completionEvidence（新增必备）**：`{path, sha256}`，CheckBinding 口径（路径 SafePath 合法、文件存在、Digest 相符）——真实执行记录/报告（本批正例绑定 §4 的 run.log）。
3. **entry 区段覆盖（新增加严）**：entry 自身必须出现在 sources（`REUSE-EXECUTION : closure form must bind entry region`）。
4. **dot-source helper 完整性（新增加严）**：对 entry 与全部 sources 逐文件扫描已检出的 dot-source 引用（仅 `$PSScriptRoot`／`./`／`.\` 三种形态，检测为下界），每个被引用的同目录 `*.ps1` 必须登记为 sources 区段（`REUSE-EXECUTION : dot-sourced helper not bound in sources`）；`$scriptDir` 等别名形态与未检出调用链仍由独立语义验收核对（沿 :251 既有注释口径）。
5. **targets 仍必须为空**（:247 不放宽）。
6. **closed 沿 closures 协议（status=closed 时必备）**：行内 `acceptance={commit, path, reviewedCommit}`——
   - path 必须为 `traceability/acceptance/*.json`；commit/reviewedCommit 必须 40 位 hex；
   - `git show commit:path` 的验收记录对象必须存在、`verdict=pass`、`taskId=REUSE-*`、`record.reviewedCommit == acceptance.reviewedCommit`；
   - reviewedCommit 必须为当前 tip 祖先；
   - 被验提交处的矩阵行必须存在且 status ∈ {migrated, closed}，且与当前行**除 status 推进与 acceptance 引用补记外逐字段一致**（防止验收后更换来源/证据/边界——与 closures.json :347-357 的既有语义一致）。
   违反任一项 → `REUSE-CLOSURE`。

foundation-governance.json schema 影响面：新增两个可选行级字段 `completionEvidence`（结项必备）、`acceptance`（closed 必备）；其余 schema 不变；不触 catalog.json/permissions.json/closures.json。

## 2. 正例登记（本批唯一行变更）

`foundation-governance.designGolden.test-golden-assets`（单行，entry=RobWork/scripts/industrialrobot/test-golden-assets.ps1，91 行）：

- `decision: pending→business-specific`，`status: pending→migrated`（closed 留给独验后关闭批——acceptance 须真实独立验收记录，本批不伪造）；
- sources 扩为两条全文区段绑定：entry 全文 1-91 ＋ dot-source helper `golden-assets.ps1` 全文 1-43（`Test-DeclaredGoldenJson`）；inputs 同步补 helper 绑定；
- `completionEvidence` → 本目录 `test-golden-assets-run.log`（§4）；
- `nonOverlapEvidence` → 本 README（§3 论证＋检索证据）；
- 其余 105 行 governance-script 行 decision/status/语义登记零改动（保持 pending）；其中 **10 行因本批授权修改 `reuse-contract.ps1` 而对它的 input 绑定 sha256 做机械刷新**（`foundation-governance.validate-task.{root,Test-AnchorExists,Test-AnchorRequired,index-and-contract,reuse-gate}`、`foundation-governance.verify-task.{preflight-and-dryrun,execute-and-count}`、`foundation-governance.test-governance-lite.{environment,Invoke-Probe,fixtures-and-report}`——唯一变化＝哈希值 `7ffa6694…`→`780a4b1d…`，即被授权编辑文件的新 Digest 实算值；无任何 decision/status/来源/语义字段变化，属授权编辑的直接机械后果而非登记变更）；
- 两脚本（test-golden-assets.ps1/golden-assets.ps1）的 sha256 现值实算与原登记值逐一相等（`2ff51394…`、`32a1af18…`，Digest 口径：剥 BOM＋CRLF→LF＋尾换行），零代码改动。

黄金 .mjs 19 条（`make_dyn_two_link_golden` 5＋`make_opt_pareto_golden` 14 的 pending 行，绑定 `testdata/golden/*/generate/*.mjs`）**不在 106 行内、不受新规则影响**：它们是无 execution 绑定的产品路径行（targets 绑 CMake 测试目标），结项走既有产品路径（:284 nonOverlapEvidence＋closures.json 协议；同文件 3 条 business-specific/migrated 先例＝conditionIdHex/import_expectations 两行），分类登记形态不变。

## 3. nonOverlap 论证（business-specific 依据，:284）

`test-golden-assets.ps1`/`golden-assets.ps1` 是本仓治理侧黄金 JSON 数据资产的完整性校验执行关系：`ird-golden-manifest/1` 数据协议与 `Test-DeclaredGoldenJson` 校验入口为 DESIGN-T01 本仓自建治理设施，不是上游原语的复制/重写/绕过：

- 上游检索实测（2026-10-28，HEAD 0d1c27bf2，零命中）：
  - S1 `rg -l 'ird-golden-manifest' RobWork/RobWork/src RobWork/RobWorkSim/src` → exit 1（零命中）；
  - S2 `rg -li 'declaredgolden|golden.?manifest' RobWork/RobWork/src RobWork/RobWorkSim/src` → exit 1（零命中）；
  - S3 `rg -c 'Test-DeclaredGoldenJson' RobWork/scripts/industrialrobot/*.ps1` → 仅 3 个治理脚本命中（14/2/1），产品/上游零引用；
- 无上游对象消费：脚本零 `rw/*`/`rws/*` include、零模型/State 操作（纯本地 JSON manifest/完整性校验），不触发权限面；
- 非核心算法、无平行实现结构： golden-assets.ps1 仅 SHA-256 摘要比对与 manifest 身份检查（标准工具），upgrade 路径＝无（治理脚本无上游迁移对象）。

## 4. 真实执行证据（completionEvidence）

- `test-golden-assets-run.log`：`pwsh -NoProfile -File RobWork/scripts/industrialrobot/test-golden-assets.ps1` 于 HEAD `0d1c27bf2`、2026-10-28 实跑，exit 0，17/17 PASS（命令、HEAD、退出码与全量 stdout 在 log 头部）。sha256 见矩阵行 completionEvidence 绑定（Digest 口径实算）。
- 执行只写 `build/design-fix-t01/golden-assets/` 合成夹具根（脚本内 SafePath/ReadText 均指向该根），未触碰 `testdata/golden` 真实黄金资产与任何治理数据（沿 RC-00「不顺手重建黄金资产」边界）。

## 5. 负例清单（test-reuse-governance.ps1 新增 15 Case，全部真实执行）

| Case | 注入 | 断言失败码 |
| --- | --- | --- |
| script-closure-migrated-positive | 正例：migrated＋completionEvidence＋helper 绑定 | PASS |
| script-closure-closed-positive | 正例：closed＋真实 acceptance 记录（sandbox 冻结提交） | PASS |
| script-closure-missing-completion-evidence | 删除 completionEvidence | REUSE-EXECUTION |
| script-closure-stale-completion-evidence | completionEvidence.sha256 伪造 | REUSE-FRESHNESS |
| script-closure-missing-completion-file | completionEvidence.path 不存在 | REUSE-MISSING |
| script-closure-entry-region-unbound | sources 缺 entry 区段 | REUSE-EXECUTION |
| script-closure-missing-helper-region | sources 遗漏同目录 dot-source helper 区段 | REUSE-EXECUTION |
| script-closure-stale-source-region | sources 区段摘要过期 | REUSE-FRESHNESS |
| script-closure-targets-nonempty | targets 非空（:247 不放宽） | REUSE-EXECUTION |
| script-closure-audited-not-admitted | business-specific/audited（非准入状态） | REUSE-EXECUTION |
| script-closure-closed-without-acceptance | 未绑 acceptance 就 closed | REUSE-CLOSURE |
| script-closure-forged-acceptance-rejected | 验收记录 verdict=fail | REUSE-CLOSURE |
| script-closure-acceptance-object-missing | acceptance.path 指向记录提交中不存在的对象 | REUSE-CLOSURE |
| script-closure-acceptance-path-outside | acceptance.path 出 traceability/acceptance | REUSE-CLOSURE |
| script-closure-acceptance-future-sha-rejected | reviewedCommit=全零（不可达 SHA） | REUSE-CLOSURE |

夹具（ScriptClosureMatrix/CommitClosureFixtureRegistration/CommitClosureRecord）仅在沙箱检出内写入与提交，Case finally 字节恢复＋mixed reset；expectedTests 157→172。

**沙箱基建修复（本批实施发现，HEAD 实测复现）**：validate-reuse.ps1:49 自 DESIGN-T01（5f7f39147）起 dot-source `golden-assets.ps1`，而 harness 沙箱只复制 validator＋reuse-contract.ps1——门禁在隔离检出于加载第 49 行即断，全部用例 exit 1（修复前实测 157 例全红，`.cache/reuse-tests/*.log` 均为同一错误）。修复＝harness 补 `Copy-Item golden-assets.ps1` 进沙箱并随报告钉 `goldenAssetsSha256`；属负例可真实执行的前提修复，不放宽任何门禁语义。

## 6. NOT RUN（如实分列）

- **规则独立验收**：NOT RUN，待所有者另开会话（实施者不自签）。本批负例＋正例真实执行仅构成初验，不冒称独验 PASS。
- **106 行逐行关闭**：未启动。按 RC-10 :426「新增规则未独验前不得据其关闭债务」，关闭批在规则独验后另批实施。
- commit-lint/verify-task-scope：本批不执行 git 写命令，随提交批显式 `-Base 0d1c27bf2 -Head <送验SHA>` 执行。
- C++ 构建/ird_gates：零产品代码、CMake、公共接口变更（仅治理脚本＋设计区登记），按 AGENTS §4 增量范围为治理脚本正负例＋文档门禁。

## 7. 独立验收核验点（留给所有者独验会话）

1. 规则变更 diff：`reuse-contract.ps1` scriptAudit 分支（:238 起结项块）——核对既有约束锚点（:232/:240/:244-245/:249-253/:257/:284）未放宽、结项分支仅加严；
2. 正例：矩阵行 `designGolden.test-golden-assets` 现树实测——decision/status、两条 sources 区段 Digest 复算、inputs、completionEvidence/nonOverlapEvidence CheckBinding；10 行 input 绑定刷新仅为哈希值替换（diff 逐行核：10 行 `7ffa6694…`→`780a4b1d…`，无其他字段变化）；
3. completionEvidence 复跑：README §4 命令重放，stdout 与 run.log 比对＋Digest 复算一致；
4. 负例套件：§5 全量重跑（`test-reuse-governance.ps1`，172 例全绿）＋抽查至少 3 个新 Case 的失败输出确含断言码；
5. 语义面（机器不能自证）：helper 检测为下界的接受度、closed 行 acceptance 引用补记豁免的接受度、`audited` 非准入状态的裁决确认；
6. 独验 PASS 后的关闭批入口：106 行逐行迁移 closed＋行内 acceptance（或按所有者裁决改走其他形态）。
