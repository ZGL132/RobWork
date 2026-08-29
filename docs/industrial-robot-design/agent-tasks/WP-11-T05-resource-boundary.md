# WP-11-T05 URDF、网格与 JSON 安全边界

- Task ID：WP-11-T05
- 需求/阶段：REQ-05、SEL-01～02、NFR-COR-03、NFR-SEC-01～03；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`；模块方案：`module-design/secure-io.md`
- 前置：WP-11-T01、WP-04 content object、WP-01 构建脚本。
- 允许：修改 `io/include/.../JsonDocumentReader.hpp`、`ResourceImportService.hpp`、`src/JsonDocumentReader.cpp`、`ResourceImportService.cpp`、`test/ResourceBoundaryTest.cpp`、`testdata/io/resources/`。
- 禁止：URDF 语义转换（归 WP-13）、执行外部实体/网络/命令/宏、加载资源根外文件或修改项目 revision。
- 产出：受预算约束的安全字节读取、外部实体/URL/越界引用拒绝和资源缺失诊断。

## Given/When/Then

- Given DOCTYPE/外部实体、网络 URL、命令宏、符号链接或资源根外引用，When read，Then拒绝并返回稳定诊断。
- Given损坏 JSON/URDF、过深嵌套、非有限坐标或网格复杂度超限，When read，Then返回 Input/System 诊断且旧项目不变。
- Given合法有限资源，When read，Then返回安全字节/路径句柄，业务适配器再进行语义解析。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_resource_boundary_test$'`。证据：恶意样本、预算、路径和诊断日志。提交：`WP-11-T05: enforce URDF mesh JSON boundaries`。

停止：解析库默认启用外部实体或安全层需要承担业务语义时暂停。
