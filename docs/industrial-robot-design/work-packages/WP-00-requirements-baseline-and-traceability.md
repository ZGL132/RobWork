# WP-00 需求基线与追踪实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立唯一、可版本化、可机器检查的需求基线，并保证 124 项需求、19 个核心场景、工作包、实现、测试和评审双向可追踪。

**Architecture:** Markdown 需求文件是语义权威，CSV 矩阵是执行索引。生成器从需求表和第 16 章追踪表提取稳定 ID，再叠加显式工作包所有权；验证器阻止重复、缺失、空验收和非法工作包。

**Tech Stack:** Markdown、PowerShell 7、CSV、Git。

---

## 范围与所有权

**拥有文件：**

- `docs/industrial-robot-design-software-requirements.md`
- `docs/industrial-robot-design-development-task-breakdown.md`
- `docs/industrial-robot-design/requirement-traceability.csv`
- `docs/industrial-robot-design/generate-traceability.ps1`
- `docs/industrial-robot-design/validate-development-docs.ps1`

**覆盖：** 全部 124 项稳定需求、AT-01～19、A-GATE-01～07、阶段 B～E 退出条件。

**不负责：** 修改产品实现、重新解释算法容差、批准需求语义变化。

## 输出契约

追踪 CSV 固定字段：

```text
requirement_id,priority,requirement_summary,work_package,
implementation_task,test_task,review_task,acceptance_scenario,phase,status
```

每个需求恰好一行；`work_package` 第一项是主实现包，其余为支持包；状态只允许 `Planned、Ready、Implementing、Verifying、Reviewing、Rework、Integratable、Integrated、Deferred`。P1 只有经评审才可标记 `Deferred`。

## 任务

### Task 1：冻结需求版本

- [ ] 核对需求文件标题、修订记录和已确认决策，确认 v0.4 将首版直接导入格式统一为 CSV。
- [ ] 搜索所有旧工作簿格式关键词，只允许修订记录保留历史说明。
- [ ] 检查 REQ-05、SEL-01、SEL-02、NFR-SEC-01、NFR-SEC-03 和第 16 章追踪表语义一致。
- [ ] 运行 Markdown 表格列数、需求 ID 唯一性和 AT 编号连续性检查。

验证命令：

```powershell
pwsh -NoProfile -File .\docs\industrial-robot-design\validate-development-docs.ps1
```

预期：输出 `124 requirements, 19 acceptance tests, 0 trace gaps`，退出码为 0。

### Task 2：生成追踪矩阵

- [ ] 运行生成器，从需求文件重新生成 CSV，不人工编辑生成结果。
- [ ] 验证 124 行、124 个唯一 ID、110 个 P0、14 个 P1。
- [ ] 验证每行工作包、实现任务、测试任务、评审任务、验收场景和阶段均非空。
- [ ] 验证 CSV 中引用的工作包都属于 WP-00～WP-25。

```powershell
pwsh -NoProfile -File .\docs\industrial-robot-design\generate-traceability.ps1
Import-Csv .\docs\industrial-robot-design\requirement-traceability.csv |
    Group-Object requirement_id |
    Where-Object Count -ne 1
```

预期：生成器报告 124 行，第二条命令无输出。

### Task 3：建立文档门禁脚本

- [ ] 创建 `validate-development-docs.ps1`，验证编号、表格、占位文本、追踪覆盖和工作包文件存在性。
- [ ] 对缺少一个需求映射、重复 ID、空验收或不存在工作包文件分别编写故障夹具。
- [ ] 验证脚本对四类故障均返回非零，对正式文档返回 0。

### Task 4：独立验证与评审

- [ ] 验证者随机抽取每个需求前缀至少两项，从需求正文追到工作包、测试和阶段。
- [ ] 评审者检查正文中的强制性规则是否存在没有稳定需求 ID 或工作包承接的情况。
- [ ] 对发现的缺口先修订需求/总纲，再重新生成矩阵，不直接手改 CSV。

## 退出条件

- 124 项需求和 19 个 AT 场景完整、唯一且可双向追踪。
- P0 追踪覆盖率 100%；P1 全部保留目标阶段和验收方法。
- 文档门禁在本机与 GitLab CI 使用同一命令。
- 占位标记扫描命中数为 0。
