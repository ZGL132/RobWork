# WP-07-T04 RobWork 适配投影

- Task ID：WP-07-T04
- 需求/阶段：ARC-05、CON-06、NFR-COR-05、NFR-MNT-07；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/persistence-schema.md`；模块方案：`module-design/policy-collision.md`
- 前置：WP-07-T01/T02、WP-06 RuntimeNameMap、RobWork 环境。
- 允许：修改 `policy/include/.../CollisionPolicyAdapters.hpp`、`src/RobWorkCollisionAdapter.cpp`、`test/RobWorkProjectionTest.cpp`、`testdata/policy/xml/`。
- 禁止：把 XML 设置变成第二权威、静默选择冲突配置、修改 RuntimeNameMap 或 GUI。
- 产出：从单一策略生成 CollisionSetup、ProximitySetup、路径 profile 的适配器。

## 数据流

`normalized policy + RuntimeNameMap -> resolve object IDs -> create RobWork setups -> import XML as draft -> compare fields -> report conflicts -> round-trip verify`。

## Given/When/Then

- Given合法策略，When project to RobWork，Then setups 的 pair、enabled、distance 和名称可反解且与策略 hash 一致。
- Given两份 XML 在 enabled/pair/distance 冲突，When merge，Then返回冲突诊断和草稿，不静默择一。
- Given未知 runtime name，When project，Then返回 `IRD-POLICY-NAME-UNRESOLVED` 且无正式 setup。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_rw_projection_test$'
```
证据：策略/setup 往返 JSON、冲突清单、名称反解、RobWork 版本和日志。提交：`WP-07-T04: implement RobWork policy projection`。

停止：RobWork 设置无法表达策略或需要增加未批准默认值时暂停。
