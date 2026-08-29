# WP-06-T02 RuntimeNameMap 双向映射

- Task ID：WP-06-T02
- 需求/阶段：ARC-03、ARC-04、CON-06、MDL-06、MDL-14、NFR-MNT-07；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/runtime-model.md`
- 前置：WP-06-T01、WP-03 ObjectIdentity。
- 允许：修改 `runtime/include/.../RuntimeNameMap.hpp`、`IRuntimeNameResolver.hpp`、`src/RuntimeNameMap.cpp`、`test/NameMapTest.cpp`、`testdata/runtime/names/`。
- 禁止：修改 objectId 规则、各业务插件名称拼接、GUI 文本、历史快照格式和手工 CSV。
- 产出：`resolve(objectId)`/`reverse(scopedName)` 一一对应实现及稳定诊断。

## 数据流

`canonical identities -> validate ownerScopeId/localName -> allocate device name -> build sorted bindings -> resolver index`。内部名称为 `<runtimeDeviceName>.<localName>`；WORLD/环境对象保持全局名。

## Given/When/Then

- Given Arm、ArmA、RobotB 及重复 Joint1/TCP，When build，Then所有 binding 唯一且可逆。
- Given unknown objectId/name、歧义名称、双前缀、旧前缀或去前缀重名，When resolve/reverse，Then返回明确诊断，不取首个匹配。
- Given ownerScopeId 不同但 localName 相同，When resolve，Then允许并保持作用域隔离；同一作用域重复则拒绝。

## 测试、证据与提交

覆盖大小写、非法字符、排序、WORLD 例外和序列化往返。命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_name_map_test$'
```
证据：binding 表、双向逆映射检查、拒绝诊断和黄金夹具哈希。提交：`WP-06-T02: implement deterministic runtime name map`。

停止：名称格式或多机械臂作用域无法由契约确定时暂停。
