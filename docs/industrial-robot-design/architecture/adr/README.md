# 架构决策记录

每个 ADR 使用 `ADR-NNN-<topic>.md` 命名，必须包含：背景、决策、备选方案、影响、迁移、需求 ID、受影响 WP、验证证据和状态（Proposed/Accepted/Superseded）。

> D1 检查点：`IRD-D1-20260829`；D2 检查点：`IRD-D2-20260829`；D10 联合评审：`IRD-D10-20260829`  
> 当前结论：ADR-001～005 全部于 `IRD-D10-20260829` 联合评审通过（`Accepted`）；此后修改已有决策时优先新增替代 ADR 并将旧 ADR 标记 `Superseded`。

| ID | 决策 | 所有者 | 关联契约 | 状态 |
| --- | --- | --- | --- | --- |
| [ADR-001](ADR-001-single-robot-owner-scope.md) | 首版单机械臂与稳定所有者作用域 | WP-03、04、06 | CTR-DOM-001/002、CTR-NAM-001 | `Accepted` |
| [ADR-002](ADR-002-rwdesign-directory-format.md) | `.rwdesign` 目录式规范项目格式 | WP-04、11 | CTR-PER-001～003 | `Accepted` |
| [ADR-003](ADR-003-release-slices-and-opt-b.md) | R1/R2 发布切片与 OPT-B 权威范围 | WP-00、20、21、24 | CTR-REL-001、CTR-OPT-001 | `Accepted` |
| [ADR-004](ADR-004-single-authority-shared-semantics.md) | 跨模块共享语义的单一权威所有者 | WP-03、05、06、07、09、18 | CTR-DOM/NAM/POL/DIA/EXE | `Accepted` |
| [ADR-005](ADR-005-orthogonal-result-status-and-naming.md) | 评估正交状态拆分、Completed+Warning 裁决与公共符号命名裁决 | WP-03、05、08、20 | CTR-DOM-004、CTR-EXE-001/004、CTR-OPT-002 | `Accepted` |

新 ADR 使用 [TEMPLATE.md](TEMPLATE.md)。编号一经登记不得复用；修改已有决策时优先新增替代 ADR，将旧 ADR 标记 `Superseded` 并保留历史链接。索引不替代正文，正文完成也不等于通过接受评审。

## 接受流程

1. 决策所有者确认决策文本和迁移责任；
2. 所有登记消费者确认接口、数据和发布影响；
3. 独立测试负责人确认验证证据可执行；
4. 契约注册表和符号注册表同步；
5. 状态从 `Proposed` 改为 `Accepted`，记录评审日期与证据位置。
