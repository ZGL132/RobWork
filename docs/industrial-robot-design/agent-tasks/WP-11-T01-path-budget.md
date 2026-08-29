# WP-11-T01 路径规范化与资源预算

- Task ID：WP-11-T01
- 需求/阶段：REQ-05、SEL-01～02、NFR-REL-04、NFR-SEC-01～03；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`；模块方案：`module-design/secure-io.md`
- 前置：WP-03 core、WP-04 content object、WP-01 构建脚本。
- 允许：修改 `io/include/.../ImportBudget.hpp`、`SafeProjectPath.hpp`、`src/SafeProjectPath.cpp`、`BudgetGuard.cpp`、`test/PathBudgetTest.cpp`、`testdata/io/paths/`。
- 禁止：业务语义解析、项目 revision 写入、绕过预算、修改 requirements 或手工 CSV。
- 产出：路径边界校验、预算预检和超限稳定诊断。

## Given/When/Then

- Given `..`、绝对路径、UNC、反斜杠、大小写变体、符号链接或超长路径，When resolve，Then拒绝并返回 `IRD-IO-PATH-ESCAPE`。
- Given单文件/总量/深度/记录/几何/展开比任一超限，When preflight，Then立即停止且不产出部分记录。
- Given合法项目相对路径和预算，When read，Then返回规范路径、预算消耗和安全句柄。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_path_budget_test$'`。证据：路径矩阵、预算配置、消耗曲线和诊断。提交：`WP-11-T01: implement safe paths and import budgets`。

停止：平台边界无法证明或预算阈值未冻结时暂停。
