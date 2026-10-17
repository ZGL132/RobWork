---
name: ird-verify-change
description: 工业机械臂变更验证——按变更面选择验证命令矩阵，以 PASS/FAIL/SKIP/NOT RUN 四态如实报告并附证据路径。证据文化：每条 claim 可证伪、可重算；未执行的验证绝不标注通过。
---

# ird-verify-change：变更验证（四态如实报告）

**时机**：任一改动面（文档/代码/脚本/契约/GUI）完成后，提交前或验收段。
**原则**：A claim is falsifiable and recomputable——报告的每个结论都必须能被第三方用同一条命令重跑出来。

## 命令矩阵（按变更面选择）

| 变更面 | 必跑 | 命令 |
| --- | --- | --- |
| 纯文档（doc 树） | ✓ | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` |
| 产品代码 | ✓ | 集成模式构建＋目标 `ctest`（含 `_test`/`_contract_test`）＋独立冒烟（vcpkg＋Qt 前缀） |
| 源码红线面 | ✓ | `ird_gates`（命中集与登记基线 diff）＋`core/test/BuildRedLineTest` |
| 治理/验证脚本 | ✓ | 被改脚本本体＋`validate-docs.ps1`（回归） |
| 任务契约 JSON | ✓ | `validate-task.ps1 -TaskFile <契约>`；实施完成另跑 `verify-task.ps1` |
| GUI 面 | ✓ | 按 `ird-qt-gui-windows` Skill 流程（真窗口，禁 offscreen） |

## 报告格式（四态，逐条）

```markdown
## 验证报告：<taskId/批次>
- [PASS] <命令>——<关键数字>（证据：<路径>）
- [FAIL] <命令>——<失败摘要首行>（日志：<路径>；原因分析：<一句>）
- [SKIP] <命令>——<豁免依据：登记号/先例>
- [NOT RUN] <命令>——<未执行原因，如实>
```

## 纪律

- 四态之外没有第五态；"应该能过"不是结论；
- 失败时先停（不带病前进），登记原因后再决定返工或上报；
- 证据落位：gtest XML / per-exe ird-test-report / 构建日志 → `traceability/`（只增不改）；
- 门禁类验证若涉及警告模式过渡期（新检查先警告后强制），报告中注明当前模式。
