# Pipeline Fencing and Failure Evidence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 使流水线锁操作无检查后竞态，并让失败验收记录成为可验证的返工输入。

**Architecture:** `pipeline-lock.ps1` 以命名 mutex 串行化同仓库锁文件的所有读改删操作，token 校验与变更在同一临界区完成。`state.json` 增加仅用于返工 implementing 阶段的 `lastFailureRecord`；校验器和派发模板把它作为唯一失败证据指针。

**Tech Stack:** PowerShell 5.1 / pwsh 7、Windows .NET `System.Threading.Mutex`、Git、JSON 状态文件。

---

### Task 1: 建立治理脚本回归测试

**Files:**
- Create: `RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1`
- Modify: `RobWork/scripts/industrialrobot/pipeline-lock.ps1`
- Modify: `RobWork/scripts/industrialrobot/validate-state.ps1`

- [ ] **Step 1: 写失败用例**

在测试脚本中建立临时 `.git` 目录，构造 A 锁过期、B 接管后 A 以旧 token release 的场景；断言 B 的 `tickId` 保持不变。构造 implementing＋`attempts.fix=1` 状态，分别断言缺失 `lastFailureRecord` 失败、合法记录通过。

```powershell
Assert-ExitCode 4 (Invoke-Lock release $oldToken)
Assert-Equal $newToken (Read-LockTickId)
Assert-ExitCode 1 (Invoke-StateValidator $missingFailureRecord)
Assert-ExitCode 0 (Invoke-StateValidator $validFailureRecord)
```

- [ ] **Step 2: 运行测试确认红灯**

Run: `pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1`

Expected: 因 `lastFailureRecord` 尚未是 schema 字段而失败。

- [ ] **Step 3: 最小实现 mutex 临界区与失败记录 schema**

`pipeline-lock.ps1` 为 acquire/renew/release/陈锁接管建立同名 mutex；锁文件的读取、token 对比、续租和删除全部在该 mutex 内完成。`validate-state.ps1` 在 `implementing && attempts.fix>0` 时要求 `lastFailureRecord`，验证 branch/path/40 位 commit/正整数 attempt；全新实施、验收与成功合入状态拒绝残留该字段。

- [ ] **Step 4: 运行测试确认绿灯**

Run: `pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1`

Expected: 所有断言通过，临时锁目录被清理。

### Task 2: 同步协议与状态样例

**Files:**
- Modify: `RobWork/doc/industrial-robot-design/automation-pipeline.md`
- Modify: `RobWork/doc/industrial-robot-design/acceptance-protocol.md`
- Modify: `RobWork/doc/industrial-robot-design/development-task-breakdown.md`
- Modify: `RobWork/doc/industrial-robot-design/traceability/pipeline/state.json`

- [ ] **Step 1: 写失败文档断言**

在 Task 1 测试脚本加入文本断言：PIPE 必须包含 `lastFailureRecord` 与“阻塞等待返回后 renew”；ACC 不得再含把证据分支写为 `acc/<taskId>` 的操作指令。

```powershell
Assert-Match $pipe 'lastFailureRecord'
Assert-Match $pipe '返回后.*renew'
Assert-NotMatch $acc '落在 `acc/<taskId>` 分支'
```

- [ ] **Step 2: 运行测试确认红灯**

Run: `pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1`

Expected: 当前协议未定义失败记录字段且 ACC §5 仍有旧分支名，测试失败。

- [ ] **Step 3: 最小协议修订**

PIPE 明确：失败时保留 `lastFailureRecord`，返工实施者只读取它；每次阻塞等待返回后 renew 成功才写 state。ACC §5 统一 evidence 分支为 `acc/<taskId>/<attempt>`。DTB 登记本轮治理修复；初始 paused state 增加 `lastFailureRecord:null`。

- [ ] **Step 4: 运行测试确认绿灯**

Run: `pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1`

Expected: 脚本、状态与协议断言全通过。

### Task 3: 全量验证与交付

**Files:**
- Modify: 本计划的完成复选框

- [ ] **Step 1: 运行治理验证**

Run:

```powershell
pwsh -File RobWork/scripts/industrialrobot/test-pipeline-pipeline.ps1
pwsh -File RobWork/scripts/industrialrobot/validate-state.ps1
pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1
```

Expected: 三项均 PASS；状态保持 `paused`。

- [ ] **Step 2: 运行 ready 契约回归**

Run: 遍历 `tasks/**/*.json` 中 `status=ready` 的契约，并逐份运行 `validate-task.ps1`。

Expected: 每份输出 `validate-task: PASS`。

- [ ] **Step 3: 检查变更与提交**

Run: `git diff --check; git status --short`

Expected: 无空白错误；仅本计划列出的治理文件变化。

- [ ] **Step 4: 提交并推送**

提交信息使用 `[governance]` 中文摘要，正文说明 mutex fencing、失败证据链、执行过的真实验证和“流水线仍 paused”；随后 `git push origin redesign-main`。
