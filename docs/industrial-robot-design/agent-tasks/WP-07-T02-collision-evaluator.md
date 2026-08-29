# WP-07-T02 共享碰撞评估器

- Task ID：WP-07-T02
- 需求/阶段：ARC-05、CON-06、KIN-05、TRJ-04、NFR-COR-05；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`；模块方案：`module-design/policy-collision.md`
- 前置：WP-07-T01、WP-06 编译工件、WP-05 快照接口。
- 允许：修改 `policy/include/.../CollisionEvaluator.hpp`、`CollisionEvaluation.hpp`、`src/CollisionEvaluator.cpp`、`test/CollisionEvaluatorTest.cpp`、`testdata/policy/pairs/`。
- 禁止：实现第二套碰撞语义、修改 RobWork 编译器、GUI 高亮或项目 revision。
- 产出：统一参与/排除/允许接触/安全距离和后端故障行为。

## 数据流

`snapshot + compiled artifacts + state + normalized policy -> enumerate object pairs -> backend distance/contact -> apply exclusions/allowances/safety threshold -> CollisionEvaluation`。excluded 不产生无碰证据；allowed 记录但不判硬失败。

## Given/When/Then

- Given三个模拟消费者使用同一 snapshot，When evaluate，Then pair IDs、距离、状态和原因码完全一致。
- Given excluded pair，When evaluate，Then不检测硬失败且证据标明 excluded。
- Given allowed contact，When evaluate，Then记录接触和距离但 EngineeringStatus 不因该对失败。
- Given后端不可用或距离未知，When evaluate，Then按 fallback 返回 DataInsufficient/Failed，绝不当作安全。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_collision_evaluator_test$'
```
证据：三入口对照、pair 结果、后端诊断和 snapshot/policy hash。提交：`WP-07-T02: implement shared collision evaluator`。

停止：消费者结果不一致或未知距离需要默认安全时暂停并报告。
