# 方案 B.1 自动化流水线编排实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. 本计划遵循所有者“禁止使用子智能体”的当前指令，所有阶段由同一会话按角色顺序执行并在验收记录中声明独立性降级。

**Goal:** 把方案 B.1 的十一项实现任务按真实拓扑顺序、外部门禁、验收与合入规则编排成可恢复的自动化流水线。

**Architecture:** 使用 `b1-host-integration-batch.json` 作为批次顺序的机器事实源，各 canonical 契约继续作为实现范围和验收的唯一事实源。流水线在单会话中顺序切换实施者与验收者角色，仍保留 SHA 冻结、独立 worktree、失败返工和冲突即停规则。

**Tech Stack:** PowerShell 7、JSON 任务契约、Git、CMake/CTest、RobWorkStudio Qt Widgets。

---

### Task 1: 完成 DOC-T14 增补审查

**Files:**
- Modify: `RobWork/doc/industrial-robot-design/tasks/DOC-T14.json`
- Modify: `RobWork/doc/industrial-robot-design/traceability/contract-compile-log.md`
- Test: `RobWork/scripts/industrialrobot/validate-docs.ps1`

- [ ] **Step 1: 核对 DOC-T14 改动范围与 12 份契约字段**
- [ ] **Step 2: 运行 validate-docs、十二份 validate-task 与 validate-state**
- [ ] **Step 3: 把字母序入队死锁和 WP-24-T03 外部前置写入评审结论**
- [ ] **Step 4: 修订后重新执行全量文档门禁**
- [ ] **Step 5: 独立评审通过后再把 DOC-T14 置 done、十一份实现契约原子置 ready**

### Task 2: 冻结 B.1 批次顺序与门禁

**Files:**
- Create: `RobWork/doc/industrial-robot-design/traceability/pipeline/b1-host-integration-batch.json`
- Create: `RobWork/scripts/industrialrobot/validate-b1-batch.ps1`
- Modify: `RobWork/doc/industrial-robot-design/automation-pipeline.md`

- [ ] **Step 1: 按 UI-T19→RT-T14→WP-24-T08→UI-T20→UI-T21→UI-T22→WP-13-T20→WP-14-T10→WP-15-T18→UI-T23→WP-24-T09 写入显式序列**
- [ ] **Step 2: 为 WP-24-T08 登记 WP-24-T03-COMPLETE 外部门禁**
- [ ] **Step 3: 运行 `pwsh -File RobWork/scripts/industrialrobot/validate-b1-batch.ps1`，预期输出 `PASS (stages=11, openGates=1)`**
- [ ] **Step 4: 保持十一份实现契约为 planned，避免未评审任务被领取**

### Task 3: 按批次顺序执行实现、验收与合入

**Files:**
- Read: `RobWork/doc/industrial-robot-design/traceability/pipeline/b1-host-integration-batch.json`
- Read: `RobWork/doc/industrial-robot-design/automation-pipeline.md`
- Read: `RobWork/doc/industrial-robot-design/tasks/foundation/<taskId>.json`

- [ ] **Step 1: idle tick 按 manifest-order 把 ready 契约追加队尾**
- [ ] **Step 2: 队首若有 open 外部门禁，保持 idle 并报告门禁证据要求**
- [ ] **Step 3: 同一会话按 T-IMPL 执行实施并冻结 headSha**
- [ ] **Step 4: 切换到验收角色，只按 T-ACC 从冻结 SHA 复验，并声明单会话独立性降级**
- [ ] **Step 5: pass 后按不可变 SHA 自动合入；fail 则携带 lastFailureRecord 返工，超过上限转 blocked**
- [ ] **Step 6: WP-24-T09 done、队列无本批条目且 phase=idle 时，把批次状态置 done**

### Task 4: 每阶段工程验证

**Files:**
- Test: `RobWork/doc/industrial-robot-design/traceability/builds/<taskId>/`
- Test: `RobWork/doc/industrial-robot-design/traceability/acceptance/<taskId>-<date>.md`

- [ ] **Step 1: 按契约逐条运行 verify**
- [ ] **Step 2: Qt GUI 测试进入 VS x64 环境，设置 `QT_QPA_PLATFORM=windows`，一次只启动一个绝对路径 executable**
- [ ] **Step 3: 保存构建日志、gtest XML、ird-test-report.json 与 GUI 点验留痕**
- [ ] **Step 4: 运行 ird_gates、validate-docs、validate-task、validate-state 和 validate-b1-batch**
- [ ] **Step 5: 验证证据不足或真实失败时停止，不标记通过**
