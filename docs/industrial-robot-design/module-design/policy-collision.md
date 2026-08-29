# 工程策略与碰撞模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-07；阶段/发布：阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`

## 1. 模块职责与目录

模块拥有唯一策略值对象、碰撞对象参与规则、共享评估器、路径采样协议和 RobWork 投影。它不拥有 WorkCell 编译、GUI 高亮、项目事务或评估算法。

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/
  include/sdurws/ird/policy/
    EngineeringPolicySet.hpp CollisionPolicy.hpp
    IEngineeringPolicyProvider.hpp CollisionEvaluator.hpp
    CollisionEvaluation.hpp CollisionPolicyAdapters.hpp
    PathValidationProfile.hpp PolicyDiagnostics.hpp
  src/PolicyNormalizer.cpp CollisionEvaluator.cpp PathValidator.cpp
      RobWorkCollisionAdapter.cpp PolicyProvider.cpp PolicyJson.cpp
  test/PolicyNormalizationTest.cpp CollisionEvaluatorTest.cpp
      PathProtocolTest.cpp RobWorkProjectionTest.cpp PolicyEntryTest.cpp
```

目标：`sdurws_ird_policy`、`sdurws_ird_policy_test`、`sdurws_ird_policy_contract_test`。允许 WP-03、WP-05、WP-06 和 RobWork Proximity；禁止 Qt Widgets、业务插件私有策略和项目 revision 写入。

## 2. 字段契约

`EngineeringPolicySet`：`policyId`、`ownerScopeId`、`schemaVersion`、`sourceRevision`、`collision`、`pathValidation`、`createdAt`。`CollisionPolicy`：`collisionExecutionMode`、`detectorBackend`、`participation[]`、`excludedPairs[]`、`allowedContactPairs[]`、`safetyDistanceM`、`unknownDistanceFallback`。对象对包含双方 ownerScopeId/objectId 并按 ID 排序；安全距离 finite 且非负；所有引用必须存在于同一快照。

策略规范化先验证来源和字段，再排序数组、去除完全重复项、拒绝语义冲突，最后计算稳定 content hash。插件不得补充默认值或覆盖策略字段。

## 3. 碰撞评估逻辑

评估输入是不可变 snapshot、CompiledRobotArtifacts、状态/轨迹和策略。参与状态决定 pair 是否检测；excluded pair 被跳过但不生成无碰证据；allowed contact 仍记录最小距离和接触，但不判硬失败；普通 pair 低于 safetyDistance 判 Infeasible。后端不可用或距离未知按 `unknownDistanceFallback` 返回 DataInsufficient/Failed，禁止默认为安全。

输出 `CollisionEvaluation`：`snapshotId`、`policyHash`、`stateId/pathId`、`pairResults[]`、`minimumDistanceM`、`executionOutcome`、`engineeringStatus`、`payloadCompleteness`、`diagnostics[]`、`conclusionText`。同一输入下 pair、采样点和诊断顺序稳定。

## 4. 路径验证协议

初始将路径分成 10 个等分段；转动关节最大步长 0.05 rad，移动关节最大步长 0.01 m，安全余量 0.005 m，自适应二分最大深度 8。对碰撞或距离不确定段优先二分；深度耗尽仍未满足分辨率返回 `Completed + DataInsufficient + Complete`。结论只能是“在本策略与分辨率下未发现碰撞”，不得声称连续安全证明。

## 5. RobWork 投影

`CollisionPolicyAdapters` 从同一策略和 `RuntimeNameMap` 生成 `CollisionSetup`、`ProximitySetup` 和路径 profile；所有名称通过 resolver 反解。XML 导入结果先转为策略草稿，若 enabled、pair 或距离冲突则返回诊断并要求显式选择；不得静默采用最后读取的一份。

## 6. 测试与证据

夹具覆盖对象对冲突、未知对象、负/NaN 距离、excluded/allowed、三个模拟消费者、一致性、距离 fallback、10 段采样、0.05/0.01 步长、深度 8、XML 冲突、旧私有开关和名称反解。契约测试固定检查策略 hash、状态组合、结论措辞和证据缺口。证据含 snapshot/policy/revision 身份、采样参数、每对距离、后端版本、诊断和独立签名。

## 7. 迁移与评审

旧 `collisionSetup.enabled`、`proximitySetup.enabled` 和插件开关先隔离为只读适配，扫描通过后删除；无法证明旧结果策略来源时标 EvidenceOnly。评审必须确认策略唯一所有权、未知距离不安全、显示状态不进 hash、WORLD/环境对象映射一致和跨入口结果一致。
