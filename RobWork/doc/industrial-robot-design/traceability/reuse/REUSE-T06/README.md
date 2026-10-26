# REUSE-T06 / WP-20 全量收口证据（2026-10-27）

任务：WP-20 / T06 最终收口（migration §13.3 WP-20 行）。起始 SHA `56bc2814064f5824dbd5e9ab9f763915494a7fac`（工作区干净）。**零产品代码变更**——本包全部产出为治理面统计、核实与登记。

## 证据清单

| 文件 | 内容 |
| --- | --- |
| `final-mode-statistics.json` | 全 20 单元 functions 矩阵终态统计（逐单元 byStatus/byDecision＋decision×status 交叉）、catalog 40 能力 verified/candidate 清单、inventory 1401 条 classification 分布 |
| `pending-retention-classification.json` | pending 594 行逐组保留理由三分类（业务专属自持 348／待批 124／待后续包 122），组→行集映射由 scope+capability 完全确定 |
| `dead-core-sweep.md` | 删死核心扫尾与测试回流检查全记录（rg 命令与实测结果） |

## 口径与边界（诚实声明）

1. **最终判断从关闭记录与矩阵行得出，不改原 classification/基点**（§12.8）：inventory 的 audit-required=1383 为 T01 审计基点冻结面，本轮仅统计不回写；functions 矩阵行 status/decision 零改动。
2. **pending 不冒充关闭**：594 行全部归入三类并逐组给出理由；组内行共享同一 capability 与保留理由文本（矩阵行 differences/search.conclusion 原文为本包采样依据）。
3. **在档保留面非漏网**：仅位置 3 维 DLS＋约束 5 函数（GAP-IK-01，GOV-T27 §8 清单）、RK4＋质量阵消元（GAP-FD-BUILD-01）、自持 SHA（GAP-DIGEST-01）均有 blocked 登记链。
4. **发现项如实登记不在本包处置**：CORE-EVENT 16 行（core 5＋execution 11）differences 文本仍含「F967 待改」字样——F-967 已 fixed 且独验 pass，属登记滞后；rg 核实生产装配零引用。行面刷新留 CORE-EVENT 审计收口批次。
5. **集成回归不重跑**：本包零产品代码变更，集成证据引用各收口包原始记录（REUSE-T30 runtime 309／T51 kinematics 297／T52 dynamics 119／T54 project 290／GOV-T27 297／T41 execution 157+38 等），符合 AGENTS §4 增量口径与"重复命令执行一次、引用同一证据"。

## 父任务归位与登记

- **REUSE-T02 → done**：scope（§12.6）被 T13（offset 前置）/T17/T19（锚与 FK/Jacobian）/T30（WC/DWC 映射，独验 pass）/T51（q_rw↔q_auth）/T41（宿主例外）全部消费。
- **REUSE-T03 → done**：scope（§12.7 T03-A/B）被 T19（done）/T51（done）/GOV-T27（done，GAP-IK-01 定案 blocked）全部消费。
- **REUSE-T04 → planned 保留**：T04-A＝T52 ready 待独验；T04-B＝WP-09 blocked（GAP-FD-BUILD-01 未批准）。
- **REUSE-T05 → planned 保留**：大量子任务消费（done/ready 清单见契约 note）；剩余＝pending 594 行三分类中 B/C 两类，待批/待后续包。
- **REUSE-T06 → ready（实施完成待独验）**：本契约。

## 独验移交面（由独立验收员执行，实施者不自签）

- 阻断面分列：T42~T48/T50/T52 八契约 ready 待独验；GAP-IK-01/GAP-FD-BUILD-01/GAP-PRJ-DIAG-BOUNDARY-01/F-966/F-974/F-975 待所有者；GOV-T21 接收待所有者；T04/T05 父任务 planned 有据。
- 结论限定本轮版本/范围，不声称不存在不可发现语义重复（§12.8）。
