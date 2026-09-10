# 自动化流水线协议（automation-pipeline）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.3（2026-09-10 审核修复：state schema、tick 调用约定、守卫机器化、合并/续作协议、三份提示词模板；含 v1.2.1 守卫豁免） |
| 文档代号 | PIPE |
| 上游 | acceptance-protocol.md（验收段完全复用其清单与独立性要求）、contract-compilation.md（ready 契约的唯一产出通道）、development-task-breakdown.md §5.7/§8（三段式流程与契约家族）、AGENTS.md §6（提交/推送/循环约定） |
| 状态载体 | `traceability/pipeline/state.json`（唯一事实源；每 tick 读写并更新 `heartbeat.updatedAt`；崩溃后从状态恢复，**不依赖任何会话记忆**；schema 见 §0.2，机器校验 `validate-state.ps1`） |

## 0. 上下文自足性（tick 的全部输入）

任何全新上下文的执行者只需三样东西即可正确执行一个 tick：①本文件；②`state.json`（含 docRefs 指针与完整状态）；③其内嵌引用的契约路径。tick 提示词不携带历史对话；一切长期知识（约定/红线/流程）必须已在文档中——这是 DTB §5.7"持久状态落文档"的延伸。若 tick 发现 state.json 缺字段或与本文件矛盾：以本文件为准修复 state.json 并在汇报中注明。

### 0.1 tick 调用约定（v1.3——此前"定时 tick"无载体，触发方式凭会话即兴）

- **触发方式**：①所有者口令"执行一个 tick"（或"恢复流水线"后的首个 tick）——任何被授权会话收到该口令即按附录 A 模板编排；②所有者显式配置的定时自动化（如 ZCode 定时任务）——其提示词**逐字**使用附录 A 模板，不得改写。除此之外不存在第三种入口；未持口令的会话不得自行编排 tick。
- **并发约束**：同一时刻至多一个 tick 在跑（state.json 占用即标识）；§7 单工作树单轨道。
- **tick 报告**：每 tick 结束输出固定结构汇报——phase 变迁、当前任务、证据路径、下一步等待点。所有者凭报告决策，不读过程。

### 0.2 state.json schema（v1.3——schema 此前无文档、无校验器，"状态损坏"只能靠人眼）

机器校验：`pwsh -File RobWork/scripts/industrialrobot/validate-state.ps1`（守卫 5 的执行面）。字段纪律：

| 字段 | 约束 |
| --- | --- |
| `schemaVersion` | 恒 `ird-pipeline/1` |
| `phase` | 枚举：paused / idle / implementing / awaiting_acceptance / awaiting_merge / awaiting_unit_review / blocked / stopped |
| `currentTask` / `branch` / `base` | implementing/awaiting_acceptance/awaiting_merge ⇒ currentTask 必填（implementing 还须 branch）；idle/paused ⇒ currentTask 必须 null |
| `attempts` | `implement`/`fix` 整数 ≥0，返工/重启计数 |
| `heartbeat.updatedAt` | ISO 时间戳 `YYYY-MM-DDTHH:mm[:ss]`（可带 `±HH:MM` 时区偏移或 `Z`）——纯日期无法排序同日多 tick，也无法判 tick 死亡（v1.3 修复） |
| `heartbeat.ticksNoProgress` | 整数 ≥0，达 `policy.noProgressLimit` 即熔断（守卫 6）；paused 为维护者意图态，不计无进展（v1.2.1 口径） |
| `policy` | 必备键：`autoMerge{enabled,classes}`、`strictQueueOrder`、`autoDiscovery`、`unitCheckpoint`、`unitCheckpointExemptFirstUnit`（首个完成合并的单元豁免紧随其后的单元检查点——所有者已在逐任务合并决策中完成同等审核；默认 true，仅豁免一次）、`noProgressLimit`、`maxImplementRestarts`、`maxFixCycles`。**policy 变更＝PIPE 增量修订先行，再改 state** |
| `queue` | 字符串数组、不重复；每项必须真实存在且非 done（收尾时移出）；队首按严格次序消费 |
| `history` | 追加式数组；每项必备 `task`/`unit`/`result`/`commits`/`mergedAt`，可选 `failReason`（结构化失败原因分类，供单元检查点复盘聚合） |
| `docRefs` | 必备键：pipeline、acceptance、agents、dtbTaskRegistry、taskContracts、contractCompilation |
| `pausedReason` | **≤200 字事实陈述**——状态纪律：叙述性豁免/规则变体必须走 PIPE 增量修订，禁止以自由文本藏在 state.json 里（v1.2 曾发生 pausedReason 豁免单元检查点而未修订 PIPE 的失同步，v1.3 起禁止） |

### 0.3 阅读阶梯（各角色输入预算——防通读也防跳读，v1.2）

| 角色 | 必读 | 按需下沉 | 禁止 |
| --- | --- | --- | --- |
| tick 编排者 | 本文件＋state.json | 守卫所需的 git 状态 | 读任务细节/代码/上游文档 |
| 实施者 | 任务契约全读；AGENTS.md §2/§3＋DTB §5；契约 designRefs **锚定的**单元卡章节 | 语义存疑时回查 REQUIREMENTS/ARCHITECTURE 对应**条目原文**（AGENTS.md §7 回查阀，按需触发）；跨切任务按契约 designRefs 清单读（如 WP-01-T01 的 ARCH §3.2/§3.5/§5.2/§7.2） | 全量通读需求/架构/全卡；凭记忆推断需求语义 |
| 验收者 | 任务契约＋acceptance-protocol＋语义符合度核对所需的卡内章节＋AGENTS.md §2/§5 | 4.8 文档同步核对所需的登记节 | 通读上游全档替代代码/证据核查 |

理由：单元卡是需求＋架构在该单元的消化产物，契约又是卡的编译产物（designRefs 带锚点、acceptance 逐条化）——重读全量等于重做已评审的推导，且挤占留给代码与证据的注意力；两例外（跨切/治理任务）的阅读范围由契约 designRefs 显式列出。**跨单元任务的语义陷阱不靠阅读阶梯覆盖，靠契约 `knownPitfalls` 显式携带**（contract-compilation.md §2"陷阱随契约走"）。

## 1. 角色映射

| 协议角色 | 流水线承载 | 独立性保证 |
| --- | --- | --- |
| 实施者 | 全新子代理，仅输入：任务契约路径、单元卡/DTB 章节引用、分支名 | 不读验收历史；从分支当前提交状态续作 |
| 验收者 | 另一个全新子代理，仅输入：契约路径、分支、base..head、验收记录输出路径 | **只给产物不给叙述**——主会话不得向其转述实施过程；验收在**独立 worktree** 进行（ACC §3），不占用流水线工作树 |
| 契约编译者 | 治理会话按 contract-compilation.md §3 七步执行 | 评审者≠编译者；编译者不得担任同契约实施者 |
| 所有者 | 维护者（人） | 每任务合并决策（默认门控）＋每单元人工审核＋语义裁决＋暂停/恢复＋队列调整 |

## 2. 状态机

| phase | 含义 | 下一态 |
| --- | --- | --- |
| `paused` | 暂停（初始/用户暂停/环境占用） | idle（用户恢复） |
| `idle` | 空闲，按严格次序选下一个任务 | implementing / awaiting_unit_review（单元切换时） |
| `implementing` | 实施子代理工作中（分支已建、分步提交） | awaiting_acceptance / blocked |
| `awaiting_acceptance` | 待验收 tick（验收子代理对抗式核查） | awaiting_merge（pass）/ implementing（fail 返工） |
| `awaiting_merge` | 验收 pass，等所有者合并指令 | idle（合并完成） |
| `awaiting_unit_review` | **单元检查点**：上一单元最后一个任务已合并，等所有者单元级人工审核 | idle（所有者批准继续） |
| `blocked` | 熔断：重试超限/语义分歧/O 项裁决/守卫异常/无进展 | idle（所有者解除）/ paused |

单元检查点豁免（v1.3 收编为机器字段）：`policy.unitCheckpointExemptFirstUnit=true` 时，流水线**首个**完成合并的单元豁免紧随其后的检查点（先例：CORE-T01 手动闭环，合并决策即审核）；豁免仅一次，第二个单元起恢复硬门控。

## 3. 守卫（每 tick 最先执行，任一命中即报告退出，**不得 stash/checkout 干扰**）

1. 当前分支不是 `redesign-main`（且非本流水线在管分支）→ 暂停报告；工作树不干净 → 暂停报告。**"不干净"判定（v1.2.1 豁免＋v1.3 精确化）**：以 `git status --porcelain` 为准，其中**流水线自持路径不计脏**——`traceability/pipeline/**`（状态心跳写回）、`traceability/acceptance/**`（验收记录落盘）、纯构建产物目录（`build*/`、`*-smoke/` 等未跟踪构建树，正常应被 .gitignore 覆盖）；**除上述豁免外的任何条目**（尤其已跟踪源码/文档的修改）即脏。豁免防的是"tick 写心跳被自己的守卫暂停"的自锁，不是给手工改动开口子；
2. phase=paused/stopped → 报告退出；
3. `main` 分支任何形式触碰 → 永久禁止；
4. 任务契约 status 非 ready、或 dependsOn 存在未 done 项 → 该任务不可领（机器前提：`validate-task.ps1` 三查通过——前置引用存在、designRefs 锚点真实、interUnit ⇒ knownPitfalls）；契约未编译（planned）即"不可领"的典型原因，按 contract-compilation.md 编译放行；
5. 流水线自身异常（状态损坏/不可修复矛盾）→ stopped，等所有者（机器判定：`validate-state.ps1` 任一失败）；
6. **无进展检测**：连续 `policy.noProgressLimit`（默认 3）个 tick 状态机无推进（heartbeat.ticksNoProgress 累计；paused 不计）→ blocked 并报告卡点；
7. **环境自检（v1.3）**：git、pwsh（或 powershell 5.1）、构建工具链可用；validate-state/validate-task 可运行（环境口径登记于 DTB §5.1）。

## 4. tick 流程与次序纪律

1. **守卫**（§3，含 `validate-state.ps1`）→ 2. **恢复判定**：按状态机续作（implementing→续派实施子代理——续作者先按 §5 提交对账协议盘点分支已消账的 acceptance 条目再动手，重启上限 `maxImplementRestarts=2`；awaiting_acceptance→派验收子代理）→ 3. **选任务**（仅 idle 时）：
   - **严格次序**：`queue` 按序消费，队首不可领（未 ready/前置未满足/blocked）时**不自动跳取后续任务**——停在 idle 报告"队首 <taskId> 不可领：原因"，等所有者调整队列或解除阻塞；
   - `autoDiscovery` 默认 **false**（自动发现新 ready 契约需所有者在 policy 中显式开启）；
   - **单元检查点**：若本次拟领任务的 `unit` 与上一已完成任务不同，先转 `awaiting_unit_review`（所有者说"单元审核通过，继续"后领新任务；§2 豁免条款除外）；
4. **实施段**：子代理建分支、按契约实现（**防偷懒纪律见 §5**）、双模式构建、测试留痕、文档同步、分步四段式 commit（每个可独立验证的步骤一提交，禁止攒一个大提交）、push origin → 5. **验收段**：验收子代理在**独立 worktree** 按 acceptance-protocol.md（v1.2，含 4.11 偷懒扫描与 findings 消账核对）出 pass/fail＋逐项证据 → 6. **裁决处理**：pass→awaiting_merge（`autoMerge.enabled=false` 时**永不自动合并**，预授权须所有者显式写入 policy）；fail→fix 循环（新实施子代理只拿验收记录＋契约，上限 `maxFixCycles=3`，超限 blocked）→ 7. **收尾**（所有者"合并 <taskId>"指令后，v1.3 固化次序）：①`git merge --no-ff` 合入 `redesign-main`（保留任务分支拓扑）；冲突即停转 blocked 等所有者，不自行取舍语义；②删除任务分支（本地＋origin）；③治理提交翻转契约 status=done；④解锁 dependents、history 追加（含 failReason 若有）、heartbeat 重置、queue 移出该任务；⑤push `origin redesign-main`；⑥state 落盘并出 tick 报告。

## 5. 防偷懒纪律（实施端与验收端双保险）＋中断对账协议

**实施端（写进每个实施子代理的指令，附录 B 模板已内嵌）**：
- 契约 `acceptance` 数组**逐条对应实现**，逐条自证；禁止 `TODO/FIXME/stub/placeholder/未实现/throw "not implemented"`（契约显式标注"触发式延后"的除外，如 WP-02-T11 类，须在提交信息列出豁免依据）；
- 公共接口函数必须有真实逻辑与错误处理，不允许空函数体/恒等返回/只编译不行为；
- 测试必须断言行为与边界（对照附录 D 容差与任务卡反例），禁止恒真断言、"只跑不断言"的用例；每个 acceptance 条目至少对应一个具名测试或一条执行证据；
- 注释按 AGENTS.md §2 全量执行（文件头/Doxygen/高危信息表）——注释缺失本身即验收 fail 项；
- **中断对账协议（v1.3）**：每个实现提交正文末尾标注本提交消账的 acceptance 条目序号（`Acc-Covered: 1,3-4` 格式）——实施中断后，续作者（maxImplementRestarts 内的子代理）先 `git log` 盘点分支已消账条目，对账无误再继续，未消账条目即为剩余工作量；无任何标注的提交在续作时按"未消账"从重盘点。

**验收端**：acceptance-protocol.md 第 4.11 条（偷懒/缩水扫描）＋第 4.4/4.5 条强化（逐条证据表、测试真实失败能力）——无证据即 fail，不采信"已实现"的口头声明。

## 6. 所有者触点与授权分级

- **每任务**：pass 后 `awaiting_merge`，等"合并 <taskId>"（autoMerge 默认关闭且不预授权——开启须显式写入 policy 并注明类别）；
- **每单元**：单元最后任务合并后 `awaiting_unit_review`，等"单元审核通过，继续"——单元级人工审核是硬门控（§2 豁免条款除外）；所有者此时消费该单元 history 聚合（fix 次数、failReason 分类）作复盘输入；
- **裁决**：语义分歧/O 项/需求疑问 → blocked＋登记 DTB §4；
- **控制口令**："执行一个 tick" / "流水线状态" / "恢复流水线" / "暂停流水线" / "调整队列：<序列>" / "单元审核通过，继续" / "合并 <taskId>"。

## 7. 边界与诚实声明

- 验收独立性为**模型级**（全新子代理＋只给产物），弱于跨会话人工分离——以对抗式清单＋复现构建＋偷懒扫描弥补；重要任务（L 类）建议所有者亲自复核验收记录；
- 单工作树＝单轨道：流水线占用工作区期间请勿并行手动实施；验收子代理使用独立 worktree，不占此工作树；
- 连续失败熔断后**不自动换任务**（§4 严格次序），等所有者；
- **发现闭环（v1.3）**：验收产出的一切建议级问题必须逐条转登 `traceability/findings.json`（编号 F-xxx 顺延，状态 open/fixed＋处置建议＋责任方）——"留档"不等于"有人管"；每份验收记录的 4.8 同步核对未关闭 findings 与本任务的相关性；治理资产（脚本/流水线文档）自身的缺陷同样入册（先例：CORE-T01 验收 G-1~G-4 因无跟踪机制搁置，v1.3 建册补登）。

## 8. 附录：三份标准提示词模板（v1.3）

> 模板即纪律的载体：派发子代理时**逐字使用并仅替换 `<>` 占位符**，不增删条款；模板修订＝PIPE 增量修订（版本行同步）。此前三份模板无权威文本，每次编排即兴重写造成漂移——v1.3 起封存于此。

### 附录 A · 编排者模板（T-ORCH v1）

```text
你是本仓库自动化流水线的 tick 编排者。输入仅限：automation-pipeline.md、
traceability/pipeline/state.json（路径见 state.docRefs）。禁止：读任务契约正文、读产品代码、
读上游需求/架构文档——编排者只看状态与守卫结果。

按以下次序执行一个 tick：
1. 守卫（PIPE §3）：git status/branch 检查（"脏"判定按 §3.1 豁免口径）＋运行
   validate-state.ps1；任一命中即按 §3 处置退出，不得 stash/checkout 干扰。
2. 恢复判定（PIPE §4.2）：implementing→续派实施者（附录 B 模板）；awaiting_acceptance→
   派验收者（附录 C 模板）；其余 phase 按状态机等待点停止并报告。
3. 选任务（仅 idle）：严格次序取 queue 队首；校验 ready＋前置 done；单元切换时按 §2
   判定 awaiting_unit_review（含 unitCheckpointExemptFirstUnit 豁免）。
4. 派发实施子代理：逐字使用附录 B 模板，仅替换占位符；不追加不删减。
5. 实施完成（或中断重启）后派发验收子代理：逐字使用附录 C 模板；只传产物指针
   （契约/分支/base..head/证据路径），不转述任何实施叙述。
6. 裁决处理与收尾（PIPE §4.6/4.7）：合并仅凭所有者"合并 <taskId>"指令，按 §4.7 固化次序执行。
7. 每次状态写回同步 heartbeat.updatedAt=当前 ISO 时间戳；tick 结束输出固定报告：
   phase 变迁 / 当前任务 / 证据路径 / 下一步等待点。
```

### 附录 B · 实施者模板（T-IMPL v1）

```text
你是任务 <taskId> 的实施者，负责实施段全程。输入：契约 <tasks/.../<taskId>.json>、
分支 <wp*-t*>、base <redesign-main 当前提交>。不读验收历史。

阅读阶梯（PIPE §0.3）：
- 契约全读；AGENTS.md §2/§3＋DTB §5；契约 designRefs 锚定的单元卡章节。
- interUnit=true 时逐条核对 knownPitfalls 的处置约束，命中未决项→停止。
- 语义存疑才回查 REQUIREMENTS/ARCHITECTURE 条目原文；禁止全量通读与凭记忆推断。

实现纪律（PIPE §5，违反任一即验收 fail）：
- acceptance 逐条实现、逐条自证；禁止 TODO/FIXME/stub/空实现/恒等返回
  （契约显式豁免除外，须在提交信息列出依据）。
- 测试断言行为与边界；每条 acceptance 至少对应一个具名测试或一条执行证据。
- 注释按 AGENTS.md §2 全量执行；注释缺失＝验收 fail 项。
- 中断对账：每个提交正文末尾标注 Acc-Covered: <本提交消账的 acceptance 条目序号>。

工程流程（AGENTS.md §6.3/§6.4）：
建分支→实现→双模式构建（AGENTS.md §4.1；冒烟模式必须带 vcpkg toolchain 参数）→
自查 validate-task.ps1（ird_gates 未建成前如实标注未执行）→测试执行留痕
（gtest XML＋ird-test-report.json）→文档同步→分步四段式提交→push origin。
停止条件：需求/架构语义拿不准、knownPitfalls 未决项、契约缺陷→停止并报告 blocked
请求，不得在代码中自行裁决，不得私改契约。
完成输出：验收请求（ACC §1 固定格式：任务 ID/分支/base..head/证据路径）。
```

### 附录 C · 验收者模板（T-ACC v1）

```text
你是任务 <taskId> 的验收者：全新上下文，只消费产物，不消费实施叙述（ACC §2 四条
独立性要求逐条适用；为"不通过"找证据）。
输入：契约 <路径>、分支 <wp*-t*>、commit 范围 <base..head>、验收记录输出路径
<traceability/acceptance/<taskId>-<YYYYMMDD>[-rN].md>（同日多次验收加 -r2 序号）。

执行环境（ACC §3）：在独立 worktree 建立验收现场（不占用流水线工作树）；
集成模式重新配置（确认 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON）后构建；冒烟模式另配
临时目录；契约 verify 命令逐条亲手执行。

检查清单：acceptance-protocol.md §4 的 4.1～4.11 逐项出证据（通过给命令输出/文件行号，
失败给反证）；4.8 同时核对 traceability/findings.json 未关闭条目与本任务的相关性。
裁决与产出（ACC §5）：verdict＋逐项证据写入指定路径；建议级问题逐条转登
traceability/findings.json（F-xxx 编号顺延）；你不得合入，不得修改实施产物。
```

## 9. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 DTB v0.8 建立：六态状态机、五守卫、tick 流程、返工熔断、所有者授权分级 |
| v1.1 | 2026-09-10 | 维护者要求加固：①§0 上下文自足性（tick 三输入、以本文件为准修复状态）；②§4 严格次序（队首不可领不跳队、autoDiscovery 默认关闭）；③§3.6 无进展检测（3 tick 无推进即 blocked）；④§5 防偷懒纪律双保险＋验收协议 v1.1 第 4.11 条；⑤§2/§6 单元检查点 awaiting_unit_review（单元间人工审核硬门控） |
| v1.2 | 2026-09-10 | §0 增补阅读阶梯：三角色（编排/实施/验收）各自的必读—按需下沉—禁止三档输入预算；实施者按契约 designRefs 锚点定向读卡、语义存疑才回查需求条目原文（§7 回查阀），禁止全量通读——防通读挤占代码注意力，也防跳读漏契约范围 |
| v1.2.1 | 2026-09-10 | §3.1 守卫豁免：流水线自持路径（pipeline 状态心跳、acceptance 记录）与纯构建产物目录不视为"工作树不干净"，防心跳写回触发自锁 |
| v1.3 | 2026-09-10 | 审核修复（修复对象＝流水线审核报告 P0/P1/P2 项，在 v1.2.1 基础上叠加）：①§0.1 tick 调用约定（触发口令/定时模板逐字使用/并发约束/tick 报告——消除"定时 tick 无载体"）；②§0.2 state.json schema 文档化＋validate-state.ps1 机器校验＋heartbeat ISO 时间戳＋pausedReason ≤200 字状态纪律（禁止叙述性豁免藏 state——v1.2 实例整改，豁免收编为 policy.unitCheckpointExemptFirstUnit 机器字段）；③§3 守卫精确化（"脏"判定结构化、守卫机器执行面、环境自检）；④§4.7 合并机制固化（--no-ff/冲突即停/分支删除/状态翻转次序）与验收独立 worktree；⑤§5 中断对账协议（Acc-Covered 提交标注）；⑥§7 发现闭环（findings.json 强制转登＋验收 4.8 消账核对）；⑦§8 三份标准提示词模板封存（T-ORCH/T-IMPL/T-ACC v1）；⑧上游增补 contract-compilation.md（ready 契约唯一产出通道，阅读阶梯的编译前提） |
