# 自动化流水线协议（automation-pipeline）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.11（2026-09-11 所有者预授权平台层整链批次序列（B 模式）：autoDiscovery 开启依据登记——每批 DOC-Txx 编译放行后新 ready 契约由 tick 自动追加队尾，消除逐批手工"调整队列"；T-ORCH 升 v9） |
| 文档代号 | PIPE |
| 上游 | acceptance-protocol.md（验收段完全复用其清单与独立性要求）、contract-compilation.md（ready 契约的唯一产出通道）、development-task-breakdown.md §5.7/§8（三段式流程与契约家族）、AGENTS.md §6（提交/推送/循环约定） |
| 状态载体 | `traceability/pipeline/state.json`（唯一事实源；schema 见 §0.2，机器校验 `validate-state.ps1`） |
| 并发载体 | `.git/ird-pipeline-tick.lock`（原子锁，`pipeline-lock.ps1` 管理——§0.1） |

## 0. 上下文自足性（tick 的全部输入）

任何全新上下文的执行者只需三样东西即可正确执行一个 tick：①本文件；②`state.json`（含 docRefs 指针与完整状态）；③其内嵌引用的契约路径。tick 提示词不携带历史对话；一切长期知识（约定/红线/流程）必须已在文档中——这是 DTB §5.7"持久状态落文档"的延伸。若 tick 发现 state.json 缺字段或与本文件矛盾：以本文件为准修复 state.json 并在汇报中注明。

### 0.1 tick 调用约定与原子锁（v1.6：命名 mutex＋fencing token 消除锁操作 TOCTOU）

- **原子锁**：每 tick **第一步**执行 `pwsh -File RobWork/scripts/industrialrobot/pipeline-lock.ps1 -Action acquire -LeaseMinutes <policy.leaseMinutes.implement>`。脚本先取得由**绝对仓库路径 SHA-256**派生的 Windows 命名 mutex；`status`/`acquire`/`renew`/`release` 的完整“读锁→判定→修改”均在同一临界区内，随后才以 .NET FileStream CreateNew（O_EXCL）独占创建 `.git/ird-pipeline-tick.lock`。因此旧 tick 即使在 fencing 核对后被调度打断，也不可能在新持有者接管后删除或覆盖新锁。锁内容含 `tickId`/`workerId`（GUID）＋`acquiredAt`/`leaseExpiresAt`。**acquire 失败（退出码 3）＝有未到期锁在持 → 本 tick 只输出锁信息后退出，不读不写 state**；锁已过期或损坏 → 脚本将陈锁改名 `.stale-<时间戳>` 留证后接管并继续。
- **fencing token（v1.6）**：acquire 返回的 `tickId` 即 fencing token（每次成功获取——含接管——都生成新 token）。`release` 与 `renew` **必须携带 `-Token <tickId>`**，脚本在上述 mutex 临界区内与锁内 tickId 核对：不匹配（旧 tick 租约过期被接管后复活，试图释放/续租新 tick 的锁）一律拒绝（退出码 4），绝不删除他人锁。**失去租约的旧 tick 不得再写状态、不得触碰锁**——发现自身 token 失配（release 被拒/续租被拒）即自终止并报告。
- **状态写入纪律（诚实边界）**：state.json 的文件写入无法做到硬件级 fencing——纪律为"仅持锁 tick 可写，且每次写入记录 `tickToken`＝自身锁 tickId"；validate-state 的事后检测面＝tickToken 存在性（in-flight 阶段非空）＋心跳时钟健全性（不得晚于当前时刻 5 分钟以上）＋心跳单调性（不得早于活跃 run 的 startedAt）。历史上确有无锁心跳写入与未来时间戳写入两类事故（2026-09-10 实录），均会被上述检测捕获。
- **长等待续租**：派发子代理阻塞等待前执行 `renew -Token <tickId> -LeaseMinutes <对应时长>`；**阻塞等待返回后再次 renew**，确认本 tick 在处理完成事件、写 state 或释放锁前仍是持有者。任一次 renew 返回退出码 4 即自终止，防健康长任务被租约误杀也防过期 tick 落盘。
- **触发方式**：①所有者口令"执行一个 tick"（或"恢复流水线"后的首个 tick）；②所有者显式配置的定时自动化——提示词**逐字**使用附录 A 模板。除此之外不存在第三种入口。
- **并发约束**：同一时刻至多一个持锁 tick；锁是唯一并发裁判。§7 单工作树单轨道不变。
- **tick 报告**：每 tick 结束输出固定结构汇报——phase 变迁、当前任务、证据路径、下一步等待点。
- **tickId/tickCount**：锁内 `tickId` 为本次 tick 的 GUID（＝fencing token＝state.tickToken）；state 的 `tickCount` 每 tick 递增一次。

### 0.2 state.json schema（机器校验：validate-state.ps1；守卫 5 的执行面）

字段纪律：

| 字段 | 约束 |
| --- | --- |
| `schemaVersion` | 恒 `ird-pipeline/1` |
| `phase` | 枚举：paused / idle / implementing / awaiting_acceptance / awaiting_merge / awaiting_unit_review / blocked / stopped |
| `tickCount` | 整数 ≥0，每 tick 递增（审计计数） |
| `tickToken` | 写入本状态的 tick 之锁 tickId（fencing 侧写，v1.5）：in-flight 阶段（implementing/awaiting_acceptance/awaiting_merge）必须非空；paused/idle 保留上次写入者作审计。心跳另有时钟健全性（≤now+5min）与单调性（≥活跃 run.startedAt）校验——无锁写入/未来时间戳/回退写入均被事后检出 |
| `currentTask` / `branch` / `base` | implementing/awaiting_acceptance/awaiting_merge ⇒ currentTask 必填；`base`＝领取任务时冻结的 `redesign-main` 基线 **40 位 SHA**；idle/paused ⇒ currentTask 必须 null |
| `headSha` | 实施完成时冻结的任务分支尖端 40 位 SHA（进入 awaiting_acceptance 前写入）；fail 返工/idle/paused/blocked 时必须清 null（陈旧 SHA 不得残留） |
| `acceptedHead` | 验收 pass 时＝headSha 的冻结拷贝；合并只能合并**该 SHA**，不合并分支引用（防验收后分支新增提交混入） |
| `acceptanceRecord` | 结构化对象 `{branch, path, commit}`：验收记录所在 evidence 分支 `acc/<taskId>/<attempt>`（attempt＝该任务第几次验收，v1.5 起 fail 重试不复用分支名）＋记录路径＋**不可变 evidence 提交 SHA（40 位）**——收尾按该 SHA 合并并先验证远端 ref 未漂移（ACC §3 回传流程的落点）；仅 awaiting_merge 必填 |
| `lastFailureRecord` | 结构化对象 `{branch, path, commit, attempt}`：上一轮**失败**验收记录的 evidence 分支、路径、不可变记录提交 SHA 与尝试序号。仅 `implementing && attempts.fix>0`（返工）必填；attempt 必为正 JSON 整数且**等于 `attempts.accept`**（只允许最近一轮验收），branch 必为当前任务的 `acc/<taskId>/<attempt>`；首次实施及其他 phase 必为 null。它不是可合入的 `acceptanceRecord`，只作为返工实施者的失败证据输入 |
| `run` | 工作者租约对象：`{kind: implement\|acceptance, runId, workerId, task, startedAt, leaseExpiresAt}`（ISO 时间戳）。implementing ⇒ 必有 run（kind=implement）；awaiting_acceptance ⇒ run 为 null（待派验收）或 kind=acceptance（在验）；其余 phase ⇒ null |
| `attempts` | `implement`/`fix`/`accept` 整数 ≥0；implement 完成进入验收时清零；accept＝验收派发计数（evidence 分支序号来源） |
| `heartbeat.updatedAt` | ISO 时间戳（**规范写入格式 `yyyy-MM-ddTHH:mm:sszzz` 秒精度＋本地偏移**；校验器对 pwsh7 自动 DateTime 化做归一化后仍须匹配 ISO 正则）——纯日期无法排序同日 tick |
| `heartbeat.ticksNoProgress` | 整数 ≥0，达 `policy.noProgressLimit` 即熔断（守卫 6）；paused 不计 |
| `policy` | 必备键见 §6.1 执行语义表；**policy 变更＝PIPE 增量修订先行，再改 state** |
| `queue` | **结构化条目**数组（v1.4）：每项 `{taskId, contractPath, branch}`——三键必填、taskId 不重复、任务非 done；**v1.5 真读校验**：contractPath 指向的契约本体 taskId/branch 必须与条目一致（错路径/错分支不可领取），且队内每份契约过 validate-task 三查（编排者不读契约正文，消费 validate-state 的机器结论）。队首可领取性由 validate-state 输出 |
| `history` | 追加式数组；每项必备 `task`/`unit`/`result`/`commits`/`mergedAt`，可选 `failReason` |
| `docRefs` | 必备键：pipeline、acceptance、contractCompilation、agents、dtbTaskRegistry、taskContracts |
| `pausedReason` | **≤200 字事实陈述**——叙述性豁免/规则变体必须走 PIPE 增量修订（v1.3 状态纪律） |

### 0.3 阅读阶梯（各角色输入预算——防通读也防跳读，v1.2）

| 角色 | 必读 | 按需下沉 | 禁止 |
| --- | --- | --- | --- |
| tick 编排者 | 本文件＋state.json（queue 结构化条目自带 contractPath/branch 指针；可领取性消费 validate-state 的 `queue-head:` 输出行，不读契约正文） | 守卫所需的 git 状态 | 读任务契约正文/代码/上游文档 |
| 实施者 | 任务契约全读；AGENTS.md §2/§3＋DTB §5；契约 designRefs **锚定的**单元卡章节 | 语义存疑时回查 REQUIREMENTS/ARCHITECTURE 对应**条目原文**（AGENTS.md §7 回查阀，按需触发）；跨切任务按契约 designRefs 清单读 | 全量通读需求/架构/全卡；凭记忆推断需求语义 |
| 验收者 | 任务契约＋acceptance-protocol＋语义符合度核对所需的卡内章节＋AGENTS.md §2/§5 | 4.8 文档同步核对所需的登记节 | 通读上游全档替代代码/证据核查 |

理由：单元卡是需求＋架构在该单元的消化产物，契约又是卡的编译产物（designRefs 带锚点、acceptance 逐条化）——重读全量等于重做已评审的推导，且挤占留给代码与证据的注意力；两例外（跨切/治理任务）的阅读范围由契约 designRefs 显式列出。**跨单元任务的语义陷阱不靠阅读阶梯覆盖，靠契约 `knownPitfalls` 显式携带**（CCP §2"陷阱随契约走"）。

### 0.4 派发执行模式（v1.9 恢复；v1.7 单会话模式废止）

2026-09-10 所有者解除"禁止使用子智能体"指令（v1.7 §0.4 预留的解除路径）：实施段与验收段**恢复派发全新子代理**——tick 会话为编排者，按附录 B/C 模板逐字派发实施者/验收者子代理并**阻塞等待**；角色脚本、纪律与检查清单不变。恢复前置与纪律：

- **能力实证先行**：恢复前以无害子代理调用实证派发可用性（2026-09-10 实测：子代理派发/执行/回报正常）；
- **独立性回到模型级**：全新子代理＋只给产物（§1 角色映射），§7 独立性声明恢复 v1.6 口径；验收记录不再需要 §0.4 降级声明；
- **续租纪律不变**：派发前 renew 续租、阻塞等待返回后再次 renew 成功才处理完成事件（v1.6 原语义——fencing 失配即自终止）；
- **编排者不亲自实施/验收**：tick 会话只读状态、锁与守卫结果＋派发与完成事件处理，不直接执行附录 B/C 的角色脚本（v1.7 的教训固化：承载者混同即独立性丢失）；
- 再次切换执行模型（任何方向）均须所有者指令＋PIPE 增量修订，禁止静默切回。

## 1. 角色映射

| 协议角色 | 流水线承载 | 独立性保证 |
| --- | --- | --- |
| 实施者 | 全新子代理（§0.4 派发模式），仅输入：任务契约路径、分支名、base SHA；返工时另含 `lastFailureRecord` | 首次实施不读验收历史；返工仅读取该不可变失败记录，中断续作先按 §5 对账协议盘点 |
| 验收者 | 另一个全新子代理，仅输入：契约路径、headSha（冻结送验对象）、验收记录输出路径 | **只给产物不给叙述**——主会话不得向其转述实施过程；验收在**独立 worktree**（detached @ headSha）进行（ACC §3） |
| 契约编译者 | 治理会话按 contract-compilation.md §3 七步执行 | 评审者≠编译者；编译者不得担任同契约实施者 |
| 所有者 | 维护者（人） | 每任务合并决策（默认门控）＋每单元人工审核＋语义裁决＋暂停/恢复＋队列调整 |

## 2. 状态机与工作者生命周期

| phase | 含义 | 下一态 |
| --- | --- | --- |
| `paused` | 暂停（初始/用户暂停/环境占用） | idle（用户恢复） |
| `idle` | 空闲，按严格次序选下一个任务 | implementing / awaiting_unit_review（单元切换时） |
| `implementing` | 实施工作者派发中（run.kind=implement，租约在计） | awaiting_acceptance（完成）/ blocked |
| `awaiting_acceptance` | run=null：待派验收；run.kind=acceptance：验收进行中 | awaiting_merge（pass）/ implementing（fail 返工） |
| `awaiting_merge` | 验收 pass（acceptedHead 已冻结）。**v1.8 全自动化**：autoMerge 全词表开启后正常路径不再驻留此态（pass 的同 tick 直接执行 §4.7 收尾）；此态仅作为"pass 落盘与收尾之间中断"的自愈锚点——后续 tick 恢复判定见 §4.2 | idle（收尾完成）/ blocked（漂移/冲突） |
| `awaiting_unit_review` | **单元检查点**（v1.8：unitCheckpoint=false 后不可达，保留枚举仅为状态机兼容与历史状态可读） | idle（v1.8 起无此转换） |
| `blocked` | 熔断：重试超限/语义分歧/O 项裁决/守卫异常/无进展/分支漂移 | idle（所有者解除）/ paused |

**工作者完成事件（v1.6；v1.9 恢复子代理承载）**：tick 对实施/验收子代理**阻塞等待**；子代理返回后必须再次 `renew -Token <tickId>`，成功才是可处理的完成事件——由派发 tick 负责状态迁移（实施完成→冻结 headSha/清 run/进 awaiting_acceptance；验收完成→写 acceptedHead/acceptanceRecord/清 run）。续租 token 失配表示租约已被接管，旧 tick 必须终止，不能写入任何完成状态。派发会话崩溃时：run 租约未到期 → 后续 tick **只汇报不续派**；租约到期 → 视工作者已死，按重启上限处置。attempts.implement 计重启次数（实施完成即清零），attempts.fix 计验收 fail 后的返工轮次。

单元检查点豁免：`policy.unitCheckpointExemptFirstUnit=true` 时，流水线**首个**完成合并的单元豁免紧随其后的检查点，豁免消费时置 `policy.firstUnitCheckpointExempted=true`（一次性标记，validate-state 强制存在；CORE-T01 手动闭环即首单元审核，标记初始为 true）。

## 3. 守卫（每 tick 最先执行，任一命中即报告退出，**不得 stash/checkout 干扰**）

1. 当前分支不是 `redesign-main`（且非本流水线在管分支）→ 暂停报告；工作树不干净 → 暂停报告。**"不干净"判定（v1.2.1 豁免＋v1.3 精确化）**：以 `git status --porcelain` 为准，其中**流水线自持路径不计脏**——`traceability/pipeline/**`、`traceability/acceptance/**`、纯构建产物目录（`build*/`、`*-smoke/`，应被 .gitignore 覆盖）；**除上述豁免外的任何条目**即脏；
2. phase=paused/stopped → 报告退出；
3. `main` 分支任何形式触碰 → 永久禁止；
4. 队首不可领 → 停在 idle 报告原因（机器结论＝validate-state 的 `queue-head:` 行：status=ready 且 dependsOn 全 done 且契约过三查——**编排者不读契约正文**）；契约未编译（planned）即典型不可领原因，按 CCP 编译放行；
5. 流水线自身异常（状态损坏/不可修复矛盾）→ stopped，等所有者（机器判定：`validate-state.ps1` 任一失败）；
6. **无进展检测**：连续 `policy.noProgressLimit`（默认 3）个 tick 状态机无推进（heartbeat.ticksNoProgress 累计；paused 不计）→ blocked 并报告卡点；
7. **环境自检（v1.3）**：git、pwsh（或 powershell 5.1）、构建工具链可用；validate-state/validate-task/pipeline-lock 可运行（环境口径登记于 DTB §5.1）。

## 4. tick 流程与次序纪律（v1.4 重写）

1. **取锁**（§0.1：pipeline-lock acquire；失败即退出）→ tickCount+1 → **守卫**（§3，含 `validate-state.ps1`）。
2. **恢复判定**（按状态机＋run 租约）：
   - implementing：run 未到期 → **只汇报退出**（工作者运行中，禁止续派）；到期 → attempts.implement+1，超 `maxImplementRestarts` 转 blocked，否则重派实施者（新 run，租约重置）；
   - awaiting_acceptance：run=null → 派验收者（置 run.kind=acceptance）；run 未到期 → 只汇报退出；到期 → 重派（计数并入 fix 口径由所有者裁决，默认不自动重派验收、报告等待）；
   - awaiting_merge：**v1.8 自动化**——直接执行 §4.7 七步收尾（双漂移检测仍前置，任何一步失败转 blocked）；awaiting_unit_review：v1.8 不可达态，遗留此态时置 idle 继续选任务；blocked → 汇报等待点（熔断/漂移/语义分歧是真失败，全自动化不掩盖失败，仍停等所有者）。
3. **选任务**（仅 idle）：读 queue 队首**结构化条目**（taskId/contractPath/branch 即所需全部）；可领取性以 validate-state `queue-head:` 行为准；领取时**冻结 base＝当前 redesign-main HEAD（40 位 SHA）**、置 currentTask/branch、attempts 清零、phase=implementing＋run（kind=implement，租约 policy.leaseMinutes.implement）。
   - **严格次序**：队首不可领时不跳队——报告等所有者；`autoDiscovery`/`strictQueueOrder`/`unitCheckpoint` 的分支语义见 §6.1；
   - **单元检查点**：拟领任务 unit 与上一已完成任务不同 → awaiting_unit_review（§2 豁免条款除外）。
4. **实施段**：派发实施子代理（附录 B 模板；输入含 base SHA）并**阻塞等待**；完成事件处理：`headSha = git rev-parse <branch>`（40 位冻结）、清 run、attempts.implement 清零、phase=awaiting_acceptance。实施子代理内部流程不变（防偷懒纪律 §5、双模式构建、留痕、分步提交、push origin）。
5. **验收段**：派发验收子代理（附录 C 模板；输入＝契约路径＋headSha＋**attempt 号（＝attempts.accept 递增后的值）**＋记录输出路径）并阻塞等待（先 `renew` 续租，返回后再次 `renew` 成功才处理完成事件）；验收者在独立 worktree（**detached @ headSha**，不用分支名——任务分支可能仍被实施工作树检出）复现核查，验收记录提交到 **evidence 分支 `acc/<taskId>/<attempt>`**（基于 origin/redesign-main；attempt 隔离保证 fail 重试永不复用已存在的分支名）并**推送**（ACC §3——记录不随临时 worktree 消亡）。完成事件：pass → `acceptedHead=headSha`、`acceptanceRecord={branch:acc/<taskId>/<attempt>, path, commit}`（commit＝evidence 分支上的记录提交 **40 位 SHA，不可变合并对象**）、`lastFailureRecord=null`、清 run、phase=awaiting_merge；fail → 先将验收者输出三元组与 attempt 写为 `lastFailureRecord={branch,path,commit,attempt}`，再 attempts.fix+1（超 `maxFixCycles` 转 blocked 且清该字段），未超限则 phase=implementing＋新 run（返工实施者输入＝契约＋lastFailureRecord），headSha/acceptedHead/acceptanceRecord 清 null。
6. **裁决处理**：pass→§4.7 收尾（**v1.8 全自动化**：autoMerge.enabled=true 且类别 ∈ classes〔全词表 doc/build/implementation〕即同 tick 执行，不再驻留 awaiting_merge——所有者预授权登记于 policy；收尾七步的漂移检测与冲突即停转 blocked 的保护不变）；fail→fix 循环。
7. **收尾**（v1.8 起随 pass 自动执行；原"仅凭所有者'合并 <taskId>'指令"门控按所有者 2026-09-10 全自动化指令移除，七步固化次序不变）：
   ① `git fetch origin`，验证 `git rev-parse origin/<branch>` **== acceptedHead**——不符即转 blocked 报告"验收后分支漂移"（有人绕过流水线推送），不自行取舍；
   ② 合入验收记录：验证 `git rev-parse origin/<evidence分支>` **== acceptanceRecord.commit**（evidence 分支漂移同样转 blocked），随后 `git merge --no-ff <acceptanceRecord.commit>`——**按不可变 SHA 合并，不合并分支尖端**（evidence 分支被后续追加提交时不会带入未审查内容）；
   ③ **按 SHA 合入代码**：`git merge --no-ff <acceptedHead>`（不是分支引用——即使分支被移动也只合并已验收提交）；冲突即停转 blocked；
   ④ `git push origin redesign-main`；
   ⑤ 治理提交：契约 status=done、解锁 dependents、history 追加（含 failReason 若有）、queue 移出该任务、heartbeat 重置、清 currentTask/branch/base/headSha/acceptedHead/acceptanceRecord/lastFailureRecord、phase=idle、再 push；
   ⑥ 删除任务分支与 evidence 分支（本地＋origin）、state 落盘、出 tick 报告；
   ⑦ **释放锁**（`release -Token <tickId>`——token 失配即自终止报告，不得触碰他人锁）。

## 5. 防偷懒纪律（实施端与验收端双保险）＋中断对账协议

**实施端（写进每个实施子代理的指令，附录 B 模板已内嵌）**：
- 契约 `acceptance` 数组**逐条对应实现**，逐条自证；禁止 `TODO/FIXME/stub/placeholder/未实现/throw "not implemented"`（契约显式标注"触发式延后"的除外，须在提交信息列出豁免依据）；
- 公共接口函数必须有真实逻辑与错误处理，不允许空函数体/恒等返回/只编译不行为；
- 测试必须断言行为与边界（对照附录 D 容差与任务卡反例），禁止恒真断言；每个 acceptance 条目至少对应一个具名测试或一条执行证据；
- 注释按 AGENTS.md §2 全量执行——注释缺失本身即验收 fail 项；
- **中断对账协议（v1.3）**：每个实现提交正文末尾标注本提交消账的 acceptance 条目序号（`Acc-Covered: 1,3-4` 格式）——实施中断后，续作者（maxImplementRestarts 内的子代理）先 `git log` 盘点分支已消账条目，对账无误再继续；无任何标注的提交按"未消账"从重盘点。

**验收端**：acceptance-protocol.md 第 4.11 条（偷懒/缩水扫描）＋第 4.4/4.5 条强化（逐条证据表、测试真实失败能力）——无证据即 fail，不采信"已实现"的口头声明。

## 6. 所有者触点与授权分级（v1.8 全自动化修订）

- **例行流程无人值守**（2026-09-10 所有者指令）：领取→实施→验收→合入收尾→下一任务全程自动；autoMerge 预授权登记于 state.policy（enabled=true＋全词表），单元检查点关闭（unitCheckpoint=false）。
- **仍需所有者的情形（诚实边界，不因自动化移除）**：blocked（熔断/重试超限/分支漂移/守卫异常）解除；语义分歧/O 项/需求疑问裁决（登记 DTB §4 后停在该任务）；"暂停流水线"/"恢复流水线"/"调整队列"等监督口令随时可用。
- **监督建议**：所有者抽查验收记录（traceability/acceptance/）与 findings.json 开放项；L 类重要任务建议人工复核验收记录（§7 独立性降级背景下的补偿措施）。

### 6.1 policy 字段执行语义（v1.4——每个字段必须注明消费点，防"看似可配置、实际无效"）

| 字段 | 消费点 | 语义 |
| --- | --- | --- |
| `autoMerge.enabled` / `classes` | §4.6 pass 分支 | enabled=false：pass 一律 awaiting_merge，classes 忽略。enabled=true：仅当任务类别 ∈ classes 才允许 tick 直接合并。**v1.8 生效值**：enabled=true＋classes=全词表（所有者 2026-09-10 全自动化指令，预授权登记于 state——机制不变，仅取值切换）。**类别判定**（由契约 `outputs` 归一）：仅 doc-update/traceability-update ⇒ `doc`；含构建落位（T01 类）⇒ `build`；含 implementation/unit-tests ⇒ `implementation`。词表封闭 {doc, build, implementation}，validate-state 拒绝词表外取值 |
| `autoDiscovery` | §4.3 之后 | true：每 tick 扫描 tasks/ 发现"status=ready 且不在 queue"的契约，**追加队尾**并在报告中列出（不插队）；false：跳过扫描。**2026-09-11 起所有者裁决开启（B 模式预授权＋autoDiscovery，PIPE v1.11）**：编译批次（DOC-Txx）放行的新 ready 契约由 tick 自动追加队尾，整链批次供给不再逐批手工"调整队列"；批次间依赖顺序由编译批次本身的队列序保证（附录 A 步骤 4） |
| `strictQueueOrder` | §4.3 | true（默认）：队首不可领即停等所有者。false：允许依序向后取**首个**可领条目（跳过项保留在队列原位并在报告标注）——仅在所有者显式接受乱序时开启 |
| `unitCheckpoint` | §4.3 | true：单元切换先 awaiting_unit_review。false：跳过该门控。**v1.8 生效值**：false（所有者 2026-09-10 全自动化指令）——单元级复盘不删除，改由所有者事后消费 history 聚合（fix 次数、failReason 分类）作监督输入 |
| `unitCheckpointExemptFirstUnit` / `firstUnitCheckpointExempted` | §4.3 | 前者为配置（是否豁免首单元检查点）、后者为**一次性消费标记**（豁免被使用后置 true，此后任何单元切换均走完整检查点）；两者均为 validate-state 必备 bool |
| `noProgressLimit` / `maxImplementRestarts` / `maxFixCycles` | §3.6 / §4.2 / §4.6 | 熔断阈值，语义见对应节 |
| `leaseMinutes.implement` / `leaseMinutes.acceptance` | §0.1 / §4.2 / §4.5 | 工作者租约时长（分钟）：implement 默认 120（实施耗时长，取宽估值防误杀健康运行）；acceptance 默认 60。租约到期是"允许重派"的必要条件而非充分条件（还需 phase 处于对应状态） |

## 7. 边界与诚实声明

- 验收独立性为**模型级**（全新子代理＋只给产物，v1.9 恢复），弱于跨会话人工分离——以对抗式清单＋复现构建＋偷懒扫描弥补；重要任务（L 类）建议所有者亲自复核验收记录；
- 单工作树＝单轨道：流水线占用工作区期间请勿并行手动实施；验收子代理使用独立 worktree（detached @ headSha）＋记录分支，不占此工作树；
- 连续失败熔断后**不自动换任务**（§4 严格次序），blocked 停等所有者——**v1.8 全自动化不改变此条**：自动化的是例行流程，不是失败处置；blocked、语义裁决、漂移检测即停等保护全部保留；
- **全自动化下的风险声明（v1.8 立项，v1.9 更新）**：自动合入仍在生效——验收与合并不再有所有者门控。v1.9 恢复子代理派发后，实施/验收的独立性回到模型级（原有最大弱点已消除），残余风险为"模型级弱于人工跨会话"（见上条）。所有者应以抽查验收记录、核对 findings.json 开放项、关注 blocked 报告作为监督补偿；任何"suspicious green"（如连续高频 pass）值得人工复盘；
- **发现闭环（v1.3）**：验收建议级问题必须逐条转登 `traceability/findings.json`（F-xxx 顺延）；验收记录 4.8 核对未关闭条目；治理资产自身缺陷同样入册；
- **锁的边界（v1.6 诚实声明）**：命名 mutex＋原子锁防的是“两个 tick 并发派工”以及 release/renew 的核对后修改窗口；它防不了持锁会话自身的死亡（靠租约到期自愈）与所有者绕过流水线直接操作 git（靠 §4.7① 分支漂移检测兜底）。

## 8. 附录：三份标准提示词模板（v1.7 修订）

> 模板即纪律的载体：派发对应子代理时**逐字使用并仅替换 `<>` 占位符**，不增删条款（v1.9 §0.4：实施者/验收者由 tick 派发全新子代理承载）；模板修订＝PIPE 增量修订（版本行同步）。

### 附录 A · 编排者模板（T-ORCH v9）

```text
你是本仓库自动化流水线的 tick 编排者。输入仅限：automation-pipeline.md、
traceability/pipeline/state.json（docRefs 给出全部指针）。禁止：读任务契约正文、
读产品代码、读上游需求/架构文档——编排者只看状态、锁与守卫结果。

按以下次序执行一个 tick（PIPE §4）：
1. 取锁：pwsh -File RobWork/scripts/industrialrobot/pipeline-lock.ps1 -Action acquire
   -LeaseMinutes <state.policy.leaseMinutes.implement>；退出码非 0＝锁被持有或竞争失败
   → 输出锁信息后结束（不读不写 state）。成功则记录输出中的 tickId＝你的 fencing token。
2. 守卫（PIPE §3）：git status/branch（"脏"判定按 §3.1 豁免口径）＋运行
   validate-state.ps1（消费其 queue-head: 行作队首可领取性结论）；任一命中按 §3 处置退出。
3. 恢复判定（PIPE §4.2）：implementing/awaiting_acceptance 且 run 未到期 → 只汇报退出
   （工作者运行中，禁止续派）；run 到期 → 按 §4.2 计数与上限处置；awaiting_merge →
   直接执行 §4.7 七步收尾（v1.8 自动化，双漂移检测仍前置，失败转 blocked）；
   awaiting_unit_review → 置 idle 继续（v1.8 不可达态自愈）；blocked → 汇报等待点。
4. 选任务（仅 idle）：用 queue 队首条目的 taskId/contractPath/branch 三字段（不读契约正文；
   条目与契约的一致性已由 validate-state 真读校验）；不可领（queue-head: claimable=false）
   → 报告等所有者；单元切换按 §6.1 unitCheckpoint 判定。autoDiscovery=true 时（v1.11
   当前配置）在领取前先扫描 tasks/：发现"status=ready 且不在 queue"的契约**追加队尾**
   并在报告中列出（按目录字母序自然成拓扑序——任务号两位数字；不插队）。
5. 领取：冻结 base=git rev-parse redesign-main（40 位）；置 currentTask/branch/base、
   attempts 清零（implement/fix）、phase=implementing、run{kind=implement, runId/workerId=GUID,
   startedAt/leaseExpiresAt=now±leaseMinutes.implement}；写回 state（tickToken=你的 token）。
6. 实施段（§0.4 派发模式）：renew -Token 续租后，逐字使用附录 B 模板派发实施子代理
   （占位符：契约路径=队首条目 contractPath、分支=队首条目 branch、base=刚冻结 SHA；
   返工时附 lastFailureRecord）并阻塞等待；返回后再次 renew，成功才处理完成事件：
   headSha=git rev-parse <branch>；清 run、attempts.implement=0、
   lastFailureRecord=null、phase=awaiting_acceptance、写回 state。
7. 验收段（§0.4 派发模式）：attempts.accept+1；renew 续租；逐字附录 C 模板派发验收
   子代理（输入=契约路径/headSha/attempt 号/记录路径）并阻塞等待；返回后再次 renew，
   成功才处理完成事件（编排者不得向验收子代理转述实施过程——只传四项输入）。
   pass：acceptedHead=headSha、
   acceptanceRecord={branch:acc/<taskId>/<attempt>, path, commit}（验收者输出解析）、
   lastFailureRecord=null、清 run、**同 tick 续行第 8 步收尾（v1.8 自动合入，不驻留
   awaiting_merge）**；fail：把同一验收输出写入
   lastFailureRecord={branch,path,commit,attempt}，再按 §4.5 处置。
8. 收尾（v1.8 全自动：pass 后同 tick 执行，不再需要所有者"合并 <taskId>"指令；
   PIPE §4.7 七步固化次序不变：任务分支漂移检测→
   evidence 分支漂移检测→按 acceptanceRecord.commit 合并记录→按 acceptedHead 合并代码→
   push→治理提交与状态清理→删双分支）；任何一步失败即转 blocked 报告，不自行取舍。
9. 每次状态写回同步 heartbeat.updatedAt、tickToken=你的 token 与 tickCount+1；tick 结束
   输出固定报告（phase 变迁/当前任务/证据路径/下一步等待点），并执行
   pipeline-lock release -Token <你的 tickId>——token 失配（退出码 4）＝你的租约已被接管，
   自终止并报告，不得重试。
   报告中的队列摘要必须**逐字引用** validate-state 输出的 `queue-head:` 行（taskId 与
   claimable 结论），禁止叙述性改写、预测性表述或对未入队契约作可领取性判断——唯一
   例外是"队列空"时按 queue-head: (empty) 原样转述（F-013 教训：tick#40 报告编造
   "队首 TK-T03 依赖均已 done"而权威队列实为 CORE-T10、TK-T03 尚 planned）。
```

### 附录 B · 实施者模板（T-IMPL v3）

```text
你是任务 <taskId> 的实施者，负责实施段全程。输入：契约 <contractPath>、
分支 <branch>、base <40 位 SHA>、返工失败证据 <lastFailureRecord 或 null>。首次实施时
lastFailureRecord 必为 null，不读验收历史；返工时只读取该记录定位的不可变验收结论，逐项修复
阻断问题，不得把它当作验收通过或合入授权。

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
从 base 建分支 <branch>→实现→双模式构建（AGENTS.md §4.1；冒烟模式必须带 vcpkg
toolchain 参数）→自查 validate-task.ps1（ird_gates 未建成前如实标注未执行）→
测试执行留痕（gtest XML＋ird-test-report.json）→文档同步→分步四段式提交→
push origin <branch>。
停止条件：需求/架构语义拿不准、knownPitfalls 未决项、契约缺陷→停止并报告 blocked
请求，不得在代码中自行裁决，不得私改契约。
完成输出：验收请求（ACC §1 固定格式：任务 ID/分支/base..head（40 位 SHA）/
证据路径）。
```

### 附录 C · 验收者模板（T-ACC v3）

```text
你是任务 <taskId> 的验收者：全新上下文，只消费产物，不消费实施叙述（ACC §2 四条
独立性要求逐条适用；为"不通过"找证据）。
输入：契约 <contractPath>、送验提交 <headSha（40 位，冻结对象）>、验收尝试序号 <attempt>、
验收记录输出路径 <traceability/acceptance/<taskId>-<YYYYMMDD>[-rN].md>（同日多次验收
加 -r2 序号；与 attempt 独立——文件名防覆盖，attempt 防 evidence 分支重名）。

执行环境（ACC §3，按 SHA 不按分支名——任务分支可能仍被实施工作树检出）：
- 复现现场：git worktree add --detach <临时目录A> <headSha>；集成模式重新配置
  （确认 RWS_BUILD_INDUSTRIALROBOT:BOOL=ON）后构建；冒烟另配临时目录（带 toolchain）；
  契约 verify 命令逐条亲手执行。
- 记录回传（防记录随临时 worktree 消亡）：git worktree add -b acc/<taskId>/<attempt>
  <临时目录B> origin/redesign-main（attempt 序号保证 fail 重试不复用已有分支名）；
  在 B 内写验收记录→git add＋commit→push origin acc/<taskId>/<attempt>；
  输出记录三元组 {branch:acc/<taskId>/<attempt>, path, commit:40 位提交 SHA}
  （编排者将写入 state.acceptanceRecord——该 commit 是收尾按 SHA 合并的不可变对象）。
  确认推送成功后才允许删除两个 worktree。

检查清单：acceptance-protocol.md §4 的 4.1～4.11 逐项出证据（通过给命令输出/文件行号，
失败给反证；4.1 的 diff 范围＝<headSha 所在分支的 base..head>＝state.base..headSha）；
4.8 同时核对 traceability/findings.json 未关闭条目与本任务的相关性。
裁决与产出（ACC §5）：verdict＋逐项证据写入记录；建议级问题逐条转登
traceability/findings.json（F-xxx 编号顺延）；你不得合入，不得修改实施产物，
不得向任务分支推送任何提交（送验对象已冻结为 <headSha>）。
```

## 9. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 DTB v0.8 建立：六态状态机、五守卫、tick 流程、返工熔断、所有者授权分级 |
| v1.1 | 2026-09-10 | 维护者要求加固：①§0 上下文自足性；②§4 严格次序；③§3.6 无进展检测；④§5 防偷懒双保险＋ACC 4.11；⑤§2/§6 单元检查点 |
| v1.2 | 2026-09-10 | §0 阅读阶梯：三角色必读—按需下沉—禁止三档输入预算 |
| v1.2.1 | 2026-09-10 | §3.1 守卫豁免：流水线自持路径与构建产物目录不计脏，防心跳写回自锁 |
| v1.3 | 2026-09-10 | 一轮审核修复：tick 调用约定、state schema 文档化＋validate-state、守卫精确化、合并次序固化、中断对账（Acc-Covered）、发现闭环（findings.json）、三份提示词模板封存、契约编译协议（CCP）挂接 |
| v1.4 | 2026-09-10 | 二轮审查修复（6 条结构性问题）：①§0.1 原子锁 pipeline-lock.ps1；②工作者生命周期（run 租约＋阻塞等待＋显式完成事件）；③SHA 冻结链（base/headSha/acceptedHead；按 SHA 合并＋漂移检测）；④queue 结构化条目＋队首可领取性输出；⑤验收 detached worktree＋记录分支回传；⑥§6.1 policy 执行语义表；⑦附录模板升 v2 |
| v1.5 | 2026-09-10 | 三轮审查修复（1 P0＋3 P1）：①fencing token——release/renew 必须 -Token 与锁内 tickId 核对（退出码 4 拒绝），旧 tick 超时被接管后无法释放/续租新锁；状态写入纪律（仅持锁可写＋tickToken 侧写＋心跳时钟健全性 ≤now+5min 与单调性 ≥run.startedAt——两类历史事故实录均被检出）；②queue 真读校验（contractPath 指向契约本体的 taskId/branch 必须与条目一致＋队内契约逐份过 validate-task）；③evidence 分支按尝试隔离 acc/<taskId>/<attempt>（attempts.accept 计数；fail 重试不复用分支名），acceptanceRecord.commit 收紧为 40 位不可变 SHA，收尾先验 evidence 分支未漂移再按该 SHA 合并（不合并分支尖端）；④in-flight 三态（implementing/awaiting_acceptance/awaiting_merge）统一强制 branch＋base＋currentTask==queue 队首＋branch==队列条目；⑤附录模板升 v3（token/续租/attempt/双漂移检测） |
| v1.6 | 2026-09-10 | 四轮审查修复（关闭剩余 P0/P1）：①pipeline-lock 全动作置于按仓库路径派生的 Windows 命名 mutex，令 fencing 核对与删除/续租无 TOCTOU 窗口；②阻塞等待返回后必须再次 renew，旧 tick 不得处理完成事件或写 state；③新增 lastFailureRecord{branch,path,commit,attempt}，验收 fail 将不可变证据回传返工实施者，首次实施及非返工阶段强制清 null；④附录 T-ORCH 升 v4、T-IMPL 升 v3，明确上述输入与时序 |
| v1.7 | 2026-09-10 | 所有者指令"禁止使用子智能体"：新增 §0.4 单会话执行模式——实施/验收角色由 tick 会话顺序扮演，附录 B/C 纪律与清单不变；独立性降级须在验收记录声明；续租纪律保持（角色前后各一次）；T-ORCH 升 v5（步骤 6/7 改会话内执行）；指令解除须增量修订，禁止静默切回 |
| v1.8 | 2026-09-10 | 所有者指令"流程全自动化，不需要所有者指令"：①state.policy 切换——autoMerge.enabled=true＋classes=全词表 {doc,build,implementation}（预授权按 §6.1 机制登记）；unitCheckpoint=false（单元检查点关闭，awaiting_unit_review 成不可达态）；②§4.6/§4.7 pass 后同 tick 执行七步收尾（漂移检测/冲突即停转 blocked 的保护不变），§4.2 awaiting_merge 变为收尾中断自愈锚点；③§6 所有者触点改监督性（blocked 解除/语义裁决/暂停恢复仍需所有者——诚实边界）；④§7 增双重放宽风险声明（单会话＋自动合入）与监督补偿建议；⑤T-ORCH 升 v6 |
| v1.9 | 2026-09-10 | 所有者解除子代理禁令（走 §0.4 预留的解除路径）：①恢复 v1.6 派发执行模型——实施/验收由全新子代理承载（§0.4 重写、§1 角色映射、§2 完成事件、附录 A 步骤 6/7 恢复"派发＋阻塞等待＋二次 renew"），附录 T-ORCH 升 v7；②恢复前完成派发能力实证（无害子代理调用成功）；③独立性回到模型级，验收记录不再需要降级声明，§7 风险声明更新（仅余自动合入放宽）；④新增承载者混同禁令：编排者不亲自实施/验收；⑤v1.8 全部自动化语义（autoMerge 全词表、无单元检查点、pass 同 tick 收尾、监督性触点）保留不变 |
| v1.10 | 2026-09-11 | F-013 整改（tick#40 报告编造"队首 TK-T03 依赖均已 done"，权威队列实为 CORE-T10 且 TK-T03 尚 planned——报告叙述失实而状态未被污染）：附录 A T-ORCH 步骤 9 增补硬性规定——tick 报告的队列摘要必须逐字引用 validate-state 的 queue-head: 行，禁止叙述性改写、预测性表述或对未入队契约作可领取性判断（队列空按 "(empty)" 原样转述）；T-ORCH 升 v8 |
| v1.11 | 2026-09-11 | 所有者裁决"预授权平台层整链批次序列（B 模式）＋autoDiscovery"：①§6.1 autoDiscovery 语义行登记开启依据——DOC-Txx 编译批次放行的新 ready 契约由 tick 自动追加队尾，消除逐批手工"调整队列"（state 侧翻转随本修订之后执行）；②批次波次固化：DOC-T06 runtime(12)→T07 evidence(11)→T08 policy(11)→T09 diagnostics(10)→T10 project(15)→T11 io(6)→T12 execution(10)→T13 ui(13)，EX/UI 的跨单元依赖由前序波次自然满足；③附录 A T-ORCH 升 v9（步骤 4 增补扫描动作与字母序＝拓扑序说明）；④O 项阻塞（O-09/O-24/O-31）浮出时仍停等所有者，预授权不掩盖真失败 |
