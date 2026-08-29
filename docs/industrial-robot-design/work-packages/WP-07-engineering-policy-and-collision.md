# WP-07 统一工程策略与碰撞实施计划

> 阶段/发布：阶段 A / R1；策略与碰撞公共接口所有者：WP-07。实现者、验证者和评审者必须是不同执行上下文。

**目标：** 建立项目修订中的唯一 `EngineeringPolicySet` 和共享 `CollisionEvaluator`，使运动学、轨迹、优化及三个模拟消费者在同一快照下使用相同对象参与规则、距离分辨率和结论。

## 1. 目标与边界

交付策略规范化、对象对冲突校验、统一碰撞评估器、路径验证协议、RobWork `CollisionSetup`/`ProximitySetup` 投影、统一入口和私有开关扫描。不实现 GUI 高亮、WorkCell 编译、轨迹规划、碰撞后端算法本身或项目事务。

## 2. 需求、契约和发布切片

- 需求：ARC-05、CON-06、KIN-05、TRJ-04、UX-08、NFR-COR-05、NFR-MNT-07、AT-06、AT-19。
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`。
- 模块方案：`module-design/policy-collision.md`。
- 阶段/发布：阶段 A / R1；阶段 B 的静态链路只消费本模块已冻结策略，轨迹/动力学由阶段 C 扩展。

## 3. 文件所有权与依赖

拥有目录：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/`，含 `include/sdurws/ird/policy/`、`src/`、`test/`、`testdata/`、`evidence/`。允许 WP-03 core、WP-06 RuntimeNameMap、RobWork Proximity/PathPlanning 和 Qt Core；禁止 Qt Widgets、业务插件私有碰撞开关、直接写项目 revision、手工 CSV。

目标：`sdurws_ird_policy`、`sdurws_ird_policy_test`、`sdurws_ird_policy_contract_test`。

## 4. 策略字段和规范化

`EngineeringPolicySet` 与 `CollisionPolicy` 字段以需求 §6.7.2 为唯一权威（`collisionExecutionMode: Enabled | DisabledForDraft`、`detectorBackend/version`、`collisionParticipationByObject`、`excludedPairs[]`、`allowedContactPairs[]`、`safetyDistance`、`pathValidationProfile`、`policySchemaVersion`）。安全距离必须 finite 且非负；对象对按 `(ownerScopeId, objectId)` 排序，禁止同时出现在 excluded 和 allowed；未知对象、重复规则和缺来源拒绝。

`DisabledForDraft` 只能用于草稿预览，不得与全局 `EvaluationMode = Quick/Verified` 混用（需求 §6.7.2）；`RequiredEvidenceProfile` 要求碰撞证据时，就绪校验阻止 Verified。显示碰撞几何、高亮和当前选择属于 WP-10 会话状态，不进入策略内容身份。

## 5. 评估和路径验证数据流

```text
Policy + RuntimeNameMap + Canonical/Compiled artifact + state/trajectory
  -> normalize object participation and pair rules
  -> evaluate shared collision backend
  -> sample path (10 equal segments)
  -> recursively bisect violating/uncertain segments
  -> rotational step 0.05 rad, prismatic step 0.01 m, margin 0.005 m, depth <= 8
  -> CollisionEvaluation + diagnostics + evidence
```

`excludedPairs` 不产生“无碰”证据；`allowedContactPairs` 仍检测并记录距离，但按批准条件不判硬失败。距离查询不可用时按 `unknownDistanceFallback` 返回 `DataInsufficient`，不得默认安全。深度耗尽仍未达到分辨率时返回 `Completed + DataInsufficient + Complete`，结论固定为“在本策略与分辨率下未发现碰撞”，不宣称连续安全证明。

## 6. RobWork 投影和一致性

同一策略和名称映射生成 `CollisionSetup`、`ProximitySetup`、路径验证配置；XML 导入只作为输入，冲突合并为策略草稿并返回诊断，不静默择一。投影后逐项反解 objectId、参与状态、排除/允许对、安全距离和运行时名称，与策略规范 JSON 对比。

## 任务

| 任务 | 独立产出 | 任务卡 |
| --- | --- | --- |
| WP-07-T01 | 策略规范化、来源和冲突 | [T01](../agent-tasks/WP-07-T01-policy-normalization.md) |
| WP-07-T02 | 共享碰撞评估器 | [T02](../agent-tasks/WP-07-T02-collision-evaluator.md) |
| WP-07-T03 | 路径采样与分辨率协议 | [T03](../agent-tasks/WP-07-T03-path-protocol.md) |
| WP-07-T04 | RobWork 设置投影和 XML 合并 | [T04](../agent-tasks/WP-07-T04-rw-projection.md) |
| WP-07-T05 | Provider 入口、扫描和旧链路删除 | [T05](../agent-tasks/WP-07-T05-policy-entry.md) |

依赖：T01 → T02 → T03；T04 依赖 T01/T02 和 WP-06；T05 依赖 T01/T04。每张卡一个 worktree、分支和评审提交。

## 7. 失败分类与状态

- 输入错误：非法距离、未知 objectId、重复/冲突对象对、无来源策略；返回 Input，不产生正式证据。
- 工程不可行：碰撞、允许接触条件不满足、分辨率不足或距离未知；返回 Engineering/DataInsufficient，保留完整采样记录。
- 系统错误：后端不可用、RobWork 投影失败、资源/进程故障；返回 System，旧策略和历史结果不变。

## 验证

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_policy_test
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 8. 测试、迁移和证据

测试覆盖策略 JSON 往返、对象对矩阵、三个消费者一致性、距离 fallback、采样边界、深度耗尽、允许接触、XML 冲突、运行时名称反解和私有开关扫描。固定输入/seed/线程时，结论、对象对顺序、采样点和诊断顺序一致。

证据必须含 policy/snapshot/revision 身份、规范策略 hash、采样配置、对象对结果、距离数据、fallback 原因、RobWork 版本、命令日志和独立评审签名。旧 `collisionSetup.enabled`、`proximitySetup.enabled` 链路按 Migratable/Rewrite/EvidenceOnly 留存迁移记录。

## 退出条件

A-GATE-07 与 AT-19 通过；同一快照跨三个入口返回相同对象 ID 对、状态、距离和原因码；不可用距离不被当作安全；显示开关不改变 revision、sliceHash、缓存或碰撞结论；所有私有碰撞开关扫描通过。
