# RobWork 工业机械臂 Agent-Native 上下文治理实施计划

> **For agentic workers:** 使用 superpowers:executing-plans 或 superpowers:subagent-driven-development，逐任务执行；每个任务独立提交。

**目标：** 把 industrial-robot-design 从“所有设计、任务、运行证据混在一棵树里”改造成 DSH 风格的分层 Agent 工作台，让新会话能按任务加载最小充分上下文、执行针对性验证并完成交接。

**架构：** 保留现有需求、架构、单元详设和任务 JSON 的语义，不一次性重写。新增分层规则、决策记录、可触发技能、上下文包和证据索引；先索引和校验，再逐步迁移，最后用真实任务做新会话接手演练。

**技术栈：** Markdown、JSON、PowerShell、CMake/CTest、GoogleTest、Visual Studio 2022 x64、Qt 6 Widgets。

**评审修正记录（2026-10-17，计划首次执行前吸收）：**

1. **复用既有状态层，不重建任务状态规范**——流水线状态机器校验层已存在（`automation-pipeline.md` §0.2 state.json schema、`validate-state.ps1`、`validate-task.ps1`、`traceability/findings.json` 登记簿）。Task 3 的任务 JSON 字段补齐必须对齐 state.json 既有 schema，不另造第二套状态语义；资产盘点复用 `audit/file-inventory.txt`（868 行）与 `audit/full-audit-report.md` 既有底稿。
2. **CI 不是从零接入，是扩面挂载**——CI 门禁已由 WP-01-T02 落位（`scripts/industrialrobot/ci/` GitHub/GitLab 双模板，2026-10-12 已与 gate-all 测试面拉齐）。Task 6/7 新增校验脚本的工作是挂载进既有 workflow 与 `gate-all.ps1`，且可在 Task 6 完成后部分前移，不等全部任务完工。
3. **与 PIPE 的权威关系显式化**——`traceability/pipeline/state.json` 是流水线状态的唯一权威；asset-inventory 与后续 context pack 只登记其路径与 schema 出处，**不复制 status/phase/queue 等状态字段**。防止治理层与 PIPE 出现两套"当前任务状态"答案（Task 1 起生效，见 context/README.md 权威关系节）。
4. **Task 3 删除动作推迟**——第一遍对大文档只做加法（补"本文负责/不负责/下游入口"三段＋链接化），历史叙述与重复模板的删除推迟到 Task 7 证据索引建成、可做链接完整性校验之后单独批次执行。理由：DTB 是追加式治理文档，findings/验收记录大量交叉引用其章节号，贸然删除产生断链且 revert 困难。

另附一条保留意见：Task 5 Skills 的验收口径是"新会话按 AGENTS.md 入口能找到并照执行"，不要求运行端自动发现/自动触发（项目级 skills 的自动发现能力因 Agent 运行端而异）。

---

### Task 1：冻结基线并建立资产清单

**Files**

- Create: RobWork/doc/industrial-robot-design/context/README.md
- Create: RobWork/doc/industrial-robot-design/context/asset-inventory.json
- Modify: RobWork/scripts/industrialrobot/validate-docs.ps1

- [ ] 统计当前 20 个单元、任务 JSON 数量、traceability 文件数量和现有门禁结果；基线快照记入 context/README.md 并标注快照日期。盘点复用既有底稿 `audit/file-inventory.txt` 与 `audit/full-audit-report.md`，不重复建库存（评审修正 1）。
- [ ] 在 context/README.md 定义 authority、workflow、task-source、generated、evidence、decision、archive 七类资产，以及默认是否进入 Agent 上下文；`traceability/pipeline/state.json` 归入 authority 类并声明"流水线状态唯一权威＝state.json，清单与上下文包只引用、不复制状态字段"（评审修正 3）。
- [ ] 在 asset-inventory.json 为每个根文档和一级目录记录 path、kind、owner、defaultContext、generated、canonicalLinks；另显式登记 load-bearing 嵌套资产（state.json、findings.json、requirements-to-units.json、phase-one-task-index.json）。
- [ ] 扩展 validate-docs.ps1，检查清单路径存在、分类合法、链接可解析、未登记根级资产时报错；保留现有任务 ID、20 单元和 B.1 检查。
- [ ] 运行 pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1 和 git diff --check；提交 [docs] 建立工业机械臂文档资产清单与分类门禁。

禁止在本任务删除或移动任何 traceability 产物。

### Task 2：建立 DSH 风格的作用域规则

**Files**

- Create: RobWork/doc/industrial-robot-design/AGENTS.md
- Create: RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md
- Modify: AGENTS.md
- （落位适配 2026-10-17：原定 tests/AGENTS.md 并入源码区 AGENTS.md 的"测试规则"节——仓库无顶层 tests/ 目录，测试按 `<unit>/test`、`<unit>/contract_test`、`<unit>/gui_test`、`<unit>/perf_test` 分散组织，单设 tests/ 目录放规则文件不会被按目录工作的 Agent 发现）

- [ ] 在根 AGENTS.md 增加阅读顺序：根规则、设计规则、对应 unit、任务 JSON、验证脚本、证据索引。
- [ ] 在 design/AGENTS.md 明确 REQUIREMENTS、ARCHITECTURE、units、development-task-breakdown、tasks 和 traceability 的权威关系。
- [ ] 在源码 AGENTS.md 只保留 C++17、命名空间、Qt 分层、公共头、框架只读、注释契约、测试命名和 Windows GUI 规则。
- [ ] 在源码区 AGENTS.md 测试规则节规定单元、契约、GUI、性能四类测试的所有者与证据输出；"集成"口径＝双模式构建的集成模式门禁（根 §4.1，唯一交付口径）。
- [ ] 扩展 validate-docs.ps1 检查规则文件存在及链接有效；运行门禁并提交 [docs] 增加工业机械臂分层 Agent 规则入口。根 AGENTS.md 的改动单独成提交、单独验收（回退讨论对策：入口文件影响所有后续会话）。

### Task 3：建立最小阅读路径，停止默认全文加载

**Files**

- Create: RobWork/doc/industrial-robot-design/README.md
- Create: RobWork/doc/industrial-robot-design/context/reading-paths.md
- Modify: DETAILED-DESIGN.md
- Modify: development-task-breakdown.md
- Modify: automation-pipeline.md
- Modify: acceptance-protocol.md
- Modify: contract-compilation.md

- [ ] README.md 只保留项目定位、当前阶段、入口表、变更规则和最短阅读路径。
- [ ] reading-paths.md 定义核心算法、Qt 功能、项目持久化、流程门禁、测试故障五条路径；每条列必须读、按需读、禁止默认读的内容。
- [ ] 为四份大文档增加"本文负责／本文不负责／下游入口"三段并用链接替代重复叙述。第一遍只做加法——历史叙述与重复模板的**删除推迟**到 Task 7 证据索引建成、可做链接完整性校验之后单独批次执行（评审修正 4）。
- [ ] 为任务 JSON 统一补齐 status、ownerUnit、sourceRequirements、verificationCommands、evidencePath；字段口径对齐 automation-pipeline.md §0.2 state.json 既有 schema 与 findings.json 登记簿，不另造第二套状态语义——任务执行态以 state.json 为唯一权威，任务 JSON 只承载引用与验证信息（评审修正 1）。旧字段先兼容读取并由脚本报告缺失。
- [ ] 运行文档门禁、JSON 校验和 git diff --check；提交 [docs] 建立工业机械臂最小阅读路径与文档职责。

### Task 4：迁移长期决策到 Agent Notes

**Files**

- Create: RobWork/.agents/notes/README.md
- Create: RobWork/.agents/notes/proposed/.gitkeep
- Create: RobWork/.agents/notes/implemented/.gitkeep
- Create: RobWork/.agents/notes/rejected/.gitkeep
- Create: RobWork/.agents/notes/archived/.gitkeep
- Create: 首批五条 implemented Note
- Modify: context/asset-inventory.json
- Modify: validate-docs.ps1

- [ ] 定义 Problem、Decision、Alternatives considered、Consequences、Verification、Related sources 格式。
- [ ] 从 ARCHITECTURE 的 SA-02、业务单元依赖红线、不可变修订、确定性编译、Qt 与计算内核分层中迁移五条稳定决策。
- [ ] ARCHITECTURE 只保留当前规则和 Note 链接；运行日志、验收过程和重复任务描述不迁移。
- [ ] 校验 status、必需章节和 Related sources；implemented Note 不得使用未实现的计划语气。
- [ ] 运行门禁并提交 [governance] 建立工业机械臂 Agent 决策记录。

### Task 5：把执行协议拆成可触发的 Agent Skills

**Files**

- Create: RobWork/.agents/skills/ird-discover-change/SKILL.md
- Create: RobWork/.agents/skills/ird-design-change/SKILL.md
- Create: RobWork/.agents/skills/ird-implement-task/SKILL.md
- Create: RobWork/.agents/skills/ird-verify-change/SKILL.md
- Create: RobWork/.agents/skills/ird-qt-gui-windows/SKILL.md
- Create: RobWork/.agents/skills/ird-handoff/SKILL.md
- Modify: AGENTS.md
- Modify: automation-pipeline.md

- [ ] discover：输出分支、变更范围、适用规则、需求/单元/WP、依赖边、禁止路径和最小上下文，不修改源码。
- [ ] design：输出行为、接口、失败语义、生命周期、测试矩阵、文档影响和是否新增 Note；跨单元或公共接口变化先审阅。
- [ ] implement：一次只处理一个任务卡；代码、测试、CMake 和契约文档同提交；禁止跨任务重构。
- [ ] verify：按变更面选择命令，报告 PASS、FAIL、SKIP、NOT RUN 和证据路径。
- [ ] qt-gui-windows：Visual Studio x64、QT_QPA_PLATFORM=windows、绝对路径、一次一个 GUI 可执行文件；禁止 offscreen。
- [ ] handoff：统一记录目标、完成项、未完成项、SHA、文件、命令、证据、阻塞和下一步。
- [ ] Skills 验收口径＝"新会话按 AGENTS.md 入口能找到并照执行"，不要求运行端自动发现/自动触发（项目级 skills 的自动发现能力因 Agent 运行端而异）。
- [ ] 提交 [docs] 将工业机械臂执行协议拆为 Agent Skills。

### Task 6：生成按任务的最小上下文包

**Files**

- Create: RobWork/scripts/industrialrobot/build-context-pack.ps1
- Create: RobWork/scripts/industrialrobot/validate-context-pack.ps1
- Create: RobWork/doc/industrial-robot-design/context/packs/
- Modify: asset-inventory.json
- Modify: validate-docs.ps1

- [ ] build-context-pack.ps1 接受 TaskId 或 Unit，解析任务 JSON 的 ownerUnit、需求、验证命令和证据路径。
- [ ] 输出 taskId.md，包含任务身份、基线、规则、权威章节、代码根、禁止路径、验证命令和已知阻塞，并记录源文件 SHA-256。
- [ ] validate-context-pack.ps1 在源文件变化后报告 pack 过期；默认不嵌入 traceability 原始日志。
- [ ] 选择一个 core 或 diagnostics 任务试生成，确认不读完整 development-task-breakdown.md 也能定位实现和测试。
- [ ] 运行两个脚本和 validate-docs.ps1；提交 [tooling] 增加按任务生成的工业机械臂上下文包。

### Task 7：把 traceability 改成可检索证据库

**Files**

- Create: RobWork/doc/industrial-robot-design/traceability/README.md
- Create: RobWork/scripts/industrialrobot/index-evidence.ps1
- Create: RobWork/doc/industrial-robot-design/traceability/evidence-index.json
- Modify: validate-docs.ps1
- Modify: .gitignore（只在确认是临时产物后修改）

- [ ] 定义 current、superseded、historical、unverified 生命周期，并记录产生 SHA、命令、环境、目标、结果、任务和当前性。
- [ ] index-evidence.ps1 扫描日志、JUnit XML、JSON 和截图元数据，只生成摘要索引，不把大文件嵌入索引。
- [ ] 先索引现有约 6500 个文件，报告重复、无法关联、缺命令或缺 SHA 的证据；未经索引不得删除或移动。
- [ ] 让 Agent 默认读取 evidence-index.json，按任务或单元检索原始证据。
- [ ] 运行索引和文档门禁；提交 [tooling] 将工业机械臂追踪证据改为可检索索引。

### Task 8：真实新会话接手演练

**Files**

- Create: RobWork/doc/industrial-robot-design/context/handoff-template.md
- Create: RobWork/doc/industrial-robot-design/context/handoff-drill.md
- Modify: 一个试点任务 JSON、对应 unit 文档和 evidence-index.json

- [ ] 选择一个跨文档但风险可控的 core 或 diagnostics 任务；不要选择全量 UI 改版或历史证据清理。
- [ ] 在干净工作树中只提供仓库、TaskId、当前分支和 context pack，不提供本对话全文。
- [ ] 严格执行 discover → design → implement → verify → handoff；代码、测试和文档形成一个可回溯提交。
- [ ] 由另一个新会话检查定位、上下文最小性、禁止路径、验证命令、真实证据和下一步可继续性。
- [ ] 交接失败时修改 AGENTS、Skill、任务 schema、脚本或 pack 生成逻辑，不只追加提示词。
- [ ] 运行 validate-docs.ps1、validate-context-pack.ps1、index-evidence.ps1 和 git diff --check；把实际 SHA、命令、失败和修正写入 handoff-drill.md。

## 执行顺序与停止条件

严格按 Task 1 至 Task 8 执行。Task 1-3 完成前不批量移动或删除旧文档和证据。遇到权威文档冲突、任务无法唯一映射、索引无法追溯 SHA、门禁失败未分类或 Windows Qt 无法按规则启动时，停止后续迁移并先修复。另设两条：

- **CI 扩面前移（评审修正 2）**：CI 门禁已由 WP-01-T02 落位（`scripts/industrialrobot/ci/` 双模板＋`gate-all.ps1`）。Task 6/7 产出的新校验脚本（validate-context-pack、index-evidence、validate-docs 扩展）在各自任务完成时即挂载进既有 workflow 与 gate-all，不等第三阶段统一接入。
- **PIPE 权威关系红线（评审修正 3）**：治理层与 PIPE 的权威关系被打破——asset-inventory 或 context pack 中出现与 state.json 不一致的状态复制品——时，停止迁移并先修复。

## 完成定义

新 Agent 能从分层规则进入正确局部规则；每类任务有最小阅读路径；长期决策有独立 Note；上下文包可生成并检查过期；证据可检索且不污染默认上下文；至少一个真实任务已由新会话独立接手并通过对应验证。

