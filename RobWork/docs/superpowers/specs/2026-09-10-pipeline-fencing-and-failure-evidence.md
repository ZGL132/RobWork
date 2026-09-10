# 流水线 fencing 与失败验收证据修复设计

## 目标

消除已过期 tick 在新 tick 接管锁后仍能删除或改写新锁的竞态；使失败验收记录成为返工实施者唯一、可定位且可验证的输入；并消除验收协议中的旧 evidence 分支命名。

## 范围与非目标

- 修改范围限于 `RobWork/scripts/industrialrobot/pipeline-lock.ps1`、`validate-state.ps1`、流水线协议、验收协议、状态样例及其治理验证脚本。
- 不修改产品源码、任务契约语义、自动合入授权或现有任务队列顺序。
- 不把 `state.json` 当作跨机器分布式事务存储；本修复只保证同一 Windows 仓库上的 tick 锁操作具备原子临界区和 fencing 语义。

## 设计

### 1. 锁操作的原子临界区

`pipeline-lock.ps1` 增加以仓库路径派生的 Windows 命名 mutex。`acquire`、陈锁接管、`renew` 与 `release` 均在持有此 mutex 时完成“读取锁内容 → 核对 token/租约 → 改名、续租或删除”的整个序列。

`acquire` 生成的 `tickId` 仍是 fencing token。`renew` 与 `release` 仅在锁内 `tickId` 与调用方 token 相等时成功；不等时返回退出码 4。这样旧 tick 即使在 token 检查后被新 tick 接管，也不能在临界区外删除或覆盖新锁。

编排者在每次可能耗时的阻塞等待返回后、写 `state.json` 前，必须重新 renew；renew 失败即终止该 tick，不写状态也不释放他人的锁。

### 2. 失败验收记录

状态 schema 新增 `lastFailureRecord`，形态为 `{branch, path, commit, attempt}`。失败验收完成后，保留验收者已推送的 evidence 分支与记录 SHA 到该字段；清空的是 `headSha`、`acceptedHead` 和成功态 `acceptanceRecord`。

返工实施者模板新增显式输入 `lastFailureRecord`，并且只允许读取这一个记录，不读取其他验收历史。新实施完成并冻结新的 `headSha` 后清除 `lastFailureRecord`。状态校验器验证其分支名、路径、40 位提交 SHA、attempt 正整数及其仅出现于返工 implementing 阶段的约束。

### 3. 协议一致性与回归测试

ACC 的所有 evidence 分支表述统一为 `acc/<taskId>/<attempt>`。新增 PowerShell 回归测试覆盖：

1. 旧 token 在新 token 接管后 renew/release 都返回 not-owner，且新锁内容不变；
2. `lastFailureRecord` 在返工 implementing 状态必需且满足 schema，缺失或非法形态被拒绝；
3. 成功合入或全新实施状态不得残留失败记录；
4. 文档中不存在旧的 `acc/<taskId>` 证据分支操作指令。

## 验收标准

- `pipeline-lock` 的竞态回归测试通过，且错 token 不会删除或覆盖新锁。
- `validate-state.ps1` 对合法失败返工状态通过，对缺失/非法失败记录失败。
- `validate-docs.ps1`、所有 ready 契约的 `validate-task.ps1` 与脚本回归测试通过。
- 流水线保持 `paused`，不自动领取或执行 TK-T01。
