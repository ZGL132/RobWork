# WP-11-T02 RFC 4180 CSV 安全读取

- Task ID：WP-11-T02
- 需求/阶段：REQ-05、SEL-01～02、NFR-SEC-01～03、AT-02；阶段 A / R1
- 架构契约：`architecture/testing-contract.md`、`architecture/public-interfaces.md`；模块方案：`module-design/secure-io.md`
- 前置：WP-11-T01、WP-03 units/diagnostics。
- 允许：修改 `io/include/.../CsvReader.hpp`、`src/CsvReader.cpp`、`test/CsvReaderTest.cpp`、`testdata/io/csv/reader/`。
- 禁止：执行公式/宏/命令、自动转零、业务字段映射或修改项目。
- 产出：UTF-8/BOM、RFC 4180 引号/换行、逐行诊断和显式单位转换读取器。

## Given/When/Then

- Given UTF-8/BOM、引号、逗号、CRLF/LF 和内嵌换行，When read，Then行号/字段/原值保持可追溯。
- Given重复表头、空字段、非法单位、NaN/Infinity 或错误引号，When read，Then返回 Input 诊断且不转零。
- Given `=1+1`、`+cmd`、`-2+3`、`@SUM` 文本，When read，Then仅作为文本，不触发执行路径。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_csv_reader_test$'`。证据：原始 CSV、规范记录、行级诊断和哈希。提交：`WP-11-T02: implement safe CSV reader`。

停止：编码策略或字段单位未定义时暂停。
