# WP-09-T01 诊断 Schema 与字段完整性

- Task ID：WP-09-T01
- 需求/阶段：ERR-01、UX-03、NFR-REL-05、NFR-MNT-03、NFR-SEC-07；阶段 A / R1
- 架构契约：`architecture/domain-model.md`、`architecture/public-interfaces.md`；模块方案：`module-design/diagnostics.md`
- 前置：WP-03 core、WP-01 构建脚本。
- 允许：修改 `diagnostics/include/.../EngineeringDiagnostic.hpp`、`DiagnosticValue.hpp`、`src/Diagnostic.cpp`、`src/DiagnosticJson.cpp`、`test/DiagnosticSchemaTest.cpp`、`testdata/diagnostics/schema/`。
- 禁止：修改需求、业务错误判断、其他模块公共接口、手工 CSV 或日志输出策略。
- 产出：13 字段不可变诊断、值类型校验和稳定 JSON 往返。

## 数据流

`cause/context -> construct immutable diagnostic -> validate code/category/action/objectId/value -> canonical JSON -> consumer`。缺 runtime name 可空，缺 objectId 不能以名称替代。

## Given/When/Then

- Given缺 code、actual、expected、action 或非法 category，When construct，Then拒绝并返回 Input 诊断。
- Given NaN/Infinity 或名称代替 ID，When serialize，Then失败且不静默转零。
- Given合法 13 字段，When round-trip，Then字段、枚举和关联 ID 完全一致。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostic_schema_test$'
```
证据：字段矩阵、JSON 样例、拒绝诊断和评审签名。提交：`WP-09-T01: implement diagnostic schema`。

停止：字段数量、可空性或错误类别与架构契约不一致时暂停。
