# WP-02 黄金数据与数值验证实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立所有算法、适配器和端到端验收共用的不可变黄金数据与容差框架，使旧代码迁移和新实现正确性可以独立判断。

**Architecture:** `testkit` 提供数据清单、加载器、解析参考值、故障注入和统一断言；业务测试消费它但不得修改黄金数据。算法级容差与外部工具/真实机器人容差严格分层。

**Tech Stack:** C++、CTest、RobWork/RobWorkSim、JSON、CSV、PowerShell。

---

## 范围与所有权

**创建：**

- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/manifest.json`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/models/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/requirements/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/collision/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/dynamics/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/catalog/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/optimization/`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testdata/performance/`

**覆盖需求：** NFR-COR-01～03、NFR-COR-05、AT-01～19、第 15.1～15.3 节。

## 数据集

1. 同一六轴臂的 StandardDH、ExplicitJoint、URDF 等价模型，至少两个非 Z 局部轴。
2. 缺失/零/非单位轴、continuous、prismatic、固定附件、多可动分支、planar/floating、Mimic 样本。
3. DH 转换 `Exact、ExactNonUnique、Approximate、NotRepresentable、AnalysisFailed` 样本。
4. `Arm/ArmA/RobotB`、重复局部名、未知/双前缀和重命名名称样本。
5. 统一碰撞策略、边界安全距离、允许/排除对和显示开关组合。
6. 解析 FK/Jacobian/二连杆动力学、正动力学收敛和传动映射参考值。
7. 可行/不可行器件目录、多段能力曲线和引用错误目录包。
8. 基线、预期改进、硬约束失败和 Pareto 人工小数据集。
9. 5,000 任务、100,000 采样和 10,000 候选性能数据生成规则与固定种子。

## 任务

### Task 1：数据清单和完整性测试

- [ ] 先编写缺文件、哈希不符、重复 ID、未知单位和未记录来源的失败测试。
- [ ] 定义 manifest Schema：数据 ID、版本、用途、单位、来源、生成方法、期望结果和 SHA-256。
- [ ] 实现只读加载器并验证篡改任何样本都会失败。

### Task 2：数学断言库

- [ ] 实现位置、旋转测地角、有向轴、矩阵、相对/绝对误差和稳定集合比较断言。
- [ ] 冻结 `J_norm = [J_v/L*; J_ω]`，4/5 轴使用任务子空间 Jacobian。
- [ ] 将算法级和端到端容差定义为不同类型，编译接口不允许混用。
- [ ] 对朴素 `acos(a·b)` 的近 1 精度退化编写回归样本。

### Task 3：领域黄金数据

- [ ] 建立 AT-01、15～18 对应的模型、转换、命名和 URDF 样本。
- [ ] 建立 AT-02～08、19 对应的需求、碰撞、轨迹、动力和目录样本。
- [ ] 建立 AT-09～14 对应的优化、取消、恢复、崩溃和规模数据生成清单。

### Task 4：迁移判定协议

- [ ] 每个候选旧算法先在独立适配器中运行黄金数据。
- [ ] 结果满足冻结容差且不依赖旧 Widget/私有状态时才标记 `Migratable`。
- [ ] 未通过时标记 `Rewrite`；只有数据或测试夹具可复用时标记 `EvidenceOnly`。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_testkit_test$'
```

## 退出条件

- 第 15.1 节每类数据均有清单、来源、哈希和至少一个使用它的测试。
- 算法级与端到端容差不能在代码中混用。
- 固定种子下黄金结果可重复，非有限数和非法单位明确失败。
- 旧代码迁移均有 `Migratable、Rewrite、EvidenceOnly` 证据结论。
