# WP-07-T01 策略规范化与冲突

- Task ID：WP-07-T01
- 需求/阶段：ARC-05、CON-06、KIN-05、TRJ-04、UX-08、NFR-COR-05；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/policy-collision.md`
- 前置：WP-03 core、WP-04 revision query、WP-01 构建脚本。
- 允许：修改 `policy/include/.../EngineeringPolicySet.hpp`、`CollisionPolicy.hpp`、`src/PolicyNormalizer.cpp`、`src/PolicyJson.cpp`、`test/PolicyNormalizationTest.cpp`、`testdata/policy/normalization/`。
- 禁止：修改需求、WP-03 枚举、WP-06 名称规则、业务插件默认值、手工 CSV。
- 产出：策略字段校验、对象对规范化、冲突诊断和稳定 policy hash。

## 数据流

`raw policy -> validate source/IDs/units -> canonicalize pair ordering and arrays -> detect duplicates/conflicts -> serialize -> SHA-256`。对象对只用 ownerScopeId/objectId，不用显示名称。

## Given/When/Then

- Given同一对象对同时 excluded/allowed、未知对象、重复规则、负/NaN 距离或缺来源，When normalize，Then拒绝并返回稳定 Input 诊断。
- Given数组顺序不同但语义相同，When normalize，Then policy JSON/hash 相同。
- Given DisabledForDraft/Quick/Verified，When validate，Then仅允许契约规定的证据等级和后端组合。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy_normalization_test$'
```
证据：规范 JSON、冲突矩阵、hash、诊断和评审签名。提交：`WP-07-T01: implement policy normalization`。

停止：策略字段含义或默认值未冻结时暂停，不自行添加默认规则。
