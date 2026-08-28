# WP-07 统一工程策略与碰撞实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立项目级唯一 `EngineeringPolicySet` 和共享碰撞评估器，使运动学、轨迹和优化在同一快照上得到完全一致的对象与结论。

**Architecture:** 策略是项目修订中的不可变值对象，评估器只读取快照策略。RobWork CollisionSetup、ProximitySetup 和路径验证配置只是确定性适配输出，不是独立权威。

**Tech Stack:** C++、RobWork Proximity/PathPlanning、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_policy`、`sdurws_ird_policy_test`。

**创建：**

- `industrialrobot/policy/include/.../EngineeringPolicySet.hpp`
- `industrialrobot/policy/include/.../CollisionPolicy.hpp`
- `industrialrobot/policy/include/.../IEngineeringPolicyProvider.hpp`
- `industrialrobot/policy/include/.../CollisionEvaluator.hpp`
- `industrialrobot/policy/include/.../CollisionPolicyAdapters.hpp`
- `industrialrobot/policy/include/.../PathValidationProfile.hpp`
- `industrialrobot/policy/src/`
- `industrialrobot/policy/test/`

**覆盖需求：** ARC-05，CON-06，KIN-05，TRJ-04，UX-08，NFR-COR-05，NFR-MNT-07，AT-06、19。

## 策略 Schema

```cpp
struct CollisionPolicy {
    CollisionExecutionMode collisionExecutionMode;
    DetectorBackendRef detector;
    ObjectParticipationMap participation;
    std::vector<ObjectPair> excludedPairs;
    std::vector<ObjectPair> allowedContactPairs;
    Length safetyDistance;
    PathValidationProfile pathValidation;
    SchemaVersion schemaVersion;
};
```

`DisabledForDraft` 只允许缺正式碰撞结论的草稿预览；RequiredEvidenceProfile 要求碰撞时必须阻止 Verified。显示碰撞几何和高亮属于 WP-10 会话状态，不进入策略。

## 任务

### Task 1：策略规范化与冲突

- [ ] 先写同一对象对同时 allowed/excluded、未知对象、重复规则、无来源和负安全距离失败测试。
- [ ] 规范化对象对顺序只使用 objectId，不使用显示名称。
- [ ] 实现稳定内容身份和 Schema 往返；插件不能叠加私有默认值。

### Task 2：共享碰撞评估器

- [ ] 用同一状态从三个模拟消费者调用评估器，比较对象 ID 对、判定和原因码。
- [ ] 实现对象参与、排除、允许接触、安全距离和后端不可用语义。
- [ ] excludedPairs 不产生无碰证据；allowedContactPairs 仍检测和记录，但按批准条件不判硬失败。

### Task 3：路径验证协议

- [ ] 实现初始 10 等分、自适应二分、转动 `0.05 rad`、移动 `0.01 m`、余量 `0.005 m` 和最大深度 8。
- [ ] 深度耗尽仍不满足分辨率时返回 Completed + DataInsufficient + Complete。
- [ ] 结论固定为“在本策略与分辨率下未发现碰撞”，不宣称连续安全证明。

### Task 4：RobWork 适配投影

- [ ] 从同一 CollisionPolicy 与 RuntimeNameMap 生成 CollisionSetup、ProximitySetup 和路径配置。
- [ ] 导入两种 XML 时合并为策略草稿并报告冲突，不静默选择一份。
- [ ] 往返测试验证 objectId、策略字段和运行时名称一致。

### Task 5：统一入口和扫描

- [ ] 提供 IEngineeringPolicyProvider，仅按项目修订/快照查询。
- [ ] 边界扫描禁止业务插件出现本地碰撞启用、安全距离、排除规则或不同默认值。
- [ ] 删除旧 `collisionSetup.enabled`、`proximitySetup.enabled` 和分析插件私有碰撞开关链路。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy_test$'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 退出条件

- A-GATE-07 和 AT-19 通过。
- 同一快照跨入口返回完全相同的稳定对象 ID、状态和原因码。
- 纯显示开关不改变项目修订、输入切片、缓存或碰撞结论。
