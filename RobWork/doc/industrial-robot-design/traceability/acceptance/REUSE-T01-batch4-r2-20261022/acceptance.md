# REUSE-T01 第四批 B4-P2-01 返工独立复核

**结论：PASS，B4-P2-01已修正。** 只复核此前第四批唯一需求追溯误绑；不复验本提交的五项API适用性实验，不重建、不扩大范围。全REUSE-T01仍未完成，所有pending不因此转为direct/adapter已通过或库存已关闭。

- 冻结：`877f6afee38446ab43c5e6a6fd9b09593925aedb`。
- 前次FAIL：`c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5`，报告 `C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch4-r1-b46e9b34/acceptance.md` 原样保留。
- 独立隔离clone：`C:/Users/zgl18/AppData/Local/Temp/REUSE-T01-batch4-r1-b46e9b34/repo`，detached HEAD切换至本次冻结，前后状态均干净。本次报告和命令原件保存在本目录。父工作树未修改，不提交、推送或派子代理。

## 返工事实

`reuse/functions/modeling.json` 的 `modeling.makeLinkPlaceholderCylinder` 已从错误的 `MDL-02` 改为 `MDL-04`，并引用 `units/modeling.md#2.5`、`#5.2` 和真实契约 `tasks/foundation/WP-13-T07.json`。

独立核实：REQUIREMENTS.md:213的MDL-04覆盖视觉/碰撞几何等配置；units/modeling.md:220明确将连杆几何/autoLink/占位圆柱映射到MDL-04和AT-01，:517定义连杆占位几何辅助；WP-13-T07确含MDL-04、AT-01上下文及“连杆占位圆柱生成”的独立acceptance项。需求、设计、真实任务契约形成一致追溯，不要求在矩阵中重复抄写AT原文。

与上次冻结逐字段比较，本行仅requirements及designRefs发生改变；sources、namespace、区段1097-1178、摘要、目标、CMake input及pending边界均不变，产品源码没有变化。未重新审查其他行的API实验文字，也未把它们当本次PASS依据。

原件：`reviewed-row.json`、`rework-checks.json`、`WP-13-T07.json`、`requirement-design-evidence.log`、复核脚本`recheck.ps1`。

## 真实执行

在隔离冻结检出中执行：

| 命令 | 结果 | 原件 |
| --- | --- | --- |
| `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json` | PASS，exit0 | task.log、task.exit.txt |
| `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-docs.ps1` | PASS，exit0；DeepEvidence NOT RUN | docs.log、docs.exit.txt |
| `pwsh -NoProfile -File RobWork/scripts/industrialrobot/validate-reuse.ps1 -Head 877f6afee38446ab43c5e6a6fd9b09593925aedb -TaskFile RobWork/doc/industrial-robot-design/tasks/REUSE-T01.json` | PASS governance only，exit0 | reuse.log、reuse.exit.txt |

门禁报告：functions=121、closedFiles=0、DEBT=16、UNRESOLVED=1399。三项门禁3 PASS / 0 FAIL；未执行的构建、GUI/产品数值测试、五项API适用性重建及远程CI均为NOT RUN，不计为通过。`status-final.log`为空，`head.txt`为本次冻结提交。

本次仅关闭B4-P2-01返工问题。先前第四批其他登记检查引用前次报告；本提交五项API验证由另一独立验收承接，本报告不替代它。全T01逐功能覆盖、生产复用适用性与迁移结项仍待完成。
