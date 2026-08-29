# WP-09-T05 静态一致性扫描

- Task ID：WP-09-T05
- 需求/阶段：ERR-01、NFR-MNT-03、NFR-SEC-07；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/diagnostics.md`
- 前置：WP-09-T01/T02、WP-01 边界扫描入口。
- 允许：修改 `diagnostics/test/StaticConsistencyTest.cpp`、`testdata/diagnostics/scan/`、`scripts/industrial-robot/check-diagnostics.ps1`（由 WP-01 调用）。
- 禁止：业务插件硬编码状态词/诊断码/单位文案；不得扩大测试字符串例外。
- 产出：重复 code、硬编码中文词、未登记 action 和错误映射旁路扫描。

## Given/When/Then

- Given业务插件出现硬编码状态词、重复 code 或本地单位转换文案，When scan，Then非零退出并报告文件/行号。
- Given测试期望字符串和诊断目录资源，When scan，Then按明确白名单通过。
- Given mapper 旁路创建诊断，When scan，Then失败并指出调用位置。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_static_consistency_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-diagnostics.ps1
```
证据：扫描报告、白名单、违规夹具和评审签名。提交：`WP-09-T05: enforce diagnostic consistency scan`。

停止：扫描误报无法解释或需要允许业务插件自定义同义 code 时暂停。
