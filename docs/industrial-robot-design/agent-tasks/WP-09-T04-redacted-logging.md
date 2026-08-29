# WP-09-T04 分级日志与脱敏

- Task ID：WP-09-T04
- 需求/阶段：UX-06、NFR-REL-05、NFR-SEC-07；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/diagnostics.md`
- 前置：WP-09-T01/T02、WP-08 运行身份字段、WP-01 日志脚本。
- 允许：修改 `diagnostics/include/.../ILogSink.hpp`、`LogEvent.hpp`、`LogRedactionPolicy.hpp`、`src/LogSink.cpp`、`src/Redaction.cpp`、`test/RedactedLoggingTest.cpp`。
- 禁止：输出 token/password/连接串、未经允许绝对路径、完整调用栈或崩溃转储内容；不得丢失 objectId。
- 产出：用户/开发日志分级、关联 ID 和幂等脱敏。

## Given/When/Then

- Given Windows 用户目录、UNC、环境变量、令牌、密码和连接串，When redact，Then替换为稳定占位符且不可逆。
- Given开发事件，When log，Then保留 project/branch/revision/run/attempt/snapshot 关联 ID，不写凭据或完整 hash。
- Given用户日志，When render，Then仅有可行动文案、对象显示名和安全相对路径。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_redacted_logging_test$'
```
证据：脱敏前后样本、日志级别矩阵、关联 ID 和崩溃转储引用。提交：`WP-09-T04: implement redacted logging`。

停止：脱敏导致无法定位 objectId，或发现任何凭据泄漏时暂停。
