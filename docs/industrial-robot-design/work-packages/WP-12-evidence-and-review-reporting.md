# WP-12 证据与评审报告实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 从明确项目修订和结果集合生成唯一 `ReviewReport`，输出适合工程评审的 HTML、PDF 及 JSON/CSV 证据数据包。

**Architecture:** ReviewReport 是不可变结构化权威；所有展示格式由同一对象渲染。报告只查询 WP-04/05，不读取当前界面。PDF 由 Qt HTML/PrintSupport 离线生成，不引入新渲染依赖。

**Tech Stack:** C++、Qt Core/Gui/PrintSupport、HTML/CSS、JSON、CSV、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_reporting`、`sdurws_ird_reporting_test`。

**创建：**

- `industrialrobot/reporting/include/.../ReviewReport.hpp`
- `industrialrobot/reporting/include/.../ReviewReportBuilder.hpp`
- `industrialrobot/reporting/include/.../HtmlReportRenderer.hpp`
- `industrialrobot/reporting/include/.../PdfReportRenderer.hpp`
- `industrialrobot/reporting/include/.../EvidenceDataExporter.hpp`
- `industrialrobot/reporting/resources/report.zh-CN.html`
- `industrialrobot/reporting/resources/report.css`
- `industrialrobot/reporting/src/`
- `industrialrobot/reporting/test/`

**覆盖需求：** NFR-COR-04，EVI-01，REQ-06，OPT-04、07～09，NFR-SEC-03、07，第 5.2、5.3 节报告要求。

## ReviewReport 最小内容

```text
reportId / schemaVersion
projectId / branchId / projectRevision
selected result IDs and snapshot IDs
software/dependency baseline
design summary and baseline differences
requirements conclusion and evidence gaps
kinematic/trajectory/dynamic/selection conclusions
Pareto candidates and selection rationale
hard-constraint evidence and diagnostics
assumptions, limitations and fixed conclusion wording
reviewer/sign-off metadata
```

报告只能把满足 RequiredEvidenceProfile 的 Verified 完整结果标为正式可行。Quick、Partial、DataInsufficient 和过期结果可以作为历史/参考附录，但必须显著标识且不进入正式结论。

## 任务

### Task 1：权威报告对象

- [ ] 先写缺项目修订、快照、必需结果、策略、名称映射或软件基线的失败测试。
- [ ] 实现 ReviewReportBuilder，只接受明确 ID，不接受“当前界面结果”。
- [ ] 复用 WP-05 正式可行判定和 WP-09 诊断，不复制逻辑。

### Task 2：新机型与改型内容

- [ ] 新机型报告列出需求、设计参数、证据、候选和限制。
- [ ] 改型报告逐指标展示基线值、候选值、绝对变化、相对变化和来源。
- [ ] 不得用单一加权分数替代 Pareto 工程取舍。

### Task 3：HTML 与 PDF

- [ ] HTML 使用内嵌/本地 CSS 和资源，不访问网络。
- [ ] PDF 从同一 HTML 模型生成，验证分页、中文字体、表格、图例和页码。
- [ ] 关键数值、状态、诊断码和快照身份在 HTML/PDF/JSON 中一致。

### Task 4：JSON/CSV 证据包

- [ ] JSON 完整保存 ReviewReport 和引用身份；非有限数不得产生非法 JSON。
- [ ] CSV 分别输出设计参数、需求结果、候选指标、硬约束、器件淘汰和诊断。
- [ ] 调用 WP-11 CsvWriter 防公式注入，同时在 JSON 保存原值。

### Task 5：限制和固定措辞

- [ ] 碰撞结论使用“在本策略与分辨率下未发现碰撞”。
- [ ] 无签署公差时只显示“敏感度参考”，不输出鲁棒通过或概率结论。
- [ ] 关节侧机械能与电能严格区分，并展示传动效率/回馈假设。
- [ ] 结构优化未做强度/刚度校核时明确说明，不表述为结构已验证。

### Task 6：往返和可复现

- [ ] 删除/覆盖外部源后，使用项目不可变副本重新生成相同报告。
- [ ] 相同 ReviewReport 结构化内容生成语义等价 HTML/PDF 和逐字段一致数据包。
- [ ] 历史报告保留旧快照名称，新报告使用当前 RuntimeNameMap，不混淆 objectId。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_reporting_test$'
```

## 退出条件

- 每项正式结论可追到项目修订、快照、策略、评估器版本和证据。
- HTML、PDF、JSON 和 CSV 在关键字段与工程状态上完全一致。
- 报告不读取 Widget 或当前会话态，不把证据不足包装成通过。
