# WP-11-T03 CSV 公式注入安全写出

- Task ID：WP-11-T03
- 需求/阶段：REQ-05、SEL-01～02、NFR-SEC-01～03；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/secure-io.md`
- 前置：WP-11-T02、WP-09 diagnostics。
- 允许：修改 `io/include/.../CsvWriter.hpp`、`src/CsvWriter.cpp`、`test/CsvWriterTest.cpp`、`testdata/io/csv/writer/`。
- 禁止：破坏数值列类型、在 writer 中执行公式、让业务插件自行导出 CSV。
- 产出：危险文本统一转义、数值保持数值和安全往返策略。

## Given/When/Then

- Given文本以 `=`,`+`,`-`,`@` 开头，When export，Then加统一安全前缀；JSON 证据仍保留原值。
- Given真正数值负数，When export，Then按数值类型写出，不误判为公式文本。
- Given普通文本、引号、换行和 Unicode，When round-trip，Then字段内容可恢复且不执行。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_csv_writer_test$'`。证据：输入/输出 CSV、类型矩阵、脱敏/转义规则和评审。提交：`WP-11-T03: implement formula-safe CSV writer`。

停止：转义规则会改变数值语义或发现旁路 writer 时暂停。
