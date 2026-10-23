# REUSE-T19 编号修正与主线合并——独立复核记录（2026-10-24）

- 复核人：所有者授权的独立复核会话（子智能体，非实施者自签）。
- 范围：编号冲突修正（两轮）＋主线合并治理增量。包本体（FK/Jacobian 上游复用）已于 2026-10-23 独立验收 PASS（acceptance-20261024.md，受检 20522f776/434500c43）。

## 提交链（本分支 codex/reuse-foundation）

| 提交 | 内容 |
| --- | --- |
| 6da9cfa8f | （既有）REUSE-T18 独立验收记录归档 |
| c77ce3c58 | 编号映射修正：tasks/changes/traceability 目录 git mv 改登记 REUSE-T19；契约/验收/README/migration/单元卡 v0.39 追加映射；代码与用例内 REUSE_T18 实施期标识按受检 SHA 约定保留 |
| 09dd1407c | 契约 allowedFiles 三条目指引改指改名后路径（scope 15 项越界修复） |
| 41c5e5a38 | Merge 691cbd42e (redesign-main)：主线 F-958 收口并入；kinematics.json 行级语义解析（A 侧 T17/T19 为基＋主线 TestMainReport.main 摘要刷新＋CMake 绑定刷新；主线 T03 旧行 FrozenRuntimeView 不复活）；runtime/ui 自动合并（两侧改动文件集不相交） |
| 8babc8d70 | 契约 verify[0] 改指本包契约路径（复核发现漏改，validate-reuse 以新路径 PASS） |

## 复核判定：PASS（最终复核冻结 SHA：8babc8d7068a44ae2b7818b7526406f73de7cdae）

七项检查全过：①历史与冻结面（治理提交零代码触碰）；②受检 SHA 保持（Fk.cpp blob 与 20522f776 受检值逐字一致）；③无悬挂引用（活 verify/evidence 引用归零，余下命中仅为映射说明/主线 F-958 自身文件/受检证据副本）；④合并语义（43 行、T19 语义、无 FrozenRuntimeView 复活、主线 TestMainReport.main 刷新忠实保留）；⑤门禁与测试复跑（validate-reuse/validate-docs PASS、kinematics 全量 291 PASSED＋1 既有 skip exit 0、契约 57/57、verify-task-scope PASS 20 路径）；⑥共存性（主线 REUSE-T18〔F-958〕与本包 REUSE-T19 契约/变更清单并存无覆盖）；⑦B 侧不受影响（codex/reuse-testkit-setcheck tip＝a2a80a48b 未动）。

## 移交协调者

- 本分支已具备 `--no-ff` 合入 redesign-main 条件（合入动作与合入门禁归协调者）。
- 本包结束后 codex/reuse-foundation 不再叠加新包；下一包从更新后主线新建任务分支。
- 任务号分配流程已按协调规则执行：REUSE-T19 取主线最低空闲号（T19–T23 空闲中取首）。
