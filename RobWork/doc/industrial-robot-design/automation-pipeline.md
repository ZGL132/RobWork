# 自动化流水线协议（automation-pipeline）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-09-10 建立，随 DTB v0.8 登记） |
| 文档代号 | PIPE |
| 上游 | acceptance-protocol.md（验收段完全复用其清单与独立性要求）、development-task-breakdown.md §5.7/§8（三段式流程与契约家族）、AGENTS.md §6（提交/推送/循环约定） |
| 状态载体 | `traceability/pipeline/state.json`（唯一事实源；每 tick 读写；崩溃后从状态恢复，不依赖会话记忆） |
| 驱动方式 | 定时自动化 tick（每 30 分钟）＋ 手动触发（任一会话说"流水线 tick"） |

## 1. 角色映射

| 协议角色 | 流水线承载 | 独立性保证 |
| --- | --- | --- |
| 实施者 | 全新子代理，仅输入：任务契约路径、单元卡/DTB 章节引用、分支名 | 不读验收历史；从分支当前提交状态续作 |
| 验收者 | 另一个全新子代理，仅输入：契约路径、分支、base..head、验收记录输出路径 | **只给产物不给叙述**——主会话不得向其转述实施过程 |
| 所有者 | 维护者（人） | 合并决策（除预授权类）、语义裁决、暂停/恢复 |

## 2. 状态机

| phase | 含义 | 下一态 |
| --- | --- | --- |
| `paused` | 暂停（初始态/用户暂停/环境占用） | idle（用户恢复） |
| `idle` | 空闲，选下一个任务 | implementing / 保持 idle（无可领任务） |
| `implementing` | 实施子代理工作中（分支已建、按步提交） | awaiting_acceptance（验收请求就绪）/ blocked |
| `awaiting_acceptance` | 待验收 tick（验收子代理对抗式核查） | awaiting_merge（pass）/ implementing（fail 返工） |
| `awaiting_merge` | 验收 pass，等所有者合并（或预授权自动合并） | idle（合并完成）/ 保持 |
| `blocked` | 熔断：重试超限/语义分歧/O 项裁决/守卫异常 | idle（所有者解除）/ paused |

状态文件还承载：`currentTask`、`branch`、`base`、`attempts{implement,fix}`、`history[]`（每任务一行结论）、`policy`、`queue[]`。

## 3. 守卫（每个 tick 最先执行，任一命中即 paused 并报告，**不得 stash/checkout 干扰**）

1. 工作树不干净，或当前分支不是 `redesign-main`（且无本流水线在管分支）→ paused（例如 CORE-T01 手动会话进行中）；
2. 状态 phase=paused/stopped → 报告退出；
3. `main` 分支任何形式触碰 → 永久禁止（AGENTS.md §6.2 红线）；
4. 任务契约 status 非 ready、或 dependsOn 存在未 done 项 → 该任务不可领；
5. 流水线自身异常（状态文件损坏/冲突）→ stopped，等所有者。

## 4. tick 流程

1. **守卫**（§3）→ 2. **恢复判定**：phase 非 idle 时按状态机续作（implementing→继续/重启实施子代理〔重启上限 policy.maxImplementRestarts=2〕；awaiting_acceptance→派验收子代理）→ 3. **选任务**（idle 时）：queue 顺序优先，其后自动发现"ready 且 dependsOn 全 done"的契约 → 4. **实施段**：子代理建分支 `wp<nn>-t<kk>`、按契约实现、双模式构建、测试留痕、文档同步、四段式 commit、push origin → 5. **验收段**：验收子代理按 acceptance-protocol.md 出 pass/fail＋证据 → 6. **裁决处理**：pass→（policy.autoMerge 命中则自动合并推送，否则 awaiting_merge 报告）；fail→fix 循环（新实施子代理只拿验收记录＋契约；上限 maxFixCycles=3，超限 blocked）→ 7. **收尾**：合并后更新契约 status=done、解锁 dependents、history 追加、state 落盘。

## 5. 所有者触点与授权分级

- **默认**：`policy.autoMerge.enabled=false`——每个 pass 都停在 awaiting_merge，由你说"合并 <taskId>"（或自己 git merge）；
- **预授权**（可选）：你明确授权后按任务类别/规模开自动合并（如仅 S 类构建落位），记录进 policy；
- **裁决**：语义分歧、O 项、需求疑问 → blocked＋登记 DTB §4，等你裁定；
- **控制**："暂停流水线"/"恢复流水线"/"流水线状态" 三个口令随时有效。

## 6. 边界与诚实声明

- 验收独立性为**模型级**（全新子代理＋只给产物），弱于跨会话级人工分离——以对抗式清单＋复现构建弥补；
- 单工作树＝单轨道：流水线占用工作区期间，请勿并行手动实施（守卫会互相避让，但会互相暂停）；
- 实施子代理单个任务超长时按步提交、tick 可续作；连续失败熔断后**不自动换任务**，等所有者。

## 7. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随 DTB v0.8 建立：六态状态机、五守卫、tick 流程、返工熔断、所有者授权分级；状态载体 traceability/pipeline/state.json |
