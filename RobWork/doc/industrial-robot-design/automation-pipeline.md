# 自动化流水线协议（automation-pipeline）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.1（2026-09-10 加固：上下文自足、严格次序、防卡死、防偷懒、单元检查点） |
| 文档代号 | PIPE |
| 上游 | acceptance-protocol.md（验收段完全复用其清单与独立性要求）、development-task-breakdown.md §5.7/§8（三段式流程与契约家族）、AGENTS.md §6（提交/推送/循环约定） |
| 状态载体 | `traceability/pipeline/state.json`（唯一事实源；每 tick 读写并更新 `heartbeat.updatedAt`；崩溃后从状态恢复，**不依赖任何会话记忆**） |

## 0. 上下文自足性（tick 的全部输入）

任何全新上下文的执行者只需三样东西即可正确执行一个 tick：①本文件；②`state.json`（含 docRefs 指针与完整状态）；③其内嵌引用的契约路径。tick 提示词不携带历史对话；一切长期知识（约定/红线/流程）必须已在文档中——这是 DTB §5.7"持久状态落文档"的延伸。若 tick 发现 state.json 缺字段或与本文件矛盾：以本文件为准修复 state.json 并在汇报中注明。

## 1. 角色映射

| 协议角色 | 流水线承载 | 独立性保证 |
| --- | --- | --- |
| 实施者 | 全新子代理，仅输入：任务契约路径、单元卡/DTB 章节引用、分支名 | 不读验收历史；从分支当前提交状态续作 |
| 验收者 | 另一个全新子代理，仅输入：契约路径、分支、base..head、验收记录输出路径 | **只给产物不给叙述**——主会话不得向其转述实施过程 |
| 所有者 | 维护者（人） | 每任务合并决策（默认门控）＋每单元人工审核＋语义裁决＋暂停/恢复 |

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

## 3. 守卫（每 tick 最先执行，任一命中即报告退出，**不得 stash/checkout 干扰**）

1. 工作树不干净，或当前分支不是 `redesign-main`（且非本流水线在管分支）→ 暂停报告；
2. phase=paused/stopped → 报告退出；
3. `main` 分支任何形式触碰 → 永久禁止；
4. 任务契约 status 非 ready、或 dependsOn 存在未 done 项 → 该任务不可领；
5. 流水线自身异常（状态损坏/不可修复矛盾）→ stopped，等所有者；
6. **无进展检测**：连续 `policy.noProgressLimit`（默认 3）个 tick 状态机无推进（heartbeat.ticksNoProgress 累计）→ blocked 并报告卡点。

## 4. tick 流程与次序纪律

1. **守卫**（§3）→ 2. **恢复判定**：按状态机续作（implementing→续派实施子代理，重启上限 `maxImplementRestarts=2`；awaiting_acceptance→派验收子代理）→ 3. **选任务**（仅 idle 时）：
   - **严格次序**：`queue` 按序消费，队首不可领（未 ready/前置未满足/blocked）时**不自动跳取后续任务**——停在 idle 报告"队首 <taskId> 不可领：原因"，等所有者调整队列或解除阻塞；
   - `autoDiscovery` 默认 **false**（自动发现新 ready 契约需所有者在 policy 中显式开启）；
   - **单元检查点**：若本次拟领任务的 `unit` 与上一已完成任务不同，先转 `awaiting_unit_review`（所有者说"单元审核通过，继续"后领新任务）；
   4. **实施段**：子代理建分支、按契约实现（**防偷懒纪律见 §5**）、双模式构建、测试留痕、文档同步、分步四段式 commit（每个可独立验证的步骤一提交，禁止攒一个大提交）、push origin → 5. **验收段**：验收子代理按 acceptance-protocol.md（v1.1，含 4.11 偷懒扫描）出 pass/fail＋逐项证据 → 6. **裁决处理**：pass→awaiting_merge（`autoMerge.enabled=false` 时**永不自动合并**，预授权须所有者显式写入 policy）；fail→fix 循环（新实施子代理只拿验收记录＋契约，上限 `maxFixCycles=3`，超限 blocked）→ 7. **收尾**：所有者合并指令后执行合并推送、契约 status=done、解锁 dependents、history 追加、heartbeat 重置、state 落盘。

## 5. 防偷懒纪律（实施端与验收端双保险）

**实施端（写进每个实施子代理的指令）**：
- 契约 `acceptance` 数组**逐条对应实现**，逐条自证；禁止 `TODO/FIXME/stub/placeholder/未实现/throw "not implemented"`（契约显式标注"触发式延后"的除外，如 WP-02-T11 类，须在提交信息列出豁免依据）；
- 公共接口函数必须有真实逻辑与错误处理，不允许空函数体/恒等返回/只编译不行为；
- 测试必须断言行为与边界（对照附录 D 容差与任务卡反例），禁止恒真断言、"只跑不断言"的用例；每个 acceptance 条目至少对应一个具名测试或一条执行证据；
- 注释按 AGENTS.md §2 全量执行（文件头/Doxygen/高危信息表）——注释缺失本身即验收 fail 项。

**验收端**：acceptance-protocol.md v1.1 第 4.11 条（偷懒/缩水扫描）＋第 4.4/4.5 条强化（逐条证据表、测试真实失败能力）——无证据即 fail，不采信"已实现"的口头声明。

## 6. 所有者触点与授权分级

- **每任务**：pass 后 `awaiting_merge`，等"合并 <taskId>"（autoMerge 默认关闭且不预授权——开启须显式写入 policy 并注明类别）；
- **每单元**：单元最后任务合并后 `awaiting_unit_review`，等"单元审核通过，继续"——单元级人工审核是硬门控；
- **裁决**：语义分歧/O 项/需求疑问 → blocked＋登记 DTB §4；
- **控制口令**："流水线状态" / "恢复流水线" / "暂停流水线" / "调整队列：<序列>" / "单元审核通过，继续"。

## 7. 边界与诚实声明

- 验收独立性为**模型级**（全新子代理＋只给产物），弱于跨会话人工分离——以对抗式清单＋复现构建＋偷懒扫描弥补；重要任务（L 类）建议所有者亲自复核验收记录；
- 单工作树＝单轨道：流水线占用工作区期间请勿并行手动实施；
- 连续失败熔断后**不自动换任务**（§4 严格次序），等所有者。

## 8. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 DTB v0.8 建立：六态状态机、五守卫、tick 流程、返工熔断、所有者授权分级 |
| v1.1 | 2026-09-10 | 维护者要求加固：①§0 上下文自足性（tick 三输入、以本文件为准修复状态）；②§4 严格次序（队首不可领不跳队、autoDiscovery 默认关闭）；③§3.6 无进展检测（3 tick 无推进即 blocked）；④§5 防偷懒纪律双保险＋验收协议 v1.1 第 4.11 条；⑤§2/§6 单元检查点 awaiting_unit_review（单元间人工审核硬门控） |
